// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#include "MasterControl/MasterControlRuntime.h"

#include "ForsettiCore/ActivationStore.h"
#include "ForsettiCore/CapabilityPolicy.h"
#include "ForsettiCore/CompatibilityChecker.h"
#include "ForsettiCore/EntitlementProviders.h"
#include "ForsettiCore/ForsettiContext.h"
#include "ForsettiCore/ForsettiEventBus.h"
#include "ForsettiCore/ForsettiLogger.h"
#include "ForsettiCore/ForsettiRuntime.h"
#include "ForsettiCore/ForsettiServiceContainer.h"
#include "ForsettiCore/ModuleManager.h"
#include "ForsettiCore/StaticModuleRegistry.h"
#include "ForsettiCore/UISurfaceManager.h"
#include "ForsettiPlatform/DefaultPlatformServices.h"
#include "MasterControl/MasterControlDefaults.h"
#include "MasterControl/MasterControlModules.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <Windows.h>
#include <ShlObj.h>    // SHGetFolderPathW + CSIDL_APPDATA — used by findCommandOnPath
                       // to locate npm-global binaries under %APPDATA%\npm.
#include <iphlpapi.h>
#include <urlmon.h>
#include <wincrypt.h>
#include <winhttp.h>
#include <windns.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <limits>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>

namespace MasterControl {

namespace {

// RAII wrapper that guarantees the wrapped std::thread is joined on scope exit
// even when the enclosing function unwinds through an exception. Used around
// child-process pipe-reader threads so that captures of on-stack buffers can
// never outlive the threads writing into them.
struct ScopedThread {
    std::thread t;

    ScopedThread() = default;
    explicit ScopedThread(std::thread&& thread) noexcept : t(std::move(thread)) {}
    ScopedThread(const ScopedThread&) = delete;
    ScopedThread& operator=(const ScopedThread&) = delete;
    ScopedThread(ScopedThread&& other) noexcept : t(std::move(other.t)) {}
    ScopedThread& operator=(ScopedThread&& other) noexcept {
        if (this != &other) {
            if (t.joinable()) {
                t.join();
            }
            t = std::move(other.t);
        }
        return *this;
    }
    ~ScopedThread() {
        if (t.joinable()) {
            t.join();
        }
    }
};

std::wstring wideFromUtf8(const std::string& input) {
    if (input.empty()) {
        return {};
    }

    const int required = MultiByteToWideChar(
        CP_UTF8,
        0,
        input.c_str(),
        static_cast<int>(input.size()),
        nullptr,
        0);

    std::wstring output(static_cast<size_t>(required), L'\0');
    MultiByteToWideChar(
        CP_UTF8,
        0,
        input.c_str(),
        static_cast<int>(input.size()),
        output.data(),
        required);
    return output;
}

std::string utf8FromWide(const std::wstring& input) {
    if (input.empty()) {
        return {};
    }

    const int required = WideCharToMultiByte(
        CP_UTF8,
        0,
        input.c_str(),
        static_cast<int>(input.size()),
        nullptr,
        0,
        nullptr,
        nullptr);

    std::string output(static_cast<size_t>(required), '\0');
    WideCharToMultiByte(
        CP_UTF8,
        0,
        input.c_str(),
        static_cast<int>(input.size()),
        output.data(),
        required,
        nullptr,
        nullptr);
    return output;
}

bool startsWith(const std::string& value, const std::string& prefix) {
    return value.rfind(prefix, 0) == 0;
}

bool endsWith(const std::string& value, const std::string& suffix) {
    if (suffix.size() > value.size()) { return false; }
    return std::equal(suffix.rbegin(), suffix.rend(), value.rbegin());
}

bool startsWithInsensitive(const std::string& value, const std::string& prefix) {
    if (value.size() < prefix.size()) {
        return false;
    }

    for (size_t index = 0; index < prefix.size(); ++index) {
        if (std::tolower(static_cast<unsigned char>(value[index])) !=
            std::tolower(static_cast<unsigned char>(prefix[index]))) {
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Provider identity resolution helpers
// ---------------------------------------------------------------------------
// Resolve a ProviderCapabilityDescriptor by concrete provider identity rather
// than by coarse ProviderKind. The lookup order is:
//   1. Exact match on capability.providerId == providerId
//   2. Prefix match for auto-connect-generated ids (format: {providerId}-YYYYMMDD-HHMMSS)
//   3. Fallback to kind-based match (backward compatibility with pre-existing configs)
// This ensures ChatGPT and Codex (both ProviderKind::Codex) resolve to their
// own distinct capability descriptors.
// ---------------------------------------------------------------------------

template <typename Iterator>
Iterator findCapabilityByProviderId(
    Iterator begin, Iterator end,
    const std::string& providerId, ProviderKind kind) {
    if (!providerId.empty()) {
        // Exact providerId match
        auto exact = std::find_if(begin, end,
            [&providerId](const ProviderCapabilityDescriptor& capability) {
                return capability.providerId == providerId;
            });
        if (exact != end) {
            return exact;
        }
        // Prefix match for auto-connect-generated ids ({providerId}-YYYYMMDD-HHMMSS)
        auto prefix = std::find_if(begin, end,
            [&providerId](const ProviderCapabilityDescriptor& capability) {
                return !capability.providerId.empty()
                    && providerId.size() > capability.providerId.size()
                    && providerId[capability.providerId.size()] == '-'
                    && providerId.compare(0, capability.providerId.size(), capability.providerId) == 0;
            });
        if (prefix != end) {
            return prefix;
        }
    }
    // Fallback: kind-based match for backward compatibility
    return std::find_if(begin, end,
        [kind](const ProviderCapabilityDescriptor& capability) {
            return capability.kind == kind;
        });
}

template <typename Iterator>
Iterator findExecutionRegistrationByProviderId(
    Iterator begin, Iterator end,
    const std::string& providerId, ProviderKind kind) {
    if (!providerId.empty()) {
        auto exact = std::find_if(begin, end,
            [&providerId](const ProviderExecutionRegistration& registration) {
                return registration.providerId == providerId;
            });
        if (exact != end) {
            return exact;
        }
        auto prefix = std::find_if(begin, end,
            [&providerId](const ProviderExecutionRegistration& registration) {
                return !registration.providerId.empty()
                    && providerId.size() > registration.providerId.size()
                    && providerId[registration.providerId.size()] == '-'
                    && providerId.compare(0, registration.providerId.size(), registration.providerId) == 0;
            });
        if (prefix != end) {
            return prefix;
        }
    }
    return std::find_if(begin, end,
        [kind](const ProviderExecutionRegistration& registration) {
            return registration.kind == kind;
        });
}

bool isRemoteSource(const std::string& source) {
    return startsWith(source, "http://") || startsWith(source, "https://");
}

// ---------------------------------------------------------------------------
// WS1 / WS4 / WS6 — Setup helpers
// ---------------------------------------------------------------------------

// Readiness snapshot assembly. Source-neutral: a manually created workflow
// counts as ready exactly as a wizard-instantiated starter template does.
ReadinessSnapshot computeReadinessSnapshot(const DashboardSnapshot& snapshot,
                                           const AppConfiguration& config) {
    ReadinessSnapshot result;
    result.setupStarted = !config.firstRunStartedAtUtc.empty() || config.firstRunCompleted;
    result.firstRunCompleted = config.firstRunCompleted;

    // Providers: credentialed + enabled + not template.
    for (const auto& provider : snapshot.providers) {
        const bool ready = provider.credentialsConfigured && provider.enabled && !provider.isTemplate;
        if (ready) { ++result.providersReadyCount; } else { ++result.providersMissingCount; }
    }

    // MCP servers: online endpoint of the MCPServer kind, not template.
    for (const auto& endpoint : snapshot.endpoints) {
        if (endpoint.kind != EndpointKind::MCPServer) {
            continue;
        }
        if (endpoint.isTemplate) { ++result.mcpMissingCount; continue; }
        if (endpoint.status == EndpointStatus::Online) { ++result.mcpReadyCount; }
        else { ++result.mcpMissingCount; }
    }

    // Specialists: sub-agent endpoints with at least one provider assignment.
    std::set<std::string> assignedSpecialists;
    for (const auto& assignment : snapshot.providerAssignments) {
        if (assignment.kind == ProviderAssignmentTargetKind::SubAgent) {
            assignedSpecialists.insert(assignment.targetId);
        }
    }
    for (const auto& endpoint : snapshot.endpoints) {
        if (endpoint.kind != EndpointKind::SubAgent) {
            continue;
        }
        if (endpoint.isTemplate) { ++result.specialistsMissingCount; continue; }
        if (assignedSpecialists.count(endpoint.id) > 0) { ++result.specialistsReadyCount; }
        else { ++result.specialistsMissingCount; }
    }

    // Workflows: source-neutral. A workflow is valid when it has at least one
    // provider assignment and at least one executable target (specialist or
    // role). Count of ready workflows = count of distinct providers that have
    // at least one assignment to a specialist or role. Missing = 1 if zero
    // valid workflows exist, otherwise 0.
    std::set<std::string> providersWithValidAssignment;
    for (const auto& assignment : snapshot.providerAssignments) {
        if (assignment.providerId.empty()) {
            continue;
        }
        if (assignment.kind == ProviderAssignmentTargetKind::SubAgent
            || assignment.kind == ProviderAssignmentTargetKind::Role
            || assignment.kind == ProviderAssignmentTargetKind::SubAgentGroup) {
            providersWithValidAssignment.insert(assignment.providerId);
        }
    }
    result.workflowsReadyCount = static_cast<int>(providersWithValidAssignment.size());
    result.workflowsMissingCount = (result.workflowsReadyCount == 0) ? 1 : 0;

    // Blocking issues + recommended next step.
    if (result.providersReadyCount == 0) {
        result.blockingIssues.push_back(ReadinessIssue{
            "providers.none-ready", "providers", "blocking",
            "No providers connected",
            "Connect at least one AI provider to get started.",
            "providers", "Connect a provider"
        });
        result.recommendedNextStep = "connect-first-provider";
    } else if (result.mcpReadyCount == 0) {
        result.blockingIssues.push_back(ReadinessIssue{
            "mcp.none-ready", "mcp", "warning",
            "No MCP servers online",
            "Add or bring online at least one MCP server so providers can share tool lanes.",
            "runtime", "Add MCP server"
        });
        result.recommendedNextStep = "add-mcp";
    } else if (result.specialistsReadyCount == 0) {
        result.blockingIssues.push_back(ReadinessIssue{
            "specialists.none-ready", "specialists", "warning",
            "No specialists assigned",
            "Assign at least one specialist/role so providers have something to execute.",
            "providers", "Assign a specialist"
        });
        result.recommendedNextStep = "create-specialist";
    } else if (result.workflowsReadyCount == 0) {
        result.blockingIssues.push_back(ReadinessIssue{
            "workflows.none-ready", "workflows", "warning",
            "No workflows created",
            "Create a starter workflow (or wire up a manual one) to complete setup.",
            "setup-readiness", "Create a workflow"
        });
        result.recommendedNextStep = "create-starter-workflow";
    } else if (!result.firstRunCompleted) {
        result.recommendedNextStep = "review";
    } else {
        result.recommendedNextStep = "complete";
    }

    result.updatedAtUtc = timestampNowUtc();
    return result;
}

// Supported-dependency catalog. Ordered by dependency: nodejs has no
// prerequisite and is the runtime both CLI tools need. Installing nodejs
// first via winget (OpenJS.NodeJS.LTS) puts `node` and `npm` on PATH, which
// unblocks the subsequent claude-code-cli / codex-cli npm installs.
std::vector<SupportedDependency> buildSupportedDependencyCatalog() {
    SupportedDependency nodejs;
    nodejs.id = "nodejs";
    nodejs.displayName = "Node.js (LTS)";
    nodejs.description = "JavaScript runtime + npm. Required by the Claude Code and Codex CLIs.";
    nodejs.detectCommand = "node --version";
    // --scope machine puts node and npm under %ProgramFiles%\nodejs so the
    // service account sees them on PATH for subsequent npm calls. Accept all
    // agreements so the install is fully non-interactive.
    nodejs.installMethod =
        "winget install --id OpenJS.NodeJS.LTS -e --source winget --scope machine "
        "--silent --accept-package-agreements --accept-source-agreements --disable-interactivity";
    nodejs.docsUrl = "https://nodejs.org/";
    nodejs.installTimeoutSeconds = 600;
    // Node.js has no prerequisite — leave prerequisiteProbeCommand empty.

    SupportedDependency claude;
    claude.id = "claude-code-cli";
    claude.displayName = "Claude Code CLI";
    claude.description = "CLI runtime for Claude Code provider execution and account sign-in.";
    claude.detectCommand = "claude --version";
    claude.installMethod = "npm install -g @anthropic-ai/claude-code";
    claude.docsUrl = "https://docs.anthropic.com/claude-code";
    claude.installTimeoutSeconds = 300;
    claude.prerequisiteProbeCommand = "npm --version";
    claude.prerequisiteName = "Node.js (npm)";

    SupportedDependency codex;
    codex.id = "codex-cli";
    codex.displayName = "Codex CLI";
    codex.description = "CLI runtime for OpenAI account sign-in (ChatGPT + Codex providers) and Codex provider execution.";
    codex.detectCommand = "codex --version";
    codex.installMethod = "npm install -g @openai/codex";
    codex.docsUrl = "https://github.com/openai/codex";
    codex.installTimeoutSeconds = 300;
    codex.prerequisiteProbeCommand = "npm --version";
    codex.prerequisiteName = "Node.js (npm)";

    return { nodejs, claude, codex };
}

// Starter workflow template catalog (WS6).
std::vector<StarterWorkflowTemplate> buildStarterWorkflowTemplates() {
    return {
        StarterWorkflowTemplate{
            "single-provider-demo", "Single-Provider Demo",
            "One provider, no MCP, simple task routing.", 1, 0, 0
        },
        StarterWorkflowTemplate{
            "mcp-assisted-demo", "MCP-Assisted Demo",
            "One provider plus one MCP server.", 1, 1, 0
        },
        StarterWorkflowTemplate{
            "specialist-team-demo", "Specialist Team Demo",
            "Two providers with planner + coder specialists.", 2, 0, 2
        }
    };
}

std::string extractHostFromUrl(const std::string& source) {
    const auto schemePosition = source.find("://");
    if (schemePosition == std::string::npos) {
        return {};
    }

    const auto hostStart = schemePosition + 3;
    const auto hostEnd = source.find_first_of("/:", hostStart);
    if (hostEnd == std::string::npos) {
        return source.substr(hostStart);
    }
    return source.substr(hostStart, hostEnd - hostStart);
}

std::string sanitizePathComponent(std::string value) {
    for (char& character : value) {
        if (!(std::isalnum(static_cast<unsigned char>(character)) || character == '-' || character == '_')) {
            character = '_';
        }
    }
    return value;
}

std::string trimCopy(std::string value) {
    value.erase(
        value.begin(),
        std::find_if(
            value.begin(),
            value.end(),
            [](unsigned char character) { return !std::isspace(character); }));
    value.erase(
        std::find_if(
            value.rbegin(),
            value.rend(),
            [](unsigned char character) { return !std::isspace(character); })
            .base(),
        value.end());
    return value;
}

std::wstring quoteWindowsArgument(const std::wstring& argument) {
    if (argument.empty()) {
        return L"\"\"";
    }

    const bool requiresQuotes = argument.find_first_of(L" \t\n\v\"") != std::wstring::npos;
    if (!requiresQuotes) {
        return argument;
    }

    std::wstring quoted = L"\"";
    size_t backslashCount = 0;
    for (const wchar_t character : argument) {
        if (character == L'\\') {
            ++backslashCount;
            continue;
        }
        if (character == L'"') {
            quoted.append(backslashCount * 2 + 1, L'\\');
            quoted.push_back(L'"');
            backslashCount = 0;
            continue;
        }
        if (backslashCount > 0) {
            quoted.append(backslashCount, L'\\');
            backslashCount = 0;
        }
        quoted.push_back(character);
    }
    if (backslashCount > 0) {
        quoted.append(backslashCount * 2, L'\\');
    }
    quoted.push_back(L'"');
    return quoted;
}

std::string quotePosixShellArgument(const std::string& argument) {
    if (argument.empty()) {
        return "''";
    }

    std::string quoted;
    quoted.reserve(argument.size() + 2);
    quoted.push_back('\'');
    for (const char character : argument) {
        if (character == '\'') {
            quoted += "'\\''";
        } else {
            quoted.push_back(character);
        }
    }
    quoted.push_back('\'');
    return quoted;
}

std::wstring quotePowerShellLiteral(const std::wstring& argument) {
    std::wstring quoted;
    quoted.reserve(argument.size() + 2);
    quoted.push_back(L'\'');
    for (const wchar_t character : argument) {
        if (character == L'\'') {
            quoted += L"''";
        } else {
            quoted.push_back(character);
        }
    }
    quoted.push_back(L'\'');
    return quoted;
}

std::wstring joinCommandArguments(const std::vector<std::wstring>& arguments) {
    std::wstring commandLine;
    for (size_t index = 0; index < arguments.size(); ++index) {
        if (index > 0) {
            commandLine.push_back(L' ');
        }
        commandLine += quoteWindowsArgument(arguments[index]);
    }
    return commandLine;
}

std::optional<std::filesystem::path> findCommandOnPath(const std::vector<std::wstring>& fileNames) {
    // 1. Standard SearchPathW which honours the process %PATH%. This misses
    //    binaries installed into the service account's npm-global location,
    //    but catches anything on the real PATH.
    for (const auto& fileName : fileNames) {
        std::array<wchar_t, 4096> buffer{};
        const DWORD length = SearchPathW(nullptr, fileName.c_str(), nullptr, static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
        if (length > 0 && length < buffer.size()) {
            return std::filesystem::path(buffer.data());
        }
    }
    // 2. Explicit well-known npm-global bin directories. When the service
    //    runs as LocalSystem, `npm install -g` drops binaries into
    //    %SystemRoot%\System32\config\systemprofile\AppData\Roaming\npm
    //    which isn't on the process PATH. When it runs as the current user,
    //    %APPDATA%\npm is the target. Also probe the nodejs install dir
    //    in case npm wrote a binary alongside node itself.
    std::vector<std::filesystem::path> fallbackDirs;
    wchar_t systemRoot[MAX_PATH] = {};
    const auto systemRootLen = GetEnvironmentVariableW(L"SystemRoot", systemRoot, MAX_PATH);
    if (systemRootLen > 0 && systemRootLen < MAX_PATH) {
        fallbackDirs.emplace_back(
            std::filesystem::path(systemRoot) / L"System32" / L"config" / L"systemprofile" / L"AppData" / L"Roaming" / L"npm");
    }
    wchar_t appDataBuf[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appDataBuf))) {
        fallbackDirs.emplace_back(std::filesystem::path(appDataBuf) / L"npm");
    }
    fallbackDirs.emplace_back(L"C:\\Program Files\\nodejs");
    for (const auto& fallbackDir : fallbackDirs) {
        for (const auto& fileName : fileNames) {
            std::filesystem::path candidate = fallbackDir / fileName;
            std::error_code ec;
            if (std::filesystem::exists(candidate, ec)) {
                return candidate;
            }
        }
    }
    return std::nullopt;
}

struct ParsedUrl final {
    bool valid = false;
    bool secure = false;
    std::string host;
    INTERNET_PORT port = 0;
    std::wstring pathAndQuery = L"/";
};

ParsedUrl parseUrl(const std::string& url) {
    ParsedUrl parsed;
    URL_COMPONENTSW components{};
    components.dwStructSize = sizeof(components);

    std::wstring wideUrl = wideFromUtf8(url);
    std::wstring host(256, L'\0');
    std::wstring path(2048, L'\0');
    std::wstring extraInfo(2048, L'\0');
    components.lpszHostName = host.data();
    components.dwHostNameLength = static_cast<DWORD>(host.size());
    components.lpszUrlPath = path.data();
    components.dwUrlPathLength = static_cast<DWORD>(path.size());
    components.lpszExtraInfo = extraInfo.data();
    components.dwExtraInfoLength = static_cast<DWORD>(extraInfo.size());

    if (!WinHttpCrackUrl(wideUrl.c_str(), static_cast<DWORD>(wideUrl.size()), 0, &components)) {
        return parsed;
    }

    host.resize(components.dwHostNameLength);
    path.resize(components.dwUrlPathLength);
    extraInfo.resize(components.dwExtraInfoLength);

    parsed.valid = true;
    parsed.secure = components.nScheme == INTERNET_SCHEME_HTTPS;
    parsed.host = utf8FromWide(host);
    parsed.port = components.nPort;
    parsed.pathAndQuery = path.empty() ? L"/" : path;
    if (!extraInfo.empty()) {
        parsed.pathAndQuery += extraInfo;
    }
    return parsed;
}

struct HttpClientResponse final {
    bool succeeded = false;
    int statusCode = 0;
    std::string body;
    std::string errorMessage;
};

HttpClientResponse sendJsonRequest(const std::string& method,
                                   const std::string& url,
                                   const std::vector<std::pair<std::wstring, std::wstring>>& headers,
                                   const std::string& body) {
    HttpClientResponse response;
    const auto parsed = parseUrl(url);
    if (!parsed.valid) {
        response.errorMessage = "Invalid URL.";
        return response;
    }

    HINTERNET session = WinHttpOpen(
        L"MasterControlServiceHost/2.0",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);
    if (session == nullptr) {
        response.errorMessage = "Unable to initialize WinHTTP.";
        return response;
    }

    WinHttpSetTimeouts(session, 3000, 3000, 10000, 30000);

    HINTERNET connection = WinHttpConnect(session, wideFromUtf8(parsed.host).c_str(), parsed.port, 0);
    if (connection == nullptr) {
        response.errorMessage = "Unable to connect to the remote provider.";
        WinHttpCloseHandle(session);
        return response;
    }

    const DWORD requestFlags = parsed.secure ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET request = WinHttpOpenRequest(
        connection,
        wideFromUtf8(method).c_str(),
        parsed.pathAndQuery.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        requestFlags);
    if (request == nullptr) {
        response.errorMessage = "Unable to open the remote provider request.";
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return response;
    }

    std::wstring headerBlock = L"Content-Type: application/json\r\n";
    for (const auto& [name, value] : headers) {
        headerBlock += name;
        headerBlock += L": ";
        headerBlock += value;
        headerBlock += L"\r\n";
    }

    LPVOID optionalData = body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(body.data());
    const DWORD optionalLength = static_cast<DWORD>(body.size());
    if (WinHttpSendRequest(
            request,
            headerBlock.c_str(),
            static_cast<DWORD>(headerBlock.size()),
            optionalData,
            optionalLength,
            optionalLength,
            0) == 0 ||
        WinHttpReceiveResponse(request, nullptr) == 0) {
        response.errorMessage = "The remote provider request failed.";
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return response;
    }

    DWORD statusCode = 0;
    DWORD statusCodeSize = sizeof(statusCode);
    WinHttpQueryHeaders(
        request,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,
        &statusCode,
        &statusCodeSize,
        WINHTTP_NO_HEADER_INDEX);
    response.statusCode = static_cast<int>(statusCode);

    // Cap accumulated response at 32 MiB. Any upstream provider that legitimately
    // needs more than this can be revisited; for now, a malformed or runaway
    // response must not be able to exhaust server memory.
    constexpr size_t kMaxProviderResponseBytes = 32ull * 1024ull * 1024ull;
    std::string responseBody;
    for (;;) {
        DWORD available = 0;
        if (WinHttpQueryDataAvailable(request, &available) == 0 || available == 0) {
            break;
        }

        if (responseBody.size() + static_cast<size_t>(available) > kMaxProviderResponseBytes) {
            response.errorMessage = "Remote provider response exceeded the 32 MiB size limit.";
            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connection);
            WinHttpCloseHandle(session);
            return response;
        }

        std::string chunk(static_cast<size_t>(available), '\0');
        DWORD bytesRead = 0;
        if (WinHttpReadData(request, chunk.data(), available, &bytesRead) == 0) {
            response.errorMessage = "Unable to read the remote provider response.";
            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connection);
            WinHttpCloseHandle(session);
            return response;
        }

        chunk.resize(static_cast<size_t>(bytesRead));
        responseBody += chunk;
    }

    response.succeeded = response.statusCode >= 200 && response.statusCode < 300;
    response.body = std::move(responseBody);
    if (!response.succeeded && response.errorMessage.empty()) {
        response.errorMessage = "Remote provider returned an unsuccessful status code.";
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return response;
}

struct ProcessCaptureResult final {
    bool launched = false;
    int exitCode = -1;
    std::string stdoutText;
    std::string stderrText;
    bool resourcePolicyApplied = false;
    std::string resourcePolicySummary;
};

struct ProcessResourcePolicy final {
    bool enforce = false;
    bool denyLaunch = false;
    DWORD cpuRate = 0;
    SIZE_T processMemoryLimitBytes = 0;
    std::string summary;
    std::string denialMessage;
};

std::optional<std::string> resourceEnvelopeDenialMessage(const ResourceAllocationProfile& profile,
                                                         bool requiresNetwork) {
    if (profile.cpuPercent <= 0) {
        return "Resource policy denied launch because CPU allocation is 0%.";
    }
    if (profile.memoryPercent <= 0) {
        return "Resource policy denied launch because memory allocation is 0%.";
    }
    if (profile.storagePercent <= 0) {
        return "Resource policy denied launch because storage allocation is 0%.";
    }
    if (requiresNetwork && profile.bandwidthPercent <= 0) {
        return "Resource policy denied launch because bandwidth allocation is 0% for network-governed work.";
    }
    return std::nullopt;
}

ProcessResourcePolicy buildProcessResourcePolicy(const std::optional<ResourceAllocationProfile>& profile,
                                                 bool requiresNetwork) {
    ProcessResourcePolicy policy;
    if (!profile.has_value()) {
        return policy;
    }

    if (const auto denialMessage = resourceEnvelopeDenialMessage(*profile, requiresNetwork);
        denialMessage.has_value()) {
        policy.denyLaunch = true;
        policy.denialMessage = *denialMessage;
        return policy;
    }

    MEMORYSTATUSEX memoryStatus{};
    memoryStatus.dwLength = sizeof(memoryStatus);
    if (GlobalMemoryStatusEx(&memoryStatus) == 0 || memoryStatus.ullTotalPhys == 0) {
        policy.denyLaunch = true;
        policy.denialMessage = "Resource policy could not determine total physical memory for process enforcement.";
        return policy;
    }

    policy.enforce = true;
    policy.cpuRate = static_cast<DWORD>((std::clamp)(profile->cpuPercent, 1, 100) * 100);

    const auto memoryLimit = (memoryStatus.ullTotalPhys * static_cast<uint64_t>(profile->memoryPercent)) / 100ULL;
    if (memoryLimit == 0U) {
        policy.denyLaunch = true;
        policy.denialMessage = "Resource policy computed a zero-byte memory ceiling for the process launch.";
        policy.enforce = false;
        return policy;
    }
    policy.processMemoryLimitBytes = static_cast<SIZE_T>(memoryLimit);

    std::ostringstream summary;
    summary << "CPU cap " << profile->cpuPercent
            << "%, memory cap " << profile->memoryPercent
            << "%, storage envelope " << profile->storagePercent << "%";
    if (requiresNetwork) {
        summary << ", bandwidth envelope " << profile->bandwidthPercent << "%";
    }
    summary << '.';
    policy.summary = summary.str();
    return policy;
}

ProcessCaptureResult runProcessCapture(const std::wstring& commandLine,
                                       const std::filesystem::path& workingDirectory,
                                       const std::vector<std::pair<std::wstring, std::wstring>>& environmentOverrides = {},
                                       const std::optional<ResourceAllocationProfile>& resourceAllocationProfile = std::nullopt,
                                       bool requiresNetwork = false) {
    ProcessCaptureResult result;
    const auto resourcePolicy = buildProcessResourcePolicy(resourceAllocationProfile, requiresNetwork);
    result.resourcePolicyApplied = resourcePolicy.enforce;
    result.resourcePolicySummary = resourcePolicy.summary;
    if (resourcePolicy.denyLaunch) {
        result.stderrText = resourcePolicy.denialMessage;
        return result;
    }

    SECURITY_ATTRIBUTES securityAttributes{};
    securityAttributes.nLength = sizeof(securityAttributes);
    securityAttributes.bInheritHandle = TRUE;

    HANDLE stdoutRead = nullptr;
    HANDLE stdoutWrite = nullptr;
    HANDLE stderrRead = nullptr;
    HANDLE stderrWrite = nullptr;
    if (!CreatePipe(&stdoutRead, &stdoutWrite, &securityAttributes, 0) ||
        !SetHandleInformation(stdoutRead, HANDLE_FLAG_INHERIT, 0) ||
        !CreatePipe(&stderrRead, &stderrWrite, &securityAttributes, 0) ||
        !SetHandleInformation(stderrRead, HANDLE_FLAG_INHERIT, 0)) {
        if (stdoutRead) CloseHandle(stdoutRead);
        if (stdoutWrite) CloseHandle(stdoutWrite);
        if (stderrRead) CloseHandle(stderrRead);
        if (stderrWrite) CloseHandle(stderrWrite);
        return result;
    }

    LPWCH environmentBlock = nullptr;
    if (!environmentOverrides.empty()) {
        const LPWCH currentEnvironment = GetEnvironmentStringsW();
        if (currentEnvironment != nullptr) {
            std::map<std::wstring, std::wstring> variables;
            for (const wchar_t* cursor = currentEnvironment; *cursor != L'\0'; cursor += wcslen(cursor) + 1) {
                std::wstring entry(cursor);
                const auto separator = entry.find(L'=');
                if (separator == std::wstring::npos) {
                    continue;
                }
                variables[entry.substr(0, separator)] = entry.substr(separator + 1);
            }
            FreeEnvironmentStringsW(currentEnvironment);

            for (const auto& [name, value] : environmentOverrides) {
                variables[name] = value;
            }

            std::wstring flatEnvironment;
            for (const auto& [name, value] : variables) {
                flatEnvironment += name;
                flatEnvironment += L'=';
                flatEnvironment += value;
                flatEnvironment.push_back(L'\0');
            }
            flatEnvironment.push_back(L'\0');

            environmentBlock = static_cast<LPWCH>(HeapAlloc(GetProcessHeap(), 0, (flatEnvironment.size() + 1) * sizeof(wchar_t)));
            if (environmentBlock != nullptr) {
                memcpy(environmentBlock, flatEnvironment.data(), flatEnvironment.size() * sizeof(wchar_t));
            }
        }
    }

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.dwFlags = STARTF_USESTDHANDLES;
    startupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startupInfo.hStdOutput = stdoutWrite;
    startupInfo.hStdError = stderrWrite;

    PROCESS_INFORMATION processInformation{};
    std::wstring mutableCommandLine = commandLine;
    const std::wstring workingDirectoryWide = workingDirectory.wstring();
    const DWORD creationFlags = CREATE_NO_WINDOW |
        (environmentBlock != nullptr ? CREATE_UNICODE_ENVIRONMENT : 0) |
        (resourcePolicy.enforce ? CREATE_SUSPENDED : 0);
    result.launched = CreateProcessW(
        nullptr,
        mutableCommandLine.data(),
        nullptr,
        nullptr,
        TRUE,
        creationFlags,
        environmentBlock,
        workingDirectoryWide.empty() ? nullptr : workingDirectoryWide.c_str(),
        &startupInfo,
        &processInformation) != 0;

    if (environmentBlock != nullptr) {
        HeapFree(GetProcessHeap(), 0, environmentBlock);
    }
    CloseHandle(stdoutWrite);
    CloseHandle(stderrWrite);

    if (!result.launched) {
        const DWORD lastError = GetLastError();
        wchar_t* messageBuffer = nullptr;
        FormatMessageW(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr, lastError, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            reinterpret_cast<LPWSTR>(&messageBuffer), 0, nullptr);
        result.stderrText = "Failed to launch process (error " + std::to_string(lastError) + "): "
            + (messageBuffer ? utf8FromWide(messageBuffer) : "unknown error");
        if (messageBuffer) {
            LocalFree(messageBuffer);
        }
        CloseHandle(stdoutRead);
        CloseHandle(stderrRead);
        return result;
    }

    HANDLE jobObject = nullptr;
    if (resourcePolicy.enforce) {
        jobObject = CreateJobObjectW(nullptr, nullptr);
        if (jobObject == nullptr) {
            TerminateProcess(processInformation.hProcess, ERROR_NOT_SUPPORTED);
            CloseHandle(stdoutRead);
            CloseHandle(stderrRead);
            CloseHandle(processInformation.hThread);
            CloseHandle(processInformation.hProcess);
            result.launched = false;
            result.stderrText = "Resource policy could not create a Windows job object.";
            return result;
        }

        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limitInformation{};
        limitInformation.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_PROCESS_MEMORY;
        limitInformation.ProcessMemoryLimit = resourcePolicy.processMemoryLimitBytes;
        if (!SetInformationJobObject(
                jobObject,
                JobObjectExtendedLimitInformation,
                &limitInformation,
                sizeof(limitInformation))) {
            TerminateProcess(processInformation.hProcess, ERROR_NOT_SUPPORTED);
            CloseHandle(jobObject);
            CloseHandle(stdoutRead);
            CloseHandle(stderrRead);
            CloseHandle(processInformation.hThread);
            CloseHandle(processInformation.hProcess);
            result.launched = false;
            result.stderrText = "Resource policy could not apply the Windows memory limit.";
            return result;
        }

        JOBOBJECT_CPU_RATE_CONTROL_INFORMATION cpuInformation{};
        cpuInformation.ControlFlags = JOB_OBJECT_CPU_RATE_CONTROL_ENABLE | JOB_OBJECT_CPU_RATE_CONTROL_HARD_CAP;
        cpuInformation.CpuRate = resourcePolicy.cpuRate;
        if (!SetInformationJobObject(
                jobObject,
                JobObjectCpuRateControlInformation,
                &cpuInformation,
                sizeof(cpuInformation))) {
            TerminateProcess(processInformation.hProcess, ERROR_NOT_SUPPORTED);
            CloseHandle(jobObject);
            CloseHandle(stdoutRead);
            CloseHandle(stderrRead);
            CloseHandle(processInformation.hThread);
            CloseHandle(processInformation.hProcess);
            result.launched = false;
            result.stderrText = "Resource policy could not apply the Windows CPU limit.";
            return result;
        }

        if (!AssignProcessToJobObject(jobObject, processInformation.hProcess)) {
            TerminateProcess(processInformation.hProcess, ERROR_NOT_SUPPORTED);
            CloseHandle(jobObject);
            CloseHandle(stdoutRead);
            CloseHandle(stderrRead);
            CloseHandle(processInformation.hThread);
            CloseHandle(processInformation.hProcess);
            result.launched = false;
            result.stderrText = "Resource policy could not attach the process to the Windows job object.";
            return result;
        }

        ResumeThread(processInformation.hThread);
    }

    // -----------------------------------------------------------------------
    // Hardened pipe draining + timeout enforcement
    // -----------------------------------------------------------------------
    // The canonical execution sequence is:
    //   1. Launch reader threads (they block on pipe data)
    //   2. WaitForSingleObject(hProcess, timeout) — if timeout kills process
    //   3. Join reader threads (pipe broken by process exit → quick return)
    //   4. Read exit code, close handles
    // This avoids the classic Windows pipe deadlock where sequential reads
    // block if the child fills one pipe buffer before the other is drained.
    // -----------------------------------------------------------------------

    static constexpr size_t kMaxCaptureBytes = 4 * 1024 * 1024; // 4 MB per stream

    auto readPipeBounded = [](HANDLE handle) -> std::string {
        std::string captured;
        char buffer[4096];
        DWORD bytesRead = 0;
        bool truncated = false;
        while (ReadFile(handle, buffer, sizeof(buffer), &bytesRead, nullptr) && bytesRead > 0) {
            if (!truncated && captured.size() + bytesRead > kMaxCaptureBytes) {
                const size_t remaining = kMaxCaptureBytes - captured.size();
                if (remaining > 0) {
                    captured.append(buffer, static_cast<size_t>(remaining));
                }
                truncated = true;
            }
            if (truncated) {
                // Continue draining the pipe so the child process is not blocked.
                continue;
            }
            captured.append(buffer, buffer + bytesRead);
        }
        if (truncated) {
            captured.insert(0, "[output truncated: exceeded 4 MB capture limit — first 4 MB preserved]\n");
        }
        return captured;
    };

    // Step 1: Launch concurrent reader threads under RAII scope guards so that
    // any exception between here and the join below cannot leave the threads
    // writing into the stdoutCapture/stderrCapture locals after they unwind.
    std::string stdoutCapture, stderrCapture;
    ScopedThread stdoutThread(std::thread([&stdoutCapture, stdoutRead, &readPipeBounded]() {
        stdoutCapture = readPipeBounded(stdoutRead);
    }));
    ScopedThread stderrThread(std::thread([&stderrCapture, stderrRead, &readPipeBounded]() {
        stderrCapture = readPipeBounded(stderrRead);
    }));

    // Step 2: Wait for process exit with timeout
    const DWORD timeoutMs = resourcePolicy.enforce ? 300000 : 300000; // 5-minute default
    const DWORD waitResult = WaitForSingleObject(processInformation.hProcess, timeoutMs);
    if (waitResult == WAIT_TIMEOUT) {
        // Kill the process (tree if job object exists, single process otherwise).
        // Killing the process breaks the pipes, which unblocks the reader threads.
        if (jobObject != nullptr) {
            TerminateJobObject(jobObject, ERROR_TIMEOUT);
        } else {
            // Note: TerminateProcess does NOT kill child processes when no job object
            // is present. Child processes become orphaned in this case.
            TerminateProcess(processInformation.hProcess, ERROR_TIMEOUT);
        }
        result.stderrText += "\n[Process terminated: execution exceeded "
            + std::to_string(timeoutMs / 1000) + "s timeout]";
    }

    // Step 3: Join reader threads (safe — pipes are broken by process exit/kill).
    // The ScopedThread destructors would join on unwind as well; joining explicitly
    // here ensures the captured strings are complete before we move them below.
    stdoutThread.t.join();
    stderrThread.t.join();
    result.stdoutText = std::move(stdoutCapture);
    if (!stderrCapture.empty()) {
        if (!result.stderrText.empty()) {
            result.stderrText += "\n";
        }
        result.stderrText += stderrCapture;
    }

    // Step 4: Read exit code and clean up handles
    DWORD exitCode = 0;
    if (GetExitCodeProcess(processInformation.hProcess, &exitCode) != 0) {
        result.exitCode = static_cast<int>(exitCode);
    }

    CloseHandle(stdoutRead);
    CloseHandle(stderrRead);
    if (jobObject != nullptr) {
        CloseHandle(jobObject);
    }
    CloseHandle(processInformation.hThread);
    CloseHandle(processInformation.hProcess);
    return result;
}

std::string buildEndpointUrl(const RuntimeEndpoint& endpoint) {
    const std::string scheme = endpoint.protocol.empty() ? "http" : endpoint.protocol;
    const std::string routePath = endpoint.routePath.empty()
        ? std::string("/")
        : (endpoint.routePath.front() == '/' ? endpoint.routePath : "/" + endpoint.routePath);
    return scheme + "://" + endpoint.host + ":" + std::to_string(endpoint.port) + routePath;
}

std::string generateExecutionId() {
    static std::atomic<uint64_t> counter{ 1 };
    const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return "exec-" + std::to_string(stamp) + "-" + std::to_string(counter.fetch_add(1));
}

nlohmann::json readJsonFile(const std::filesystem::path& filePath) {
    std::ifstream stream(filePath);
    if (!stream.is_open()) {
        return nlohmann::json{};
    }

    try {
        nlohmann::json json;
        stream >> json;
        return json;
    } catch (const nlohmann::json::exception&) {
        return nlohmann::json{};
    } catch (const std::ios_base::failure&) {
        return nlohmann::json{};
    }
}

[[nodiscard]] bool writeJsonFile(const std::filesystem::path& filePath, const nlohmann::json& json) noexcept {
    try {
        std::error_code ec;
        if (filePath.has_parent_path()) {
            std::filesystem::create_directories(filePath.parent_path(), ec);
            if (ec) {
                return false;
            }
        }

        auto tempPath = filePath;
        tempPath += L".tmp";

        {
            std::ofstream stream(tempPath, std::ios::trunc | std::ios::binary);
            if (!stream.is_open()) {
                return false;
            }
            stream << json.dump(2);
            stream.flush();
            if (!stream.good()) {
                std::filesystem::remove(tempPath, ec);
                return false;
            }
        }

        std::filesystem::rename(tempPath, filePath, ec);
        if (ec) {
            std::filesystem::copy_file(
                tempPath,
                filePath,
                std::filesystem::copy_options::overwrite_existing,
                ec);
            std::error_code removeEc;
            std::filesystem::remove(tempPath, removeEc);
            if (ec) {
                return false;
            }
        }

        return true;
    } catch (const std::exception&) {
        return false;
    } catch (...) {
        return false;
    }
}

// Non-throwing JSON helpers. Callers that must not propagate
// nlohmann::json::exception to network or UI surfaces should route through
// these rather than calling .at()/.get<T>() directly.

std::optional<nlohmann::json> tryParseJson(std::string_view text) noexcept {
    try {
        return nlohmann::json::parse(text);
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    } catch (...) {
        return std::nullopt;
    }
}

template <typename T>
std::optional<T> tryGet(const nlohmann::json& json, std::string_view key) noexcept {
    try {
        const auto iterator = json.find(key);
        if (iterator == json.end() || iterator->is_null()) {
            return std::nullopt;
        }
        return iterator->get<T>();
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    } catch (...) {
        return std::nullopt;
    }
}

template <typename T>
T getOr(const nlohmann::json& json, std::string_view key, T fallback) noexcept {
    auto value = tryGet<T>(json, key);
    return value.has_value() ? std::move(*value) : std::move(fallback);
}

std::string readTextFile(const std::filesystem::path& filePath) {
    std::ifstream stream(filePath, std::ios::binary);
    if (!stream.is_open()) {
        return {};
    }

    std::ostringstream output;
    output << stream.rdbuf();
    return output.str();
}

std::string encodeBase64(const std::string& bytes) {
    if (bytes.empty()) {
        return {};
    }

    DWORD required = 0;
    CryptBinaryToStringA(
        reinterpret_cast<const BYTE*>(bytes.data()),
        static_cast<DWORD>(bytes.size()),
        CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
        nullptr,
        &required);

    std::string encoded(static_cast<size_t>(required), '\0');
    CryptBinaryToStringA(
        reinterpret_cast<const BYTE*>(bytes.data()),
        static_cast<DWORD>(bytes.size()),
        CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
        encoded.data(),
        &required);

    if (!encoded.empty() && encoded.back() == '\0') {
        encoded.pop_back();
    }
    return encoded;
}

std::string decodeBase64(const std::string& text) {
    if (text.empty()) {
        return {};
    }

    DWORD required = 0;
    CryptStringToBinaryA(
        text.c_str(),
        static_cast<DWORD>(text.size()),
        CRYPT_STRING_BASE64,
        nullptr,
        &required,
        nullptr,
        nullptr);

    std::string bytes(static_cast<size_t>(required), '\0');
    CryptStringToBinaryA(
        text.c_str(),
        static_cast<DWORD>(text.size()),
        CRYPT_STRING_BASE64,
        reinterpret_cast<BYTE*>(bytes.data()),
        &required,
        nullptr,
        nullptr);
    return bytes;
}

std::string protectSecretPayload(const std::string& plainText) {
    DATA_BLOB inputBlob{};
    inputBlob.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(plainText.data()));
    inputBlob.cbData = static_cast<DWORD>(plainText.size());

    DATA_BLOB outputBlob{};
    if (CryptProtectData(&inputBlob, L"MasterControlProviderCredentials", nullptr, nullptr, nullptr, 0, &outputBlob) == 0) {
        throw std::runtime_error("Unable to protect provider credential payload.");
    }

    std::string protectedBytes(reinterpret_cast<const char*>(outputBlob.pbData), outputBlob.cbData);
    LocalFree(outputBlob.pbData);
    return encodeBase64(protectedBytes);
}

std::string unprotectSecretPayload(const std::string& protectedText) {
    if (protectedText.empty()) {
        return {};
    }

    const auto protectedBytes = decodeBase64(protectedText);
    DATA_BLOB inputBlob{};
    inputBlob.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(protectedBytes.data()));
    inputBlob.cbData = static_cast<DWORD>(protectedBytes.size());

    DATA_BLOB outputBlob{};
    if (CryptUnprotectData(&inputBlob, nullptr, nullptr, nullptr, nullptr, 0, &outputBlob) == 0) {
        throw std::runtime_error("Unable to unprotect provider credential payload.");
    }

    std::string plainText(reinterpret_cast<const char*>(outputBlob.pbData), outputBlob.cbData);
    LocalFree(outputBlob.pbData);
    return plainText;
}

std::string lowercase(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return value;
}

bool isBlank(const std::string& value) {
    return std::all_of(
        value.begin(),
        value.end(),
        [](const unsigned char character) { return std::isspace(character) != 0; });
}

bool pathIsWithinRoot(const std::filesystem::path& candidate, const std::filesystem::path& root) {
    const auto normalizedCandidate = lowercase(std::filesystem::weakly_canonical(candidate).generic_string());
    auto normalizedRoot = lowercase(std::filesystem::weakly_canonical(root).generic_string());
    if (!normalizedRoot.empty() && normalizedRoot.back() != '/') {
        normalizedRoot.push_back('/');
    }

    return normalizedCandidate == normalizedRoot.substr(0, normalizedRoot.size() - 1) ||
        startsWith(normalizedCandidate, normalizedRoot);
}

class ScopedWinsock final {
public:
    ScopedWinsock() {
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            throw std::runtime_error("WSAStartup failed");
        }
    }

    ~ScopedWinsock() {
        WSACleanup();
    }
};

struct SharedState final {
    mutable std::mutex mutex;
    AppConfiguration configuration;
    std::vector<InstallProvenance> installHistory;
    std::vector<ProviderExecutionRecord> providerExecutionHistory;
};

std::string platformKey(const PlatformTarget platform) {
    switch (platform) {
        case PlatformTarget::Windows: return "windows";
        case PlatformTarget::MacOS: return "macos";
        case PlatformTarget::IOS: return "ios";
        case PlatformTarget::Unknown: break;
    }
    return "unknown";
}

std::string dotLocalHostName(std::string hostName) {
    hostName = trimCopy(std::move(hostName));
    if (hostName.empty()) {
        return "localhost.local";
    }
    if (hostName.find('.') == std::string::npos) {
        hostName += ".local";
    }
    return hostName;
}

std::string platformGatewayConfigPath(const PlatformTarget platform) {
    return "/api/platform-services/config/" + platformKey(platform);
}

std::string platformGatewayRoutePath(const PlatformTarget platform) {
    return "/mcp/gateway/" + platformKey(platform);
}

std::string governanceRoutePath(const PlatformTarget platform) {
    return "/mcp/governance/" + platformKey(platform);
}

PlatformTarget platformFromKey(const std::string& value) {
    if (startsWithInsensitive(value, "windows")) {
        return PlatformTarget::Windows;
    }
    if (startsWithInsensitive(value, "macos") || startsWithInsensitive(value, "mac")) {
        return PlatformTarget::MacOS;
    }
    if (startsWithInsensitive(value, "ios")) {
        return PlatformTarget::IOS;
    }
    return PlatformTarget::Unknown;
}

EndpointStatus endpointStatusFromServiceState(const std::string& value) {
    if (value == "online" || value == "advertised") {
        return EndpointStatus::Online;
    }
    if (value == "configured" || value == "disabled") {
        return EndpointStatus::Degraded;
    }
    if (value == "offline") {
        return EndpointStatus::Offline;
    }
    return EndpointStatus::Unknown;
}

struct BootstrapManifestContract final {
    std::string version = "1.0.0";
    std::string bootstrapScript;
    std::string bootstrapArguments;
    std::vector<RuntimeEndpoint> seededEndpoints;
    std::vector<ProviderConnection> providers;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    BootstrapManifestContract,
    version,
    bootstrapScript,
    bootstrapArguments,
    seededEndpoints,
    providers)

struct ValidatedBootstrapManifest final {
    BootstrapManifestContract contract;
    std::filesystem::path bootstrapPath;
};

struct EntitlementStateDocument final {
    std::vector<std::string> unlockedModuleIDs;
    std::vector<std::string> unlockedProductIDs;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    EntitlementStateDocument,
    unlockedModuleIDs,
    unlockedProductIDs)

struct ProviderCredentialsDocument final {
    std::map<std::string, std::string> protectedPayloadsByProviderId;
    std::map<std::string, std::string> updatedAtUtcByProviderId;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    ProviderCredentialsDocument,
    protectedPayloadsByProviderId,
    updatedAtUtcByProviderId)

EntitlementStateDocument buildDefaultEntitlementStateDocument() {
    return EntitlementStateDocument{
        {
            "com.mastercontrol.environment-discovery",
            "com.mastercontrol.host-telemetry",
            "com.mastercontrol.runtime-inventory",
            "com.mastercontrol.configuration",
            "com.mastercontrol.provider-codex",
            "com.mastercontrol.provider-claude-code",
            "com.mastercontrol.provider-xai",
            "com.mastercontrol.dashboard-ui"
        },
        {
            "mastercontrol.iap.installer-import",
            "mastercontrol.iap.provider-integration",
            "mastercontrol.iap.export",
            "mastercontrol.iap.command-logic-unit",
            "mastercontrol.iap.gateway-windows",
            "mastercontrol.iap.gateway-macos",
            "mastercontrol.iap.gateway-ios",
            "mastercontrol.iap.governance-windows",
            "mastercontrol.iap.governance-macos",
            "mastercontrol.iap.governance-ios",
            "mastercontrol.iap.beacon-gateway"
        }
    };
}

const std::set<std::string>& protectedForsettiModuleIds() {
    static const std::set<std::string> moduleIds = {
        "com.mastercontrol.configuration",
        "com.mastercontrol.runtime-inventory",
        "com.mastercontrol.provider-integration",
        "com.mastercontrol.command-logic-unit",
        "com.mastercontrol.dashboard-ui"
    };
    return moduleIds;
}

bool isProtectedForsettiModule(const std::string& moduleId) {
    return protectedForsettiModuleIds().find(moduleId) != protectedForsettiModuleIds().end();
}

GovernanceProfile buildFallbackGovernanceProfile() {
    return GovernanceProfile{
        "Command Logic Unit",
        "Governance is not assumed. CLU keeps the local MCP command plane inside declared operator intent and surfaces runtime drift before it becomes invisible.",
        {
            GovernanceDocument{
                "clu-constitution",
                "CLU Constitution",
                "constitution",
                "Defines the baseline rules for agentic operation inside Master Control Orchestration Server.",
                "Contract before action. Scope is binding. Truthfulness is mandatory. Governance overrides convenience."
            }
        },
        {
            GovernanceRole{
                "architect",
                "Architect",
                "planning",
                { "Define allowed scope", "Classify changes", "Establish delivery intent" },
                { "Expand scope without approval" },
                { "Task contract", "Scope definition" }
            },
            GovernanceRole{
                "builder",
                "Builder",
                "execution",
                { "Implement approved changes", "Report runtime outcomes" },
                { "Hide failures", "Edit unrelated surfaces" },
                { "Artifact changes", "Change summary" }
            },
            GovernanceRole{
                "validator",
                "Validator",
                "verification",
                { "Review compliance posture", "Block incomplete delivery" },
                { "Approve blocked findings" },
                { "Validation status", "Compliance report" }
            }
        },
        {
            GovernanceRule{
                "CLU-C001",
                "No meaningful autonomous action without declared scope",
                "critical",
                "block_operation",
                "Agent autonomy should remain disabled unless the operator has explicitly enabled it for the dashboard."
            },
            GovernanceRule{
                "CLU-C002",
                "No unsafe open-LAN posture without explicit operator intent",
                "critical",
                "block_operation",
                "Security protocols should not be disabled while the browser surface remains open on the LAN."
            },
            GovernanceRule{
                "CLU-C003",
                "Troubleshooting bypass must remain visible and temporary",
                "high",
                "warn_operator",
                "Troubleshooting bypass is allowed, but it should remain visible until normal protections are restored."
            }
        },
        {
            "Confirm the security posture before enabling AI autonomy.",
            "Review trusted hosts before allowing unattended remote imports."
        }
    };
}

class JsonActivationStore final : public Forsetti::IActivationStore {
public:
    explicit JsonActivationStore(std::filesystem::path filePath)
        : filePath_(std::move(filePath)) {}

    Forsetti::ActivationState loadState() const override {
        if (!std::filesystem::exists(filePath_)) {
            return {};
        }

        return readJsonFile(filePath_).get<Forsetti::ActivationState>();
    }

    void saveState(const Forsetti::ActivationState& state) override {
        (void)writeJsonFile(filePath_, state);
    }

private:
    std::filesystem::path filePath_;
};

class FileBackedEntitlementProvider final : public Forsetti::IEntitlementProvider {
public:
    FileBackedEntitlementProvider(std::filesystem::path filePath,
                                  EntitlementStateDocument defaultState)
        : filePath_(std::move(filePath))
        , defaultState_(std::move(defaultState)) {
        ensureStateFile();
        refreshEntitlements();
        watcher_ = std::thread([this]() { watchForChanges(); });
    }

    ~FileBackedEntitlementProvider() override {
        stopRequested_.store(true, std::memory_order_release);
        if (watcher_.joinable()) {
            watcher_.join();
        }
    }

    bool isUnlocked(const std::string& moduleIDOrProductID) const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return unlockedModuleIDs_.find(moduleIDOrProductID) != unlockedModuleIDs_.end() ||
            unlockedProductIDs_.find(moduleIDOrProductID) != unlockedProductIDs_.end();
    }

    void refreshEntitlements() override {
        ensureStateFile();
        const auto stateJson = readJsonFile(filePath_);
        const auto state = stateJson.empty()
            ? defaultState_
            : stateJson.get<EntitlementStateDocument>();

        std::set<std::string> nextModuleIDs(
            state.unlockedModuleIDs.begin(),
            state.unlockedModuleIDs.end());
        std::set<std::string> nextProductIDs(
            state.unlockedProductIDs.begin(),
            state.unlockedProductIDs.end());

        std::vector<std::function<void()>> callbacks;
        bool changed = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            changed = nextModuleIDs != unlockedModuleIDs_ ||
                nextProductIDs != unlockedProductIDs_;
            unlockedModuleIDs_ = std::move(nextModuleIDs);
            unlockedProductIDs_ = std::move(nextProductIDs);
            if (changed) {
                callbacks = callbacks_;
            }
        }

        for (const auto& callback : callbacks) {
            callback();
        }
    }

    void onEntitlementsChanged(std::function<void()> callback) override {
        std::lock_guard<std::mutex> lock(mutex_);
        callbacks_.push_back(std::move(callback));
    }

    void restorePurchases() override {
        refreshEntitlements();
    }

    [[nodiscard]] EntitlementStateDocument currentStateDocument() const {
        std::lock_guard<std::mutex> lock(mutex_);
        EntitlementStateDocument state;
        state.unlockedModuleIDs.assign(unlockedModuleIDs_.begin(), unlockedModuleIDs_.end());
        state.unlockedProductIDs.assign(unlockedProductIDs_.begin(), unlockedProductIDs_.end());
        return state;
    }

    void setModuleUnlocked(const std::string& moduleId, const bool unlocked) {
        ensureStateFile();
        auto stateJson = readJsonFile(filePath_);
        auto state = stateJson.empty()
            ? defaultState_
            : stateJson.get<EntitlementStateDocument>();

        auto& moduleIds = state.unlockedModuleIDs;
        moduleIds.erase(
            std::remove(moduleIds.begin(), moduleIds.end(), moduleId),
            moduleIds.end());
        if (unlocked) {
            moduleIds.push_back(moduleId);
        }

        std::sort(moduleIds.begin(), moduleIds.end());
        moduleIds.erase(std::unique(moduleIds.begin(), moduleIds.end()), moduleIds.end());
        (void)writeJsonFile(filePath_, state);
        refreshEntitlements();
    }

private:
    void ensureStateFile() const {
        if (!std::filesystem::exists(filePath_)) {
            (void)writeJsonFile(filePath_, defaultState_);
        }
    }

    std::filesystem::file_time_type lastWriteTime() const {
        std::error_code errorCode;
        if (!std::filesystem::exists(filePath_, errorCode)) {
            return {};
        }

        const auto value = std::filesystem::last_write_time(filePath_, errorCode);
        if (errorCode) {
            return {};
        }
        return value;
    }

    void watchForChanges() {
        auto knownWriteTime = lastWriteTime();
        while (!stopRequested_.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(750));
            const auto currentWriteTime = lastWriteTime();
            if (currentWriteTime != knownWriteTime) {
                knownWriteTime = currentWriteTime;
                refreshEntitlements();
            }
        }
    }

    std::filesystem::path filePath_;
    EntitlementStateDocument defaultState_;
    mutable std::mutex mutex_;
    std::set<std::string> unlockedModuleIDs_;
    std::set<std::string> unlockedProductIDs_;
    std::vector<std::function<void()>> callbacks_;
    std::atomic<bool> stopRequested_{ false };
    std::thread watcher_;
};

class FileBackedConfigurationService final : public IConfigurationService {
public:
    FileBackedConfigurationService(std::shared_ptr<SharedState> state, std::filesystem::path filePath)
        : state_(std::move(state))
        , filePath_(std::move(filePath)) {
        // Unified ingress path: readJsonFile now returns {} on missing/malformed
        // input, and the .get<AppConfiguration>() call is guarded against parser
        // and type-mismatch exceptions so a corrupt configuration.json falls
        // back to defaults rather than crashing startup.
        bool loaded = false;
        const auto json = readJsonFile(filePath_);
        if (!json.is_null() && !json.empty()) {
            try {
                state_->configuration = json.get<AppConfiguration>();
                loaded = true;
            } catch (const nlohmann::json::exception&) {
                loaded = false;
            } catch (...) {
                loaded = false;
            }
        }

        if (!loaded) {
            state_->configuration = buildDefaultConfiguration();
            (void)persistLocked();
        }
    }

    AppConfiguration current() const override {
        std::lock_guard<std::mutex> lock(state_->mutex);
        return state_->configuration;
    }

    OperationResult update(const AppConfiguration& configuration,
                           bool confirmUnsafeChanges) override {
        if (!configuration.security.securityProtocolsEnabled && !confirmUnsafeChanges) {
            return OperationResult{
                false,
                true,
                "Disabling security protocols requires explicit confirmation."
            };
        }

        bool persisted = false;
        {
            std::lock_guard<std::mutex> lock(state_->mutex);
            state_->configuration = configuration;
            persisted = persistLocked();
        }

        if (!persisted) {
            return OperationResult{ false, false, "Configuration could not be written to disk." };
        }
        return OperationResult{ true, false, "Configuration updated." };
    }

private:
    bool persistLocked() const noexcept {
        return writeJsonFile(filePath_, state_->configuration);
    }

    std::shared_ptr<SharedState> state_;
    std::filesystem::path filePath_;
};

class ResourceAllocationService final : public IResourceAllocationService {
public:
    ResourceAllocationService(std::shared_ptr<SharedState> state, std::filesystem::path filePath)
        : state_(std::move(state))
        , filePath_(std::move(filePath)) {}

    ResourceAllocationProfile current() const override {
        std::lock_guard<std::mutex> lock(state_->mutex);
        return state_->configuration.resourceAllocation;
    }

    OperationResult update(const ResourceAllocationProfile& profile) override {
        const auto isValidPercent = [](const int value) {
            return value >= 0 && value <= 100;
        };

        if (!isValidPercent(profile.cpuPercent) ||
            !isValidPercent(profile.memoryPercent) ||
            !isValidPercent(profile.bandwidthPercent) ||
            !isValidPercent(profile.storagePercent)) {
            return OperationResult{ false, false, "Resource allocation values must be between 0 and 100." };
        }

        std::lock_guard<std::mutex> lock(state_->mutex);
        state_->configuration.resourceAllocation = profile;
        if (!writeJsonFile(filePath_, state_->configuration)) {
            return OperationResult{ false, false, "Resource allocation could not be written to disk." };
        }
        return OperationResult{ true, false, "Resource allocation updated." };
    }

private:
    std::shared_ptr<SharedState> state_;
    std::filesystem::path filePath_;
};

class WindowsHostTelemetryService final : public ITelemetryService {
public:
    HostTelemetrySnapshot captureSnapshot() override;

private:
    struct NetworkSample final {
        std::chrono::steady_clock::time_point timestamp;
        uint64_t bytesSent = 0;
        uint64_t bytesReceived = 0;
    };

    static std::string readComputerName();
    double readCpuPercent();
    void readPrimaryNetworkIdentity(std::string& ipAddress,
                                    std::string& macAddress,
                                    uint64_t& bytesSentPerSecond,
                                    uint64_t& bytesReceivedPerSecond);

    std::mutex mutex_;
    ULONGLONG lastIdle_ = 0;
    ULONGLONG lastTotal_ = 0;
    std::optional<NetworkSample> lastNetworkSample_;
};

HostTelemetrySnapshot WindowsHostTelemetryService::captureSnapshot() {
    HostTelemetrySnapshot snapshot;
    snapshot.hostName = readComputerName();
    snapshot.operatingSystem = "Windows";
    snapshot.capturedAtUtc = timestampNowUtc();

    snapshot.cpuPercent = readCpuPercent();

    MEMORYSTATUSEX memoryStatus{};
    memoryStatus.dwLength = sizeof(memoryStatus);
    if (GlobalMemoryStatusEx(&memoryStatus) != 0) {
        snapshot.memoryPercent = static_cast<double>(memoryStatus.dwMemoryLoad);
        snapshot.totalMemoryBytes = memoryStatus.ullTotalPhys;
        snapshot.freeMemoryBytes = memoryStatus.ullAvailPhys;
    }

    ULARGE_INTEGER freeBytesAvailable{};
    ULARGE_INTEGER totalBytes{};
    ULARGE_INTEGER totalFreeBytes{};
    if (GetDiskFreeSpaceExW(L"C:\\", &freeBytesAvailable, &totalBytes, &totalFreeBytes) != 0) {
        snapshot.totalDiskBytes = totalBytes.QuadPart;
        snapshot.freeDiskBytes = totalFreeBytes.QuadPart;
        if (snapshot.totalDiskBytes != 0U) {
            const auto usedBytes = snapshot.totalDiskBytes - snapshot.freeDiskBytes;
            snapshot.diskPercent = (static_cast<double>(usedBytes) / static_cast<double>(snapshot.totalDiskBytes)) * 100.0;
        }
    }

    readPrimaryNetworkIdentity(snapshot.primaryIpAddress, snapshot.primaryMacAddress, snapshot.bytesSentPerSecond, snapshot.bytesReceivedPerSecond);
    return snapshot;
}

std::string WindowsHostTelemetryService::readComputerName() {
    char buffer[MAX_COMPUTERNAME_LENGTH + 1]{};
    DWORD size = MAX_COMPUTERNAME_LENGTH + 1;
    if (GetComputerNameA(buffer, &size) == 0) {
        return "MASTER-CONTROL";
    }
    return std::string(buffer, size);
}

double WindowsHostTelemetryService::readCpuPercent() {
    FILETIME idleTime{};
    FILETIME kernelTime{};
    FILETIME userTime{};
    if (GetSystemTimes(&idleTime, &kernelTime, &userTime) == 0) {
        return 0.0;
    }

    const ULONGLONG idle = (static_cast<ULONGLONG>(idleTime.dwHighDateTime) << 32) | idleTime.dwLowDateTime;
    const ULONGLONG kernel = (static_cast<ULONGLONG>(kernelTime.dwHighDateTime) << 32) | kernelTime.dwLowDateTime;
    const ULONGLONG user = (static_cast<ULONGLONG>(userTime.dwHighDateTime) << 32) | userTime.dwLowDateTime;

    std::lock_guard<std::mutex> lock(mutex_);
    if (lastTotal_ == 0U) {
        lastIdle_ = idle;
        lastTotal_ = kernel + user;
        return 0.0;
    }

    const ULONGLONG total = kernel + user;
    const ULONGLONG idleDelta = idle - lastIdle_;
    const ULONGLONG totalDelta = total - lastTotal_;
    lastIdle_ = idle;
    lastTotal_ = total;

    if (totalDelta == 0U) {
        return 0.0;
    }

    return static_cast<double>(totalDelta - idleDelta) * 100.0 / static_cast<double>(totalDelta);
}

void WindowsHostTelemetryService::readPrimaryNetworkIdentity(std::string& ipAddress,
                                                             std::string& macAddress,
                                                             uint64_t& bytesSentPerSecond,
                                                             uint64_t& bytesReceivedPerSecond) {
    ULONG outBufferLength = 16 * 1024;
    std::vector<unsigned char> buffer(outBufferLength);
    IP_ADAPTER_ADDRESSES* addresses = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
    DWORD result = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, nullptr, addresses, &outBufferLength);
    if (result == ERROR_BUFFER_OVERFLOW) {
        buffer.resize(outBufferLength);
        addresses = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
        result = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, nullptr, addresses, &outBufferLength);
    }

    if (result != NO_ERROR) {
        return;
    }

    for (auto* adapter = addresses; adapter != nullptr; adapter = adapter->Next) {
        if (adapter->OperStatus != IfOperStatusUp || adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK) {
            continue;
        }

        for (auto* unicast = adapter->FirstUnicastAddress; unicast != nullptr; unicast = unicast->Next) {
            char host[NI_MAXHOST]{};
            if (getnameinfo(
                    unicast->Address.lpSockaddr,
                    static_cast<socklen_t>(unicast->Address.iSockaddrLength),
                    host,
                    NI_MAXHOST,
                    nullptr,
                    0,
                    NI_NUMERICHOST) == 0) {
                ipAddress = host;
                break;
            }
        }

        if (adapter->PhysicalAddressLength > 0U) {
            std::ostringstream macStream;
            for (ULONG index = 0; index < adapter->PhysicalAddressLength; ++index) {
                if (index > 0U) {
                    macStream << ':';
                }
                macStream << std::hex << std::uppercase << static_cast<int>(adapter->PhysicalAddress[index]);
            }
            macAddress = macStream.str();
        }

        MIB_IF_ROW2 row{};
        row.InterfaceIndex = adapter->IfIndex;
        if (GetIfEntry2(&row) == NO_ERROR) {
            const auto now = std::chrono::steady_clock::now();
            const uint64_t sent = row.OutOctets;
            const uint64_t received = row.InOctets;

            std::lock_guard<std::mutex> lock(mutex_);
            if (lastNetworkSample_.has_value()) {
                const auto elapsedMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastNetworkSample_->timestamp).count();
                if (elapsedMilliseconds > 0) {
                    const auto elapsedSeconds = static_cast<double>(elapsedMilliseconds) / 1000.0;
                    bytesSentPerSecond = static_cast<uint64_t>(static_cast<double>(sent - lastNetworkSample_->bytesSent) / elapsedSeconds);
                    bytesReceivedPerSecond = static_cast<uint64_t>(static_cast<double>(received - lastNetworkSample_->bytesReceived) / elapsedSeconds);
                }
            }
            lastNetworkSample_ = NetworkSample{ now, sent, received };
        }
        break;
    }
}

class RuntimeInventoryService final : public IRuntimeInventoryService {
public:
    explicit RuntimeInventoryService(std::shared_ptr<SharedState> state)
        : state_(std::move(state)) {
        refresh();
    }

    std::vector<RuntimeEndpoint> listEndpoints() override {
        std::lock_guard<std::mutex> lock(mutex_);
        return endpoints_;
    }

    void refresh() override;

    // Kicks refresh() off on a detached background thread so the calling
    // admin API handler returns immediately. The service instance lives
    // for the entire process lifetime (owned by MasterControlApplication::Impl)
    // so the detached thread's `this` pointer is stable until shutdown.
    // Under load, a pending flag prevents stacking redundant refresh
    // threads — the second caller just coalesces onto the in-flight one.
    void refreshAsync() override {
        bool expected = false;
        if (!refreshPending_.compare_exchange_strong(expected, true)) {
            return; // a refresh is already in flight
        }
        std::thread([this]() {
            try {
                refresh();
            } catch (...) {
                // swallow — detached background work must never terminate()
            }
            refreshPending_.store(false);
        }).detach();
    }

private:
    static EndpointStatus probeEndpoint(const std::string& host, uint16_t port);
    static bool sameEndpointConfiguration(const std::vector<RuntimeEndpoint>& left,
                                          const std::vector<RuntimeEndpoint>& right);

    std::shared_ptr<SharedState> state_;
    std::mutex mutex_;
    std::vector<RuntimeEndpoint> endpoints_;
    std::chrono::steady_clock::time_point lastRefreshAt_{};
    std::chrono::seconds refreshInterval_{ 15 };
    std::atomic_bool refreshPending_{ false };
};

void RuntimeInventoryService::refresh() {
    std::vector<RuntimeEndpoint> endpoints;
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        endpoints = state_->configuration.activeProfile.seededEndpoints;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto now = std::chrono::steady_clock::now();
        if (!endpoints_.empty() && lastRefreshAt_.time_since_epoch().count() != 0 &&
            now - lastRefreshAt_ < refreshInterval_ &&
            sameEndpointConfiguration(endpoints_, endpoints)) {
            return;
        }
    }

    for (auto& endpoint : endpoints) {
        endpoint.lastCheckedUtc = timestampNowUtc();
        if (endpoint.port == 0 || endpoint.host.empty() || endpoint.protocol == "virtual") {
            endpoint.status = EndpointStatus::Unknown;
            continue;
        }

        endpoint.status = probeEndpoint(endpoint.host, endpoint.port);
    }

    std::lock_guard<std::mutex> lock(mutex_);
    endpoints_ = std::move(endpoints);
    lastRefreshAt_ = std::chrono::steady_clock::now();
}

bool RuntimeInventoryService::sameEndpointConfiguration(const std::vector<RuntimeEndpoint>& left,
                                                        const std::vector<RuntimeEndpoint>& right) {
    if (left.size() != right.size()) {
        return false;
    }

    for (size_t index = 0; index < left.size(); ++index) {
        if (left[index].id != right[index].id ||
            left[index].host != right[index].host ||
            left[index].port != right[index].port ||
            left[index].protocol != right[index].protocol ||
            left[index].routePath != right[index].routePath ||
            left[index].kind != right[index].kind ||
            left[index].userDefined != right[index].userDefined ||
            left[index].specialization != right[index].specialization) {
            return false;
        }
    }
    return true;
}

EndpointStatus RuntimeInventoryService::probeEndpoint(const std::string& host, const uint16_t port) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* results = nullptr;
    const auto portText = std::to_string(port);
    if (getaddrinfo(host.c_str(), portText.c_str(), &hints, &results) != 0) {
        return EndpointStatus::Offline;
    }

    EndpointStatus status = EndpointStatus::Offline;
    for (addrinfo* result = results; result != nullptr; result = result->ai_next) {
        SOCKET socketHandle = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
        if (socketHandle == INVALID_SOCKET) {
            continue;
        }

        u_long nonBlocking = 1;
        ioctlsocket(socketHandle, FIONBIO, &nonBlocking);
        const int connectResult = connect(socketHandle, result->ai_addr, static_cast<int>(result->ai_addrlen));
        if (connectResult == 0) {
            status = EndpointStatus::Online;
            closesocket(socketHandle);
            break;
        }

        fd_set writeSet;
        FD_ZERO(&writeSet);
        FD_SET(socketHandle, &writeSet);
        timeval timeout{};
        timeout.tv_sec = 0;
        timeout.tv_usec = 400000;

        if (select(0, nullptr, &writeSet, nullptr, &timeout) > 0) {
            int socketError = 0;
            int socketErrorLength = sizeof(socketError);
            getsockopt(socketHandle, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&socketError), &socketErrorLength);
            status = (socketError == 0) ? EndpointStatus::Online : EndpointStatus::Offline;
        }

        closesocket(socketHandle);
        if (status == EndpointStatus::Online) {
            break;
        }
    }

    freeaddrinfo(results);
    return status;
}

class PackageTrustEvaluator final : public IPackageTrustEvaluator {
public:
    explicit PackageTrustEvaluator(std::shared_ptr<SharedState> state)
        : state_(std::move(state)) {}

    PackageTrustDecision evaluate(const std::string& source,
                                  bool allowUntrustedExecution) const override {
        if (!isRemoteSource(source)) {
            return PackageTrustDecision{ true, false, "Local package source." };
        }

        const auto host = extractHostFromUrl(source);
        {
            std::lock_guard<std::mutex> lock(state_->mutex);
            const auto& trustedHosts = state_->configuration.security.trustedRemoteHosts;
            const auto trusted = std::find(trustedHosts.begin(), trustedHosts.end(), host) != trustedHosts.end();
            if (trusted) {
                return PackageTrustDecision{ true, false, "Host is allowlisted for unattended execution." };
            }
        }

        if (allowUntrustedExecution) {
            return PackageTrustDecision{ false, false, "Explicit approval granted for an untrusted remote source." };
        }

        return PackageTrustDecision{ false, true, "Remote host is not trusted for unattended execution." };
    }

private:
    std::shared_ptr<SharedState> state_;
};

class ProviderCatalogService final : public IProviderCatalogService {
public:
    std::vector<ProviderCapabilityDescriptor> listCapabilities() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<ProviderCapabilityDescriptor> capabilities;
        capabilities.reserve(capabilitiesById_.size());
        for (const auto& [providerId, capability] : capabilitiesById_) {
            capabilities.push_back(capability);
        }

        std::sort(
            capabilities.begin(),
            capabilities.end(),
            [](const ProviderCapabilityDescriptor& left, const ProviderCapabilityDescriptor& right) {
                return std::tie(left.displayName, left.providerId) < std::tie(right.displayName, right.providerId);
            });
        return capabilities;
    }

    void upsertCapability(const ProviderCapabilityDescriptor& capability) override {
        std::lock_guard<std::mutex> lock(mutex_);
        capabilitiesById_[capability.providerId] = capability;
    }

    void removeCapability(const std::string& providerId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        capabilitiesById_.erase(providerId);
    }

private:
    mutable std::mutex mutex_;
    std::map<std::string, ProviderCapabilityDescriptor> capabilitiesById_;
};

class ProviderRegistryService final : public IProviderRegistry {
public:
    ProviderRegistryService(std::shared_ptr<SharedState> state,
                            std::filesystem::path configurationFile,
                            std::shared_ptr<IProviderCatalogService> providerCatalogService)
        : state_(std::move(state))
        , configurationFile_(std::move(configurationFile))
        , providerCatalogService_(std::move(providerCatalogService)) {}

    // Late-bound dependencies for the Auto-Connect flow. These services are
    // created after ProviderRegistryService (credentialStore takes a shared_ptr
    // to this registry in its constructor) so we cannot take them upfront.
    // Injected from the application composition root via weak_ptr to avoid
    // circular ownership.
    void wireAutoConnectDependencies(
        std::weak_ptr<IProviderCredentialStore> credentialStore,
        std::weak_ptr<IProviderAssignmentService> assignmentService) {
        credentialStore_ = std::move(credentialStore);
        assignmentService_ = std::move(assignmentService);
    }

    std::vector<ProviderConnection> listProviders() const override {
        std::lock_guard<std::mutex> lock(state_->mutex);
        return state_->configuration.providers;
    }

    OperationResult upsertProvider(const ProviderConnection& provider) override {
        if (provider.id.empty() || isBlank(provider.id)) {
            return OperationResult{ false, false, "Provider ID is required." };
        }
        if (provider.displayName.empty() || isBlank(provider.displayName)) {
            return OperationResult{ false, false, "Provider display name is required." };
        }
        if (provider.baseUrl.empty() || isBlank(provider.baseUrl)) {
            return OperationResult{ false, false, "Provider base URL is required." };
        }

        std::lock_guard<std::mutex> lock(state_->mutex);
        const auto capabilities = providerCatalogService_->listCapabilities();
        const auto capabilityIterator = findCapabilityByProviderId(
            capabilities.begin(), capabilities.end(), provider.id, provider.kind);
        if (capabilityIterator == capabilities.end()) {
            return OperationResult{ false, false, "The selected provider kind is not currently supported by an active provider module." };
        }

        auto& providers = state_->configuration.providers;
        ProviderConnection normalizedProvider = provider;
        normalizedProvider.isTemplate = false; // User-saved providers are no longer templates
        if (normalizedProvider.modelId.empty()) {
            normalizedProvider.modelId = capabilityIterator->recommendedModel;
        }
        const auto iterator = std::find_if(
            providers.begin(),
            providers.end(),
            [&provider](const ProviderConnection& candidate) { return candidate.id == provider.id; });

        if (iterator == providers.end()) {
            providers.push_back(normalizedProvider);
        } else {
            normalizedProvider.credentialsConfigured = iterator->credentialsConfigured;
            *iterator = normalizedProvider;
        }

        if (!writeJsonFile(configurationFile_, state_->configuration)) {
            return OperationResult{ false, false, "Provider settings could not be written to disk." };
        }
        return OperationResult{ true, false, "Provider settings updated." };
    }

    AutoConnectResult autoConnectProvider(const AutoConnectRequest& request) override {
        AutoConnectResult result;
        const auto totalStart = std::chrono::steady_clock::now();

        auto recordStep = [&result](const std::string& stage,
                                    bool succeeded,
                                    const std::string& message,
                                    int latencyMs = 0) {
            AutoConnectStep step;
            step.stage = stage;
            step.succeeded = succeeded;
            step.message = message;
            step.latencyMs = latencyMs;
            result.steps.push_back(std::move(step));
        };

        // -- Stage 1: Resolve capability ---------------------------------------
        const auto capabilities = providerCatalogService_->listCapabilities();
        const auto capabilityIterator = findCapabilityByProviderId(
            capabilities.begin(), capabilities.end(), request.providerId, request.kind);
        if (capabilityIterator == capabilities.end()) {
            result.errorMessage = "The selected provider kind has no active provider module.";
            recordStep("resolve-capability", false, result.errorMessage);
            result.summary = result.errorMessage;
            return result;
        }
        const auto& capability = *capabilityIterator;
        recordStep("resolve-capability", true,
                   "Matched '" + capability.displayName + "' module (" + capability.providerId + ")");

        // -- Stage 2: Derive connection shape ----------------------------------
        ProviderConnection connection;
        connection.kind = request.kind;
        connection.displayName = request.displayNameOverride.empty()
            ? capability.displayName
            : request.displayNameOverride;
        connection.baseUrl = request.baseUrlOverride.empty()
            ? capability.defaultBaseUrl
            : request.baseUrlOverride;
        connection.modelId = request.modelIdOverride.empty()
            ? capability.recommendedModel
            : request.modelIdOverride;
        connection.enabled = true;
        connection.allowAutonomousControl = request.allowAutonomousControl
            && capability.supportsAutonomousControl;

        // Unique provider id: {providerId}-YYYYMMDD-HHMMSS — scoped under the
        // canonical module providerId so multiple connections for the same
        // kind remain disambiguated even across clock ticks.
        connection.id = generateAutoConnectProviderId(capability.providerId);
        result.providerId = connection.id;
        result.displayName = connection.displayName;
        result.baseUrl = connection.baseUrl;
        recordStep("derive-shape", true,
                   "Generated id '" + connection.id + "' targeting " + connection.baseUrl);

        // -- Stage 3: Validate required credential fields ----------------------
        std::vector<std::string> missingFields;
        for (const auto& descriptor : capability.credentialFields) {
            if (!descriptor.required) {
                continue;
            }
            const auto iterator = request.credentials.find(descriptor.fieldId);
            if (iterator == request.credentials.end() || iterator->second.empty()) {
                missingFields.push_back(descriptor.label.empty() ? descriptor.fieldId : descriptor.label);
            }
        }
        if (!missingFields.empty()) {
            std::string joined;
            for (const auto& field : missingFields) {
                if (!joined.empty()) joined += ", ";
                joined += field;
            }
            result.errorMessage = "Missing required credential fields: " + joined;
            recordStep("validate-credentials", false, result.errorMessage);
            result.summary = result.errorMessage;
            return result;
        }
        recordStep("validate-credentials", true, "All required credential fields supplied");

        // -- Stage 4: Connectivity + auth probe + model discovery --------------
        if (request.discoverModels) {
            const auto probeStart = std::chrono::steady_clock::now();
            const auto discoveryResult = discoverRemoteModels(connection.baseUrl, request.credentials);
            const auto probeLatency = static_cast<int>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - probeStart).count());
            if (discoveryResult.succeeded) {
                result.discoveredModels = discoveryResult.models;
                std::string message = "Discovered " + std::to_string(result.discoveredModels.size())
                    + " model(s) from " + connection.baseUrl;
                recordStep("discover-models", true, message, probeLatency);

                // Prefer capability.recommendedModel if the remote actually
                // reports it; otherwise fall back to the first listed model.
                if (!capability.recommendedModel.empty()) {
                    const auto matchIt = std::find_if(
                        result.discoveredModels.begin(),
                        result.discoveredModels.end(),
                        [&capability](const DiscoveredModel& model) {
                            return model.id == capability.recommendedModel;
                        });
                    if (matchIt != result.discoveredModels.end()) {
                        connection.modelId = capability.recommendedModel;
                        recordStep("select-model", true,
                                   "Selected recommended model '" + connection.modelId + "'");
                    } else if (!result.discoveredModels.empty()) {
                        connection.modelId = result.discoveredModels.front().id;
                        recordStep("select-model", true,
                                   "Recommended model not exposed by remote; falling back to '"
                                   + connection.modelId + "'");
                    }
                } else if (!result.discoveredModels.empty() && connection.modelId.empty()) {
                    connection.modelId = result.discoveredModels.front().id;
                    recordStep("select-model", true,
                               "No capability recommendation; selected '" + connection.modelId + "'");
                }
            } else {
                // Non-fatal: providers that do not implement a /models route
                // (e.g. Claude Code CLI, local inference servers) should still
                // install cleanly. We keep capability.recommendedModel and
                // defer credential validation to the first execution.
                recordStep("discover-models", false,
                           "Remote models endpoint not reachable: " + discoveryResult.errorMessage
                           + " (falling back to capability recommendation)",
                           probeLatency);
            }
        } else {
            recordStep("discover-models", true, "Model discovery skipped by request");
        }
        result.selectedModelId = connection.modelId;

        // -- Stage 5: Register provider ----------------------------------------
        const auto upsertResult = upsertProvider(connection);
        if (!upsertResult.succeeded) {
            result.errorMessage = "Provider registration failed: " + upsertResult.message;
            recordStep("register-provider", false, result.errorMessage);
            result.summary = result.errorMessage;
            return result;
        }
        recordStep("register-provider", true, "Provider registered in configuration");

        // Track overall success across stages 6-7. Previously we hard-coded
        // result.succeeded = true at finalize even when stage 6 or 7 had
        // recorded a failure step — so an unrouted credential store or a
        // rejected assignment came back as a green success to the caller.
        bool postRegistrationSucceeded = true;

        // -- Stage 6: Persist credentials via DPAPI -----------------------------
        if (auto store = credentialStore_.lock()) {
            ProviderCredentialUpdate update;
            update.providerId = connection.id;
            update.values = request.credentials;
            const auto credentialResult = store->upsertCredentials(update);
            if (!credentialResult.succeeded) {
                result.errorMessage = "Credential storage failed: " + credentialResult.message;
                recordStep("store-credentials", false, result.errorMessage);
                // Roll back the provider registration so we do not leave an
                // unusable record behind.
                removeProviderInternal(connection.id);
                result.summary = result.errorMessage;
                return result;
            }
            recordStep("store-credentials", true, "Credentials encrypted with DPAPI and stored");
        } else {
            // Credential store not wired: credentials were NOT persisted.
            // This is a runtime-wiring failure but we don't roll back the
            // provider registration — the operator can re-enter credentials
            // via the direct API. Surface it as a failed overall result so
            // the caller knows credentials are missing.
            recordStep("store-credentials", false,
                       "Credential store unavailable (runtime wiring incomplete)");
            result.errorMessage = "Credential store unavailable; credentials were not persisted.";
            postRegistrationSucceeded = false;
        }

        // -- Stage 7: Apply requested role assignments -------------------------
        if (auto assignmentService = assignmentService_.lock()) {
            const auto targets = assignmentService->listTargets();
            for (const auto& targetId : request.assignmentTargetIds) {
                const auto targetIt = std::find_if(
                    targets.begin(), targets.end(),
                    [&targetId](const ProviderAssignmentTarget& candidate) {
                        return candidate.targetId == targetId;
                    });
                if (targetIt == targets.end()) {
                    result.assignmentsFailed.push_back(targetId + " (unknown target)");
                    continue;
                }

                ProviderAssignment assignment;
                assignment.targetId = targetIt->targetId;
                assignment.kind = targetIt->kind;
                assignment.providerId = connection.id;
                const auto opResult = assignmentService->upsertAssignment(assignment);
                if (opResult.succeeded) {
                    result.assignmentsApplied.push_back(targetIt->displayName);
                } else {
                    result.assignmentsFailed.push_back(targetIt->displayName + ": " + opResult.message);
                }
            }

            std::string summary = "Applied " + std::to_string(result.assignmentsApplied.size())
                + " role assignment(s)";
            if (!result.assignmentsFailed.empty()) {
                summary += ", " + std::to_string(result.assignmentsFailed.size()) + " failed";
            }
            const bool allAssignmentsApplied = result.assignmentsFailed.empty();
            recordStep("apply-assignments", allAssignmentsApplied, summary);
            if (!allAssignmentsApplied) {
                postRegistrationSucceeded = false;
                if (result.errorMessage.empty()) {
                    result.errorMessage = std::to_string(result.assignmentsFailed.size())
                        + " role assignment(s) failed: " + result.assignmentsFailed.front();
                }
            }
        } else if (!request.assignmentTargetIds.empty()) {
            recordStep("apply-assignments", false,
                       "Assignment service unavailable (runtime wiring incomplete)");
            for (const auto& targetId : request.assignmentTargetIds) {
                result.assignmentsFailed.push_back(targetId + " (assignment service unavailable)");
            }
            if (result.errorMessage.empty()) {
                result.errorMessage = "Assignment service unavailable; requested role assignments were not applied.";
            }
            postRegistrationSucceeded = false;
        }

        // -- Finalize ----------------------------------------------------------
        result.totalLatencyMs = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - totalStart).count());
        result.succeeded = postRegistrationSucceeded;
        if (!postRegistrationSucceeded) {
            if (result.summary.empty()) {
                result.summary = result.errorMessage.empty()
                    ? "Auto-connect completed with one or more failures."
                    : result.errorMessage;
            }
            return result;
        }
        result.summary = "Connected '" + connection.displayName + "' in "
            + std::to_string(result.totalLatencyMs) + "ms ("
            + std::to_string(result.discoveredModels.size()) + " model(s), "
            + std::to_string(result.assignmentsApplied.size()) + " role(s))";
        return result;
    }

private:
    struct ModelDiscoveryResult final {
        bool succeeded = false;
        std::vector<DiscoveredModel> models;
        std::string errorMessage;
    };

    // Generates a unique provider id using the canonical module providerId as
    // a prefix and a local timestamp suffix — stable enough to survive a bulk
    // rebuild and unique across typical manual creation rates.
    static std::string generateAutoConnectProviderId(const std::string& modulePrefix) {
        std::string prefix = modulePrefix.empty() ? std::string("provider") : modulePrefix;
        std::transform(prefix.begin(), prefix.end(), prefix.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

        SYSTEMTIME localTime{};
        GetLocalTime(&localTime);
        char suffix[32]{};
        std::snprintf(
            suffix, sizeof(suffix), "-%04u%02u%02u-%02u%02u%02u",
            localTime.wYear, localTime.wMonth, localTime.wDay,
            localTime.wHour, localTime.wMinute, localTime.wSecond);
        return prefix + suffix;
    }

    // Issues an OpenAI-compatible `GET {baseUrl}/models` request using the
    // already-established WinHTTP sendJsonRequest helper. The call is used
    // for three things at once: connectivity check, credential validation,
    // and model discovery. Providers that do not implement this route
    // (Claude Code CLI, local servers) fall back to capability.recommendedModel.
    ModelDiscoveryResult discoverRemoteModels(const std::string& baseUrl,
                                              const std::map<std::string, std::string>& credentials) {
        ModelDiscoveryResult discovery;
        if (baseUrl.empty()) {
            discovery.errorMessage = "Base URL not set";
            return discovery;
        }

        std::string trimmedBase = baseUrl;
        while (!trimmedBase.empty() && trimmedBase.back() == '/') {
            trimmedBase.pop_back();
        }
        const std::string modelsUrl = trimmedBase + "/models";

        // Build authentication headers from whatever credential fields the
        // user supplied. Rather than hard-coding a short list of field ids,
        // we walk every credential the user provided and pick the first one
        // whose id looks like an API key / auth token (the convention used
        // by every provider module in the tree). This keeps the pipeline
        // module-agnostic — a new module can declare any field id and the
        // auto-connect probe still Just Works.
        std::vector<std::pair<std::wstring, std::wstring>> headers;
        auto normalizedLower = [](std::string input) {
            std::transform(input.begin(), input.end(), input.begin(),
                           [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            return input;
        };
        std::string bearer;
        std::string customAuth;
        for (const auto& [fieldId, value] : credentials) {
            if (value.empty()) continue;
            const auto lower = normalizedLower(fieldId);
            if (lower == "authorization") {
                customAuth = value;
                continue;
            }
            const bool looksLikeKey =
                lower.find("api_key") != std::string::npos ||
                lower.find("apikey") != std::string::npos ||
                lower.find("api-key") != std::string::npos ||
                lower.find("token") != std::string::npos ||
                lower.find("secret") != std::string::npos ||
                lower == "key";
            if (looksLikeKey && bearer.empty()) {
                bearer = value;
            }
        }
        if (!bearer.empty()) {
            headers.emplace_back(L"Authorization", L"Bearer " + wideFromUtf8(bearer));
        }
        if (!customAuth.empty()) {
            headers.emplace_back(L"Authorization", wideFromUtf8(customAuth));
        }

        const auto response = sendJsonRequest("GET", modelsUrl, headers, std::string());
        if (!response.succeeded) {
            discovery.errorMessage = response.errorMessage.empty()
                ? ("HTTP " + std::to_string(response.statusCode))
                : response.errorMessage;
            return discovery;
        }

        // Parse the OpenAI-compatible `{"data": [{"id": "..."}, ...]}` shape.
        // Some providers nest the list under "models" instead — try both.
        try {
            const auto payload = nlohmann::json::parse(response.body, nullptr, false);
            if (payload.is_discarded()) {
                discovery.errorMessage = "Response was not valid JSON";
                return discovery;
            }
            const nlohmann::json* list = nullptr;
            if (payload.contains("data") && payload["data"].is_array()) {
                list = &payload["data"];
            } else if (payload.contains("models") && payload["models"].is_array()) {
                list = &payload["models"];
            } else if (payload.is_array()) {
                list = &payload;
            }
            if (list == nullptr) {
                discovery.errorMessage = "Response did not contain a model list";
                return discovery;
            }
            for (const auto& entry : *list) {
                DiscoveredModel model;
                if (entry.is_string()) {
                    model.id = entry.get<std::string>();
                } else if (entry.is_object()) {
                    if (entry.contains("id") && entry["id"].is_string()) {
                        model.id = entry["id"].get<std::string>();
                    } else if (entry.contains("name") && entry["name"].is_string()) {
                        model.id = entry["name"].get<std::string>();
                    }
                    if (entry.contains("display_name") && entry["display_name"].is_string()) {
                        model.displayName = entry["display_name"].get<std::string>();
                    } else if (entry.contains("displayName") && entry["displayName"].is_string()) {
                        model.displayName = entry["displayName"].get<std::string>();
                    }
                    if (entry.contains("description") && entry["description"].is_string()) {
                        model.description = entry["description"].get<std::string>();
                    }
                }
                if (!model.id.empty()) {
                    discovery.models.push_back(std::move(model));
                }
            }
            discovery.succeeded = true;
            return discovery;
        } catch (const std::exception& exc) {
            discovery.errorMessage = std::string("Parse error: ") + exc.what();
            return discovery;
        }
    }

    // Removes a provider by id from the configuration without touching its
    // credentials. Used to roll back a partial Auto-Connect when a later
    // stage (credential storage, assignment) fails.
    void removeProviderInternal(const std::string& providerId) {
        std::lock_guard<std::mutex> lock(state_->mutex);
        auto& providers = state_->configuration.providers;
        providers.erase(
            std::remove_if(providers.begin(), providers.end(),
                           [&providerId](const ProviderConnection& candidate) {
                               return candidate.id == providerId;
                           }),
            providers.end());
        (void)writeJsonFile(configurationFile_, state_->configuration);
    }

    std::shared_ptr<SharedState> state_;
    std::filesystem::path configurationFile_;
    std::shared_ptr<IProviderCatalogService> providerCatalogService_;
    std::weak_ptr<IProviderCredentialStore> credentialStore_;
    std::weak_ptr<IProviderAssignmentService> assignmentService_;
};

class ProviderCredentialStore final : public IProviderCredentialStore {
public:
    ProviderCredentialStore(std::filesystem::path filePath,
                            std::shared_ptr<IProviderRegistry> providerRegistry,
                            std::shared_ptr<IProviderCatalogService> providerCatalogService)
        : filePath_(std::move(filePath))
        , providerRegistry_(std::move(providerRegistry))
        , providerCatalogService_(std::move(providerCatalogService)) {
        if (!std::filesystem::exists(filePath_)) {
            (void)writeJsonFile(filePath_, ProviderCredentialsDocument{});
        }
    }

    std::vector<ProviderCredentialStatus> listStatuses() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return loadStatusesLocked();
    }

    std::map<std::string, std::string> readCredentials(const std::string& providerId) const override {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto document = loadDocumentLocked();
        const auto iterator = document.protectedPayloadsByProviderId.find(providerId);
        if (iterator == document.protectedPayloadsByProviderId.end()) {
            return {};
        }

        const auto payload = tryParseJson(unprotectSecretPayload(iterator->second));
        if (!payload.has_value()) {
            return {};
        }
        try {
            return payload->get<std::map<std::string, std::string>>();
        } catch (const nlohmann::json::exception&) {
            return {};
        } catch (...) {
            return {};
        }
    }

    OperationResult upsertCredentials(const ProviderCredentialUpdate& update) override {
        if (update.providerId.empty() || isBlank(update.providerId)) {
            return OperationResult{ false, false, "Provider credentials must target a provider route." };
        }

        const auto providers = providerRegistry_->listProviders();
        const auto providerIterator = std::find_if(
            providers.begin(),
            providers.end(),
            [&update](const ProviderConnection& provider) { return provider.id == update.providerId; });
        if (providerIterator == providers.end()) {
            return OperationResult{ false, false, "Provider route was not found." };
        }

        const auto capabilities = providerCatalogService_->listCapabilities();
        const auto capabilityIterator = findCapabilityByProviderId(
            capabilities.begin(), capabilities.end(),
            providerIterator->id, providerIterator->kind);
        if (capabilityIterator == capabilities.end()) {
            return OperationResult{ false, false, "Provider module metadata is not available for the selected route." };
        }

        for (const auto& field : capabilityIterator->credentialFields) {
            if (field.required && field.requirementGroup.empty()) {
                const auto iterator = update.values.find(field.fieldId);
                if (iterator == update.values.end() || iterator->second.empty() || isBlank(iterator->second)) {
                    return OperationResult{ false, false, field.label + " is required." };
                }
            }
        }

        std::set<std::string> requirementGroups;
        for (const auto& field : capabilityIterator->credentialFields) {
            if (!field.requirementGroup.empty()) {
                requirementGroups.insert(field.requirementGroup);
            }
        }
        for (const auto& requirementGroup : requirementGroups) {
            bool satisfied = false;
            for (const auto& field : capabilityIterator->credentialFields) {
                if (field.requirementGroup != requirementGroup) {
                    continue;
                }

                const auto iterator = update.values.find(field.fieldId);
                if (iterator != update.values.end() && !iterator->second.empty() && !isBlank(iterator->second)) {
                    satisfied = true;
                    break;
                }
            }
            if (!satisfied) {
                return OperationResult{ false, false, "One of the required " + requirementGroup + " credentials must be provided." };
            }
        }

        nlohmann::json payload = nlohmann::json::object();
        for (const auto& [fieldId, value] : update.values) {
            if (!value.empty() && !isBlank(value)) {
                payload[fieldId] = value;
            }
        }

        std::lock_guard<std::mutex> lock(mutex_);
        auto document = loadDocumentLocked();
        document.protectedPayloadsByProviderId[update.providerId] = protectSecretPayload(payload.dump());
        document.updatedAtUtcByProviderId[update.providerId] = timestampNowUtc();
        if (!writeJsonFile(filePath_, document)) {
            return OperationResult{ false, false, "Provider credentials could not be written to the local secure store." };
        }
        return OperationResult{ true, false, "Provider credentials were saved to the local secure store." };
    }

private:
    ProviderCredentialsDocument loadDocumentLocked() const {
        const auto json = readJsonFile(filePath_);
        if (json.is_null() || json.empty()) {
            return {};
        }
        return json.get<ProviderCredentialsDocument>();
    }

    std::vector<ProviderCredentialStatus> loadStatusesLocked() const {
        std::vector<ProviderCredentialStatus> statuses;
        const auto document = loadDocumentLocked();
        for (const auto& [providerId, protectedPayload] : document.protectedPayloadsByProviderId) {
            ProviderCredentialStatus status;
            status.providerId = providerId;
            if (const auto updatedAtIterator = document.updatedAtUtcByProviderId.find(providerId);
                updatedAtIterator != document.updatedAtUtcByProviderId.end()) {
                status.updatedAtUtc = updatedAtIterator->second;
            }

            try {
                const auto payload = nlohmann::json::parse(unprotectSecretPayload(protectedPayload));
                status.configured = !payload.empty();
                for (const auto& [fieldId, value] : payload.items()) {
                    if (value.is_string() && !value.get<std::string>().empty()) {
                        status.configuredFieldIds.push_back(fieldId);
                    }
                }
                status.message = status.configured
                    ? "Credentials are present in secure storage."
                    : "No provider credentials are currently configured.";
            } catch (...) {
                status.configured = false;
                status.message = "Stored credentials could not be read.";
            }

            statuses.push_back(std::move(status));
        }
        return statuses;
    }

    std::filesystem::path filePath_;
    std::shared_ptr<IProviderRegistry> providerRegistry_;
    std::shared_ptr<IProviderCatalogService> providerCatalogService_;
    mutable std::mutex mutex_;
};

// CLI-based sign-in service. Spawns `claude login` or `codex login` in a new
// console window so the operator can complete OAuth in their own browser,
// then polls for completion (process exit + expected auth file present).
// On success, registers a ProviderConnection that routes execution through
// the CLI — no API key is ever stored by us; the CLI keeps its own tokens.
class ProviderCliSignInService final {
public:
    struct SessionState {
        std::string sessionId;
        std::string bridge;          // "claude" or "codex"
        std::string providerId;      // e.g. "claude-code", "codex"
        std::string status;          // "pending" | "complete" | "failed"
        std::string message;
        std::string accountLabel;
        std::string startedAtUtc;
        std::string completedAtUtc;
        HANDLE processHandle = nullptr;
        HANDLE waitThread = nullptr;
        std::filesystem::path authFilePath;
    };

    ProviderCliSignInService(std::shared_ptr<IProviderRegistry> providerRegistry,
                             std::shared_ptr<IProviderCatalogService> providerCatalogService)
        : providerRegistry_(std::move(providerRegistry))
        , providerCatalogService_(std::move(providerCatalogService)) {}

    ~ProviderCliSignInService() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [_, session] : sessions_) {
            if (session.processHandle != nullptr) {
                CloseHandle(session.processHandle);
            }
            if (session.waitThread != nullptr) {
                CloseHandle(session.waitThread);
            }
        }
    }

    nlohmann::json startSession(const std::string& bridge, const std::string& providerId) {
        nlohmann::json response = nlohmann::json::object();

        if (bridge != "claude" && bridge != "codex") {
            response["succeeded"] = false;
            response["message"] = "Unsupported sign-in bridge: " + bridge;
            return response;
        }

        // Resolve the CLI binary path. If it isn't installed, tell the UI
        // explicitly so it can offer an "install" action instead of a
        // sign-in spinner that never finishes.
        const auto cliPath = resolveCliPath(bridge);
        if (!cliPath.has_value()) {
            response["succeeded"] = false;
            response["message"] = bridgeDisplayName(bridge) + " is not installed on this host. Install it first (e.g. via the dependency setup) and try again.";
            response["cliInstalled"] = false;
            response["bridge"] = bridge;
            return response;
        }

        SessionState session;
        session.sessionId = generateSessionId();
        session.bridge = bridge;
        session.providerId = providerId;
        session.status = "pending";
        session.message = "Complete the sign-in prompt in the console window or browser.";
        session.accountLabel = bridge == "claude"
            ? "Claude Pro / Max / Team account"
            : "OpenAI account (ChatGPT + Codex)";
        session.startedAtUtc = timestampNowUtc();
        session.authFilePath = expectedAuthFilePath(bridge);

        // Spawn the CLI's login subcommand in a new console window so the
        // operator can see the OAuth URL / device code prompts. We do NOT
        // capture stdout/stderr — the CLI owns the interaction.
        std::wstring commandLine;
        if (startsWithInsensitive(cliPath->extension().string(), ".cmd") ||
            startsWithInsensitive(cliPath->extension().string(), ".bat")) {
            commandLine = L"cmd.exe /d /s /c ";
            commandLine += quoteWindowsArgument(cliPath->wstring());
            commandLine += L" login";
        } else {
            commandLine = quoteWindowsArgument(cliPath->wstring());
            commandLine += L" login";
        }

        STARTUPINFOW startupInfo{};
        startupInfo.cb = sizeof(startupInfo);
        PROCESS_INFORMATION processInfo{};
        std::wstring mutableCommand = commandLine;
        const BOOL created = CreateProcessW(
            nullptr,
            mutableCommand.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NEW_CONSOLE,
            nullptr,
            nullptr,
            &startupInfo,
            &processInfo);
        if (created == 0) {
            const DWORD lastError = GetLastError();
            response["succeeded"] = false;
            response["message"] = "Failed to launch " + bridgeDisplayName(bridge) + " sign-in (error " + std::to_string(lastError) + ").";
            response["cliInstalled"] = true;
            return response;
        }
        CloseHandle(processInfo.hThread);
        session.processHandle = processInfo.hProcess;

        const std::string sessionId = session.sessionId;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            sessions_[sessionId] = std::move(session);
        }

        response["succeeded"] = true;
        response["sessionId"] = sessionId;
        response["message"] = "Sign-in console opened. Complete the prompt to finish.";
        response["bridge"] = bridge;
        return response;
    }

    nlohmann::json querySession(const std::string& sessionId) {
        nlohmann::json response = nlohmann::json::object();
        std::lock_guard<std::mutex> lock(mutex_);
        const auto iterator = sessions_.find(sessionId);
        if (iterator == sessions_.end()) {
            response["succeeded"] = false;
            response["message"] = "Unknown sign-in session.";
            return response;
        }

        auto& session = iterator->second;
        if (session.status == "pending" && session.processHandle != nullptr) {
            const DWORD waitResult = WaitForSingleObject(session.processHandle, 0);
            if (waitResult == WAIT_OBJECT_0) {
                DWORD exitCode = 0;
                GetExitCodeProcess(session.processHandle, &exitCode);
                CloseHandle(session.processHandle);
                session.processHandle = nullptr;
                session.completedAtUtc = timestampNowUtc();

                const bool authFileExists = !session.authFilePath.empty() &&
                    std::filesystem::exists(session.authFilePath);
                if (exitCode == 0 && authFileExists) {
                    session.status = "complete";
                    registerBridgedProvider(session.bridge, session.providerId);
                    if (session.bridge == "codex") {
                        session.message = "Signed in. ChatGPT (planning / reasoning) and Codex (coding agent) are both registered — assign each to roles below.";
                    } else {
                        session.message = "Signed in. " + session.accountLabel + " is registered — assign it to a role below.";
                    }
                } else if (exitCode == 0) {
                    session.status = "failed";
                    session.message = "Sign-in appeared to exit cleanly, but " + bridgeDisplayName(session.bridge) + " did not create its credential file.";
                } else {
                    session.status = "failed";
                    session.message = "Sign-in was canceled or failed (exit code " + std::to_string(exitCode) + ").";
                }
            }
        }

        response["succeeded"] = true;
        response["sessionId"] = session.sessionId;
        response["status"] = session.status;
        response["message"] = session.message;
        response["bridge"] = session.bridge;
        response["providerId"] = session.providerId;
        response["accountLabel"] = session.accountLabel;
        response["startedAtUtc"] = session.startedAtUtc;
        response["completedAtUtc"] = session.completedAtUtc;
        return response;
    }

    nlohmann::json detectInstalled() const {
        nlohmann::json response = nlohmann::json::array();
        for (const char* bridge : { "claude", "codex" }) {
            nlohmann::json entry = nlohmann::json::object();
            entry["bridge"] = bridge;
            entry["displayName"] = bridgeDisplayName(bridge);
            const auto cliPath = resolveCliPath(bridge);
            entry["installed"] = cliPath.has_value();
            entry["path"] = cliPath.has_value() ? cliPath->string() : "";
            const auto authPath = expectedAuthFilePath(bridge);
            entry["signedIn"] = !authPath.empty() && std::filesystem::exists(authPath);
            response.push_back(entry);
        }
        return response;
    }

private:
    static std::string generateSessionId() {
        static std::atomic<uint64_t> counter{ 0 };
        const auto now = std::chrono::system_clock::now().time_since_epoch();
        const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(now).count();
        return "signin-" + std::to_string(micros) + "-" + std::to_string(counter.fetch_add(1));
    }

    static std::optional<std::filesystem::path> resolveCliPath(const std::string& bridge) {
        if (bridge == "claude") {
            return findCommandOnPath({ L"claude.exe", L"claude.cmd", L"claude.bat", L"claude" });
        }
        if (bridge == "codex") {
            return findCommandOnPath({ L"codex.exe", L"codex.cmd", L"codex.bat", L"codex" });
        }
        return std::nullopt;
    }

    static std::filesystem::path expectedAuthFilePath(const std::string& bridge) {
        const DWORD required = GetEnvironmentVariableW(L"USERPROFILE", nullptr, 0);
        if (required == 0) {
            return {};
        }
        std::wstring userProfile(static_cast<size_t>(required - 1), L'\0');
        GetEnvironmentVariableW(L"USERPROFILE", userProfile.data(), required);
        std::filesystem::path base(userProfile);
        if (bridge == "claude") {
            // Claude Code stores OAuth tokens here as of Claude Code 2.x.
            return base / L".claude" / L".credentials.json";
        }
        if (bridge == "codex") {
            return base / L".codex" / L"auth.json";
        }
        return {};
    }

    static std::string bridgeDisplayName(const std::string& bridge) {
        if (bridge == "claude") return "Claude Code CLI";
        if (bridge == "codex") return "Codex CLI";
        return bridge;
    }

    void registerBridgedProvider(const std::string& bridge, const std::string& providerIdHint) {
        // A single `codex login` authenticates the user's OpenAI account for
        // BOTH ChatGPT (general reasoning) and Codex (coding agent). We
        // therefore register every capability whose cliBridgeCommand
        // matches the bridge that just completed sign-in — the operator
        // gets each distinct provider entry (chatgpt + codex) available
        // for role assignment without having to sign in twice.
        //
        // For bridges that map 1:1 to a single provider (claude → claude-code)
        // this still registers exactly one entry because only one capability
        // declares the "claude" bridge.
        //
        // providerIdHint is retained for diagnostics / future routing but
        // no longer restricts the set of registered providers — the user's
        // intent in clicking "Sign in with ChatGPT" is to unlock the
        // account, not to choose one of two logical endpoints.
        (void)providerIdHint;
        const auto capabilities = providerCatalogService_->listCapabilities();
        for (const auto& capability : capabilities) {
            if (capability.cliBridgeCommand != bridge) {
                continue;
            }
            ProviderConnection provider;
            provider.id = capability.providerId;
            provider.kind = capability.kind;
            provider.displayName = capability.displayName;
            provider.baseUrl = capability.defaultBaseUrl;
            provider.modelId = capability.recommendedModel;
            provider.enabled = true;
            provider.allowAutonomousControl = false;
            provider.credentialsConfigured = true; // via CLI's own OAuth store
            provider.isTemplate = false;
            (void)providerRegistry_->upsertProvider(provider);
        }
    }

    mutable std::mutex mutex_;
    std::map<std::string, SessionState> sessions_;
    std::shared_ptr<IProviderRegistry> providerRegistry_;
    std::shared_ptr<IProviderCatalogService> providerCatalogService_;
};

class SubAgentCatalogService final : public ISubAgentCatalogService {
public:
    SubAgentCatalogService(std::shared_ptr<SharedState> state,
                           std::filesystem::path configurationFile,
                           std::shared_ptr<IRuntimeInventoryService> inventoryService)
        : state_(std::move(state))
        , configurationFile_(std::move(configurationFile))
        , inventoryService_(std::move(inventoryService)) {}

    std::vector<RuntimeEndpoint> listCustomSubAgents() const override {
        std::lock_guard<std::mutex> lock(state_->mutex);
        std::vector<RuntimeEndpoint> subAgents;
        for (const auto& endpoint : state_->configuration.activeProfile.seededEndpoints) {
            if (endpoint.kind == EndpointKind::SubAgent && endpoint.userDefined) {
                subAgents.push_back(endpoint);
            }
        }

        std::sort(
            subAgents.begin(),
            subAgents.end(),
            [](const RuntimeEndpoint& left, const RuntimeEndpoint& right) {
                return std::tie(left.displayName, left.id) < std::tie(right.displayName, right.id);
            });
        return subAgents;
    }

    OperationResult upsertSubAgent(const RuntimeEndpoint& subAgent) override {
        if (subAgent.id.empty() || isBlank(subAgent.id)) {
            return OperationResult{ false, false, "Sub-agent ID is required." };
        }
        if (subAgent.displayName.empty() || isBlank(subAgent.displayName)) {
            return OperationResult{ false, false, "Sub-agent display name is required." };
        }

        const std::set<std::string> reservedTargetIds{
            "planner",
            "architect",
            "coding-specialists"
        };
        if (reservedTargetIds.contains(subAgent.id)) {
            return OperationResult{ false, false, "The requested sub-agent ID conflicts with a reserved orchestration target." };
        }

        RuntimeEndpoint normalized = subAgent;
        normalized.kind = EndpointKind::SubAgent;
        normalized.userDefined = true;
        normalized.id = trimCopy(normalized.id);
        normalized.displayName = trimCopy(normalized.displayName);
        normalized.host = trimCopy(normalized.host);
        normalized.protocol = trimCopy(normalized.protocol);
        normalized.description = trimCopy(normalized.description);
        normalized.routePath = trimCopy(normalized.routePath);
        normalized.specialization = trimCopy(normalized.specialization);
        normalized.status = EndpointStatus::Unknown;
        normalized.lastCheckedUtc = timestampNowUtc();
        if (normalized.host.empty()) {
            normalized.host = state_->configuration.activeProfile.preferredBindAddress;
        }
        if (normalized.protocol.empty()) {
            normalized.protocol = normalized.port == 0 ? "virtual" : "http";
        }
        if (normalized.description.empty() && !normalized.specialization.empty()) {
            normalized.description = normalized.specialization + " specialist lane";
        }

        {
            std::lock_guard<std::mutex> lock(state_->mutex);

            const auto conflictingGroup = std::find_if(
                state_->configuration.subAgentGroups.begin(),
                state_->configuration.subAgentGroups.end(),
                [&normalized](const SubAgentGroupDefinition& group) { return group.groupId == normalized.id; });
            if (conflictingGroup != state_->configuration.subAgentGroups.end()) {
                return OperationResult{ false, false, "The requested sub-agent ID conflicts with an existing sub-agent group." };
            }

            auto& endpoints = state_->configuration.activeProfile.seededEndpoints;
            const auto endpointIterator = std::find_if(
                endpoints.begin(),
                endpoints.end(),
                [&normalized](const RuntimeEndpoint& endpoint) { return endpoint.id == normalized.id; });
            if (endpointIterator != endpoints.end() && !endpointIterator->userDefined) {
                return OperationResult{ false, false, "The requested sub-agent ID is already owned by a seeded or imported runtime endpoint." };
            }

            if (endpointIterator == endpoints.end()) {
                endpoints.push_back(normalized);
            } else {
                *endpointIterator = normalized;
            }

            if (!writeJsonFile(configurationFile_, state_->configuration)) {
                return OperationResult{ false, false, "Custom sub-agent settings could not be written to disk." };
            }
        }
        inventoryService_->refreshAsync();
        return OperationResult{ true, false, "Custom sub-agent settings updated." };
    }

    OperationResult removeSubAgent(const std::string& subAgentId) override {
        if (subAgentId.empty() || isBlank(subAgentId)) {
            return OperationResult{ false, false, "A custom sub-agent ID is required." };
        }

        {
            std::lock_guard<std::mutex> lock(state_->mutex);

            auto& endpoints = state_->configuration.activeProfile.seededEndpoints;
            const auto iterator = std::find_if(
                endpoints.begin(),
                endpoints.end(),
                [&subAgentId](const RuntimeEndpoint& endpoint) { return endpoint.id == subAgentId && endpoint.kind == EndpointKind::SubAgent; });
            if (iterator == endpoints.end()) {
                return OperationResult{ false, false, "The requested sub-agent was not found." };
            }
            if (!iterator->userDefined) {
                return OperationResult{ false, false, "Only user-defined sub-agents can be removed." };
            }

            endpoints.erase(iterator);

            auto& assignments = state_->configuration.providerAssignments;
            assignments.erase(
                std::remove_if(
                    assignments.begin(),
                    assignments.end(),
                    [&subAgentId](const ProviderAssignment& assignment) { return assignment.targetId == subAgentId; }),
                assignments.end());

            auto& groups = state_->configuration.subAgentGroups;
            for (auto& group : groups) {
                group.memberTargetIds.erase(
                    std::remove(group.memberTargetIds.begin(), group.memberTargetIds.end(), subAgentId),
                    group.memberTargetIds.end());
            }

            std::vector<std::string> removedGroupIds;
            groups.erase(
                std::remove_if(
                    groups.begin(),
                    groups.end(),
                    [&removedGroupIds](const SubAgentGroupDefinition& group) {
                        if (!group.memberTargetIds.empty()) {
                            return false;
                        }
                        removedGroupIds.push_back(group.groupId);
                        return true;
                    }),
                groups.end());

            if (!removedGroupIds.empty()) {
                assignments.erase(
                    std::remove_if(
                        assignments.begin(),
                        assignments.end(),
                        [&removedGroupIds](const ProviderAssignment& assignment) {
                            return std::find(removedGroupIds.begin(), removedGroupIds.end(), assignment.targetId) != removedGroupIds.end() ||
                                std::find(removedGroupIds.begin(), removedGroupIds.end(), assignment.sourceGroupId) != removedGroupIds.end();
                        }),
                    assignments.end());
            }

            if (!writeJsonFile(configurationFile_, state_->configuration)) {
                return OperationResult{ false, false, "Sub-agent removal could not be written to disk." };
            }
        }
        inventoryService_->refreshAsync();
        return OperationResult{ true, false, "Custom sub-agent removed." };
    }

private:
    std::shared_ptr<SharedState> state_;
    std::filesystem::path configurationFile_;
    std::shared_ptr<IRuntimeInventoryService> inventoryService_;
};

class McpServerCatalogService final : public IMcpServerCatalogService {
public:
    McpServerCatalogService(std::shared_ptr<SharedState> state,
                            std::filesystem::path configurationFile,
                            std::shared_ptr<IRuntimeInventoryService> inventoryService)
        : state_(std::move(state))
        , configurationFile_(std::move(configurationFile))
        , inventoryService_(std::move(inventoryService)) {}

    std::vector<RuntimeEndpoint> listCustomMcpServers() const override {
        std::lock_guard<std::mutex> lock(state_->mutex);
        std::vector<RuntimeEndpoint> mcpServers;
        for (const auto& endpoint : state_->configuration.activeProfile.seededEndpoints) {
            if (endpoint.kind == EndpointKind::MCPServer && endpoint.userDefined) {
                mcpServers.push_back(endpoint);
            }
        }

        std::sort(
            mcpServers.begin(),
            mcpServers.end(),
            [](const RuntimeEndpoint& left, const RuntimeEndpoint& right) {
                return std::tie(left.displayName, left.id) < std::tie(right.displayName, right.id);
            });
        return mcpServers;
    }

    OperationResult upsertMcpServer(const RuntimeEndpoint& mcpServer) override {
        if (mcpServer.id.empty() || isBlank(mcpServer.id)) {
            return OperationResult{ false, false, "MCP server ID is required." };
        }
        if (mcpServer.displayName.empty() || isBlank(mcpServer.displayName)) {
            return OperationResult{ false, false, "MCP server display name is required." };
        }
        if (mcpServer.port == 0) {
            return OperationResult{ false, false, "MCP servers require a listening port." };
        }

        RuntimeEndpoint normalized = mcpServer;
        normalized.kind = EndpointKind::MCPServer;
        normalized.userDefined = true;
        normalized.id = trimCopy(normalized.id);
        normalized.displayName = trimCopy(normalized.displayName);
        normalized.host = trimCopy(normalized.host);
        normalized.protocol = trimCopy(normalized.protocol);
        normalized.description = trimCopy(normalized.description);
        normalized.routePath = trimCopy(normalized.routePath);
        normalized.specialization.clear();
        normalized.status = EndpointStatus::Unknown;
        normalized.lastCheckedUtc = timestampNowUtc();
        if (normalized.protocol.empty()) {
            normalized.protocol = "http";
        }
        if (normalized.description.empty()) {
            normalized.description = "Custom MCP server lane.";
        }

        {
            std::lock_guard<std::mutex> lock(state_->mutex);
            if (normalized.host.empty()) {
                normalized.host = state_->configuration.activeProfile.preferredBindAddress;
            }
            auto& endpoints = state_->configuration.activeProfile.seededEndpoints;
            const auto endpointIterator = std::find_if(
                endpoints.begin(),
                endpoints.end(),
                [&normalized](const RuntimeEndpoint& endpoint) { return endpoint.id == normalized.id; });
            if (endpointIterator != endpoints.end() && !endpointIterator->userDefined) {
                return OperationResult{ false, false, "The requested MCP server ID is already owned by a seeded or imported runtime endpoint." };
            }

            if (endpointIterator == endpoints.end()) {
                endpoints.push_back(normalized);
            } else {
                *endpointIterator = normalized;
            }

            if (!writeJsonFile(configurationFile_, state_->configuration)) {
                return OperationResult{ false, false, "Custom MCP server settings could not be written to disk." };
            }
        }
        inventoryService_->refreshAsync();
        return OperationResult{ true, false, "Custom MCP server settings updated." };
    }

    OperationResult removeMcpServer(const std::string& mcpServerId) override {
        if (mcpServerId.empty() || isBlank(mcpServerId)) {
            return OperationResult{ false, false, "A custom MCP server ID is required." };
        }

        {
            std::lock_guard<std::mutex> lock(state_->mutex);
            auto& endpoints = state_->configuration.activeProfile.seededEndpoints;
            const auto iterator = std::find_if(
                endpoints.begin(),
                endpoints.end(),
                [&mcpServerId](const RuntimeEndpoint& endpoint) {
                    return endpoint.id == mcpServerId && endpoint.kind == EndpointKind::MCPServer;
                });
            if (iterator == endpoints.end()) {
                return OperationResult{ false, false, "The requested MCP server was not found." };
            }
            if (!iterator->userDefined) {
                return OperationResult{ false, false, "Only user-defined MCP servers can be removed." };
            }

            endpoints.erase(iterator);
            if (!writeJsonFile(configurationFile_, state_->configuration)) {
                return OperationResult{ false, false, "MCP server removal could not be written to disk." };
            }
        }
        inventoryService_->refreshAsync();
        return OperationResult{ true, false, "Custom MCP server removed." };
    }

private:
    std::shared_ptr<SharedState> state_;
    std::filesystem::path configurationFile_;
    std::shared_ptr<IRuntimeInventoryService> inventoryService_;
};

class SubAgentGroupService final : public ISubAgentGroupService {
public:
    SubAgentGroupService(std::shared_ptr<SharedState> state,
                         std::filesystem::path configurationFile,
                         std::shared_ptr<IRuntimeInventoryService> inventoryService)
        : state_(std::move(state))
        , configurationFile_(std::move(configurationFile))
        , inventoryService_(std::move(inventoryService)) {}

    std::vector<SubAgentGroupDefinition> listGroups() const override {
        std::lock_guard<std::mutex> lock(state_->mutex);
        return state_->configuration.subAgentGroups;
    }

    OperationResult upsertGroup(const SubAgentGroupDefinition& group) override {
        if (group.groupId.empty() || isBlank(group.groupId)) {
            return OperationResult{ false, false, "Sub-agent group ID is required." };
        }
        if (group.displayName.empty() || isBlank(group.displayName)) {
            return OperationResult{ false, false, "Sub-agent group display name is required." };
        }

        // Read sub-agent IDs from the configuration state directly rather
        // than the inventoryService cache. The inventory refreshes the cache
        // asynchronously now (to avoid blocking upsert handlers on slow
        // TCP probes), which means a sub-agent upserted milliseconds ago
        // may not yet be visible in listEndpoints(). The configuration
        // state is the source of truth and is updated synchronously.
        std::set<std::string> subAgentIds;
        {
            std::lock_guard<std::mutex> lock(state_->mutex);
            for (const auto& endpoint : state_->configuration.activeProfile.seededEndpoints) {
                if (endpoint.kind == EndpointKind::SubAgent && !endpoint.id.empty()) {
                    subAgentIds.insert(endpoint.id);
                }
            }
        }

        if (subAgentIds.empty()) {
            return OperationResult{ false, false, "No sub-agents are currently available for grouping." };
        }

        const std::set<std::string> reservedTargetIds{
            "planner",
            "architect",
            "coding-specialists"
        };
        if (reservedTargetIds.contains(group.groupId) || subAgentIds.contains(group.groupId)) {
            return OperationResult{ false, false, "The requested sub-agent group ID conflicts with an existing orchestration target." };
        }

        std::vector<std::string> memberTargetIds;
        std::set<std::string> seenMembers;
        for (const auto& memberTargetId : group.memberTargetIds) {
            if (!subAgentIds.contains(memberTargetId) || seenMembers.contains(memberTargetId)) {
                continue;
            }
            seenMembers.insert(memberTargetId);
            memberTargetIds.push_back(memberTargetId);
        }

        if (memberTargetIds.empty()) {
            return OperationResult{ false, false, "Select at least one valid sub-agent for the group." };
        }

        std::lock_guard<std::mutex> lock(state_->mutex);
        auto& groups = state_->configuration.subAgentGroups;
        auto iterator = std::find_if(
            groups.begin(),
            groups.end(),
            [&group](const SubAgentGroupDefinition& candidate) { return candidate.groupId == group.groupId; });

        SubAgentGroupDefinition normalized = group;
        normalized.memberTargetIds = std::move(memberTargetIds);
        normalized.updatedAtUtc = timestampNowUtc();
        normalized.description = trimCopy(normalized.description);

        if (iterator == groups.end()) {
            groups.push_back(std::move(normalized));
        } else {
            *iterator = std::move(normalized);
        }

        std::sort(
            groups.begin(),
            groups.end(),
            [](const SubAgentGroupDefinition& left, const SubAgentGroupDefinition& right) {
                return std::tie(left.displayName, left.groupId) < std::tie(right.displayName, right.groupId);
            });
        if (!writeJsonFile(configurationFile_, state_->configuration)) {
            return OperationResult{ false, false, "Sub-agent group settings could not be written to disk." };
        }
        return OperationResult{ true, false, "Sub-agent group settings updated." };
    }

    OperationResult removeGroup(const std::string& groupId) override {
        if (groupId.empty() || isBlank(groupId)) {
            return OperationResult{ false, false, "A sub-agent group ID is required." };
        }

        std::lock_guard<std::mutex> lock(state_->mutex);
        auto& groups = state_->configuration.subAgentGroups;
        const auto originalSize = groups.size();
        groups.erase(
            std::remove_if(
                groups.begin(),
                groups.end(),
                [&groupId](const SubAgentGroupDefinition& candidate) { return candidate.groupId == groupId; }),
            groups.end());

        if (groups.size() == originalSize) {
            return OperationResult{ false, false, "The requested sub-agent group was not found." };
        }

        auto& assignments = state_->configuration.providerAssignments;
        assignments.erase(
            std::remove_if(
                assignments.begin(),
                assignments.end(),
                [&groupId](const ProviderAssignment& assignment) {
                    return assignment.targetId == groupId || assignment.sourceGroupId == groupId;
                }),
            assignments.end());

        if (!writeJsonFile(configurationFile_, state_->configuration)) {
            return OperationResult{ false, false, "Sub-agent group removal could not be written to disk." };
        }
        return OperationResult{ true, false, "Sub-agent group removed." };
    }

private:
    std::shared_ptr<SharedState> state_;
    std::filesystem::path configurationFile_;
    std::shared_ptr<IRuntimeInventoryService> inventoryService_;
};

class ProviderAssignmentService final : public IProviderAssignmentService {
public:
    ProviderAssignmentService(std::shared_ptr<SharedState> state,
                              std::filesystem::path configurationFile,
                              std::shared_ptr<IRuntimeInventoryService> inventoryService,
                              std::shared_ptr<ISubAgentGroupService> subAgentGroupService,
                              std::shared_ptr<IProviderRegistry> providerRegistry)
        : state_(std::move(state))
        , configurationFile_(std::move(configurationFile))
        , inventoryService_(std::move(inventoryService))
        , subAgentGroupService_(std::move(subAgentGroupService))
        , providerRegistry_(std::move(providerRegistry)) {}

    std::vector<ProviderAssignmentTarget> listTargets() const override {
        auto endpoints = inventoryService_->listEndpoints();
        std::set<std::string> availableSubAgentIds;
        std::vector<ProviderAssignmentTarget> targets{
            ProviderAssignmentTarget{
                "planner",
                ProviderAssignmentTargetKind::Role,
                "Planner",
                "Owns global planning and delivery sequencing.",
                {}
            },
            ProviderAssignmentTarget{
                "architect",
                ProviderAssignmentTargetKind::Role,
                "Architect",
                "Owns solution architecture and design decisions.",
                {}
            },
            ProviderAssignmentTarget{
                "auditor",
                ProviderAssignmentTargetKind::Role,
                "Auditor",
                "Owns review, verification, and compliance-oriented delivery checks.",
                {}
            }
        };

        ProviderAssignmentTarget specialistGroup;
        specialistGroup.targetId = "coding-specialists";
        specialistGroup.kind = ProviderAssignmentTargetKind::SubAgentGroup;
        specialistGroup.displayName = "Coding Specialists";
        specialistGroup.description = "Assign one provider across the current sub-agent specialist pool.";

        for (const auto& endpoint : endpoints) {
            if (endpoint.kind != EndpointKind::SubAgent) {
                continue;
            }

            availableSubAgentIds.insert(endpoint.id);
            specialistGroup.memberTargetIds.push_back(endpoint.id);
            targets.push_back(ProviderAssignmentTarget{
                endpoint.id,
                ProviderAssignmentTargetKind::SubAgent,
                endpoint.displayName,
                endpoint.description.empty() ? "Sub-agent route" : endpoint.description,
                {}
            });
        }

        if (!specialistGroup.memberTargetIds.empty()) {
            targets.push_back(std::move(specialistGroup));
        }

        for (const auto& group : subAgentGroupService_->listGroups()) {
            ProviderAssignmentTarget customGroup;
            customGroup.targetId = group.groupId;
            customGroup.kind = ProviderAssignmentTargetKind::SubAgentGroup;
            customGroup.displayName = group.displayName;
            customGroup.description = group.description.empty()
                ? "Assign one provider across a named sub-agent specialist group."
                : group.description;

            for (const auto& memberTargetId : group.memberTargetIds) {
                if (availableSubAgentIds.contains(memberTargetId)) {
                    customGroup.memberTargetIds.push_back(memberTargetId);
                }
            }

            if (!customGroup.memberTargetIds.empty()) {
                targets.push_back(std::move(customGroup));
            }
        }

        std::sort(
            targets.begin(),
            targets.end(),
            [](const ProviderAssignmentTarget& left, const ProviderAssignmentTarget& right) {
                return std::tie(left.kind, left.displayName, left.targetId) < std::tie(right.kind, right.displayName, right.targetId);
            });
        return targets;
    }

    std::vector<ProviderAssignment> listAssignments() const override {
        std::lock_guard<std::mutex> lock(state_->mutex);
        return state_->configuration.providerAssignments;
    }

    OperationResult upsertAssignment(const ProviderAssignment& assignment) override {
        const auto targets = listTargets();
        const auto targetIterator = std::find_if(
            targets.begin(),
            targets.end(),
            [&assignment](const ProviderAssignmentTarget& target) { return target.targetId == assignment.targetId; });
        if (targetIterator == targets.end()) {
            return OperationResult{ false, false, "The requested orchestration target is not available." };
        }

        if (!assignment.providerId.empty()) {
            const auto providers = providerRegistry_->listProviders();
            const auto providerIterator = std::find_if(
                providers.begin(),
                providers.end(),
                [&assignment](const ProviderConnection& provider) { return provider.id == assignment.providerId; });
            if (providerIterator == providers.end()) {
                return OperationResult{ false, false, "The selected provider route does not exist." };
            }
        }

        std::lock_guard<std::mutex> lock(state_->mutex);
        auto& assignments = state_->configuration.providerAssignments;

        auto clearTarget = [&assignments](const std::string& targetId) {
            assignments.erase(
                std::remove_if(
                    assignments.begin(),
                    assignments.end(),
                    [&targetId](const ProviderAssignment& candidate) { return candidate.targetId == targetId; }),
                assignments.end());
        };

        if (targetIterator->kind == ProviderAssignmentTargetKind::SubAgentGroup) {
            clearTarget(assignment.targetId);
            assignments.erase(
                std::remove_if(
                    assignments.begin(),
                    assignments.end(),
                    [&assignment](const ProviderAssignment& candidate) { return candidate.sourceGroupId == assignment.targetId; }),
                assignments.end());

            if (!assignment.providerId.empty()) {
                for (const auto& memberTargetId : targetIterator->memberTargetIds) {
                    clearTarget(memberTargetId);
                }

                assignments.push_back(ProviderAssignment{
                    assignment.targetId,
                    ProviderAssignmentTargetKind::SubAgentGroup,
                    assignment.providerId,
                    timestampNowUtc(),
                    ""
                });
                for (const auto& memberTargetId : targetIterator->memberTargetIds) {
                    assignments.push_back(ProviderAssignment{
                        memberTargetId,
                        ProviderAssignmentTargetKind::SubAgent,
                        assignment.providerId,
                        timestampNowUtc(),
                        assignment.targetId
                    });
                }
            }

            if (!writeJsonFile(configurationFile_, state_->configuration)) {
                return OperationResult{ false, false, "Provider assignment update could not be written to disk." };
            }
            const std::string verb = assignment.providerId.empty() ? "Cleared" : "Assigned";
            return OperationResult{
                true,
                false,
                verb + " " + std::to_string(targetIterator->memberTargetIds.size()) + " sub-agent routes for " + targetIterator->displayName + "."
            };
        }

        clearTarget(assignment.targetId);
        if (!assignment.providerId.empty()) {
            assignments.push_back(ProviderAssignment{
                assignment.targetId,
                targetIterator->kind,
                assignment.providerId,
                timestampNowUtc(),
                ""
            });
        }

        if (!writeJsonFile(configurationFile_, state_->configuration)) {
            return OperationResult{ false, false, "Provider ownership update could not be written to disk." };
        }
        return OperationResult{
            true,
            false,
            assignment.providerId.empty()
                ? "Provider ownership was cleared."
                : "Provider ownership was updated."
        };
    }

private:
    std::shared_ptr<SharedState> state_;
    std::filesystem::path configurationFile_;
    std::shared_ptr<IRuntimeInventoryService> inventoryService_;
    std::shared_ptr<ISubAgentGroupService> subAgentGroupService_;
    std::shared_ptr<IProviderRegistry> providerRegistry_;
};

class ProviderExecutionCatalogService final : public IProviderExecutionCatalogService {
public:
    std::vector<ProviderExecutionRegistration> listRegistrations() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<ProviderExecutionRegistration> registrations;
        registrations.reserve(registrationsByProviderId_.size());
        for (const auto& [providerId, registration] : registrationsByProviderId_) {
            (void)providerId;
            registrations.push_back(registration);
        }

        std::sort(
            registrations.begin(),
            registrations.end(),
            [](const ProviderExecutionRegistration& left, const ProviderExecutionRegistration& right) {
                return std::tie(left.displayName, left.providerId) < std::tie(right.displayName, right.providerId);
            });
        return registrations;
    }

    void upsertRegistration(const ProviderExecutionRegistration& registration) override {
        std::lock_guard<std::mutex> lock(mutex_);
        registrationsByProviderId_[registration.providerId] = registration;
    }

    void removeRegistration(const std::string& providerId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto iterator = registrationsByProviderId_.begin(); iterator != registrationsByProviderId_.end();) {
            if (iterator->second.providerId == providerId) {
                iterator = registrationsByProviderId_.erase(iterator);
            } else {
                ++iterator;
            }
        }
    }

private:
    mutable std::mutex mutex_;
    std::map<std::string, ProviderExecutionRegistration> registrationsByProviderId_;
};

class ProviderExecutionService final : public IProviderExecutionService {
public:
    ProviderExecutionService(std::shared_ptr<SharedState> state,
                             std::shared_ptr<IProviderRegistry> providerRegistry,
                             std::shared_ptr<IProviderCredentialStore> providerCredentialStore,
                             std::shared_ptr<IProviderAssignmentService> providerAssignmentService,
                             std::shared_ptr<IRuntimeInventoryService> inventoryService,
                             std::shared_ptr<IProviderExecutionCatalogService> providerExecutionCatalogService,
                             std::shared_ptr<ICommandLogicUnitService> commandLogicUnitService)
        : state_(std::move(state))
        , providerRegistry_(std::move(providerRegistry))
        , providerCredentialStore_(std::move(providerCredentialStore))
        , providerAssignmentService_(std::move(providerAssignmentService))
        , inventoryService_(std::move(inventoryService))
        , providerExecutionCatalogService_(std::move(providerExecutionCatalogService))
        , commandLogicUnitService_(std::move(commandLogicUnitService)) {}

    std::vector<ProviderExecutionRecord> history() const override {
        std::lock_guard<std::mutex> lock(state_->mutex);
        return state_->providerExecutionHistory;
    }

    ProviderExecutionRecord execute(const ProviderExecutionRequest& request) override {
        ProviderExecutionRecord record;
        record.executionId = generateExecutionId();
        record.targetId = request.targetId;
        record.status = ProviderExecutionStatus::Failed;
        record.startedAtUtc = timestampNowUtc();

        if (request.targetId.empty() || isBlank(request.targetId)) {
            record.errorMessage = "A provider execution target is required.";
            record.completedAtUtc = timestampNowUtc();
            persistRecord(record);
            return record;
        }
        if (request.prompt.empty() || isBlank(request.prompt)) {
            record.errorMessage = "A provider execution prompt is required.";
            record.completedAtUtc = timestampNowUtc();
            persistRecord(record);
            return record;
        }

        const auto targets = providerAssignmentService_->listTargets();
        const auto targetIterator = std::find_if(
            targets.begin(),
            targets.end(),
            [&request](const ProviderAssignmentTarget& target) { return target.targetId == request.targetId; });
        if (targetIterator == targets.end()) {
            record.errorMessage = "The requested orchestration target is not available.";
            record.completedAtUtc = timestampNowUtc();
            persistRecord(record);
            return record;
        }
        record.targetDisplayName = targetIterator->displayName;

        if (commandLogicUnitService_) {
            const auto decision = commandLogicUnitService_->enforceAction(GovernanceEnforcementRequest{
                GovernanceActionKind::ProviderExecution,
                request.targetId,
                {},
                {},
                false
            });
            if (!decision.allowed) {
                record.errorMessage = decision.message.empty()
                    ? "CLU blocked provider execution."
                    : decision.message;
                record.completedAtUtc = timestampNowUtc();
                persistRecord(record);
                return record;
            }
        }

        const auto assignments = providerAssignmentService_->listAssignments();
        const auto assignmentIterator = std::find_if(
            assignments.begin(),
            assignments.end(),
            [&request](const ProviderAssignment& assignment) { return assignment.targetId == request.targetId; });
        if (assignmentIterator == assignments.end() || assignmentIterator->providerId.empty()) {
            record.errorMessage = "No provider route owns the requested orchestration target.";
            record.completedAtUtc = timestampNowUtc();
            persistRecord(record);
            return record;
        }

        const auto providers = providerRegistry_->listProviders();
        const auto providerIterator = std::find_if(
            providers.begin(),
            providers.end(),
            [&assignmentIterator](const ProviderConnection& provider) { return provider.id == assignmentIterator->providerId; });
        if (providerIterator == providers.end()) {
            record.errorMessage = "The assigned provider route is no longer available.";
            record.completedAtUtc = timestampNowUtc();
            persistRecord(record);
            return record;
        }

        record.providerId = providerIterator->id;
        record.providerKind = providerIterator->kind;
        record.providerDisplayName = providerIterator->displayName;
        record.modelId = providerIterator->modelId;

        const auto registrations = providerExecutionCatalogService_->listRegistrations();
        const auto registrationIterator = findExecutionRegistrationByProviderId(
            registrations.begin(), registrations.end(),
            providerIterator->id, providerIterator->kind);
        if (registrationIterator == registrations.end()) {
            record.errorMessage = "No active provider execution module is available for the assigned provider kind.";
            record.completedAtUtc = timestampNowUtc();
            persistRecord(record);
            return record;
        }

        const auto credentials = providerCredentialStore_->readCredentials(providerIterator->id);
        const bool cliBridgedTransport =
            registrationIterator->transport == ProviderExecutionTransport::ClaudeCodeCli ||
            registrationIterator->transport == ProviderExecutionTransport::CodexCli;
        // CLI-bridged transports get their credentials from the CLI's own OAuth
        // store (e.g., `claude login`, `codex login`), so an empty credential
        // bag is the expected path there. OpenAI-compatible direct HTTP still
        // requires an API key because it does not share the CLI's auth.
        if (credentials.empty() && !cliBridgedTransport) {
            record.errorMessage = "Provider credentials must be configured before execution can start.";
            record.completedAtUtc = timestampNowUtc();
            persistRecord(record);
            return record;
        }

        const auto accessibleEndpoints = resolveAccessibleMcpEndpoints(request);
        for (const auto& endpoint : accessibleEndpoints) {
            record.referencedMcpServerIds.push_back(endpoint.id);
        }

        record.status = ProviderExecutionStatus::Running;
        switch (registrationIterator->transport) {
            case ProviderExecutionTransport::ClaudeCodeCli:
                record = executeClaudeCodeCli(*providerIterator, request, accessibleEndpoints, credentials, record);
                break;
            case ProviderExecutionTransport::CodexCli:
                record = executeCodexCli(*providerIterator, request, accessibleEndpoints, credentials, record);
                break;
            case ProviderExecutionTransport::OpenAICompatibleChat:
            default:
                record = executeOpenAICompatibleChat(*providerIterator, request, accessibleEndpoints, credentials, record);
                break;
        }
        if (record.completedAtUtc.empty()) {
            record.completedAtUtc = timestampNowUtc();
        }
        persistRecord(record);
        return record;
    }

private:
    std::vector<RuntimeEndpoint> resolveAccessibleMcpEndpoints(const ProviderExecutionRequest& request) const {
        std::vector<RuntimeEndpoint> endpoints;
        for (const auto& endpoint : inventoryService_->listEndpoints()) {
            if (endpoint.kind != EndpointKind::MCPServer && endpoint.kind != EndpointKind::Gateway) {
                continue;
            }
            if (endpoint.host.empty() || endpoint.port == 0) {
                continue;
            }
            if (!request.preferredMcpServerIds.empty() &&
                std::find(request.preferredMcpServerIds.begin(), request.preferredMcpServerIds.end(), endpoint.id) ==
                    request.preferredMcpServerIds.end()) {
                continue;
            }
            endpoints.push_back(endpoint);
        }
        return endpoints;
    }

    static std::string firstCredentialValue(
        const std::map<std::string, std::string>& credentials,
        const std::initializer_list<const char*>& keys) {
        for (const char* key : keys) {
            const auto iterator = credentials.find(key);
            if (iterator != credentials.end() && !iterator->second.empty() && !isBlank(iterator->second)) {
                return iterator->second;
            }
        }
        return {};
    }

    static std::string normalizeBaseUrl(std::string baseUrl) {
        while (!baseUrl.empty() && baseUrl.back() == '/') {
            baseUrl.pop_back();
        }
        return baseUrl;
    }

    static std::string extractAssistantText(const nlohmann::json& message) {
        if (!message.contains("content")) {
            return {};
        }
        const auto& content = message.at("content");
        if (content.is_string()) {
            return content.get<std::string>();
        }
        if (!content.is_array()) {
            return {};
        }

        std::ostringstream stream;
        for (const auto& item : content) {
            if (!item.is_object()) {
                continue;
            }
            if (item.value("type", "") == "text") {
                if (item.contains("text")) {
                    if (item.at("text").is_string()) {
                        stream << item.at("text").get<std::string>();
                    } else if (item.at("text").is_object()) {
                        stream << item.at("text").value("value", "");
                    }
                }
            }
        }
        return trimCopy(stream.str());
    }

    std::string buildExecutionSystemPrompt(const ProviderConnection& provider,
                                           const ProviderExecutionRequest& request,
                                           const std::vector<RuntimeEndpoint>& endpoints,
                                           const bool useJsonRpcTools) const {
        std::ostringstream prompt;
        prompt << "You are operating inside Master Control Orchestration Server as the provider assigned to target '" << request.targetId << "'.\n"
               << "Stay within the requested orchestration lane and do not assume ownership of other roles or sub-agents.\n"
               << "Assigned provider route: " << provider.displayName << " (" << provider.id << ").\n";
        if (!request.workingDirectory.empty()) {
            prompt << "Preferred working directory: " << request.workingDirectory << "\n";
        }
        if (endpoints.empty()) {
            prompt << "No shared MCP servers are currently published for this execution.\n";
        } else {
            prompt << "Shared MCP server access is available for these endpoints:\n";
            for (const auto& endpoint : endpoints) {
                prompt << "- " << endpoint.id << " | " << endpoint.displayName << " | " << buildEndpointUrl(endpoint) << "\n";
            }
        }
        if (useJsonRpcTools) {
            prompt << "Use the provided function tools to inspect shared MCP servers and invoke JSON-RPC requests against them when you need tool access.\n";
        } else {
            prompt << "Use the provided MCP configuration to access the shared MCP servers directly.\n";
        }
        return prompt.str();
    }

    static nlohmann::json buildClaudeMcpConfig(const std::vector<RuntimeEndpoint>& endpoints) {
        nlohmann::json servers = nlohmann::json::object();
        for (const auto& endpoint : endpoints) {
            servers[endpoint.id] = {
                { "type", "http" },
                { "url", buildEndpointUrl(endpoint) }
            };
        }
        return nlohmann::json{ { "mcpServers", servers } };
    }

    ProviderExecutionRecord executeOpenAICompatibleChat(const ProviderConnection& provider,
                                                        const ProviderExecutionRequest& request,
                                                        const std::vector<RuntimeEndpoint>& endpoints,
                                                        const std::map<std::string, std::string>& credentials,
                                                        ProviderExecutionRecord record) const {
        const std::string bearerToken = firstCredentialValue(credentials, { "openai_api_key", "xai_api_key" });
        if (bearerToken.empty()) {
            record.status = ProviderExecutionStatus::Failed;
            record.errorMessage = "A bearer API key is required for the selected provider route.";
            record.completedAtUtc = timestampNowUtc();
            return record;
        }

        nlohmann::json messages = nlohmann::json::array({
            {
                { "role", "system" },
                { "content", buildExecutionSystemPrompt(provider, request, endpoints, true) }
            },
            {
                { "role", "user" },
                { "content", request.prompt }
            }
        });

        const nlohmann::json tools = nlohmann::json::array({
            {
                { "type", "function" },
                { "function", {
                    { "name", "master_control_list_mcp_servers" },
                    { "description", "List the MCP server endpoints currently shared through Master Control Orchestration Server for this execution." },
                    { "parameters", {
                        { "type", "object" },
                        { "properties", nlohmann::json::object() },
                        { "additionalProperties", false }
                    } }
                } }
            },
            {
                { "type", "function" },
                { "function", {
                    { "name", "master_control_invoke_mcp_jsonrpc" },
                    { "description", "Invoke a JSON-RPC request against a shared MCP server endpoint exposed by Master Control Orchestration Server." },
                    { "parameters", {
                        { "type", "object" },
                        { "properties", {
                            { "server_id", { { "type", "string" }, { "description", "The MCP server id returned by master_control_list_mcp_servers." } } },
                            { "request", {
                                { "type", "object" },
                                { "description", "A complete JSON-RPC request body to send to the MCP server." }
                            } }
                        } },
                        { "required", nlohmann::json::array({ "server_id", "request" }) },
                        { "additionalProperties", false }
                    } }
                } }
            }
        });

        const std::string endpointUrl = normalizeBaseUrl(provider.baseUrl) + "/chat/completions";
        const int maxTurns = (std::max)(1, request.maxTurns);
        for (int turn = 0; turn < maxTurns; ++turn) {
            nlohmann::json requestBody = {
                { "model", provider.modelId },
                { "messages", messages }
            };
            if (request.allowToolAccess) {
                requestBody["tools"] = tools;
                requestBody["tool_choice"] = "auto";
            }

            const auto response = sendJsonRequest(
                "POST",
                endpointUrl,
                { { L"Authorization", L"Bearer " + wideFromUtf8(bearerToken) } },
                requestBody.dump());
            record.rawResponse = response.body;
            if (!response.succeeded) {
                record.status = ProviderExecutionStatus::Failed;
                record.errorMessage = response.errorMessage.empty() ? response.body : response.errorMessage;
                record.completedAtUtc = timestampNowUtc();
                return record;
            }

            nlohmann::json payload;
            try {
                payload = nlohmann::json::parse(response.body);
            } catch (...) {
                record.status = ProviderExecutionStatus::Failed;
                record.errorMessage = "The provider returned an unreadable JSON payload.";
                record.completedAtUtc = timestampNowUtc();
                return record;
            }

            const auto* choices = payload.contains("choices") && payload.at("choices").is_array() && !payload.at("choices").empty()
                ? &payload.at("choices")
                : nullptr;
            if (choices == nullptr || !choices->front().contains("message")) {
                record.status = ProviderExecutionStatus::Failed;
                record.errorMessage = "The provider response did not include a completion message.";
                record.completedAtUtc = timestampNowUtc();
                return record;
            }

            const auto assistantMessage = choices->front().at("message");
            messages.push_back(assistantMessage);
            if (request.allowToolAccess &&
                assistantMessage.contains("tool_calls") &&
                assistantMessage.at("tool_calls").is_array() &&
                !assistantMessage.at("tool_calls").empty()) {
                for (const auto& toolCall : assistantMessage.at("tool_calls")) {
                    const std::string toolName = toolCall.contains("function")
                        ? toolCall.at("function").value("name", "")
                        : std::string{};
                    std::string toolResponse = R"({"ok":false,"message":"Unsupported tool call."})";
                    if (toolName == "master_control_list_mcp_servers") {
                        nlohmann::json servers = nlohmann::json::array();
                        for (const auto& endpoint : endpoints) {
                            servers.push_back({
                                { "id", endpoint.id },
                                { "displayName", endpoint.displayName },
                                { "url", buildEndpointUrl(endpoint) }
                            });
                        }
                        toolResponse = nlohmann::json{ { "servers", servers } }.dump();
                    } else if (toolName == "master_control_invoke_mcp_jsonrpc") {
                        try {
                            const auto arguments = nlohmann::json::parse(
                                toolCall.at("function").value("arguments", "{}"));
                            const std::string serverId = arguments.value("server_id", "");
                            const auto endpointIterator = std::find_if(
                                endpoints.begin(),
                                endpoints.end(),
                                [&serverId](const RuntimeEndpoint& endpoint) { return endpoint.id == serverId; });
                            if (endpointIterator == endpoints.end()) {
                                toolResponse = R"({"ok":false,"message":"Requested MCP server is not available for this execution."})";
                            } else {
                                const auto proxyResponse = sendJsonRequest(
                                    "POST",
                                    buildEndpointUrl(*endpointIterator),
                                    {},
                                    arguments.at("request").dump());
                                if (proxyResponse.succeeded) {
                                    nlohmann::json proxyPayload = {
                                        { "ok", true },
                                        { "statusCode", proxyResponse.statusCode }
                                    };
                                    try {
                                        proxyPayload["body"] = nlohmann::json::parse(proxyResponse.body);
                                    } catch (...) {
                                        proxyPayload["body"] = proxyResponse.body;
                                    }
                                    toolResponse = proxyPayload.dump();
                                } else {
                                    toolResponse = nlohmann::json{
                                        { "ok", false },
                                        { "statusCode", proxyResponse.statusCode },
                                        { "message", proxyResponse.errorMessage.empty() ? proxyResponse.body : proxyResponse.errorMessage }
                                    }.dump();
                                }
                            }
                        } catch (...) {
                            toolResponse = R"({"ok":false,"message":"Tool call arguments were invalid JSON."})";
                        }
                    }

                    record.toolEvents.push_back(toolName);
                    messages.push_back({
                        { "role", "tool" },
                        { "tool_call_id", toolCall.value("id", "") },
                        { "content", toolResponse }
                    });
                }
                continue;
            }

            record.outputText = extractAssistantText(assistantMessage);
            if (record.outputText.empty()) {
                record.outputText = payload.dump(2);
            }
            record.status = ProviderExecutionStatus::Succeeded;
            record.completedAtUtc = timestampNowUtc();
            return record;
        }

        record.status = ProviderExecutionStatus::Failed;
        record.errorMessage = "The provider execution exhausted its turn budget before returning a final answer.";
        record.completedAtUtc = timestampNowUtc();
        return record;
    }

    ProviderExecutionRecord executeClaudeCodeCli(const ProviderConnection& provider,
                                                 const ProviderExecutionRequest& request,
                                                 const std::vector<RuntimeEndpoint>& endpoints,
                                                 const std::map<std::string, std::string>& credentials,
                                                 ProviderExecutionRecord record) const {
        std::optional<std::filesystem::path> claudeCommand;
        const DWORD configuredCommandLength = GetEnvironmentVariableW(L"MASTERCONTROL_CLAUDE_COMMAND", nullptr, 0);
        if (configuredCommandLength > 0) {
            std::wstring configured(static_cast<size_t>(configuredCommandLength - 1), L'\0');
            GetEnvironmentVariableW(L"MASTERCONTROL_CLAUDE_COMMAND", configured.data(), configuredCommandLength);
            claudeCommand = std::filesystem::path(configured);
        } else {
            claudeCommand = findCommandOnPath({ L"claude.exe", L"claude.cmd", L"claude.bat" });
        }
        if (!claudeCommand.has_value()) {
            record.status = ProviderExecutionStatus::Failed;
            record.errorMessage = "Claude Code was not found on PATH.";
            record.completedAtUtc = timestampNowUtc();
            return record;
        }

        const auto executionDirectory = std::filesystem::temp_directory_path() / "MasterControlOrchestrationServer" / "provider-executions" / sanitizePathComponent(record.executionId);
        std::filesystem::create_directories(executionDirectory);
        const auto systemPromptFile = executionDirectory / "system-prompt.txt";
        {
            std::ofstream stream(systemPromptFile, std::ios::trunc);
            stream << buildExecutionSystemPrompt(provider, request, endpoints, false);
        }

        std::filesystem::path mcpConfigFile;
        if (request.allowToolAccess && !endpoints.empty()) {
            mcpConfigFile = executionDirectory / "mcp.json";
            if (!writeJsonFile(mcpConfigFile, buildClaudeMcpConfig(endpoints))) {
                record.status = ProviderExecutionStatus::Failed;
                record.errorMessage = "Failed to write MCP configuration for Claude execution.";
                record.completedAtUtc = timestampNowUtc();
                return record;
            }
        }

        std::vector<std::wstring> arguments{
            claudeCommand->wstring(),
            L"-p",
            wideFromUtf8(request.prompt),
            L"--output-format",
            L"json",
            L"--max-turns",
            std::to_wstring((std::max)(1, request.maxTurns)),
            L"--append-system-prompt-file",
            systemPromptFile.wstring()
        };
        if (!provider.modelId.empty()) {
            arguments.push_back(L"--model");
            arguments.push_back(wideFromUtf8(provider.modelId));
        }
        if (!mcpConfigFile.empty()) {
            arguments.push_back(L"--mcp-config");
            arguments.push_back(mcpConfigFile.wstring());
            arguments.push_back(L"--strict-mcp-config");
            arguments.push_back(L"--dangerously-skip-permissions");
            record.toolEvents.push_back("claude_code_cli_mcp");
        }

        std::wstring commandLine;
        if (startsWithInsensitive(claudeCommand->extension().string(), ".cmd") ||
            startsWithInsensitive(claudeCommand->extension().string(), ".bat")) {
            commandLine = L"cmd.exe /d /s /c ";
            commandLine += quoteWindowsArgument(joinCommandArguments(arguments));
        } else {
            commandLine = joinCommandArguments(arguments);
        }

        std::vector<std::pair<std::wstring, std::wstring>> environmentOverrides;
        const std::string apiKey = firstCredentialValue(credentials, { "anthropic_api_key" });
        const std::string authToken = firstCredentialValue(credentials, { "anthropic_auth_token" });
        if (!apiKey.empty()) {
            environmentOverrides.emplace_back(L"ANTHROPIC_API_KEY", wideFromUtf8(apiKey));
        }
        if (!authToken.empty()) {
            environmentOverrides.emplace_back(L"ANTHROPIC_AUTH_TOKEN", wideFromUtf8(authToken));
        }
        if (!provider.baseUrl.empty()) {
            environmentOverrides.emplace_back(L"ANTHROPIC_BASE_URL", wideFromUtf8(provider.baseUrl));
        }
        if (!provider.modelId.empty()) {
            environmentOverrides.emplace_back(L"ANTHROPIC_MODEL", wideFromUtf8(provider.modelId));
        }

        const auto workingDirectory = request.workingDirectory.empty()
            ? std::filesystem::current_path()
            : std::filesystem::path(wideFromUtf8(request.workingDirectory));
        const auto process = runProcessCapture(
            commandLine,
            workingDirectory,
            environmentOverrides,
            currentResourceAllocationProfile(),
            true);
        record.rawResponse = process.stdoutText.empty() ? process.stderrText : process.stdoutText;
        if (!process.launched) {
            record.status = ProviderExecutionStatus::Failed;
            record.errorMessage = trimCopy(process.stderrText.empty() ? process.stdoutText : process.stderrText);
            if (record.errorMessage.empty()) {
                record.errorMessage = "Claude Code could not be launched.";
            }
            record.completedAtUtc = timestampNowUtc();
            return record;
        }
        if (process.exitCode != 0) {
            record.status = ProviderExecutionStatus::Failed;
            record.errorMessage = trimCopy(process.stderrText.empty() ? process.stdoutText : process.stderrText);
            if (record.errorMessage.empty()) {
                record.errorMessage = "Claude Code returned a non-zero exit code.";
            }
            record.completedAtUtc = timestampNowUtc();
            return record;
        }

        const std::string stdoutText = trimCopy(process.stdoutText);
        try {
            const auto payload = nlohmann::json::parse(stdoutText);
            if (payload.contains("result") && payload.at("result").is_string()) {
                record.outputText = payload.at("result").get<std::string>();
            } else if (payload.contains("content") && payload.at("content").is_string()) {
                record.outputText = payload.at("content").get<std::string>();
            } else if (payload.contains("message") && payload.at("message").is_string()) {
                record.outputText = payload.at("message").get<std::string>();
            } else {
                record.outputText = stdoutText;
            }
        } catch (...) {
            record.outputText = stdoutText;
        }
        if (record.outputText.empty()) {
            record.outputText = trimCopy(process.stderrText);
        }
        record.status = ProviderExecutionStatus::Succeeded;
        record.completedAtUtc = timestampNowUtc();
        return record;
    }

    // Codex CLI executor. Mirrors executeClaudeCodeCli: finds the `codex`
    // binary on PATH, invokes `codex exec` with the user's prompt, and
    // captures stdout. No credentials are passed — the Codex CLI uses its
    // own OAuth-stored tokens (populated by the in-shell Sign-In wizard).
    // If the user opted into an API-key fallback, we forward it via env.
    ProviderExecutionRecord executeCodexCli(const ProviderConnection& provider,
                                            const ProviderExecutionRequest& request,
                                            const std::vector<RuntimeEndpoint>& /*endpoints*/,
                                            const std::map<std::string, std::string>& credentials,
                                            ProviderExecutionRecord record) const {
        std::optional<std::filesystem::path> codexCommand;
        const DWORD configuredCommandLength = GetEnvironmentVariableW(L"MASTERCONTROL_CODEX_COMMAND", nullptr, 0);
        if (configuredCommandLength > 0) {
            std::wstring configured(static_cast<size_t>(configuredCommandLength - 1), L'\0');
            GetEnvironmentVariableW(L"MASTERCONTROL_CODEX_COMMAND", configured.data(), configuredCommandLength);
            codexCommand = std::filesystem::path(configured);
        } else {
            codexCommand = findCommandOnPath({ L"codex.exe", L"codex.cmd", L"codex.bat", L"codex" });
        }
        if (!codexCommand.has_value()) {
            record.status = ProviderExecutionStatus::Failed;
            record.errorMessage = "Codex CLI was not found on PATH. Run the Sign in to ChatGPT wizard first.";
            record.completedAtUtc = timestampNowUtc();
            return record;
        }

        std::vector<std::wstring> arguments{
            codexCommand->wstring(),
            L"exec",
            wideFromUtf8(request.prompt)
        };
        if (!provider.modelId.empty()) {
            arguments.push_back(L"--model");
            arguments.push_back(wideFromUtf8(provider.modelId));
        }

        std::wstring commandLine;
        if (startsWithInsensitive(codexCommand->extension().string(), ".cmd") ||
            startsWithInsensitive(codexCommand->extension().string(), ".bat")) {
            commandLine = L"cmd.exe /d /s /c ";
            commandLine += quoteWindowsArgument(joinCommandArguments(arguments));
        } else {
            commandLine = joinCommandArguments(arguments);
        }

        std::vector<std::pair<std::wstring, std::wstring>> environmentOverrides;
        const std::string apiKey = firstCredentialValue(credentials, { "openai_api_key" });
        if (!apiKey.empty()) {
            // Users with an OpenAI API key can opt into bypassing ChatGPT
            // sign-in. When present we forward it; otherwise the Codex CLI
            // uses its own ~/.codex/auth.json OAuth tokens.
            environmentOverrides.emplace_back(L"OPENAI_API_KEY", wideFromUtf8(apiKey));
        }

        const auto workingDirectory = request.workingDirectory.empty()
            ? std::filesystem::current_path()
            : std::filesystem::path(wideFromUtf8(request.workingDirectory));
        const auto process = runProcessCapture(
            commandLine,
            workingDirectory,
            environmentOverrides,
            currentResourceAllocationProfile(),
            true);
        record.rawResponse = process.stdoutText.empty() ? process.stderrText : process.stdoutText;
        if (!process.launched) {
            record.status = ProviderExecutionStatus::Failed;
            record.errorMessage = trimCopy(process.stderrText.empty() ? process.stdoutText : process.stderrText);
            if (record.errorMessage.empty()) {
                record.errorMessage = "Codex CLI could not be launched.";
            }
            record.completedAtUtc = timestampNowUtc();
            return record;
        }
        if (process.exitCode != 0) {
            record.status = ProviderExecutionStatus::Failed;
            record.errorMessage = trimCopy(process.stderrText.empty() ? process.stdoutText : process.stderrText);
            if (record.errorMessage.empty()) {
                record.errorMessage = "Codex CLI returned a non-zero exit code.";
            }
            record.completedAtUtc = timestampNowUtc();
            return record;
        }

        record.outputText = trimCopy(process.stdoutText);
        if (record.outputText.empty()) {
            record.outputText = trimCopy(process.stderrText);
        }
        record.status = ProviderExecutionStatus::Succeeded;
        record.completedAtUtc = timestampNowUtc();
        return record;
    }

    void persistRecord(const ProviderExecutionRecord& record) const {
        std::lock_guard<std::mutex> lock(state_->mutex);
        state_->providerExecutionHistory.insert(state_->providerExecutionHistory.begin(), record);
        if (state_->providerExecutionHistory.size() > 24) {
            state_->providerExecutionHistory.resize(24);
        }
    }

    ResourceAllocationProfile currentResourceAllocationProfile() const {
        std::lock_guard<std::mutex> lock(state_->mutex);
        return state_->configuration.resourceAllocation;
    }

    std::shared_ptr<SharedState> state_;
    std::shared_ptr<IProviderRegistry> providerRegistry_;
    std::shared_ptr<IProviderCredentialStore> providerCredentialStore_;
    std::shared_ptr<IProviderAssignmentService> providerAssignmentService_;
    std::shared_ptr<IRuntimeInventoryService> inventoryService_;
    std::shared_ptr<IProviderExecutionCatalogService> providerExecutionCatalogService_;
    std::shared_ptr<ICommandLogicUnitService> commandLogicUnitService_;
};

class InstallerOrchestrator final : public IInstallerOrchestrator, public IBootstrapRepoService, public IZipBundleService {
public:
    InstallerOrchestrator(std::shared_ptr<SharedState> state,
                          std::shared_ptr<IPackageTrustEvaluator> trustEvaluator,
                          AppPaths paths)
        : state_(std::move(state))
        , trustEvaluator_(std::move(trustEvaluator))
        , paths_(std::move(paths)) {
        if (std::filesystem::exists(paths_.installHistoryFile)) {
            const auto json = readJsonFile(paths_.installHistoryFile);
            if (!json.is_null() && !json.empty()) {
                state_->installHistory = json.get<std::vector<InstallProvenance>>();
            }
        }
    }

    OperationResult installPackage(const InstallerPackageSpec& spec) override;
    OperationResult installFromRepository(const BootstrapRepoSpec& spec) override;
    OperationResult installFromZipBundle(const ZipBundleSpec& spec) override;

    std::vector<InstallProvenance> history() const override {
        std::lock_guard<std::mutex> lock(state_->mutex);
        return state_->installHistory;
    }

private:
    std::optional<ValidatedBootstrapManifest> loadManifestContract(const std::filesystem::path& manifestPath,
                                                                   const std::filesystem::path& rootDirectory,
                                                                   std::string& errorMessage) const;
    void applyManifestRegistration(const BootstrapManifestContract& contract);
    std::filesystem::path resolvePackage(const std::string& source, const std::string& localPath) const;
    int executePackage(const std::filesystem::path& payloadPath,
                       InstallerKind kind,
                       const std::string& arguments) const;
    int executeBootstrap(const std::filesystem::path& bootstrapPath,
                         const std::string& arguments,
                         const std::filesystem::path& workingDirectory) const;
    int executeCommand(const std::wstring& command, const std::filesystem::path& workingDirectory) const;
    void recordHistory(const InstallProvenance& provenance);

    std::shared_ptr<SharedState> state_;
    std::shared_ptr<IPackageTrustEvaluator> trustEvaluator_;
    AppPaths paths_;
};

OperationResult InstallerOrchestrator::installPackage(const InstallerPackageSpec& spec) {
    const auto decision = trustEvaluator_->evaluate(
        !spec.source.empty() ? spec.source : spec.localPath,
        spec.allowUntrustedExecution);
    if (decision.requiresExplicitApproval) {
        return OperationResult{ false, false, decision.reason };
    }

    const auto localPath = resolvePackage(spec.source, spec.localPath);
    if (localPath.empty() || !std::filesystem::exists(localPath)) {
        return OperationResult{ false, false, "Installer payload could not be resolved." };
    }

    const int exitCode = executePackage(localPath, spec.kind, spec.arguments);
    recordHistory(InstallProvenance{
        spec.kind,
        !spec.source.empty() ? spec.source : spec.localPath,
        timestampNowUtc(),
        "1.0.0",
        decision.isTrusted,
        "Process exited with code " + std::to_string(exitCode)
    });

    return OperationResult{
        exitCode == 0,
        false,
        exitCode == 0 ? "Installer completed successfully." : "Installer failed."
    };
}

OperationResult InstallerOrchestrator::installFromRepository(const BootstrapRepoSpec& spec) {
    const auto decision = trustEvaluator_->evaluate(spec.repositoryUrl, spec.allowUntrustedExecution);
    if (decision.requiresExplicitApproval) {
        return OperationResult{ false, false, decision.reason };
    }

    const auto repoDirectory = paths_.workDirectory / ("repo_" + sanitizePathComponent(spec.branch) + "_" + sanitizePathComponent(timestampNowUtc()));
    std::filesystem::create_directories(repoDirectory.parent_path());

    const std::wstring command = L"git clone --depth 1 --branch \"" +
        wideFromUtf8(spec.branch) + L"\" \"" + wideFromUtf8(spec.repositoryUrl) +
        L"\" \"" + repoDirectory.wstring() + L"\"";

    const int cloneExitCode = executeCommand(command, paths_.workDirectory);
    if (cloneExitCode != 0) {
        return OperationResult{ false, false, "Failed to clone bootstrap repository." };
    }

    const auto manifestPath = repoDirectory / spec.manifestFile;
    std::string manifestError;
    const auto manifest = loadManifestContract(manifestPath, repoDirectory, manifestError);
    if (!manifest.has_value()) {
        return OperationResult{ false, false, manifestError };
    }

    const int bootstrapExitCode = executeBootstrap(
        manifest->bootstrapPath,
        manifest->contract.bootstrapArguments,
        manifest->bootstrapPath.parent_path());
    if (bootstrapExitCode == 0) {
        applyManifestRegistration(manifest->contract);
    }

    const auto registeredEndpoints = manifest->contract.seededEndpoints.size();
    const auto registeredProviders = manifest->contract.providers.size();
    std::ostringstream summary;
    summary << "Bootstrap exited with code " << bootstrapExitCode;
    if (bootstrapExitCode == 0 && (registeredEndpoints > 0U || registeredProviders > 0U)) {
        summary << "; registered " << registeredEndpoints << " endpoints and " << registeredProviders << " providers";
    }

    recordHistory(InstallProvenance{
        InstallerKind::GitBootstrapRepo,
        spec.repositoryUrl,
        timestampNowUtc(),
        manifest->contract.version,
        decision.isTrusted,
        summary.str()
    });

    std::ostringstream message;
    message << (bootstrapExitCode == 0 ? "Bootstrap repository installed." : "Bootstrap repository failed.");
    if (bootstrapExitCode == 0 && (registeredEndpoints > 0U || registeredProviders > 0U)) {
        message << " Registered " << registeredEndpoints << " endpoints and " << registeredProviders << " providers.";
    }

    return OperationResult{
        bootstrapExitCode == 0,
        false,
        message.str()
    };
}

OperationResult InstallerOrchestrator::installFromZipBundle(const ZipBundleSpec& spec) {
    const auto decision = trustEvaluator_->evaluate(spec.source, spec.allowUntrustedExecution);
    if (decision.requiresExplicitApproval) {
        return OperationResult{ false, false, decision.reason };
    }

    const auto zipPath = resolvePackage(spec.source, "");
    if (zipPath.empty() || !std::filesystem::exists(zipPath)) {
        return OperationResult{ false, false, "Zip bundle could not be resolved." };
    }

    const auto extractDirectory = paths_.workDirectory / ("zip_" + sanitizePathComponent(timestampNowUtc()));
    std::filesystem::create_directories(extractDirectory);

    const auto powershell = findCommandOnPath({ L"pwsh.exe", L"powershell.exe" });
    if (!powershell.has_value()) {
        return OperationResult{ false, false, "A PowerShell executable could not be located." };
    }

    const auto extractCommand = joinCommandArguments({
        powershell->wstring(),
        L"-NoProfile",
        L"-ExecutionPolicy",
        L"Bypass",
        L"-Command",
        L"Expand-Archive -Path " + quotePowerShellLiteral(zipPath.wstring()) +
            L" -DestinationPath " + quotePowerShellLiteral(extractDirectory.wstring()) + L" -Force"
    });

    const int extractExitCode = executeCommand(extractCommand, paths_.workDirectory);
    if (extractExitCode != 0) {
        return OperationResult{ false, false, "Zip bundle extraction failed." };
    }

    const auto manifestPath = extractDirectory / spec.manifestFile;
    std::string manifestError;
    const auto manifest = loadManifestContract(manifestPath, extractDirectory, manifestError);
    if (!manifest.has_value()) {
        return OperationResult{ false, false, manifestError };
    }

    const int bootstrapExitCode = executeBootstrap(
        manifest->bootstrapPath,
        manifest->contract.bootstrapArguments,
        manifest->bootstrapPath.parent_path());
    if (bootstrapExitCode == 0) {
        applyManifestRegistration(manifest->contract);
    }

    const auto registeredEndpoints = manifest->contract.seededEndpoints.size();
    const auto registeredProviders = manifest->contract.providers.size();
    std::ostringstream summary;
    summary << "Zip bootstrap exited with code " << bootstrapExitCode;
    if (bootstrapExitCode == 0 && (registeredEndpoints > 0U || registeredProviders > 0U)) {
        summary << "; registered " << registeredEndpoints << " endpoints and " << registeredProviders << " providers";
    }

    recordHistory(InstallProvenance{
        InstallerKind::ZipBundle,
        spec.source,
        timestampNowUtc(),
        manifest->contract.version,
        decision.isTrusted,
        summary.str()
    });

    std::ostringstream message;
    message << (bootstrapExitCode == 0 ? "Zip bundle installed." : "Zip bundle bootstrap failed.");
    if (bootstrapExitCode == 0 && (registeredEndpoints > 0U || registeredProviders > 0U)) {
        message << " Registered " << registeredEndpoints << " endpoints and " << registeredProviders << " providers.";
    }

    return OperationResult{
        bootstrapExitCode == 0,
        false,
        message.str()
    };
}

std::optional<ValidatedBootstrapManifest> InstallerOrchestrator::loadManifestContract(
    const std::filesystem::path& manifestPath,
    const std::filesystem::path& rootDirectory,
    std::string& errorMessage) const {
    if (!std::filesystem::exists(manifestPath)) {
        errorMessage = "Bootstrap manifest is missing.";
        return std::nullopt;
    }

    try {
        const auto manifestJson = readJsonFile(manifestPath);
        if (!manifestJson.is_object()) {
            errorMessage = "Bootstrap manifest must be a JSON object.";
            return std::nullopt;
        }

        const auto contract = manifestJson.get<BootstrapManifestContract>();
        if (contract.bootstrapScript.empty()) {
            errorMessage = "Bootstrap manifest must declare bootstrapScript.";
            return std::nullopt;
        }

        const auto bootstrapPath = std::filesystem::weakly_canonical(rootDirectory / contract.bootstrapScript);
        const auto canonicalRoot = std::filesystem::weakly_canonical(rootDirectory);
        if (!pathIsWithinRoot(bootstrapPath, canonicalRoot)) {
            errorMessage = "Bootstrap script must stay within the imported package root.";
            return std::nullopt;
        }

        if (!std::filesystem::exists(bootstrapPath) || !std::filesystem::is_regular_file(bootstrapPath)) {
            errorMessage = "Bootstrap script declared by the manifest was not found.";
            return std::nullopt;
        }

        return ValidatedBootstrapManifest{ contract, bootstrapPath };
    } catch (const std::exception& exception) {
        errorMessage = std::string("Bootstrap manifest could not be parsed: ") + exception.what();
        return std::nullopt;
    }
}

void InstallerOrchestrator::applyManifestRegistration(const BootstrapManifestContract& contract) {
    std::lock_guard<std::mutex> lock(state_->mutex);

    auto& endpoints = state_->configuration.activeProfile.seededEndpoints;
    auto& providers = state_->configuration.providers;
    const auto preferredHost = state_->configuration.activeProfile.preferredBindAddress.empty()
        ? std::string("127.0.0.1")
        : state_->configuration.activeProfile.preferredBindAddress;

    for (auto endpoint : contract.seededEndpoints) {
        if (endpoint.id.empty()) {
            continue;
        }
        if (endpoint.displayName.empty()) {
            endpoint.displayName = endpoint.id;
        }
        if (endpoint.host.empty()) {
            endpoint.host = preferredHost;
        }
        if (endpoint.protocol.empty()) {
            endpoint.protocol = "http";
        }
        if (endpoint.routePath.empty()) {
            endpoint.routePath = "/";
        }

        const auto endpointIterator = std::find_if(
            endpoints.begin(),
            endpoints.end(),
            [&endpoint](const RuntimeEndpoint& candidate) { return candidate.id == endpoint.id; });
        if (endpointIterator == endpoints.end()) {
            endpoints.push_back(std::move(endpoint));
        } else {
            *endpointIterator = std::move(endpoint);
        }
    }

    for (const auto& provider : contract.providers) {
        if (provider.id.empty()) {
            continue;
        }

        const auto providerIterator = std::find_if(
            providers.begin(),
            providers.end(),
            [&provider](const ProviderConnection& candidate) { return candidate.id == provider.id; });
        if (providerIterator == providers.end()) {
            providers.push_back(provider);
        } else {
            *providerIterator = provider;
        }
    }

    (void)writeJsonFile(paths_.configurationFile, state_->configuration);
}

std::filesystem::path InstallerOrchestrator::resolvePackage(const std::string& source, const std::string& localPath) const {
    if (!localPath.empty()) {
        return std::filesystem::path(localPath);
    }

    if (source.empty()) {
        return {};
    }

    if (!isRemoteSource(source)) {
        return std::filesystem::path(source);
    }

    const auto destination = paths_.workDirectory / (sanitizePathComponent(extractHostFromUrl(source)) + "_" + sanitizePathComponent(timestampNowUtc()));
    const auto destinationWithExtension = destination.string() + ".payload";
    const HRESULT result = URLDownloadToFileW(
        nullptr,
        wideFromUtf8(source).c_str(),
        wideFromUtf8(destinationWithExtension).c_str(),
        0,
        nullptr);

    if (FAILED(result)) {
        return {};
    }

    return std::filesystem::path(destinationWithExtension);
}

int InstallerOrchestrator::executePackage(const std::filesystem::path& payloadPath,
                                          const InstallerKind kind,
                                          const std::string& arguments) const {
    std::wstring command;
    switch (kind) {
        case InstallerKind::Msi:
            command = L"msiexec.exe /i \"" + payloadPath.wstring() + L"\" /passive " + wideFromUtf8(arguments);
            break;
        case InstallerKind::Exe:
            command = L"\"" + payloadPath.wstring() + L"\" " + wideFromUtf8(arguments);
            break;
        case InstallerKind::PowerShell: {
            const auto powershell = findCommandOnPath({ L"pwsh.exe", L"powershell.exe" });
            if (!powershell.has_value()) {
                return 1;
            }
            command = L"\"" + powershell->wstring() + L"\" -NoProfile -ExecutionPolicy Bypass -File \"" +
                payloadPath.wstring() + L"\" " + wideFromUtf8(arguments);
            break;
        }
        default:
            return 1;
    }

    return executeCommand(command, payloadPath.parent_path());
}

int InstallerOrchestrator::executeBootstrap(const std::filesystem::path& bootstrapPath,
                                            const std::string& arguments,
                                            const std::filesystem::path& workingDirectory) const {
    if (!std::filesystem::exists(bootstrapPath)) {
        return 1;
    }

    std::wstring command;
    if (bootstrapPath.extension() == ".ps1") {
        const auto powershell = findCommandOnPath({ L"pwsh.exe", L"powershell.exe" });
        if (!powershell.has_value()) {
            return 1;
        }
        command = L"\"" + powershell->wstring() + L"\" -NoProfile -ExecutionPolicy Bypass -File \"" +
            bootstrapPath.wstring() + L"\" " + wideFromUtf8(arguments);
    } else {
        command = L"\"" + bootstrapPath.wstring() + L"\" " + wideFromUtf8(arguments);
    }

    return executeCommand(command, workingDirectory);
}

int InstallerOrchestrator::executeCommand(const std::wstring& command, const std::filesystem::path& workingDirectory) const {
    ResourceAllocationProfile allocationProfile;
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        allocationProfile = state_->configuration.resourceAllocation;
    }

    const auto process = runProcessCapture(command, workingDirectory, {}, allocationProfile);
    if (!process.launched) {
        return process.exitCode >= 0 ? process.exitCode : 1;
    }
    return process.exitCode;
}

void InstallerOrchestrator::recordHistory(const InstallProvenance& provenance) {
    std::lock_guard<std::mutex> lock(state_->mutex);
    state_->installHistory.push_back(provenance);
    (void)writeJsonFile(paths_.installHistoryFile, state_->installHistory);
}

class ExportService final : public IExportService {
public:
    ExportService(std::shared_ptr<IRuntimeInventoryService> inventoryService,
                  std::shared_ptr<IConfigurationService> configurationService)
        : inventoryService_(std::move(inventoryService))
        , configurationService_(std::move(configurationService)) {}

    std::vector<ExportArtifact> generateExports() const override {
        const auto endpoints = inventoryService_->listEndpoints();
        const auto configuration = configurationService_->current();
        const auto iterator = std::find_if(
            endpoints.begin(),
            endpoints.end(),
            [](const RuntimeEndpoint& endpoint) {
                // Accept the retired aggregator-gateway id so older exported profiles still resolve.
                return endpoint.id == "platform-gateway" || endpoint.id == "aggregator-gateway" || endpoint.kind == EndpointKind::Gateway;
            });
        const auto browserIterator = std::find_if(
            endpoints.begin(),
            endpoints.end(),
            [](const RuntimeEndpoint& endpoint) {
                return endpoint.id == "browser-gateway" || endpoint.kind == EndpointKind::BrowserGateway;
            });

        const auto preferredHost = configuration.activeProfile.preferredBindAddress.empty()
            ? std::string("127.0.0.1")
            : configuration.activeProfile.preferredBindAddress;
        const auto gatewayHost = iterator != endpoints.end() && iterator->host != "0.0.0.0" ? iterator->host : preferredHost;
        const auto gatewayPort = iterator != endpoints.end() ? iterator->port : static_cast<uint16_t>(7200);
        const auto gatewayUrl = "http://" + gatewayHost + ":" + std::to_string(gatewayPort) + "/mcp/gateway";
        const auto browserHost = browserIterator != endpoints.end() && browserIterator->host != "0.0.0.0"
            ? browserIterator->host
            : preferredHost;
        const auto browserPort = browserIterator != endpoints.end() ? browserIterator->port : configuration.browserPort;
        const auto browserUrl = "http://" + browserHost + ":" + std::to_string(browserPort) + "/";

        nlohmann::json claudeJson = {
            { "mcpServers", {
                { "master-control-gateway", {
                    { "type", "http" },
                    { "url", gatewayUrl }
                } }
            } }
        };

        nlohmann::json codexJson = {
            { "name", "master-control-gateway" },
            { "transport", "http" },
            { "url", gatewayUrl }
        };

        nlohmann::json openAiJson = {
            { "gateway", gatewayUrl },
            { "provider", "openai" },
            { "recommendedModel", "gpt-5" }
        };

        nlohmann::json xAiJson = {
            { "gateway", gatewayUrl },
            { "provider", "xai" },
            { "recommendedModel", "grok-code" }
        };

        nlohmann::json gatewayProfile = {
            { "instanceName", configuration.instanceName },
            { "environmentName", configuration.activeProfile.environmentName },
            { "browserUrl", browserUrl },
            { "gatewayUrl", gatewayUrl },
            { "beaconPort", configuration.beaconPort },
            { "exportedAtUtc", timestampNowUtc() },
            { "endpoints", endpoints }
        };

        std::ostringstream claudeScript;
        claudeScript
            << "param(\n"
            << "  [string]$ConfigPath = (Join-Path $HOME '.claude.json')\n"
            << ")\n\n"
            << "$gatewayUrl = '" << gatewayUrl << "'\n"
            << "$config = @{}\n"
            << "if (Test-Path $ConfigPath) {\n"
            << "  $config = Get-Content $ConfigPath -Raw | ConvertFrom-Json -AsHashtable\n"
            << "}\n"
            << "if (-not $config) { $config = @{} }\n"
            << "if (-not $config.ContainsKey('mcpServers')) { $config['mcpServers'] = @{} }\n"
            << "$config['mcpServers']['master-control-gateway'] = @{ type = 'http'; url = $gatewayUrl }\n"
            << "$config | ConvertTo-Json -Depth 8 | Set-Content -Path $ConfigPath -Encoding UTF8\n"
            << "Write-Host \"Updated Claude Code MCP config at $ConfigPath\"\n";

        std::ostringstream codexScript;
        codexScript
            << "param(\n"
            << "  [string]$OutputPath = (Join-Path $PWD 'codex-mcp.json')\n"
            << ")\n\n"
            << "$payload = @'\n"
            << codexJson.dump(2) << '\n'
            << "'@\n"
            << "$payload | Set-Content -Path $OutputPath -Encoding UTF8\n"
            << "Write-Host \"Wrote Codex MCP config to $OutputPath\"\n";

        return {
            ExportArtifact{ "gateway-profile", "master-control-gateway-profile.json", "application/json", gatewayProfile.dump(2) },
            ExportArtifact{ "claude", ".claude.json", "application/json", claudeJson.dump(2) },
            ExportArtifact{ "claude-installer", "Install-ClaudeGateway.ps1", "text/plain", claudeScript.str() },
            ExportArtifact{ "codex", "codex-mcp.json", "application/json", codexJson.dump(2) },
            ExportArtifact{ "codex-installer", "Install-CodexGateway.ps1", "text/plain", codexScript.str() },
            ExportArtifact{ "openai", "openai-gateway.json", "application/json", openAiJson.dump(2) },
            ExportArtifact{ "xai", "xai-gateway.json", "application/json", xAiJson.dump(2) }
        };
    }

private:
    std::shared_ptr<IRuntimeInventoryService> inventoryService_;
    std::shared_ptr<IConfigurationService> configurationService_;
};

class CommandLogicUnitService final : public ICommandLogicUnitService {
public:
    CommandLogicUnitService(std::filesystem::path profileFile,
                            std::shared_ptr<IConfigurationService> configurationService,
                            std::shared_ptr<IProviderRegistry> providerRegistry,
                            std::shared_ptr<IProviderAssignmentService> providerAssignmentService,
                            std::shared_ptr<IInstallerOrchestrator> installerOrchestrator,
                            std::shared_ptr<IExportService> exportService,
                            std::shared_ptr<IAppleRemoteHostService> appleRemoteHostService,
                            std::shared_ptr<IPlatformServiceCatalogService> platformServiceCatalogService,
                            std::shared_ptr<IPlatformGovernanceToolService> platformGovernanceToolService)
        : profile_(loadProfile(std::move(profileFile)))
        , configurationService_(std::move(configurationService))
        , providerRegistry_(std::move(providerRegistry))
        , providerAssignmentService_(std::move(providerAssignmentService))
        , installerOrchestrator_(std::move(installerOrchestrator))
        , exportService_(std::move(exportService))
        , appleRemoteHostService_(std::move(appleRemoteHostService))
        , platformServiceCatalogService_(std::move(platformServiceCatalogService))
        , platformGovernanceToolService_(std::move(platformGovernanceToolService)) {}

    GovernanceSnapshot currentGovernance() const override {
        GovernanceSnapshot snapshot;
        snapshot.unitName = profile_.unitName.empty() ? std::string("Command Logic Unit") : profile_.unitName;
        snapshot.posture = "pass";
        snapshot.doctrine = profile_.doctrine;
        snapshot.lastEvaluatedUtc = timestampNowUtc();
        snapshot.documents = profile_.documents;
        snapshot.roles = profile_.roles;
        snapshot.rules = profile_.rules;
        snapshot.operatorChecklist = profile_.operatorChecklist;

        const auto ensureRule = [&snapshot](GovernanceRule rule) {
            const auto iterator = std::find_if(
                snapshot.rules.begin(),
                snapshot.rules.end(),
                [&rule](const GovernanceRule& candidate) { return candidate.ruleId == rule.ruleId; });
            if (iterator == snapshot.rules.end()) {
                snapshot.rules.push_back(std::move(rule));
            }
        };
        ensureRule(GovernanceRule{
            "CLU-C008",
            "Managed Resource Envelope",
            "high",
            "Governed work is blocked when the enforced CPU, memory, bandwidth, or storage envelope is denied.",
            "CLU preflights provider execution and managed install actions against the configured local CPU, memory, bandwidth, and storage envelope before spawning governed workloads."
        });

        const auto configuration = configurationService_->current();
        const auto providers = providerRegistry_->listProviders();
        const auto assignments = providerAssignmentService_->listAssignments();
        const auto installHistory = installerOrchestrator_->history();
        const auto exports = exportService_->generateExports();
        snapshot.appleRemoteHosts = appleRemoteHostService_
            ? appleRemoteHostService_->listHosts()
            : std::vector<AppleRemoteHost>{};
        snapshot.platformGateways = platformServiceCatalogService_ ? platformServiceCatalogService_->listGateways() : std::vector<PlatformGatewayDescriptor>{};
        snapshot.governanceServers = platformServiceCatalogService_
            ? platformServiceCatalogService_->listGovernanceServers()
            : std::vector<GovernanceServerDescriptor>{};
        snapshot.availableTools = platformGovernanceToolService_
            ? platformGovernanceToolService_->listTools()
            : std::vector<GovernanceToolDescriptor>{};
        snapshot.recentExecutions = platformGovernanceToolService_
            ? platformGovernanceToolService_->recentExecutions()
            : std::vector<GovernanceToolResult>{};
        snapshot.appleOperations = platformGovernanceToolService_
            ? platformGovernanceToolService_->recentAppleOperations()
            : std::vector<AppleOperationRecord>{};

        auto appendFinding = [&](const std::string& ruleId,
                                 const std::string& status,
                                 const std::string& message,
                                 std::optional<std::string> severityOverride = std::nullopt) {
            GovernanceFinding finding;
            if (const auto* rule = findRule(ruleId); rule != nullptr) {
                finding.ruleId = rule->ruleId;
                finding.title = rule->title;
                finding.severity = severityOverride.value_or(rule->severity);
            } else {
                finding.ruleId = ruleId;
                finding.title = ruleId;
                finding.severity = severityOverride.value_or(std::string("medium"));
            }
            finding.status = status;
            finding.message = message;
            snapshot.findings.push_back(std::move(finding));

            if (status == "blocked") {
                snapshot.posture = "blocked";
            } else if (status == "warning" && snapshot.posture != "blocked") {
                snapshot.posture = "warning";
            }
        };

        const auto autonomousProviderCount = static_cast<size_t>(std::count_if(
            providers.begin(),
            providers.end(),
            [](const ProviderConnection& provider) {
                return provider.enabled && provider.allowAutonomousControl;
            }));

        if (configuration.resourceAllocation.cpuPercent <= 0) {
            appendFinding(
                "CLU-C008",
                "blocked",
                "Managed execution is blocked because CPU allocation is set to 0%. Raise the CPU envelope before running governed providers or installs.");
            snapshot.recommendedActions.push_back("Increase CPU allocation above 0% to re-enable CLU-governed provider execution and managed installs.");
        }

        if (configuration.resourceAllocation.memoryPercent <= 0) {
            appendFinding(
                "CLU-C008",
                "blocked",
                "Managed execution is blocked because memory allocation is set to 0%. Raise the memory envelope before running governed providers or installs.");
            snapshot.recommendedActions.push_back("Increase memory allocation above 0% to re-enable CLU-governed provider execution and managed installs.");
        }

        if (configuration.resourceAllocation.bandwidthPercent <= 0) {
            appendFinding(
                "CLU-C008",
                "blocked",
                "Governed network execution is blocked because bandwidth allocation is set to 0%. Raise the bandwidth envelope before running provider routes or remote installs.");
            snapshot.recommendedActions.push_back("Increase bandwidth allocation above 0% to re-enable governed provider routes and remote installs.");
        }

        if (configuration.resourceAllocation.storagePercent <= 0) {
            appendFinding(
                "CLU-C008",
                "blocked",
                "Managed execution is blocked because storage allocation is set to 0%. Raise the storage envelope before running governed providers or installs.");
            snapshot.recommendedActions.push_back("Increase storage allocation above 0% to re-enable CLU-governed provider execution and managed installs.");
        }

        if (!configuration.security.securityProtocolsEnabled && configuration.security.allowOpenLanAccess) {
            appendFinding(
                "CLU-C002",
                "blocked",
                "Security protocols are disabled while browser access remains open on the LAN. Restore protocols or close LAN exposure.");
            snapshot.recommendedActions.push_back("Re-enable security protocols or disable open LAN access before continuing unattended operations.");
        }

        if (configuration.security.allowTroubleshootingBypass) {
            appendFinding(
                "CLU-C003",
                "warning",
                "Troubleshooting bypass is enabled. CLU is treating the runtime as temporarily degraded until bypass mode is cleared.");
            snapshot.recommendedActions.push_back("Turn off troubleshooting bypass after diagnostics are complete.");
        }

        if (configuration.aiAutonomyEnabled && autonomousProviderCount == 0U) {
            appendFinding(
                "CLU-C001",
                "warning",
                "Global AI autonomy is enabled, but no provider route currently allows autonomous control.");
            snapshot.recommendedActions.push_back("Either disable global AI autonomy or explicitly authorize at least one provider for autonomous control.");
        }

        if (!configuration.aiAutonomyEnabled && autonomousProviderCount > 0U) {
            appendFinding(
                "CLU-C004",
                "warning",
                "One or more provider routes advertise autonomous control while global dashboard autonomy remains disabled.");
            snapshot.recommendedActions.push_back("Align provider autonomy flags with the global AI autonomy posture.");
        }

        if (std::none_of(providers.begin(), providers.end(), [](const ProviderConnection& provider) { return provider.enabled; })) {
            appendFinding(
                "CLU-C004",
                "warning",
                "No enabled provider routes are available for governed agent operations.");
            snapshot.recommendedActions.push_back("Enable at least one provider route before expecting CLU-managed agent workflows to run.");
        }

        if (!providers.empty() && assignments.empty()) {
            appendFinding(
                "CLU-C004",
                "warning",
                "Provider routes are configured, but no orchestration roles or sub-agents have been assigned to a provider owner yet.");
            snapshot.recommendedActions.push_back("Assign planner, architect, or sub-agent ownership before expecting governed provider execution.");
        }

        if (std::any_of(installHistory.begin(), installHistory.end(), [](const InstallProvenance& entry) { return !entry.trusted; })) {
            appendFinding(
                "CLU-C005",
                "warning",
                "One or more imported packages were recorded as untrusted. CLU is surfacing the provenance risk for operator review.");
            snapshot.recommendedActions.push_back("Review untrusted install history entries and replace or remove them if they are no longer needed.");
        }

        const bool hasGatewayProfile = std::any_of(
            exports.begin(),
            exports.end(),
            [](const ExportArtifact& artifact) { return artifact.fileName == "master-control-gateway-profile.json"; });
        const bool hasCodexHelper = std::any_of(
            exports.begin(),
            exports.end(),
            [](const ExportArtifact& artifact) { return artifact.fileName == "Install-CodexGateway.ps1"; });
        const bool hasClaudeHelper = std::any_of(
            exports.begin(),
            exports.end(),
            [](const ExportArtifact& artifact) { return artifact.fileName == "Install-ClaudeGateway.ps1"; });

        if (!(hasGatewayProfile && hasCodexHelper && hasClaudeHelper)) {
            appendFinding(
                "CLU-C006",
                "warning",
                "One or more expected governance and agent handoff artifacts are missing from the current export set.");
            snapshot.recommendedActions.push_back("Refresh exports and verify the gateway, Codex, and Claude handoff artifacts are still being generated.");
        }

        for (const auto platform : { PlatformTarget::Windows, PlatformTarget::MacOS, PlatformTarget::IOS }) {
            const auto platformName = platformKey(platform);
            const bool hasGateway = std::any_of(
                snapshot.platformGateways.begin(),
                snapshot.platformGateways.end(),
                [platform](const PlatformGatewayDescriptor& descriptor) {
                    return descriptor.platform == platform && descriptor.lanAdvertisementEnabled;
                });
            const bool hasGovernanceServer = std::any_of(
                snapshot.governanceServers.begin(),
                snapshot.governanceServers.end(),
                [platform](const GovernanceServerDescriptor& descriptor) {
                    return descriptor.platform == platform;
                });

            if (!hasGateway || !hasGovernanceServer) {
                appendFinding(
                    "CLU-C006",
                    "warning",
                    "The " + platformName + " governance lane is incomplete. Both the platform gateway module and governance MCP server module must be active.");
                snapshot.recommendedActions.push_back(
                    "Activate the " + platformName + " gateway and governance MCP server modules before advertising that lane to LAN clients.");
            }
        }

        if (std::any_of(
                snapshot.recentExecutions.begin(),
                snapshot.recentExecutions.end(),
                [](const GovernanceToolResult& result) { return result.status == GovernanceToolStatus::Failed; })) {
            appendFinding(
                "CLU-C007",
                "warning",
                "Recent governance tool execution failures were detected. Review the affected enforcement lanes before relying on autonomous governance.");
            snapshot.recommendedActions.push_back("Review recent governance tool failures and repair the affected platform governance lane.");
        }

        if (snapshot.findings.empty()) {
            snapshot.recommendedActions.push_back("Current runtime posture is aligned with the CLU governance profile.");
        }

        return snapshot;
    }

    GovernanceEnforcementDecision enforceAction(const GovernanceEnforcementRequest& request) const override {
        GovernanceEnforcementDecision decision;
        decision.action = request.action;
        decision.allowed = true;

        const auto configuration = configurationService_->current();
        const auto snapshot = currentGovernance();
        decision.posture = snapshot.posture;

        const auto blockedFindings = [&snapshot]() {
            std::vector<GovernanceFinding> findings;
            for (const auto& finding : snapshot.findings) {
                if (finding.status == "blocked") {
                    findings.push_back(finding);
                }
            }
            return findings;
        }();

        const auto appendBlockedFindingMessages = [&decision, &blockedFindings]() {
            for (const auto& finding : blockedFindings) {
                decision.blockingFindings.push_back(finding.title + ": " + finding.message);
            }
            if (!blockedFindings.empty()) {
                decision.ruleId = blockedFindings.front().ruleId;
            }
        };
        const auto resourcePreflightBlock = [&configuration, &decision](const std::string& actionLabel,
                                                                       bool requiresNetwork) -> bool {
            auto resourceMessage = std::string{};
            if (configuration.resourceAllocation.cpuPercent <= 0) {
                resourceMessage = "The managed resource envelope denies CPU allocation (0%).";
            } else if (configuration.resourceAllocation.memoryPercent <= 0) {
                resourceMessage = "The managed resource envelope denies memory allocation (0%).";
            } else if (configuration.resourceAllocation.storagePercent <= 0) {
                resourceMessage = "The managed resource envelope denies storage allocation (0%).";
            } else if (requiresNetwork && configuration.resourceAllocation.bandwidthPercent <= 0) {
                resourceMessage = "The managed resource envelope denies bandwidth allocation (0%) for network-governed work.";
            }

            if (resourceMessage.empty()) {
                return false;
            }

            decision.allowed = false;
            decision.ruleId = "CLU-C008";
            decision.blockingFindings.push_back(resourceMessage);
            decision.message = "CLU blocked " + actionLabel + " because the managed resource policy denies launch. " + resourceMessage;
            return true;
        };

        if (request.action == GovernanceActionKind::ProviderExecution) {
            if (resourcePreflightBlock("provider execution", true)) {
                return decision;
            }
            if (snapshot.posture == "blocked") {
                decision.allowed = false;
                appendBlockedFindingMessages();
                decision.message = "CLU blocked provider execution because runtime posture is blocked.";
                if (!decision.blockingFindings.empty()) {
                    decision.message += " " + decision.blockingFindings.front();
                }
            }
            return decision;
        }

        if (request.action == GovernanceActionKind::ProviderAutonomyEnable) {
            if (snapshot.posture == "blocked") {
                decision.allowed = false;
                appendBlockedFindingMessages();
                decision.message = "CLU blocked provider autonomous control while runtime posture is blocked.";
                if (!decision.blockingFindings.empty()) {
                    decision.message += " " + decision.blockingFindings.front();
                }
                return decision;
            }

            if (!configuration.aiAutonomyEnabled) {
                decision.allowed = false;
                decision.ruleId = "CLU-C004";
                decision.message = "Enable global AI autonomy before granting provider autonomous control.";
            }
            return decision;
        }

        if (request.action == GovernanceActionKind::RemoteInstall) {
            if (resourcePreflightBlock("managed install", isRemoteSource(request.source))) {
                return decision;
            }
            if (isRemoteSource(request.source) && snapshot.posture == "blocked") {
                decision.allowed = false;
                appendBlockedFindingMessages();
                decision.message = "CLU blocked remote install while runtime posture is blocked.";
                if (!decision.blockingFindings.empty()) {
                    decision.message += " " + decision.blockingFindings.front();
                }
            }
            return decision;
        }

        return decision;
    }

    GovernanceToolResult executeGovernanceTool(const GovernanceToolRequest& request) override {
        GovernanceToolResult result;
        result.platform = request.platform;
        result.toolId = request.toolId;
        result.startedAtUtc = timestampNowUtc();

        if (request.platform == PlatformTarget::Unknown) {
            result.status = GovernanceToolStatus::Failed;
            result.summary = "Governance tool execution requires a target platform.";
            result.completedAtUtc = timestampNowUtc();
            return result;
        }

        const auto governanceServers = platformServiceCatalogService_
            ? platformServiceCatalogService_->listGovernanceServers()
            : std::vector<GovernanceServerDescriptor>{};
        const auto serverIterator = std::find_if(
            governanceServers.begin(),
            governanceServers.end(),
            [&request](const GovernanceServerDescriptor& descriptor) { return descriptor.platform == request.platform; });
        if (serverIterator == governanceServers.end()) {
            result.status = GovernanceToolStatus::Failed;
            result.summary = "No governance server lane is active for platform " + platformKey(request.platform) + ".";
            result.completedAtUtc = timestampNowUtc();
            return result;
        }

        if (std::find(serverIterator->toolIds.begin(), serverIterator->toolIds.end(), request.toolId) == serverIterator->toolIds.end()) {
            result.status = GovernanceToolStatus::Failed;
            result.summary = "Governance tool '" + request.toolId + "' is not published by the active " +
                serverIterator->displayName + " lane.";
            result.completedAtUtc = timestampNowUtc();
            return result;
        }

        if (!platformGovernanceToolService_) {
            result.status = GovernanceToolStatus::Failed;
            result.summary = "Governance tool service is unavailable.";
            result.completedAtUtc = timestampNowUtc();
            return result;
        }

        return platformGovernanceToolService_->execute(request);
    }

    OperationResult cancelAppleOperation(const std::string& operationId) override {
        if (trimCopy(operationId).empty()) {
            return OperationResult{ false, false, "Apple operation cancellation requires an operationId." };
        }
        if (!platformGovernanceToolService_) {
            return OperationResult{ false, false, "Governance tool service is unavailable." };
        }
        return platformGovernanceToolService_->cancelAppleOperation(operationId);
    }

private:
    static GovernanceProfile loadProfile(std::filesystem::path profileFile) {
        if (!profileFile.empty() && std::filesystem::exists(profileFile)) {
            const auto json = readJsonFile(profileFile);
            if (!json.is_null() && !json.empty()) {
                try {
                    return json.get<GovernanceProfile>();
                } catch (...) {
                }
            }
        }
        return buildFallbackGovernanceProfile();
    }

    const GovernanceRule* findRule(const std::string& ruleId) const {
        const auto iterator = std::find_if(
            profile_.rules.begin(),
            profile_.rules.end(),
            [&ruleId](const GovernanceRule& rule) { return rule.ruleId == ruleId; });
        return iterator == profile_.rules.end() ? nullptr : &(*iterator);
    }

    GovernanceProfile profile_;
    std::shared_ptr<IConfigurationService> configurationService_;
    std::shared_ptr<IProviderRegistry> providerRegistry_;
    std::shared_ptr<IProviderAssignmentService> providerAssignmentService_;
    std::shared_ptr<IInstallerOrchestrator> installerOrchestrator_;
    std::shared_ptr<IExportService> exportService_;
    std::shared_ptr<IAppleRemoteHostService> appleRemoteHostService_;
    std::shared_ptr<IPlatformServiceCatalogService> platformServiceCatalogService_;
    std::shared_ptr<IPlatformGovernanceToolService> platformGovernanceToolService_;
};

class ForsettiSurfaceService final : public IForsettiSurfaceService {
public:
    ForsettiSurfaceService(std::shared_ptr<Forsetti::UISurfaceManager> surfaceManager,
                           std::shared_ptr<IModuleControlSurfaceService> controlSurfaceService)
        : surfaceManager_(std::move(surfaceManager))
        , controlSurfaceService_(std::move(controlSurfaceService)) {}

    ForsettiSurfaceSnapshot currentSurface() const override {
        if (!surfaceManager_) {
            return {};
        }

        std::lock_guard<std::mutex> lock(mutex_);
        return ForsettiSurfaceSnapshot{
            surfaceManager_->currentThemeMask(),
            surfaceManager_->currentToolbarItems(),
            surfaceManager_->currentViewInjectionsBySlot(),
            surfaceManager_->currentOverlaySchema(),
            controlSurfaceService_ ? controlSurfaceService_->listControlSurfaceRequests() : std::vector<ModuleControlSurfaceRequest>{},
            publishedByModuleId_,
            publishedAtUtc_
        };
    }

    void publishModuleSurface(const std::string& moduleId,
                              const Forsetti::UIContributions& contributions) override {
        if (!surfaceManager_) {
            return;
        }

        surfaceManager_->addModuleContributions(moduleId, contributions);
        surfaceManager_->rebuildSurfaceState();

        std::lock_guard<std::mutex> lock(mutex_);
        publishedByModuleId_ = moduleId;
        publishedAtUtc_ = timestampNowUtc();
    }

    void removeModuleSurface(const std::string& moduleId) override {
        if (!surfaceManager_) {
            return;
        }

        surfaceManager_->removeModuleContributions(moduleId);
        surfaceManager_->rebuildSurfaceState();

        std::lock_guard<std::mutex> lock(mutex_);
        if (publishedByModuleId_ == moduleId) {
            publishedByModuleId_.clear();
            publishedAtUtc_.clear();
        }
    }

private:
    std::shared_ptr<Forsetti::UISurfaceManager> surfaceManager_;
    std::shared_ptr<IModuleControlSurfaceService> controlSurfaceService_;
    mutable std::mutex mutex_;
    std::string publishedByModuleId_;
    std::string publishedAtUtc_;
};

class ModuleControlSurfaceService final : public IModuleControlSurfaceService {
public:
    void upsertControlSurfaceRequest(const ModuleControlSurfaceRequest& request) override {
        std::lock_guard<std::mutex> lock(mutex_);
        requestsByKey_[makeKey(request.moduleId, request.featureId)] = request;
    }

    void removeControlSurfaceRequest(const std::string& moduleId,
                                     const std::string& featureId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        requestsByKey_.erase(makeKey(moduleId, featureId));
    }

    void removeControlSurfaceRequestsForModule(const std::string& moduleId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto iterator = requestsByKey_.begin(); iterator != requestsByKey_.end();) {
            if (iterator->second.moduleId == moduleId) {
                iterator = requestsByKey_.erase(iterator);
            } else {
                ++iterator;
            }
        }
    }

    std::vector<ModuleControlSurfaceRequest> listControlSurfaceRequests() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<ModuleControlSurfaceRequest> requests;
        requests.reserve(requestsByKey_.size());
        for (const auto& [key, request] : requestsByKey_) {
            requests.push_back(request);
        }
        return requests;
    }

private:
    static std::string makeKey(const std::string& moduleId, const std::string& featureId) {
        return moduleId + "::" + featureId;
    }

    mutable std::mutex mutex_;
    std::map<std::string, ModuleControlSurfaceRequest> requestsByKey_;
};

class PlatformServiceCatalogService final : public IPlatformServiceCatalogService {
public:
    PlatformServiceCatalogService(std::shared_ptr<IConfigurationService> configurationService,
                                  std::shared_ptr<ITelemetryService> telemetryService)
        : configurationService_(std::move(configurationService))
        , telemetryService_(std::move(telemetryService)) {}

    ~PlatformServiceCatalogService() override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [key, registration] : gatewaysByModuleId_) {
            deregisterGatewayLocked(registration);
        }
    }

    void upsertGateway(const PlatformGatewayDescriptor& descriptor) override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto normalized = normalizeGatewayDescriptor(descriptor);
        auto& registration = gatewaysByModuleId_[normalized.moduleId];
        deregisterGatewayLocked(registration);
        registration.descriptor = std::move(normalized);
        registerGatewayLocked(registration);
    }

    void removeGateway(const std::string& moduleId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto iterator = gatewaysByModuleId_.find(moduleId);
        if (iterator == gatewaysByModuleId_.end()) {
            return;
        }
        deregisterGatewayLocked(iterator->second);
        gatewaysByModuleId_.erase(iterator);
    }

    std::vector<PlatformGatewayDescriptor> listGateways() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<PlatformGatewayDescriptor> descriptors;
        descriptors.reserve(gatewaysByModuleId_.size());
        for (const auto& [key, registration] : gatewaysByModuleId_) {
            descriptors.push_back(registration.descriptor);
        }
        std::sort(
            descriptors.begin(),
            descriptors.end(),
            [](const PlatformGatewayDescriptor& left, const PlatformGatewayDescriptor& right) {
                return std::tie(left.platform, left.displayName, left.serviceId) <
                    std::tie(right.platform, right.displayName, right.serviceId);
            });
        return descriptors;
    }

    void upsertGovernanceServer(const GovernanceServerDescriptor& descriptor) override {
        std::lock_guard<std::mutex> lock(mutex_);
        governanceByModuleId_[descriptor.moduleId] = descriptor;
    }

    void removeGovernanceServer(const std::string& moduleId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        governanceByModuleId_.erase(moduleId);
    }

    std::vector<GovernanceServerDescriptor> listGovernanceServers() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<GovernanceServerDescriptor> descriptors;
        descriptors.reserve(governanceByModuleId_.size());
        for (const auto& [key, descriptor] : governanceByModuleId_) {
            descriptors.push_back(descriptor);
        }
        std::sort(
            descriptors.begin(),
            descriptors.end(),
            [](const GovernanceServerDescriptor& left, const GovernanceServerDescriptor& right) {
                return std::tie(left.platform, left.displayName, left.serviceId) <
                    std::tie(right.platform, right.displayName, right.serviceId);
            });
        return descriptors;
    }

private:
    struct GatewayRegistration final {
        PlatformGatewayDescriptor descriptor;
        DNS_SERVICE_REGISTER_REQUEST request{};
        PDNS_SERVICE_INSTANCE instance = nullptr;
        bool registered = false;
    };

    PlatformGatewayDescriptor normalizeGatewayDescriptor(PlatformGatewayDescriptor descriptor) const {
        const auto configuration = configurationService_->current();
        const auto snapshot = telemetryService_->captureSnapshot();

        if (descriptor.displayName.empty()) {
            descriptor.displayName = "Platform Gateway";
        }
        if (descriptor.instanceLabel.empty()) {
            descriptor.instanceLabel = configuration.instanceName + " " + descriptor.displayName;
        }
        if (descriptor.hostName.empty()) {
            descriptor.hostName = snapshot.hostName;
        }
        descriptor.hostName = dotLocalHostName(descriptor.hostName);

        if (descriptor.ipAddress.empty() || descriptor.ipAddress == "0.0.0.0") {
            descriptor.ipAddress = snapshot.primaryIpAddress.empty() ? configuration.bindAddress : snapshot.primaryIpAddress;
        }
        if (descriptor.ipAddress.empty() || descriptor.ipAddress == "0.0.0.0") {
            descriptor.ipAddress = "127.0.0.1";
        }
        if (descriptor.port == 0) {
            descriptor.port = configuration.browserPort;
        }
        if (descriptor.gatewayPath.empty()) {
            descriptor.gatewayPath = platformGatewayRoutePath(descriptor.platform);
        }
        if (descriptor.configPath.empty()) {
            descriptor.configPath = platformGatewayConfigPath(descriptor.platform);
        }
        descriptor.properties["platform"] = platformKey(descriptor.platform);
        descriptor.properties["config_path"] = descriptor.configPath;
        descriptor.properties["gateway_path"] = descriptor.gatewayPath;
        descriptor.properties["service_id"] = descriptor.serviceId;
        if (descriptor.status.empty() || descriptor.status == "starting") {
            descriptor.status = "configured";
        }
        return descriptor;
    }

    void registerGatewayLocked(GatewayRegistration& registration) {
        const auto& descriptor = registration.descriptor;
        if (!descriptor.lanAdvertisementEnabled || descriptor.serviceType.empty() || descriptor.port == 0) {
            registration.descriptor.status = "disabled";
            return;
        }

        std::vector<std::wstring> keysWide;
        std::vector<std::wstring> valuesWide;
        keysWide.reserve(descriptor.properties.size());
        valuesWide.reserve(descriptor.properties.size());
        for (const auto& [key, value] : descriptor.properties) {
            keysWide.push_back(wideFromUtf8(key));
            valuesWide.push_back(wideFromUtf8(value));
        }

        std::vector<PCWSTR> keyPointers;
        std::vector<PCWSTR> valuePointers;
        keyPointers.reserve(keysWide.size());
        valuePointers.reserve(valuesWide.size());
        for (size_t index = 0; index < keysWide.size(); ++index) {
            keyPointers.push_back(keysWide[index].c_str());
            valuePointers.push_back(valuesWide[index].c_str());
        }

        const auto instanceName = wideFromUtf8(descriptor.instanceLabel + "." + descriptor.serviceType);
        const auto hostName = wideFromUtf8(descriptor.hostName);

        // Parse the descriptor's IP as either IPv4 or IPv6. DnsServiceConstructInstance
        // accepts one or both; at least one must be non-null or the call returns NULL
        // and we flip to "registration_failed" with no DNS activity. Previously only
        // AF_INET was tried, which silently broke gateway advertisement on
        // IPv6-only hosts or hosts where SharedTelemetry::readPrimaryNetworkIdentity
        // picked the IPv6 ULA as the primary address (observed: all three
        // Windows/macOS/iOS gateway lanes stuck at "registration_failed" on a
        // machine with a working IPv6 primary interface).
        IP4_ADDRESS ipv4Address = 0;
        PIP4_ADDRESS ipv4Pointer = nullptr;
        IN_ADDR parsedV4{};
        IP6_ADDRESS ipv6Address{};
        PIP6_ADDRESS ipv6Pointer = nullptr;
        IN6_ADDR parsedV6{};
        const auto wideIp = wideFromUtf8(descriptor.ipAddress);
        if (InetPtonW(AF_INET, wideIp.c_str(), &parsedV4) == 1) {
            ipv4Address = parsedV4.S_un.S_addr;
            ipv4Pointer = &ipv4Address;
        } else if (InetPtonW(AF_INET6, wideIp.c_str(), &parsedV6) == 1) {
            // DNS_SERVICE_INSTANCE IP6_ADDRESS is a struct containing a 16-byte array
            // IP6Dword[4] that maps 1:1 onto in6_addr's 16 bytes.
            std::memcpy(&ipv6Address, &parsedV6, sizeof(ipv6Address));
            ipv6Pointer = &ipv6Address;
        } else {
            // Descriptor address neither v4 nor v6; try a loopback fallback so we
            // still advertise on local interfaces rather than silently failing.
            parsedV4.S_un.S_addr = htonl(INADDR_LOOPBACK);
            ipv4Address = parsedV4.S_un.S_addr;
            ipv4Pointer = &ipv4Address;
        }

        registration.instance = DnsServiceConstructInstance(
            instanceName.c_str(),
            hostName.c_str(),
            ipv4Pointer,
            ipv6Pointer,
            descriptor.port,
            0,
            0,
            static_cast<DWORD>(keyPointers.size()),
            keyPointers.empty() ? nullptr : keyPointers.data(),
            valuePointers.empty() ? nullptr : valuePointers.data());

        if (registration.instance == nullptr) {
            registration.descriptor.status = "registration_failed";
            return;
        }

        DNS_SERVICE_REGISTER_REQUEST request{};
        request.Version = 1;
        request.InterfaceIndex = 0;
        request.pServiceInstance = registration.instance;
        request.pRegisterCompletionCallback = nullptr;
        request.pQueryContext = nullptr;
        request.hCredentials = nullptr;
        request.unicastEnabled = FALSE;

        const auto status = DnsServiceRegister(&request, nullptr);
        registration.request = request;
        registration.registered = status == ERROR_SUCCESS;
        if (registration.registered) {
            registration.descriptor.status = "advertised";
        } else {
            // Capture the specific DnsServiceRegister error code in properties
            // so operators can diagnose why mDNS failed — otherwise a generic
            // "registration_failed" gives no clue whether the prereq is
            // elevation, missing Bonjour/Dnscache, bad service type, etc.
            registration.descriptor.status = "registration_failed";
            char errBuf[32]{};
            std::snprintf(errBuf, sizeof(errBuf), "0x%08X", static_cast<unsigned>(status));
            registration.descriptor.properties["last_register_error"] = errBuf;
            registration.descriptor.properties["last_register_error_decimal"] = std::to_string(status);
        }
    }

    void deregisterGatewayLocked(GatewayRegistration& registration) {
        if (registration.instance == nullptr) {
            return;
        }

        if (registration.registered) {
            DnsServiceDeRegister(&registration.request, nullptr);
            registration.registered = false;
        }

        DnsServiceFreeInstance(registration.instance);
        registration.instance = nullptr;
    }

    std::shared_ptr<IConfigurationService> configurationService_;
    std::shared_ptr<ITelemetryService> telemetryService_;
    mutable std::mutex mutex_;
    std::map<std::string, GatewayRegistration> gatewaysByModuleId_;
    std::map<std::string, GovernanceServerDescriptor> governanceByModuleId_;
};

class AppleRemoteHostService final : public IAppleRemoteHostService {
public:
    AppleRemoteHostService(std::shared_ptr<SharedState> state, std::filesystem::path filePath)
        : state_(std::move(state))
        , filePath_(std::move(filePath)) {}

    std::vector<AppleRemoteHost> listHosts() const override {
        std::lock_guard<std::mutex> lock(state_->mutex);
        return state_->configuration.appleRemoteHosts;
    }

    OperationResult upsertHost(const AppleRemoteHost& host) override {
        auto normalized = normalizeHost(host);
        const auto validation = validateHost(normalized);
        if (!validation.succeeded) {
            return validation;
        }

        std::lock_guard<std::mutex> lock(state_->mutex);
        auto& hosts = state_->configuration.appleRemoteHosts;
        const auto iterator = std::find_if(
            hosts.begin(),
            hosts.end(),
            [&normalized](const AppleRemoteHost& candidate) { return candidate.hostId == normalized.hostId; });
        if (iterator == hosts.end()) {
            hosts.push_back(std::move(normalized));
        } else {
            *iterator = std::move(normalized);
        }
        persistLocked();
        return OperationResult{ true, false, "Apple remote host updated." };
    }

    OperationResult removeHost(const std::string& hostId) override {
        const auto trimmedHostId = trimCopy(hostId);
        if (trimmedHostId.empty()) {
            return OperationResult{ false, false, "Apple remote host removal requires a hostId." };
        }

        std::lock_guard<std::mutex> lock(state_->mutex);
        auto& hosts = state_->configuration.appleRemoteHosts;
        const auto iterator = std::remove_if(
            hosts.begin(),
            hosts.end(),
            [&trimmedHostId](const AppleRemoteHost& host) { return host.hostId == trimmedHostId; });
        if (iterator == hosts.end()) {
            return OperationResult{ false, false, "Apple remote host '" + trimmedHostId + "' was not found." };
        }

        hosts.erase(iterator, hosts.end());
        persistLocked();
        return OperationResult{ true, false, "Apple remote host removed." };
    }

    std::optional<AppleRemoteHost> inspectHost(const std::string& hostId) override {
        AppleRemoteHost host;
        {
            std::lock_guard<std::mutex> lock(state_->mutex);
            const auto iterator = std::find_if(
                state_->configuration.appleRemoteHosts.begin(),
                state_->configuration.appleRemoteHosts.end(),
                [&hostId](const AppleRemoteHost& candidate) { return candidate.hostId == hostId; });
            if (iterator == state_->configuration.appleRemoteHosts.end()) {
                return std::nullopt;
            }
            host = *iterator;
        }

        auto inspected = inspectHostState(host);
        persistHost(inspected);
        return inspected;
    }

    std::optional<AppleRemoteHost> selectHostForPlatform(const PlatformTarget platform) override {
        auto hosts = listHosts();
        std::optional<AppleRemoteHost> bestHost;
        int bestScore = (std::numeric_limits<int>::min)();

        for (const auto& configuredHost : hosts) {
            if (!configuredHost.enabled || !supportsPlatform(configuredHost, platform)) {
                continue;
            }

            auto inspected = inspectHostState(configuredHost);
            persistHost(inspected);

            const int score = scoreHost(inspected, platform);
            if (!bestHost.has_value() || score > bestScore) {
                bestScore = score;
                bestHost = std::move(inspected);
            }
        }

        return bestHost;
    }

    AppleRemoteCommandResult executeCommand(
        const std::string& hostId,
        const AppleRemoteCommandRequest& request) override {
        AppleRemoteCommandResult result;
        const auto inspectedHost = inspectHost(hostId);
        if (!inspectedHost.has_value()) {
            result.errorMessage = "Apple remote host '" + trimCopy(hostId) + "' was not found.";
            return result;
        }

        auto host = *inspectedHost;
        result.hostId = host.hostId;
        result.transport = host.transport;
        if (!host.enabled) {
            result.errorMessage = "Apple remote host '" + host.displayName + "' is disabled.";
            return result;
        }

        auto normalizedRequest = normalizeCommandRequest(request, host);
        if (normalizedRequest.executable.empty()) {
            result.errorMessage = "Apple remote execution requires an executable.";
            return result;
        }

        switch (host.transport) {
            case AppleRemoteTransport::CompanionService:
                return executeCompanionCommand(host, normalizedRequest);
            case AppleRemoteTransport::Ssh:
                return executeSshCommand(host, normalizedRequest);
            default:
                result.errorMessage = "Unsupported Apple remote host transport.";
                return result;
        }
    }

private:
    static AppleRemoteHost normalizeHost(AppleRemoteHost host) {
        host.hostId = trimCopy(host.hostId);
        host.displayName = trimCopy(host.displayName);
        host.address = trimCopy(host.address);
        host.username = trimCopy(host.username);
        host.serviceBaseUrl = trimCopy(host.serviceBaseUrl);
        host.companionHealthPath = trimCopy(host.companionHealthPath);
        host.companionExecutePath = trimCopy(host.companionExecutePath);
        host.preferredDeveloperDirectory = trimCopy(host.preferredDeveloperDirectory);
        host.defaultSigningIdentity = trimCopy(host.defaultSigningIdentity);
        host.defaultNotaryKeychainProfile = trimCopy(host.defaultNotaryKeychainProfile);
        host.defaultNotaryTeamId = trimCopy(host.defaultNotaryTeamId);

        if (host.displayName.empty()) {
            host.displayName = host.hostId;
        }
        if (host.companionHealthPath.empty()) {
            host.companionHealthPath = "/healthz";
        }
        if (host.companionExecutePath.empty()) {
            host.companionExecutePath = "/execute";
        }

        host.platforms.erase(
            std::remove(host.platforms.begin(), host.platforms.end(), PlatformTarget::Unknown),
            host.platforms.end());
        std::sort(host.platforms.begin(), host.platforms.end());
        host.platforms.erase(std::unique(host.platforms.begin(), host.platforms.end()), host.platforms.end());

        return host;
    }

    static OperationResult validateHost(const AppleRemoteHost& host) {
        if (host.hostId.empty()) {
            return OperationResult{ false, false, "Apple remote host requires a hostId." };
        }
        if (host.displayName.empty()) {
            return OperationResult{ false, false, "Apple remote host requires a displayName." };
        }
        if (host.transport == AppleRemoteTransport::Unknown) {
            return OperationResult{ false, false, "Apple remote host requires a supported transport." };
        }
        if (host.platforms.empty()) {
            return OperationResult{ false, false, "Apple remote host must declare at least one target platform." };
        }

        if (host.transport == AppleRemoteTransport::Ssh) {
            if (host.address.empty()) {
                return OperationResult{ false, false, "SSH Apple remote hosts require an address or hostname." };
            }
        } else if (host.transport == AppleRemoteTransport::CompanionService) {
            if (host.serviceBaseUrl.empty() && host.address.empty()) {
                return OperationResult{
                    false,
                    false,
                    "Companion-service Apple remote hosts require either a serviceBaseUrl or an address."
                };
            }
            if (host.serviceBaseUrl.empty() && host.port == 0) {
                return OperationResult{
                    false,
                    false,
                    "Companion-service Apple remote hosts require a port when serviceBaseUrl is not provided."
                };
            }
        }

        return OperationResult{ true, false, "Apple remote host is valid." };
    }

    static bool supportsPlatform(const AppleRemoteHost& host, const PlatformTarget platform) {
        return std::find(host.platforms.begin(), host.platforms.end(), platform) != host.platforms.end();
    }

    static bool isReadyForPlatform(const AppleRemoteHost& host, const PlatformTarget platform) {
        if (!host.toolchain.reachable || !host.toolchain.xcodeInstalled) {
            return false;
        }

        if (platform == PlatformTarget::MacOS) {
            return host.toolchain.macosSdkAvailable;
        }
        if (platform == PlatformTarget::IOS) {
            return host.toolchain.iosSdkAvailable &&
                host.signing.signingReady &&
                (host.toolchain.simulatorControlAvailable || host.toolchain.deviceControlAvailable);
        }
        return false;
    }

    static int scoreHost(const AppleRemoteHost& host, const PlatformTarget platform) {
        int score = 0;
        if (host.enabled) {
            score += 50;
        }
        if (supportsPlatform(host, platform)) {
            score += 50;
        }
        if (host.toolchain.reachable) {
            score += 40;
        }
        if (host.toolchain.xcodeInstalled) {
            score += 30;
        }
        if (platform == PlatformTarget::MacOS && host.toolchain.macosSdkAvailable) {
            score += 20;
        }
        if (platform == PlatformTarget::IOS && host.toolchain.iosSdkAvailable) {
            score += 20;
        }
        if (platform == PlatformTarget::IOS && host.signing.signingReady) {
            score += 15;
        }
        if (platform == PlatformTarget::IOS && host.toolchain.simulatorControlAvailable) {
            score += 10;
        }
        if (host.transport == AppleRemoteTransport::CompanionService) {
            score += 2;
        }
        if (isReadyForPlatform(host, platform)) {
            score += 100;
        }
        return score;
    }

    static std::string trimTrailingSlash(std::string value) {
        while (!value.empty() && value.back() == '/') {
            value.pop_back();
        }
        return value;
    }

    static std::string buildCompanionHealthUrl(const AppleRemoteHost& host) {
        std::string path = trimCopy(host.companionHealthPath);
        if (path.empty()) {
            path = "/healthz";
        }
        if (startsWith(path, "http://") || startsWith(path, "https://")) {
            return path;
        }
        if (!path.empty() && path.front() != '/') {
            path.insert(path.begin(), '/');
        }
        if (!host.serviceBaseUrl.empty()) {
            return trimTrailingSlash(host.serviceBaseUrl) + path;
        }

        const uint16_t port = host.port == 0 ? 80 : host.port;
        return "http://" + host.address + ":" + std::to_string(port) + path;
    }

    static std::string buildCompanionExecuteUrl(const AppleRemoteHost& host) {
        std::string path = trimCopy(host.companionExecutePath);
        if (path.empty()) {
            path = "/execute";
        }
        if (startsWith(path, "http://") || startsWith(path, "https://")) {
            return path;
        }
        if (!path.empty() && path.front() != '/') {
            path.insert(path.begin(), '/');
        }
        if (!host.serviceBaseUrl.empty()) {
            return trimTrailingSlash(host.serviceBaseUrl) + path;
        }

        const uint16_t port = host.port == 0 ? 80 : host.port;
        return "http://" + host.address + ":" + std::to_string(port) + path;
    }

    static AppleRemoteCommandRequest normalizeCommandRequest(
        AppleRemoteCommandRequest request,
        const AppleRemoteHost& host) {
        request.executable = trimCopy(request.executable);
        request.workingDirectory = trimCopy(request.workingDirectory);
        if (request.timeoutSeconds <= 0) {
            request.timeoutSeconds = 900;
        }
        if (!host.preferredDeveloperDirectory.empty() &&
            request.environment.find("DEVELOPER_DIR") == request.environment.end()) {
            request.environment.emplace("DEVELOPER_DIR", host.preferredDeveloperDirectory);
        }
        return request;
    }

    static std::string joinSummaryParts(const std::vector<std::string>& parts,
                                        const char* separator = " | ") {
        std::ostringstream stream;
        bool first = true;
        for (const auto& part : parts) {
            const auto trimmed = trimCopy(part);
            if (trimmed.empty()) {
                continue;
            }
            if (!first) {
                stream << separator;
            }
            stream << trimmed;
            first = false;
        }
        return trimCopy(stream.str());
    }

    static std::string deriveToolchainStatus(const AppleRemoteHost& host) {
        if (!host.enabled) {
            return "disabled";
        }
        if (!host.toolchain.reachable) {
            return "offline";
        }
        if (host.toolchain.xcodeInstalled &&
            (host.toolchain.macosSdkAvailable || host.toolchain.iosSdkAvailable)) {
            return "ready";
        }
        return "degraded";
    }

    static std::string deriveSigningStatus(const AppleRemoteHost& host) {
        return host.signing.signingReady ? "ready" : "not_ready";
    }

    static std::string buildAppleTransportSummary(const AppleRemoteHost& host) {
        switch (host.transport) {
            case AppleRemoteTransport::CompanionService: {
                const auto baseUrl = trimCopy(host.serviceBaseUrl);
                if (!baseUrl.empty()) {
                    return "Companion service at " + baseUrl;
                }
                if (!trimCopy(host.address).empty()) {
                    const auto port = host.port == 0 ? 80 : host.port;
                    return "Companion service via http://" + host.address + ":" + std::to_string(port);
                }
                return "Companion service transport";
            }
            case AppleRemoteTransport::Ssh: {
                auto target = trimCopy(host.address);
                if (!trimCopy(host.username).empty()) {
                    target = host.username + "@" + target;
                }
                if (host.port != 0) {
                    target += ":" + std::to_string(host.port);
                }
                return target.empty() ? "SSH transport" : "SSH to " + target;
            }
            default:
                return "Unknown transport";
        }
    }

    static std::string buildAppleCredentialProfileSummary(const AppleRemoteHost& host) {
        std::vector<std::string> parts;
        if (!trimCopy(host.defaultSigningIdentity).empty()) {
            parts.push_back("Default signing identity: " + host.defaultSigningIdentity);
        }
        if (!trimCopy(host.defaultNotaryKeychainProfile).empty()) {
            parts.push_back("Default notary profile: " + host.defaultNotaryKeychainProfile);
        }
        if (!trimCopy(host.defaultNotaryTeamId).empty()) {
            parts.push_back("Default team ID: " + host.defaultNotaryTeamId);
        }
        if (parts.empty()) {
            return "No host distribution defaults configured.";
        }
        return joinSummaryParts(parts, "; ");
    }

    static std::vector<std::string> deriveAppleReadinessIssues(const AppleRemoteHost& host) {
        std::vector<std::string> issues;
        if (!host.enabled) {
            issues.push_back("Host is disabled.");
            return issues;
        }
        if (!host.toolchain.reachable) {
            issues.push_back("Remote host is unreachable.");
        }
        if (!host.toolchain.xcodeInstalled) {
            issues.push_back("Xcode is unavailable.");
        }
        if (supportsPlatform(host, PlatformTarget::MacOS) && !host.toolchain.macosSdkAvailable) {
            issues.push_back("macOS SDK route is unavailable.");
        }
        if (supportsPlatform(host, PlatformTarget::IOS) && !host.toolchain.iosSdkAvailable) {
            issues.push_back("iOS SDK route is unavailable.");
        }
        if (supportsPlatform(host, PlatformTarget::IOS) && !host.signing.signingReady) {
            issues.push_back("iOS signing is not ready.");
        }
        if (supportsPlatform(host, PlatformTarget::IOS) &&
            !host.toolchain.simulatorControlAvailable &&
            !host.toolchain.deviceControlAvailable) {
            issues.push_back("Neither simulator nor device control is available.");
        }
        return issues;
    }

    static AppleRemoteHost finalizeAppleHostState(AppleRemoteHost host) {
        host.transportSummary = buildAppleTransportSummary(host);
        host.credentialProfileSummary = buildAppleCredentialProfileSummary(host);
        host.readinessIssues = deriveAppleReadinessIssues(host);
        return host;
    }

    static AppleRemoteHost applyCompanionPayload(AppleRemoteHost host, const nlohmann::json& payload) {
        const auto checkedAtUtc = timestampNowUtc();
        const auto toolchainPayload =
            payload.contains("toolchain") && payload.at("toolchain").is_object() ? payload.at("toolchain") : payload;
        const auto signingPayload =
            payload.contains("signing") && payload.at("signing").is_object() ? payload.at("signing") : payload;

        host.toolchain.reachable = toolchainPayload.value("reachable", true);
        host.toolchain.xcodeVersion = trimCopy(toolchainPayload.value("xcodeVersion", std::string{}));
        host.toolchain.xcodeInstalled = toolchainPayload.value(
            "xcodeInstalled",
            !host.toolchain.xcodeVersion.empty());
        host.toolchain.developerDirectory = trimCopy(
            toolchainPayload.value("developerDirectory", host.preferredDeveloperDirectory));
        host.toolchain.macosSdkAvailable = toolchainPayload.value("macosSdkAvailable", false);
        host.toolchain.iosSdkAvailable = toolchainPayload.value("iosSdkAvailable", false);
        host.toolchain.simulatorControlAvailable = toolchainPayload.value("simulatorControlAvailable", false);
        host.toolchain.deviceControlAvailable = toolchainPayload.value("deviceControlAvailable", false);
        if (toolchainPayload.contains("simulatorRuntimes") && toolchainPayload.at("simulatorRuntimes").is_array()) {
            host.toolchain.simulatorRuntimes.clear();
            for (const auto& runtime : toolchainPayload.at("simulatorRuntimes")) {
                if (runtime.is_string()) {
                    host.toolchain.simulatorRuntimes.push_back(runtime.get<std::string>());
                }
            }
        }
        host.toolchain.checkedAtUtc = checkedAtUtc;
        host.toolchain.status = trimCopy(toolchainPayload.value("status", deriveToolchainStatus(host)));
        host.toolchain.message = trimCopy(toolchainPayload.value("message", std::string{}));

        host.signing.signingReady = signingPayload.value("signingReady", false);
        host.signing.developmentSigningReady =
            signingPayload.value("developmentSigningReady", host.signing.signingReady);
        host.signing.distributionSigningReady =
            signingPayload.value("distributionSigningReady", host.signing.signingReady);
        if (signingPayload.contains("availableTeams") && signingPayload.at("availableTeams").is_array()) {
            host.signing.availableTeams.clear();
            for (const auto& team : signingPayload.at("availableTeams")) {
                if (team.is_string()) {
                    host.signing.availableTeams.push_back(team.get<std::string>());
                }
            }
        }
        host.signing.status = trimCopy(signingPayload.value("status", deriveSigningStatus(host)));
        host.signing.message = trimCopy(signingPayload.value("message", std::string{}));
        return finalizeAppleHostState(std::move(host));
    }

    static std::vector<std::string> parseSimulatorRuntimes(const std::string& stdoutText) {
        std::vector<std::string> runtimes;
        try {
            const auto payload = nlohmann::json::parse(stdoutText);
            if (!payload.contains("runtimes") || !payload.at("runtimes").is_array()) {
                return runtimes;
            }

            for (const auto& runtime : payload.at("runtimes")) {
                if (!runtime.is_object()) {
                    continue;
                }
                if (runtime.contains("isAvailable") && runtime.at("isAvailable").is_boolean() &&
                    !runtime.at("isAvailable").get<bool>()) {
                    continue;
                }
                if (runtime.contains("name") && runtime.at("name").is_string()) {
                    runtimes.push_back(runtime.at("name").get<std::string>());
                } else if (runtime.contains("identifier") && runtime.at("identifier").is_string()) {
                    runtimes.push_back(runtime.at("identifier").get<std::string>());
                }
            }
        } catch (...) {
        }
        return runtimes;
    }

    static std::vector<std::string> parseSigningTeams(const std::string& stdoutText) {
        std::vector<std::string> teams;
        std::istringstream stream(stdoutText);
        std::string line;
        while (std::getline(stream, line)) {
            const auto open = line.find('(');
            const auto close = line.find(')', open == std::string::npos ? 0 : open + 1);
            if (open == std::string::npos || close == std::string::npos || close <= open + 1) {
                continue;
            }
            const auto team = trimCopy(line.substr(open + 1, close - open - 1));
            if (!team.empty()) {
                teams.push_back(team);
            }
        }
        std::sort(teams.begin(), teams.end());
        teams.erase(std::unique(teams.begin(), teams.end()), teams.end());
        return teams;
    }

    static AppleRemoteCommandResult parseCompanionCommandPayload(
        const AppleRemoteHost& host,
        const HttpClientResponse& response) {
        AppleRemoteCommandResult result;
        result.hostId = host.hostId;
        result.transport = AppleRemoteTransport::CompanionService;
        result.rawResponse = response.body;
        if (!response.succeeded) {
            result.errorMessage = response.errorMessage.empty()
                ? "The Apple companion service did not return a successful response."
                : response.errorMessage;
            return result;
        }

        try {
            const auto payload = nlohmann::json::parse(response.body);
            const auto commandPayload =
                payload.contains("result") && payload.at("result").is_object() ? payload.at("result") : payload;
            result.launched = commandPayload.value("launched", true);
            result.exitCode = commandPayload.value("exitCode", result.launched ? 0 : -1);
            result.stdoutText = commandPayload.value("stdout", std::string{});
            result.stderrText = commandPayload.value("stderr", std::string{});
            result.errorMessage = commandPayload.value("errorMessage", std::string{});
            result.succeeded = commandPayload.value(
                "succeeded",
                result.launched && result.exitCode == 0 && result.errorMessage.empty());
            return result;
        } catch (...) {
            result.errorMessage = "The Apple companion service returned an unreadable execution payload.";
            return result;
        }
    }

    static std::string buildRemoteShellScript(const AppleRemoteCommandRequest& request) {
        std::ostringstream script;
        if (!request.workingDirectory.empty()) {
            script << "cd " << quotePosixShellArgument(request.workingDirectory) << " && ";
        }
        for (const auto& [key, value] : request.environment) {
            script << key << "=" << quotePosixShellArgument(value) << ' ';
        }
        script << quotePosixShellArgument(request.executable);
        for (const auto& argument : request.arguments) {
            script << ' ' << quotePosixShellArgument(argument);
        }
        return script.str();
    }

    AppleRemoteCommandResult executeCompanionCommand(
        const AppleRemoteHost& host,
        const AppleRemoteCommandRequest& request) const {
        const auto url = buildCompanionExecuteUrl(host);
        const auto payload = nlohmann::json{
            { "executable", request.executable },
            { "arguments", request.arguments },
            { "workingDirectory", request.workingDirectory },
            { "environment", request.environment },
            { "timeoutSeconds", request.timeoutSeconds }
        };
        const auto response = sendJsonRequest(
            "POST",
            url,
            { { L"Content-Type", L"application/json" } },
            payload.dump());
        return parseCompanionCommandPayload(host, response);
    }

    AppleRemoteCommandResult executeSshCommand(
        const AppleRemoteHost& host,
        const AppleRemoteCommandRequest& request) const {
        AppleRemoteCommandResult result;
        result.hostId = host.hostId;
        result.transport = AppleRemoteTransport::Ssh;

        const auto sshPath = findCommandOnPath({ L"ssh.exe" });
        if (!sshPath.has_value()) {
            result.errorMessage = "ssh.exe is not available on the Windows host.";
            return result;
        }

        std::vector<std::wstring> arguments;
        arguments.push_back(sshPath->wstring());
        arguments.push_back(L"-o");
        arguments.push_back(L"BatchMode=yes");
        arguments.push_back(L"-o");
        arguments.push_back(L"ConnectTimeout=8");
        if (host.port != 0) {
            arguments.push_back(L"-p");
            arguments.push_back(std::to_wstring(host.port));
        }

        std::string target = host.address;
        if (!host.username.empty()) {
            target = host.username + "@" + host.address;
        }
        arguments.push_back(wideFromUtf8(target));
        arguments.push_back(L"sh");
        arguments.push_back(L"-lc");
        arguments.push_back(wideFromUtf8(buildRemoteShellScript(request)));

        ResourceAllocationProfile allocationProfile;
        {
            std::lock_guard<std::mutex> lock(state_->mutex);
            allocationProfile = state_->configuration.resourceAllocation;
        }

        const auto process = runProcessCapture(
            joinCommandArguments(arguments),
            std::filesystem::current_path(),
            {},
            allocationProfile,
            true);
        result.launched = process.launched;
        result.exitCode = process.exitCode;
        result.stdoutText = process.stdoutText;
        result.stderrText = process.stderrText;
        result.rawResponse = process.stdoutText.empty() ? process.stderrText : process.stdoutText;
        result.succeeded = process.launched && process.exitCode == 0;
        if (!result.succeeded) {
            result.errorMessage = trimCopy(
                process.stderrText.empty() ? process.stdoutText : process.stderrText);
            if (result.errorMessage.empty() && !process.launched) {
                result.errorMessage = "SSH remote command could not be launched.";
            }
        }
        return result;
    }

    AppleRemoteHost inspectCompanionService(AppleRemoteHost host) const {
        host.toolchain.checkedAtUtc = timestampNowUtc();
        const auto url = buildCompanionHealthUrl(host);
        const auto response = sendJsonRequest("GET", url, {}, "");
        if (!response.succeeded) {
            host.toolchain.reachable = false;
            host.toolchain.status = "offline";
            host.toolchain.message = response.errorMessage.empty()
                ? "The Apple companion service did not respond successfully."
                : response.errorMessage;
            host.signing.status = "unknown";
            host.signing.message = "Signing state is unavailable because the companion service is unreachable.";
            return finalizeAppleHostState(std::move(host));
        }

        try {
            return applyCompanionPayload(std::move(host), nlohmann::json::parse(response.body));
        } catch (...) {
            host.toolchain.reachable = false;
            host.toolchain.status = "invalid_payload";
            host.toolchain.message = "The Apple companion service returned an unreadable JSON payload.";
            host.signing.status = "invalid_payload";
            host.signing.message = "The Apple companion service returned an unreadable JSON payload.";
            return finalizeAppleHostState(std::move(host));
        }
    }

    AppleRemoteHost inspectSshHost(AppleRemoteHost host) const {
        host.toolchain.checkedAtUtc = timestampNowUtc();

        const auto sshPath = findCommandOnPath({ L"ssh.exe" });
        if (!sshPath.has_value()) {
            host.toolchain.reachable = false;
            host.toolchain.status = "unavailable";
            host.toolchain.message = "ssh.exe is not available on the Windows host.";
            host.signing.status = "unavailable";
            host.signing.message = "SSH transport is unavailable because ssh.exe could not be found.";
            return finalizeAppleHostState(std::move(host));
        }

        const auto runRemoteCommand = [&](const std::string& remoteCommand) {
            AppleRemoteCommandRequest request;
            request.executable = "sh";
            request.arguments = { "-lc", remoteCommand };
            request.workingDirectory.clear();
            request.environment.clear();
            request.timeoutSeconds = 60;
            return executeSshCommand(host, request);
        };

        const auto developerDirectory = runRemoteCommand("xcode-select -p");
        const auto xcodeVersion = runRemoteCommand("xcodebuild -version");
        const auto macosSdk = runRemoteCommand("xcrun --sdk macosx --show-sdk-path");
        const auto iosSdk = runRemoteCommand("xcrun --sdk iphoneos --show-sdk-path");
        const auto simulatorState = runRemoteCommand("xcrun simctl list runtimes --json");
        const auto deviceControl = runRemoteCommand("xcrun devicectl list devices");
        const auto signingState = runRemoteCommand("security find-identity -v -p codesigning");

        host.toolchain.reachable = developerDirectory.launched && developerDirectory.succeeded;
        host.toolchain.developerDirectory = trimCopy(developerDirectory.stdoutText);
        host.toolchain.xcodeInstalled = xcodeVersion.launched && xcodeVersion.succeeded;
        host.toolchain.xcodeVersion.clear();
        {
            std::istringstream versionStream(xcodeVersion.stdoutText);
            std::getline(versionStream, host.toolchain.xcodeVersion);
            host.toolchain.xcodeVersion = trimCopy(host.toolchain.xcodeVersion);
        }
        host.toolchain.macosSdkAvailable = macosSdk.launched && macosSdk.succeeded;
        host.toolchain.iosSdkAvailable = iosSdk.launched && iosSdk.succeeded;
        host.toolchain.simulatorControlAvailable = simulatorState.launched && simulatorState.succeeded;
        host.toolchain.deviceControlAvailable = deviceControl.launched && deviceControl.succeeded;
        host.toolchain.simulatorRuntimes = parseSimulatorRuntimes(simulatorState.stdoutText);
        host.toolchain.status = deriveToolchainStatus(host);

        std::vector<std::string> toolchainMessages;
        if (!host.toolchain.reachable) {
            toolchainMessages.push_back("The remote Mac did not respond to xcode-select.");
        } else if (!host.toolchain.xcodeInstalled) {
            toolchainMessages.push_back("Xcode is not available through ssh on the selected host.");
        } else {
            toolchainMessages.push_back("Xcode toolchain is reachable through ssh.");
        }
        if (!host.toolchain.macosSdkAvailable && supportsPlatform(host, PlatformTarget::MacOS)) {
            toolchainMessages.push_back("The macOS SDK is not available on the selected host.");
        }
        if (!host.toolchain.iosSdkAvailable && supportsPlatform(host, PlatformTarget::IOS)) {
            toolchainMessages.push_back("The iOS SDK is not available on the selected host.");
        }
        std::ostringstream toolchainMessageStream;
        for (size_t index = 0; index < toolchainMessages.size(); ++index) {
            if (index > 0) {
                toolchainMessageStream << ' ';
            }
            toolchainMessageStream << toolchainMessages[index];
        }
        host.toolchain.message = trimCopy(toolchainMessageStream.str());

        host.signing.signingReady = signingState.launched &&
            signingState.succeeded &&
            signingState.stdoutText.find("0 valid identities found") == std::string::npos;
        host.signing.developmentSigningReady = host.signing.signingReady;
        host.signing.distributionSigningReady = host.signing.signingReady;
        host.signing.availableTeams = parseSigningTeams(signingState.stdoutText);
        host.signing.status = deriveSigningStatus(host);
        host.signing.message = host.signing.signingReady
            ? "Code-signing identities are available on the selected Apple host."
            : "No usable code-signing identities were detected on the selected Apple host.";

        return finalizeAppleHostState(std::move(host));
    }

    AppleRemoteHost inspectHostState(const AppleRemoteHost& host) const {
        if (!host.enabled) {
            auto inspected = host;
            inspected.toolchain.checkedAtUtc = timestampNowUtc();
            inspected.toolchain.status = "disabled";
            inspected.toolchain.message = "This Apple remote host is disabled.";
            inspected.signing.status = "disabled";
            inspected.signing.message = "This Apple remote host is disabled.";
            return finalizeAppleHostState(std::move(inspected));
        }

        switch (host.transport) {
            case AppleRemoteTransport::CompanionService:
                return inspectCompanionService(host);
            case AppleRemoteTransport::Ssh:
                return inspectSshHost(host);
            default: {
                auto inspected = host;
                inspected.toolchain.checkedAtUtc = timestampNowUtc();
                inspected.toolchain.reachable = false;
                inspected.toolchain.status = "unsupported_transport";
                inspected.toolchain.message = "Unsupported Apple host transport.";
                inspected.signing.status = "unsupported_transport";
                inspected.signing.message = "Unsupported Apple host transport.";
                return finalizeAppleHostState(std::move(inspected));
            }
        }
    }

    void persistHost(const AppleRemoteHost& host) {
        std::lock_guard<std::mutex> lock(state_->mutex);
        auto& hosts = state_->configuration.appleRemoteHosts;
        const auto iterator = std::find_if(
            hosts.begin(),
            hosts.end(),
            [&host](const AppleRemoteHost& candidate) { return candidate.hostId == host.hostId; });
        if (iterator == hosts.end()) {
            hosts.push_back(host);
        } else {
            *iterator = host;
        }
        persistLocked();
    }

    void persistLocked() const {
        (void)writeJsonFile(filePath_, state_->configuration);
    }

    std::shared_ptr<SharedState> state_;
    std::filesystem::path filePath_;
};

class PlatformGovernanceToolService final : public IPlatformGovernanceToolService {
public:
    PlatformGovernanceToolService(AppPaths paths,
                                  std::shared_ptr<IAppleRemoteHostService> appleRemoteHostService,
                                  std::shared_ptr<IConfigurationService> configurationService)
        : paths_(std::move(paths))
        , appleRemoteHostService_(std::move(appleRemoteHostService))
        , configurationService_(std::move(configurationService)) {
        if (std::filesystem::exists(paths_.appleOperationHistoryFile)) {
            const auto json = readJsonFile(paths_.appleOperationHistoryFile);
            if (!json.is_null() && !json.empty()) {
                recentAppleOperations_ = json.get<std::vector<AppleOperationRecord>>();
            }
        }
        reconcilePersistedAppleOperations();
        worker_ = std::thread([this]() { appleWorkerLoop(); });
    }

    ~PlatformGovernanceToolService() override {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopWorker_ = true;
        }
        workerCv_.notify_all();
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    void upsertTool(const GovernanceToolDescriptor& descriptor) override {
        std::lock_guard<std::mutex> lock(mutex_);
        toolsByKey_[makeKey(descriptor.moduleId, descriptor.toolId)] = descriptor;
    }

    void removeToolsForModule(const std::string& moduleId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto iterator = toolsByKey_.begin(); iterator != toolsByKey_.end();) {
            if (iterator->second.moduleId == moduleId) {
                iterator = toolsByKey_.erase(iterator);
            } else {
                ++iterator;
            }
        }
    }

    std::vector<GovernanceToolDescriptor> listTools() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<GovernanceToolDescriptor> descriptors;
        descriptors.reserve(toolsByKey_.size());
        for (const auto& [key, descriptor] : toolsByKey_) {
            descriptors.push_back(descriptor);
        }
        std::sort(
            descriptors.begin(),
            descriptors.end(),
            [](const GovernanceToolDescriptor& left, const GovernanceToolDescriptor& right) {
                return std::tie(left.platform, left.displayName, left.toolId) <
                    std::tie(right.platform, right.displayName, right.toolId);
            });
        return descriptors;
    }

    std::vector<GovernanceToolResult> recentExecutions() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return recentExecutions_;
    }

    std::vector<AppleOperationRecord> recentAppleOperations() const override {
        std::vector<AppleOperationRecord> operations;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            operations = recentAppleOperations_;
        }
        for (auto& operation : operations) {
            operation = refreshAppleOperationReplayState(std::move(operation));
        }
        return operations;
    }

    OperationResult cancelAppleOperation(const std::string& operationId) override {
        OperationResult result;
        AppleOperationRecord record;
        bool foundQueued = false;
        bool foundRunning = false;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto queuedIterator = std::find_if(
                appleQueue_.begin(),
                appleQueue_.end(),
                [&operationId](const AppleQueuedTask& task) {
                    return task.record.operationId == operationId;
                });
            if (queuedIterator != appleQueue_.end()) {
                record = queuedIterator->record;
                appleQueue_.erase(queuedIterator);
                foundQueued = true;
            } else {
                const auto persistedIterator = std::find_if(
                    recentAppleOperations_.begin(),
                    recentAppleOperations_.end(),
                    [&operationId](const AppleOperationRecord& candidate) {
                        return candidate.operationId == operationId;
                    });
                if (persistedIterator == recentAppleOperations_.end()) {
                    result.succeeded = false;
                    result.message = "Apple operation '" + operationId + "' was not found.";
                    return result;
                }

                record = *persistedIterator;
                foundRunning = runningAppleOperationId_.has_value() &&
                    *runningAppleOperationId_ == operationId;
            }
        }

        if (foundRunning || record.status == AppleOperationStatus::Running) {
            result.succeeded = false;
            result.message =
                "Apple operation '" + operationId +
                "' is already running. Transport-level interruption is not available yet.";
            return result;
        }

        if (!foundQueued && record.status != AppleOperationStatus::Queued) {
            result.succeeded = false;
            result.message =
                "Apple operation '" + operationId + "' is already in terminal state '" +
                to_string(record.status) + "'.";
            return result;
        }

        markAppleOperationCanceled(
            record,
            foundQueued
                ? "Apple governance operation was canceled before execution."
                : "Apple governance operation was canceled.");
        recordCanceledExecution(record);

        result.succeeded = true;
        result.message = "Canceled Apple operation '" + record.displayName + "'.";
        return result;
    }

    GovernanceToolResult execute(const GovernanceToolRequest& request) override {
        GovernanceToolResult result;
        result.platform = request.platform;
        result.toolId = request.toolId;
        result.startedAtUtc = timestampNowUtc();

        const auto descriptor = findDescriptor(request.platform, request.toolId);
        if (!descriptor.has_value()) {
            result.status = GovernanceToolStatus::Failed;
            result.summary = "Governance tool '" + request.toolId + "' is not registered for platform " +
                platformKey(request.platform) + ".";
            result.completedAtUtc = timestampNowUtc();
            recordExecution(result);
            return result;
        }

        result.displayName = descriptor->displayName;
        if (descriptor->requiresRemoteToolchain && isQueuedAppleOperationalTool(*descriptor)) {
            auto appleOperation = queueAppleOperation(*descriptor, request);
            if (!enqueueAppleOperation(*descriptor, request, appleOperation)) {
                markAppleOperationCanceled(appleOperation, "Apple governance queue is shutting down.");
                result.status = GovernanceToolStatus::Failed;
                result.summary = "Apple governance queue is not accepting new work.";
                result.completedAtUtc = timestampNowUtc();
                return result;
            }

            result.status = GovernanceToolStatus::Passed;
            result.succeeded = true;
            result.summary = "Queued " + descriptor->displayName + " for Apple governance execution.";
            result.completedAtUtc = timestampNowUtc();
            result.rawOutput = nlohmann::json{
                { "operationId", appleOperation.operationId },
                { "status", to_string(appleOperation.status) },
                { "queuedAtUtc", appleOperation.queuedAtUtc }
            }.dump(2);
            result.findings.push_back(makeFinding(
                descriptor->toolId,
                descriptor->displayName,
                "low",
                "pass",
                "Apple governance execution was queued for asynchronous processing."));
            return result;
        }

        std::optional<AppleOperationRecord> appleOperation;
        if (descriptor->requiresRemoteToolchain) {
            appleOperation = queueAppleOperation(*descriptor, request);
        }

        try {
            if (descriptor->requiresRemoteToolchain) {
                executeAppleRemoteTool(*descriptor, request, result, appleOperation ? &*appleOperation : nullptr);
            } else {
                switch (request.platform) {
                    case PlatformTarget::Windows:
                        executeWindowsTool(*descriptor, request, result);
                        break;
                    case PlatformTarget::MacOS:
                    case PlatformTarget::IOS:
                        result.status = GovernanceToolStatus::Unsupported;
                        result.summary = "Remote governance routing is required for this platform.";
                        break;
                    default:
                        result.status = GovernanceToolStatus::Failed;
                        result.summary = "Unknown governance platform.";
                        break;
                }
            }
        } catch (const std::exception& exception) {
            result.status = GovernanceToolStatus::Failed;
            result.summary = exception.what();
        }

        result.completedAtUtc = timestampNowUtc();
        if (appleOperation.has_value()) {
            completeAppleOperation(*appleOperation, result);
        }
        recordExecution(result);
        return result;
    }

private:
    struct AppleQueuedTask final {
        GovernanceToolDescriptor descriptor;
        GovernanceToolRequest request;
        AppleOperationRecord record;
    };

    static std::string makeKey(const std::string& moduleId, const std::string& toolId) {
        return moduleId + "::" + toolId;
    }

    static bool isQueuedAppleOperationalTool(const GovernanceToolDescriptor& descriptor) {
        static const std::set<std::string> queuedToolIds = {
            "forsetti.macos.build",
            "forsetti.macos.test",
            "forsetti.macos.archive",
            "forsetti.macos.sign",
            "forsetti.macos.notarize",
            "forsetti.macos.staple",
            "forsetti.ios.simulator.list",
            "forsetti.ios.build",
            "forsetti.ios.test",
            "forsetti.ios.archive",
            "forsetti.ios.export",
            "forsetti.ios.device.install"
        };
        return descriptor.requiresRemoteToolchain &&
            queuedToolIds.contains(descriptor.toolId);
    }

    static GovernanceFinding makeFinding(const std::string& ruleId,
                                         const std::string& title,
                                         const std::string& severity,
                                         const std::string& status,
                                         const std::string& message) {
        return GovernanceFinding{ ruleId, title, severity, status, message };
    }

    static void appendProcessFinding(std::vector<GovernanceFinding>& findings,
                                     const std::string& ruleId,
                                     const std::string& title,
                                     const std::string& scriptName,
                                     const ProcessCaptureResult& process) {
        const bool passed = process.launched && process.exitCode == 0;
        findings.push_back(makeFinding(
            ruleId,
            title,
            passed ? "low" : "high",
            passed ? "pass" : "blocked",
            scriptName + (passed
                    ? " completed successfully."
                    : " failed with exit code " + std::to_string(process.exitCode) + ".")));
    }

    std::optional<GovernanceToolDescriptor> findDescriptor(const PlatformTarget platform,
                                                           const std::string& toolId) const {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto iterator = std::find_if(
            toolsByKey_.begin(),
            toolsByKey_.end(),
            [platform, &toolId](const auto& entry) {
                return entry.second.platform == platform && entry.second.toolId == toolId;
            });
        if (iterator == toolsByKey_.end()) {
            return std::nullopt;
        }
        return iterator->second;
    }

    void recordExecution(const GovernanceToolResult& result) {
        std::lock_guard<std::mutex> lock(mutex_);
        recentExecutions_.insert(recentExecutions_.begin(), result);
        if (recentExecutions_.size() > 20) {
            recentExecutions_.resize(20);
        }
    }

    static std::string findAppleArtifactPath(const GovernanceToolDescriptor& descriptor,
                                             const GovernanceToolRequest& request) {
        std::vector<const char*> keys;
        if (descriptor.toolId == "forsetti.ios.export") {
            keys = { "exportPath", "outputPath", "outputDirectory", "artifactPath", "archivePath" };
        } else if (descriptor.toolId == "forsetti.ios.device.install") {
            keys = { "appPath", "applicationPath", "bundlePath", "artifactPath" };
        } else if (descriptor.toolId == "forsetti.macos.sign") {
            keys = { "bundlePath", "appPath", "targetBundlePath", "artifactPath" };
        } else if (descriptor.toolId == "forsetti.macos.notarize" ||
                   descriptor.toolId == "forsetti.macos.staple") {
            keys = { "artifactPath", "bundlePath", "staplePath", "targetPath" };
        } else if (descriptor.toolId == "forsetti.macos.archive" ||
                   descriptor.toolId == "forsetti.ios.archive") {
            keys = { "archivePath", "artifactPath" };
        } else {
            keys = { "artifactPath", "bundlePath", "appPath", "archivePath", "exportPath", "outputPath", "outputDirectory" };
        }

        for (const auto* key : keys) {
            const auto iterator = request.options.find(key);
            if (iterator == request.options.end()) {
                continue;
            }
            const auto value = trimCopy(iterator->second);
            if (!value.empty()) {
                return value;
            }
        }
        return {};
    }

    struct SanitizedAppleRequestOptions final {
        std::map<std::string, std::string> persistedOptions;
        std::vector<std::string> redactedKeys;
    };

    static std::string joinAppleGovernanceSummaryParts(const std::vector<std::string>& parts,
                                                       const char* separator = " | ") {
        std::ostringstream stream;
        bool first = true;
        for (const auto& part : parts) {
            const auto trimmed = trimCopy(part);
            if (trimmed.empty()) {
                continue;
            }
            if (!first) {
                stream << separator;
            }
            stream << trimmed;
            first = false;
        }
        return trimCopy(stream.str());
    }

    static SanitizedAppleRequestOptions sanitizeAppleRequestOptions(const GovernanceToolRequest& request) {
        static const std::set<std::string> allowedKeys = {
            "workspace",
            "project",
            "xcodeproj",
            "scheme",
            "configuration",
            "destination",
            "sdk",
            "archivePath",
            "exportPath",
            "outputPath",
            "outputDirectory",
            "exportOptionsPlist",
            "exportOptionsPlistPath",
            "deviceId",
            "device",
            "deviceIdentifier",
            "appPath",
            "applicationPath",
            "bundlePath",
            "targetBundlePath",
            "signingIdentity",
            "identity",
            "entitlementsPath",
            "artifactPath",
            "submissionPath",
            "packagePath",
            "staplePath",
            "keychainProfile",
            "notaryKeychainProfile",
            "teamId",
            "notaryTeamId",
            "timeoutSeconds",
            "force",
            "deep",
            "timestamp",
            "hardenedRuntime",
            "runtime",
            "remoteWorkingDirectory",
            "workingDirectory",
            "targetRoot"
        };

        static const std::set<std::string> secretKeys = {
            "appleId",
            "notaryAppleId",
            "password",
            "appleIdPassword",
            "appSpecificPassword",
            "notaryPassword"
        };

        SanitizedAppleRequestOptions sanitized;
        for (const auto& [key, value] : request.options) {
            const auto trimmedValue = trimCopy(value);
            if (trimmedValue.empty()) {
                continue;
            }
            if (secretKeys.contains(key)) {
                sanitized.redactedKeys.push_back(key);
                continue;
            }
            if (!allowedKeys.contains(key)) {
                continue;
            }
            sanitized.persistedOptions.emplace(key, trimmedValue);
        }
        std::sort(sanitized.redactedKeys.begin(), sanitized.redactedKeys.end());
        sanitized.redactedKeys.erase(
            std::unique(sanitized.redactedKeys.begin(), sanitized.redactedKeys.end()),
            sanitized.redactedKeys.end());
        return sanitized;
    }

    static std::string buildAppleRouteReason(const GovernanceToolRequest& request,
                                             const AppleRemoteHost& host,
                                             const std::string& preferredHostId) {
        if (!trimCopy(preferredHostId).empty()) {
            return "Requested Apple host '" + preferredHostId + "' was used for the " +
                platformKey(request.platform) + " governance lane.";
        }
        return "CLU selected Apple host '" + host.displayName + "' as the best eligible " +
            platformKey(request.platform) + " route.";
    }

    static std::string buildAppleDiagnosticSummary(const AppleRemoteHost& host,
                                                   const PlatformTarget platform) {
        std::vector<std::string> parts;
        if (!trimCopy(host.transportSummary).empty()) {
            parts.push_back(host.transportSummary);
        }
        if (!trimCopy(host.toolchain.xcodeVersion).empty()) {
            parts.push_back("Xcode " + host.toolchain.xcodeVersion);
        } else if (host.toolchain.xcodeInstalled) {
            parts.push_back("Xcode available");
        } else {
            parts.push_back("Xcode unavailable");
        }
        if (!trimCopy(host.toolchain.developerDirectory).empty()) {
            parts.push_back("Developer dir " + host.toolchain.developerDirectory);
        }
        if (platform == PlatformTarget::MacOS) {
            parts.push_back(host.toolchain.macosSdkAvailable ? "macOS SDK ready" : "macOS SDK missing");
        }
        if (platform == PlatformTarget::IOS) {
            parts.push_back(host.toolchain.iosSdkAvailable ? "iOS SDK ready" : "iOS SDK missing");
            parts.push_back(host.signing.signingReady ? "Signing ready" : "Signing unavailable");
            parts.push_back(
                (host.toolchain.simulatorControlAvailable || host.toolchain.deviceControlAvailable)
                    ? "Runtime control available"
                    : "Runtime control unavailable");
        }
        if (!host.readinessIssues.empty()) {
            parts.push_back("Open issues: " + joinAppleGovernanceSummaryParts(host.readinessIssues, "; "));
        }
        return joinAppleGovernanceSummaryParts(parts, "; ");
    }

    static std::string buildAppleOperationCredentialProfileSummary(const GovernanceToolDescriptor& descriptor,
                                                                   const GovernanceToolRequest& request,
                                                                   const AppleRemoteHost& host,
                                                                   const std::vector<std::string>& redactedKeys) {
        const auto readOption = [&request](std::initializer_list<const char*> keys) {
            for (const auto* key : keys) {
                const auto iterator = request.options.find(key);
                if (iterator == request.options.end()) {
                    continue;
                }
                const auto value = trimCopy(iterator->second);
                if (!value.empty()) {
                    return value;
                }
            }
            return std::string{};
        };

        std::vector<std::string> parts;
        if (descriptor.toolId == "forsetti.macos.sign") {
            const auto signingIdentity = readOption({ "signingIdentity", "identity" });
            if (!signingIdentity.empty()) {
                parts.push_back("Explicit signing identity: " + signingIdentity);
            } else if (!trimCopy(host.defaultSigningIdentity).empty()) {
                parts.push_back("Host signing identity: " + host.defaultSigningIdentity);
            } else {
                parts.push_back("No signing identity is configured.");
            }
        } else if (descriptor.toolId == "forsetti.macos.notarize") {
            const auto keychainProfile = readOption({ "keychainProfile", "notaryKeychainProfile" });
            const auto teamId = readOption({ "teamId", "notaryTeamId" });
            if (!keychainProfile.empty()) {
                parts.push_back("Explicit notary profile: " + keychainProfile);
            } else if (!trimCopy(host.defaultNotaryKeychainProfile).empty()) {
                parts.push_back("Host notary profile: " + host.defaultNotaryKeychainProfile);
            }
            if (!teamId.empty()) {
                parts.push_back("Explicit team ID: " + teamId);
            } else if (!trimCopy(host.defaultNotaryTeamId).empty()) {
                parts.push_back("Host team ID: " + host.defaultNotaryTeamId);
            }
            if (!redactedKeys.empty()) {
                parts.push_back("Apple ID credentials were supplied and redacted from persisted history.");
            } else if (parts.empty()) {
                parts.push_back("No notarization credential profile is configured.");
            }
        }

        if (parts.empty()) {
            return host.credentialProfileSummary;
        }
        return joinAppleGovernanceSummaryParts(parts, "; ");
    }

    struct AppleOperationReplayAssessment final {
        bool ready = false;
        std::string message;
    };

    static bool isAppleReplayValidationTool(const std::string& toolId) {
        return toolId == "forsetti.macos.toolchain.route" ||
            toolId == "forsetti.ios.signing.route" ||
            toolId == "forsetti.macos.remote-build.validate" ||
            toolId == "forsetti.ios.remote-build.validate";
    }

    static bool replayHostSupportsPlatform(const AppleRemoteHost& host, const PlatformTarget platform) {
        return std::find(host.platforms.begin(), host.platforms.end(), platform) != host.platforms.end();
    }

    static bool replayHostReadyForPlatform(const AppleRemoteHost& host, const PlatformTarget platform) {
        if (!host.toolchain.reachable || !host.toolchain.xcodeInstalled) {
            return false;
        }
        if (platform == PlatformTarget::MacOS) {
            return host.toolchain.macosSdkAvailable;
        }
        if (platform == PlatformTarget::IOS) {
            return host.toolchain.iosSdkAvailable &&
                host.signing.signingReady &&
                (host.toolchain.simulatorControlAvailable || host.toolchain.deviceControlAvailable);
        }
        return false;
    }

    std::optional<AppleRemoteHost> resolveReplayHost(const AppleOperationRecord& record) const {
        if (!appleRemoteHostService_) {
            return std::nullopt;
        }

        const auto hosts = appleRemoteHostService_->listHosts();
        if (!trimCopy(record.hostId).empty()) {
            const auto iterator = std::find_if(
                hosts.begin(),
                hosts.end(),
                [&record](const AppleRemoteHost& candidate) { return candidate.hostId == record.hostId; });
            if (iterator == hosts.end()) {
                return std::nullopt;
            }
            return *iterator;
        }

        std::optional<AppleRemoteHost> bestHost;
        int bestScore = (std::numeric_limits<int>::min)();
        for (const auto& host : hosts) {
            if (!host.enabled || !replayHostSupportsPlatform(host, record.platform)) {
                continue;
            }

            int score = 0;
            if (host.enabled) {
                score += 50;
            }
            if (replayHostSupportsPlatform(host, record.platform)) {
                score += 50;
            }
            if (host.toolchain.reachable) {
                score += 40;
            }
            if (host.toolchain.xcodeInstalled) {
                score += 30;
            }
            if (record.platform == PlatformTarget::MacOS && host.toolchain.macosSdkAvailable) {
                score += 20;
            }
            if (record.platform == PlatformTarget::IOS && host.toolchain.iosSdkAvailable) {
                score += 20;
            }
            if (record.platform == PlatformTarget::IOS && host.signing.signingReady) {
                score += 15;
            }
            if (record.platform == PlatformTarget::IOS && host.toolchain.simulatorControlAvailable) {
                score += 10;
            }
            if (host.transport == AppleRemoteTransport::CompanionService) {
                score += 2;
            }
            if (replayHostReadyForPlatform(host, record.platform)) {
                score += 100;
            }

            if (!bestHost.has_value() || score > bestScore) {
                bestScore = score;
                bestHost = host;
            }
        }
        return bestHost;
    }

    AppleOperationReplayAssessment assessAppleOperationReplay(const AppleOperationRecord& record) const {
        if (record.status == AppleOperationStatus::Queued || record.status == AppleOperationStatus::Running) {
            return AppleOperationReplayAssessment{
                false,
                "Wait for the current Apple operation to finish before rerunning it."
            };
        }

        const auto descriptor = findDescriptor(record.platform, record.toolId);
        if (!descriptor.has_value()) {
            return AppleOperationReplayAssessment{
                false,
                "The recorded Apple governance tool is no longer registered in this build."
            };
        }

        const auto host = resolveReplayHost(record);
        if (!host.has_value()) {
            return AppleOperationReplayAssessment{
                false,
                trimCopy(record.hostId).empty()
                    ? "No enabled Apple host is currently configured for automatic " + platformKey(record.platform) +
                        " reruns."
                    : "The original Apple host '" + record.hostId +
                        "' is no longer available for safe reruns."
            };
        }

        if (!host->enabled) {
            return AppleOperationReplayAssessment{
                false,
                "Apple host '" + host->displayName + "' is disabled and cannot be used for reruns."
            };
        }

        if (!replayHostSupportsPlatform(*host, record.platform)) {
            return AppleOperationReplayAssessment{
                false,
                "Apple host '" + host->displayName + "' no longer advertises the " +
                    platformKey(record.platform) + " governance lane."
            };
        }

        if (isAppleReplayValidationTool(record.toolId)) {
            std::vector<std::string> parts;
            parts.push_back(
                "Ready to rerun CLU validation on Apple host '" + host->displayName + "' via " +
                to_string(host->transport) + ".");
            if (!host->readinessIssues.empty()) {
                parts.push_back("Current readiness gaps: " + joinAppleGovernanceSummaryParts(host->readinessIssues, "; "));
            }
            return AppleOperationReplayAssessment{ true, joinAppleGovernanceSummaryParts(parts, "; ") };
        }

        GovernanceToolRequest request;
        request.platform = record.platform;
        request.toolId = record.toolId;
        request.targetPath = record.targetPath;
        request.options = record.requestOptions;

        GovernanceToolResult preview;
        preview.platform = record.platform;
        preview.toolId = record.toolId;
        preview.displayName = descriptor->displayName;

        const auto command = buildAppleOperationalCommand(*descriptor, request, *host, preview);
        if (!command.has_value()) {
            std::string message = trimCopy(preview.summary);
            if (record.toolId == "forsetti.macos.notarize" &&
                !record.redactedRequestOptionKeys.empty() &&
                trimCopy(host->defaultNotaryKeychainProfile).empty()) {
                message =
                    "Rerun requires a host default notarization profile or fresh Apple ID credentials because "
                    "the original secrets were redacted from stored history.";
            } else if (message.empty() && !preview.findings.empty()) {
                message = preview.findings.front().message;
            }
            if (message.empty()) {
                message = "Stored Apple operation no longer has enough information for a safe rerun.";
            }
            return AppleOperationReplayAssessment{ false, message };
        }

        std::vector<std::string> parts;
        parts.push_back(
            "Ready to rerun on Apple host '" + host->displayName + "' via " +
            to_string(host->transport) + ".");
        if (record.toolId == "forsetti.macos.notarize" &&
            !record.redactedRequestOptionKeys.empty() &&
            !trimCopy(host->defaultNotaryKeychainProfile).empty()) {
            parts.push_back(
                "Redacted Apple ID secrets will be replaced by host notary profile '" +
                host->defaultNotaryKeychainProfile + "'.");
        } else if (!record.redactedRequestOptionKeys.empty()) {
            parts.push_back("Stored history still omits sensitive request values.");
        }
        if (!host->readinessIssues.empty()) {
            parts.push_back("Current readiness gaps: " + joinAppleGovernanceSummaryParts(host->readinessIssues, "; "));
        }
        return AppleOperationReplayAssessment{ true, joinAppleGovernanceSummaryParts(parts, "; ") };
    }

    AppleOperationRecord refreshAppleOperationReplayState(AppleOperationRecord record) const {
        const auto assessment = assessAppleOperationReplay(record);
        record.rerunReady = assessment.ready;
        record.rerunReadinessMessage = assessment.message;
        return record;
    }

    void reconcilePersistedAppleOperations() {
        bool changed = false;
        const auto interruptedAtUtc = timestampNowUtc();
        for (auto& operation : recentAppleOperations_) {
            const auto previousStatus = operation.status;
            if (previousStatus != AppleOperationStatus::Queued &&
                previousStatus != AppleOperationStatus::Running) {
                continue;
            }

            operation.status = AppleOperationStatus::Blocked;
            operation.completedAtUtc = interruptedAtUtc;
            if (previousStatus == AppleOperationStatus::Running) {
                operation.summary = "Apple governance operation was interrupted by service restart.";
                if (operation.errorMessage.empty()) {
                    operation.errorMessage = "Apple governance operation did not finish before restart.";
                }
            } else {
                operation.summary = "Queued Apple governance operation was interrupted by service restart.";
                if (operation.errorMessage.empty()) {
                    operation.errorMessage = "Queued Apple governance operation did not resume after restart.";
                }
            }
            changed = true;
        }

        if (changed) {
            (void)writeJsonFile(paths_.appleOperationHistoryFile, recentAppleOperations_);
        }
    }

    bool enqueueAppleOperation(const GovernanceToolDescriptor& descriptor,
                               const GovernanceToolRequest& request,
                               const AppleOperationRecord& record) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopWorker_) {
                return false;
            }
            appleQueue_.push_back(AppleQueuedTask{ descriptor, request, record });
        }
        workerCv_.notify_one();
        return true;
    }

    void appleWorkerLoop() {
        for (;;) {
            AppleQueuedTask task;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                workerCv_.wait(lock, [this]() { return stopWorker_ || !appleQueue_.empty(); });
                if (stopWorker_ && appleQueue_.empty()) {
                    return;
                }
                task = std::move(appleQueue_.front());
                appleQueue_.pop_front();
                runningAppleOperationId_ = task.record.operationId;
            }

            GovernanceToolResult result;
            result.platform = task.request.platform;
            result.toolId = task.request.toolId;
            result.displayName = task.descriptor.displayName;
            result.startedAtUtc = timestampNowUtc();

            try {
                executeAppleRemoteTool(task.descriptor, task.request, result, &task.record);
            } catch (const std::exception& exception) {
                result.status = GovernanceToolStatus::Failed;
                result.summary = exception.what();
            }

            result.completedAtUtc = timestampNowUtc();
            completeAppleOperation(task.record, result);
            recordExecution(result);

            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (runningAppleOperationId_.has_value() &&
                    *runningAppleOperationId_ == task.record.operationId) {
                    runningAppleOperationId_.reset();
                }
            }
        }
    }

    AppleOperationRecord queueAppleOperation(const GovernanceToolDescriptor& descriptor,
                                             const GovernanceToolRequest& request) {
        AppleOperationRecord record;
        const auto sanitizedOptions = sanitizeAppleRequestOptions(request);
        record.operationId = generateExecutionId();
        record.platform = request.platform;
        record.toolId = request.toolId;
        record.displayName = descriptor.displayName;
        record.status = AppleOperationStatus::Queued;
        record.queuedAtUtc = timestampNowUtc();
        record.workingDirectory = resolveAppleWorkingDirectory(request);
        record.artifactPath = findAppleArtifactPath(descriptor, request);
        record.targetPath = request.targetPath;
        record.requestOptions = sanitizedOptions.persistedOptions;
        record.redactedRequestOptionKeys = sanitizedOptions.redactedKeys;
        record.summary = "Queued Apple governance operation.";
        if (!record.redactedRequestOptionKeys.empty()) {
            record.diagnosticSummary =
                "Sensitive Apple request options were redacted from persisted history.";
        }
        persistAppleOperation(record);
        return record;
    }

    void markAppleOperationRunning(AppleOperationRecord& record,
                                   const AppleRemoteHost& host) {
        record.hostId = host.hostId;
        record.hostDisplayName = host.displayName;
        record.transport = host.transport;
        record.status = AppleOperationStatus::Running;
        record.startedAtUtc = timestampNowUtc();
        record.summary = "Running on Apple host '" + host.displayName + "' via " + to_string(host.transport) + ".";
        persistAppleOperation(record);
    }

    void completeAppleOperation(AppleOperationRecord& record,
                                const GovernanceToolResult& result) {
        if (record.startedAtUtc.empty()) {
            record.startedAtUtc = result.startedAtUtc;
        }
        record.completedAtUtc = result.completedAtUtc.empty() ? timestampNowUtc() : result.completedAtUtc;
        record.status = mapAppleOperationStatus(result);
        record.summary = result.summary;
        record.rawOutput = result.rawOutput;
        if (!result.routeReason.empty()) {
            record.routeReason = result.routeReason;
        }
        if (!result.diagnosticSummary.empty()) {
            record.diagnosticSummary = result.diagnosticSummary;
        }
        if (!result.readinessIssues.empty()) {
            record.readinessIssues = result.readinessIssues;
        }
        if (!result.succeeded && !result.findings.empty()) {
            record.errorMessage = result.findings.front().message;
        }
        if (!result.succeeded && record.errorMessage.empty()) {
            record.errorMessage = result.summary;
        }
        persistAppleOperation(record);
    }

    void markAppleOperationCanceled(AppleOperationRecord& record,
                                    const std::string& message) {
        record.status = AppleOperationStatus::Canceled;
        record.completedAtUtc = timestampNowUtc();
        record.summary = message;
        if (record.errorMessage.empty()) {
            record.errorMessage = message;
        }
        persistAppleOperation(record);
    }

    void recordCanceledExecution(const AppleOperationRecord& record) {
        GovernanceToolResult result;
        result.platform = record.platform;
        result.toolId = record.toolId;
        result.displayName = record.displayName;
        result.status = GovernanceToolStatus::Warning;
        result.succeeded = false;
        result.summary = record.summary;
        result.startedAtUtc = record.startedAtUtc.empty() ? record.queuedAtUtc : record.startedAtUtc;
        result.completedAtUtc = record.completedAtUtc.empty() ? timestampNowUtc() : record.completedAtUtc;
        result.routeReason = record.routeReason;
        result.diagnosticSummary = record.diagnosticSummary;
        result.readinessIssues = record.readinessIssues;
        result.findings.push_back(makeFinding(
            record.toolId,
            record.displayName,
            "medium",
            "warning",
            record.summary));
        recordExecution(result);
    }

    static AppleOperationStatus mapAppleOperationStatus(const GovernanceToolResult& result) {
        if (result.succeeded) {
            return AppleOperationStatus::Succeeded;
        }
        switch (result.status) {
            case GovernanceToolStatus::Warning:
            case GovernanceToolStatus::Unsupported:
                return AppleOperationStatus::Blocked;
            case GovernanceToolStatus::Passed:
                return AppleOperationStatus::Succeeded;
            case GovernanceToolStatus::Failed:
            default:
                return AppleOperationStatus::Failed;
        }
    }

    void persistAppleOperation(const AppleOperationRecord& record) {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto iterator = std::find_if(
            recentAppleOperations_.begin(),
            recentAppleOperations_.end(),
            [&record](const AppleOperationRecord& candidate) { return candidate.operationId == record.operationId; });
        if (iterator == recentAppleOperations_.end()) {
            recentAppleOperations_.insert(recentAppleOperations_.begin(), record);
        } else {
            *iterator = record;
            std::rotate(recentAppleOperations_.begin(), iterator, iterator + 1);
        }
        if (recentAppleOperations_.size() > 30) {
            recentAppleOperations_.resize(30);
        }
        (void)writeJsonFile(paths_.appleOperationHistoryFile, recentAppleOperations_);
    }

    std::filesystem::path resolveTargetRoot(const GovernanceToolRequest& request) const {
        if (!trimCopy(request.targetPath).empty()) {
            return std::filesystem::path(request.targetPath);
        }
        if (const auto iterator = request.options.find("targetRoot"); iterator != request.options.end() &&
            !trimCopy(iterator->second).empty()) {
            return std::filesystem::path(iterator->second);
        }
        return std::filesystem::current_path();
    }

    std::optional<std::filesystem::path> findVendoredForsettiScriptsRoot(const std::filesystem::path& targetRoot) const {
        const std::array<std::filesystem::path, 3> candidates = {
            targetRoot / "Forsetti-Framework-Windows-main" / "Forsetti-Framework-Windows-main" / "Scripts",
            targetRoot / "Forsetti-Framework-Windows-main" / "Scripts",
            std::filesystem::current_path() / "Forsetti-Framework-Windows-main" / "Forsetti-Framework-Windows-main" / "Scripts"
        };

        for (const auto& candidate : candidates) {
            if (std::filesystem::exists(candidate / "check-architecture.ps1")) {
                return candidate;
            }
        }
        return std::nullopt;
    }

    std::vector<std::filesystem::path> candidatePayloadRoots(const std::filesystem::path& targetRoot) const {
        std::vector<std::filesystem::path> candidates;
        candidates.push_back(targetRoot);
        const auto distRoot = targetRoot / "dist";
        if (std::filesystem::exists(distRoot) && std::filesystem::is_directory(distRoot)) {
            for (const auto& entry : std::filesystem::directory_iterator(distRoot)) {
                if (entry.is_directory()) {
                    candidates.push_back(entry.path());
                }
            }
        }
        return candidates;
    }

    static std::optional<std::filesystem::path> findSharePayloadRoot(const std::filesystem::path& root) {
        for (const auto* shareLeaf : { "MasterControlOrchestrationServer", "MasterControlProgram" }) {
            const auto candidate = root / "share" / shareLeaf;
            if (std::filesystem::exists(candidate / "web" / "index.html") &&
                std::filesystem::exists(candidate / "ForsettiManifests" / "DashboardUIModule.json")) {
                return candidate;
            }
        }
        return std::nullopt;
    }

    static bool hasPayloadLayout(const std::filesystem::path& root) {
        return std::filesystem::exists(root / "MasterControlServiceHost.exe") &&
            std::filesystem::exists(root / "MasterControlShell.exe") &&
            std::filesystem::exists(root / "MasterControlBootstrapper.exe") &&
            findSharePayloadRoot(root).has_value();
    }

    ProcessCaptureResult runPowerShellScript(const std::filesystem::path& scriptPath,
                                             const std::filesystem::path& workingDirectory) const {
        const auto powershell = findCommandOnPath({ L"pwsh.exe", L"powershell.exe" });
        if (!powershell.has_value()) {
            return {};
        }

        const auto commandLine = joinCommandArguments({
            powershell->wstring(),
            L"-NoProfile",
            L"-ExecutionPolicy",
            L"Bypass",
            L"-File",
            scriptPath.wstring()
        });
        const auto resourceAllocation = configurationService_
            ? std::optional<ResourceAllocationProfile>(configurationService_->current().resourceAllocation)
            : std::nullopt;
        return runProcessCapture(commandLine, workingDirectory, {}, resourceAllocation, false);
    }

    static std::string optionValue(
        const GovernanceToolRequest& request,
        std::initializer_list<const char*> keys) {
        for (const auto* key : keys) {
            const auto iterator = request.options.find(key);
            if (iterator == request.options.end()) {
                continue;
            }
            const auto value = trimCopy(iterator->second);
            if (!value.empty()) {
                return value;
            }
        }
        return {};
    }

    static void appendProjectSelectionArguments(std::vector<std::string>& arguments,
                                                const GovernanceToolRequest& request) {
        const auto workspace = optionValue(request, { "workspace" });
        const auto project = optionValue(request, { "project", "xcodeproj" });
        if (!workspace.empty()) {
            arguments.push_back("-workspace");
            arguments.push_back(workspace);
            return;
        }
        if (!project.empty()) {
            arguments.push_back("-project");
            arguments.push_back(project);
        }
    }

    static std::string remotePathJoin(const std::string& left, const std::string& right) {
        if (left.empty()) {
            return right;
        }
        if (right.empty()) {
            return left;
        }
        if (left.back() == '/' || left.back() == '\\') {
            return left + right;
        }
        return left + "/" + right;
    }

    std::string resolveAppleWorkingDirectory(const GovernanceToolRequest& request) const {
        const auto workingDirectory = optionValue(request, { "remoteWorkingDirectory", "workingDirectory" });
        if (!workingDirectory.empty()) {
            return workingDirectory;
        }
        if (!trimCopy(request.targetPath).empty()) {
            return request.targetPath;
        }
        if (const auto iterator = request.options.find("targetRoot"); iterator != request.options.end()) {
            const auto targetRoot = trimCopy(iterator->second);
            if (!targetRoot.empty()) {
                return targetRoot;
            }
        }
        return ".";
    }

    std::optional<AppleRemoteCommandRequest> buildAppleOperationalCommand(
        const GovernanceToolDescriptor& descriptor,
        const GovernanceToolRequest& request,
        const AppleRemoteHost& host,
        GovernanceToolResult& result) const {
        AppleRemoteCommandRequest command;
        command.timeoutSeconds = 1800;
        command.workingDirectory = resolveAppleWorkingDirectory(request);

        const auto scheme = optionValue(request, { "scheme" });
        const auto configuration = optionValue(request, { "configuration" });
        const auto destination = optionValue(request, { "destination" });
        const auto sdk = optionValue(request, { "sdk" });
        const auto archivePath = optionValue(request, { "archivePath" });
        const auto exportPath = optionValue(request, { "exportPath", "outputPath", "outputDirectory" });
        const auto exportOptionsPlist = optionValue(request, { "exportOptionsPlist", "exportOptionsPlistPath" });
        const auto deviceId = optionValue(request, { "deviceId", "device", "deviceIdentifier" });
        const auto appPath = optionValue(request, { "appPath", "applicationPath", "bundlePath" });
        const auto bundlePath = optionValue(request, { "bundlePath", "appPath", "targetBundlePath" });
        const auto signingIdentity = [&]() {
            const auto explicitIdentity = optionValue(request, { "signingIdentity", "identity" });
            return explicitIdentity.empty() ? host.defaultSigningIdentity : explicitIdentity;
        }();
        const auto entitlementsPath = optionValue(request, { "entitlementsPath" });
        const auto notarizationArtifactPath = optionValue(
            request,
            { "artifactPath", "submissionPath", "packagePath", "bundlePath" });
        const auto staplePath = optionValue(request, { "artifactPath", "bundlePath", "staplePath", "targetPath" });
        const auto keychainProfile = [&]() {
            const auto explicitProfile = optionValue(request, { "keychainProfile", "notaryKeychainProfile" });
            return explicitProfile.empty() ? host.defaultNotaryKeychainProfile : explicitProfile;
        }();
        const auto appleId = optionValue(request, { "appleId", "notaryAppleId" });
        const auto appleIdPassword = optionValue(
            request,
            { "appSpecificPassword", "appleIdPassword", "notaryPassword", "password" });
        const auto teamId = [&]() {
            const auto explicitTeamId = optionValue(request, { "teamId", "notaryTeamId" });
            return explicitTeamId.empty() ? host.defaultNotaryTeamId : explicitTeamId;
        }();
        const auto timeoutText = optionValue(request, { "timeoutSeconds" });
        const auto parseBoolOption = [&](std::initializer_list<const char*> keys, const bool defaultValue) {
            auto value = optionValue(request, keys);
            if (value.empty()) {
                return defaultValue;
            }
            std::transform(
                value.begin(),
                value.end(),
                value.begin(),
                [](const unsigned char character) {
                    return static_cast<char>(std::tolower(character));
                });
            return !(value == "0" || value == "false" || value == "no" || value == "off");
        };
        if (!timeoutText.empty()) {
            try {
                command.timeoutSeconds = (std::max)(60, std::stoi(timeoutText));
            } catch (...) {
            }
        }

        if (descriptor.toolId == "forsetti.ios.simulator.list") {
            command.executable = "xcrun";
            command.arguments = { "simctl", "list", "devices", "available", "--json" };
            command.timeoutSeconds = 120;
            return command;
        }

        if (descriptor.toolId == "forsetti.macos.sign") {
            if (!host.signing.distributionSigningReady) {
                result.status = GovernanceToolStatus::Warning;
                result.summary = "macOS signing requires distribution signing readiness on the selected Apple host.";
                result.findings.push_back(makeFinding(
                    descriptor.toolId,
                    descriptor.displayName,
                    "high",
                    "blocked",
                    "The selected Apple host does not report distribution code-signing readiness."));
                return std::nullopt;
            }
            if (bundlePath.empty()) {
                result.status = GovernanceToolStatus::Warning;
                result.summary = "macOS signing requires a bundlePath option.";
                result.findings.push_back(makeFinding(
                    descriptor.toolId,
                    descriptor.displayName,
                    "high",
                    "blocked",
                    "Provide request.options.bundlePath before invoking macOS signing."));
                return std::nullopt;
            }
            if (signingIdentity.empty()) {
                result.status = GovernanceToolStatus::Warning;
                result.summary = "macOS signing requires a signingIdentity option.";
                result.findings.push_back(makeFinding(
                    descriptor.toolId,
                    descriptor.displayName,
                    "high",
                    "blocked",
                    "Provide request.options.signingIdentity before invoking macOS signing."));
                return std::nullopt;
            }

            command.executable = "codesign";
            if (parseBoolOption({ "force" }, true)) {
                command.arguments.push_back("--force");
            }
            if (parseBoolOption({ "deep" }, true)) {
                command.arguments.push_back("--deep");
            }
            if (parseBoolOption({ "timestamp" }, true)) {
                command.arguments.push_back("--timestamp");
            }
            if (parseBoolOption({ "hardenedRuntime", "runtime" }, true)) {
                command.arguments.push_back("--options");
                command.arguments.push_back("runtime");
            }
            if (!entitlementsPath.empty()) {
                command.arguments.push_back("--entitlements");
                command.arguments.push_back(entitlementsPath);
            }
            command.arguments.push_back("--sign");
            command.arguments.push_back(signingIdentity);
            command.arguments.push_back(bundlePath);
            command.timeoutSeconds = (std::max)(command.timeoutSeconds, 600);
            return command;
        }

        if (descriptor.toolId == "forsetti.macos.notarize") {
            if (notarizationArtifactPath.empty()) {
                result.status = GovernanceToolStatus::Warning;
                result.summary = "macOS notarization requires an artifactPath option.";
                result.findings.push_back(makeFinding(
                    descriptor.toolId,
                    descriptor.displayName,
                    "high",
                    "blocked",
                    "Provide request.options.artifactPath before invoking macOS notarization."));
                return std::nullopt;
            }

            command.executable = "xcrun";
            command.arguments = { "notarytool", "submit", notarizationArtifactPath, "--wait" };
            if (!keychainProfile.empty()) {
                command.arguments.push_back("--keychain-profile");
                command.arguments.push_back(keychainProfile);
            } else {
                if (appleId.empty() || appleIdPassword.empty() || teamId.empty()) {
                    result.status = GovernanceToolStatus::Warning;
                    result.summary = "macOS notarization requires either keychainProfile or Apple ID credentials.";
                    result.findings.push_back(makeFinding(
                        descriptor.toolId,
                        descriptor.displayName,
                        "high",
                        "blocked",
                        "Provide request.options.keychainProfile or the appleId, appSpecificPassword, and teamId options before invoking macOS notarization."));
                    return std::nullopt;
                }
                command.arguments.push_back("--apple-id");
                command.arguments.push_back(appleId);
                command.arguments.push_back("--password");
                command.arguments.push_back(appleIdPassword);
                command.arguments.push_back("--team-id");
                command.arguments.push_back(teamId);
            }
            command.timeoutSeconds = (std::max)(command.timeoutSeconds, 3600);
            return command;
        }

        if (descriptor.toolId == "forsetti.macos.staple") {
            if (staplePath.empty()) {
                result.status = GovernanceToolStatus::Warning;
                result.summary = "macOS stapling requires an artifactPath or bundlePath option.";
                result.findings.push_back(makeFinding(
                    descriptor.toolId,
                    descriptor.displayName,
                    "high",
                    "blocked",
                    "Provide request.options.artifactPath or request.options.bundlePath before invoking macOS stapling."));
                return std::nullopt;
            }

            command.executable = "xcrun";
            command.arguments = { "stapler", "staple", staplePath };
            command.timeoutSeconds = (std::max)(command.timeoutSeconds, 900);
            return command;
        }

        if (descriptor.toolId == "forsetti.ios.export") {
            if (archivePath.empty()) {
                result.status = GovernanceToolStatus::Warning;
                result.summary = "iOS archive export requires an archivePath option.";
                result.findings.push_back(makeFinding(
                    descriptor.toolId,
                    descriptor.displayName,
                    "high",
                    "blocked",
                    "Provide request.options.archivePath before exporting an iOS archive."));
                return std::nullopt;
            }
            if (exportOptionsPlist.empty()) {
                result.status = GovernanceToolStatus::Warning;
                result.summary = "iOS archive export requires an exportOptionsPlist option.";
                result.findings.push_back(makeFinding(
                    descriptor.toolId,
                    descriptor.displayName,
                    "high",
                    "blocked",
                    "Provide request.options.exportOptionsPlist with a remote ExportOptions.plist path before exporting an iOS archive."));
                return std::nullopt;
            }

            const auto resolvedExportPath = !exportPath.empty()
                ? exportPath
                : remotePathJoin(remotePathJoin(command.workingDirectory, "build"), "ios-export");

            command.executable = "xcodebuild";
            command.arguments = {
                "-exportArchive",
                "-archivePath",
                archivePath,
                "-exportPath",
                resolvedExportPath,
                "-exportOptionsPlist",
                exportOptionsPlist
            };
            command.timeoutSeconds = (std::max)(command.timeoutSeconds, 1200);
            return command;
        }

        if (descriptor.toolId == "forsetti.ios.device.install") {
            if (!host.toolchain.deviceControlAvailable) {
                result.status = GovernanceToolStatus::Warning;
                result.summary = "iOS device install requires device control on the selected Apple host.";
                result.findings.push_back(makeFinding(
                    descriptor.toolId,
                    descriptor.displayName,
                    "high",
                    "blocked",
                    "The selected Apple host does not report xcrun devicectl availability."));
                return std::nullopt;
            }
            if (deviceId.empty()) {
                result.status = GovernanceToolStatus::Warning;
                result.summary = "iOS device install requires a deviceId option.";
                result.findings.push_back(makeFinding(
                    descriptor.toolId,
                    descriptor.displayName,
                    "high",
                    "blocked",
                    "Provide request.options.deviceId before installing an app on a connected iOS device."));
                return std::nullopt;
            }
            if (appPath.empty()) {
                result.status = GovernanceToolStatus::Warning;
                result.summary = "iOS device install requires an appPath option.";
                result.findings.push_back(makeFinding(
                    descriptor.toolId,
                    descriptor.displayName,
                    "high",
                    "blocked",
                    "Provide request.options.appPath with the remote .app bundle path before device installation."));
                return std::nullopt;
            }

            command.executable = "xcrun";
            command.arguments = {
                "devicectl",
                "device",
                "install",
                "app",
                "--device",
                deviceId,
                appPath
            };
            command.timeoutSeconds = (std::max)(command.timeoutSeconds, 600);
            return command;
        }

        if (scheme.empty()) {
            result.status = GovernanceToolStatus::Warning;
            result.summary = "Apple build governance tools require a scheme option.";
            result.findings.push_back(makeFinding(
                descriptor.toolId,
                descriptor.displayName,
                "high",
                "blocked",
                "Provide request.options.scheme before invoking this Apple governance tool."));
            return std::nullopt;
        }

        command.executable = "xcodebuild";
        appendProjectSelectionArguments(command.arguments, request);
        command.arguments.push_back("-scheme");
        command.arguments.push_back(scheme);

        if (!configuration.empty()) {
            command.arguments.push_back("-configuration");
            command.arguments.push_back(configuration);
        } else if (descriptor.toolId == "forsetti.macos.archive" ||
                   descriptor.toolId == "forsetti.ios.archive") {
            command.arguments.push_back("-configuration");
            command.arguments.push_back("Release");
        } else {
            command.arguments.push_back("-configuration");
            command.arguments.push_back("Debug");
        }

        if (!sdk.empty()) {
            command.arguments.push_back("-sdk");
            command.arguments.push_back(sdk);
        } else if (request.platform == PlatformTarget::IOS &&
                   descriptor.toolId == "forsetti.ios.archive") {
            command.arguments.push_back("-sdk");
            command.arguments.push_back("iphoneos");
        }

        if (!destination.empty()) {
            command.arguments.push_back("-destination");
            command.arguments.push_back(destination);
        } else if (request.platform == PlatformTarget::IOS &&
                   descriptor.toolId == "forsetti.ios.build" &&
                   host.toolchain.simulatorControlAvailable) {
            command.arguments.push_back("-destination");
            command.arguments.push_back("generic/platform=iOS Simulator");
        } else if (request.platform == PlatformTarget::IOS &&
                   descriptor.toolId == "forsetti.ios.archive") {
            command.arguments.push_back("-destination");
            command.arguments.push_back("generic/platform=iOS");
        }

        if (descriptor.toolId == "forsetti.macos.build" || descriptor.toolId == "forsetti.ios.build") {
            command.arguments.push_back("build");
            return command;
        }

        if (descriptor.toolId == "forsetti.macos.test" || descriptor.toolId == "forsetti.ios.test") {
            if (request.platform == PlatformTarget::IOS && destination.empty()) {
                result.status = GovernanceToolStatus::Warning;
                result.summary = "iOS test governance requires a destination option.";
                result.findings.push_back(makeFinding(
                    descriptor.toolId,
                    descriptor.displayName,
                    "high",
                    "blocked",
                    "Provide request.options.destination for remote iOS test execution."));
                return std::nullopt;
            }
            command.arguments.push_back("test");
            return command;
        }

        if (descriptor.toolId == "forsetti.macos.archive") {
            const auto resolvedArchivePath = !archivePath.empty()
                ? archivePath
                : remotePathJoin(remotePathJoin(command.workingDirectory, "build"), scheme + ".xcarchive");
            command.arguments.push_back("-archivePath");
            command.arguments.push_back(resolvedArchivePath);
            command.arguments.push_back("archive");
            return command;
        }

        if (descriptor.toolId == "forsetti.ios.archive") {
            const auto resolvedArchivePath = !archivePath.empty()
                ? archivePath
                : remotePathJoin(remotePathJoin(command.workingDirectory, "build"), scheme + ".xcarchive");
            command.arguments.push_back("-archivePath");
            command.arguments.push_back(resolvedArchivePath);
            command.arguments.push_back("archive");
            return command;
        }

        return std::nullopt;
    }

    void executeWindowsTool(const GovernanceToolDescriptor& descriptor,
                            const GovernanceToolRequest& request,
                            GovernanceToolResult& result) const {
        const auto targetRoot = resolveTargetRoot(request);

        if (descriptor.toolId == "forsetti.windows.manifest.validate") {
            const auto scriptPath = targetRoot / "scripts" / "check-mastercontrol-forsetti.ps1";
            if (!std::filesystem::exists(scriptPath)) {
                result.status = GovernanceToolStatus::Failed;
                result.summary = "Master Control Forsetti compliance script was not found at " + scriptPath.string() + ".";
                result.findings.push_back(makeFinding(
                    descriptor.toolId,
                    descriptor.displayName,
                    "high",
                    "blocked",
                    "The repo-owned Forsetti compliance script is missing."));
                return;
            }

            const auto process = runPowerShellScript(scriptPath, targetRoot);
            result.rawOutput = process.stdoutText + process.stderrText;
            appendProcessFinding(result.findings, descriptor.toolId, descriptor.displayName, scriptPath.filename().string(), process);
            result.succeeded = process.launched && process.exitCode == 0;
            result.status = result.succeeded ? GovernanceToolStatus::Passed : GovernanceToolStatus::Failed;
            result.summary = result.succeeded
                ? "Repo-owned Forsetti compliance validation passed."
                : "Repo-owned Forsetti compliance validation failed.";
            return;
        }

        if (descriptor.toolId == "forsetti.windows.architecture.validate") {
            const auto scriptsRoot = findVendoredForsettiScriptsRoot(targetRoot);
            if (!scriptsRoot.has_value()) {
                result.status = GovernanceToolStatus::Failed;
                result.summary = "Vendored Forsetti architecture scripts were not found.";
                result.findings.push_back(makeFinding(
                    descriptor.toolId,
                    descriptor.displayName,
                    "high",
                    "blocked",
                    "The vendored Forsetti Scripts directory could not be located for architecture validation."));
                return;
            }

            bool allPassed = true;
            std::ostringstream combinedOutput;
            for (const auto& scriptName : { "check-architecture.ps1", "check-dependencies.ps1" }) {
                const auto scriptPath = *scriptsRoot / scriptName;
                if (!std::filesystem::exists(scriptPath)) {
                    allPassed = false;
                    result.findings.push_back(makeFinding(
                        descriptor.toolId,
                        descriptor.displayName,
                        "high",
                        "blocked",
                        std::string(scriptName) + " is missing from the vendored Forsetti Scripts directory."));
                    continue;
                }

                const auto process = runPowerShellScript(scriptPath, *scriptsRoot);
                combinedOutput << process.stdoutText << process.stderrText;
                appendProcessFinding(result.findings, descriptor.toolId, descriptor.displayName, scriptName, process);
                allPassed = allPassed && process.launched && process.exitCode == 0;
            }

            result.rawOutput = combinedOutput.str();
            result.succeeded = allPassed;
            result.status = allPassed ? GovernanceToolStatus::Passed : GovernanceToolStatus::Failed;
            result.summary = allPassed
                ? "Vendored Forsetti architecture and dependency validation passed."
                : "Vendored Forsetti architecture and dependency validation reported violations.";
            return;
        }

        if (descriptor.toolId == "forsetti.windows.module-boundary.inspect") {
            const auto modulesCMake = targetRoot / "src" / "MasterControlModules" / "CMakeLists.txt";
            const auto appCMake = targetRoot / "src" / "MasterControlApp" / "CMakeLists.txt";
            const auto manifestsRoot = targetRoot / "src" / "MasterControlModules" / "Resources" / "ForsettiManifests";
            const auto modulesCMakeText = readTextFile(modulesCMake);
            const auto appCMakeText = readTextFile(appCMake);

            result.findings.push_back(makeFinding(
                descriptor.toolId,
                descriptor.displayName,
                std::filesystem::exists(modulesCMake) ? "low" : "high",
                std::filesystem::exists(modulesCMake) ? "pass" : "blocked",
                std::filesystem::exists(modulesCMake)
                    ? "MasterControlModules CMake target is present."
                    : "MasterControlModules CMake target is missing."));
            result.findings.push_back(makeFinding(
                descriptor.toolId,
                descriptor.displayName,
                modulesCMakeText.find("ForsettiPlatform") == std::string::npos ? "low" : "high",
                modulesCMakeText.find("ForsettiPlatform") == std::string::npos ? "pass" : "blocked",
                modulesCMakeText.find("ForsettiPlatform") == std::string::npos
                    ? "MasterControlModules stays Core-only and does not link ForsettiPlatform."
                    : "MasterControlModules links ForsettiPlatform, which violates the Core-only module boundary."));
            result.findings.push_back(makeFinding(
                descriptor.toolId,
                descriptor.displayName,
                appCMakeText.find("MasterControlModules.cpp") == std::string::npos ? "low" : "high",
                appCMakeText.find("MasterControlModules.cpp") == std::string::npos ? "pass" : "blocked",
                appCMakeText.find("MasterControlModules.cpp") == std::string::npos
                    ? "MasterControlApp does not compile MasterControlModules.cpp directly."
                    : "MasterControlApp compiles MasterControlModules.cpp directly, breaking the module boundary."));
            result.findings.push_back(makeFinding(
                descriptor.toolId,
                descriptor.displayName,
                std::filesystem::exists(manifestsRoot) ? "low" : "high",
                std::filesystem::exists(manifestsRoot) ? "pass" : "blocked",
                std::filesystem::exists(manifestsRoot)
                    ? "Forsetti manifests are staged from the module resource tree."
                    : "Forsetti manifests are missing from src/MasterControlModules/Resources/ForsettiManifests."));

            result.succeeded = std::all_of(
                result.findings.begin(),
                result.findings.end(),
                [](const GovernanceFinding& finding) { return finding.status == "pass"; });
            result.status = result.succeeded ? GovernanceToolStatus::Passed : GovernanceToolStatus::Failed;
            result.summary = result.succeeded
                ? "Module boundary inspection passed."
                : "Module boundary inspection found one or more Forsetti violations.";
            return;
        }

        if (descriptor.toolId == "forsetti.windows.package.validate") {
            const auto candidates = candidatePayloadRoots(targetRoot);
            const auto payloadIterator = std::find_if(candidates.begin(), candidates.end(), [](const auto& candidate) {
                return hasPayloadLayout(candidate);
            });

            if (payloadIterator == candidates.end()) {
                result.status = GovernanceToolStatus::Failed;
                result.summary = "No staged Master Control payload root was found for package validation.";
                result.findings.push_back(makeFinding(
                    descriptor.toolId,
                    descriptor.displayName,
                    "high",
                    "blocked",
                    "Expected executables and staged web/manifest payloads were not found under the target path or its dist/* children."));
                return;
            }

            const auto& payloadRoot = *payloadIterator;
            const auto shareRoot = findSharePayloadRoot(payloadRoot).value_or(
                payloadRoot / "share" / "MasterControlOrchestrationServer");
            const std::array<std::pair<std::filesystem::path, std::string>, 5> requiredArtifacts = {{
                { payloadRoot / "MasterControlServiceHost.exe", "Service host executable is staged." },
                { payloadRoot / "MasterControlShell.exe", "Shell executable is staged." },
                { payloadRoot / "MasterControlBootstrapper.exe", "Bootstrapper executable is staged." },
                { shareRoot / "web" / "index.html", "Browser payload is staged." },
                { shareRoot / "ForsettiManifests" / "DashboardUIModule.json", "Forsetti UI manifest is staged." }
            }};

            for (const auto& [artifactPath, message] : requiredArtifacts) {
                result.findings.push_back(makeFinding(
                    descriptor.toolId,
                    descriptor.displayName,
                    std::filesystem::exists(artifactPath) ? "low" : "high",
                    std::filesystem::exists(artifactPath) ? "pass" : "blocked",
                    std::filesystem::exists(artifactPath)
                        ? message
                        : artifactPath.string() + " is missing from the staged payload."));
            }

            result.succeeded = std::all_of(
                result.findings.begin(),
                result.findings.end(),
                [](const GovernanceFinding& finding) { return finding.status == "pass"; });
            result.status = result.succeeded ? GovernanceToolStatus::Passed : GovernanceToolStatus::Failed;
            result.summary = result.succeeded
                ? "Staged payload validation passed."
                : "Staged payload validation found missing deployment artifacts.";
            result.rawOutput = payloadRoot.string();
            return;
        }

        result.status = GovernanceToolStatus::Failed;
        result.summary = "Unknown Windows governance tool ID '" + descriptor.toolId + "'.";
    }

    void executeAppleRemoteTool(const GovernanceToolDescriptor& descriptor,
                                const GovernanceToolRequest& request,
                                GovernanceToolResult& result,
                                AppleOperationRecord* appleOperation) {
        if (!appleRemoteHostService_) {
            result.status = GovernanceToolStatus::Failed;
            result.summary = "Apple remote host service is unavailable.";
            result.routeReason = "The Apple governance lane could not reach the CLU Apple host registry.";
            result.diagnosticSummary = "The Forsetti Apple remote host service is unavailable in the runtime.";
            result.findings.push_back(makeFinding(
                descriptor.toolId,
                descriptor.displayName,
                "high",
                "blocked",
                "The Apple remote host registry service is not available in the Forsetti runtime."));
            if (appleOperation != nullptr) {
                appleOperation->summary = result.summary;
                appleOperation->errorMessage = result.summary;
                appleOperation->routeReason = result.routeReason;
                appleOperation->diagnosticSummary = result.diagnosticSummary;
            }
            return;
        }

        std::optional<AppleRemoteHost> host;
        const auto preferredHostId = optionValue(request, { "hostId", "preferredHostId" });
        if (!preferredHostId.empty()) {
            host = appleRemoteHostService_->inspectHost(preferredHostId);
        } else {
            host = appleRemoteHostService_->selectHostForPlatform(request.platform);
        }
        if (!host.has_value()) {
            result.status = GovernanceToolStatus::Warning;
            result.summary = preferredHostId.empty()
                ? "No enabled Apple remote host is configured for platform " + platformKey(request.platform) + "."
                : "Preferred Apple remote host '" + preferredHostId + "' is not available.";
            result.routeReason = preferredHostId.empty()
                ? "CLU could not find an enabled Apple host for automatic " + platformKey(request.platform) + " routing."
                : "Requested Apple host '" + preferredHostId + "' was unavailable.";
            result.diagnosticSummary = preferredHostId.empty()
                ? "Register or enable an Apple host with ssh or companion_service transport for this platform lane."
                : "The requested Apple host could not be loaded from the CLU host registry.";
            result.findings.push_back(makeFinding(
                "governance.remote-toolchain",
                descriptor.displayName,
                "high",
                "blocked",
                preferredHostId.empty()
                    ? "Configure an Apple remote host with either ssh or companion_service transport before using this governance lane."
                    : "The requested Apple remote host could not be loaded from the CLU host registry."));
            if (appleOperation != nullptr) {
                appleOperation->summary = result.summary;
                appleOperation->errorMessage = preferredHostId.empty()
                    ? "No eligible Apple host is configured."
                    : "The requested Apple host is unavailable.";
                appleOperation->routeReason = result.routeReason;
                appleOperation->diagnosticSummary = result.diagnosticSummary;
            }
            return;
        }
        if (host->platforms.end() == std::find(host->platforms.begin(), host->platforms.end(), request.platform)) {
            result.status = GovernanceToolStatus::Warning;
            result.summary = "Apple host '" + host->displayName + "' does not advertise support for " +
                platformKey(request.platform) + ".";
            result.routeReason = "The selected Apple host does not publish the requested platform lane.";
            result.diagnosticSummary = host->transportSummary.empty()
                ? "The selected Apple host does not advertise the requested platform."
                : host->transportSummary + " does not advertise the requested platform.";
            result.findings.push_back(makeFinding(
                "governance.remote-toolchain",
                descriptor.displayName,
                "high",
                "blocked",
                "The selected Apple host does not declare the requested platform lane."));
            if (appleOperation != nullptr) {
                appleOperation->summary = result.summary;
                appleOperation->errorMessage = "Selected host does not support the requested platform.";
                appleOperation->routeReason = result.routeReason;
                appleOperation->diagnosticSummary = result.diagnosticSummary;
            }
            return;
        }

        const auto routeReason = buildAppleRouteReason(request, *host, preferredHostId);
        const auto selectedDeveloperDirectory = trimCopy(
            host->toolchain.developerDirectory.empty()
                ? host->preferredDeveloperDirectory
                : host->toolchain.developerDirectory);
        const auto credentialProfileSummary = buildAppleOperationCredentialProfileSummary(
            descriptor,
            request,
            *host,
            appleOperation == nullptr ? std::vector<std::string>{} : appleOperation->redactedRequestOptionKeys);
        const auto diagnosticSummary = joinAppleGovernanceSummaryParts(
            {
                buildAppleDiagnosticSummary(*host, request.platform),
                credentialProfileSummary
            },
            "; ");

        result.routeReason = routeReason;
        result.diagnosticSummary = diagnosticSummary;
        result.readinessIssues = host->readinessIssues;

        if (appleOperation != nullptr) {
            appleOperation->routeReason = routeReason;
            appleOperation->selectedDeveloperDirectory = selectedDeveloperDirectory;
            appleOperation->credentialProfileSummary = credentialProfileSummary;
            appleOperation->readinessIssues = host->readinessIssues;
            appleOperation->diagnosticSummary = joinAppleGovernanceSummaryParts(
                { appleOperation->diagnosticSummary, diagnosticSummary },
                "; ");
            markAppleOperationRunning(*appleOperation, *host);
        }

        const bool readyForPlatform =
            host->toolchain.reachable &&
            host->toolchain.xcodeInstalled &&
            ((request.platform == PlatformTarget::MacOS && host->toolchain.macosSdkAvailable) ||
             (request.platform == PlatformTarget::IOS &&
              host->toolchain.iosSdkAvailable &&
              host->signing.signingReady &&
              (host->toolchain.simulatorControlAvailable || host->toolchain.deviceControlAvailable)));

        result.rawOutput = nlohmann::json(*host).dump(2);
        result.findings.push_back(makeFinding(
            descriptor.toolId,
            descriptor.displayName,
            host->toolchain.reachable ? "low" : "high",
            host->toolchain.reachable ? "pass" : "blocked",
            "Selected Apple host '" + host->displayName + "' via " + to_string(host->transport) + "."));
        result.findings.push_back(makeFinding(
            descriptor.toolId,
            descriptor.displayName,
            host->toolchain.xcodeInstalled ? "low" : "high",
            host->toolchain.xcodeInstalled ? "pass" : "blocked",
            host->toolchain.xcodeInstalled
                ? "Xcode is available on the selected Apple host."
                : "Xcode is not available on the selected Apple host."));
        result.findings.push_back(makeFinding(
            descriptor.toolId,
            descriptor.displayName,
            readyForPlatform ? "low" : "medium",
            readyForPlatform ? "pass" : "warning",
            request.platform == PlatformTarget::MacOS
                ? (host->toolchain.macosSdkAvailable
                    ? "macOS SDK routing is ready."
                    : "The selected Apple host is missing the macOS SDK route.")
                : (host->toolchain.iosSdkAvailable
                    ? "iOS SDK routing is available."
                    : "The selected Apple host is missing the iOS SDK route.")));
        if (request.platform == PlatformTarget::IOS) {
            result.findings.push_back(makeFinding(
                descriptor.toolId,
                descriptor.displayName,
                host->signing.signingReady ? "low" : "high",
                host->signing.signingReady ? "pass" : "blocked",
                host->signing.signingReady
                    ? "iOS signing is ready on the selected Apple host."
                    : "iOS signing is not ready on the selected Apple host."));
        }

        if (descriptor.toolId == "forsetti.macos.toolchain.route" ||
            descriptor.toolId == "forsetti.ios.signing.route" ||
            descriptor.toolId == "forsetti.macos.remote-build.validate" ||
            descriptor.toolId == "forsetti.ios.remote-build.validate") {
            result.succeeded = readyForPlatform;
            result.status = readyForPlatform ? GovernanceToolStatus::Passed : GovernanceToolStatus::Warning;
            result.summary = readyForPlatform
                ? "Governance routed to Apple host '" + host->displayName + "' via " + to_string(host->transport) + "."
                : "Apple host '" + host->displayName + "' is configured, but not ready for " +
                    platformKey(request.platform) + " governance execution.";
            if (appleOperation != nullptr && !readyForPlatform) {
                appleOperation->errorMessage = "The selected Apple host is not ready for the requested platform.";
            }
            return;
        }

        if (descriptor.toolId == "forsetti.macos.build" ||
            descriptor.toolId == "forsetti.macos.test" ||
            descriptor.toolId == "forsetti.macos.archive" ||
            descriptor.toolId == "forsetti.macos.sign" ||
            descriptor.toolId == "forsetti.macos.notarize" ||
            descriptor.toolId == "forsetti.macos.staple" ||
            descriptor.toolId == "forsetti.ios.simulator.list" ||
            descriptor.toolId == "forsetti.ios.build" ||
            descriptor.toolId == "forsetti.ios.test" ||
            descriptor.toolId == "forsetti.ios.archive" ||
            descriptor.toolId == "forsetti.ios.export" ||
            descriptor.toolId == "forsetti.ios.device.install") {
            const auto command = buildAppleOperationalCommand(descriptor, request, *host, result);
            if (!command.has_value()) {
                result.succeeded = false;
                if (result.summary.empty()) {
                    result.status = GovernanceToolStatus::Warning;
                    result.summary = "Apple governance tool options are incomplete.";
                }
                if (appleOperation != nullptr) {
                    appleOperation->summary = result.summary;
                    appleOperation->errorMessage = result.summary;
                }
                return;
            }

            const auto execution = appleRemoteHostService_->executeCommand(host->hostId, *command);
            result.rawOutput = execution.stdoutText.empty() ? execution.stderrText : execution.stdoutText;
            if (!execution.rawResponse.empty()) {
                result.rawOutput = execution.rawResponse;
            }
            result.findings.push_back(makeFinding(
                descriptor.toolId,
                descriptor.displayName,
                execution.succeeded ? "low" : "high",
                execution.succeeded ? "pass" : "blocked",
                "Remote execution used host '" + host->displayName + "' via " + to_string(execution.transport) + "."));

            if (descriptor.toolId == "forsetti.ios.simulator.list") {
                result.succeeded = execution.succeeded;
                result.status = execution.succeeded ? GovernanceToolStatus::Passed : GovernanceToolStatus::Failed;
                result.summary = execution.succeeded
                    ? "Remote iOS simulator inventory retrieved from '" + host->displayName + "'."
                    : "Remote iOS simulator inventory failed on '" + host->displayName + "'.";
                if (!execution.errorMessage.empty()) {
                    result.findings.push_back(makeFinding(
                        descriptor.toolId,
                        descriptor.displayName,
                        "medium",
                        "warning",
                        execution.errorMessage));
                    if (appleOperation != nullptr) {
                        appleOperation->errorMessage = execution.errorMessage;
                    }
                }
                return;
            }

            result.succeeded = execution.succeeded;
            result.status = execution.succeeded ? GovernanceToolStatus::Passed : GovernanceToolStatus::Failed;
            result.summary = execution.succeeded
                ? descriptor.displayName + " completed on Apple host '" + host->displayName + "'."
                : descriptor.displayName + " failed on Apple host '" + host->displayName + "'.";
            if (!execution.errorMessage.empty()) {
                result.findings.push_back(makeFinding(
                    descriptor.toolId,
                    descriptor.displayName,
                    execution.succeeded ? "low" : "high",
                    execution.succeeded ? "pass" : "blocked",
                    execution.errorMessage));
                if (appleOperation != nullptr) {
                    appleOperation->errorMessage = execution.errorMessage;
                }
            }
            return;
        }

        result.succeeded = false;
        result.status = GovernanceToolStatus::Warning;
        result.summary =
            "Apple remote governance readiness is configured, but platform-specific inspection for '" +
            descriptor.toolId + "' will be completed in the next Apple execution slice.";
        result.findings.push_back(makeFinding(
            descriptor.toolId,
            descriptor.displayName,
            "medium",
            "warning",
            "Remote host selection is now real, but this specific Apple governance tool still needs a dedicated inspection handler."));
    }

    AppPaths paths_;
    std::shared_ptr<IAppleRemoteHostService> appleRemoteHostService_;
    std::shared_ptr<IConfigurationService> configurationService_;
    mutable std::mutex mutex_;
    std::condition_variable workerCv_;
    std::map<std::string, GovernanceToolDescriptor> toolsByKey_;
    std::vector<GovernanceToolResult> recentExecutions_;
    std::vector<AppleOperationRecord> recentAppleOperations_;
    std::deque<AppleQueuedTask> appleQueue_;
    std::optional<std::string> runningAppleOperationId_;
    bool stopWorker_ = false;
    std::thread worker_;
};

class BeaconService final : public IBeaconService {
public:
    BeaconService(std::shared_ptr<IConfigurationService> configurationService,
                  std::shared_ptr<ITelemetryService> telemetryService,
                  std::shared_ptr<IPlatformServiceCatalogService> platformServiceCatalogService)
        : configurationService_(std::move(configurationService))
        , telemetryService_(std::move(telemetryService))
        , platformServiceCatalogService_(std::move(platformServiceCatalogService)) {}

    BeaconAdvertisement currentAdvertisement() const override {
        const auto configuration = configurationService_->current();
        const auto snapshot = telemetryService_->captureSnapshot();
        return BeaconAdvertisement{
            configuration.instanceName,
            snapshot.hostName,
            snapshot.primaryIpAddress.empty() ? configuration.bindAddress : snapshot.primaryIpAddress,
            configuration.browserPort,
            configuration.browserPort,
            "online",
            platformServiceCatalogService_ ? platformServiceCatalogService_->listGateways() : std::vector<PlatformGatewayDescriptor>{}
        };
    }

    void start() override;
    void stop() override;
    ~BeaconService() override {
        stop();
    }

private:
    std::shared_ptr<IConfigurationService> configurationService_;
    std::shared_ptr<ITelemetryService> telemetryService_;
    std::shared_ptr<IPlatformServiceCatalogService> platformServiceCatalogService_;
    std::atomic<bool> running_{ false };
    std::thread worker_;
};

void BeaconService::start() {
    if (running_.exchange(true)) {
        return;
    }

    worker_ = std::thread([this]() {
        SOCKET socketHandle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (socketHandle == INVALID_SOCKET) {
            running_ = false;
            return;
        }

        BOOL broadcastEnabled = TRUE;
        setsockopt(socketHandle, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&broadcastEnabled), sizeof(broadcastEnabled));

        while (running_) {
            const auto configuration = configurationService_->current();
            if (!configuration.beaconEnabled) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                continue;
            }

            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_port = htons(configuration.beaconPort);
            address.sin_addr.s_addr = INADDR_BROADCAST;

            const auto payload = nlohmann::json(currentAdvertisement()).dump();
            sendto(socketHandle, payload.c_str(), static_cast<int>(payload.size()), 0, reinterpret_cast<sockaddr*>(&address), sizeof(address));
            std::this_thread::sleep_for(std::chrono::seconds(configuration.beaconBroadcastIntervalSeconds));
        }

        closesocket(socketHandle);
    });
}

void BeaconService::stop() {
    running_ = false;
    if (worker_.joinable()) {
        worker_.join();
    }
}

class AdminApiService final : public IAdminApiService {
public:
    AdminApiService(std::shared_ptr<ITelemetryService> telemetryService,
                    std::shared_ptr<IRuntimeInventoryService> inventoryService,
                    std::shared_ptr<IConfigurationService> configurationService,
                    std::shared_ptr<IPlatformServiceCatalogService> platformServiceCatalogService,
                    std::shared_ptr<IAppleRemoteHostService> appleRemoteHostService,
                    std::shared_ptr<IProviderRegistry> providerRegistry,
                    std::shared_ptr<IProviderCatalogService> providerCatalogService,
                    std::shared_ptr<IProviderCredentialStore> providerCredentialStore,
                    std::shared_ptr<IMcpServerCatalogService> mcpServerCatalogService,
                    std::shared_ptr<ISubAgentCatalogService> subAgentCatalogService,
                    std::shared_ptr<ISubAgentGroupService> subAgentGroupService,
                    std::shared_ptr<IProviderAssignmentService> providerAssignmentService,
                    std::shared_ptr<IProviderExecutionCatalogService> providerExecutionCatalogService,
                    std::shared_ptr<IProviderExecutionService> providerExecutionService,
                    std::shared_ptr<IInstallerOrchestrator> installerOrchestrator,
                    std::shared_ptr<IBootstrapRepoService> bootstrapRepoService,
                    std::shared_ptr<IZipBundleService> zipBundleService,
                    std::shared_ptr<IExportService> exportService,
                    std::shared_ptr<ICommandLogicUnitService> commandLogicUnitService,
                    std::shared_ptr<IForsettiSurfaceService> surfaceService)
        : telemetryService_(std::move(telemetryService))
        , inventoryService_(std::move(inventoryService))
        , configurationService_(std::move(configurationService))
        , platformServiceCatalogService_(std::move(platformServiceCatalogService))
        , appleRemoteHostService_(std::move(appleRemoteHostService))
        , providerRegistry_(std::move(providerRegistry))
        , providerCatalogService_(std::move(providerCatalogService))
        , providerCredentialStore_(std::move(providerCredentialStore))
        , mcpServerCatalogService_(std::move(mcpServerCatalogService))
        , subAgentCatalogService_(std::move(subAgentCatalogService))
        , subAgentGroupService_(std::move(subAgentGroupService))
        , providerAssignmentService_(std::move(providerAssignmentService))
        , providerExecutionCatalogService_(std::move(providerExecutionCatalogService))
        , providerExecutionService_(std::move(providerExecutionService))
        , installerOrchestrator_(std::move(installerOrchestrator))
        , bootstrapRepoService_(std::move(bootstrapRepoService))
        , zipBundleService_(std::move(zipBundleService))
        , exportService_(std::move(exportService))
        , commandLogicUnitService_(std::move(commandLogicUnitService))
        , surfaceService_(std::move(surfaceService)) {}

    DashboardSnapshot snapshot() override {
        // Use synchronous refresh so the snapshot always reflects the latest
        // configuration state — callers expect a consistent view.
        inventoryService_->refresh();

        DashboardSnapshot snapshot;
        snapshot.telemetry = telemetryService_->captureSnapshot();
        snapshot.endpoints = inventoryService_->listEndpoints();
        snapshot.providers = providerRegistry_->listProviders();
        snapshot.providerCapabilities = providerCatalogService_->listCapabilities();
        snapshot.providerCredentialStatuses = providerCredentialStore_->listStatuses();
        snapshot.subAgentGroups = subAgentGroupService_->listGroups();
        snapshot.providerAssignmentTargets = providerAssignmentService_->listTargets();
        snapshot.providerAssignments = providerAssignmentService_->listAssignments();
        snapshot.providerExecutionRegistrations = providerExecutionCatalogService_->listRegistrations();
        snapshot.providerExecutionHistory = providerExecutionService_->history();
        for (auto& provider : snapshot.providers) {
            const auto statusIterator = std::find_if(
                snapshot.providerCredentialStatuses.begin(),
                snapshot.providerCredentialStatuses.end(),
                [&provider](const ProviderCredentialStatus& status) { return status.providerId == provider.id; });
            provider.credentialsConfigured = statusIterator != snapshot.providerCredentialStatuses.end() &&
                statusIterator->configured;
        }
        snapshot.installHistory = installerOrchestrator_->history();
        snapshot.exports = exportService_->generateExports();
        const auto configuration = configurationService_->current();
        snapshot.resourceAllocation = configuration.resourceAllocation;
        snapshot.security = configuration.security;
        snapshot.governance = commandLogicUnitService_->currentGovernance();
        snapshot.appleRemoteHosts = appleRemoteHostService_
            ? appleRemoteHostService_->listHosts()
            : std::vector<AppleRemoteHost>{};
        snapshot.platformGateways = platformServiceCatalogService_
            ? platformServiceCatalogService_->listGateways()
            : std::vector<PlatformGatewayDescriptor>{};
        snapshot.governanceServers = platformServiceCatalogService_
            ? platformServiceCatalogService_->listGovernanceServers()
            : std::vector<GovernanceServerDescriptor>{};
        snapshot.surface = surfaceService_->currentSurface();
        for (const auto& gateway : snapshot.platformGateways) {
            snapshot.endpoints.push_back(RuntimeEndpoint{
                gateway.serviceId,
                gateway.displayName,
                EndpointKind::Gateway,
                gateway.ipAddress,
                gateway.port,
                "http",
                endpointStatusFromServiceState(gateway.status),
                "Forsetti platform gateway for " + platformKey(gateway.platform) + " clients.",
                timestampNowUtc(),
                gateway.gatewayPath,
                {},
                false
            });
        }
        for (const auto& governance : snapshot.governanceServers) {
            const auto gatewayIterator = std::find_if(
                snapshot.platformGateways.begin(),
                snapshot.platformGateways.end(),
                [&governance](const PlatformGatewayDescriptor& gateway) {
                    return gateway.serviceId == governance.gatewayServiceId;
                });
            snapshot.endpoints.push_back(RuntimeEndpoint{
                governance.serviceId,
                governance.displayName,
                EndpointKind::MCPServer,
                gatewayIterator != snapshot.platformGateways.end() ? gatewayIterator->ipAddress : std::string(),
                gatewayIterator != snapshot.platformGateways.end() ? gatewayIterator->port : configuration.browserPort,
                "http",
                endpointStatusFromServiceState(governance.status),
                "Governance MCP server lane for " + platformKey(governance.platform) + " enforcement.",
                timestampNowUtc(),
                governance.routePath,
                platformKey(governance.platform),
                false
            });
        }
        return snapshot;
    }

    GovernanceSnapshot governance() const override {
        return commandLogicUnitService_->currentGovernance();
    }

    GovernanceToolResult executeGovernanceToolJson(const std::string& requestBody) override {
        return commandLogicUnitService_->executeGovernanceTool(
            nlohmann::json::parse(requestBody).get<GovernanceToolRequest>());
    }

    OperationResult cancelAppleOperationJson(const std::string& requestBody) override {
        const auto request = nlohmann::json::parse(requestBody).get<AppleOperationCancelRequest>();
        return commandLogicUnitService_->cancelAppleOperation(request.operationId);
    }

    OperationResult applyConfigurationJson(const std::string& requestBody,
                                           bool confirmUnsafeChanges) override {
        return configurationService_->update(nlohmann::json::parse(requestBody).get<AppConfiguration>(), confirmUnsafeChanges);
    }

    OperationResult upsertProviderJson(const std::string& requestBody) override {
        const auto provider = nlohmann::json::parse(requestBody).get<ProviderConnection>();
        if (provider.allowAutonomousControl && commandLogicUnitService_) {
            const auto decision = commandLogicUnitService_->enforceAction(GovernanceEnforcementRequest{
                GovernanceActionKind::ProviderAutonomyEnable,
                {},
                provider.id,
                {},
                false
            });
            if (!decision.allowed) {
                return OperationResult{ false, false, decision.message };
            }
        }
        return providerRegistry_->upsertProvider(provider);
    }

    AutoConnectResult autoConnectProviderJson(const std::string& requestBody) override {
        AutoConnectRequest request;
        try {
            if (requestBody.empty()) {
                AutoConnectResult err;
                err.errorMessage = "Request body is empty. Provide JSON with kind + credentials.";
                err.summary = err.errorMessage;
                AutoConnectStep step;
                step.stage = "parse";
                step.succeeded = false;
                step.message = err.errorMessage;
                err.steps.push_back(std::move(step));
                return err;
            }
            request = nlohmann::json::parse(requestBody).get<AutoConnectRequest>();
        } catch (const std::exception& parseErr) {
            // Previously this path threw uncaught, which surfaced to the HTTP
            // layer as HTTP 500 with an empty body — operator saw a dead
            // connection without any clue what was wrong. Return a structured
            // failure with the parse reason instead.
            AutoConnectResult err;
            err.errorMessage = std::string("Could not parse request: ") + parseErr.what();
            err.summary = err.errorMessage;
            AutoConnectStep step;
            step.stage = "parse";
            step.succeeded = false;
            step.message = err.errorMessage;
            err.steps.push_back(std::move(step));
            return err;
        }
        // Autonomy enforcement is applied to the derived provider id inside
        // autoConnectProvider's register-provider stage, so we do not need a
        // pre-check here — the registry will emit a step error if the CLU
        // denies the connection. We still fail fast if the kind is missing.
        if (request.allowAutonomousControl && commandLogicUnitService_) {
            const auto decision = commandLogicUnitService_->enforceAction(GovernanceEnforcementRequest{
                GovernanceActionKind::ProviderAutonomyEnable,
                {},
                {},
                {},
                false
            });
            if (!decision.allowed) {
                AutoConnectResult denied;
                denied.errorMessage = decision.message;
                denied.summary = decision.message;
                AutoConnectStep step;
                step.stage = "governance-check";
                step.succeeded = false;
                step.message = decision.message;
                denied.steps.push_back(std::move(step));
                return denied;
            }
        }
        return providerRegistry_->autoConnectProvider(request);
    }

    OperationResult upsertProviderCredentialsJson(const std::string& requestBody) override {
        return providerCredentialStore_->upsertCredentials(nlohmann::json::parse(requestBody).get<ProviderCredentialUpdate>());
    }

    OperationResult upsertAppleRemoteHostJson(const std::string& requestBody) override {
        return appleRemoteHostService_->upsertHost(nlohmann::json::parse(requestBody).get<AppleRemoteHost>());
    }

    OperationResult removeAppleRemoteHostJson(const std::string& requestBody) override {
        return appleRemoteHostService_->removeHost(
            nlohmann::json::parse(requestBody).get<AppleRemoteHostRemovalRequest>().hostId);
    }

    OperationResult upsertMcpServerJson(const std::string& requestBody) override {
        return mcpServerCatalogService_->upsertMcpServer(nlohmann::json::parse(requestBody).get<RuntimeEndpoint>());
    }

    OperationResult removeMcpServerJson(const std::string& requestBody) override {
        return mcpServerCatalogService_->removeMcpServer(
            nlohmann::json::parse(requestBody).get<McpServerRemovalRequest>().mcpServerId);
    }

    OperationResult upsertSubAgentJson(const std::string& requestBody) override {
        return subAgentCatalogService_->upsertSubAgent(nlohmann::json::parse(requestBody).get<RuntimeEndpoint>());
    }

    OperationResult removeSubAgentJson(const std::string& requestBody) override {
        return subAgentCatalogService_->removeSubAgent(
            nlohmann::json::parse(requestBody).get<SubAgentRemovalRequest>().subAgentId);
    }

    OperationResult upsertSubAgentGroupJson(const std::string& requestBody) override {
        return subAgentGroupService_->upsertGroup(nlohmann::json::parse(requestBody).get<SubAgentGroupDefinition>());
    }

    OperationResult removeSubAgentGroupJson(const std::string& requestBody) override {
        return subAgentGroupService_->removeGroup(
            nlohmann::json::parse(requestBody).get<SubAgentGroupRemovalRequest>().groupId);
    }

    OperationResult upsertProviderAssignmentJson(const std::string& requestBody) override {
        return providerAssignmentService_->upsertAssignment(nlohmann::json::parse(requestBody).get<ProviderAssignment>());
    }

    ProviderExecutionRecord executeProviderTaskJson(const std::string& requestBody) override {
        return providerExecutionService_->execute(nlohmann::json::parse(requestBody).get<ProviderExecutionRequest>());
    }

    OperationResult installPackageJson(const std::string& requestBody) override {
        const auto spec = nlohmann::json::parse(requestBody).get<InstallerPackageSpec>();
        if (commandLogicUnitService_) {
            const auto decision = commandLogicUnitService_->enforceAction(GovernanceEnforcementRequest{
                GovernanceActionKind::RemoteInstall,
                {},
                {},
                !spec.source.empty() ? spec.source : spec.localPath,
                spec.allowUntrustedExecution
            });
            if (!decision.allowed) {
                return OperationResult{ false, false, decision.message };
            }
        }
        return installerOrchestrator_->installPackage(spec);
    }

    OperationResult installRepoJson(const std::string& requestBody) override {
        const auto spec = nlohmann::json::parse(requestBody).get<BootstrapRepoSpec>();
        if (commandLogicUnitService_) {
            const auto decision = commandLogicUnitService_->enforceAction(GovernanceEnforcementRequest{
                GovernanceActionKind::RemoteInstall,
                {},
                {},
                spec.repositoryUrl,
                spec.allowUntrustedExecution
            });
            if (!decision.allowed) {
                return OperationResult{ false, false, decision.message };
            }
        }
        return bootstrapRepoService_->installFromRepository(spec);
    }

    OperationResult installZipJson(const std::string& requestBody) override {
        const auto spec = nlohmann::json::parse(requestBody).get<ZipBundleSpec>();
        if (commandLogicUnitService_) {
            const auto decision = commandLogicUnitService_->enforceAction(GovernanceEnforcementRequest{
                GovernanceActionKind::RemoteInstall,
                {},
                {},
                spec.source,
                false
            });
            if (!decision.allowed) {
                return OperationResult{ false, false, decision.message };
            }
        }
        return zipBundleService_->installFromZipBundle(spec);
    }

private:
    std::shared_ptr<ITelemetryService> telemetryService_;
    std::shared_ptr<IRuntimeInventoryService> inventoryService_;
    std::shared_ptr<IConfigurationService> configurationService_;
    std::shared_ptr<IPlatformServiceCatalogService> platformServiceCatalogService_;
    std::shared_ptr<IAppleRemoteHostService> appleRemoteHostService_;
    std::shared_ptr<IProviderRegistry> providerRegistry_;
    std::shared_ptr<IProviderCatalogService> providerCatalogService_;
    std::shared_ptr<IProviderCredentialStore> providerCredentialStore_;
    std::shared_ptr<IMcpServerCatalogService> mcpServerCatalogService_;
    std::shared_ptr<ISubAgentCatalogService> subAgentCatalogService_;
    std::shared_ptr<ISubAgentGroupService> subAgentGroupService_;
    std::shared_ptr<IProviderAssignmentService> providerAssignmentService_;
    std::shared_ptr<IProviderExecutionCatalogService> providerExecutionCatalogService_;
    std::shared_ptr<IProviderExecutionService> providerExecutionService_;
    std::shared_ptr<IInstallerOrchestrator> installerOrchestrator_;
    std::shared_ptr<IBootstrapRepoService> bootstrapRepoService_;
    std::shared_ptr<IZipBundleService> zipBundleService_;
    std::shared_ptr<IExportService> exportService_;
    std::shared_ptr<ICommandLogicUnitService> commandLogicUnitService_;
    std::shared_ptr<IForsettiSurfaceService> surfaceService_;
};

struct HttpRequest final {
    std::string method;
    std::string path;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
};

struct HttpResponse final {
    int statusCode = 200;
    std::string contentType = "application/json";
    std::string body;
};

// ---------------------------------------------------------------------------
// ActivityEventRing — fixed-capacity, thread-safe, monotonically-indexed in-
// memory ring buffer of ActivityEvent records. Every inbound admin API
// request, outbound provider execution, governance decision, auto-connect
// call, and service lifecycle transition appends here so the shell and
// browser dashboard can render a live stream of "commands and requests".
// Readers query with a cursor id and get back events strictly newer than
// that cursor plus the current high-water-mark id.
// ---------------------------------------------------------------------------
class ActivityEventRing final {
public:
    static constexpr size_t kCapacity = 512;

    void append(const ActivityEvent& input) {
        std::lock_guard<std::mutex> lock(mutex_);
        ActivityEvent event = input;
        ++nextSequence_;
        event.id = std::to_string(nextSequence_);
        if (event.timestampUtc.empty()) {
            event.timestampUtc = currentUtcTimestamp();
        }
        ring_.push_back(std::move(event));
        if (ring_.size() > kCapacity) {
            ring_.pop_front();
        }
    }

    // Returns events with id > sinceId (sinceId may be empty = return all).
    // Also returns the current high-water-mark id so the caller can poll
    // incrementally.
    struct Snapshot {
        std::vector<ActivityEvent> events;
        std::string highWaterMarkId;
    };
    Snapshot read(const std::string& sinceId, size_t maxCount = kCapacity) const {
        std::lock_guard<std::mutex> lock(mutex_);
        Snapshot out;
        out.highWaterMarkId = std::to_string(nextSequence_);

        uint64_t sinceSeq = 0;
        if (!sinceId.empty()) {
            try { sinceSeq = std::stoull(sinceId); } catch (...) { sinceSeq = 0; }
        }

        size_t collected = 0;
        for (const auto& event : ring_) {
            if (collected >= maxCount) break;
            uint64_t eventSeq = 0;
            try { eventSeq = std::stoull(event.id); } catch (...) { continue; }
            if (eventSeq > sinceSeq) {
                out.events.push_back(event);
                ++collected;
            }
        }
        return out;
    }

private:
    static std::string currentUtcTimestamp() {
        const auto now = std::chrono::system_clock::now();
        const auto nowTime = std::chrono::system_clock::to_time_t(now);
        const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                               now.time_since_epoch()).count() % 1000;
        tm utc{};
        gmtime_s(&utc, &nowTime);
        char buffer[40]{};
        std::snprintf(
            buffer, sizeof(buffer),
            "%04d-%02d-%02dT%02d:%02d:%02d.%03lldZ",
            utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
            utc.tm_hour, utc.tm_min, utc.tm_sec,
            static_cast<long long>(nowMs));
        return buffer;
    }

    mutable std::mutex mutex_;
    std::deque<ActivityEvent> ring_;
    uint64_t nextSequence_ = 0;
};

// Process-global activity ring. Using a static inside a function avoids the
// static-initialization-order trap across translation units.
ActivityEventRing& globalActivityRing() {
    static ActivityEventRing instance;
    return instance;
}

class SimpleHttpServer final {
public:
    using RequestHandler = std::function<HttpResponse(const HttpRequest&)>;

    SimpleHttpServer(std::string bindAddress, uint16_t port, RequestHandler handler)
        : bindAddress_(std::move(bindAddress))
        , port_(port)
        , handler_(std::move(handler)) {}

    bool start();
    void stop();
    ~SimpleHttpServer() { stop(); }

private:
    static std::string reasonPhrase(int statusCode);
    static HttpRequest parseRequest(const std::string& rawRequest);
    static void sendResponse(SOCKET client, const HttpResponse& response);
    void run();
    void handleClient(SOCKET client);

    std::string bindAddress_;
    uint16_t port_;
    RequestHandler handler_;
    std::atomic<bool> running_{ false };
    std::atomic<bool> startupComplete_{ false };
    std::atomic<bool> startupSucceeded_{ false };
    std::thread worker_;
    SOCKET listenSocket_ = INVALID_SOCKET;
    std::mutex startupMutex_;
    std::condition_variable startupCv_;
};

bool SimpleHttpServer::start() {
    if (running_.exchange(true)) {
        return true;
    }

    startupComplete_ = false;
    startupSucceeded_ = false;
    worker_ = std::thread([this]() { run(); });

    std::unique_lock<std::mutex> lock(startupMutex_);
    startupCv_.wait(lock, [this]() { return startupComplete_.load(); });
    return startupSucceeded_.load();
}

void SimpleHttpServer::stop() {
    running_ = false;
    if (listenSocket_ != INVALID_SOCKET) {
        closesocket(listenSocket_);
        listenSocket_ = INVALID_SOCKET;
    }
    if (worker_.joinable()) {
        worker_.join();
    }
}

std::string SimpleHttpServer::reasonPhrase(const int statusCode) {
    switch (statusCode) {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 500: return "Internal Server Error";
        default: return "OK";
    }
}

HttpRequest SimpleHttpServer::parseRequest(const std::string& rawRequest) {
    HttpRequest request;
    std::istringstream stream(rawRequest);
    std::string requestLine;
    std::getline(stream, requestLine);
    if (!requestLine.empty() && requestLine.back() == '\r') {
        requestLine.pop_back();
    }

    std::istringstream requestLineStream(requestLine);
    requestLineStream >> request.method >> request.path;

    std::string headerLine;
    while (std::getline(stream, headerLine)) {
        if (headerLine == "\r" || headerLine.empty()) {
            break;
        }
        if (headerLine.back() == '\r') {
            headerLine.pop_back();
        }
        const auto separator = headerLine.find(':');
        if (separator == std::string::npos) {
            continue;
        }
        auto name = headerLine.substr(0, separator);
        auto value = headerLine.substr(separator + 1);
        if (!value.empty() && value.front() == ' ') {
            value.erase(value.begin());
        }
        request.headers[name] = value;
    }

    std::ostringstream body;
    body << stream.rdbuf();
    request.body = body.str();
    return request;
}

void SimpleHttpServer::sendResponse(SOCKET client, const HttpResponse& response) {
    std::ostringstream stream;
    stream << "HTTP/1.1 " << response.statusCode << ' ' << reasonPhrase(response.statusCode) << "\r\n";
    stream << "Content-Type: " << response.contentType << "\r\n";
    stream << "Content-Length: " << response.body.size() << "\r\n";
    stream << "Connection: close\r\n\r\n";
    stream << response.body;

    const auto data = stream.str();
    send(client, data.c_str(), static_cast<int>(data.size()), 0);
}

void SimpleHttpServer::run() {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    addrinfo* results = nullptr;
    const auto portText = std::to_string(port_);
    if (getaddrinfo(bindAddress_.c_str(), portText.c_str(), &hints, &results) != 0) {
        running_ = false;
        {
            std::lock_guard<std::mutex> lock(startupMutex_);
            startupSucceeded_ = false;
            startupComplete_ = true;
        }
        startupCv_.notify_all();
        return;
    }

    for (addrinfo* result = results; result != nullptr; result = result->ai_next) {
        listenSocket_ = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
        if (listenSocket_ == INVALID_SOCKET) {
            continue;
        }

        const int reuse = 1;
        setsockopt(listenSocket_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

        if (bind(listenSocket_, result->ai_addr, static_cast<int>(result->ai_addrlen)) == 0 &&
            listen(listenSocket_, SOMAXCONN) == 0) {
            break;
        }

        closesocket(listenSocket_);
        listenSocket_ = INVALID_SOCKET;
    }

    freeaddrinfo(results);
    if (listenSocket_ == INVALID_SOCKET) {
        running_ = false;
        {
            std::lock_guard<std::mutex> lock(startupMutex_);
            startupSucceeded_ = false;
            startupComplete_ = true;
        }
        startupCv_.notify_all();
        return;
    }

    {
        std::lock_guard<std::mutex> lock(startupMutex_);
        startupSucceeded_ = true;
        startupComplete_ = true;
    }
    startupCv_.notify_all();

    while (running_) {
        SOCKET client = accept(listenSocket_, nullptr, nullptr);
        if (client == INVALID_SOCKET) {
            continue;
        }
        handleClient(client);
        closesocket(client);
    }
}

void SimpleHttpServer::handleClient(SOCKET client) {
    std::string requestBuffer;
    char chunk[4096]{};
    int bytesRead = 0;
    while ((bytesRead = recv(client, chunk, static_cast<int>(sizeof(chunk)), 0)) > 0) {
        requestBuffer.append(chunk, chunk + bytesRead);
        const auto headerEnd = requestBuffer.find("\r\n\r\n");
        if (headerEnd == std::string::npos) {
            continue;
        }
        const auto contentLengthPosition = requestBuffer.find("Content-Length:");
        if (contentLengthPosition == std::string::npos) {
            break;
        }

        const auto lineEnd = requestBuffer.find("\r\n", contentLengthPosition);
        const auto value = requestBuffer.substr(contentLengthPosition + 15, lineEnd - (contentLengthPosition + 15));
        const auto contentLength = static_cast<size_t>(std::stoi(value));
        if (requestBuffer.size() >= headerEnd + 4 + contentLength) {
            break;
        }
    }

    const auto request = parseRequest(requestBuffer);
    const auto response = handler_(request);
    sendResponse(client, response);
}

std::string contentTypeForPath(const std::filesystem::path& path) {
    const auto extension = path.extension().string();
    if (extension == ".css") {
        return "text/css";
    }
    if (extension == ".js") {
        return "application/javascript";
    }
    if (extension == ".json") {
        return "application/json";
    }
    return "text/html; charset=utf-8";
}

} // namespace

class MasterControlApplication::Impl final {
public:
    Impl()
        : winsock_(std::make_unique<ScopedWinsock>())
        , paths_(resolveAppPaths())
        , state_(std::make_shared<SharedState>()) {}

    bool initialize();
    void shutdown();
    void requestStop() { stopRequested_ = true; }
    int runInteractive();
    std::string browserUrl() const;
    DashboardSnapshot snapshot() { return adminApiService_->snapshot(); }
    GovernanceToolResult executeGovernanceToolJson(const std::string& requestBody) { return adminApiService_->executeGovernanceToolJson(requestBody); }
    OperationResult cancelAppleOperationJson(const std::string& requestBody) { return adminApiService_->cancelAppleOperationJson(requestBody); }
    OperationResult applyConfigurationJson(const std::string& requestBody, bool confirmUnsafeChanges) { return adminApiService_->applyConfigurationJson(requestBody, confirmUnsafeChanges); }
    OperationResult upsertProviderJson(const std::string& requestBody) { return adminApiService_->upsertProviderJson(requestBody); }
    AutoConnectResult autoConnectProviderJson(const std::string& requestBody) { return adminApiService_->autoConnectProviderJson(requestBody); }
    OperationResult upsertProviderCredentialsJson(const std::string& requestBody) { return adminApiService_->upsertProviderCredentialsJson(requestBody); }
    OperationResult upsertAppleRemoteHostJson(const std::string& requestBody) { return adminApiService_->upsertAppleRemoteHostJson(requestBody); }
    OperationResult removeAppleRemoteHostJson(const std::string& requestBody) { return adminApiService_->removeAppleRemoteHostJson(requestBody); }
    OperationResult upsertMcpServerJson(const std::string& requestBody) { return adminApiService_->upsertMcpServerJson(requestBody); }
    OperationResult removeMcpServerJson(const std::string& requestBody) { return adminApiService_->removeMcpServerJson(requestBody); }
    OperationResult upsertSubAgentJson(const std::string& requestBody) { return adminApiService_->upsertSubAgentJson(requestBody); }
    OperationResult removeSubAgentJson(const std::string& requestBody) { return adminApiService_->removeSubAgentJson(requestBody); }
    OperationResult upsertSubAgentGroupJson(const std::string& requestBody) { return adminApiService_->upsertSubAgentGroupJson(requestBody); }
    OperationResult removeSubAgentGroupJson(const std::string& requestBody) { return adminApiService_->removeSubAgentGroupJson(requestBody); }
    OperationResult upsertProviderAssignmentJson(const std::string& requestBody) { return adminApiService_->upsertProviderAssignmentJson(requestBody); }
    ProviderExecutionRecord executeProviderTaskJson(const std::string& requestBody) { return adminApiService_->executeProviderTaskJson(requestBody); }
    OperationResult installPackageJson(const std::string& requestBody) { return adminApiService_->installPackageJson(requestBody); }
    OperationResult installRepoJson(const std::string& requestBody) { return adminApiService_->installRepoJson(requestBody); }
    OperationResult installZipJson(const std::string& requestBody) { return adminApiService_->installZipJson(requestBody); }
    BeaconAdvertisement beaconAdvertisement() const { return beaconService_->currentAdvertisement(); }

private:
    void registerConfigurationDefaults();
    void createForsettiRuntime();
    void activateDefaultModules();
    nlohmann::json forsettiModuleCatalog() const;
    OperationResult manageForsettiModule(const std::string& moduleId, const std::string& action);
    HttpResponse handleHttpRequest(const HttpRequest& request);
    HttpResponse staticFileResponse(std::string path) const;

    template <typename T>
    static HttpResponse jsonResponse(const T& value, int statusCode = 200) {
        return HttpResponse{ statusCode, "application/json", nlohmann::json(value).dump(2) };
    }

    std::unique_ptr<ScopedWinsock> winsock_;
    AppPaths paths_;
    std::shared_ptr<SharedState> state_;
    std::shared_ptr<IConfigurationService> configurationService_;
    std::shared_ptr<IResourceAllocationService> resourceAllocationService_;
    std::shared_ptr<ITelemetryService> telemetryService_;
    std::shared_ptr<IRuntimeInventoryService> inventoryService_;
    std::shared_ptr<IPlatformServiceCatalogService> platformServiceCatalogService_;
    std::shared_ptr<IAppleRemoteHostService> appleRemoteHostService_;
    std::shared_ptr<IPackageTrustEvaluator> trustEvaluator_;
    std::shared_ptr<InstallerOrchestrator> installerOrchestrator_;
    std::shared_ptr<IProviderRegistry> providerRegistry_;
    std::shared_ptr<IProviderCatalogService> providerCatalogService_;
    std::shared_ptr<IProviderCredentialStore> providerCredentialStore_;
    std::shared_ptr<IMcpServerCatalogService> mcpServerCatalogService_;
    std::shared_ptr<ISubAgentCatalogService> subAgentCatalogService_;
    std::shared_ptr<ISubAgentGroupService> subAgentGroupService_;
    std::shared_ptr<IProviderAssignmentService> providerAssignmentService_;
    std::shared_ptr<IProviderExecutionCatalogService> providerExecutionCatalogService_;
    std::shared_ptr<IProviderExecutionService> providerExecutionService_;
    std::shared_ptr<ProviderCliSignInService> providerCliSignInService_;
    std::shared_ptr<IExportService> exportService_;
    std::shared_ptr<IPlatformGovernanceToolService> platformGovernanceToolService_;
    std::shared_ptr<ICommandLogicUnitService> commandLogicUnitService_;
    std::shared_ptr<IModuleControlSurfaceService> controlSurfaceService_;
    std::shared_ptr<IForsettiSurfaceService> surfaceService_;
    std::shared_ptr<IBeaconService> beaconService_;
    std::shared_ptr<IAdminApiService> adminApiService_;
    std::shared_ptr<Forsetti::UISurfaceManager> surfaceManager_;
    std::shared_ptr<Forsetti::IEntitlementProvider> entitlementProvider_;
    std::shared_ptr<FileBackedEntitlementProvider> fileBackedEntitlementProvider_;
    std::unique_ptr<Forsetti::ForsettiRuntime> runtime_;
    std::unique_ptr<SimpleHttpServer> httpServer_;
    std::atomic<bool> stopRequested_{ false };
    bool initialized_ = false;
};

bool MasterControlApplication::Impl::initialize() {
    configurationService_ = std::make_shared<FileBackedConfigurationService>(state_, paths_.configurationFile);
    resourceAllocationService_ = std::make_shared<ResourceAllocationService>(state_, paths_.configurationFile);
    telemetryService_ = std::make_shared<WindowsHostTelemetryService>();
    inventoryService_ = std::make_shared<RuntimeInventoryService>(state_);
    appleRemoteHostService_ = std::make_shared<AppleRemoteHostService>(state_, paths_.configurationFile);
    trustEvaluator_ = std::make_shared<PackageTrustEvaluator>(state_);
    installerOrchestrator_ = std::make_shared<InstallerOrchestrator>(state_, trustEvaluator_, paths_);
    providerCatalogService_ = std::make_shared<ProviderCatalogService>();
    providerRegistry_ = std::make_shared<ProviderRegistryService>(state_, paths_.configurationFile, providerCatalogService_);
    providerCredentialStore_ = std::make_shared<ProviderCredentialStore>(paths_.providerCredentialsFile, providerRegistry_, providerCatalogService_);
    mcpServerCatalogService_ = std::make_shared<McpServerCatalogService>(state_, paths_.configurationFile, inventoryService_);
    subAgentCatalogService_ = std::make_shared<SubAgentCatalogService>(state_, paths_.configurationFile, inventoryService_);
    subAgentGroupService_ = std::make_shared<SubAgentGroupService>(state_, paths_.configurationFile, inventoryService_);
    providerAssignmentService_ = std::make_shared<ProviderAssignmentService>(
        state_,
        paths_.configurationFile,
        inventoryService_,
        subAgentGroupService_,
        providerRegistry_);

    // Late-bind the Auto-Connect dependencies on the provider registry. Both
    // the credential store and the assignment service are created *after* the
    // registry (the credential store takes a shared_ptr to the registry, which
    // would cycle if held the other way). Passing weak_ptrs breaks the cycle
    // and lets autoConnectProvider() drive all three in a single call.
    if (auto concreteRegistry = std::dynamic_pointer_cast<ProviderRegistryService>(providerRegistry_)) {
        concreteRegistry->wireAutoConnectDependencies(
            std::weak_ptr<IProviderCredentialStore>(providerCredentialStore_),
            std::weak_ptr<IProviderAssignmentService>(providerAssignmentService_));
    }
    exportService_ = std::make_shared<ExportService>(inventoryService_, configurationService_);
    platformServiceCatalogService_ = std::make_shared<PlatformServiceCatalogService>(configurationService_, telemetryService_);
    platformGovernanceToolService_ = std::make_shared<PlatformGovernanceToolService>(
        paths_,
        appleRemoteHostService_,
        configurationService_);
    commandLogicUnitService_ = std::make_shared<CommandLogicUnitService>(
        paths_.cluProfileFile,
        configurationService_,
        providerRegistry_,
        providerAssignmentService_,
        installerOrchestrator_,
        exportService_,
        appleRemoteHostService_,
        platformServiceCatalogService_,
        platformGovernanceToolService_);
    providerExecutionCatalogService_ = std::make_shared<ProviderExecutionCatalogService>();
    providerExecutionService_ = std::make_shared<ProviderExecutionService>(
        state_,
        providerRegistry_,
        providerCredentialStore_,
        providerAssignmentService_,
        inventoryService_,
        providerExecutionCatalogService_,
        commandLogicUnitService_);
    providerCliSignInService_ = std::make_shared<ProviderCliSignInService>(providerRegistry_, providerCatalogService_);
    controlSurfaceService_ = std::make_shared<ModuleControlSurfaceService>();
    beaconService_ = std::make_shared<BeaconService>(configurationService_, telemetryService_, platformServiceCatalogService_);
    registerConfigurationDefaults();
    createForsettiRuntime();

    adminApiService_ = std::make_shared<AdminApiService>(
        telemetryService_,
        inventoryService_,
        configurationService_,
        platformServiceCatalogService_,
        appleRemoteHostService_,
        providerRegistry_,
        providerCatalogService_,
        providerCredentialStore_,
        mcpServerCatalogService_,
        subAgentCatalogService_,
        subAgentGroupService_,
        providerAssignmentService_,
        providerExecutionCatalogService_,
        providerExecutionService_,
        installerOrchestrator_,
        installerOrchestrator_,
        installerOrchestrator_,
        exportService_,
        commandLogicUnitService_,
        surfaceService_);

    runtime_->boot();
    if (entitlementProvider_) {
        entitlementProvider_->onEntitlementsChanged([this]() {
            if (!runtime_ || !runtime_->isBooted()) {
                return;
            }
            activateDefaultModules();
        });
    }
    activateDefaultModules();

    if (configurationService_->current().beaconEnabled) {
        beaconService_->start();
    }

    const auto currentConfiguration = configurationService_->current();
    httpServer_ = std::make_unique<SimpleHttpServer>(
        currentConfiguration.bindAddress == "0.0.0.0" ? "0.0.0.0" : currentConfiguration.bindAddress,
        currentConfiguration.browserPort,
        [this](const HttpRequest& request) { return handleHttpRequest(request); });
    if (!httpServer_->start()) {
        httpServer_.reset();
        if (beaconService_) {
            beaconService_->stop();
        }
        if (runtime_) {
            runtime_->shutdown();
        }
        return false;
    }
    initialized_ = true;
    return true;
}

void MasterControlApplication::Impl::shutdown() {
    if (!initialized_) {
        return;
    }
    if (httpServer_) {
        httpServer_->stop();
    }
    if (beaconService_) {
        beaconService_->stop();
    }
    if (runtime_) {
        runtime_->shutdown();
    }
    initialized_ = false;
}

int MasterControlApplication::Impl::runInteractive() {
    while (!stopRequested_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    return 0;
}

std::string MasterControlApplication::Impl::browserUrl() const {
    const auto configuration = configurationService_->current();
    const auto host = configuration.bindAddress == "0.0.0.0" ? "127.0.0.1" : configuration.bindAddress;
    return "http://" + host + ":" + std::to_string(configuration.browserPort) + "/";
}

void MasterControlApplication::Impl::registerConfigurationDefaults() {
    const auto currentConfiguration = configurationService_->current();
    std::lock_guard<std::mutex> lock(state_->mutex);
    state_->configuration = currentConfiguration;
    if (state_->configuration.providers.empty()) {
        state_->configuration.providers = buildDefaultProviders();
        (void)writeJsonFile(paths_.configurationFile, state_->configuration);
    }
}

void MasterControlApplication::Impl::createForsettiRuntime() {
    auto services = std::make_shared<Forsetti::ServiceContainer>();
    Forsetti::DefaultForsettiPlatformServices::registerAll(*services);
    services->registerService<IConfigurationService>(configurationService_);
    services->registerService<IResourceAllocationService>(resourceAllocationService_);
    services->registerService<ITelemetryService>(telemetryService_);
    services->registerService<IRuntimeInventoryService>(inventoryService_);
    services->registerService<IPlatformServiceCatalogService>(platformServiceCatalogService_);
    services->registerService<IAppleRemoteHostService>(appleRemoteHostService_);
    services->registerService<IPackageTrustEvaluator>(trustEvaluator_);
    services->registerService<IInstallerOrchestrator>(installerOrchestrator_);
    services->registerService<IBootstrapRepoService>(installerOrchestrator_);
    services->registerService<IZipBundleService>(installerOrchestrator_);
    services->registerService<IProviderRegistry>(providerRegistry_);
    services->registerService<IProviderCatalogService>(providerCatalogService_);
    services->registerService<IProviderCredentialStore>(providerCredentialStore_);
    services->registerService<IMcpServerCatalogService>(mcpServerCatalogService_);
    services->registerService<ISubAgentCatalogService>(subAgentCatalogService_);
    services->registerService<ISubAgentGroupService>(subAgentGroupService_);
    services->registerService<IProviderAssignmentService>(providerAssignmentService_);
    services->registerService<IProviderExecutionCatalogService>(providerExecutionCatalogService_);
    services->registerService<IProviderExecutionService>(providerExecutionService_);
    services->registerService<IExportService>(exportService_);
    services->registerService<IPlatformGovernanceToolService>(platformGovernanceToolService_);
    services->registerService<ICommandLogicUnitService>(commandLogicUnitService_);
    services->registerService<IModuleControlSurfaceService>(controlSurfaceService_);
    services->registerService<IBeaconService>(beaconService_);

    auto eventBus = std::make_shared<Forsetti::InMemoryEventBus>();
    auto logger = std::make_shared<Forsetti::ConsoleLogger>();
    auto router = std::make_shared<Forsetti::NoopOverlayRouter>();
    auto guard = std::make_shared<Forsetti::DefaultModuleCommunicationGuard>();
    auto context = std::make_shared<Forsetti::ForsettiContext>(services, eventBus, logger, router, guard);
    surfaceManager_ = std::make_shared<Forsetti::UISurfaceManager>();
    surfaceService_ = std::make_shared<ForsettiSurfaceService>(surfaceManager_, controlSurfaceService_);
    services->registerService<IForsettiSurfaceService>(surfaceService_);
    auto compatibilityPolicy = std::make_shared<Forsetti::AllowAllCapabilityPolicy>();
    auto checker = std::make_shared<Forsetti::CompatibilityChecker>(Forsetti::SemVer{ 0, 1, 0 }, compatibilityPolicy);
    fileBackedEntitlementProvider_ = std::make_shared<FileBackedEntitlementProvider>(
        paths_.entitlementsFile,
        buildDefaultEntitlementStateDocument());
    entitlementProvider_ = fileBackedEntitlementProvider_;
    auto activationStore = std::make_shared<JsonActivationStore>(paths_.dataDirectory / "state" / "activation-state.json");

    auto registry = Forsetti::ForsettiStaticModuleRegistry::buildRegistry([](Forsetti::ModuleRegistry& registry) {
        registerMasterControlModules(registry);
    });

    auto moduleManager = std::make_unique<Forsetti::ModuleManager>(
        std::move(registry),
        checker,
        entitlementProvider_,
        activationStore,
        surfaceManager_,
        context);

    runtime_ = std::make_unique<Forsetti::ForsettiRuntime>(
        std::move(moduleManager),
        entitlementProvider_,
        eventBus,
        paths_.manifestsDirectory.string());
}

void MasterControlApplication::Impl::activateDefaultModules() {
    static const std::array<const char*, 19> moduleIds = {
        "com.mastercontrol.environment-discovery",
        "com.mastercontrol.host-telemetry",
        "com.mastercontrol.runtime-inventory",
        "com.mastercontrol.configuration",
        "com.mastercontrol.installer-import",
        "com.mastercontrol.provider-integration",
        "com.mastercontrol.provider-codex",
        "com.mastercontrol.provider-claude-code",
        "com.mastercontrol.provider-xai",
        "com.mastercontrol.export",
        "com.mastercontrol.command-logic-unit",
        "com.mastercontrol.gateway-windows",
        "com.mastercontrol.gateway-macos",
        "com.mastercontrol.gateway-ios",
        "com.mastercontrol.governance-windows",
        "com.mastercontrol.governance-macos",
        "com.mastercontrol.governance-ios",
        "com.mastercontrol.beacon-gateway",
        "com.mastercontrol.dashboard-ui"
    };

    for (const auto* moduleId : moduleIds) {
        if (!runtime_->moduleManager().isModuleActive(moduleId)) {
            try {
                runtime_->activateModule(moduleId);
            } catch (const std::exception&) {
            }
        }
    }
}

nlohmann::json MasterControlApplication::Impl::forsettiModuleCatalog() const {
    nlohmann::json modules = nlohmann::json::array();
    if (!runtime_) {
        return nlohmann::json{
            { "succeeded", false },
            { "message", "Forsetti runtime is not initialized." },
            { "modules", modules }
        };
    }

    for (const auto& [moduleId, manifest] : runtime_->moduleManager().manifestsByID()) {
        std::vector<std::string> supportedPlatforms;
        supportedPlatforms.reserve(manifest.supportedPlatforms.size());
        for (const auto platform : manifest.supportedPlatforms) {
            supportedPlatforms.push_back(Forsetti::to_string(platform));
        }

        std::vector<std::string> capabilitiesRequested;
        capabilitiesRequested.reserve(manifest.capabilitiesRequested.size());
        for (const auto capability : manifest.capabilitiesRequested) {
            capabilitiesRequested.push_back(Forsetti::to_string(capability));
        }

        const auto active = runtime_->moduleManager().isModuleActive(moduleId);
        const auto unlocked = entitlementProvider_ && entitlementProvider_->isUnlocked(moduleId);
        const auto protectedModule = isProtectedForsettiModule(moduleId);

        std::string recommendedAction = unlocked ? (active ? "update" : "enable") : "install";
        std::string statusSummary = active
            ? "Active in the current Forsetti runtime."
            : (unlocked ? "Unlocked but inactive. Enable or update to bring it online." : "Locked until installed through entitlements.");
        if (protectedModule) {
            statusSummary += " Core orchestration module protections are enabled.";
        }

        modules.push_back(nlohmann::json{
            { "moduleId", moduleId },
            { "displayName", manifest.displayName },
            { "moduleType", Forsetti::to_string(manifest.moduleType) },
            { "version", manifest.moduleVersion.toString() },
            { "entryPoint", manifest.entryPoint },
            { "supportedPlatforms", supportedPlatforms },
            { "capabilitiesRequested", capabilitiesRequested },
            { "active", active },
            { "unlocked", unlocked },
            { "protectedModule", protectedModule },
            { "recommendedAction", recommendedAction },
            { "statusSummary", statusSummary }
        });
    }

    return nlohmann::json{
        { "succeeded", true },
        { "message", "Forsetti module catalog loaded." },
        { "modules", modules }
    };
}

OperationResult MasterControlApplication::Impl::manageForsettiModule(const std::string& moduleId,
                                                                    const std::string& action) {
    if (!runtime_) {
        return OperationResult{ false, false, "Forsetti runtime is not initialized." };
    }
    if (!fileBackedEntitlementProvider_) {
        return OperationResult{ false, false, "Forsetti entitlement management is unavailable." };
    }
    if (moduleId.empty()) {
        return OperationResult{ false, false, "Select a Forsetti module before applying an action." };
    }

    const auto manifestIterator = runtime_->moduleManager().manifestsByID().find(moduleId);
    if (manifestIterator == runtime_->moduleManager().manifestsByID().end()) {
        return OperationResult{ false, false, "The selected Forsetti module was not found." };
    }

    const auto normalizedAction = trimCopy(action);
    if (normalizedAction.empty()) {
        return OperationResult{ false, false, "Select a Forsetti module action before continuing." };
    }
    if ((normalizedAction == "disable" || normalizedAction == "remove") && isProtectedForsettiModule(moduleId)) {
        return OperationResult{ false, false, "Core orchestration modules cannot be disabled from the guided module wizard." };
    }

    try {
        if (normalizedAction == "install" || normalizedAction == "enable") {
            fileBackedEntitlementProvider_->setModuleUnlocked(moduleId, true);
            if (!runtime_->moduleManager().isModuleActive(moduleId)) {
                runtime_->activateModule(moduleId);
            }
            return OperationResult{ true, false, "Forsetti module is now installed and active." };
        }

        if (normalizedAction == "update" || normalizedAction == "reload") {
            fileBackedEntitlementProvider_->setModuleUnlocked(moduleId, true);
            if (runtime_->moduleManager().isModuleActive(moduleId)) {
                runtime_->deactivateModule(moduleId);
            }
            runtime_->activateModule(moduleId);
            return OperationResult{ true, false, "Forsetti module was refreshed in the live runtime." };
        }

        if (normalizedAction == "disable" || normalizedAction == "remove") {
            if (runtime_->moduleManager().isModuleActive(moduleId)) {
                runtime_->deactivateModule(moduleId);
            }
            fileBackedEntitlementProvider_->setModuleUnlocked(moduleId, false);
            return OperationResult{ true, false, "Forsetti module was disabled and removed from the unlocked set." };
        }
    } catch (const std::exception& exception) {
        return OperationResult{ false, false, exception.what() };
    }

    return OperationResult{ false, false, "Unknown Forsetti module action." };
}

HttpResponse MasterControlApplication::Impl::handleHttpRequest(const HttpRequest& request) {
    // Dedicated /api/activity read route — served before the main handler
    // body so incremental polling stays cheap and never touches the heavier
    // snapshot path. Clients pass `?since={id}` to get only new events.
    if (request.method == "GET" && request.path.rfind("/api/activity", 0) == 0) {
        std::string sinceId;
        const auto queryStart = request.path.find('?');
        if (queryStart != std::string::npos) {
            const auto query = request.path.substr(queryStart + 1);
            const auto sincePos = query.find("since=");
            if (sincePos != std::string::npos) {
                sinceId = query.substr(sincePos + 6);
                const auto amp = sinceId.find('&');
                if (amp != std::string::npos) sinceId = sinceId.substr(0, amp);
            }
        }
        const auto snap = globalActivityRing().read(sinceId);
        nlohmann::json body;
        body["highWaterMarkId"] = snap.highWaterMarkId;
        body["events"] = nlohmann::json::array();
        for (const auto& e : snap.events) {
            body["events"].push_back(e);
        }
        return HttpResponse{ 200, "application/json", body.dump() };
    }

    // Capture every inbound admin API request into the live activity ring.
    // Shell's own poll targets (/api/dashboard, /api/config) are skipped so
    // the poll loop doesn't thrash the ring.
    const auto requestStart = std::chrono::steady_clock::now();
    const bool skipActivity =
        request.path == "/api/dashboard" ||
        request.path == "/api/config" ||
        request.path == "/api/health";

    const auto response = ([&]() -> HttpResponse {
    try {
        const auto gateways = platformServiceCatalogService_
            ? platformServiceCatalogService_->listGateways()
            : std::vector<PlatformGatewayDescriptor>{};
        const auto governanceServers = platformServiceCatalogService_
            ? platformServiceCatalogService_->listGovernanceServers()
            : std::vector<GovernanceServerDescriptor>{};
        const auto appleHosts = appleRemoteHostService_
            ? appleRemoteHostService_->listHosts()
            : std::vector<AppleRemoteHost>{};
        const auto findGateway = [&gateways](const PlatformTarget platform) -> std::optional<PlatformGatewayDescriptor> {
            const auto iterator = std::find_if(
                gateways.begin(),
                gateways.end(),
                [platform](const PlatformGatewayDescriptor& descriptor) { return descriptor.platform == platform; });
            if (iterator == gateways.end()) {
                return std::nullopt;
            }
            return *iterator;
        };
        const auto findGovernanceServer =
            [&governanceServers](const PlatformTarget platform) -> std::optional<GovernanceServerDescriptor> {
            const auto iterator = std::find_if(
                governanceServers.begin(),
                governanceServers.end(),
                [platform](const GovernanceServerDescriptor& descriptor) { return descriptor.platform == platform; });
            if (iterator == governanceServers.end()) {
                return std::nullopt;
            }
            return *iterator;
        };
        const auto listAppleHostsForPlatform =
            [&appleHosts](const PlatformTarget platform) -> std::vector<AppleRemoteHost> {
                std::vector<AppleRemoteHost> filtered;
                for (const auto& host : appleHosts) {
                    if (std::find(host.platforms.begin(), host.platforms.end(), platform) != host.platforms.end()) {
                        filtered.push_back(host);
                    }
                }
                return filtered;
            };
        const auto isAppleHostReadyForPlatform = [](const AppleRemoteHost& host, const PlatformTarget platform) {
            if (!host.toolchain.reachable || !host.toolchain.xcodeInstalled) {
                return false;
            }
            if (platform == PlatformTarget::MacOS) {
                return host.toolchain.macosSdkAvailable;
            }
            if (platform == PlatformTarget::IOS) {
                return host.toolchain.iosSdkAvailable &&
                    host.signing.signingReady &&
                    (host.toolchain.simulatorControlAvailable || host.toolchain.deviceControlAvailable);
            }
            return true;
        };
        const auto makeBaseUrl = [this]() {
            const auto configuration = configurationService_->current();
            const auto telemetry = telemetryService_->captureSnapshot();
            std::string host = configuration.bindAddress;
            if (host.empty() || host == "0.0.0.0") {
                host = telemetry.primaryIpAddress.empty() ? std::string("127.0.0.1") : telemetry.primaryIpAddress;
            }
            return std::string("http://") + host + ":" + std::to_string(configuration.browserPort);
        };
        const auto makePlatformConfigDocument = [&, this](const PlatformTarget platform) {
            const auto gateway = findGateway(platform);
            const auto governance = findGovernanceServer(platform);
            if (!gateway.has_value() || !governance.has_value()) {
                return nlohmann::json{};
            }
            const auto platformAppleHosts = listAppleHostsForPlatform(platform);
            const auto selectedAppleHost =
                appleRemoteHostService_ ? appleRemoteHostService_->selectHostForPlatform(platform) : std::nullopt;

            const auto baseUrl = makeBaseUrl();
            const auto gatewayUrl = baseUrl + gateway->gatewayPath;
            const auto configUrl = baseUrl + gateway->configPath;
            const auto governanceUrl = baseUrl + governance->routePath;

            return nlohmann::json{
                { "platform", platformKey(platform) },
                { "serviceId", gateway->serviceId },
                { "displayName", gateway->displayName },
                { "serviceType", gateway->serviceType },
                { "browserUrl", browserUrl() },
                { "gatewayUrl", gatewayUrl },
                { "governanceUrl", governanceUrl },
                { "configUrl", configUrl },
                { "lanServiceDiscovery", {
                    { "mode", "dns-sd" },
                    { "serviceType", gateway->serviceType },
                    { "instanceLabel", gateway->instanceLabel },
                    { "hostName", gateway->hostName },
                    { "port", gateway->port }
                } },
                { "clientSelectionRule", "Clients should discover and connect to the service that matches their platform OS." },
                { "governanceTools", governance->toolIds },
                { "appleHosts", platformAppleHosts },
                { "selectedAppleHost", selectedAppleHost.has_value() ? nlohmann::json(*selectedAppleHost) : nlohmann::json() },
                { "routeable", platform == PlatformTarget::Windows ||
                        (selectedAppleHost.has_value() && isAppleHostReadyForPlatform(*selectedAppleHost, platform)) },
                { "agentConfigurations", nlohmann::json::array({
                    {
                        { "provider", "codex" },
                        { "credentialFields", nlohmann::json::array({ "OPENAI_API_KEY" }) },
                        { "recommendedModel", "gpt-5.4" },
                        { "mcp", {
                            { "name", gateway->serviceId },
                            { "transport", "http" },
                            { "url", gatewayUrl }
                        } }
                    },
                    {
                        { "provider", "claude-code" },
                        { "credentialFields", nlohmann::json::array({ "ANTHROPIC_API_KEY", "ANTHROPIC_AUTH_TOKEN" }) },
                        { "mcp", {
                            { "mcpServers", {
                                { gateway->serviceId, {
                                    { "type", "http" },
                                    { "url", gatewayUrl }
                                } }
                            } }
                        } }
                    },
                    {
                        { "provider", "xai" },
                        { "credentialFields", nlohmann::json::array({ "XAI_API_KEY" }) },
                        { "recommendedModel", "grok-code-fast-1" },
                        { "mcp", {
                            { "gateway", gatewayUrl },
                            { "provider", "xai" },
                            { "recommendedModel", "grok-code-fast-1" }
                        } }
                    }
                }) }
            };
        };
        if (request.method == "GET" && request.path == "/api/health") {
            return jsonResponse(nlohmann::json{ { "status", "ok" }, { "time", timestampNowUtc() } });
        }
        if (request.method == "GET" && request.path == "/api/dashboard") {
            return jsonResponse(snapshot());
        }
        if (request.method == "GET" && request.path == "/api/config") {
            return jsonResponse(configurationService_->current());
        }
        if (request.method == "GET" && request.path == "/api/providers") {
            return jsonResponse(providerRegistry_->listProviders());
        }
        if (request.method == "GET" && request.path == "/api/exports") {
            return jsonResponse(exportService_->generateExports());
        }
        if (request.method == "GET" && request.path == "/api/clu") {
            return jsonResponse(commandLogicUnitService_->currentGovernance());
        }
        if (request.method == "GET" && request.path == "/api/clu/tools") {
            return jsonResponse(commandLogicUnitService_->currentGovernance().availableTools);
        }
        if (request.method == "GET" && request.path == "/api/clu/apple-operations") {
            return jsonResponse(commandLogicUnitService_->currentGovernance().appleOperations);
        }
        if (request.method == "GET" && request.path == "/api/forsetti/surface") {
            return jsonResponse(surfaceService_->currentSurface());
        }
        if (request.method == "GET" && request.path == "/api/forsetti/modules") {
            return jsonResponse(forsettiModuleCatalog());
        }
        if (request.method == "GET" && request.path == "/api/install/history") {
            return jsonResponse(installerOrchestrator_->history());
        }
        if (request.method == "GET" && request.path == "/api/beacon") {
            return jsonResponse(beaconService_->currentAdvertisement());
        }
        if (request.method == "GET" && request.path == "/api/environment-hints") {
            // Report which provider credential environment variables are set,
            // enabling guided workflows to pre-fill detected credentials.
            const auto capabilities = providerCatalogService_->listCapabilities();
            nlohmann::json hints = nlohmann::json::object();
            for (const auto& capability : capabilities) {
                for (const auto& field : capability.credentialFields) {
                    if (!field.environmentVariableHint.empty()) {
                        const auto wideHint = wideFromUtf8(field.environmentVariableHint);
                        const DWORD exists = GetEnvironmentVariableW(wideHint.c_str(), nullptr, 0);
                        hints[field.environmentVariableHint] = (exists > 0);
                    }
                }
            }
            return jsonResponse(hints);
        }
        // -------------------------------------------------------------------
        // WS1 — Readiness and setup lifecycle
        // -------------------------------------------------------------------
        if (request.method == "GET" && request.path == "/api/readiness") {
            const auto snap = adminApiService_->snapshot();
            const auto cfg = configurationService_->current();
            return jsonResponse(computeReadinessSnapshot(snap, cfg));
        }
        if (request.method == "POST" && request.path == "/api/setup/start") {
            auto cfg = configurationService_->current();
            if (cfg.firstRunStartedAtUtc.empty()) {
                cfg.firstRunStartedAtUtc = timestampNowUtc();
                const auto result = configurationService_->update(cfg, false);
                if (!result.succeeded) {
                    return jsonResponse(result, 400);
                }
            }
            return jsonResponse(OperationResult{ true, false, "Setup started." });
        }
        if (request.method == "POST" && request.path == "/api/setup/complete") {
            std::vector<std::string> skippedSteps;
            try {
                if (!request.body.empty()) {
                    const auto body = nlohmann::json::parse(request.body);
                    if (body.contains("skippedSteps") && body["skippedSteps"].is_array()) {
                        for (const auto& entry : body["skippedSteps"]) {
                            if (entry.is_string()) {
                                skippedSteps.push_back(entry.get<std::string>());
                            }
                        }
                    }
                }
            } catch (const std::exception&) {
                // Body is optional — treat parse errors as empty body.
            }
            auto cfg = configurationService_->current();
            cfg.firstRunCompleted = true;
            cfg.firstRunCompletedAtUtc = timestampNowUtc();
            cfg.firstRunSkippedSteps = skippedSteps;
            const auto result = configurationService_->update(cfg, false);
            if (!result.succeeded) {
                return jsonResponse(result, 400);
            }
            return jsonResponse(OperationResult{ true, false, "Setup marked complete." });
        }
        if (request.method == "POST" && request.path == "/api/setup/reset") {
            auto cfg = configurationService_->current();
            cfg.firstRunCompleted = false;
            cfg.firstRunStartedAtUtc.clear();
            cfg.firstRunCompletedAtUtc.clear();
            cfg.firstRunSkippedSteps.clear();
            const auto result = configurationService_->update(cfg, false);
            if (!result.succeeded) {
                return jsonResponse(result, 400);
            }
            return jsonResponse(OperationResult{ true, false, "Setup reset." });
        }
        // -------------------------------------------------------------------
        // WS4 — Provider install automation (Claude Code CLI only)
        // -------------------------------------------------------------------
        if (request.method == "GET" && request.path == "/api/setup/dependencies") {
            const auto catalog = buildSupportedDependencyCatalog();
            nlohmann::json document = nlohmann::json::object();
            nlohmann::json array = nlohmann::json::array();
            const auto workDir = std::filesystem::current_path();
            for (const auto& descriptor : catalog) {
                DependencyDetection detection;
                detection.id = descriptor.id;
                detection.detectedAtUtc = timestampNowUtc();
                // Branch A — probe the dependency itself.
                const auto detectCommandLine = std::wstring(L"cmd.exe /c ") + wideFromUtf8(descriptor.detectCommand);
                const auto detectProbe = runProcessCapture(detectCommandLine, workDir, {}, std::nullopt, false);
                if (detectProbe.launched && detectProbe.exitCode == 0) {
                    detection.state = "ready";
                    detection.preflight = "ready";
                    // Extract the first non-empty stdout line as detected version.
                    std::string firstLine;
                    for (char ch : detectProbe.stdoutText) {
                        if (ch == '\r' || ch == '\n') { if (!firstLine.empty()) break; else continue; }
                        firstLine += ch;
                    }
                    detection.detectedVersion = firstLine;
                } else if (descriptor.prerequisiteProbeCommand.empty()) {
                    // Dependency has no prerequisite (e.g. nodejs itself via
                    // winget). The install command can run directly.
                    detection.state = "not-installed";
                    detection.preflight = "installable";
                    detection.detail = descriptor.displayName + " not detected. Ready to install.";
                } else {
                    // Probe the declared prerequisite (typically `npm --version`
                    // for the CLI tools; Node.js is their prerequisite).
                    const auto prereqCommandLine = std::wstring(L"cmd.exe /c ")
                        + wideFromUtf8(descriptor.prerequisiteProbeCommand);
                    const auto prereqProbe = runProcessCapture(prereqCommandLine, workDir, {}, std::nullopt, false);
                    if (prereqProbe.launched && prereqProbe.exitCode == 0) {
                        detection.state = "not-installed";
                        detection.preflight = "installable";
                        detection.detail = descriptor.displayName
                            + " not detected. " + descriptor.prerequisiteName + " available — ready to install.";
                    } else {
                        detection.state = "manual-action-required";
                        detection.preflight = "prerequisite-missing";
                        detection.detail = descriptor.prerequisiteName
                            + " is required to install " + descriptor.displayName
                            + " but was not detected on PATH.";
                    }
                }
                array.push_back({
                    { "descriptor", descriptor },
                    { "detection", detection }
                });
            }
            document["dependencies"] = array;
            return jsonResponse(document);
        }
        if (request.method == "POST" && startsWith(request.path, "/api/setup/dependencies/")
            && endsWith(request.path, "/install")) {
            const auto prefix = std::string("/api/setup/dependencies/");
            const auto suffix = std::string("/install");
            const auto id = request.path.substr(prefix.size(),
                request.path.size() - prefix.size() - suffix.size());
            const auto catalog = buildSupportedDependencyCatalog();
            const auto descriptorIt = std::find_if(catalog.begin(), catalog.end(),
                [&id](const SupportedDependency& d) { return d.id == id; });
            if (descriptorIt == catalog.end()) {
                return jsonResponse(OperationResult{ false, false, "Unknown dependency id." }, 404);
            }
            const auto& descriptor = *descriptorIt;
            const auto workDir = std::filesystem::current_path();
            const auto installStart = std::chrono::steady_clock::now();
            DependencyInstallResult result;
            result.id = descriptor.id;

            // Re-run preflight first so short-circuits happen before any install.
            const auto detectCommandLine = std::wstring(L"cmd.exe /c ") + wideFromUtf8(descriptor.detectCommand);
            const auto preDetect = runProcessCapture(detectCommandLine, workDir, {}, std::nullopt, false);
            if (preDetect.launched && preDetect.exitCode == 0) {
                // Branch A — already installed.
                std::string firstLine;
                for (char ch : preDetect.stdoutText) {
                    if (ch == '\r' || ch == '\n') { if (!firstLine.empty()) break; else continue; }
                    firstLine += ch;
                }
                result.succeeded = true;
                result.finalState = "ready";
                result.exitCode = 0;
                result.summary = "Already installed (" + firstLine + ").";
                result.postInstallDetection = DependencyDetection{
                    descriptor.id, "ready", "ready", firstLine, "", timestampNowUtc()
                };
            } else {
                // Refresh the service process's PATH from HKLM/HKCU
                // Environment keys before probing the prerequisite. winget
                // installs (especially --scope machine) update HKLM's Path
                // but the service's own PATH was snapshotted at startup —
                // without this refresh, an "Install Node.js" click followed
                // immediately by "Install Claude Code CLI" would fail the
                // npm prerequisite check even though npm is on disk.
                auto refreshPathFromRegistry = []() -> std::wstring {
                    auto readKey = [](HKEY hive, const wchar_t* subkey) -> std::wstring {
                        HKEY handle = nullptr;
                        if (RegOpenKeyExW(hive, subkey, 0, KEY_READ, &handle) != ERROR_SUCCESS) return {};
                        DWORD type = 0, bytes = 0;
                        RegQueryValueExW(handle, L"Path", nullptr, &type, nullptr, &bytes);
                        std::wstring value;
                        if (bytes > 0 && (type == REG_SZ || type == REG_EXPAND_SZ)) {
                            value.resize(bytes / sizeof(wchar_t));
                            RegQueryValueExW(handle, L"Path", nullptr, &type,
                                reinterpret_cast<LPBYTE>(value.data()), &bytes);
                            while (!value.empty() && value.back() == L'\0') value.pop_back();
                            if (type == REG_EXPAND_SZ) {
                                wchar_t expanded[32768];
                                const auto n = ExpandEnvironmentStringsW(value.c_str(), expanded, 32768);
                                if (n > 0 && n <= 32768) value.assign(expanded, n - 1);
                            }
                        }
                        RegCloseKey(handle);
                        return value;
                    };
                    const auto machine = readKey(HKEY_LOCAL_MACHINE,
                        L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment");
                    const auto user = readKey(HKEY_CURRENT_USER, L"Environment");
                    std::wstring combined = machine;
                    if (!user.empty()) {
                        if (!combined.empty() && combined.back() != L';') combined += L';';
                        combined += user;
                    }
                    if (!combined.empty()) SetEnvironmentVariableW(L"Path", combined.c_str());
                    return combined;
                };
                const auto refreshedPath = refreshPathFromRegistry();

                // Probe the declared prerequisite (if any). Dependencies with
                // no prerequisite (e.g. nodejs installed via winget) skip this
                // check entirely and go straight to the install command.
                bool prerequisiteOk = true;
                if (!descriptor.prerequisiteProbeCommand.empty()) {
                    const auto prereqCommandLine = std::wstring(L"cmd.exe /c ")
                        + wideFromUtf8(descriptor.prerequisiteProbeCommand);
                    std::vector<std::pair<std::wstring, std::wstring>> preEnv;
                    if (!refreshedPath.empty()) preEnv.emplace_back(L"Path", refreshedPath);
                    const auto prereqProbe = runProcessCapture(prereqCommandLine, workDir, preEnv, std::nullopt, false);
                    prerequisiteOk = prereqProbe.launched && prereqProbe.exitCode == 0;
                }
                if (!prerequisiteOk) {
                    // Prerequisite missing. No install attempted.
                    result.succeeded = false;
                    result.finalState = "manual-action-required";
                    result.exitCode = -1;
                    result.summary = "Cannot install: " + descriptor.prerequisiteName
                                   + " not detected. Install it first from "
                                   + descriptor.docsUrl + " and retry.";
                    result.postInstallDetection = DependencyDetection{
                        descriptor.id, "manual-action-required", "prerequisite-missing", "",
                        descriptor.prerequisiteName + " not on PATH.", timestampNowUtc()
                    };
                } else {
                    // Branch B — install.
                    const auto installCommandLine = std::wstring(L"cmd.exe /c ") + wideFromUtf8(descriptor.installMethod);
                    const auto installProbe = runProcessCapture(installCommandLine, workDir, {}, std::nullopt, true);
                    // Truncate stdout/stderr tails to last ~2KB for the response payload.
                    auto tailOf = [](const std::string& s) -> std::string {
                        constexpr size_t kTail = 2048;
                        return s.size() <= kTail ? s : s.substr(s.size() - kTail);
                    };
                    result.stdoutTail = tailOf(installProbe.stdoutText);
                    result.stderrTail = tailOf(installProbe.stderrText);
                    result.exitCode = installProbe.exitCode;

                    // The install command (winget install --scope machine / npm install -g)
                    // may have dropped binaries into a new directory. The
                    // pre-probe already refreshed PATH from the registry and
                    // populated `refreshedPath`; re-refresh it now so any
                    // path update that happened DURING the install (e.g.
                    // winget created C:\Program Files\nodejs and amended
                    // machine PATH mid-run) is picked up before postDetect.
                    const auto postInstallRefreshedPath = refreshPathFromRegistry();
                    // Post-install PATH must include every plausible npm-global
                    // bin directory so `npm install -g <pkg>` results are
                    // visible regardless of whether the service runs as the
                    // current user, LocalSystem, or a different service
                    // account. LocalSystem's npm global lives under
                    // %SystemRoot%\System32\config\systemprofile\AppData\Roaming\npm
                    // which isn't on any default PATH.
                    std::wstring extendedPath = postInstallRefreshedPath.empty()
                        ? refreshedPath
                        : postInstallRefreshedPath;
                    auto appendIfPresent = [&extendedPath](const std::wstring& dir) {
                        if (!dir.empty()) {
                            if (!extendedPath.empty() && extendedPath.back() != L';') extendedPath += L';';
                            extendedPath += dir;
                        }
                    };
                    appendIfPresent(L"C:\\Windows\\System32\\config\\systemprofile\\AppData\\Roaming\\npm");
                    appendIfPresent(L"C:\\Program Files\\nodejs");
                    wchar_t userProfileBuf[MAX_PATH] = {};
                    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, userProfileBuf))) {
                        appendIfPresent(std::wstring(userProfileBuf) + L"\\npm");
                    }
                    std::vector<std::pair<std::wstring, std::wstring>> refreshedEnv;
                    if (!extendedPath.empty()) {
                        refreshedEnv.emplace_back(L"Path", extendedPath);
                    }
                    const auto postDetect = runProcessCapture(detectCommandLine, workDir, refreshedEnv, std::nullopt, false);
                    std::string postVersion;
                    for (char ch : postDetect.stdoutText) {
                        if (ch == '\r' || ch == '\n') { if (!postVersion.empty()) break; else continue; }
                        postVersion += ch;
                    }
                    const bool nowReady = postDetect.launched && postDetect.exitCode == 0;
                    const auto stderrLower = [&]() {
                        std::string lower = installProbe.stderrText;
                        std::transform(lower.begin(), lower.end(), lower.begin(),
                                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
                        return lower;
                    }();
                    const bool elevationFailure = installProbe.exitCode != 0
                        && (stderrLower.find("eacces") != std::string::npos
                            || stderrLower.find("eperm") != std::string::npos);
                    if (nowReady) {
                        result.succeeded = true;
                        result.finalState = "ready";
                        result.summary = descriptor.displayName + " installed (" + postVersion + ").";
                        result.postInstallDetection = DependencyDetection{
                            descriptor.id, "ready", "ready", postVersion, "", timestampNowUtc()
                        };
                    } else if (elevationFailure) {
                        result.succeeded = false;
                        result.finalState = "manual-action-required";
                        result.summary = "npm install failed with a permission error. Run '"
                            + descriptor.installMethod + "' from an elevated PowerShell and return here.";
                        result.postInstallDetection = DependencyDetection{
                            descriptor.id, "manual-action-required", "installable", "",
                            "Elevation required.", timestampNowUtc()
                        };
                    } else {
                        // Winget returns APPINSTALLER_CLI_ERROR_UPDATE_NOT_APPLICABLE
                        // (0x8A15002B = -1978335189 signed) when the package
                        // is already installed. Post-detect couldn't confirm it
                        // (likely because PATH is stale on this process), but
                        // the tool IS installed — check a well-known fallback
                        // path before giving up. This avoids the confusing
                        // "Install returned exit code -1978335189" message when
                        // the install actually succeeded.
                        const bool wingetAlreadyInstalled =
                            installProbe.exitCode == -1978335189 ||
                            (installProbe.stdoutText.find("already installed") != std::string::npos);
                        std::string fallbackVersion;
                        if (wingetAlreadyInstalled && descriptor.id == "nodejs") {
                            const auto fallbackDetect = runProcessCapture(
                                L"cmd.exe /c \"\"C:\\Program Files\\nodejs\\node.exe\" --version\"",
                                workDir, refreshedEnv, std::nullopt, false);
                            if (fallbackDetect.launched && fallbackDetect.exitCode == 0) {
                                for (char ch : fallbackDetect.stdoutText) {
                                    if (ch == '\r' || ch == '\n') { if (!fallbackVersion.empty()) break; else continue; }
                                    fallbackVersion += ch;
                                }
                            }
                        }
                        if (!fallbackVersion.empty()) {
                            result.succeeded = true;
                            result.finalState = "ready";
                            result.summary = descriptor.displayName + " already installed (" + fallbackVersion + ").";
                            result.postInstallDetection = DependencyDetection{
                                descriptor.id, "ready", "ready", fallbackVersion, "", timestampNowUtc()
                            };
                        } else {
                            result.succeeded = false;
                            result.finalState = "failed";
                            result.summary = "Install returned exit code " + std::to_string(installProbe.exitCode) + ".";
                            result.postInstallDetection = DependencyDetection{
                                descriptor.id,
                                nowReady ? "ready" : "failed",
                                "installable",
                                postVersion,
                                "Post-install detection did not find " + descriptor.displayName + ".",
                                timestampNowUtc()
                            };
                        }
                    }
                }
            }
            const auto installEnd = std::chrono::steady_clock::now();
            result.totalLatencyMs = static_cast<int>(
                std::chrono::duration_cast<std::chrono::milliseconds>(installEnd - installStart).count());
            // Return HTTP 400 on failure so clients (curl, fetch) see a real
            // error status instead of having to inspect the JSON body. The
            // previous `: 200 : 200` short-circuit masked every install
            // failure as a 200, which broke client-side .ok checks and
            // silently promoted a failed install to a green lane in the UI.
            return jsonResponse(result, result.succeeded ? 200 : 400);
        }
        // -------------------------------------------------------------------
        // WS6 — Starter workflow templates
        // -------------------------------------------------------------------
        if (request.method == "GET" && request.path == "/api/setup/workflow-templates") {
            const auto templates = buildStarterWorkflowTemplates();
            nlohmann::json document = { { "templates", templates } };
            return jsonResponse(document);
        }
        if (request.method == "POST"
            && startsWith(request.path, "/api/setup/workflow-templates/")
            && endsWith(request.path, "/instantiate")) {
            const auto prefix = std::string("/api/setup/workflow-templates/");
            const auto suffix = std::string("/instantiate");
            const auto id = request.path.substr(prefix.size(),
                request.path.size() - prefix.size() - suffix.size());
            const auto templates = buildStarterWorkflowTemplates();
            const auto templateIt = std::find_if(templates.begin(), templates.end(),
                [&id](const StarterWorkflowTemplate& t) { return t.id == id; });
            if (templateIt == templates.end()) {
                return jsonResponse(OperationResult{ false, false, "Unknown starter workflow template id." }, 404);
            }
            // Validate prerequisites against current readiness.
            const auto snap = adminApiService_->snapshot();
            const auto cfg = configurationService_->current();
            const auto readiness = computeReadinessSnapshot(snap, cfg);
            if (readiness.providersReadyCount < templateIt->requiresProviders) {
                StarterWorkflowInstantiateResult fail;
                fail.succeeded = false;
                fail.message = "This starter workflow requires at least "
                    + std::to_string(templateIt->requiresProviders)
                    + " connected provider(s). Currently ready: "
                    + std::to_string(readiness.providersReadyCount) + ".";
                return jsonResponse(fail);
            }
            if (readiness.mcpReadyCount < templateIt->requiresMcp) {
                StarterWorkflowInstantiateResult fail;
                fail.succeeded = false;
                fail.message = "This starter workflow requires at least "
                    + std::to_string(templateIt->requiresMcp)
                    + " MCP server(s) online. Currently ready: "
                    + std::to_string(readiness.mcpReadyCount) + ".";
                return jsonResponse(fail);
            }
            if (readiness.specialistsReadyCount < templateIt->requiresSpecialists) {
                StarterWorkflowInstantiateResult fail;
                fail.succeeded = false;
                fail.message = "This starter workflow requires at least "
                    + std::to_string(templateIt->requiresSpecialists)
                    + " specialist(s) assigned. Currently ready: "
                    + std::to_string(readiness.specialistsReadyCount) + ".";
                return jsonResponse(fail);
            }
            // Build the actual assignment plan:
            //   - Always wire provider #1 to the "planner" role.
            //   - If the template requires N providers (N > 1), wire provider
            //     #2 to "architect" and, if N >= 3, provider #3 to "auditor"
            //     (the three default Role targets — see listTargets()).
            //   - If the template requires specialists, find SubAgent targets
            //     and fan them across the ready providers round-robin, up to
            //     requiresSpecialists.
            // Previously this code always created one assignment (provider #1
            // → planner), so a template advertising "2 providers + 2
            // specialists" (like specialist-team-demo) silently ended up with
            // exactly one assignment — the template's prerequisites were
            // already validated above, but the instantiation wasn't honoring
            // them.
            std::vector<std::string> readyProviderIds;
            for (const auto& provider : snap.providers) {
                if (provider.credentialsConfigured && provider.enabled && !provider.isTemplate) {
                    readyProviderIds.push_back(provider.id);
                }
            }
            // Enumerate all assignment targets so we can pick specialist
            // (SubAgent-kind) ones for the team template.
            const auto allTargets = providerAssignmentService_->listTargets();
            std::vector<std::string> specialistTargetIds;
            for (const auto& target : allTargets) {
                if (target.kind == ProviderAssignmentTargetKind::SubAgent) {
                    specialistTargetIds.push_back(target.targetId);
                }
            }

            struct PlannedAssignment {
                std::string targetId;
                ProviderAssignmentTargetKind kind;
                std::string providerId;
            };
            std::vector<PlannedAssignment> plan;

            // Default Role targets in listTargets() order: planner, architect, auditor.
            // NOTE: parens around std::max/std::min are deliberate — Windows
            // headers can define max/min as macros, so we follow the
            // codebase convention (see MasterControlRuntime.cpp:4145 etc.).
            static constexpr const char* kDefaultRoleTargets[] = { "planner", "architect", "auditor" };
            const int providerCount = (std::max)(1, templateIt->requiresProviders);
            for (int i = 0; i < providerCount && i < 3; ++i) {
                const size_t providerIdx = (std::min)(
                    static_cast<size_t>(i),
                    readyProviderIds.size() - 1);
                const auto& providerId = readyProviderIds[providerIdx];
                plan.push_back({
                    std::string(kDefaultRoleTargets[i]),
                    ProviderAssignmentTargetKind::Role,
                    providerId
                });
            }

            // Specialist assignments: round-robin across ready providers.
            for (int i = 0;
                 i < templateIt->requiresSpecialists
                 && static_cast<size_t>(i) < specialistTargetIds.size();
                 ++i) {
                const auto& providerId = readyProviderIds[
                    static_cast<size_t>(i) % readyProviderIds.size()];
                plan.push_back({
                    specialistTargetIds[i],
                    ProviderAssignmentTargetKind::SubAgent,
                    providerId
                });
            }

            std::vector<std::string> appliedTargetIds;
            std::vector<std::string> failedTargets;
            for (const auto& planned : plan) {
                ProviderAssignment assignment;
                assignment.providerId = planned.providerId;
                assignment.targetId = planned.targetId;
                assignment.kind = planned.kind;
                assignment.updatedAtUtc = timestampNowUtc();
                const auto assignResult = providerAssignmentService_->upsertAssignment(assignment);
                if (assignResult.succeeded) {
                    appliedTargetIds.push_back(planned.targetId);
                } else {
                    failedTargets.push_back(planned.targetId + ": " + assignResult.message);
                }
            }

            StarterWorkflowInstantiateResult response;
            response.succeeded = !appliedTargetIds.empty() && failedTargets.empty();
            response.workflowId = templateIt->id + ":"
                + std::to_string(appliedTargetIds.size()) + "-targets";
            if (response.succeeded) {
                response.message = "Instantiated starter workflow '" + templateIt->displayName
                    + "' (" + std::to_string(appliedTargetIds.size()) + " assignment(s) applied).";
            } else if (!appliedTargetIds.empty()) {
                response.message = "Partial instantiation: "
                    + std::to_string(appliedTargetIds.size()) + " applied, "
                    + std::to_string(failedTargets.size()) + " failed ("
                    + failedTargets.front() + ").";
            } else {
                response.message = failedTargets.empty()
                    ? "No assignments could be applied for this template."
                    : failedTargets.front();
            }
            return jsonResponse(response);
        }
        if (request.method == "GET" && request.path == "/api/platform-services") {
            return jsonResponse(nlohmann::json{
                { "gateways", gateways },
                { "governanceServers", governanceServers },
                { "appleHosts", appleHosts }
            });
        }
        if (request.method == "GET" && request.path == "/api/platform-services/gateways") {
            return jsonResponse(gateways);
        }
        if (request.method == "GET" && request.path == "/api/platform-services/governance") {
            return jsonResponse(governanceServers);
        }
        if (request.method == "GET" && request.path == "/api/platform-services/apple-hosts") {
            return jsonResponse(appleHosts);
        }
        if (request.method == "GET" && startsWith(request.path, "/api/platform-services/config/")) {
            const auto platform = platformFromKey(request.path.substr(std::string("/api/platform-services/config/").size()));
            const auto document = makePlatformConfigDocument(platform);
            if (platform == PlatformTarget::Unknown || document.empty()) {
                return jsonResponse(nlohmann::json{ { "message", "Platform service config not found." } }, 404);
            }
            return jsonResponse(document);
        }
        if (request.method == "GET" && startsWith(request.path, "/mcp/gateway/")) {
            const auto platform = platformFromKey(request.path.substr(std::string("/mcp/gateway/").size()));
            const auto gateway = findGateway(platform);
            const auto governance = findGovernanceServer(platform);
            if (!gateway.has_value() || !governance.has_value()) {
                return jsonResponse(nlohmann::json{ { "message", "Gateway service not found." } }, 404);
            }
            return jsonResponse(nlohmann::json{
                { "service", "platform-gateway" },
                { "platform", platformKey(platform) },
                { "gateway", *gateway },
                { "governanceServer", *governance },
                { "configuration", makePlatformConfigDocument(platform) }
            });
        }
        if (request.method == "GET" && startsWith(request.path, "/mcp/governance/")) {
            const auto platform = platformFromKey(request.path.substr(std::string("/mcp/governance/").size()));
            const auto governance = findGovernanceServer(platform);
            const auto gateway = findGateway(platform);
            const auto selectedAppleHost =
                appleRemoteHostService_ ? appleRemoteHostService_->selectHostForPlatform(platform) : std::nullopt;
            if (!gateway.has_value() || !governance.has_value()) {
                return jsonResponse(nlohmann::json{ { "message", "Governance service not found." } }, 404);
            }
            const auto governanceSnapshot = commandLogicUnitService_->currentGovernance();
            nlohmann::json platformTools = nlohmann::json::array();
            for (const auto& tool : governanceSnapshot.availableTools) {
                if (tool.platform == platform) {
                    platformTools.push_back(tool);
                }
            }
            nlohmann::json platformOperations = nlohmann::json::array();
            for (const auto& operation : governanceSnapshot.appleOperations) {
                if (operation.platform == platform) {
                    platformOperations.push_back(operation);
                }
            }
            return jsonResponse(nlohmann::json{
                { "service", "governance-mcp-server" },
                { "platform", platformKey(platform) },
                { "gateway", *gateway },
                { "governanceServer", *governance },
                { "toolIds", governance->toolIds },
                { "tools", platformTools },
                { "recentOperations", platformOperations },
                { "requiresRemoteToolchain", governance->requiresRemoteToolchain },
                { "selectedAppleHost", selectedAppleHost.has_value() ? nlohmann::json(*selectedAppleHost) : nlohmann::json() },
                { "routeable", platform == PlatformTarget::Windows ||
                        (selectedAppleHost.has_value() && isAppleHostReadyForPlatform(*selectedAppleHost, platform)) }
            });
        }
        if (request.method == "POST" && request.path == "/api/settings/advanced-mode") {
            try {
                const auto body = nlohmann::json::parse(request.body);
                const bool enabled = body.value("enabled", false);
                auto configuration = configurationService_->current();
                configuration.advancedMode = enabled;
                const auto result = configurationService_->update(configuration, false);
                if (!result.succeeded) {
                    return jsonResponse(result, 400);
                }
                return jsonResponse(OperationResult{ true, false, enabled ? "Advanced mode enabled." : "Advanced mode disabled." });
            } catch (const std::exception& error) {
                return jsonResponse(OperationResult{ false, false, error.what() }, 400);
            }
        }
        if (request.method == "POST" && request.path == "/api/config") {
            const bool confirmUnsafeChanges = request.headers.contains("X-Confirm-Unsafe") &&
                request.headers.at("X-Confirm-Unsafe") == "1";
            const auto result = adminApiService_->applyConfigurationJson(request.body, confirmUnsafeChanges);
            return jsonResponse(result, result.succeeded ? 200 : 400);
        }
        if (request.method == "POST" && request.path == "/api/providers") {
            const auto result = adminApiService_->upsertProviderJson(request.body);
            return jsonResponse(result, result.succeeded ? 200 : 400);
        }
        if (request.method == "POST" && request.path == "/api/providers/credentials") {
            const auto result = adminApiService_->upsertProviderCredentialsJson(request.body);
            return jsonResponse(result, result.succeeded ? 200 : 400);
        }
        if (request.method == "POST" && request.path == "/api/providers/auto-connect") {
            const auto result = adminApiService_->autoConnectProviderJson(request.body);
            return jsonResponse(result, result.succeeded ? 200 : 400);
        }
        // Account-only sign-in wizard: spawn `claude login` or `codex login`
        // in a new console, let the user complete OAuth in their browser,
        // then poll for completion. On success a ProviderConnection is
        // registered that routes execution through the CLI — no API key
        // ever needs to be stored by the server.
        if (request.method == "POST" && request.path == "/api/providers/signin/start") {
            if (!providerCliSignInService_) {
                return jsonResponse(nlohmann::json{
                    { "succeeded", false },
                    { "message", "Sign-in service is not ready." }
                }, 500);
            }
            nlohmann::json body = nlohmann::json::object();
            try {
                if (!request.body.empty()) {
                    body = nlohmann::json::parse(request.body);
                }
            } catch (...) {
                return jsonResponse(nlohmann::json{
                    { "succeeded", false },
                    { "message", "Invalid JSON body." }
                }, 400);
            }
            const std::string bridge = body.value("bridge", std::string{});
            const std::string providerId = body.value("providerId", std::string{});
            const auto result = providerCliSignInService_->startSession(bridge, providerId);
            const int status = result.value("succeeded", false) ? 200 : 400;
            return jsonResponse(result, status);
        }
        if (request.method == "GET" && startsWith(request.path, "/api/providers/signin/status")) {
            if (!providerCliSignInService_) {
                return jsonResponse(nlohmann::json{
                    { "succeeded", false },
                    { "message", "Sign-in service is not ready." }
                }, 500);
            }
            // sessionId is passed as a query string parameter: ?sessionId=<id>
            std::string sessionId;
            const auto qmark = request.path.find('?');
            if (qmark != std::string::npos) {
                const auto query = request.path.substr(qmark + 1);
                size_t cursor = 0;
                while (cursor < query.size()) {
                    const auto amp = query.find('&', cursor);
                    const auto piece = query.substr(cursor, amp == std::string::npos ? std::string::npos : amp - cursor);
                    const auto eq = piece.find('=');
                    if (eq != std::string::npos) {
                        const auto key = piece.substr(0, eq);
                        const auto value = piece.substr(eq + 1);
                        if (key == "sessionId") {
                            sessionId = value;
                            break;
                        }
                    }
                    if (amp == std::string::npos) break;
                    cursor = amp + 1;
                }
            }
            if (sessionId.empty()) {
                return jsonResponse(nlohmann::json{
                    { "succeeded", false },
                    { "message", "Missing sessionId parameter." }
                }, 400);
            }
            const auto result = providerCliSignInService_->querySession(sessionId);
            return jsonResponse(result, 200);
        }
        if (request.method == "GET" && request.path == "/api/providers/signin/installed") {
            if (!providerCliSignInService_) {
                return jsonResponse(nlohmann::json::array(), 200);
            }
            return jsonResponse(providerCliSignInService_->detectInstalled(), 200);
        }
        if (request.method == "POST" && request.path == "/api/platform-services/apple-hosts") {
            const auto result = adminApiService_->upsertAppleRemoteHostJson(request.body);
            return jsonResponse(result, result.succeeded ? 200 : 400);
        }
        if (request.method == "POST" && request.path == "/api/platform-services/apple-hosts/remove") {
            const auto result = adminApiService_->removeAppleRemoteHostJson(request.body);
            return jsonResponse(result, result.succeeded ? 200 : 400);
        }
        if (request.method == "POST" && request.path == "/api/runtime/mcp-servers") {
            const auto result = adminApiService_->upsertMcpServerJson(request.body);
            return jsonResponse(result, result.succeeded ? 200 : 400);
        }
        if (request.method == "POST" && request.path == "/api/runtime/mcp-servers/remove") {
            const auto result = adminApiService_->removeMcpServerJson(request.body);
            return jsonResponse(result, result.succeeded ? 200 : 400);
        }
        if (request.method == "POST" && request.path == "/api/runtime/subagents") {
            const auto result = adminApiService_->upsertSubAgentJson(request.body);
            return jsonResponse(result, result.succeeded ? 200 : 400);
        }
        if (request.method == "POST" && request.path == "/api/runtime/subagents/remove") {
            const auto result = adminApiService_->removeSubAgentJson(request.body);
            return jsonResponse(result, result.succeeded ? 200 : 400);
        }
        if (request.method == "POST" && request.path == "/api/providers/groups") {
            const auto result = adminApiService_->upsertSubAgentGroupJson(request.body);
            return jsonResponse(result, result.succeeded ? 200 : 400);
        }
        if (request.method == "POST" && request.path == "/api/providers/groups/remove") {
            const auto result = adminApiService_->removeSubAgentGroupJson(request.body);
            return jsonResponse(result, result.succeeded ? 200 : 400);
        }
        if (request.method == "POST" && request.path == "/api/providers/assignments") {
            const auto result = adminApiService_->upsertProviderAssignmentJson(request.body);
            return jsonResponse(result, result.succeeded ? 200 : 400);
        }
        if (request.method == "POST" && request.path == "/api/providers/execute") {
            const auto result = adminApiService_->executeProviderTaskJson(request.body);
            return jsonResponse(result, result.status == ProviderExecutionStatus::Succeeded ? 200 : 400);
        }
        if (request.method == "POST" && request.path == "/api/forsetti/modules/state") {
            const auto payload = request.body.empty() ? nlohmann::json::object() : nlohmann::json::parse(request.body);
            const auto result = manageForsettiModule(
                payload.value("moduleId", std::string{}),
                payload.value("action", std::string{}));
            return jsonResponse(result, result.succeeded ? 200 : 400);
        }
        if (request.method == "POST" && request.path == "/api/clu/execute") {
            const auto result = adminApiService_->executeGovernanceToolJson(request.body);
            return jsonResponse(result, result.succeeded ? 200 : 400);
        }
        if (request.method == "POST" && request.path == "/api/clu/apple-operations/cancel") {
            const auto result = adminApiService_->cancelAppleOperationJson(request.body);
            return jsonResponse(result, result.succeeded ? 200 : 400);
        }
        if (request.method == "POST" && request.path == "/api/install/package") {
            const auto result = adminApiService_->installPackageJson(request.body);
            return jsonResponse(result, result.succeeded ? 200 : 400);
        }
        if (request.method == "POST" && request.path == "/api/install/repo") {
            const auto result = adminApiService_->installRepoJson(request.body);
            return jsonResponse(result, result.succeeded ? 200 : 400);
        }
        if (request.method == "POST" && request.path == "/api/install/zip") {
            const auto result = adminApiService_->installZipJson(request.body);
            return jsonResponse(result, result.succeeded ? 200 : 400);
        }
        if (request.method == "POST" && startsWith(request.path, "/mcp/governance/")) {
            const auto routePlatform = platformFromKey(request.path.substr(std::string("/mcp/governance/").size()));
            auto payload = request.body.empty() ? nlohmann::json::object() : nlohmann::json::parse(request.body);
            if (!payload.contains("platform") || platformFromKey(payload.value("platform", "")) == PlatformTarget::Unknown) {
                payload["platform"] = platformKey(routePlatform);
            }
            const auto result = commandLogicUnitService_->executeGovernanceTool(payload.get<GovernanceToolRequest>());
            return jsonResponse(result, result.succeeded ? 200 : 400);
        }

        return staticFileResponse(request.path);
    } catch (const std::exception& exception) {
        return jsonResponse(nlohmann::json{ { "succeeded", false }, { "message", exception.what() } }, 500);
    }
    })();  // end of request-handler lambda

    // Emit activity event for every non-polling route.
    if (!skipActivity) {
        const auto latency = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - requestStart).count());
        ActivityEvent event;
        event.kind = "admin_api_request";
        event.actor = "admin-api";
        event.method = request.method;
        event.target = request.path;
        event.statusCode = response.statusCode;
        event.latencyMs = latency;
        event.message = request.method + " " + request.path
            + " -> " + std::to_string(response.statusCode)
            + " (" + std::to_string(latency) + "ms)";
        globalActivityRing().append(event);
    }

    return response;
}

HttpResponse MasterControlApplication::Impl::staticFileResponse(std::string path) const {
    if (path.empty() || path == "/") {
        path = "/index.html";
    }
    if (!path.empty() && path.front() == '/') {
        path.erase(path.begin());
    }

    const auto filePath = paths_.webRootDirectory / path;
    if (!std::filesystem::exists(filePath)) {
        return HttpResponse{ 404, "application/json", R"({"message":"Not found"})" };
    }

    return HttpResponse{ 200, contentTypeForPath(filePath), readTextFile(filePath) };
}

MasterControlApplication::MasterControlApplication()
    : impl_(std::make_unique<Impl>()) {}

MasterControlApplication::~MasterControlApplication() {
    shutdown();
}

bool MasterControlApplication::initialize() {
    return impl_->initialize();
}

void MasterControlApplication::shutdown() {
    impl_->shutdown();
}

void MasterControlApplication::requestStop() {
    impl_->requestStop();
}

int MasterControlApplication::runInteractive() {
    return impl_->runInteractive();
}

std::string MasterControlApplication::browserUrl() const {
    return impl_->browserUrl();
}

DashboardSnapshot MasterControlApplication::snapshot() {
    return impl_->snapshot();
}

GovernanceToolResult MasterControlApplication::executeGovernanceToolJson(const std::string& requestBody) {
    return impl_->executeGovernanceToolJson(requestBody);
}

OperationResult MasterControlApplication::cancelAppleOperationJson(const std::string& requestBody) {
    return impl_->cancelAppleOperationJson(requestBody);
}

OperationResult MasterControlApplication::applyConfigurationJson(const std::string& requestBody, bool confirmUnsafeChanges) {
    return impl_->applyConfigurationJson(requestBody, confirmUnsafeChanges);
}

OperationResult MasterControlApplication::upsertProviderJson(const std::string& requestBody) {
    return impl_->upsertProviderJson(requestBody);
}

OperationResult MasterControlApplication::upsertProviderCredentialsJson(const std::string& requestBody) {
    return impl_->upsertProviderCredentialsJson(requestBody);
}

AutoConnectResult MasterControlApplication::autoConnectProviderJson(const std::string& requestBody) {
    return impl_->autoConnectProviderJson(requestBody);
}

OperationResult MasterControlApplication::upsertAppleRemoteHostJson(const std::string& requestBody) {
    return impl_->upsertAppleRemoteHostJson(requestBody);
}

OperationResult MasterControlApplication::removeAppleRemoteHostJson(const std::string& requestBody) {
    return impl_->removeAppleRemoteHostJson(requestBody);
}

OperationResult MasterControlApplication::upsertMcpServerJson(const std::string& requestBody) {
    return impl_->upsertMcpServerJson(requestBody);
}

OperationResult MasterControlApplication::removeMcpServerJson(const std::string& requestBody) {
    return impl_->removeMcpServerJson(requestBody);
}

OperationResult MasterControlApplication::upsertSubAgentJson(const std::string& requestBody) {
    return impl_->upsertSubAgentJson(requestBody);
}

OperationResult MasterControlApplication::removeSubAgentJson(const std::string& requestBody) {
    return impl_->removeSubAgentJson(requestBody);
}

OperationResult MasterControlApplication::upsertSubAgentGroupJson(const std::string& requestBody) {
    return impl_->upsertSubAgentGroupJson(requestBody);
}

OperationResult MasterControlApplication::removeSubAgentGroupJson(const std::string& requestBody) {
    return impl_->removeSubAgentGroupJson(requestBody);
}

OperationResult MasterControlApplication::upsertProviderAssignmentJson(const std::string& requestBody) {
    return impl_->upsertProviderAssignmentJson(requestBody);
}

ProviderExecutionRecord MasterControlApplication::executeProviderTaskJson(const std::string& requestBody) {
    return impl_->executeProviderTaskJson(requestBody);
}

OperationResult MasterControlApplication::installPackageJson(const std::string& requestBody) {
    return impl_->installPackageJson(requestBody);
}

OperationResult MasterControlApplication::installRepoJson(const std::string& requestBody) {
    return impl_->installRepoJson(requestBody);
}

OperationResult MasterControlApplication::installZipJson(const std::string& requestBody) {
    return impl_->installZipJson(requestBody);
}

BeaconAdvertisement MasterControlApplication::beaconAdvertisement() const {
    return impl_->beaconAdvertisement();
}

} // namespace MasterControl
