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
#include "SupervisorAssignmentService.h"

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
#include <unordered_set>

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

// v0.9.3: bracket-wrap an IPv6 host literal for use in an HTTP URL per
// RFC 3986. IPv4 strings, DNS names, and already-bracketed IPv6 strings
// are returned unchanged. Pre-v0.9.3 multiple URL-composition sites
// (adminBase, OnboardingProfileService::profileFor, the dashboard's
// browserUrl, gateway URL builder) each had local copies of this logic
// or omitted it entirely, with the result that /api/discovery served
// raw "http://fde3:c02c:...:32c8:7300/..." which every RFC 3986 parser
// rejects as "Invalid port specified" (the unbracketed IPv6 colons
// clash with the port separator). Centralizing in one helper means
// every published URL goes out parseable.
std::string bracketIpv6Host(const std::string& host) {
    if (host.empty()) {
        return host;
    }
    if (host.front() == '[' && host.back() == ']') {
        return host;
    }
    // IPv6 literals are uniquely identifiable: they contain a ':'.
    // IPv4 addresses cannot. DNS hostnames cannot. So a single
    // string::find on ':' is sufficient -- no need to invoke
    // InetPtonW per call.
    return (host.find(':') != std::string::npos) ? ("[" + host + "]") : host;
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

// v0.9.86: shared wildcard-bind substitution. Pre-v0.9.86 three route
// handlers (/api/health/summary, /api/gateway/status, /api/gateway/
// health) each carried their own inline lambda that did this work; v0.9.86
// hoists the logic to one named helper so the substitution rule lives in
// one place and the route handlers shrink to a single call. Walks the
// same chain resolveMcosServerHost uses (bindAddress -> activeProfile
// .preferredBindAddress -> 127.0.0.1), then substitutes any of the
// three wildcard host forms in both URL-prefix and free-text positions.
std::string substituteWildcardHostInUrl(const std::string& raw,
                                          const AppConfiguration& configuration) {
    if (raw.empty()) return raw;
    std::string lanIp;
    const auto& bindAddress = configuration.bindAddress;
    const auto& preferred = configuration.activeProfile.preferredBindAddress;
    if (!bindAddress.empty() && bindAddress != "0.0.0.0"
        && bindAddress != "::" && bindAddress != "[::]") {
        lanIp = bindAddress;
    }
    if ((lanIp.empty() || lanIp == "0.0.0.0")
        && !preferred.empty() && preferred != "0.0.0.0") {
        lanIp = preferred;
    }
    if (lanIp.empty() || lanIp == "0.0.0.0") {
        lanIp = "127.0.0.1";
    }
    std::string out = raw;
    for (const auto* wildcardForm : { "0.0.0.0", "[::]", "[::0]" }) {
        const std::string asUrlPattern = std::string("://") + wildcardForm + ":";
        const auto urlPos = out.find(asUrlPattern);
        if (urlPos != std::string::npos) {
            out.replace(urlPos + 3, std::string(wildcardForm).size(), lanIp);
        }
        while (true) {
            const auto narrativePos = out.find(std::string("http://") + wildcardForm);
            if (narrativePos == std::string::npos) break;
            out.replace(narrativePos + 7, std::string(wildcardForm).size(), lanIp);
        }
    }
    return out;
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

// v0.9.53: shared validator for any operator-supplied resource id that
// will be embedded in a URL path segment (poolId, clientId,
// mcpServerId, subAgentId, groupId). Restricts to ASCII letters,
// digits, '-', '_', '.' -- the same character set every existing
// operator entity already uses (baseline-tools, sequential-thinking,
// etc.). Pre-v0.9.52 the only validation was non-empty / non-blank;
// a probe with id='foo/bar' registered the entity, then the
// /api/<resource>/{id} routes split the slash into a sub-resource
// segment and the entity was unreachable for direct GET / DELETE.
// Empty input is rejected by the caller's existing isBlank check;
// this helper assumes a non-empty trimmed string.
bool isSafeUrlSegmentId(const std::string& value) {
    if (value.empty()) return false;
    for (const unsigned char c : value) {
        const bool ok = (c >= 'a' && c <= 'z')
                     || (c >= 'A' && c <= 'Z')
                     || (c >= '0' && c <= '9')
                     || c == '-' || c == '_' || c == '.';
        if (!ok) return false;
    }
    return true;
}

constexpr const char* kSafeIdRejectionMessage =
    "Id must be ASCII letters, digits, '-', '_', or '.' (no slashes, whitespace, or other URL-unsafe characters).";

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
            return;
        }

        // v0.10.0: prune deprecated endpoint IDs that the operator has
        // had removed from the catalog. Existing installs may still
        // carry these in seededEndpoints from an older default seed;
        // they would otherwise persist forever even after the catalog
        // change because the loader replays whatever is on disk. The
        // prune is idempotent and only rewrites the config file when
        // it actually removes something, so subsequent boots are no-ops.
        static const std::vector<std::string> kDeprecatedEndpointIds = {
            "playwright",
            "docker-control"
        };
        auto& seeded = state_->configuration.activeProfile.seededEndpoints;
        const auto sizeBefore = seeded.size();
        seeded.erase(
            std::remove_if(seeded.begin(), seeded.end(),
                [](const RuntimeEndpoint& ep) {
                    for (const auto& deprecated : kDeprecatedEndpointIds) {
                        if (ep.id == deprecated) return true;
                    }
                    return false;
                }),
            seeded.end());
        if (seeded.size() != sizeBefore) {
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
    // v0.9.30: use the DNS hostname (no 15-char cap) instead of the
    // NetBIOS name. Pre-v0.9.30 this called GetComputerNameA which
    // returns ComputerNamePhysicalNetBIOS by default -- capped at
    // MAX_COMPUTERNAME_LENGTH = 15 by the NetBIOS protocol. Hosts
    // whose actual name is longer ("PC-GAMING-R7-5800X" -> truncated
    // to "PC-GAMING-R7-58") had their hostName silently chopped in
    // /api/beacon, /api/dashboard, and any client surface that read
    // it. The bug-hunt found this because the worker's tools/call
    // host_info reports the full hostname (Win32 differs by API),
    // making the truncation a clear inconsistency between MCOS-
    // surfaced identity and worker-surfaced identity. RFC 1035 caps
    // a DNS hostname at 255 octets so a 256-byte buffer suffices.
    char buffer[256]{};
    DWORD size = sizeof(buffer);
    if (GetComputerNameExA(ComputerNameDnsHostname, buffer, &size) == 0) {
        // Fall back to the NetBIOS API only if the DNS query fails
        // outright -- truncated-but-present beats nothing.
        DWORD legacySize = MAX_COMPUTERNAME_LENGTH + 1;
        if (GetComputerNameA(buffer, &legacySize) == 0) {
            return "MASTER-CONTROL";
        }
        return std::string(buffer, legacySize);
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

    // v0.9.3: IPv4-first preference. Pre-v0.9.3 we walked the unicast
    // address list and broke on the first hit -- on dual-stack hosts
    // Windows often surfaces the IPv6 ULA before the IPv4 LAN address,
    // so MCOS ended up advertising e.g. "fde3:c02c:...:32c8" in the
    // discovery doc even though "192.168.1.7" was also live and is what
    // every IPv4-only LAN AI client expects. The two-pass walk takes
    // an IPv4 address from any non-loopback adapter on pass 1 and only
    // falls through to IPv6 on pass 2 if no IPv4 was found. Link-local
    // IPv6 (fe80::) and link-local IPv4 (169.254.x.x) are deprioritized
    // in pass 2 since they can't be reached from outside the local
    // segment.
    auto extractAddressFamily = [](const SOCKET_ADDRESS& addr) -> int {
        return addr.lpSockaddr ? addr.lpSockaddr->sa_family : 0;
    };
    auto formatHost = [](const SOCKET_ADDRESS& addr, std::string& out) -> bool {
        char host[NI_MAXHOST]{};
        if (getnameinfo(addr.lpSockaddr,
                        static_cast<socklen_t>(addr.iSockaddrLength),
                        host, NI_MAXHOST,
                        nullptr, 0,
                        NI_NUMERICHOST) == 0) {
            out = host;
            return true;
        }
        return false;
    };
    auto isLinkLocalIpv4 = [](const std::string& s) {
        return s.rfind("169.254.", 0) == 0;
    };
    auto isLinkLocalIpv6 = [](const std::string& s) {
        // Lower-case lexicographic test against the link-local prefix
        // fe80::/10. getnameinfo + AF_INET6 always produces lower-case
        // hex, but we still defensively normalize.
        if (s.size() < 4) return false;
        const char a = static_cast<char>(std::tolower(static_cast<unsigned char>(s[0])));
        const char b = static_cast<char>(std::tolower(static_cast<unsigned char>(s[1])));
        const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(s[2])));
        const char d = static_cast<char>(std::tolower(static_cast<unsigned char>(s[3])));
        return a == 'f' && b == 'e' && (c == '8' || c == '9' || c == 'a' || c == 'b') && d == '0';
    };

    // Pass 1: routable IPv4
    for (auto* adapter = addresses; adapter != nullptr && ipAddress.empty(); adapter = adapter->Next) {
        if (adapter->OperStatus != IfOperStatusUp || adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK) {
            continue;
        }
        for (auto* unicast = adapter->FirstUnicastAddress; unicast != nullptr; unicast = unicast->Next) {
            if (extractAddressFamily(unicast->Address) != AF_INET) continue;
            std::string candidate;
            if (!formatHost(unicast->Address, candidate)) continue;
            if (isLinkLocalIpv4(candidate)) continue;
            ipAddress = candidate;
            break;
        }
    }

    // Pass 2: routable IPv6 if no IPv4 was found
    if (ipAddress.empty()) {
        for (auto* adapter = addresses; adapter != nullptr && ipAddress.empty(); adapter = adapter->Next) {
            if (adapter->OperStatus != IfOperStatusUp || adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK) {
                continue;
            }
            for (auto* unicast = adapter->FirstUnicastAddress; unicast != nullptr; unicast = unicast->Next) {
                if (extractAddressFamily(unicast->Address) != AF_INET6) continue;
                std::string candidate;
                if (!formatHost(unicast->Address, candidate)) continue;
                if (isLinkLocalIpv6(candidate)) continue;
                ipAddress = candidate;
                break;
            }
        }
    }

    // Pass 3: any address (link-local IPv4/IPv6) so we don't return empty
    // when the host has nothing better.
    if (ipAddress.empty()) {
        for (auto* adapter = addresses; adapter != nullptr && ipAddress.empty(); adapter = adapter->Next) {
            if (adapter->OperStatus != IfOperStatusUp || adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK) {
                continue;
            }
            for (auto* unicast = adapter->FirstUnicastAddress; unicast != nullptr; unicast = unicast->Next) {
                std::string candidate;
                if (formatHost(unicast->Address, candidate)) {
                    ipAddress = candidate;
                    break;
                }
            }
        }
    }

    // MAC address + traffic counters from the same adapter the IP came
    // from. We do a separate pass to find that adapter; if ipAddress is
    // still empty (no UP non-loopback adapter at all) we skip telemetry
    // gracefully.
    for (auto* adapter = addresses; adapter != nullptr; adapter = adapter->Next) {
        if (adapter->OperStatus != IfOperStatusUp || adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK) {
            continue;
        }
        // Match adapter to the IP we already chose. If there's no chosen
        // IP, fall through to the first eligible adapter for telemetry.
        bool match = ipAddress.empty();
        for (auto* unicast = adapter->FirstUnicastAddress; unicast != nullptr && !match; unicast = unicast->Next) {
            std::string candidate;
            if (formatHost(unicast->Address, candidate) && candidate == ipAddress) {
                match = true;
            }
        }
        if (!match) continue;

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
        if (!isSafeUrlSegmentId(trimCopy(subAgent.id))) {
            return OperationResult{ false, false,
                std::string("Sub-agent ID is invalid. ") + kSafeIdRejectionMessage };
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
        if (!isSafeUrlSegmentId(trimCopy(mcpServer.id))) {
            return OperationResult{ false, false,
                std::string("MCP server ID is invalid. ") + kSafeIdRejectionMessage };
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
        if (!isSafeUrlSegmentId(trimCopy(group.groupId))) {
            return OperationResult{ false, false,
                std::string("Sub-agent group ID is invalid. ") + kSafeIdRejectionMessage };
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
    // v0.9.3: bracket IPv6 host literals so the LAN client config bundle
    // ends up with an RFC 3986-parseable mcosServer URL.
    const auto mcosServer = "http://" + bracketIpv6Host(host) + ":" + std::to_string(configuration.browserPort);

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
        , configurationFile_(std::move(configurationFile)) {
        // v0.9.53: scrub previously-persisted clients with URL-unsafe ids
        // (the v0.9.52 bug-hunt registered a "foo/bar" client before the
        // upsert-time validation existed; that entry is unreachable for
        // direct DELETE /api/clients/{id} because the slash splits the
        // path). Boot-time scrub keeps the persisted file truthful and
        // unblocks the operator's normal cleanup path.
        std::lock_guard<std::mutex> lock(state_->mutex);
        auto& clients = state_->configuration.lanClients;
        const auto before = clients.size();
        clients.erase(
            std::remove_if(clients.begin(), clients.end(),
                [](const LanClient& c) {
                    return !isSafeUrlSegmentId(trimCopy(c.clientId));
                }),
            clients.end());
        if (clients.size() != before) {
            // Persist the scrubbed list so the bad entries don't reappear.
            // The other LanClient mutators use writeJsonFile directly with
            // configurationFile_ + state_->configuration.
            (void)writeJsonFile(configurationFile_, state_->configuration);
        }
    }

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
        if (!isSafeUrlSegmentId(trimCopy(input.clientId))) {
            return OperationResult{ false, false,
                std::string("LAN client id is invalid. ") + kSafeIdRejectionMessage };
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
        // v0.9.3: bracket IPv6 host literals in this exported handoff
        // bundle so a LAN client copying it into their MCP config gets a
        // URL their HTTP library can actually parse.
        const auto gatewayUrl = "http://" + bracketIpv6Host(gatewayHost) + ":" + std::to_string(gatewayPort) + "/mcp/gateway";
        const auto browserHost = browserIterator != endpoints.end() && browserIterator->host != "0.0.0.0"
            ? browserIterator->host
            : preferredHost;
        const auto browserPort = browserIterator != endpoints.end() ? browserIterator->port : configuration.browserPort;
        const auto browserUrl = "http://" + bracketIpv6Host(browserHost) + ":" + std::to_string(browserPort) + "/";

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

    // v0.9.36: DnsServiceRegister rejects pRegisterCompletionCallback==
    // nullptr with ERROR_INVALID_PARAMETER (Win32 87 / 0x57). Pre-v0.9.36
    // /api/beacon showed last_register_error=0x00000057 for all three
    // platform-gateway lanes (windows / macos / ios) and the BeaconService
    // had the same nullptr-callback bug, breaking _mcos._tcp.local
    // announcement too. The callback is purely observability -- the
    // synchronous DnsServiceRegister return value carries the "accepted"
    // decision and the OS handles the actual mDNS broadcast/re-broadcast.
    // We free the PDNS_SERVICE_INSTANCE in deregisterGatewayLocked() not
    // in the callback, so this no-op path is safe.
    static void CALLBACK dnsRegisterCallback(DWORD /*status*/,
        PVOID /*pQueryContext*/,
        PDNS_SERVICE_INSTANCE /*pInstance*/) {
        // No-op completion handler. See comment above.
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
        // v0.9.36: non-null callback required (see dnsRegisterCallback
        // comment above). nullptr here = ERROR_INVALID_PARAMETER from
        // DnsServiceRegister.
        request.pRegisterCompletionCallback = &PlatformServiceCatalogService::dnsRegisterCallback;
        request.pQueryContext = nullptr;
        request.hCredentials = nullptr;
        request.unicastEnabled = FALSE;

        const auto status = DnsServiceRegister(&request, nullptr);
        registration.request = request;
        // v0.9.36: also accept DNS_REQUEST_PENDING. The async-with-
        // callback path returns DNS_REQUEST_PENDING (= 0x2522) to mean
        // "registration accepted, completion will fire later"; that's
        // the success case for our usage pattern.
        registration.registered = (status == ERROR_SUCCESS || status == DNS_REQUEST_PENDING);
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
        // v0.9.3: bracket IPv6 host literals.
        return "http://" + bracketIpv6Host(host.address) + ":" + std::to_string(port) + path;
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
        // v0.9.3: bracket IPv6 host literals.
        return "http://" + bracketIpv6Host(host.address) + ":" + std::to_string(port) + path;
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

        // v0.9.3: precedence chain for the LAN IP advertised in the
        // discovery doc. The order matters because the listen socket
        // and the advertised URL must agree -- a discovery doc that
        // advertises 10.0.0.5 while the listen socket is bound only to
        // 192.168.1.7 sends clients somewhere they can't reach.
        //
        //   1. bindAddress (when not wildcard) -- where MCOS is
        //      DEFINITELY listening. If the operator pinned the listen
        //      socket to a specific address, that's the address LAN
        //      clients must use; nothing else can be right.
        //   2. preferredBindAddress (operator-overridden advertise IP).
        //      Used when bindAddress is wildcard (0.0.0.0 / ::).
        //   3. primaryIpAddress (auto-detected, IPv4-first as of
        //      v0.9.3 Fix #2).
        //   4. configuration.bindAddress as last-resort literal.
        //   5. 127.0.0.1.
        std::string lanIp;
        const std::string& bindAddress = configuration.bindAddress;
        const std::string& preferred =
            configuration.activeProfile.preferredBindAddress;
        if (!bindAddress.empty()
            && bindAddress != "0.0.0.0"
            && bindAddress != "::"
            && bindAddress != "[::]") {
            lanIp = bindAddress;
        }
        if ((lanIp.empty() || lanIp == "0.0.0.0")
            && !preferred.empty() && preferred != "0.0.0.0") {
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

        // v0.9.3: every host->URL composition uses the bracketIpv6Host
        // helper hoisted at the top of this TU so adminBase + the gateway
        // URL builder + downstream onboarding profiles all stay in
        // lockstep on RFC 3986 IPv6 bracketing.
        const std::string adminBase = "http://" + bracketIpv6Host(lanIp) + ":" + std::to_string(configuration.browserPort);

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
        if (gatewayHost.empty() || gatewayHost == "0.0.0.0" || gatewayHost == "::" || gatewayHost == "[::]") {
            gatewayHost = lanIp;
        }
        document.gateway.type = mcpGateway_ ? mcpGateway_->AdapterType() : to_string(gatewayConfig.type);
        // v0.9.0: always compose mcpUrl + healthUrl from the resolved
        // gatewayHost (LAN IP) rather than the adapter's literal
        // composeMcpUrl(listenHost) which would carry through the
        // wildcard '0.0.0.0'. v0.9.3: shares bracketIpv6Host with
        // adminBase + onboarding URL builders.
        const std::string hostInUrl = bracketIpv6Host(gatewayHost);
        document.gateway.mcpUrl    = "http://" + hostInUrl + ":" + std::to_string(gatewayConfig.listenPort) + gatewayConfig.mcpPath;
        document.gateway.healthUrl = "http://" + hostInUrl + ":" + std::to_string(gatewayConfig.listenPort) + gatewayConfig.healthPath;
        document.gateway.state = mcpGateway_ ? to_string(mcpGateway_->CurrentStatus().state) : "disabled";

        document.onboarding.generic = adminBase + "/api/onboarding/generic";
        document.onboarding.claudeCode = adminBase + "/api/onboarding/claude-code";
        document.onboarding.codex = adminBase + "/api/onboarding/codex";
        document.onboarding.grok = adminBase + "/api/onboarding/grok";
        document.onboarding.chatgpt = adminBase + "/api/onboarding/chatgpt";

        document.governance.bundleBaseUrl = adminBase + "/api/governance/bundles";
        document.governance.cluProfileUrl = adminBase + "/api/governance/profile";
        document.governance.decisionsUrl = adminBase + "/api/governance/decisions";

        // v0.9.4: surface the operator-side admin endpoints in the
        // discovery doc so dashboards / operator tooling can find them
        // without out-of-band knowledge. The admin block is
        // operator-facing -- LAN AI clients keep using the gateway URL
        // + onboarding profiles. None of these paths require auth on
        // the trusted LAN surface (per ADR-001), but they are gated by
        // the same network-trust assumption as the rest of port
        // browserPort.
        document.admin.poolsUrl          = adminBase + "/api/pools";
        document.admin.clientsUrl        = adminBase + "/api/clients";
        document.admin.activityUrl       = adminBase + "/api/activity";
        document.admin.hostTelemetryUrl  = adminBase + "/api/host/telemetry";
        document.admin.gatewayStatusUrl  = adminBase + "/api/gateway/status";
        document.admin.gatewayToolsUrl   = adminBase + "/api/gateway/tools";
        document.admin.healthUrl         = adminBase + "/api/health";
        // v0.9.18: persistence-health URL added to discovery so
        // operators can find it from the well-known doc.
        document.admin.activityHealthUrl = adminBase + "/api/activity/health";
        // v0.9.22: single-URL health summary URL.
        document.admin.healthSummaryUrl  = adminBase + "/api/health/summary";

        document.capabilities = {
            "mcp-gateway",
            std::string(document.gateway.type) + "-adapter",
            "dns-sd",
            "udp-beacon",
            "forsetti-governance",
            "clu",
            // v0.9.4: signal protocol-revision parity with the
            // discovery TXT advertisement and the gateway's
            // initialize handshake. Strict clients can short-circuit
            // negotiation when this string is present + matches the
            // version they want.
            "mcp-2025-03-26"
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

    // v0.9.36: non-null pRegisterCompletionCallback is required by
    // DnsServiceRegister. See PlatformServiceCatalogService::dnsRegisterCallback
    // for the rationale -- same fix applied to both DNS-SD register sites.
    static void CALLBACK dnsRegisterCallback(DWORD /*status*/,
        PVOID /*pQueryContext*/,
        PDNS_SERVICE_INSTANCE /*pInstance*/) {
        // No-op completion handler.
    }

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
        // v0.9.36: see dnsRegisterCallback comment above.
        request.pRegisterCompletionCallback = &dnsRegisterCallback;
        request.pQueryContext = nullptr;
        request.hCredentials = nullptr;
        request.unicastEnabled = FALSE;

        const auto status = DnsServiceRegister(&request, nullptr);
        registration.request = request;
        // v0.9.36: also accept DNS_REQUEST_PENDING (async-with-callback
        // success path).
        registration.registered = (status == ERROR_SUCCESS || status == DNS_REQUEST_PENDING);
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

// v0.9.8: forward declarations so WorkerSupervisor::drainAndEmit
// SupervisorEvents can append pool death/respawn/quarantine events
// to the global activity ring without seeing the ActivityEventRing
// class definition (which lives later in this TU, around line
// 10523). Pre-v0.9.8 the supervisor only fed the PHASE-08
// TelemetryAggregator; pool death/respawn events did NOT surface in
// /api/activity, leaving operators reading the audit log blind to
// those events. The helpers below are defined alongside the ring so
// they have full visibility into the ActivityEvent + Ring shape;
// here we only declare them.
void appendPoolLifecycleActivity(const std::string& kind,
                                 const std::string& poolId,
                                 const std::string& message,
                                 int statusCodeHint);

// v0.9.10: separate helper for the supervisor-shutdown wedge event.
// Same pattern as appendPoolLifecycleActivity -- defined alongside
// the global activity ring and forward-declared here so the
// supervisor destructor can call it without visibility into the
// ActivityEventRing class. Pre-v0.9.10 the destructor's detach path
// silently swallowed the wedge; now operators see exactly when the
// watchdog had to be detached and what state the supervisor was in
// at that moment.
void appendSupervisorWedgeActivity(const std::string& message,
                                   int poolCount,
                                   int childCount);

class WorkerSupervisor final : public IWorkerSupervisor {
public:
    WorkerSupervisor() {
        // v0.9.6: start the background watchdog. The watchdog calls
        // reapDeadInstancesLocked every kWatchdogIntervalMs so a pool
        // whose only worker died while no LAN-client traffic was
        // arriving still gets respawned. Pre-v0.9.6 reap was purely
        // opportunistic (ran inside listPools/findPool/ensureMin
        // Instances/scaleUpOnce); a pool with no traffic + a dead
        // worker stayed dark until the next operator query, which
        // could be never on a quiet LAN.
        //
        // v0.9.7: after each reap pass the watchdog drains the
        // accumulated DeathLogEntry / RespawnLogEntry vectors and
        // emits TelemetryEvents through telemetryAggregator_ (if set
        // via setTelemetryAggregator). The drain runs OUTSIDE
        // mutex_ to avoid the supervisor mutex_ overlapping with
        // the aggregator's internal lock; the drain method takes
        // mutex_ briefly to swap the vectors out, then releases it
        // before emitting.
        watchdogRunning_ = true;
        watchdogThread_ = std::thread([this]() {
            while (watchdogRunning_.load(std::memory_order_acquire)) {
                // v0.9.12: condition_variable wait_for replaces the
                // 100ms-step polled sleep. Symmetric with
                // BeaconService's v0.9.11 conversion. The destructor's
                // bounded-shutdown path uses notify_all (added below)
                // to wake the watchdog immediately, so the worst-case
                // shutdown is now one full reap+drain cycle (~few ms)
                // instead of "up to 100ms of sleep, then reap, then
                // drain". Pre-v0.9.12 the 100ms granularity was fine
                // for SCM but felt sloppy compared to BeaconService.
                {
                    std::unique_lock<std::mutex> lock(watchdogSleepMutex_);
                    watchdogSleepCv_.wait_for(lock,
                        std::chrono::milliseconds(kWatchdogIntervalMs),
                        [this]() { return !watchdogRunning_.load(std::memory_order_acquire); });
                }
                if (!watchdogRunning_.load(std::memory_order_acquire)) {
                    return;
                }
                // Reap is short (a few GetExitCodeProcess calls + a
                // possible spawn or two); we hold mutex_ for that
                // window only. Operator API calls may pile up
                // briefly during a reap that triggers a respawn;
                // that's the same path operator-driven scale takes.
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    (void)reapDeadInstancesLocked();
                }
                // v0.9.7: drain + emit events outside mutex_.
                drainAndEmitSupervisorEvents();
                // v0.9.8: garbage-collect crash-window timestamps that
                // have aged out of the kCrashWindowSeconds rolling
                // window. Keeps poolFailureTimestamps_ from growing
                // unboundedly on long-running pools.
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    pruneCrashWindowsLocked();
                }
            }
        });
    }

    // v0.9.7: setter for the TelemetryAggregator pointer. Wired by
    // the runtime composition root so the supervisor can emit
    // pool-lifecycle events without the runtime having to poll.
    // Leaving it null (the default) keeps the supervisor aggregator-
    // agnostic for tests and the supervised-mock path.
    void setTelemetryAggregator(std::shared_ptr<ITelemetryAggregator> aggregator) {
        std::lock_guard<std::mutex> lock(mutex_);
        telemetryAggregator_ = std::move(aggregator);
    }

    ~WorkerSupervisor() override {
        // v0.9.9: bounded shutdown. Pre-v0.9.9 the destructor called
        // watchdogThread_.join() unconditionally, which blocks
        // indefinitely if the watchdog is wedged inside a long reap
        // (e.g., a CreateProcessW that's hanging while the auto-
        // respawn path tries to spin up a fresh child during the
        // service-stop signal). The v0.9.8 deploy hit this directly:
        // SCM showed the service in StopPending for 60+ seconds and
        // had to be force-killed. Now the destructor:
        //
        //   1. Flips watchdogRunning_ to false (the loop's 100ms
        //      sleep step picks this up within 100ms when the loop
        //      is between cycles).
        //   2. Joins with a bounded deadline. The watchdog should
        //      observe the flag and exit within one cycle (~2.1s
        //      worst case = sleep + reap). We give it 5s to be
        //      generous.
        //   3. If join doesn't complete in time, detach the thread.
        //      The process is exiting anyway; the OS will reclaim
        //      the thread when the process terminates. This is
        //      strictly better than blocking SCM forever.
        watchdogRunning_ = false;
        // v0.9.12: notify the watchdog's sleep cv so it observes the
        // flag immediately. Pre-v0.9.12 the destructor relied on the
        // watchdog's 100ms polled sleep step picking up the flag,
        // which added up to 100ms before the reap-and-exit path
        // could run. Now the watchdog wakes within the cv-wakeup
        // latency (microseconds) and exits.
        {
            std::lock_guard<std::mutex> lock(watchdogSleepMutex_);
            watchdogSleepCv_.notify_all();
        }
        if (watchdogThread_.joinable()) {
            // std::thread doesn't expose a join-with-timeout natively.
            // Spin a helper thread that joins, and time-bound the
            // wait via a future. If the helper finishes within the
            // deadline, the watchdog joined cleanly; otherwise we
            // detach and proceed.
            std::atomic<bool> joinComplete{ false };
            std::thread joiner([this, &joinComplete]() {
                if (watchdogThread_.joinable()) {
                    watchdogThread_.join();
                }
                joinComplete.store(true, std::memory_order_release);
            });
            const auto deadline = std::chrono::steady_clock::now()
                                + std::chrono::milliseconds(kShutdownJoinTimeoutMs);
            while (std::chrono::steady_clock::now() < deadline
                   && !joinComplete.load(std::memory_order_acquire)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            if (joinComplete.load(std::memory_order_acquire)) {
                joiner.join();
            } else {
                // v0.9.10: surface the wedge in two places so the
                // operator (and post-mortem analysis) can see WHY
                // the process took the detach path instead of a
                // clean join. globalActivityRing() is fine to
                // touch here because shutdownAll hasn't run yet
                // and the activity ring is thread-safe + a process-
                // global static with a stable lifetime. We capture
                // a coarse snapshot of supervisor state (pool count,
                // child count) to help spot patterns -- e.g. if all
                // wedges happen with a respawn-in-flight, the
                // pattern is CreateProcessW slowness, not a
                // deadlock.
                const int poolCount = [&]() {
                    std::lock_guard<std::mutex> lock(mutex_);
                    return static_cast<int>(pools_.size());
                }();
                const int childCount = [&]() {
                    std::lock_guard<std::mutex> lock(mutex_);
                    return static_cast<int>(children_.size());
                }();
                appendSupervisorWedgeActivity(
                    "Watchdog did not exit within "
                    + std::to_string(kShutdownJoinTimeoutMs)
                    + "ms of watchdogRunning_=false. Detaching.",
                    poolCount, childCount);

                // Watchdog wedged. Detach both the watchdog (so its
                // destructor doesn't terminate() the program) and
                // the joiner (it will return when the watchdog
                // eventually unblocks; until then the process is
                // already exiting and we don't care).
                watchdogThread_.detach();
                joiner.detach();
            }
        }
        (void)shutdownAll();
    }

    std::vector<ManagedEndpointPool> listPools() const override {
        std::vector<ManagedEndpointPool> result;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            // v0.9.5: opportunistic reap so operator queries see live state.
            // Pre-v0.9.5 a dead worker was still listed as Ready until the
            // next outbound stdio call failed. const_cast is safe here:
            // listPools is logically const (read view of state) but
            // physically mutates the bookkeeping (children_ + pool.instances)
            // when a child has exited; that mutation is incidental upkeep,
            // not a change to the user-visible contract.
            const_cast<WorkerSupervisor*>(this)->reapDeadInstancesLocked();
            result.reserve(pools_.size());
            for (const auto& [_, pool] : pools_) {
                ManagedEndpointPool snapshot = pool;
                refreshInstanceLoadLocked(snapshot);
                snapshot.quarantinedUntilUtc =
                    const_cast<WorkerSupervisor*>(this)->quarantineExpiryUtcLocked(snapshot.poolId);
                result.push_back(std::move(snapshot));
            }
            std::sort(result.begin(), result.end(),
                      [](const ManagedEndpointPool& a, const ManagedEndpointPool& b) {
                          return a.poolId < b.poolId;
                      });
        }
        // v0.9.7: emit any death/respawn events that the reap above
        // produced. Run with mutex_ released to avoid lock-ordering
        // issues with the aggregator's own internal lock.
        const_cast<WorkerSupervisor*>(this)->drainAndEmitSupervisorEvents();
        return result;
    }

    std::optional<ManagedEndpointPool> findPool(const std::string& poolId) const override {
        std::optional<ManagedEndpointPool> result;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            // v0.9.5: opportunistic reap (see listPools). Same const_cast
            // rationale.
            const_cast<WorkerSupervisor*>(this)->reapDeadInstancesLocked();
            const auto iterator = pools_.find(poolId);
            if (iterator == pools_.end()) {
                return std::nullopt;
            }
            ManagedEndpointPool snapshot = iterator->second;
            refreshInstanceLoadLocked(snapshot);
            snapshot.quarantinedUntilUtc =
                const_cast<WorkerSupervisor*>(this)->quarantineExpiryUtcLocked(snapshot.poolId);
            result = std::move(snapshot);
        }
        // v0.9.7: emit reap-produced events outside mutex_.
        const_cast<WorkerSupervisor*>(this)->drainAndEmitSupervisorEvents();
        return result;
    }

    OperationResult upsertPool(ManagedEndpointPool pool) override {
        if (pool.poolId.empty()) {
            return OperationResult{ false, false, "Pool id is required." };
        }
        // v0.9.52: validate poolId character set. Pre-v0.9.52 a poolId
        // like "foo/bar" was accepted, registered, and then unreachable
        // because /api/pools/{poolId} split the slash into a sub-
        // resource segment ("foo" with subresource "bar") -- the pool
        // could not be removed via the API.
        // v0.9.53: factored to the shared isSafeUrlSegmentId helper so
        // every operator-supplied id (clientId, mcpServerId, subAgentId,
        // groupId, poolId) goes through the same gate.
        if (!isSafeUrlSegmentId(pool.poolId)) {
            return OperationResult{ false, false,
                std::string("Pool id is invalid. ") + kSafeIdRejectionMessage };
        }
        // v0.9.52: scale-policy sanity check. Pre-v0.9.52 a registration
        // with minInstances > maxInstances was accepted; ensureMin
        // Instances would then loop trying to spawn workers up to the
        // unreachable target. Negative values were accepted as well.
        if (pool.scalePolicy.minInstances < 0) {
            return OperationResult{ false, false,
                "scalePolicy.minInstances must be >= 0." };
        }
        if (pool.scalePolicy.maxInstances < 1) {
            return OperationResult{ false, false,
                "scalePolicy.maxInstances must be >= 1." };
        }
        if (pool.scalePolicy.minInstances > pool.scalePolicy.maxInstances) {
            return OperationResult{ false, false,
                "scalePolicy.minInstances must be <= scalePolicy.maxInstances." };
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
        // v0.9.5: reap before counting -- a dead worker that has not yet
        // been reaped would otherwise count toward the "current" total
        // and short-circuit the spawn-replacement path. Pre-v0.9.5 the
        // sequence (kill worker; POST /api/pools/{id}/scale) reported
        // "Pool already at or above minInstances" while the actual
        // ready count was zero, leaving the pool dark.
        reapDeadInstancesLocked();
        const auto iterator = pools_.find(poolId);
        if (iterator == pools_.end()) {
            return OperationResult{ false, false, "Unknown pool id." };
        }
        ManagedEndpointPool& pool = iterator->second;
        // Parenthesize std::max to bypass the Windows.h max() macro
        // collision when NOMINMAX isn't reached for this TU.
        const int desired = (std::max)(0, pool.scalePolicy.minInstances);
        // v0.9.5: count only Starting/Ready/Busy. Failed/Stopped/
        // Draining instances do not contribute to user-visible
        // capacity and must not block respawn. (Note: post-reap there
        // shouldn't be any Failed/Stopped left in pool.instances --
        // reapDeadInstancesLocked erases them entirely -- but keep the
        // healthy-only count in case a future code path leaves a
        // Failed instance in place for inspection.)
        const int current = healthyInstanceCountLocked(pool);
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
        // v0.9.5: reap first so the maxInstances cap reflects healthy
        // count rather than including dead-but-unreaped placeholders.
        reapDeadInstancesLocked();
        const auto iterator = pools_.find(poolId);
        if (iterator == pools_.end()) {
            return std::string();
        }
        ManagedEndpointPool& pool = iterator->second;
        const int max = (std::max)(0, pool.scalePolicy.maxInstances);
        // v0.9.5: count healthy instances against the cap. Same
        // rationale as ensureMinInstances -- Failed/Stopped do not
        // count toward "what is contributing to capacity right now".
        const int current = healthyInstanceCountLocked(pool);
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

    // PHASE-12 follow-up (v0.6.10): see IWorkerSupervisor::sendStdioJsonRpc
    // contract in MasterControlContracts.h. Implementation lives in private:
    // section below; this is the override declaration.
    StdioBridgeResult sendStdioJsonRpc(const std::string& instanceId,
                                       const std::string& request,
                                       int timeoutMs) override;

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
        // PHASE-12 follow-up (v0.6.10): stdio bridge pipes. The parent (MCOS)
        // owns the read-end of the child's stdout (`childStdoutRead`) and the
        // write-end of the child's stdin (`childStdinWrite`). The other ends
        // are inherited by the child via STARTUPINFO and closed in the parent
        // immediately after CreateProcessW. Both handles are nullptr on the
        // supervised-mock path or if pipe creation failed (the child still
        // runs; tools/call against it returns -32603 with a clear message).
        // `stdioMutex` serializes concurrent JSON-RPC requests to the SAME
        // instance (multiple LAN clients hitting the same lease, or one
        // client issuing parallel calls). It is heap-allocated so this
        // struct stays movable for std::map storage.
        // `stdioReadBuffer` accumulates partial lines across ReadFile calls.
        // `nextRequestId` is monotonic per-child and only incremented under
        // stdioMutex, so no atomic is needed.
        HANDLE childStdoutRead = nullptr;
        HANDLE childStdinWrite = nullptr;
        std::unique_ptr<std::mutex> stdioMutex;
        std::string stdioReadBuffer;
        uint64_t nextRequestId = 1;
        // v0.9.4: lazy MCP initialize handshake state. Spec-compliant
        // MCP servers (every Anthropic / community npx-installable
        // server -- @modelcontextprotocol/server-memory, server-
        // sequential-thinking, server-filesystem, mcp-server-sqlite-
        // npx, etc.) refuse `tools/list`, `tools/call`, and other
        // session methods until the client has completed the
        // `initialize` -> `notifications/initialized` handshake. The
        // in-tree MCOS baseline-tools worker happens to dispatch
        // tools/list regardless of session state, which masked this
        // gap pre-v0.9.4 -- baseline-tools "just worked", but every
        // externally-supervised pool we registered came up Ready and
        // then contributed zero tools to /api/gateway/tools because
        // its `tools/list` reply was a JSON-RPC error the gateway
        // catalog refresh silently dropped (per
        // refreshToolCatalogLocked's "skip empty / non-result"
        // policy).
        //
        // The self-handshake below runs once per ChildProcess, on the
        // first `sendStdioJsonRpc` call. After it succeeds the flag
        // stays true for the lifetime of the supervised child; if the
        // child exits and a new one is spawned for the same pool, a
        // fresh ChildProcess record is created and the handshake
        // re-runs. If the handshake itself fails (timeout / child
        // exited / parse error) the flag stays false, the original
        // call returns the bridge error, and the next attempt will
        // re-try the handshake.
        bool mcpInitialized = false;
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
        child.stdioMutex = std::make_unique<std::mutex>();
        child.jobObject = CreateJobObjectW(nullptr, nullptr);
        if (child.jobObject != nullptr) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
            limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            SetInformationJobObject(child.jobObject, JobObjectExtendedLimitInformation,
                                    &limits, sizeof(limits));
        }

        // PHASE-12 follow-up (v0.6.10): stdio bridge. Create two anonymous
        // pipes -- one for the parent to read the child's stdout, one for
        // the parent to write to the child's stdin. The child-side ends
        // are marked inheritable via SECURITY_ATTRIBUTES; the parent-side
        // ends are explicitly cleared via SetHandleInformation so the child
        // does not inherit them. CreateProcessW is called with
        // bInheritHandles=TRUE so the child receives the inheritable ends.
        // After spawn, the parent closes the child-side ends (they are now
        // open in the child process) so EOF-detection works correctly when
        // the child exits. If pipe creation fails, the child still runs in
        // no-bridge mode -- tools/call against it returns a -32603 with a
        // clear "stdio bridge unavailable" message rather than a hang.
        SECURITY_ATTRIBUTES pipeSecurity{};
        pipeSecurity.nLength        = sizeof(SECURITY_ATTRIBUTES);
        pipeSecurity.bInheritHandle = TRUE;
        pipeSecurity.lpSecurityDescriptor = nullptr;

        HANDLE childStdoutWrite = nullptr;  // child-side, inherited
        HANDLE parentStdoutRead = nullptr;  // parent-side, NOT inherited
        HANDLE childStdinRead   = nullptr;  // child-side, inherited
        HANDLE parentStdinWrite = nullptr;  // parent-side, NOT inherited
        bool bridgeAvailable = false;

        if (CreatePipe(&parentStdoutRead, &childStdoutWrite, &pipeSecurity, 0)
            && CreatePipe(&childStdinRead, &parentStdinWrite, &pipeSecurity, 0)) {
            // Parent-side ends must NOT be inherited by the child --
            // otherwise the child holds a copy and EOF never fires.
            SetHandleInformation(parentStdoutRead, HANDLE_FLAG_INHERIT, 0);
            SetHandleInformation(parentStdinWrite, HANDLE_FLAG_INHERIT, 0);
            bridgeAvailable = true;
        } else {
            // Clean up any partial allocation; child will still spawn but
            // without a bridge.
            if (parentStdoutRead) { CloseHandle(parentStdoutRead); parentStdoutRead = nullptr; }
            if (childStdoutWrite) { CloseHandle(childStdoutWrite); childStdoutWrite = nullptr; }
            if (childStdinRead)   { CloseHandle(childStdinRead);   childStdinRead   = nullptr; }
            if (parentStdinWrite) { CloseHandle(parentStdinWrite); parentStdinWrite = nullptr; }
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
        if (bridgeAvailable) {
            startupInfo.dwFlags    |= STARTF_USESTDHANDLES;
            startupInfo.hStdOutput = childStdoutWrite;
            startupInfo.hStdError  = childStdoutWrite;  // merge stderr into stdout
            startupInfo.hStdInput  = childStdinRead;
        }

        PROCESS_INFORMATION processInfo{};
        std::wstring workingDir = wideFromUtf8(pool.template_.workingDirectory);
        const BOOL launched = CreateProcessW(
            nullptr,
            mutableCommandLine.data(),
            nullptr, nullptr,
            bridgeAvailable ? TRUE : FALSE,
            CREATE_NO_WINDOW | CREATE_SUSPENDED,
            nullptr,
            workingDir.empty() ? nullptr : workingDir.c_str(),
            &startupInfo,
            &processInfo);
        if (!launched) {
            if (child.jobObject) {
                CloseHandle(child.jobObject);
            }
            if (parentStdoutRead) CloseHandle(parentStdoutRead);
            if (childStdoutWrite) CloseHandle(childStdoutWrite);
            if (childStdinRead)   CloseHandle(childStdinRead);
            if (parentStdinWrite) CloseHandle(parentStdinWrite);
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

        // Close the child-side ends in the parent so EOF detection works
        // when the child exits. Keep the parent-side ends in ChildProcess.
        if (bridgeAvailable) {
            CloseHandle(childStdoutWrite);
            CloseHandle(childStdinRead);
            child.childStdoutRead = parentStdoutRead;
            child.childStdinWrite = parentStdinWrite;
        }

        instance.processId = processInfo.dwProcessId;
        instance.supervised = true;
        children_.emplace(instance.instanceId, std::move(child));
        transitionInstanceLocked(instance, EndpointInstanceState::Ready,
                                 bridgeAvailable
                                 ? "Instance started under MCOS Job Object supervision (stdio bridge active)."
                                 : "Instance started under MCOS Job Object supervision (stdio bridge unavailable).");
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
                // PHASE-12 follow-up (v0.6.10): close stdio bridge pipes
                // before destroying the Job Object. Closing childStdinWrite
                // signals EOF to the child's stdin reader; closing
                // childStdoutRead releases the parent's read end. Both are
                // optional (may be nullptr if pipe creation failed at spawn).
                if (child.childStdinWrite != nullptr) {
                    CloseHandle(child.childStdinWrite);
                    child.childStdinWrite = nullptr;
                }
                if (child.childStdoutRead != nullptr) {
                    CloseHandle(child.childStdoutRead);
                    child.childStdoutRead = nullptr;
                }
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

    // v0.9.8: humanize a Windows exit code into a short suffix the
    // operator can read. Raw 32-bit codes are returned by
    // GetExitCodeProcess; common death causes map to NTSTATUS values
    // (which are >= 0x80000000 and surface as huge unsigned ints in
    // the death message). Pre-v0.9.8 the death message read e.g.
    // "exitCode=4294967295" which is uninformative; now it reads
    // "exitCode=4294967295 (STATUS_PROCESS_KILLED -- forced kill /
    // taskkill /f / Stop-Process -Force)".
    static std::string humanizeExitCode(uint32_t code) {
        switch (code) {
            case 0x00000000u: return "STATUS_SUCCESS -- normal exit";
            case 0x00000001u: return "exit(1) -- generic error";
            case 0xC0000005u: return "STATUS_ACCESS_VIOLATION -- segfault / null deref / bad pointer";
            case 0xC000001Du: return "STATUS_ILLEGAL_INSTRUCTION -- bad opcode / corrupt code page";
            case 0xC0000094u: return "STATUS_INTEGER_DIVIDE_BY_ZERO";
            case 0xC0000409u: return "STATUS_STACK_BUFFER_OVERRUN -- /GS triggered";
            case 0xC000013Au: return "STATUS_CONTROL_C_EXIT -- Ctrl+C / interactive stop";
            case 0xC0000142u: return "STATUS_DLL_INIT_FAILED -- DLL load failure on startup";
            case 0xC0000374u: return "STATUS_HEAP_CORRUPTION";
            case 0xC0000017u: return "STATUS_NO_MEMORY -- out of memory";
            case 0xFFFFFFFFu: return "STATUS_PROCESS_KILLED -- forced kill / taskkill /f / Stop-Process -Force";
            default: break;
        }
        // Anything else: return empty so the caller emits just the
        // numeric code.
        return std::string{};
    }

    // v0.9.8: prune crash-window entries older than kCrashWindowSeconds.
    // Caller holds mutex_.
    void pruneCrashWindowsLocked() {
        const auto cutoff = std::chrono::steady_clock::now()
                          - std::chrono::seconds(kCrashWindowSeconds);
        for (auto& [poolId, timestamps] : poolFailureTimestamps_) {
            timestamps.erase(
                std::remove_if(timestamps.begin(), timestamps.end(),
                    [&](const std::chrono::steady_clock::time_point& t) { return t < cutoff; }),
                timestamps.end());
        }
        // Also retire expired quarantines.
        const auto now = std::chrono::steady_clock::now();
        for (auto it = poolQuarantineUntil_.begin(); it != poolQuarantineUntil_.end(); ) {
            if (it->second <= now) {
                it = poolQuarantineUntil_.erase(it);
            } else {
                ++it;
            }
        }
    }

    // v0.9.8: returns true if the pool is currently quarantined (in
    // its cool-down window). Caller holds mutex_.
    bool isPoolQuarantinedLocked(const std::string& poolId) const {
        const auto it = poolQuarantineUntil_.find(poolId);
        if (it == poolQuarantineUntil_.end()) return false;
        return std::chrono::steady_clock::now() < it->second;
    }

    // v0.9.40: convert the in-memory steady_clock quarantine expiry
    // into a wall-clock ISO-8601 UTC timestamp for /api/pools/{id}.
    // Returns empty string when the pool is not quarantined. Caller
    // holds mutex_. Conversion: take the steady_clock remaining
    // duration and add it to the current system_clock so the wall-
    // clock expiry tracks the true cool-down deadline even if
    // monotonic and wall clocks have drifted.
    std::string quarantineExpiryUtcLocked(const std::string& poolId) const {
        const auto it = poolQuarantineUntil_.find(poolId);
        if (it == poolQuarantineUntil_.end()) return std::string{};
        const auto steadyNow = std::chrono::steady_clock::now();
        if (it->second <= steadyNow) return std::string{}; // already expired
        const auto remaining = it->second - steadyNow;
        // Cast to system_clock::duration to satisfy to_time_t()'s
        // expected time_point<system_clock, system_clock::duration>;
        // adding steady_clock::duration (nanoseconds on MSVC) directly
        // yields a time_point<system_clock, nanoseconds> which the
        // overload doesn't match.
        const auto wallExpiry = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            std::chrono::system_clock::now() + remaining);
        const std::time_t expiryTime = std::chrono::system_clock::to_time_t(wallExpiry);
        std::tm utcTime{};
        gmtime_s(&utcTime, &expiryTime);
        char buffer[32]{};
        std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utcTime);
        return std::string(buffer);
    }

    // v0.9.5: instance count helper that excludes Failed/Stopped/Draining --
    // those are not contributing to user-visible "is this pool serving
    // traffic" capacity. ensureMinInstances + scaleUpOnce + autoscale
    // policy decisions all use this rather than raw `instances.size()`,
    // which previously counted Failed/Stopped instances toward
    // minInstances and prevented auto-respawn after a worker died.
    static int healthyInstanceCountLocked(const ManagedEndpointPool& pool) {
        int n = 0;
        for (const auto& instance : pool.instances) {
            if (instance.state == EndpointInstanceState::Starting
                || instance.state == EndpointInstanceState::Ready
                || instance.state == EndpointInstanceState::Busy) {
                ++n;
            }
        }
        return n;
    }

    // v0.9.5: reap instances whose underlying child process has exited.
    //
    // Pre-v0.9.5 the supervisor learned about child death only when an
    // outbound `sendStdioJsonRpc` write failed -- and even then it just
    // returned the error without updating the pool's instance state, so
    // `GET /api/pools/{poolId}` continued to advertise the dead instance
    // as "ready" until the next operator-driven scale or remove.
    //
    // This helper walks every pool, calls GetExitCodeProcess on each
    // child handle, and for any instance whose process is no longer
    // STILL_ACTIVE: transitions it to Failed, records the exit code in
    // statusMessage, closes handles, drops the children_ record, and
    // erases the instance from pool.instances. After all reaps, every
    // pool whose healthyInstanceCount fell below minInstances spawns
    // replacements -- this is the auto-respawn path operators expect
    // from a "supervised" pool (without it, a single worker crash leaves
    // the pool dark forever). Telemetry events for each reap and each
    // respawn are emitted via the runtime's TelemetryAggregator
    // interface, attached after construction; we tolerate it being
    // null during early-boot reaps before the aggregator is wired.
    //
    // Caller MUST hold mutex_. Returns the number of instances reaped.
    int reapDeadInstancesLocked() {
#if !defined(_WIN32)
        return 0;
#else
        int reaped = 0;
        for (auto& [poolId, pool] : pools_) {
            for (auto it = pool.instances.begin(); it != pool.instances.end(); ) {
                const auto childIt = children_.find(it->instanceId);
                bool deadChild = false;
                DWORD exitCode = 0;
                if (childIt != children_.end()
                    && childIt->second.processInfo.hProcess != nullptr) {
                    if (GetExitCodeProcess(childIt->second.processInfo.hProcess, &exitCode)) {
                        // STILL_ACTIVE (259) means the process is alive;
                        // any other value means it exited.
                        if (exitCode != STILL_ACTIVE) {
                            deadChild = true;
                        }
                    } else {
                        // Handle invalid -- treat as dead.
                        deadChild = true;
                    }
                } else if (it->supervised
                           && it->state != EndpointInstanceState::Stopped
                           && it->state != EndpointInstanceState::Failed) {
                    // Supervised instance has no child record -- this is
                    // an inconsistency; reap to bring state in line.
                    deadChild = true;
                }

                if (deadChild) {
                    // Capture death info before we mutate state.
                    const std::string deadInstanceId = it->instanceId;
                    const std::string statusBefore = to_string(it->state);

                    if (childIt != children_.end()) {
                        ChildProcess& child = childIt->second;
                        if (child.childStdinWrite != nullptr) {
                            CloseHandle(child.childStdinWrite);
                            child.childStdinWrite = nullptr;
                        }
                        if (child.childStdoutRead != nullptr) {
                            CloseHandle(child.childStdoutRead);
                            child.childStdoutRead = nullptr;
                        }
                        if (child.jobObject != nullptr) {
                            CloseHandle(child.jobObject);
                            child.jobObject = nullptr;
                        }
                        if (child.processInfo.hProcess != nullptr) {
                            CloseHandle(child.processInfo.hProcess);
                            child.processInfo.hProcess = nullptr;
                        }
                        if (child.processInfo.hThread != nullptr) {
                            CloseHandle(child.processInfo.hThread);
                            child.processInfo.hThread = nullptr;
                        }
                        children_.erase(childIt);
                    }

                    // Erase the instance entirely from the pool. We do
                    // not keep a Failed-tombstone because the pool's
                    // operational view is "what is alive now"; the
                    // activity ring carries the historical record.
                    it = pool.instances.erase(it);
                    ++reaped;

                    // Defer telemetry emission until after the mutex is
                    // released by the caller (we just record it now).
                    deathLog_.push_back(DeathLogEntry{
                        poolId, deadInstanceId, statusBefore,
                        static_cast<uint32_t>(exitCode)
                    });

                    // v0.9.8: register the failure in the crash
                    // circuit breaker's rolling window. If the count
                    // crosses kCrashThreshold within
                    // kCrashWindowSeconds, mark the pool quarantined.
                    auto& timestamps = poolFailureTimestamps_[poolId];
                    timestamps.push_back(std::chrono::steady_clock::now());
                    // Inline prune (the watchdog also prunes; do it
                    // here so the count is fresh).
                    const auto cutoff = std::chrono::steady_clock::now()
                                      - std::chrono::seconds(kCrashWindowSeconds);
                    timestamps.erase(
                        std::remove_if(timestamps.begin(), timestamps.end(),
                            [&](const std::chrono::steady_clock::time_point& t) { return t < cutoff; }),
                        timestamps.end());
                    if (static_cast<int>(timestamps.size()) >= kCrashThreshold
                        && !isPoolQuarantinedLocked(poolId)) {
                        poolQuarantineUntil_[poolId] =
                            std::chrono::steady_clock::now()
                            + std::chrono::seconds(kQuarantineCooldownSeconds);
                        // v0.9.9: dedicated quarantine entry (was an
                        // ad-hoc DeathLogEntry pre-v0.9.9). Cleaner
                        // intent + lets the consumer carry all four
                        // fields (poolId, failureCount, window,
                        // cooldown) into the human-readable message
                        // rather than packing them into the exitCode
                        // slot.
                        quarantineLog_.push_back(QuarantineLogEntry{
                            poolId,
                            static_cast<int>(timestamps.size()),
                            kCrashWindowSeconds,
                            kQuarantineCooldownSeconds
                        });
                    }
                } else {
                    ++it;
                }
            }
        }

        // Auto-respawn pass. After reap, any pool below minInstances
        // gets fresh instances spawned to bring it back to its
        // contracted floor. The operator's contract for `minInstances`
        // is "this many are serving traffic at all times"; honoring
        // that requires us to spawn replacements without an explicit
        // POST /api/pools/{poolId}/scale.
        //
        // v0.9.8: skip pools currently in quarantine. The cool-down
        // gives a misbehaving worker time to be diagnosed by the
        // operator without MCOS spinning a respawn loop in the
        // background that fills the activity log with errors.
        // Operators can force re-spawn by POST /api/pools/{id}/scale
        // (which calls ensureMinInstances; ensureMinInstances does
        // not consult the quarantine, so it's the explicit override).
        for (auto& [poolId, pool] : pools_) {
            if (isPoolQuarantinedLocked(poolId)) {
                continue;
            }
            const int desired = (std::max)(0, pool.scalePolicy.minInstances);
            int healthy = healthyInstanceCountLocked(pool);
            while (healthy < desired) {
                EndpointInstance instance = startInstanceLocked(pool);
                respawnLog_.push_back(RespawnLogEntry{ poolId, instance.instanceId });
                pool.instances.push_back(std::move(instance));
                ++healthy;
            }
            if (reaped > 0) {
                pool.updatedAtUtc = timestampNowUtc();
            }
        }

        return reaped;
#endif
    }

    // v0.9.5: drained outside the supervisor mutex by callers that have
    // a TelemetryAggregator handle. We capture death/respawn events
    // under mutex_ (where we can read pool/child state safely) but
    // emit them to the activity ring without the mutex held, because
    // the aggregator path may itself acquire other locks.
    struct DeathLogEntry {
        std::string poolId;
        std::string instanceId;
        std::string previousState;  // e.g. "ready"
        uint32_t exitCode = 0;
    };
    struct RespawnLogEntry {
        std::string poolId;
        std::string newInstanceId;
    };
    // v0.9.9: dedicated quarantine entry. Pre-v0.9.9 quarantine events
    // were ad-hoc-encoded as DeathLogEntry with empty instanceId and
    // exitCode = failure-count-in-window; the consumer in drainAndEmit
    // distinguished by instanceId.empty(). That worked but conflated
    // two distinct lifecycle events in one struct. The dedicated type
    // makes intent explicit and lets the failure-count and the window
    // travel independently.
    struct QuarantineLogEntry {
        std::string poolId;
        int failureCountInWindow = 0;
        int windowSeconds = 0;
        int cooldownSeconds = 0;
    };
    std::vector<DeathLogEntry>     deathLog_;
    std::vector<RespawnLogEntry>   respawnLog_;
    std::vector<QuarantineLogEntry> quarantineLog_;

    void drainSupervisorEvents(std::vector<DeathLogEntry>& deaths,
                               std::vector<RespawnLogEntry>& respawns) {
        std::lock_guard<std::mutex> lock(mutex_);
        deaths   = std::move(deathLog_);
        respawns = std::move(respawnLog_);
        deathLog_.clear();
        respawnLog_.clear();
    }

    // v0.9.7: drains the accumulated death/respawn events under mutex_,
    // then emits them through telemetryAggregator_ with the mutex
    // released. Pool deaths are Telemetry::Worker::Error (operators
    // need to see them); auto-respawns are Telemetry::Worker::Info.
    // Called from the watchdog tick after every reap pass and from
    // operator-API entries that triggered a reap.
    //
    // The two-phase pattern (drain under lock, emit unlocked) is
    // mandatory because the aggregator's recordEvent may itself take
    // a different mutex (the events ring buffer's own lock); holding
    // mutex_ across that call risks deadlock when a future
    // aggregator-side path inversely needs supervisor state.
    void drainAndEmitSupervisorEvents() {
        std::vector<DeathLogEntry>      deaths;
        std::vector<RespawnLogEntry>    respawns;
        std::vector<QuarantineLogEntry> quarantines;
        std::shared_ptr<ITelemetryAggregator> aggregator;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            deaths      = std::move(deathLog_);
            respawns    = std::move(respawnLog_);
            quarantines = std::move(quarantineLog_);
            deathLog_.clear();
            respawnLog_.clear();
            quarantineLog_.clear();
            aggregator = telemetryAggregator_;
        }
        // v0.9.9: real worker deaths only. Quarantine events have
        // their own struct + their own loop below.
        for (const auto& death : deaths) {
            // v0.9.8: humanize the exit code so the operator can see
            // at a glance what kind of crash happened. Falls back to
            // the bare number when the code isn't in the table.
            std::string codeText = std::to_string(death.exitCode);
            const std::string humanized = humanizeExitCode(death.exitCode);
            if (!humanized.empty()) {
                codeText += " (" + humanized + ")";
            }
            const std::string message =
                "Pool worker '" + death.instanceId + "' exited unexpectedly"
                " (was state=" + death.previousState + ", exitCode=" + codeText + ")";

            if (aggregator) {
                TelemetryEvent evt;
                evt.category = TelemetryCategory::Worker;
                evt.severity = TelemetrySeverity::Error;
                evt.message = message;
                evt.poolId = death.poolId;
                aggregator->recordEvent(std::move(evt));
            }
            appendPoolLifecycleActivity(
                "pool_worker_death", death.poolId, message,
                500 /* operator-side severity hint */);
        }
        // v0.9.9: dedicated quarantine emit loop. Carries all four
        // QuarantineLogEntry fields into the message naturally.
        for (const auto& q : quarantines) {
            const std::string message =
                "Pool '" + q.poolId + "' QUARANTINED -- "
                + std::to_string(q.failureCountInWindow)
                + " worker crashes within " + std::to_string(q.windowSeconds)
                + "s window; auto-respawn paused for "
                + std::to_string(q.cooldownSeconds)
                + "s. Operator can force respawn via POST /api/pools/"
                + q.poolId + "/scale.";

            if (aggregator) {
                TelemetryEvent evt;
                evt.category = TelemetryCategory::Worker;
                evt.severity = TelemetrySeverity::Error;
                evt.message = message;
                evt.poolId = q.poolId;
                aggregator->recordEvent(std::move(evt));
            }
            appendPoolLifecycleActivity(
                "pool_quarantine", q.poolId, message,
                500 /* operator-side severity hint */);
        }
        for (const auto& respawn : respawns) {
            const std::string message = "Pool '" + respawn.poolId
                + "' auto-respawned to instance '" + respawn.newInstanceId
                + "' (watchdog or reap-driven)";
            if (aggregator) {
                TelemetryEvent evt;
                evt.category = TelemetryCategory::Worker;
                evt.severity = TelemetrySeverity::Info;
                evt.message = message;
                evt.poolId = respawn.poolId;
                aggregator->recordEvent(std::move(evt));
            }
            // v0.9.8: dual-emit to activity ring (see deaths above).
            appendPoolLifecycleActivity(
                "pool_worker_respawn",
                respawn.poolId,
                message,
                200 /* operator-side severity hint */);
        }
    }

    mutable std::mutex mutex_;
    std::map<std::string, ManagedEndpointPool> pools_;
    // children_ is mutable because refreshInstanceLoadLocked() updates
    // the per-PID FILETIME baseline on every read of pool state. The
    // sampling state is incidental cache, not part of the supervised
    // child contract, so it lives here under the same lock.
    mutable std::map<std::string, ChildProcess> children_;
    std::atomic<uint64_t> nextInstanceSerial_{ 1 };

    // v0.9.6: background watchdog. Wakes every kWatchdogIntervalMs and
    // calls reapDeadInstancesLocked so dead workers are detected and
    // replaced even when no LAN-client traffic is arriving. The interval
    // is intentionally larger than the typical worker startup time so
    // we don't see "ghost reap" of an instance that's still completing
    // its initialize handshake (kStartupGraceMs handles that side; the
    // watchdog only inspects established children via
    // GetExitCodeProcess). 2 seconds is operator-noticeable as
    // "auto-recovery happens within a couple seconds" without
    // generating noisy reap traffic on quiet LANs.
    static constexpr int kWatchdogIntervalMs = 2000;
    // v0.9.9: bounded shutdown deadline -- if the watchdog can't stop
    // within this many milliseconds, the destructor detaches the
    // thread and proceeds. Strictly larger than one full reap cycle
    // (~2.1s worst case) so a clean watchdog always joins; large
    // enough to avoid false-positive detachment under transient
    // CreateProcessW slowness during a respawn spawned right at
    // shutdown time. 5 seconds is the SCM patience threshold most
    // operators tolerate.
    static constexpr int kShutdownJoinTimeoutMs = 5000;
    std::thread watchdogThread_;
    std::atomic<bool> watchdogRunning_{ false };
    // v0.9.12: condition variable for the watchdog sleep, symmetric
    // with BeaconService's v0.9.11 sleep model. Pre-v0.9.12 the
    // watchdog used a 100ms-step polled sleep that observed
    // watchdogRunning_=false within 100ms; the cv lets the
    // destructor wake it immediately via notify_all. Worst-case
    // shutdown is now bounded by one in-flight reap+drain cycle
    // (a few ms) instead of "up to 100ms of sleep, then reap".
    std::mutex watchdogSleepMutex_;
    std::condition_variable watchdogSleepCv_;

    // v0.9.7: telemetry aggregator pointer used by drainAndEmit
    // SupervisorEvents. Set via setTelemetryAggregator from the
    // runtime composition root; null until the runtime wires it.
    // Held under mutex_ for safe atomic shared_ptr swap (modeling
    // future hot-swap scenarios -- today the runtime sets it once
    // at startup and never changes it).
    std::shared_ptr<ITelemetryAggregator> telemetryAggregator_;

    // v0.9.8: per-pool crash circuit breaker.
    //
    // poolFailureTimestamps_[poolId] holds the steady_clock timestamps
    // of recent worker deaths for that pool. The reap path appends a
    // timestamp on every death; the watchdog tick prunes entries older
    // than kCrashWindowSeconds. If the count of timestamps within the
    // window crosses kCrashThreshold, the pool is quarantined: the
    // auto-respawn loop in reapDeadInstancesLocked skips that pool,
    // and a Telemetry::Worker::Error is emitted naming the
    // quarantine. Quarantine lifts after kQuarantineCooldownSeconds;
    // operators can also force re-spawn via POST /api/pools/{id}/scale.
    //
    // Pre-v0.9.8 a worker that crashed in a tight loop got auto-
    // respawned every 2s indefinitely, filling the activity log with
    // hundreds of error events per minute and consuming a Job Object
    // slot on every cycle.
    static constexpr int kCrashThreshold        = 5;   // failures
    static constexpr int kCrashWindowSeconds    = 60;  // rolling window
    static constexpr int kQuarantineCooldownSeconds = 60;
    std::map<std::string, std::vector<std::chrono::steady_clock::time_point>>
        poolFailureTimestamps_;
    std::map<std::string, std::chrono::steady_clock::time_point>
        poolQuarantineUntil_;

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

// PHASE-12 follow-up (v0.6.10): WorkerSupervisor::sendStdioJsonRpc.
// See IWorkerSupervisor::sendStdioJsonRpc contract in MasterControlContracts.h.
//
// Synchronous stdin write -> stdout poll-read loop with deadline-based timeout.
// Uses PeekNamedPipe to test for available bytes without blocking, ReadFile
// to drain whatever is available, and parses newline-delimited JSON lines
// from `stdioReadBuffer` until a line carrying the JSON-RPC `id` we sent
// arrives. The JSON-RPC spec for stdio MCP transports is line-delimited:
// each request and response occupies one '\n'-terminated line of UTF-8 JSON.
// We do NOT depend on the child producing exactly one line per request --
// MCP servers may emit log lines, notifications, or progress messages
// interleaved with responses; we filter by matching `id`.
//
// Concurrency model: a per-instance `stdioMutex` serializes calls to the
// SAME instance. Different instances are independent. The supervisor's
// pool-mutex (`mutex_`) is held only briefly to look up the ChildProcess,
// then released before the blocking I/O so other supervisor traffic
// (telemetry sampling, lease accounting) is not blocked on a 30s RPC.
StdioBridgeResult WorkerSupervisor::sendStdioJsonRpc(const std::string& instanceId,
                                                      const std::string& request,
                                                      int timeoutMs) {
    StdioBridgeResult result{};
#if !defined(_WIN32)
    (void)instanceId; (void)request; (void)timeoutMs;
    result.errorMessage = "stdio bridge requires Windows";
    return result;
#else
    // Phase 1: under the supervisor mutex, look up the child and grab the
    // pipe handles + per-instance stdio mutex. Release the supervisor
    // mutex before any blocking I/O so other paths (telemetry, scaling)
    // are not throttled by long RPCs.
    HANDLE writeHandle = nullptr;
    HANDLE readHandle  = nullptr;
    std::mutex* perInstanceMutex = nullptr;
    bool needsHandshake = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = children_.find(instanceId);
        if (it == children_.end()) {
            result.errorMessage = "instance not found in supervisor (instanceId=" + instanceId + ")";
            return result;
        }
        ChildProcess& child = it->second;
        if (!child.active || child.processInfo.hProcess == nullptr) {
            result.errorMessage = "instance is not active";
            return result;
        }
        if (child.childStdinWrite == nullptr || child.childStdoutRead == nullptr) {
            result.errorMessage = "stdio bridge unavailable on this instance (pipe creation failed at spawn, or supervised-mock mode)";
            return result;
        }
        if (!child.stdioMutex) {
            result.errorMessage = "stdio bridge mutex missing (internal bug)";
            return result;
        }
        writeHandle      = child.childStdinWrite;
        readHandle       = child.childStdoutRead;
        perInstanceMutex = child.stdioMutex.get();
        needsHandshake   = !child.mcpInitialized;
    }

    // Phase 2: per-instance stdio lock. Concurrent requests to the same
    // instance queue here; concurrent requests to different instances run
    // in parallel.
    std::lock_guard<std::mutex> perInstanceLock(*perInstanceMutex);

    // v0.9.4 Phase 2.5: lazy MCP `initialize` handshake. Spec-compliant
    // MCP servers (every Anthropic / community npx-installable server)
    // refuse session methods until the client has completed the
    // initialize -> notifications/initialized handshake. We perform it
    // here, transparently, on the first sendStdioJsonRpc call per
    // ChildProcess. The in-tree baseline-tools worker tolerates being
    // called without initialize, so its mcpInitialized flag also gets
    // flipped on first call but the handshake itself is just a no-op
    // exchange of valid envelopes -- no behavioral change for it.
    //
    // Concurrency: needsHandshake is read under mutex_ above, but the
    // handshake itself runs only inside perInstanceLock. We re-check
    // under perInstanceLock + briefly under mutex_ to handle the
    // (theoretical) race where two callers see needsHandshake=true
    // simultaneously; the second caller will find mcpInitialized
    // already true after acquiring perInstanceLock and skip.
    if (needsHandshake) {
        // Re-check under the supervisor mutex now that we have the
        // per-instance lock, in case another thread already
        // handshook this instance.
        bool stillNeeds = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = children_.find(instanceId);
            if (it == children_.end()) {
                result.errorMessage = "instance disappeared before handshake";
                return result;
            }
            stillNeeds = !it->second.mcpInitialized;
        }

        if (stillNeeds) {
            // Use a fresh request id for the handshake so we never
            // collide with the caller's id (which the dispatcher just
            // rewrote to bridgeRequestIdCounter_++ for this very
            // reason). We give the handshake a short, fixed deadline
            // (3s) -- if a child takes longer than that to respond
            // to `initialize`, it is wedged or unhealthy and the
            // caller's request would have failed anyway.
            const std::string handshakeId = std::string("\"mcos-handshake-")
                + instanceId + std::string("\"");
            const std::string initEnvelope = std::string(
                "{\"jsonrpc\":\"2.0\",\"id\":") + handshakeId + std::string(
                ",\"method\":\"initialize\",\"params\":"
                "{\"protocolVersion\":\"2025-03-26\","
                "\"capabilities\":{},"
                "\"clientInfo\":{\"name\":\"mcos-supervisor\",\"version\":\""
                MASTERCONTROL_VERSION
                "\"}}}\n");

            // Write the initialize envelope.
            {
                const char* cursor = initEnvelope.data();
                DWORD remaining = static_cast<DWORD>(initEnvelope.size());
                while (remaining > 0) {
                    DWORD written = 0;
                    if (!WriteFile(writeHandle, cursor, remaining, &written, nullptr) || written == 0) {
                        result.errorMessage = "WriteFile (initialize handshake) failed";
                        return result;
                    }
                    cursor += written;
                    remaining -= written;
                }
            }

            // Drain stdout for up to 3s, scanning for a line whose `id`
            // is our handshakeId. Discard everything else (banner
            // text from `npx` / `node`, server boot messages on
            // merged stderr, etc.). Reuses the same poll-read pattern
            // as the main loop below.
            {
                const auto handshakeDeadline =
                    std::chrono::steady_clock::now() + std::chrono::milliseconds(3000);
                std::vector<char> readChunk(4096);
                bool gotInitResponse = false;
                while (std::chrono::steady_clock::now() < handshakeDeadline && !gotInitResponse) {
                    DWORD bytesAvailable = 0;
                    if (!PeekNamedPipe(readHandle, nullptr, 0, nullptr, &bytesAvailable, nullptr)) {
                        result.errorMessage = "PeekNamedPipe failed during initialize handshake";
                        return result;
                    }
                    if (bytesAvailable > 0) {
                        const DWORD toRead = (bytesAvailable < (DWORD)readChunk.size())
                            ? bytesAvailable : (DWORD)readChunk.size();
                        DWORD readBytes = 0;
                        if (!ReadFile(readHandle, readChunk.data(), toRead, &readBytes, nullptr) || readBytes == 0) {
                            result.errorMessage = "ReadFile failed during initialize handshake";
                            return result;
                        }
                        std::lock_guard<std::mutex> lock(mutex_);
                        auto it = children_.find(instanceId);
                        if (it == children_.end()) {
                            result.errorMessage = "instance disappeared during handshake read";
                            return result;
                        }
                        it->second.stdioReadBuffer.append(readChunk.data(), readBytes);

                        // Consume complete lines, looking for our id.
                        auto& buf = it->second.stdioReadBuffer;
                        size_t consumedThrough = 0;
                        size_t lineStart = 0;
                        for (size_t i = 0; i < buf.size(); ++i) {
                            if (buf[i] == '\n') {
                                std::string line = buf.substr(lineStart, i - lineStart);
                                if (!line.empty() && line.back() == '\r') line.pop_back();
                                lineStart = i + 1;
                                consumedThrough = i + 1;
                                try {
                                    auto j = nlohmann::json::parse(line);
                                    if (j.contains("id") && j["id"].dump() == handshakeId) {
                                        gotInitResponse = true;
                                        break;
                                    }
                                } catch (...) {
                                    // Banner text or non-JSON -- skip.
                                }
                            }
                        }
                        if (consumedThrough > 0) {
                            buf.erase(0, consumedThrough);
                        }
                    } else {
                        std::this_thread::sleep_for(std::chrono::milliseconds(25));
                    }
                }
                if (!gotInitResponse) {
                    result.errorMessage = "initialize handshake timed out (no response in 3s)";
                    return result;
                }
            }

            // Send notifications/initialized (notification -- no id,
            // no response expected).
            {
                const std::string notifEnvelope =
                    "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}\n";
                const char* cursor = notifEnvelope.data();
                DWORD remaining = static_cast<DWORD>(notifEnvelope.size());
                while (remaining > 0) {
                    DWORD written = 0;
                    if (!WriteFile(writeHandle, cursor, remaining, &written, nullptr) || written == 0) {
                        result.errorMessage = "WriteFile (notifications/initialized) failed";
                        return result;
                    }
                    cursor += written;
                    remaining -= written;
                }
            }

            // Mark this instance as initialized so subsequent calls
            // skip the handshake.
            {
                std::lock_guard<std::mutex> lock(mutex_);
                auto it = children_.find(instanceId);
                if (it != children_.end()) {
                    it->second.mcpInitialized = true;
                }
            }
        }
    }

    // Phase 3: write request + '\n' to child stdin. WriteFile may make a
    // partial write on a full pipe, so loop until the whole envelope is
    // out the door. The supervisor child-stdin pipe is synchronous and
    // anonymous (CreatePipe), so WriteFile blocks until kernel buffer
    // space is available; that is acceptable here because we hold only
    // the per-instance mutex, not the supervisor mutex.
    std::string framed = request;
    if (framed.empty() || framed.back() != '\n') {
        framed.push_back('\n');
    }
    {
        const char* cursor = framed.data();
        DWORD remaining = static_cast<DWORD>(framed.size());
        while (remaining > 0) {
            DWORD written = 0;
            if (!WriteFile(writeHandle, cursor, remaining, &written, nullptr) || written == 0) {
                result.errorMessage = "WriteFile to child stdin failed (child likely exited)";
                // v0.9.5: mark the instance as Failed so the next
                // listPools/findPool call (which now reaps before
                // returning) sees correct state immediately, instead
                // of advertising the dead instance as Ready until a
                // separate reap pass runs. We also pull the
                // ChildProcess record out of children_ here so the
                // background reap doesn't have to enumerate again.
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    auto childIt = children_.find(instanceId);
                    if (childIt != children_.end()) {
                        ChildProcess& child = childIt->second;
                        if (child.childStdinWrite != nullptr) {
                            CloseHandle(child.childStdinWrite);
                            child.childStdinWrite = nullptr;
                        }
                        if (child.childStdoutRead != nullptr) {
                            CloseHandle(child.childStdoutRead);
                            child.childStdoutRead = nullptr;
                        }
                        if (child.jobObject != nullptr) {
                            CloseHandle(child.jobObject);
                            child.jobObject = nullptr;
                        }
                        if (child.processInfo.hProcess != nullptr) {
                            CloseHandle(child.processInfo.hProcess);
                            child.processInfo.hProcess = nullptr;
                        }
                        if (child.processInfo.hThread != nullptr) {
                            CloseHandle(child.processInfo.hThread);
                            child.processInfo.hThread = nullptr;
                        }
                        children_.erase(childIt);
                    }
                    for (auto& [poolId, pool] : pools_) {
                        for (auto it = pool.instances.begin(); it != pool.instances.end(); ++it) {
                            if (it->instanceId == instanceId) {
                                deathLog_.push_back(DeathLogEntry{
                                    poolId, instanceId,
                                    to_string(it->state), 0
                                });
                                pool.instances.erase(it);
                                pool.updatedAtUtc = timestampNowUtc();
                                break;
                            }
                        }
                    }
                }
                return result;
            }
            cursor += written;
            remaining -= written;
        }
    }

    // Phase 4: parse the desired id out of the JSON-RPC envelope so we
    // can match the response. Tolerant of whitespace; the wire format is
    // produced by NativeHttpSysGatewayAdapter so we know the exact shape,
    // but we still pull `id` defensively rather than assuming a position.
    std::string targetId;
    {
        try {
            const auto parsed = nlohmann::json::parse(request);
            if (parsed.contains("id")) {
                // id may be string, integer, or null (notification). For
                // notifications we don't expect a response -- skip the read
                // loop and return empty success.
                if (parsed["id"].is_null()) {
                    result.succeeded = true;
                    return result;
                }
                targetId = parsed["id"].dump();  // canonical form: "42" or "\"abc\""
            } else {
                result.succeeded = true;  // notification -- no response expected
                return result;
            }
        } catch (const std::exception& ex) {
            result.errorMessage = std::string("could not parse request envelope to extract id: ") + ex.what();
            return result;
        }
    }

    // Phase 5: read-with-deadline loop. Re-grab the supervisor mutex only
    // for the ChildProcess buffer mutation (so children_ isn't being
    // resized concurrently); release between sleeps.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    constexpr int kPollIntervalMs = 25;
    constexpr DWORD kReadChunkBytes = 4096;
    std::vector<char> readChunk(kReadChunkBytes);

    while (std::chrono::steady_clock::now() < deadline) {
        // Probe pipe for available bytes without blocking.
        DWORD bytesAvailable = 0;
        if (!PeekNamedPipe(readHandle, nullptr, 0, nullptr, &bytesAvailable, nullptr)) {
            const DWORD err = GetLastError();
            result.errorMessage = "PeekNamedPipe failed (child likely exited); GetLastError=" + std::to_string(err);
            return result;
        }
        if (bytesAvailable > 0) {
            const DWORD toRead = (bytesAvailable < kReadChunkBytes) ? bytesAvailable : kReadChunkBytes;
            DWORD readBytes = 0;
            if (!ReadFile(readHandle, readChunk.data(), toRead, &readBytes, nullptr) || readBytes == 0) {
                result.errorMessage = "ReadFile from child stdout failed";
                return result;
            }

            // Append to buffer under supervisor mutex (children_ may be
            // mutated by other threads; we need consistent access).
            {
                std::lock_guard<std::mutex> lock(mutex_);
                auto it = children_.find(instanceId);
                if (it == children_.end()) {
                    result.errorMessage = "instance disappeared while reading";
                    return result;
                }
                it->second.stdioReadBuffer.append(readChunk.data(), readBytes);
            }

            // Scan buffered lines for a JSON object whose `id` matches.
            // Drain consumed bytes from the buffer so subsequent calls
            // see only unconsumed input.
            std::string consumed;
            std::string remaining;
            std::string snapshot;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                auto it = children_.find(instanceId);
                if (it == children_.end()) {
                    result.errorMessage = "instance disappeared while scanning";
                    return result;
                }
                snapshot = it->second.stdioReadBuffer;
            }

            std::size_t pos = 0;
            std::size_t consumedThrough = 0;
            bool matched = false;
            while (pos < snapshot.size()) {
                const std::size_t newlineAt = snapshot.find('\n', pos);
                if (newlineAt == std::string::npos) {
                    break;  // partial line at tail; keep for next read
                }
                std::string line = snapshot.substr(pos, newlineAt - pos);
                pos = newlineAt + 1;

                // Skip empty lines and lines that do not look like JSON.
                bool jsonish = false;
                for (char c : line) {
                    if (!std::isspace(static_cast<unsigned char>(c))) {
                        jsonish = (c == '{' || c == '[');
                        break;
                    }
                }
                if (!jsonish) {
                    consumedThrough = pos;
                    continue;
                }

                try {
                    const auto envelope = nlohmann::json::parse(line);
                    if (envelope.is_object() && envelope.contains("id") && !envelope["id"].is_null()) {
                        if (envelope["id"].dump() == targetId) {
                            result.succeeded   = true;
                            result.responseBody = line;
                            matched = true;
                            consumedThrough = pos;
                            break;
                        }
                    }
                    // Not our reply; drop line, advance.
                    consumedThrough = pos;
                } catch (...) {
                    // Not parseable JSON; drop line, advance.
                    consumedThrough = pos;
                }
            }

            // Trim consumed prefix from the live buffer.
            if (consumedThrough > 0) {
                std::lock_guard<std::mutex> lock(mutex_);
                auto it = children_.find(instanceId);
                if (it != children_.end()) {
                    if (consumedThrough >= it->second.stdioReadBuffer.size()) {
                        it->second.stdioReadBuffer.clear();
                    } else {
                        it->second.stdioReadBuffer.erase(0, consumedThrough);
                    }
                }
            }

            if (matched) {
                return result;
            }
            // Fall through to continue polling -- response may arrive in
            // a subsequent ReadFile.
        } else {
            // No data; back off briefly and re-poll.
            std::this_thread::sleep_for(std::chrono::milliseconds(kPollIntervalMs));
        }
    }

    result.errorMessage = "stdio bridge timed out after " + std::to_string(timeoutMs) + " ms (no response with matching id=" + targetId + ")";
    return result;
#endif
}

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
        // v0.7.1: copy LAN-client identity verbatim from the request so the
        // dashboard's per-sub-agent client-attribution panel can show
        // "Recon: 192.168.1.42 (claude-code)" without the dashboard having
        // to maintain its own side index of who acquired what. Both fields
        // may be empty for legacy callers.
        lease.clientIpAddress = request.clientIpAddress;
        lease.clientType      = request.clientType;
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
        // v0.9.3: bracket IPv6 host literals so onboarding profile URLs
        // are RFC 3986-parseable. Pre-v0.9.3 governanceBundleUrl +
        // discoveryUrl came out as "http://fde3:c02c:...:32c8:7300/..."
        // which RFC 3986 parsers reject as "Invalid port specified."
        const std::string adminBase = "http://" + bracketIpv6Host(lanIp) + ":" + std::to_string(configuration.browserPort);

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
    // v0.9.11: interruptible sleep. Pre-v0.9.11 the worker called
    // std::this_thread::sleep_for(beaconBroadcastIntervalSeconds), so
    // stop() had to wait up to a full broadcast interval (15s by
    // default) before the worker observed running_=false and exited.
    // Service stop measured 14.8s on v0.9.10 because of this. The
    // condition variable lets stop() notify_all() and the worker
    // wakes immediately (wait_for returns), checks running_, and
    // exits within ms.
    std::mutex sleepMutex_;
    std::condition_variable sleepCv_;
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
                // v0.9.11: also use the condition-variable wait here
                // so stop() can interrupt the disabled-state poll
                // promptly.
                std::unique_lock<std::mutex> lock(sleepMutex_);
                sleepCv_.wait_for(lock, std::chrono::seconds(1),
                                  [this]() { return !running_.load(); });
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
            // v0.9.11: interruptible sleep. wait_for returns when
            // either the predicate is true (running_ flipped to
            // false) OR the timeout elapses, whichever comes first.
            // Pre-v0.9.11 a plain sleep_for blocked stop() until
            // the next broadcast interval ended.
            {
                std::unique_lock<std::mutex> lock(sleepMutex_);
                sleepCv_.wait_for(lock,
                    std::chrono::seconds(configuration.beaconBroadcastIntervalSeconds),
                    [this]() { return !running_.load(); });
            }
        }

        closesocket(socketHandle);
    });
}

void BeaconService::stop() {
    running_ = false;
    // v0.9.11: notify the sleeping worker so it observes
    // running_=false immediately rather than waiting for the next
    // broadcast interval. Notify under the mutex so there's no
    // missed-wakeup race (the worker's wait_for predicate
    // re-checks running_ after notification).
    {
        std::lock_guard<std::mutex> lock(sleepMutex_);
        sleepCv_.notify_all();
    }
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

    // v0.7.1: late-bind the worker supervisor + lease router so the
    // dashboard snapshot can compute per-sub-agent utilization and
    // per-pool active-client lists without restructuring AdminApiService's
    // construction order. Called once from MasterControlApplication
    // immediately after both the supervisor and lease router exist.
    void AttachWorkerLayer(std::shared_ptr<IWorkerSupervisor> workerSupervisor,
                           std::shared_ptr<ILeaseRouter> leaseRouter) {
        workerSupervisor_ = std::move(workerSupervisor);
        leaseRouter_ = std::move(leaseRouter);
    }

    // v0.9.15: start a background thread that keeps the reachability
    // cache warm. Every kReachabilityProberIntervalMs the thread walks
    // the current inventory and probes every (host, port) pair so the
    // cache is rebuilt before its TTL expires. With this in place the
    // operator dashboard never blocks on probes -- every snapshot is
    // a cache hit. Pre-v0.9.15 the v0.9.14 cache delivered ~36ms
    // dashboard latency in steady state but a periodic 6-20s spike
    // every 30s when the TTL expired and the next snapshot had to
    // re-sweep all probes synchronously. v0.9.15 eliminates the spike
    // by moving the probe work to a separate thread.
    void StartReachabilityProber() {
        if (reachabilityProberRunning_.exchange(true)) return;
        reachabilityProberThread_ = std::thread([this]() {
            // Initial settle so we don't start probing while inventory
            // is still hydrating during runtime construction.
            for (int slept = 0; slept < kReachabilityProberSettleMs; slept += 100) {
                if (!reachabilityProberRunning_.load(std::memory_order_acquire)) return;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            while (reachabilityProberRunning_.load(std::memory_order_acquire)) {
                // Walk inventory, probe each endpoint to refresh
                // its cache entry.
                // v0.9.16: walk the SAME endpoint union the
                // dashboard snapshot probes. Pre-v0.9.16 the prober
                // only walked inventoryService_->listEndpoints(),
                // missing platform gateways and governance servers
                // that the snapshot synthesizes inline (lines
                // ~10417 + ~10439). Those misses meant every
                // dashboard call had to do synchronous probes for
                // 5-13 seconds for the missing endpoints, even
                // when the prober was running. Now the prober
                // probes the union {inventory + platform gateways
                // + governance servers}.
                try {
                    auto probeOne = [this](const std::string& host, uint16_t port) {
                        if (!reachabilityProberRunning_.load(std::memory_order_acquire)) return;
                        if (host.empty() || port == 0) return;
                        std::string hostPortIgnored;
                        // v0.9.16: prober explicitly opts into the
                        // synchronous-probe path -- it has to actually
                        // populate the cache. Snapshot callers leave
                        // allowSynchronousProbe at the default false
                        // so they never block.
                        (void)probeReachabilityCached(host, port, hostPortIgnored,
                                                      /*allowSynchronousProbe=*/true);
                    };
                    if (inventoryService_) {
                        for (const auto& ep : inventoryService_->listEndpoints()) {
                            probeOne(ep.host, ep.port);
                        }
                    }
                    if (platformServiceCatalogService_) {
                        for (const auto& gateway : platformServiceCatalogService_->listGateways()) {
                            probeOne(gateway.ipAddress, gateway.port);
                        }
                        // Governance servers route through the gateway
                        // (snapshot uses the gateway's host:port for
                        // the governance entry too) so probing the
                        // gateway covers them; explicitly walking
                        // listGovernanceServers() would just re-probe
                        // the same host:port pairs from a different
                        // angle.
                    }
                } catch (...) {
                    // Inventory / catalog walk threw -- skip this
                    // cycle; try again next interval.
                }
                // Interruptible sleep symmetric with the supervisor
                // watchdog and BeaconService patterns.
                std::unique_lock<std::mutex> lock(reachabilityProberSleepMutex_);
                reachabilityProberSleepCv_.wait_for(lock,
                    std::chrono::milliseconds(kReachabilityProberIntervalMs),
                    [this]() { return !reachabilityProberRunning_.load(std::memory_order_acquire); });
            }
        });
    }

    void StopReachabilityProber() {
        if (!reachabilityProberRunning_.exchange(false)) return;
        {
            std::lock_guard<std::mutex> lock(reachabilityProberSleepMutex_);
            reachabilityProberSleepCv_.notify_all();
        }
        if (reachabilityProberThread_.joinable()) {
            reachabilityProberThread_.join();
        }
    }

    ~AdminApiService() override {
        StopReachabilityProber();
    }

private:
    // v0.9.14: per-endpoint reachability cache. Pre-v0.9.14 the
    // dashboard snapshot did a 200ms TCP-connect probe for EVERY
    // sub-agent and EVERY mcp-server endpoint serially -- with ~30
    // endpoints in a typical install that ran 6.6 seconds warm and
    // 20 seconds cold (live-probe at /api/dashboard). The shell
    // and browser dashboard both poll /api/dashboard, so the slow
    // snapshot blocked every refresh. The cache turns a re-probe
    // within kReachabilityCacheTtlSeconds into an O(1) map lookup,
    // cutting snapshot latency to <100ms in steady state. The
    // host:port string is the cache key. Probes still happen on
    // first call and on TTL expiry.
    // v0.9.14 follow-up: TTL must be larger than the worst-case time
    // to populate the entire cache from cold (one probe per endpoint
    // serially, ~6s for ~30 endpoints with 200ms per address). 5s
    // (the original) was shorter than that, so by the time a full
    // sweep completed the first entries' TTL had already expired and
    // the next snapshot re-probed everything. 30s is comfortably
    // longer than any plausible cold-sweep duration AND short enough
    // that operator-visible reachability changes (worker came back
    // online, network blip recovered) surface within half a minute.
    static constexpr int kReachabilityCacheTtlSeconds = 30;
    // v0.9.15: background prober interval. Every this-many-ms the
    // prober thread re-probes every known endpoint so the cache is
    // refreshed before its TTL expires. 10s is comfortably under
    // the 30s TTL so even a slow sweep (current worst observed: 6s)
    // finishes well within the cache validity window. The settle is
    // a one-time wait at thread start so we don't probe before the
    // inventory has finished hydrating during runtime construction.
    static constexpr int kReachabilityProberIntervalMs = 10000;
    static constexpr int kReachabilityProberSettleMs   = 2000;

    // v0.9.56: per-cache-entry failure diagnostic. Pre-v0.9.56 the
    // cache only remembered reachable=bool; the dashboard had no way
    // to surface WHY a probe failed, so operators saw a red dot with
    // empty status text and had to guess (firewall? wrong port? no
    // listener? DNS?). lastErrorCategory is a stable enum
    // ("connection_refused"/"timeout"/"dns_failed"/"socket_error"/"")
    // and lastErrorMessage is a human-readable line including the
    // host:port. The snapshot path mirrors these into the runtime
    // stats so the dashboard can render an actionable diagnostic.
    struct ReachabilityCacheEntry {
        bool reachable = false;
        std::chrono::steady_clock::time_point lastProbedAt;
        std::string lastErrorCategory;
        std::string lastErrorMessage;
        std::string lastErrorAtUtc;
    };
    mutable std::mutex reachabilityCacheMutex_;
    mutable std::map<std::string, ReachabilityCacheEntry> reachabilityCache_;

    // v0.9.56: lookup last-known reachability diagnostic for a
    // formatted host:port key. Returns nullopt when no probe has
    // happened yet so the snapshot path can decide whether to render
    // "no_endpoint_registered" vs "no_listener". Read-only; safe to
    // call from the snapshot thread.
    struct ReachabilityDiagnostic {
        bool reachable = false;
        std::string errorCategory;
        std::string errorMessage;
        std::string errorAtUtc;
    };
    std::optional<ReachabilityDiagnostic>
    lastReachabilityDiagnostic(const std::string& hostPort) const {
        std::lock_guard<std::mutex> lock(reachabilityCacheMutex_);
        auto it = reachabilityCache_.find(hostPort);
        if (it == reachabilityCache_.end()) return std::nullopt;
        ReachabilityDiagnostic out;
        out.reachable     = it->second.reachable;
        out.errorCategory = it->second.lastErrorCategory;
        out.errorMessage  = it->second.lastErrorMessage;
        out.errorAtUtc    = it->second.lastErrorAtUtc;
        return out;
    }

    // v0.9.60: detection of pre-installed npm MCP packages. Scans
    // candidate npm-root directories for canonical package paths
    // (entityId -> "C:\\...\\node_modules\\<package>") and caches
    // a yes/no per known entity id. Lazy: runs on first call to
    // packageDetected() and stored in detectedPackagesCache_.
    //
    // Why we infer the npm root from existing pool args first: the 4
    // already-pooled MCPs (filesystem / memory / sequential-thinking
    // / sqlite) carry an absolute path through node_modules, so the
    // operator's actual installation root is captured there. Falling
    // back to common locations (per-user APPDATA, C:\Program Files\
    // nodejs) covers fresh installs that don't have any pool yet.
    //
    // Returns true iff the canonical install path for the given
    // seeded entity id exists on disk. Returns false for unknown ids
    // or when no scan root could be derived.
    mutable std::mutex detectedPackagesMutex_;
    mutable bool detectedPackagesScanned_ = false;
    mutable std::map<std::string, bool> detectedPackagesCache_;

    bool packageDetected(const std::string& entityId) const {
        std::lock_guard<std::mutex> lock(detectedPackagesMutex_);
        if (!detectedPackagesScanned_) {
            performNpmPackageDetectionLocked();
            detectedPackagesScanned_ = true;
        }
        auto it = detectedPackagesCache_.find(entityId);
        return it != detectedPackagesCache_.end() && it->second;
    }

    // Caller must hold detectedPackagesMutex_.
    void performNpmPackageDetectionLocked() const {
        // entityId -> canonical package directory name under node_modules
        // ("@scope/name" or "name"). Only entries with a verified
        // canonical npm package; matches the kKnownInstallCommand
        // map in enrichRuntimeStatDiagnostics so the operator-facing
        // surfaces stay consistent.
        // v0.10.0: playwright dropped from the catalog and from this map.
        const std::vector<std::pair<std::string, std::string>> knownPackages = {
            { "chrome-devtools",     "chrome-devtools-mcp" },
            { "filesystem",          "@modelcontextprotocol/server-filesystem" },
            { "memory",              "@modelcontextprotocol/server-memory" },
            { "sequential-thinking", "@modelcontextprotocol/server-sequential-thinking" },
            { "sqlite",              "mcp-server-sqlite-npx" }
        };

        // Collect candidate npm roots. First, infer from any existing
        // pool whose args contain "node_modules"; that captures the
        // operator's actual install path. Then add common fallbacks.
        std::vector<std::filesystem::path> candidateRoots;
        if (workerSupervisor_) {
            try {
                for (const auto& pool : workerSupervisor_->listPools()) {
                    for (const auto& arg : pool.template_.args) {
                        const auto pos = arg.find("node_modules");
                        if (pos != std::string::npos) {
                            // arg looks like
                            //   <root>\node_modules\<scope>\<pkg>\dist\index.js
                            // We want the prefix ending right at
                            //   <root>\node_modules
                            const auto rootEnd = pos + std::string("node_modules").size();
                            candidateRoots.emplace_back(arg.substr(0, rootEnd));
                            break;
                        }
                    }
                }
            } catch (...) {
                // Supervisor walk threw -- fall through to fallback roots.
            }
        }
        // Fallback roots covering typical Windows npm globals. Use
        // Win32 GetEnvironmentVariableW (not std::getenv) to honor
        // the Windows-native rule (`.claude/rules/10-windows-native-cpp.md`)
        // and to avoid the CRT-deprecated getenv warning.
#if defined(_WIN32)
        {
            wchar_t appdataBuf[MAX_PATH * 2];
            const DWORD len = ::GetEnvironmentVariableW(L"APPDATA",
                                                        appdataBuf,
                                                        ARRAYSIZE(appdataBuf));
            if (len > 0 && len < ARRAYSIZE(appdataBuf)) {
                candidateRoots.emplace_back(
                    std::filesystem::path(std::wstring(appdataBuf, len))
                    / L"npm" / L"node_modules");
            }
        }
#endif
        candidateRoots.emplace_back(std::filesystem::path("C:/Program Files/nodejs/node_modules"));

        // Deduplicate paths (case-insensitive on Windows would be
        // ideal, but std::filesystem::path equality covers the common
        // case where the same string was added twice).
        std::set<std::filesystem::path> uniqueRoots(candidateRoots.begin(), candidateRoots.end());

        for (const auto& [entityId, packageName] : knownPackages) {
            bool found = false;
            for (const auto& root : uniqueRoots) {
                std::error_code ec;
                if (std::filesystem::exists(root / packageName, ec)) {
                    found = true;
                    break;
                }
            }
            detectedPackagesCache_[entityId] = found;
        }
    }

    // v0.9.56: classify the install-state for a runtime stat entry
    // and synthesize an actionable operator hint. Stat T must have
    // .reachable, .poolId, .endpointHostPort, .readyInstanceCount,
    // .unavailableReason, .lastErrorMessage, .lastErrorAtUtc,
    // .installState, .installHint -- both SubAgentRuntimeStat and
    // McpServerRuntimeStat satisfy that shape. Pre-v0.9.56 these
    // five fields didn't exist, so the dashboard had no way to tell
    // operators (a) whether an entry was a stdio-supervised pool
    // wrap, an admin-port helper, or a placeholder waiting for a
    // pool, and (b) why a TCP probe was failing. The classification
    // is conservative: reachable=true + ready instances => install
    // state "installed_and_supervised"; reachable=true + admin port
    // => "online_via_admin_port"; otherwise the seeded catalog
    // entry is treated as a placeholder needing a pool.
    template <typename Stat>
    void enrichRuntimeStatDiagnostics(Stat& stat,
                                      const std::string& entityId,
                                      const std::string& kind, // "sub-agent" or "MCP server"
                                      uint16_t adminPort,
                                      bool hasManagedPool,
                                      uint16_t seededPort,
                                      const std::string& seededHost) const {
        // Resolve diagnostic from the cache by reformatting the
        // probe key, since stats may have rewritten endpointHostPort
        // to "stdio (supervised pool: ...)" by the time we run.
        std::optional<ReachabilityDiagnostic> diag;
        if (!seededHost.empty() && seededPort != 0) {
            const bool isIPv6Literal = seededHost.find(':') != std::string::npos;
            const std::string probeKey = isIPv6Literal
                ? "[" + seededHost + "]:" + std::to_string(seededPort)
                : seededHost + ":" + std::to_string(seededPort);
            diag = lastReachabilityDiagnostic(probeKey);
        }
        if (diag.has_value()) {
            stat.lastErrorMessage = diag->errorMessage;
            stat.lastErrorAtUtc   = diag->errorAtUtc;
        }

        // Path 1: stdio-supervised pool with ready instances. The
        // v0.9.25 truth-wins fix already set reachable=true and
        // rewrote endpointHostPort to "stdio (supervised pool: N
        // ready instance)". Mark installState accordingly and clear
        // the unavailableReason.
        if (hasManagedPool && stat.readyInstanceCount > 0 && stat.reachable) {
            stat.installState      = "installed_and_supervised";
            stat.unavailableReason = "";
            stat.installHint       = "Healthy. Pool '" + stat.poolId
                                   + "' supervises " + std::to_string(stat.readyInstanceCount)
                                   + " ready instance(s).";
            // Clear the legacy probe error -- this entry is
            // explicitly NOT a TCP-listener entry. Pre-v0.9.56 the
            // cached "connection_refused" message lingered even on
            // healthy stdio pools because the cache key was the
            // seeded TCP host:port that we never bind to.
            stat.lastErrorMessage = "";
            stat.lastErrorAtUtc   = "";
            return;
        }

        // Path 2: pool exists but no ready instances yet (cold or
        // crashed). reachable can be true via stdio supervision but
        // readyInstanceCount=0 means leases will fail.
        if (hasManagedPool && stat.readyInstanceCount == 0) {
            stat.installState      = "supervised_pool_not_ready";
            stat.unavailableReason = "supervised_pool_not_ready";
            stat.installHint       = "Pool '" + stat.poolId
                                   + "' is registered but no instances are Ready. "
                                   + "Inspect supervisor logs (Job Object exit codes) for the worker process.";
            if (stat.lastErrorMessage.empty()) {
                stat.lastErrorMessage = "Supervised pool has 0 ready instances.";
            }
            return;
        }

        // Path 3: reachable=true with no managed pool. The probe
        // succeeded against a TCP listener (e.g. governance servers
        // reporting at the admin port). Render as online but clearly
        // not pool-managed so operators can tell it apart from a
        // supervised stdio pool.
        if (stat.reachable && !hasManagedPool) {
            const bool servedFromAdmin = (seededPort == adminPort);
            stat.installState      = servedFromAdmin
                ? "online_via_admin_port"
                : "online_via_external_listener";
            stat.unavailableReason = "";
            stat.installHint       = servedFromAdmin
                ? "Served by the MCOS admin port (" + std::to_string(adminPort)
                  + "). For supervised lifecycle, register a pool: "
                  "POST /api/pools with poolId=\"" + entityId + "\"."
                : "Reachable at " + stat.endpointHostPort
                  + " but not under MCOS supervision. "
                  "Register a pool: POST /api/pools with poolId=\"" + entityId + "\".";
            stat.lastErrorMessage = "";
            stat.lastErrorAtUtc   = "";
            return;
        }

        // Path 4: not reachable, no pool. This is the fake-telemetry
        // case the v0.9.56 diagnostic surface was added to expose.
        // Use the cached probe failure category to set
        // unavailableReason; if no probe has happened yet
        // (cache empty) fall back to "no_endpoint_registered" if
        // there's no host/port at all, otherwise "no_listener".
        stat.installState = "awaiting_pool_registration";
        if (diag.has_value() && !diag->errorCategory.empty()) {
            stat.unavailableReason = diag->errorCategory;
        } else if (seededHost.empty() || seededPort == 0) {
            stat.unavailableReason = "no_endpoint_registered";
        } else {
            stat.unavailableReason = "no_listener";
        }

        // v0.9.58: surface a known canonical install command for
        // entries that map to a public npm MCP package. Pre-v0.9.58
        // the installHint just said "register a managed pool" with
        // no install path; operators couldn't tell apart "this MCP
        // has a published package, just install it" from "this is
        // an opaque catalog name with no obvious vendor." The map
        // is conservative -- only entries with a verified canonical
        // npm package are populated. Empty string means "no known
        // canonical install path; operator authors the pool template
        // by hand." Detection of whether the package is already
        // installed is deferred (see installPackageDetected default).
        // v0.10.0: playwright removed from the install-hint map alongside
        // its catalog removal.
        static const std::map<std::string, std::string> kKnownInstallCommand = {
            { "chrome-devtools",            "npm install -g chrome-devtools-mcp" },
            { "filesystem",                 "npm install -g @modelcontextprotocol/server-filesystem" },
            { "memory",                     "npm install -g @modelcontextprotocol/server-memory" },
            { "sequential-thinking",        "npm install -g @modelcontextprotocol/server-sequential-thinking" },
            { "sqlite",                     "npm install -g mcp-server-sqlite-npx" },
        };
        const auto installIt = kKnownInstallCommand.find(entityId);
        if (installIt != kKnownInstallCommand.end()) {
            stat.installCommand = installIt->second;
        }

        // v0.9.60: detect whether the canonical npm package is
        // already installed on disk. When detected, the install hint
        // changes from "install + register" to "register" so the
        // operator knows they can skip the npm step. The detection
        // result is cached on first call so we don't re-walk node_
        // modules on every snapshot.
        if (!stat.installCommand.empty()) {
            stat.installPackageDetected = packageDetected(entityId);
        }

        std::string installSuffix;
        if (!stat.installCommand.empty()) {
            if (stat.installPackageDetected) {
                installSuffix = " Canonical npm package detected on disk; skip the install and register a pool.";
            } else {
                installSuffix = " Known install command: `" + stat.installCommand + "`.";
            }
        }
        stat.installHint = std::string("Catalog placeholder for ") + kind
                         + " '" + entityId + "'. No process listens on "
                         + (seededPort == 0
                              ? std::string("the registered endpoint")
                              : seededHost + ":" + std::to_string(seededPort))
                         + ". Register a managed pool to make this entry live: "
                           "POST /api/pools with poolId=\"" + entityId + "\" "
                           "and an executable template."
                         + installSuffix;
        if (stat.lastErrorMessage.empty()) {
            // Synthesize a message even if the prober hasn't run yet
            // so the dashboard has SOMETHING to render in the
            // 30-second window before the first sweep completes.
            stat.lastErrorMessage = "No probe result yet for "
                + (seededPort == 0
                    ? std::string("(no endpoint)")
                    : seededHost + ":" + std::to_string(seededPort))
                + ". Background prober runs every "
                + std::to_string(kReachabilityProberIntervalMs / 1000)
                + "s.";
        }
    }

    // v0.9.15: background prober.
    std::thread reachabilityProberThread_;
    std::atomic<bool> reachabilityProberRunning_{ false };
    std::mutex reachabilityProberSleepMutex_;
    std::condition_variable reachabilityProberSleepCv_;

    // Probe (or cache-hit) the reachability of a host:port pair.
    // outHostPort is the operator-display string ("host:port" or
    // "[ipv6]:port"). Returns true iff the probe (or cached probe)
    // succeeded.
    //
    // v0.9.16 follow-up: the snapshot caller path is non-blocking
    // when the background prober is alive -- a cache miss returns
    // the stale value (or false if no prior probe has happened) and
    // the prober is responsible for keeping the cache fresh. Pre-
    // v0.9.16 a stale entry triggered a synchronous re-probe which
    // blocked the dashboard for up to N×200ms per missing endpoint
    // (5-19s observed with ~30 endpoints). Since the prober now
    // covers the same endpoint set the snapshot probes, every
    // endpoint should be reliably cached after the first sweep
    // completes. The dashboard never blocks again.
    //
    // The synchronous-probe-on-miss path is only taken when the
    // prober is NOT running (test mode, supervised-mock paths).
    bool probeReachabilityCached(const std::string& host,
                                 uint16_t port,
                                 std::string& outHostPort,
                                 bool allowSynchronousProbe = false) const {
        if (host.empty() || port == 0) {
            outHostPort.clear();
            return false;
        }
        const bool isIPv6Literal = host.find(':') != std::string::npos;
        if (isIPv6Literal) {
            outHostPort = "[" + host + "]:" + std::to_string(port);
        } else {
            outHostPort = host + ":" + std::to_string(port);
        }

        // Cache check.
        const auto now = std::chrono::steady_clock::now();
        bool cacheHasEntry = false;
        bool cachedValue = false;
        {
            std::lock_guard<std::mutex> lock(reachabilityCacheMutex_);
            auto it = reachabilityCache_.find(outHostPort);
            if (it != reachabilityCache_.end()) {
                cacheHasEntry = true;
                cachedValue = it->second.reachable;
                if ((now - it->second.lastProbedAt)
                       < std::chrono::seconds(kReachabilityCacheTtlSeconds)) {
                    return it->second.reachable;
                }
            }
        }

        // v0.9.16: stale-or-empty + caller doesn't allow synchronous
        // probe (i.e., this is the snapshot path, not the prober) +
        // prober is alive -> return last-known-good (or false)
        // WITHOUT blocking. The prober will refresh the entry on its
        // next sweep. Snapshots are always O(map lookups) regardless
        // of TTL expiry, eliminating the v0.9.15 periodic spike.
        //
        // Pre-v0.9.16 stale entries triggered a synchronous re-probe
        // which blocked the dashboard for up to N x 200ms per
        // missing endpoint (5-19s observed). The prober now covers
        // the same endpoint set the snapshot probes, so every
        // endpoint is reliably cached after the first sweep
        // completes; the snapshot can return immediately.
        if (!allowSynchronousProbe
            && reachabilityProberRunning_.load(std::memory_order_acquire)) {
            return cacheHasEntry ? cachedValue : false;
        }

        // Cache miss: actually probe. Logic is identical to the
        // v0.7.6 / v0.8.2 / v0.9.x in-line probe -- non-blocking
        // TCP connect with a 200ms select timeout per resolved
        // address. v0.9.56 extends it to also capture the failure
        // category (dns_failed / connection_refused / timeout /
        // socket_error) so the snapshot can render an actionable
        // diagnostic instead of an empty status string.
        bool reachable = false;
        std::string errorCategory;
        std::string errorMessage;
#if defined(_WIN32)
        addrinfo hints{};
        hints.ai_family   = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        addrinfo* resolved = nullptr;
        const std::string portStr = std::to_string(port);
        const int gaiResult = ::getaddrinfo(host.c_str(),
                                            portStr.c_str(),
                                            &hints, &resolved);
        if (gaiResult != 0 || resolved == nullptr) {
            errorCategory = "dns_failed";
            errorMessage  = "DNS resolution failed for " + outHostPort
                          + " (getaddrinfo=" + std::to_string(gaiResult) + ").";
        } else {
            int lastSockErr = 0;
            bool anyTimeout = false;
            for (addrinfo* it = resolved; it != nullptr && !reachable; it = it->ai_next) {
                SOCKET s = ::socket(it->ai_family, it->ai_socktype, it->ai_protocol);
                if (s == INVALID_SOCKET) continue;
                u_long nb = 1;
                ::ioctlsocket(s, FIONBIO, &nb);
                const int connectResult = ::connect(s, it->ai_addr, (int)it->ai_addrlen);
                if (connectResult == 0) {
                    reachable = true;
                } else {
                    const int wsaErr = WSAGetLastError();
                    if (wsaErr == WSAEWOULDBLOCK) {
                        fd_set writeSet{};
                        FD_ZERO(&writeSet);
                        FD_SET(s, &writeSet);
                        fd_set errorSet{};
                        FD_ZERO(&errorSet);
                        FD_SET(s, &errorSet);
                        timeval tv{};
                        tv.tv_sec = 0;
                        tv.tv_usec = 200 * 1000; // 200ms
                        const int sel = ::select(0, nullptr, &writeSet, &errorSet, &tv);
                        if (sel == 0) {
                            anyTimeout = true;
                        } else if (sel > 0 && FD_ISSET(s, &writeSet)) {
                            int sockErr = 0;
                            int sockErrLen = sizeof(sockErr);
                            ::getsockopt(s, SOL_SOCKET, SO_ERROR,
                                         reinterpret_cast<char*>(&sockErr), &sockErrLen);
                            if (sockErr == 0) {
                                reachable = true;
                            } else {
                                lastSockErr = sockErr;
                            }
                        } else {
                            lastSockErr = WSAGetLastError();
                        }
                    } else {
                        lastSockErr = wsaErr;
                    }
                }
                ::closesocket(s);
            }
            ::freeaddrinfo(resolved);
            if (!reachable) {
                if (lastSockErr == WSAECONNREFUSED) {
                    errorCategory = "connection_refused";
                    errorMessage  = "TCP connect refused at " + outHostPort
                                  + " - no process listening on the port.";
                } else if (lastSockErr == WSAEHOSTUNREACH || lastSockErr == WSAENETUNREACH) {
                    errorCategory = "host_unreachable";
                    errorMessage  = "Host unreachable for " + outHostPort
                                  + " (WSA error " + std::to_string(lastSockErr) + ").";
                } else if (anyTimeout && lastSockErr == 0) {
                    errorCategory = "timeout";
                    errorMessage  = "TCP connect to " + outHostPort
                                  + " timed out after 200ms - target may be firewalled or down.";
                } else if (lastSockErr != 0) {
                    errorCategory = "socket_error";
                    errorMessage  = "Socket error connecting to " + outHostPort
                                  + " (WSA error " + std::to_string(lastSockErr) + ").";
                } else {
                    errorCategory = "no_listener";
                    errorMessage  = "No TCP listener answered at " + outHostPort + ".";
                }
            }
        }
#endif

        // Cache the result, including v0.9.56 diagnostic fields.
        const std::string nowUtc = reachable ? std::string() : timestampNowUtc();
        {
            std::lock_guard<std::mutex> lock(reachabilityCacheMutex_);
            ReachabilityCacheEntry entry;
            entry.reachable          = reachable;
            entry.lastProbedAt       = now;
            entry.lastErrorCategory  = errorCategory;
            entry.lastErrorMessage   = errorMessage;
            entry.lastErrorAtUtc     = nowUtc;
            reachabilityCache_[outHostPort] = std::move(entry);
        }
        return reachable;
    }

public:

    DashboardSnapshot snapshot() override {
        // v0.9.16: refreshAsync instead of refresh. Pre-v0.9.16 this
        // call did synchronous 400ms-per-endpoint TCP probes inside
        // RuntimeInventoryService::probeEndpoint -- with ~30
        // endpoints that's 12 seconds of blocking on the dashboard
        // path. The contract comment on refreshAsync literally
        // mentions "10+ seconds" -- it was added in mutating handlers
        // for exactly this reason but never adopted in snapshot.
        // refreshAsync fires the probe loop on a detached background
        // thread and returns immediately; the snapshot then reads
        // the inventory state as it currently is (might be slightly
        // stale on the very first call after a configuration mutation,
        // but never blocks). The reachability cache + background
        // prober handle the dashboard's reachability needs
        // independently of the inventory's own probes.
        inventoryService_->refreshAsync();

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
        // v0.7.1: per-sub-agent runtime stats. For each registered SubAgent
        // endpoint, we look for a managed pool whose poolId matches the
        // sub-agent's id (operators wire this with POST /api/pools using
        // poolId == subAgentId). We then derive utilization% from active
        // leases / capacity, and harvest the LAN client identity (IP +
        // clientType + sessionId) from each active lease so the operator
        // can see which client is using which sub-agent in real time.
        // Sub-agents without a matching pool report utilizationPercent =
        // -1.0 (ADR-002 §9 honest-unavailable sentinel) and the dashboard
        // renders that as "no managed pool — register one to enable
        // autoscale + utilization".
        if (workerSupervisor_) {
            const auto pools = workerSupervisor_->listPools();
            std::map<std::string, ManagedEndpointPool> poolById;
            for (const auto& pool : pools) {
                poolById.emplace(pool.poolId, pool);
            }
            // v0.9.56: pull the admin port once so the diagnostic
            // enrichment helper can classify "online_via_admin_port"
            // entries (governance servers reuse the admin port).
            const uint16_t diagAdminPort =
                configurationService_ ? configurationService_->current().browserPort : 0;
            for (const auto& endpoint : snapshot.endpoints) {
                if (endpoint.kind != EndpointKind::SubAgent) {
                    continue;
                }
                SubAgentRuntimeStat stat;
                stat.subAgentId     = endpoint.id;
                stat.displayName    = endpoint.displayName;
                stat.specialization = endpoint.specialization;
                stat.status         = to_string(endpoint.status);

                // v0.9.14: reachability probe is now routed through
                // probeReachabilityCached so a 30-endpoint dashboard
                // snapshot can serve from the cache (kReachabilityCache
                // TtlSeconds=5) instead of doing 30 serial 200ms TCP
                // probes (~6 seconds wall time). Pre-v0.9.14 inline
                // probe block was 60+ lines of WinSock; now it's one
                // call. The cache key is host:port (formatted with
                // RFC 3986 IPv6 brackets per v0.8.2).
                if (!endpoint.host.empty() && endpoint.port != 0) {
                    stat.reachable =
                        probeReachabilityCached(endpoint.host,
                                                endpoint.port,
                                                stat.endpointHostPort);
                    stat.lastProbedAtUtc = timestampNowUtc();
                }

                const auto poolIt = poolById.find(endpoint.id);
                if (poolIt == poolById.end()) {
                    // v0.7.6: no managed pool yet, but we still have
                    // reachability + endpoint info above. Render with 0%
                    // utilization so the card shows a real graphic instead
                    // of "unavailable". The leaseCapacity stays 0 so the
                    // operator sees that no leases are routed yet.
                    stat.utilizationPercent = 0.0;
                    // v0.9.56: even without a pool, classify install
                    // state + capture probe diagnostic so the dashboard
                    // can render an actionable hint instead of an empty
                    // status field.
                    enrichRuntimeStatDiagnostics(stat, endpoint.id, "sub-agent",
                                                 diagAdminPort,
                                                 /*hasManagedPool=*/false,
                                                 endpoint.port, endpoint.host);
                    snapshot.subAgentRuntimeStats.push_back(std::move(stat));
                    continue;
                }
                const auto& pool = poolIt->second;
                stat.poolId               = pool.poolId;
                stat.totalInstanceCount   = static_cast<int>(pool.instances.size());
                stat.maxInstancesAllowed  = pool.scalePolicy.maxInstances;
                stat.autoscaleEnabled     = pool.scalePolicy.maxInstances > pool.scalePolicy.minInstances;

                int readyCount = 0;
                for (const auto& instance : pool.instances) {
                    if (instance.state == EndpointInstanceState::Ready) {
                        ++readyCount;
                    }
                }
                stat.readyInstanceCount = readyCount;

                const int perInstanceCap = (pool.scalePolicy.maxActiveLeasesPerInstance > 0)
                    ? pool.scalePolicy.maxActiveLeasesPerInstance
                    : 1;
                stat.leaseCapacity = readyCount * perInstanceCap;

                if (leaseRouter_) {
                    const auto leases = leaseRouter_->activeLeases(pool.poolId);
                    stat.activeLeaseCount = static_cast<int>(leases.size());
                    for (const auto& lease : leases) {
                        SubAgentLeaseHolder holder;
                        holder.ipAddress     = lease.clientIpAddress;
                        holder.clientType    = lease.clientType;
                        holder.sessionId     = lease.sessionId;
                        holder.acquiredAtUtc = lease.acquiredAtUtc;
                        stat.activeClients.push_back(std::move(holder));
                    }
                }

                if (stat.leaseCapacity > 0) {
                    stat.utilizationPercent = (static_cast<double>(stat.activeLeaseCount)
                        / static_cast<double>(stat.leaseCapacity)) * 100.0;
                    if (stat.utilizationPercent > 100.0) {
                        stat.utilizationPercent = 100.0;
                    }
                } else {
                    // Pool exists but no Ready instances yet -- treat as
                    // 0% (cold) rather than -1.0 (unconfigured).
                    stat.utilizationPercent = 0.0;
                }
                // v0.9.25: when a sub-agent has a supervised pool with
                // at least one Ready instance, override reach=true.
                // Pre-v0.9.25 the dashboard reported reach=false based
                // solely on the legacy TCP probe to the inventory's
                // host:port, which is meaningless for stdio-supervised
                // pools (workers run as MCOS children, not TCP
                // listeners on the inventory's port). The result was
                // an operator looking at the dashboard seeing red
                // dots for 'filesystem', 'memory', etc. while
                // tools/list at the gateway happily aggregated their
                // tools. Now the supervised-pool truth wins.
                if (readyCount > 0) {
                    stat.reachable = true;
                    stat.endpointHostPort = "stdio (supervised pool: "
                        + std::to_string(readyCount) + " ready instance"
                        + (readyCount == 1 ? "" : "s") + ")";
                    stat.lastProbedAtUtc = timestampNowUtc();
                }
                // v0.9.56: classify install state for the pool-wrapped
                // sub-agent. This populates installState=
                // "installed_and_supervised" when readyCount>0 and
                // "supervised_pool_not_ready" when the pool exists
                // but no instances are Ready, so operators can tell
                // a healthy stdio pool from a stuck one.
                enrichRuntimeStatDiagnostics(stat, endpoint.id, "sub-agent",
                                             diagAdminPort,
                                             /*hasManagedPool=*/true,
                                             endpoint.port, endpoint.host);
                snapshot.subAgentRuntimeStats.push_back(std::move(stat));
            }

            // v0.8.3: parallel loop for MCP servers. Same probe + pool
            // lookup pipeline as sub-agents above. Operator pointed out
            // that 17+ MCP servers in the inventory had no card surface
            // -- only sub-agents had cards with utilization / status /
            // active-clients. v0.8.3 closes that gap by emitting the
            // same telemetry shape (mcpServerRuntimeStats) so the
            // browser dashboard and the WinUI shell can render an MCP
            // Servers card grid mirror of the Sub-Agents card grid.
            for (const auto& endpoint : snapshot.endpoints) {
                if (endpoint.kind != EndpointKind::MCPServer) {
                    continue;
                }
                McpServerRuntimeStat stat;
                stat.mcpServerId    = endpoint.id;
                stat.displayName    = endpoint.displayName;
                stat.specialization = endpoint.specialization;
                stat.status         = to_string(endpoint.status);

                // v0.9.14: routed through the cached reachability
                // helper. Same probe semantics as the sub-agent
                // path above; same cache; same TTL. Drops the
                // mcp-server portion of the snapshot from O(N
                // probes) to O(N map lookups) on every refresh
                // within the TTL window.
                if (!endpoint.host.empty() && endpoint.port != 0) {
                    stat.reachable =
                        probeReachabilityCached(endpoint.host,
                                                endpoint.port,
                                                stat.endpointHostPort);
                    stat.lastProbedAtUtc = timestampNowUtc();
                }

                const auto poolIt = poolById.find(endpoint.id);
                if (poolIt == poolById.end()) {
                    stat.utilizationPercent = 0.0;
                    // v0.9.56: same diagnostic enrichment as the
                    // sub-agent path above. Catalog placeholders
                    // (computer-use, desktop-control, chrome-devtools,
                    // etc.) hit this branch and need
                    // installState="awaiting_pool_registration"
                    // plus a probe-failure category in
                    // unavailableReason.
                    enrichRuntimeStatDiagnostics(stat, endpoint.id, "MCP server",
                                                 diagAdminPort,
                                                 /*hasManagedPool=*/false,
                                                 endpoint.port, endpoint.host);
                    snapshot.mcpServerRuntimeStats.push_back(std::move(stat));
                    continue;
                }
                const auto& pool = poolIt->second;
                stat.poolId              = pool.poolId;
                stat.totalInstanceCount  = static_cast<int>(pool.instances.size());
                stat.maxInstancesAllowed = pool.scalePolicy.maxInstances;
                stat.autoscaleEnabled    = pool.scalePolicy.maxInstances > pool.scalePolicy.minInstances;

                int readyCount = 0;
                for (const auto& instance : pool.instances) {
                    if (instance.state == EndpointInstanceState::Ready) {
                        ++readyCount;
                    }
                }
                stat.readyInstanceCount = readyCount;

                const int perInstanceCap = (pool.scalePolicy.maxActiveLeasesPerInstance > 0)
                    ? pool.scalePolicy.maxActiveLeasesPerInstance
                    : 1;
                stat.leaseCapacity = readyCount * perInstanceCap;

                if (leaseRouter_) {
                    const auto leases = leaseRouter_->activeLeases(pool.poolId);
                    stat.activeLeaseCount = static_cast<int>(leases.size());
                    for (const auto& lease : leases) {
                        McpServerLeaseHolder holder;
                        holder.ipAddress     = lease.clientIpAddress;
                        holder.clientType    = lease.clientType;
                        holder.sessionId     = lease.sessionId;
                        holder.acquiredAtUtc = lease.acquiredAtUtc;
                        stat.activeClients.push_back(std::move(holder));
                    }
                }

                if (stat.leaseCapacity > 0) {
                    stat.utilizationPercent = (static_cast<double>(stat.activeLeaseCount)
                        / static_cast<double>(stat.leaseCapacity)) * 100.0;
                    if (stat.utilizationPercent > 100.0) {
                        stat.utilizationPercent = 100.0;
                    }
                } else {
                    stat.utilizationPercent = 0.0;
                }
                // v0.9.25: same supervised-pool truth-wins fix as
                // the sub-agent path above. With this in place a
                // dashboard query against a healthy 5-pool
                // deployment shows reach=true for the pool-wrapped
                // mcp-servers (filesystem, memory, sequential-
                // thinking, sqlite, baseline-tools) instead of red
                // dots from the meaningless-for-stdio TCP probe.
                if (readyCount > 0) {
                    stat.reachable = true;
                    stat.endpointHostPort = "stdio (supervised pool: "
                        + std::to_string(readyCount) + " ready instance"
                        + (readyCount == 1 ? "" : "s") + ")";
                    stat.lastProbedAtUtc = timestampNowUtc();
                }
                // v0.9.56: same diagnostic enrichment as the sub-agent
                // pool-wrapped branch. Healthy pools (filesystem,
                // memory, sequential-thinking, sqlite, baseline-tools)
                // get installState="installed_and_supervised"; a pool
                // that registered but has 0 ready instances gets
                // "supervised_pool_not_ready" with an inspect-the-
                // supervisor-logs hint.
                enrichRuntimeStatDiagnostics(stat, endpoint.id, "MCP server",
                                             diagAdminPort,
                                             /*hasManagedPool=*/true,
                                             endpoint.port, endpoint.host);
                snapshot.mcpServerRuntimeStats.push_back(std::move(stat));
            }
        }

        // v0.10.5: throttle dashboard-snapshot writes to once per
        // 60 seconds. Pre-v0.10.5 every call to snapshot() (which
        // happens on every shell + browser poll, ~1 Hz combined)
        // wrote a full telemetry row to disk. Live measurement on
        // this host: telemetry.jsonl grew to 24 MB after ~27 hours
        // of continuous running and would have crossed 1 GB inside
        // 6 weeks with no rotation policy. Throttling to 60-second
        // intervals cuts growth ~60x to ~350 KB/day while still
        // giving the operator a per-minute trend. Other event types
        // (self-test, supervisor lifecycle, etc.) are unaffected
        // because they aren't snapshot-rate.
        {
            using clock = std::chrono::steady_clock;
            static std::atomic<int64_t> lastDashboardSnapshotEpoch{ 0 };
            const auto nowEpoch = std::chrono::duration_cast<std::chrono::seconds>(
                clock::now().time_since_epoch()).count();
            int64_t prev = lastDashboardSnapshotEpoch.load(std::memory_order_relaxed);
            constexpr int64_t kIntervalSeconds = 60;
            if (prev == 0 || (nowEpoch - prev) >= kIntervalSeconds) {
                if (lastDashboardSnapshotEpoch.compare_exchange_strong(prev, nowEpoch,
                        std::memory_order_acq_rel)) {
                    MasterControl::Diagnostics::appendTelemetry(
                        L"runtime",
                        "dashboard-snapshot",
                        nlohmann::json{
                            { "hostName", snapshot.telemetry.hostName },
                            { "primaryIpAddress", snapshot.telemetry.primaryIpAddress },
                            { "cpuPercent", snapshot.telemetry.cpuPercent },
                            { "endpoints", snapshot.endpoints.size() },
                            { "subAgentStats", snapshot.subAgentRuntimeStats.size() },
                            { "mcpServerStats", snapshot.mcpServerRuntimeStats.size() }
                        });
                }
            }
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
    // v0.7.1: late-bound via AttachWorkerLayer once the worker supervisor
    // and lease router are constructed. Used only by snapshot() to compute
    // per-sub-agent utilization and client attribution. nullptr is fine --
    // snapshot() degrades gracefully and reports utilizationPercent = -1.0.
    std::shared_ptr<IWorkerSupervisor> workerSupervisor_;
    std::shared_ptr<ILeaseRouter> leaseRouter_;
};

struct HttpRequest final {
    std::string method;
    // v0.9.50: `path` is the URI path WITHOUT the query string. Pre-
    // v0.9.50 parseRequest stored the full target verbatim ("/api/
    // dashboard?cache=bust") and route handlers used strict equality
    // (request.path == "/api/dashboard"); a probe with any query string
    // therefore failed to match the route and fell through to the
    // v0.9.28 supportedMethodsForPath logic, which DID strip the query
    // for its lookup -- so the path appeared "known" but the request
    // appeared "method not allowed" and the client got 405 for an
    // ordinary cache-bust GET. Splitting at parse time keeps every
    // route comparison correct and leaves the query string available
    // via the new `query` member for routes that need it (/api/activity
    // since=, /api/telemetry/events max=).
    std::string path;
    std::string query; // v0.9.50: portion after '?', no leading '?'.
    std::unordered_map<std::string, std::string> headers;
    std::string body;
};

struct HttpResponse final {
    int statusCode = 200;
    std::string contentType = "application/json";
    std::string body;
    // v0.9.26: when true, sendResponse emits headers + Content-
    // Length matching the body size, but skips writing the body
    // bytes themselves. Set by SimpleHttpServer's HEAD-rewrite
    // path so HEAD requests get GET-equivalent headers without
    // the payload (RFC 7231 §4.3.2).
    bool suppressBody = false;
    // v0.9.28: arbitrary response headers emitted after the
    // standard Content-Type / Content-Length / CORS block. Used
    // for the Allow: header on 405 Method Not Allowed responses
    // (RFC 7231 §6.5.5) and forward-compat for Retry-After,
    // Location, ETag, etc. when later routes need them.
    std::vector<std::pair<std::string, std::string>> extraHeaders;
    // v0.9.71: when set, SimpleHttpServer recognizes this as a
    // streaming response, sends Content-Type: text/event-stream
    // headers + Cache-Control: no-cache + Connection: keep-alive,
    // then hands the socket to the streamHandler callback which
    // owns the connection until it returns. The callback runs on a
    // detached worker thread so the accept loop is never blocked
    // by an SSE client. The handler is responsible for closing
    // the socket when it exits.
    bool streamMode = false;
    std::function<void(SOCKET, std::atomic<bool>* /*serverRunning*/)> streamHandler;
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

    // v0.9.13: persist appends to a JSON-lines file so events survive
    // service restart. Pre-v0.9.13 the ring was in-memory only -- a
    // wedge / crash / SCM force-stop lost all forensic data, including
    // the v0.9.10 supervisor_shutdown_wedge events that were
    // specifically designed for post-mortem. Now operators reading the
    // activity surface after a restart see the prior process's tail
    // events plus the new process's events, which is exactly what
    // deploy retros need.
    //
    // Format: one JSON object per line, encoded via the existing
    // ActivityEvent NLOHMANN macros. Append-only; the file is read
    // once at construction (the first call to globalActivityRing())
    // to repopulate the in-memory ring with the most recent
    // kPersistedTailMax events. The file is truncated and rewritten
    // when it exceeds kFileBytesSoftMax to keep on-disk size bounded.
    static constexpr size_t kPersistedTailMax = kCapacity;
    static constexpr size_t kFileBytesSoftMax = 8 * 1024 * 1024; // 8 MB

    // Set the persistence path. Called once during runtime
    // construction with %LOCALAPPDATA%\Master Control Orchestration
    // Server\activity.jsonl. If never set (e.g. test mode or
    // pre-runtime-init code paths), append() runs in-memory only and
    // load() is a no-op.
    void setPersistencePath(const std::filesystem::path& path) {
        std::lock_guard<std::mutex> lock(mutex_);
        persistPath_ = path;
        // Try to load existing file content into the ring so
        // post-restart readers see continuity. We deliberately ignore
        // load failures (corrupt file, permission issues, etc.) --
        // worst case is we start with an empty ring, which is the
        // pre-v0.9.13 baseline behavior.
        loadFromDiskLocked();
    }

    void append(const ActivityEvent& input) {
        std::lock_guard<std::mutex> lock(mutex_);
        ActivityEvent event = input;
        ++nextSequence_;
        event.id = std::to_string(nextSequence_);
        if (event.timestampUtc.empty()) {
            event.timestampUtc = currentUtcTimestamp();
        }
        // v0.9.13: write to disk before pushing to the in-memory
        // ring. If the disk write fails we still update the ring;
        // an operator querying /api/activity sees the event and a
        // future improvement could surface persistence health.
        appendToDiskLocked(event);
        ring_.push_back(std::move(event));
        if (ring_.size() > kCapacity) {
            ring_.pop_front();
        }
    }

    // Returns events with id > sinceId (sinceId may be empty).
    // Also returns the current high-water-mark id so the caller can poll
    // incrementally.
    //
    // v0.9.32: when sinceId is empty (the "give me recent activity" use
    // case from operator dashboards), return the LATEST maxCount events,
    // oldest-first within the slice. Pre-v0.9.32 the loop iterated the
    // ring forward and stopped at maxCount, returning the OLDEST maxCount
    // events -- which for a 459-event ring with max=10 surfaced events
    // from hours ago, not the live tail. The /api/activity?max=N
    // bug-hunt of v0.9.32 caught this when probing what "recent 10
    // events" looked like and got events from 06:46:11Z (boot-time
    // pool_worker_respawns) instead of 20:55:12Z (current). When sinceId
    // is provided (incremental polling: "give me what's new since N"),
    // iterate forward and return the OLDEST maxCount events newer than
    // sinceId so the caller renders them in chronological order without
    // missing intermediate events.
    struct Snapshot {
        std::vector<ActivityEvent> events;
        std::string highWaterMarkId;
    };
    Snapshot read(const std::string& sinceId, size_t maxCount = kCapacity) const {
        std::lock_guard<std::mutex> lock(mutex_);
        Snapshot out;
        out.highWaterMarkId = std::to_string(nextSequence_);

        if (sinceId.empty()) {
            // Tail semantics: latest maxCount events, oldest-first within
            // the slice.
            const size_t take = (maxCount < ring_.size()) ? maxCount : ring_.size();
            const size_t startIdx = ring_.size() - take;
            out.events.reserve(take);
            for (size_t i = startIdx; i < ring_.size(); ++i) {
                out.events.push_back(ring_[i]);
            }
            return out;
        }

        // Incremental-poll semantics: events newer than sinceId, oldest-
        // first, capped at maxCount.
        uint64_t sinceSeq = 0;
        try { sinceSeq = std::stoull(sinceId); } catch (...) { sinceSeq = 0; }

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

    // v0.9.13: persistence members.
    std::filesystem::path persistPath_;
    // v0.9.17: persistence health surface. Operators previously had
    // no signal whether disk-write failures were silently swallowed
    // by appendToDiskLocked. Now health snapshot is exposed via a
    // dedicated method (PersistenceHealth) so /api/activity/health
    // can return it. Counters increment on each write attempt;
    // lastErrorMessage_ captures the most recent failure for
    // diagnosis. All under mutex_ so reads are consistent.
    uint64_t persistedAppendCount_ = 0;
    uint64_t persistedAppendErrorCount_ = 0;
    std::string lastPersistErrorMessage_;
    std::chrono::steady_clock::time_point lastPersistErrorAt_{};

    // Caller holds mutex_. No-op if persistPath_ is empty.
    void appendToDiskLocked(const ActivityEvent& event) {
        if (persistPath_.empty()) return;
        ++persistedAppendCount_;
        try {
            // Make the parent directory if it doesn't exist yet (first run).
            std::error_code ec;
            std::filesystem::create_directories(persistPath_.parent_path(), ec);
            // Truncate-and-rewrite when file gets large. We do this
            // INLINE on the append path because the alternative (a
            // separate maintenance thread) is more moving parts than
            // this need warrants. The check is a stat() so it is
            // cheap; the rewrite only runs when the file crosses the
            // soft cap (8MB ~= weeks of typical traffic).
            const auto size = std::filesystem::file_size(persistPath_, ec);
            if (!ec && size > kFileBytesSoftMax) {
                rewriteFromRingLocked();
            }
            std::ofstream out(persistPath_, std::ios::app | std::ios::binary);
            if (!out) {
                // v0.9.17: surface the open failure in the health
                // counters so /api/activity/health can report it.
                ++persistedAppendErrorCount_;
                lastPersistErrorMessage_ = "ofstream open failed (path=" +
                    persistPath_.string() + ")";
                lastPersistErrorAt_ = std::chrono::steady_clock::now();
                return;
            }
            out << nlohmann::json(event).dump() << '\n';
            if (!out) {
                ++persistedAppendErrorCount_;
                lastPersistErrorMessage_ = "ofstream write failed (path=" +
                    persistPath_.string() + ")";
                lastPersistErrorAt_ = std::chrono::steady_clock::now();
            }
        } catch (const std::exception& ex) {
            // Persistence is best-effort; don't let it disrupt the
            // in-memory append path or the caller. v0.9.17: capture
            // the exception text so /api/activity/health surfaces it.
            ++persistedAppendErrorCount_;
            lastPersistErrorMessage_ = std::string("exception: ") + ex.what();
            lastPersistErrorAt_ = std::chrono::steady_clock::now();
        } catch (...) {
            ++persistedAppendErrorCount_;
            lastPersistErrorMessage_ = "exception: unknown";
            lastPersistErrorAt_ = std::chrono::steady_clock::now();
        }
    }

public:
    // v0.9.17: persistence health snapshot for /api/activity/health.
    // Returns a JSON-able view of the persistence layer's recent
    // behavior so operators can spot disk-full / permission /
    // path-issues without reading the file itself.
    struct PersistenceHealth {
        bool persistenceEnabled = false;
        std::string filePath;
        uint64_t bytesOnDisk = 0;
        uint64_t totalAppends = 0;
        uint64_t totalAppendErrors = 0;
        std::string lastErrorMessage;     // empty if no errors observed
        int64_t lastErrorSecondsAgo = -1; // -1 if no errors observed
        size_t inMemoryRingSize = 0;
        uint64_t highWaterMarkId = 0;
    };
    PersistenceHealth persistenceHealth() const {
        std::lock_guard<std::mutex> lock(mutex_);
        PersistenceHealth out;
        out.persistenceEnabled = !persistPath_.empty();
        out.filePath = persistPath_.string();
        out.totalAppends = persistedAppendCount_;
        out.totalAppendErrors = persistedAppendErrorCount_;
        out.lastErrorMessage = lastPersistErrorMessage_;
        if (lastPersistErrorAt_ != std::chrono::steady_clock::time_point{}) {
            const auto age = std::chrono::steady_clock::now() - lastPersistErrorAt_;
            out.lastErrorSecondsAgo = std::chrono::duration_cast<std::chrono::seconds>(age).count();
        }
        out.inMemoryRingSize = ring_.size();
        out.highWaterMarkId = nextSequence_;
        if (!persistPath_.empty()) {
            std::error_code ec;
            const auto size = std::filesystem::file_size(persistPath_, ec);
            if (!ec) out.bytesOnDisk = size;
        }
        return out;
    }
private:

    // Caller holds mutex_. Truncates the file and writes the current
    // in-memory ring as the new content. Bounds disk size.
    void rewriteFromRingLocked() {
        try {
            std::ofstream out(persistPath_, std::ios::trunc | std::ios::binary);
            if (!out) return;
            for (const auto& evt : ring_) {
                out << nlohmann::json(evt).dump() << '\n';
            }
        } catch (...) { /* best-effort */ }
    }

    // Caller holds mutex_. Reads the persisted file and repopulates
    // the in-memory ring with up to kPersistedTailMax most-recent
    // events. Sets nextSequence_ from the highest id observed so
    // future events get fresh ids.
    void loadFromDiskLocked() {
        if (persistPath_.empty()) return;
        std::error_code ec;
        if (!std::filesystem::exists(persistPath_, ec)) return;
        try {
            std::ifstream in(persistPath_, std::ios::binary);
            if (!in) return;
            std::deque<ActivityEvent> loaded;
            std::string line;
            uint64_t maxSeqSeen = 0;
            while (std::getline(in, line)) {
                if (line.empty()) continue;
                try {
                    auto j = nlohmann::json::parse(line);
                    auto evt = j.get<ActivityEvent>();
                    uint64_t seq = 0;
                    try { seq = std::stoull(evt.id); } catch (...) {}
                    if (seq > maxSeqSeen) maxSeqSeen = seq;
                    loaded.push_back(std::move(evt));
                    if (loaded.size() > kPersistedTailMax) {
                        loaded.pop_front();
                    }
                } catch (...) {
                    // Skip malformed lines; previous-version files or
                    // partial writes shouldn't poison the new run.
                }
            }
            ring_       = std::move(loaded);
            nextSequence_ = maxSeqSeen;
        } catch (...) { /* best-effort */ }
    }
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

// v0.9.8: pool-lifecycle activity-ring helper. Called from
// WorkerSupervisor::drainAndEmitSupervisorEvents to surface worker
// death / auto-respawn / quarantine events in /api/activity.
// Forward-declared near the supervisor class so the supervisor (which
// is defined earlier in this TU than the activity ring class) can
// call it without seeing the full ring/event types. Pre-v0.9.8
// /api/activity was blind to pool-lifecycle events because no
// producer reached the global ring from the supervisor.
void appendPoolLifecycleActivity(const std::string& kind,
                                 const std::string& poolId,
                                 const std::string& message,
                                 int statusCodeHint) {
    ActivityEvent event;
    event.kind = kind;             // pool_worker_death | pool_worker_respawn | pool_quarantine
    event.actor = "supervisor-watchdog";
    event.target = poolId;         // for joins with /api/pools by poolId
    event.statusCode = statusCodeHint;
    event.message = message;
    globalActivityRing().append(event);
}

// v0.9.10: emit a supervisor_shutdown_wedge ActivityEvent from the
// supervisor's destructor when the watchdog had to be detached.
// Captures the supervisor's pool/child count at detach time so
// post-mortem analysis can spot patterns (e.g. always wedges when
// children > N, or always wedges with a respawn-in-flight).
// Forward-declared near the supervisor class for visibility-without-
// dependency reasons; defined here so the ActivityEvent struct is
// fully visible.
void appendSupervisorWedgeActivity(const std::string& message,
                                   int poolCount,
                                   int childCount) {
    ActivityEvent event;
    event.kind   = "supervisor_shutdown_wedge";
    event.actor  = "supervisor-destructor";
    event.target = "watchdog-thread";
    event.statusCode = 504;        // gateway-timeout flavor
    event.message = message
        + " supervisor-state-at-detach: pools=" + std::to_string(poolCount)
        + " children=" + std::to_string(childCount) + ".";
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

    // v0.9.50: split path at '?' so route handlers can do strict
    // equality / prefix matching without manually stripping. The
    // raw query (without leading '?') is preserved in request.query
    // for routes that need to parse it (since=, max=, etc.).
    const auto queryPos = request.path.find('?');
    if (queryPos != std::string::npos) {
        request.query = request.path.substr(queryPos + 1);
        request.path = request.path.substr(0, queryPos);
    }

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
    // v0.9.26: Content-Length always reflects the GET-equivalent
    // body size, even when suppressBody is true (HEAD path), per
    // RFC 7231 §4.3.2. The actual body is written below only when
    // suppressBody is false.
    stream << "Content-Length: " << response.body.size() << "\r\n";
    // v0.9.27: maximally-permissive CORS headers on every response.
    // The MCOS LAN-trust model (ADR-001: no app-layer auth on the
    // AI-client surface; network-level trust only) means any client
    // that can reach this port is by definition allowed to call any
    // endpoint -- the wildcard origin doesn't grant access that
    // wasn't already implicit. Operator dashboards running from a
    // different LAN origin (e.g. a browser tab opened against a
    // local file or a dev server on a different port) can now make
    // cross-origin XHR/fetch calls without a 404 on preflight.
    stream << "Access-Control-Allow-Origin: *\r\n";
    stream << "Access-Control-Allow-Methods: GET, HEAD, POST, OPTIONS\r\n";
    stream << "Access-Control-Allow-Headers: Content-Type, X-MCOS-Client-Id, X-MCOS-Client-Type\r\n";
    stream << "Access-Control-Max-Age: 86400\r\n";
    // v0.9.28: emit any additional response-specific headers
    // (e.g. Allow on a 405) before terminating the header block.
    for (const auto& [name, value] : response.extraHeaders) {
        stream << name << ": " << value << "\r\n";
    }
    stream << "Connection: close\r\n\r\n";
    if (!response.suppressBody) {
        stream << response.body;
    }

    const auto data = stream.str();
    send(client, data.c_str(), static_cast<int>(data.size()), 0);
}

void SimpleHttpServer::run() {
    // v0.9.3: dual-stack bind. Pre-v0.9.3 SimpleHttpServer called
    // getaddrinfo(AF_UNSPEC, AI_PASSIVE) for "0.0.0.0" and the IPv4
    // wildcard came back first, so the loop bound IPv4-only and the
    // admin port was unreachable on IPv6. The discovery doc on a
    // dual-stack host still happily advertised an IPv6 onboarding URL
    // (which IS reachable from IPv6 clients in principle) but the
    // admin port wasn't actually listening on IPv6 -- so the
    // advertised URL produced "Unable to connect" from any IPv6
    // client. Fix: when bindAddress is wildcard ("0.0.0.0", "::",
    // empty), bind a single dual-stack IPv6 socket with IPV6_V6ONLY
    // off. Windows' dual-stack semantics let the same socket accept
    // IPv4-mapped (::ffff:1.2.3.4) and native IPv6 simultaneously.
    // Specific bindAddress values (e.g. "192.168.1.7") still go
    // through the per-family path so an operator-pinned IPv4 address
    // produces a clean IPv4-only listener.
    const bool wildcard = bindAddress_.empty()
        || bindAddress_ == "0.0.0.0"
        || bindAddress_ == "::"
        || bindAddress_ == "[::]";

    if (wildcard) {
        listenSocket_ = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
        if (listenSocket_ != INVALID_SOCKET) {
            const int reuse = 1;
            setsockopt(listenSocket_, SOL_SOCKET, SO_REUSEADDR,
                       reinterpret_cast<const char*>(&reuse), sizeof(reuse));
            // Critical: turn off IPV6_V6ONLY so this single socket
            // accepts both IPv4 and IPv6 connections.
            const int v6only = 0;
            setsockopt(listenSocket_, IPPROTO_IPV6, IPV6_V6ONLY,
                       reinterpret_cast<const char*>(&v6only), sizeof(v6only));

            sockaddr_in6 addr{};
            addr.sin6_family = AF_INET6;
            addr.sin6_addr = in6addr_any;
            addr.sin6_port = htons(port_);

            if (bind(listenSocket_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0
                && listen(listenSocket_, SOMAXCONN) == 0) {
                {
                    std::lock_guard<std::mutex> lock(startupMutex_);
                    startupSucceeded_ = true;
                    startupComplete_ = true;
                }
                startupCv_.notify_all();

                while (running_) {
                    sockaddr_storage clientAddr{};
                    int clientLen = sizeof(clientAddr);
                    SOCKET client = accept(listenSocket_,
                                            reinterpret_cast<sockaddr*>(&clientAddr),
                                            &clientLen);
                    if (client == INVALID_SOCKET) {
                        if (running_) continue;
                        break;
                    }
                    handleClient(client);
                }
                return;
            }
            closesocket(listenSocket_);
            listenSocket_ = INVALID_SOCKET;
        }
        // Dual-stack failed (e.g. operator disabled IPv6 stack). Fall
        // through to the legacy per-family path which will pick
        // whichever family getaddrinfo returns first for "0.0.0.0".
    }

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

    auto request = parseRequest(requestBuffer);

    // v0.9.27: OPTIONS preflight short-circuit. Browser-based
    // dashboards from a different origin (e.g. an operator running
    // a debug UI on http://localhost:9999 hitting the MCOS admin
    // port) need a CORS preflight response. Pre-v0.9.27 every
    // OPTIONS returned 404 because no route matched method ==
    // "OPTIONS"; the browser then refused to send the actual
    // request. The LAN-trust model (no app-layer auth, network-
    // level trust per ADR-001) means we can return the maximally-
    // permissive CORS headers without compromising security
    // posture: any LAN client that can reach the port already
    // has the same access an embedded same-origin dashboard does.
    if (request.method == "OPTIONS") {
        HttpResponse cors;
        cors.statusCode = 204;
        cors.contentType = "text/plain";
        cors.body = "";
        // The wildcard origin works because the trust model is
        // "any client that can reach this port is trusted." Sub-
        // sequent actual requests use the same Access-Control-
        // Allow-* headers (added below for every response).
        cors.suppressBody = true;
        sendResponse(client, cors);
        return;
    }

    // v0.9.26: HEAD requests should produce the same response as
    // GET, minus the body (RFC 7231 §4.3.2). Pre-v0.9.26 every HEAD
    // hit our 404 fall-through because no route matched on
    // method=='HEAD'. Common monitoring tools (Pingdom, Uptime
    // Robot, healthchecks.io, even curl -I) probe with HEAD for
    // cheap liveness checks; getting a 404 made every operator
    // route look dead.
    //
    // We rewrite the verb to GET for the handler dispatch so every
    // existing GET route handles HEAD transparently. The send-
    // response path then suppresses the body via a flag so HEAD
    // gets the headers but no payload bytes.
    const bool isHead = (request.method == "HEAD");
    if (isHead) {
        request.method = "GET";
    }
    auto response = handler_(request);
    if (isHead) {
        // Keep Content-Length reflecting what the body WOULD be,
        // per RFC 7231 §4.3.2. Just don't send the body itself.
        response.suppressBody = true;
    }
    // v0.9.71: streaming response (SSE). Send the SSE-style headers
    // synchronously, then hand the socket to a detached worker
    // thread that runs the streamHandler until it decides to exit.
    // Returning from this function lets the accept loop pick up the
    // next client; the SSE thread owns its socket from here.
    if (response.streamMode && response.streamHandler) {
        std::ostringstream hdr;
        hdr << "HTTP/1.1 200 OK\r\n";
        hdr << "Content-Type: text/event-stream\r\n";
        hdr << "Cache-Control: no-cache, no-store, must-revalidate\r\n";
        hdr << "Pragma: no-cache\r\n";
        hdr << "Connection: keep-alive\r\n";
        hdr << "X-Accel-Buffering: no\r\n";
        hdr << "Access-Control-Allow-Origin: *\r\n";
        hdr << "Access-Control-Allow-Methods: GET, OPTIONS\r\n";
        hdr << "\r\n";
        const auto headerBytes = hdr.str();
        ::send(client, headerBytes.c_str(), static_cast<int>(headerBytes.size()), 0);
        auto handler = std::move(response.streamHandler);
        std::atomic<bool>* serverRunning = &running_;
        std::thread([client, serverRunning, handler = std::move(handler)]() {
            handler(client, serverRunning);
            ::shutdown(client, SD_BOTH);
            ::closesocket(client);
        }).detach();
        return;
    }
    sendResponse(client, response);
    ::shutdown(client, SD_BOTH);
    ::closesocket(client);
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

// v0.10.0: extend the Claude plugin toggle so it not only creates the
// junction at ~/.claude/plugins/mcos-control but also flips the
// "mcos-control" entry in ~/.claude/settings.json's enabledPlugins map.
// Pre-v0.10.0 the junction alone was insufficient: Claude Code only
// loads plugins whose key appears in enabledPlugins, so the operator
// would toggle on, see the junction created, restart Claude Code, and
// still find no /mcos-control:* commands or skills available. This
// helper is best-effort — failure to write the settings file does not
// fail the toggle (the junction is the authoritative state); the
// resulting issue is surfaced through claudeSettingsLastError so the
// UI status row can show it. The chosen key form is just "mcos-control"
// (no @marketplace suffix) which matches the local-plugin convention.
struct ClaudeSettingsUpdateOutcome {
    bool attempted = false;
    bool succeeded = false;
    std::string errorMessage;
};

inline ClaudeSettingsUpdateOutcome setClaudeMcosPluginEnabled(const std::string& profileDir,
                                                              bool enabled) {
    ClaudeSettingsUpdateOutcome out;
    if (profileDir.empty()) return out;
    out.attempted = true;

    const std::filesystem::path settingsPath =
        std::filesystem::path(profileDir) / ".claude" / "settings.json";

    nlohmann::json doc = nlohmann::json::object();
    std::error_code ec;
    if (std::filesystem::exists(settingsPath, ec)) {
        std::ifstream in(settingsPath, std::ios::binary);
        if (in) {
            try {
                in >> doc;
                if (!doc.is_object()) doc = nlohmann::json::object();
            } catch (const nlohmann::json::exception& e) {
                out.errorMessage = std::string("settings.json parse failed: ") + e.what();
                return out;
            }
        }
    }

    if (!doc.contains("enabledPlugins") || !doc["enabledPlugins"].is_object()) {
        doc["enabledPlugins"] = nlohmann::json::object();
    }
    auto& enabledPlugins = doc["enabledPlugins"];
    static constexpr const char* kPluginKey = "mcos-control";

    if (enabled) {
        enabledPlugins[kPluginKey] = true;
    } else {
        if (enabledPlugins.contains(kPluginKey)) {
            enabledPlugins.erase(kPluginKey);
        } else {
            // Already in the desired state; nothing to write.
            out.succeeded = true;
            return out;
        }
    }

    // Ensure parent (.claude\) tree exists before write.
    std::filesystem::create_directories(settingsPath.parent_path(), ec);
    if (ec) {
        out.errorMessage = "create_directories(" + settingsPath.parent_path().string()
            + "): " + ec.message();
        return out;
    }

    std::ofstream outFile(settingsPath, std::ios::binary | std::ios::trunc);
    if (!outFile) {
        out.errorMessage = "open for write failed: " + settingsPath.string();
        return out;
    }
    outFile << doc.dump(2);
    if (!outFile.good()) {
        out.errorMessage = "write failed: " + settingsPath.string();
        return out;
    }
    out.succeeded = true;
    return out;
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
    // v0.10.8: resolve the supervisor service's mcpEndpoint /
    // discoveryEndpoint / fingerprintSeed against the current
    // configuration + telemetry snapshot, and push them into
    // supervisorAssignmentService_ via setEndpoints(). Called by the
    // route layer before any select/generate/confirm so the JSON the
    // operator hands to a remote supervisor client carries a URL the
    // remote can actually reach (LAN-routable IPv4 + gateway port +
    // /mcp), matching the same resolution chain DiscoveryService uses
    // for the well-known discovery doc.
    void refreshSupervisorEndpoints();
    // v0.6.8: mirror WorkerSupervisor::pools_ into AppConfiguration.pools
    // and persist to disk. Called from /api/pools POST and /api/pools/{id}/remove
    // so pool definitions survive service restart / MSI upgrade.
    void persistSupervisedPoolsToConfiguration();

    template <typename T>
    static HttpResponse jsonResponse(const T& value, int statusCode = 200) {
        // v0.9.49: dump() with error_handler_t::replace so invalid UTF-8
        // bytes in any string field become U+FFFD instead of throwing
        // type_error.316. Pre-v0.9.49 the admin port returned 500 with
        // "invalid UTF-8 byte at index N" when a client's POST body had
        // mojibake/Latin-1 bytes -- the catch arm built a diagnostic
        // message embedding ex.what() (which contained the bad byte),
        // and jsonResponse->dump() then threw on the bad byte itself,
        // bouncing up to the outer 500 handler. The /mcp port had the
        // same pattern but with worse blast radius (v0.9.48 crash); the
        // admin port had the outer 500 catch as a backstop. v0.9.49
        // makes both paths produce a clean 400 with the bytes replaced
        // by U+FFFD, which is what RFC 8259 §8.1 expects of a tolerant
        // JSON serializer.
        return HttpResponse{ statusCode, "application/json",
            nlohmann::json(value).dump(2, ' ', false,
                nlohmann::json::error_handler_t::replace) };
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
    // v0.9.76: Supervisor Agent Assignment Wizard backend. Backs the
    // /api/supervisor/* routes and the WinUI Shell's selection popup.
    // unique_ptr because the impl is module-private and exposed only
    // via the ISupervisorAssignmentService interface; no other service
    // shares ownership.
    std::unique_ptr<MasterControl::ISupervisorAssignmentService> supervisorAssignmentService_;
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

    // v0.9.69: boot self-test machinery. runBootSelfTestsAsync spawns
    // a worker thread that probes the freshly-booted runtime (admin
    // API endpoints, supervised pool handshakes, gateway state,
    // activity ring, telemetry sampler, on-disk worker exes), records
    // each result to the activity ring (kind="self_test", statusCode
    // 200/500), and caches the snapshot for /api/self-tests. The
    // existing v0.8.7 Error Reporting frame on the WinUI Overview
    // surface harvests error-flagged activity events automatically,
    // so failed probes appear there with no further wiring.
    mutable std::mutex selfTestMutex_;
    SelfTestSnapshot lastSelfTestSnapshot_;
    std::thread selfTestThread_;
    std::atomic<bool> selfTestThreadJoinable_{ false };

    void runBootSelfTestsAsync();
    SelfTestSnapshot runBootSelfTestsNow();
    SelfTestSnapshot getLastSelfTestSnapshot() const {
        std::lock_guard<std::mutex> lock(selfTestMutex_);
        return lastSelfTestSnapshot_;
    }
};

bool MasterControlApplication::Impl::initialize() {
    // v0.9.13: wire activity-ring persistence to a JSON-lines file
    // under the data dir so events survive service restart. Pre-
    // v0.9.13 the ring was in-memory only, which meant a wedge or
    // crash lost all forensic data including the v0.9.10
    // supervisor_shutdown_wedge events that were specifically
    // designed for post-mortem. Now operators reading
    // /api/activity after a restart see the prior process's tail
    // events plus the new run's events. Done FIRST in initialize()
    // so any subsequent activity-ring appends (gateway boot,
    // configuration load, etc.) get persisted.
    globalActivityRing().setPersistencePath(paths_.dataDirectory / "activity.jsonl");

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

    // v0.9.76: Supervisor Agent Assignment Wizard backend. The wizard
    // surfaces three providers (chatgpt | claude | grok) through the
    // WinUI Shell + browser dashboard; the service issues short-lived
    // tokenRef-style configuration files conforming to
    // mcos.supervisor.config.v1 and tracks lifecycle state to disk so
    // a restart preserves the operator's selection.
    //
    // v0.10.8: the mcpEndpoint baked into the generated config used to
    // point at "http://127.0.0.1:<browserPort>/mcp" (e.g. :7300/mcp).
    // That URL is wrong on two axes for a remote ChatGPT supervisor:
    //   - the MCP gateway listener is the native HTTP.sys adapter on
    //     <gatewayPort> (default 8080) at gateway.mcpPath ("/mcp"), NOT
    //     the admin/browser port; ":7300/mcp" returns 404 because
    //     /mcp is not a route on the browser HTTP server.
    //   - 127.0.0.1 is only reachable from the same host. A remote
    //     supervisor client (LAN ChatGPT desktop, ChatGPT cloud connector
    //     via tunnel, anything off-box) must be handed the LAN-routable
    //     IPv4 the discovery doc already advertises.
    // The initial value here is a same-host fallback (127.0.0.1 +
    // gatewayPort). The route layer (handleHttpRequest) calls
    // refreshSupervisorEndpoints() just before select/generate/confirm
    // to overwrite this with the live LAN-resolved values, mirroring
    // the exact resolution chain DiscoveryService uses for the well-
    // known doc.
    {
        const auto cfg = configurationService_->current();
        const auto bindHost = !cfg.bindAddress.empty() ? cfg.bindAddress : std::string("127.0.0.1");
        const auto resolvedHost = (bindHost == "0.0.0.0" || bindHost == "::"
            || bindHost == "[::]" || bindHost == "::0")
            ? std::string("127.0.0.1") : bindHost;
        const auto adminBase = std::string("http://") + resolvedHost + ":" + std::to_string(cfg.browserPort);
        const auto gatewayMcpPath = cfg.mcpGateway.mcpPath.empty() ? std::string("/mcp") : cfg.mcpGateway.mcpPath;
        const auto gatewayBase = std::string("http://") + resolvedHost + ":" + std::to_string(cfg.mcpGateway.listenPort);
        MasterControl::SupervisorAssignmentServiceContext supervisorCtx;
        supervisorCtx.dataDirectory = paths_.dataDirectory;
        supervisorCtx.mcpEndpoint = gatewayBase + gatewayMcpPath;
        supervisorCtx.discoveryEndpoint = adminBase + "/.well-known/mcos.json";
        supervisorCtx.serverDisplayName = cfg.instanceName.empty()
            ? std::string("Master Control Orchestration Server")
            : cfg.instanceName;
        supervisorCtx.fingerprintSeed = bindHost + ":" + std::to_string(cfg.browserPort)
            + "|" + supervisorCtx.serverDisplayName;
        supervisorAssignmentService_ = MasterControl::createSupervisorAssignmentService(std::move(supervisorCtx));
    }

    registerConfigurationDefaults();
    createForsettiRuntime();

    // v0.9.0: MCP Gateway is exclusively the native HTTP.sys adapter.
    // MCPJungle support was dropped per the operator directive
    // ("MCP Jungle support is to be dropped in place of a custom
    // solution"). Persisted configs that still carry
    // mcpGateway.type='mcpjungle' from v0.8.x and earlier transparently
    // resolve to the native substrate -- the enum value is kept in
    // GatewayType only so old JSON deserializes without rejection.
    // gatewayConfig.type is otherwise ignored.
    {
        const auto gatewayConfig = configurationService_->current().mcpGateway;
        mcpGateway_ = std::make_shared<NativeHttpSysGatewayAdapter>(gatewayConfig);
    }

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
    // PHASE-07 layers leases + autoscale on top.
    //
    // v0.6.8 added pool persistence: AppConfiguration carries the pool
    // definition list, mirroring WorkerSupervisor::pools_ to disk on
    // every upsert/remove, and we hydrate the supervisor here at boot
    // so pool definitions survive service restart and MSI MajorUpgrade.
    // Through v0.6.7 the supervisor came up empty, the operator's
    // registered pools were memory-only, and every restart wiped them.
    workerSupervisor_ = std::make_shared<WorkerSupervisor>();
    {
        const auto bootCfg = configurationService_->current();
        for (const auto& persistedPool : bootCfg.pools) {
            // upsertPool returns success even on duplicate; passing the
            // freshly-loaded definition is idempotent. instances are not
            // hydrated -- supervised processes don't survive a restart by
            // design (Job Object KILL_ON_JOB_CLOSE), so re-spawning is
            // operator-triggered via POST /api/pools/{id}/scale.
            workerSupervisor_->upsertPool(persistedPool);
        }
    }

    // v0.9.1: register the always-on baseline tools pool. Pre-v0.9.1 the
    // 23-server catalog was advertised by name only -- no worker process
    // was bound to any pool, so gateway tools/list returned [] on every
    // fresh install. v0.9.1 ships an in-tree native worker
    // (mcos-baseline-tools-worker.exe) that exposes mcos.echo / mcos.now /
    // mcos.host_info / mcos.add. We force-upsert the pool here every boot
    // (rather than seeding it through buildDefaultConfiguration) because
    // the pool's executable path is install-location-dependent and must
    // be resolved against the live executable directory: an MSI
    // MajorUpgrade that relocates the install would leave a stale path
    // baked into a buildDefaultConfiguration-issued seed.
    {
        const auto baselineWorkerPath =
            (paths_.executableDirectory / "mcos-baseline-tools-worker.exe").string();

        ManagedEndpointPool baselinePool;
        baselinePool.poolId         = "baseline-tools";
        baselinePool.kind           = EndpointPoolKind::McpServer;
        baselinePool.displayName    = "MCOS Baseline Tools";
        baselinePool.logicalMcpUrl  = ""; // logical -- routed through gateway
        baselinePool.template_.executable        = baselineWorkerPath;
        baselinePool.template_.args              = {};
        baselinePool.template_.workingDirectory  = paths_.executableDirectory.string();
        baselinePool.template_.transport         = "stdio_jsonrpc";
        baselinePool.template_.healthProbe.transport = "stdio_handshake";
        baselinePool.scalePolicy.minInstances              = 1;
        // v0.9.3: lift max instance + max leases-per-instance ceilings.
        // Pre-v0.9.3 maxActiveLeasesPerInstance=4 throttled 10 parallel
        // tools/call probes to 4 successes + 6 lease failures even though
        // each call is a microsecond-scale stdio round-trip. Each lease
        // is held only for the lifetime of one tools/call (the gateway
        // releases on response). 64 concurrent leases per instance is
        // far past any realistic bridge-saturation point and gives
        // headroom for bursty clients without forcing scale-out.
        baselinePool.scalePolicy.maxInstances              = 2;
        baselinePool.scalePolicy.maxActiveLeasesPerInstance = 64;
        baselinePool.scalePolicy.scaleOutQueueWaitMs        = 1500;
        baselinePool.scalePolicy.scaleInIdleSeconds         = 300;

        workerSupervisor_->upsertPool(baselinePool);

        // v0.9.61: terminal-shell pool. Reuses the same worker exe with
        // a --specialization arg so a single binary serves multiple
        // logical MCPs. Pre-v0.9.61 the catalog advertised
        // 'terminal-shell' at hardcoded port 7108 with nothing actually
        // listening; v0.9.61 backs that catalog id with a real
        // supervised stdio pool exposing shell.exec(command, [cwd],
        // [timeoutMs]) -> {stdout, stderr, exitCode, timedOut}. The
        // gateway's tools/list aggregate now namespaces it as
        // terminal-shell__shell.exec; LAN AI clients can use it the
        // same way they use filesystem__read_file etc.
        ManagedEndpointPool shellPool;
        shellPool.poolId         = "terminal-shell";
        shellPool.kind           = EndpointPoolKind::McpServer;
        shellPool.displayName    = "Terminal / Shell MCP";
        shellPool.logicalMcpUrl  = "";
        shellPool.template_.executable        = baselineWorkerPath;
        shellPool.template_.args              = { "--specialization=terminal-shell" };
        shellPool.template_.workingDirectory  = paths_.executableDirectory.string();
        shellPool.template_.transport         = "stdio_jsonrpc";
        shellPool.template_.healthProbe.transport = "stdio_handshake";
        shellPool.scalePolicy.minInstances              = 1;
        shellPool.scalePolicy.maxInstances              = 2;
        shellPool.scalePolicy.maxActiveLeasesPerInstance = 8;
        shellPool.scalePolicy.scaleOutQueueWaitMs        = 1500;
        shellPool.scalePolicy.scaleInIdleSeconds         = 300;
        workerSupervisor_->upsertPool(shellPool);

        // v0.9.62: local-git pool. Same multi-spec worker, different
        // --specialization arg. Exposes git.run(args[], [cwd],
        // [timeoutMs]) -- arbitrary git invocations are routed
        // through CreateProcessW + pipes so the LAN AI client gets
        // exitCode + stdout/stderr without having to shell out itself.
        ManagedEndpointPool gitPool;
        gitPool.poolId         = "local-git";
        gitPool.kind           = EndpointPoolKind::McpServer;
        gitPool.displayName    = "Local Git MCP";
        gitPool.logicalMcpUrl  = "";
        gitPool.template_.executable        = baselineWorkerPath;
        gitPool.template_.args              = { "--specialization=local-git" };
        gitPool.template_.workingDirectory  = paths_.executableDirectory.string();
        gitPool.template_.transport         = "stdio_jsonrpc";
        gitPool.template_.healthProbe.transport = "stdio_handshake";
        gitPool.scalePolicy.minInstances              = 1;
        gitPool.scalePolicy.maxInstances              = 2;
        gitPool.scalePolicy.maxActiveLeasesPerInstance = 8;
        gitPool.scalePolicy.scaleOutQueueWaitMs        = 1500;
        gitPool.scalePolicy.scaleInIdleSeconds         = 300;
        workerSupervisor_->upsertPool(gitPool);

        // v0.9.62: file-search pool. Exposes search.grep(pattern,
        // [path], [glob], [maxMatches], [timeoutMs]) using ripgrep
        // when on PATH and a PowerShell Select-String fallback when
        // not, so a fresh install without rg still serves the tool.
        ManagedEndpointPool searchPool;
        searchPool.poolId         = "file-search";
        searchPool.kind           = EndpointPoolKind::McpServer;
        searchPool.displayName    = "File Search MCP";
        searchPool.logicalMcpUrl  = "";
        searchPool.template_.executable        = baselineWorkerPath;
        searchPool.template_.args              = { "--specialization=file-search" };
        searchPool.template_.workingDirectory  = paths_.executableDirectory.string();
        searchPool.template_.transport         = "stdio_jsonrpc";
        searchPool.template_.healthProbe.transport = "stdio_handshake";
        searchPool.scalePolicy.minInstances              = 1;
        searchPool.scalePolicy.maxInstances              = 2;
        searchPool.scalePolicy.maxActiveLeasesPerInstance = 4;
        searchPool.scalePolicy.scaleOutQueueWaitMs        = 1500;
        searchPool.scalePolicy.scaleInIdleSeconds         = 300;
        workerSupervisor_->upsertPool(searchPool);

        // v0.9.63: client-tracker pool. Bridges the LAN-client roster
        // surface (already populated via /api/telemetry/heartbeat into
        // /api/clients) up as an MCP tool so AI clients can query
        // "who else is connected to MCOS right now" without having to
        // know about the admin port directly.
        ManagedEndpointPool clientTrackerPool;
        clientTrackerPool.poolId         = "client-tracker";
        clientTrackerPool.kind           = EndpointPoolKind::McpServer;
        clientTrackerPool.displayName    = "Client Tracker MCP";
        clientTrackerPool.logicalMcpUrl  = "";
        clientTrackerPool.template_.executable        = baselineWorkerPath;
        clientTrackerPool.template_.args              = { "--specialization=client-tracker" };
        clientTrackerPool.template_.workingDirectory  = paths_.executableDirectory.string();
        clientTrackerPool.template_.transport         = "stdio_jsonrpc";
        clientTrackerPool.template_.healthProbe.transport = "stdio_handshake";
        clientTrackerPool.scalePolicy.minInstances              = 1;
        clientTrackerPool.scalePolicy.maxInstances              = 2;
        clientTrackerPool.scalePolicy.maxActiveLeasesPerInstance = 4;
        clientTrackerPool.scalePolicy.scaleOutQueueWaitMs        = 1500;
        clientTrackerPool.scalePolicy.scaleInIdleSeconds         = 300;
        workerSupervisor_->upsertPool(clientTrackerPool);

        // v0.9.63: metrics pool. Bridges the host telemetry +
        // gateway throughput surfaces up as MCP tools (metrics.host,
        // metrics.gateway). Honest-unavailable contract preserved:
        // capturedAtUtc=='' on host telemetry means the sampler
        // hasn't taken its first reading yet (PHASE-08 §9), and the
        // bridge passes that signal through unchanged.
        ManagedEndpointPool metricsPool;
        metricsPool.poolId         = "metrics";
        metricsPool.kind           = EndpointPoolKind::McpServer;
        metricsPool.displayName    = "Metrics MCP";
        metricsPool.logicalMcpUrl  = "";
        metricsPool.template_.executable        = baselineWorkerPath;
        metricsPool.template_.args              = { "--specialization=metrics" };
        metricsPool.template_.workingDirectory  = paths_.executableDirectory.string();
        metricsPool.template_.transport         = "stdio_jsonrpc";
        metricsPool.template_.healthProbe.transport = "stdio_handshake";
        metricsPool.scalePolicy.minInstances              = 1;
        metricsPool.scalePolicy.maxInstances              = 2;
        metricsPool.scalePolicy.maxActiveLeasesPerInstance = 4;
        metricsPool.scalePolicy.scaleOutQueueWaitMs        = 1500;
        metricsPool.scalePolicy.scaleInIdleSeconds         = 300;
        workerSupervisor_->upsertPool(metricsPool);

        // v0.9.64: register the next batch of in-tree pools that all
        // share the multi-spec mcos-baseline-tools-worker.exe binary.
        // Each pool wraps a different --specialization arg so the
        // gateway aggregator namespaces their tools per pool. Pre-
        // v0.9.64 these catalog ids were placeholders without backings;
        // they're now first-class supervised pools that come up on
        // boot.
        auto registerWorkerPool = [&](const std::string& poolId,
                                       const std::string& displayName,
                                       int leasesPerInstance) {
            ManagedEndpointPool p;
            p.poolId         = poolId;
            p.kind           = EndpointPoolKind::McpServer;
            p.displayName    = displayName;
            p.logicalMcpUrl  = "";
            p.template_.executable        = baselineWorkerPath;
            p.template_.args              = { "--specialization=" + poolId };
            p.template_.workingDirectory  = paths_.executableDirectory.string();
            p.template_.transport         = "stdio_jsonrpc";
            p.template_.healthProbe.transport = "stdio_handshake";
            p.scalePolicy.minInstances              = 1;
            p.scalePolicy.maxInstances              = 2;
            p.scalePolicy.maxActiveLeasesPerInstance = leasesPerInstance;
            p.scalePolicy.scaleOutQueueWaitMs        = 1500;
            p.scalePolicy.scaleInIdleSeconds         = 300;
            workerSupervisor_->upsertPool(p);
        };
        registerWorkerPool("code-execution-repl", "Code Execution / REPL MCP", 4);
        registerWorkerPool("local-test-runner",   "Local Test Runner MCP",     2);
        registerWorkerPool("local-build-tool",    "Local Build Tool MCP",      2);
        registerWorkerPool("local-linter",        "Local Linter MCP",          4);
        registerWorkerPool("local-indexer",       "Local Indexer MCP",         4);
        // v0.9.65: native-storage + watcher batch.
        registerWorkerPool("persistent-context",  "Persistent Context MCP",    8);
        registerWorkerPool("knowledge-graph",     "Knowledge Graph MCP",       8);
        registerWorkerPool("file-watcher",        "File Watcher MCP",          4);
        // v0.9.66: Win32-input batch.
        registerWorkerPool("keyboard-mouse-control", "Keyboard & Mouse Control MCP", 4);
        registerWorkerPool("screen-capture-vision",  "Screen Capture / Vision MCP",  2);
        registerWorkerPool("desktop-control",        "Desktop Control MCP",          4);
        registerWorkerPool("computer-use",           "Computer Use MCP",             4);

        // v0.9.67: 7 default sub-agents. Same multi-spec worker
        // binary, --specialization=<role>. kind=SubAgent so the
        // dashboard renders them in the sub-agent grid (separate
        // from the MCP server grid). The sub-agent runtime stat
        // pipeline (line ~10729 in this file) keys by poolId, so
        // the supervisor pool's poolId matching the seeded
        // sub-agent id is what flips installState to
        // installed_and_supervised.
        auto registerSubAgentPool = [&](const std::string& poolId,
                                         const std::string& displayName) {
            ManagedEndpointPool p;
            p.poolId         = poolId;
            p.kind           = EndpointPoolKind::SubAgent;
            p.displayName    = displayName;
            p.logicalMcpUrl  = "";
            p.template_.executable        = baselineWorkerPath;
            p.template_.args              = { "--specialization=" + poolId };
            p.template_.workingDirectory  = paths_.executableDirectory.string();
            p.template_.transport         = "stdio_jsonrpc";
            p.template_.healthProbe.transport = "stdio_handshake";
            p.scalePolicy.minInstances              = 1;
            p.scalePolicy.maxInstances              = 2;
            p.scalePolicy.maxActiveLeasesPerInstance = 4;
            p.scalePolicy.scaleOutQueueWaitMs        = 1500;
            p.scalePolicy.scaleInIdleSeconds         = 300;
            workerSupervisor_->upsertPool(p);
        };
        registerSubAgentPool("sentinel",   "Sentinel — rule guard + activity tail");
        registerSubAgentPool("architect",  "Architect — phases + ADR drafting");
        registerSubAgentPool("forge",      "Forge — pool templates + specialization registry");
        registerSubAgentPool("scribe",     "Scribe — release reports + version state");
        registerSubAgentPool("recon",      "Recon — dashboard + diagnostics + gateway tools");
        registerSubAgentPool("nexus",      "Nexus — coordination + discovery + clients");
        registerSubAgentPool("watchtower", "Watchtower — health + activity monitoring");

        // v0.9.68: external-dependency MCPs registered as default
        // pools too. The multi-spec worker exposes a small status
        // surface (docker-control: docker.status / docker.list_containers
        // shelling out to docker.exe; local-database: db.status /
        // db.set_connection_string persisting via persistent-context).
        // These pools are installed_and_supervised because the worker
        // itself is in-tree -- the underlying capability (Docker
        // Desktop / a configured DSN) is the operator concern, but
        // the MCP wire is live.
        registerWorkerPool("docker-control", "Docker Control MCP",   2);
        registerWorkerPool("local-database", "Local Database MCP",   2);
    }

    // v0.9.1: auto-spawn instances for any pool whose minInstances >= 1.
    // The persisted-pool hydration above only registers definitions; it
    // does NOT spawn children. Without this loop the gateway's tools/list
    // would still return [] on a fresh install because no instance is
    // Ready to be queried via the stdio bridge. ensureMinInstances is
    // idempotent so this is safe to run after every boot. Failures
    // (missing binary, CreateProcessW error) leave the pool in a
    // supervised-mock or Failed state -- exactly the honest reporting
    // contract from ADR-002 §9; the dashboard surfaces the real state.
    {
        const auto livePools = workerSupervisor_->listPools();
        for (const auto& pool : livePools) {
            if (pool.scalePolicy.minInstances >= 1) {
                workerSupervisor_->ensureMinInstances(pool.poolId);
            }
        }
    }

    // PHASE-07 (ADR-002 §8): construct the lease router that resolves
    // LeaseRequest -> EndpointLease using sticky-session + least-loaded
    // routing, with same-type scale-out when all Ready instances are at
    // maxActiveLeasesPerInstance and the pool has not reached its
    // scalePolicy.maxInstances ceiling.
    leaseRouter_ = std::make_shared<LeaseRouter>(workerSupervisor_);

    // PHASE-12 follow-up (v0.6.10): wire the native HTTP.sys gateway to
    // the supervisor + lease router so tools/call can forward to a
    // supervised pool instance via the stdio bridge. v0.9.0: every
    // adapter is the native one now (MCPJungle dropped); the cast is
    // unconditional but kept defensive in case future substrates land.
    if (auto nativeAdapter =
            std::dynamic_pointer_cast<NativeHttpSysGatewayAdapter>(mcpGateway_)) {
        nativeAdapter->AttachWorkerBridge(workerSupervisor_, leaseRouter_);
    }

    // PHASE-08 (ADR-002 §9): construct the telemetry aggregator that
    // owns the activity event ring, the connected-client roster, and
    // gateway traffic counters. Honest only — no fake utilization, no
    // fabricated client metrics. Per-AI-client CPU/GPU/disk arrives via
    // POST /api/telemetry/heartbeat or sidecar.
    //
    // v0.9.3: aggregator construction moved BEFORE gateway auto-start.
    // Pre-v0.9.3 the auto-start fired with telemetryAggregator_ still
    // null, which silently dropped the boot telemetry event AND meant
    // a Failed boot status was invisible to the activity ring. Now the
    // aggregator exists when the gateway boots so failures are recorded
    // as Telemetry::Gateway::Error events the operator can see in
    // /api/activity.
    telemetryAggregator_ = std::make_shared<TelemetryAggregator>();
    {
        TelemetryEvent boot;
        boot.category = TelemetryCategory::System;
        boot.severity = TelemetrySeverity::Info;
        boot.message = "MCOS runtime constructing telemetry aggregator. PHASE-08 baseline event.";
        telemetryAggregator_->recordEvent(std::move(boot));
    }

    // v0.9.7: hand the aggregator to the worker supervisor so its
    // background watchdog can emit Telemetry::Worker::{Error,Info}
    // events when it reaps a dead instance or auto-respawns one.
    // Pre-v0.9.7 the supervisor's deathLog_ / respawnLog_ vectors
    // collected events but no consumer drained them; operators
    // saw nothing in /api/activity when a worker crashed and
    // got auto-restarted in the background. With this wire-up the
    // operator sees, for example, "Pool worker 'memory#3' exited
    // unexpectedly (was state=ready, exitCode=0) ... Pool 'memory'
    // auto-respawned to instance 'memory#7' (watchdog or reap-driven)"
    // in the activity surface within the watchdog interval (2s).
    if (auto sup = std::dynamic_pointer_cast<WorkerSupervisor>(workerSupervisor_)) {
        sup->setTelemetryAggregator(telemetryAggregator_);
    }

    // v0.9.0: auto-start the gateway at boot when configuration says
    // enabled=true (the new default). Pre-v0.9.0 the gateway only started
    // on an explicit POST /api/gateway/start, so a fresh install left
    // /api/discovery advertising state='disabled' and the mcpUrl
    // unreachable -- exactly what the operator hit testing as a LAN
    // client. Auto-start makes the freshly-installed system self-host
    // its MCP gateway out of the box. v0.9.3: failures are recorded as
    // Severity::Error events (not Info) so the activity log surfaces
    // them without the operator having to query /api/gateway/status.
    if (mcpGateway_ && configurationService_->current().mcpGateway.enabled) {
        const auto bootStatus = mcpGateway_->Start();
        const bool succeeded = (bootStatus.state == GatewayState::Running);
        TelemetryEvent evt;
        evt.category = TelemetryCategory::Gateway;
        evt.severity = succeeded ? TelemetrySeverity::Info : TelemetrySeverity::Error;
        evt.message  = std::string(succeeded ? "Gateway auto-started at boot. " : "Gateway auto-start FAILED at boot. ")
                     + "State: " + bootStatus.message;
        telemetryAggregator_->recordEvent(std::move(evt));
        // v0.9.3: also push to the activity ring so /api/activity
        // shows boot-time gateway lifecycle in the operator's audit log.
        ActivityEvent ringEvt;
        ringEvt.kind    = "gateway_lifecycle";
        ringEvt.actor   = "runtime-boot";
        ringEvt.target  = "auto-start";
        ringEvt.statusCode = succeeded ? 200 : 500;
        ringEvt.message = std::string(succeeded ? "Gateway auto-started at boot. " : "Gateway auto-start FAILED at boot. ")
                        + "State: " + bootStatus.message;
        globalActivityRing().append(ringEvt);
    }

    // PHASE-02: register one stable logical MCP endpoint with the gateway
    // adapter so subsequent phases (PHASE-06 worker pools, PHASE-07 lease
    // routing) have a known registration shape to extend. This is an
    // in-memory registration only; the gateway substrate (the native
    // HTTP.sys adapter as of v0.9.0) routes incoming MCP traffic through
    // the WorkerSupervisor + LeaseRouter stack rather than this entry --
    // the entry is a marker so /api/gateway/status reports a non-zero
    // registration count for honest legibility.
    {
        McpServerRegistration logicalPool;
        logicalPool.name = "mcos-default-pool";
        logicalPool.description = "MCOS default logical MCP pool. Backend instances are managed by the worker supervisor (PHASE-06).";
        logicalPool.transport = McpServerTransport::StreamableHttp;
        logicalPool.url = "http://127.0.0.1:" + std::to_string(configurationService_->current().browserPort) + "/mcp/pools/default/mcp";
        logicalPool.sessionMode = "stateful";
        logicalPool.headers["X-MCOS-Gateway-Source"] = "native";
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

    // v0.7.1: hand the worker layer to AdminApi so dashboard snapshot can
    // compute per-sub-agent utilization + active-client attribution. The
    // supervisor + lease router were both constructed earlier in this same
    // scope (PHASE-06 / PHASE-07) so both shared_ptrs are non-null here.
    // adminApiService_ is typed as the IAdminApiService interface; the
    // method is on the concrete type, so we dynamic_pointer_cast to call.
    if (auto concrete = std::dynamic_pointer_cast<AdminApiService>(adminApiService_)) {
        concrete->AttachWorkerLayer(workerSupervisor_, leaseRouter_);
        // v0.9.15: kick off the background reachability prober so
        // /api/dashboard never blocks on TCP probes. The prober
        // refreshes the cache every 10s; the cache TTL is 30s, so
        // the dashboard always sees a warm cache.
        concrete->StartReachabilityProber();
    }

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
    // v0.9.69: kick off the boot self-test sweep on a background
    // thread. The probes wait a few seconds for pool instances to
    // hand-shake before running, so they don't false-fail on a
    // freshly-spawned worker that's still spelling out its initialize
    // response. Failed probes record kind="self_test" /
    // statusCode=500 activity events that the existing Error
    // Reporting frame on the Overview deck surfaces automatically.
    runBootSelfTestsAsync();
    return true;
}

void MasterControlApplication::Impl::runBootSelfTestsAsync() {
    if (selfTestThreadJoinable_.load()) {
        if (selfTestThread_.joinable()) selfTestThread_.join();
        selfTestThreadJoinable_.store(false);
    }
    selfTestThreadJoinable_.store(true);
    selfTestThread_ = std::thread([this]() {
        // Pool instances need a moment to spawn + complete their MCP
        // handshake. 3s is comfortably past observed handshake latency
        // (~80-200ms per worker on the test rig).
        std::this_thread::sleep_for(std::chrono::seconds(3));
        if (stopRequested_.load()) return;
        auto snap = runBootSelfTestsNow();
        std::lock_guard<std::mutex> lock(selfTestMutex_);
        lastSelfTestSnapshot_ = std::move(snap);
    });
}

namespace {
// v0.9.69: TCP-connect probe used by self-test http_endpoint
// category. Returns {ok, durationMs, message}. Same non-blocking
// connect / 500ms select pattern AdminApiService::probeReachability
// Cached uses, but standalone here so the self-test machinery
// doesn't depend on the AdminApiService internals.
struct TcpProbeResult { bool ok = false; int durationMs = 0; std::string message; };
TcpProbeResult selfTestTcpProbe(const std::string& host, uint16_t port, DWORD timeoutMs) {
    TcpProbeResult out;
    const auto start = std::chrono::steady_clock::now();
    addrinfo hints{};
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    addrinfo* resolved = nullptr;
    const std::string portStr = std::to_string(port);
    const int gai = ::getaddrinfo(host.c_str(), portStr.c_str(), &hints, &resolved);
    if (gai != 0 || resolved == nullptr) {
        out.message = "DNS resolution failed for " + host + ":" + portStr;
        out.durationMs = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count());
        return out;
    }
    bool reachable = false;
    for (addrinfo* it = resolved; it != nullptr && !reachable; it = it->ai_next) {
        SOCKET s = ::socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (s == INVALID_SOCKET) continue;
        u_long nb = 1;
        ::ioctlsocket(s, FIONBIO, &nb);
        const int connectResult = ::connect(s, it->ai_addr, (int)it->ai_addrlen);
        if (connectResult == 0) {
            reachable = true;
        } else if (WSAGetLastError() == WSAEWOULDBLOCK) {
            fd_set ws{}, es{};
            FD_ZERO(&ws); FD_SET(s, &ws);
            FD_ZERO(&es); FD_SET(s, &es);
            timeval tv{};
            tv.tv_sec  = timeoutMs / 1000;
            tv.tv_usec = (timeoutMs % 1000) * 1000;
            if (::select(0, nullptr, &ws, &es, &tv) > 0 && FD_ISSET(s, &ws)) {
                int sockErr = 0; int sockErrLen = sizeof(sockErr);
                ::getsockopt(s, SOL_SOCKET, SO_ERROR,
                             reinterpret_cast<char*>(&sockErr), &sockErrLen);
                reachable = (sockErr == 0);
            }
        }
        ::closesocket(s);
    }
    ::freeaddrinfo(resolved);
    out.ok = reachable;
    out.durationMs = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count());
    if (out.ok) {
        out.message = "TCP connect to " + host + ":" + portStr + " OK in "
                    + std::to_string(out.durationMs) + "ms";
    } else {
        out.message = "TCP connect to " + host + ":" + portStr
                    + " failed after " + std::to_string(out.durationMs) + "ms";
    }
    return out;
}

void emitSelfTestActivity(const SelfTestResult& r) {
    ActivityEvent evt;
    evt.kind       = "self_test";
    evt.actor      = "boot-self-test";
    evt.target     = r.name;
    evt.statusCode = r.ok ? 200 : 500;
    evt.message    = (r.ok ? "PASS: " : "FAIL: ") + r.name + " — " + r.message;
    evt.latencyMs  = r.durationMs;
    globalActivityRing().append(evt);

    // v0.10.2: also route self-test FAILURES to the persistent
    // Diagnostics log so the operator's audit trail survives a
    // service restart and can be inspected from outside MCOS. The
    // operator directive on this surface was "If a test fails, then
    // it MUST be reported in the Overview Error Frame AND reported
    // to the diagnostic module." The activity ring covers the Error
    // Frame; this call covers the diagnostic module side. Passes
    // remain in the activity ring only -- writing every PASS to the
    // persistent jsonl would generate ~30+ lines per boot indefinitely
    // and the per-pass detail is recoverable from the activity ring
    // when needed.
    if (!r.ok) {
        MasterControl::Diagnostics::appendEvent(
            L"runtime",
            "error",
            "self_test_failure",
            r.message,
            nlohmann::json{
                { "name",       r.name },
                { "category",   r.category },
                { "durationMs", r.durationMs },
                { "ranAtUtc",   r.ranAtUtc }
            });
    }
}
} // namespace

SelfTestSnapshot MasterControlApplication::Impl::runBootSelfTestsNow() {
    SelfTestSnapshot snap;
    snap.startedAtUtc = timestampNowUtc();

    auto record = [&](const std::string& name,
                      const std::string& category,
                      bool ok,
                      const std::string& message,
                      int durationMs) {
        SelfTestResult r;
        r.name = name;
        r.category = category;
        r.ok = ok;
        r.message = message;
        r.durationMs = durationMs;
        r.ranAtUtc = timestampNowUtc();
        emitSelfTestActivity(r);
        snap.results.push_back(std::move(r));
    };

    // 1. Admin HTTP server reachable on its bind port.
    const auto cfg = configurationService_ ? configurationService_->current() : AppConfiguration{};
    {
        const auto p = selfTestTcpProbe("127.0.0.1", cfg.browserPort, 1000);
        record("http.admin_port", "http_endpoint", p.ok, p.message, p.durationMs);
    }
    // 2. Gateway listening port reachable.
    if (mcpGateway_) {
        // Parse the gateway URL host:port. Format is http://<host>:<port>/<path>.
        const auto status = mcpGateway_->CurrentStatus();
        std::string host = "127.0.0.1";
        uint16_t port = 8080;
        const auto& url = status.mcpUrl;
        const auto schemeEnd = url.find("://");
        if (schemeEnd != std::string::npos) {
            const auto hostStart = schemeEnd + 3;
            const auto pathStart = url.find('/', hostStart);
            const auto hostPort  = url.substr(hostStart,
                                              pathStart == std::string::npos
                                                ? std::string::npos
                                                : pathStart - hostStart);
            const auto colon = hostPort.find(':');
            if (colon != std::string::npos) {
                host = hostPort.substr(0, colon);
                if (host == "0.0.0.0") host = "127.0.0.1";
                try { port = static_cast<uint16_t>(std::stoi(hostPort.substr(colon + 1))); }
                catch (...) { port = 8080; }
            }
        }
        const auto p = selfTestTcpProbe(host, port, 1000);
        record("gateway.tcp_listen", "gateway", p.ok, p.message, p.durationMs);
        const bool gwRunning = (status.state == GatewayState::Running);
        record("gateway.state_running", "gateway", gwRunning,
               gwRunning ? "Gateway state=running" : ("Gateway state=" + to_string(status.state)),
               0);
        // v0.10.4: assert the gateway is advertising at least one tool.
        // 0 tools means pool registration silently failed (or no pools
        // exist) and external AI clients connecting through the gateway
        // would see an empty tools/list response. Live MCOS deployments
        // expect ~90 tools (one per registered MCP-server / sub-agent
        // catalog entry, summed across the worker pools), so a zero
        // count is a genuine misconfiguration the operator should see.
        const auto toolCount = mcpGateway_->ListTools().size();
        const bool hasTools = toolCount >= 1;
        std::string toolMsg = "Gateway is advertising " + std::to_string(toolCount) + " tools.";
        if (!hasTools) {
            toolMsg += " Expected >= 1; check pool registration and worker handshake state.";
        }
        record("gateway.tool_count_nonzero", "gateway", hasTools, toolMsg, 0);
    }
    // 3. Each registered pool: at least one Ready instance OR
    //    minInstances=0 (intentionally not auto-started).
    if (workerSupervisor_) {
        const auto pools = workerSupervisor_->listPools();
        for (const auto& pool : pools) {
            const auto pStart = std::chrono::steady_clock::now();
            int ready = 0;
            for (const auto& inst : pool.instances) {
                if (inst.state == EndpointInstanceState::Ready) ++ready;
            }
            const bool expectedRunning = pool.scalePolicy.minInstances >= 1;
            const bool ok = expectedRunning ? (ready >= 1) : true;
            std::string msg = "pool=" + pool.poolId + " kind=" + to_string(pool.kind)
                           + " ready=" + std::to_string(ready)
                           + "/" + std::to_string(pool.instances.size());
            if (!expectedRunning) msg += " (minInstances=0; skipped)";
            const int dur = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - pStart).count());
            const std::string category = (pool.kind == EndpointPoolKind::SubAgent)
                ? "sub_agent" : "pool_handshake";
            record("pool." + pool.poolId, category, ok, msg, dur);
        }
    }
    // 4. Activity ring persistence health.
    {
        const auto rh = globalActivityRing().persistenceHealth();
        const bool ok = rh.persistenceEnabled && rh.totalAppendErrors == 0;
        std::string msg = "persistenceEnabled=" + std::string(rh.persistenceEnabled ? "true" : "false")
                       + ", totalAppends=" + std::to_string(rh.totalAppends)
                       + ", totalAppendErrors=" + std::to_string(rh.totalAppendErrors);
        record("activity_ring.persistence", "telemetry", ok, msg, 0);
    }
    // 5. Telemetry sampler liveness.
    if (telemetryService_) {
        const auto t = telemetryService_->captureSnapshot();
        const bool ok = !t.capturedAtUtc.empty();
        record("telemetry.sampler", "telemetry", ok,
               ok ? ("Sampler captured at " + t.capturedAtUtc)
                  : "Sampler has not produced a reading yet",
               0);
    }
    // 6. On-disk worker exe presence.
    {
        const auto exe = paths_.executableDirectory / "mcos-baseline-tools-worker.exe";
        std::error_code ec;
        const bool ok = std::filesystem::exists(exe, ec);
        record("process.baseline_worker_exe", "process", ok,
               ok ? ("Worker exe present at " + exe.string())
                  : ("Worker exe MISSING at " + exe.string()),
               0);
    }
    // 7. v0.10.3: Persistent Diagnostics log writability.
    //    Self-test failures are routed to the persistent log via
    //    MasterControl::Diagnostics::appendEvent (added v0.10.2). If
    //    the destination directory is unwritable (locked-down account,
    //    missing PUBLIC env var, ACL change, etc.) those rows are
    //    silently dropped and the operator loses the audit trail.
    //    Probe writability explicitly so a failure surfaces in the
    //    Overview Error Frame instead of being invisible.
    {
        const auto pStart = std::chrono::steady_clock::now();
        const auto paths = MasterControl::Diagnostics::resolvePersistentLogPaths(L"runtime");
        std::error_code ec;
        std::filesystem::create_directories(paths.componentDirectory, ec);
        bool ok = !ec;
        std::string msg;
        if (!ok) {
            msg = "create_directories(" + paths.componentDirectory.string() + "): " + ec.message();
        } else {
            const auto probe = paths.componentDirectory / ".write-probe";
            {
                std::ofstream out(probe, std::ios::binary | std::ios::trunc);
                ok = out.is_open();
                if (ok) {
                    out << "{}";
                    ok = out.good();
                }
            }
            if (ok) {
                std::filesystem::remove(probe, ec);
                msg = "Diagnostics log directory writable at " + paths.componentDirectory.string();
            } else {
                msg = "Diagnostics log directory write failed at " + paths.componentDirectory.string();
            }
        }
        const int dur = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - pStart).count());
        record("diagnostics.log_writable", "telemetry", ok, msg, dur);
    }

    snap.finishedAtUtc = timestampNowUtc();
    snap.totalCount = static_cast<int>(snap.results.size());
    snap.passedCount = 0;
    for (const auto& r : snap.results) if (r.ok) ++snap.passedCount;
    snap.failedCount = snap.totalCount - snap.passedCount;

    // Aggregate result -> single activity event so a watcher reading
    // the audit log sees one PASS/FAIL summary alongside the per-probe
    // detail rows.
    {
        ActivityEvent evt;
        evt.kind       = "self_test_summary";
        evt.actor      = "boot-self-test";
        evt.target     = "boot";
        evt.statusCode = (snap.failedCount == 0) ? 200 : 500;
        evt.message    = "Boot self-test "
                       + std::to_string(snap.passedCount) + "/"
                       + std::to_string(snap.totalCount) + " passed";
        globalActivityRing().append(evt);
    }
    // v0.10.2: also write the summary to the persistent Diagnostics
    // log so each boot leaves one clear audit-trail line on disk
    // (operator directive on visible self-tests + diagnostic module).
    // Severity is "info" when all probes passed and "warn" when any
    // failed; the per-failure detail rows already wrote at "error"
    // severity inside emitSelfTestActivity above.
    MasterControl::Diagnostics::appendEvent(
        L"runtime",
        snap.failedCount == 0 ? "info" : "warn",
        "self_test_summary",
        std::string("Boot self-test ")
            + std::to_string(snap.passedCount) + "/"
            + std::to_string(snap.totalCount) + " passed",
        nlohmann::json{
            { "passed",        snap.passedCount },
            { "failed",        snap.failedCount },
            { "total",         snap.totalCount },
            { "startedAtUtc",  snap.startedAtUtc },
            { "finishedAtUtc", snap.finishedAtUtc }
        });
    return snap;
}

void MasterControlApplication::Impl::shutdown() {
    if (!initialized_) {
        return;
    }
    // v0.9.69: signal the self-test thread to bail (it sleeps 3s
    // before running) and join so a fast stop doesn't leave it
    // running into a torn-down runtime.
    stopRequested_.store(true);
    if (selfTestThreadJoinable_.load() && selfTestThread_.joinable()) {
        selfTestThread_.join();
        selfTestThreadJoinable_.store(false);
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
    // v0.9.78: supervisor heartbeat watchdog. Every ~5s, ask the
    // supervisor service to flip Connected -> Disconnected if its
    // lastHeartbeatUtc is older than the threshold. When a transition
    // happens, emit a supervisor_disconnected activity event so the
    // operator sees the lifecycle change in /api/activity + the
    // Overview Error Reporting card. Threshold is generous (120s) so
    // a single missed heartbeat from a connector with retry/backoff
    // doesn't trigger spurious disconnects.
    constexpr auto kSupervisorHeartbeatThreshold = std::chrono::seconds(120);
    constexpr auto kSupervisorSweepInterval = std::chrono::seconds(5);
    auto lastSupervisorSweep = std::chrono::steady_clock::now();
    while (!stopRequested_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        if (supervisorAssignmentService_) {
            const auto now = std::chrono::steady_clock::now();
            if (now - lastSupervisorSweep >= kSupervisorSweepInterval) {
                lastSupervisorSweep = now;
                if (supervisorAssignmentService_->expireConnectionIfStale(kSupervisorHeartbeatThreshold)) {
                    const auto status = supervisorAssignmentService_->getStatus();
                    ActivityEvent evt;
                    evt.kind = "supervisor_disconnected";
                    evt.actor = "supervisor-watchdog";
                    evt.target = MasterControl::providerIdString(status.provider);
                    evt.statusCode = 504;
                    evt.message = std::string("Supervisor watchdog flipped state to Disconnected (no heartbeat for >")
                        + std::to_string(kSupervisorHeartbeatThreshold.count()) + "s). Assignment "
                        + status.assignmentId + ".";
                    globalActivityRing().append(evt);
                    // v0.9.92: persistent diagnostics mirror. Watchdog
                    // disconnects are operator-relevant: they often
                    // indicate the remote supervisor client crashed or
                    // lost network. Severity=warn (not error) so the
                    // log marker is visible without flagging health.ok.
                    MasterControl::Diagnostics::appendEvent(
                        L"supervisor",
                        "warn",
                        "supervisor_disconnected",
                        evt.message,
                        nlohmann::json{
                            { "providerId", MasterControl::providerIdString(status.provider) },
                            { "assignmentId", status.assignmentId },
                            { "thresholdSeconds", static_cast<int64_t>(kSupervisorHeartbeatThreshold.count()) }
                        });
                }
            }
        }
    }
    return 0;
}

std::string MasterControlApplication::Impl::browserUrl() const {
    const auto configuration = configurationService_->current();
    const auto host = configuration.bindAddress == "0.0.0.0" ? "127.0.0.1" : configuration.bindAddress;
    // v0.9.3: bracket IPv6 host literals (operator-set bindAddress could
    // be either family).
    return "http://" + bracketIpv6Host(host) + ":" + std::to_string(configuration.browserPort) + "/";
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

// v0.9.28: enumerate the supported methods for known operator API
// paths so an unsupported method on a known path returns 405 with
// an Allow header per RFC 7231 §6.5.5, instead of falling through
// to a 404 that hides whether the path exists at all.
//
// Maintenance: when adding a new route to handleHttpRequest below,
// add the (path, methods) pair here so verb-mismatch on the new
// route returns 405. The exact-match table covers literal path
// matches; the prefix table covers the routes that use
// startsWith() / rfind(... , 0) == 0 dispatch. HEAD and OPTIONS are
// added to every entry's Allow header automatically because the
// HEAD->GET rewrite and OPTIONS preflight short-circuit live in
// SimpleHttpServer::handleClient and apply universally.
static std::vector<std::string> supportedMethodsForPath(const std::string& path) {
    // Strip query string for the lookup -- /api/foo?bar=baz is the
    // same logical resource as /api/foo for method-allow purposes.
    std::string p = path;
    const auto q = p.find('?');
    if (q != std::string::npos) {
        p = p.substr(0, q);
    }

    static const std::unordered_map<std::string, std::vector<std::string>> kExact = {
        // Liveness / version
        {"/api/health", {"GET"}},
        {"/api/version", {"GET"}},
        {"/api/health/summary", {"GET"}},
        {"/api/host/telemetry", {"GET"}},
        {"/api/readiness", {"GET"}},
        {"/api/activity/health", {"GET"}},
        // Discovery / onboarding / governance
        {"/.well-known/mcos.json", {"GET"}},
        {"/api/discovery", {"GET"}},
        {"/api/onboarding", {"GET"}},
        {"/api/beacon", {"GET"}},
        {"/api/environment-hints", {"GET"}},
        {"/api/governance/profile", {"GET"}},
        {"/api/governance/bundles", {"GET"}},
        {"/api/governance/decisions", {"GET", "POST"}},
        // Snapshot / config
        {"/api/dashboard", {"GET"}},
        {"/api/config", {"GET", "POST"}},
        {"/api/exports", {"GET"}},
        {"/api/clu", {"GET"}},
        {"/api/clu/tools", {"GET"}},
        {"/api/clu/apple-operations", {"GET"}},
        {"/api/clu/apple-operations/cancel", {"POST"}},
        {"/api/clu/approvals", {"GET"}},
        {"/api/clu/execute", {"POST"}},
        {"/api/forsetti/surface", {"GET"}},
        {"/api/forsetti/modules", {"GET"}},
        {"/api/forsetti/modules/state", {"POST"}},
        // Pools
        {"/api/pools", {"GET", "POST"}},
        // Telemetry
        {"/api/telemetry/clients", {"GET"}},
        {"/api/telemetry/gateway", {"GET"}},
        {"/api/telemetry/heartbeat", {"POST"}},
        // Gateway control
        {"/api/gateway/status", {"GET"}},
        {"/api/gateway/health", {"GET"}},
        {"/api/gateway/tools", {"GET"}},
        {"/api/gateway/start", {"POST"}},
        {"/api/gateway/stop", {"POST"}},
        {"/api/gateway/restart", {"POST"}},
        // Client surfaces
        {"/api/client/mcp-servers", {"GET"}},
        {"/api/client/sub-agents", {"GET"}},
        {"/api/client/activity", {"GET"}},
        {"/api/client/governance/profile", {"GET"}},
        {"/api/client/governance/decisions", {"POST"}},
        {"/api/client/heartbeat", {"POST"}},
        {"/api/clients", {"GET", "POST"}},
        // Setup / install
        {"/api/setup/start", {"POST"}},
        {"/api/setup/complete", {"POST"}},
        {"/api/setup/reset", {"POST"}},
        {"/api/setup/dependencies", {"GET"}},
        {"/api/setup/workflow-templates", {"GET"}},
        {"/api/install/history", {"GET"}},
        {"/api/install/package", {"POST"}},
        {"/api/install/repo", {"POST"}},
        {"/api/install/zip", {"POST"}},
        // Settings / runtime registration
        {"/api/settings/advanced-mode", {"POST"}},
        {"/api/runtime/mcp-servers", {"POST"}},
        {"/api/runtime/mcp-servers/remove", {"POST"}},
        {"/api/runtime/subagents", {"POST"}},
        {"/api/runtime/subagents/remove", {"POST"}},
        {"/api/runtime/subagent-groups", {"POST"}},
        {"/api/runtime/subagent-groups/remove", {"POST"}},
        // Platform services
        {"/api/platform-services", {"GET"}},
        {"/api/platform-services/gateways", {"GET"}},
        {"/api/platform-services/governance", {"GET"}},
        {"/api/platform-services/apple-hosts", {"GET", "POST"}},
        {"/api/platform-services/apple-hosts/remove", {"POST"}},
        // Plugin status
        {"/api/claude-plugin/status", {"GET"}},
        {"/api/claude-plugin/toggle", {"POST"}},
        // v0.9.56: operator-facing diagnostic surface. Returns the
        // same per-entry runtime stats as /api/dashboard but bundled
        // with an aggregate count by installState so the operator
        // can answer "how many of my catalog entries are actually
        // live, supervised, or just placeholders?" in one request.
        {"/api/diagnostics/runtime-stats", {"GET"}},
        // v0.9.69: boot-time self-test snapshot + re-run trigger.
        {"/api/self-tests",     {"GET"}},
        {"/api/self-tests/run", {"POST"}},
        // v0.9.71: real-time SSE push channel for dashboard +
        // activity events. Connection stays open until the client
        // disconnects or the server stops.
        {"/api/events",         {"GET"}},
        // v0.9.73: Forsetti Agentic Edition governance surface.
        // The manifest endpoint catalogs vendored documents +
        // policies + agents + contracts + schemas + standards by
        // path; the document endpoint serves them by relative path.
        {"/api/governance/agentic-edition/manifest", {"GET"}},
        // v0.9.76: Supervisor Agent Assignment Wizard surface.
        {"/api/supervisor/assignment",        {"GET"}},
        {"/api/supervisor/assignment/select", {"POST"}},
        {"/api/supervisor/assignment/revoke", {"POST"}},
        {"/api/supervisor/config/generate",   {"POST"}},
        {"/api/supervisor/connect/confirm",   {"POST"}},
        {"/api/supervisor/heartbeat",         {"POST"}},
        {"/api/supervisor/status",            {"GET"}},
    };

    static const std::vector<std::pair<std::string, std::vector<std::string>>> kPrefix = {
        // Note: longer prefixes first so "/api/setup/dependencies/" wins
        // over "/api/setup/" if both ever overlap.
        {"/api/onboarding/", {"GET"}},
        {"/api/governance/bundles/", {"GET"}},
        {"/api/setup/dependencies/", {"POST"}},
        {"/api/platform-services/config/", {"GET"}},
        {"/api/clients/", {"GET", "DELETE"}},
        {"/api/leases/", {"POST"}},
        {"/api/pools/", {"GET", "POST"}},
        {"/api/telemetry/events", {"GET"}},
        {"/api/activity", {"GET"}},
        {"/mcp/gateway/", {"GET"}},
        {"/mcp/governance/", {"GET", "POST"}},
        // v0.9.73: prefix route for fetching individual Forsetti
        // Agentic Edition documents by relative path. Manifest
        // endpoint is exact-match above.
        {"/api/governance/agentic-edition/document/", {"GET"}},
    };

    auto exact = kExact.find(p);
    if (exact != kExact.end()) {
        return exact->second;
    }
    for (const auto& entry : kPrefix) {
        const auto& prefix = entry.first;
        if (p.size() >= prefix.size()
            && p.compare(0, prefix.size(), prefix) == 0) {
            return entry.second;
        }
    }
    return {};
}

// v0.9.28: build a canonical Allow-header value from a route's
// supported method list. Always surfaces HEAD when GET is supported
// (the HEAD->GET rewrite makes this transparently true) and always
// surfaces OPTIONS (preflight short-circuit handles it for every
// path). Keeps the header in stable verb order so caches don't see
// spurious vary churn between requests.
static std::string buildAllowHeader(const std::vector<std::string>& methods) {
    static const std::vector<std::string> kOrder = {
        "GET", "HEAD", "POST", "PUT", "PATCH", "DELETE", "OPTIONS"
    };
    std::unordered_set<std::string> set(methods.begin(), methods.end());
    if (set.count("GET")) {
        set.insert("HEAD");
    }
    set.insert("OPTIONS");
    std::string allow;
    for (const auto& verb : kOrder) {
        if (set.count(verb)) {
            if (!allow.empty()) {
                allow += ", ";
            }
            allow += verb;
        }
    }
    return allow;
}

void MasterControlApplication::Impl::refreshSupervisorEndpoints() {
    // v0.10.8: mirror DiscoveryService::currentDocument() precedence chain
    // so the supervisor config carries the same LAN-routable URL the
    // well-known discovery doc advertises. Run on-demand from the route
    // layer because the supervisor service is constructed in
    // initialize() -- before the first telemetry capture -- and the
    // initial mcpEndpoint stamped into the context is necessarily a
    // 127.0.0.1 fallback. By the time the operator clicks "Generate
    // Config" the snapshot tick has run and the primary IPv4 LAN
    // address is known, so we refresh just-in-time.
    if (!supervisorAssignmentService_) return;
    if (!configurationService_) return;

    const auto cfg = configurationService_->current();
    std::string lanIp;
    const std::string& bindAddress = cfg.bindAddress;
    const std::string& preferred = cfg.activeProfile.preferredBindAddress;
    if (!bindAddress.empty()
        && bindAddress != "0.0.0.0"
        && bindAddress != "::"
        && bindAddress != "[::]") {
        lanIp = bindAddress;
    }
    if ((lanIp.empty() || lanIp == "0.0.0.0")
        && !preferred.empty() && preferred != "0.0.0.0") {
        lanIp = preferred;
    }
    if ((lanIp.empty() || lanIp == "0.0.0.0") && telemetryService_) {
        try {
            const auto snap = telemetryService_->captureSnapshot();
            if (!snap.primaryIpAddress.empty()) {
                lanIp = snap.primaryIpAddress;
            }
        } catch (...) {
            // telemetry capture is best-effort here; fall through to
            // the bind/loopback fallbacks below.
        }
    }
    if (lanIp.empty() || lanIp == "0.0.0.0") {
        lanIp = cfg.bindAddress;
    }
    if (lanIp.empty() || lanIp == "0.0.0.0") {
        lanIp = "127.0.0.1";
    }

    const auto hostInUrl = bracketIpv6Host(lanIp);
    const auto adminBase = std::string("http://") + hostInUrl + ":" + std::to_string(cfg.browserPort);
    const auto gatewayMcpPath = cfg.mcpGateway.mcpPath.empty() ? std::string("/mcp") : cfg.mcpGateway.mcpPath;
    const auto gatewayBase = std::string("http://") + hostInUrl + ":" + std::to_string(cfg.mcpGateway.listenPort);
    const auto mcpEndpoint = gatewayBase + gatewayMcpPath;
    const auto discoveryEndpoint = adminBase + "/.well-known/mcos.json";
    const auto serverName = cfg.instanceName.empty()
        ? std::string("Master Control Orchestration Server")
        : cfg.instanceName;
    const auto fingerprintSeed = lanIp + ":" + std::to_string(cfg.browserPort) + "|" + serverName;

    supervisorAssignmentService_->setEndpoints(mcpEndpoint, discoveryEndpoint, fingerprintSeed);
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

    // v0.9.71: real-time push channel. /api/events upgrades to a
    // Server-Sent Events stream that emits the current dashboard
    // snapshot, the latest health summary, and any new activity-ring
    // entries on every tick. Dashboard JS opens an EventSource and
    // replaces its 2s polling interval with stream-driven updates so
    // every tab/view reflects new state within a beat. Falls back to
    // polling automatically when the connection drops (EventSource
    // re-connects on its own; the dashboard's polling timer is left
    // running at a slower 5s cadence as a safety net).
    if (request.method == "GET" && request.path == "/api/events") {
        HttpResponse resp;
        resp.statusCode = 200;
        resp.streamMode = true;
        resp.streamHandler = [this](SOCKET client, std::atomic<bool>* serverRunning) {
            auto sendEvent = [&](const std::string& eventName,
                                 const std::string& jsonBody) -> bool {
                std::ostringstream frame;
                frame << "event: " << eventName << "\n";
                // Split on \n so each line is prefixed with "data:"
                // per RFC SSE; nlohmann's dump(0) emits no newlines
                // so the entire body lives on a single data: line,
                // but we still iterate to be safe.
                std::istringstream lineStream(jsonBody);
                std::string line;
                while (std::getline(lineStream, line)) {
                    frame << "data: " << line << "\n";
                }
                frame << "\n";
                const auto bytes = frame.str();
                int sent = ::send(client, bytes.c_str(),
                                  static_cast<int>(bytes.size()), 0);
                return sent == static_cast<int>(bytes.size());
            };
            // Initial keepalive comment so EventSource clients fire
            // 'open' immediately. Per SSE spec, lines starting with
            // ':' are ignored by the client.
            const std::string hello = ": mcos sse stream open\n\n";
            ::send(client, hello.c_str(), static_cast<int>(hello.size()), 0);

            std::string lastActivityId;
            std::string lastSnapshotHash;
            int loopsWithoutChange = 0;

            while (true) {
                if (serverRunning && !serverRunning->load()) break;
                if (stopRequested_.load()) break;
                // Snapshot tick.
                try {
                    if (adminApiService_) {
                        const auto snap = adminApiService_->snapshot();
                        const std::string body = nlohmann::json(snap).dump();
                        // Cheap "did anything change" hash so unchanged
                        // payloads can be skipped (saves bytes for idle
                        // clients). Use std::hash on the dumped string.
                        const auto h = std::to_string(std::hash<std::string>{}(body));
                        if (h != lastSnapshotHash) {
                            lastSnapshotHash = h;
                            loopsWithoutChange = 0;
                            if (!sendEvent("dashboard", body)) break;
                        } else {
                            ++loopsWithoutChange;
                        }
                    }
                } catch (...) {
                    // Snapshot threw -- skip this tick.
                }
                // Activity-event tick. Stream events appended since
                // the high-water mark we last sent. ActivityEventRing's
                // public read() takes (sinceId, maxCount).
                try {
                    const auto recent = globalActivityRing().read(
                        lastActivityId, 100);
                    if (!recent.events.empty()) {
                        for (const auto& evt : recent.events) {
                            const std::string body = nlohmann::json(evt).dump();
                            if (!sendEvent("activity", body)) break;
                        }
                        lastActivityId = recent.highWaterMarkId;
                    } else if (lastActivityId.empty()) {
                        // First tick with no events -- still record
                        // the high-water mark so subsequent ticks ask
                        // for "events newer than this".
                        lastActivityId = recent.highWaterMarkId;
                    }
                } catch (...) {
                    // Activity ring threw -- skip this tick.
                }
                // Heartbeat keepalive every 15s of no-change so proxies
                // / browsers don't time out the idle connection.
                if (loopsWithoutChange > 0 && loopsWithoutChange % 15 == 0) {
                    const std::string ping = ": ping\n\n";
                    if (::send(client, ping.c_str(),
                               static_cast<int>(ping.size()), 0)
                        != static_cast<int>(ping.size())) {
                        break;
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            }
        };
        return resp;
    }

    // v0.9.73: Forsetti Agentic Edition governance surface. The new
    // governance authority (FORSETTI_CONSTITUTION.md, COMPLIANCE_
    // POLICY.md, CHANGE_CONTROL_POLICY.md, RELEASE_POLICY.md,
    // DOCUMENTATION_POLICY.md, VISION.md, AGENTS.md, agents/*.md,
    // contracts/*.md, core/policies/*.json, policies/*.json,
    // schemas/*.json, standards/*.md) is vendored at
    // <install>/share/Forsetti-Agentic-Edition/. CLU surfaces it
    // directly so any LAN AI client (or operator-side tool) can
    // pull the canonical authority documents over the gateway
    // without a separate distribution channel. Per operator
    // directive: this governance supersedes any previous governance.
    if (request.method == "GET" && request.path == "/api/governance/agentic-edition/manifest") {
        auto root = paths_.executableDirectory / "share" / "Forsetti-Agentic-Edition";
        if (!std::filesystem::exists(root)) {
            return HttpResponse{ 503, "application/json",
                nlohmann::json{
                    { "available", false },
                    { "expectedRoot", root.string() },
                    { "hint", "Vendor the forsetti-agentic-edition source tree under share/Forsetti-Agentic-Edition/ in the install directory." }
                }.dump() };
        }
        nlohmann::json manifest = {
            { "available", true },
            { "root",      root.string() },
            { "documents", nlohmann::json::array() },
            { "policies",  nlohmann::json::array() },
            { "agents",    nlohmann::json::array() },
            { "contracts", nlohmann::json::array() },
            { "schemas",   nlohmann::json::array() },
            { "standards", nlohmann::json::array() }
        };
        std::error_code ec;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(root, ec)) {
            if (ec) break;
            if (!entry.is_regular_file()) continue;
            auto rel = std::filesystem::relative(entry.path(), root, ec);
            if (ec) continue;
            std::string relStr = rel.generic_string();
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](unsigned char c){ return std::tolower(c); });
            const auto sz = static_cast<int64_t>(std::filesystem::file_size(entry.path(), ec));
            nlohmann::json item = {
                { "path",      relStr },
                { "sizeBytes", ec ? 0 : sz }
            };
            // Bucket by location so consumers can navigate by
            // category. The same file is also surfaced in the flat
            // 'documents' array so a client that just wants
            // everything can iterate one list.
            manifest["documents"].push_back(item);
            const std::string lower = [&]() {
                std::string s = relStr;
                std::transform(s.begin(), s.end(), s.begin(),
                               [](unsigned char c){ return std::tolower(c); });
                return s;
            }();
            if ((lower.rfind("policies/", 0) == 0 || lower.rfind("core/policies/", 0) == 0)
                && ext == ".json") {
                manifest["policies"].push_back(item);
            }
            if (lower.rfind("agents/", 0) == 0 && ext == ".md") {
                manifest["agents"].push_back(item);
            }
            if (lower.rfind("contracts/", 0) == 0 && ext == ".md") {
                manifest["contracts"].push_back(item);
            }
            if (lower.rfind("schemas/", 0) == 0 && ext == ".json") {
                manifest["schemas"].push_back(item);
            }
            if (lower.rfind("standards/", 0) == 0 && ext == ".md") {
                manifest["standards"].push_back(item);
            }
        }
        return HttpResponse{ 200, "application/json", manifest.dump() };
    }
    if (request.method == "GET" &&
        request.path.rfind("/api/governance/agentic-edition/document/", 0) == 0) {
        // Strip the prefix and look up the file under the vendored
        // directory. Reject any path that contains '..' to prevent
        // traversal outside the governance tree.
        const std::string prefix = "/api/governance/agentic-edition/document/";
        std::string rel = request.path.substr(prefix.size());
        if (rel.find("..") != std::string::npos
            || rel.find('\\') != std::string::npos) {
            return HttpResponse{ 400, "application/json",
                nlohmann::json{ { "error", "Path traversal rejected." } }.dump() };
        }
        auto root = paths_.executableDirectory / "share" / "Forsetti-Agentic-Edition";
        auto target = root / rel;
        std::error_code ec;
        if (!std::filesystem::exists(target, ec) || std::filesystem::is_directory(target, ec)) {
            return HttpResponse{ 404, "application/json",
                nlohmann::json{ { "error", "Document not found." },
                                { "requested", rel } }.dump() };
        }
        std::ifstream in(target, std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        std::string body = ss.str();
        std::string contentType = "text/plain; charset=utf-8";
        const auto ext = target.extension().string();
        if (ext == ".md")   contentType = "text/markdown; charset=utf-8";
        if (ext == ".json") contentType = "application/json";
        if (ext == ".html") contentType = "text/html; charset=utf-8";
        return HttpResponse{ 200, contentType, std::move(body) };
    }

    // v0.9.76: Supervisor Agent Assignment Wizard surface.
    // Six routes implementing the lifecycle described in the
    // Supervisor Agent Assignment Wizard Implementation Package
    // (docs/IMPLEMENTATION_INSTRUCTIONS.md, BACKEND_SPEC.md):
    //
    //   GET  /api/supervisor/assignment        - current SupervisorAssignment
    //   POST /api/supervisor/assignment/select - pick provider, issue config
    //   POST /api/supervisor/assignment/revoke - revoke active/pending
    //   POST /api/supervisor/config/generate   - re-issue config without revoke
    //   POST /api/supervisor/connect/confirm   - remote client confirms
    //   GET  /api/supervisor/status            - compact status for UI card
    //
    // All routes audit-log to the activity ring with kind="supervisor_*"
    // so the operator can audit-trail through /api/activity. LAN-trust:
    // no app-layer auth is required to *reach* the surface (the trust
    // model is enforced at the network level per .claude/rules/00-mcos-
    // realignment.md). Server-side capability validation enforces the
    // forbidden-by-default tool list for every confirm-connection call.
    if (request.method == "GET" && request.path == "/api/supervisor/assignment") {
        if (!supervisorAssignmentService_) {
            return HttpResponse{ 503, "application/json",
                nlohmann::json{ {"ok", false}, {"errorMessage", "Supervisor service not initialized."} }.dump() };
        }
        auto body = MasterControl::toJson(supervisorAssignmentService_->getCurrentAssignment());
        return HttpResponse{ 200, "application/json", body.dump() };
    }
    if (request.method == "GET" && request.path == "/api/supervisor/status") {
        if (!supervisorAssignmentService_) {
            return HttpResponse{ 503, "application/json",
                nlohmann::json{ {"ok", false}, {"errorMessage", "Supervisor service not initialized."} }.dump() };
        }
        auto body = MasterControl::toJson(supervisorAssignmentService_->getStatus());
        return HttpResponse{ 200, "application/json", body.dump() };
    }
    if (request.method == "POST" && request.path == "/api/supervisor/assignment/select") {
        if (!supervisorAssignmentService_) {
            return HttpResponse{ 503, "application/json",
                nlohmann::json{ {"ok", false}, {"errorMessage", "Supervisor service not initialized."} }.dump() };
        }
        nlohmann::json parsed;
        try { parsed = nlohmann::json::parse(request.body); }
        catch (...) {
            return HttpResponse{ 400, "application/json",
                nlohmann::json{ {"ok", false}, {"errorMessage", "Request body is not valid JSON."} }.dump() };
        }
        MasterControl::SupervisorSelectRequest selectRequest;
        const auto err = MasterControl::parseSelectRequest(parsed, selectRequest);
        if (!err.empty()) {
            return HttpResponse{ 400, "application/json",
                nlohmann::json{ {"ok", false}, {"errorMessage", err} }.dump() };
        }
        // v0.10.8: refresh the supervisor's stamped endpoints right
        // before issuing the config so the JSON carries the LAN-routable
        // gateway URL the remote ChatGPT/Claude/Grok client must reach.
        refreshSupervisorEndpoints();
        const auto issue = supervisorAssignmentService_->selectAndIssue(selectRequest);
        {
            ActivityEvent evt;
            evt.kind = "supervisor_select";
            evt.actor = context.actor;
            evt.target = MasterControl::providerIdString(selectRequest.provider);
            evt.statusCode = issue.ok ? 200 : 400;
            evt.message = issue.ok
                ? std::string("Supervisor selected: ")
                    + MasterControl::providerDisplayName(selectRequest.provider)
                    + " (assignment " + issue.assignment.assignmentId
                    + ", config " + issue.assignment.configId + ")."
                : std::string("Supervisor select rejected: ") + issue.errorMessage;
            globalActivityRing().append(evt);
            // v0.9.92: dual-emit to the persistent Diagnostics module so
            // supervisor lifecycle survives in <PUBLIC>\Documents\...\
            // logs\supervisor\events.jsonl across service restarts.
            MasterControl::Diagnostics::appendEvent(
                L"supervisor",
                issue.ok ? "info" : "error",
                "supervisor_select",
                evt.message,
                nlohmann::json{
                    { "providerId", MasterControl::providerIdString(selectRequest.provider) },
                    { "assignmentId", issue.assignment.assignmentId },
                    { "configId", issue.assignment.configId },
                    { "actor", context.actor }
                });
        }
        return HttpResponse{ issue.ok ? 200 : 400, "application/json",
            MasterControl::toJson(issue).dump() };
    }
    if (request.method == "POST" && request.path == "/api/supervisor/config/generate") {
        if (!supervisorAssignmentService_) {
            return HttpResponse{ 503, "application/json",
                nlohmann::json{ {"ok", false}, {"errorMessage", "Supervisor service not initialized."} }.dump() };
        }
        // Body is optional. If providerId is supplied, treat this as
        // shorthand for select+issue so the WinUI Shell can post a
        // single request from the wizard. Otherwise re-emit the
        // config for the existing assignment.
        nlohmann::json parsed = nlohmann::json::object();
        if (!request.body.empty()) {
            try { parsed = nlohmann::json::parse(request.body); }
            catch (...) {
                return HttpResponse{ 400, "application/json",
                    nlohmann::json{ {"ok", false}, {"errorMessage", "Request body is not valid JSON."} }.dump() };
            }
        }
        if (parsed.is_object() && parsed.contains("providerId") && parsed["providerId"].is_string()) {
            MasterControl::SupervisorSelectRequest selectRequest;
            const auto err = MasterControl::parseSelectRequest(parsed, selectRequest);
            if (!err.empty()) {
                return HttpResponse{ 400, "application/json",
                    nlohmann::json{ {"ok", false}, {"errorMessage", err} }.dump() };
            }
            // v0.10.8: refresh endpoints so the (re)issued config has
            // the LAN-routable gateway URL, not the 127.0.0.1 fallback.
            refreshSupervisorEndpoints();
            const auto issue = supervisorAssignmentService_->selectAndIssue(selectRequest);
            {
                ActivityEvent evt;
                evt.kind = "supervisor_config_issue";
                evt.actor = context.actor;
                evt.target = MasterControl::providerIdString(selectRequest.provider);
                evt.statusCode = issue.ok ? 200 : 400;
                evt.message = issue.ok
                    ? std::string("Supervisor config generated for ")
                        + MasterControl::providerDisplayName(selectRequest.provider)
                        + " (config " + issue.assignment.configId + ")."
                    : std::string("Supervisor config generation rejected: ") + issue.errorMessage;
                globalActivityRing().append(evt);
                // v0.9.93: persistent diagnostics mirror (parity with the
                // other supervisor_* lifecycle dual-emits added in v0.9.92).
                MasterControl::Diagnostics::appendEvent(
                    L"supervisor",
                    issue.ok ? "info" : "error",
                    "supervisor_config_issue",
                    evt.message,
                    nlohmann::json{
                        { "providerId", MasterControl::providerIdString(selectRequest.provider) },
                        { "assignmentId", issue.assignment.assignmentId },
                        { "configId", issue.assignment.configId },
                        { "actor", context.actor }
                    });
            }
            return HttpResponse{ issue.ok ? 200 : 400, "application/json",
                MasterControl::toJson(issue).dump() };
        }
        // v0.10.8: regenerate-without-providerId also refreshes endpoints
        // so the new configId carries the LAN-routable URL.
        refreshSupervisorEndpoints();
        const auto issue = supervisorAssignmentService_->regenerateConfig();
        {
            ActivityEvent evt;
            evt.kind = "supervisor_config_regenerate";
            evt.actor = context.actor;
            evt.target = issue.ok ? MasterControl::providerIdString(issue.assignment.provider) : "";
            evt.statusCode = issue.ok ? 200 : 400;
            evt.message = issue.ok
                ? std::string("Supervisor config regenerated (config ")
                    + issue.assignment.configId + ")."
                : std::string("Supervisor config regenerate rejected: ") + issue.errorMessage;
            globalActivityRing().append(evt);
            // v0.9.93: persistent diagnostics mirror.
            MasterControl::Diagnostics::appendEvent(
                L"supervisor",
                issue.ok ? "info" : "error",
                "supervisor_config_regenerate",
                evt.message,
                nlohmann::json{
                    { "providerId", MasterControl::providerIdString(issue.assignment.provider) },
                    { "assignmentId", issue.assignment.assignmentId },
                    { "configId", issue.assignment.configId },
                    { "actor", context.actor }
                });
        }
        return HttpResponse{ issue.ok ? 200 : 400, "application/json",
            MasterControl::toJson(issue).dump() };
    }
    if (request.method == "POST" && request.path == "/api/supervisor/assignment/revoke") {
        if (!supervisorAssignmentService_) {
            return HttpResponse{ 503, "application/json",
                nlohmann::json{ {"ok", false}, {"errorMessage", "Supervisor service not initialized."} }.dump() };
        }
        std::string reason;
        if (!request.body.empty()) {
            try {
                const auto parsed = nlohmann::json::parse(request.body);
                if (parsed.is_object()) reason = parsed.value("reason", std::string{});
            } catch (...) { /* allow empty body */ }
        }
        const auto previous = supervisorAssignmentService_->getCurrentAssignment();
        supervisorAssignmentService_->revoke(reason);
        {
            ActivityEvent evt;
            evt.kind = "supervisor_revoke";
            evt.actor = context.actor;
            evt.target = MasterControl::providerIdString(previous.provider);
            evt.statusCode = 200;
            evt.message = std::string("Supervisor revoked")
                + (previous.assignmentId.empty()
                    ? std::string(" (no prior assignment).")
                    : std::string(" (assignment ") + previous.assignmentId + "). Reason: ")
                + (reason.empty() ? std::string("operator-initiated") : reason) + ".";
            globalActivityRing().append(evt);
            // v0.9.92: persistent diagnostics mirror.
            MasterControl::Diagnostics::appendEvent(
                L"supervisor",
                "warn",
                "supervisor_revoke",
                evt.message,
                nlohmann::json{
                    { "providerId", MasterControl::providerIdString(previous.provider) },
                    { "assignmentId", previous.assignmentId },
                    { "reason", reason },
                    { "actor", context.actor }
                });
        }
        nlohmann::json body = nlohmann::json::object();
        body["ok"] = true;
        body["assignment"] = MasterControl::toJson(supervisorAssignmentService_->getCurrentAssignment());
        return HttpResponse{ 200, "application/json", body.dump() };
    }
    if (request.method == "POST" && request.path == "/api/supervisor/heartbeat") {
        if (!supervisorAssignmentService_) {
            return HttpResponse{ 503, "application/json",
                nlohmann::json{ {"ok", false}, {"errorMessage", "Supervisor service not initialized."} }.dump() };
        }
        std::string clientId;
        if (!request.body.empty()) {
            try {
                const auto parsed = nlohmann::json::parse(request.body);
                if (parsed.is_object()) clientId = parsed.value("clientId", std::string{});
            } catch (...) { /* tolerate empty / malformed body */ }
        }
        supervisorAssignmentService_->recordHeartbeat(clientId);
        auto status = supervisorAssignmentService_->getStatus();
        return HttpResponse{ 200, "application/json",
            MasterControl::toJson(status).dump() };
    }
    if (request.method == "POST" && request.path == "/api/supervisor/connect/confirm") {
        if (!supervisorAssignmentService_) {
            return HttpResponse{ 503, "application/json",
                nlohmann::json{ {"ok", false}, {"errorMessage", "Supervisor service not initialized."} }.dump() };
        }
        nlohmann::json parsed;
        try { parsed = nlohmann::json::parse(request.body); }
        catch (...) {
            return HttpResponse{ 400, "application/json",
                nlohmann::json{ {"ok", false}, {"errorMessage", "Request body is not valid JSON."} }.dump() };
        }
        MasterControl::SupervisorConnectionClaim claim;
        const auto err = MasterControl::parseConnectionClaim(parsed, claim);
        if (!err.empty()) {
            return HttpResponse{ 400, "application/json",
                nlohmann::json{ {"ok", false}, {"errorMessage", err} }.dump() };
        }
        const auto result = supervisorAssignmentService_->confirmConnection(claim);
        {
            ActivityEvent evt;
            evt.kind = result.ok ? "supervisor_connect" : "supervisor_connect_rejected";
            evt.actor = context.actor;
            evt.target = MasterControl::providerIdString(claim.provider);
            evt.statusCode = result.ok ? 200 : 403;
            evt.message = result.ok
                ? std::string("Supervisor connection confirmed for ")
                    + MasterControl::providerDisplayName(claim.provider)
                    + " (client " + result.clientId + ", assignment "
                    + result.assignmentId + ")."
                : std::string("Supervisor connection rejected: ") + result.errorMessage;
            globalActivityRing().append(evt);
            // v0.9.92: persistent diagnostics mirror. Rejected connects
            // land as severity=error so the per-component log is the
            // operator's first stop when a supervisor handshake fails.
            MasterControl::Diagnostics::appendEvent(
                L"supervisor",
                result.ok ? "info" : "error",
                result.ok ? "supervisor_connect" : "supervisor_connect_rejected",
                evt.message,
                nlohmann::json{
                    { "providerId", MasterControl::providerIdString(claim.provider) },
                    { "assignmentId", result.assignmentId },
                    { "configId", claim.configId },
                    { "clientId", claim.clientId },
                    { "capabilities", claim.capabilities },
                    { "actor", context.actor }
                });
        }
        return HttpResponse{ result.ok ? 200 : 403, "application/json",
            MasterControl::toJson(result).dump() };
    }

    // v0.9.69: boot self-test surface. Returns the cached snapshot
    // produced by runBootSelfTestsNow() (results, counts, timing).
    // Served BEFORE /api/activity so the route is reachable even
    // before the activity ring has any events. The body is empty
    // until the boot sweep finishes (~3s after service start).
    if (request.method == "GET" && request.path == "/api/self-tests") {
        const auto snap = getLastSelfTestSnapshot();
        nlohmann::json body = snap;
        body["pending"] = snap.startedAtUtc.empty();
        return HttpResponse{ 200, "application/json", body.dump() };
    }
    // v0.9.69: re-run the self-test sweep on demand. Operator-only;
    // no body required. Returns the freshly-computed snapshot.
    if (request.method == "POST" && request.path == "/api/self-tests/run") {
        auto snap = runBootSelfTestsNow();
        {
            std::lock_guard<std::mutex> lock(selfTestMutex_);
            lastSelfTestSnapshot_ = snap;
        }
        return HttpResponse{ 200, "application/json", nlohmann::json(snap).dump() };
    }

    // Dedicated /api/activity read route — served before the main handler
    // v0.9.17: persistence health surface. Reports the activity
    // ring's persistence layer state -- file path, bytes on disk,
    // total append count, error count, last error message + when
    // it happened, current ring size + high-water-mark id. Sits
    // BEFORE the /api/activity prefix match so it isn't shadowed.
    if (request.method == "GET" && request.path == "/api/activity/health") {
        const auto h = globalActivityRing().persistenceHealth();
        nlohmann::json body;
        body["persistenceEnabled"]  = h.persistenceEnabled;
        body["filePath"]            = h.filePath;
        body["bytesOnDisk"]         = h.bytesOnDisk;
        body["totalAppends"]        = h.totalAppends;
        body["totalAppendErrors"]   = h.totalAppendErrors;
        body["lastErrorMessage"]    = h.lastErrorMessage;
        body["lastErrorSecondsAgo"] = h.lastErrorSecondsAgo;
        body["inMemoryRingSize"]    = h.inMemoryRingSize;
        body["highWaterMarkId"]     = h.highWaterMarkId;
        // Also a top-level "ok" flag for quick-check semantics.
        body["ok"] = h.persistenceEnabled
                  && h.totalAppendErrors == 0;
        return HttpResponse{ 200, "application/json", body.dump() };
    }

    // body so incremental polling stays cheap and never touches the heavier
    // snapshot path. Clients pass `?since={id}` to get only new events.
    // v0.9.89: also supports `?kind=<exact|prefix*>` so an operator
    // dashboard can subscribe to a slice (supervisor_*, pool_worker_*,
    // self_test, etc.). The match is server-side so cross-LAN consumers
    // don't have to pull the full ring just to filter client-side.
    if (request.method == "GET" && request.path == "/api/activity") {
        std::string sinceId;
        std::string kindFilter;
        bool kindFilterIsPrefix = false;
        // v0.9.50: query string now lives in request.query (parseRequest
        // splits at '?'). Route now matches strict /api/activity equality
        // because the query string is no longer in request.path; pre-
        // v0.9.50 we used rfind("/api/activity", 0) == 0 to absorb the
        // query suffix.
        size_t maxCount = ActivityEventRing::kCapacity;
        if (!request.query.empty()) {
            const auto& query = request.query;
            const auto sincePos = query.find("since=");
            if (sincePos != std::string::npos) {
                sinceId = query.substr(sincePos + 6);
                const auto amp = sinceId.find('&');
                if (amp != std::string::npos) sinceId = sinceId.substr(0, amp);
            }
            const auto maxPos = query.find("max=");
            if (maxPos != std::string::npos) {
                auto maxValue = query.substr(maxPos + 4);
                const auto amp = maxValue.find('&');
                if (amp != std::string::npos) {
                    maxValue = maxValue.substr(0, amp);
                }
                try {
                    const auto parsed = std::stoull(maxValue);
                    if (parsed > 0) {
                        maxCount = (parsed > ActivityEventRing::kCapacity)
                            ? ActivityEventRing::kCapacity
                            : static_cast<size_t>(parsed);
                    }
                } catch (...) {
                    // Bad max= value -> fall through to the default cap.
                }
            }
            // v0.9.89: kind filter. Accepts either exact match
            // ("?kind=supervisor_connect") or a prefix wildcard
            // ("?kind=supervisor_*") so operators can subscribe to a
            // whole family of events without enumerating individual
            // kinds. Unknown URL-encoding is left as-is.
            const auto kindPos = query.find("kind=");
            if (kindPos != std::string::npos) {
                kindFilter = query.substr(kindPos + 5);
                const auto amp = kindFilter.find('&');
                if (amp != std::string::npos) kindFilter = kindFilter.substr(0, amp);
                if (!kindFilter.empty() && kindFilter.back() == '*') {
                    kindFilterIsPrefix = true;
                    kindFilter.pop_back();
                }
            }
        }
        // Read 2x the requested cap when filtering so the filter doesn't
        // starve the response when 99% of events are not the requested
        // kind. The ring caps at kCapacity overall, so an upper bound
        // on read size avoids unbounded growth. Parens around std::min
        // defeat the windows.h min/max macros that would otherwise
        // expand the identifier.
        const size_t expandedReadCap = maxCount * 4;
        const size_t ringCap = static_cast<size_t>(ActivityEventRing::kCapacity);
        const size_t readCount = kindFilter.empty()
            ? maxCount
            : (expandedReadCap < ringCap ? expandedReadCap : ringCap);
        const auto snap = globalActivityRing().read(sinceId, readCount);
        nlohmann::json body;
        body["highWaterMarkId"] = snap.highWaterMarkId;
        body["events"] = nlohmann::json::array();
        size_t emitted = 0;
        for (const auto& e : snap.events) {
            if (!kindFilter.empty()) {
                if (kindFilterIsPrefix) {
                    if (e.kind.compare(0, kindFilter.size(), kindFilter) != 0) continue;
                } else {
                    if (e.kind != kindFilter) continue;
                }
            }
            body["events"].push_back(e);
            if (++emitted >= maxCount) break;
        }
        if (!kindFilter.empty()) {
            body["kindFilter"] = kindFilter + (kindFilterIsPrefix ? "*" : "");
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
        // v0.9.24: minimal version probe -- lighter than /api/health/summary
        // for monitoring tools that just want to know "what version is
        // running here." Returns { version, time } so a poller can detect
        // a deploy by watching the version string flip. The v0.9.24 bug-
        // hunt found that /api/version was a 404 even though the version
        // string is already embedded in /api/health/summary, the
        // /.well-known/mcos.json doc, and the MCP initialize handshake's
        // serverInfo.version -- but there was no single-field shortcut.
        if (request.method == "GET" && request.path == "/api/version") {
            return jsonResponse(nlohmann::json{
                { "version", std::string{ MASTERCONTROL_VERSION } },
                { "time",    timestampNowUtc() }
            });
        }
        // v0.9.21: /api/health/summary -- single-URL operational
        // health view for monitoring tools, deploy gates, and the
        // operator dashboard's status chip. Stitches together
        // version, gateway state + tool count, pool roster
        // (healthy/quarantined), activity-ring persistence health,
        // and host telemetry into one JSON object so a poller
        // doesn't have to fan out across 7+ endpoints. Returns
        // top-level "ok" boolean: true iff gateway is running AND
        // every registered pool has at least one healthy instance
        // AND zero pools quarantined AND activity-ring persistence
        // has zero errors AND telemetry sampler has run at least
        // once (capturedAtUtc non-empty).
        if (request.method == "GET" && request.path == "/api/health/summary") {
            nlohmann::json out;
            out["version"] = std::string{ MASTERCONTROL_VERSION };
            out["time"] = timestampNowUtc();

            // v0.9.86: wildcard substitution lives in
            // substituteWildcardHostInUrl() (free helper at the top of
            // this TU). Pre-v0.9.86 this block carried an inline lambda
            // duplicated in /api/gateway/status + /api/gateway/health.

            // Gateway block.
            nlohmann::json gw;
            gw["state"] = "disabled";
            gw["toolCount"] = 0;
            if (mcpGateway_) {
                const auto status = mcpGateway_->CurrentStatus();
                gw["state"] = to_string(status.state);
                gw["mcpUrl"] = substituteWildcardHostInUrl(status.mcpUrl, configurationService_->current());
                gw["mcpUrlRaw"] = status.mcpUrl;
                gw["adapterType"] = mcpGateway_->AdapterType();
                gw["toolCount"] = static_cast<int>(mcpGateway_->ListTools().size());
            }
            out["gateway"] = gw;

            // Pool block.
            int poolCount = 0;
            int healthyPoolCount = 0;
            int totalHealthyInstances = 0;
            int quarantinedPoolCount = 0;
            if (workerSupervisor_) {
                const auto pools = workerSupervisor_->listPools();
                poolCount = static_cast<int>(pools.size());
                for (const auto& pool : pools) {
                    int healthy = 0;
                    for (const auto& inst : pool.instances) {
                        if (inst.state == EndpointInstanceState::Ready
                            || inst.state == EndpointInstanceState::Starting
                            || inst.state == EndpointInstanceState::Busy) {
                            ++healthy;
                        }
                    }
                    if (healthy > 0) {
                        ++healthyPoolCount;
                    } else if (pool.scalePolicy.minInstances > 0) {
                        // Pool is supposed to have at least one
                        // running instance but doesn't -- count as
                        // quarantined-or-dark for the summary.
                        ++quarantinedPoolCount;
                    }
                    totalHealthyInstances += healthy;
                }
            }
            nlohmann::json poolsJson;
            poolsJson["total"] = poolCount;
            poolsJson["withHealthyInstance"] = healthyPoolCount;
            poolsJson["totalHealthyInstances"] = totalHealthyInstances;
            poolsJson["darkOrQuarantined"] = quarantinedPoolCount;
            out["pools"] = poolsJson;

            // Activity-ring persistence health.
            const auto ringHealth = globalActivityRing().persistenceHealth();
            nlohmann::json ringJson;
            ringJson["enabled"] = ringHealth.persistenceEnabled;
            ringJson["bytesOnDisk"] = ringHealth.bytesOnDisk;
            ringJson["totalAppends"] = ringHealth.totalAppends;
            ringJson["totalAppendErrors"] = ringHealth.totalAppendErrors;
            out["activityRing"] = ringJson;

            // Host-telemetry sampler liveness.
            nlohmann::json teleJson;
            teleJson["sampleAvailable"] = false;
            if (telemetryService_) {
                const auto snap = telemetryService_->captureSnapshot();
                teleJson["sampleAvailable"] = !snap.capturedAtUtc.empty();
                teleJson["capturedAtUtc"] = snap.capturedAtUtc;
                teleJson["cpuPercent"] = snap.cpuPercent;
                teleJson["memoryPercent"] = snap.memoryPercent;
            }
            out["hostTelemetry"] = teleJson;

            // v0.9.87: supervisor block. Include the active supervisor
            // (if any) so /api/health/summary surfaces the lifecycle
            // state alongside gateway + pools + telemetry. An assignment
            // in `error` state is counted against overall `ok` so the
            // operator's dashboard health pill goes red when something's
            // wrong with the supervisor handshake; `off`, `revoked`, and
            // `disconnected` are not errors (operator intent / heartbeat
            // gap respectively) so they don't trip overall ok.
            bool supervisorOk = true;
            if (supervisorAssignmentService_) {
                const auto status = supervisorAssignmentService_->getStatus();
                nlohmann::json supJson;
                supJson["state"] = MasterControl::supervisorStateString(status.state);
                supJson["activeProviderId"] = MasterControl::providerIdString(status.provider);
                supJson["active"] = status.active;
                supJson["lastErrorMessage"] = status.lastErrorMessage;
                supervisorOk = (status.state != MasterControl::SupervisorState::Error);
                out["supervisor"] = supJson;
            }

            // Top-level ok flag: gateway running AND every pool
            // with minInstances>0 has a healthy instance AND
            // persistence ring has no errors AND telemetry sampler
            // has at least one reading AND supervisor is not in error.
            const bool gatewayOk = (gw.value("state", std::string{}) == "running");
            const bool poolsOk = quarantinedPoolCount == 0;
            const bool ringOk = ringHealth.persistenceEnabled
                              && ringHealth.totalAppendErrors == 0;
            const bool teleOk = teleJson.value("sampleAvailable", false);
            out["ok"] = gatewayOk && poolsOk && ringOk && teleOk && supervisorOk;

            return jsonResponse(out);
        }
        // v0.9.4 (Fix #4): host telemetry surface. Pre-v0.9.4 the
        // discovery doc, the dashboard, and the realignment manifest
        // (PHASE-08 Real-Time Telemetry deliverable) all implied a
        // host-wide telemetry endpoint, but no path returned it -- the
        // captured `HostTelemetrySnapshot` was only embedded in
        // composite responses (lan-config bundles, etc.). LAN
        // operators / dashboards that want the same data the
        // TelemetrySectionControl shows can now poll
        // `/api/host/telemetry` directly. The shape is the
        // `HostTelemetrySnapshot` struct serialized via the existing
        // NLOHMANN macro: cpuPercent, memoryPercent, diskPercent,
        // totalMemoryBytes, freeMemoryBytes, totalDiskBytes,
        // freeDiskBytes, bytesSentPerSecond, bytesReceivedPerSecond,
        // hostName, primaryIpAddress, primaryMacAddress,
        // operatingSystem, capturedAtUtc.
        //
        // PDH counters are populated by the WindowsHostTelemetryService
        // background sampler (see WindowsHostTelemetryService::start),
        // so this route is honest: if the sampler hasn't taken its
        // first reading yet (e.g. first hundred milliseconds of boot)
        // the cpu/memory/disk percent fields read 0.0 and the
        // capturedAtUtc field is empty -- ADR-002 §9 "no fake
        // telemetry" applies. Clients that need the
        // honest-unavailable signal can detect it by checking
        // capturedAtUtc.
        if (request.method == "GET" && request.path == "/api/host/telemetry") {
            if (!telemetryService_) {
                // No service wired -- emit a default-constructed
                // snapshot rather than 503. The empty capturedAtUtc
                // tells the consumer this is the unavailable state.
                return jsonResponse(HostTelemetrySnapshot{});
            }
            return jsonResponse(telemetryService_->captureSnapshot());
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
            auto status = claudePluginStatusJson(state);
            // v0.10.0: also report whether Claude Code's settings.json
            // currently lists "mcos-control" in enabledPlugins so the
            // operator can see at a glance whether Claude Code will
            // actually load the plugin even though the junction exists.
            bool settingsEnabled = false;
            std::string settingsReadError;
            if (state.activeUserResolved) {
                const std::filesystem::path settingsPath =
                    std::filesystem::path(state.profileDir) / ".claude" / "settings.json";
                std::error_code ec;
                if (std::filesystem::exists(settingsPath, ec)) {
                    std::ifstream in(settingsPath, std::ios::binary);
                    if (in) {
                        try {
                            nlohmann::json doc;
                            in >> doc;
                            if (doc.is_object()
                                && doc.contains("enabledPlugins")
                                && doc["enabledPlugins"].is_object()
                                && doc["enabledPlugins"].contains("mcos-control")) {
                                settingsEnabled = doc["enabledPlugins"]["mcos-control"].is_boolean()
                                    ? doc["enabledPlugins"]["mcos-control"].get<bool>()
                                    : false;
                            }
                        } catch (const nlohmann::json::exception& e) {
                            settingsReadError = std::string("settings.json parse failed: ") + e.what();
                        }
                    }
                }
            }
            status["claudeSettingsEnabled"]    = settingsEnabled;
            status["claudeSettingsReadError"]  = settingsReadError;
            return jsonResponse(status);
        }
        if (request.method == "POST" && request.path == "/api/claude-plugin/toggle") {
            const auto before = resolveClaudePluginState(paths_.executableDirectory);
            if (!before.activeUserResolved) {
                return jsonResponse(claudePluginStatusJson(before, false));
            }
            const bool desiredOn = !before.registered;
            std::string err;
            const bool ok = desiredOn
                ? createClaudePluginJunction(before.target, before.source, err)
                : removeClaudePluginJunction(before.target, err);
            // v0.10.0: also flip the matching enabledPlugins entry in
            // the active user's ~/.claude/settings.json so Claude Code
            // actually loads the plugin on next start. Pre-v0.10.0
            // operators reported "Claude Code plugin missing" because
            // the junction alone was insufficient — Claude Code
            // requires the key in enabledPlugins to mount the plugin.
            ClaudeSettingsUpdateOutcome settingsOutcome;
            if (ok) {
                settingsOutcome = setClaudeMcosPluginEnabled(before.profileDir, desiredOn);
            }
            const auto after = resolveClaudePluginState(paths_.executableDirectory);
            auto status = claudePluginStatusJson(after, ok, err);
            status["claudeSettingsAttempted"]   = settingsOutcome.attempted;
            status["claudeSettingsSucceeded"]   = settingsOutcome.succeeded;
            status["claudeSettingsLastError"]   = settingsOutcome.errorMessage;
            return jsonResponse(status);
        }
        if (request.method == "GET" && request.path == "/api/dashboard") {
            return jsonResponse(snapshot());
        }
        // v0.9.56: operator-facing diagnostic surface. Returns the
        // per-entry runtime stats (with the new install-state +
        // probe-failure-reason fields) plus an aggregate count by
        // installState so an operator can answer "of my N MCP
        // servers / sub-agents, how many are actually live?" in a
        // single request. Pre-v0.9.56 this required parsing the full
        // /api/dashboard payload (~125kB) and bucketing client-side.
        if (request.method == "GET" && request.path == "/api/diagnostics/runtime-stats") {
            const auto snap = snapshot();
            auto bucket = [](const auto& vec) {
                std::map<std::string, int> counts;
                int reachableCount = 0;
                for (const auto& s : vec) {
                    counts[s.installState.empty() ? "unknown" : s.installState]++;
                    if (s.reachable) ++reachableCount;
                }
                nlohmann::json out;
                out["total"] = static_cast<int>(vec.size());
                out["reachable"] = reachableCount;
                out["unreachable"] = static_cast<int>(vec.size()) - reachableCount;
                nlohmann::json byState = nlohmann::json::object();
                for (const auto& [k, v] : counts) byState[k] = v;
                out["byInstallState"] = byState;
                return out;
            };
            nlohmann::json out;
            out["generatedAtUtc"] = timestampNowUtc();
            out["version"]        = std::string{ MASTERCONTROL_VERSION };
            out["mcpServers"] = nlohmann::json{
                { "summary", bucket(snap.mcpServerRuntimeStats) },
                { "entries", snap.mcpServerRuntimeStats }
            };
            out["subAgents"] = nlohmann::json{
                { "summary", bucket(snap.subAgentRuntimeStats) },
                { "entries", snap.subAgentRuntimeStats }
            };
            return jsonResponse(out);
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
            // v0.9.35: validate the platform suffix at the route layer.
            // The underlying GovernanceBundleService::normalizePlatform
            // defaulted to "windows" on any unknown input, so a probe at
            // /api/governance/bundles/freebsd silently returned the
            // windows bundle (10 417 bytes identical to ./windows). The
            // bug-hunt found this. RFC 7231 §6.5.4: 404 is right when the
            // server has no representation for the URI; the client gets a
            // clear "this platform isn't supported" response instead of
            // an unrelated platform's bundle they'd act on by mistake.
            if (platformFromKey(platform) == PlatformTarget::Unknown) {
                return jsonResponse(OperationResult{ false, false,
                    std::string("Unknown governance bundle platform '") + platform
                    + "'. Supported: windows, macos, ios." }, 404);
            }
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
            // Documentation-only GET. The POST path is wired below
            // (v0.9.19) so a 'curl GET' smoke probe still gets a
            // contract-shape response without exercising
            // enforcement.
            //
            // v0.9.20: also surface the valid action-enum and
            // outcome-enum strings + a complete request-shape
            // example. Pre-v0.9.20 a client posting to this
            // endpoint had to read the source (or stumble through
            // 'Unknown enum string' errors) to discover that
            // 'action' is snake_case ('remote_install',
            // 'client_register', ...). Now the GET response is a
            // self-describing contract.
            nlohmann::json response;
            response["method"] = "POST";
            response["expects"] = "GovernanceEnforcementRequest";
            response["returns"] = "GovernanceEnforcementDecision";
            response["note"] = "POST /api/governance/decisions evaluates a request through CLU's enforceAction.";

            // Enumerate every valid action-enum string by walking
            // the enum and using to_string. This stays in sync
            // automatically if the enum grows.
            response["validActionStrings"] = {
                to_string(GovernanceActionKind::ClientRegister),
                to_string(GovernanceActionKind::ClientPrivilegeChange),
                to_string(GovernanceActionKind::ClientAutonomousModeChange),
                to_string(GovernanceActionKind::ClientRevoke),
                to_string(GovernanceActionKind::McpServerCreate),
                to_string(GovernanceActionKind::McpServerModify),
                to_string(GovernanceActionKind::McpServerRemove),
                to_string(GovernanceActionKind::SubAgentCreate),
                to_string(GovernanceActionKind::SubAgentModify),
                to_string(GovernanceActionKind::SubAgentRemove),
                to_string(GovernanceActionKind::ModuleEnable),
                to_string(GovernanceActionKind::ModuleDisable),
                to_string(GovernanceActionKind::GovernancePolicyChange),
                to_string(GovernanceActionKind::RemoteInstall)
            };
            response["possibleOutcomeStrings"] = {
                to_string(GovernanceDecisionOutcome::Allow),
                to_string(GovernanceDecisionOutcome::Block),
                to_string(GovernanceDecisionOutcome::RequiresOperatorApproval)
            };
            response["requestShapeExample"] = {
                { "action", to_string(GovernanceActionKind::RemoteInstall) },
                { "actor", "<client-id>" },
                { "subjectId", "<package-source-or-resource-id>" },
                { "allowUntrustedExecution", false }
            };
            response["responseShapeExample"] = {
                { "action", to_string(GovernanceActionKind::RemoteInstall) },
                { "allowed", true },
                { "outcome", to_string(GovernanceDecisionOutcome::Allow) },
                { "blockingFindings", nlohmann::json::array() },
                { "deferredActionId", "" },
                { "message", "" },
                { "posture", "<governance-posture>" },
                { "ruleId", "" }
            };
            return jsonResponse(response);
        }
        // v0.9.19: POST /api/governance/decisions wires CLU's
        // enforceAction. The GET stub in v0.9.4 promised "POST
        // handler lands in PHASE-07 alongside the lease router" but
        // PHASE-07 shipped without this; the POST returned
        // {"message":"Not found"} for ten releases. The
        // commandLogicUnitService_->enforceAction surface has
        // existed since PHASE-05 and is already used by the
        // installPackageJson / installRepoJson / executeForsetti
        // ToolJson handlers; this route just reuses that path
        // without any new logic.
        //
        // Body shape (GovernanceEnforcementRequest):
        //   { "action": <enum string>, "actor": <client-id>,
        //     "subjectId": <string>, "allowUntrustedExecution": <bool> }
        // Response shape (GovernanceEnforcementDecision):
        //   { "action": ..., "allowed": <bool>, "outcome": <enum>,
        //     "message": <string>, "posture": <enum>,
        //     "blockedFindings": [...] }
        if (request.method == "POST" && request.path == "/api/governance/decisions") {
            if (!commandLogicUnitService_) {
                // v0.9.39: 503 Service Unavailable -- the named runtime
                // service is not initialized (partial-init / shutdown
                // window). RFC 7231 §6.6.4: a 503 communicates that the
                // server is currently unable to handle the request due
                // to a temporary overload or maintenance.
                return jsonResponse(OperationResult{ false, false,
                    "CLU service is not running." }, 503);
            }
            try {
                const auto enforcementRequest =
                    nlohmann::json::parse(request.body)
                        .get<GovernanceEnforcementRequest>();
                const auto decision =
                    commandLogicUnitService_->enforceAction(enforcementRequest);
                return jsonResponse(decision);
            } catch (const std::exception& ex) {
                // v0.9.29: malformed/empty client body is a 400, not 200.
                // Pre-v0.9.29 the catch returned 200 with succeeded:false
                // and a parse-error string; clients that key on HTTP status
                // (every standard HTTP middleware: nginx upstream metrics,
                // Prometheus blackbox exporter, browser fetch.ok, retry
                // libraries) saw "200 OK" and treated the response as a
                // success. RFC 7231 §6.5.1: 400 is the correct verdict
                // when the request body cannot be parsed.
                return jsonResponse(OperationResult{ false, false,
                    std::string("Invalid GovernanceEnforcementRequest JSON: ")
                    + ex.what() }, 400);
            }
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
                // v0.9.30: route-shape error -> 400, not 200.
                return jsonResponse(OperationResult{ false, false, "Unknown pool sub-resource; expected leases|saturation." }, 400);
            }
            const auto pool = workerSupervisor_
                ? workerSupervisor_->findPool(poolId)
                : std::optional<ManagedEndpointPool>{};
            if (!pool.has_value()) {
                // v0.9.30: a real "resource doesn't exist" response.
                // Pre-v0.9.30 returned 200 OK with succeeded:false.
                // RFC 7231 §6.5.4: 404 is the right status when the
                // server has no current representation for the URI.
                return jsonResponse(OperationResult{ false, false, "Unknown pool id." }, 404);
            }
            return jsonResponse(*pool);
        }
        if (request.method == "POST" && request.path == "/api/pools") {
            try {
                auto poolBody = nlohmann::json::parse(request.body).get<ManagedEndpointPool>();
                if (!workerSupervisor_) {
                    // v0.9.39: 503 -- supervisor not initialized (see CLU 503 above).
                return jsonResponse(OperationResult{ false, false, "Worker supervisor is not running." }, 503);
                }
                const std::string poolIdEvt = poolBody.poolId;
                const std::string poolKindEvt = to_string(poolBody.kind);
                const auto result = workerSupervisor_->upsertPool(std::move(poolBody));
                if (result.succeeded) {
                    // v0.6.8 persistence: mirror the supervisor's pool list
                    // to disk so the definition survives restart / upgrade.
                    persistSupervisedPoolsToConfiguration();
                    // v0.9.6: bump the gateway's tool-catalog cache so
                    // LAN-client tools/list reflects the new pool's
                    // tools immediately. Pre-v0.9.6 operators waited
                    // up to 30s (the TTL) for the new pool to surface.
                    if (mcpGateway_) {
                        mcpGateway_->InvalidateToolCatalog();
                    }
                    // v0.6.8 telemetry events ring producer: emit a
                    // categorized event so the dashboard's Recent Telemetry
                    // Events panel and /api/telemetry/events surface this
                    // lifecycle event. Through v0.6.7 only /api/activity
                    // had a producer; the telemetry events ring stayed
                    // empty.
                    if (telemetryAggregator_) {
                        TelemetryEvent evt;
                        evt.category = TelemetryCategory::Worker;
                        evt.severity = TelemetrySeverity::Info;
                        evt.message = std::string("Pool registered: ") + poolIdEvt
                            + " (kind=" + poolKindEvt + ")";
                        evt.poolId = poolIdEvt;
                        telemetryAggregator_->recordEvent(std::move(evt));
                    }
                }
                if (result.succeeded) { return jsonResponse(result, 200); }
                // v0.9.52: upsertPool now validates poolId character set
                // and scalePolicy bounds. Failure is client error -> 400.
                return jsonResponse(result, 400);
            } catch (const std::exception& ex) {
                // v0.9.29: malformed body -> 400 (see governance-decisions
                // catch above for the rationale -- middleware/observability
                // can't tell a parse error apart from success otherwise).
                return jsonResponse(OperationResult{ false, false, std::string("Invalid pool JSON: ") + ex.what() }, 400);
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
                // v0.9.39: 503 -- supervisor not initialized (see CLU 503 above).
                return jsonResponse(OperationResult{ false, false, "Worker supervisor is not running." }, 503);
            }
            if (action == "remove") {
                const auto result = workerSupervisor_->removePool(poolId);
                if (result.succeeded) {
                    persistSupervisedPoolsToConfiguration();
                    // v0.9.6: invalidate tools/list cache -- removed
                    // pool's tools must drop out of the next LAN-client
                    // tools/list response.
                    if (mcpGateway_) {
                        mcpGateway_->InvalidateToolCatalog();
                    }
                    if (telemetryAggregator_) {
                        TelemetryEvent evt;
                        evt.category = TelemetryCategory::Worker;
                        evt.severity = TelemetrySeverity::Info;
                        evt.message = "Pool removed: " + poolId;
                        evt.poolId = poolId;
                        telemetryAggregator_->recordEvent(std::move(evt));
                    }
                }
                return jsonResponse(result);
            }
            if (action == "scale") {
                const auto result = workerSupervisor_->ensureMinInstances(poolId);
                if (result.succeeded) {
                    // v0.9.6: invalidate tools/list cache -- a fresh
                    // instance may report a different tool catalog
                    // (e.g., after a pool was previously dark with
                    // zero instances).
                    if (mcpGateway_) {
                        mcpGateway_->InvalidateToolCatalog();
                    }
                    if (telemetryAggregator_) {
                        TelemetryEvent evt;
                        evt.category = TelemetryCategory::Worker;
                        evt.severity = TelemetrySeverity::Info;
                        evt.message = "Pool scaled to minInstances: " + poolId;
                        evt.poolId = poolId;
                        telemetryAggregator_->recordEvent(std::move(evt));
                    }
                }
                return jsonResponse(result);
            }
            if (action == "drain") {
                const auto result = workerSupervisor_->drainPool(poolId);
                if (result.succeeded) {
                    // v0.9.6: invalidate tools/list cache -- draining
                    // pool's tools should not advertise as available
                    // for new traffic.
                    if (mcpGateway_) {
                        mcpGateway_->InvalidateToolCatalog();
                    }
                    if (telemetryAggregator_) {
                        TelemetryEvent evt;
                        evt.category = TelemetryCategory::Worker;
                        evt.severity = TelemetrySeverity::Warning;
                        evt.message = "Pool draining: " + poolId
                            + " (existing sticky leases finish on their bound instance; new sessions route elsewhere)";
                        evt.poolId = poolId;
                        telemetryAggregator_->recordEvent(std::move(evt));
                    }
                }
                return jsonResponse(result);
            }
            // v0.9.23: POST /api/pools/{poolId}/restart -- hot-rotate
            // a pool's workers in one operator call. Equivalent to
            // sending the existing /remove + (re-register from
            // persisted config) + /scale sequence, but atomic from
            // the operator's perspective. Use cases: a misbehaving
            // worker that hasn't crossed the quarantine threshold,
            // a config-file change to the pool's environment that
            // needs a respawn to take effect, or a precautionary
            // rotation after a memory-leak suspicion. Implementation
            // re-uses removePool + the persisted configuration's
            // pool definition + ensureMinInstances; nothing here is
            // new behavior, just convenience composition. The
            // operator alternative is two separate POSTs which is
            // racy if monitoring polls in between.
            if (action == "restart") {
                // Snapshot the current pool's definition before we
                // remove it -- the supervisor zeroes the instances
                // list on remove but keeps no archived template.
                auto snapshot = workerSupervisor_->findPool(poolId);
                if (!snapshot.has_value()) {
                    return jsonResponse(OperationResult{ false, false,
                        "Cannot restart unknown pool '" + poolId + "'." });
                }
                ManagedEndpointPool def = *snapshot;
                def.instances.clear();          // re-spawned by ensureMinInstances
                def.createdAtUtc.clear();       // upsert resets these
                def.updatedAtUtc.clear();

                const auto removeResult = workerSupervisor_->removePool(poolId);
                if (!removeResult.succeeded) {
                    return jsonResponse(removeResult);
                }
                const auto upsertResult = workerSupervisor_->upsertPool(def);
                if (!upsertResult.succeeded) {
                    return jsonResponse(upsertResult);
                }
                const auto scaleResult = workerSupervisor_->ensureMinInstances(poolId);
                // Persistence + cache invalidation (same as the
                // remove/upsert/scale handlers above).
                persistSupervisedPoolsToConfiguration();
                if (mcpGateway_) {
                    mcpGateway_->InvalidateToolCatalog();
                }
                if (telemetryAggregator_) {
                    TelemetryEvent evt;
                    evt.category = TelemetryCategory::Worker;
                    evt.severity = TelemetrySeverity::Info;
                    evt.message = "Pool restarted: " + poolId;
                    evt.poolId = poolId;
                    telemetryAggregator_->recordEvent(std::move(evt));
                }
                return jsonResponse(OperationResult{ true, false,
                    "Pool restarted (removed + re-registered + scaled to minInstances): " + scaleResult.message });
            }
            // PHASE-07 lease acquire: POST /api/pools/{poolId}/leases
            if (action == "leases") {
                if (!leaseRouter_) {
                    // v0.9.39: 503 -- lease router not initialized.
                return jsonResponse(OperationResult{ false, false, "Lease router is not running." }, 503);
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
                const auto lease = leaseRouter_->acquireLease(leaseRequest);
                if (telemetryAggregator_) {
                    TelemetryEvent evt;
                    evt.category = TelemetryCategory::Worker;
                    evt.severity = (lease.state == LeaseState::Failed)
                        ? TelemetrySeverity::Warning
                        : TelemetrySeverity::Info;
                    evt.message = (lease.state == LeaseState::Failed)
                        ? std::string("Lease acquire failed for pool ") + poolId + ": " + lease.statusMessage
                        : std::string("Lease ") + lease.leaseId + " acquired on pool " + poolId
                            + " -> instance " + lease.instanceId;
                    evt.poolId = poolId;
                    evt.instanceId = lease.instanceId;
                    telemetryAggregator_->recordEvent(std::move(evt));
                }
                // v0.9.33: surface "unknown pool" through HTTP status. The
                // lease router reports the failure via lease.state=Failed
                // with statusMessage="Unknown pool."; pre-v0.9.33 the route
                // returned the failed lease object with HTTP 200, so a
                // client that polled lease.state had to parse the
                // statusMessage to tell "pool not yet up" apart from "pool
                // does not exist." 404 + the same body lets generic HTTP
                // monitoring reach the same conclusion without parsing
                // the inner state machine.
                if (lease.state == LeaseState::Failed && lease.statusMessage == "Unknown pool.") {
                    return jsonResponse(lease, 404);
                }
                return jsonResponse(lease);
            }
            // v0.9.30: route-shape error -> 400, not 200.
            return jsonResponse(OperationResult{ false, false, "Unknown pool action; expected remove|scale|drain|leases." }, 400);
        }
        // -------------------------------------------------------------------
        // PHASE-08 (ADR-002 §9): real-time telemetry surface.
        // GET  /api/telemetry/events?max=N  -> recent activity events.
        // GET  /api/telemetry/clients       -> connected-client roster.
        // GET  /api/telemetry/gateway       -> gateway traffic snapshot.
        // POST /api/telemetry/heartbeat     -> AI-client heartbeat ingest.
        if (request.method == "GET" && request.path == "/api/telemetry/events") {
            // Honor ?max=N. Through v0.6.7 the route only matched the
            // bare path; ?max=N produced a 404 and the route also
            // returned a raw array instead of the {events, maxEvents}
            // envelope the dashboard expects. Both fixed in v0.6.8.
            // v0.9.50: query string is in request.query now.
            std::size_t maxEvents = 1024;
            if (!request.query.empty()) {
                const auto& qs = request.query;
                const auto maxAt = qs.find("max=");
                if (maxAt != std::string::npos) {
                    auto value = qs.substr(maxAt + 4);
                    const auto amp = value.find('&');
                    if (amp != std::string::npos) value = value.substr(0, amp);
                    try { maxEvents = static_cast<std::size_t>(std::stoul(value)); } catch (...) {}
                    if (maxEvents == 0) maxEvents = 1024;
                }
            }
            const auto events = telemetryAggregator_
                ? telemetryAggregator_->recentEvents(maxEvents)
                : std::vector<TelemetryEvent>{};
            nlohmann::json body = {
                { "events", events },
                { "maxEvents", maxEvents }
            };
            return HttpResponse{ 200, "application/json", body.dump() };
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
                // v0.9.39: 503 -- telemetry aggregator not initialized.
                return jsonResponse(OperationResult{ false, false, "Telemetry aggregator is not running." }, 503);
            }
            try {
                auto heartbeat = nlohmann::json::parse(request.body).get<ClientHeartbeat>();
                if (heartbeat.clientId.empty()) {
                    // v0.9.29: missing required field -> 400.
                    return jsonResponse(OperationResult{ false, false, "ClientHeartbeat.clientId is required." }, 400);
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
                // v0.9.29: malformed body -> 400 (see governance-decisions
                // catch above for the rationale).
                return jsonResponse(OperationResult{ false, false, std::string("Invalid heartbeat JSON: ") + ex.what() }, 400);
            }
        }
        // PHASE-07 lease release: POST /api/leases/{leaseId}/release
        if (request.method == "POST" && startsWith(request.path, "/api/leases/")) {
            const auto prefix = std::string("/api/leases/");
            const std::string suffix = request.path.substr(prefix.size());
            const auto slash = suffix.find('/');
            if (slash == std::string::npos || suffix.substr(slash + 1) != "release") {
                // v0.9.30: route-shape error -> 400, not 200.
                return jsonResponse(OperationResult{ false, false, "Lease route is POST /api/leases/{leaseId}/release." }, 400);
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
            if (!leaseRouter_) {
                // v0.9.39: 503 -- lease router not initialized.
                return jsonResponse(OperationResult{ false, false, "Lease router is not running." }, 503);
            }
            const auto releaseResult = leaseRouter_->releaseLease(leaseId, reason);
            // v0.9.33: "Unknown lease id." -> 404. RFC 7231 §6.5.4: the
            // resource the URI points at no longer (or never did) exist.
            // Pre-v0.9.33 returned 200 with succeeded:false; that hid
            // the "release failed because we never had this lease" case
            // from generic HTTP monitoring.
            if (!releaseResult.succeeded && releaseResult.message == "Unknown lease id.") {
                return jsonResponse(releaseResult, 404);
            }
            return jsonResponse(releaseResult);
        }
        // -------------------------------------------------------------------
        // /api/gateway/* — MCP Gateway adapter surface (PHASE-02 of ADR-002).
        // PHASE-04 onboarding profiles point clients at the gateway URL
        // exposed here, not at the admin port.
        //
        // v0.9.86: wildcard substitution moved to free helper at the
        // top of this TU (substituteWildcardHostInUrl). Both
        // /api/gateway/status and /api/gateway/health rewrite the URL
        // + the narrative message before serializing, and surface the
        // unsubstituted URL as mcpUrlRaw for diagnostic UIs.
        if (request.method == "GET" && request.path == "/api/gateway/status") {
            GatewayStatus status = mcpGateway_ ? mcpGateway_->CurrentStatus() : GatewayStatus{};
            const auto rawUrl = status.mcpUrl;
            const auto& cfg = configurationService_->current();
            status.mcpUrl = substituteWildcardHostInUrl(status.mcpUrl, cfg);
            status.message = substituteWildcardHostInUrl(status.message, cfg);
            nlohmann::json body = status;
            body["mcpUrlRaw"] = rawUrl;
            return HttpResponse{ 200, "application/json", body.dump() };
        }
        if (request.method == "GET" && request.path == "/api/gateway/health") {
            GatewayHealth health = mcpGateway_ ? mcpGateway_->Probe() : GatewayHealth{};
            const auto rawUrl = health.mcpUrl;
            const auto& cfg = configurationService_->current();
            health.mcpUrl = substituteWildcardHostInUrl(health.mcpUrl, cfg);
            health.message = substituteWildcardHostInUrl(health.message, cfg);
            nlohmann::json body = health;
            body["mcpUrlRaw"] = rawUrl;
            return HttpResponse{ 200, "application/json", body.dump() };
        }
        if (request.method == "GET" && request.path == "/api/gateway/tools") {
            return jsonResponse(mcpGateway_ ? mcpGateway_->ListTools() : std::vector<McpToolDescriptor>{});
        }
        // v0.9.3: helper to surface gateway lifecycle in BOTH event
        // surfaces -- the activity ring (read by /api/activity, the
        // operator-facing audit log) AND the telemetry aggregator
        // (used by the dashboard's Recent Telemetry tile). Pre-v0.9.3
        // gateway events landed only in telemetryAggregator; the
        // operator looking at /api/activity to debug a connection
        // problem saw zero gateway entries.
        const auto recordGatewayLifecycle = [&](const std::string& route,
                                                 const GatewayStatus& status,
                                                 bool ok) {
            ActivityEvent ringEvt;
            ringEvt.kind    = "gateway_lifecycle";
            ringEvt.actor   = "admin-api";
            ringEvt.target  = route;
            ringEvt.method  = request.method;
            ringEvt.statusCode = ok ? 200 : 500;
            ringEvt.message = "Gateway " + route + " -> state=" + to_string(status.state)
                            + " (" + status.message + ")";
            globalActivityRing().append(ringEvt);
            if (telemetryAggregator_) {
                TelemetryEvent evt;
                evt.category = TelemetryCategory::Gateway;
                evt.severity = ok ? TelemetrySeverity::Info : TelemetrySeverity::Error;
                evt.message  = "Gateway " + route + ". State: " + status.message;
                telemetryAggregator_->recordEvent(std::move(evt));
            }
        };

        if (request.method == "POST" && request.path == "/api/gateway/start") {
            const auto status = mcpGateway_ ? mcpGateway_->Start() : GatewayStatus{};
            recordGatewayLifecycle("start", status, status.state == GatewayState::Running);
            return jsonResponse(status);
        }
        if (request.method == "POST" && request.path == "/api/gateway/stop") {
            const auto status = mcpGateway_ ? mcpGateway_->Stop() : GatewayStatus{};
            recordGatewayLifecycle("stop", status,
                status.state == GatewayState::Stopped || status.state == GatewayState::Disabled);
            return jsonResponse(status);
        }
        // v0.9.3: explicit gateway restart route. Pre-v0.9.3 the only
        // recovery from a wedged gateway state (e.g. boot-time
        // ERROR_ALREADY_EXISTS that the v0.9.3 retry-with-backoff still
        // can't drain) was to know the route name "/api/gateway/start"
        // -- a clean Stop()-then-Start() sequence wasn't exposed at
        // all. Restart is the right verb for "we're failed, drop
        // everything and try again."
        if (request.method == "POST" && request.path == "/api/gateway/restart") {
            if (!mcpGateway_) {
                return jsonResponse(GatewayStatus{});
            }
            (void)mcpGateway_->Stop();
            const auto status = mcpGateway_->Start();
            recordGatewayLifecycle("restart", status, status.state == GatewayState::Running);
            return jsonResponse(status);
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
            // v0.9.3: also surface every supervised WorkerSupervisor pool
            // whose kind is McpServer and which has at least one Ready
            // instance. Pre-v0.9.3 this surface returned [] on a fresh
            // install because the seeded RuntimeEndpoint list is all
            // templates and the v0.9.1 baseline-tools pool is owned by
            // WorkerSupervisor (not the inventory). LAN clients consuming
            // this catalog need to see what's actually serving traffic.
            if (workerSupervisor_) {
                for (const auto& pool : workerSupervisor_->listPools()) {
                    if (pool.kind != EndpointPoolKind::McpServer) continue;
                    bool hasReady = false;
                    for (const auto& inst : pool.instances) {
                        if (inst.state == EndpointInstanceState::Ready) { hasReady = true; break; }
                    }
                    if (!hasReady) continue;
                    RuntimeEndpoint ep;
                    ep.id            = pool.poolId;
                    ep.displayName   = pool.displayName.empty() ? pool.poolId : pool.displayName;
                    ep.kind          = EndpointKind::MCPServer;
                    ep.host          = configurationService_->current().bindAddress.empty()
                                        ? std::string("0.0.0.0")
                                        : configurationService_->current().bindAddress;
                    ep.port          = configurationService_->current().mcpGateway.listenPort;
                    ep.protocol      = "http";
                    ep.routePath     = configurationService_->current().mcpGateway.mcpPath;
                    ep.status        = EndpointStatus::Online;
                    ep.description   = "Supervised MCP pool routed through the gateway. Tool names are prefixed with '" + pool.poolId + "__'.";
                    ep.isTemplate    = false;
                    ep.lastCheckedUtc = pool.updatedAtUtc;
                    mcpEndpoints.push_back(std::move(ep));
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
            // v0.9.3: surface live SubAgent pools too. Same rationale as
            // /api/client/mcp-servers above.
            if (workerSupervisor_) {
                for (const auto& pool : workerSupervisor_->listPools()) {
                    if (pool.kind != EndpointPoolKind::SubAgent) continue;
                    bool hasReady = false;
                    for (const auto& inst : pool.instances) {
                        if (inst.state == EndpointInstanceState::Ready) { hasReady = true; break; }
                    }
                    if (!hasReady) continue;
                    RuntimeEndpoint ep;
                    ep.id            = pool.poolId;
                    ep.displayName   = pool.displayName.empty() ? pool.poolId : pool.displayName;
                    ep.kind          = EndpointKind::SubAgent;
                    ep.host          = configurationService_->current().bindAddress.empty()
                                        ? std::string("0.0.0.0")
                                        : configurationService_->current().bindAddress;
                    ep.port          = configurationService_->current().mcpGateway.listenPort;
                    ep.protocol      = "http";
                    ep.routePath     = configurationService_->current().mcpGateway.mcpPath;
                    ep.status        = EndpointStatus::Online;
                    ep.description   = "Supervised sub-agent pool routed through the gateway.";
                    ep.isTemplate    = false;
                    ep.lastCheckedUtc = pool.updatedAtUtc;
                    subAgentEndpoints.push_back(std::move(ep));
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
        // v0.9.54: catch POST /api/setup/dependencies/{id}/{verb} for
        // any verb other than /install. Pre-v0.9.54 these fell through
        // to the v0.9.28 supportedMethodsForPath fallback which saw the
        // prefix as POST-allowed and returned a misleading 405 ("Method
        // Not Allowed" with Allow: POST, OPTIONS) -- POST IS allowed,
        // it's the URL action segment that's unknown. Now we return a
        // proper 400 listing the valid actions ('install' is the only
        // one this gateway implements; preflight, uninstall, detect
        // were on the bug-hunt's wishlist but never wired up).
        if (request.method == "POST" && startsWith(request.path, "/api/setup/dependencies/")) {
            const auto prefix = std::string("/api/setup/dependencies/");
            const auto suffix = request.path.substr(prefix.size());
            const auto slash = suffix.find('/');
            if (slash != std::string::npos) {
                const std::string action = suffix.substr(slash + 1);
                return jsonResponse(OperationResult{ false, false,
                    std::string("Unknown dependency action '") + action
                    + "'. Supported actions: install." }, 400);
            }
            // Bare /api/setup/dependencies/{id} with no action -> also 400.
            return jsonResponse(OperationResult{ false, false,
                "Dependency action route is POST /api/setup/dependencies/{id}/install." }, 400);
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
            // v0.9.42: wrap parse-throws as 400 (continuation of v0.9.41
            // and v0.9.38 sweeps).
            const bool confirmUnsafeChanges = request.headers.contains("X-Confirm-Unsafe") &&
                request.headers.at("X-Confirm-Unsafe") == "1";
            try {
                const auto result = adminApiService_->applyConfigurationJson(request.body, confirmUnsafeChanges);
                // v0.7.4: emit a telemetry event on every successful configuration
                // write so the dashboard's Recent Telemetry Events panel reflects
                // real activity. Pre-v0.7.4 the events ring stayed empty after
                // boot for normal operator traffic (config writes, gateway
                // start/stop, etc.), giving the impression telemetry was static.
                if (result.succeeded && telemetryAggregator_) {
                    TelemetryEvent evt;
                    evt.category = TelemetryCategory::System;
                    evt.severity = TelemetrySeverity::Info;
                    evt.message  = "Configuration updated via /api/config.";
                    telemetryAggregator_->recordEvent(std::move(evt));
                }
                return jsonResponse(result, result.succeeded ? 200 : 400);
            } catch (const std::exception& ex) {
                return jsonResponse(OperationResult{ false, false,
                    std::string("Invalid configuration payload: ") + ex.what() }, 400);
            }
        }
        if (request.method == "POST" && request.path == "/api/platform-services/apple-hosts") {
            try {
                const auto result = adminApiService_->upsertAppleRemoteHostJson(request.body);
                return jsonResponse(result, result.succeeded ? 200 : 400);
            } catch (const std::exception& ex) {
                return jsonResponse(OperationResult{ false, false,
                    std::string("Invalid Apple remote host payload: ") + ex.what() }, 400);
            }
        }
        if (request.method == "POST" && request.path == "/api/platform-services/apple-hosts/remove") {
            try {
                const auto result = adminApiService_->removeAppleRemoteHostJson(request.body);
                if (result.succeeded) { return jsonResponse(result, 200); }
                // v0.9.45: 404 for missing target. Closes the carry from
                // the v0.9.44 release report. removeHost() already
                // distinguishes "no such hostId" ("Apple remote host
                // '<id>' was not found.") from "missing input"
                // ("requires a hostId."), but the route was treating
                // both as 400. Use a substring match because the not-
                // found message embeds the hostId itself in quotes.
                const int status = (result.message.find("was not found.") != std::string::npos) ? 404 : 400;
                return jsonResponse(result, status);
            } catch (const std::exception& ex) {
                // v0.9.44: parse-error -> 400 (was uncaught -> 500).
                return jsonResponse(OperationResult{ false, false,
                    std::string("Invalid Apple remote host removal payload: ") + ex.what() }, 400);
            }
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
            // v0.9.38: catch the from_json invalid_argument throws ("Unknown
            // enum string: mcp-server" when the client sent kind:'mcp-server'
            // instead of 'mcp_server'). Pre-v0.9.38 they bubbled to the
            // outer 500 handler -- 500 is for server-side bugs, but a
            // client sending the wrong enum spelling is a 400. Same shape
            // as v0.9.29's parse-error 400 fix.
            try {
                const auto result = adminApiService_->upsertMcpServerJson(request.body);
                return jsonResponse(result, result.succeeded ? 200 : 400);
            } catch (const std::exception& ex) {
                return jsonResponse(OperationResult{ false, false,
                    std::string("Invalid runtime MCP server payload: ") + ex.what() }, 400);
            }
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
            // v0.9.44: catch parse-error throws (was uncaught -> 500).
            // Pre-v0.9.44 the v0.9.38 404-vs-400 logic only ran when
            // removeMcpServerJson returned cleanly; on a malformed body
            // the inner json::parse threw past it to the outer 500.
            try {
                const auto result = adminApiService_->removeMcpServerJson(request.body);
                if (result.succeeded) { return jsonResponse(result, 200); }
                // v0.9.38: 404 for missing target, 400 for everything else.
                const int status = (result.message == "The requested MCP server was not found.") ? 404 : 400;
                return jsonResponse(result, status);
            } catch (const std::exception& ex) {
                return jsonResponse(OperationResult{ false, false,
                    std::string("Invalid MCP server removal payload: ") + ex.what() }, 400);
            }
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
            // v0.9.38: catch enum/parse throws (see /api/runtime/mcp-servers
            // upsert above). Lift the result-only declaration into a try
            // block; the auto-promote-to-pool block below stays inside.
            OperationResult result;
            try {
                result = adminApiService_->upsertSubAgentJson(request.body);
            } catch (const std::exception& ex) {
                return jsonResponse(OperationResult{ false, false,
                    std::string("Invalid runtime sub-agent payload: ") + ex.what() }, 400);
            }
            // v0.7.2: auto-promote a sub-agent registration to a managed
            // pool when the registration carries a `command` field. This
            // makes the autoscale + utilization story turn-key: the
            // operator POSTs once to /api/runtime/subagents with command/
            // args/workingDirectory and the runtime mirrors it into the
            // worker supervisor's pool registry with poolId == endpoint.id
            // and a default scale policy of {minInstances=0, maxInstances=
            // autoscaleMaxInstances, maxActiveLeasesPerInstance=
            // autoscaleMaxLeasesPerInstance}. The same lease router that
            // serves managed pools then transparently autoscales the
            // sub-agent under saturation. Sub-agents without a `command`
            // (network-addressable sub-agents at host:port) still register
            // as inventory entries only -- no pool, no autoscale, and the
            // dashboard's per-sub-agent panel renders honest-unavailable
            // for them.
            if (result.succeeded && workerSupervisor_) {
                try {
                    const auto requestJson = nlohmann::json::parse(request.body);
                    const auto endpoint = requestJson.get<RuntimeEndpoint>();
                    if (endpoint.kind == EndpointKind::SubAgent
                        && !endpoint.id.empty()
                        && !endpoint.command.empty()) {
                        ManagedEndpointPool autoPool;
                        autoPool.poolId      = endpoint.id;
                        autoPool.kind        = EndpointPoolKind::SubAgent;
                        autoPool.displayName = endpoint.displayName.empty()
                            ? endpoint.id
                            : endpoint.displayName;
                        autoPool.template_.executable       = endpoint.command;
                        autoPool.template_.args             = endpoint.args;
                        autoPool.template_.workingDirectory = endpoint.workingDirectory;
                        autoPool.template_.environment      = endpoint.environment;
                        autoPool.scalePolicy.minInstances              = 0;
                        autoPool.scalePolicy.maxInstances              = (std::max)(1, endpoint.autoscaleMaxInstances);
                        autoPool.scalePolicy.maxActiveLeasesPerInstance = (std::max)(1, endpoint.autoscaleMaxLeasesPerInstance);
                        (void)workerSupervisor_->upsertPool(autoPool);
                    }
                } catch (...) {
                    // Body shape didn't deserialize cleanly into a
                    // RuntimeEndpoint with the v0.7.2 fields. The base
                    // sub-agent registration still succeeded; we just
                    // skip auto-promotion and let the operator wire the
                    // pool manually via /api/pools.
                }
            }
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
            // v0.9.44: catch parse-error throws (was uncaught -> 500).
            try {
                const auto result = adminApiService_->removeSubAgentJson(request.body);
                // v0.7.2: mirror the removal into the worker supervisor's
                // pool registry so an auto-promoted pool is reaped together
                // with its sub-agent inventory entry. removePool drains
                // existing instances under their Job Object before erasing.
                if (result.succeeded && workerSupervisor_ && !removalTarget.empty()) {
                    (void)workerSupervisor_->removePool(removalTarget);
                }
                if (result.succeeded) { return jsonResponse(result, 200); }
                // v0.9.38: 404 for missing target, 400 otherwise.
                const int status = (result.message == "The requested sub-agent was not found.") ? 404 : 400;
                return jsonResponse(result, status);
            } catch (const std::exception& ex) {
                return jsonResponse(OperationResult{ false, false,
                    std::string("Invalid sub-agent removal payload: ") + ex.what() }, 400);
            }
        }
        if (request.method == "POST" && request.path == "/api/runtime/subagent-groups") {
            // Sub-agent groups are organizational metadata over the existing
            // sub-agent roster; gate them on canModifySubAgents since they
            // affect how sub-agents are addressed in execution.
            if (auto deny = requirePrivilege(context.privileges.canModifySubAgents,
                                             "canModifySubAgents")) {
                return *deny;
            }
            try {
                const auto result = adminApiService_->upsertSubAgentGroupJson(request.body);
                return jsonResponse(result, result.succeeded ? 200 : 400);
            } catch (const std::exception& ex) {
                // v0.9.42: parse-error -> 400.
                return jsonResponse(OperationResult{ false, false,
                    std::string("Invalid sub-agent group payload: ") + ex.what() }, 400);
            }
        }
        if (request.method == "POST" && request.path == "/api/runtime/subagent-groups/remove") {
            if (auto deny = requirePrivilege(context.privileges.canModifySubAgents,
                                             "canModifySubAgents")) {
                return *deny;
            }
            try {
                const auto result = adminApiService_->removeSubAgentGroupJson(request.body);
                if (result.succeeded) { return jsonResponse(result, 200); }
                // v0.9.44: 404 for missing target, 400 otherwise
                // (continues v0.9.30/v0.9.33/v0.9.37/v0.9.38/v0.9.43 sweep).
                const int status = (result.message == "The requested sub-agent group was not found.") ? 404 : 400;
                return jsonResponse(result, status);
            } catch (const std::exception& ex) {
                // v0.9.44: parse-error -> 400 (was uncaught -> 500).
                return jsonResponse(OperationResult{ false, false,
                    std::string("Invalid sub-agent group removal payload: ") + ex.what() }, 400);
            }
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
                if (result.succeeded) { return jsonResponse(result, 200); }
                // v0.9.46: 404 for missing client (continues v0.9.30
                // 200/false-with-not-found-message -> 404 sweep).
                const int status = (result.message == "LAN client not found.") ? 404 : 400;
                return jsonResponse(result, status);
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
                if (result.succeeded) { return jsonResponse(result, 200); }
                // v0.9.46: 404 for missing client.
                const int status = (result.message == "LAN client not found.") ? 404 : 400;
                return jsonResponse(result, status);
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
            if (result.succeeded) {
                return jsonResponse(result, 200);
            }
            // v0.9.37: distinguish 404 (resource doesn't exist) from 400
            // (request was malformed). Pre-v0.9.37 every removeClient
            // failure returned 400 -- including the common case of "tried
            // to delete a client that's not registered." RFC 7231 §6.5.4
            // is the right status for that. Other failure modes (governance
            // denial, validation errors) keep 400.
            const int status = (result.message == "LAN client not found.") ? 404 : 400;
            return jsonResponse(result, status);
        }
        if (request.method == "POST" && request.path == "/api/forsetti/modules/state") {
            if (auto deny = requirePrivilege(context.privileges.canManageModules,
                                             "canManageModules")) {
                return *deny;
            }
            // v0.9.42: parse-error -> 400. Pre-v0.9.42 nlohmann::json::parse
            // on a malformed body threw past the route to the outer 500
            // handler. Empty body is special-cased to {} and is fine.
            nlohmann::json payload;
            try {
                payload = request.body.empty() ? nlohmann::json::object() : nlohmann::json::parse(request.body);
            } catch (const std::exception& ex) {
                return jsonResponse(OperationResult{ false, false,
                    std::string("Invalid Forsetti module state payload: ") + ex.what() }, 400);
            }
            const auto moduleId = payload.value("moduleId", std::string{});
            const auto action = payload.value("action", std::string{});
            const auto governanceAction = (action == "disable" || action == "remove")
                ? GovernanceActionKind::ModuleDisable
                : GovernanceActionKind::ModuleEnable;
            if (auto governance = enforceGovernance(governanceAction, moduleId)) {
                return *governance;
            }
            const auto result = manageForsettiModule(moduleId, action);
            if (result.succeeded) { return jsonResponse(result, 200); }
            // v0.9.47: 404 for missing module.
            const int status = (result.message == "The selected Forsetti module was not found.") ? 404 : 400;
            return jsonResponse(result, status);
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
                // v0.9.43: 503 -- approval queue service not initialized
                // (closes a v0.9.39 sweep gap).
                return jsonResponse(OperationResult{ false, false, "Approval queue is not ready." }, 503);
            }
            const auto prefix = std::string("/api/clu/approvals/");
            const auto suffix = std::string("/approve");
            const auto deferredId = request.path.substr(
                prefix.size(),
                request.path.size() - prefix.size() - suffix.size());
            const auto result = governanceApprovalQueueService_->approve(deferredId, context.actor);
            if (result.succeeded) { return jsonResponse(result, 200); }
            // v0.9.43: 404 for missing deferred action, 400 otherwise.
            // Same pattern as v0.9.30 / v0.9.33 / v0.9.37 / v0.9.38.
            const int status = (result.message == "Deferred action not found.") ? 404 : 400;
            return jsonResponse(result, status);
        }
        if (request.method == "POST"
            && startsWith(request.path, "/api/clu/approvals/")
            && endsWith(request.path, "/reject")) {
            if (auto deny = requirePrivilege(context.privileges.canChangeGovernancePolicy,
                                             "canChangeGovernancePolicy")) {
                return *deny;
            }
            if (!governanceApprovalQueueService_) {
                // v0.9.43: 503 -- approval queue service not initialized
                // (closes a v0.9.39 sweep gap).
                return jsonResponse(OperationResult{ false, false, "Approval queue is not ready." }, 503);
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
            if (result.succeeded) { return jsonResponse(result, 200); }
            // v0.9.43: 404 for missing deferred action.
            const int status = (result.message == "Deferred action not found.") ? 404 : 400;
            return jsonResponse(result, status);
        }
        // v0.9.55: catch POST /api/clu/approvals/{id}/{verb} for any
        // verb other than /approve or /reject. Pre-v0.9.55 these fell
        // through to staticFileResponse and returned a generic 404 "Not
        // found", which is technically correct but doesn't tell the
        // operator that the URL shape is the issue. Same shape as the
        // v0.9.54 setup-dependencies bad-verb 400.
        if (request.method == "POST" && startsWith(request.path, "/api/clu/approvals/")) {
            const auto prefix = std::string("/api/clu/approvals/");
            const auto suffix = request.path.substr(prefix.size());
            const auto slash = suffix.find('/');
            if (slash != std::string::npos) {
                const std::string action = suffix.substr(slash + 1);
                return jsonResponse(OperationResult{ false, false,
                    std::string("Unknown approval action '") + action
                    + "'. Supported actions: approve, reject." }, 400);
            }
            return jsonResponse(OperationResult{ false, false,
                "Approval action route is POST /api/clu/approvals/{id}/{approve|reject}." }, 400);
        }
        if (request.method == "POST" && request.path == "/api/clu/execute") {
            try {
                const auto result = adminApiService_->executeGovernanceToolJson(request.body);
                return jsonResponse(result, result.succeeded ? 200 : 400);
            } catch (const std::exception& ex) {
                // v0.9.42: parse-error -> 400.
                return jsonResponse(OperationResult{ false, false,
                    std::string("Invalid CLU execute payload: ") + ex.what() }, 400);
            }
        }
        if (request.method == "POST" && request.path == "/api/clu/apple-operations/cancel") {
            try {
                const auto result = adminApiService_->cancelAppleOperationJson(request.body);
                if (result.succeeded) { return jsonResponse(result, 200); }
                // v0.9.47: 404 for missing operation. Substring match
                // because the message embeds the operationId in quotes
                // ("Apple operation '<id>' was not found.").
                const int status = (result.message.find("was not found.") != std::string::npos) ? 404 : 400;
                return jsonResponse(result, status);
            } catch (const std::exception& ex) {
                // v0.9.42: parse-error -> 400.
                return jsonResponse(OperationResult{ false, false,
                    std::string("Invalid Apple operations cancel payload: ") + ex.what() }, 400);
            }
        }
        // v0.9.41: catch parse / from_json throws and return 400, not
        // 500. Pre-v0.9.41 the three install routes called into
        // adminApiService_'s install*Json helpers which call
        // nlohmann::json::parse(requestBody).get<Spec>() with no try/
        // catch; an empty body or a body that doesn't deserialize into
        // the Spec (InstallerPackageSpec / BootstrapRepoSpec /
        // ZipBundleSpec) threw and the throw bubbled to the outer 500
        // handler. Same shape as the v0.9.38 fix for /api/runtime/
        // mcp-servers and /api/runtime/subagents.
        if (request.method == "POST" && request.path == "/api/install/package") {
            try {
                const auto result = adminApiService_->installPackageJson(request.body);
                return jsonResponse(result, result.succeeded ? 200 : 400);
            } catch (const std::exception& ex) {
                return jsonResponse(OperationResult{ false, false,
                    std::string("Invalid InstallerPackageSpec payload: ") + ex.what() }, 400);
            }
        }
        if (request.method == "POST" && request.path == "/api/install/repo") {
            try {
                const auto result = adminApiService_->installRepoJson(request.body);
                return jsonResponse(result, result.succeeded ? 200 : 400);
            } catch (const std::exception& ex) {
                return jsonResponse(OperationResult{ false, false,
                    std::string("Invalid BootstrapRepoSpec payload: ") + ex.what() }, 400);
            }
        }
        if (request.method == "POST" && request.path == "/api/install/zip") {
            try {
                const auto result = adminApiService_->installZipJson(request.body);
                return jsonResponse(result, result.succeeded ? 200 : 400);
            } catch (const std::exception& ex) {
                return jsonResponse(OperationResult{ false, false,
                    std::string("Invalid ZipBundleSpec payload: ") + ex.what() }, 400);
            }
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

        // v0.9.28: before falling through to staticFileResponse (which
        // returns 404 if no static file matches), check whether the
        // path matches a known operator API route under a different
        // method. RFC 7231 §6.5.5: a known URI hit by an unsupported
        // verb should return 405 with an Allow header listing the
        // supported methods. Pre-v0.9.28 this returned an opaque 404
        // that hid the difference between "endpoint doesn't exist"
        // and "wrong verb on a real endpoint" -- the bug-hunt found
        // this when DELETE/PUT/PATCH on /api/version returned 404.
        const auto allowed = supportedMethodsForPath(request.path);
        if (!allowed.empty()) {
            HttpResponse mna;
            mna.statusCode = 405;
            mna.contentType = "application/json";
            mna.extraHeaders.push_back({"Allow", buildAllowHeader(allowed)});
            mna.body = nlohmann::json{
                { "succeeded", false },
                { "message", "Method Not Allowed" },
                { "path", request.path },
                { "method", request.method },
                { "allow", buildAllowHeader(allowed) }
            }.dump();
            return mna;
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

void MasterControlApplication::Impl::persistSupervisedPoolsToConfiguration() {
    if (!workerSupervisor_ || !configurationService_) {
        return;
    }
    // Snapshot the live supervisor pools, strip live instance state
    // (PIDs / Ready/Busy lifecycle / sampled telemetry) so we serialize
    // only the persistent definition. Instance state is not durable
    // across an MCOS process restart -- supervised processes die when
    // their Job Object closes -- so persisting them would lie. The
    // operator re-spawns via POST /api/pools/{id}/scale after load.
    auto live = workerSupervisor_->listPools();
    for (auto& pool : live) {
        pool.instances.clear();
    }

    auto cfg = configurationService_->current();
    cfg.pools = std::move(live);
    // confirmUnsafeChanges=false: this is operator-driven (POST /api/pools
    // from the dashboard or the bridge plugin), so it does not require
    // CLU governance approval. The configuration update goes through the
    // normal save path.
    configurationService_->update(cfg, false);
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
