// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#include "pch.h"

#include "MasterControl/MasterControlDiagnostics.h"
#include "SnapshotCollectionMerge.h"
#include "ShellRuntime.h"

#include <ShlObj.h>
#include <mutex>

namespace MasterControlShell {

namespace {

using namespace winrt;
using namespace Windows::Data::Json;

// Keep the legacy service identity stable so the shell can attach to upgraded installs.
constexpr wchar_t kServiceName[] = L"MasterControlProgram";
constexpr wchar_t kDataDirectoryOverrideVariable[] = L"MASTERCONTROL_DATA_DIR";
constexpr wchar_t kCurrentDataDirectoryLeaf[] = L"MasterControlOrchestrationServer";
constexpr wchar_t kLegacyDataDirectoryLeaf[] = L"MasterControlProgram";

struct ServiceSnapshot final {
    ServiceState state = ServiceState::Missing;
    uint32_t processId = 0;
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

std::optional<std::wstring> wideEnvironmentVariable(const wchar_t* name) {
    const DWORD required = GetEnvironmentVariableW(name, nullptr, 0);
    if (required == 0) {
        return std::nullopt;
    }

    std::wstring value(static_cast<size_t>(required), L'\0');
    const DWORD copied = GetEnvironmentVariableW(name, value.data(), required);
    if (copied == 0) {
        return std::nullopt;
    }
    value.resize(static_cast<size_t>(copied));
    return value;
}

std::filesystem::path programDataDirectory() {
    PWSTR path = nullptr;
    const HRESULT result = SHGetKnownFolderPath(FOLDERID_ProgramData, KF_FLAG_DEFAULT, nullptr, &path);
    if (FAILED(result)) {
        return std::filesystem::temp_directory_path();
    }

    std::filesystem::path directory(path);
    CoTaskMemFree(path);
    return directory;
}

std::string narrowFromWide(const std::wstring& input) {
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

std::wstring trimWideCopy(std::wstring value) {
    value.erase(
        value.begin(),
        std::find_if(
            value.begin(),
            value.end(),
            [](wchar_t character) { return iswspace(character) == 0; }));
    value.erase(
        std::find_if(
            value.rbegin(),
            value.rend(),
            [](wchar_t character) { return iswspace(character) == 0; })
            .base(),
        value.end());
    return value;
}

std::string jsonStringOr(const JsonObject& object, const wchar_t* key, const std::string& fallback = {}) {
    if (!object.HasKey(key)) {
        return fallback;
    }

    if (const auto value = object.GetNamedValue(key, nullptr); value != nullptr && value.ValueType() == JsonValueType::String) {
        return narrowFromWide(value.GetString().c_str());
    }
    return fallback;
}

bool jsonBoolOr(const JsonObject& object, const wchar_t* key, const bool fallback = false) {
    if (!object.HasKey(key)) {
        return fallback;
    }

    if (const auto value = object.GetNamedValue(key, nullptr); value != nullptr && value.ValueType() == JsonValueType::Boolean) {
        return value.GetBoolean();
    }
    return fallback;
}

double jsonNumberOr(const JsonObject& object, const wchar_t* key, const double fallback = 0.0) {
    if (!object.HasKey(key)) {
        return fallback;
    }

    if (const auto value = object.GetNamedValue(key, nullptr); value != nullptr && value.ValueType() == JsonValueType::Number) {
        return value.GetNumber();
    }
    return fallback;
}

std::optional<JsonObject> parseJsonObject(const std::string& text) {
    if (text.empty()) {
        return std::nullopt;
    }

    JsonObject json;
    if (!JsonObject::TryParse(hstring(wideFromUtf8(text)), json)) {
        return std::nullopt;
    }
    return json;
}

std::optional<JsonArray> parseJsonArray(const std::string& text) {
    if (text.empty()) {
        return std::nullopt;
    }

    JsonArray json;
    if (!JsonArray::TryParse(hstring(wideFromUtf8(text)), json)) {
        return std::nullopt;
    }
    return json;
}

std::optional<std::string> readFileUtf8(const std::filesystem::path& filePath) {
    std::ifstream stream(filePath, std::ios::binary);
    if (!stream.is_open()) {
        return std::nullopt;
    }

    std::string content(
        (std::istreambuf_iterator<char>(stream)),
        std::istreambuf_iterator<char>());

    if (content.size() >= 3 &&
        static_cast<unsigned char>(content[0]) == 0xEF &&
        static_cast<unsigned char>(content[1]) == 0xBB &&
        static_cast<unsigned char>(content[2]) == 0xBF) {
        content.erase(0, 3);
    }

    return content;
}

struct LocalCliSignInSession final {
    std::wstring sessionId;
    std::wstring bridge;
    std::wstring providerId;
    std::wstring status = L"pending";
    std::wstring message;
    std::wstring accountLabel;
    std::filesystem::path authFilePath;
    HANDLE processHandle = nullptr;
    bool registrationInProgress = false;
    bool awaitingAuthFile = false;
    std::chrono::steady_clock::time_point authFileDeadline{};
};

std::mutex gCliSignInMutex;
std::map<std::wstring, LocalCliSignInSession> gCliSignInSessions;
std::atomic<uint64_t> gCliSignInCounter{ 0 };

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

std::optional<std::filesystem::path> findCommandOnPath(const std::vector<std::wstring>& fileNames) {
    for (const auto& fileName : fileNames) {
        std::array<wchar_t, 4096> buffer{};
        const DWORD length = SearchPathW(nullptr, fileName.c_str(), nullptr, static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
        if (length > 0 && length < buffer.size()) {
            return std::filesystem::path(buffer.data());
        }
    }

    std::vector<std::filesystem::path> fallbackDirectories;
    wchar_t systemRoot[MAX_PATH] = {};
    const auto systemRootLength = GetEnvironmentVariableW(L"SystemRoot", systemRoot, MAX_PATH);
    if (systemRootLength > 0 && systemRootLength < MAX_PATH) {
        fallbackDirectories.emplace_back(
            std::filesystem::path(systemRoot) / L"System32" / L"config" / L"systemprofile" / L"AppData" / L"Roaming" / L"npm");
    }

    wchar_t appDataBuffer[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appDataBuffer))) {
        fallbackDirectories.emplace_back(std::filesystem::path(appDataBuffer) / L"npm");
    }

    fallbackDirectories.emplace_back(L"C:\\Program Files\\nodejs");
    for (const auto& directory : fallbackDirectories) {
        for (const auto& fileName : fileNames) {
            const auto candidate = directory / fileName;
            std::error_code error;
            if (std::filesystem::exists(candidate, error)) {
                return candidate;
            }
        }
    }

    return std::nullopt;
}

std::optional<std::filesystem::path> resolveCliPath(const std::wstring& bridge) {
    if (bridge == L"claude") {
        if (const auto configured = wideEnvironmentVariable(L"MASTERCONTROL_CLAUDE_COMMAND");
            configured.has_value() && !configured->empty()) {
            return std::filesystem::path(*configured);
        }
        return findCommandOnPath({ L"claude.exe", L"claude.cmd", L"claude.bat", L"claude" });
    }
    if (bridge == L"codex") {
        if (const auto configured = wideEnvironmentVariable(L"MASTERCONTROL_CODEX_COMMAND");
            configured.has_value() && !configured->empty()) {
            return std::filesystem::path(*configured);
        }
        return findCommandOnPath({ L"codex.exe", L"codex.cmd", L"codex.bat", L"codex" });
    }
    return std::nullopt;
}

std::optional<std::filesystem::path> resolveCommandProcessorPath() {
    if (const auto comSpec = wideEnvironmentVariable(L"ComSpec"); comSpec.has_value() && !comSpec->empty()) {
        std::filesystem::path candidate(*comSpec);
        std::error_code error;
        if (std::filesystem::exists(candidate, error)) {
            return candidate;
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

    return findCommandOnPath({ L"cmd.exe", L"cmd" });
}

std::optional<std::filesystem::path> currentUserProfileDirectory() {
    if (const auto userProfile = wideEnvironmentVariable(L"USERPROFILE"); userProfile.has_value() && !userProfile->empty()) {
        return std::filesystem::path(*userProfile);
    }

    PWSTR profilePath = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Profile, KF_FLAG_DEFAULT, nullptr, &profilePath)) &&
        profilePath != nullptr) {
        std::filesystem::path profile(profilePath);
        CoTaskMemFree(profilePath);
        return profile;
    }

    if (profilePath != nullptr) {
        CoTaskMemFree(profilePath);
    }
    return std::nullopt;
}

std::filesystem::path expectedCliAuthFilePath(const std::wstring& bridge) {
    const auto profileDirectory = currentUserProfileDirectory();
    if (!profileDirectory.has_value()) {
        return {};
    }

    if (bridge == L"claude") {
        return *profileDirectory / L".claude" / L".credentials.json";
    }
    if (bridge == L"codex") {
        return *profileDirectory / L".codex" / L"auth.json";
    }
    return {};
}

std::wstring cliBridgeDisplayName(const std::wstring& bridge) {
    if (bridge == L"claude") {
        return L"Claude Code CLI";
    }
    if (bridge == L"codex") {
        return L"Codex CLI";
    }
    return bridge;
}

std::wstring cliBridgeAccountLabel(const std::wstring& bridge) {
    if (bridge == L"claude") {
        return L"Claude Pro / Max / Team account";
    }
    if (bridge == L"codex") {
        return L"OpenAI account (ChatGPT + Codex)";
    }
    return bridge;
}

bool usesCmdShim(const std::filesystem::path& executablePath) {
    const auto extension = executablePath.extension().wstring();
    return _wcsicmp(extension.c_str(), L".cmd") == 0 ||
        _wcsicmp(extension.c_str(), L".bat") == 0;
}

std::wstring generateCliSignInSessionId() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(now).count();
    return L"signin-" + std::to_wstring(micros) + L"-" + std::to_wstring(gCliSignInCounter.fetch_add(1));
}

std::wstring serviceStateLabel(const ServiceState state) {
    switch (state) {
        case ServiceState::Missing: return L"Missing";
        case ServiceState::Stopped: return L"Stopped";
        case ServiceState::StartPending: return L"Starting";
        case ServiceState::StopPending: return L"Stopping";
        case ServiceState::Running: return L"Running";
        case ServiceState::Paused: return L"Paused";
        default: return L"Unknown";
    }
}

ServiceState translateServiceState(const DWORD state) {
    switch (state) {
        case SERVICE_STOPPED: return ServiceState::Stopped;
        case SERVICE_START_PENDING: return ServiceState::StartPending;
        case SERVICE_STOP_PENDING: return ServiceState::StopPending;
        case SERVICE_RUNNING: return ServiceState::Running;
        case SERVICE_PAUSED: return ServiceState::Paused;
        default: return ServiceState::Unknown;
    }
}

ServiceSnapshot queryServiceSnapshot() {
    ServiceSnapshot snapshot;

    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (scm == nullptr) {
        snapshot.state = ServiceState::Unknown;
        return snapshot;
    }

    SC_HANDLE service = OpenServiceW(scm, kServiceName, SERVICE_QUERY_STATUS);
    if (service == nullptr) {
        CloseServiceHandle(scm);
        snapshot.state = ServiceState::Missing;
        return snapshot;
    }

    SERVICE_STATUS_PROCESS status{};
    DWORD bytesNeeded = 0;
    if (QueryServiceStatusEx(
            service,
            SC_STATUS_PROCESS_INFO,
            reinterpret_cast<LPBYTE>(&status),
            sizeof(status),
            &bytesNeeded) != 0) {
        snapshot.state = translateServiceState(status.dwCurrentState);
        snapshot.processId = status.dwProcessId;
    } else {
        snapshot.state = ServiceState::Unknown;
    }

    CloseServiceHandle(service);
    CloseServiceHandle(scm);
    return snapshot;
}

struct HttpResponse final {
    DWORD statusCode = 0;
    std::string body;
};

std::optional<HttpResponse> httpRequest(const std::string& host,
                                        const INTERNET_PORT port,
                                        const std::wstring& method,
                                        const std::wstring& path,
                                        const std::string& requestBody,
                                        const std::vector<std::pair<std::wstring, std::wstring>>& headers,
                                        std::wstring& errorMessage) {
    // NO_PROXY: this is exclusively localhost traffic on 127.0.0.1 (or the
    // configured LAN bind address). AUTOMATIC_PROXY was forcing WinHttp to
    // run WPAD/PAC detection on every request and — on a machine whose
    // corp-managed proxy config lists localhost as *not* bypassed, or where
    // the proxy service is transiently slow — the dashboard probe was
    // dropping through the 4s receive timeout, leaving providerCapabilities_
    // empty (observed: dropdown blank, "Assign Roles" empty, "API OFFLINE"
    // chip shown even though service is running and /api/health returns
    // HTTP 200 to curl). Going direct fixes both.
    HINTERNET session = WinHttpOpen(
        L"MasterControlShell/2.0",
        WINHTTP_ACCESS_TYPE_NO_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);
    if (session == nullptr) {
        errorMessage = L"Unable to initialize WinHTTP.";
        return std::nullopt;
    }

    // Fail fast on DNS/connect when the service is dead (500ms is plenty
    // for localhost). Bumped the receive timeout from 4s -> 10s because
    // /api/dashboard on a real box with governance state + Apple operations
    // + execution history + Forsetti module catalog can legitimately take
    // 3-5 seconds to serialize — the old 4s ceiling was clipping snapshots
    // mid-flight and emptying providerCapabilities_. Values tuned for
    // "fail fast on dead, patient on slow-but-alive".
    WinHttpSetTimeouts(session, 500, 500, 500, 10000);

    HINTERNET connection = WinHttpConnect(session, wideFromUtf8(host).c_str(), port, 0);
    if (connection == nullptr) {
        errorMessage = L"Unable to connect to the local admin API.";
        WinHttpCloseHandle(session);
        return std::nullopt;
    }

    HINTERNET request = WinHttpOpenRequest(
        connection,
        method.c_str(),
        path.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        0);
    if (request == nullptr) {
        errorMessage = L"Unable to open the local admin API request.";
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return std::nullopt;
    }

    std::wstring headerBlock;
    for (const auto& [name, value] : headers) {
        headerBlock += name;
        headerBlock += L": ";
        headerBlock += value;
        headerBlock += L"\r\n";
    }
    if (!requestBody.empty() && headerBlock.find(L"Content-Type:") == std::wstring::npos) {
        headerBlock += L"Content-Type: application/json\r\n";
    }

    const wchar_t* headerData = headerBlock.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headerBlock.c_str();
    const DWORD headerLength = headerBlock.empty() ? 0 : static_cast<DWORD>(headerBlock.size());
    LPVOID optionalData = requestBody.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(requestBody.data());
    const DWORD optionalLength = static_cast<DWORD>(requestBody.size());

    if (WinHttpSendRequest(request,
                           headerData,
                           headerLength,
                           optionalData,
                           optionalLength,
                           optionalLength,
                           0) == 0 ||
        WinHttpReceiveResponse(request, nullptr) == 0) {
        errorMessage = L"Local admin API request failed.";
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return std::nullopt;
    }

    HttpResponse response;
    DWORD statusCode = 0;
    DWORD statusCodeSize = sizeof(statusCode);
    WinHttpQueryHeaders(
        request,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,
        &statusCode,
        &statusCodeSize,
        WINHTTP_NO_HEADER_INDEX);
    response.statusCode = statusCode;

    std::string body;
    for (;;) {
        DWORD available = 0;
        if (WinHttpQueryDataAvailable(request, &available) == 0 || available == 0) {
            break;
        }

        std::string chunk(static_cast<size_t>(available), '\0');
        DWORD bytesRead = 0;
        if (WinHttpReadData(request, chunk.data(), available, &bytesRead) == 0) {
            errorMessage = L"Unable to read the local admin API response.";
            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connection);
            WinHttpCloseHandle(session);
            return std::nullopt;
        }

        chunk.resize(static_cast<size_t>(bytesRead));
        body += chunk;
    }

    response.body = std::move(body);

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return response;
}

std::optional<HttpResponse> httpGet(const std::string& host,
                                    const INTERNET_PORT port,
                                    const std::wstring& path,
                                    std::wstring& errorMessage) {
    return httpRequest(host, port, L"GET", path, {}, {}, errorMessage);
}

std::string dashboardHostFromBindAddress(const std::string& bindAddress) {
    return bindAddress == "0.0.0.0" ? std::string("127.0.0.1") : bindAddress;
}

std::pair<std::string, uint16_t> adminApiEndpoint(const std::filesystem::path& configurationFile) {
    std::string bindAddress = "0.0.0.0";
    uint16_t browserPort = 7300;

    if (const auto configurationText = readFileUtf8(configurationFile); configurationText.has_value()) {
        if (const auto configuration = parseJsonObject(*configurationText); configuration.has_value()) {
            bindAddress = jsonStringOr(*configuration, L"bindAddress", bindAddress);
            browserPort = static_cast<uint16_t>(jsonNumberOr(*configuration, L"browserPort", browserPort));
        }
    }

    return { dashboardHostFromBindAddress(bindAddress), browserPort };
}

std::vector<std::wstring> jsonStringArrayOr(const JsonObject& object, const wchar_t* key) {
    std::vector<std::wstring> values;
    if (!object.HasKey(key)) {
        return values;
    }

    for (const auto& value : object.GetNamedArray(key, JsonArray())) {
        if (value.ValueType() == JsonValueType::String) {
            values.emplace_back(value.GetString().c_str());
        }
    }
    return values;
}

std::map<std::wstring, std::wstring> jsonStringMapOr(const JsonObject& object, const wchar_t* key) {
    std::map<std::wstring, std::wstring> values;
    if (!object.HasKey(key)) {
        return values;
    }

    const auto mapObject = object.GetNamedObject(key, JsonObject());
    for (const auto& pair : mapObject.GetView()) {
        if (pair.Value().ValueType() == JsonValueType::String) {
            values.emplace(std::wstring(pair.Key().c_str()), std::wstring(pair.Value().GetString().c_str()));
        }
    }
    return values;
}

ShellRuntimeEndpoint runtimeEndpointFromJson(const JsonObject& object) {
    ShellRuntimeEndpoint endpoint;
    endpoint.id = wideFromUtf8(jsonStringOr(object, L"id", ""));
    endpoint.displayName = wideFromUtf8(jsonStringOr(object, L"displayName", ""));
    endpoint.kind = wideFromUtf8(jsonStringOr(object, L"kind", "mcp_server"));
    endpoint.host = wideFromUtf8(jsonStringOr(object, L"host", ""));
    endpoint.port = static_cast<uint16_t>(jsonNumberOr(object, L"port", 0));
    endpoint.protocol = wideFromUtf8(jsonStringOr(object, L"protocol", "http"));
    endpoint.status = wideFromUtf8(jsonStringOr(object, L"status", "unknown"));
    endpoint.description = wideFromUtf8(jsonStringOr(object, L"description", ""));
    endpoint.routePath = wideFromUtf8(jsonStringOr(object, L"routePath", ""));
    endpoint.specialization = wideFromUtf8(jsonStringOr(object, L"specialization", ""));
    endpoint.userDefined = jsonBoolOr(object, L"userDefined", false);
    return endpoint;
}

ShellProviderConnection providerFromJson(const JsonObject& object) {
    ShellProviderConnection provider;
    provider.id = wideFromUtf8(jsonStringOr(object, L"id", ""));
    provider.kind = wideFromUtf8(jsonStringOr(object, L"kind", "generic"));
    provider.displayName = wideFromUtf8(jsonStringOr(object, L"displayName", ""));
    provider.baseUrl = wideFromUtf8(jsonStringOr(object, L"baseUrl", ""));
    provider.modelId = wideFromUtf8(jsonStringOr(object, L"modelId", ""));
    provider.enabled = jsonBoolOr(object, L"enabled", true);
    provider.allowAutonomousControl = jsonBoolOr(object, L"allowAutonomousControl", false);
    provider.credentialsConfigured = jsonBoolOr(object, L"credentialsConfigured", false);
    return provider;
}

ShellProviderCredentialField credentialFieldFromJson(const JsonObject& object) {
    ShellProviderCredentialField field;
    field.fieldId = wideFromUtf8(jsonStringOr(object, L"fieldId", ""));
    field.label = wideFromUtf8(jsonStringOr(object, L"label", ""));
    field.kind = wideFromUtf8(jsonStringOr(object, L"kind", "text"));
    field.helpText = wideFromUtf8(jsonStringOr(object, L"helpText", ""));
    field.placeholder = wideFromUtf8(jsonStringOr(object, L"placeholder", ""));
    field.environmentVariableHint = wideFromUtf8(jsonStringOr(object, L"environmentVariableHint", ""));
    field.requirementGroup = wideFromUtf8(jsonStringOr(object, L"requirementGroup", ""));
    field.secret = jsonBoolOr(object, L"secret", false);
    field.required = jsonBoolOr(object, L"required", false);
    return field;
}

ShellProviderCapability providerCapabilityFromJson(const JsonObject& object) {
    ShellProviderCapability capability;
    capability.moduleId = wideFromUtf8(jsonStringOr(object, L"moduleId", ""));
    capability.providerId = wideFromUtf8(jsonStringOr(object, L"providerId", ""));
    capability.kind = wideFromUtf8(jsonStringOr(object, L"kind", "generic"));
    capability.displayName = wideFromUtf8(jsonStringOr(object, L"displayName", ""));
    capability.description = wideFromUtf8(jsonStringOr(object, L"description", ""));
    capability.defaultBaseUrl = wideFromUtf8(jsonStringOr(object, L"defaultBaseUrl", ""));
    capability.recommendedModel = wideFromUtf8(jsonStringOr(object, L"recommendedModel", ""));
    capability.runtimeRequirements = jsonStringArrayOr(object, L"runtimeRequirements");
    capability.supportedTargets = jsonStringArrayOr(object, L"supportedTargets");
    capability.supportsSharedMcpAccess = jsonBoolOr(object, L"supportsSharedMcpAccess", true);
    capability.supportsAutonomousControl = jsonBoolOr(object, L"supportsAutonomousControl", true);
    for (const auto& value : object.GetNamedArray(L"credentialFields", JsonArray())) {
        if (value.ValueType() == JsonValueType::Object) {
            capability.credentialFields.push_back(credentialFieldFromJson(value.GetObject()));
        }
    }
    return capability;
}

ShellProviderCredentialStatus providerCredentialStatusFromJson(const JsonObject& object) {
    ShellProviderCredentialStatus status;
    status.providerId = wideFromUtf8(jsonStringOr(object, L"providerId", ""));
    status.configured = jsonBoolOr(object, L"configured", false);
    status.configuredFieldIds = jsonStringArrayOr(object, L"configuredFieldIds");
    status.updatedAtUtc = wideFromUtf8(jsonStringOr(object, L"updatedAtUtc", ""));
    status.message = wideFromUtf8(jsonStringOr(object, L"message", ""));
    return status;
}

ShellSubAgentGroupDefinition subAgentGroupFromJson(const JsonObject& object) {
    ShellSubAgentGroupDefinition group;
    group.groupId = wideFromUtf8(jsonStringOr(object, L"groupId", ""));
    group.displayName = wideFromUtf8(jsonStringOr(object, L"displayName", ""));
    group.description = wideFromUtf8(jsonStringOr(object, L"description", ""));
    group.memberTargetIds = jsonStringArrayOr(object, L"memberTargetIds");
    group.updatedAtUtc = wideFromUtf8(jsonStringOr(object, L"updatedAtUtc", ""));
    return group;
}

ShellProviderAssignmentTarget providerAssignmentTargetFromJson(const JsonObject& object) {
    ShellProviderAssignmentTarget target;
    target.targetId = wideFromUtf8(jsonStringOr(object, L"targetId", ""));
    target.kind = wideFromUtf8(jsonStringOr(object, L"kind", "role"));
    target.displayName = wideFromUtf8(jsonStringOr(object, L"displayName", ""));
    target.description = wideFromUtf8(jsonStringOr(object, L"description", ""));
    target.memberTargetIds = jsonStringArrayOr(object, L"memberTargetIds");
    return target;
}

ShellProviderAssignment providerAssignmentFromJson(const JsonObject& object) {
    ShellProviderAssignment assignment;
    assignment.targetId = wideFromUtf8(jsonStringOr(object, L"targetId", ""));
    assignment.kind = wideFromUtf8(jsonStringOr(object, L"kind", "role"));
    assignment.providerId = wideFromUtf8(jsonStringOr(object, L"providerId", ""));
    assignment.updatedAtUtc = wideFromUtf8(jsonStringOr(object, L"updatedAtUtc", ""));
    return assignment;
}

ShellProviderExecutionRegistration providerExecutionRegistrationFromJson(const JsonObject& object) {
    ShellProviderExecutionRegistration registration;
    registration.moduleId = wideFromUtf8(jsonStringOr(object, L"moduleId", ""));
    registration.providerId = wideFromUtf8(jsonStringOr(object, L"providerId", ""));
    registration.kind = wideFromUtf8(jsonStringOr(object, L"kind", "generic"));
    registration.displayName = wideFromUtf8(jsonStringOr(object, L"displayName", ""));
    registration.transport = wideFromUtf8(jsonStringOr(object, L"transport", ""));
    registration.supportsSharedMcpAccess = jsonBoolOr(object, L"supportsSharedMcpAccess", true);
    registration.supportsDirectMcpConfig = jsonBoolOr(object, L"supportsDirectMcpConfig", false);
    return registration;
}

ShellProviderExecutionRecord providerExecutionRecordFromJson(const JsonObject& object) {
    ShellProviderExecutionRecord record;
    record.executionId = wideFromUtf8(jsonStringOr(object, L"executionId", ""));
    record.targetId = wideFromUtf8(jsonStringOr(object, L"targetId", ""));
    record.targetDisplayName = wideFromUtf8(jsonStringOr(object, L"targetDisplayName", ""));
    record.providerId = wideFromUtf8(jsonStringOr(object, L"providerId", ""));
    record.providerDisplayName = wideFromUtf8(jsonStringOr(object, L"providerDisplayName", ""));
    record.status = wideFromUtf8(jsonStringOr(object, L"status", "failed"));
    record.modelId = wideFromUtf8(jsonStringOr(object, L"modelId", ""));
    record.referencedMcpServerIds = jsonStringArrayOr(object, L"referencedMcpServerIds");
    record.toolEvents = jsonStringArrayOr(object, L"toolEvents");
    record.outputText = wideFromUtf8(jsonStringOr(object, L"outputText", ""));
    record.rawResponse = wideFromUtf8(jsonStringOr(object, L"rawResponse", ""));
    record.startedAtUtc = wideFromUtf8(jsonStringOr(object, L"startedAtUtc", ""));
    record.completedAtUtc = wideFromUtf8(jsonStringOr(object, L"completedAtUtc", ""));
    record.errorMessage = wideFromUtf8(jsonStringOr(object, L"errorMessage", ""));
    return record;
}

ShellSecuritySettings securityFromJson(const JsonObject& object) {
    ShellSecuritySettings settings;
    settings.enableTls = jsonBoolOr(object, L"enableTls", false);
    settings.enableAuthentication = jsonBoolOr(object, L"enableAuthentication", false);
    settings.allowTroubleshootingBypass = jsonBoolOr(object, L"allowTroubleshootingBypass", false);
    settings.allowOpenLanAccess = jsonBoolOr(object, L"allowOpenLanAccess", true);
    settings.securityProtocolsEnabled = jsonBoolOr(object, L"securityProtocolsEnabled", true);
    settings.trustedRemoteHosts = jsonStringArrayOr(object, L"trustedRemoteHosts");
    return settings;
}

ShellAppleRemoteHost appleRemoteHostFromJson(const JsonObject& object) {
    ShellAppleRemoteHost host;
    host.hostId = wideFromUtf8(jsonStringOr(object, L"hostId", ""));
    host.displayName = wideFromUtf8(jsonStringOr(object, L"displayName", ""));
    host.transport = wideFromUtf8(jsonStringOr(object, L"transport", "unknown"));
    host.platforms = jsonStringArrayOr(object, L"platforms");
    host.address = wideFromUtf8(jsonStringOr(object, L"address", ""));
    host.port = static_cast<uint16_t>(jsonNumberOr(object, L"port", 0));
    host.username = wideFromUtf8(jsonStringOr(object, L"username", ""));
    host.serviceBaseUrl = wideFromUtf8(jsonStringOr(object, L"serviceBaseUrl", ""));
    host.companionHealthPath = wideFromUtf8(jsonStringOr(object, L"companionHealthPath", "/healthz"));
    host.companionExecutePath = wideFromUtf8(jsonStringOr(object, L"companionExecutePath", "/execute"));
    host.preferredDeveloperDirectory = wideFromUtf8(jsonStringOr(object, L"preferredDeveloperDirectory", ""));
    host.defaultSigningIdentity = wideFromUtf8(jsonStringOr(object, L"defaultSigningIdentity", ""));
    host.defaultNotaryKeychainProfile = wideFromUtf8(jsonStringOr(object, L"defaultNotaryKeychainProfile", ""));
    host.defaultNotaryTeamId = wideFromUtf8(jsonStringOr(object, L"defaultNotaryTeamId", ""));
    host.enabled = jsonBoolOr(object, L"enabled", true);

    const auto toolchain = object.GetNamedObject(L"toolchain", JsonObject());
    host.reachable = jsonBoolOr(toolchain, L"reachable", false);
    host.xcodeInstalled = jsonBoolOr(toolchain, L"xcodeInstalled", false);
    host.xcodeVersion = wideFromUtf8(jsonStringOr(toolchain, L"xcodeVersion", ""));
    host.developerDirectory = wideFromUtf8(jsonStringOr(toolchain, L"developerDirectory", ""));
    host.macosSdkAvailable = jsonBoolOr(toolchain, L"macosSdkAvailable", false);
    host.iosSdkAvailable = jsonBoolOr(toolchain, L"iosSdkAvailable", false);
    host.simulatorControlAvailable = jsonBoolOr(toolchain, L"simulatorControlAvailable", false);
    host.deviceControlAvailable = jsonBoolOr(toolchain, L"deviceControlAvailable", false);
    host.toolchainCheckedAtUtc = wideFromUtf8(jsonStringOr(toolchain, L"checkedAtUtc", ""));
    host.toolchainStatus = wideFromUtf8(jsonStringOr(toolchain, L"status", "unknown"));
    host.toolchainMessage = wideFromUtf8(jsonStringOr(toolchain, L"message", ""));
    host.simulatorRuntimes = jsonStringArrayOr(toolchain, L"simulatorRuntimes");

    const auto signing = object.GetNamedObject(L"signing", JsonObject());
    host.signingReady = jsonBoolOr(signing, L"signingReady", false);
    host.developmentSigningReady = jsonBoolOr(signing, L"developmentSigningReady", false);
    host.distributionSigningReady = jsonBoolOr(signing, L"distributionSigningReady", false);
    host.availableTeams = jsonStringArrayOr(signing, L"availableTeams");
    host.signingStatus = wideFromUtf8(jsonStringOr(signing, L"status", "unknown"));
    host.signingMessage = wideFromUtf8(jsonStringOr(signing, L"message", ""));
    host.transportSummary = wideFromUtf8(jsonStringOr(object, L"transportSummary", ""));
    host.credentialProfileSummary = wideFromUtf8(jsonStringOr(object, L"credentialProfileSummary", ""));
    host.readinessIssues = jsonStringArrayOr(object, L"readinessIssues");
    return host;
}

ShellAppleOperationRecord appleOperationFromJson(const JsonObject& object) {
    ShellAppleOperationRecord record;
    record.operationId = wideFromUtf8(jsonStringOr(object, L"operationId", ""));
    record.platform = wideFromUtf8(jsonStringOr(object, L"platform", "unknown"));
    record.toolId = wideFromUtf8(jsonStringOr(object, L"toolId", ""));
    record.displayName = wideFromUtf8(jsonStringOr(object, L"displayName", ""));
    record.hostId = wideFromUtf8(jsonStringOr(object, L"hostId", ""));
    record.hostDisplayName = wideFromUtf8(jsonStringOr(object, L"hostDisplayName", ""));
    record.transport = wideFromUtf8(jsonStringOr(object, L"transport", "unknown"));
    record.status = wideFromUtf8(jsonStringOr(object, L"status", "queued"));
    record.workingDirectory = wideFromUtf8(jsonStringOr(object, L"workingDirectory", ""));
    record.artifactPath = wideFromUtf8(jsonStringOr(object, L"artifactPath", ""));
    record.targetPath = wideFromUtf8(jsonStringOr(object, L"targetPath", ""));
    record.summary = wideFromUtf8(jsonStringOr(object, L"summary", ""));
    record.rawOutput = wideFromUtf8(jsonStringOr(object, L"rawOutput", ""));
    record.errorMessage = wideFromUtf8(jsonStringOr(object, L"errorMessage", ""));
    record.queuedAtUtc = wideFromUtf8(jsonStringOr(object, L"queuedAtUtc", ""));
    record.startedAtUtc = wideFromUtf8(jsonStringOr(object, L"startedAtUtc", ""));
    record.completedAtUtc = wideFromUtf8(jsonStringOr(object, L"completedAtUtc", ""));
    record.requestOptions = jsonStringMapOr(object, L"requestOptions");
    record.routeReason = wideFromUtf8(jsonStringOr(object, L"routeReason", ""));
    record.diagnosticSummary = wideFromUtf8(jsonStringOr(object, L"diagnosticSummary", ""));
    record.selectedDeveloperDirectory = wideFromUtf8(jsonStringOr(object, L"selectedDeveloperDirectory", ""));
    record.credentialProfileSummary = wideFromUtf8(jsonStringOr(object, L"credentialProfileSummary", ""));
    record.readinessIssues = jsonStringArrayOr(object, L"readinessIssues");
    record.redactedRequestOptionKeys = jsonStringArrayOr(object, L"redactedRequestOptionKeys");
    record.rerunReady = jsonBoolOr(object, L"rerunReady", false);
    record.rerunReadinessMessage = wideFromUtf8(jsonStringOr(object, L"rerunReadinessMessage", ""));
    return record;
}

std::string installerKindString(const ShellInstallerKind kind) {
    switch (kind) {
        case ShellInstallerKind::Msi: return "msi";
        case ShellInstallerKind::Exe: return "exe";
        case ShellInstallerKind::PowerShell: return "powershell";
        default: return "exe";
    }
}

JsonObject providerToJson(const ShellProviderConnection& provider) {
    JsonObject object;
    object.SetNamedValue(L"id", JsonValue::CreateStringValue(provider.id));
    object.SetNamedValue(L"kind", JsonValue::CreateStringValue(provider.kind));
    object.SetNamedValue(L"displayName", JsonValue::CreateStringValue(provider.displayName));
    object.SetNamedValue(L"baseUrl", JsonValue::CreateStringValue(provider.baseUrl));
    object.SetNamedValue(L"modelId", JsonValue::CreateStringValue(provider.modelId));
    object.SetNamedValue(L"enabled", JsonValue::CreateBooleanValue(provider.enabled));
    object.SetNamedValue(L"allowAutonomousControl", JsonValue::CreateBooleanValue(provider.allowAutonomousControl));
    return object;
}

JsonObject runtimeEndpointToJson(const ShellRuntimeEndpoint& endpoint) {
    JsonObject object;
    object.SetNamedValue(L"id", JsonValue::CreateStringValue(endpoint.id));
    object.SetNamedValue(L"displayName", JsonValue::CreateStringValue(endpoint.displayName));
    object.SetNamedValue(
        L"kind",
        JsonValue::CreateStringValue(endpoint.kind.empty() ? std::wstring(L"mcp_server") : endpoint.kind));
    object.SetNamedValue(L"host", JsonValue::CreateStringValue(endpoint.host));
    object.SetNamedValue(L"port", JsonValue::CreateNumberValue(endpoint.port));
    object.SetNamedValue(L"protocol", JsonValue::CreateStringValue(endpoint.protocol));
    object.SetNamedValue(L"description", JsonValue::CreateStringValue(endpoint.description));
    object.SetNamedValue(L"routePath", JsonValue::CreateStringValue(endpoint.routePath));
    object.SetNamedValue(L"specialization", JsonValue::CreateStringValue(endpoint.specialization));
    object.SetNamedValue(L"userDefined", JsonValue::CreateBooleanValue(true));
    return object;
}

JsonObject providerCredentialsToJson(const std::wstring& providerId,
                                     const std::vector<std::pair<std::wstring, std::wstring>>& values) {
    JsonObject object;
    JsonObject credentialValues;
    object.SetNamedValue(L"providerId", JsonValue::CreateStringValue(providerId));
    for (const auto& [fieldId, value] : values) {
        credentialValues.SetNamedValue(fieldId, JsonValue::CreateStringValue(value));
    }
    object.SetNamedValue(L"values", credentialValues);
    return object;
}

JsonObject subAgentGroupToJson(const ShellSubAgentGroupDefinition& group) {
    JsonObject object;
    JsonArray members;
    object.SetNamedValue(L"groupId", JsonValue::CreateStringValue(group.groupId));
    object.SetNamedValue(L"displayName", JsonValue::CreateStringValue(group.displayName));
    object.SetNamedValue(L"description", JsonValue::CreateStringValue(group.description));
    for (const auto& memberTargetId : group.memberTargetIds) {
        members.Append(JsonValue::CreateStringValue(memberTargetId));
    }
    object.SetNamedValue(L"memberTargetIds", members);
    return object;
}

JsonObject providerAssignmentToJson(const ShellProviderAssignment& assignment) {
    JsonObject object;
    object.SetNamedValue(L"targetId", JsonValue::CreateStringValue(assignment.targetId));
    object.SetNamedValue(L"kind", JsonValue::CreateStringValue(assignment.kind));
    object.SetNamedValue(L"providerId", JsonValue::CreateStringValue(assignment.providerId));
    return object;
}

JsonObject stringMapToJson(const std::map<std::wstring, std::wstring>& values) {
    JsonObject object;
    for (const auto& [key, value] : values) {
        object.SetNamedValue(key, JsonValue::CreateStringValue(value));
    }
    return object;
}

JsonObject appleRemoteHostToJson(const ShellAppleRemoteHost& host) {
    JsonObject object;
    JsonArray platforms;
    for (const auto& platform : host.platforms) {
        platforms.Append(JsonValue::CreateStringValue(platform));
    }
    object.SetNamedValue(L"hostId", JsonValue::CreateStringValue(host.hostId));
    object.SetNamedValue(L"displayName", JsonValue::CreateStringValue(host.displayName));
    object.SetNamedValue(L"transport", JsonValue::CreateStringValue(host.transport));
    object.SetNamedValue(L"platforms", platforms);
    object.SetNamedValue(L"address", JsonValue::CreateStringValue(host.address));
    object.SetNamedValue(L"port", JsonValue::CreateNumberValue(host.port));
    object.SetNamedValue(L"username", JsonValue::CreateStringValue(host.username));
    object.SetNamedValue(L"serviceBaseUrl", JsonValue::CreateStringValue(host.serviceBaseUrl));
    object.SetNamedValue(L"companionHealthPath", JsonValue::CreateStringValue(host.companionHealthPath));
    object.SetNamedValue(L"companionExecutePath", JsonValue::CreateStringValue(host.companionExecutePath));
    object.SetNamedValue(L"preferredDeveloperDirectory", JsonValue::CreateStringValue(host.preferredDeveloperDirectory));
    object.SetNamedValue(L"defaultSigningIdentity", JsonValue::CreateStringValue(host.defaultSigningIdentity));
    object.SetNamedValue(L"defaultNotaryKeychainProfile", JsonValue::CreateStringValue(host.defaultNotaryKeychainProfile));
    object.SetNamedValue(L"defaultNotaryTeamId", JsonValue::CreateStringValue(host.defaultNotaryTeamId));
    object.SetNamedValue(L"enabled", JsonValue::CreateBooleanValue(host.enabled));
    return object;
}

JsonObject governanceToolRequestToJson(const std::wstring& platform,
                                       const std::wstring& toolId,
                                       const std::wstring& targetPath,
                                       const std::map<std::wstring, std::wstring>& options) {
    JsonObject object;
    object.SetNamedValue(L"platform", JsonValue::CreateStringValue(platform));
    object.SetNamedValue(L"toolId", JsonValue::CreateStringValue(toolId));
    object.SetNamedValue(L"targetPath", JsonValue::CreateStringValue(targetPath));
    object.SetNamedValue(L"options", stringMapToJson(options));
    return object;
}

JsonObject packageToJson(const ShellInstallerPackageSpec& spec) {
    JsonObject object;
    object.SetNamedValue(L"kind", JsonValue::CreateStringValue(wideFromUtf8(installerKindString(spec.kind))));
    object.SetNamedValue(L"source", JsonValue::CreateStringValue(spec.source));
    object.SetNamedValue(L"localPath", JsonValue::CreateStringValue(L""));
    object.SetNamedValue(L"arguments", JsonValue::CreateStringValue(spec.arguments));
    object.SetNamedValue(L"allowUntrustedExecution", JsonValue::CreateBooleanValue(spec.allowUntrustedExecution));
    return object;
}

JsonObject repoToJson(const ShellBootstrapRepoSpec& spec) {
    JsonObject object;
    object.SetNamedValue(L"repositoryUrl", JsonValue::CreateStringValue(spec.repositoryUrl));
    object.SetNamedValue(L"branch", JsonValue::CreateStringValue(spec.branch));
    object.SetNamedValue(L"manifestFile", JsonValue::CreateStringValue(spec.manifestFile));
    object.SetNamedValue(L"allowUntrustedExecution", JsonValue::CreateBooleanValue(spec.allowUntrustedExecution));
    return object;
}

JsonObject zipToJson(const ShellZipBundleSpec& spec) {
    JsonObject object;
    object.SetNamedValue(L"source", JsonValue::CreateStringValue(spec.source));
    object.SetNamedValue(L"manifestFile", JsonValue::CreateStringValue(spec.manifestFile));
    object.SetNamedValue(L"allowUntrustedExecution", JsonValue::CreateBooleanValue(spec.allowUntrustedExecution));
    return object;
}

ShellExportArtifact exportArtifactFromJson(const JsonObject& object) {
    ShellExportArtifact artifact;
    artifact.id = wideFromUtf8(jsonStringOr(object, L"id", ""));
    artifact.fileName = wideFromUtf8(jsonStringOr(object, L"fileName", ""));
    artifact.mediaType = wideFromUtf8(jsonStringOr(object, L"mediaType", ""));
    artifact.content = wideFromUtf8(jsonStringOr(object, L"content", ""));
    return artifact;
}

ShellNavigationPointer navigationPointerFromJson(const JsonObject& object) {
    ShellNavigationPointer pointer;
    pointer.id = wideFromUtf8(jsonStringOr(object, L"pointerID", ""));
    pointer.label = wideFromUtf8(jsonStringOr(object, L"label", ""));
    pointer.destinationId = wideFromUtf8(jsonStringOr(object, L"baseDestinationID", ""));
    return pointer;
}

ShellToolbarActionKind toolbarActionKindFromJson(const JsonObject& action) {
    const auto type = jsonStringOr(action, L"type", "");
    if (type == "navigate") {
        return ShellToolbarActionKind::Navigate;
    }
    if (type == "openOverlay") {
        return ShellToolbarActionKind::OpenOverlay;
    }
    if (type == "publishEvent") {
        return ShellToolbarActionKind::PublishEvent;
    }
    return ShellToolbarActionKind::Unknown;
}

ShellToolbarItem toolbarItemFromJson(const JsonObject& object) {
    ShellToolbarItem item;
    item.id = wideFromUtf8(jsonStringOr(object, L"itemID", ""));
    item.title = wideFromUtf8(jsonStringOr(object, L"title", ""));
    item.systemImageName = wideFromUtf8(jsonStringOr(object, L"systemImageName", ""));

    const auto action = object.GetNamedObject(L"action", JsonObject());
    item.actionKind = toolbarActionKindFromJson(action);
    switch (item.actionKind) {
        case ShellToolbarActionKind::Navigate:
            item.targetId = wideFromUtf8(jsonStringOr(action, L"destinationID", ""));
            break;
        case ShellToolbarActionKind::OpenOverlay:
            item.targetId = wideFromUtf8(jsonStringOr(action, L"routeID", ""));
            break;
        case ShellToolbarActionKind::PublishEvent:
            item.targetId = wideFromUtf8(jsonStringOr(action, L"eventType", ""));
            break;
        default:
            break;
    }
    return item;
}

ShellOverlayPresentation overlayPresentationFromJson(const std::string& presentation) {
    if (presentation == "fullScreen") {
        return ShellOverlayPresentation::FullScreen;
    }
    if (presentation == "popover") {
        return ShellOverlayPresentation::Popover;
    }
    return ShellOverlayPresentation::Sheet;
}

ShellOverlayRoute overlayRouteFromJson(const JsonObject& object) {
    ShellOverlayRoute route;
    route.id = wideFromUtf8(jsonStringOr(object, L"routeID", ""));
    route.label = wideFromUtf8(jsonStringOr(object, L"label", ""));
    route.presentation = overlayPresentationFromJson(jsonStringOr(object, L"presentation", "sheet"));

    const auto destination = object.GetNamedObject(L"destination", JsonObject());
    const auto destinationType = jsonStringOr(destination, L"type", "base");
    if (destinationType == "moduleOverlay") {
        route.targetsModuleView = true;
        route.moduleId = wideFromUtf8(jsonStringOr(destination, L"moduleID", ""));
        route.viewId = wideFromUtf8(jsonStringOr(destination, L"viewID", ""));
    } else {
        route.destinationId = wideFromUtf8(jsonStringOr(destination, L"destinationID", ""));
    }

    return route;
}

ShellViewInjection viewInjectionFromJson(const JsonObject& object) {
    ShellViewInjection injection;
    injection.id = wideFromUtf8(jsonStringOr(object, L"injectionID", ""));
    injection.slot = wideFromUtf8(jsonStringOr(object, L"slot", ""));
    injection.viewId = wideFromUtf8(jsonStringOr(object, L"viewID", ""));
    injection.priority = static_cast<int>(jsonNumberOr(object, L"priority", 0));
    return injection;
}

JsonArray stringArrayToJson(const std::vector<std::wstring>& values) {
    JsonArray array;
    for (const auto& value : values) {
        array.Append(JsonValue::CreateStringValue(value));
    }
    return array;
}

ShellOperationResult operationResultFromResponse(const HttpResponse& response) {
    ShellOperationResult result;
    result.succeeded = response.statusCode >= 200 && response.statusCode < 300;

    if (const auto json = parseJsonObject(response.body); json.has_value()) {
        result.succeeded = jsonBoolOr(*json, L"succeeded", result.succeeded);
        result.requiresConfirmation = jsonBoolOr(*json, L"requiresConfirmation", false);
        result.message = wideFromUtf8(jsonStringOr(*json, L"message", result.succeeded ? "Operation completed." : "Operation failed."));
    } else {
        result.message = result.succeeded ? L"Operation completed." : L"Operation failed.";
    }

    return result;
}

ShellOperationResult governanceOperationResultFromResponse(const HttpResponse& response) {
    ShellOperationResult result;
    result.succeeded = response.statusCode >= 200 && response.statusCode < 300;

    if (const auto json = parseJsonObject(response.body); json.has_value()) {
        result.succeeded = jsonBoolOr(*json, L"succeeded", result.succeeded);
        const auto summary = wideFromUtf8(jsonStringOr(*json, L"summary", ""));
        result.message = !summary.empty()
            ? summary
            : wideFromUtf8(jsonStringOr(*json, L"message", result.succeeded ? "Governance execution completed." : "Governance execution failed."));
    } else {
        result.message = result.succeeded ? L"Governance execution completed." : L"Governance execution failed.";
    }

    return result;
}

std::wstring boolLabel(const bool value) {
    return value ? L"yes" : L"no";
}

std::wstring formatPercentRow(const wchar_t* label, const double value) {
    std::wostringstream stream;
    stream.setf(std::ios::fixed);
    stream.precision(1);
    stream << label << L": " << value << L"%";
    return stream.str();
}

void appendJsonArrayRows(const JsonArray& array,
                         const std::function<std::wstring(const JsonObject&)>& formatter,
                         std::vector<std::wstring>& rows) {
    for (const auto& value : array) {
        if (value.ValueType() != JsonValueType::Object) {
            continue;
        }
        rows.push_back(formatter(value.GetObject()));
    }
}

void appendStringArrayRows(const JsonArray& array, std::vector<std::wstring>& rows) {
    for (const auto& value : array) {
        if (value.ValueType() == JsonValueType::String) {
            rows.push_back(std::wstring(value.GetString().c_str()));
        }
    }
}

std::wstring endpointRow(const JsonObject& object) {
    std::wostringstream stream;
    stream << wideFromUtf8(jsonStringOr(object, L"displayName", "endpoint"))
           << L"  |  "
           << wideFromUtf8(jsonStringOr(object, L"host", "127.0.0.1"))
           << L':'
           << static_cast<int>(jsonNumberOr(object, L"port", 0))
           << L"  |  "
           << wideFromUtf8(jsonStringOr(object, L"status", "unknown"));
    return stream.str();
}

std::wstring runtimeEndpointRow(const ShellRuntimeEndpoint& endpoint) {
    std::wostringstream stream;
    stream << (endpoint.displayName.empty() ? endpoint.id : endpoint.displayName)
           << L"  |  "
           << (endpoint.specialization.empty() ? endpoint.kind : endpoint.specialization)
           << L"  |  ";
    if (!endpoint.host.empty()) {
        stream << endpoint.host;
        if (endpoint.port != 0) {
            stream << L':' << endpoint.port;
        }
    } else {
        stream << L"logical lane";
    }
    stream << L"  |  " << (endpoint.status.empty() ? L"unknown" : endpoint.status);
    if (endpoint.userDefined) {
        stream << L"  |  custom";
    }
    return stream.str();
}

std::wstring providerRow(const JsonObject& object) {
    std::wostringstream stream;
    stream << wideFromUtf8(jsonStringOr(object, L"displayName", "provider"))
           << L"  |  "
           << wideFromUtf8(jsonStringOr(object, L"baseUrl", ""))
           << L"  |  model="
           << wideFromUtf8(jsonStringOr(object, L"modelId", "default"))
           << L"  |  autonomous="
           << boolLabel(jsonBoolOr(object, L"allowAutonomousControl"))
           << L"  |  credentials="
           << boolLabel(jsonBoolOr(object, L"credentialsConfigured"));
    return stream.str();
}

std::wstring providerConnectionRow(const ShellProviderConnection& provider) {
    std::wostringstream stream;
    stream << (provider.displayName.empty() ? provider.id : provider.displayName)
           << L"  |  "
           << provider.baseUrl
           << L"  |  model="
           << (provider.modelId.empty() ? L"default" : provider.modelId)
           << L"  |  autonomous="
           << boolLabel(provider.allowAutonomousControl)
           << L"  |  credentials="
           << boolLabel(provider.credentialsConfigured);
    return stream.str();
}

std::wstring providerCapabilityRow(const ShellProviderCapability& capability) {
    std::wostringstream stream;
    stream << capability.displayName
           << L"  |  targets="
           << (capability.supportedTargets.empty() ? L"none" : std::to_wstring(capability.supportedTargets.size()))
           << L"  |  model="
           << (capability.recommendedModel.empty() ? L"optional" : capability.recommendedModel);
    return stream.str();
}

std::wstring providerAssignmentRow(const ShellProviderAssignment& assignment,
                                   const std::vector<ShellProviderAssignmentTarget>& targets,
                                   const std::vector<ShellProviderConnection>& providers) {
    const auto targetIterator = std::find_if(
        targets.begin(),
        targets.end(),
        [&assignment](const ShellProviderAssignmentTarget& target) { return target.targetId == assignment.targetId; });
    const auto providerIterator = std::find_if(
        providers.begin(),
        providers.end(),
        [&assignment](const ShellProviderConnection& provider) { return provider.id == assignment.providerId; });

    std::wostringstream stream;
    stream << (targetIterator == targets.end() ? assignment.targetId : targetIterator->displayName)
           << L"  |  "
           << (providerIterator == providers.end() ? assignment.providerId : providerIterator->displayName)
           << L"  |  "
           << assignment.updatedAtUtc;
    return stream.str();
}

std::wstring providerExecutionRegistrationRow(const ShellProviderExecutionRegistration& registration) {
    std::wostringstream stream;
    stream << (registration.displayName.empty() ? registration.providerId : registration.displayName)
           << L"  |  "
           << registration.transport
           << L"  |  shared MCP "
           << boolLabel(registration.supportsSharedMcpAccess);
    if (registration.supportsDirectMcpConfig) {
        stream << L"  |  direct config";
    }
    return stream.str();
}

std::wstring providerExecutionHistoryRow(const ShellProviderExecutionRecord& record) {
    std::wostringstream stream;
    stream << L"[" << record.status << L"] "
           << (record.targetDisplayName.empty() ? record.targetId : record.targetDisplayName)
           << L"  |  "
           << (record.providerDisplayName.empty() ? record.providerId : record.providerDisplayName);
    if (!record.completedAtUtc.empty()) {
        stream << L"  |  " << record.completedAtUtc;
    }
    return stream.str();
}

std::wstring installRow(const JsonObject& object) {
    std::wostringstream stream;
    stream << wideFromUtf8(jsonStringOr(object, L"source", "import"))
           << L"  |  "
           << wideFromUtf8(jsonStringOr(object, L"installedAtUtc", ""))
           << L"  |  trusted="
           << boolLabel(jsonBoolOr(object, L"trusted"))
           << L"  |  "
           << wideFromUtf8(jsonStringOr(object, L"executionSummary", ""));
    return stream.str();
}

std::wstring exportRow(const JsonObject& object) {
    std::wostringstream stream;
    stream << wideFromUtf8(jsonStringOr(object, L"fileName", "artifact"))
           << L"  |  "
           << wideFromUtf8(jsonStringOr(object, L"mediaType", ""));
    return stream.str();
}

std::wstring governanceFindingRow(const JsonObject& object) {
    std::wostringstream stream;
    stream << L"["
           << wideFromUtf8(jsonStringOr(object, L"status", "pass"))
           << L"] "
           << wideFromUtf8(jsonStringOr(object, L"title", "Governance finding"))
           << L"  |  "
           << wideFromUtf8(jsonStringOr(object, L"message", ""));
    return stream.str();
}

std::wstring governanceRoleRow(const JsonObject& object) {
    std::wostringstream stream;
    stream << wideFromUtf8(jsonStringOr(object, L"name", "Role"))
           << L"  |  "
           << wideFromUtf8(jsonStringOr(object, L"authorityLevel", "authority"));
    return stream.str();
}

std::wstring governanceRuleRow(const JsonObject& object) {
    std::wostringstream stream;
    stream << wideFromUtf8(jsonStringOr(object, L"ruleId", "CLU"))
           << L"  |  "
           << wideFromUtf8(jsonStringOr(object, L"title", "Governance rule"))
           << L"  |  severity="
           << wideFromUtf8(jsonStringOr(object, L"severity", "medium"));
    return stream.str();
}

std::wstring governanceDocumentRow(const JsonObject& object) {
    std::wostringstream stream;
    stream << wideFromUtf8(jsonStringOr(object, L"title", "Governance document"))
           << L"  |  "
           << wideFromUtf8(jsonStringOr(object, L"category", "policy"))
           << L"  |  "
           << wideFromUtf8(jsonStringOr(object, L"summary", ""));
    return stream.str();
}

std::wstring joinJsonStringArray(const JsonArray& array, const wchar_t* separator = L", ") {
    std::wstring combined;
    for (const auto& value : array) {
        if (value.ValueType() != JsonValueType::String) {
            continue;
        }

        if (!combined.empty()) {
            combined += separator;
        }
        combined += std::wstring(value.GetString().c_str());
    }
    return combined;
}

std::wstring appleRemoteHostRow(const JsonObject& object) {
    const auto toolchain = object.GetNamedObject(L"toolchain", JsonObject());
    const auto signing = object.GetNamedObject(L"signing", JsonObject());
    const auto platforms = joinJsonStringArray(object.GetNamedArray(L"platforms", JsonArray()));
    const auto transport = wideFromUtf8(jsonStringOr(object, L"transport", "unknown"));
    const auto displayName = wideFromUtf8(jsonStringOr(object, L"displayName", jsonStringOr(object, L"hostId", "Apple host")));
    const auto address = wideFromUtf8(jsonStringOr(object, L"address", jsonStringOr(object, L"serviceBaseUrl", "unconfigured")));
    const auto port = static_cast<int>(jsonNumberOr(object, L"port", 0));

    std::wostringstream stream;
    stream << displayName
           << L"  |  "
           << (platforms.empty() ? L"no platform targets" : platforms)
           << L"  |  "
           << transport
           << L"  |  "
           << (address.empty() ? L"unconfigured" : address);
    if (port > 0) {
        stream << L':' << port;
    }
    stream << L"  |  toolchain="
           << wideFromUtf8(jsonStringOr(toolchain, L"status", "unknown"))
           << L"  |  signing="
           << wideFromUtf8(jsonStringOr(signing, L"status", "unknown"));

    if (const auto version = wideFromUtf8(jsonStringOr(toolchain, L"xcodeVersion", "")); !version.empty()) {
        stream << L"  |  Xcode " << version;
    }
    if (const auto transportSummary = wideFromUtf8(jsonStringOr(object, L"transportSummary", "")); !transportSummary.empty()) {
        stream << L"  |  " << transportSummary;
    }
    const auto readinessIssues = jsonStringArrayOr(object, L"readinessIssues");
    if (!readinessIssues.empty()) {
        stream << L"  |  issue: " << readinessIssues.front();
    }
    return stream.str();
}

std::wstring appleOperationRow(const JsonObject& object) {
    std::wostringstream stream;
    stream << L"["
           << wideFromUtf8(jsonStringOr(object, L"status", "queued"))
           << L"] "
           << wideFromUtf8(jsonStringOr(object, L"displayName", jsonStringOr(object, L"toolId", "Apple operation")))
           << L"  |  "
           << wideFromUtf8(jsonStringOr(object, L"platform", "unknown"))
           << L"  |  "
           << wideFromUtf8(jsonStringOr(object, L"hostDisplayName", jsonStringOr(object, L"hostId", "unassigned host")))
           << L"  |  "
           << wideFromUtf8(jsonStringOr(object, L"transport", "unknown"));

    if (const auto artifact = wideFromUtf8(jsonStringOr(object, L"artifactPath", "")); !artifact.empty()) {
        stream << L"  |  artifact=" << artifact;
    } else if (const auto summary = wideFromUtf8(jsonStringOr(object, L"summary", "")); !summary.empty()) {
        stream << L"  |  " << summary;
    }
    if (const auto routeReason = wideFromUtf8(jsonStringOr(object, L"routeReason", "")); !routeReason.empty()) {
        stream << L"  |  " << routeReason;
    }
    const auto redactedKeys = jsonStringArrayOr(object, L"redactedRequestOptionKeys");
    if (!redactedKeys.empty()) {
        stream << L"  |  redacted credentials";
    }

    if (const auto completedAt = wideFromUtf8(jsonStringOr(object, L"completedAtUtc", "")); !completedAt.empty()) {
        stream << L"  |  " << completedAt;
    }
    return stream.str();
}

std::wstring platformGatewayRow(const JsonObject& object) {
    std::wostringstream stream;
    stream << wideFromUtf8(jsonStringOr(object, L"displayName", jsonStringOr(object, L"serviceId", "gateway")))
           << L"  |  "
           << wideFromUtf8(jsonStringOr(object, L"platform", "unknown"))
           << L"  |  "
           << wideFromUtf8(jsonStringOr(object, L"serviceType", "_service._tcp.local"))
           << L"  |  "
           << wideFromUtf8(jsonStringOr(object, L"ipAddress", jsonStringOr(object, L"hostName", "unpublished")));

    if (const auto port = static_cast<int>(jsonNumberOr(object, L"port", 0)); port > 0) {
        stream << L':' << port;
    }

    stream << L"  |  "
           << wideFromUtf8(jsonStringOr(object, L"status", "unknown"));
    return stream.str();
}

std::wstring governanceServerRow(const JsonObject& object) {
    std::wostringstream stream;
    stream << wideFromUtf8(jsonStringOr(object, L"displayName", jsonStringOr(object, L"serviceId", "governance")))
           << L"  |  "
           << wideFromUtf8(jsonStringOr(object, L"platform", "unknown"))
           << L"  |  route="
           << wideFromUtf8(jsonStringOr(object, L"routePath", "/mcp/governance"))
           << L"  |  tools="
           << object.GetNamedArray(L"toolIds", JsonArray()).Size()
           << L"  |  "
           << wideFromUtf8(jsonStringOr(object, L"status", "unknown"));

    if (jsonBoolOr(object, L"requiresRemoteToolchain")) {
        stream << L"  |  remote toolchain";
    }
    return stream.str();
}

std::wstring governanceExecutionRow(const JsonObject& object) {
    std::wostringstream stream;
    stream << L"["
           << wideFromUtf8(jsonStringOr(object, L"status", "failed"))
           << L"] "
           << wideFromUtf8(jsonStringOr(object, L"displayName", jsonStringOr(object, L"toolId", "governance tool")))
           << L"  |  "
           << wideFromUtf8(jsonStringOr(object, L"platform", "unknown"));

    if (const auto summary = wideFromUtf8(jsonStringOr(object, L"summary", "")); !summary.empty()) {
        stream << L"  |  " << summary;
    }

    if (const auto completedAt = wideFromUtf8(jsonStringOr(object, L"completedAtUtc", "")); !completedAt.empty()) {
        stream << L"  |  " << completedAt;
    }
    return stream.str();
}

ShellForsettiModule forsettiModuleFromJson(const JsonObject& object) {
    ShellForsettiModule module;
    module.moduleId = wideFromUtf8(jsonStringOr(object, L"moduleId", ""));
    module.displayName = wideFromUtf8(jsonStringOr(object, L"displayName", ""));
    module.moduleType = wideFromUtf8(jsonStringOr(object, L"moduleType", "service"));
    module.version = wideFromUtf8(jsonStringOr(object, L"version", "0.0.0"));
    module.entryPoint = wideFromUtf8(jsonStringOr(object, L"entryPoint", ""));
    module.supportedPlatforms = jsonStringArrayOr(object, L"supportedPlatforms");
    module.capabilitiesRequested = jsonStringArrayOr(object, L"capabilitiesRequested");
    module.active = jsonBoolOr(object, L"active");
    module.unlocked = jsonBoolOr(object, L"unlocked");
    module.protectedModule = jsonBoolOr(object, L"protectedModule");
    module.recommendedAction = wideFromUtf8(jsonStringOr(object, L"recommendedAction", ""));
    module.statusSummary = wideFromUtf8(jsonStringOr(object, L"statusSummary", ""));
    return module;
}

ShellForsettiModuleCatalogResult fetchForsettiModulesFromAdminApi(const std::filesystem::path& configurationFile) {
    std::wstring errorMessage;
    const auto [host, port] = adminApiEndpoint(configurationFile);
    const auto response = httpGet(host, port, L"/api/forsetti/modules", errorMessage);
    if (!response.has_value()) {
        return ShellForsettiModuleCatalogResult{
            false,
            errorMessage.empty() ? L"Unable to load the Forsetti module catalog from the local admin API." : errorMessage,
            {}
        };
    }
    if (response->statusCode != 200) {
        return ShellForsettiModuleCatalogResult{
            false,
            L"Local admin API rejected the Forsetti module catalog request.",
            {}
        };
    }

    const auto json = parseJsonObject(response->body);
    if (!json.has_value()) {
        return ShellForsettiModuleCatalogResult{
            false,
            L"Local admin API returned invalid Forsetti module JSON.",
            {}
        };
    }

    ShellForsettiModuleCatalogResult result;
    result.succeeded = jsonBoolOr(*json, L"succeeded", true);
    result.message = wideFromUtf8(jsonStringOr(*json, L"message", "Forsetti module catalog loaded."));
    const auto modules = json->GetNamedArray(L"modules", JsonArray());
    for (const auto& value : modules) {
        if (value.ValueType() == JsonValueType::Object) {
            result.modules.push_back(forsettiModuleFromJson(value.GetObject()));
        }
    }
    if (result.message.empty()) {
        result.message = result.succeeded
            ? L"Forsetti module catalog loaded."
            : L"Forsetti module catalog is unavailable.";
    }
    return result;
}

JsonObject forsettiModuleActionToJson(const std::wstring& moduleId, const std::wstring& action) {
    JsonObject payload;
    payload.SetNamedValue(L"moduleId", JsonValue::CreateStringValue(moduleId));
    payload.SetNamedValue(L"action", JsonValue::CreateStringValue(action));
    return payload;
}

void openPathInExplorer(const std::filesystem::path& path) {
    if (std::filesystem::is_regular_file(path)) {
        std::wstring arguments = L"/select,\"" + path.wstring() + L"\"";
        ShellExecuteW(nullptr, L"open", L"explorer.exe", arguments.c_str(), nullptr, SW_SHOWNORMAL);
        return;
    }

    std::filesystem::create_directories(path);
    ShellExecuteW(nullptr, L"open", path.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

std::optional<JsonObject> fetchConfigurationFromAdminApi(const std::filesystem::path& configurationFile,
                                                         std::wstring& errorMessage) {
    const auto [host, port] = adminApiEndpoint(configurationFile);
    const auto response = httpGet(host, port, L"/api/config", errorMessage);
    if (!response.has_value()) {
        return std::nullopt;
    }
    if (response->statusCode != 200) {
        errorMessage = L"Local admin API rejected the configuration request.";
        return std::nullopt;
    }
    const auto json = parseJsonObject(response->body);
    if (!json.has_value()) {
        errorMessage = L"Local admin API returned invalid configuration JSON.";
        return std::nullopt;
    }
    return json;
}

ShellOperationResult postConfigurationToAdminApi(const std::filesystem::path& configurationFile,
                                                 const JsonObject& configuration,
                                                 const bool confirmUnsafeChanges) {
    std::wstring errorMessage;
    const auto [host, port] = adminApiEndpoint(configurationFile);
    std::vector<std::pair<std::wstring, std::wstring>> headers;
    if (confirmUnsafeChanges) {
        headers.emplace_back(L"X-Confirm-Unsafe", L"1");
    }

    const auto response = httpRequest(
        host,
        port,
        L"POST",
        L"/api/config",
        narrowFromWide(configuration.Stringify().c_str()),
        headers,
        errorMessage);
    if (!response.has_value()) {
        return ShellOperationResult{ false, false, errorMessage.empty() ? L"Unable to update configuration through the local admin API." : errorMessage };
    }

    return operationResultFromResponse(*response);
}

ShellOperationResult postProviderToAdminApi(const std::filesystem::path& configurationFile,
                                            const ShellProviderConnection& provider) {
    std::wstring errorMessage;
    const auto [host, port] = adminApiEndpoint(configurationFile);
    const auto response = httpRequest(
        host,
        port,
        L"POST",
        L"/api/providers",
        narrowFromWide(providerToJson(provider).Stringify().c_str()),
        {},
        errorMessage);
    if (!response.has_value()) {
        return ShellOperationResult{ false, false, errorMessage.empty() ? L"Unable to update provider settings through the local admin API." : errorMessage };
    }

    return operationResultFromResponse(*response);
}

ShellOperationResult postJsonObjectToAdminApi(const std::filesystem::path& configurationFile,
                                              const std::wstring& endpoint,
                                              const JsonObject& payload,
                                              const std::wstring& fallbackMessage) {
    std::wstring errorMessage;
    const auto [host, port] = adminApiEndpoint(configurationFile);
    const auto response = httpRequest(
        host,
        port,
        L"POST",
        endpoint,
        narrowFromWide(payload.Stringify().c_str()),
        {},
        errorMessage);
    if (!response.has_value()) {
        return ShellOperationResult{
            false,
            false,
            errorMessage.empty() ? fallbackMessage : errorMessage
        };
    }

    return operationResultFromResponse(*response);
}

ShellAutoConnectProviderResult postAutoConnectProviderToAdminApi(
    const std::filesystem::path& configurationFile,
    const ShellAutoConnectProviderRequest& request) {
    ShellAutoConnectProviderResult result;

    JsonObject payload;
    payload.SetNamedValue(L"kind", JsonValue::CreateStringValue(request.kind));
    if (!request.providerId.empty()) {
        payload.SetNamedValue(L"providerId", JsonValue::CreateStringValue(request.providerId));
    }
    payload.SetNamedValue(L"allowAutonomousControl",
                          JsonValue::CreateBooleanValue(request.allowAutonomousControl));
    payload.SetNamedValue(L"discoverModels", JsonValue::CreateBooleanValue(request.discoverModels));
    if (!request.displayNameOverride.empty()) {
        payload.SetNamedValue(L"displayNameOverride",
                              JsonValue::CreateStringValue(request.displayNameOverride));
    }
    if (!request.baseUrlOverride.empty()) {
        payload.SetNamedValue(L"baseUrlOverride", JsonValue::CreateStringValue(request.baseUrlOverride));
    }
    if (!request.modelIdOverride.empty()) {
        payload.SetNamedValue(L"modelIdOverride", JsonValue::CreateStringValue(request.modelIdOverride));
    }

    // credentials: object mapping fieldId -> value
    JsonObject credentialsObject;
    for (const auto& [fieldId, value] : request.credentials) {
        credentialsObject.SetNamedValue(fieldId, JsonValue::CreateStringValue(value));
    }
    payload.SetNamedValue(L"credentials", credentialsObject);

    // assignmentTargetIds: array of strings
    JsonArray targetsArray;
    for (const auto& targetId : request.assignmentTargetIds) {
        targetsArray.Append(JsonValue::CreateStringValue(targetId));
    }
    payload.SetNamedValue(L"assignmentTargetIds", targetsArray);

    std::wstring errorMessage;
    const auto [host, port] = adminApiEndpoint(configurationFile);
    const auto response = httpRequest(
        host,
        port,
        L"POST",
        L"/api/providers/auto-connect",
        narrowFromWide(payload.Stringify().c_str()),
        {},
        errorMessage);
    if (!response.has_value()) {
        result.errorMessage = errorMessage.empty()
            ? std::wstring(L"Unable to auto-connect the provider through the local admin API.")
            : errorMessage;
        result.summary = result.errorMessage;
        return result;
    }

    const auto json = parseJsonObject(response->body);
    if (!json.has_value()) {
        result.errorMessage = response->statusCode == 200
            ? std::wstring(L"Local admin API returned invalid auto-connect JSON.")
            : std::wstring(L"Local admin API rejected the auto-connect request.");
        result.summary = result.errorMessage;
        return result;
    }

    const auto& body = *json;
    result.succeeded = body.HasKey(L"succeeded") && body.GetNamedBoolean(L"succeeded", false);
    result.providerId = body.GetNamedString(L"providerId", L"");
    result.displayName = body.GetNamedString(L"displayName", L"");
    result.baseUrl = body.GetNamedString(L"baseUrl", L"");
    result.selectedModelId = body.GetNamedString(L"selectedModelId", L"");
    result.summary = body.GetNamedString(L"summary", L"");
    result.errorMessage = body.GetNamedString(L"errorMessage", L"");
    result.totalLatencyMs = static_cast<int>(body.GetNamedNumber(L"totalLatencyMs", 0));

    if (body.HasKey(L"steps")) {
        const auto stepsArray = body.GetNamedArray(L"steps", JsonArray());
        for (const auto& entry : stepsArray) {
            if (entry.ValueType() != JsonValueType::Object) continue;
            const auto obj = entry.GetObject();
            ShellAutoConnectProviderStep step;
            step.stage = obj.GetNamedString(L"stage", L"");
            step.succeeded = obj.HasKey(L"succeeded") && obj.GetNamedBoolean(L"succeeded", false);
            step.message = obj.GetNamedString(L"message", L"");
            step.latencyMs = static_cast<int>(obj.GetNamedNumber(L"latencyMs", 0));
            result.steps.push_back(std::move(step));
        }
    }

    if (body.HasKey(L"discoveredModels")) {
        const auto modelsArray = body.GetNamedArray(L"discoveredModels", JsonArray());
        for (const auto& entry : modelsArray) {
            if (entry.ValueType() != JsonValueType::Object) continue;
            const auto obj = entry.GetObject();
            ShellAutoConnectProviderModel model;
            model.id = obj.GetNamedString(L"id", L"");
            model.displayName = obj.GetNamedString(L"displayName", L"");
            model.description = obj.GetNamedString(L"description", L"");
            result.discoveredModels.push_back(std::move(model));
        }
    }

    if (body.HasKey(L"assignmentsApplied")) {
        const auto array = body.GetNamedArray(L"assignmentsApplied", JsonArray());
        for (const auto& entry : array) {
            if (entry.ValueType() == JsonValueType::String) {
                result.assignmentsApplied.push_back(entry.GetString().c_str());
            }
        }
    }
    if (body.HasKey(L"assignmentsFailed")) {
        const auto array = body.GetNamedArray(L"assignmentsFailed", JsonArray());
        for (const auto& entry : array) {
            if (entry.ValueType() == JsonValueType::String) {
                result.assignmentsFailed.push_back(entry.GetString().c_str());
            }
        }
    }

    if (response->statusCode != 200 && result.errorMessage.empty()) {
        result.errorMessage = L"Local admin API rejected the auto-connect request.";
        result.summary = result.errorMessage;
    }
    return result;
}

ShellProviderExecutionRecord postProviderExecutionToAdminApi(const std::filesystem::path& configurationFile,
                                                             const ShellProviderExecutionRequest& request) {
    ShellProviderExecutionRecord record;
    record.targetId = request.targetId;
    record.status = L"failed";

    JsonObject payload;
    payload.SetNamedValue(L"targetId", JsonValue::CreateStringValue(request.targetId));
    payload.SetNamedValue(L"prompt", JsonValue::CreateStringValue(request.prompt));
    payload.SetNamedValue(L"allowToolAccess", JsonValue::CreateBooleanValue(request.allowToolAccess));
    payload.SetNamedValue(L"maxTurns", JsonValue::CreateNumberValue(request.maxTurns));

    std::wstring errorMessage;
    const auto [host, port] = adminApiEndpoint(configurationFile);
    const auto response = httpRequest(
        host,
        port,
        L"POST",
        L"/api/providers/execute",
        narrowFromWide(payload.Stringify().c_str()),
        {},
        errorMessage);
    if (!response.has_value()) {
        record.errorMessage = errorMessage.empty()
            ? L"Unable to execute the provider task through the local admin API."
            : errorMessage;
        return record;
    }

    const auto json = parseJsonObject(response->body);
    if (!json.has_value()) {
        record.errorMessage = response->statusCode == 200
            ? L"Local admin API returned invalid execution JSON."
            : L"Local admin API rejected the provider execution request.";
        return record;
    }

    auto parsedRecord = providerExecutionRecordFromJson(*json);
    if (parsedRecord.status.empty()) {
        parsedRecord.status = response->statusCode == 200 ? L"succeeded" : L"failed";
    }
    if (response->statusCode != 200 && parsedRecord.errorMessage.empty()) {
        parsedRecord.errorMessage = L"Local admin API rejected the provider execution request.";
    }
    return parsedRecord;
}

ShellOperationResult postGovernanceToolToAdminApi(const std::filesystem::path& configurationFile,
                                                  const std::wstring& platform,
                                                  const std::wstring& toolId,
                                                  const std::wstring& targetPath,
                                                  const std::map<std::wstring, std::wstring>& options) {
    std::wstring errorMessage;
    const auto [host, port] = adminApiEndpoint(configurationFile);
    const auto payload = governanceToolRequestToJson(platform, toolId, targetPath, options);
    const auto response = httpRequest(
        host,
        port,
        L"POST",
        L"/api/clu/execute",
        narrowFromWide(payload.Stringify().c_str()),
        {},
        errorMessage);
    if (!response.has_value()) {
        return ShellOperationResult{
            false,
            false,
            errorMessage.empty() ? L"Unable to execute the governance tool through the local admin API." : errorMessage
        };
    }

    return governanceOperationResultFromResponse(*response);
}

ShellExportFetchResult fetchExportsFromAdminApi(const std::filesystem::path& configurationFile) {
    std::wstring errorMessage;
    const auto [host, port] = adminApiEndpoint(configurationFile);
    const auto response = httpGet(host, port, L"/api/exports", errorMessage);
    if (!response.has_value()) {
        return ShellExportFetchResult{
            false,
            errorMessage.empty() ? L"Unable to load export artifacts from the local admin API." : errorMessage,
            {}
        };
    }
    if (response->statusCode != 200) {
        return ShellExportFetchResult{
            false,
            L"Local admin API rejected the export request.",
            {}
        };
    }

    const auto exportArray = parseJsonArray(response->body);
    if (!exportArray.has_value()) {
        return ShellExportFetchResult{
            false,
            L"Local admin API returned invalid export JSON.",
            {}
        };
    }

    std::vector<ShellExportArtifact> artifacts;
    for (const auto& value : *exportArray) {
        if (value.ValueType() == JsonValueType::Object) {
            artifacts.push_back(exportArtifactFromJson(value.GetObject()));
        }
    }

    std::wostringstream message;
    message << L"Loaded " << artifacts.size() << L" export artifacts from the local admin API.";
    return ShellExportFetchResult{ true, message.str(), std::move(artifacts) };
}

std::wstring safeFolderComponent(std::wstring value) {
    for (auto& character : value) {
        if (character == L'\\' || character == L'/' || character == L':' ||
            character == L'*' || character == L'?' || character == L'"' ||
            character == L'<' || character == L'>' || character == L'|') {
            character = L'_';
        }
    }
    return value;
}

std::wstring exportTimestampFolderName() {
    const auto now = std::chrono::system_clock::now();
    const auto nowTime = std::chrono::system_clock::to_time_t(now);
    tm localTime{};
    localtime_s(&localTime, &nowTime);

    std::wostringstream stream;
    stream << L"export-" << std::put_time(&localTime, L"%Y%m%d-%H%M%S");
    return stream.str();
}

bool writeUtf8File(const std::filesystem::path& filePath, const std::wstring& content) {
    std::filesystem::create_directories(filePath.parent_path());
    std::ofstream stream(filePath, std::ios::binary | std::ios::trunc);
    if (!stream.is_open()) {
        return false;
    }

    const auto utf8 = narrowFromWide(content);
    stream.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
    return stream.good();
}

} // namespace

ShellSnapshot ShellRuntime::CaptureSnapshot() const {
    ShellSnapshot snapshot;

    const auto configurationFile = ResolveConfigurationFile();
    const auto dataDirectory = ResolveDataDirectory();
    snapshot.configPath = configurationFile.wstring();
    snapshot.dataDirectory = dataDirectory.wstring();

    const auto service = queryServiceSnapshot();
    snapshot.serviceState = service.state;
    snapshot.serviceProcessId = service.processId;
    snapshot.canStartService = service.state == ServiceState::Missing || service.state == ServiceState::Stopped || service.state == ServiceState::Unknown;
    snapshot.canStopService = service.state == ServiceState::Running || service.state == ServiceState::StartPending;

    std::string instanceName = "Master Control Orchestration Server";
    std::string bindAddress = "0.0.0.0";
    uint16_t browserPort = 7300;
    uint16_t beaconPort = 7301;
    std::string environmentName = "Pending service snapshot";
    std::string preferredBindAddress = "127.0.0.1";
    std::string macAddress = "n/a";
    bool beaconEnabled = true;
    bool aiAutonomyEnabled = false;
    bool advancedMode = false;          // WS5 parity with browser
    bool firstRunCompleted = false;     // WS1 parity with browser
    bool securityProtocolsEnabled = true;
    bool openLanAccess = true;
    ShellSecuritySettings securitySettings;
    std::vector<ShellRuntimeEndpoint> endpoints;
    std::vector<ShellProviderConnection> providers;
    std::vector<ShellProviderCapability> providerCapabilities;
    std::vector<ShellProviderCredentialStatus> providerCredentialStatuses;
    std::vector<ShellSubAgentGroupDefinition> subAgentGroups;
    std::vector<ShellProviderAssignmentTarget> providerAssignmentTargets;
    std::vector<ShellProviderAssignment> providerAssignments;
    std::vector<ShellProviderExecutionRegistration> providerExecutionRegistrations;
    std::vector<ShellProviderExecutionRecord> providerExecutionHistory;
    std::vector<ShellAppleRemoteHost> appleRemoteHosts;
    std::vector<ShellAppleOperationRecord> appleOperations;
    int cpuPercent = 50;
    int memoryPercent = 50;
    int bandwidthPercent = 50;
    int storagePercent = 50;

    if (const auto configurationText = readFileUtf8(configurationFile); configurationText.has_value()) {
        if (const auto configuration = parseJsonObject(*configurationText); configuration.has_value()) {
            instanceName = jsonStringOr(*configuration, L"instanceName", instanceName);
            bindAddress = jsonStringOr(*configuration, L"bindAddress", bindAddress);
            browserPort = static_cast<uint16_t>(jsonNumberOr(*configuration, L"browserPort", browserPort));
            beaconPort = static_cast<uint16_t>(jsonNumberOr(*configuration, L"beaconPort", beaconPort));

            if (configuration->HasKey(L"activeProfile")) {
                const auto profile = configuration->GetNamedObject(L"activeProfile", JsonObject());
                environmentName = jsonStringOr(profile, L"environmentName", environmentName);
                preferredBindAddress = jsonStringOr(profile, L"preferredBindAddress", preferredBindAddress);
                macAddress = jsonStringOr(profile, L"macAddress", macAddress);
                for (const auto& value : profile.GetNamedArray(L"seededEndpoints", JsonArray())) {
                    if (value.ValueType() == JsonValueType::Object) {
                        endpoints.push_back(runtimeEndpointFromJson(value.GetObject()));
                    }
                }
            }

            if (configuration->HasKey(L"security")) {
                const auto security = configuration->GetNamedObject(L"security", JsonObject());
                securitySettings = securityFromJson(security);
                securityProtocolsEnabled = jsonBoolOr(security, L"securityProtocolsEnabled", securityProtocolsEnabled);
                openLanAccess = jsonBoolOr(security, L"allowOpenLanAccess", openLanAccess);
            }

            if (configuration->HasKey(L"resourceAllocation")) {
                const auto resourceAllocation = configuration->GetNamedObject(L"resourceAllocation", JsonObject());
                cpuPercent = static_cast<int>(jsonNumberOr(resourceAllocation, L"cpuPercent", cpuPercent));
                memoryPercent = static_cast<int>(jsonNumberOr(resourceAllocation, L"memoryPercent", memoryPercent));
                bandwidthPercent = static_cast<int>(jsonNumberOr(resourceAllocation, L"bandwidthPercent", bandwidthPercent));
                storagePercent = static_cast<int>(jsonNumberOr(resourceAllocation, L"storagePercent", storagePercent));
            }

            beaconEnabled = jsonBoolOr(*configuration, L"beaconEnabled", beaconEnabled);
            aiAutonomyEnabled = jsonBoolOr(*configuration, L"aiAutonomyEnabled", aiAutonomyEnabled);
            advancedMode = jsonBoolOr(*configuration, L"advancedMode", advancedMode);
            firstRunCompleted = jsonBoolOr(*configuration, L"firstRunCompleted", firstRunCompleted);

            if (configuration->HasKey(L"providers")) {
                for (const auto& value : configuration->GetNamedArray(L"providers", JsonArray())) {
                    if (value.ValueType() == JsonValueType::Object) {
                        providers.push_back(providerFromJson(value.GetObject()));
                    }
                }
            }

            if (configuration->HasKey(L"subAgentGroups")) {
                for (const auto& value : configuration->GetNamedArray(L"subAgentGroups", JsonArray())) {
                    if (value.ValueType() == JsonValueType::Object) {
                        subAgentGroups.push_back(subAgentGroupFromJson(value.GetObject()));
                    }
                }
            }
        }
    }

    snapshot.dashboardUrl = wideFromUtf8("http://" + dashboardHostFromBindAddress(bindAddress) + ":" + std::to_string(browserPort) + "/");

    std::wstring hostName = wideFromUtf8(environmentName);
    std::wstring operatingSystem = L"Service offline";
    std::wstring ipAddress = wideFromUtf8(preferredBindAddress);
    std::wstring telemetryText = L"Live telemetry will appear when the service and local admin API are reachable.";
    std::vector<ShellNavigationPointer> navigationPointers;
    std::vector<ShellToolbarItem> toolbarItems;
    std::vector<ShellOverlayRoute> overlayRoutes;
    std::map<std::wstring, std::vector<ShellViewInjection>> viewInjectionsBySlot;
    std::vector<std::wstring> endpointRows;
    std::vector<std::wstring> providerRows;
    std::vector<std::wstring> providerCapabilityRows;
    std::vector<std::wstring> providerAssignmentRows;
    std::vector<std::wstring> providerExecutionRegistrationRows;
    std::vector<std::wstring> providerExecutionHistoryRows;
    std::vector<std::wstring> installRows;
    std::vector<std::wstring> exportRows;
    std::wstring governancePosture = L"Pending";
    std::wstring governanceDoctrine = L"CLU governance posture will appear when the local admin API is reachable.";
    std::wstring governanceLastEvaluatedUtc = L"Pending";
    std::vector<std::wstring> governanceFindingRows;
    std::vector<std::wstring> governanceRoleRows;
    std::vector<std::wstring> governanceRuleRows;
    std::vector<std::wstring> governanceDocumentRows;
    std::vector<std::wstring> governanceActionRows;
    std::vector<std::wstring> appleRemoteHostRows;
    std::vector<std::wstring> appleOperationRows;
    std::vector<std::wstring> platformGatewayRows;
    std::vector<std::wstring> governanceServerRows;
    std::vector<std::wstring> governanceExecutionRows;

    // Probe the admin API directly regardless of the Windows SCM service
    // state. The old code only tried to talk to the API when the service
    // was "Running" or "StartPending", which meant running in console
    // mode (e.g. developer workflow, non-admin install, uninstalled test
    // machine) left the shell permanently showing "API OFFLINE" and an
    // empty provider capability list even though the service host was
    // listening on 127.0.0.1:7300. If the direct probe succeeds we trust
    // it over the SCM report — the API's response is authoritative for
    // whether the shell can actually work.
    {
        std::wstring errorMessage;
        const auto health = httpGet(dashboardHostFromBindAddress(bindAddress), browserPort, L"/api/health", errorMessage);
        snapshot.apiHealthy = health.has_value() && health->statusCode == 200;

        const auto dashboard = httpGet(dashboardHostFromBindAddress(bindAddress), browserPort, L"/api/dashboard", errorMessage);
        if (dashboard.has_value() && dashboard->statusCode == 200) {
            if (const auto dashboardJson = parseJsonObject(dashboard->body); dashboardJson.has_value()) {
                if (dashboardJson->HasKey(L"telemetry")) {
                    const auto telemetry = dashboardJson->GetNamedObject(L"telemetry", JsonObject());
                    hostName = wideFromUtf8(jsonStringOr(telemetry, L"hostName", narrowFromWide(hostName)));
                    operatingSystem = wideFromUtf8(jsonStringOr(telemetry, L"operatingSystem", narrowFromWide(operatingSystem)));
                    ipAddress = wideFromUtf8(jsonStringOr(telemetry, L"primaryIpAddress", narrowFromWide(ipAddress)));
                    snapshot.cpuPercent = jsonNumberOr(telemetry, L"cpuPercent");
                    snapshot.memoryPercent = jsonNumberOr(telemetry, L"memoryPercent");
                    snapshot.diskPercent = jsonNumberOr(telemetry, L"diskPercent");
                    snapshot.bytesSentPerSecond = static_cast<uint64_t>(jsonNumberOr(telemetry, L"bytesSentPerSecond"));
                    snapshot.bytesReceivedPerSecond = static_cast<uint64_t>(jsonNumberOr(telemetry, L"bytesReceivedPerSecond"));
                    snapshot.telemetryCapturedAtUtc = wideFromUtf8(jsonStringOr(telemetry, L"capturedAtUtc", ""));
                    if (const auto telemetryMac = jsonStringOr(telemetry, L"primaryMacAddress", ""); !telemetryMac.empty()) {
                        macAddress = telemetryMac;
                    }

                    std::wostringstream telemetryStream;
                    telemetryStream << formatPercentRow(L"CPU", jsonNumberOr(telemetry, L"cpuPercent")) << L'\n'
                                    << formatPercentRow(L"Memory", jsonNumberOr(telemetry, L"memoryPercent")) << L'\n'
                                    << formatPercentRow(L"Disk", jsonNumberOr(telemetry, L"diskPercent")) << L'\n'
                                    << L"Traffic: tx " << static_cast<uint64_t>(jsonNumberOr(telemetry, L"bytesSentPerSecond"))
                                    << L" B/s | rx " << static_cast<uint64_t>(jsonNumberOr(telemetry, L"bytesReceivedPerSecond")) << L" B/s\n"
                                    << L"Captured: " << wideFromUtf8(jsonStringOr(telemetry, L"capturedAtUtc", ""));
                    telemetryText = telemetryStream.str();
                }

                appendJsonArrayRows(
                    dashboardJson->GetNamedArray(L"endpoints", JsonArray()),
                    endpointRow,
                    endpointRows);
                endpoints.clear();
                for (const auto& value : dashboardJson->GetNamedArray(L"endpoints", JsonArray())) {
                    if (value.ValueType() == JsonValueType::Object) {
                        endpoints.push_back(runtimeEndpointFromJson(value.GetObject()));
                    }
                }
                appendJsonArrayRows(
                    dashboardJson->GetNamedArray(L"providers", JsonArray()),
                    providerRow,
                    providerRows);
                std::vector<ShellProviderConnection> liveProviders;
                for (const auto& value : dashboardJson->GetNamedArray(L"providers", JsonArray())) {
                    if (value.ValueType() == JsonValueType::Object) {
                        liveProviders.push_back(providerFromJson(value.GetObject()));
                    }
                }
                providers = mergeAuthoritativeSnapshotCollection(
                    providers,
                    std::move(liveProviders),
                    dashboardJson->HasKey(L"providers"),
                    [](const ShellProviderConnection& provider) { return provider.id; });
                for (const auto& value : dashboardJson->GetNamedArray(L"providerCapabilities", JsonArray())) {
                    if (value.ValueType() == JsonValueType::Object) {
                        providerCapabilities.push_back(providerCapabilityFromJson(value.GetObject()));
                    }
                }
                for (const auto& value : dashboardJson->GetNamedArray(L"providerCredentialStatuses", JsonArray())) {
                    if (value.ValueType() == JsonValueType::Object) {
                        providerCredentialStatuses.push_back(providerCredentialStatusFromJson(value.GetObject()));
                    }
                }
                std::vector<ShellSubAgentGroupDefinition> liveSubAgentGroups;
                for (const auto& value : dashboardJson->GetNamedArray(L"subAgentGroups", JsonArray())) {
                    if (value.ValueType() == JsonValueType::Object) {
                        liveSubAgentGroups.push_back(subAgentGroupFromJson(value.GetObject()));
                    }
                }
                subAgentGroups = mergeAuthoritativeSnapshotCollection(
                    subAgentGroups,
                    std::move(liveSubAgentGroups),
                    dashboardJson->HasKey(L"subAgentGroups"),
                    [](const ShellSubAgentGroupDefinition& group) { return group.groupId; });
                for (const auto& value : dashboardJson->GetNamedArray(L"providerAssignmentTargets", JsonArray())) {
                    if (value.ValueType() == JsonValueType::Object) {
                        providerAssignmentTargets.push_back(providerAssignmentTargetFromJson(value.GetObject()));
                    }
                }
                for (const auto& value : dashboardJson->GetNamedArray(L"providerAssignments", JsonArray())) {
                    if (value.ValueType() == JsonValueType::Object) {
                        providerAssignments.push_back(providerAssignmentFromJson(value.GetObject()));
                    }
                }
                for (const auto& value : dashboardJson->GetNamedArray(L"providerExecutionRegistrations", JsonArray())) {
                    if (value.ValueType() == JsonValueType::Object) {
                        providerExecutionRegistrations.push_back(providerExecutionRegistrationFromJson(value.GetObject()));
                    }
                }
                for (const auto& value : dashboardJson->GetNamedArray(L"providerExecutionHistory", JsonArray())) {
                    if (value.ValueType() == JsonValueType::Object) {
                        providerExecutionHistory.push_back(providerExecutionRecordFromJson(value.GetObject()));
                    }
                }
                appendJsonArrayRows(
                    dashboardJson->GetNamedArray(L"installHistory", JsonArray()),
                    installRow,
                    installRows);
                appendJsonArrayRows(
                    dashboardJson->GetNamedArray(L"exports", JsonArray()),
                    exportRow,
                    exportRows);
                if (dashboardJson->HasKey(L"governance")) {
                    const auto governance = dashboardJson->GetNamedObject(L"governance", JsonObject());
                    governancePosture = wideFromUtf8(jsonStringOr(governance, L"posture", "pass"));
                    governanceDoctrine = wideFromUtf8(jsonStringOr(governance, L"doctrine", narrowFromWide(governanceDoctrine)));
                    governanceLastEvaluatedUtc = wideFromUtf8(jsonStringOr(governance, L"lastEvaluatedUtc", "Pending"));

                    appendJsonArrayRows(
                        governance.GetNamedArray(L"findings", JsonArray()),
                        governanceFindingRow,
                        governanceFindingRows);
                    appendJsonArrayRows(
                        governance.GetNamedArray(L"roles", JsonArray()),
                        governanceRoleRow,
                        governanceRoleRows);
                    appendJsonArrayRows(
                        governance.GetNamedArray(L"rules", JsonArray()),
                        governanceRuleRow,
                        governanceRuleRows);
                    appendJsonArrayRows(
                        governance.GetNamedArray(L"documents", JsonArray()),
                        governanceDocumentRow,
                        governanceDocumentRows);
                    appendStringArrayRows(
                        governance.GetNamedArray(L"recommendedActions", JsonArray()),
                        governanceActionRows);
                    appendJsonArrayRows(
                        governance.GetNamedArray(L"appleRemoteHosts", JsonArray()),
                        appleRemoteHostRow,
                        appleRemoteHostRows);
                    for (const auto& value : governance.GetNamedArray(L"appleRemoteHosts", JsonArray())) {
                        if (value.ValueType() == JsonValueType::Object) {
                            appleRemoteHosts.push_back(appleRemoteHostFromJson(value.GetObject()));
                        }
                    }
                    appendJsonArrayRows(
                        governance.GetNamedArray(L"appleOperations", JsonArray()),
                        appleOperationRow,
                        appleOperationRows);
                    for (const auto& value : governance.GetNamedArray(L"appleOperations", JsonArray())) {
                        if (value.ValueType() == JsonValueType::Object) {
                            appleOperations.push_back(appleOperationFromJson(value.GetObject()));
                        }
                    }
                    appendJsonArrayRows(
                        governance.GetNamedArray(L"platformGateways", JsonArray()),
                        platformGatewayRow,
                        platformGatewayRows);
                    appendJsonArrayRows(
                        governance.GetNamedArray(L"governanceServers", JsonArray()),
                        governanceServerRow,
                        governanceServerRows);
                    appendJsonArrayRows(
                        governance.GetNamedArray(L"recentExecutions", JsonArray()),
                        governanceExecutionRow,
                        governanceExecutionRows);
                }

                if (dashboardJson->HasKey(L"surface")) {
                    const auto surface = dashboardJson->GetNamedObject(L"surface", JsonObject());
                    for (const auto& value : surface.GetNamedArray(L"toolbarItems", JsonArray())) {
                        if (value.ValueType() == JsonValueType::Object) {
                            toolbarItems.push_back(toolbarItemFromJson(value.GetObject()));
                        }
                    }

                    const auto viewInjectionObject = surface.GetNamedObject(L"viewInjectionsBySlot", JsonObject());
                    for (const auto& pair : viewInjectionObject.GetView()) {
                        if (pair.Value().ValueType() != JsonValueType::Array) {
                            continue;
                        }

                        auto& slotInjections = viewInjectionsBySlot[std::wstring(pair.Key().c_str())];
                        for (const auto& value : pair.Value().GetArray()) {
                            if (value.ValueType() == JsonValueType::Object) {
                                slotInjections.push_back(viewInjectionFromJson(value.GetObject()));
                            }
                        }

                        std::sort(
                            slotInjections.begin(),
                            slotInjections.end(),
                            [](const auto& left, const auto& right) { return left.priority > right.priority; });
                    }

                    if (surface.HasKey(L"overlaySchema")) {
                        const auto overlaySchema = surface.GetNamedObject(L"overlaySchema", JsonObject());
                        for (const auto& value : overlaySchema.GetNamedArray(L"navigationPointers", JsonArray())) {
                            if (value.ValueType() == JsonValueType::Object) {
                                navigationPointers.push_back(navigationPointerFromJson(value.GetObject()));
                            }
                        }
                        for (const auto& value : overlaySchema.GetNamedArray(L"overlayRoutes", JsonArray())) {
                            if (value.ValueType() == JsonValueType::Object) {
                                overlayRoutes.push_back(overlayRouteFromJson(value.GetObject()));
                            }
                        }
                    }
                }

                snapshot.statusMessage = L"Live status refreshed from the local admin API.";
            } else {
                snapshot.statusMessage = L"Local admin API returned invalid dashboard JSON.";
            }
        } else if (!errorMessage.empty()) {
            snapshot.statusMessage = std::move(errorMessage);
        } else if (service.state == ServiceState::Missing) {
            snapshot.statusMessage = L"Admin API unreachable and service not installed. Start the service host or install the bootstrapper.";
        } else if (service.state == ServiceState::Running) {
            snapshot.statusMessage = L"Service is running but the admin API is not responding yet.";
        } else {
            snapshot.statusMessage = L"Local admin API is not responding.";
        }
    }

    if (providerRows.empty() && !providers.empty()) {
        for (const auto& provider : providers) {
            providerRows.push_back(providerConnectionRow(provider));
        }
    }
    if (providerCapabilityRows.empty() && !providerCapabilities.empty()) {
        for (const auto& capability : providerCapabilities) {
            providerCapabilityRows.push_back(providerCapabilityRow(capability));
        }
    }
    if (providerAssignmentRows.empty() && !providerAssignments.empty()) {
        for (const auto& assignment : providerAssignments) {
            providerAssignmentRows.push_back(providerAssignmentRow(assignment, providerAssignmentTargets, providers));
        }
    }
    if (providerExecutionRegistrationRows.empty() && !providerExecutionRegistrations.empty()) {
        for (const auto& registration : providerExecutionRegistrations) {
            providerExecutionRegistrationRows.push_back(providerExecutionRegistrationRow(registration));
        }
    }
    if (providerExecutionHistoryRows.empty() && !providerExecutionHistory.empty()) {
        for (const auto& record : providerExecutionHistory) {
            providerExecutionHistoryRows.push_back(providerExecutionHistoryRow(record));
        }
    }
    if (endpointRows.empty() && !endpoints.empty()) {
        for (const auto& endpoint : endpoints) {
            endpointRows.push_back(runtimeEndpointRow(endpoint));
        }
    }

    snapshot.endpointCount = endpointRows.size();
    snapshot.providerCount = providers.empty() ? providerRows.size() : providers.size();
    snapshot.installCount = installRows.size();
    snapshot.exportCount = exportRows.size();
    snapshot.governanceRoleCount = governanceRoleRows.size();
    snapshot.governanceRuleCount = governanceRuleRows.size();
    snapshot.governanceDocumentCount = governanceDocumentRows.size();
    snapshot.governanceFindingCount = governanceFindingRows.size();
    snapshot.appleRemoteHostCount = appleRemoteHostRows.size();
    snapshot.appleOperationCount = appleOperationRows.size();
    snapshot.platformGatewayCount = platformGatewayRows.size();
    snapshot.governanceServerCount = governanceServerRows.size();
    snapshot.governanceExecutionCount = governanceExecutionRows.size();

    if (endpointRows.empty()) {
        endpointRows.push_back(L"No endpoint snapshot is available yet.");
    }
    if (providerRows.empty()) {
        providerRows.push_back(L"No provider connections have been loaded yet.");
    }
    if (providerCapabilityRows.empty()) {
        providerCapabilityRows.push_back(L"No provider modules have published capability descriptors yet.");
    }
    if (providerAssignmentRows.empty()) {
        providerAssignmentRows.push_back(L"No orchestration roles or sub-agents have provider ownership yet.");
    }
    if (providerExecutionRegistrationRows.empty()) {
        providerExecutionRegistrationRows.push_back(L"No provider execution modules have registered transports yet.");
    }
    if (providerExecutionHistoryRows.empty()) {
        providerExecutionHistoryRows.push_back(L"No provider execution history has been recorded yet.");
    }
    if (installRows.empty()) {
        installRows.push_back(L"No installer provenance has been recorded yet.");
    }
    if (exportRows.empty()) {
        exportRows.push_back(L"No export artifacts are currently published.");
    }
    if (governanceFindingRows.empty()) {
        governanceFindingRows.push_back(L"No active CLU findings. Current posture is aligned with the published governance profile.");
    }
    if (governanceRoleRows.empty()) {
        governanceRoleRows.push_back(L"No CLU role definitions are available.");
    }
    if (governanceRuleRows.empty()) {
        governanceRuleRows.push_back(L"No CLU rule definitions are available.");
    }
    if (governanceDocumentRows.empty()) {
        governanceDocumentRows.push_back(L"No CLU documents are available.");
    }
    if (governanceActionRows.empty()) {
        governanceActionRows.push_back(L"No immediate operator actions are recommended.");
    }
    if (appleRemoteHostRows.empty()) {
        appleRemoteHostRows.push_back(L"No Apple remote hosts are registered yet.");
    }
    if (appleOperationRows.empty()) {
        appleOperationRows.push_back(L"No Apple governance operations have been recorded yet.");
    }
    if (platformGatewayRows.empty()) {
        platformGatewayRows.push_back(L"No platform gateway lanes are published yet.");
    }
    if (governanceServerRows.empty()) {
        governanceServerRows.push_back(L"No governance server lanes are published yet.");
    }
    if (governanceExecutionRows.empty()) {
        governanceExecutionRows.push_back(L"No governance tool executions have been recorded yet.");
    }

    std::wostringstream overviewStream;
    overviewStream << L"Service state: " << serviceStateLabel(service.state);
    if (service.processId != 0) {
        overviewStream << L" (PID " << service.processId << L')';
    }
    overviewStream << L'\n'
                   << L"Admin API health: " << (snapshot.apiHealthy ? L"reachable" : L"offline") << L'\n'
                   << L"Dashboard URL: " << snapshot.dashboardUrl << L'\n'
                   << L"Config path: " << snapshot.configPath << L'\n'
                   << L"Data directory: " << snapshot.dataDirectory;

    std::wostringstream environmentStream;
    environmentStream << L"Environment: " << wideFromUtf8(environmentName) << L'\n'
                      << L"Host: " << hostName << L'\n'
                      << L"Operating system: " << operatingSystem << L'\n'
                      << L"Primary IP: " << ipAddress << L'\n'
                      << L"Primary MAC: " << wideFromUtf8(macAddress);

    std::wostringstream configurationStream;
    configurationStream << L"Instance name: " << wideFromUtf8(instanceName) << L'\n'
                        << L"Browser port: " << browserPort << L'\n'
                        << L"Beacon port: " << beaconPort << L'\n'
                        << L"Bind address: " << wideFromUtf8(bindAddress) << L'\n'
                        << L"Beacon enabled: " << boolLabel(beaconEnabled) << L'\n'
                        << L"AI autonomy: " << boolLabel(aiAutonomyEnabled) << L'\n'
                        << L"Security protocols: " << boolLabel(securityProtocolsEnabled) << L'\n'
                        << L"Open LAN access: " << boolLabel(openLanAccess) << L'\n'
                        << L"Resource envelope: CPU " << cpuPercent
                        << L"% | RAM " << memoryPercent
                        << L"% | Bandwidth " << bandwidthPercent
                        << L"% | Storage " << storagePercent << L'%';

    std::wostringstream governanceStream;
    governanceStream << L"CLU posture: " << governancePosture << L'\n'
                     << L"Evaluated: " << governanceLastEvaluatedUtc << L'\n'
                     << L"Findings: " << snapshot.governanceFindingCount << L'\n'
                     << L"Roles / rules / documents: "
                     << snapshot.governanceRoleCount << L" / "
                     << snapshot.governanceRuleCount << L" / "
                     << snapshot.governanceDocumentCount << L'\n'
                     << L"Managed resource envelope: CPU " << cpuPercent
                     << L"% | RAM " << memoryPercent
                     << L"% | Bandwidth " << bandwidthPercent
                     << L"% | Storage " << storagePercent
                     << L"%";
    if (cpuPercent <= 0 || memoryPercent <= 0 || bandwidthPercent <= 0 || storagePercent <= 0) {
        governanceStream << L"  (one or more governed launch lanes blocked)";
    }
    governanceStream << L'\n'
                     << L"Apple hosts / operations: "
                     << snapshot.appleRemoteHostCount << L" / "
                     << snapshot.appleOperationCount << L'\n'
                     << L"Gateway lanes / governance servers: "
                     << snapshot.platformGatewayCount << L" / "
                     << snapshot.governanceServerCount << L'\n'
                     << governanceDoctrine;

    snapshot.overviewText = overviewStream.str();
    snapshot.telemetryText = telemetryText;
    snapshot.environmentText = environmentStream.str();
    snapshot.configurationText = configurationStream.str();
    snapshot.governancePosture = governancePosture;
    snapshot.governanceDoctrine = governanceDoctrine;
    snapshot.governanceNarrative = governanceStream.str();
    snapshot.governanceLastEvaluatedUtc = governanceLastEvaluatedUtc;
    snapshot.browserPort = browserPort;
    snapshot.beaconPort = beaconPort;
    snapshot.beaconEnabled = beaconEnabled;
    snapshot.aiAutonomyEnabled = aiAutonomyEnabled;
    snapshot.advancedMode = advancedMode;
    snapshot.firstRunCompleted = firstRunCompleted;
    snapshot.securityProtocolsEnabled = securityProtocolsEnabled;
    snapshot.openLanAccess = openLanAccess;
    snapshot.securitySettings = std::move(securitySettings);
    snapshot.endpoints = std::move(endpoints);
    snapshot.cpuAllocationPercent = cpuPercent;
    snapshot.memoryAllocationPercent = memoryPercent;
    snapshot.bandwidthAllocationPercent = bandwidthPercent;
    snapshot.storageAllocationPercent = storagePercent;
    snapshot.environmentName = wideFromUtf8(environmentName);
    snapshot.hostName = hostName;
    snapshot.operatingSystem = operatingSystem;
    snapshot.primaryIpAddress = ipAddress;
    snapshot.primaryMacAddress = wideFromUtf8(macAddress);
    snapshot.instanceName = wideFromUtf8(instanceName);
    snapshot.bindAddress = wideFromUtf8(bindAddress);
    snapshot.providers = std::move(providers);
    snapshot.providerCapabilities = std::move(providerCapabilities);
    snapshot.providerCredentialStatuses = std::move(providerCredentialStatuses);
    snapshot.subAgentGroups = std::move(subAgentGroups);
    snapshot.providerAssignmentTargets = std::move(providerAssignmentTargets);
    snapshot.providerAssignments = std::move(providerAssignments);
    snapshot.providerExecutionRegistrations = std::move(providerExecutionRegistrations);
    snapshot.providerExecutionHistory = std::move(providerExecutionHistory);
    snapshot.appleRemoteHosts = std::move(appleRemoteHosts);
    snapshot.appleOperations = std::move(appleOperations);
    snapshot.navigationPointers = std::move(navigationPointers);
    snapshot.toolbarItems = std::move(toolbarItems);
    snapshot.overlayRoutes = std::move(overlayRoutes);
    snapshot.viewInjectionsBySlot = std::move(viewInjectionsBySlot);
    snapshot.endpointRows = std::move(endpointRows);
    snapshot.providerRows = std::move(providerRows);
    snapshot.providerCapabilityRows = std::move(providerCapabilityRows);
    snapshot.providerAssignmentRows = std::move(providerAssignmentRows);
    snapshot.installRows = std::move(installRows);
    snapshot.exportRows = std::move(exportRows);
    snapshot.governanceFindingRows = std::move(governanceFindingRows);
    snapshot.governanceRoleRows = std::move(governanceRoleRows);
    snapshot.governanceRuleRows = std::move(governanceRuleRows);
    snapshot.governanceDocumentRows = std::move(governanceDocumentRows);
    snapshot.governanceActionRows = std::move(governanceActionRows);
    snapshot.appleRemoteHostRows = std::move(appleRemoteHostRows);
    snapshot.appleOperationRows = std::move(appleOperationRows);
    snapshot.platformGatewayRows = std::move(platformGatewayRows);
    snapshot.governanceServerRows = std::move(governanceServerRows);
    snapshot.governanceExecutionRows = std::move(governanceExecutionRows);
    return snapshot;
}

bool ShellRuntime::StartService(std::wstring& message) const {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (scm == nullptr) {
        message = L"Unable to connect to the Windows Service Control Manager.";
        return false;
    }

    SC_HANDLE service = OpenServiceW(scm, kServiceName, SERVICE_START | SERVICE_QUERY_STATUS);
    if (service == nullptr) {
        CloseServiceHandle(scm);
        message = L"Master Control Orchestration Server service is not installed.";
        return false;
    }

    SERVICE_STATUS_PROCESS status{};
    DWORD bytesNeeded = 0;
    bool started = false;
    if (QueryServiceStatusEx(
            service,
            SC_STATUS_PROCESS_INFO,
            reinterpret_cast<LPBYTE>(&status),
            sizeof(status),
            &bytesNeeded) != 0 &&
        status.dwCurrentState == SERVICE_RUNNING) {
        message = L"Service is already running.";
        started = true;
    } else if (::StartServiceW(service, 0, nullptr) != 0 || GetLastError() == ERROR_SERVICE_ALREADY_RUNNING) {
        message = L"Service start requested.";
        started = true;
    } else {
        message = L"Unable to start the Master Control Orchestration Server service.";
    }

    CloseServiceHandle(service);
    CloseServiceHandle(scm);
    return started;
}

bool ShellRuntime::StopService(std::wstring& message) const {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (scm == nullptr) {
        message = L"Unable to connect to the Windows Service Control Manager.";
        return false;
    }

    SC_HANDLE service = OpenServiceW(scm, kServiceName, SERVICE_STOP | SERVICE_QUERY_STATUS);
    if (service == nullptr) {
        CloseServiceHandle(scm);
        message = L"Master Control Orchestration Server service is not installed.";
        return false;
    }

    SERVICE_STATUS status{};
    const bool stopped = ControlService(service, SERVICE_CONTROL_STOP, &status) != 0 || GetLastError() == ERROR_SERVICE_NOT_ACTIVE;
    message = stopped ? L"Service stop requested." : L"Unable to stop the Master Control Orchestration Server service.";

    CloseServiceHandle(service);
    CloseServiceHandle(scm);
    return stopped;
}

ShellOperationResult ShellRuntime::UpsertSubAgent(const ShellRuntimeEndpoint& subAgent) const {
    if (subAgent.id.empty()) {
        return ShellOperationResult{ false, false, L"Sub-agent ID is required." };
    }
    if (subAgent.displayName.empty()) {
        return ShellOperationResult{ false, false, L"Sub-agent display name is required." };
    }

    return postJsonObjectToAdminApi(
        ResolveConfigurationFile(),
        L"/api/runtime/subagents",
        runtimeEndpointToJson(subAgent),
        L"Unable to save custom sub-agent settings through the local admin API.");
}

ShellOperationResult ShellRuntime::UpsertMcpServer(const ShellRuntimeEndpoint& mcpServer) const {
    if (mcpServer.id.empty()) {
        return ShellOperationResult{ false, false, L"MCP server ID is required." };
    }
    if (mcpServer.displayName.empty()) {
        return ShellOperationResult{ false, false, L"MCP server display name is required." };
    }
    if (mcpServer.port == 0) {
        return ShellOperationResult{ false, false, L"MCP servers require a listening port." };
    }

    ShellRuntimeEndpoint normalized = mcpServer;
    if (normalized.kind.empty()) {
        normalized.kind = L"mcp_server";
    }
    if (normalized.protocol.empty()) {
        normalized.protocol = L"http";
    }

    return postJsonObjectToAdminApi(
        ResolveConfigurationFile(),
        L"/api/runtime/mcp-servers",
        runtimeEndpointToJson(normalized),
        L"Unable to save custom MCP server settings through the local admin API.");
}

ShellOperationResult ShellRuntime::RemoveMcpServer(const std::wstring& mcpServerId) const {
    if (mcpServerId.empty()) {
        return ShellOperationResult{ false, false, L"Select a custom MCP server before removing it." };
    }

    JsonObject payload;
    payload.SetNamedValue(L"mcpServerId", JsonValue::CreateStringValue(mcpServerId));
    return postJsonObjectToAdminApi(
        ResolveConfigurationFile(),
        L"/api/runtime/mcp-servers/remove",
        payload,
        L"Unable to remove the selected custom MCP server through the local admin API.");
}

ShellOperationResult ShellRuntime::RemoveSubAgent(const std::wstring& subAgentId) const {
    if (subAgentId.empty()) {
        return ShellOperationResult{ false, false, L"Select a custom sub-agent before removing it." };
    }

    JsonObject payload;
    payload.SetNamedValue(L"subAgentId", JsonValue::CreateStringValue(subAgentId));
    return postJsonObjectToAdminApi(
        ResolveConfigurationFile(),
        L"/api/runtime/subagents/remove",
        payload,
        L"Unable to remove the selected custom sub-agent through the local admin API.");
}

ShellOperationResult ShellRuntime::UpsertProvider(const ShellProviderConnection& provider) const {
    if (provider.id.empty()) {
        return ShellOperationResult{ false, false, L"Provider ID is required." };
    }
    if (provider.displayName.empty()) {
        return ShellOperationResult{ false, false, L"Provider display name is required." };
    }
    if (provider.baseUrl.empty()) {
        return ShellOperationResult{ false, false, L"Provider base URL is required." };
    }

    return postProviderToAdminApi(ResolveConfigurationFile(), provider);
}

ShellOperationResult ShellRuntime::UpsertProviderCredentials(
    const std::wstring& providerId,
    const std::vector<std::pair<std::wstring, std::wstring>>& values) const {
    if (providerId.empty()) {
        return ShellOperationResult{ false, false, L"Select a provider route before saving credentials." };
    }

    return postJsonObjectToAdminApi(
        ResolveConfigurationFile(),
        L"/api/providers/credentials",
        providerCredentialsToJson(providerId, values),
        L"Unable to update provider credentials through the local admin API.");
}

ShellOperationResult ShellRuntime::UpsertAppleRemoteHost(const ShellAppleRemoteHost& host) const {
    if (host.hostId.empty()) {
        return ShellOperationResult{ false, false, L"Apple remote host ID is required." };
    }
    if (host.displayName.empty()) {
        return ShellOperationResult{ false, false, L"Apple remote host display name is required." };
    }
    if (host.transport.empty() || host.transport == L"unknown") {
        return ShellOperationResult{ false, false, L"Select either SSH or Companion Service transport." };
    }
    if (host.platforms.empty()) {
        return ShellOperationResult{ false, false, L"Select at least one Apple platform for the remote host." };
    }

    return postJsonObjectToAdminApi(
        ResolveConfigurationFile(),
        L"/api/platform-services/apple-hosts",
        appleRemoteHostToJson(host),
        L"Unable to update the Apple remote host through the local admin API.");
}

ShellOperationResult ShellRuntime::RemoveAppleRemoteHost(const std::wstring& hostId) const {
    if (hostId.empty()) {
        return ShellOperationResult{ false, false, L"Select an Apple remote host before removing it." };
    }

    JsonObject payload;
    payload.SetNamedValue(L"hostId", JsonValue::CreateStringValue(hostId));
    return postJsonObjectToAdminApi(
        ResolveConfigurationFile(),
        L"/api/platform-services/apple-hosts/remove",
        payload,
        L"Unable to remove the Apple remote host through the local admin API.");
}

ShellOperationResult ShellRuntime::UpsertSubAgentGroup(const ShellSubAgentGroupDefinition& group) const {
    if (group.groupId.empty()) {
        return ShellOperationResult{ false, false, L"Enter a group ID before saving the sub-agent group." };
    }
    return postJsonObjectToAdminApi(
        ResolveConfigurationFile(),
        L"/api/providers/groups",
        subAgentGroupToJson(group),
        L"Unable to update the sub-agent group through the local admin API.");
}

ShellOperationResult ShellRuntime::RemoveSubAgentGroup(const std::wstring& groupId) const {
    if (groupId.empty()) {
        return ShellOperationResult{ false, false, L"Select a sub-agent group before removing it." };
    }

    JsonObject payload;
    payload.SetNamedValue(L"groupId", JsonValue::CreateStringValue(groupId));
    return postJsonObjectToAdminApi(
        ResolveConfigurationFile(),
        L"/api/providers/groups/remove",
        payload,
        L"Unable to remove the sub-agent group through the local admin API.");
}

ShellOperationResult ShellRuntime::UpsertProviderAssignment(const ShellProviderAssignment& assignment) const {
    if (assignment.targetId.empty()) {
        return ShellOperationResult{ false, false, L"Select an orchestration target before saving provider ownership." };
    }

    MasterControl::Diagnostics::appendEvent(
        L"shell",
        "info",
        "provider-assignment-save-start",
        "Submitting provider ownership update through the local admin API.",
        nlohmann::json{
            { "targetId", MasterControl::Diagnostics::utf8FromWide(assignment.targetId) },
            { "targetKind", MasterControl::Diagnostics::utf8FromWide(assignment.kind) },
            { "providerId", MasterControl::Diagnostics::utf8FromWide(assignment.providerId) }
        });

    auto result = postJsonObjectToAdminApi(
        ResolveConfigurationFile(),
        L"/api/providers/assignments",
        providerAssignmentToJson(assignment),
        L"Unable to update provider ownership through the local admin API.");

    MasterControl::Diagnostics::appendEvent(
        L"shell",
        result.succeeded ? "info" : "warning",
        result.succeeded ? "provider-assignment-save-complete" : "provider-assignment-save-failed",
        MasterControl::Diagnostics::utf8FromWide(result.message),
        nlohmann::json{
            { "targetId", MasterControl::Diagnostics::utf8FromWide(assignment.targetId) },
            { "targetKind", MasterControl::Diagnostics::utf8FromWide(assignment.kind) },
            { "providerId", MasterControl::Diagnostics::utf8FromWide(assignment.providerId) }
        });
    return result;
}

ShellForsettiModuleCatalogResult ShellRuntime::FetchForsettiModules() const {
    return fetchForsettiModulesFromAdminApi(ResolveConfigurationFile());
}

ShellOperationResult ShellRuntime::ManageForsettiModule(const std::wstring& moduleId, const std::wstring& action) const {
    if (moduleId.empty()) {
        return ShellOperationResult{ false, false, L"Select a Forsetti module before applying a module action." };
    }

    const auto normalizedAction = trimWideCopy(action);
    if (normalizedAction.empty()) {
        return ShellOperationResult{ false, false, L"Select a Forsetti module action before continuing." };
    }

    return postJsonObjectToAdminApi(
        ResolveConfigurationFile(),
        L"/api/forsetti/modules/state",
        forsettiModuleActionToJson(moduleId, normalizedAction),
        L"Unable to update the Forsetti module state through the local admin API.");
}

ShellActivityStreamResult ShellRuntime::FetchActivityEvents(const std::wstring& sinceId) const {
    ShellActivityStreamResult result;
    std::wstring errorMessage;
    const auto [host, port] = adminApiEndpoint(ResolveConfigurationFile());
    std::wstring path = L"/api/activity";
    if (!sinceId.empty()) {
        path += L"?since=";
        path += sinceId;
    }
    const auto response = httpGet(host, port, path, errorMessage);
    if (!response.has_value()) {
        result.errorMessage = errorMessage.empty()
            ? std::wstring(L"Unable to reach the local admin API activity stream.")
            : errorMessage;
        return result;
    }
    if (response->statusCode != 200) {
        result.errorMessage = L"Local admin API rejected the activity request.";
        return result;
    }
    const auto body = parseJsonObject(response->body);
    if (!body.has_value()) {
        result.errorMessage = L"Local admin API returned invalid activity JSON.";
        return result;
    }

    result.highWaterMarkId = body->GetNamedString(L"highWaterMarkId", L"");
    if (body->HasKey(L"events")) {
        const auto eventsArray = body->GetNamedArray(L"events", JsonArray());
        for (const auto& entry : eventsArray) {
            if (entry.ValueType() != JsonValueType::Object) continue;
            const auto obj = entry.GetObject();
            ShellActivityEvent event;
            event.id = obj.GetNamedString(L"id", L"");
            event.kind = obj.GetNamedString(L"kind", L"");
            event.timestampUtc = obj.GetNamedString(L"timestampUtc", L"");
            event.actor = obj.GetNamedString(L"actor", L"");
            event.method = obj.GetNamedString(L"method", L"");
            event.target = obj.GetNamedString(L"target", L"");
            event.statusCode = static_cast<int>(obj.GetNamedNumber(L"statusCode", 0));
            event.latencyMs = static_cast<int>(obj.GetNamedNumber(L"latencyMs", 0));
            event.message = obj.GetNamedString(L"message", L"");
            event.detail = obj.GetNamedString(L"detail", L"");
            result.events.push_back(std::move(event));
        }
    }
    result.succeeded = true;
    return result;
}

ShellAutoConnectProviderResult ShellRuntime::AutoConnectProvider(
    const ShellAutoConnectProviderRequest& request) const {
    ShellAutoConnectProviderResult result;

    if (request.kind.empty()) {
        result.errorMessage = L"Select a provider kind before auto-connecting.";
        result.summary = result.errorMessage;
        return result;
    }

    MasterControl::Diagnostics::appendEvent(
        L"shell",
        "info",
        "provider-auto-connect-start",
        "Submitting provider auto-connect request from the Windows application.",
        nlohmann::json{
            { "providerIdHint", MasterControl::Diagnostics::utf8FromWide(request.providerId) },
            { "kind", MasterControl::Diagnostics::utf8FromWide(request.kind) },
            { "credentialFieldCount", request.credentials.size() },
            { "assignmentTargetIds", [assignmentTargetIds = request.assignmentTargetIds]() {
                nlohmann::json values = nlohmann::json::array();
                for (const auto& targetId : assignmentTargetIds) {
                    values.push_back(MasterControl::Diagnostics::utf8FromWide(targetId));
                }
                return values;
            }() },
            { "allowAutonomousControl", request.allowAutonomousControl },
            { "discoverModels", request.discoverModels }
        });

    result = postAutoConnectProviderToAdminApi(ResolveConfigurationFile(), request);
    MasterControl::Diagnostics::appendEvent(
        L"shell",
        result.succeeded ? "info" : "warning",
        result.succeeded ? "provider-auto-connect-complete" : "provider-auto-connect-failed",
        MasterControl::Diagnostics::utf8FromWide(result.summary.empty() ? result.errorMessage : result.summary),
        nlohmann::json{
            { "providerId", MasterControl::Diagnostics::utf8FromWide(result.providerId) },
            { "assignmentCount", result.assignmentsApplied.size() }
        });
    return result;
}

ShellProviderExecutionRecord ShellRuntime::ExecuteProviderTask(const ShellProviderExecutionRequest& request) const {
    ShellProviderExecutionRecord record;
    record.targetId = request.targetId;
    record.status = L"failed";

    if (request.targetId.empty()) {
        record.errorMessage = L"Select an orchestration target before executing a provider task.";
        return record;
    }
    if (request.prompt.empty()) {
        record.errorMessage = L"Enter a prompt before executing a provider task.";
        return record;
    }

    return postProviderExecutionToAdminApi(ResolveConfigurationFile(), request);
}

ShellOperationResult ShellRuntime::ExecuteGovernanceTool(const std::wstring& platform,
                                                         const std::wstring& toolId,
                                                         const std::wstring& targetPath,
                                                         const std::map<std::wstring, std::wstring>& options) const {
    if (platform.empty()) {
        return ShellOperationResult{ false, false, L"Governance execution requires a platform." };
    }
    if (toolId.empty()) {
        return ShellOperationResult{ false, false, L"Governance execution requires a tool ID." };
    }

    return postGovernanceToolToAdminApi(ResolveConfigurationFile(), platform, toolId, targetPath, options);
}

ShellOperationResult ShellRuntime::UpdateAiAutonomyEnabled(const bool enabled) const {
    std::wstring errorMessage;
    auto configuration = fetchConfigurationFromAdminApi(ResolveConfigurationFile(), errorMessage);
    if (!configuration.has_value()) {
        return ShellOperationResult{ false, false, errorMessage.empty() ? L"Unable to load the current configuration." : errorMessage };
    }

    configuration->SetNamedValue(L"aiAutonomyEnabled", JsonValue::CreateBooleanValue(enabled));
    return postConfigurationToAdminApi(ResolveConfigurationFile(), *configuration, false);
}

ShellOperationResult ShellRuntime::UpdateSecuritySettings(const ShellSecuritySettings& settings,
                                                          const bool confirmUnsafeChanges) const {
    std::wstring errorMessage;
    auto configuration = fetchConfigurationFromAdminApi(ResolveConfigurationFile(), errorMessage);
    if (!configuration.has_value()) {
        return ShellOperationResult{ false, false, errorMessage.empty() ? L"Unable to load the current configuration." : errorMessage };
    }

    JsonObject security = configuration->HasKey(L"security")
        ? configuration->GetNamedObject(L"security", JsonObject())
        : JsonObject();

    security.SetNamedValue(L"enableTls", JsonValue::CreateBooleanValue(settings.enableTls));
    security.SetNamedValue(L"enableAuthentication", JsonValue::CreateBooleanValue(settings.enableAuthentication));
    security.SetNamedValue(L"allowTroubleshootingBypass", JsonValue::CreateBooleanValue(settings.allowTroubleshootingBypass));
    security.SetNamedValue(L"allowOpenLanAccess", JsonValue::CreateBooleanValue(settings.allowOpenLanAccess));
    security.SetNamedValue(L"securityProtocolsEnabled", JsonValue::CreateBooleanValue(settings.securityProtocolsEnabled));
    security.SetNamedValue(L"trustedRemoteHosts", stringArrayToJson(settings.trustedRemoteHosts));
    configuration->SetNamedValue(L"security", security);

    return postConfigurationToAdminApi(ResolveConfigurationFile(), *configuration, confirmUnsafeChanges);
}

ShellOperationResult ShellRuntime::UpdateHostSettings(const ShellHostSettings& settings) const {
    if (trimWideCopy(settings.instanceName).empty()) {
        return ShellOperationResult{ false, false, L"Instance name is required." };
    }
    if (trimWideCopy(settings.bindAddress).empty()) {
        return ShellOperationResult{ false, false, L"Bind address is required." };
    }
    if (settings.browserPort == 0) {
        return ShellOperationResult{ false, false, L"Browser port must be between 1 and 65535." };
    }
    if (settings.beaconPort == 0) {
        return ShellOperationResult{ false, false, L"Beacon port must be between 1 and 65535." };
    }

    const auto percentInRange = [](const int value) {
        return value >= 0 && value <= 100;
    };
    if (!percentInRange(settings.cpuAllocationPercent) ||
        !percentInRange(settings.memoryAllocationPercent) ||
        !percentInRange(settings.bandwidthAllocationPercent) ||
        !percentInRange(settings.storageAllocationPercent)) {
        return ShellOperationResult{ false, false, L"Resource allocation percentages must stay between 0 and 100." };
    }

    std::wstring errorMessage;
    auto configuration = fetchConfigurationFromAdminApi(ResolveConfigurationFile(), errorMessage);
    if (!configuration.has_value()) {
        return ShellOperationResult{ false, false, errorMessage.empty() ? L"Unable to load the current configuration." : errorMessage };
    }

    configuration->SetNamedValue(L"instanceName", JsonValue::CreateStringValue(trimWideCopy(settings.instanceName)));
    configuration->SetNamedValue(L"bindAddress", JsonValue::CreateStringValue(trimWideCopy(settings.bindAddress)));
    configuration->SetNamedValue(L"browserPort", JsonValue::CreateNumberValue(settings.browserPort));
    configuration->SetNamedValue(L"beaconPort", JsonValue::CreateNumberValue(settings.beaconPort));
    configuration->SetNamedValue(L"beaconEnabled", JsonValue::CreateBooleanValue(settings.beaconEnabled));

    JsonObject resourceAllocation = configuration->HasKey(L"resourceAllocation")
        ? configuration->GetNamedObject(L"resourceAllocation", JsonObject())
        : JsonObject();
    resourceAllocation.SetNamedValue(L"cpuPercent", JsonValue::CreateNumberValue(settings.cpuAllocationPercent));
    resourceAllocation.SetNamedValue(L"memoryPercent", JsonValue::CreateNumberValue(settings.memoryAllocationPercent));
    resourceAllocation.SetNamedValue(L"bandwidthPercent", JsonValue::CreateNumberValue(settings.bandwidthAllocationPercent));
    resourceAllocation.SetNamedValue(L"storagePercent", JsonValue::CreateNumberValue(settings.storageAllocationPercent));
    configuration->SetNamedValue(L"resourceAllocation", resourceAllocation);

    return postConfigurationToAdminApi(ResolveConfigurationFile(), *configuration, false);
}

ShellOperationResult ShellRuntime::InstallPackage(const ShellInstallerPackageSpec& spec) const {
    if (spec.source.empty()) {
        return ShellOperationResult{ false, false, L"Installer source or local path is required." };
    }

    return postJsonObjectToAdminApi(
        ResolveConfigurationFile(),
        L"/api/install/package",
        packageToJson(spec),
        L"Unable to run the package installer through the local admin API.");
}

ShellOperationResult ShellRuntime::InstallRepository(const ShellBootstrapRepoSpec& spec) const {
    if (spec.repositoryUrl.empty()) {
        return ShellOperationResult{ false, false, L"Bootstrap repository URL is required." };
    }

    return postJsonObjectToAdminApi(
        ResolveConfigurationFile(),
        L"/api/install/repo",
        repoToJson(spec),
        L"Unable to run the bootstrap repository import through the local admin API.");
}

ShellOperationResult ShellRuntime::InstallZipBundle(const ShellZipBundleSpec& spec) const {
    if (spec.source.empty()) {
        return ShellOperationResult{ false, false, L"Zip bundle source or local path is required." };
    }

    return postJsonObjectToAdminApi(
        ResolveConfigurationFile(),
        L"/api/install/zip",
        zipToJson(spec),
        L"Unable to run the zip bundle import through the local admin API.");
}

ShellExportFetchResult ShellRuntime::FetchExports() const {
    return fetchExportsFromAdminApi(ResolveConfigurationFile());
}

ShellOperationResult ShellRuntime::MaterializeExports(const std::vector<std::wstring>& artifactIds) const {
    const auto exports = FetchExports();
    if (!exports.succeeded) {
        return ShellOperationResult{ false, false, exports.message };
    }

    std::vector<ShellExportArtifact> selectedArtifacts;
    if (artifactIds.empty()) {
        selectedArtifacts = exports.artifacts;
    } else {
        for (const auto& artifactId : artifactIds) {
            const auto iterator = std::find_if(
                exports.artifacts.begin(),
                exports.artifacts.end(),
                [&artifactId](const auto& artifact) { return artifact.id == artifactId; });
            if (iterator != exports.artifacts.end()) {
                selectedArtifacts.push_back(*iterator);
            }
        }
    }

    if (selectedArtifacts.empty()) {
        return ShellOperationResult{ false, false, L"No matching export artifacts were available." };
    }

    const auto exportDirectory = ResolveExportsDirectory() / exportTimestampFolderName();
    std::filesystem::create_directories(exportDirectory);

    for (const auto& artifact : selectedArtifacts) {
        const auto fileName = artifact.fileName.empty() ? L"artifact.txt" : safeFolderComponent(artifact.fileName);
        if (!writeUtf8File(exportDirectory / fileName, artifact.content)) {
            return ShellOperationResult{ false, false, L"Failed to write one or more export artifacts to disk." };
        }
    }

    std::wostringstream message;
    message << L"Exported " << selectedArtifacts.size() << L" artifact";
    if (selectedArtifacts.size() != 1) {
        message << L"s";
    }
    message << L" to " << exportDirectory.wstring();
    return ShellOperationResult{ true, false, message.str() };
}

void ShellRuntime::OpenDashboard(const ShellSnapshot& snapshot) const {
    ShellExecuteW(nullptr, L"open", snapshot.dashboardUrl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void ShellRuntime::OpenConfig() const {
    openPathInExplorer(ResolveConfigurationFile());
}

void ShellRuntime::OpenDataDirectory() const {
    openPathInExplorer(ResolveDataDirectory());
}

void ShellRuntime::OpenExportsDirectory() const {
    openPathInExplorer(ResolveExportsDirectory());
}

std::filesystem::path ShellRuntime::ResolveDataDirectory() const {
    if (const auto overrideValue = wideEnvironmentVariable(kDataDirectoryOverrideVariable); overrideValue.has_value()) {
        return std::filesystem::path(*overrideValue);
    }

    const auto currentDirectory = programDataDirectory() / kCurrentDataDirectoryLeaf;
    const auto legacyDirectory = programDataDirectory() / kLegacyDataDirectoryLeaf;
    if (!std::filesystem::exists(currentDirectory) && std::filesystem::exists(legacyDirectory)) {
        return legacyDirectory;
    }

    return currentDirectory;
}

std::filesystem::path ShellRuntime::ResolveConfigurationFile() const {
    const auto currentFile = ResolveDataDirectory() / "config" / "master-control-orchestration-server.json";
    const auto legacyFile = ResolveDataDirectory() / "config" / "master-control-program.json";
    if (!std::filesystem::exists(currentFile) && std::filesystem::exists(legacyFile)) {
        return legacyFile;
    }

    return currentFile;
}

std::filesystem::path ShellRuntime::ResolveExportsDirectory() const {
    return ResolveDataDirectory() / "exports";
}

ShellRuntime::ShellCliSignInStartResult ShellRuntime::StartCliSignIn(
    const std::wstring& bridge,
    const std::wstring& providerId) const {
    ShellCliSignInStartResult result;

    const auto cliPath = resolveCliPath(bridge);
    if (!cliPath.has_value()) {
        result.message = cliBridgeDisplayName(bridge) +
            L" is not installed for this Windows user session. Install it first and try again.";
        result.bridge = bridge;
        result.cliInstalled = false;
        return result;
    }

    const auto executablePath = *cliPath;
    std::wstring applicationName = executablePath.wstring();
    std::wstring commandLine;
    if (usesCmdShim(*cliPath)) {
        const auto commandProcessor = resolveCommandProcessorPath();
        if (!commandProcessor.has_value()) {
            result.message = L"Windows could not locate cmd.exe to launch " +
                cliBridgeDisplayName(bridge) + L" sign-in.";
            result.bridge = bridge;
            result.cliInstalled = true;
            return result;
        }

        applicationName = commandProcessor->wstring();
        commandLine = quoteWindowsArgument(commandProcessor->wstring());
        commandLine += L" /d /s /c ";
        commandLine += quoteWindowsArgument(executablePath.wstring());
        commandLine += L" login";
    } else {
        commandLine = quoteWindowsArgument(executablePath.wstring());
        commandLine += L" login";
    }

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo{};
    std::wstring mutableCommand = commandLine;
    auto workingDirectory = currentUserProfileDirectory().value_or(executablePath.parent_path());
    std::error_code workingDirectoryError;
    if (!workingDirectory.empty() && !std::filesystem::exists(workingDirectory, workingDirectoryError)) {
        workingDirectory = executablePath.parent_path();
    }
    const std::wstring workingDirectoryText = workingDirectory.empty()
        ? std::wstring()
        : workingDirectory.wstring();
    const BOOL created = CreateProcessW(
        applicationName.empty() ? nullptr : applicationName.c_str(),
        mutableCommand.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NEW_CONSOLE | CREATE_UNICODE_ENVIRONMENT,
        nullptr,
        workingDirectoryText.empty() ? nullptr : workingDirectoryText.c_str(),
        &startupInfo,
        &processInfo);
    if (created == 0) {
        const DWORD lastError = GetLastError();
        result.message = L"Failed to launch " + cliBridgeDisplayName(bridge) +
            L" sign-in (error " + std::to_wstring(lastError) + L").";
        result.bridge = bridge;
        result.cliInstalled = true;
        return result;
    }

    CloseHandle(processInfo.hThread);

    LocalCliSignInSession session;
    session.sessionId = generateCliSignInSessionId();
    session.bridge = bridge;
    session.providerId = providerId;
    session.message = L"Complete the sign-in prompt in the console window or browser.";
    session.accountLabel = cliBridgeAccountLabel(bridge);
    session.authFilePath = expectedCliAuthFilePath(bridge);
    session.processHandle = processInfo.hProcess;

    {
        std::lock_guard<std::mutex> lock(gCliSignInMutex);
        gCliSignInSessions[session.sessionId] = session;
    }

    result.succeeded = true;
    result.message = L"Sign-in console opened. Complete the prompt to finish.";
    result.sessionId = session.sessionId;
    result.bridge = bridge;
    result.cliInstalled = true;
    return result;
}

ShellRuntime::ShellCliSignInStatusResult ShellRuntime::GetCliSignInStatus(
    const std::wstring& sessionId) const {
    ShellCliSignInStatusResult result;
    result.succeeded = false;
    result.status = L"failed";

    if (sessionId.empty()) {
        result.message = L"Missing session id.";
        return result;
    }

    std::optional<std::pair<std::wstring, std::wstring>> registrationRequest;
    {
        std::lock_guard<std::mutex> lock(gCliSignInMutex);
        const auto iterator = gCliSignInSessions.find(sessionId);
        if (iterator == gCliSignInSessions.end()) {
            result.message = L"Unknown sign-in session.";
            return result;
        }

        auto& session = iterator->second;
        if (session.status == L"pending") {
            const auto now = std::chrono::steady_clock::now();
            if (session.processHandle != nullptr) {
                const DWORD waitResult = WaitForSingleObject(session.processHandle, 0);
                if (waitResult == WAIT_OBJECT_0) {
                    DWORD exitCode = 0;
                    GetExitCodeProcess(session.processHandle, &exitCode);
                    CloseHandle(session.processHandle);
                    session.processHandle = nullptr;

                    const bool authFileExists = !session.authFilePath.empty() &&
                        std::filesystem::exists(session.authFilePath);
                    if (exitCode == 0 && authFileExists) {
                        if (!session.registrationInProgress) {
                            session.registrationInProgress = true;
                            session.awaitingAuthFile = false;
                            session.authFileDeadline = {};
                            session.message = L"Finishing sign-in and registering the provider...";
                            registrationRequest = std::make_pair(session.bridge, session.providerId);
                        }
                    } else if (exitCode == 0) {
                        session.awaitingAuthFile = true;
                        session.authFileDeadline = now + std::chrono::seconds(30);
                        session.message = L"Sign-in finished. Waiting for " + cliBridgeDisplayName(session.bridge) +
                            L" to finish writing its credential file for this Windows user.";
                    } else {
                        session.status = L"failed";
                        session.message = L"Sign-in was canceled or failed (exit code " +
                            std::to_wstring(exitCode) + L").";
                    }
                }
            }

            if (session.status == L"pending" && session.awaitingAuthFile && !session.registrationInProgress) {
                const bool authFileExists = !session.authFilePath.empty() &&
                    std::filesystem::exists(session.authFilePath);
                if (authFileExists) {
                    session.registrationInProgress = true;
                    session.awaitingAuthFile = false;
                    session.authFileDeadline = {};
                    session.message = L"Finishing sign-in and registering the provider...";
                    registrationRequest = std::make_pair(session.bridge, session.providerId);
                } else if (session.authFileDeadline != std::chrono::steady_clock::time_point{} &&
                           now >= session.authFileDeadline) {
                    session.status = L"failed";
                    session.awaitingAuthFile = false;
                    session.authFileDeadline = {};
                    session.message = L"Sign-in exited cleanly, but " + cliBridgeDisplayName(session.bridge) +
                        L" did not create its credential file for this Windows user.";
                }
            }
        }
    }

    if (registrationRequest.has_value()) {
        JsonObject payload;
        payload.SetNamedValue(L"bridge", JsonValue::CreateStringValue(registrationRequest->first));
        payload.SetNamedValue(L"providerId", JsonValue::CreateStringValue(registrationRequest->second));
        const auto registerResult = postJsonObjectToAdminApi(
            ResolveConfigurationFile(),
            L"/api/providers/signin/register",
            payload,
            L"Unable to register the signed-in provider through the local admin API.");

        std::lock_guard<std::mutex> lock(gCliSignInMutex);
        const auto iterator = gCliSignInSessions.find(sessionId);
        if (iterator != gCliSignInSessions.end()) {
            auto& session = iterator->second;
            session.registrationInProgress = false;
            session.awaitingAuthFile = false;
            session.authFileDeadline = {};
            if (registerResult.succeeded) {
                session.status = L"complete";
                const auto successMessage = session.bridge == L"codex"
                    ? std::wstring(L"Signed in. ChatGPT (planning / reasoning) and Codex (coding agent) are both registered - assign each to roles below.")
                    : std::wstring(L"Signed in. ") + session.accountLabel + L" is registered - assign it to a role below.";
                if (session.bridge == L"codex") {
                    session.message = L"Signed in. ChatGPT (planning / reasoning) and Codex (coding agent) are both registered — assign each to roles below.";
                } else {
                    session.message = L"Signed in. " + session.accountLabel + L" is registered — assign it to a role below.";
                }
                session.message = successMessage;
            } else {
                session.status = L"failed";
                session.message = registerResult.message.empty()
                    ? L"Sign-in finished, but provider registration failed."
                    : registerResult.message;
            }
        }
    }

    std::lock_guard<std::mutex> lock(gCliSignInMutex);
    const auto iterator = gCliSignInSessions.find(sessionId);
    if (iterator == gCliSignInSessions.end()) {
        result.message = L"Unknown sign-in session.";
        return result;
    }

    result.succeeded = true;
    result.status = iterator->second.status;
    result.message = iterator->second.message;
    result.bridge = iterator->second.bridge;
    result.providerId = iterator->second.providerId;
    result.accountLabel = iterator->second.accountLabel;
    return result;
}

std::vector<ShellRuntime::ShellCliSignInDetectEntry> ShellRuntime::DetectCliSignInInstalled() const {
    std::vector<ShellCliSignInDetectEntry> entries;

    for (const auto& bridge : { std::wstring(L"claude"), std::wstring(L"codex") }) {
        ShellCliSignInDetectEntry entry;
        entry.bridge = bridge;
        entry.displayName = cliBridgeDisplayName(bridge);
        entry.installed = resolveCliPath(bridge).has_value();
        const auto authPath = expectedCliAuthFilePath(bridge);
        entry.signedIn = !authPath.empty() && std::filesystem::exists(authPath);
        entries.push_back(entry);
    }
    return entries;
}

ShellOperationResult ShellRuntime::RegisterCliSignedInProvider(
    const std::wstring& bridge,
    const std::wstring& providerId) const {
    if (bridge != L"claude" && bridge != L"codex") {
        return ShellOperationResult{
            false,
            false,
            L"Unsupported CLI bridge."
        };
    }

    if (!resolveCliPath(bridge).has_value()) {
        return ShellOperationResult{
            false,
            false,
            cliBridgeDisplayName(bridge) +
                L" is not installed for this Windows user session. Install it first and try again."
        };
    }

    const auto authPath = expectedCliAuthFilePath(bridge);
    if (authPath.empty() || !std::filesystem::exists(authPath)) {
        return ShellOperationResult{
            false,
            false,
            L"No saved " + cliBridgeDisplayName(bridge) +
                L" sign-in was found for this Windows user. Complete sign-in first."
        };
    }

    JsonObject payload;
    payload.SetNamedValue(L"bridge", JsonValue::CreateStringValue(bridge));
    payload.SetNamedValue(L"providerId", JsonValue::CreateStringValue(providerId));
    return postJsonObjectToAdminApi(
        ResolveConfigurationFile(),
        L"/api/providers/signin/register",
        payload,
        L"Unable to register the signed-in provider through the local admin API.");
}

// Internal: POST /api/setup/dependencies/{id}/install and unpack the
// structured response. Blocks until the backend finishes running the
// catalog's install command (up to its preset timeout, currently 300-600s).
static ShellRuntime::ShellCliDependencyInstallResult postDependencyInstall(
    const std::string& host,
    uint16_t port,
    const std::wstring& dependencyId) {
    ShellRuntime::ShellCliDependencyInstallResult result;

    std::wstring errorMessage;
    const auto path = L"/api/setup/dependencies/" + dependencyId + L"/install";
    const auto response = httpRequest(
        host, port, L"POST", path, std::string("{}"), {}, errorMessage);
    if (!response.has_value()) {
        result.status = L"failed";
        result.summary = errorMessage.empty()
            ? L"Unable to reach the admin API."
            : errorMessage;
        return result;
    }
    const auto body = parseJsonObject(response->body);
    if (!body.has_value()) {
        result.status = L"failed";
        result.summary = L"The admin API returned an unreadable install response.";
        return result;
    }
    result.succeeded = jsonBoolOr(*body, L"succeeded", false);
    result.status = wideFromUtf8(jsonStringOr(*body, L"finalState", "failed"));
    result.summary = wideFromUtf8(jsonStringOr(*body, L"summary", ""));
    result.detail = wideFromUtf8(jsonStringOr(*body, L"detail", ""));
    result.exitCode = static_cast<int>(jsonNumberOr(*body, L"exitCode", -1.0));
    if (body->HasKey(L"postInstallDetection")) {
        const auto detection = body->GetNamedValue(L"postInstallDetection");
        if (detection.ValueType() == JsonValueType::Object) {
            const auto detectionObj = detection.GetObject();
            result.detectedVersion = wideFromUtf8(
                jsonStringOr(detectionObj, L"detectedVersion", ""));
        }
    }
    return result;
}

// Auto-chain install: the user clicks "Install Claude Code CLI", but on a
// fresh machine npm itself may not be on PATH. Detect that case from the
// first install attempt's finalState and detail, install Node.js LTS via
// winget, and retry the original install. This lets the Install button
// work on a clean Windows install without the operator knowing or caring
// that npm is a prerequisite.
ShellRuntime::ShellCliDependencyInstallResult ShellRuntime::InstallCliDependency(
    const std::wstring& bridge) const {
    ShellCliDependencyInstallResult result;
    result.bridge = bridge;

    std::wstring dependencyId;
    if (bridge == L"claude") {
        dependencyId = L"claude-code-cli";
    } else if (bridge == L"codex") {
        dependencyId = L"codex-cli";
    } else {
        result.status = L"failed";
        result.summary = L"Unknown CLI bridge.";
        result.detail = L"Expected 'claude' or 'codex'.";
        return result;
    }

    const auto [host, port] = adminApiEndpoint(ResolveConfigurationFile());

    // First attempt — the happy path on a machine that already has Node.js.
    result = postDependencyInstall(host, port, dependencyId);
    result.bridge = bridge;

    // Prerequisite missing? finalState is "manual-action-required" and the
    // postInstallDetection.preflight is "prerequisite-missing" with a detail
    // that mentions Node.js or npm. Install nodejs via the catalog's winget
    // command, then retry the CLI install.
    const bool prerequisiteMissing =
        !result.succeeded &&
        (result.status == L"manual-action-required" ||
         result.status == L"failed") &&
        (result.summary.find(L"Node.js") != std::wstring::npos ||
         result.summary.find(L"npm") != std::wstring::npos ||
         result.detail.find(L"Node.js") != std::wstring::npos ||
         result.detail.find(L"npm") != std::wstring::npos);

    if (prerequisiteMissing) {
        const auto nodeResult = postDependencyInstall(host, port, L"nodejs");
        if (!nodeResult.succeeded) {
            // Could not install Node.js — surface that to the UI instead of
            // the cryptic "npm not on PATH" from the first attempt. Keep the
            // bridge and treat the overall install as failed.
            result = nodeResult;
            result.bridge = bridge;
            if (result.summary.empty()) {
                result.summary = L"Could not install Node.js (required prerequisite).";
            }
            return result;
        }
        // Node.js installed. Retry the CLI install — this time npm should be
        // on PATH and `npm install -g` will succeed.
        result = postDependencyInstall(host, port, dependencyId);
        result.bridge = bridge;
        if (result.succeeded && !result.summary.empty()) {
            // Prepend a note so the operator sees Node.js was installed as
            // part of this action, not behind their back.
            result.summary = L"Installed Node.js first, then " + result.summary;
        }
    }
    return result;
}

} // namespace MasterControlShell
