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

std::string lowercase(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return value;
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

class ProviderRegistryService final : public IProviderRegistry {
public:
    ProviderRegistryService(std::shared_ptr<SharedState> state, std::filesystem::path configurationFile)
        : state_(std::move(state))
        , configurationFile_(std::move(configurationFile)) {}

    std::vector<ProviderConnection> listProviders() const override {
        std::lock_guard<std::mutex> lock(state_->mutex);
        return state_->configuration.providers;
    }

    OperationResult upsertProvider(const ProviderConnection& provider) override {
        std::lock_guard<std::mutex> lock(state_->mutex);
        auto& providers = state_->configuration.providers;
        const auto iterator = std::find_if(
            providers.begin(),
            providers.end(),
            [&provider](const ProviderConnection& candidate) { return candidate.id == provider.id; });

        if (iterator == providers.end()) {
            providers.push_back(provider);
        } else {
            *iterator = provider;
        }

        writeJsonFile(configurationFile_, state_->configuration);
        return OperationResult{ true, false, "Provider settings updated." };
    }

private:
    std::shared_ptr<SharedState> state_;
    std::filesystem::path configurationFile_;
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
                    std::shared_ptr<IInstallerOrchestrator> installerOrchestrator,
                    std::shared_ptr<IBootstrapRepoService> bootstrapRepoService,
                    std::shared_ptr<IZipBundleService> zipBundleService,
                    std::shared_ptr<IExportService> exportService)
        : telemetryService_(std::move(telemetryService))
        , inventoryService_(std::move(inventoryService))
        , configurationService_(std::move(configurationService))
        , providerRegistry_(std::move(providerRegistry))
        , installerOrchestrator_(std::move(installerOrchestrator))
        , bootstrapRepoService_(std::move(bootstrapRepoService))
        , zipBundleService_(std::move(zipBundleService))
        , exportService_(std::move(exportService)) {}

    DashboardSnapshot snapshot() override {
        inventoryService_->refresh();

        DashboardSnapshot snapshot;
        snapshot.telemetry = telemetryService_->captureSnapshot();
        snapshot.endpoints = inventoryService_->listEndpoints();
        snapshot.providers = providerRegistry_->listProviders();
        snapshot.installHistory = installerOrchestrator_->history();
        snapshot.exports = exportService_->generateExports();
        const auto configuration = configurationService_->current();
        snapshot.resourceAllocation = configuration.resourceAllocation;
        snapshot.security = configuration.security;
        return snapshot;
    }

    OperationResult applyConfigurationJson(const std::string& requestBody,
                                           bool confirmUnsafeChanges) override {
        return configurationService_->update(nlohmann::json::parse(requestBody).get<AppConfiguration>(), confirmUnsafeChanges);
    }

    OperationResult upsertProviderJson(const std::string& requestBody) override {
        return providerRegistry_->upsertProvider(nlohmann::json::parse(requestBody).get<ProviderConnection>());
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
    std::shared_ptr<IInstallerOrchestrator> installerOrchestrator_;
    std::shared_ptr<IBootstrapRepoService> bootstrapRepoService_;
    std::shared_ptr<IZipBundleService> zipBundleService_;
    std::shared_ptr<IExportService> exportService_;
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
    std::shared_ptr<IExportService> exportService_;
    std::shared_ptr<IBeaconService> beaconService_;
    std::shared_ptr<IAdminApiService> adminApiService_;
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
    providerRegistry_ = std::make_shared<ProviderRegistryService>(state_, paths_.configurationFile);
    exportService_ = std::make_shared<ExportService>(inventoryService_, configurationService_);
    beaconService_ = std::make_shared<BeaconService>(configurationService_, telemetryService_);
    adminApiService_ = std::make_shared<AdminApiService>(
        telemetryService_,
        inventoryService_,
        configurationService_,
        providerRegistry_,
        installerOrchestrator_,
        installerOrchestrator_,
        installerOrchestrator_,
        exportService_);

    registerConfigurationDefaults();
    createForsettiRuntime();

    runtime_->boot();
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
    services->registerService<IExportService>(exportService_);
    services->registerService<IBeaconService>(beaconService_);
    services->registerService<IAdminApiService>(adminApiService_);

    auto eventBus = std::make_shared<Forsetti::InMemoryEventBus>();
    auto logger = std::make_shared<Forsetti::ConsoleLogger>();
    auto router = std::make_shared<Forsetti::NoopOverlayRouter>();
    auto guard = std::make_shared<Forsetti::DefaultModuleCommunicationGuard>();
    auto context = std::make_shared<Forsetti::ForsettiContext>(services, eventBus, logger, router, guard);
    auto surfaceManager = std::make_shared<Forsetti::UISurfaceManager>();
    auto compatibilityPolicy = std::make_shared<Forsetti::AllowAllCapabilityPolicy>();
    auto checker = std::make_shared<Forsetti::CompatibilityChecker>(Forsetti::SemVer{ 0, 1, 0 }, compatibilityPolicy);
    auto entitlementProvider = std::make_shared<Forsetti::AllowAllEntitlementProvider>();
    auto activationStore = std::make_shared<JsonActivationStore>(paths_.dataDirectory / "state" / "activation-state.json");

    auto registry = Forsetti::ForsettiStaticModuleRegistry::buildRegistry([](Forsetti::ModuleRegistry& registry) {
        registerMasterControlModules(registry);
    });

    auto moduleManager = std::make_unique<Forsetti::ModuleManager>(
        std::move(registry),
        checker,
        entitlementProvider,
        activationStore,
        surfaceManager,
        context);

    runtime_ = std::make_unique<Forsetti::ForsettiRuntime>(
        std::move(moduleManager),
        entitlementProvider,
        eventBus,
        paths_.manifestsDirectory.string());
}

void MasterControlApplication::Impl::activateDefaultModules() {
    static const std::array<const char*, 9> moduleIds = {
        "com.mastercontrol.environment-discovery",
        "com.mastercontrol.host-telemetry",
        "com.mastercontrol.runtime-inventory",
        "com.mastercontrol.configuration",
        "com.mastercontrol.installer-import",
        "com.mastercontrol.provider-integration",
        "com.mastercontrol.export",
        "com.mastercontrol.beacon-gateway",
        "com.mastercontrol.dashboard-ui"
    };

    for (const auto* moduleId : moduleIds) {
        if (!runtime_->moduleManager().isModuleActive(moduleId)) {
            runtime_->activateModule(moduleId);
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
