// Master Control Program
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#include "MasterControl/MasterControlDefaults.h"
#include "MasterControl/MasterControlModels.h"
#include "MasterControl/MasterControlRuntime.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

bool expect(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << "Test failed: " << message << '\n';
        return false;
    }
    return true;
}

class ScopedEnvironmentOverride final {
public:
    ScopedEnvironmentOverride(std::wstring name, std::wstring value)
        : name_(std::move(name)) {
        const DWORD required = GetEnvironmentVariableW(name_.c_str(), nullptr, 0);
        if (required > 0) {
            originalValue_.emplace(static_cast<size_t>(required - 1), L'\0');
            GetEnvironmentVariableW(name_.c_str(), originalValue_->data(), required);
        }

        SetEnvironmentVariableW(name_.c_str(), value.c_str());
    }

    ~ScopedEnvironmentOverride() {
        SetEnvironmentVariableW(name_.c_str(), originalValue_.has_value() ? originalValue_->c_str() : nullptr);
    }

private:
    std::wstring name_;
    std::optional<std::wstring> originalValue_;
};

std::filesystem::path makeTempRoot() {
    const auto root = std::filesystem::temp_directory_path() /
        ("MasterControlProgramTests_" + std::to_string(GetCurrentProcessId()));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    return root;
}

void writeTextFile(const std::filesystem::path& filePath, const std::string& contents) {
    std::filesystem::create_directories(filePath.parent_path());
    std::ofstream output(filePath, std::ios::binary | std::ios::trunc);
    output << contents;
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

std::optional<std::string> readFileUtf8(const std::filesystem::path& filePath) {
    std::ifstream stream(filePath, std::ios::binary);
    if (!stream.is_open()) {
        return std::nullopt;
    }

    return std::string(
        (std::istreambuf_iterator<char>(stream)),
        std::istreambuf_iterator<char>());
}

struct TestHttpRequest final {
    std::string method;
    std::string path;
    std::map<std::string, std::string> headers;
    std::string body;
};

struct TestHttpResponse final {
    int statusCode = 200;
    std::string contentType = "application/json";
    std::string body;
};

std::string trimAscii(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string lowercaseAscii(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return value;
}

std::string httpStatusReason(const int statusCode) {
    switch (statusCode) {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 500: return "Internal Server Error";
        default: return "OK";
    }
}

class SimpleHttpServer final {
public:
    using Handler = std::function<TestHttpResponse(const TestHttpRequest&)>;

    explicit SimpleHttpServer(Handler handler)
        : handler_(std::move(handler)) {
        WSAData data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            throw std::runtime_error("WSAStartup failed");
        }

        listenSocket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listenSocket_ == INVALID_SOCKET) {
            WSACleanup();
            throw std::runtime_error("Unable to create test socket");
        }

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = 0;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

        if (bind(listenSocket_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR) {
            closesocket(listenSocket_);
            WSACleanup();
            throw std::runtime_error("Unable to bind test socket");
        }

        if (listen(listenSocket_, SOMAXCONN) == SOCKET_ERROR) {
            closesocket(listenSocket_);
            WSACleanup();
            throw std::runtime_error("Unable to listen on test socket");
        }

        sockaddr_in boundAddress{};
        int boundAddressLength = sizeof(boundAddress);
        getsockname(listenSocket_, reinterpret_cast<sockaddr*>(&boundAddress), &boundAddressLength);
        port_ = ntohs(boundAddress.sin_port);

        worker_ = std::thread([this]() { Run(); });
    }

    ~SimpleHttpServer() {
        Stop();
        WSACleanup();
    }

    SimpleHttpServer(const SimpleHttpServer&) = delete;
    SimpleHttpServer& operator=(const SimpleHttpServer&) = delete;

    [[nodiscard]] uint16_t port() const {
        return port_;
    }

    [[nodiscard]] std::vector<TestHttpRequest> requests() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return requests_;
    }

    void Stop() {
        if (stopped_.exchange(true)) {
            return;
        }

        if (listenSocket_ != INVALID_SOCKET) {
            SOCKET wakeSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (wakeSocket != INVALID_SOCKET) {
                sockaddr_in address{};
                address.sin_family = AF_INET;
                address.sin_port = htons(port_);
                inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
                connect(wakeSocket, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
                closesocket(wakeSocket);
            }

            closesocket(listenSocket_);
            listenSocket_ = INVALID_SOCKET;
        }

        if (worker_.joinable()) {
            worker_.join();
        }
    }

private:
    void Run() {
        while (!stopped_) {
            SOCKET clientSocket = accept(listenSocket_, nullptr, nullptr);
            if (clientSocket == INVALID_SOCKET) {
                if (stopped_) {
                    break;
                }
                continue;
            }

            if (stopped_) {
                closesocket(clientSocket);
                break;
            }

            HandleClient(clientSocket);
            closesocket(clientSocket);
        }
    }

    void HandleClient(const SOCKET clientSocket) {
        DWORD timeoutMilliseconds = 5000;
        setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeoutMilliseconds), sizeof(timeoutMilliseconds));
        setsockopt(clientSocket, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeoutMilliseconds), sizeof(timeoutMilliseconds));

        std::string requestBuffer;
        char chunk[4096]{};
        size_t contentLength = 0;
        bool parsedHeaders = false;

        while (true) {
            const int bytesReceived = recv(clientSocket, chunk, sizeof(chunk), 0);
            if (bytesReceived <= 0) {
                break;
            }

            requestBuffer.append(chunk, static_cast<size_t>(bytesReceived));
            const size_t headerEnd = requestBuffer.find("\r\n\r\n");
            if (headerEnd == std::string::npos) {
                continue;
            }

            if (!parsedHeaders) {
                parsedHeaders = true;
                const std::string headerText = requestBuffer.substr(0, headerEnd);
                std::istringstream headerStream(headerText);
                std::string line;
                std::getline(headerStream, line); // request line
                while (std::getline(headerStream, line)) {
                    if (!line.empty() && line.back() == '\r') {
                        line.pop_back();
                    }
                    const auto colon = line.find(':');
                    if (colon == std::string::npos) {
                        continue;
                    }
                    const auto key = lowercaseAscii(trimAscii(line.substr(0, colon)));
                    const auto value = trimAscii(line.substr(colon + 1));
                    if (key == "content-length") {
                        contentLength = static_cast<size_t>(std::stoul(value));
                    }
                }
            }

            const size_t bodyStart = headerEnd + 4;
            if (requestBuffer.size() >= bodyStart + contentLength) {
                break;
            }
        }

        TestHttpRequest request;
        const size_t headerEnd = requestBuffer.find("\r\n\r\n");
        if (headerEnd != std::string::npos) {
            std::istringstream headerStream(requestBuffer.substr(0, headerEnd));
            std::string requestLine;
            std::getline(headerStream, requestLine);
            if (!requestLine.empty() && requestLine.back() == '\r') {
                requestLine.pop_back();
            }

            std::istringstream requestLineStream(requestLine);
            requestLineStream >> request.method >> request.path;

            std::string line;
            while (std::getline(headerStream, line)) {
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                const auto colon = line.find(':');
                if (colon == std::string::npos) {
                    continue;
                }
                request.headers.emplace(
                    lowercaseAscii(trimAscii(line.substr(0, colon))),
                    trimAscii(line.substr(colon + 1)));
            }

            request.body = requestBuffer.substr(headerEnd + 4);
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            requests_.push_back(request);
        }

        const auto response = handler_(request);
        std::ostringstream responseStream;
        responseStream << "HTTP/1.1 " << response.statusCode << ' ' << httpStatusReason(response.statusCode) << "\r\n"
                       << "Content-Type: " << response.contentType << "\r\n"
                       << "Content-Length: " << response.body.size() << "\r\n"
                       << "Connection: close\r\n\r\n"
                       << response.body;
        const auto responseText = responseStream.str();
        send(clientSocket, responseText.data(), static_cast<int>(responseText.size()), 0);
    }

    Handler handler_;
    SOCKET listenSocket_ = INVALID_SOCKET;
    uint16_t port_ = 0;
    std::thread worker_;
    std::atomic<bool> stopped_{ false };
    mutable std::mutex mutex_;
    std::vector<TestHttpRequest> requests_;
};

std::filesystem::path currentExecutablePath() {
    wchar_t buffer[MAX_PATH]{};
    const auto length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    return std::filesystem::path(std::wstring(buffer, length));
}

std::filesystem::path bootstrapperBinaryPath() {
    const auto executablePath = currentExecutablePath();
    const auto configurationName = executablePath.parent_path().filename();
    const auto buildRoot = executablePath.parent_path().parent_path().parent_path();
    return buildRoot / "src" / "MasterControlBootstrapper" / configurationName / "MasterControlBootstrapper.exe";
}

int runProcess(const std::wstring& command, const std::filesystem::path& workingDirectory) {
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

bool toolExists(const wchar_t* executableName) {
    wchar_t pathBuffer[MAX_PATH]{};
    return SearchPathW(nullptr, executableName, nullptr, MAX_PATH, pathBuffer, nullptr) > 0;
}

std::optional<MasterControl::RuntimeEndpoint> findEndpoint(const std::vector<MasterControl::RuntimeEndpoint>& endpoints,
                                                           const std::string& id) {
    const auto iterator = std::find_if(
        endpoints.begin(),
        endpoints.end(),
        [&id](const MasterControl::RuntimeEndpoint& endpoint) { return endpoint.id == id; });
    if (iterator == endpoints.end()) {
        return std::nullopt;
    }
    return *iterator;
}

bool hasProvider(const std::vector<MasterControl::ProviderConnection>& providers, const std::string& id) {
    return std::any_of(
        providers.begin(),
        providers.end(),
        [&id](const MasterControl::ProviderConnection& provider) { return provider.id == id; });
}

bool hasProviderCapability(const std::vector<MasterControl::ProviderCapabilityDescriptor>& capabilities,
                          const std::string& providerId) {
    return std::any_of(
        capabilities.begin(),
        capabilities.end(),
        [&providerId](const MasterControl::ProviderCapabilityDescriptor& capability) {
            return capability.providerId == providerId;
        });
}

std::optional<MasterControl::ProviderCredentialStatus> findCredentialStatus(
    const std::vector<MasterControl::ProviderCredentialStatus>& statuses,
    const std::string& providerId) {
    const auto iterator = std::find_if(
        statuses.begin(),
        statuses.end(),
        [&providerId](const MasterControl::ProviderCredentialStatus& status) { return status.providerId == providerId; });
    if (iterator == statuses.end()) {
        return std::nullopt;
    }
    return *iterator;
}

bool hasAssignmentTarget(const std::vector<MasterControl::ProviderAssignmentTarget>& targets,
                         const std::string& targetId) {
    return std::any_of(
        targets.begin(),
        targets.end(),
        [&targetId](const MasterControl::ProviderAssignmentTarget& target) { return target.targetId == targetId; });
}

std::optional<MasterControl::ProviderAssignment> findAssignment(
    const std::vector<MasterControl::ProviderAssignment>& assignments,
    const std::string& targetId) {
    const auto iterator = std::find_if(
        assignments.begin(),
        assignments.end(),
        [&targetId](const MasterControl::ProviderAssignment& assignment) { return assignment.targetId == targetId; });
    if (iterator == assignments.end()) {
        return std::nullopt;
    }
    return *iterator;
}

std::optional<MasterControl::SubAgentGroupDefinition> findSubAgentGroup(
    const std::vector<MasterControl::SubAgentGroupDefinition>& groups,
    const std::string& groupId) {
    const auto iterator = std::find_if(
        groups.begin(),
        groups.end(),
        [&groupId](const MasterControl::SubAgentGroupDefinition& group) { return group.groupId == groupId; });
    if (iterator == groups.end()) {
        return std::nullopt;
    }
    return *iterator;
}

std::optional<MasterControl::ProviderConnection> findProvider(
    const std::vector<MasterControl::ProviderConnection>& providers,
    const std::string& id) {
    const auto iterator = std::find_if(
        providers.begin(),
        providers.end(),
        [&id](const MasterControl::ProviderConnection& provider) { return provider.id == id; });
    if (iterator == providers.end()) {
        return std::nullopt;
    }
    return *iterator;
}

bool hasExport(const std::vector<MasterControl::ExportArtifact>& artifacts, const std::string& fileName) {
    return std::any_of(
        artifacts.begin(),
        artifacts.end(),
        [&fileName](const MasterControl::ExportArtifact& artifact) { return artifact.fileName == fileName; });
}

bool hasNavigationDestination(const std::optional<Forsetti::OverlaySchema>& overlaySchema,
                              const std::string& destinationId) {
    if (!overlaySchema.has_value()) {
        return false;
    }

    return std::any_of(
        overlaySchema->navigationPointers.begin(),
        overlaySchema->navigationPointers.end(),
        [&destinationId](const Forsetti::NavigationPointer& pointer) {
            return pointer.baseDestinationID == destinationId;
        });
}

bool hasToolbarItemId(const std::vector<Forsetti::ToolbarItemDescriptor>& toolbarItems,
                      const std::string& itemId) {
    return std::any_of(
        toolbarItems.begin(),
        toolbarItems.end(),
        [&itemId](const Forsetti::ToolbarItemDescriptor& item) {
            return item.itemID == itemId;
        });
}

bool hasOverlayRouteId(const std::optional<Forsetti::OverlaySchema>& overlaySchema,
                       const std::string& routeId) {
    if (!overlaySchema.has_value()) {
        return false;
    }

    return std::any_of(
        overlaySchema->overlayRoutes.begin(),
        overlaySchema->overlayRoutes.end(),
        [&routeId](const Forsetti::OverlayRoute& route) {
            return route.routeID == routeId;
        });
}

bool hasControlRequestsForModules(const std::vector<MasterControl::ModuleControlSurfaceRequest>& requests,
                                  const std::vector<std::string>& moduleIds) {
    return std::all_of(
        moduleIds.begin(),
        moduleIds.end(),
        [&requests](const std::string& moduleId) {
            return std::any_of(
                requests.begin(),
                requests.end(),
                [&moduleId](const MasterControl::ModuleControlSurfaceRequest& request) {
                    return request.moduleId == moduleId;
                });
        });
}

bool hasHistoryEntry(const std::vector<MasterControl::InstallProvenance>& entries,
                     MasterControl::InstallerKind kind,
                     const std::string& source) {
    return std::any_of(
        entries.begin(),
        entries.end(),
        [kind, &source](const MasterControl::InstallProvenance& entry) {
            return entry.kind == kind && entry.source == source;
        });
}

bool hasProviderExecutionTool(const MasterControl::ProviderExecutionRecord& record, const std::string& toolName) {
    return std::find(record.toolEvents.begin(), record.toolEvents.end(), toolName) != record.toolEvents.end();
}

bool hasProviderExecutionRegistration(
    const std::vector<MasterControl::ProviderExecutionRegistration>& registrations,
    const std::string& providerId) {
    return std::any_of(
        registrations.begin(),
        registrations.end(),
        [&providerId](const MasterControl::ProviderExecutionRegistration& registration) {
            return registration.providerId == providerId;
        });
}

std::wstring escapePowerShellLiteral(std::wstring value) {
    size_t position = 0;
    while ((position = value.find(L'\'', position)) != std::wstring::npos) {
        value.insert(position, L"'");
        position += 2;
    }
    return value;
}

bool createGitImportFixture(const std::filesystem::path& repoRoot) {
    writeTextFile(repoRoot / "bootstrap.ps1", "New-Item -Path (Join-Path $PSScriptRoot 'repo-bootstrap.ok') -ItemType File -Force | Out-Null\nexit 0\n");

    const nlohmann::json manifest = {
        { "version", "2.0.0" },
        { "bootstrapScript", "bootstrap.ps1" },
        { "seededEndpoints", nlohmann::json::array({
            {
                { "id", "repo-import-endpoint" },
                { "displayName", "Repo Import Endpoint" },
                { "kind", "mcp_server" },
                { "host", "" },
                { "port", 7420 },
                { "protocol", "http" },
                { "routePath", "/health" },
                { "description", "Installed from repository contract" }
            }
        }) },
        { "providers", nlohmann::json::array({
            {
                { "id", "repo-import-provider" },
                { "kind", "generic" },
                { "displayName", "Repo Import Provider" },
                { "baseUrl", "https://repo.example.test" },
                { "enabled", true },
                { "allowAutonomousControl", false }
            }
        }) }
    };

    writeTextFile(repoRoot / "mcp-bootstrap.json", manifest.dump(2));

    return runProcess(L"git init", repoRoot) == 0 &&
        runProcess(L"git config user.email master-control-tests@example.com", repoRoot) == 0 &&
        runProcess(L"git config user.name \"Master Control Tests\"", repoRoot) == 0 &&
        runProcess(L"git add .", repoRoot) == 0 &&
        runProcess(L"git commit -m \"Initial bootstrap fixture\"", repoRoot) == 0 &&
        runProcess(L"git branch -M main", repoRoot) == 0;
}

bool createZipImportFixture(const std::filesystem::path& sourceRoot, const std::filesystem::path& zipPath) {
    writeTextFile(sourceRoot / "bootstrap.ps1", "New-Item -Path (Join-Path $PSScriptRoot 'zip-bootstrap.ok') -ItemType File -Force | Out-Null\nexit 0\n");

    const nlohmann::json manifest = {
        { "version", "3.1.0" },
        { "bootstrapScript", "bootstrap.ps1" },
        { "seededEndpoints", nlohmann::json::array({
            {
                { "id", "zip-import-endpoint" },
                { "displayName", "Zip Import Endpoint" },
                { "kind", "sub_agent" },
                { "host", "" },
                { "port", 7421 },
                { "protocol", "http" },
                { "routePath", "/status" },
                { "description", "Installed from zip contract" }
            }
        }) },
        { "providers", nlohmann::json::array({
            {
                { "id", "zip-import-provider" },
                { "kind", "generic" },
                { "displayName", "Zip Import Provider" },
                { "baseUrl", "https://zip.example.test" },
                { "enabled", true },
                { "allowAutonomousControl", true }
            }
        }) }
    };

    writeTextFile(sourceRoot / "mcp-bootstrap.json", manifest.dump(2));

    const auto command = L"pwsh -NoProfile -ExecutionPolicy Bypass -Command \"Compress-Archive -Path '" +
        escapePowerShellLiteral((sourceRoot / L"*").wstring()) +
        L"' -DestinationPath '" + escapePowerShellLiteral(zipPath.wstring()) + L"' -Force\"";

    return runProcess(command, sourceRoot.parent_path()) == 0;
}

} // namespace

int main() {
    bool success = true;

    const auto environment = MasterControl::detectLocalEnvironment();
    success &= expect(!environment.hostName.empty(), "Detected environment should include a host name");
    success &= expect(!environment.operatingSystem.empty(), "Detected environment should include an operating system description");
    success &= expect(!environment.preferredBindAddress.empty(), "Detected environment should include a preferred bind address");

    const auto configuration = MasterControl::buildDefaultConfiguration();
    success &= expect(configuration.browserPort == 7300, "Default browser port should be 7300");
    success &= expect(configuration.activeProfile.preferredBindAddress == environment.preferredBindAddress, "Default profile should honor the detected bind address");
    success &= expect(!configuration.activeProfile.environmentName.empty(), "Default profile should describe the detected environment");
    success &= expect(configuration.providers.size() >= 3, "Default providers should include the supported named adapters");

    const auto gatewayEndpoint = findEndpoint(configuration.activeProfile.seededEndpoints, "aggregator-gateway");
    success &= expect(gatewayEndpoint.has_value(), "BLADE profile should include the aggregator gateway");
    success &= expect(gatewayEndpoint.has_value() && gatewayEndpoint->host == configuration.activeProfile.preferredBindAddress, "Seeded endpoints should use the detected host");

    const nlohmann::json serialized = configuration;
    const auto roundTripped = serialized.get<MasterControl::AppConfiguration>();
    success &= expect(roundTripped.instanceName == configuration.instanceName, "Configuration should round-trip through JSON");

    const auto tempRoot = makeTempRoot();
    {
        ScopedEnvironmentOverride dataDirectoryOverride(L"MASTERCONTROL_DATA_DIR", (tempRoot / "data").wstring());
        const auto appPaths = MasterControl::resolveAppPaths();

        MasterControl::MasterControlApplication application;
        success &= expect(application.initialize(), "Application should initialize");

        auto snapshot = application.snapshot();
        const std::vector<std::string> controlSurfaceModuleIds = {
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
            "com.mastercontrol.beacon-gateway"
        };
        success &= expect(!snapshot.endpoints.empty(), "Snapshot should include endpoints");
        success &= expect(hasExport(snapshot.exports, "Install-ClaudeGateway.ps1"), "Exports should include a Claude installer helper");
        success &= expect(hasExport(snapshot.exports, "Install-CodexGateway.ps1"), "Exports should include a Codex installer helper");
        success &= expect(snapshot.governance.unitName == "Command Logic Unit", "Snapshot should expose the CLU governance unit");
        success &= expect(!snapshot.governance.roles.empty(), "CLU snapshot should publish governance roles");
        success &= expect(!snapshot.governance.rules.empty(), "CLU snapshot should publish governance rules");
        success &= expect(!snapshot.governance.documents.empty(), "CLU snapshot should publish governance documents");
        success &= expect(snapshot.surface.overlaySchema.has_value(), "Snapshot should expose Forsetti overlay metadata");
        success &= expect(
            snapshot.surface.overlaySchema.has_value() &&
                snapshot.surface.overlaySchema->navigationPointers.size() >= 8,
            "Forsetti overlay metadata should describe the shell navigation lanes");
        success &= expect(snapshot.surface.toolbarItems.size() >= 6, "Forsetti surface snapshot should expose toolbar items");
        success &= expect(
            snapshot.surface.viewInjectionsBySlot.size() >= 8,
            "Forsetti surface snapshot should expose injected section slots");
        success &= expect(
            hasControlRequestsForModules(snapshot.surface.registeredControlSurfaceRequests, controlSurfaceModuleIds),
            "Every service module should register its control-surface needs with the framework");
        success &= expect(
            snapshot.surface.publishedByModuleId == "com.mastercontrol.dashboard-ui",
            "The dashboard UI module should publish the composed framework surface");
        success &= expect(
            !snapshot.surface.publishedAtUtc.empty(),
            "The framework surface snapshot should record when the UI module registered its composed surface");
        success &= expect(
            snapshot.surface.viewInjectionsBySlot.contains("overview") &&
                !snapshot.surface.viewInjectionsBySlot.at("overview").empty() &&
                snapshot.surface.viewInjectionsBySlot.at("overview").front().viewID == "OverviewSectionView",
            "Forsetti overview slot should resolve to the overview section view");
        success &= expect(hasProviderCapability(snapshot.providerCapabilities, "codex"), "Codex provider module should publish a capability descriptor.");
        success &= expect(hasProviderCapability(snapshot.providerCapabilities, "claude-code"), "Claude Code provider module should publish a capability descriptor.");
        success &= expect(hasProviderCapability(snapshot.providerCapabilities, "xai-grok"), "xAI provider module should publish a capability descriptor.");
        success &= expect(hasAssignmentTarget(snapshot.providerAssignmentTargets, "planner"), "Provider assignment targets should include planner ownership.");
        success &= expect(hasAssignmentTarget(snapshot.providerAssignmentTargets, "architect"), "Provider assignment targets should include architect ownership.");
        success &= expect(hasAssignmentTarget(snapshot.providerAssignmentTargets, "coding-specialists"), "Provider assignment targets should include the coding specialist group.");
        success &= expect(
            hasNavigationDestination(snapshot.surface.overlaySchema, "clu"),
            "Forsetti overlay metadata should publish CLU navigation through the framework surface");
        success &= expect(
            hasToolbarItemId(snapshot.surface.toolbarItems, "command-logic-unit-dashboard"),
            "Forsetti surface snapshot should publish the CLU toolbar item");
        success &= expect(
            snapshot.surface.viewInjectionsBySlot.contains("clu") &&
                !snapshot.surface.viewInjectionsBySlot.at("clu").empty() &&
                snapshot.surface.viewInjectionsBySlot.at("clu").front().viewID == "CommandLogicUnitSectionView",
            "Forsetti CLU slot should resolve to the CLU section view");
        success &= expect(
            hasOverlayRouteId(snapshot.surface.overlaySchema, "imports-overlay"),
            "Forsetti overlay metadata should publish the imports overlay route");
        success &= expect(
            hasOverlayRouteId(snapshot.surface.overlaySchema, "settings-overlay"),
            "Forsetti overlay metadata should publish the settings overlay route");
        success &= expect(
            hasOverlayRouteId(snapshot.surface.overlaySchema, "exports-overlay"),
            "Forsetti overlay metadata should publish the exports overlay route");

        success &= expect(std::filesystem::exists(appPaths.entitlementsFile), "The runtime should seed an entitlement state file");
        writeTextFile(
            appPaths.entitlementsFile,
            nlohmann::json{
                { "unlockedModuleIDs", nlohmann::json::array({
                    "com.mastercontrol.environment-discovery",
                    "com.mastercontrol.host-telemetry",
                    "com.mastercontrol.runtime-inventory",
                    "com.mastercontrol.configuration",
                    "com.mastercontrol.provider-codex",
                    "com.mastercontrol.provider-claude-code",
                    "com.mastercontrol.provider-xai",
                    "com.mastercontrol.dashboard-ui"
                }) },
                { "unlockedProductIDs", nlohmann::json::array({
                    "mastercontrol.iap.installer-import",
                    "mastercontrol.iap.provider-integration",
                    "mastercontrol.iap.export",
                    "mastercontrol.iap.beacon-gateway"
                }) }
            }.dump(2));

        for (int attempt = 0; attempt < 12; ++attempt) {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            snapshot = application.snapshot();
            if (!hasNavigationDestination(snapshot.surface.overlaySchema, "clu")) {
                break;
            }
        }

        success &= expect(
            !hasNavigationDestination(snapshot.surface.overlaySchema, "clu"),
            "Forsetti entitlement reconciliation should remove CLU navigation when its product is no longer unlocked");
        success &= expect(
            !hasToolbarItemId(snapshot.surface.toolbarItems, "command-logic-unit-dashboard"),
            "Forsetti entitlement reconciliation should remove the CLU toolbar item when its product is no longer unlocked");

        writeTextFile(
            appPaths.entitlementsFile,
            nlohmann::json{
                { "unlockedModuleIDs", nlohmann::json::array({
                    "com.mastercontrol.environment-discovery",
                    "com.mastercontrol.host-telemetry",
                    "com.mastercontrol.runtime-inventory",
                    "com.mastercontrol.configuration",
                    "com.mastercontrol.provider-codex",
                    "com.mastercontrol.provider-claude-code",
                    "com.mastercontrol.provider-xai",
                    "com.mastercontrol.dashboard-ui"
                }) },
                { "unlockedProductIDs", nlohmann::json::array({
                    "mastercontrol.iap.installer-import",
                    "mastercontrol.iap.provider-integration",
                    "mastercontrol.iap.export",
                    "mastercontrol.iap.command-logic-unit",
                    "mastercontrol.iap.beacon-gateway"
                }) }
            }.dump(2));

        for (int attempt = 0; attempt < 12; ++attempt) {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            snapshot = application.snapshot();
            if (hasNavigationDestination(snapshot.surface.overlaySchema, "clu")) {
                break;
            }
        }

        success &= expect(
            hasNavigationDestination(snapshot.surface.overlaySchema, "clu"),
            "Forsetti entitlement reconciliation should restore CLU navigation when the product is unlocked again");

        auto unsafeConfiguration = configuration;
        unsafeConfiguration.security.securityProtocolsEnabled = false;
        const auto unsafeResult = application.applyConfigurationJson(nlohmann::json(unsafeConfiguration).dump(), false);
        success &= expect(!unsafeResult.succeeded && unsafeResult.requiresConfirmation, "Unsafe configuration should require confirmation");
        const auto confirmedResult = application.applyConfigurationJson(nlohmann::json(unsafeConfiguration).dump(), true);
        success &= expect(confirmedResult.succeeded, "Unsafe configuration should succeed after confirmation");
        snapshot = application.snapshot();
        success &= expect(snapshot.governance.posture == "blocked", "CLU should block unsafe open-LAN posture when security protocols are disabled");
        success &= expect(!snapshot.governance.findings.empty(), "CLU should record findings for blocked security posture");

        auto managedConfiguration = configuration;
        managedConfiguration.aiAutonomyEnabled = true;
        managedConfiguration.security.securityProtocolsEnabled = true;
        managedConfiguration.security.enableTls = true;
        managedConfiguration.security.enableAuthentication = true;
        managedConfiguration.security.allowTroubleshootingBypass = true;
        managedConfiguration.security.allowOpenLanAccess = false;
        managedConfiguration.security.trustedRemoteHosts = {
            "192.168.1.20",
            "builder-node.local"
        };
        const auto managedResult = application.applyConfigurationJson(nlohmann::json(managedConfiguration).dump(), false);
        success &= expect(managedResult.succeeded, "Managed security configuration should apply successfully");

        snapshot = application.snapshot();
        success &= expect(snapshot.security.securityProtocolsEnabled, "Security protocols should remain enabled after managed update");
        success &= expect(snapshot.security.enableTls, "Managed security update should enable TLS");
        success &= expect(snapshot.security.enableAuthentication, "Managed security update should enable authentication");
        success &= expect(snapshot.security.allowTroubleshootingBypass, "Managed security update should allow troubleshooting bypass");
        success &= expect(!snapshot.security.allowOpenLanAccess, "Managed security update should disable open LAN access");
        success &= expect(snapshot.security.trustedRemoteHosts.size() == 2, "Managed security update should persist trusted hosts");
        success &= expect(
            snapshot.security.trustedRemoteHosts.size() == 2 &&
                snapshot.security.trustedRemoteHosts[0] == "192.168.1.20" &&
                snapshot.security.trustedRemoteHosts[1] == "builder-node.local",
            "Managed security update should preserve trusted host ordering");

        const auto invalidProviderResult = application.upsertProviderJson(nlohmann::json{
            { "id", "invalid-provider" },
            { "kind", "generic" },
            { "displayName", "Invalid Provider" }
        }.dump());
        success &= expect(!invalidProviderResult.succeeded, "Provider updates without a base URL should be rejected");

        const auto providerUpsertResult = application.upsertProviderJson(nlohmann::json{
            { "id", "ops-provider" },
            { "kind", "xai" },
            { "displayName", "Operations Provider" },
            { "baseUrl", "https://ops.example.test/v1" },
            { "modelId", "grok-code-fast-1" },
            { "enabled", true },
            { "allowAutonomousControl", true }
        }.dump());
        success &= expect(providerUpsertResult.succeeded, "Provider upsert should succeed");

        snapshot = application.snapshot();
        const auto upsertedProvider = findProvider(snapshot.providers, "ops-provider");
        success &= expect(upsertedProvider.has_value(), "Provider upsert should be reflected in the runtime snapshot");
        success &= expect(
            upsertedProvider.has_value() && upsertedProvider->allowAutonomousControl,
            "Provider upsert should preserve autonomous control settings");
        success &= expect(
            upsertedProvider.has_value() && upsertedProvider->baseUrl == "https://ops.example.test/v1",
            "Provider upsert should persist the provider base URL");
        success &= expect(
            upsertedProvider.has_value() && upsertedProvider->modelId == "grok-code-fast-1",
            "Provider upsert should persist the provider model selection");
        success &= expect(snapshot.providers.size() >= 4, "Provider upsert should extend the provider registry");
        const auto providerCredentialsResult = application.upsertProviderCredentialsJson(nlohmann::json{
            { "providerId", "ops-provider" },
            { "values", nlohmann::json{
                { "xai_api_key", "test-xai-key" }
            } }
        }.dump());
        success &= expect(providerCredentialsResult.succeeded, "Provider credentials should save through the secure store.");

        const auto customMcpServerResult = application.upsertMcpServerJson(nlohmann::json{
            { "id", "swift-tools-mcp" },
            { "displayName", "Swift Tools MCP" },
            { "kind", "mcp_server" },
            { "host", "127.0.0.1" },
            { "port", 7440 },
            { "protocol", "http" },
            { "routePath", "/mcp" },
            { "description", "Custom shared Swift MCP tool lane." },
            { "specialization", "" },
            { "userDefined", true }
        }.dump());
        success &= expect(customMcpServerResult.succeeded, "Custom MCP server creation should succeed.");

        snapshot = application.snapshot();
        const auto swiftToolsMcpEndpoint = findEndpoint(snapshot.endpoints, "swift-tools-mcp");
        success &= expect(swiftToolsMcpEndpoint.has_value(), "Custom MCP servers should be reflected in the runtime snapshot.");
        success &= expect(
            swiftToolsMcpEndpoint.has_value() &&
                swiftToolsMcpEndpoint->kind == MasterControl::EndpointKind::MCPServer &&
                swiftToolsMcpEndpoint->userDefined,
            "Custom MCP servers should be marked as user-defined shared runtime endpoints.");
        success &= expect(
            !hasAssignmentTarget(snapshot.providerAssignmentTargets, "swift-tools-mcp"),
            "Custom MCP servers should remain shared infrastructure, not provider ownership targets.");

        const auto customSubAgentResult = application.upsertSubAgentJson(nlohmann::json{
            { "id", "swift-agent" },
            { "displayName", "Swift Agent" },
            { "kind", "sub_agent" },
            { "host", "" },
            { "port", 0 },
            { "protocol", "virtual" },
            { "routePath", "" },
            { "description", "Custom Swift coding specialist lane." },
            { "specialization", "Swift" },
            { "userDefined", true }
        }.dump());
        success &= expect(customSubAgentResult.succeeded, "Custom sub-agent creation should succeed.");

        snapshot = application.snapshot();
        const auto swiftAgentEndpoint = findEndpoint(snapshot.endpoints, "swift-agent");
        success &= expect(swiftAgentEndpoint.has_value(), "Custom sub-agents should be reflected in the runtime snapshot.");
        success &= expect(
            swiftAgentEndpoint.has_value() && swiftAgentEndpoint->userDefined,
            "Custom sub-agents should be marked as user-defined lanes.");
        success &= expect(
            swiftAgentEndpoint.has_value() && swiftAgentEndpoint->specialization == "Swift",
            "Custom sub-agents should persist their specialization.");
        success &= expect(
            hasAssignmentTarget(snapshot.providerAssignmentTargets, "swift-agent"),
            "Custom sub-agents should be published as provider ownership targets.");

        const auto swiftAgentAssignmentResult = application.upsertProviderAssignmentJson(nlohmann::json{
            { "targetId", "swift-agent" },
            { "kind", "sub_agent" },
            { "providerId", "ops-provider" }
        }.dump());
        success &= expect(swiftAgentAssignmentResult.succeeded, "Custom sub-agents should support individual provider ownership.");

        const auto plannerAssignmentResult = application.upsertProviderAssignmentJson(nlohmann::json{
            { "targetId", "planner" },
            { "kind", "role" },
            { "providerId", "ops-provider" }
        }.dump());
        success &= expect(plannerAssignmentResult.succeeded, "Planner ownership assignment should succeed.");

        const auto groupAssignmentResult = application.upsertProviderAssignmentJson(nlohmann::json{
            { "targetId", "coding-specialists" },
            { "kind", "sub_agent_group" },
            { "providerId", "ops-provider" }
        }.dump());
        success &= expect(groupAssignmentResult.succeeded, "Sub-agent group ownership assignment should fan out successfully.");

        const auto customGroupResult = application.upsertSubAgentGroupJson(nlohmann::json{
            { "groupId", "swift-specialists" },
            { "displayName", "Swift Specialists" },
            { "description", "Apple-platform coding specialists." },
            { "memberTargetIds", nlohmann::json::array({ "sentinel", "forge" }) }
        }.dump());
        success &= expect(customGroupResult.succeeded, "Custom sub-agent group creation should succeed.");

        const auto reviewProviderResult = application.upsertProviderJson(nlohmann::json{
            { "id", "review-provider" },
            { "kind", "codex" },
            { "displayName", "Review Provider" },
            { "baseUrl", "https://api.openai.example/v1" },
            { "modelId", "gpt-5.4" },
            { "enabled", true },
            { "allowAutonomousControl", false }
        }.dump());
        success &= expect(reviewProviderResult.succeeded, "A second provider route should save for override testing.");

        snapshot = application.snapshot();
        success &= expect(
            findSubAgentGroup(snapshot.subAgentGroups, "swift-specialists").has_value(),
            "Custom sub-agent groups should be reflected in the runtime snapshot.");
        success &= expect(
            hasAssignmentTarget(snapshot.providerAssignmentTargets, "swift-specialists"),
            "Custom sub-agent groups should be published as provider assignment targets.");

        const auto customGroupAssignmentResult = application.upsertProviderAssignmentJson(nlohmann::json{
            { "targetId", "swift-specialists" },
            { "kind", "sub_agent_group" },
            { "providerId", "ops-provider" }
        }.dump());
        success &= expect(customGroupAssignmentResult.succeeded, "Custom sub-agent groups should support provider ownership fan-out.");

        snapshot = application.snapshot();
        success &= expect(
            findAssignment(snapshot.providerAssignments, "swift-specialists").has_value(),
            "Custom sub-agent group ownership should persist as an explicit group assignment.");

        const auto sentinelOverrideResult = application.upsertProviderAssignmentJson(nlohmann::json{
            { "targetId", "sentinel" },
            { "kind", "sub_agent" },
            { "providerId", "review-provider" }
        }.dump());
        success &= expect(sentinelOverrideResult.succeeded, "Individual sub-agent ownership should override a group baseline.");

        const auto clearCustomGroupResult = application.upsertProviderAssignmentJson(nlohmann::json{
            { "targetId", "swift-specialists" },
            { "kind", "sub_agent_group" },
            { "providerId", "" }
        }.dump());
        success &= expect(clearCustomGroupResult.succeeded, "Clearing a custom group ownership assignment should succeed.");

        snapshot = application.snapshot();
        const auto credentialStatus = findCredentialStatus(snapshot.providerCredentialStatuses, "ops-provider");
        success &= expect(
            credentialStatus.has_value() && credentialStatus->configured,
            "Provider credential status should reflect the secure store update.");
        success &= expect(
            findAssignment(snapshot.providerAssignments, "planner").has_value(),
            "Provider ownership assignments should be reflected in the runtime snapshot.");
        success &= expect(
            findAssignment(snapshot.providerAssignments, "sentinel").has_value(),
            "Sub-agent group ownership should fan out to the live sub-agent pool.");
        const auto sentinelAssignment = findAssignment(snapshot.providerAssignments, "sentinel");
        success &= expect(
            sentinelAssignment.has_value() && sentinelAssignment->providerId == "review-provider",
            "Clearing a custom group should preserve a later explicit sub-agent override.");
        success &= expect(
            !findAssignment(snapshot.providerAssignments, "swift-specialists").has_value(),
            "Clearing a custom group should remove the explicit group ownership record.");
        success &= expect(
            !findAssignment(snapshot.providerAssignments, "forge").has_value(),
            "Clearing a custom group should remove remaining group-fanout ownership for untouched members.");
        success &= expect(
            findAssignment(snapshot.providerAssignments, "swift-agent").has_value(),
            "Custom sub-agent ownership should persist independently of unrelated group changes.");
        success &= expect(hasExport(snapshot.exports, "master-control-gateway-profile.json"), "Exports should include the gateway profile");
        success &= expect(
            hasProviderExecutionRegistration(snapshot.providerExecutionRegistrations, "codex") &&
                hasProviderExecutionRegistration(snapshot.providerExecutionRegistrations, "claude-code") &&
                hasProviderExecutionRegistration(snapshot.providerExecutionRegistrations, "xai-grok"),
            "Provider modules should register their execution transports with the runtime.");

        const auto removeCustomSubAgentResult = application.removeSubAgentJson(nlohmann::json{
            { "subAgentId", "swift-agent" }
        }.dump());
        success &= expect(removeCustomSubAgentResult.succeeded, "Custom sub-agents should be removable.");

        snapshot = application.snapshot();
        success &= expect(
            !findEndpoint(snapshot.endpoints, "swift-agent").has_value(),
            "Removing a custom sub-agent should remove it from the runtime snapshot.");
        success &= expect(
            !hasAssignmentTarget(snapshot.providerAssignmentTargets, "swift-agent"),
            "Removing a custom sub-agent should remove it from the ownership target list.");
        success &= expect(
            !findAssignment(snapshot.providerAssignments, "swift-agent").has_value(),
            "Removing a custom sub-agent should clear its provider ownership assignment.");

        {
            std::atomic<int> providerRequestCount{ 0 };
            SimpleHttpServer providerServer([&providerRequestCount](const TestHttpRequest&) {
                const int requestIndex = providerRequestCount.fetch_add(1);
                if (requestIndex == 0) {
                    return TestHttpResponse{
                        200,
                        "application/json",
                        nlohmann::json{
                            { "choices", nlohmann::json::array({
                                {
                                    { "message", {
                                        { "role", "assistant" },
                                        { "content", "" },
                                        { "tool_calls", nlohmann::json::array({
                                            {
                                                { "id", "call-1" },
                                                { "type", "function" },
                                                { "function", {
                                                    { "name", "master_control_list_mcp_servers" },
                                                    { "arguments", "{}" }
                                                } }
                                            }
                                        }) }
                                    } }
                                }
                            }) }
                        }.dump()
                    };
                }

                return TestHttpResponse{
                    200,
                    "application/json",
                    nlohmann::json{
                        { "choices", nlohmann::json::array({
                            {
                                { "message", {
                                    { "role", "assistant" },
                                    { "content", "Planner route complete." }
                                } }
                            }
                        }) }
                    }.dump()
                };
            });

            const auto providerRouteResult = application.upsertProviderJson(nlohmann::json{
                { "id", "local-xai-provider" },
                { "kind", "xai" },
                { "displayName", "Local xAI Provider" },
                { "baseUrl", "http://127.0.0.1:" + std::to_string(providerServer.port()) + "/v1" },
                { "modelId", "grok-code-fast-1" },
                { "enabled", true },
                { "allowAutonomousControl", false }
            }.dump());
            success &= expect(providerRouteResult.succeeded, "Local xAI provider route should save successfully.");

            const auto providerCredentialResult = application.upsertProviderCredentialsJson(nlohmann::json{
                { "providerId", "local-xai-provider" },
                { "values", nlohmann::json{
                    { "xai_api_key", "local-xai-key" }
                } }
            }.dump());
            success &= expect(providerCredentialResult.succeeded, "Local xAI provider credentials should save successfully.");

            const auto plannerOwnershipResult = application.upsertProviderAssignmentJson(nlohmann::json{
                { "targetId", "planner" },
                { "kind", "role" },
                { "providerId", "local-xai-provider" }
            }.dump());
            success &= expect(plannerOwnershipResult.succeeded, "Planner ownership should be reassigned to the local xAI provider.");

            const auto executionRecord = application.executeProviderTaskJson(nlohmann::json{
                { "targetId", "planner" },
                { "prompt", "Review shared MCP capacity and produce a planning response." },
                { "allowToolAccess", true },
                { "maxTurns", 3 }
            }.dump());
            success &= expect(
                executionRecord.status == MasterControl::ProviderExecutionStatus::Succeeded,
                "OpenAI-compatible provider execution should succeed against the local test server.");
            success &= expect(executionRecord.providerId == "local-xai-provider", "Execution should route to the assigned provider.");
            success &= expect(executionRecord.outputText == "Planner route complete.", "Execution should return the provider's final assistant text.");
            success &= expect(
                hasProviderExecutionTool(executionRecord, "master_control_list_mcp_servers"),
                "OpenAI-compatible provider execution should record shared MCP tool usage.");
            success &= expect(
                !executionRecord.referencedMcpServerIds.empty(),
                "OpenAI-compatible provider execution should receive the shared MCP endpoint set.");
            success &= expect(
                std::find(
                    executionRecord.referencedMcpServerIds.begin(),
                    executionRecord.referencedMcpServerIds.end(),
                    "swift-tools-mcp") != executionRecord.referencedMcpServerIds.end(),
                "OpenAI-compatible provider execution should include custom shared MCP servers in the shared endpoint set.");

            const auto providerRequests = providerServer.requests();
            success &= expect(providerRequests.size() == 2, "OpenAI-compatible execution should complete a tool-call round trip.");
            if (providerRequests.size() >= 2) {
                success &= expect(providerRequests[0].path == "/v1/chat/completions", "Provider requests should target the OpenAI-compatible chat completions route.");
                success &= expect(
                    providerRequests[0].headers.find("authorization") != providerRequests[0].headers.end() &&
                        providerRequests[0].headers.at("authorization") == "Bearer local-xai-key",
                    "Provider execution should forward bearer authorization.");

                const auto firstPayload = nlohmann::json::parse(providerRequests[0].body);
                success &= expect(firstPayload.contains("tools"), "OpenAI-compatible execution should publish shared MCP tools to the provider.");
                success &= expect(firstPayload.value("model", "") == "grok-code-fast-1", "OpenAI-compatible execution should send the selected model.");

                const auto secondPayload = nlohmann::json::parse(providerRequests[1].body);
                success &= expect(secondPayload.contains("messages"), "Tool-followup requests should include the full message transcript.");
                const auto hasToolRole = std::any_of(
                    secondPayload.at("messages").begin(),
                    secondPayload.at("messages").end(),
                    [](const auto& message) {
                        return message.is_object() && message.value("role", "") == "tool";
                    });
                success &= expect(hasToolRole, "Tool-followup requests should append a tool response message.");
            }

            snapshot = application.snapshot();
            success &= expect(
                !snapshot.providerExecutionHistory.empty() &&
                    snapshot.providerExecutionHistory.front().executionId == executionRecord.executionId,
                "Successful provider execution should be persisted to runtime history.");

            const auto removeCustomMcpServerResult = application.removeMcpServerJson(nlohmann::json{
                { "mcpServerId", "swift-tools-mcp" }
            }.dump());
            success &= expect(removeCustomMcpServerResult.succeeded, "Custom MCP servers should be removable.");

            snapshot = application.snapshot();
            success &= expect(
                !findEndpoint(snapshot.endpoints, "swift-tools-mcp").has_value(),
                "Removing a custom MCP server should remove it from the runtime snapshot.");
        }

        {
            const auto fakeClaudeCommand = tempRoot / "fake-claude.cmd";
            const auto fakeClaudeArgs = tempRoot / "fake-claude-args.txt";
            const auto fakeClaudeKey = tempRoot / "fake-claude-key.txt";
            writeTextFile(
                fakeClaudeCommand,
                "@echo off\r\n"
                "setlocal\r\n"
                "set \"ARGS_FILE=" + utf8FromWide(fakeClaudeArgs.wstring()) + "\"\r\n"
                "set \"KEY_FILE=" + utf8FromWide(fakeClaudeKey.wstring()) + "\"\r\n"
                "echo %* > \"%ARGS_FILE%\"\r\n"
                "echo %ANTHROPIC_API_KEY% > \"%KEY_FILE%\"\r\n"
                "echo {\"result\":\"Claude execution ok.\"}\r\n"
                "exit /b 0\r\n");

            ScopedEnvironmentOverride claudeCommandOverride(L"MASTERCONTROL_CLAUDE_COMMAND", fakeClaudeCommand.wstring());

            const auto claudeProviderResult = application.upsertProviderJson(nlohmann::json{
                { "id", "local-claude-provider" },
                { "kind", "claude_code" },
                { "displayName", "Local Claude Provider" },
                { "baseUrl", "https://api.anthropic.com" },
                { "modelId", "claude-sonnet-4-5" },
                { "enabled", true },
                { "allowAutonomousControl", false }
            }.dump());
            success &= expect(claudeProviderResult.succeeded, "Local Claude Code provider route should save successfully.");

            const auto claudeCredentialResult = application.upsertProviderCredentialsJson(nlohmann::json{
                { "providerId", "local-claude-provider" },
                { "values", nlohmann::json{
                    { "anthropic_api_key", "local-anthropic-key" }
                } }
            }.dump());
            success &= expect(claudeCredentialResult.succeeded, "Local Claude Code credentials should save successfully.");

            const auto architectOwnershipResult = application.upsertProviderAssignmentJson(nlohmann::json{
                { "targetId", "architect" },
                { "kind", "role" },
                { "providerId", "local-claude-provider" }
            }.dump());
            success &= expect(architectOwnershipResult.succeeded, "Architect ownership should be assigned to the local Claude provider.");

            const auto claudeExecution = application.executeProviderTaskJson(nlohmann::json{
                { "targetId", "architect" },
                { "prompt", "Produce a concise architecture note." },
                { "allowToolAccess", true },
                { "maxTurns", 2 }
            }.dump());
            success &= expect(
                claudeExecution.status == MasterControl::ProviderExecutionStatus::Succeeded,
                "Claude Code execution should succeed against the local CLI shim.");
            success &= expect(claudeExecution.providerId == "local-claude-provider", "Claude execution should route to the assigned provider.");
            success &= expect(claudeExecution.outputText == "Claude execution ok.", "Claude execution should parse JSON CLI output.");
            success &= expect(
                hasProviderExecutionTool(claudeExecution, "claude_code_cli_mcp"),
                "Claude execution should record direct MCP configuration usage when tool access is enabled.");
            success &= expect(std::filesystem::exists(fakeClaudeArgs), "Claude execution should invoke the configured CLI shim.");
            success &= expect(std::filesystem::exists(fakeClaudeKey), "Claude execution should project provider credentials into the CLI environment.");
            if (std::filesystem::exists(fakeClaudeArgs)) {
                const auto argsFile = readFileUtf8(fakeClaudeArgs);
                success &= expect(argsFile.has_value() && argsFile->find("--output-format json") != std::string::npos, "Claude execution should request JSON output mode.");
                success &= expect(argsFile.has_value() && argsFile->find("--append-system-prompt-file") != std::string::npos, "Claude execution should pass the system prompt file.");
                success &= expect(argsFile.has_value() && argsFile->find("--mcp-config") != std::string::npos, "Claude execution should pass the generated MCP config when tool access is enabled.");
            }
            if (std::filesystem::exists(fakeClaudeKey)) {
                const auto keyFile = readFileUtf8(fakeClaudeKey);
                success &= expect(keyFile.has_value() && keyFile->find("local-anthropic-key") != std::string::npos, "Claude execution should forward the configured API key.");
            }

            snapshot = application.snapshot();
            success &= expect(
                !snapshot.providerExecutionHistory.empty() &&
                    snapshot.providerExecutionHistory.front().executionId == claudeExecution.executionId,
                "Claude execution should be persisted to runtime history.");
        }

        if (toolExists(L"pwsh.exe")) {
            const auto packageScript = tempRoot / "package-install.ps1";
            const auto markerFile = tempRoot / "package-install.ok";
            writeTextFile(
                packageScript,
                "New-Item -Path (Join-Path $PSScriptRoot 'package-install.ok') -ItemType File -Force | Out-Null\nexit 0\n");

            const auto packageSource = packageScript.string();
            const auto packageResult = application.installPackageJson(nlohmann::json{
                { "kind", "powershell" },
                { "localPath", packageSource },
                { "arguments", "" }
            }.dump());
            success &= expect(packageResult.succeeded, "Local PowerShell package install should succeed");
            success &= expect(std::filesystem::exists(markerFile), "Local PowerShell package install should execute the payload");

            snapshot = application.snapshot();
            success &= expect(
                hasHistoryEntry(snapshot.installHistory, MasterControl::InstallerKind::PowerShell, packageSource),
                "Local PowerShell package install should be recorded in history");
        } else {
            std::cout << "Skipping package import test because pwsh.exe was not found.\n";
        }

        if (toolExists(L"git.exe")) {
            const auto repoRoot = tempRoot / "repo-fixture";
            success &= expect(createGitImportFixture(repoRoot), "Repository fixture should be created successfully");
            if (success) {
                const auto repoSource = repoRoot.string();
                const auto repoResult = application.installRepoJson(nlohmann::json{
                    { "repositoryUrl", repoSource },
                    { "branch", "main" },
                    { "manifestFile", "mcp-bootstrap.json" }
                }.dump());
                success &= expect(repoResult.succeeded, "Repository import should succeed");

                snapshot = application.snapshot();
                const auto repoEndpoint = findEndpoint(snapshot.endpoints, "repo-import-endpoint");
                success &= expect(repoEndpoint.has_value(), "Repository import should register endpoints");
                success &= expect(repoEndpoint.has_value() && repoEndpoint->host == configuration.activeProfile.preferredBindAddress, "Repository import should backfill missing hosts");
                success &= expect(hasProvider(snapshot.providers, "repo-import-provider"), "Repository import should register providers");
                success &= expect(hasHistoryEntry(snapshot.installHistory, MasterControl::InstallerKind::GitBootstrapRepo, repoSource), "Repository import should be recorded in history");
            }
        } else {
            std::cout << "Skipping repository import test because git.exe was not found.\n";
        }

        if (toolExists(L"pwsh.exe")) {
            const auto zipRoot = tempRoot / "zip-fixture";
            const auto zipPath = tempRoot / "zip-fixture.zip";
            success &= expect(createZipImportFixture(zipRoot, zipPath), "Zip fixture should be created successfully");
            if (success) {
                const auto zipSource = zipPath.string();
                const auto zipResult = application.installZipJson(nlohmann::json{
                    { "source", zipSource },
                    { "manifestFile", "mcp-bootstrap.json" }
                }.dump());
                success &= expect(zipResult.succeeded, "Zip import should succeed");

                snapshot = application.snapshot();
                const auto zipEndpoint = findEndpoint(snapshot.endpoints, "zip-import-endpoint");
                success &= expect(zipEndpoint.has_value(), "Zip import should register endpoints");
                success &= expect(zipEndpoint.has_value() && zipEndpoint->host == configuration.activeProfile.preferredBindAddress, "Zip import should backfill missing hosts");
                success &= expect(hasProvider(snapshot.providers, "zip-import-provider"), "Zip import should register providers");
                success &= expect(hasHistoryEntry(snapshot.installHistory, MasterControl::InstallerKind::ZipBundle, zipSource), "Zip import should be recorded in history");
            }
        } else {
            std::cout << "Skipping zip import test because pwsh.exe was not found.\n";
        }

        application.shutdown();
    }

    const auto bootstrapperBinary = bootstrapperBinaryPath();
    success &= expect(std::filesystem::exists(bootstrapperBinary), "Bootstrapper binary should exist for installer validation");
    if (success) {
        const auto bootstrapInstallDirectory = tempRoot / "bootstrapper-install";
        const auto bootstrapDataDirectory = tempRoot / "bootstrapper-data";
        const auto bootstrapConfigurationFile = bootstrapDataDirectory / "config" / "master-control-program.json";
        const auto bootstrapInstallStateFile = bootstrapInstallDirectory / "installation-state.json";

        ScopedEnvironmentOverride bootstrapDataOverride(L"MASTERCONTROL_DATA_DIR", bootstrapDataDirectory.wstring());

        const auto installCommand = L"\"" + bootstrapperBinary.wstring() + L"\" install \"" +
            bootstrapInstallDirectory.wstring() +
            L"\" --skip-service --skip-firewall --skip-shortcuts --skip-uninstall-registration";
        success &= expect(
            runProcess(installCommand, tempRoot) == 0,
            "Bootstrapper install should succeed when system integrations are skipped");
        success &= expect(
            std::filesystem::exists(bootstrapInstallDirectory / "MasterControlServiceHost.exe"),
            "Bootstrapper install should stage the service host");
        success &= expect(
            std::filesystem::exists(bootstrapInstallDirectory / "MasterControlShell.exe"),
            "Bootstrapper install should stage the shell host");
        success &= expect(
            std::filesystem::exists(bootstrapInstallDirectory / "share" / "MasterControlProgram" / "web" / "index.html"),
            "Bootstrapper install should stage browser resources");
        success &= expect(
            std::filesystem::exists(bootstrapInstallDirectory / "share" / "MasterControlProgram" / "ForsettiManifests" / "DashboardUIModule.json"),
            "Bootstrapper install should stage Forsetti manifests");
        success &= expect(
            std::filesystem::exists(bootstrapInstallStateFile),
            "Bootstrapper install should write installation state");
        success &= expect(
            std::filesystem::exists(bootstrapConfigurationFile),
            "Bootstrapper install should seed configuration in the configured data directory");

        std::filesystem::remove(bootstrapConfigurationFile);
        const auto repairCommand = L"\"" + bootstrapperBinary.wstring() + L"\" repair \"" +
            bootstrapInstallDirectory.wstring() +
            L"\" --skip-service --skip-firewall --skip-shortcuts --skip-uninstall-registration";
        success &= expect(
            runProcess(repairCommand, tempRoot) == 0,
            "Bootstrapper repair should succeed when system integrations are skipped");
        success &= expect(
            std::filesystem::exists(bootstrapConfigurationFile),
            "Bootstrapper repair should reseed missing configuration");

        const auto uninstallCommand = L"\"" + bootstrapperBinary.wstring() + L"\" uninstall \"" +
            bootstrapInstallDirectory.wstring() +
            L"\" --purge-install-dir --purge-data --skip-service --skip-firewall --skip-shortcuts --skip-uninstall-registration";
        success &= expect(
            runProcess(uninstallCommand, tempRoot) == 0,
            "Bootstrapper uninstall should succeed when system integrations are skipped");
        success &= expect(
            !std::filesystem::exists(bootstrapInstallDirectory),
            "Bootstrapper uninstall should remove the install directory when requested");
        success &= expect(
            !std::filesystem::exists(bootstrapDataDirectory),
            "Bootstrapper uninstall should remove the data directory when requested");
    }

    std::filesystem::remove_all(tempRoot);
    return success ? 0 : 1;
}
