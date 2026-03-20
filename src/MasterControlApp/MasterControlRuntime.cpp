// Master Control Program
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
#include <iphlpapi.h>
#include <urlmon.h>
#include <wincrypt.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>

namespace MasterControl {

namespace {

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

bool startsWith(const std::string& value, const std::string& prefix) {
    return value.rfind(prefix, 0) == 0;
}

bool isRemoteSource(const std::string& source) {
    return startsWith(source, "http://") || startsWith(source, "https://");
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

nlohmann::json readJsonFile(const std::filesystem::path& filePath) {
    std::ifstream stream(filePath);
    if (!stream.is_open()) {
        return nlohmann::json{};
    }

    nlohmann::json json;
    stream >> json;
    return json;
}

void writeJsonFile(const std::filesystem::path& filePath, const nlohmann::json& json) {
    std::filesystem::create_directories(filePath.parent_path());
    std::ofstream stream(filePath, std::ios::trunc);
    stream << json.dump(2);
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
};

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
            "mastercontrol.iap.beacon-gateway"
        }
    };
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
                "Defines the baseline rules for agentic operation inside Master Control Program.",
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
        writeJsonFile(filePath_, state);
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

private:
    void ensureStateFile() const {
        if (!std::filesystem::exists(filePath_)) {
            writeJsonFile(filePath_, defaultState_);
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
        if (std::filesystem::exists(filePath_)) {
            const auto json = readJsonFile(filePath_);
            if (!json.is_null() && !json.empty()) {
                state_->configuration = json.get<AppConfiguration>();
            }
        } else {
            state_->configuration = buildDefaultConfiguration();
            persistLocked();
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

        {
            std::lock_guard<std::mutex> lock(state_->mutex);
            state_->configuration = configuration;
            persistLocked();
        }

        return OperationResult{ true, false, "Configuration updated." };
    }

private:
    void persistLocked() const {
        writeJsonFile(filePath_, state_->configuration);
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
        writeJsonFile(filePath_, state_->configuration);
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

private:
    static EndpointStatus probeEndpoint(const std::string& host, uint16_t port);

    std::shared_ptr<SharedState> state_;
    std::mutex mutex_;
    std::vector<RuntimeEndpoint> endpoints_;
};

void RuntimeInventoryService::refresh() {
    std::vector<RuntimeEndpoint> endpoints;
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        endpoints = state_->configuration.activeProfile.seededEndpoints;
    }

    for (auto& endpoint : endpoints) {
        endpoint.status = probeEndpoint(endpoint.host, endpoint.port);
        endpoint.lastCheckedUtc = timestampNowUtc();
    }

    std::lock_guard<std::mutex> lock(mutex_);
    endpoints_ = std::move(endpoints);
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
        const auto capabilities = providerCatalogService_->listCapabilities();
        const auto capabilityIterator = std::find_if(
            capabilities.begin(),
            capabilities.end(),
            [&provider](const ProviderCapabilityDescriptor& capability) { return capability.kind == provider.kind; });
        if (capabilityIterator == capabilities.end()) {
            return OperationResult{ false, false, "The selected provider kind is not currently supported by an active provider module." };
        }

        std::lock_guard<std::mutex> lock(state_->mutex);
        auto& providers = state_->configuration.providers;
        ProviderConnection normalizedProvider = provider;
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

        writeJsonFile(configurationFile_, state_->configuration);
        return OperationResult{ true, false, "Provider settings updated." };
    }

private:
    std::shared_ptr<SharedState> state_;
    std::filesystem::path configurationFile_;
    std::shared_ptr<IProviderCatalogService> providerCatalogService_;
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
            writeJsonFile(filePath_, ProviderCredentialsDocument{});
        }
    }

    std::vector<ProviderCredentialStatus> listStatuses() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return loadStatusesLocked();
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
        const auto capabilityIterator = std::find_if(
            capabilities.begin(),
            capabilities.end(),
            [providerKind = providerIterator->kind](const ProviderCapabilityDescriptor& capability) {
                return capability.kind == providerKind;
            });
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
        writeJsonFile(filePath_, document);
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

class ProviderAssignmentService final : public IProviderAssignmentService {
public:
    ProviderAssignmentService(std::shared_ptr<SharedState> state,
                              std::filesystem::path configurationFile,
                              std::shared_ptr<IRuntimeInventoryService> inventoryService,
                              std::shared_ptr<IProviderRegistry> providerRegistry)
        : state_(std::move(state))
        , configurationFile_(std::move(configurationFile))
        , inventoryService_(std::move(inventoryService))
        , providerRegistry_(std::move(providerRegistry)) {}

    std::vector<ProviderAssignmentTarget> listTargets() const override {
        auto endpoints = inventoryService_->listEndpoints();
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
            for (const auto& memberTargetId : targetIterator->memberTargetIds) {
                clearTarget(memberTargetId);
                if (!assignment.providerId.empty()) {
                    assignments.push_back(ProviderAssignment{
                        memberTargetId,
                        ProviderAssignmentTargetKind::SubAgent,
                        assignment.providerId,
                        timestampNowUtc()
                    });
                }
            }

            writeJsonFile(configurationFile_, state_->configuration);
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
                timestampNowUtc()
            });
        }

        writeJsonFile(configurationFile_, state_->configuration);
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
    std::shared_ptr<IProviderRegistry> providerRegistry_;
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
    static int executeCommand(const std::wstring& command, const std::filesystem::path& workingDirectory);
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

    const std::wstring extractCommand =
        L"pwsh -NoProfile -ExecutionPolicy Bypass -Command \"Expand-Archive -Path '" +
        zipPath.wstring() + L"' -DestinationPath '" + extractDirectory.wstring() + L"' -Force\"";

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

    writeJsonFile(paths_.configurationFile, state_->configuration);
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
        case InstallerKind::PowerShell:
            command = L"pwsh -NoProfile -ExecutionPolicy Bypass -File \"" + payloadPath.wstring() + L"\" " + wideFromUtf8(arguments);
            break;
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
        command = L"pwsh -NoProfile -ExecutionPolicy Bypass -File \"" + bootstrapPath.wstring() + L"\" " + wideFromUtf8(arguments);
    } else {
        command = L"\"" + bootstrapPath.wstring() + L"\" " + wideFromUtf8(arguments);
    }

    return executeCommand(command, workingDirectory);
}

int InstallerOrchestrator::executeCommand(const std::wstring& command, const std::filesystem::path& workingDirectory) {
    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInformation{};
    std::wstring mutableCommand = command;

    if (CreateProcessW(
            nullptr,
            mutableCommand.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW,
            nullptr,
            workingDirectory.empty() ? nullptr : workingDirectory.wstring().c_str(),
            &startupInfo,
            &processInformation) == 0) {
        return static_cast<int>(GetLastError());
    }

    WaitForSingleObject(processInformation.hProcess, INFINITE);

    DWORD exitCode = 1;
    GetExitCodeProcess(processInformation.hProcess, &exitCode);
    CloseHandle(processInformation.hThread);
    CloseHandle(processInformation.hProcess);
    return static_cast<int>(exitCode);
}

void InstallerOrchestrator::recordHistory(const InstallProvenance& provenance) {
    std::lock_guard<std::mutex> lock(state_->mutex);
    state_->installHistory.push_back(provenance);
    writeJsonFile(paths_.installHistoryFile, state_->installHistory);
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
                return endpoint.id == "aggregator-gateway" || endpoint.kind == EndpointKind::Gateway;
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
                            std::shared_ptr<IExportService> exportService)
        : profile_(loadProfile(std::move(profileFile)))
        , configurationService_(std::move(configurationService))
        , providerRegistry_(std::move(providerRegistry))
        , providerAssignmentService_(std::move(providerAssignmentService))
        , installerOrchestrator_(std::move(installerOrchestrator))
        , exportService_(std::move(exportService)) {}

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

        const auto configuration = configurationService_->current();
        const auto providers = providerRegistry_->listProviders();
        const auto assignments = providerAssignmentService_->listAssignments();
        const auto installHistory = installerOrchestrator_->history();
        const auto exports = exportService_->generateExports();

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

        if (snapshot.findings.empty()) {
            snapshot.recommendedActions.push_back("Current runtime posture is aligned with the CLU governance profile.");
        }

        return snapshot;
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

class BeaconService final : public IBeaconService {
public:
    BeaconService(std::shared_ptr<IConfigurationService> configurationService,
                  std::shared_ptr<ITelemetryService> telemetryService)
        : configurationService_(std::move(configurationService))
        , telemetryService_(std::move(telemetryService)) {}

    BeaconAdvertisement currentAdvertisement() const override {
        const auto configuration = configurationService_->current();
        const auto snapshot = telemetryService_->captureSnapshot();
        return BeaconAdvertisement{
            configuration.instanceName,
            snapshot.hostName,
            snapshot.primaryIpAddress.empty() ? configuration.bindAddress : snapshot.primaryIpAddress,
            configuration.browserPort,
            7200,
            "online"
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
                    std::shared_ptr<IProviderRegistry> providerRegistry,
                    std::shared_ptr<IProviderCatalogService> providerCatalogService,
                    std::shared_ptr<IProviderCredentialStore> providerCredentialStore,
                    std::shared_ptr<IProviderAssignmentService> providerAssignmentService,
                    std::shared_ptr<IInstallerOrchestrator> installerOrchestrator,
                    std::shared_ptr<IBootstrapRepoService> bootstrapRepoService,
                    std::shared_ptr<IZipBundleService> zipBundleService,
                    std::shared_ptr<IExportService> exportService,
                    std::shared_ptr<ICommandLogicUnitService> commandLogicUnitService,
                    std::shared_ptr<IForsettiSurfaceService> surfaceService)
        : telemetryService_(std::move(telemetryService))
        , inventoryService_(std::move(inventoryService))
        , configurationService_(std::move(configurationService))
        , providerRegistry_(std::move(providerRegistry))
        , providerCatalogService_(std::move(providerCatalogService))
        , providerCredentialStore_(std::move(providerCredentialStore))
        , providerAssignmentService_(std::move(providerAssignmentService))
        , installerOrchestrator_(std::move(installerOrchestrator))
        , bootstrapRepoService_(std::move(bootstrapRepoService))
        , zipBundleService_(std::move(zipBundleService))
        , exportService_(std::move(exportService))
        , commandLogicUnitService_(std::move(commandLogicUnitService))
        , surfaceService_(std::move(surfaceService)) {}

    DashboardSnapshot snapshot() override {
        inventoryService_->refresh();

        DashboardSnapshot snapshot;
        snapshot.telemetry = telemetryService_->captureSnapshot();
        snapshot.endpoints = inventoryService_->listEndpoints();
        snapshot.providers = providerRegistry_->listProviders();
        snapshot.providerCapabilities = providerCatalogService_->listCapabilities();
        snapshot.providerCredentialStatuses = providerCredentialStore_->listStatuses();
        snapshot.providerAssignmentTargets = providerAssignmentService_->listTargets();
        snapshot.providerAssignments = providerAssignmentService_->listAssignments();
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
        snapshot.surface = surfaceService_->currentSurface();
        return snapshot;
    }

    GovernanceSnapshot governance() const override {
        return commandLogicUnitService_->currentGovernance();
    }

    OperationResult applyConfigurationJson(const std::string& requestBody,
                                           bool confirmUnsafeChanges) override {
        return configurationService_->update(nlohmann::json::parse(requestBody).get<AppConfiguration>(), confirmUnsafeChanges);
    }

    OperationResult upsertProviderJson(const std::string& requestBody) override {
        return providerRegistry_->upsertProvider(nlohmann::json::parse(requestBody).get<ProviderConnection>());
    }

    OperationResult upsertProviderCredentialsJson(const std::string& requestBody) override {
        return providerCredentialStore_->upsertCredentials(nlohmann::json::parse(requestBody).get<ProviderCredentialUpdate>());
    }

    OperationResult upsertProviderAssignmentJson(const std::string& requestBody) override {
        return providerAssignmentService_->upsertAssignment(nlohmann::json::parse(requestBody).get<ProviderAssignment>());
    }

    OperationResult installPackageJson(const std::string& requestBody) override {
        return installerOrchestrator_->installPackage(nlohmann::json::parse(requestBody).get<InstallerPackageSpec>());
    }

    OperationResult installRepoJson(const std::string& requestBody) override {
        return bootstrapRepoService_->installFromRepository(nlohmann::json::parse(requestBody).get<BootstrapRepoSpec>());
    }

    OperationResult installZipJson(const std::string& requestBody) override {
        return zipBundleService_->installFromZipBundle(nlohmann::json::parse(requestBody).get<ZipBundleSpec>());
    }

private:
    std::shared_ptr<ITelemetryService> telemetryService_;
    std::shared_ptr<IRuntimeInventoryService> inventoryService_;
    std::shared_ptr<IConfigurationService> configurationService_;
    std::shared_ptr<IProviderRegistry> providerRegistry_;
    std::shared_ptr<IProviderCatalogService> providerCatalogService_;
    std::shared_ptr<IProviderCredentialStore> providerCredentialStore_;
    std::shared_ptr<IProviderAssignmentService> providerAssignmentService_;
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
    std::thread worker_;
    SOCKET listenSocket_ = INVALID_SOCKET;
};

bool SimpleHttpServer::start() {
    if (running_.exchange(true)) {
        return true;
    }
    worker_ = std::thread([this]() { run(); });
    return true;
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
        return;
    }

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
    OperationResult applyConfigurationJson(const std::string& requestBody, bool confirmUnsafeChanges) { return adminApiService_->applyConfigurationJson(requestBody, confirmUnsafeChanges); }
    OperationResult upsertProviderJson(const std::string& requestBody) { return adminApiService_->upsertProviderJson(requestBody); }
    OperationResult upsertProviderCredentialsJson(const std::string& requestBody) { return adminApiService_->upsertProviderCredentialsJson(requestBody); }
    OperationResult upsertProviderAssignmentJson(const std::string& requestBody) { return adminApiService_->upsertProviderAssignmentJson(requestBody); }
    OperationResult installPackageJson(const std::string& requestBody) { return adminApiService_->installPackageJson(requestBody); }
    OperationResult installRepoJson(const std::string& requestBody) { return adminApiService_->installRepoJson(requestBody); }
    OperationResult installZipJson(const std::string& requestBody) { return adminApiService_->installZipJson(requestBody); }
    BeaconAdvertisement beaconAdvertisement() const { return beaconService_->currentAdvertisement(); }

private:
    void registerConfigurationDefaults();
    void createForsettiRuntime();
    void activateDefaultModules();
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
    std::shared_ptr<IPackageTrustEvaluator> trustEvaluator_;
    std::shared_ptr<InstallerOrchestrator> installerOrchestrator_;
    std::shared_ptr<IProviderRegistry> providerRegistry_;
    std::shared_ptr<IProviderCatalogService> providerCatalogService_;
    std::shared_ptr<IProviderCredentialStore> providerCredentialStore_;
    std::shared_ptr<IProviderAssignmentService> providerAssignmentService_;
    std::shared_ptr<IExportService> exportService_;
    std::shared_ptr<ICommandLogicUnitService> commandLogicUnitService_;
    std::shared_ptr<IModuleControlSurfaceService> controlSurfaceService_;
    std::shared_ptr<IForsettiSurfaceService> surfaceService_;
    std::shared_ptr<IBeaconService> beaconService_;
    std::shared_ptr<IAdminApiService> adminApiService_;
    std::shared_ptr<Forsetti::UISurfaceManager> surfaceManager_;
    std::shared_ptr<Forsetti::IEntitlementProvider> entitlementProvider_;
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
    trustEvaluator_ = std::make_shared<PackageTrustEvaluator>(state_);
    installerOrchestrator_ = std::make_shared<InstallerOrchestrator>(state_, trustEvaluator_, paths_);
    providerCatalogService_ = std::make_shared<ProviderCatalogService>();
    providerRegistry_ = std::make_shared<ProviderRegistryService>(state_, paths_.configurationFile, providerCatalogService_);
    providerCredentialStore_ = std::make_shared<ProviderCredentialStore>(paths_.providerCredentialsFile, providerRegistry_, providerCatalogService_);
    providerAssignmentService_ = std::make_shared<ProviderAssignmentService>(state_, paths_.configurationFile, inventoryService_, providerRegistry_);
    exportService_ = std::make_shared<ExportService>(inventoryService_, configurationService_);
    commandLogicUnitService_ = std::make_shared<CommandLogicUnitService>(
        paths_.cluProfileFile,
        configurationService_,
        providerRegistry_,
        providerAssignmentService_,
        installerOrchestrator_,
        exportService_);
    controlSurfaceService_ = std::make_shared<ModuleControlSurfaceService>();
    beaconService_ = std::make_shared<BeaconService>(configurationService_, telemetryService_);
    registerConfigurationDefaults();
    createForsettiRuntime();

    adminApiService_ = std::make_shared<AdminApiService>(
        telemetryService_,
        inventoryService_,
        configurationService_,
        providerRegistry_,
        providerCatalogService_,
        providerCredentialStore_,
        providerAssignmentService_,
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
    httpServer_->start();
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
        writeJsonFile(paths_.configurationFile, state_->configuration);
    }
}

void MasterControlApplication::Impl::createForsettiRuntime() {
    auto services = std::make_shared<Forsetti::ServiceContainer>();
    Forsetti::DefaultForsettiPlatformServices::registerAll(*services);
    services->registerService<IConfigurationService>(configurationService_);
    services->registerService<IResourceAllocationService>(resourceAllocationService_);
    services->registerService<ITelemetryService>(telemetryService_);
    services->registerService<IRuntimeInventoryService>(inventoryService_);
    services->registerService<IPackageTrustEvaluator>(trustEvaluator_);
    services->registerService<IInstallerOrchestrator>(installerOrchestrator_);
    services->registerService<IBootstrapRepoService>(installerOrchestrator_);
    services->registerService<IZipBundleService>(installerOrchestrator_);
    services->registerService<IProviderRegistry>(providerRegistry_);
    services->registerService<IProviderCatalogService>(providerCatalogService_);
    services->registerService<IProviderCredentialStore>(providerCredentialStore_);
    services->registerService<IProviderAssignmentService>(providerAssignmentService_);
    services->registerService<IExportService>(exportService_);
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
    entitlementProvider_ = std::make_shared<FileBackedEntitlementProvider>(
        paths_.entitlementsFile,
        buildDefaultEntitlementStateDocument());
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
    static const std::array<const char*, 13> moduleIds = {
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

HttpResponse MasterControlApplication::Impl::handleHttpRequest(const HttpRequest& request) {
    try {
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
        if (request.method == "GET" && request.path == "/api/forsetti/surface") {
            return jsonResponse(surfaceService_->currentSurface());
        }
        if (request.method == "GET" && request.path == "/api/install/history") {
            return jsonResponse(installerOrchestrator_->history());
        }
        if (request.method == "GET" && request.path == "/api/beacon") {
            return jsonResponse(beaconService_->currentAdvertisement());
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
        if (request.method == "POST" && request.path == "/api/providers/assignments") {
            const auto result = adminApiService_->upsertProviderAssignmentJson(request.body);
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

        return staticFileResponse(request.path);
    } catch (const std::exception& exception) {
        return jsonResponse(nlohmann::json{ { "succeeded", false }, { "message", exception.what() } }, 500);
    }
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

OperationResult MasterControlApplication::applyConfigurationJson(const std::string& requestBody, bool confirmUnsafeChanges) {
    return impl_->applyConfigurationJson(requestBody, confirmUnsafeChanges);
}

OperationResult MasterControlApplication::upsertProviderJson(const std::string& requestBody) {
    return impl_->upsertProviderJson(requestBody);
}

OperationResult MasterControlApplication::upsertProviderCredentialsJson(const std::string& requestBody) {
    return impl_->upsertProviderCredentialsJson(requestBody);
}

OperationResult MasterControlApplication::upsertProviderAssignmentJson(const std::string& requestBody) {
    return impl_->upsertProviderAssignmentJson(requestBody);
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
