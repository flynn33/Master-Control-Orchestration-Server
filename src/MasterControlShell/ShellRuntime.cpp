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

std::optional<HttpResponse> httpGet(const std::string& host,
                                    const INTERNET_PORT port,
                                    const std::wstring& path,
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
        L"GET",
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

    if (WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) == 0 ||
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

std::string dashboardHostFromBindAddress(const std::string& bindAddress) {
    return bindAddress == "0.0.0.0" ? std::string("127.0.0.1") : bindAddress;
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
        }
    }

    snapshot.dashboardUrl = wideFromUtf8("http://" + dashboardHostFromBindAddress(bindAddress) + ":" + std::to_string(browserPort) + "/");

    std::wstring hostName = wideFromUtf8(environmentName);
    std::wstring operatingSystem = L"Service offline";
    std::wstring ipAddress = wideFromUtf8(preferredBindAddress);
    std::wstring telemetryText = L"Live telemetry will appear when the service and local admin API are reachable.";
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

void ShellRuntime::OpenDashboard(const ShellSnapshot& snapshot) const {
    ShellExecuteW(nullptr, L"open", snapshot.dashboardUrl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void ShellRuntime::OpenConfig() const {
    openPathInExplorer(ResolveConfigurationFile());
}

void ShellRuntime::OpenDataDirectory() const {
    openPathInExplorer(ResolveDataDirectory());
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

} // namespace MasterControlShell
