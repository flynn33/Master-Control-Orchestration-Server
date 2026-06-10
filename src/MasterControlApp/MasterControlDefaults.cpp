// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#include "MasterControl/CapabilityAuthorization.h"
#include "MasterControl/MasterControlDefaults.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <iphlpapi.h>
#include <ShlObj.h>
#include <Windows.h>
#include <rpc.h>

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
    // v0.9.3: ipAddress now defaults to empty (was "127.0.0.1"). The
    // detector below uses .empty() to decide whether each pass succeeded;
    // a non-empty default short-circuited pass 1 entirely, so on every
    // dual-stack host the bootstrapper wrote "preferredBindAddress=
    // 127.0.0.1" into the freshly-installed config and LAN clients
    // could only reach MCOS over loopback. The caller in
    // detectLocalEnvironment substitutes "127.0.0.1" downstream when
    // ipAddress is empty, so the operator-facing fallback is preserved.
    std::string ipAddress;
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

    // v0.9.3: IPv4-first preference (mirrors the runtime's
    // WindowsHostTelemetryService::readPrimaryNetworkIdentity fix).
    // Pre-v0.9.3 this took whatever Windows surfaced first, which is
    // typically the IPv6 ULA on dual-stack hosts -- so the bootstrapper
    // wrote "preferredBindAddress=fde3:..." into the freshly-installed
    // config, and even though every later URL builder bracketed the
    // IPv6 properly, the admin port (bound to 0.0.0.0 / IPv4 only) was
    // unreachable on that advertised IPv6. The two-pass walk takes a
    // routable IPv4 first; only if no IPv4 exists does it fall through
    // to routable IPv6, then link-local as last resort.
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
    auto isLinkLocalIpv4 = [](const std::string& s) { return s.rfind("169.254.", 0) == 0; };
    auto isLinkLocalIpv6 = [](const std::string& s) {
        if (s.size() < 4) return false;
        const char a = static_cast<char>(std::tolower(static_cast<unsigned char>(s[0])));
        const char b = static_cast<char>(std::tolower(static_cast<unsigned char>(s[1])));
        const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(s[2])));
        const char d = static_cast<char>(std::tolower(static_cast<unsigned char>(s[3])));
        return a == 'f' && b == 'e' && (c == '8' || c == '9' || c == 'a' || c == 'b') && d == '0';
    };
    auto familyOf = [](const SOCKET_ADDRESS& addr) -> int {
        return addr.lpSockaddr ? addr.lpSockaddr->sa_family : 0;
    };

    NetworkIdentity identity;
    // v0.9.3: belt-and-suspenders -- ipAddress default was "127.0.0.1"
    // through v0.9.2, which made the empty()-driven pass progression
    // skip pass 1 entirely. The struct default now starts empty (see
    // the type definition), so this line is documentation more than
    // code, but it pins the contract.
    identity.ipAddress.clear();

    // Pass 1: routable IPv4
    for (auto* adapter = addresses; adapter != nullptr && identity.ipAddress.empty(); adapter = adapter->Next) {
        if (adapter->OperStatus != IfOperStatusUp || adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        for (auto* unicast = adapter->FirstUnicastAddress; unicast != nullptr; unicast = unicast->Next) {
            if (familyOf(unicast->Address) != AF_INET) continue;
            std::string candidate;
            if (!formatHost(unicast->Address, candidate)) continue;
            if (isLinkLocalIpv4(candidate)) continue;
            identity.ipAddress = candidate;
            break;
        }
    }
    // Pass 2: routable IPv6
    if (identity.ipAddress.empty()) {
        for (auto* adapter = addresses; adapter != nullptr && identity.ipAddress.empty(); adapter = adapter->Next) {
            if (adapter->OperStatus != IfOperStatusUp || adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
            for (auto* unicast = adapter->FirstUnicastAddress; unicast != nullptr; unicast = unicast->Next) {
                if (familyOf(unicast->Address) != AF_INET6) continue;
                std::string candidate;
                if (!formatHost(unicast->Address, candidate)) continue;
                if (isLinkLocalIpv6(candidate)) continue;
                // Strip IPv6 zone identifier (the trailing %iface) -- not
                // legal inside an HTTP URL host.
                const auto pct = candidate.find('%');
                if (pct != std::string::npos) candidate.erase(pct);
                identity.ipAddress = candidate;
                break;
            }
        }
    }
    // Pass 3: any address as last resort
    if (identity.ipAddress.empty()) {
        for (auto* adapter = addresses; adapter != nullptr && identity.ipAddress.empty(); adapter = adapter->Next) {
            if (adapter->OperStatus != IfOperStatusUp || adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
            for (auto* unicast = adapter->FirstUnicastAddress; unicast != nullptr; unicast = unicast->Next) {
                std::string candidate;
                if (formatHost(unicast->Address, candidate)) {
                    const auto pct = candidate.find('%');
                    if (pct != std::string::npos) candidate.erase(pct);
                    identity.ipAddress = candidate;
                    break;
                }
            }
        }
    }

    // MAC: take the MAC of the first eligible adapter (matches the IP we
    // already chose if possible; otherwise just any UP non-loopback NIC).
    for (auto* adapter = addresses; adapter != nullptr; adapter = adapter->Next) {
        if (adapter->OperStatus != IfOperStatusUp || adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        bool match = identity.ipAddress.empty();
        for (auto* unicast = adapter->FirstUnicastAddress; unicast != nullptr && !match; unicast = unicast->Next) {
            std::string candidate;
            if (formatHost(unicast->Address, candidate)) {
                const auto pct = candidate.find('%');
                if (pct != std::string::npos) candidate.erase(pct);
                if (candidate == identity.ipAddress) match = true;
            }
        }
        if (!match) continue;
        if (adapter->PhysicalAddressLength > 0U) {
            std::ostringstream macStream;
            for (ULONG index = 0; index < adapter->PhysicalAddressLength; ++index) {
                if (index > 0U) macStream << ':';
                macStream << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
                          << static_cast<unsigned int>(adapter->PhysicalAddress[index]);
            }
            identity.macAddress = macStream.str();
        }
        return identity;
    }

    return identity;
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
    endpoint.status = EndpointStatus::Template;
    endpoint.routePath = routePath;
    endpoint.description = description;
    endpoint.lastCheckedUtc = "";
    endpoint.isTemplate = true;
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

    // One-shot ProgramData migration: if only the legacy "MasterControlProgram"
    // directory exists, attempt to rename it to the canonical product-name path so
    // installs from pre-0.2 builds are invisibly unified. On failure (files in use,
    // permission denied), we fall back to reading from the legacy path — this
    // preserves existing behavior so we never break an upgrade.
    std::error_code migrationError;
    if (!dataDirectoryOverride.has_value() &&
        !std::filesystem::exists(defaultDataDirectory, migrationError) &&
        std::filesystem::exists(legacyDataDirectory, migrationError)) {
        migrationError.clear();
        std::filesystem::rename(legacyDataDirectory, defaultDataDirectory, migrationError);
        // If rename failed, migrationError is set; defaultDataDirectory will not
        // exist below and we'll transparently fall back to legacyDataDirectory.
    }

    const auto dataDirectory = dataDirectoryOverride.has_value()
        ? std::filesystem::path(*dataDirectoryOverride)
        : (!std::filesystem::exists(defaultDataDirectory) && std::filesystem::exists(legacyDataDirectory)
            ? legacyDataDirectory
            : defaultDataDirectory);
    const auto installHistoryFile = dataDirectory / "state" / "install-history.json";
    const auto appleOperationHistoryFile = dataDirectory / "state" / "apple-operations.json";
    const auto entitlementsFile = dataDirectory / "state" / "entitlements.json";
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

    // PHASE-05: vendored Forsetti instructions live alongside the source
    // tree at compile time. The path is recorded so GovernanceBundleService
    // can read forsettiFrameworkVersion at request time without re-deriving
    // the vendor layout. Production deployments that ship the file under
    // <executable>/share/forsetti/ can override via the resource dir, but
    // the source path is the canonical fallback per ADR-001 vendoring rules.
    auto forsettiInstructionsFile = std::filesystem::path(MASTERCONTROL_SOURCE_FORSETTI_INSTRUCTIONS_FILE);

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
        manifestsDirectory,
        webRootDirectory,
        cluProfileFile,
        workDirectory,
        forsettiInstructionsFile
    };
}

std::vector<RuntimeEndpoint> buildDefaultSeededEndpointsForHost(std::string host) {
    if (host.empty()) {
        host = "127.0.0.1";
    }

    std::vector<RuntimeEndpoint> endpoints;

    // v0.11.0-alpha.3: the gateway row now points at the native HTTP.sys
    // adapter (cfg.mcpGateway.listenPort default 8080, healthPath
    // /health). The previous seed advertised port 7200 -- the legacy
    // external gateway retired at v0.9.0 -- so the row could never probe
    // reachable and the Exports surface derived a dead client URL from
    // it. Existing installs keep their persisted endpoint list; this
    // only affects fresh configurations.
    endpoints.push_back(makeEndpoint("platform-gateway", "Platform Gateway", EndpointKind::Gateway, host, 8080, "/health", "Native HTTP.sys MCP gateway"));
    endpoints.push_back(makeEndpoint("browser-gateway", "Browser Gateway", EndpointKind::BrowserGateway, host, 7300, "/", "Master Control Orchestration Server browser surface"));
    endpoints.push_back(makeEndpoint("client-tracker", "Client Tracker", EndpointKind::MCPServer, host, 7120, "/api/clients", "LAN client tracker"));
    endpoints.push_back(makeEndpoint("metrics", "Metrics", EndpointKind::MCPServer, host, 7121, "/api/metrics", "Host metrics feed"));

    // v0.8.4: minimum baseline MCP server catalog. Operator-specified
    // 23-server set replacing the v0.7.x placeholder names (repo-search /
    // docs-search / fs-cache / etc.). Each entry is the host:port the
    // server WILL listen on once the supervised worker is registered
    // against it -- the inventory is operator-readable advertisement
    // even when no listener exists yet, exactly like the sub-agent
    // catalog. id is kebab-case for stable identifier; displayName is
    // the human-readable label that lands on the card's title row;
    // specialization is the category the operator scans by; description
    // is the muted body line. Reachability dot will paint red until a
    // real worker process binds to the listed port.
    // v0.10.0: Docker Control MCP and Playwright MCP removed at operator
    // direction. The Windows-native MCOS hard rule already classed
    // Docker as development-only, not the Windows production path. The container surface now lives outside the MCOS
    // baseline; operators who need container automation register a
    // pool template explicitly. Playwright was a vendor-specific
    // browser-automation seed that overlapped with chrome-devtools and
    // generic computer-use; its removal aligns the catalog with the
    // intended LAN MCP Gateway scope and shrinks the seed set from 23
    // to 21.
    struct McpSeed { const char* id; const char* displayName; uint16_t port; const char* specialization; const char* description; };
    const std::array<McpSeed, 21> baselineMcpCatalog = {{
        { "filesystem",                 "Filesystem MCP",                 7101, "filesystem",     "Read/write files and directories under operator-allowed roots." },
        { "memory",                     "Memory MCP",                     7102, "memory",         "Short-term scratch memory shared across MCP-driven sessions." },
        { "sequential-thinking",        "Sequential Thinking MCP",        7103, "reasoning",      "Step-by-step planning + reasoning trace surface for agent loops." },
        { "computer-use",               "Computer Use MCP",               7104, "automation",     "Anthropic-style computer-use primitives: screenshot, click, type, scroll." },
        { "desktop-control",            "Desktop Control MCP",            7105, "automation",     "Higher-level desktop control: window management, focus, app launch." },
        { "chrome-devtools",            "Chrome DevTools MCP",            7107, "browser",        "Chrome DevTools Protocol surface: DOM, network, console, performance." },
        { "terminal-shell",             "Terminal / Shell MCP",           7108, "shell",          "Execute shell commands in supervised PowerShell / cmd / bash sessions." },
        { "local-git",                  "Local Git MCP",                  7109, "vcs",            "Local git operations: status, diff, commit, branch, log, blame." },
        { "sqlite",                     "SQLite MCP",                     7110, "database",       "Direct SQLite query surface against operator-attached *.sqlite files." },
        { "local-database",             "Local Database MCP",             7111, "database",       "Generic local DB driver (Postgres / MySQL / SQL Server) over operator-supplied connection strings." },
        { "file-search",                "File Search MCP",                7113, "filesystem",     "Indexed full-text search across operator-allowed file roots (ripgrep-backed)." },
        { "code-execution-repl",        "Code Execution / REPL MCP",      7114, "execution",      "Sandboxed Python / Node / PowerShell REPL for ad-hoc code evaluation." },
        { "local-test-runner",          "Local Test Runner MCP",          7115, "testing",        "Run pytest / jest / ctest / dotnet-test in the operator's repo and stream results." },
        { "screen-capture-vision",      "Screen Capture / Vision MCP",    7116, "vision",         "Capture screen regions and run vision OCR / object detection locally." },
        { "keyboard-mouse-control",     "Keyboard & Mouse Control MCP",   7117, "automation",     "Low-level input synthesis (SendInput) for keystrokes, mouse, hotkeys." },
        { "persistent-context",         "Persistent Context MCP",         7118, "memory",         "Long-term cross-session memory persisted to disk." },
        { "local-build-tool",           "Local Build Tool MCP",           7119, "build",          "Drive cmake / msbuild / cargo / gradle / npm / dotnet build with structured output." },
        { "local-indexer",              "Local Indexer MCP",              7122, "indexer",        "Project-wide symbol + reference index (tree-sitter / clangd / pyright)." },
        { "file-watcher",               "File Watcher MCP",               7123, "filesystem",     "Watch operator-specified paths for create/modify/delete and stream change events." },
        { "knowledge-graph",            "Knowledge Graph MCP",            7124, "memory",         "Triple-store knowledge graph over operator-imported entities + relations." },
        { "local-linter",               "Local Linter MCP",               7125, "quality",        "Run eslint / pylint / clang-tidy / ruff against operator-specified files." },
    }};

    for (const auto& seed : baselineMcpCatalog) {
        auto endpoint = makeEndpoint(seed.id, seed.displayName, EndpointKind::MCPServer,
                                     host, seed.port, "/", seed.description);
        endpoint.specialization = seed.specialization;
        endpoint.requiredCapabilities = requiredCapabilitiesForMcpTool(seed.id, "");
        endpoint.risk = highestCapabilityRisk(endpoint.requiredCapabilities);
        endpoint.highRisk = !endpoint.requiredCapabilities.empty();
        endpoints.push_back(std::move(endpoint));
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

std::vector<RuntimeEndpoint> buildDefaultSeededEndpoints() {
    return buildDefaultSeededEndpointsForHost(detectLocalEnvironment().preferredBindAddress);
}

namespace {

// PHASE-03 (ADR-002 §4): generate a stable instance id for the discovery
// document. Uses Win32 UuidCreate (rpcrt4) and lowercases the canonical
// 36-char form. Operators can override by editing AppConfiguration.instanceId
// in mcos.json — the field round-trips like every other configuration key.
std::string generateInstanceIdUtf8() {
    UUID uuid{};
    if (UuidCreate(&uuid) != RPC_S_OK) {
        return "";
    }
    RPC_CSTR uuidString = nullptr;
    if (UuidToStringA(&uuid, &uuidString) != RPC_S_OK || uuidString == nullptr) {
        return "";
    }
    std::string canonical(reinterpret_cast<char*>(uuidString));
    RpcStringFreeA(&uuidString);
    std::transform(canonical.begin(), canonical.end(), canonical.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return "mcos-" + canonical;
}

} // namespace

AppConfiguration buildDefaultConfiguration() {
    const auto environment = detectLocalEnvironment();

    AppConfiguration configuration;
    configuration.instanceName = "Master Control Orchestration Server";
    configuration.instanceId = generateInstanceIdUtf8();
    configuration.bindAddress = "127.0.0.1";
    configuration.browserPort = 7300;
    configuration.beaconPort = 7301;
    configuration.beaconBroadcastIntervalSeconds = 15;
    configuration.beaconEnabled = false;
    configuration.aiAutonomyEnabled = false;
    configuration.security.enableTls = false;
    configuration.security.enableAuthentication = true;
    configuration.security.allowTroubleshootingBypass = false;
    configuration.security.allowOpenLanAccess = false;
    configuration.security.securityProtocolsEnabled = true;
    configuration.security.securityPosture = "local-only";
    configuration.activeProfile.environmentName = environment.hostName + " - " + environment.operatingSystem;
    configuration.activeProfile.preferredBindAddress = environment.preferredBindAddress;
    configuration.activeProfile.macAddress = environment.macAddress;
    configuration.activeProfile.seededEndpoints = buildDefaultSeededEndpointsForHost(environment.preferredBindAddress);

    // PHASE-02: MCP Gateway is configured (defaults populated) but disabled.
    // Operators flip `enabled` to true once an gateway binary is installed
    // and the gateway port (default 8080) is reachable from LAN clients.
    // The gateway URL is logically distinct from the admin port (7300).
    // v0.9.0: the in-process HTTP.sys adapter support dropped, native HTTP.sys substrate is the
    // only option. Fresh installs keep the gateway local/off until setup
    // explicitly validates LAN posture. binaryPath /
    // databasePath stay set to empty for back-compat schema rather than
    // being silently dropped.
    configuration.mcpGateway.type = GatewayType::Native;
    configuration.mcpGateway.enabled = false;
    configuration.mcpGateway.binaryPath = "";
    configuration.mcpGateway.listenHost = "127.0.0.1";
    configuration.mcpGateway.listenPort = 8080;
    configuration.mcpGateway.mcpPath = "/mcp";
    configuration.mcpGateway.healthPath = "/health";
    configuration.mcpGateway.databasePath = "";
    configuration.mcpGateway.mode = "local-only";

    return configuration;
}

std::string executableDirectoryUtf8() {
    return utf8FromWide(currentExecutableDirectory().wstring());
}

} // namespace MasterControl
