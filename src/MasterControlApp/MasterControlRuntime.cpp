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
#include "MasterControl/MasterControlDiagnostics.h"
#include "ForsettiPlatform/DefaultPlatformServices.h"
#include "MasterControl/AuthenticatedRequestContext.h"
#include "MasterControl/ILanClientAccessService.h"
#include "MasterControl/MasterControlDefaults.h"
#include "MasterControl/MasterControlModules.h"
#include "MasterControl/MasterControlVersion.h"

#include <bcrypt.h>
#include <iomanip>
#include "MasterControl/McpGatewayAdapters.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <Windows.h>
#include <ShlObj.h>    // SHGetFolderPathW + CSIDL_APPDATA — used by findCommandOnPath
                       // to locate npm-global binaries under %APPDATA%\npm.
#include <Userenv.h>
#include <WtsApi32.h>
#include <iphlpapi.h>
#include <psapi.h>
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

bool isRemoteSource(const std::string& source) {
    return startsWith(source, "http://") || startsWith(source, "https://");
}

// Resolve the externally-reachable host portion of the MCOS admin URL.
// Per ADR-001 the LAN client config bundle must never serve "0.0.0.0" - the
// bundle is consumed by AI clients on other hosts who cannot route to the
// wildcard bind address. Falls back through preferredBindAddress (set by the
// environment-discovery module on startup) and finally "127.0.0.1" so the
// bundle is always self-consistent even when the host network detection
// returns nothing.
std::string resolveMcosServerHost(const AppConfiguration& configuration) {
    if (!configuration.bindAddress.empty() && configuration.bindAddress != "0.0.0.0") {
        return configuration.bindAddress;
    }
    if (!configuration.activeProfile.preferredBindAddress.empty()) {
        return configuration.activeProfile.preferredBindAddress;
    }
    return "127.0.0.1";
}

// ---------------------------------------------------------------------------
// WS1 / WS4 / WS6 — Setup helpers
// ---------------------------------------------------------------------------

// Readiness snapshot assembly. Reduced to MCP and sub-agent catalogs after the
// AI provider stack was removed per ADR-001. Phase 3+ reintroduces a readiness
// path for LAN client registration.
ReadinessSnapshot computeReadinessSnapshot(const DashboardSnapshot& snapshot,
                                           const AppConfiguration& config) {
    ReadinessSnapshot result;
    result.setupStarted = !config.firstRunStartedAtUtc.empty() || config.firstRunCompleted;
    result.firstRunCompleted = config.firstRunCompleted;

    for (const auto& endpoint : snapshot.endpoints) {
        if (endpoint.kind != EndpointKind::MCPServer) {
            continue;
        }
        if (endpoint.isTemplate) { ++result.mcpMissingCount; continue; }
        if (endpoint.status == EndpointStatus::Online) { ++result.mcpReadyCount; }
        else { ++result.mcpMissingCount; }
    }

    for (const auto& endpoint : snapshot.endpoints) {
        if (endpoint.kind != EndpointKind::SubAgent) {
            continue;
        }
        if (endpoint.isTemplate) { ++result.specialistsMissingCount; continue; }
        ++result.specialistsReadyCount;
    }

    result.workflowsReadyCount = 0;
    result.workflowsMissingCount = 1;

    if (result.mcpReadyCount == 0) {
        result.blockingIssues.push_back(ReadinessIssue{
            "mcp.none-ready", "mcp", "warning",
            "No MCP servers online",
            "Add or bring online at least one MCP server so LAN clients can share tool lanes.",
            "runtime", "Add MCP server"
        });
        result.recommendedNextStep = "add-mcp";
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

    return { nodejs };
}

// Starter workflow template catalog.
std::vector<StarterWorkflowTemplate> buildStarterWorkflowTemplates() {
    return {};
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

constexpr auto kCliAuthFileGracePeriod = std::chrono::seconds(30);

std::optional<std::filesystem::path> environmentPathVariable(const wchar_t* name) {
    const DWORD required = GetEnvironmentVariableW(name, nullptr, 0);
    if (required <= 1) {
        return std::nullopt;
    }

    std::wstring value(static_cast<size_t>(required), L'\0');
    const DWORD copied = GetEnvironmentVariableW(name, value.data(), required);
    if (copied == 0) {
        return std::nullopt;
    }
    value.resize(static_cast<size_t>(copied));
    if (value.empty()) {
        return std::nullopt;
    }
    return std::filesystem::path(value);
}

std::optional<std::filesystem::path> interactiveUserProfileDirectory() {
    if (const auto overrideProfile = environmentPathVariable(L"MASTERCONTROL_INTERACTIVE_USERPROFILE");
        overrideProfile.has_value() && !overrideProfile->empty()) {
        return overrideProfile;
    }

    const DWORD activeSessionId = WTSGetActiveConsoleSessionId();
    if (activeSessionId != 0xFFFFFFFF) {
        HANDLE userToken = nullptr;
        if (WTSQueryUserToken(activeSessionId, &userToken) != 0) {
            DWORD required = 0;
            GetUserProfileDirectoryW(userToken, nullptr, &required);
            if (required > 1) {
                std::wstring profile(static_cast<size_t>(required - 1), L'\0');
                if (GetUserProfileDirectoryW(userToken, profile.data(), &required) != 0 && !profile.empty()) {
                    CloseHandle(userToken);
                    return std::filesystem::path(profile);
                }
            }
            CloseHandle(userToken);
        }
    }

    if (const auto currentProfile = environmentPathVariable(L"USERPROFILE");
        currentProfile.has_value() && !currentProfile->empty()) {
        return currentProfile;
    }

    PWSTR profilePath = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Profile, KF_FLAG_DEFAULT, nullptr, &profilePath)) &&
        profilePath != nullptr) {
        const std::filesystem::path profile(profilePath);
        CoTaskMemFree(profilePath);
        return profile;
    }
    if (profilePath != nullptr) {
        CoTaskMemFree(profilePath);
    }
    return std::nullopt;
}

std::optional<std::filesystem::path> interactiveUserAppDataDirectory() {
    if (const auto profile = interactiveUserProfileDirectory(); profile.has_value() && !profile->empty()) {
        return *profile / L"AppData" / L"Roaming";
    }
    if (const auto appData = environmentPathVariable(L"APPDATA"); appData.has_value() && !appData->empty()) {
        return appData;
    }
    return std::nullopt;
}

std::optional<std::filesystem::path> interactiveUserLocalAppDataDirectory() {
    if (const auto profile = interactiveUserProfileDirectory(); profile.has_value() && !profile->empty()) {
        return *profile / L"AppData" / L"Local";
    }
    if (const auto localAppData = environmentPathVariable(L"LOCALAPPDATA");
        localAppData.has_value() && !localAppData->empty()) {
        return localAppData;
    }
    return std::nullopt;
}

void appendInteractiveUserProfileEnvironment(std::vector<std::pair<std::wstring, std::wstring>>& environmentOverrides) {
    const auto interactiveProfile = interactiveUserProfileDirectory();
    if (!interactiveProfile.has_value() || interactiveProfile->empty()) {
        return;
    }

    environmentOverrides.emplace_back(L"USERPROFILE", interactiveProfile->wstring());
    environmentOverrides.emplace_back(L"HOME", interactiveProfile->wstring());
    if (const auto appData = interactiveUserAppDataDirectory(); appData.has_value() && !appData->empty()) {
        environmentOverrides.emplace_back(L"APPDATA", appData->wstring());
    }
    if (const auto localAppData = interactiveUserLocalAppDataDirectory();
        localAppData.has_value() && !localAppData->empty()) {
        environmentOverrides.emplace_back(L"LOCALAPPDATA", localAppData->wstring());
    }
}

std::optional<std::filesystem::path> resolveCommandProcessorPath() {
    if (const auto comSpec = environmentPathVariable(L"ComSpec"); comSpec.has_value() && !comSpec->empty()) {
        std::error_code error;
        if (std::filesystem::exists(*comSpec, error)) {
            return comSpec;
        }
    }

    wchar_t systemRoot[MAX_PATH] = {};
    const auto systemRootLength = GetEnvironmentVariableW(L"SystemRoot", systemRoot, MAX_PATH);
    if (systemRootLength > 0 && systemRootLength < MAX_PATH) {
        const auto candidate = std::filesystem::path(systemRoot) / L"System32" / L"cmd.exe";
        std::error_code error;
        if (std::filesystem::exists(candidate, error)) {
            return candidate;
        }
    }

    for (const auto* fileName : { L"cmd.exe", L"cmd" }) {
        std::array<wchar_t, 4096> buffer{};
        const DWORD length = SearchPathW(nullptr, fileName, nullptr, static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
        if (length > 0 && length < buffer.size()) {
            return std::filesystem::path(buffer.data());
        }
    }

    return std::nullopt;
}

std::optional<std::filesystem::path> findCommandOnPath(const std::vector<std::wstring>& fileNames) {
    // 1. Explicit well-known npm-global bin directories. When the service
    //    runs as LocalSystem, `npm install -g` drops binaries into
    //    %SystemRoot%\System32\config\systemprofile\AppData\Roaming\npm
    //    which isn't on the process PATH. When the interactive user installed
    //    the CLI, %USERPROFILE%\AppData\Roaming\npm is the target even though
    //    the service itself is running as LocalSystem. Also probe the nodejs
    //    install dir in case npm wrote a binary alongside node itself.
    std::vector<std::filesystem::path> fallbackDirs;
    if (const auto interactiveAppData = interactiveUserAppDataDirectory();
        interactiveAppData.has_value() && !interactiveAppData->empty()) {
        fallbackDirs.emplace_back(*interactiveAppData / L"npm");
    }
    wchar_t systemRoot[MAX_PATH] = {};
    const auto systemRootLen = GetEnvironmentVariableW(L"SystemRoot", systemRoot, MAX_PATH);
    if (systemRootLen > 0 && systemRootLen < MAX_PATH) {
        fallbackDirs.emplace_back(
            std::filesystem::path(systemRoot) / L"System32" / L"config" / L"systemprofile" / L"AppData" / L"Roaming" / L"npm");
    }
    if (const auto currentAppData = environmentPathVariable(L"APPDATA");
        currentAppData.has_value() && !currentAppData->empty()) {
        fallbackDirs.emplace_back(*currentAppData / L"npm");
    } else {
        wchar_t appDataBuf[MAX_PATH] = {};
        if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appDataBuf))) {
            fallbackDirs.emplace_back(std::filesystem::path(appDataBuf) / L"npm");
        }
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
    // 2. Standard SearchPathW which honours the process %PATH%. This is a
    //    useful fallback, but we intentionally prefer the interactive user's
    //    npm-global shim directory above so the host-installed CLI wins over
    //    any stale or service-account copy on PATH.
    for (const auto& fileName : fileNames) {
        std::array<wchar_t, 4096> buffer{};
        const DWORD length = SearchPathW(nullptr, fileName.c_str(), nullptr, static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
        if (length > 0 && length < buffer.size()) {
            return std::filesystem::path(buffer.data());
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
        response.errorMessage = "Unable to connect to the remote endpoint.";
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
        response.errorMessage = "Unable to open the remote endpoint request.";
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
        response.errorMessage = "The remote endpoint request failed.";
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

    // Cap accumulated response at 32 MiB. Any upstream endpoint that legitimately
    // needs more than this can be revisited; for now, a malformed or runaway
    // response must not be able to exhaust server memory.
    constexpr size_t kMaxResponseBytes = 32ull * 1024ull * 1024ull;
    std::string responseBody;
    for (;;) {
        DWORD available = 0;
        if (WinHttpQueryDataAvailable(request, &available) == 0 || available == 0) {
            break;
        }

        if (responseBody.size() + static_cast<size_t>(available) > kMaxResponseBytes) {
            response.errorMessage = "Remote endpoint response exceeded the 32 MiB size limit.";
            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connection);
            WinHttpCloseHandle(session);
            return response;
        }

        std::string chunk(static_cast<size_t>(available), '\0');
        DWORD bytesRead = 0;
        if (WinHttpReadData(request, chunk.data(), available, &bytesRead) == 0) {
            response.errorMessage = "Unable to read the remote endpoint response.";
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
        response.errorMessage = "Remote endpoint returned an unsuccessful status code.";
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
    if (CryptProtectData(&inputBlob, L"MasterControlSecretStore", nullptr, nullptr, nullptr, 0, &outputBlob) == 0) {
        throw std::runtime_error("Unable to protect DPAPI secret payload.");
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
        throw std::runtime_error("Unable to unprotect DPAPI secret payload.");
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
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    BootstrapManifestContract,
    version,
    bootstrapScript,
    bootstrapArguments,
    seededEndpoints)

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

EntitlementStateDocument buildDefaultEntitlementStateDocument() {
    return EntitlementStateDocument{
        {
            "com.mastercontrol.environment-discovery",
            "com.mastercontrol.host-telemetry",
            "com.mastercontrol.runtime-inventory",
            "com.mastercontrol.configuration",
            "com.mastercontrol.dashboard-ui"
        },
        {
            "mastercontrol.iap.installer-import",
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




// CLI-based sign-in service. Spawns `claude login` or `codex login` in a new
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

            auto& groups = state_->configuration.subAgentGroups;
            for (auto& group : groups) {
                group.memberTargetIds.erase(
                    std::remove(group.memberTargetIds.begin(), group.memberTargetIds.end(), subAgentId),
                    group.memberTargetIds.end());
            }

            groups.erase(
                std::remove_if(
                    groups.begin(),
                    groups.end(),
                    [](const SubAgentGroupDefinition& group) { return group.memberTargetIds.empty(); }),
                groups.end());

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

// Forward-declared activity emitter. The activity ring class is defined
// further down (it lives with the HTTP layer); this thin free-function
// wrapper lets earlier services emit events without needing the full ring
// type at their point of definition. The implementation lives below the
// ring class definition.
void appendLanClientActivity(const std::string& kind,
                             const std::string& target,
                             const std::string& message);

// Phase 5 of ADR-001: server-authored configuration bundle that an AI
// client downloads (via GET /api/clients/{id}/config) and drops onto its
// host. The bundle is the onboarding primitive - it tells the client how
// to reach MCOS, what header to identify itself with, what privileges it
// carries, what catalogs to discover, and what governance rules apply.
//
// The shape is fixed at schemaVersion 1.0. Future schema bumps add fields
// without removing existing ones so older clients stay compatible.
nlohmann::json composeLanClientConfigBundle(const LanClient& client,
                                            const AppConfiguration& configuration) {
    const auto host = resolveMcosServerHost(configuration);
    const auto mcosServer = "http://" + host + ":" + std::to_string(configuration.browserPort);

    nlohmann::json bundle;
    bundle["schemaVersion"] = "1.0";
    bundle["issuedAtUtc"] = timestampNowUtc();
    bundle["mcosServer"] = mcosServer;
    bundle["clientId"] = client.clientId;
    bundle["displayName"] = client.displayName;
    bundle["clientType"] = client.clientType;
    bundle["enabled"] = client.enabled;
    bundle["identification"] = nlohmann::json{
        { "header", "X-MCOS-Client-Id" },
        { "value", client.clientId }
    };
    bundle["privileges"] = client.privileges;
    bundle["autonomousMode"] = client.autonomousMode;
    bundle["catalogs"] = nlohmann::json{
        { "mcpServers", "/api/client/mcp-servers" },
        { "subAgents", "/api/client/sub-agents" },
        { "activity", "/api/client/activity" }
    };
    bundle["governance"] = nlohmann::json{
        { "authority", "CLU" },
        { "framework", "Forsetti Framework for Agentic Coding" },
        { "profileEndpoint", "/api/client/governance/profile" },
        { "decisionEndpoint", "/api/client/governance/decisions" }
    };
    bundle["rules"] = nlohmann::json::array({
        "All MCP servers registered with MCOS are available for use by every LAN client.",
        "All sub-agents registered with MCOS are available for use by every LAN client.",
        "Creation, modification, and removal of MCP servers and sub-agents are governed by the privileges listed above.",
        "Autonomous mode (when enabled) allows unlimited creation of MCP servers and sub-agents. All other actions remain privilege-gated.",
        "Every action is recorded in the MCOS activity stream and evaluated by CLU per Forsetti governance."
    });
    bundle["instructions"] = nlohmann::json{
        { "heartbeat", "POST /api/client/heartbeat at least every 60 seconds to remain in the live roster." },
        { "discovery", "Use the catalogs to discover the current shared fabric." },
        { "invocation", "MCP servers and sub-agents are addressed directly using the endpoint metadata in each catalog entry." },
        { "governance", "Before any privileged mutation, GET the governance profile and pre-check with the decisionEndpoint." }
    };
    return bundle;
}

// LanClientAccessService - Phase 3 of ADR-001 LAN Client Control Plane.
// Owns persistence of the LanClient roster and emits activity events on
// every lifecycle mutation. Identity is by clientId alone; no secrets are
// exchanged on the trusted LAN. Phase 4 fills the LanClientPrivileges
// boolean fields. Phase 6 attaches client identity to incoming requests
// via X-MCOS-Client-Id header lookup against this service.
//
// Future cleanup: extract to its own translation unit once the SharedState
// + writeJsonFile + isBlank/trimCopy helpers move to a shared internal
// header. Pattern matches other in-process services in this file for now.
class LanClientAccessService final : public ILanClientAccessService {
public:
    LanClientAccessService(std::shared_ptr<SharedState> state,
                           std::filesystem::path configurationFile)
        : state_(std::move(state))
        , configurationFile_(std::move(configurationFile)) {}

    std::vector<LanClient> listClients() const override {
        std::lock_guard<std::mutex> lock(state_->mutex);
        return state_->configuration.lanClients;
    }

    std::optional<LanClient> getClient(const std::string& clientId) const override {
        if (clientId.empty()) {
            return std::nullopt;
        }
        const auto normalized = normalizeId(clientId);
        std::lock_guard<std::mutex> lock(state_->mutex);
        const auto iterator = std::find_if(
            state_->configuration.lanClients.begin(),
            state_->configuration.lanClients.end(),
            [&normalized](const LanClient& candidate) { return candidate.clientId == normalized; });
        if (iterator == state_->configuration.lanClients.end()) {
            return std::nullopt;
        }
        return *iterator;
    }

    OperationResult upsertClient(const LanClient& input) override {
        if (input.clientId.empty() || isBlank(input.clientId)) {
            return OperationResult{ false, false, "LAN client id is required." };
        }
        if (input.displayName.empty() || isBlank(input.displayName)) {
            return OperationResult{ false, false, "LAN client display name is required." };
        }

        LanClient normalized = input;
        normalized.clientId = normalizeId(normalized.clientId);
        normalized.displayName = trimCopy(normalized.displayName);
        normalized.clientType = trimCopy(normalized.clientType);
        normalized.hostName = trimCopy(normalized.hostName);
        normalized.networkAddress = trimCopy(normalized.networkAddress);

        // Phase 7 has lifted the autonomous-mode soft gate. CLU enforces
        // the policy now via the ClientAutonomousModeChange action kind.
        // upsertClient stores whatever the caller provides; the route
        // handler runs CLU enforcement before invoking us.

        const auto now = timestampNowUtc();
        bool createdNew = false;

        {
            std::lock_guard<std::mutex> lock(state_->mutex);
            auto& clients = state_->configuration.lanClients;
            const auto iterator = std::find_if(
                clients.begin(),
                clients.end(),
                [&normalized](const LanClient& candidate) { return candidate.clientId == normalized.clientId; });
            if (iterator == clients.end()) {
                if (normalized.createdAtUtc.empty()) {
                    normalized.createdAtUtc = now;
                }
                if (normalized.lastSeenUtc.empty()) {
                    normalized.lastSeenUtc = now;
                }
                clients.push_back(normalized);
                createdNew = true;
            } else {
                normalized.createdAtUtc = iterator->createdAtUtc.empty() ? now : iterator->createdAtUtc;
                if (normalized.lastSeenUtc.empty()) {
                    normalized.lastSeenUtc = iterator->lastSeenUtc;
                }
                if (!normalized.enabled && iterator->enabled) {
                    normalized.disabledAtUtc = now;
                } else if (normalized.enabled) {
                    normalized.disabledAtUtc.clear();
                }
                *iterator = normalized;
            }

            std::sort(
                clients.begin(),
                clients.end(),
                [](const LanClient& left, const LanClient& right) {
                    return std::tie(left.displayName, left.clientId) < std::tie(right.displayName, right.clientId);
                });

            if (!writeJsonFile(configurationFile_, state_->configuration)) {
                return OperationResult{ false, false, "LAN client could not be written to disk." };
            }
        }

        emitEvent(createdNew ? "lan-client-created" : "lan-client-updated",
                  normalized.clientId,
                  createdNew
                    ? "Registered LAN client " + normalized.clientId
                    : "Updated LAN client " + normalized.clientId);
        return OperationResult{ true, false, createdNew ? "LAN client registered." : "LAN client updated." };
    }

    OperationResult disableClient(const std::string& clientId) override {
        return setEnabled(clientId, false);
    }

    OperationResult enableClient(const std::string& clientId) override {
        return setEnabled(clientId, true);
    }

    OperationResult removeClient(const std::string& clientId) override {
        if (clientId.empty() || isBlank(clientId)) {
            return OperationResult{ false, false, "LAN client id is required." };
        }
        const auto normalized = normalizeId(clientId);

        {
            std::lock_guard<std::mutex> lock(state_->mutex);
            auto& clients = state_->configuration.lanClients;
            const auto originalSize = clients.size();
            clients.erase(
                std::remove_if(
                    clients.begin(),
                    clients.end(),
                    [&normalized](const LanClient& candidate) { return candidate.clientId == normalized; }),
                clients.end());
            if (clients.size() == originalSize) {
                return OperationResult{ false, false, "LAN client not found." };
            }
            if (!writeJsonFile(configurationFile_, state_->configuration)) {
                return OperationResult{ false, false, "LAN client removal could not be written to disk." };
            }
        }

        emitEvent("lan-client-removed", normalized, "Removed LAN client " + normalized);
        return OperationResult{ true, false, "LAN client removed." };
    }

    void touchClient(const std::string& clientId, const std::string& observedAddress) override {
        if (clientId.empty()) {
            return;
        }
        const auto normalized = normalizeId(clientId);
        const auto now = timestampNowUtc();
        std::lock_guard<std::mutex> lock(state_->mutex);
        auto& clients = state_->configuration.lanClients;
        const auto iterator = std::find_if(
            clients.begin(),
            clients.end(),
            [&normalized](const LanClient& candidate) { return candidate.clientId == normalized; });
        if (iterator == clients.end()) {
            return;
        }
        iterator->lastSeenUtc = now;
        if (!observedAddress.empty()) {
            iterator->networkAddress = observedAddress;
        }
        // touchClient is hot-path; deliberately skip writeJsonFile to avoid
        // disk thrash on every authenticated request. Phase 6 may add a
        // periodic flush if last-seen survival across restarts becomes a
        // requirement.
    }

    OperationResult setPrivileges(const std::string& clientId,
                                  const LanClientPrivileges& privileges) override {
        if (clientId.empty() || isBlank(clientId)) {
            return OperationResult{ false, false, "LAN client id is required." };
        }
        const auto normalized = normalizeId(clientId);

        {
            std::lock_guard<std::mutex> lock(state_->mutex);
            auto& clients = state_->configuration.lanClients;
            const auto iterator = std::find_if(
                clients.begin(),
                clients.end(),
                [&normalized](const LanClient& candidate) { return candidate.clientId == normalized; });
            if (iterator == clients.end()) {
                return OperationResult{ false, false, "LAN client not found." };
            }
            iterator->privileges = privileges;
            if (!writeJsonFile(configurationFile_, state_->configuration)) {
                return OperationResult{ false, false, "LAN client privileges could not be written to disk." };
            }
        }

        emitEvent("lan-client-privileges-changed",
                  normalized,
                  "Updated privileges for LAN client " + normalized);
        return OperationResult{ true, false, "LAN client privileges updated." };
    }

    OperationResult setAutonomousMode(const std::string& clientId, bool enabled) override {
        if (clientId.empty() || isBlank(clientId)) {
            return OperationResult{ false, false, "LAN client id is required." };
        }
        const auto normalized = normalizeId(clientId);
        const auto now = timestampNowUtc();

        {
            std::lock_guard<std::mutex> lock(state_->mutex);
            auto& clients = state_->configuration.lanClients;
            const auto iterator = std::find_if(
                clients.begin(),
                clients.end(),
                [&normalized](const LanClient& candidate) { return candidate.clientId == normalized; });
            if (iterator == clients.end()) {
                return OperationResult{ false, false, "LAN client not found." };
            }
            if (iterator->autonomousMode == enabled) {
                return OperationResult{
                    true,
                    false,
                    enabled ? "Autonomous mode was already enabled." : "Autonomous mode was already disabled."
                };
            }
            iterator->autonomousMode = enabled;
            if (!writeJsonFile(configurationFile_, state_->configuration)) {
                return OperationResult{ false, false, "Autonomous mode change could not be written to disk." };
            }
        }

        emitEvent("lan-client-autonomous-mode-changed",
                  normalized,
                  (enabled ? std::string("Enabled autonomous mode for LAN client ")
                           : std::string("Disabled autonomous mode for LAN client "))
                  + normalized);
        return OperationResult{ true, false, enabled ? "Autonomous mode enabled." : "Autonomous mode disabled." };
    }

private:
    static std::string normalizeId(std::string clientId) {
        std::transform(clientId.begin(), clientId.end(), clientId.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        return clientId;
    }

    OperationResult setEnabled(const std::string& clientId, bool enabled) {
        if (clientId.empty() || isBlank(clientId)) {
            return OperationResult{ false, false, "LAN client id is required." };
        }
        const auto normalized = normalizeId(clientId);
        const auto now = timestampNowUtc();

        {
            std::lock_guard<std::mutex> lock(state_->mutex);
            auto& clients = state_->configuration.lanClients;
            const auto iterator = std::find_if(
                clients.begin(),
                clients.end(),
                [&normalized](const LanClient& candidate) { return candidate.clientId == normalized; });
            if (iterator == clients.end()) {
                return OperationResult{ false, false, "LAN client not found." };
            }
            if (iterator->enabled == enabled) {
                return OperationResult{ true, false,
                    enabled ? "LAN client was already enabled." : "LAN client was already disabled." };
            }
            iterator->enabled = enabled;
            iterator->disabledAtUtc = enabled ? std::string() : now;
            if (!writeJsonFile(configurationFile_, state_->configuration)) {
                return OperationResult{ false, false, "LAN client state could not be written to disk." };
            }
        }

        emitEvent(enabled ? "lan-client-enabled" : "lan-client-disabled",
                  normalized,
                  (enabled ? "Enabled LAN client " : "Disabled LAN client ") + normalized);
        return OperationResult{ true, false, enabled ? "LAN client enabled." : "LAN client disabled." };
    }

    void emitEvent(const std::string& kind, const std::string& clientId, const std::string& message) const {
        appendLanClientActivity(kind, clientId, message);
    }

    std::shared_ptr<SharedState> state_;
    std::filesystem::path configurationFile_;
};

// Phase 7: in-memory approval queue. Mutations whose CLU outcome is
// RequiresOperatorApproval are staged here until an operator approves
// (the original mutation is replayed) or rejects (the deferred record
// is closed without effect). Persistence across service restarts is
// deliberately deferred - long-running deferrals are operationally
// suspect on a trusted LAN, so the queue lives in process memory only.
class GovernanceApprovalQueueService final : public IGovernanceApprovalQueueService {
public:
    std::vector<GovernanceDeferredAction> listPending() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<GovernanceDeferredAction> pending;
        for (const auto& action : actions_) {
            if (action.status == "pending") {
                pending.push_back(action);
            }
        }
        return pending;
    }

    std::vector<GovernanceDeferredAction> listAll() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return actions_;
    }

    GovernanceDeferredAction stage(const GovernanceDeferredAction& input) override {
        std::lock_guard<std::mutex> lock(mutex_);
        GovernanceDeferredAction action = input;
        ++nextSequence_;
        action.id = "deferred-" + std::to_string(nextSequence_);
        action.status = "pending";
        action.createdAtUtc = timestampNowUtc();
        actions_.push_back(action);
        appendLanClientActivity(
            "governance-deferred",
            action.id,
            std::string("Staged ") + to_string(action.action) + " for operator approval (actor=" + action.actor + ")");
        return action;
    }

    OperationResult approve(const std::string& deferredActionId,
                            const std::string& operatorActor) override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto* action = findById(deferredActionId);
        if (action == nullptr) {
            return OperationResult{ false, false, "Deferred action not found." };
        }
        if (action->status != "pending") {
            return OperationResult{ false, false,
                "Deferred action is no longer pending (current status: " + action->status + ")." };
        }
        action->status = "approved";
        action->decidedAtUtc = timestampNowUtc();
        action->decidedBy = operatorActor.empty() ? std::string("operator") : operatorActor;
        appendLanClientActivity(
            "governance-approved",
            action->id,
            std::string("Operator approved deferred ") + to_string(action->action) + " for actor=" + action->actor);
        return OperationResult{ true, false, "Deferred action approved." };
    }

    OperationResult reject(const std::string& deferredActionId,
                           const std::string& operatorActor,
                           const std::string& reason) override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto* action = findById(deferredActionId);
        if (action == nullptr) {
            return OperationResult{ false, false, "Deferred action not found." };
        }
        if (action->status != "pending") {
            return OperationResult{ false, false,
                "Deferred action is no longer pending (current status: " + action->status + ")." };
        }
        action->status = "rejected";
        action->decidedAtUtc = timestampNowUtc();
        action->decidedBy = operatorActor.empty() ? std::string("operator") : operatorActor;
        action->reason = reason;
        appendLanClientActivity(
            "governance-rejected",
            action->id,
            std::string("Operator rejected deferred ") + to_string(action->action) + " (reason=" + reason + ")");
        return OperationResult{ true, false, "Deferred action rejected." };
    }

private:
    GovernanceDeferredAction* findById(const std::string& id) {
        for (auto& action : actions_) {
            if (action.id == id) {
                return &action;
            }
        }
        return nullptr;
    }

    mutable std::mutex mutex_;
    std::vector<GovernanceDeferredAction> actions_;
    uint64_t nextSequence_ = 0;
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
    std::ostringstream summary;
    summary << "Bootstrap exited with code " << bootstrapExitCode;
    if (bootstrapExitCode == 0 && registeredEndpoints > 0U) {
        summary << "; registered " << registeredEndpoints << " endpoints";
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
    if (bootstrapExitCode == 0 && registeredEndpoints > 0U) {
        message << " Registered " << registeredEndpoints << " endpoints.";
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
    std::ostringstream summary;
    summary << "Zip bootstrap exited with code " << bootstrapExitCode;
    if (bootstrapExitCode == 0 && registeredEndpoints > 0U) {
        summary << "; registered " << registeredEndpoints << " endpoints";
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
    if (bootstrapExitCode == 0 && registeredEndpoints > 0U) {
        message << " Registered " << registeredEndpoints << " endpoints.";
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
                  std::shared_ptr<IConfigurationService> configurationService,
                  std::shared_ptr<ILanClientAccessService> lanClientAccessService)
        : inventoryService_(std::move(inventoryService))
        , configurationService_(std::move(configurationService))
        , lanClientAccessService_(std::move(lanClientAccessService)) {}

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
            { "clientType", "openai" },
            { "recommendedModel", "gpt-5" }
        };

        nlohmann::json xAiJson = {
            { "gateway", gatewayUrl },
            { "clientType", "xai" },
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

        std::vector<ExportArtifact> artifacts = {
            ExportArtifact{ "gateway-profile", "master-control-gateway-profile.json", "application/json", gatewayProfile.dump(2) },
            ExportArtifact{ "claude", ".claude.json", "application/json", claudeJson.dump(2) },
            ExportArtifact{ "claude-installer", "Install-ClaudeGateway.ps1", "text/plain", claudeScript.str() },
            ExportArtifact{ "codex", "codex-mcp.json", "application/json", codexJson.dump(2) },
            ExportArtifact{ "codex-installer", "Install-CodexGateway.ps1", "text/plain", codexScript.str() },
            ExportArtifact{ "openai", "openai-gateway.json", "application/json", openAiJson.dump(2) },
            ExportArtifact{ "xai", "xai-gateway.json", "application/json", xAiJson.dump(2) }
        };

        // Phase 5 of ADR-001: surface a per-LAN-client config bundle so
        // operators can download bundles from the Exports surface as well
        // as from the dedicated /api/clients/{id}/config route. Disabled
        // clients are deliberately omitted - their bundle would tell the
        // remote AI it can connect when in fact MCOS rejects its requests.
        if (lanClientAccessService_) {
            for (const auto& client : lanClientAccessService_->listClients()) {
                if (!client.enabled || client.clientId.empty()) {
                    continue;
                }
                const auto bundle = composeLanClientConfigBundle(client, configuration);
                artifacts.push_back(ExportArtifact{
                    "lan-client-config:" + client.clientId,
                    "lan-client-" + client.clientId + ".json",
                    "application/json",
                    bundle.dump(2)
                });
            }
        }

        return artifacts;
    }

private:
    std::shared_ptr<IRuntimeInventoryService> inventoryService_;
    std::shared_ptr<IConfigurationService> configurationService_;
    std::shared_ptr<ILanClientAccessService> lanClientAccessService_;
};

class CommandLogicUnitService final : public ICommandLogicUnitService {
public:
    CommandLogicUnitService(std::filesystem::path profileFile,
                            std::shared_ptr<IConfigurationService> configurationService,
                            std::shared_ptr<IInstallerOrchestrator> installerOrchestrator,
                            std::shared_ptr<IExportService> exportService,
                            std::shared_ptr<IAppleRemoteHostService> appleRemoteHostService,
                            std::shared_ptr<IPlatformServiceCatalogService> platformServiceCatalogService,
                            std::shared_ptr<IPlatformGovernanceToolService> platformGovernanceToolService)
        : profile_(loadProfile(std::move(profileFile)))
        , configurationService_(std::move(configurationService))
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
            "CLU preflights managed install actions against the configured local CPU, memory, bandwidth, and storage envelope before spawning governed workloads."
        });

        const auto configuration = configurationService_->current();
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

        if (configuration.resourceAllocation.cpuPercent <= 0) {
            appendFinding(
                "CLU-C008",
                "blocked",
                "Managed execution is blocked because CPU allocation is set to 0%. Raise the CPU envelope before running governed installs.");
            snapshot.recommendedActions.push_back("Increase CPU allocation above 0% to re-enable CLU-governed managed installs.");
        }

        if (configuration.resourceAllocation.memoryPercent <= 0) {
            appendFinding(
                "CLU-C008",
                "blocked",
                "Managed execution is blocked because memory allocation is set to 0%. Raise the memory envelope before running governed installs.");
            snapshot.recommendedActions.push_back("Increase memory allocation above 0% to re-enable CLU-governed managed installs.");
        }

        if (configuration.resourceAllocation.bandwidthPercent <= 0) {
            appendFinding(
                "CLU-C008",
                "blocked",
                "Governed network execution is blocked because bandwidth allocation is set to 0%. Raise the bandwidth envelope before running remote installs.");
            snapshot.recommendedActions.push_back("Increase bandwidth allocation above 0% to re-enable governed remote installs.");
        }

        if (configuration.resourceAllocation.storagePercent <= 0) {
            appendFinding(
                "CLU-C008",
                "blocked",
                "Managed execution is blocked because storage allocation is set to 0%. Raise the storage envelope before running governed installs.");
            snapshot.recommendedActions.push_back("Increase storage allocation above 0% to re-enable CLU-governed managed installs.");
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
        decision.outcome = GovernanceDecisionOutcome::Allow;

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
            decision.outcome = GovernanceDecisionOutcome::Block;
            decision.ruleId = "CLU-C008";
            decision.blockingFindings.push_back(resourceMessage);
            decision.message = "CLU blocked " + actionLabel + " because the managed resource policy denies launch. " + resourceMessage;
            return true;
        };

        // Phase 7 dispatch: every supported action kind passes through CLU
        // before the privilege gate's HTTP layer applies the decision.
        // Default outcome is Allow; specific kinds Block when posture is
        // bad or RequireOperatorApproval when the profile demands it.
        switch (request.action) {
            case GovernanceActionKind::RemoteInstall: {
                if (resourcePreflightBlock("managed install", isRemoteSource(request.source))) {
                    return decision;
                }
                if (isRemoteSource(request.source) && snapshot.posture == "blocked") {
                    decision.allowed = false;
                    decision.outcome = GovernanceDecisionOutcome::Block;
                    appendBlockedFindingMessages();
                    decision.message = "CLU blocked remote install while runtime posture is blocked.";
                    if (!decision.blockingFindings.empty()) {
                        decision.message += " " + decision.blockingFindings.front();
                    }
                }
                return decision;
            }

            case GovernanceActionKind::McpServerCreate:
            case GovernanceActionKind::SubAgentCreate:
            case GovernanceActionKind::McpServerModify:
            case GovernanceActionKind::SubAgentModify: {
                // Catalog mutations are allowed when posture is fine. They
                // do not require per-action approval; the privilege gates
                // already determined the caller has authority. CLU only
                // intervenes when global posture is blocked.
                if (snapshot.posture == "blocked") {
                    decision.allowed = false;
                    decision.outcome = GovernanceDecisionOutcome::Block;
                    appendBlockedFindingMessages();
                    decision.message = "CLU blocked catalog mutation while runtime posture is blocked.";
                }
                return decision;
            }

            case GovernanceActionKind::McpServerRemove:
            case GovernanceActionKind::SubAgentRemove: {
                // Removals are destructive. When posture is blocked CLU
                // refuses outright; otherwise the privilege gate is
                // sufficient. (Future profile rules may flip this to
                // RequiresOperatorApproval for shared-fabric resources.)
                if (snapshot.posture == "blocked") {
                    decision.allowed = false;
                    decision.outcome = GovernanceDecisionOutcome::Block;
                    appendBlockedFindingMessages();
                    decision.message = "CLU blocked catalog removal while runtime posture is blocked.";
                }
                return decision;
            }

            case GovernanceActionKind::ClientRegister:
            case GovernanceActionKind::ClientPrivilegeChange:
            case GovernanceActionKind::ClientRevoke: {
                // Client-roster mutations are operator-driven and already
                // privilege-gated by canManageClients. CLU only blocks
                // them when posture is blocked.
                if (snapshot.posture == "blocked") {
                    decision.allowed = false;
                    decision.outcome = GovernanceDecisionOutcome::Block;
                    appendBlockedFindingMessages();
                    decision.message = "CLU blocked LAN client roster change while runtime posture is blocked.";
                }
                return decision;
            }

            case GovernanceActionKind::ClientAutonomousModeChange: {
                // Autonomous mode is a privileged switch. CLU governs it
                // tightly: posture must be fine AND global aiAutonomyEnabled
                // must be true before any client may flip to autonomous.
                // Disabling autonomous mode (request.source == "disable")
                // is always allowed.
                if (request.source == "disable") {
                    return decision;
                }
                if (snapshot.posture == "blocked") {
                    decision.allowed = false;
                    decision.outcome = GovernanceDecisionOutcome::Block;
                    appendBlockedFindingMessages();
                    decision.message = "CLU blocked autonomous-mode change while runtime posture is blocked.";
                    return decision;
                }
                if (!configuration.aiAutonomyEnabled) {
                    decision.allowed = false;
                    decision.outcome = GovernanceDecisionOutcome::Block;
                    decision.ruleId = "CLU-C009";
                    decision.message = "Enable global AI autonomy in configuration before granting client autonomous mode.";
                    return decision;
                }
                return decision;
            }

            case GovernanceActionKind::ModuleEnable:
            case GovernanceActionKind::ModuleDisable: {
                // Module lifecycle is operator-only territory. Posture
                // governs this strictly because a misconfigured module set
                // can desync the runtime.
                if (snapshot.posture == "blocked") {
                    decision.allowed = false;
                    decision.outcome = GovernanceDecisionOutcome::Block;
                    appendBlockedFindingMessages();
                    decision.message = "CLU blocked Forsetti module lifecycle change while runtime posture is blocked.";
                }
                return decision;
            }

            case GovernanceActionKind::GovernancePolicyChange: {
                // Editing the CLU profile itself is always sensitive.
                // Defer to operator approval so changes are auditable.
                decision.outcome = GovernanceDecisionOutcome::RequiresOperatorApproval;
                decision.ruleId = "CLU-C010";
                decision.message = "Governance policy edits require operator approval.";
                return decision;
            }

            case GovernanceActionKind::Unknown:
            default:
                return decision;
        }
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

// PHASE-03 (ADR-002 §4): LAN Discovery Service. Composes the gateway-first
// discovery document, registers the three MCOS DNS-SD service types
// (`_mcos._tcp`, `_mcos-mcp._tcp`, `_mcos-onboarding._tcp`), and exposes the
// document to BeaconService for UDP broadcast and to the admin API for
// `/.well-known/mcos.json` and `/api/discovery` consumption.
class DiscoveryService final : public IDiscoveryService {
public:
    DiscoveryService(std::shared_ptr<IConfigurationService> configurationService,
                     std::shared_ptr<ITelemetryService> telemetryService,
                     std::shared_ptr<IMcpGateway> mcpGateway)
        : configurationService_(std::move(configurationService))
        , telemetryService_(std::move(telemetryService))
        , mcpGateway_(std::move(mcpGateway)) {}

    ~DiscoveryService() override {
        stop();
    }

    DiscoveryDocument currentDocument() const override {
        const auto configuration = configurationService_->current();
        const auto snapshot = telemetryService_->captureSnapshot();

        // Operator override takes precedence over auto-detected
        // primaryIpAddress so the discovery doc advertises the LAN IP
        // the operator chose, not whatever interface the
        // GetAdaptersAddresses enumeration happened to surface first
        // (which on dual-stack hosts is often the IPv6 ULA, not the
        // IPv4 LAN address clients expect).
        std::string lanIp;
        const std::string& preferred =
            configuration.activeProfile.preferredBindAddress;
        if (!preferred.empty() && preferred != "0.0.0.0") {
            lanIp = preferred;
        }
        if (lanIp.empty() || lanIp == "0.0.0.0") {
            lanIp = snapshot.primaryIpAddress;
        }
        if (lanIp.empty() || lanIp == "0.0.0.0") {
            lanIp = configuration.bindAddress;
        }
        if (lanIp.empty() || lanIp == "0.0.0.0") {
            lanIp = "127.0.0.1";
        }

        const std::string adminBase = "http://" + lanIp + ":" + std::to_string(configuration.browserPort);

        DiscoveryDocument document;
        document.product = "MCOS";
        document.role = "mcp-gateway-host";
        document.version = MASTERCONTROL_VERSION;
        document.instanceId = configuration.instanceId.empty()
            ? std::string("mcos-unidentified")
            : configuration.instanceId;
        document.instanceName = configuration.instanceName;
        document.trust = "lan";
        document.auth = "none";

        const auto& gatewayConfig = configuration.mcpGateway;
        std::string gatewayHost = gatewayConfig.listenHost;
        if (gatewayHost.empty() || gatewayHost == "0.0.0.0") {
            gatewayHost = lanIp;
        }
        document.gateway.type = mcpGateway_ ? mcpGateway_->AdapterType() : to_string(gatewayConfig.type);
        document.gateway.mcpUrl = mcpGateway_ ? mcpGateway_->GatewayMcpUrl()
                                              : ("http://" + gatewayHost + ":" + std::to_string(gatewayConfig.listenPort) + gatewayConfig.mcpPath);
        document.gateway.healthUrl = "http://" + gatewayHost + ":" + std::to_string(gatewayConfig.listenPort) + gatewayConfig.healthPath;
        document.gateway.state = mcpGateway_ ? to_string(mcpGateway_->CurrentStatus().state) : "disabled";

        document.onboarding.generic = adminBase + "/api/onboarding/generic";
        document.onboarding.claudeCode = adminBase + "/api/onboarding/claude-code";
        document.onboarding.codex = adminBase + "/api/onboarding/codex";
        document.onboarding.grok = adminBase + "/api/onboarding/grok";
        document.onboarding.chatgpt = adminBase + "/api/onboarding/chatgpt";

        document.governance.bundleBaseUrl = adminBase + "/api/governance/bundles";
        document.governance.cluProfileUrl = adminBase + "/api/governance/profile";
        document.governance.decisionsUrl = adminBase + "/api/governance/decisions";

        document.capabilities = {
            "mcp-gateway",
            std::string(document.gateway.type) + "-adapter",
            "dns-sd",
            "udp-beacon",
            "forsetti-governance",
            "clu"
        };
        document.serverIpAddress = lanIp;
        document.generatedAtUtc = timestampNowUtc();
        return document;
    }

    void start() override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (started_) {
            return;
        }
        started_ = true;
        registerAllInstancesLocked();
    }

    void stop() override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!started_) {
            return;
        }
        deregisterAllInstancesLocked();
        started_ = false;
    }

    // Public list of TXT key/value pairs for the DNS-SD records. Test code
    // and the `/api/discovery` route both consume this for completeness.
    std::map<std::string, std::string> dnsTxtFields() const {
        const auto configuration = configurationService_->current();
        const auto& gatewayConfig = configuration.mcpGateway;
        std::map<std::string, std::string> txt;
        txt["product"] = "MCOS";
        txt["role"] = "mcp-gateway";
        txt["gateway"] = mcpGateway_ ? mcpGateway_->AdapterType() : to_string(gatewayConfig.type);
        txt["mcp_path"] = gatewayConfig.mcpPath;
        txt["config_path"] = "/api/onboarding";
        txt["governance_path"] = "/api/governance/bundles";
        txt["protovers"] = "2025-03-26";
        txt["auth"] = "none";
        txt["trust"] = "lan";
        txt["clu"] = "true";
        txt["forsetti"] = "true";
        return txt;
    }

private:
    struct Registration final {
        std::string serviceType;
        std::string instanceLabel;
        uint16_t port = 0;
        DNS_SERVICE_REGISTER_REQUEST request{};
        PDNS_SERVICE_INSTANCE instance = nullptr;
        bool registered = false;
        std::string status; // "advertised" | "registration_failed" | "disabled"
        std::string lastError;
    };

    void registerAllInstancesLocked() {
        const auto configuration = configurationService_->current();
        const auto snapshot = telemetryService_->captureSnapshot();

        const std::string adminInstance = configuration.instanceName;
        registrations_.clear();

        struct Plan {
            const char* serviceType;
            std::string instanceSuffix;
            uint16_t port;
        };
        const Plan plans[] = {
            { "_mcos._tcp.local",            std::string{},               configuration.browserPort },
            { "_mcos-mcp._tcp.local",        std::string(" MCP"),         configuration.mcpGateway.listenPort },
            { "_mcos-onboarding._tcp.local", std::string(" Onboarding"),  configuration.browserPort }
        };

        for (const auto& plan : plans) {
            Registration registration;
            registration.serviceType = plan.serviceType;
            registration.instanceLabel = adminInstance + plan.instanceSuffix;
            registration.port = plan.port;
            registerOneLocked(registration, snapshot);
            registrations_.push_back(std::move(registration));
        }
    }

    void registerOneLocked(Registration& registration, const HostTelemetrySnapshot& snapshot) {
        const auto configuration = configurationService_->current();
        const auto txt = dnsTxtFields();

        std::vector<std::wstring> keysWide;
        std::vector<std::wstring> valuesWide;
        keysWide.reserve(txt.size());
        valuesWide.reserve(txt.size());
        for (const auto& [key, value] : txt) {
            keysWide.push_back(wideFromUtf8(key));
            valuesWide.push_back(wideFromUtf8(value));
        }
        std::vector<PCWSTR> keyPointers;
        std::vector<PCWSTR> valuePointers;
        keyPointers.reserve(keysWide.size());
        valuePointers.reserve(valuesWide.size());
        for (size_t i = 0; i < keysWide.size(); ++i) {
            keyPointers.push_back(keysWide[i].c_str());
            valuePointers.push_back(valuesWide[i].c_str());
        }

        const auto instanceName = wideFromUtf8(registration.instanceLabel + "." + registration.serviceType);
        const auto hostName = wideFromUtf8(dotLocalHostName(snapshot.hostName));

        // Same precedence as DiscoveryService::currentDocument: operator
        // override wins over auto-detection, so DNS-SD advertises the
        // LAN IP the operator chose rather than the auto-picked
        // (often IPv6 ULA) interface.
        std::string lanIp;
        const std::string& preferred =
            configuration.activeProfile.preferredBindAddress;
        if (!preferred.empty() && preferred != "0.0.0.0") {
            lanIp = preferred;
        }
        if (lanIp.empty() || lanIp == "0.0.0.0") {
            lanIp = snapshot.primaryIpAddress;
        }
        if (lanIp.empty() || lanIp == "0.0.0.0") {
            lanIp = configuration.bindAddress;
        }
        if (lanIp.empty() || lanIp == "0.0.0.0") {
            lanIp = "127.0.0.1";
        }

        IP4_ADDRESS ipv4Address = 0;
        PIP4_ADDRESS ipv4Pointer = nullptr;
        IN_ADDR parsedV4{};
        IP6_ADDRESS ipv6Address{};
        PIP6_ADDRESS ipv6Pointer = nullptr;
        IN6_ADDR parsedV6{};
        const auto wideIp = wideFromUtf8(lanIp);
        if (InetPtonW(AF_INET, wideIp.c_str(), &parsedV4) == 1) {
            ipv4Address = parsedV4.S_un.S_addr;
            ipv4Pointer = &ipv4Address;
        } else if (InetPtonW(AF_INET6, wideIp.c_str(), &parsedV6) == 1) {
            std::memcpy(&ipv6Address, &parsedV6, sizeof(ipv6Address));
            ipv6Pointer = &ipv6Address;
        } else {
            parsedV4.S_un.S_addr = htonl(INADDR_LOOPBACK);
            ipv4Address = parsedV4.S_un.S_addr;
            ipv4Pointer = &ipv4Address;
        }

        registration.instance = DnsServiceConstructInstance(
            instanceName.c_str(),
            hostName.c_str(),
            ipv4Pointer, ipv6Pointer,
            registration.port,
            0, 0,
            static_cast<DWORD>(keyPointers.size()),
            keyPointers.empty() ? nullptr : keyPointers.data(),
            valuePointers.empty() ? nullptr : valuePointers.data());

        if (registration.instance == nullptr) {
            registration.status = "registration_failed";
            registration.lastError = "DnsServiceConstructInstance returned NULL";
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
        registration.registered = (status == ERROR_SUCCESS);
        if (registration.registered) {
            registration.status = "advertised";
        } else {
            registration.status = "registration_failed";
            char errBuf[32]{};
            std::snprintf(errBuf, sizeof(errBuf), "0x%08X", static_cast<unsigned>(status));
            registration.lastError = errBuf;
        }
    }

    void deregisterAllInstancesLocked() {
        for (auto& registration : registrations_) {
            if (registration.instance == nullptr) {
                continue;
            }
            if (registration.registered) {
                DnsServiceDeRegister(&registration.request, nullptr);
                registration.registered = false;
            }
            DnsServiceFreeInstance(registration.instance);
            registration.instance = nullptr;
        }
        registrations_.clear();
    }

    std::shared_ptr<IConfigurationService> configurationService_;
    std::shared_ptr<ITelemetryService> telemetryService_;
    std::shared_ptr<IMcpGateway> mcpGateway_;
    mutable std::mutex mutex_;
    bool started_ = false;
    std::vector<Registration> registrations_;
};

// PHASE-06 (ADR-002 §7): WorkerSupervisor. Manages `ManagedEndpointPool`
// records and their `EndpointInstance` children. Process trees are
// contained with Windows Job Objects (JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE)
// so the supervisor's destructor reaps the whole tree atomically.
//
// PHASE-06 implements the lifecycle state machine and the
// upsert/remove/ensureMin/drain/shutdown surface. PHASE-07 layers leases
// + autoscale; PHASE-08 wires per-instance telemetry. Empty pools and
// pools whose template is missing must NOT spawn children -- ADR-002 §9
// ("no fake live infrastructure"). The supervised-mock path reports
// supervised=false and statusMessage="Supervised-mock mode".
class WorkerSupervisor final : public IWorkerSupervisor {
public:
    ~WorkerSupervisor() override {
        (void)shutdownAll();
    }

    std::vector<ManagedEndpointPool> listPools() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<ManagedEndpointPool> result;
        result.reserve(pools_.size());
        for (const auto& [_, pool] : pools_) {
            ManagedEndpointPool snapshot = pool;
            refreshInstanceLoadLocked(snapshot);
            result.push_back(std::move(snapshot));
        }
        std::sort(result.begin(), result.end(),
                  [](const ManagedEndpointPool& a, const ManagedEndpointPool& b) {
                      return a.poolId < b.poolId;
                  });
        return result;
    }

    std::optional<ManagedEndpointPool> findPool(const std::string& poolId) const override {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto iterator = pools_.find(poolId);
        if (iterator == pools_.end()) {
            return std::nullopt;
        }
        ManagedEndpointPool snapshot = iterator->second;
        refreshInstanceLoadLocked(snapshot);
        return snapshot;
    }

    OperationResult upsertPool(ManagedEndpointPool pool) override {
        if (pool.poolId.empty()) {
            return OperationResult{ false, false, "Pool id is required." };
        }
        std::lock_guard<std::mutex> lock(mutex_);
        const auto now = timestampNowUtc();
        auto iterator = pools_.find(pool.poolId);
        if (iterator == pools_.end()) {
            pool.createdAtUtc = now;
            pool.updatedAtUtc = now;
            pool.instances = {};
            pools_[pool.poolId] = std::move(pool);
        } else {
            pool.createdAtUtc = iterator->second.createdAtUtc;
            pool.updatedAtUtc = now;
            pool.instances = iterator->second.instances;
            iterator->second = std::move(pool);
        }
        return OperationResult{ true, false, "Pool registered." };
    }

    OperationResult removePool(const std::string& poolId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto iterator = pools_.find(poolId);
        if (iterator == pools_.end()) {
            return OperationResult{ false, false, "Unknown pool id." };
        }
        terminateInstancesLocked(iterator->second);
        pools_.erase(iterator);
        return OperationResult{ true, false, "Pool removed." };
    }

    OperationResult ensureMinInstances(const std::string& poolId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto iterator = pools_.find(poolId);
        if (iterator == pools_.end()) {
            return OperationResult{ false, false, "Unknown pool id." };
        }
        ManagedEndpointPool& pool = iterator->second;
        // Parenthesize std::max to bypass the Windows.h max() macro
        // collision when NOMINMAX isn't reached for this TU.
        const int desired = (std::max)(0, pool.scalePolicy.minInstances);
        const int current = static_cast<int>(pool.instances.size());
        if (current >= desired) {
            return OperationResult{ true, false, "Pool already at or above minInstances." };
        }
        for (int spawned = current; spawned < desired; ++spawned) {
            EndpointInstance instance = startInstanceLocked(pool);
            pool.instances.push_back(std::move(instance));
        }
        pool.updatedAtUtc = timestampNowUtc();
        return OperationResult{ true, false, "Pool scaled to minInstances." };
    }

    OperationResult drainPool(const std::string& poolId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto iterator = pools_.find(poolId);
        if (iterator == pools_.end()) {
            return OperationResult{ false, false, "Unknown pool id." };
        }
        for (auto& instance : iterator->second.instances) {
            transitionInstanceLocked(instance, EndpointInstanceState::Draining,
                                     "Drain requested by supervisor.");
        }
        return OperationResult{ true, false, "Pool draining." };
    }

    std::string scaleUpOnce(const std::string& poolId) override {
        // PHASE-07: invoked by the LeaseRouter when all Ready instances
        // are at maxActiveLeasesPerInstance and the pool has not yet
        // reached scalePolicy.maxInstances. Spawns one new instance and
        // returns its id (empty if the pool is already at max).
        std::lock_guard<std::mutex> lock(mutex_);
        const auto iterator = pools_.find(poolId);
        if (iterator == pools_.end()) {
            return std::string();
        }
        ManagedEndpointPool& pool = iterator->second;
        const int max = (std::max)(0, pool.scalePolicy.maxInstances);
        const int current = static_cast<int>(pool.instances.size());
        if (current >= max) {
            return std::string();
        }
        EndpointInstance instance = startInstanceLocked(pool);
        const std::string newInstanceId = instance.instanceId;
        pool.instances.push_back(std::move(instance));
        pool.updatedAtUtc = timestampNowUtc();
        return newInstanceId;
    }

    OperationResult shutdownAll() override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [_, pool] : pools_) {
            terminateInstancesLocked(pool);
        }
        return OperationResult{ true, false, "All pools shut down." };
    }

private:
    struct ChildProcess final {
#if defined(_WIN32)
        HANDLE jobObject = nullptr;
        PROCESS_INFORMATION processInfo{};
        // Per-instance CPU/RAM telemetry baseline. CPU% is computed from
        // the delta of (kernel + user) FILETIMEs vs delta wallclock since
        // the previous sample, divided by the host's logical CPU count.
        // The first sample after spawn establishes a baseline only and
        // returns 0% CPU; subsequent samples produce a real reading.
        ULARGE_INTEGER lastKernelTime{};
        ULARGE_INTEGER lastUserTime{};
        ULARGE_INTEGER lastSampleTime{};
        bool haveLoadBaseline = false;
#endif
        bool active = false;
    };

    EndpointInstance startInstanceLocked(ManagedEndpointPool& pool) {
        EndpointInstance instance;
        instance.poolId = pool.poolId;
        instance.instanceId = pool.poolId + "#" + std::to_string(nextInstanceSerial_++);
        instance.state = EndpointInstanceState::Starting;
        instance.startedAtUtc = timestampNowUtc();
        instance.lastTransitionAtUtc = instance.startedAtUtc;
        instance.statusMessage = "Instance starting.";

        if (pool.template_.executable.empty()
            || !std::filesystem::exists(std::filesystem::path(pool.template_.executable))) {
            // Supervised-mock path. State machine still advances so callers
            // can exercise the contract; the instance reports supervised=false
            // until a real binary is configured. Honors ADR-002 §9 by NOT
            // claiming live process infrastructure when none exists.
            instance.supervised = false;
            transitionInstanceLocked(instance, EndpointInstanceState::Ready,
                                     "Supervised-mock mode (no binary configured).");
            return instance;
        }

#if defined(_WIN32)
        ChildProcess child;
        child.jobObject = CreateJobObjectW(nullptr, nullptr);
        if (child.jobObject != nullptr) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
            limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            SetInformationJobObject(child.jobObject, JobObjectExtendedLimitInformation,
                                    &limits, sizeof(limits));
        }

        std::wstring commandLine = L"\"" + wideFromUtf8(pool.template_.executable) + L"\"";
        for (const auto& arg : pool.template_.args) {
            commandLine += L" " + wideFromUtf8(arg);
        }
        std::vector<wchar_t> mutableCommandLine(commandLine.begin(), commandLine.end());
        mutableCommandLine.push_back(L'\0');

        STARTUPINFOW startupInfo{};
        startupInfo.cb = sizeof(startupInfo);
        startupInfo.dwFlags = STARTF_USESHOWWINDOW;
        startupInfo.wShowWindow = SW_HIDE;

        PROCESS_INFORMATION processInfo{};
        std::wstring workingDir = wideFromUtf8(pool.template_.workingDirectory);
        const BOOL launched = CreateProcessW(
            nullptr,
            mutableCommandLine.data(),
            nullptr, nullptr,
            FALSE,
            CREATE_NO_WINDOW | CREATE_SUSPENDED,
            nullptr,
            workingDir.empty() ? nullptr : workingDir.c_str(),
            &startupInfo,
            &processInfo);
        if (!launched) {
            if (child.jobObject) {
                CloseHandle(child.jobObject);
            }
            transitionInstanceLocked(instance, EndpointInstanceState::Failed,
                                     "CreateProcessW failed for the configured pool template.");
            return instance;
        }
        if (child.jobObject) {
            AssignProcessToJobObject(child.jobObject, processInfo.hProcess);
        }
        ResumeThread(processInfo.hThread);
        child.processInfo = processInfo;
        child.active = true;

        instance.processId = processInfo.dwProcessId;
        instance.supervised = true;
        children_.emplace(instance.instanceId, std::move(child));
        transitionInstanceLocked(instance, EndpointInstanceState::Ready,
                                 "Instance started under MCOS Job Object supervision.");
#else
        transitionInstanceLocked(instance, EndpointInstanceState::Failed,
                                 "Process supervision requires Windows.");
#endif
        return instance;
    }

    void transitionInstanceLocked(EndpointInstance& instance,
                                   EndpointInstanceState target,
                                   const std::string& message) {
        instance.state = target;
        instance.lastTransitionAtUtc = timestampNowUtc();
        instance.statusMessage = message;
    }

    void terminateInstancesLocked(ManagedEndpointPool& pool) {
        for (auto& instance : pool.instances) {
#if defined(_WIN32)
            const auto childIterator = children_.find(instance.instanceId);
            if (childIterator != children_.end()) {
                ChildProcess& child = childIterator->second;
                if (child.jobObject != nullptr) {
                    CloseHandle(child.jobObject);
                    child.jobObject = nullptr;
                }
                if (child.processInfo.hProcess != nullptr) {
                    CloseHandle(child.processInfo.hProcess);
                }
                if (child.processInfo.hThread != nullptr) {
                    CloseHandle(child.processInfo.hThread);
                }
                children_.erase(childIterator);
            }
#endif
            transitionInstanceLocked(instance, EndpointInstanceState::Stopped,
                                     "Instance stopped by supervisor.");
        }
        pool.instances.clear();
    }

    mutable std::mutex mutex_;
    std::map<std::string, ManagedEndpointPool> pools_;
    // children_ is mutable because refreshInstanceLoadLocked() updates
    // the per-PID FILETIME baseline on every read of pool state. The
    // sampling state is incidental cache, not part of the supervised
    // child contract, so it lives here under the same lock.
    mutable std::map<std::string, ChildProcess> children_;
    std::atomic<uint64_t> nextInstanceSerial_{ 1 };

#if defined(_WIN32)
    // Cached host logical-CPU count (denominator for percent-of-system
    // CPU). Captured once at construction; the runtime is not expected
    // to gain or lose cores at runtime in any deployment we ship.
    mutable DWORD cpuCount_ = 0;

    // Sample one supervised instance's process load and overwrite the
    // EndpointInstance::telemetry fields with real numbers. The first
    // call after spawn establishes the FILETIME baseline and reports
    // 0.0% CPU; subsequent calls yield real percentages computed from
    // the delta. Caller holds mutex_.
    void sampleProcessLoadLocked(EndpointInstance& instance, ChildProcess& child) const {
        if (!child.active || child.processInfo.hProcess == nullptr) {
            return;
        }
        if (cpuCount_ == 0) {
            SYSTEM_INFO si{};
            GetSystemInfo(&si);
            cpuCount_ = (si.dwNumberOfProcessors > 0) ? si.dwNumberOfProcessors : 1;
        }

        FILETIME ftCreate{}, ftExit{}, ftKernel{}, ftUser{};
        if (!GetProcessTimes(child.processInfo.hProcess,
                             &ftCreate, &ftExit, &ftKernel, &ftUser)) {
            return; // process gone or handle invalid; leave telemetry alone
        }
        ULARGE_INTEGER kernelTime{};
        kernelTime.LowPart  = ftKernel.dwLowDateTime;
        kernelTime.HighPart = ftKernel.dwHighDateTime;
        ULARGE_INTEGER userTime{};
        userTime.LowPart  = ftUser.dwLowDateTime;
        userTime.HighPart = ftUser.dwHighDateTime;

        FILETIME ftNow{};
        GetSystemTimeAsFileTime(&ftNow);
        ULARGE_INTEGER nowTime{};
        nowTime.LowPart  = ftNow.dwLowDateTime;
        nowTime.HighPart = ftNow.dwHighDateTime;

        double cpuPercent = 0.0;
        if (child.haveLoadBaseline) {
            const ULONGLONG kernelDelta = kernelTime.QuadPart - child.lastKernelTime.QuadPart;
            const ULONGLONG userDelta   = userTime.QuadPart   - child.lastUserTime.QuadPart;
            const ULONGLONG wallDelta   = nowTime.QuadPart    - child.lastSampleTime.QuadPart;
            if (wallDelta > 0) {
                const double busy   = static_cast<double>(kernelDelta + userDelta);
                const double window = static_cast<double>(wallDelta) * static_cast<double>(cpuCount_);
                cpuPercent = (busy / window) * 100.0;
                if (cpuPercent < 0.0) {
                    cpuPercent = 0.0;
                } else if (cpuPercent > 100.0) {
                    cpuPercent = 100.0; // clamp transient spikes from sampler skew
                }
            }
        }
        child.lastKernelTime = kernelTime;
        child.lastUserTime   = userTime;
        child.lastSampleTime = nowTime;
        child.haveLoadBaseline = true;

        PROCESS_MEMORY_COUNTERS_EX memCounters{};
        memCounters.cb = sizeof(memCounters);
        double memMb = -1.0;
        if (GetProcessMemoryInfo(child.processInfo.hProcess,
                                 reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memCounters),
                                 sizeof(memCounters))) {
            // WorkingSetSize is the resident set size (what shows in Task
            // Manager's "Memory" column). PrivateUsage is more meaningful
            // for capacity planning but matches Task Manager's "Commit"
            // column. We surface working set since that's what operators
            // recognize from the Task Manager column.
            memMb = static_cast<double>(memCounters.WorkingSetSize) / (1024.0 * 1024.0);
        }

        instance.telemetry.cpuPercent     = cpuPercent;
        instance.telemetry.memoryMbytes   = memMb;
        instance.telemetry.lastProbedAtUtc = timestampNowUtc();
    }
#endif

    // Walk a pool snapshot's instances, refreshing telemetry from the
    // live ChildProcess sample state. Skips instances whose ChildProcess
    // is not in the supervisor's children_ map (supervised-mock instances,
    // or instances that have been reaped). The persistent FILETIME
    // baseline lives in children_ (which is mutable), so the next call
    // re-samples and re-writes into the fresh snapshot. Caller holds
    // mutex_.
    void refreshInstanceLoadLocked(ManagedEndpointPool& pool) const {
#if defined(_WIN32)
        for (auto& instance : pool.instances) {
            auto childIt = children_.find(instance.instanceId);
            if (childIt == children_.end()) {
                continue;
            }
            sampleProcessLoadLocked(instance, childIt->second);
        }
#else
        (void)pool;
#endif
    }
};

// PHASE-07 (ADR-002 §8): LeaseRouter. Resolves a LeaseRequest into a
// concrete EndpointLease bound to one Ready instance. The selection
// rule:
//   1. If the request carries a sessionId that maps to an active
//      lease, return that lease verbatim (sticky session). Honors
//      ADR-002 §8: no hot-migration of active stateful streams.
//   2. Otherwise, find the Ready instance with the fewest active
//      leases (excluding Draining instances). If that instance has
//      headroom under maxActiveLeasesPerInstance, bind the lease.
//   3. If all Ready instances are at capacity AND the pool has not
//      yet reached scalePolicy.maxInstances, ask the supervisor to
//      scaleUpOnce(). Bind the new lease to the freshly-spawned
//      instance.
//   4. If saturated and at maxInstances, return a Failed lease with
//      a saturation message; the caller can retry.
class LeaseRouter final : public ILeaseRouter {
public:
    explicit LeaseRouter(std::shared_ptr<IWorkerSupervisor> workerSupervisor)
        : workerSupervisor_(std::move(workerSupervisor)) {}

    EndpointLease acquireLease(const LeaseRequest& request) override {
        if (request.poolId.empty()) {
            EndpointLease lease;
            lease.state = LeaseState::Failed;
            lease.statusMessage = "LeaseRequest is missing poolId.";
            return lease;
        }

        std::lock_guard<std::mutex> lock(mutex_);

        // 1) Sticky session: if the request carries a sessionId that maps
        //    to an active lease, return it verbatim. Active stateful
        //    streams must not be hot-migrated (ADR-002 §8).
        if (!request.sessionId.empty()) {
            const auto sessionIterator = stickySessions_.find(stickyKey(request.poolId, request.sessionId));
            if (sessionIterator != stickySessions_.end()) {
                const auto leaseIterator = leases_.find(sessionIterator->second);
                if (leaseIterator != leases_.end() && leaseIterator->second.state == LeaseState::Active) {
                    return leaseIterator->second;
                }
                // Sticky entry pointed at a stale lease; clean up.
                stickySessions_.erase(sessionIterator);
            }
        }

        const auto poolOpt = workerSupervisor_ ? workerSupervisor_->findPool(request.poolId) : std::optional<ManagedEndpointPool>{};
        if (!poolOpt.has_value()) {
            EndpointLease lease;
            lease.poolId = request.poolId;
            lease.state = LeaseState::Failed;
            lease.statusMessage = "Unknown pool.";
            return lease;
        }
        const ManagedEndpointPool& pool = *poolOpt;
        const int maxLeasesPerInstance = (std::max)(1, pool.scalePolicy.maxActiveLeasesPerInstance);

        // 2) Pick the Ready instance with the fewest active leases.
        const auto choice = selectLeastLoadedReadyLocked(pool, maxLeasesPerInstance);
        if (choice.has_value()) {
            return bindLeaseLocked(pool.poolId, *choice, request);
        }

        // 3) Try same-type scale-out. The supervisor refuses if the pool
        //    is already at scalePolicy.maxInstances.
        const std::string newInstanceId = workerSupervisor_->scaleUpOnce(request.poolId);
        if (!newInstanceId.empty()) {
            return bindLeaseLocked(pool.poolId, newInstanceId, request, /*scaleOutTriggered=*/true);
        }

        // 4) Saturated and at max — surface a Failed lease the caller can
        //    retry once existing leases drain.
        EndpointLease lease;
        lease.poolId = pool.poolId;
        lease.state = LeaseState::Failed;
        lease.statusMessage = "Pool saturated; at scalePolicy.maxInstances. Retry after existing leases release.";
        return lease;
    }

    OperationResult releaseLease(const std::string& leaseId, const std::string& reason) override {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto iterator = leases_.find(leaseId);
        if (iterator == leases_.end()) {
            return OperationResult{ false, false, "Unknown lease id." };
        }
        EndpointLease& lease = iterator->second;
        if (lease.state != LeaseState::Active) {
            return OperationResult{ true, false, "Lease already released." };
        }
        lease.state = LeaseState::Released;
        lease.releasedAtUtc = timestampNowUtc();
        lease.statusMessage = reason.empty() ? std::string("Lease released.") : reason;
        if (!lease.sessionId.empty()) {
            stickySessions_.erase(stickyKey(lease.poolId, lease.sessionId));
        }
        return OperationResult{ true, false, "Lease released." };
    }

    std::vector<EndpointLease> activeLeases(const std::string& poolId) const override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<EndpointLease> result;
        for (const auto& [_, lease] : leases_) {
            if (lease.poolId == poolId && lease.state == LeaseState::Active) {
                result.push_back(lease);
            }
        }
        std::sort(result.begin(), result.end(),
                  [](const EndpointLease& a, const EndpointLease& b) {
                      return a.acquiredAtUtc < b.acquiredAtUtc;
                  });
        return result;
    }

    PoolSaturation saturationFor(const std::string& poolId) const override {
        std::lock_guard<std::mutex> lock(mutex_);
        PoolSaturation saturation;
        saturation.poolId = poolId;
        const auto poolOpt = workerSupervisor_ ? workerSupervisor_->findPool(poolId) : std::optional<ManagedEndpointPool>{};
        if (!poolOpt.has_value()) {
            return saturation;
        }
        const ManagedEndpointPool& pool = *poolOpt;
        saturation.instanceCount = static_cast<int>(pool.instances.size());
        for (const auto& instance : pool.instances) {
            if (instance.state == EndpointInstanceState::Ready
                || instance.state == EndpointInstanceState::Busy) {
                ++saturation.readyInstanceCount;
            }
            if (instance.state == EndpointInstanceState::Draining) {
                ++saturation.drainingInstanceCount;
            }
        }
        for (const auto& [_, lease] : leases_) {
            if (lease.poolId == poolId && lease.state == LeaseState::Active) {
                ++saturation.activeLeaseCount;
            }
        }
        saturation.maxActiveLeasesPerInstance = (std::max)(1, pool.scalePolicy.maxActiveLeasesPerInstance);
        const int capacity = saturation.readyInstanceCount * saturation.maxActiveLeasesPerInstance;
        saturation.atSaturation = (saturation.readyInstanceCount > 0 && saturation.activeLeaseCount >= capacity);
        saturation.atMaxInstances = saturation.instanceCount >= (std::max)(0, pool.scalePolicy.maxInstances);
        return saturation;
    }

private:
    static std::string stickyKey(const std::string& poolId, const std::string& sessionId) {
        return poolId + "::" + sessionId;
    }

    std::optional<std::string> selectLeastLoadedReadyLocked(const ManagedEndpointPool& pool,
                                                             int maxLeasesPerInstance) const {
        std::map<std::string, int> activeByInstance;
        for (const auto& [_, lease] : leases_) {
            if (lease.poolId == pool.poolId && lease.state == LeaseState::Active) {
                activeByInstance[lease.instanceId] += 1;
            }
        }
        std::optional<std::string> bestInstanceId;
        // Parenthesize std::numeric_limits<int>::max to bypass the
        // Windows.h max() macro collision in this TU.
        int bestLoad = (std::numeric_limits<int>::max)();
        for (const auto& instance : pool.instances) {
            if (instance.state != EndpointInstanceState::Ready
                && instance.state != EndpointInstanceState::Busy) {
                continue;
            }
            const int load = activeByInstance[instance.instanceId];
            if (load >= maxLeasesPerInstance) {
                continue;
            }
            if (load < bestLoad) {
                bestLoad = load;
                bestInstanceId = instance.instanceId;
            }
        }
        return bestInstanceId;
    }

    EndpointLease bindLeaseLocked(const std::string& poolId,
                                   const std::string& instanceId,
                                   const LeaseRequest& request,
                                   bool scaleOutTriggered = false) {
        EndpointLease lease;
        lease.leaseId = "lease-" + std::to_string(nextLeaseSerial_++);
        lease.poolId = poolId;
        lease.instanceId = instanceId;
        lease.state = LeaseState::Active;
        lease.acquiredAtUtc = timestampNowUtc();
        if (request.stateful || !request.sessionId.empty()) {
            lease.sessionId = request.sessionId;
            if (!lease.sessionId.empty()) {
                stickySessions_[stickyKey(poolId, lease.sessionId)] = lease.leaseId;
            }
        }
        lease.statusMessage = scaleOutTriggered
            ? "Lease bound to freshly-spawned instance after pool scale-out."
            : "Lease bound to least-loaded Ready instance.";
        leases_[lease.leaseId] = lease;
        return lease;
    }

    mutable std::mutex mutex_;
    std::shared_ptr<IWorkerSupervisor> workerSupervisor_;
    std::map<std::string, EndpointLease> leases_;
    std::map<std::string, std::string> stickySessions_;
    std::atomic<uint64_t> nextLeaseSerial_{ 1 };
};

// PHASE-08 (ADR-002 §9): TelemetryAggregator. Holds the in-memory
// activity event ring (default 1024 entries), connected-client roster
// keyed by clientId, and gateway traffic counters. Honest only:
// per-client CPU/GPU/disk arrives ONLY via ClientHeartbeat. The
// recordEvent path is the unified sink for all categorized warnings
// and errors; PHASE-09's dashboard streams from recentEvents().
class TelemetryAggregator final : public ITelemetryAggregator {
public:
    TelemetryAggregator() = default;

    void recordEvent(TelemetryEvent event) override {
        if (event.timestamp.empty()) {
            event.timestamp = timestampNowUtc();
        }
        std::lock_guard<std::mutex> lock(mutex_);
        events_.push_back(std::move(event));
        if (events_.size() > kMaxEvents_) {
            events_.erase(events_.begin(),
                          events_.begin() + (events_.size() - kMaxEvents_));
        }
    }

    std::vector<TelemetryEvent> recentEvents(std::size_t maxEvents) const override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (maxEvents == 0 || events_.empty()) {
            return {};
        }
        const std::size_t take = (std::min)(maxEvents, events_.size());
        return std::vector<TelemetryEvent>(events_.end() - static_cast<std::ptrdiff_t>(take),
                                            events_.end());
    }

    void recordHeartbeat(ClientHeartbeat heartbeat) override {
        if (heartbeat.clientId.empty()) {
            return;
        }
        if (heartbeat.sentAtUtc.empty()) {
            heartbeat.sentAtUtc = timestampNowUtc();
        }
        std::lock_guard<std::mutex> lock(mutex_);
        auto& presence = clients_[heartbeat.clientId];
        if (presence.firstSeenUtc.empty()) {
            presence.firstSeenUtc = heartbeat.sentAtUtc;
            presence.connectionCount = 1;
        }
        presence.clientId = heartbeat.clientId;
        if (!heartbeat.clientType.empty()) {
            presence.clientType = heartbeat.clientType;
        }
        if (!heartbeat.ipAddress.empty()) {
            presence.ipAddress = heartbeat.ipAddress;
        }
        presence.lastSeenUtc = heartbeat.sentAtUtc;
        presence.heartbeatPresent = true;
        presence.lastHeartbeat = heartbeat;
    }

    std::vector<ClientPresence> clientRoster() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<ClientPresence> roster;
        roster.reserve(clients_.size());
        for (const auto& [_, presence] : clients_) {
            roster.push_back(presence);
        }
        std::sort(roster.begin(), roster.end(),
                  [](const ClientPresence& a, const ClientPresence& b) {
                      return a.clientId < b.clientId;
                  });
        return roster;
    }

    GatewayTrafficSnapshot gatewayTraffic() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return gatewayTraffic_;
    }

    void incrementGatewayRequest(bool errored) override {
        std::lock_guard<std::mutex> lock(mutex_);
        gatewayTraffic_.requestsLastMinute += 1;
        if (errored) {
            gatewayTraffic_.errorsLastMinute += 1;
        }
        gatewayTraffic_.lastEventAtUtc = timestampNowUtc();
    }

    void setGatewayTrafficContext(const std::string& adapterType,
                                   const std::string& mcpUrl,
                                   GatewayHealthStatus healthStatus,
                                   int registeredServerCount) {
        std::lock_guard<std::mutex> lock(mutex_);
        gatewayTraffic_.adapterType = adapterType;
        gatewayTraffic_.mcpUrl = mcpUrl;
        gatewayTraffic_.healthStatus = healthStatus;
        gatewayTraffic_.registeredServerCount = registeredServerCount;
        gatewayTraffic_.activeClientCount = static_cast<int>(clients_.size());
    }

private:
    static constexpr std::size_t kMaxEvents_ = 1024;
    mutable std::mutex mutex_;
    std::vector<TelemetryEvent> events_;
    std::map<std::string, ClientPresence> clients_;
    GatewayTrafficSnapshot gatewayTraffic_;
};

// PHASE-05 (ADR-002 §6): Governance Bundle Service. Composes per-platform
// governance bundles served at /api/governance/bundles/{windows|macos|ios}.
// Hydrates rules from resources/clu/governance-profile.json (the source of
// truth) and forsettiFrameworkVersion from the vendored Forsetti
// instructions JSON. Checksum is SHA-256 over canonical content (excludes
// the checksum and timestamp fields so the digest is stable).
class GovernanceBundleService final : public IGovernanceBundleService {
public:
    GovernanceBundleService(std::filesystem::path cluProfileFile,
                            std::filesystem::path forsettiInstructionsFile)
        : cluProfileFile_(std::move(cluProfileFile))
        , forsettiInstructionsFile_(std::move(forsettiInstructionsFile)) {}

    std::vector<std::string> supportedPlatforms() const override {
        return { "windows", "macos", "ios" };
    }

    GovernanceBundle bundleFor(const std::string& platform) const override {
        const std::string normalized = normalizePlatform(platform);
        const auto cluProfile = loadCluProfile();
        const auto forsetti = loadForsettiInstructions();

        GovernanceBundle bundle;
        bundle.platform = normalized;
        bundle.forsettiFrameworkVersion = jsonStringOrEmpty(forsetti, "schemaVersion");
        if (bundle.forsettiFrameworkVersion.empty()) {
            bundle.forsettiFrameworkVersion = "unknown";
        }
        bundle.agenticCodingFrameworkVersion = "1.0";
        bundle.cluSchemaVersion = "1.0";
        bundle.decisionPolicy =
            "Mutating actions pass through CLU enforceAction. Outcomes: Allow / Block / "
            "RequiresOperatorApproval. RequiresOperatorApproval stages the action in the "
            "operator approval queue with the original payload preserved.";
        bundle.rulesJson = composeRulesJson(cluProfile, normalized);
        bundle.instructionsMarkdown = composeInstructionsMarkdown(cluProfile, forsetti, normalized);
        bundle.checksum = sha256Hex(canonicalSerialization(bundle));
        bundle.generatedAt = timestampNowUtc();
        return bundle;
    }

    GovernanceProfileSummary profileSummary() const override {
        const auto cluProfile = loadCluProfile();
        GovernanceProfileSummary summary;
        summary.unitName = jsonStringOrEmpty(cluProfile, "unitName");
        summary.doctrine = jsonStringOrEmpty(cluProfile, "doctrine");
        summary.cluSchemaVersion = "1.0";
        summary.generatedAt = timestampNowUtc();
        if (cluProfile.contains("documents") && cluProfile["documents"].is_array()) {
            for (const auto& doc : cluProfile["documents"]) {
                if (doc.contains("id") && doc["id"].is_string()) {
                    summary.documentIds.push_back(doc["id"].get<std::string>());
                }
            }
        }
        if (cluProfile.contains("roles") && cluProfile["roles"].is_array()) {
            for (const auto& role : cluProfile["roles"]) {
                if (role.contains("roleId") && role["roleId"].is_string()) {
                    summary.roleIds.push_back(role["roleId"].get<std::string>());
                }
            }
        }
        if (cluProfile.contains("rules") && cluProfile["rules"].is_array()) {
            for (const auto& rule : cluProfile["rules"]) {
                if (rule.contains("ruleId") && rule["ruleId"].is_string()) {
                    summary.ruleIds.push_back(rule["ruleId"].get<std::string>());
                }
            }
        }
        return summary;
    }

private:
    static std::string normalizePlatform(const std::string& platform) {
        std::string lowered;
        lowered.reserve(platform.size());
        for (const auto ch : platform) {
            lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        }
        if (lowered == "windows") return "windows";
        if (lowered == "macos" || lowered == "mac" || lowered == "osx") return "macos";
        if (lowered == "ios" || lowered == "iphoneos") return "ios";
        return "windows";
    }

    static std::string jsonStringOrEmpty(const nlohmann::json& object, const char* key) {
        if (!object.is_object()) {
            return std::string();
        }
        const auto iterator = object.find(key);
        if (iterator == object.end() || !iterator->is_string()) {
            return std::string();
        }
        return iterator->get<std::string>();
    }

    nlohmann::json loadCluProfile() const {
        try {
            if (!std::filesystem::exists(cluProfileFile_)) {
                return nlohmann::json::object();
            }
            std::ifstream stream(cluProfileFile_, std::ios::binary);
            if (!stream) {
                return nlohmann::json::object();
            }
            return nlohmann::json::parse(stream, nullptr, /*allow_exceptions=*/false);
        } catch (...) {
            return nlohmann::json::object();
        }
    }

    nlohmann::json loadForsettiInstructions() const {
        try {
            if (!std::filesystem::exists(forsettiInstructionsFile_)) {
                return nlohmann::json::object();
            }
            std::ifstream stream(forsettiInstructionsFile_, std::ios::binary);
            if (!stream) {
                return nlohmann::json::object();
            }
            return nlohmann::json::parse(stream, nullptr, /*allow_exceptions=*/false);
        } catch (...) {
            return nlohmann::json::object();
        }
    }

    nlohmann::json composeRulesJson(const nlohmann::json& cluProfile, const std::string& platform) const {
        nlohmann::json rules = nlohmann::json::object();
        rules["platform"] = platform;
        rules["doctrine"] = jsonStringOrEmpty(cluProfile, "doctrine");
        rules["unitName"] = jsonStringOrEmpty(cluProfile, "unitName");
        rules["documents"] = cluProfile.value("documents", nlohmann::json::array());
        rules["roles"] = cluProfile.value("roles", nlohmann::json::array());
        rules["rules"] = cluProfile.value("rules", nlohmann::json::array());
        rules["actionKinds"] = cluProfile.value("actionKinds", nlohmann::json::array());
        return rules;
    }

    std::string composeInstructionsMarkdown(const nlohmann::json& cluProfile,
                                             const nlohmann::json& forsetti,
                                             const std::string& platform) const {
        std::ostringstream md;
        md << "# CLU Governance Bundle - " << platform << "\n\n";
        md << "## Doctrine\n\n";
        md << jsonStringOrEmpty(cluProfile, "doctrine") << "\n\n";
        md << "## Forsetti framework\n\n";
        md << "- Project: " << jsonStringOrEmpty(forsetti, "projectName") << "\n";
        md << "- Schema version: " << jsonStringOrEmpty(forsetti, "schemaVersion") << "\n";
        md << "- Owner: " << jsonStringOrEmpty(forsetti, "owner") << "\n\n";
        md << "## Forsetti Framework for Agentic Coding\n\n";
        md << "- Contract before action.\n";
        md << "- Scope is binding.\n";
        md << "- Truthfulness is mandatory.\n";
        md << "- Governance overrides convenience.\n";
        md << "- No meaningful autonomous action without declared scope.\n\n";
        md << "## Client guidance\n\n";
        md << "Before any mutating call (creating/modifying/removing MCP servers, sub-agents, "
              "governance policy, or modules), the client MUST consult this bundle and respect "
              "CLU's enforceAction outcome. On the LAN MCP gateway surface MCOS does not "
              "collect provider credentials. Only the operator surface authenticates actors via "
              "X-MCOS-Client-Id.\n";
        return md.str();
    }

    // Canonical content for checksum. Excludes the checksum and generatedAt
    // fields so the digest is stable across regenerations.
    std::string canonicalSerialization(const GovernanceBundle& bundle) const {
        nlohmann::json canonical;
        canonical["platform"] = bundle.platform;
        canonical["forsettiFrameworkVersion"] = bundle.forsettiFrameworkVersion;
        canonical["agenticCodingFrameworkVersion"] = bundle.agenticCodingFrameworkVersion;
        canonical["cluSchemaVersion"] = bundle.cluSchemaVersion;
        canonical["instructionsMarkdown"] = bundle.instructionsMarkdown;
        canonical["rulesJson"] = bundle.rulesJson;
        canonical["decisionPolicy"] = bundle.decisionPolicy;
        return canonical.dump();
    }

    static std::string sha256Hex(const std::string& input) {
#if defined(_WIN32)
        BCRYPT_ALG_HANDLE algorithm = nullptr;
        if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) {
            return std::string();
        }
        DWORD hashSize = 0;
        ULONG resultSize = 0;
        BCryptGetProperty(algorithm,
                          BCRYPT_HASH_LENGTH,
                          reinterpret_cast<PUCHAR>(&hashSize),
                          sizeof(hashSize),
                          &resultSize, 0);
        std::vector<UCHAR> hashBuffer(hashSize, 0);
        BCRYPT_HASH_HANDLE hash = nullptr;
        if (BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) != 0) {
            BCryptCloseAlgorithmProvider(algorithm, 0);
            return std::string();
        }
        BCryptHashData(hash,
                       reinterpret_cast<PUCHAR>(const_cast<char*>(input.data())),
                       static_cast<ULONG>(input.size()),
                       0);
        BCryptFinishHash(hash, hashBuffer.data(), hashSize, 0);
        BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(algorithm, 0);

        std::ostringstream stream;
        stream << std::hex << std::setfill('0');
        for (const auto byte : hashBuffer) {
            stream << std::setw(2) << static_cast<int>(byte);
        }
        return "sha256:" + stream.str();
#else
        (void)input;
        return std::string();
#endif
    }

    std::filesystem::path cluProfileFile_;
    std::filesystem::path forsettiInstructionsFile_;
};

// PHASE-04 (ADR-002 §5): Onboarding Profile Service. Composes a per-client
// profile from the discovery document (gateway URL + governance bundle URL)
// and known per-clientType configuration shape. The generic profile is the
// fallback for any unrecognized clientType. ChatGPT is documented as a
// connector-edge case with caveats explaining LAN reachability constraints.
class OnboardingProfileService final : public IOnboardingProfileService {
public:
    OnboardingProfileService(std::shared_ptr<IConfigurationService> configurationService,
                             std::shared_ptr<IDiscoveryService> discoveryService)
        : configurationService_(std::move(configurationService))
        , discoveryService_(std::move(discoveryService)) {}

    std::vector<std::string> knownClientTypes() const override {
        return { "claude-code", "codex", "grok", "chatgpt", "generic" };
    }

    OnboardingProfile profileFor(const std::string& clientType) const override {
        const std::string normalized = normalizeClientType(clientType);
        const auto document = discoveryService_
            ? discoveryService_->currentDocument()
            : DiscoveryDocument{};
        const auto configuration = configurationService_->current();
        std::string lanIp = document.serverIpAddress;
        if (lanIp.empty() || lanIp == "0.0.0.0") {
            lanIp = "127.0.0.1";
        }
        const std::string adminBase = "http://" + lanIp + ":" + std::to_string(configuration.browserPort);

        OnboardingProfile profile;
        profile.clientType = normalized;
        profile.gatewayMcpUrl = document.gateway.mcpUrl;
        profile.transport = "streamable_http";
        profile.authRequired = false;       // schema const; ADR-002 §1 invariant
        profile.trust = "lan";
        profile.governanceBundleUrl = adminBase + "/api/governance/bundles/windows";
        profile.discoveryUrl = adminBase + "/.well-known/mcos.json";
        profile.instanceId = document.instanceId;

        if (normalized == "claude-code") {
            populateClaudeCode(profile);
        } else if (normalized == "codex") {
            populateCodex(profile);
        } else if (normalized == "grok") {
            populateGrok(profile);
        } else if (normalized == "chatgpt") {
            populateChatGpt(profile);
        } else {
            populateGeneric(profile);
        }
        return profile;
    }

private:
    static std::string normalizeClientType(const std::string& clientType) {
        std::string lowered;
        lowered.reserve(clientType.size());
        for (const auto ch : clientType) {
            lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        }
        if (lowered == "claude-code" || lowered == "claude_code" || lowered == "claudecode") {
            return "claude-code";
        }
        if (lowered == "codex") return "codex";
        if (lowered == "grok" || lowered == "xai" || lowered == "xai-grok") return "grok";
        if (lowered == "chatgpt" || lowered == "openai") return "chatgpt";
        return "generic";
    }

    static OnboardingConfigSnippet jsonSnippet(std::string format,
                                               std::string description,
                                               std::string filename,
                                               nlohmann::json content) {
        OnboardingConfigSnippet snippet;
        snippet.format = std::move(format);
        snippet.description = std::move(description);
        snippet.filename = std::move(filename);
        snippet.content = std::move(content);
        return snippet;
    }

    void populateClaudeCode(OnboardingProfile& profile) const {
        profile.displayName = "Claude Code (Anthropic)";
        profile.configSnippets.push_back(jsonSnippet(
            "json",
            "Add MCOS as a Streamable HTTP MCP server in Claude Code's user settings. Drop this fragment into the mcpServers map of your config.",
            ".mcp.json",
            { { "mcpServers", { { "mcos", {
                { "url", profile.gatewayMcpUrl },
                { "transport", "streamable_http" }
            } } } } }));
        profile.manualInstructions = {
            "Open Claude Code settings and add the MCOS MCP server using the JSON fragment above.",
            "No bearer token or app-layer login is required on the trusted LAN gateway.",
            "Load the CLU/Forsetti governance bundle from the URL above before granting Claude Code mutating access."
        };
        profile.verificationSteps = {
            "Restart Claude Code so it picks up the new MCP server entry.",
            "Run `mcp__mcos__list_tools` (or the equivalent slash command) to confirm tool aggregation.",
            "Confirm GET /.well-known/mcos.json reports gateway.state=running.",
        };
        profile.caveats = {
            "Claude Code consumes Streamable HTTP MCP servers natively; no companion utility is required."
        };
    }

    void populateCodex(OnboardingProfile& profile) const {
        profile.displayName = "Codex CLI (OpenAI)";
        profile.configSnippets.push_back(jsonSnippet(
            "json",
            "Codex 0.4+ accepts MCP servers in its config. Add this fragment to the codex config under mcpServers.",
            "codex.config.json",
            { { "mcpServers", { { "mcos", {
                { "url", profile.gatewayMcpUrl },
                { "transport", "streamable_http" }
            } } } } }));
        profile.manualInstructions = {
            "Add MCOS to Codex's MCP config and restart the CLI.",
            "Authenticate Codex with OpenAI as you normally would; no MCOS-side credentials are needed.",
            "MCOS does not collect or proxy your OpenAI credentials. The gateway carries tool calls only."
        };
        profile.verificationSteps = {
            "Run a Codex session and ask it to list available MCP tools.",
            "Confirm tool calls land at the MCOS gateway URL above.",
            "Verify governance bundle retrieval before granting mutating tool access."
        };
        profile.caveats = {
            "Codex builds older than 0.4 may require the companion utility to write the config file."
        };
    }

    void populateGrok(OnboardingProfile& profile) const {
        profile.displayName = "Grok (xAI)";
        profile.configSnippets.push_back(jsonSnippet(
            "json",
            "Grok consumes MCP servers via its agent SDK. Provide the gateway URL as a Streamable HTTP MCP backend.",
            "grok.mcp.json",
            { { "mcpServers", { { "mcos", {
                { "url", profile.gatewayMcpUrl },
                { "transport", "streamable_http" }
            } } } } }));
        profile.manualInstructions = {
            "Configure your Grok agent to point at the MCOS gateway URL.",
            "MCOS does not collect or proxy your xAI API key.",
            "Apply the LAN-trust mode: no bearer token is sent on the gateway connection."
        };
        profile.verificationSteps = {
            "Confirm Grok's tool listing returns the MCOS-aggregated set.",
            "Verify governance bundle retrieval.",
            "Run a low-impact tool call to confirm round-trip."
        };
        profile.caveats = {
            "Grok agent runtimes vary; if your runtime cannot consume Streamable HTTP, use the companion utility's stdio bridge."
        };
    }

    void populateChatGpt(OnboardingProfile& profile) const {
        profile.displayName = "ChatGPT (Connector-Edge)";
        // ChatGPT does not consume LAN-only MCP servers directly; the
        // recommended path is the connector-edge pattern where a
        // user-side companion exposes the LAN gateway through the
        // ChatGPT Connectors surface. The profile still documents the
        // MCOS gateway URL for completeness.
        profile.configSnippets.push_back(jsonSnippet(
            "json",
            "ChatGPT does not natively consume LAN MCP servers. The companion utility (PHASE-04 deferred) is the recommended bridge — see caveats below.",
            "chatgpt-connector-edge-notes.json",
            { { "mcosGatewayUrl", profile.gatewayMcpUrl },
              { "discoveryUrl", profile.discoveryUrl },
              { "connectorEdgeRecommended", true } }));
        profile.manualInstructions = {
            "ChatGPT runs in OpenAI's hosted environment and cannot reach a LAN-only MCP gateway directly.",
            "Use the companion utility (or a connector-edge proxy) on a host that has both ChatGPT connectivity and LAN access to MCOS.",
            "MCOS does not collect or proxy ChatGPT credentials. The gateway carries tool calls only when reached via a connector-edge proxy."
        };
        profile.verificationSteps = {
            "Confirm the connector-edge proxy is running and reachable from ChatGPT.",
            "Confirm the proxy can resolve MCOS via /.well-known/mcos.json.",
            "Run a low-impact tool call from ChatGPT and verify it lands at the MCOS gateway."
        };
        profile.caveats = {
            "ChatGPT's MCP support is connector-edge / optional. LAN-only deployments without a connector-edge proxy cannot reach MCOS from ChatGPT.",
            "When the connector-edge proxy is configured, MCOS still treats the inbound traffic as LAN-trusted; do not expose the gateway port to the public internet."
        };
    }

    void populateGeneric(OnboardingProfile& profile) const {
        profile.clientType = "generic";
        profile.displayName = "Generic MCP Client";
        profile.configSnippets.push_back(jsonSnippet(
            "json",
            "Generic Streamable HTTP MCP server registration. Drop this into your client's MCP server config.",
            ".mcp.json",
            { { "mcpServers", { { "mcos", {
                { "url", profile.gatewayMcpUrl }
            } } } } }));
        profile.manualInstructions = {
            "Add the MCOS gateway MCP URL to your MCP client.",
            "No bearer token or app-layer login is required on the trusted LAN surface.",
            "Load the CLU/Forsetti governance bundle for your platform."
        };
        profile.verificationSteps = {
            "Resolve MCOS discovery document.",
            "Connect to gateway MCP URL.",
            "List tools through the gateway.",
            "Confirm governance bundle retrieval."
        };
        profile.caveats = {
            "Unknown client types fall through to this profile; replace it with a typed profile if your client has a documented MCP setup."
        };
    }

    std::shared_ptr<IConfigurationService> configurationService_;
    std::shared_ptr<IDiscoveryService> discoveryService_;
};

class BeaconService final : public IBeaconService {
public:
    BeaconService(std::shared_ptr<IConfigurationService> configurationService,
                  std::shared_ptr<ITelemetryService> telemetryService,
                  std::shared_ptr<IPlatformServiceCatalogService> platformServiceCatalogService,
                  std::shared_ptr<DiscoveryService> discoveryService)
        : configurationService_(std::move(configurationService))
        , telemetryService_(std::move(telemetryService))
        , platformServiceCatalogService_(std::move(platformServiceCatalogService))
        , discoveryService_(std::move(discoveryService)) {}

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
    std::shared_ptr<DiscoveryService> discoveryService_;
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

            // PHASE-03: UDP beacon now broadcasts the gateway-first
            // DiscoveryDocument JSON instead of the legacy provider-era
            // BeaconAdvertisement. /api/beacon still returns the legacy
            // shape for browsers that haven't migrated; /api/discovery
            // and /.well-known/mcos.json are the primary surfaces.
            const auto payload = discoveryService_
                ? nlohmann::json(discoveryService_->currentDocument()).dump()
                : nlohmann::json(currentAdvertisement()).dump();
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
                    std::shared_ptr<IMcpServerCatalogService> mcpServerCatalogService,
                    std::shared_ptr<ISubAgentCatalogService> subAgentCatalogService,
                    std::shared_ptr<ISubAgentGroupService> subAgentGroupService,
                    std::shared_ptr<IInstallerOrchestrator> installerOrchestrator,
                    std::shared_ptr<IBootstrapRepoService> bootstrapRepoService,
                    std::shared_ptr<IZipBundleService> zipBundleService,
                    std::shared_ptr<IExportService> exportService,
                    std::shared_ptr<ICommandLogicUnitService> commandLogicUnitService,
                    std::shared_ptr<IForsettiSurfaceService> surfaceService,
                    std::shared_ptr<IMcpGateway> mcpGateway,
                    std::shared_ptr<IDiscoveryService> discoveryService)
        : telemetryService_(std::move(telemetryService))
        , inventoryService_(std::move(inventoryService))
        , configurationService_(std::move(configurationService))
        , platformServiceCatalogService_(std::move(platformServiceCatalogService))
        , appleRemoteHostService_(std::move(appleRemoteHostService))
        , mcpServerCatalogService_(std::move(mcpServerCatalogService))
        , subAgentCatalogService_(std::move(subAgentCatalogService))
        , subAgentGroupService_(std::move(subAgentGroupService))
        , installerOrchestrator_(std::move(installerOrchestrator))
        , bootstrapRepoService_(std::move(bootstrapRepoService))
        , zipBundleService_(std::move(zipBundleService))
        , exportService_(std::move(exportService))
        , commandLogicUnitService_(std::move(commandLogicUnitService))
        , surfaceService_(std::move(surfaceService))
        , mcpGateway_(std::move(mcpGateway))
        , discoveryService_(std::move(discoveryService)) {}

    DashboardSnapshot snapshot() override {
        // Use synchronous refresh so the snapshot always reflects the latest
        // configuration state — callers expect a consistent view.
        inventoryService_->refresh();

        DashboardSnapshot snapshot;
        snapshot.telemetry = telemetryService_->captureSnapshot();
        snapshot.endpoints = inventoryService_->listEndpoints();
        snapshot.subAgentGroups = subAgentGroupService_->listGroups();
        snapshot.installHistory = installerOrchestrator_->history();
        snapshot.exports = exportService_->generateExports();
        const auto configuration = configurationService_->current();
        // Operator override for advertised host IP. WindowsHostTelemetryService
        // auto-picks an interface via GetAdaptersAddresses(AF_UNSPEC) and on
        // dual-stack hosts often surfaces the IPv6 ULA before the IPv4 LAN
        // address. Same precedence as DiscoveryService::currentDocument
        // (v0.6.4): operator-set preferredBindAddress wins, otherwise the
        // auto-detected primaryIpAddress is preserved. v0.6.6 carries this
        // override through to dashboard telemetry so the WinUI shell + the
        // browser dashboard's host-network panel see the same chosen IP
        // that LAN clients see.
        const std::string& preferredAdvertiseIp = configuration.activeProfile.preferredBindAddress;
        if (!preferredAdvertiseIp.empty() && preferredAdvertiseIp != "0.0.0.0") {
            snapshot.telemetry.primaryIpAddress = preferredAdvertiseIp;
        }
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
        if (mcpGateway_) {
            snapshot.mcpGatewayStatus = mcpGateway_->CurrentStatus();
            snapshot.mcpGatewayHealth = mcpGateway_->Probe();
            snapshot.mcpGatewayTools = mcpGateway_->ListTools();
        }
        if (discoveryService_) {
            snapshot.discovery = discoveryService_->currentDocument();
        }
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
        MasterControl::Diagnostics::appendTelemetry(
            L"runtime",
            "dashboard-snapshot",
            nlohmann::json{
                { "hostName", snapshot.telemetry.hostName },
                { "primaryIpAddress", snapshot.telemetry.primaryIpAddress },
                { "cpuPercent", snapshot.telemetry.cpuPercent },
                { "endpoints", snapshot.endpoints.size() }
            });
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

    OperationResult installPackageJson(const std::string& requestBody) override {
        const auto spec = nlohmann::json::parse(requestBody).get<InstallerPackageSpec>();
        if (commandLogicUnitService_) {
            const auto decision = commandLogicUnitService_->enforceAction(GovernanceEnforcementRequest{
                GovernanceActionKind::RemoteInstall,
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
    std::shared_ptr<IMcpServerCatalogService> mcpServerCatalogService_;
    std::shared_ptr<ISubAgentCatalogService> subAgentCatalogService_;
    std::shared_ptr<ISubAgentGroupService> subAgentGroupService_;
    std::shared_ptr<IInstallerOrchestrator> installerOrchestrator_;
    std::shared_ptr<IBootstrapRepoService> bootstrapRepoService_;
    std::shared_ptr<IZipBundleService> zipBundleService_;
    std::shared_ptr<IExportService> exportService_;
    std::shared_ptr<ICommandLogicUnitService> commandLogicUnitService_;
    std::shared_ptr<IForsettiSurfaceService> surfaceService_;
    std::shared_ptr<IMcpGateway> mcpGateway_;
    std::shared_ptr<IDiscoveryService> discoveryService_;
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
// request, governance decision,
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

void appendLanClientActivity(const std::string& kind,
                             const std::string& target,
                             const std::string& message) {
    ActivityEvent event;
    event.kind = kind;
    event.actor = "lan-client-access";
    event.target = target;
    event.message = message;
    globalActivityRing().append(event);
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

// ----------------------------------------------------------------------
// Claude Code plugin (mcos-control) registration toggle.
//
// The plugin source ships at <install-root>\share\claude-plugins\mcos-control.
// Registering means dropping a directory junction at
// <activeUserProfile>\.claude\plugins\mcos-control that points back at the
// install source. Junction creation does NOT require admin privilege (unlike
// symbolic links), and RemoveDirectoryW on a reparse point removes the link
// only — never the target.
//
// Active-user resolution: when the runtime is hosted as a Windows service
// (the default), GetEnvironmentVariableW("USERPROFILE") returns SYSTEM's
// profile, which is the wrong target. WTSGetActiveConsoleSessionId +
// WTSQueryUserToken + CreateEnvironmentBlock recover the interactive user's
// USERPROFILE and USERNAME. In --console mode this still works (the active
// console session is the user who launched the runtime).
// ----------------------------------------------------------------------
struct ClaudePluginState {
    bool registered = false;
    bool activeUserResolved = false;
    std::string profileDir;
    std::string userName;
    std::string source;
    std::string target;
    std::string lastError;
};

// Read a wide environment variable from the current process and return it
// as UTF-8. Empty string if unset.
inline std::string readProcessEnvUtf8(const wchar_t* name) {
    const DWORD required = GetEnvironmentVariableW(name, nullptr, 0);
    if (required == 0) {
        return {};
    }
    std::wstring buffer(static_cast<size_t>(required), L'\0');
    const DWORD written = GetEnvironmentVariableW(name, buffer.data(),
                                                  static_cast<DWORD>(buffer.size()));
    if (written == 0) {
        return {};
    }
    buffer.resize(written);
    return utf8FromWide(buffer);
}

// Returns true when `path` looks like the LocalSystem profile (i.e. the
// runtime is running as SYSTEM, typically because it's hosted as the
// Windows service). Used to decide whether to short-circuit on the
// process's own USERPROFILE or recover the interactive user's via
// WTSQueryUserToken.
inline bool looksLikeSystemProfile(const std::string& path) {
    if (path.empty()) {
        return true;
    }
    // Compare case-insensitively against the standard SYSTEM path tail.
    static const std::string tail = "\\system32\\config\\systemprofile";
    if (path.size() < tail.size()) {
        return false;
    }
    const auto suffix = path.substr(path.size() - tail.size());
    return _stricmp(suffix.c_str(), tail.c_str()) == 0;
}

inline std::string resolveActiveUserProfile(std::string& userName, std::string& errorOut) {
    // Path 1: process is already running as the interactive user (this
    // happens whenever MCOS is launched as `MasterControlServiceHost.exe
    // --console`, or from the WinUI shell, or any non-service host).
    // GetEnvironmentVariableW("USERPROFILE") returns the user's profile
    // directly without any privileged Win32 calls.
    {
        const auto profile = readProcessEnvUtf8(L"USERPROFILE");
        if (!profile.empty() && !looksLikeSystemProfile(profile)) {
            const auto envUserName = readProcessEnvUtf8(L"USERNAME");
            if (!envUserName.empty()) {
                userName = envUserName;
            }
            return profile;
        }
    }

    // Helper: given a primary user token, expand its environment block and
    // pull USERPROFILE / USERNAME. Returns the profile path (UTF-8) and
    // takes ownership of closing the token.
    const auto extractFromToken = [&](HANDLE userToken) -> std::string {
        LPVOID env = nullptr;
        if (!CreateEnvironmentBlock(&env, userToken, FALSE)) {
            CloseHandle(userToken);
            return {};
        }
        std::string profile;
        for (wchar_t* p = static_cast<wchar_t*>(env); *p; p += wcslen(p) + 1) {
            const std::wstring_view entry(p);
            if (entry.size() >= 12
                && entry.compare(0, 12, L"USERPROFILE=") == 0) {
                profile = utf8FromWide(std::wstring(entry.substr(12)));
            } else if (entry.size() >= 9
                && entry.compare(0, 9, L"USERNAME=") == 0) {
                userName = utf8FromWide(std::wstring(entry.substr(9)));
            }
        }
        DestroyEnvironmentBlock(env);
        CloseHandle(userToken);
        return profile;
    };

    // Path 2: SYSTEM-hosted runtime, target the active console session.
    // WTSGetActiveConsoleSessionId + WTSQueryUserToken is the canonical
    // path; it works when an interactive user is actually signed in and
    // unlocked at the console. Returns ERROR_NO_TOKEN (1008) when the
    // console session is locked or not currently associated with a user
    // (RDP, fast-user-switch away, etc.).
    DWORD wtsLastError = 0;
    {
        const DWORD sessionId = WTSGetActiveConsoleSessionId();
        if (sessionId != 0xFFFFFFFFu) {
            HANDLE userToken = nullptr;
            if (WTSQueryUserToken(sessionId, &userToken)) {
                const auto profile = extractFromToken(userToken);
                if (!profile.empty()) {
                    return profile;
                }
            } else {
                wtsLastError = GetLastError();
            }
        }
    }

    // Path 3: SYSTEM-hosted runtime, but the console session didn't yield a
    // token. Enumerate all sessions and pick any State==Active session that
    // carries a user name. This recovers RDP sessions, locked-but-recently-
    // active console sessions, and Server Core hosts where the console
    // session may not be the user's primary session.
    PWTS_SESSION_INFO_1W sessions = nullptr;
    DWORD sessionCount = 0;
    DWORD level = 1;
    if (WTSEnumerateSessionsExW(WTS_CURRENT_SERVER_HANDLE, &level, 0, &sessions, &sessionCount)) {
        for (DWORD i = 0; i < sessionCount; ++i) {
            if (sessions[i].State != WTSActive) {
                continue;
            }
            if (sessions[i].pUserName == nullptr || *sessions[i].pUserName == L'\0') {
                continue;
            }
            HANDLE userToken = nullptr;
            if (!WTSQueryUserToken(sessions[i].SessionId, &userToken)) {
                continue;
            }
            const auto profile = extractFromToken(userToken);
            if (!profile.empty()) {
                WTSFreeMemoryExW(WTSTypeSessionInfoLevel1, sessions, sessionCount);
                return profile;
            }
        }
        WTSFreeMemoryExW(WTSTypeSessionInfoLevel1, sessions, sessionCount);
    }

    // Path 4: last resort — enumerate C:\Users\* and pick the directory
    // that already has a .claude subfolder (i.e., the user has interacted
    // with Claude Code at least once on this host). When more than one
    // qualifies we pick the most-recently-modified profile. This is best-
    // effort: if no user has used Claude Code yet, fall through to error.
    {
        std::error_code ec;
        const std::filesystem::path usersRoot(L"C:\\Users");
        std::filesystem::path bestProfile;
        std::filesystem::file_time_type bestMtime{};
        for (const auto& entry : std::filesystem::directory_iterator(usersRoot, ec)) {
            if (ec) {
                break;
            }
            if (!entry.is_directory(ec) || ec) {
                continue;
            }
            const auto claudeDir = entry.path() / ".claude";
            if (!std::filesystem::exists(claudeDir, ec) || ec) {
                continue;
            }
            const auto mtime = std::filesystem::last_write_time(entry.path(), ec);
            if (ec) {
                continue;
            }
            if (bestProfile.empty() || mtime > bestMtime) {
                bestProfile = entry.path();
                bestMtime = mtime;
            }
        }
        if (!bestProfile.empty()) {
            const auto profileStr = bestProfile.string();
            userName = bestProfile.filename().string();
            return profileStr;
        }
    }

    // No path succeeded. Surface the WTS error if we have one, otherwise
    // a generic message.
    if (wtsLastError != 0) {
        errorOut = "Could not resolve an interactive user. The console "
            "session returned WTSQueryUserToken errno "
            + std::to_string(wtsLastError)
            + " (typically locked screen / no console logon), no other "
            "active session yielded a token, and no C:\\Users\\* profile "
            "carries a .claude directory yet. Sign in to Windows on this "
            "host or run Claude Code at least once, then try again.";
    } else {
        errorOut = "No active interactive user could be resolved on this host.";
    }
    return {};
}

inline ClaudePluginState resolveClaudePluginState(const std::filesystem::path& executableDirectory) {
    ClaudePluginState s;
    const auto sourcePath = executableDirectory / "share" / "claude-plugins" / "mcos-control";
    s.source = sourcePath.string();
    s.profileDir = resolveActiveUserProfile(s.userName, s.lastError);
    s.activeUserResolved = !s.profileDir.empty();
    if (s.activeUserResolved) {
        s.target = s.profileDir + "\\.claude\\plugins\\mcos-control";
        const DWORD attrs = GetFileAttributesW(wideFromUtf8(s.target).c_str());
        s.registered = (attrs != INVALID_FILE_ATTRIBUTES)
            && ((attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0);
    }
    return s;
}

inline bool createClaudePluginJunction(const std::string& target,
                                       const std::string& source,
                                       std::string& errorOut) {
    // Verify source exists and is a directory.
    const DWORD srcAttrs = GetFileAttributesW(wideFromUtf8(source).c_str());
    if (srcAttrs == INVALID_FILE_ATTRIBUTES || !(srcAttrs & FILE_ATTRIBUTE_DIRECTORY)) {
        errorOut = "Plugin source not found at " + source
            + ". The MSI may be incomplete or the install was tampered with.";
        return false;
    }
    // Ensure parent (.claude\plugins) tree exists.
    const std::filesystem::path parent = std::filesystem::path(target).parent_path();
    std::error_code ec;
    std::filesystem::create_directories(parent, ec);
    if (ec) {
        errorOut = "create_directories(" + parent.string() + "): " + ec.message();
        return false;
    }
    // Spawn `cmd /c mklink /J "<target>" "<source>"` synchronously, hidden window.
    // mklink /J creates a directory junction — no privilege required, unlike
    // mklink /D (symlinks need SeCreateSymbolicLinkPrivilege).
    std::wstring command = L"cmd.exe /c mklink /J \""
        + wideFromUtf8(target) + L"\" \"" + wideFromUtf8(source) + L"\"";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        errorOut = "CreateProcessW for mklink failed (Win32 errno "
            + std::to_string(GetLastError()) + ").";
        return false;
    }
    WaitForSingleObject(pi.hProcess, 10000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (code != 0) {
        errorOut = "mklink /J exited with code " + std::to_string(code)
            + ". Verify that " + target + " does not already exist as a regular directory.";
        return false;
    }
    return true;
}

inline bool removeClaudePluginJunction(const std::string& target, std::string& errorOut) {
    if (RemoveDirectoryW(wideFromUtf8(target).c_str())) {
        return true;
    }
    const DWORD err = GetLastError();
    if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) {
        return true; // already gone
    }
    errorOut = "RemoveDirectoryW failed (Win32 errno " + std::to_string(err) + ").";
    return false;
}

inline nlohmann::json claudePluginStatusJson(const ClaudePluginState& s,
                                             bool ok = true,
                                             const std::string& explicitError = {}) {
    return nlohmann::json{
        {"ok", ok},
        {"registered", s.registered},
        {"activeUserResolved", s.activeUserResolved},
        {"userName", s.userName},
        {"profileDir", s.profileDir},
        {"source", s.source},
        {"target", s.target},
        {"lastError", explicitError.empty() ? s.lastError : explicitError}
    };
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
    OperationResult upsertAppleRemoteHostJson(const std::string& requestBody) { return adminApiService_->upsertAppleRemoteHostJson(requestBody); }
    OperationResult removeAppleRemoteHostJson(const std::string& requestBody) { return adminApiService_->removeAppleRemoteHostJson(requestBody); }
    OperationResult upsertMcpServerJson(const std::string& requestBody) { return adminApiService_->upsertMcpServerJson(requestBody); }
    OperationResult removeMcpServerJson(const std::string& requestBody) { return adminApiService_->removeMcpServerJson(requestBody); }
    OperationResult upsertSubAgentJson(const std::string& requestBody) { return adminApiService_->upsertSubAgentJson(requestBody); }
    OperationResult removeSubAgentJson(const std::string& requestBody) { return adminApiService_->removeSubAgentJson(requestBody); }
    OperationResult upsertSubAgentGroupJson(const std::string& requestBody) { return adminApiService_->upsertSubAgentGroupJson(requestBody); }
    OperationResult removeSubAgentGroupJson(const std::string& requestBody) { return adminApiService_->removeSubAgentGroupJson(requestBody); }
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
    std::shared_ptr<IMcpServerCatalogService> mcpServerCatalogService_;
    std::shared_ptr<ISubAgentCatalogService> subAgentCatalogService_;
    std::shared_ptr<ISubAgentGroupService> subAgentGroupService_;
    std::shared_ptr<ILanClientAccessService> lanClientAccessService_;
    std::shared_ptr<IGovernanceApprovalQueueService> governanceApprovalQueueService_;
    std::shared_ptr<IExportService> exportService_;
    std::shared_ptr<IPlatformGovernanceToolService> platformGovernanceToolService_;
    std::shared_ptr<ICommandLogicUnitService> commandLogicUnitService_;
    std::shared_ptr<IModuleControlSurfaceService> controlSurfaceService_;
    std::shared_ptr<IForsettiSurfaceService> surfaceService_;
    std::shared_ptr<IBeaconService> beaconService_;
    std::shared_ptr<IMcpGateway> mcpGateway_;
    std::shared_ptr<IDiscoveryService> discoveryService_;
    std::shared_ptr<IOnboardingProfileService> onboardingProfileService_;
    std::shared_ptr<IGovernanceBundleService> governanceBundleService_;
    std::shared_ptr<IWorkerSupervisor> workerSupervisor_;
    std::shared_ptr<ILeaseRouter> leaseRouter_;
    std::shared_ptr<ITelemetryAggregator> telemetryAggregator_;
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
    mcpServerCatalogService_ = std::make_shared<McpServerCatalogService>(state_, paths_.configurationFile, inventoryService_);
    subAgentCatalogService_ = std::make_shared<SubAgentCatalogService>(state_, paths_.configurationFile, inventoryService_);
    subAgentGroupService_ = std::make_shared<SubAgentGroupService>(state_, paths_.configurationFile, inventoryService_);
    lanClientAccessService_ = std::make_shared<LanClientAccessService>(state_, paths_.configurationFile);
    governanceApprovalQueueService_ = std::make_shared<GovernanceApprovalQueueService>();
    exportService_ = std::make_shared<ExportService>(inventoryService_, configurationService_, lanClientAccessService_);
    platformServiceCatalogService_ = std::make_shared<PlatformServiceCatalogService>(configurationService_, telemetryService_);
    platformGovernanceToolService_ = std::make_shared<PlatformGovernanceToolService>(
        paths_,
        appleRemoteHostService_,
        configurationService_);
    commandLogicUnitService_ = std::make_shared<CommandLogicUnitService>(
        paths_.cluProfileFile,
        configurationService_,
        installerOrchestrator_,
        exportService_,
        appleRemoteHostService_,
        platformServiceCatalogService_,
        platformGovernanceToolService_);
    controlSurfaceService_ = std::make_shared<ModuleControlSurfaceService>();
    registerConfigurationDefaults();
    createForsettiRuntime();

    // PHASE-02 (ADR-002 §2): construct the MCP Gateway adapter from current
    // configuration. Default config disables the gateway; operators flip
    // mcpGateway.enabled=true once an MCPJungle binary is installed.
    mcpGateway_ = std::make_shared<McpJungleGatewayAdapter>(
        configurationService_->current().mcpGateway);

    // PHASE-03 (ADR-002 §4): construct the LAN Discovery Service that owns
    // DNS-SD registration and the canonical DiscoveryDocument shape.
    auto discoveryService = std::make_shared<DiscoveryService>(
        configurationService_, telemetryService_, mcpGateway_);
    discoveryService_ = discoveryService;
    beaconService_ = std::make_shared<BeaconService>(
        configurationService_, telemetryService_, platformServiceCatalogService_, discoveryService);

    // PHASE-04 (ADR-002 §5): construct the per-client onboarding profile
    // service. Profiles compose against the live discovery document so any
    // gateway URL change (PHASE-11 native gateway swap) automatically
    // propagates to client onboarding without separate plumbing.
    onboardingProfileService_ = std::make_shared<OnboardingProfileService>(
        configurationService_, discoveryService);

    // PHASE-05 (ADR-002 §6): construct the per-platform governance bundle
    // service. Reads CLU profile and vendored Forsetti instructions on each
    // request to keep the bundle in sync with operator edits to
    // resources/clu/governance-profile.json. SHA-256 checksum is stable
    // across requests for unchanged content.
    governanceBundleService_ = std::make_shared<GovernanceBundleService>(
        paths_.cluProfileFile, paths_.forsettiInstructionsFile);

    // PHASE-06 (ADR-002 §7): construct the worker supervisor. PHASE-06
    // ships in-memory pool registration + Job Object child supervision;
    // PHASE-07 layers leases + autoscale on top. Empty supervisor at
    // boot — operators register pools via POST /api/pools or future
    // module manifests.
    workerSupervisor_ = std::make_shared<WorkerSupervisor>();

    // PHASE-07 (ADR-002 §8): construct the lease router that resolves
    // LeaseRequest -> EndpointLease using sticky-session + least-loaded
    // routing, with same-type scale-out when all Ready instances are at
    // maxActiveLeasesPerInstance and the pool has not reached its
    // scalePolicy.maxInstances ceiling.
    leaseRouter_ = std::make_shared<LeaseRouter>(workerSupervisor_);

    // PHASE-08 (ADR-002 §9): construct the telemetry aggregator that
    // owns the activity event ring, the connected-client roster, and
    // gateway traffic counters. Honest only — no fake utilization, no
    // fabricated client metrics. Per-AI-client CPU/GPU/disk arrives via
    // POST /api/telemetry/heartbeat or sidecar.
    telemetryAggregator_ = std::make_shared<TelemetryAggregator>();
    {
        TelemetryEvent boot;
        boot.category = TelemetryCategory::System;
        boot.severity = TelemetrySeverity::Info;
        boot.message = "MCOS runtime constructing telemetry aggregator. PHASE-08 baseline event.";
        telemetryAggregator_->recordEvent(std::move(boot));
    }

    // PHASE-02: register one stable logical MCP endpoint with the gateway
    // adapter so subsequent phases (PHASE-06 worker pools, PHASE-07 lease
    // routing) have a known registration shape to extend. This is an
    // in-memory registration only; the adapter reconciles with the real
    // backend on Start() once a binary is configured.
    {
        McpServerRegistration logicalPool;
        logicalPool.name = "mcos-default-pool";
        logicalPool.description = "MCOS default logical MCP pool. Backend instances are managed by the worker supervisor (PHASE-06).";
        logicalPool.transport = McpServerTransport::StreamableHttp;
        logicalPool.url = "http://127.0.0.1:" + std::to_string(configurationService_->current().browserPort) + "/mcp/pools/default/mcp";
        logicalPool.sessionMode = "stateful";
        logicalPool.headers["X-MCOS-Gateway-Source"] = "mcpjungle";
        (void)mcpGateway_->RegisterHttpServer(logicalPool);
    }

    adminApiService_ = std::make_shared<AdminApiService>(
        telemetryService_,
        inventoryService_,
        configurationService_,
        platformServiceCatalogService_,
        appleRemoteHostService_,
        mcpServerCatalogService_,
        subAgentCatalogService_,
        subAgentGroupService_,
        installerOrchestrator_,
        installerOrchestrator_,
        installerOrchestrator_,
        exportService_,
        commandLogicUnitService_,
        surfaceService_,
        mcpGateway_,
        discoveryService_);

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

    if (discoveryService_) {
        discoveryService_->start();
    }
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
    if (discoveryService_) {
        discoveryService_->stop();
    }
    if (workerSupervisor_) {
        (void)workerSupervisor_->shutdownAll();
    }
    leaseRouter_.reset();
    if (mcpGateway_) {
        mcpGateway_->Stop();
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
    services->registerService<IMcpServerCatalogService>(mcpServerCatalogService_);
    services->registerService<ISubAgentCatalogService>(subAgentCatalogService_);
    services->registerService<ISubAgentGroupService>(subAgentGroupService_);
    services->registerService<ILanClientAccessService>(lanClientAccessService_);
    services->registerService<IGovernanceApprovalQueueService>(governanceApprovalQueueService_);
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
    static const std::array<const char*, 16> moduleIds = {
        "com.mastercontrol.environment-discovery",
        "com.mastercontrol.host-telemetry",
        "com.mastercontrol.runtime-inventory",
        "com.mastercontrol.configuration",
        "com.mastercontrol.installer-import",
        "com.mastercontrol.export",
        "com.mastercontrol.lan-client-access",
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

// Phase 6: case-insensitive header lookup. HTTP headers are
// case-insensitive per RFC 7230 but the parser stores them as written.
// Used to read X-MCOS-Client-Id regardless of the casing the client sent.
static std::string findHeaderCaseInsensitive(const std::unordered_map<std::string, std::string>& headers,
                                              const std::string& name) {
    for (const auto& [key, value] : headers) {
        if (key.size() != name.size()) {
            continue;
        }
        bool match = true;
        for (size_t i = 0; i < key.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(key[i])) !=
                std::tolower(static_cast<unsigned char>(name[i]))) {
                match = false;
                break;
            }
        }
        if (match) {
            return value;
        }
    }
    return {};
}

HttpResponse MasterControlApplication::Impl::handleHttpRequest(const HttpRequest& request) {
    // Phase 6 - resolve the per-request authentication context. Identity is
    // by header alone (the LAN is trusted per ADR-001); a missing or unknown
    // header yields the operator-fallback context so the dashboard and
    // ad-hoc curl keep working. A disabled client's id is rejected up front.
    AuthenticatedRequestContext context = makeOperatorContext();
    const auto headerClientId = findHeaderCaseInsensitive(request.headers, "X-MCOS-Client-Id");
    if (!headerClientId.empty() && lanClientAccessService_) {
        const auto resolved = lanClientAccessService_->getClient(headerClientId);
        if (resolved.has_value()) {
            if (!resolved->enabled) {
                return HttpResponse{
                    403,
                    "application/json",
                    nlohmann::json{
                        { "succeeded", false },
                        { "errorMessage", "LAN client is disabled: " + resolved->clientId }
                    }.dump()
                };
            }
            context.client = resolved;
            context.privileges = resolved->privileges;
            context.autonomousMode = resolved->autonomousMode;
            context.actor = resolved->clientId;
            context.isOperatorFallback = false;
            // Hot-path liveness update; touchClient deliberately skips disk
            // write to avoid thrash on every request. Phase 9 may add a
            // periodic flush if last-seen survival becomes a hard requirement.
            lanClientAccessService_->touchClient(resolved->clientId, std::string{});
        }
    }

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

    // Phase 6 - privilege gate helper. Returns nullopt when the predicate
    // is satisfied, or a 403 HttpResponse otherwise. Captures the resolved
    // context by reference so each gate is one line at the call site.
    auto requirePrivilege = [&](bool granted, const char* privilegeName) -> std::optional<HttpResponse> {
        if (granted) {
            return std::nullopt;
        }
        nlohmann::json body = {
            { "succeeded", false },
            { "errorMessage", std::string("Required privilege missing: ") + privilegeName + "." },
            { "actor", context.actor },
            { "privilege", privilegeName }
        };
        return HttpResponse{ 403, "application/json", body.dump() };
    };

    // Phase 7 - CLU enforcement gate. Runs after the privilege check.
    // Returns nullopt when the action is allowed, otherwise a final HTTP
    // response: 403 for Block, 202 for RequiresOperatorApproval (the
    // mutation is staged in the approval queue and the deferred id is
    // handed back so the caller can poll). Allow falls through to the
    // route's normal apply path.
    auto enforceGovernance = [&](GovernanceActionKind action,
                                 const std::string& targetId,
                                 const std::string& actionSource = std::string{})
            -> std::optional<HttpResponse> {
        if (!commandLogicUnitService_) {
            return std::nullopt;
        }
        GovernanceEnforcementRequest enforcementRequest;
        enforcementRequest.action = action;
        enforcementRequest.targetId = targetId;
        enforcementRequest.actor = context.actor;
        enforcementRequest.source = actionSource;
        const auto decision = commandLogicUnitService_->enforceAction(enforcementRequest);
        if (decision.outcome == GovernanceDecisionOutcome::Allow) {
            return std::nullopt;
        }
        if (decision.outcome == GovernanceDecisionOutcome::RequiresOperatorApproval
            && governanceApprovalQueueService_) {
            GovernanceDeferredAction deferred;
            deferred.action = action;
            deferred.actor = context.actor;
            deferred.targetId = targetId;
            deferred.payload = request.body;
            deferred.reason = decision.message;
            const auto staged = governanceApprovalQueueService_->stage(deferred);
            nlohmann::json body = {
                { "succeeded", true },
                { "outcome", to_string(decision.outcome) },
                { "deferredActionId", staged.id },
                { "ruleId", decision.ruleId },
                { "message", decision.message },
                { "actor", context.actor }
            };
            return HttpResponse{ 202, "application/json", body.dump() };
        }
        nlohmann::json body = {
            { "succeeded", false },
            { "outcome", to_string(decision.outcome) },
            { "errorMessage", decision.message },
            { "ruleId", decision.ruleId },
            { "blockingFindings", decision.blockingFindings },
            { "posture", decision.posture },
            { "actor", context.actor }
        };
        return HttpResponse{ 403, "application/json", body.dump() };
    };

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
                        { "clientType", "codex" },
                        { "credentialFields", nlohmann::json::array({ "OPENAI_API_KEY" }) },
                        { "recommendedModel", "gpt-5.4" },
                        { "mcp", {
                            { "name", gateway->serviceId },
                            { "transport", "http" },
                            { "url", gatewayUrl }
                        } }
                    },
                    {
                        { "clientType", "claude-code" },
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
                        { "clientType", "xai" },
                        { "credentialFields", nlohmann::json::array({ "XAI_API_KEY" }) },
                        { "recommendedModel", "grok-code-fast-1" },
                        { "mcp", {
                            { "gateway", gatewayUrl },
                            { "clientType", "xai" },
                            { "recommendedModel", "grok-code-fast-1" }
                        } }
                    }
                }) }
            };
        };
        if (request.method == "GET" && request.path == "/api/health") {
            return jsonResponse(nlohmann::json{ { "status", "ok" }, { "time", timestampNowUtc() } });
        }
        // -------------------------------------------------------------------
        // Claude Code plugin (mcos-control) registration toggle.
        //   GET  /api/claude-plugin/status — current state for the active user
        //   POST /api/claude-plugin/toggle — flip register/unregister
        // The actual file ops are a directory junction at
        // <USERPROFILE>\.claude\plugins\mcos-control pointing at the install
        // directory's bundled plugin source.
        // -------------------------------------------------------------------
        if (request.method == "GET" && request.path == "/api/claude-plugin/status") {
            const auto state = resolveClaudePluginState(paths_.executableDirectory);
            return jsonResponse(claudePluginStatusJson(state));
        }
        if (request.method == "POST" && request.path == "/api/claude-plugin/toggle") {
            const auto before = resolveClaudePluginState(paths_.executableDirectory);
            if (!before.activeUserResolved) {
                return jsonResponse(claudePluginStatusJson(before, false));
            }
            std::string err;
            const bool ok = before.registered
                ? removeClaudePluginJunction(before.target, err)
                : createClaudePluginJunction(before.target, before.source, err);
            const auto after = resolveClaudePluginState(paths_.executableDirectory);
            return jsonResponse(claudePluginStatusJson(after, ok, err));
        }
        if (request.method == "GET" && request.path == "/api/dashboard") {
            return jsonResponse(snapshot());
        }
        if (request.method == "GET" && request.path == "/api/config") {
            return jsonResponse(configurationService_->current());
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
        // -------------------------------------------------------------------
        // PHASE-03 (ADR-002 §4): LAN discovery surface.
        // /.well-known/mcos.json: strict schema-conformant discovery doc;
        //   beacon-only fields (generatedAtUtc, serverIpAddress, instanceName)
        //   stripped per docs/implementation/MCP-GATEWAY-DISCOVERY-CONTRACT.md.
        // /api/discovery: full document including beacon metadata for
        //   diagnostics and dashboard consumption.
        if (request.method == "GET" && request.path == "/.well-known/mcos.json") {
            if (!discoveryService_) {
                return jsonResponse(nlohmann::json::object());
            }
            nlohmann::json document = discoveryService_->currentDocument();
            document.erase("generatedAtUtc");
            document.erase("serverIpAddress");
            document.erase("instanceName");
            return jsonResponse(document);
        }
        if (request.method == "GET" && request.path == "/api/discovery") {
            return jsonResponse(discoveryService_ ? discoveryService_->currentDocument() : DiscoveryDocument{});
        }
        // -------------------------------------------------------------------
        // PHASE-04 (ADR-002 §5): per-client onboarding profile surface.
        // /api/onboarding -> list known client types (generic + four
        // recognized typed clients).
        // /api/onboarding/{clientType} -> typed profile, falling through
        // to "generic" for unrecognized client types.
        if (request.method == "GET" && request.path == "/api/onboarding") {
            nlohmann::json response = nlohmann::json::object();
            response["clientTypes"] = onboardingProfileService_
                ? onboardingProfileService_->knownClientTypes()
                : std::vector<std::string>{};
            return jsonResponse(response);
        }
        if (request.method == "GET" && startsWith(request.path, "/api/onboarding/")) {
            const auto prefix = std::string("/api/onboarding/");
            const std::string clientType = request.path.substr(prefix.size());
            return jsonResponse(onboardingProfileService_
                ? onboardingProfileService_->profileFor(clientType)
                : OnboardingProfile{});
        }
        // -------------------------------------------------------------------
        // PHASE-05 (ADR-002 §6): governance bundle distribution surface.
        // Per-platform bundle: /api/governance/bundles/{windows|macos|ios}
        // Profile summary:    /api/governance/profile
        // Decisions endpoint: /api/governance/decisions (POST evaluates a
        // GovernanceEnforcementRequest through CLU; GET returns a help
        // document so smoke probes do not 404).
        if (request.method == "GET" && request.path == "/api/governance/profile") {
            return jsonResponse(governanceBundleService_
                ? governanceBundleService_->profileSummary()
                : GovernanceProfileSummary{});
        }
        if (request.method == "GET" && startsWith(request.path, "/api/governance/bundles/")) {
            const auto prefix = std::string("/api/governance/bundles/");
            const std::string platform = request.path.substr(prefix.size());
            return jsonResponse(governanceBundleService_
                ? governanceBundleService_->bundleFor(platform)
                : GovernanceBundle{});
        }
        if (request.method == "GET" && request.path == "/api/governance/bundles") {
            nlohmann::json response;
            response["platforms"] = governanceBundleService_
                ? governanceBundleService_->supportedPlatforms()
                : std::vector<std::string>{};
            return jsonResponse(response);
        }
        if (request.method == "GET" && request.path == "/api/governance/decisions") {
            // Documentation-only GET. The POST path that actually
            // evaluates governance decisions is wired via the existing
            // CLU enforcement surface in PHASE-07 — for PHASE-05 the
            // GET advertises the contract so smoke probes do not 404.
            nlohmann::json response;
            response["method"] = "POST";
            response["expects"] = "GovernanceEnforcementRequest";
            response["returns"] = "GovernanceEnforcementDecision";
            response["note"] = "POST handler lands in PHASE-07 alongside the lease router.";
            return jsonResponse(response);
        }
        // -------------------------------------------------------------------
        // PHASE-06 (ADR-002 §7): managed worker pool surface.
        // GET /api/pools                  -> list pools
        // GET /api/pools/{poolId}         -> typed pool record
        // POST /api/pools                 -> upsert a pool (JSON body)
        // POST /api/pools/{poolId}/remove -> deregister + reap children
        // POST /api/pools/{poolId}/scale  -> ensureMinInstances
        // POST /api/pools/{poolId}/drain  -> mark all instances Draining
        if (request.method == "GET" && request.path == "/api/pools") {
            return jsonResponse(workerSupervisor_
                ? workerSupervisor_->listPools()
                : std::vector<ManagedEndpointPool>{});
        }
        if (request.method == "GET" && startsWith(request.path, "/api/pools/")) {
            const auto prefix = std::string("/api/pools/");
            const std::string suffix = request.path.substr(prefix.size());
            const auto slash = suffix.find('/');
            const std::string poolId = (slash == std::string::npos) ? suffix : suffix.substr(0, slash);
            const std::string subResource = (slash == std::string::npos) ? std::string{} : suffix.substr(slash + 1);

            // PHASE-07 sub-resources: /leases (active list) and /saturation.
            if (subResource == "leases") {
                return jsonResponse(leaseRouter_
                    ? leaseRouter_->activeLeases(poolId)
                    : std::vector<EndpointLease>{});
            }
            if (subResource == "saturation") {
                return jsonResponse(leaseRouter_
                    ? leaseRouter_->saturationFor(poolId)
                    : PoolSaturation{});
            }
            if (!subResource.empty()) {
                return jsonResponse(OperationResult{ false, false, "Unknown pool sub-resource; expected leases|saturation." });
            }
            const auto pool = workerSupervisor_
                ? workerSupervisor_->findPool(poolId)
                : std::optional<ManagedEndpointPool>{};
            if (!pool.has_value()) {
                return jsonResponse(OperationResult{ false, false, "Unknown pool id." });
            }
            return jsonResponse(*pool);
        }
        if (request.method == "POST" && request.path == "/api/pools") {
            try {
                auto pool = nlohmann::json::parse(request.body).get<ManagedEndpointPool>();
                return jsonResponse(workerSupervisor_
                    ? workerSupervisor_->upsertPool(std::move(pool))
                    : OperationResult{ false, false, "Worker supervisor is not running." });
            } catch (const std::exception& ex) {
                return jsonResponse(OperationResult{ false, false, std::string("Invalid pool JSON: ") + ex.what() });
            }
        }
        if (request.method == "POST" && startsWith(request.path, "/api/pools/")) {
            const auto prefix = std::string("/api/pools/");
            const std::string suffix = request.path.substr(prefix.size());
            const auto slash = suffix.find('/');
            if (slash == std::string::npos) {
                return jsonResponse(OperationResult{ false, false, "Pool action route is /api/pools/{poolId}/{remove|scale|drain|leases}." });
            }
            const std::string poolId = suffix.substr(0, slash);
            const std::string action = suffix.substr(slash + 1);
            if (!workerSupervisor_) {
                return jsonResponse(OperationResult{ false, false, "Worker supervisor is not running." });
            }
            if (action == "remove") {
                return jsonResponse(workerSupervisor_->removePool(poolId));
            }
            if (action == "scale") {
                return jsonResponse(workerSupervisor_->ensureMinInstances(poolId));
            }
            if (action == "drain") {
                return jsonResponse(workerSupervisor_->drainPool(poolId));
            }
            // PHASE-07 lease acquire: POST /api/pools/{poolId}/leases
            if (action == "leases") {
                if (!leaseRouter_) {
                    return jsonResponse(OperationResult{ false, false, "Lease router is not running." });
                }
                LeaseRequest leaseRequest;
                leaseRequest.poolId = poolId;
                if (!request.body.empty()) {
                    try {
                        leaseRequest = nlohmann::json::parse(request.body).get<LeaseRequest>();
                        leaseRequest.poolId = poolId; // path takes precedence
                    } catch (...) {
                        // Empty / malformed body falls back to a stateless lease.
                    }
                }
                return jsonResponse(leaseRouter_->acquireLease(leaseRequest));
            }
            return jsonResponse(OperationResult{ false, false, "Unknown pool action; expected remove|scale|drain|leases." });
        }
        // -------------------------------------------------------------------
        // PHASE-08 (ADR-002 §9): real-time telemetry surface.
        // GET  /api/telemetry/events?max=N  -> recent activity events.
        // GET  /api/telemetry/clients       -> connected-client roster.
        // GET  /api/telemetry/gateway       -> gateway traffic snapshot.
        // POST /api/telemetry/heartbeat     -> AI-client heartbeat ingest.
        if (request.method == "GET" && request.path == "/api/telemetry/events") {
            std::size_t maxEvents = 100;
            // Honor ?max=N if present in the path-style query string.
            return jsonResponse(telemetryAggregator_
                ? telemetryAggregator_->recentEvents(maxEvents)
                : std::vector<TelemetryEvent>{});
        }
        if (request.method == "GET" && request.path == "/api/telemetry/clients") {
            return jsonResponse(telemetryAggregator_
                ? telemetryAggregator_->clientRoster()
                : std::vector<ClientPresence>{});
        }
        if (request.method == "GET" && request.path == "/api/telemetry/gateway") {
            if (telemetryAggregator_ && mcpGateway_) {
                const auto health = mcpGateway_->Probe();
                if (auto* concrete = dynamic_cast<TelemetryAggregator*>(telemetryAggregator_.get())) {
                    concrete->setGatewayTrafficContext(
                        health.adapterType,
                        health.mcpUrl,
                        health.status,
                        health.registeredServerCount);
                }
            }
            return jsonResponse(telemetryAggregator_
                ? telemetryAggregator_->gatewayTraffic()
                : GatewayTrafficSnapshot{});
        }
        if (request.method == "POST" && request.path == "/api/telemetry/heartbeat") {
            if (!telemetryAggregator_) {
                return jsonResponse(OperationResult{ false, false, "Telemetry aggregator is not running." });
            }
            try {
                auto heartbeat = nlohmann::json::parse(request.body).get<ClientHeartbeat>();
                if (heartbeat.clientId.empty()) {
                    return jsonResponse(OperationResult{ false, false, "ClientHeartbeat.clientId is required." });
                }
                telemetryAggregator_->recordHeartbeat(heartbeat);
                TelemetryEvent event;
                event.category = TelemetryCategory::Client;
                event.severity = TelemetrySeverity::Info;
                event.message = "Client heartbeat recorded.";
                event.clientId = heartbeat.clientId;
                telemetryAggregator_->recordEvent(std::move(event));
                return jsonResponse(OperationResult{ true, false, "Heartbeat recorded." });
            } catch (const std::exception& ex) {
                return jsonResponse(OperationResult{ false, false, std::string("Invalid heartbeat JSON: ") + ex.what() });
            }
        }
        // PHASE-07 lease release: POST /api/leases/{leaseId}/release
        if (request.method == "POST" && startsWith(request.path, "/api/leases/")) {
            const auto prefix = std::string("/api/leases/");
            const std::string suffix = request.path.substr(prefix.size());
            const auto slash = suffix.find('/');
            if (slash == std::string::npos || suffix.substr(slash + 1) != "release") {
                return jsonResponse(OperationResult{ false, false, "Lease route is POST /api/leases/{leaseId}/release." });
            }
            const std::string leaseId = suffix.substr(0, slash);
            std::string reason;
            if (!request.body.empty()) {
                try {
                    const auto body = nlohmann::json::parse(request.body);
                    reason = body.value("reason", std::string{});
                } catch (...) {
                    // Reason is optional.
                }
            }
            return jsonResponse(leaseRouter_
                ? leaseRouter_->releaseLease(leaseId, reason)
                : OperationResult{ false, false, "Lease router is not running." });
        }
        // -------------------------------------------------------------------
        // /api/gateway/* — MCP Gateway adapter surface (PHASE-02 of ADR-002).
        // PHASE-04 onboarding profiles point clients at the gateway URL
        // exposed here, not at the admin port.
        if (request.method == "GET" && request.path == "/api/gateway/status") {
            return jsonResponse(mcpGateway_ ? mcpGateway_->CurrentStatus() : GatewayStatus{});
        }
        if (request.method == "GET" && request.path == "/api/gateway/health") {
            return jsonResponse(mcpGateway_ ? mcpGateway_->Probe() : GatewayHealth{});
        }
        if (request.method == "GET" && request.path == "/api/gateway/tools") {
            return jsonResponse(mcpGateway_ ? mcpGateway_->ListTools() : std::vector<McpToolDescriptor>{});
        }
        if (request.method == "POST" && request.path == "/api/gateway/start") {
            return jsonResponse(mcpGateway_ ? mcpGateway_->Start() : GatewayStatus{});
        }
        if (request.method == "POST" && request.path == "/api/gateway/stop") {
            return jsonResponse(mcpGateway_ ? mcpGateway_->Stop() : GatewayStatus{});
        }
        if (request.method == "GET" && request.path == "/api/environment-hints") {
            // Reserved endpoint - returns an empty object after the provider
            // stack was removed per ADR-001. Phase 3+ may repurpose this for
            // LAN client environment discovery.
            return jsonResponse(nlohmann::json::object());
        }
        // -------------------------------------------------------------------
        // /api/client/* - shared-fabric read surface (Phase 6 of ADR-001)
        // -------------------------------------------------------------------
        // Open to any identified client (and to the operator-fallback
        // context). Use is never gated; any LAN client that knows MCOS is
        // there can list the catalog and send a heartbeat.
        if (request.method == "GET" && request.path == "/api/client/mcp-servers") {
            const auto endpoints = inventoryService_->listEndpoints();
            std::vector<RuntimeEndpoint> mcpEndpoints;
            for (const auto& endpoint : endpoints) {
                if (endpoint.kind == EndpointKind::MCPServer && !endpoint.isTemplate) {
                    mcpEndpoints.push_back(endpoint);
                }
            }
            return jsonResponse(mcpEndpoints);
        }
        if (request.method == "GET" && request.path == "/api/client/sub-agents") {
            const auto endpoints = inventoryService_->listEndpoints();
            std::vector<RuntimeEndpoint> subAgentEndpoints;
            for (const auto& endpoint : endpoints) {
                if (endpoint.kind == EndpointKind::SubAgent && !endpoint.isTemplate) {
                    subAgentEndpoints.push_back(endpoint);
                }
            }
            return jsonResponse(subAgentEndpoints);
        }
        if (request.method == "GET" && request.path == "/api/client/activity") {
            // Re-uses the global activity ring; clients receive the same
            // FIFO event stream as the operator dashboard. Phase 7 may
            // filter to events scoped to the requester.
            const auto snap = globalActivityRing().read("");
            nlohmann::json body;
            body["highWaterMarkId"] = snap.highWaterMarkId;
            body["events"] = nlohmann::json::array();
            for (const auto& e : snap.events) {
                body["events"].push_back(e);
            }
            return HttpResponse{ 200, "application/json", body.dump() };
        }
        if (request.method == "GET" && request.path == "/api/client/governance/profile") {
            // Mirrors /api/clu but trimmed to the read-only surface clients
            // need to render the rules they're operating under. Phase 7 will
            // expand the body if Forsetti adds client-targeted rules.
            const auto snapshot = commandLogicUnitService_->currentGovernance();
            nlohmann::json body = {
                { "authority", snapshot.unitName.empty() ? std::string("Command Logic Unit") : snapshot.unitName },
                { "framework", "Forsetti Framework for Agentic Coding" },
                { "doctrine", snapshot.doctrine },
                { "posture", snapshot.posture },
                { "documents", snapshot.documents },
                { "rules", snapshot.rules },
                { "lastEvaluatedUtc", snapshot.lastEvaluatedUtc }
            };
            return jsonResponse(body);
        }
        if (request.method == "POST" && request.path == "/api/client/governance/decisions") {
            // Phase 6 stub. Phase 7 expands GovernanceActionKind and wires
            // this endpoint to commandLogicUnitService_->enforceAction so
            // a client can pre-check whether a proposed mutation will be
            // allowed, blocked, or queued for operator approval. For now,
            // honestly report that the endpoint exists but defers to the
            // privilege gates already in place on the mutation routes.
            nlohmann::json body = {
                { "outcome", "deferred" },
                { "actor", context.actor },
                { "message", "CLU pre-check is stubbed in Phase 6. Mutation routes apply privilege gates today; richer governance decisions land in Phase 7." }
            };
            return HttpResponse{ 202, "application/json", body.dump() };
        }
        if (request.method == "POST" && request.path == "/api/client/heartbeat") {
            // Marks the calling client as live. The operator-fallback
            // context has no clientId so heartbeats from anonymous callers
            // are accepted but no-ops on the roster.
            if (context.client.has_value() && lanClientAccessService_) {
                lanClientAccessService_->touchClient(context.client->clientId, std::string{});
            }
            nlohmann::json body = {
                { "succeeded", true },
                { "clientId", context.actor },
                { "isOperatorFallback", context.isOperatorFallback }
            };
            return jsonResponse(body);
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
        // WS4 — Host-side dependency installer (e.g., Claude Code CLI)
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
            // Starter workflow instantiation was provider-driven. After the
            // provider stack was removed per ADR-001 the template catalog is
            // empty. A replacement LAN-client workflow experience lands in a
            // later remediation phase.
            StarterWorkflowInstantiateResult response;
            response.succeeded = false;
            response.message = "Starter workflow templates are temporarily unavailable "
                               "while the LAN client control plane rebuild is in progress.";
            return jsonResponse(response, 503);
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
        if (request.method == "POST" && request.path == "/api/platform-services/apple-hosts") {
            const auto result = adminApiService_->upsertAppleRemoteHostJson(request.body);
            return jsonResponse(result, result.succeeded ? 200 : 400);
        }
        if (request.method == "POST" && request.path == "/api/platform-services/apple-hosts/remove") {
            const auto result = adminApiService_->removeAppleRemoteHostJson(request.body);
            return jsonResponse(result, result.succeeded ? 200 : 400);
        }
        if (request.method == "POST" && request.path == "/api/runtime/mcp-servers") {
            // Distinguish create-vs-modify so the right privilege applies.
            // Autonomous-mode clients bypass canCreateMcpServers per ADR-001.
            std::string requestedId;
            try {
                if (!request.body.empty()) {
                    requestedId = nlohmann::json::parse(request.body).value("id", std::string{});
                }
            } catch (...) {
                return jsonResponse(OperationResult{ false, false, "Invalid JSON body." }, 400);
            }
            const auto endpoints = inventoryService_->listEndpoints();
            const bool exists = !requestedId.empty() && std::any_of(
                endpoints.begin(),
                endpoints.end(),
                [&requestedId](const RuntimeEndpoint& e) {
                    return e.id == requestedId && e.kind == EndpointKind::MCPServer;
                });
            if (exists) {
                if (auto deny = requirePrivilege(context.privileges.canModifyMcpServers,
                                                 "canModifyMcpServers")) {
                    return *deny;
                }
                if (auto governance = enforceGovernance(GovernanceActionKind::McpServerModify, requestedId)) {
                    return *governance;
                }
            } else {
                const bool granted = context.privileges.canCreateMcpServers || context.autonomousMode;
                if (auto deny = requirePrivilege(granted, "canCreateMcpServers")) {
                    return *deny;
                }
                // Autonomous-mode clients bypass CLU approval for create
                // per ADR-001's locked autonomous-mode semantics. CLU still
                // runs to honor posture-blocked enforcement.
                if (!context.autonomousMode) {
                    if (auto governance = enforceGovernance(GovernanceActionKind::McpServerCreate, requestedId)) {
                        return *governance;
                    }
                }
            }
            const auto result = adminApiService_->upsertMcpServerJson(request.body);
            return jsonResponse(result, result.succeeded ? 200 : 400);
        }
        if (request.method == "POST" && request.path == "/api/runtime/mcp-servers/remove") {
            if (auto deny = requirePrivilege(context.privileges.canRemoveMcpServers,
                                             "canRemoveMcpServers")) {
                return *deny;
            }
            std::string removalTarget;
            try {
                if (!request.body.empty()) {
                    removalTarget = nlohmann::json::parse(request.body).value("mcpServerId", std::string{});
                }
            } catch (...) {}
            if (auto governance = enforceGovernance(GovernanceActionKind::McpServerRemove, removalTarget)) {
                return *governance;
            }
            const auto result = adminApiService_->removeMcpServerJson(request.body);
            return jsonResponse(result, result.succeeded ? 200 : 400);
        }
        if (request.method == "POST" && request.path == "/api/runtime/subagents") {
            std::string requestedId;
            try {
                if (!request.body.empty()) {
                    requestedId = nlohmann::json::parse(request.body).value("id", std::string{});
                }
            } catch (...) {
                return jsonResponse(OperationResult{ false, false, "Invalid JSON body." }, 400);
            }
            const auto endpoints = inventoryService_->listEndpoints();
            const bool exists = !requestedId.empty() && std::any_of(
                endpoints.begin(),
                endpoints.end(),
                [&requestedId](const RuntimeEndpoint& e) {
                    return e.id == requestedId && e.kind == EndpointKind::SubAgent;
                });
            if (exists) {
                if (auto deny = requirePrivilege(context.privileges.canModifySubAgents,
                                                 "canModifySubAgents")) {
                    return *deny;
                }
                if (auto governance = enforceGovernance(GovernanceActionKind::SubAgentModify, requestedId)) {
                    return *governance;
                }
            } else {
                const bool granted = context.privileges.canCreateSubAgents || context.autonomousMode;
                if (auto deny = requirePrivilege(granted, "canCreateSubAgents")) {
                    return *deny;
                }
                if (!context.autonomousMode) {
                    if (auto governance = enforceGovernance(GovernanceActionKind::SubAgentCreate, requestedId)) {
                        return *governance;
                    }
                }
            }
            const auto result = adminApiService_->upsertSubAgentJson(request.body);
            return jsonResponse(result, result.succeeded ? 200 : 400);
        }
        if (request.method == "POST" && request.path == "/api/runtime/subagents/remove") {
            if (auto deny = requirePrivilege(context.privileges.canRemoveSubAgents,
                                             "canRemoveSubAgents")) {
                return *deny;
            }
            std::string removalTarget;
            try {
                if (!request.body.empty()) {
                    removalTarget = nlohmann::json::parse(request.body).value("subAgentId", std::string{});
                }
            } catch (...) {}
            if (auto governance = enforceGovernance(GovernanceActionKind::SubAgentRemove, removalTarget)) {
                return *governance;
            }
            const auto result = adminApiService_->removeSubAgentJson(request.body);
            return jsonResponse(result, result.succeeded ? 200 : 400);
        }
        if (request.method == "POST" && request.path == "/api/runtime/subagent-groups") {
            // Sub-agent groups are organizational metadata over the existing
            // sub-agent roster; gate them on canModifySubAgents since they
            // affect how sub-agents are addressed in execution.
            if (auto deny = requirePrivilege(context.privileges.canModifySubAgents,
                                             "canModifySubAgents")) {
                return *deny;
            }
            const auto result = adminApiService_->upsertSubAgentGroupJson(request.body);
            return jsonResponse(result, result.succeeded ? 200 : 400);
        }
        if (request.method == "POST" && request.path == "/api/runtime/subagent-groups/remove") {
            if (auto deny = requirePrivilege(context.privileges.canModifySubAgents,
                                             "canModifySubAgents")) {
                return *deny;
            }
            const auto result = adminApiService_->removeSubAgentGroupJson(request.body);
            return jsonResponse(result, result.succeeded ? 200 : 400);
        }
        // -------------------------------------------------------------------
        // LAN Client Access (Phase 3 of ADR-001)
        // -------------------------------------------------------------------
        if (request.method == "GET" && request.path == "/api/clients") {
            return jsonResponse(lanClientAccessService_->listClients());
        }
        if (request.method == "GET"
            && startsWith(request.path, "/api/clients/")
            && endsWith(request.path, "/config")) {
            const auto prefix = std::string("/api/clients/");
            const auto suffix = std::string("/config");
            const auto clientId = request.path.substr(
                prefix.size(),
                request.path.size() - prefix.size() - suffix.size());
            const auto client = lanClientAccessService_->getClient(clientId);
            if (!client.has_value()) {
                return HttpResponse{ 404, "application/json", "{\"message\":\"LAN client not found.\"}" };
            }
            const auto bundle = composeLanClientConfigBundle(*client, configurationService_->current());
            return jsonResponse(bundle);
        }
        if (request.method == "GET" && startsWith(request.path, "/api/clients/")) {
            const auto suffix = request.path.substr(std::string("/api/clients/").size());
            // Reject sub-resources here so they don't accidentally match the
            // single-client lookup. Only the bare /api/clients/{id} form is
            // a GET; disable/enable/delete have their own POST/DELETE methods.
            if (suffix.empty() || suffix.find('/') != std::string::npos) {
                return HttpResponse{ 404, "application/json", "{\"message\":\"Not found\"}" };
            }
            const auto client = lanClientAccessService_->getClient(suffix);
            if (!client.has_value()) {
                return HttpResponse{ 404, "application/json", "{\"message\":\"LAN client not found.\"}" };
            }
            return jsonResponse(*client);
        }
        if (request.method == "POST" && request.path == "/api/clients") {
            if (auto deny = requirePrivilege(context.privileges.canManageClients,
                                             "canManageClients")) {
                return *deny;
            }
            std::string newClientId;
            try {
                if (!request.body.empty()) {
                    newClientId = nlohmann::json::parse(request.body).value("clientId", std::string{});
                }
            } catch (...) {}
            if (auto governance = enforceGovernance(GovernanceActionKind::ClientRegister, newClientId)) {
                return *governance;
            }
            try {
                const auto client = nlohmann::json::parse(request.body).get<LanClient>();
                const auto result = lanClientAccessService_->upsertClient(client);
                return jsonResponse(result, result.succeeded ? 200 : 400);
            } catch (const std::exception& parseError) {
                return jsonResponse(
                    OperationResult{ false, false, std::string("Invalid LAN client payload: ") + parseError.what() },
                    400);
            }
        }
        if (request.method == "POST"
            && startsWith(request.path, "/api/clients/")
            && endsWith(request.path, "/disable")) {
            if (auto deny = requirePrivilege(context.privileges.canManageClients,
                                             "canManageClients")) {
                return *deny;
            }
            const auto prefix = std::string("/api/clients/");
            const auto suffix = std::string("/disable");
            const auto clientId = request.path.substr(
                prefix.size(),
                request.path.size() - prefix.size() - suffix.size());
            if (auto governance = enforceGovernance(GovernanceActionKind::ClientRevoke, clientId)) {
                return *governance;
            }
            const auto result = lanClientAccessService_->disableClient(clientId);
            return jsonResponse(result, result.succeeded ? 200 : 400);
        }
        if (request.method == "POST"
            && startsWith(request.path, "/api/clients/")
            && endsWith(request.path, "/enable")) {
            if (auto deny = requirePrivilege(context.privileges.canManageClients,
                                             "canManageClients")) {
                return *deny;
            }
            const auto prefix = std::string("/api/clients/");
            const auto suffix = std::string("/enable");
            const auto clientId = request.path.substr(
                prefix.size(),
                request.path.size() - prefix.size() - suffix.size());
            if (auto governance = enforceGovernance(GovernanceActionKind::ClientRegister, clientId)) {
                return *governance;
            }
            const auto result = lanClientAccessService_->enableClient(clientId);
            return jsonResponse(result, result.succeeded ? 200 : 400);
        }
        if (request.method == "POST"
            && startsWith(request.path, "/api/clients/")
            && endsWith(request.path, "/privileges")) {
            if (auto deny = requirePrivilege(context.privileges.canManageClients,
                                             "canManageClients")) {
                return *deny;
            }
            const auto prefix = std::string("/api/clients/");
            const auto suffix = std::string("/privileges");
            const auto clientId = request.path.substr(
                prefix.size(),
                request.path.size() - prefix.size() - suffix.size());
            if (auto governance = enforceGovernance(GovernanceActionKind::ClientPrivilegeChange, clientId)) {
                return *governance;
            }
            try {
                const auto privileges = nlohmann::json::parse(request.body).get<LanClientPrivileges>();
                const auto result = lanClientAccessService_->setPrivileges(clientId, privileges);
                return jsonResponse(result, result.succeeded ? 200 : 400);
            } catch (const std::exception& parseError) {
                return jsonResponse(
                    OperationResult{ false, false, std::string("Invalid privileges payload: ") + parseError.what() },
                    400);
            }
        }
        if (request.method == "POST"
            && startsWith(request.path, "/api/clients/")
            && endsWith(request.path, "/autonomous-mode")) {
            if (auto deny = requirePrivilege(context.privileges.canManageClients,
                                             "canManageClients")) {
                return *deny;
            }
            const auto prefix = std::string("/api/clients/");
            const auto suffix = std::string("/autonomous-mode");
            const auto clientId = request.path.substr(
                prefix.size(),
                request.path.size() - prefix.size() - suffix.size());
            try {
                const auto payload = nlohmann::json::parse(request.body);
                const bool enabled = payload.value("enabled", false);
                // Phase 7: CLU governs the autonomous-mode flip. Source
                // tag tells CLU whether this is an enable (gated by
                // posture + global aiAutonomyEnabled) or a disable (always
                // permitted).
                if (auto governance = enforceGovernance(
                        GovernanceActionKind::ClientAutonomousModeChange,
                        clientId,
                        enabled ? std::string("enable") : std::string("disable"))) {
                    return *governance;
                }
                const auto result = lanClientAccessService_->setAutonomousMode(clientId, enabled);
                return jsonResponse(result, result.succeeded ? 200 : 400);
            } catch (const std::exception& parseError) {
                return jsonResponse(
                    OperationResult{ false, false, std::string("Invalid autonomous-mode payload: ") + parseError.what() },
                    400);
            }
        }
        if (request.method == "DELETE" && startsWith(request.path, "/api/clients/")) {
            if (auto deny = requirePrivilege(context.privileges.canManageClients,
                                             "canManageClients")) {
                return *deny;
            }
            const auto clientId = request.path.substr(std::string("/api/clients/").size());
            if (clientId.empty() || clientId.find('/') != std::string::npos) {
                return HttpResponse{ 404, "application/json", "{\"message\":\"Not found\"}" };
            }
            if (auto governance = enforceGovernance(GovernanceActionKind::ClientRevoke, clientId)) {
                return *governance;
            }
            const auto result = lanClientAccessService_->removeClient(clientId);
            return jsonResponse(result, result.succeeded ? 200 : 400);
        }
        if (request.method == "POST" && request.path == "/api/forsetti/modules/state") {
            if (auto deny = requirePrivilege(context.privileges.canManageModules,
                                             "canManageModules")) {
                return *deny;
            }
            const auto payload = request.body.empty() ? nlohmann::json::object() : nlohmann::json::parse(request.body);
            const auto moduleId = payload.value("moduleId", std::string{});
            const auto action = payload.value("action", std::string{});
            const auto governanceAction = (action == "disable" || action == "remove")
                ? GovernanceActionKind::ModuleDisable
                : GovernanceActionKind::ModuleEnable;
            if (auto governance = enforceGovernance(governanceAction, moduleId)) {
                return *governance;
            }
            const auto result = manageForsettiModule(moduleId, action);
            return jsonResponse(result, result.succeeded ? 200 : 400);
        }
        // -------------------------------------------------------------------
        // Approval queue (Phase 7 of ADR-001)
        // -------------------------------------------------------------------
        if (request.method == "GET" && request.path == "/api/clu/approvals") {
            if (!governanceApprovalQueueService_) {
                return jsonResponse(nlohmann::json::array(), 200);
            }
            return jsonResponse(governanceApprovalQueueService_->listAll());
        }
        if (request.method == "POST"
            && startsWith(request.path, "/api/clu/approvals/")
            && endsWith(request.path, "/approve")) {
            if (auto deny = requirePrivilege(context.privileges.canChangeGovernancePolicy,
                                             "canChangeGovernancePolicy")) {
                return *deny;
            }
            if (!governanceApprovalQueueService_) {
                return jsonResponse(OperationResult{ false, false, "Approval queue is not ready." }, 500);
            }
            const auto prefix = std::string("/api/clu/approvals/");
            const auto suffix = std::string("/approve");
            const auto deferredId = request.path.substr(
                prefix.size(),
                request.path.size() - prefix.size() - suffix.size());
            const auto result = governanceApprovalQueueService_->approve(deferredId, context.actor);
            return jsonResponse(result, result.succeeded ? 200 : 400);
        }
        if (request.method == "POST"
            && startsWith(request.path, "/api/clu/approvals/")
            && endsWith(request.path, "/reject")) {
            if (auto deny = requirePrivilege(context.privileges.canChangeGovernancePolicy,
                                             "canChangeGovernancePolicy")) {
                return *deny;
            }
            if (!governanceApprovalQueueService_) {
                return jsonResponse(OperationResult{ false, false, "Approval queue is not ready." }, 500);
            }
            const auto prefix = std::string("/api/clu/approvals/");
            const auto suffix = std::string("/reject");
            const auto deferredId = request.path.substr(
                prefix.size(),
                request.path.size() - prefix.size() - suffix.size());
            std::string reason;
            try {
                if (!request.body.empty()) {
                    reason = nlohmann::json::parse(request.body).value("reason", std::string{});
                }
            } catch (...) {}
            const auto result = governanceApprovalQueueService_->reject(deferredId, context.actor, reason);
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

    // Emit activity event for every non-polling route. Drop entries that
    // came from malformed HTTP traffic (port-scanner probes, half-open
    // keep-alive checks, bare TCP connects from monitoring agents) which
    // SimpleHttpServer::parseRequest produces with empty method/path. Those
    // were polluting the Live Command Stream with empty rows and rendering
    // as "  -> 200 (0ms)" in the dashboard.
    if (!skipActivity && !request.method.empty() && !request.path.empty()) {
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
