// Master Control Program
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#include "pch.h"

#include "ShellRuntime.h"

#include <ShlObj.h>

namespace MasterControlShell {

namespace {

using namespace winrt;
using namespace Windows::Data::Json;

constexpr wchar_t kServiceName[] = L"MasterControlProgram";
constexpr wchar_t kDataDirectoryOverrideVariable[] = L"MASTERCONTROL_DATA_DIR";

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

    std::wstring value(static_cast<size_t>(required - 1), L'\0');
    GetEnvironmentVariableW(name, value.data(), required);
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
    HINTERNET session = WinHttpOpen(
        L"MasterControlShell/2.0",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);
    if (session == nullptr) {
        errorMessage = L"Unable to initialize WinHTTP.";
        return std::nullopt;
    }

    WinHttpSetTimeouts(session, 2000, 2000, 2000, 3000);

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

ShellProviderConnection providerFromJson(const JsonObject& object) {
    ShellProviderConnection provider;
    provider.id = wideFromUtf8(jsonStringOr(object, L"id", ""));
    provider.kind = wideFromUtf8(jsonStringOr(object, L"kind", "generic"));
    provider.displayName = wideFromUtf8(jsonStringOr(object, L"displayName", ""));
    provider.baseUrl = wideFromUtf8(jsonStringOr(object, L"baseUrl", ""));
    provider.enabled = jsonBoolOr(object, L"enabled", true);
    provider.allowAutonomousControl = jsonBoolOr(object, L"allowAutonomousControl", false);
    return provider;
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
    object.SetNamedValue(L"enabled", JsonValue::CreateBooleanValue(provider.enabled));
    object.SetNamedValue(L"allowAutonomousControl", JsonValue::CreateBooleanValue(provider.allowAutonomousControl));
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

std::wstring providerRow(const JsonObject& object) {
    std::wostringstream stream;
    stream << wideFromUtf8(jsonStringOr(object, L"displayName", "provider"))
           << L"  |  "
           << wideFromUtf8(jsonStringOr(object, L"baseUrl", ""))
           << L"  |  autonomous="
           << boolLabel(jsonBoolOr(object, L"allowAutonomousControl"));
    return stream.str();
}

std::wstring providerConnectionRow(const ShellProviderConnection& provider) {
    std::wostringstream stream;
    stream << (provider.displayName.empty() ? provider.id : provider.displayName)
           << L"  |  "
           << provider.baseUrl
           << L"  |  autonomous="
           << boolLabel(provider.allowAutonomousControl);
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

    std::string bindAddress = "0.0.0.0";
    uint16_t browserPort = 7300;
    std::string environmentName = "Pending service snapshot";
    std::string preferredBindAddress = "127.0.0.1";
    std::string macAddress = "n/a";
    bool beaconEnabled = true;
    bool aiAutonomyEnabled = false;
    bool securityProtocolsEnabled = true;
    bool openLanAccess = true;
    ShellSecuritySettings securitySettings;
    std::vector<ShellProviderConnection> providers;
    int cpuPercent = 50;
    int memoryPercent = 50;
    int bandwidthPercent = 50;
    int storagePercent = 50;

    if (const auto configurationText = readFileUtf8(configurationFile); configurationText.has_value()) {
        if (const auto configuration = parseJsonObject(*configurationText); configuration.has_value()) {
            bindAddress = jsonStringOr(*configuration, L"bindAddress", bindAddress);
            browserPort = static_cast<uint16_t>(jsonNumberOr(*configuration, L"browserPort", browserPort));

            if (configuration->HasKey(L"activeProfile")) {
                const auto profile = configuration->GetNamedObject(L"activeProfile", JsonObject());
                environmentName = jsonStringOr(profile, L"environmentName", environmentName);
                preferredBindAddress = jsonStringOr(profile, L"preferredBindAddress", preferredBindAddress);
                macAddress = jsonStringOr(profile, L"macAddress", macAddress);
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

            if (configuration->HasKey(L"providers")) {
                for (const auto& value : configuration->GetNamedArray(L"providers", JsonArray())) {
                    if (value.ValueType() == JsonValueType::Object) {
                        providers.push_back(providerFromJson(value.GetObject()));
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
    std::vector<std::wstring> installRows;
    std::vector<std::wstring> exportRows;

    if (service.state == ServiceState::Running || service.state == ServiceState::StartPending) {
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
                appendJsonArrayRows(
                    dashboardJson->GetNamedArray(L"providers", JsonArray()),
                    providerRow,
                    providerRows);
                appendJsonArrayRows(
                    dashboardJson->GetNamedArray(L"installHistory", JsonArray()),
                    installRow,
                    installRows);
                appendJsonArrayRows(
                    dashboardJson->GetNamedArray(L"exports", JsonArray()),
                    exportRow,
                    exportRows);

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
        } else {
            snapshot.statusMessage = L"Local admin API is not responding.";
        }
    } else if (service.state == ServiceState::Missing) {
        snapshot.statusMessage = L"Service is not installed. Use the bootstrapper to install the host.";
    } else {
        snapshot.statusMessage = L"Service is not running. Start it to refresh live status.";
    }

    if (providerRows.empty() && !providers.empty()) {
        for (const auto& provider : providers) {
            providerRows.push_back(providerConnectionRow(provider));
        }
    }

    snapshot.endpointCount = endpointRows.size();
    snapshot.providerCount = providers.empty() ? providerRows.size() : providers.size();
    snapshot.installCount = installRows.size();
    snapshot.exportCount = exportRows.size();

    if (endpointRows.empty()) {
        endpointRows.push_back(L"No endpoint snapshot is available yet.");
    }
    if (providerRows.empty()) {
        providerRows.push_back(L"No provider connections have been loaded yet.");
    }
    if (installRows.empty()) {
        installRows.push_back(L"No installer provenance has been recorded yet.");
    }
    if (exportRows.empty()) {
        exportRows.push_back(L"No export artifacts are currently published.");
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
    configurationStream << L"Browser port: " << browserPort << L'\n'
                        << L"Bind address: " << wideFromUtf8(bindAddress) << L'\n'
                        << L"Beacon enabled: " << boolLabel(beaconEnabled) << L'\n'
                        << L"AI autonomy: " << boolLabel(aiAutonomyEnabled) << L'\n'
                        << L"Security protocols: " << boolLabel(securityProtocolsEnabled) << L'\n'
                        << L"Open LAN access: " << boolLabel(openLanAccess) << L'\n'
                        << L"Resource envelope: CPU " << cpuPercent
                        << L"% | RAM " << memoryPercent
                        << L"% | Bandwidth " << bandwidthPercent
                        << L"% | Storage " << storagePercent << L'%';

    snapshot.overviewText = overviewStream.str();
    snapshot.telemetryText = telemetryText;
    snapshot.environmentText = environmentStream.str();
    snapshot.configurationText = configurationStream.str();
    snapshot.browserPort = browserPort;
    snapshot.beaconEnabled = beaconEnabled;
    snapshot.aiAutonomyEnabled = aiAutonomyEnabled;
    snapshot.securityProtocolsEnabled = securityProtocolsEnabled;
    snapshot.openLanAccess = openLanAccess;
    snapshot.securitySettings = std::move(securitySettings);
    snapshot.cpuAllocationPercent = cpuPercent;
    snapshot.memoryAllocationPercent = memoryPercent;
    snapshot.bandwidthAllocationPercent = bandwidthPercent;
    snapshot.storageAllocationPercent = storagePercent;
    snapshot.environmentName = wideFromUtf8(environmentName);
    snapshot.hostName = hostName;
    snapshot.operatingSystem = operatingSystem;
    snapshot.primaryIpAddress = ipAddress;
    snapshot.primaryMacAddress = wideFromUtf8(macAddress);
    snapshot.bindAddress = wideFromUtf8(bindAddress);
    snapshot.providers = std::move(providers);
    snapshot.navigationPointers = std::move(navigationPointers);
    snapshot.toolbarItems = std::move(toolbarItems);
    snapshot.overlayRoutes = std::move(overlayRoutes);
    snapshot.viewInjectionsBySlot = std::move(viewInjectionsBySlot);
    snapshot.endpointRows = std::move(endpointRows);
    snapshot.providerRows = std::move(providerRows);
    snapshot.installRows = std::move(installRows);
    snapshot.exportRows = std::move(exportRows);
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
        message = L"Master Control Program service is not installed.";
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
        message = L"Unable to start the Master Control Program service.";
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
        message = L"Master Control Program service is not installed.";
        return false;
    }

    SERVICE_STATUS status{};
    const bool stopped = ControlService(service, SERVICE_CONTROL_STOP, &status) != 0 || GetLastError() == ERROR_SERVICE_NOT_ACTIVE;
    message = stopped ? L"Service stop requested." : L"Unable to stop the Master Control Program service.";

    CloseServiceHandle(service);
    CloseServiceHandle(scm);
    return stopped;
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

    return programDataDirectory() / "MasterControlProgram";
}

std::filesystem::path ShellRuntime::ResolveConfigurationFile() const {
    return ResolveDataDirectory() / "config" / "master-control-program.json";
}

std::filesystem::path ShellRuntime::ResolveExportsDirectory() const {
    return ResolveDataDirectory() / "exports";
}

} // namespace MasterControlShell
