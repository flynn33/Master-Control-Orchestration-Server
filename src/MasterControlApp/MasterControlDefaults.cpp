// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#include "MasterControl/MasterControlDefaults.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <iphlpapi.h>
#include <ShlObj.h>
#include <Windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <iomanip>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace MasterControl {

namespace {

constexpr wchar_t kDataDirectoryOverrideVariable[] = L"MASTERCONTROL_DATA_DIR";
constexpr wchar_t kResourceDirectoryOverrideVariable[] = L"MASTERCONTROL_RESOURCE_DIR";
constexpr wchar_t kCurrentDataDirectoryLeaf[] = L"MasterControlOrchestrationServer";
// Preserve legacy leaves so upgraded installs can still discover older data/share layouts.
constexpr wchar_t kLegacyDataDirectoryLeaf[] = L"MasterControlProgram";
constexpr wchar_t kCurrentShareLeaf[] = L"MasterControlOrchestrationServer";
constexpr wchar_t kLegacyShareLeaf[] = L"MasterControlProgram";

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

std::optional<std::wstring> wideEnvironmentVariable(const wchar_t* name) {
    const DWORD required = GetEnvironmentVariableW(name, nullptr, 0);
    if (required == 0) {
        return std::nullopt;
    }

    std::wstring value(static_cast<size_t>(required - 1), L'\0');
    GetEnvironmentVariableW(name, value.data(), required);
    return value;
}

std::filesystem::path currentExecutableDirectory() {
    std::array<wchar_t, MAX_PATH> buffer{};
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0) {
        throw std::runtime_error("GetModuleFileNameW failed");
    }
    return std::filesystem::path(buffer.data()).parent_path();
}

std::filesystem::path programDataDirectory() {
    PWSTR path = nullptr;
    const HRESULT result = SHGetKnownFolderPath(FOLDERID_ProgramData, KF_FLAG_DEFAULT, nullptr, &path);
    if (FAILED(result) || path == nullptr) {
        throw std::runtime_error("Failed to resolve ProgramData directory");
    }

    std::filesystem::path output(path);
    CoTaskMemFree(path);
    return output;
}

std::string readComputerName() {
    std::array<wchar_t, MAX_COMPUTERNAME_LENGTH + 1> buffer{};
    DWORD size = static_cast<DWORD>(buffer.size());
    if (GetComputerNameExW(ComputerNameDnsHostname, buffer.data(), &size) == 0 || size == 0) {
        size = static_cast<DWORD>(buffer.size());
        if (GetComputerNameW(buffer.data(), &size) == 0 || size == 0) {
            return "MASTER-CONTROL";
        }
    }

    return utf8FromWide(std::wstring(buffer.data(), size));
}

std::optional<std::wstring> readRegistryString(const wchar_t* subKey, const wchar_t* valueName) {
    DWORD type = 0;
    DWORD bytes = 0;
    LONG result = RegGetValueW(
        HKEY_LOCAL_MACHINE,
        subKey,
        valueName,
        RRF_RT_REG_SZ,
        &type,
        nullptr,
        &bytes);
    if (result != ERROR_SUCCESS || bytes == 0) {
        return std::nullopt;
    }

    std::wstring value(bytes / sizeof(wchar_t), L'\0');
    result = RegGetValueW(
        HKEY_LOCAL_MACHINE,
        subKey,
        valueName,
        RRF_RT_REG_SZ,
        &type,
        value.data(),
        &bytes);
    if (result != ERROR_SUCCESS || value.empty()) {
        return std::nullopt;
    }

    value.resize(wcsnlen(value.c_str(), value.size()));
    return value;
}

std::string readOperatingSystemDescription() {
    constexpr wchar_t kCurrentVersionKey[] = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";

    const auto productName = readRegistryString(kCurrentVersionKey, L"ProductName");
    const auto displayVersion = readRegistryString(kCurrentVersionKey, L"DisplayVersion");
    const auto buildNumber = readRegistryString(kCurrentVersionKey, L"CurrentBuildNumber");

    std::ostringstream description;
    description << (productName.has_value() ? utf8FromWide(*productName) : "Windows");
    if (displayVersion.has_value() && !displayVersion->empty()) {
        description << ' ' << utf8FromWide(*displayVersion);
    }
    if (buildNumber.has_value() && !buildNumber->empty()) {
        description << " (build " << utf8FromWide(*buildNumber) << ')';
    }
    return description.str();
}

struct NetworkIdentity final {
    std::string ipAddress = "127.0.0.1";
    std::string macAddress;
};

RuntimeEndpoint makeEndpoint(const std::string& id,
                             const std::string& displayName,
                             EndpointKind kind,
                             const std::string& host,
                             uint16_t port,
                             const std::string& routePath,
                             const std::string& description);

NetworkIdentity detectPrimaryNetworkIdentity() {
    ULONG bufferLength = 16 * 1024;
    std::vector<unsigned char> buffer(bufferLength);
    IP_ADAPTER_ADDRESSES* addresses = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());

    DWORD result = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, nullptr, addresses, &bufferLength);
    if (result == ERROR_BUFFER_OVERFLOW) {
        buffer.resize(bufferLength);
        addresses = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
        result = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, nullptr, addresses, &bufferLength);
    }

    if (result != NO_ERROR) {
        return {};
    }

    for (auto* adapter = addresses; adapter != nullptr; adapter = adapter->Next) {
        if (adapter->OperStatus != IfOperStatusUp || adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK) {
            continue;
        }

        NetworkIdentity identity;
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
                identity.ipAddress = host;
                break;
            }
        }

        if (adapter->PhysicalAddressLength > 0U) {
            std::ostringstream macStream;
            for (ULONG index = 0; index < adapter->PhysicalAddressLength; ++index) {
                if (index > 0U) {
                    macStream << ':';
                }
                macStream << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
                          << static_cast<unsigned int>(adapter->PhysicalAddress[index]);
            }
            identity.macAddress = macStream.str();
        }

        return identity;
    }

    return {};
}

std::vector<RuntimeEndpoint> buildDefaultSeededEndpointsForHost(std::string host) {
    if (host.empty()) {
        host = "127.0.0.1";
    }

    std::vector<RuntimeEndpoint> endpoints;

    endpoints.push_back(makeEndpoint("platform-gateway", "Platform Gateway", EndpointKind::Gateway, host, 7200, "/health", "Unified orchestration gateway"));
    endpoints.push_back(makeEndpoint("browser-gateway", "Browser Gateway", EndpointKind::BrowserGateway, host, 7300, "/", "Master Control Orchestration Server browser surface"));
    endpoints.push_back(makeEndpoint("client-tracker", "Client Tracker", EndpointKind::MCPServer, host, 7120, "/api/clients", "LAN client tracker"));
    endpoints.push_back(makeEndpoint("metrics", "Metrics", EndpointKind::MCPServer, host, 7121, "/api/metrics", "Host metrics feed"));

    const std::array<std::pair<const char*, uint16_t>, 18> orchestrationServers = {{
        { "repo-search", 7101 }, { "docs-search", 7102 }, { "fs-cache", 7103 }, { "build-cache", 7104 },
        { "symbol-index", 7105 }, { "session-context", 7106 }, { "response-cache", 7107 }, { "git-intel", 7108 },
        { "file-digest", 7109 }, { "vector-search", 7110 }, { "dep-graph", 7111 }, { "lint-cache", 7112 },
        { "snippet-store", 7113 }, { "task-queue", 7114 }, { "memory", 7115 }, { "agent-comm", 7116 },
        { "coordination", 7117 }, { "event-bus", 7118 }
    }};

    for (const auto& [name, port] : orchestrationServers) {
        endpoints.push_back(makeEndpoint(name, name, EndpointKind::MCPServer, host, port, "/", "Managed orchestration MCP server"));
    }

    const std::array<std::pair<const char*, uint16_t>, 7> agents = {{
        { "sentinel", 7201 }, { "architect", 7202 }, { "forge", 7203 }, { "scribe", 7204 },
        { "recon", 7205 }, { "nexus", 7206 }, { "watchtower", 7207 }
    }};

    for (const auto& [name, port] : agents) {
        endpoints.push_back(makeEndpoint(name, name, EndpointKind::SubAgent, host, port, "/", "Managed orchestration sub-agent"));
    }

    return endpoints;
}

RuntimeEndpoint makeEndpoint(const std::string& id,
                             const std::string& displayName,
                             EndpointKind kind,
                             const std::string& host,
                             uint16_t port,
                             const std::string& routePath,
                             const std::string& description) {
    RuntimeEndpoint endpoint;
    endpoint.id = id;
    endpoint.displayName = displayName;
    endpoint.kind = kind;
    endpoint.host = host;
    endpoint.port = port;
    endpoint.protocol = "http";
    endpoint.status = EndpointStatus::Unknown;
    endpoint.routePath = routePath;
    endpoint.description = description;
    endpoint.lastCheckedUtc = "";
    return endpoint;
}

} // namespace

DiscoveredEnvironment detectLocalEnvironment() {
    WSADATA data{};
    const bool winsockReady = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    const auto networkIdentity = winsockReady ? detectPrimaryNetworkIdentity() : NetworkIdentity{};
    if (winsockReady) {
        WSACleanup();
    }

    return DiscoveredEnvironment{
        readComputerName(),
        readOperatingSystemDescription(),
        networkIdentity.ipAddress.empty() ? "127.0.0.1" : networkIdentity.ipAddress,
        networkIdentity.macAddress
    };
}

AppPaths resolveAppPaths() {
    const auto executableDirectory = currentExecutableDirectory();
    const auto dataDirectoryOverride = wideEnvironmentVariable(kDataDirectoryOverrideVariable);
    const auto defaultDataDirectory = programDataDirectory() / kCurrentDataDirectoryLeaf;
    const auto legacyDataDirectory = programDataDirectory() / kLegacyDataDirectoryLeaf;
    const auto dataDirectory = dataDirectoryOverride.has_value()
        ? std::filesystem::path(*dataDirectoryOverride)
        : (!std::filesystem::exists(defaultDataDirectory) && std::filesystem::exists(legacyDataDirectory)
            ? legacyDataDirectory
            : defaultDataDirectory);
    const auto installHistoryFile = dataDirectory / "state" / "install-history.json";
    const auto appleOperationHistoryFile = dataDirectory / "state" / "apple-operations.json";
    const auto entitlementsFile = dataDirectory / "state" / "entitlements.json";
    const auto providerCredentialsFile = dataDirectory / "state" / "provider-credentials.json";
    const auto currentConfigurationFile = dataDirectory / "config" / "master-control-orchestration-server.json";
    const auto legacyConfigurationFile = dataDirectory / "config" / "master-control-program.json";
    const auto configurationFile = !std::filesystem::exists(currentConfigurationFile) && std::filesystem::exists(legacyConfigurationFile)
        ? legacyConfigurationFile
        : currentConfigurationFile;
    const auto workDirectory = dataDirectory / "work";

std::filesystem::path manifestsDirectory;
std::filesystem::path webRootDirectory;
std::filesystem::path cluProfileFile;

    if (const auto resourceDirectory = wideEnvironmentVariable(kResourceDirectoryOverrideVariable); resourceDirectory.has_value()) {
        manifestsDirectory = std::filesystem::path(*resourceDirectory) / "ForsettiManifests";
        webRootDirectory = std::filesystem::path(*resourceDirectory) / "web";
        cluProfileFile = std::filesystem::path(*resourceDirectory) / "clu" / "governance-profile.json";
    } else {
        const auto currentShareRoot = executableDirectory / "share" / kCurrentShareLeaf;
        const auto legacyShareRoot = executableDirectory / "share" / kLegacyShareLeaf;
        const auto shareRoot =
            (std::filesystem::exists(currentShareRoot / "ForsettiManifests") || std::filesystem::exists(currentShareRoot / "web"))
                ? currentShareRoot
                : legacyShareRoot;
        manifestsDirectory = shareRoot / "ForsettiManifests";
        webRootDirectory = shareRoot / "web";
        cluProfileFile = shareRoot / "clu" / "governance-profile.json";
    }

    if (!std::filesystem::exists(manifestsDirectory)) {
        manifestsDirectory = std::filesystem::path(MASTERCONTROL_SOURCE_MODULE_MANIFESTS_DIR);
    }
    if (!std::filesystem::exists(webRootDirectory)) {
        webRootDirectory = std::filesystem::path(MASTERCONTROL_SOURCE_WEB_RESOURCES_DIR);
    }
    if (!std::filesystem::exists(cluProfileFile)) {
        cluProfileFile = std::filesystem::path(MASTERCONTROL_SOURCE_CLU_RESOURCES_DIR) / "governance-profile.json";
    }

    std::filesystem::create_directories(dataDirectory / "state");
    std::filesystem::create_directories(dataDirectory / "config");
    std::filesystem::create_directories(workDirectory);

    return AppPaths{
        executableDirectory,
        dataDirectory,
        configurationFile,
        installHistoryFile,
        appleOperationHistoryFile,
        entitlementsFile,
        providerCredentialsFile,
        manifestsDirectory,
        webRootDirectory,
        cluProfileFile,
        workDirectory
    };
}

std::vector<RuntimeEndpoint> buildDefaultSeededEndpoints() {
    return buildDefaultSeededEndpointsForHost(detectLocalEnvironment().preferredBindAddress);
}

std::vector<ProviderConnection> buildDefaultProviders() {
    return {
        ProviderConnection{ "codex", ProviderKind::Codex, "Codex", "https://api.openai.com/v1", "gpt-5.4", true, false, false },
        ProviderConnection{ "claude-code", ProviderKind::ClaudeCode, "Claude Code", "https://api.anthropic.com", "", true, false, false },
        ProviderConnection{ "xai-grok", ProviderKind::XAI, "xAI / Grok", "https://api.x.ai/v1", "grok-code-fast-1", true, false, false }
    };
}

AppConfiguration buildDefaultConfiguration() {
    const auto environment = detectLocalEnvironment();

    AppConfiguration configuration;
    configuration.instanceName = "Master Control Orchestration Server";
    configuration.bindAddress = "0.0.0.0";
    configuration.browserPort = 7300;
    configuration.beaconPort = 7301;
    configuration.beaconBroadcastIntervalSeconds = 15;
    configuration.beaconEnabled = true;
    configuration.aiAutonomyEnabled = false;
    configuration.security.enableTls = false;
    configuration.security.enableAuthentication = false;
    configuration.security.allowTroubleshootingBypass = true;
    configuration.security.allowOpenLanAccess = true;
    configuration.security.securityProtocolsEnabled = true;
    configuration.providers = buildDefaultProviders();
    configuration.activeProfile.environmentName = environment.hostName + " - " + environment.operatingSystem;
    configuration.activeProfile.preferredBindAddress = environment.preferredBindAddress;
    configuration.activeProfile.macAddress = environment.macAddress;
    configuration.activeProfile.seededEndpoints = buildDefaultSeededEndpointsForHost(environment.preferredBindAddress);
    return configuration;
}

std::string executableDirectoryUtf8() {
    return utf8FromWide(currentExecutableDirectory().wstring());
}

} // namespace MasterControl
