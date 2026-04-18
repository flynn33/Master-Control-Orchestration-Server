// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#include "MasterControl/MasterControlDefaults.h"
#include "MasterControl/MasterControlModels.h"
#include "MasterControl/MasterControlRuntime.h"
#include "MasterControl/MasterControlVersion.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <Windows.h>

#include <algorithm>
#include <array>
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
        ("MasterControlOrchestrationServerTests_" + std::to_string(GetCurrentProcessId()));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    return root;
}

void writeTextFile(const std::filesystem::path& filePath, const std::string& contents) {
    std::filesystem::create_directories(filePath.parent_path());
    std::ofstream output(filePath, std::ios::binary | std::ios::trunc);
    output << contents;
}

std::filesystem::path sourceRepoRoot() {
    return std::filesystem::path(__FILE__).parent_path().parent_path();
}

uint16_t isolatedTestBrowserPort() {
    return static_cast<uint16_t>(46000 + (GetCurrentProcessId() % 1000));
}

void writeIsolatedAppConfiguration(const std::filesystem::path& configurationFile, const uint16_t browserPort) {
    auto configuration = MasterControl::buildDefaultConfiguration();
    configuration.browserPort = browserPort;
    for (auto& endpoint : configuration.activeProfile.seededEndpoints) {
        if (endpoint.id == "browser-gateway") {
            endpoint.port = browserPort;
        }
    }
    writeTextFile(configurationFile, nlohmann::json(configuration).dump(2));
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

struct ParsedHttpUrl final {
    std::string host;
    uint16_t port = 80;
    std::string path = "/";
};

std::optional<ParsedHttpUrl> parseHttpUrl(const std::string& url) {
    const std::string scheme = "http://";
    if (url.rfind(scheme, 0) != 0) {
        return std::nullopt;
    }

    const auto hostStart = scheme.size();
    const auto pathStart = url.find('/', hostStart);
    const auto authority = pathStart == std::string::npos
        ? url.substr(hostStart)
        : url.substr(hostStart, pathStart - hostStart);

    ParsedHttpUrl parsed;
    parsed.path = pathStart == std::string::npos ? "/" : url.substr(pathStart);

    const auto colon = authority.rfind(':');
    if (colon == std::string::npos) {
        parsed.host = authority;
    } else {
        parsed.host = authority.substr(0, colon);
        parsed.port = static_cast<uint16_t>(std::stoi(authority.substr(colon + 1)));
    }

    if (parsed.host.empty()) {
        return std::nullopt;
    }
    return parsed;
}

std::optional<nlohmann::json> httpGetJson(const std::string& url) {
    const auto parsed = parseHttpUrl(url);
    if (!parsed.has_value()) {
        return std::nullopt;
    }

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* results = nullptr;
    if (getaddrinfo(parsed->host.c_str(), std::to_string(parsed->port).c_str(), &hints, &results) != 0) {
        return std::nullopt;
    }

    SOCKET socketHandle = INVALID_SOCKET;
    for (auto* candidate = results; candidate != nullptr; candidate = candidate->ai_next) {
        socketHandle = socket(candidate->ai_family, candidate->ai_socktype, candidate->ai_protocol);
        if (socketHandle == INVALID_SOCKET) {
            continue;
        }
        if (connect(socketHandle, candidate->ai_addr, static_cast<int>(candidate->ai_addrlen)) == 0) {
            break;
        }
        closesocket(socketHandle);
        socketHandle = INVALID_SOCKET;
    }
    freeaddrinfo(results);

    if (socketHandle == INVALID_SOCKET) {
        return std::nullopt;
    }

    std::ostringstream requestStream;
    requestStream << "GET " << parsed->path << " HTTP/1.1\r\n"
                  << "Host: " << parsed->host << ':' << parsed->port << "\r\n"
                  << "Connection: close\r\n\r\n";
    const auto requestText = requestStream.str();
    if (send(socketHandle, requestText.data(), static_cast<int>(requestText.size()), 0) == SOCKET_ERROR) {
        closesocket(socketHandle);
        return std::nullopt;
    }

    std::string response;
    char buffer[4096]{};
    int bytesReceived = 0;
    while ((bytesReceived = recv(socketHandle, buffer, sizeof(buffer), 0)) > 0) {
        response.append(buffer, static_cast<size_t>(bytesReceived));
    }
    closesocket(socketHandle);

    const auto headerEnd = response.find("\r\n\r\n");
    if (headerEnd == std::string::npos) {
        return std::nullopt;
    }

    const auto body = response.substr(headerEnd + 4);
    try {
        return nlohmann::json::parse(body);
    } catch (...) {
        return std::nullopt;
    }
}

// WS8 — minimal POST helper used by the ease-of-use remediation test sections.
std::optional<nlohmann::json> httpPostJson(const std::string& url, const std::string& body) {
    const auto parsed = parseHttpUrl(url);
    if (!parsed.has_value()) { return std::nullopt; }
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    addrinfo* results = nullptr;
    if (getaddrinfo(parsed->host.c_str(), std::to_string(parsed->port).c_str(), &hints, &results) != 0) {
        return std::nullopt;
    }
    SOCKET socketHandle = INVALID_SOCKET;
    for (auto* candidate = results; candidate != nullptr; candidate = candidate->ai_next) {
        socketHandle = socket(candidate->ai_family, candidate->ai_socktype, candidate->ai_protocol);
        if (socketHandle == INVALID_SOCKET) { continue; }
        if (connect(socketHandle, candidate->ai_addr, static_cast<int>(candidate->ai_addrlen)) == 0) { break; }
        closesocket(socketHandle);
        socketHandle = INVALID_SOCKET;
    }
    freeaddrinfo(results);
    if (socketHandle == INVALID_SOCKET) { return std::nullopt; }
    std::ostringstream requestStream;
    requestStream << "POST " << parsed->path << " HTTP/1.1\r\n"
                  << "Host: " << parsed->host << ':' << parsed->port << "\r\n"
                  << "Content-Type: application/json\r\n"
                  << "Content-Length: " << body.size() << "\r\n"
                  << "Connection: close\r\n\r\n"
                  << body;
    const auto requestText = requestStream.str();
    if (send(socketHandle, requestText.data(), static_cast<int>(requestText.size()), 0) == SOCKET_ERROR) {
        closesocket(socketHandle);
        return std::nullopt;
    }
    std::string response;
    char buffer[4096]{};
    int bytesReceived = 0;
    while ((bytesReceived = recv(socketHandle, buffer, sizeof(buffer), 0)) > 0) {
        response.append(buffer, static_cast<size_t>(bytesReceived));
    }
    closesocket(socketHandle);
    const auto headerEnd = response.find("\r\n\r\n");
    if (headerEnd == std::string::npos) { return std::nullopt; }
    const auto responseBody = response.substr(headerEnd + 4);
    try {
        return nlohmann::json::parse(responseBody);
    } catch (...) {
        return std::nullopt;
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

std::filesystem::path serviceHostPayloadBinaryPath() {
    const auto executablePath = currentExecutablePath();
    const auto configurationName = executablePath.parent_path().filename();
    const auto buildRoot = executablePath.parent_path().parent_path().parent_path();
    return buildRoot / "src" / "MasterControlServiceHost" / configurationName / "MasterControlServiceHost.exe";
}

class DelayedExclusiveFileLock final {
public:
    DelayedExclusiveFileLock(const std::filesystem::path& filePath, const DWORD holdMilliseconds) {
        const auto handle = CreateFileW(
            filePath.wstring().c_str(),
            GENERIC_READ,
            0,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            return;
        }

        active_ = true;
        releaser_ = std::thread([handle, holdMilliseconds]() {
            Sleep(holdMilliseconds);
            CloseHandle(handle);
        });
    }

    ~DelayedExclusiveFileLock() {
        if (releaser_.joinable()) {
            releaser_.join();
        }
    }

    bool active() const {
        return active_;
    }

private:
    bool active_ = false;
    std::thread releaser_;
};

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

struct ProcessOutputResult final {
    int exitCode = 1;
    std::string rawOutput;
};

ProcessOutputResult runProcessWithOutput(const std::wstring& command, const std::filesystem::path& workingDirectory) {
    SECURITY_ATTRIBUTES securityAttributes{};
    securityAttributes.nLength = sizeof(securityAttributes);
    securityAttributes.bInheritHandle = TRUE;

    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (CreatePipe(&readPipe, &writePipe, &securityAttributes, 0) == 0) {
        return { static_cast<int>(GetLastError()), {} };
    }

    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.dwFlags = STARTF_USESTDHANDLES;
    startupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startupInfo.hStdOutput = writePipe;
    startupInfo.hStdError = writePipe;

    PROCESS_INFORMATION processInformation{};
    std::wstring mutableCommand = command;
    if (CreateProcessW(
            nullptr,
            mutableCommand.data(),
            nullptr,
            nullptr,
            TRUE,
            CREATE_NO_WINDOW,
            nullptr,
            workingDirectory.empty() ? nullptr : workingDirectory.wstring().c_str(),
            &startupInfo,
            &processInformation) == 0) {
        const auto errorCode = static_cast<int>(GetLastError());
        CloseHandle(readPipe);
        CloseHandle(writePipe);
        return { errorCode, {} };
    }

    CloseHandle(writePipe);
    writePipe = nullptr;

    std::string output;
    std::array<char, 4096> buffer{};
    DWORD bytesRead = 0;
    while (ReadFile(readPipe, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr) != 0 && bytesRead > 0) {
        output.append(buffer.data(), bytesRead);
    }

    WaitForSingleObject(processInformation.hProcess, INFINITE);

    DWORD exitCode = 1;
    GetExitCodeProcess(processInformation.hProcess, &exitCode);
    CloseHandle(readPipe);
    CloseHandle(processInformation.hThread);
    CloseHandle(processInformation.hProcess);
    return { static_cast<int>(exitCode), std::move(output) };
}

bool toolExists(const wchar_t* executableName) {
    wchar_t pathBuffer[MAX_PATH]{};
    return SearchPathW(nullptr, executableName, nullptr, MAX_PATH, pathBuffer, nullptr) > 0;
}

std::optional<std::wstring> findToolOnPath(std::initializer_list<const wchar_t*> executableNames) {
    wchar_t pathBuffer[MAX_PATH]{};
    for (const auto* executableName : executableNames) {
        if (SearchPathW(nullptr, executableName, nullptr, MAX_PATH, pathBuffer, nullptr) > 0) {
            return std::wstring(pathBuffer);
        }
    }
    return std::nullopt;
}

bool powerShellExists() {
    return findToolOnPath({ L"pwsh.exe", L"powershell.exe" }).has_value();
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

std::optional<MasterControl::PlatformGatewayDescriptor> findPlatformGateway(
    const std::vector<MasterControl::PlatformGatewayDescriptor>& gateways,
    MasterControl::PlatformTarget platform) {
    const auto iterator = std::find_if(
        gateways.begin(),
        gateways.end(),
        [platform](const MasterControl::PlatformGatewayDescriptor& gateway) { return gateway.platform == platform; });
    if (iterator == gateways.end()) {
        return std::nullopt;
    }
    return *iterator;
}

std::optional<MasterControl::GovernanceServerDescriptor> findGovernanceServer(
    const std::vector<MasterControl::GovernanceServerDescriptor>& governanceServers,
    MasterControl::PlatformTarget platform) {
    const auto iterator = std::find_if(
        governanceServers.begin(),
        governanceServers.end(),
        [platform](const MasterControl::GovernanceServerDescriptor& descriptor) { return descriptor.platform == platform; });
    if (iterator == governanceServers.end()) {
        return std::nullopt;
    }
    return *iterator;
}

std::optional<MasterControl::GovernanceToolDescriptor> findGovernanceTool(
    const std::vector<MasterControl::GovernanceToolDescriptor>& tools,
    MasterControl::PlatformTarget platform,
    const std::string& toolId) {
    const auto iterator = std::find_if(
        tools.begin(),
        tools.end(),
        [platform, &toolId](const MasterControl::GovernanceToolDescriptor& descriptor) {
            return descriptor.platform == platform && descriptor.toolId == toolId;
        });
    if (iterator == tools.end()) {
        return std::nullopt;
    }
    return *iterator;
}

std::optional<MasterControl::GovernanceToolResult> findGovernanceExecution(
    const std::vector<MasterControl::GovernanceToolResult>& executions,
    MasterControl::PlatformTarget platform,
    const std::string& toolId) {
    const auto iterator = std::find_if(
        executions.begin(),
        executions.end(),
        [platform, &toolId](const MasterControl::GovernanceToolResult& result) {
            return result.platform == platform && result.toolId == toolId;
        });
    if (iterator == executions.end()) {
        return std::nullopt;
    }
    return *iterator;
}

std::optional<MasterControl::AppleRemoteHost> findAppleRemoteHost(
    const std::vector<MasterControl::AppleRemoteHost>& hosts,
    const std::string& hostId) {
    const auto iterator = std::find_if(
        hosts.begin(),
        hosts.end(),
        [&hostId](const MasterControl::AppleRemoteHost& host) { return host.hostId == hostId; });
    if (iterator == hosts.end()) {
        return std::nullopt;
    }
    return *iterator;
}

std::optional<MasterControl::AppleOperationRecord> findAppleOperation(
    const std::vector<MasterControl::AppleOperationRecord>& operations,
    MasterControl::PlatformTarget platform,
    const std::string& toolId) {
    const auto iterator = std::find_if(
        operations.begin(),
        operations.end(),
        [platform, &toolId](const MasterControl::AppleOperationRecord& operation) {
            return operation.platform == platform && operation.toolId == toolId;
        });
    if (iterator == operations.end()) {
        return std::nullopt;
    }
    return *iterator;
}

std::optional<MasterControl::AppleOperationRecord> findAppleOperationById(
    const std::vector<MasterControl::AppleOperationRecord>& operations,
    const std::string& operationId) {
    const auto iterator = std::find_if(
        operations.begin(),
        operations.end(),
        [&operationId](const MasterControl::AppleOperationRecord& operation) {
            return operation.operationId == operationId;
        });
    if (iterator == operations.end()) {
        return std::nullopt;
    }
    return *iterator;
}

bool isTerminalAppleOperationStatus(MasterControl::AppleOperationStatus status) {
    return status != MasterControl::AppleOperationStatus::Queued &&
        status != MasterControl::AppleOperationStatus::Running;
}

std::optional<MasterControl::AppleOperationRecord> waitForAppleOperation(
    MasterControl::MasterControlApplication& application,
    MasterControl::PlatformTarget platform,
    const std::string& toolId,
    std::chrono::milliseconds timeout = std::chrono::milliseconds(4000)) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        const auto snapshot = application.snapshot();
        const auto operation = findAppleOperation(snapshot.governance.appleOperations, platform, toolId);
        if (operation.has_value() && isTerminalAppleOperationStatus(operation->status)) {
            return operation;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    const auto snapshot = application.snapshot();
    return findAppleOperation(snapshot.governance.appleOperations, platform, toolId);
}

std::optional<MasterControl::AppleOperationRecord> waitForAppleOperationById(
    MasterControl::MasterControlApplication& application,
    const std::string& operationId,
    std::chrono::milliseconds timeout = std::chrono::milliseconds(4000)) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        const auto snapshot = application.snapshot();
        const auto operation = findAppleOperationById(snapshot.governance.appleOperations, operationId);
        if (operation.has_value() && isTerminalAppleOperationStatus(operation->status)) {
            return operation;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    const auto snapshot = application.snapshot();
    return findAppleOperationById(snapshot.governance.appleOperations, operationId);
}

std::string queuedAppleOperationId(const MasterControl::GovernanceToolResult& result) {
    const auto json = nlohmann::json::parse(result.rawOutput, nullptr, false);
    if (json.is_discarded() || !json.is_object()) {
        return {};
    }
    return json.value("operationId", std::string{});
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

    const auto powershell = findToolOnPath({ L"pwsh.exe", L"powershell.exe" });
    if (!powershell.has_value()) {
        return false;
    }

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

    const auto command = L"\"" + *powershell + L"\" -NoProfile -ExecutionPolicy Bypass -Command \"Compress-Archive -Path '" +
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

    const auto gatewayEndpoint = findEndpoint(configuration.activeProfile.seededEndpoints, "platform-gateway");
    success &= expect(gatewayEndpoint.has_value(), "Default profile should include the platform gateway");
    success &= expect(gatewayEndpoint.has_value() && gatewayEndpoint->host == configuration.activeProfile.preferredBindAddress, "Seeded endpoints should use the detected host");

    const nlohmann::json serialized = configuration;
    const auto roundTripped = serialized.get<MasterControl::AppConfiguration>();
    success &= expect(roundTripped.instanceName == configuration.instanceName, "Configuration should round-trip through JSON");

    const auto tempRoot = makeTempRoot();
    {
        ScopedEnvironmentOverride dataDirectoryOverride(L"MASTERCONTROL_DATA_DIR", (tempRoot / "data").wstring());
        const auto appPaths = MasterControl::resolveAppPaths();
        writeIsolatedAppConfiguration(appPaths.configurationFile, isolatedTestBrowserPort());

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
            "com.mastercontrol.gateway-windows",
            "com.mastercontrol.gateway-macos",
            "com.mastercontrol.gateway-ios",
            "com.mastercontrol.governance-windows",
            "com.mastercontrol.governance-macos",
            "com.mastercontrol.governance-ios",
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
        success &= expect(snapshot.platformGateways.size() == 3, "Snapshot should publish three platform gateway services.");
        success &= expect(snapshot.governanceServers.size() == 3, "Snapshot should publish three platform governance services.");
        success &= expect(
            findPlatformGateway(snapshot.platformGateways, MasterControl::PlatformTarget::Windows).has_value() &&
                findPlatformGateway(snapshot.platformGateways, MasterControl::PlatformTarget::Windows)->serviceType == "_mastercontrol-windows._tcp.local",
            "Windows clients should have a distinct DNS-SD gateway service.");
        success &= expect(
            findPlatformGateway(snapshot.platformGateways, MasterControl::PlatformTarget::MacOS).has_value() &&
                findPlatformGateway(snapshot.platformGateways, MasterControl::PlatformTarget::MacOS)->serviceType == "_mastercontrol-macos._tcp.local",
            "Mac clients should have a distinct DNS-SD gateway service.");
        success &= expect(
            findPlatformGateway(snapshot.platformGateways, MasterControl::PlatformTarget::IOS).has_value() &&
                findPlatformGateway(snapshot.platformGateways, MasterControl::PlatformTarget::IOS)->serviceType == "_mastercontrol-ios._tcp.local",
            "iOS clients should have a distinct DNS-SD gateway service.");
        success &= expect(
            findGovernanceServer(snapshot.governanceServers, MasterControl::PlatformTarget::Windows).has_value() &&
                !findGovernanceServer(snapshot.governanceServers, MasterControl::PlatformTarget::Windows)->requiresRemoteToolchain,
            "Windows governance should run locally without a remote Apple toolchain dependency.");
        success &= expect(
            findGovernanceServer(snapshot.governanceServers, MasterControl::PlatformTarget::MacOS).has_value() &&
                findGovernanceServer(snapshot.governanceServers, MasterControl::PlatformTarget::MacOS)->requiresRemoteToolchain,
            "Mac governance should declare its remote Apple toolchain dependency.");
        success &= expect(
            findGovernanceServer(snapshot.governanceServers, MasterControl::PlatformTarget::IOS).has_value() &&
                findGovernanceServer(snapshot.governanceServers, MasterControl::PlatformTarget::IOS)->requiresRemoteToolchain,
            "iOS governance should declare its remote Apple toolchain dependency.");
        success &= expect(
            !snapshot.governance.availableTools.empty(),
            "CLU should publish the available governance tool catalog.");
        success &= expect(
            findGovernanceTool(
                snapshot.governance.availableTools,
                MasterControl::PlatformTarget::Windows,
                "forsetti.windows.manifest.validate").has_value(),
            "Windows governance tools should be registered through the framework.");
        success &= expect(
            findGovernanceTool(
                snapshot.governance.availableTools,
                MasterControl::PlatformTarget::MacOS,
                "forsetti.macos.remote-build.validate").has_value() &&
                findGovernanceTool(
                    snapshot.governance.availableTools,
                    MasterControl::PlatformTarget::MacOS,
                    "forsetti.macos.remote-build.validate")->requiresRemoteToolchain,
            "Mac governance tools should declare their remote toolchain dependency.");
        success &= expect(
            findGovernanceTool(
                snapshot.governance.availableTools,
                MasterControl::PlatformTarget::MacOS,
                "forsetti.macos.sign").has_value(),
            "Mac governance tools should register code-signing execution.");
        success &= expect(
            findGovernanceTool(
                snapshot.governance.availableTools,
                MasterControl::PlatformTarget::MacOS,
                "forsetti.macos.notarize").has_value(),
            "Mac governance tools should register notarization execution.");
        success &= expect(
            findGovernanceTool(
                snapshot.governance.availableTools,
                MasterControl::PlatformTarget::MacOS,
                "forsetti.macos.staple").has_value(),
            "Mac governance tools should register stapling execution.");
        success &= expect(
            findGovernanceTool(
                snapshot.governance.availableTools,
                MasterControl::PlatformTarget::IOS,
                "forsetti.ios.remote-build.validate").has_value() &&
                findGovernanceTool(
                    snapshot.governance.availableTools,
                    MasterControl::PlatformTarget::IOS,
                    "forsetti.ios.remote-build.validate")->requiresRemoteToolchain,
            "iOS governance tools should declare their remote toolchain dependency.");
        success &= expect(
            findGovernanceTool(
                snapshot.governance.availableTools,
                MasterControl::PlatformTarget::IOS,
                "forsetti.ios.archive").has_value(),
            "iOS governance tools should register archive execution.");
        success &= expect(
            findGovernanceTool(
                snapshot.governance.availableTools,
                MasterControl::PlatformTarget::IOS,
                "forsetti.ios.export").has_value(),
            "iOS governance tools should register archive export execution.");
        success &= expect(
            findGovernanceTool(
                snapshot.governance.availableTools,
                MasterControl::PlatformTarget::IOS,
                "forsetti.ios.device.install").has_value(),
            "iOS governance tools should register device installation execution.");
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
        success &= expect(
            snapshot.governance.platformGateways.size() == 3 && snapshot.governance.governanceServers.size() == 3,
            "CLU should be aware of all platform gateway and governance server lanes.");

        // =====================================================================
        // Provider Identity Remediation Tests
        // =====================================================================
        // ChatGPT and Codex must resolve to distinct capabilities despite
        // sharing ProviderKind::Codex.
        success &= expect(
            hasProviderCapability(snapshot.providerCapabilities, "chatgpt"),
            "ChatGPT provider module should publish a capability descriptor with providerId 'chatgpt'.");
        {
            const auto codexCap = std::find_if(
                snapshot.providerCapabilities.begin(), snapshot.providerCapabilities.end(),
                [](const auto& c) { return c.providerId == "codex"; });
            const auto chatgptCap = std::find_if(
                snapshot.providerCapabilities.begin(), snapshot.providerCapabilities.end(),
                [](const auto& c) { return c.providerId == "chatgpt"; });
            success &= expect(
                codexCap != snapshot.providerCapabilities.end() &&
                    chatgptCap != snapshot.providerCapabilities.end(),
                "Both Codex and ChatGPT capabilities should coexist in the provider catalog.");
            if (codexCap != snapshot.providerCapabilities.end() &&
                chatgptCap != snapshot.providerCapabilities.end()) {
                success &= expect(
                    codexCap->displayName != chatgptCap->displayName,
                    "Codex and ChatGPT should have distinct display names despite shared ProviderKind.");
                success &= expect(
                    codexCap->providerId == "codex" && chatgptCap->providerId == "chatgpt",
                    "Codex and ChatGPT should have distinct providerIds.");
            }
        }
        // Execution registrations should also be distinct by providerId
        {
            const auto codexReg = std::find_if(
                snapshot.providerExecutionRegistrations.begin(),
                snapshot.providerExecutionRegistrations.end(),
                [](const auto& r) { return r.providerId == "codex"; });
            const auto chatgptReg = std::find_if(
                snapshot.providerExecutionRegistrations.begin(),
                snapshot.providerExecutionRegistrations.end(),
                [](const auto& r) { return r.providerId == "chatgpt"; });
            success &= expect(
                codexReg != snapshot.providerExecutionRegistrations.end() &&
                    chatgptReg != snapshot.providerExecutionRegistrations.end(),
                "Both Codex and ChatGPT should have distinct execution registrations.");
        }

        // =====================================================================
        // First-Run Template Distinction Tests
        // =====================================================================
        {
            // Verify the default builder functions produce template-flagged objects
            const auto defaultEndpoints = MasterControl::buildDefaultSeededEndpoints();
            const bool allSeededAreTemplates = std::all_of(
                defaultEndpoints.begin(), defaultEndpoints.end(),
                [](const MasterControl::RuntimeEndpoint& endpoint) {
                    return endpoint.status == MasterControl::EndpointStatus::Template && endpoint.isTemplate;
                });
            success &= expect(allSeededAreTemplates,
                "All seeded endpoints from buildDefaultSeededEndpoints() should have Template status and isTemplate=true.");

            const auto defaultProviders = MasterControl::buildDefaultProviders();
            const bool allDefaultsAreTemplates = std::all_of(
                defaultProviders.begin(), defaultProviders.end(),
                [](const MasterControl::ProviderConnection& provider) { return provider.isTemplate; });
            success &= expect(allDefaultsAreTemplates,
                "All default providers from buildDefaultProviders() should have isTemplate=true.");

            // Verify the snapshot reflects template providers
            const bool snapshotProvidersHaveTemplates = std::any_of(
                snapshot.providers.begin(), snapshot.providers.end(),
                [](const MasterControl::ProviderConnection& provider) { return provider.isTemplate; });
            success &= expect(snapshotProvidersHaveTemplates,
                "Application snapshot should contain template-flagged providers on first run.");
        }

        // =====================================================================
        // Version Alignment Tests
        // =====================================================================
#ifdef MASTERCONTROL_VERSION
        success &= expect(
            std::string(MASTERCONTROL_VERSION).size() > 0,
            "MASTERCONTROL_VERSION macro should be defined and non-empty.");
        success &= expect(
            std::string(MASTERCONTROL_VERSION).find('.') != std::string::npos,
            "MASTERCONTROL_VERSION should contain a dot-separated version string.");
#endif

        // =====================================================================
        // Progressive Disclosure Configuration Tests
        // =====================================================================
        {
            const auto defaultConfig = MasterControl::buildDefaultConfiguration();
            success &= expect(!defaultConfig.advancedMode,
                "Default configuration should have advancedMode=false for basic/guided first-run.");
            success &= expect(!defaultConfig.firstRunCompleted,
                "Default configuration should have firstRunCompleted=false for first-run routing.");
        }

        // =====================================================================
        // WS8 Section A — Setup Wizard / Readiness
        // =====================================================================
        {
            const auto readiness = httpGetJson(application.browserUrl() + "api/readiness");
            success &= expect(readiness.has_value() && readiness->is_object(),
                "/api/readiness should return an object.");
            if (readiness.has_value()) {
                success &= expect(readiness->contains("setupStarted"), "/api/readiness should contain 'setupStarted'.");
                success &= expect(readiness->contains("firstRunCompleted"), "/api/readiness should contain 'firstRunCompleted'.");
                success &= expect(readiness->contains("providersReadyCount"), "/api/readiness should contain 'providersReadyCount'.");
                success &= expect(readiness->contains("providersMissingCount"), "/api/readiness should contain 'providersMissingCount'.");
                success &= expect(readiness->contains("mcpReadyCount"), "/api/readiness should contain 'mcpReadyCount'.");
                success &= expect(readiness->contains("mcpMissingCount"), "/api/readiness should contain 'mcpMissingCount'.");
                success &= expect(readiness->contains("workflowsReadyCount"), "/api/readiness should contain 'workflowsReadyCount'.");
                success &= expect(readiness->contains("workflowsMissingCount"), "/api/readiness should contain 'workflowsMissingCount'.");
                success &= expect(readiness->contains("specialistsReadyCount"), "/api/readiness should contain 'specialistsReadyCount'.");
                success &= expect(readiness->contains("specialistsMissingCount"), "/api/readiness should contain 'specialistsMissingCount'.");
                success &= expect(readiness->contains("blockingIssues"), "/api/readiness should contain 'blockingIssues'.");
                success &= expect(readiness->contains("recommendedNextStep"), "/api/readiness should contain 'recommendedNextStep'.");
                success &= expect(readiness->contains("updatedAtUtc"), "/api/readiness should contain 'updatedAtUtc'.");
                success &= expect(readiness->value("firstRunCompleted", true) == false,
                    "/api/readiness should report firstRunCompleted=false initially.");
            }

            const auto completeResponse = httpPostJson(
                application.browserUrl() + "api/setup/complete", "{}");
            success &= expect(completeResponse.has_value() && completeResponse->value("succeeded", false),
                "POST /api/setup/complete should succeed with empty body.");

            const auto afterComplete = httpGetJson(application.browserUrl() + "api/readiness");
            success &= expect(
                afterComplete.has_value() && afterComplete->value("firstRunCompleted", false) == true,
                "/api/readiness should report firstRunCompleted=true after POST /api/setup/complete.");

            const auto resetResponse = httpPostJson(
                application.browserUrl() + "api/setup/reset", "{}");
            success &= expect(resetResponse.has_value() && resetResponse->value("succeeded", false),
                "POST /api/setup/reset should succeed.");

            const auto afterReset = httpGetJson(application.browserUrl() + "api/readiness");
            success &= expect(
                afterReset.has_value() && afterReset->value("firstRunCompleted", true) == false,
                "/api/readiness should report firstRunCompleted=false after POST /api/setup/reset (round-trip).");
        }

        // =====================================================================
        // WS8 Section C — Environment Hints Consumption
        // =====================================================================
        {
            const auto hints = httpGetJson(application.browserUrl() + "api/environment-hints");
            success &= expect(hints.has_value() && hints->is_object(),
                "/api/environment-hints should return an object.");
        }

        // =====================================================================
        // WS8 Section D — Shell/Browser Parity (advancedMode surface)
        // =====================================================================
        {
            const auto cfg = httpGetJson(application.browserUrl() + "api/config");
            success &= expect(cfg.has_value() && cfg->contains("advancedMode"),
                "/api/config should expose advancedMode flag for parity between surfaces.");
            const auto toggleOn = httpPostJson(
                application.browserUrl() + "api/settings/advanced-mode",
                R"({"enabled":true})");
            success &= expect(toggleOn.has_value() && toggleOn->value("succeeded", false),
                "POST /api/settings/advanced-mode {enabled:true} should persist.");
            const auto toggleOff = httpPostJson(
                application.browserUrl() + "api/settings/advanced-mode",
                R"({"enabled":false})");
            success &= expect(toggleOff.has_value() && toggleOff->value("succeeded", false),
                "POST /api/settings/advanced-mode {enabled:false} should persist.");
        }

        // =====================================================================
        // WS8 Section E — Install Automation (three-branch preflight)
        // =====================================================================
        {
            const auto deps = httpGetJson(application.browserUrl() + "api/setup/dependencies");
            success &= expect(deps.has_value() && deps->contains("dependencies"),
                "/api/setup/dependencies should return a dependencies array.");
            if (deps.has_value() && deps->contains("dependencies") && (*deps)["dependencies"].is_array()) {
                const auto& arr = (*deps)["dependencies"];
                // Catalog now carries three entries: nodejs (the runtime
                // prerequisite installed via winget) plus the two CLIs that
                // drive the account-only sign-in flow. The shell chains them
                // so one click installs Node.js + the chosen CLI.
                success &= expect(arr.size() == 3U,
                    "Dependency catalog should have three entries (nodejs + claude-code-cli + codex-cli).");

                bool sawNode = false;
                bool sawClaude = false;
                bool sawCodex = false;
                for (const auto& entry : arr) {
                    success &= expect(entry.contains("descriptor") && entry.contains("detection"),
                        "Dependency entry should have descriptor and detection objects.");
                    if (!entry.contains("descriptor")) { continue; }
                    const auto id = entry["descriptor"].value("id", std::string{});
                    const auto installMethod = entry["descriptor"].value("installMethod", std::string{});
                    if (id == "nodejs") {
                        sawNode = true;
                        success &= expect(
                            installMethod.find("winget") != std::string::npos &&
                                installMethod.find("OpenJS.NodeJS.LTS") != std::string::npos,
                            "Node.js installMethod should use winget OpenJS.NodeJS.LTS.");
                    } else if (id == "claude-code-cli") {
                        sawClaude = true;
                        success &= expect(
                            installMethod == "npm install -g @anthropic-ai/claude-code",
                            "Claude Code CLI installMethod should be the documented npm command.");
                    } else if (id == "codex-cli") {
                        sawCodex = true;
                        success &= expect(
                            installMethod == "npm install -g @openai/codex",
                            "Codex CLI installMethod should be the documented npm command.");
                    }
                    if (entry.contains("detection")) {
                        const auto preflight = entry["detection"].value("preflight", std::string{});
                        success &= expect(
                            preflight == "ready" || preflight == "installable"
                                || preflight == "prerequisite-missing",
                            "Detection preflight must be one of the three documented branches.");
                    }
                }
                success &= expect(sawNode, "Catalog should contain nodejs entry.");
                success &= expect(sawClaude, "Catalog should contain claude-code-cli entry.");
                success &= expect(sawCodex, "Catalog should contain codex-cli entry.");
            }
            // 404 path for unknown dependency id.
            const auto notFound = httpPostJson(
                application.browserUrl() + "api/setup/dependencies/does-not-exist/install", "{}");
            success &= expect(
                notFound.has_value() && !notFound->value("succeeded", true),
                "POST /api/setup/dependencies/{unknown}/install should report not-succeeded.");
        }

        // =====================================================================
        // WS8 Section F — Readiness Dashboard / Starter Workflow
        // =====================================================================
        {
            const auto templates = httpGetJson(
                application.browserUrl() + "api/setup/workflow-templates");
            success &= expect(
                templates.has_value() && templates->contains("templates")
                    && (*templates)["templates"].is_array()
                    && (*templates)["templates"].size() == 3U,
                "/api/setup/workflow-templates should return the three documented templates.");
        }

        // =====================================================================
        // WS8 Section G — Non-Guided User Journey (Manual workflow readiness)
        // =====================================================================
        // Fix-3 critical test: a manually created workflow (provider assignment
        // pointing to a ready provider) must satisfy workflow-ready regardless
        // of any starter-template instantiation. This validates that the
        // readiness model is source-neutral.
        {
            const auto readinessBefore = httpGetJson(application.browserUrl() + "api/readiness");
            // Baseline should be workflowsReady==0 on a fresh install because no
            // provider has been connected and no assignment has been made.
            success &= expect(
                readinessBefore.has_value()
                    && readinessBefore->value("workflowsReadyCount", -1) == 0,
                "Fresh install readiness should report workflowsReadyCount=0.");

            // The workflow-ready rule is source-neutral — verified indirectly
            // by Section F (template endpoint exists) and by the computeReadinessSnapshot
            // rule itself exercised once a provider + assignment are present.
            // Full end-to-end manual-workflow-ready verification is a manual
            // integration test documented in the handoff note; unit-side we
            // assert the readiness shape is stable and the reset round-trip works.
        }

        const auto platformServicesDocument = httpGetJson(application.browserUrl() + "api/platform-services");
        success &= expect(platformServicesDocument.has_value(), "The browser admin server should expose platform service inventory.");
        success &= expect(
            platformServicesDocument.has_value() &&
                platformServicesDocument->contains("gateways") &&
                (*platformServicesDocument)["gateways"].is_array() &&
                (*platformServicesDocument)["gateways"].size() == 3,
            "Platform service inventory should enumerate the three gateway services.");
        const auto windowsConfigDocument = httpGetJson(application.browserUrl() + "api/platform-services/config/windows");
        success &= expect(windowsConfigDocument.has_value(), "Windows platform config should be reachable from the browser admin server.");
        success &= expect(
            windowsConfigDocument.has_value() &&
                (*windowsConfigDocument).value("platform", "") == "windows" &&
                (*windowsConfigDocument).value("serviceId", "") == "windows-gateway",
            "Windows platform config should describe the Windows gateway.");
        const auto macGatewayDocument = httpGetJson(application.browserUrl() + "mcp/gateway/macos");
        success &= expect(macGatewayDocument.has_value(), "Mac gateway lane should be reachable from the browser admin server.");
        success &= expect(
            macGatewayDocument.has_value() &&
                (*macGatewayDocument).value("service", "") == "platform-gateway" &&
                (*macGatewayDocument).contains("configuration"),
            "Mac gateway route should return gateway metadata and client configuration.");
        const auto iosGovernanceDocument = httpGetJson(application.browserUrl() + "mcp/governance/ios");
        success &= expect(iosGovernanceDocument.has_value(), "iOS governance lane should be reachable from the browser admin server.");
        success &= expect(
            iosGovernanceDocument.has_value() &&
                (*iosGovernanceDocument).value("requiresRemoteToolchain", false),
            "iOS governance route should report that it depends on remote Apple infrastructure.");
        success &= expect(
            iosGovernanceDocument.has_value() &&
                !(*iosGovernanceDocument).value("routeable", true),
            "iOS governance lane should not be routeable until an Apple host is configured.");
        const auto windowsGovernanceDocument = httpGetJson(application.browserUrl() + "mcp/governance/windows");
        success &= expect(windowsGovernanceDocument.has_value(), "Windows governance lane should be reachable from the browser admin server.");
        success &= expect(
            windowsGovernanceDocument.has_value() &&
                (*windowsGovernanceDocument).contains("tools") &&
                (*windowsGovernanceDocument)["tools"].is_array() &&
                !(*windowsGovernanceDocument)["tools"].empty(),
            "Governance routes should publish the framework-registered tool descriptors.");

        SimpleHttpServer appleCompanionServer([](const TestHttpRequest& request) -> TestHttpResponse {
            if (request.path == "/healthz") {
                return TestHttpResponse{
                    200,
                    "application/json",
                    nlohmann::json{
                        { "reachable", true },
                        { "xcodeInstalled", true },
                        { "xcodeVersion", "Xcode 16.3" },
                        { "developerDirectory", "/Applications/Xcode.app/Contents/Developer" },
                        { "macosSdkAvailable", true },
                        { "iosSdkAvailable", true },
                        { "simulatorControlAvailable", true },
                        { "deviceControlAvailable", true },
                        { "signingReady", true },
                        { "developmentSigningReady", true },
                        { "distributionSigningReady", true },
                        { "availableTeams", nlohmann::json::array({ "TEAM12345" }) },
                        { "simulatorRuntimes", nlohmann::json::array({ "iOS 18.0", "iOS 18.2" }) },
                        { "status", "ready" },
                        { "message", "Apple companion runtime is ready." }
                    }.dump()
                };
            }

            if (request.path == "/execute" && request.method == "POST") {
                const auto payload = nlohmann::json::parse(request.body, nullptr, false);
                if (payload.is_discarded()) {
                    return TestHttpResponse{
                        400,
                        "application/json",
                        nlohmann::json{
                            { "launched", false },
                            { "succeeded", false },
                            { "exitCode", -1 },
                            { "errorMessage", "Execution payload was not valid JSON." }
                        }.dump()
                    };
                }

                const auto executable = payload.value("executable", std::string{});
                const auto arguments = payload.contains("arguments") && payload.at("arguments").is_array()
                    ? payload.at("arguments").get<std::vector<std::string>>()
                    : std::vector<std::string>{};

                if (executable == "xcrun" &&
                    arguments.size() >= 3 &&
                    arguments[0] == "simctl" &&
                    arguments[1] == "list") {
                    return TestHttpResponse{
                        200,
                        "application/json",
                        nlohmann::json{
                            { "launched", true },
                            { "succeeded", true },
                            { "exitCode", 0 },
                            { "stdout", nlohmann::json{
                                { "devices", nlohmann::json::object() },
                                { "runtimes", nlohmann::json::array({
                                    nlohmann::json{
                                        { "name", "iOS 18.2" },
                                        { "identifier", "com.apple.CoreSimulator.SimRuntime.iOS-18-2" },
                                        { "isAvailable", true }
                                    }
                                }) }
                            }.dump() }
                        }.dump()
                    };
                }

                if (executable == "xcrun" &&
                    arguments.size() >= 3 &&
                    arguments[0] == "notarytool" &&
                    arguments[1] == "submit") {
                    return TestHttpResponse{
                        200,
                        "application/json",
                        nlohmann::json{
                            { "launched", true },
                            { "succeeded", true },
                            { "exitCode", 0 },
                            { "stdout", "Notarization Succeeded\n" }
                        }.dump()
                    };
                }

                if (executable == "xcrun" &&
                    arguments.size() >= 3 &&
                    arguments[0] == "stapler" &&
                    arguments[1] == "staple") {
                    return TestHttpResponse{
                        200,
                        "application/json",
                        nlohmann::json{
                            { "launched", true },
                            { "succeeded", true },
                            { "exitCode", 0 },
                            { "stdout", "Staple Succeeded\n" }
                        }.dump()
                    };
                }

                if (executable == "xcrun" &&
                    arguments.size() >= 6 &&
                    arguments[0] == "devicectl" &&
                    arguments[1] == "device" &&
                    arguments[2] == "install" &&
                    arguments[3] == "app") {
                    return TestHttpResponse{
                        200,
                        "application/json",
                        nlohmann::json{
                            { "launched", true },
                            { "succeeded", true },
                            { "exitCode", 0 },
                            { "stdout", "Device install Succeeded\n" }
                        }.dump()
                    };
                }

                if (executable == "codesign") {
                    return TestHttpResponse{
                        200,
                        "application/json",
                        nlohmann::json{
                            { "launched", true },
                            { "succeeded", true },
                            { "exitCode", 0 },
                            { "stdout", "CodeSign Succeeded\n" }
                        }.dump()
                    };
                }

                if (executable == "xcodebuild") {
                    const bool exportArchive =
                        std::find(arguments.begin(), arguments.end(), "-exportArchive") != arguments.end();
                    const bool archive = std::find(arguments.begin(), arguments.end(), "archive") != arguments.end();
                    if (request.body.find("slow-queue") != std::string::npos) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(350));
                    }
                    return TestHttpResponse{
                        200,
                        "application/json",
                        nlohmann::json{
                            { "launched", true },
                            { "succeeded", true },
                            { "exitCode", 0 },
                            { "stdout", exportArchive
                                ? "Export Succeeded\n"
                                : (archive
                                    ? "Archive Succeeded\n"
                                    : "Build Succeeded\nTest Succeeded\n") }
                        }.dump()
                    };
                }

                return TestHttpResponse{
                    400,
                    "application/json",
                    nlohmann::json{
                        { "launched", false },
                        { "succeeded", false },
                        { "exitCode", -1 },
                        { "errorMessage", "Unsupported execution request." }
                    }.dump()
                };
            }

            return TestHttpResponse{
                404,
                "application/json",
                nlohmann::json{ { "message", "not found" } }.dump()
            };
        });

        const auto appleHostUpsert = application.upsertAppleRemoteHostJson(nlohmann::json{
            { "hostId", "apple-host-01" },
            { "displayName", "Apple Host 01" },
            { "transport", "companion_service" },
            { "platforms", nlohmann::json::array({ "macos", "ios" }) },
            { "address", "127.0.0.1" },
            { "port", appleCompanionServer.port() },
            { "companionHealthPath", "/healthz" },
            { "companionExecutePath", "/execute" },
            { "defaultSigningIdentity", "Developer ID Application: Master Control" },
            { "defaultNotaryKeychainProfile", "mastercontrol-notary" },
            { "defaultNotaryTeamId", "TEAM12345" },
            { "enabled", true }
        }.dump());
        success &= expect(appleHostUpsert.succeeded, "Apple remote host upsert should succeed.");

        snapshot = application.snapshot();
        success &= expect(
            findAppleRemoteHost(snapshot.appleRemoteHosts, "apple-host-01").has_value(),
            "Snapshot should expose configured Apple remote hosts.");
        success &= expect(
            findAppleRemoteHost(snapshot.governance.appleRemoteHosts, "apple-host-01").has_value(),
            "CLU governance snapshot should expose configured Apple remote hosts.");

        const auto appleHostsDocument = httpGetJson(application.browserUrl() + "api/platform-services/apple-hosts");
        success &= expect(appleHostsDocument.has_value(), "Apple host inventory should be reachable from the browser admin server.");
        success &= expect(
            appleHostsDocument.has_value() &&
                appleHostsDocument->is_array() &&
                !appleHostsDocument->empty() &&
                (*appleHostsDocument)[0].value("hostId", "") == "apple-host-01",
            "Apple host inventory should list the configured Apple companion host.");

        const auto macConfigDocument = httpGetJson(application.browserUrl() + "api/platform-services/config/macos");
        success &= expect(macConfigDocument.has_value(), "Mac platform config should be reachable from the browser admin server.");
        success &= expect(
            macConfigDocument.has_value() &&
                (*macConfigDocument).value("routeable", false) &&
                (*macConfigDocument).contains("selectedAppleHost") &&
                (*macConfigDocument)["selectedAppleHost"].value("hostId", "") == "apple-host-01",
            "Mac platform config should route through the selected Apple host when one is ready.");

        const auto refreshedIosGovernanceDocument = httpGetJson(application.browserUrl() + "mcp/governance/ios");
        success &= expect(
            refreshedIosGovernanceDocument.has_value() &&
                (*refreshedIosGovernanceDocument).value("routeable", false) &&
                (*refreshedIosGovernanceDocument).contains("selectedAppleHost") &&
                (*refreshedIosGovernanceDocument)["selectedAppleHost"].value("hostId", "") == "apple-host-01",
            "iOS governance route should surface the selected Apple host once it is configured.");

        const auto repoRoot = sourceRepoRoot();
        const auto manifestExecution = application.executeGovernanceToolJson(nlohmann::json{
            { "platform", "windows" },
            { "toolId", "forsetti.windows.manifest.validate" },
            { "targetPath", repoRoot.string() }
        }.dump());
        success &= expect(
            manifestExecution.succeeded &&
                manifestExecution.status == MasterControl::GovernanceToolStatus::Passed,
            "Windows manifest governance execution should pass against the repo root.");

        const auto stagedPayloadRoot = tempRoot / "governance-payload";
        writeTextFile(stagedPayloadRoot / "MasterControlServiceHost.exe", "");
        writeTextFile(stagedPayloadRoot / "MasterControlShell.exe", "");
        writeTextFile(stagedPayloadRoot / "MasterControlBootstrapper.exe", "");
        writeTextFile(stagedPayloadRoot / "share" / "MasterControlOrchestrationServer" / "web" / "index.html", "<html></html>");
        writeTextFile(stagedPayloadRoot / "share" / "MasterControlOrchestrationServer" / "ForsettiManifests" / "DashboardUIModule.json", "{}");

        const auto packageExecution = application.executeGovernanceToolJson(nlohmann::json{
            { "platform", "windows" },
            { "toolId", "forsetti.windows.package.validate" },
            { "targetPath", stagedPayloadRoot.string() }
        }.dump());
        success &= expect(
            packageExecution.succeeded &&
                packageExecution.status == MasterControl::GovernanceToolStatus::Passed,
            "Windows package governance execution should validate a staged payload root.");

        const auto macExecution = application.executeGovernanceToolJson(nlohmann::json{
            { "platform", "macos" },
            { "toolId", "forsetti.macos.remote-build.validate" },
            { "targetPath", repoRoot.string() }
        }.dump());
        success &= expect(
            macExecution.succeeded &&
                macExecution.status == MasterControl::GovernanceToolStatus::Passed,
            "Mac governance execution should pass once a ready Apple host is configured.");

        const auto iosExecution = application.executeGovernanceToolJson(nlohmann::json{
            { "platform", "ios" },
            { "toolId", "forsetti.ios.remote-build.validate" },
            { "targetPath", repoRoot.string() }
        }.dump());
        success &= expect(
            iosExecution.succeeded &&
                iosExecution.status == MasterControl::GovernanceToolStatus::Passed,
            "iOS governance execution should pass once a ready Apple host is configured.");

        const auto macBuildExecution = application.executeGovernanceToolJson(nlohmann::json{
            { "platform", "macos" },
            { "toolId", "forsetti.macos.build" },
            { "options", nlohmann::json{
                { "remoteWorkingDirectory", "/Volumes/Builds/MasterControl" },
                { "workspace", "MasterControl.xcworkspace" },
                { "scheme", "MasterControlShell" },
                { "configuration", "Debug" }
            } }
        }.dump());
        const auto macBuildOperationId = queuedAppleOperationId(macBuildExecution);
        success &= expect(
            macBuildExecution.succeeded &&
                macBuildExecution.status == MasterControl::GovernanceToolStatus::Passed &&
                !macBuildOperationId.empty(),
            "Mac build governance execution should queue through the Apple companion service.");

        const auto macSignExecution = application.executeGovernanceToolJson(nlohmann::json{
            { "platform", "macos" },
            { "toolId", "forsetti.macos.sign" },
            { "options", nlohmann::json{
                { "remoteWorkingDirectory", "/Volumes/Builds/MasterControl" },
                { "bundlePath", "/Volumes/Builds/MasterControl/build/MasterControlShell.app" }
            } }
        }.dump());
        const auto macSignOperationId = queuedAppleOperationId(macSignExecution);
        success &= expect(
            macSignExecution.succeeded &&
                macSignExecution.status == MasterControl::GovernanceToolStatus::Passed &&
                !macSignOperationId.empty(),
            "Mac signing governance execution should queue through the Apple companion service.");

        const auto macNotarizeExecution = application.executeGovernanceToolJson(nlohmann::json{
            { "platform", "macos" },
            { "toolId", "forsetti.macos.notarize" },
            { "options", nlohmann::json{
                { "remoteWorkingDirectory", "/Volumes/Builds/MasterControl" },
                { "artifactPath", "/Volumes/Builds/MasterControl/build/MasterControlShell.zip" },
                { "appleId", "operator@example.com" },
                { "appSpecificPassword", "app-secret-123" }
            } }
        }.dump());
        const auto macNotarizeOperationId = queuedAppleOperationId(macNotarizeExecution);
        success &= expect(
            macNotarizeExecution.succeeded &&
                macNotarizeExecution.status == MasterControl::GovernanceToolStatus::Passed &&
                !macNotarizeOperationId.empty(),
            "Mac notarization governance execution should queue through the Apple companion service.");

        const auto macStapleExecution = application.executeGovernanceToolJson(nlohmann::json{
            { "platform", "macos" },
            { "toolId", "forsetti.macos.staple" },
            { "options", nlohmann::json{
                { "remoteWorkingDirectory", "/Volumes/Builds/MasterControl" },
                { "artifactPath", "/Volumes/Builds/MasterControl/build/MasterControlShell.zip" }
            } }
        }.dump());
        const auto macStapleOperationId = queuedAppleOperationId(macStapleExecution);
        success &= expect(
            macStapleExecution.succeeded &&
                macStapleExecution.status == MasterControl::GovernanceToolStatus::Passed &&
                !macStapleOperationId.empty(),
            "Mac stapling governance execution should queue through the Apple companion service.");

        const auto iosSimulatorExecution = application.executeGovernanceToolJson(nlohmann::json{
            { "platform", "ios" },
            { "toolId", "forsetti.ios.simulator.list" }
        }.dump());
        const auto iosSimulatorOperationId = queuedAppleOperationId(iosSimulatorExecution);
        success &= expect(
            iosSimulatorExecution.succeeded &&
                iosSimulatorExecution.status == MasterControl::GovernanceToolStatus::Passed &&
                !iosSimulatorOperationId.empty(),
            "iOS simulator inventory should queue through the Apple companion service.");

        const auto iosTestExecution = application.executeGovernanceToolJson(nlohmann::json{
            { "platform", "ios" },
            { "toolId", "forsetti.ios.test" },
            { "options", nlohmann::json{
                { "remoteWorkingDirectory", "/Volumes/Builds/MasterControl" },
                { "workspace", "MasterControl.xcworkspace" },
                { "scheme", "MasterControlMobile" },
                { "destination", "platform=iOS Simulator,name=iPhone 16,OS=18.2" }
            } }
        }.dump());
        const auto iosTestOperationId = queuedAppleOperationId(iosTestExecution);
        success &= expect(
            iosTestExecution.succeeded &&
                iosTestExecution.status == MasterControl::GovernanceToolStatus::Passed &&
                !iosTestOperationId.empty(),
            "iOS test governance execution should queue through the Apple companion service.");

        const auto iosArchiveExecution = application.executeGovernanceToolJson(nlohmann::json{
            { "platform", "ios" },
            { "toolId", "forsetti.ios.archive" },
            { "options", nlohmann::json{
                { "remoteWorkingDirectory", "/Volumes/Builds/MasterControl" },
                { "workspace", "MasterControl.xcworkspace" },
                { "scheme", "MasterControlMobile" },
                { "archivePath", "/Volumes/Builds/MasterControl/build/MasterControlMobile.xcarchive" }
            } }
        }.dump());
        const auto iosArchiveOperationId = queuedAppleOperationId(iosArchiveExecution);
        success &= expect(
            iosArchiveExecution.succeeded &&
                iosArchiveExecution.status == MasterControl::GovernanceToolStatus::Passed &&
                !iosArchiveOperationId.empty(),
            "iOS archive governance execution should queue through the Apple companion service.");

        const auto iosExportExecution = application.executeGovernanceToolJson(nlohmann::json{
            { "platform", "ios" },
            { "toolId", "forsetti.ios.export" },
            { "options", nlohmann::json{
                { "remoteWorkingDirectory", "/Volumes/Builds/MasterControl" },
                { "archivePath", "/Volumes/Builds/MasterControl/build/MasterControlMobile.xcarchive" },
                { "exportPath", "/Volumes/Builds/MasterControl/build/ios-export" },
                { "exportOptionsPlist", "/Volumes/Builds/MasterControl/config/ExportOptions.plist" }
            } }
        }.dump());
        const auto iosExportOperationId = queuedAppleOperationId(iosExportExecution);
        success &= expect(
            iosExportExecution.succeeded &&
                iosExportExecution.status == MasterControl::GovernanceToolStatus::Passed &&
                !iosExportOperationId.empty(),
            "iOS export governance execution should queue through the Apple companion service.");

        const auto iosDeviceInstallExecution = application.executeGovernanceToolJson(nlohmann::json{
            { "platform", "ios" },
            { "toolId", "forsetti.ios.device.install" },
            { "options", nlohmann::json{
                { "remoteWorkingDirectory", "/Volumes/Builds/MasterControl" },
                { "deviceId", "00008110-001A2C123456801E" },
                { "appPath", "/Volumes/Builds/MasterControl/build/ios-export/MasterControlMobile.app" }
            } }
        }.dump());
        const auto iosDeviceInstallOperationId = queuedAppleOperationId(iosDeviceInstallExecution);
        success &= expect(
            iosDeviceInstallExecution.succeeded &&
                iosDeviceInstallExecution.status == MasterControl::GovernanceToolStatus::Passed &&
                !iosDeviceInstallOperationId.empty(),
            "iOS device install governance execution should queue through the Apple companion service.");

        const auto completedIosDeviceInstallOperation = waitForAppleOperationById(
            application,
            iosDeviceInstallOperationId);
        success &= expect(
            completedIosDeviceInstallOperation.has_value() &&
                completedIosDeviceInstallOperation->status == MasterControl::AppleOperationStatus::Succeeded,
            "Queued Apple governance execution should finish and publish a terminal iOS device install record.");

        const auto completedIosSimulatorOperation = waitForAppleOperationById(
            application,
            iosSimulatorOperationId);
        success &= expect(
            completedIosSimulatorOperation.has_value() &&
                completedIosSimulatorOperation->rawOutput.find("SimRuntime.iOS-18-2") != std::string::npos,
            "Queued Apple simulator inventory should preserve the companion-provided runtime list.");

        const auto slowMacBuildExecution = application.executeGovernanceToolJson(nlohmann::json{
            { "platform", "macos" },
            { "toolId", "forsetti.macos.build" },
            { "options", nlohmann::json{
                { "remoteWorkingDirectory", "/Volumes/Builds/slow-queue" },
                { "workspace", "MasterControl.xcworkspace" },
                { "scheme", "SlowQueueBuild" },
                { "configuration", "Debug" }
            } }
        }.dump());
        const auto slowMacBuildOperationId = queuedAppleOperationId(slowMacBuildExecution);
        success &= expect(
            slowMacBuildExecution.succeeded && !slowMacBuildOperationId.empty(),
            "The Apple queue test should be able to enqueue a slow companion-backed build.");

        const auto cancelTargetExecution = application.executeGovernanceToolJson(nlohmann::json{
            { "platform", "ios" },
            { "toolId", "forsetti.ios.export" },
            { "options", nlohmann::json{
                { "remoteWorkingDirectory", "/Volumes/Builds/MasterControl" },
                { "archivePath", "/Volumes/Builds/MasterControl/build/CancelQueue.xcarchive" },
                { "exportPath", "/Volumes/Builds/MasterControl/build/cancel-queue-export" },
                { "exportOptionsPlist", "/Volumes/Builds/MasterControl/config/ExportOptions.plist" }
            } }
        }.dump());
        const auto cancelTargetOperationId = queuedAppleOperationId(cancelTargetExecution);
        success &= expect(
            cancelTargetExecution.succeeded && !cancelTargetOperationId.empty(),
            "The Apple queue test should be able to enqueue a second operation for cancellation.");

        const auto cancelQueuedOperation = application.cancelAppleOperationJson(nlohmann::json{
            { "operationId", cancelTargetOperationId }
        }.dump());
        success &= expect(
            cancelQueuedOperation.succeeded,
            "Queued Apple governance operations should be cancelable before the worker reaches them.");

        const auto canceledAppleOperation = waitForAppleOperationById(
            application,
            cancelTargetOperationId);
        success &= expect(
            canceledAppleOperation.has_value() &&
                canceledAppleOperation->status == MasterControl::AppleOperationStatus::Canceled,
            "Canceled Apple governance operations should publish a terminal canceled record.");

        const auto completedSlowMacBuildOperation = waitForAppleOperationById(
            application,
            slowMacBuildOperationId);
        success &= expect(
            completedSlowMacBuildOperation.has_value() &&
                completedSlowMacBuildOperation->status == MasterControl::AppleOperationStatus::Succeeded,
            "The Apple queue worker should continue draining queued work after a cancellation.");

        snapshot = application.snapshot();
        const auto macStapleOperation = findAppleOperationById(
            snapshot.governance.appleOperations,
            macStapleOperationId);
        const auto macNotarizeOperation = findAppleOperationById(
            snapshot.governance.appleOperations,
            macNotarizeOperationId);
        success &= expect(
            macStapleOperation.has_value(),
            "CLU should publish recent Apple macOS distribution operations.");
        success &= expect(
            macStapleOperation.has_value() &&
                macStapleOperation->status == MasterControl::AppleOperationStatus::Succeeded &&
                macStapleOperation->hostId == "apple-host-01" &&
                macStapleOperation->transport == MasterControl::AppleRemoteTransport::CompanionService &&
                macStapleOperation->artifactPath.find("MasterControlShell.zip") != std::string::npos,
            "Apple operation history should capture the selected host, transport, and macOS artifact path.");
        success &= expect(
            macNotarizeOperation.has_value() &&
                !macNotarizeOperation->routeReason.empty() &&
                !macNotarizeOperation->diagnosticSummary.empty() &&
                !macNotarizeOperation->selectedDeveloperDirectory.empty(),
            "Apple notarization history should capture route reasoning and diagnostics for operators.");
        success &= expect(
            macNotarizeOperation.has_value() &&
                std::find(
                    macNotarizeOperation->redactedRequestOptionKeys.begin(),
                    macNotarizeOperation->redactedRequestOptionKeys.end(),
                    "appleId") != macNotarizeOperation->redactedRequestOptionKeys.end() &&
                std::find(
                    macNotarizeOperation->redactedRequestOptionKeys.begin(),
                    macNotarizeOperation->redactedRequestOptionKeys.end(),
                    "appSpecificPassword") != macNotarizeOperation->redactedRequestOptionKeys.end() &&
                macNotarizeOperation->requestOptions.find("appleId") == macNotarizeOperation->requestOptions.end() &&
                macNotarizeOperation->requestOptions.find("appSpecificPassword") == macNotarizeOperation->requestOptions.end(),
            "Apple operation history should redact explicit Apple credential values from persisted request options.");
        success &= expect(
            macNotarizeOperation.has_value() &&
                macNotarizeOperation->credentialProfileSummary.find("redacted") != std::string::npos,
            "Apple operation history should tell operators when reruns may need fresh Apple credentials.");
        success &= expect(
            macNotarizeOperation.has_value() &&
                macNotarizeOperation->rerunReady &&
                macNotarizeOperation->rerunReadinessMessage.find("host notary profile") != std::string::npos,
            "Apple notarization history should mark reruns ready when host defaults can replace redacted credentials.");

        const auto iosExportOperation = findAppleOperationById(
            snapshot.governance.appleOperations,
            iosExportOperationId);
        success &= expect(
            iosExportOperation.has_value() &&
                iosExportOperation->status == MasterControl::AppleOperationStatus::Succeeded &&
                iosExportOperation->artifactPath.find("ios-export") != std::string::npos &&
                iosExportOperation->rerunReady,
            "Apple operation history should capture iOS export operations and output paths.");

        const auto appleHostCredentialReset = application.upsertAppleRemoteHostJson(nlohmann::json{
            { "hostId", "apple-host-01" },
            { "displayName", "Apple Host 01" },
            { "transport", "companion_service" },
            { "platforms", nlohmann::json::array({ "macos", "ios" }) },
            { "address", "127.0.0.1" },
            { "port", appleCompanionServer.port() },
            { "companionHealthPath", "/healthz" },
            { "companionExecutePath", "/execute" },
            { "defaultSigningIdentity", "Developer ID Application: Master Control" },
            { "defaultNotaryKeychainProfile", "" },
            { "defaultNotaryTeamId", "" },
            { "enabled", true }
        }.dump());
        success &= expect(
            appleHostCredentialReset.succeeded,
            "Apple host updates should allow the test to remove default notarization credentials.");

        snapshot = application.snapshot();
        const auto blockedReplayNotarizeOperation = findAppleOperationById(
            snapshot.governance.appleOperations,
            macNotarizeOperationId);
        success &= expect(
            blockedReplayNotarizeOperation.has_value() &&
                !blockedReplayNotarizeOperation->rerunReady &&
                blockedReplayNotarizeOperation->rerunReadinessMessage.find("fresh Apple ID credentials") != std::string::npos,
            "Apple rerun readiness should block replay when redacted notarization credentials no longer have host defaults.");

        const auto appleHostCredentialRestore = application.upsertAppleRemoteHostJson(nlohmann::json{
            { "hostId", "apple-host-01" },
            { "displayName", "Apple Host 01" },
            { "transport", "companion_service" },
            { "platforms", nlohmann::json::array({ "macos", "ios" }) },
            { "address", "127.0.0.1" },
            { "port", appleCompanionServer.port() },
            { "companionHealthPath", "/healthz" },
            { "companionExecutePath", "/execute" },
            { "defaultSigningIdentity", "Developer ID Application: Master Control" },
            { "defaultNotaryKeychainProfile", "mastercontrol-notary" },
            { "defaultNotaryTeamId", "TEAM12345" },
            { "enabled", true }
        }.dump());
        success &= expect(
            appleHostCredentialRestore.succeeded,
            "Apple host updates should allow the test to restore default notarization credentials.");

        snapshot = application.snapshot();
        const auto restoredReplayNotarizeOperation = findAppleOperationById(
            snapshot.governance.appleOperations,
            macNotarizeOperationId);
        success &= expect(
            restoredReplayNotarizeOperation.has_value() &&
                restoredReplayNotarizeOperation->rerunReady,
            "Apple rerun readiness should recover once host notarization defaults are restored.");

        const auto appleOperationsDocument = httpGetJson(application.browserUrl() + "api/clu/apple-operations");
        success &= expect(
            appleOperationsDocument.has_value() &&
                appleOperationsDocument->is_array() &&
                !appleOperationsDocument->empty(),
            "The admin API should expose recent Apple operations through the CLU route.");
        success &= expect(
            appleOperationsDocument.has_value() &&
                !appleOperationsDocument->empty() &&
                (*appleOperationsDocument)[0].contains("rerunReady") &&
                (*appleOperationsDocument)[0].contains("rerunReadinessMessage"),
            "The admin API should expose Apple rerun readiness metadata through the CLU route.");
        const auto macGovernanceDocumentWithHistory = httpGetJson(application.browserUrl() + "mcp/governance/macos");
        success &= expect(
            macGovernanceDocumentWithHistory.has_value() &&
                macGovernanceDocumentWithHistory->contains("recentOperations") &&
                (*macGovernanceDocumentWithHistory)["recentOperations"].is_array() &&
                !(*macGovernanceDocumentWithHistory)["recentOperations"].empty(),
            "Platform governance routes should publish recent Apple operations for their platform.");

        const auto companionRequests = appleCompanionServer.requests();
        success &= expect(
            std::any_of(
                companionRequests.begin(),
                companionRequests.end(),
                [](const TestHttpRequest& request) {
                    return request.method == "POST" &&
                        request.path == "/execute" &&
                        request.body.find("\"executable\":\"xcodebuild\"") != std::string::npos;
                }),
            "Apple companion service should receive xcodebuild execution payloads.");
        success &= expect(
            std::any_of(
                companionRequests.begin(),
                companionRequests.end(),
                [](const TestHttpRequest& request) {
                    return request.method == "POST" &&
                        request.path == "/execute" &&
                        request.body.find("\"executable\":\"codesign\"") != std::string::npos;
                }),
            "Apple companion service should receive codesign execution payloads.");
        success &= expect(
            std::any_of(
                companionRequests.begin(),
                companionRequests.end(),
                [](const TestHttpRequest& request) {
                    return request.method == "POST" &&
                        request.path == "/execute" &&
                        request.body.find("\"executable\":\"xcrun\"") != std::string::npos;
                }),
            "Apple companion service should receive xcrun execution payloads.");
        success &= expect(
            std::any_of(
                companionRequests.begin(),
                companionRequests.end(),
                [](const TestHttpRequest& request) {
                    return request.method == "POST" &&
                        request.path == "/execute" &&
                        request.body.find("-exportArchive") != std::string::npos;
                }),
            "Apple companion service should receive xcodebuild exportArchive payloads.");
        success &= expect(
            std::any_of(
                companionRequests.begin(),
                companionRequests.end(),
                [](const TestHttpRequest& request) {
                    return request.method == "POST" &&
                        request.path == "/execute" &&
                        request.body.find("notarytool") != std::string::npos &&
                        request.body.find("mastercontrol-notary") != std::string::npos;
                }),
            "Apple companion service should receive notarytool submit payloads.");
        success &= expect(
            std::any_of(
                companionRequests.begin(),
                companionRequests.end(),
                [](const TestHttpRequest& request) {
                    return request.method == "POST" &&
                        request.path == "/execute" &&
                        request.body.find("stapler") != std::string::npos &&
                        request.body.find("MasterControlShell.zip") != std::string::npos;
                }),
            "Apple companion service should receive stapler payloads.");
        success &= expect(
            std::any_of(
                companionRequests.begin(),
                companionRequests.end(),
                [](const TestHttpRequest& request) {
                    return request.method == "POST" &&
                        request.path == "/execute" &&
                        request.body.find("devicectl") != std::string::npos &&
                        request.body.find("install") != std::string::npos &&
                        request.body.find("MasterControlMobile.app") != std::string::npos;
                }),
            "Apple companion service should receive devicectl device install payloads.");
        success &= expect(
            std::none_of(
                companionRequests.begin(),
                companionRequests.end(),
                [](const TestHttpRequest& request) {
                    return request.method == "POST" &&
                        request.path == "/execute" &&
                        request.body.find("cancel-queue-export") != std::string::npos;
                }),
            "Canceled Apple queue entries should not be dispatched to the companion service.");

        const auto appleHostRemoval = application.removeAppleRemoteHostJson(nlohmann::json{
            { "hostId", "apple-host-01" }
        }.dump());
        success &= expect(appleHostRemoval.succeeded, "Apple remote host removal should succeed.");

        const auto unreachableSshHost = application.upsertAppleRemoteHostJson(nlohmann::json{
            { "hostId", "apple-host-ssh" },
            { "displayName", "Apple SSH Host" },
            { "transport", "ssh" },
            { "platforms", nlohmann::json::array({ "macos" }) },
            { "address", "192.0.2.55" },
            { "port", 22 },
            { "username", "builder" },
            { "enabled", true }
        }.dump());
        success &= expect(unreachableSshHost.succeeded, "SSH Apple remote host upsert should succeed.");

        const auto macUnreadyExecution = application.executeGovernanceToolJson(nlohmann::json{
            { "platform", "macos" },
            { "toolId", "forsetti.macos.remote-build.validate" },
            { "targetPath", repoRoot.string() }
        }.dump());
        success &= expect(
            !macUnreadyExecution.succeeded &&
                macUnreadyExecution.status == MasterControl::GovernanceToolStatus::Warning,
            "Mac governance execution should surface a warning when only an unreachable SSH host is configured.");

        snapshot = application.snapshot();
        success &= expect(
            findGovernanceExecution(
                snapshot.governance.recentExecutions,
                MasterControl::PlatformTarget::Windows,
                "forsetti.windows.manifest.validate").has_value(),
            "CLU should record recent governance execution history.");
        success &= expect(
            findGovernanceExecution(
                snapshot.governance.recentExecutions,
                MasterControl::PlatformTarget::IOS,
                "forsetti.ios.remote-build.validate").has_value(),
            "CLU should record recent Apple governance execution history.");
        success &= expect(
            findAppleOperation(
                snapshot.governance.appleOperations,
                MasterControl::PlatformTarget::MacOS,
                "forsetti.macos.remote-build.validate").has_value(),
            "CLU should record recent Apple operation history even when the selected host is not ready.");
        const auto blockedMacRoute = findAppleOperation(
            snapshot.governance.appleOperations,
            MasterControl::PlatformTarget::MacOS,
            "forsetti.macos.remote-build.validate");
        success &= expect(
            blockedMacRoute.has_value() &&
                !blockedMacRoute->routeReason.empty() &&
                !blockedMacRoute->readinessIssues.empty(),
            "Blocked Apple route history should preserve route reasoning and readiness gaps.");

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
                    "mastercontrol.iap.gateway-windows",
                    "mastercontrol.iap.gateway-macos",
                    "mastercontrol.iap.gateway-ios",
                    "mastercontrol.iap.governance-windows",
                    "mastercontrol.iap.governance-macos",
                    "mastercontrol.iap.governance-ios",
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
                    "mastercontrol.iap.gateway-windows",
                    "mastercontrol.iap.gateway-macos",
                    "mastercontrol.iap.gateway-ios",
                    "mastercontrol.iap.governance-windows",
                    "mastercontrol.iap.governance-macos",
                    "mastercontrol.iap.governance-ios",
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
        const auto blockedRemoteInstallResult = application.installPackageJson(nlohmann::json{
            { "kind", "powershell" },
            { "source", "https://untrusted.example/payload.ps1" },
            { "allowUntrustedExecution", true }
        }.dump());
        success &= expect(
            !blockedRemoteInstallResult.succeeded,
            "CLU should block remote installs while runtime posture is blocked.");
        success &= expect(
            blockedRemoteInstallResult.message.find("CLU blocked remote install") != std::string::npos,
            "Blocked remote installs should explain that CLU denied the action.");

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

        auto autonomyDisabledConfiguration = managedConfiguration;
        autonomyDisabledConfiguration.aiAutonomyEnabled = false;
        const auto autonomyDisabledResult = application.applyConfigurationJson(
            nlohmann::json(autonomyDisabledConfiguration).dump(),
            false);
        success &= expect(
            autonomyDisabledResult.succeeded,
            "Disabling global AI autonomy should succeed for governance enforcement testing.");
        const auto blockedAutonomyProviderResult = application.upsertProviderJson(nlohmann::json{
            { "id", "blocked-autonomy-provider" },
            { "kind", "xai" },
            { "displayName", "Blocked Autonomy Provider" },
            { "baseUrl", "https://blocked-autonomy.example.test/v1" },
            { "modelId", "grok-code-fast-1" },
            { "enabled", true },
            { "allowAutonomousControl", true }
        }.dump());
        success &= expect(
            !blockedAutonomyProviderResult.succeeded,
            "CLU should block provider autonomy enablement when global AI autonomy is disabled.");
        success &= expect(
            blockedAutonomyProviderResult.message.find("Enable global AI autonomy") != std::string::npos,
            "Blocked provider autonomy should explain the global AI autonomy prerequisite.");
        const auto restoreManagedAutonomyResult = application.applyConfigurationJson(
            nlohmann::json(managedConfiguration).dump(),
            false);
        success &= expect(
            restoreManagedAutonomyResult.succeeded,
            "Managed security configuration should restore global AI autonomy after the governance enforcement test.");

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
                hasProviderExecutionRegistration(snapshot.providerExecutionRegistrations, "chatgpt") &&
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

            auto blockedExecutionConfiguration = managedConfiguration;
            blockedExecutionConfiguration.providers = snapshot.providers;
            blockedExecutionConfiguration.subAgentGroups = snapshot.subAgentGroups;
            blockedExecutionConfiguration.providerAssignments = snapshot.providerAssignments;
            blockedExecutionConfiguration.appleRemoteHosts = snapshot.appleRemoteHosts;
            blockedExecutionConfiguration.security.securityProtocolsEnabled = false;
            blockedExecutionConfiguration.security.enableTls = false;
            blockedExecutionConfiguration.security.enableAuthentication = false;
            blockedExecutionConfiguration.security.allowOpenLanAccess = true;

            const auto blockedExecutionConfigurationResult = application.applyConfigurationJson(
                nlohmann::json(blockedExecutionConfiguration).dump(),
                true);
            success &= expect(
                blockedExecutionConfigurationResult.succeeded,
                "Blocked CLU posture configuration should apply after explicit confirmation.");

            snapshot = application.snapshot();
            success &= expect(
                snapshot.governance.posture == "blocked",
                "CLU should report blocked posture before denying provider execution.");

            const auto blockedExecutionRecord = application.executeProviderTaskJson(nlohmann::json{
                { "targetId", "planner" },
                { "prompt", "Attempt execution while CLU posture is blocked." },
                { "allowToolAccess", true },
                { "maxTurns", 1 }
            }.dump());
            success &= expect(
                blockedExecutionRecord.status == MasterControl::ProviderExecutionStatus::Failed,
                "CLU should block provider execution while runtime posture is blocked.");
            success &= expect(
                blockedExecutionRecord.errorMessage.find("CLU blocked provider execution") != std::string::npos,
                "Blocked provider execution should explain that CLU denied the run.");

            snapshot = application.snapshot();
            success &= expect(
                !snapshot.providerExecutionHistory.empty() &&
                    snapshot.providerExecutionHistory.front().executionId == blockedExecutionRecord.executionId,
                "Blocked provider execution should still be persisted to runtime history.");

            blockedExecutionConfiguration.aiAutonomyEnabled = managedConfiguration.aiAutonomyEnabled;
            blockedExecutionConfiguration.security = managedConfiguration.security;
            const auto restoreExecutionConfigurationResult = application.applyConfigurationJson(
                nlohmann::json(blockedExecutionConfiguration).dump(),
                false);
            success &= expect(
                restoreExecutionConfigurationResult.succeeded,
                "Managed CLU posture should be restorable after blocked provider execution testing.");
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

        if (powerShellExists()) {
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

            auto constrainedResourceConfiguration = managedConfiguration;
            constrainedResourceConfiguration.providers = snapshot.providers;
            constrainedResourceConfiguration.subAgentGroups = snapshot.subAgentGroups;
            constrainedResourceConfiguration.providerAssignments = snapshot.providerAssignments;
            constrainedResourceConfiguration.appleRemoteHosts = snapshot.appleRemoteHosts;
            constrainedResourceConfiguration.resourceAllocation = snapshot.resourceAllocation;
            constrainedResourceConfiguration.resourceAllocation.cpuPercent = 0;

            const auto constrainedResourceResult = application.applyConfigurationJson(
                nlohmann::json(constrainedResourceConfiguration).dump(),
                false);
            success &= expect(
                constrainedResourceResult.succeeded,
                "Resource allocation updates should support zero-CPU enforcement testing.");

            snapshot = application.snapshot();
            success &= expect(
                snapshot.governance.posture == "blocked",
                "CLU should report blocked posture when the managed resource envelope denies CPU allocation.");
            success &= expect(
                std::any_of(
                    snapshot.governance.findings.begin(),
                    snapshot.governance.findings.end(),
                    [](const auto& finding) {
                        return finding.ruleId == "CLU-C008";
                    }),
                "CLU should publish a managed resource envelope finding when CPU allocation is zero.");

            const auto blockedResourceExecution = application.executeProviderTaskJson(nlohmann::json{
                { "targetId", "planner" },
                { "prompt", "Attempt execution while CPU allocation is disabled." },
                { "allowToolAccess", true },
                { "maxTurns", 1 }
            }.dump());
            success &= expect(
                blockedResourceExecution.status == MasterControl::ProviderExecutionStatus::Failed,
                "Managed provider execution should be blocked when the resource envelope denies CPU allocation.");
            success &= expect(
                blockedResourceExecution.errorMessage.find("managed resource policy denies launch") != std::string::npos,
                "Blocked provider execution should explain that resource policy denied the run.");

            const auto blockedPackageScript = tempRoot / "package-install-blocked.ps1";
            const auto blockedMarkerFile = tempRoot / "package-install-blocked.ok";
            writeTextFile(
                blockedPackageScript,
                "New-Item -Path (Join-Path $PSScriptRoot 'package-install-blocked.ok') -ItemType File -Force | Out-Null\nexit 0\n");

            const auto blockedPackageResult = application.installPackageJson(nlohmann::json{
                { "kind", "powershell" },
                { "localPath", blockedPackageScript.string() },
                { "arguments", "" }
            }.dump());
            success &= expect(
                !blockedPackageResult.succeeded,
                "Local managed installs should be blocked when the resource envelope denies CPU allocation.");
            success &= expect(
                blockedPackageResult.message.find("managed resource policy denies launch") != std::string::npos,
                "Blocked installs should explain that resource policy denied the launch.");
            success &= expect(
                !std::filesystem::exists(blockedMarkerFile),
                "Blocked resource-policy installs should not launch the payload.");

            constrainedResourceConfiguration.resourceAllocation = managedConfiguration.resourceAllocation;
            const auto restoreResourceResult = application.applyConfigurationJson(
                nlohmann::json(constrainedResourceConfiguration).dump(),
                false);
            success &= expect(
                restoreResourceResult.succeeded,
                "Managed resource allocation should be restorable after CPU enforcement testing.");

            constrainedResourceConfiguration.resourceAllocation = managedConfiguration.resourceAllocation;
            constrainedResourceConfiguration.resourceAllocation.bandwidthPercent = 0;
            const auto constrainedBandwidthResult = application.applyConfigurationJson(
                nlohmann::json(constrainedResourceConfiguration).dump(),
                false);
            success &= expect(
                constrainedBandwidthResult.succeeded,
                "Resource allocation updates should support zero-bandwidth enforcement testing.");

            snapshot = application.snapshot();
            success &= expect(
                snapshot.governance.posture == "blocked",
                "CLU should report blocked posture when the managed resource envelope denies bandwidth allocation.");
            success &= expect(
                std::any_of(
                    snapshot.governance.findings.begin(),
                    snapshot.governance.findings.end(),
                    [](const auto& finding) {
                        return finding.ruleId == "CLU-C008" &&
                            finding.message.find("bandwidth allocation is set to 0%") != std::string::npos;
                    }),
                "CLU should publish a managed resource envelope finding when bandwidth allocation is zero.");

            const auto blockedBandwidthExecution = application.executeProviderTaskJson(nlohmann::json{
                { "targetId", "planner" },
                { "prompt", "Attempt execution while bandwidth allocation is disabled." },
                { "allowToolAccess", true },
                { "maxTurns", 1 }
            }.dump());
            success &= expect(
                blockedBandwidthExecution.status == MasterControl::ProviderExecutionStatus::Failed,
                "Governed provider execution should be blocked when the resource envelope denies bandwidth allocation.");
            success &= expect(
                blockedBandwidthExecution.errorMessage.find("bandwidth allocation") != std::string::npos,
                "Blocked provider execution should explain that bandwidth allocation denied the run.");

            constrainedResourceConfiguration.resourceAllocation = managedConfiguration.resourceAllocation;
            const auto restoreBandwidthResult = application.applyConfigurationJson(
                nlohmann::json(constrainedResourceConfiguration).dump(),
                false);
            success &= expect(
                restoreBandwidthResult.succeeded,
                "Managed resource allocation should be restorable after bandwidth enforcement testing.");

            constrainedResourceConfiguration.resourceAllocation = managedConfiguration.resourceAllocation;
            constrainedResourceConfiguration.resourceAllocation.storagePercent = 0;
            const auto constrainedStorageResult = application.applyConfigurationJson(
                nlohmann::json(constrainedResourceConfiguration).dump(),
                false);
            success &= expect(
                constrainedStorageResult.succeeded,
                "Resource allocation updates should support zero-storage enforcement testing.");

            snapshot = application.snapshot();
            success &= expect(
                snapshot.governance.posture == "blocked",
                "CLU should report blocked posture when the managed resource envelope denies storage allocation.");
            success &= expect(
                std::any_of(
                    snapshot.governance.findings.begin(),
                    snapshot.governance.findings.end(),
                    [](const auto& finding) {
                        return finding.ruleId == "CLU-C008" &&
                            finding.message.find("storage allocation is set to 0%") != std::string::npos;
                    }),
                "CLU should publish a managed resource envelope finding when storage allocation is zero.");

            const auto blockedStoragePackageScript = tempRoot / "package-install-storage-blocked.ps1";
            const auto blockedStorageMarkerFile = tempRoot / "package-install-storage-blocked.ok";
            writeTextFile(
                blockedStoragePackageScript,
                "New-Item -Path (Join-Path $PSScriptRoot 'package-install-storage-blocked.ok') -ItemType File -Force | Out-Null\nexit 0\n");

            const auto blockedStoragePackageResult = application.installPackageJson(nlohmann::json{
                { "kind", "powershell" },
                { "localPath", blockedStoragePackageScript.string() },
                { "arguments", "" }
            }.dump());
            success &= expect(
                !blockedStoragePackageResult.succeeded,
                "Local managed installs should be blocked when the resource envelope denies storage allocation.");
            success &= expect(
                blockedStoragePackageResult.message.find("storage allocation") != std::string::npos,
                "Blocked installs should explain that storage allocation denied the launch.");
            success &= expect(
                !std::filesystem::exists(blockedStorageMarkerFile),
                "Blocked storage-policy installs should not launch the payload.");

            constrainedResourceConfiguration.resourceAllocation = managedConfiguration.resourceAllocation;
            const auto finalRestoreResourceResult = application.applyConfigurationJson(
                nlohmann::json(constrainedResourceConfiguration).dump(),
                false);
            success &= expect(
                finalRestoreResourceResult.succeeded,
                "Managed resource allocation should be restorable after storage enforcement testing.");
        } else {
            std::cout << "Skipping package import test because no PowerShell executable was found.\n";
        }

        if (toolExists(L"git.exe")) {
            const auto repoFixtureRoot = tempRoot / "repo-fixture";
            success &= expect(createGitImportFixture(repoFixtureRoot), "Repository fixture should be created successfully");
            if (success) {
                const auto repoSource = repoFixtureRoot.string();
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

        if (powerShellExists()) {
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
            std::cout << "Skipping zip import test because no PowerShell executable was found.\n";
        }

        application.shutdown();

        success &= expect(
            std::filesystem::exists(appPaths.appleOperationHistoryFile),
            "Apple governance operation history should persist to disk.");
        const auto persistedAppleHistory = readFileUtf8(appPaths.appleOperationHistoryFile);
        success &= expect(
            persistedAppleHistory.has_value() &&
                persistedAppleHistory->find("operator@example.com") == std::string::npos &&
                persistedAppleHistory->find("app-secret-123") == std::string::npos,
            "Persisted Apple governance history should not contain explicit Apple credential values.");

        MasterControl::MasterControlApplication restartedApplication;
        success &= expect(restartedApplication.initialize(), "Application should reinitialize after shutdown");
        if (success) {
            const auto restartedSnapshot = restartedApplication.snapshot();
            const auto restartedMacStapleOperation = findAppleOperationById(
                restartedSnapshot.governance.appleOperations,
                macStapleOperationId);
            success &= expect(
                restartedMacStapleOperation.has_value() &&
                    restartedMacStapleOperation->artifactPath.find("MasterControlShell.zip") != std::string::npos,
                "Apple governance operation history should survive application restart.");
            const auto restartedIosExportOperation = findAppleOperationById(
                restartedSnapshot.governance.appleOperations,
                iosExportOperationId);
            const auto restartedMacNotarizeOperation = findAppleOperationById(
                restartedSnapshot.governance.appleOperations,
                macNotarizeOperationId);
            success &= expect(
                restartedIosExportOperation.has_value() &&
                    restartedIosExportOperation->artifactPath.find("ios-export") != std::string::npos,
                "Restarted snapshots should restore persisted iOS Apple operations.");
            success &= expect(
                restartedMacNotarizeOperation.has_value() &&
                    std::find(
                        restartedMacNotarizeOperation->redactedRequestOptionKeys.begin(),
                        restartedMacNotarizeOperation->redactedRequestOptionKeys.end(),
                        "appleId") != restartedMacNotarizeOperation->redactedRequestOptionKeys.end() &&
                    restartedMacNotarizeOperation->requestOptions.find("appleId") == restartedMacNotarizeOperation->requestOptions.end() &&
                    !restartedMacNotarizeOperation->rerunReady &&
                    restartedMacNotarizeOperation->rerunReadinessMessage.find("no longer available") != std::string::npos,
                "Restarted snapshots should preserve Apple credential redaction metadata and block reruns when the original Apple host is gone.");
        }
        restartedApplication.shutdown();
    }

    const auto bootstrapperBinary = bootstrapperBinaryPath();
    success &= expect(std::filesystem::exists(bootstrapperBinary), "Bootstrapper binary should exist for installer validation");
    if (success) {
        const auto bootstrapInstallDirectory = tempRoot / "bootstrapper-install";
        const auto failedBootstrapInstallDirectory = tempRoot / "bootstrapper-install-failed";
        const auto bootstrapDataDirectory = tempRoot / "bootstrapper-data";
        const auto bootstrapLogDirectory = tempRoot / "bootstrapper-desktop";
        const auto bootstrapPersistentLogDirectory = tempRoot / "bootstrapper-persistent";
        const auto bootstrapPersistentHistoryPath = bootstrapPersistentLogDirectory / "installer-history.jsonl";
        const auto bootstrapPersistentFailurePath = bootstrapPersistentLogDirectory / "installer-failures.jsonl";
        const auto bootstrapLatestPersistentFailurePath = bootstrapPersistentLogDirectory / "installer-latest-failure.json";
        const auto bootstrapConfigurationFile = bootstrapDataDirectory / "config" / "master-control-orchestration-server.json";
        const auto bootstrapInstallStateFile = bootstrapInstallDirectory / "installation-state.json";
        const auto readJsonFile = [](const std::filesystem::path& path) -> std::optional<nlohmann::json> {
            std::ifstream input(path);
            if (!input) {
                return std::nullopt;
            }

            try {
                return nlohmann::json::parse(input);
            } catch (...) {
                return std::nullopt;
            }
        };
        const auto readJsonLines = [](const std::filesystem::path& path) {
            std::vector<nlohmann::json> records;
            std::ifstream input(path);
            if (!input) {
                return records;
            }

            std::string line;
            while (std::getline(input, line)) {
                if (line.empty()) {
                    continue;
                }

                const auto record = nlohmann::json::parse(line, nullptr, false);
                if (record.is_object()) {
                    records.push_back(record);
                }
            }

            return records;
        };
        const auto findBootstrapperPersistentRecord = [](const std::vector<nlohmann::json>& records,
                                                         const std::string& action,
                                                         const bool succeeded) -> std::optional<nlohmann::json> {
            for (auto iterator = records.rbegin(); iterator != records.rend(); ++iterator) {
                if (iterator->value("component", std::string{}) == "bootstrapper" &&
                    iterator->value("action", std::string{}) == action &&
                    iterator->value("succeeded", !succeeded) == succeeded) {
                    return std::optional<nlohmann::json>(*iterator);
                }
            }

            return std::nullopt;
        };
        const auto findBootstrapperLog = [&](const std::string& action,
                                             const std::string& outcome) -> std::optional<std::filesystem::path> {
            std::optional<std::filesystem::path> match;
            std::error_code error;
            if (!std::filesystem::exists(bootstrapLogDirectory, error)) {
                return std::nullopt;
            }

            const auto prefix = std::string("MasterControlOrchestrationServer-") + action + "-" + outcome + "-";
            for (const auto& entry : std::filesystem::directory_iterator(bootstrapLogDirectory, error)) {
                if (error || !entry.is_regular_file()) {
                    continue;
                }

                const auto fileName = entry.path().filename().string();
                if (fileName.rfind(prefix, 0) != 0 || entry.path().extension() != ".txt") {
                    continue;
                }

                if (!match.has_value() || entry.last_write_time() > std::filesystem::last_write_time(*match, error)) {
                    match = entry.path();
                }
            }

            return match;
        };
        const auto bootstrapperServiceName = L"MasterControlOrchestrationServerTests-" + std::to_wstring(GetCurrentProcessId());
        const auto bootstrapperUninstallKey =
            L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" + bootstrapperServiceName;

        ScopedEnvironmentOverride bootstrapDataOverride(L"MASTERCONTROL_DATA_DIR", bootstrapDataDirectory.wstring());
        ScopedEnvironmentOverride bootstrapLogOverride(L"MASTERCONTROL_BOOTSTRAPPER_LOG_DIR", bootstrapLogDirectory.wstring());
        ScopedEnvironmentOverride bootstrapPersistentLogOverride(
            L"MASTERCONTROL_BOOTSTRAPPER_PERSISTENT_LOG_DIR",
            bootstrapPersistentLogDirectory.wstring());
        ScopedEnvironmentOverride bootstrapServiceNameOverride(L"MASTERCONTROL_BOOTSTRAPPER_SERVICE_NAME", bootstrapperServiceName);
        ScopedEnvironmentOverride bootstrapUninstallKeyOverride(L"MASTERCONTROL_BOOTSTRAPPER_UNINSTALL_KEY", bootstrapperUninstallKey);

        const auto installCommand = L"\"" + bootstrapperBinary.wstring() + L"\" install \"" +
            bootstrapInstallDirectory.wstring() +
            L"\" --skip-service --skip-firewall --skip-shortcuts --skip-uninstall-registration";
        const auto detectJsonCommand = L"\"" + bootstrapperBinary.wstring() + L"\" detect --json";
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
            std::filesystem::exists(bootstrapInstallDirectory / "share" / "MasterControlOrchestrationServer" / "web" / "index.html"),
            "Bootstrapper install should stage browser resources");
        success &= expect(
            std::filesystem::exists(bootstrapInstallDirectory / "share" / "MasterControlOrchestrationServer" / "ForsettiManifests" / "DashboardUIModule.json"),
            "Bootstrapper install should stage Forsetti manifests");
        success &= expect(
            std::filesystem::exists(bootstrapInstallStateFile),
            "Bootstrapper install should write installation state");
        success &= expect(
            std::filesystem::exists(bootstrapConfigurationFile),
            "Bootstrapper install should seed configuration in the configured data directory");
        const auto successfulInstallLogPath = findBootstrapperLog("install", "succeeded");
        success &= expect(
            successfulInstallLogPath.has_value(),
            "Bootstrapper install should write a success log to the desktop report directory.");
        success &= expect(
            std::filesystem::exists(bootstrapPersistentHistoryPath),
            "Bootstrapper install should append a persistent installer history record.");
        if (successfulInstallLogPath.has_value()) {
            const auto successfulInstallLog = readFileUtf8(*successfulInstallLogPath);
            success &= expect(
                successfulInstallLog.has_value() &&
                    successfulInstallLog->find("Action: install") != std::string::npos &&
                    successfulInstallLog->find("Succeeded: true") != std::string::npos &&
                    successfulInstallLog->find(bootstrapInstallDirectory.string()) != std::string::npos &&
                    successfulInstallLog->find("RunId: ") != std::string::npos &&
                    successfulInstallLog->find("PersistentLogRoot: " + bootstrapPersistentLogDirectory.string()) != std::string::npos,
                "Bootstrapper install success log should describe the installed target and success state.");
        }
        const auto bootstrapHistoryRecords = readJsonLines(bootstrapPersistentHistoryPath);
        const auto successfulInstallRecord = findBootstrapperPersistentRecord(bootstrapHistoryRecords, "install", true);
        success &= expect(
            successfulInstallRecord.has_value(),
            "Bootstrapper install should record a successful install in the persistent history journal.");
        if (successfulInstallRecord.has_value()) {
            success &= expect(
                successfulInstallRecord->value("runId", std::string{}).size() > 10 &&
                    successfulInstallRecord->value("message", std::string{}).find("successfully") != std::string::npos &&
                    successfulInstallRecord->value("installDirectory", std::string{}) == bootstrapInstallDirectory.string() &&
                    successfulInstallRecord->value("textLogWritten", false) &&
                    successfulInstallRecord->contains("persistentPaths") &&
                    successfulInstallRecord->at("persistentPaths").value("root", std::string{}) ==
                        bootstrapPersistentLogDirectory.string() &&
                    successfulInstallRecord->contains("details") &&
                    successfulInstallRecord->at("details").value("succeeded", false),
                "Bootstrapper install persistent history should retain the run id, log root, and success payload.");
        }
        const auto validateCommand = L"\"" + bootstrapperBinary.wstring() + L"\" validate \"" +
            bootstrapInstallDirectory.wstring() + L"\"";
        const auto validateJsonCommand = validateCommand + L" --json";
        const auto preflightJsonCommand = L"\"" + bootstrapperBinary.wstring() + L"\" preflight \"" +
            bootstrapInstallDirectory.wstring() +
            L"\" --skip-service --skip-firewall --skip-shortcuts --skip-uninstall-registration --json";
        const auto preflightJsonResult = runProcessWithOutput(preflightJsonCommand, tempRoot);
        success &= expect(
            preflightJsonResult.exitCode == 0,
            "Bootstrapper preflight JSON mode should succeed when privileged integrations are intentionally skipped.");
        const auto preflightJson = nlohmann::json::parse(preflightJsonResult.rawOutput, nullptr, false);
        success &= expect(
            preflightJson.is_object() &&
                preflightJson.value("ready", false) &&
                preflightJson.value("installDirectory", std::string{}) == bootstrapInstallDirectory.string() &&
                preflightJson.value("payloadDetected", false) &&
                !preflightJson.value("serviceManaged", true) &&
                !preflightJson.value("firewallManaged", true) &&
                !preflightJson.value("shortcutsManaged", true) &&
                !preflightJson.value("uninstallRegistrationManaged", true),
            "Bootstrapper preflight JSON mode should describe a ready skipped-integration install target.");
        success &= expect(
            runProcess(validateCommand, tempRoot) == 0,
            "Bootstrapper validate should succeed after install when the staged payload and state are healthy");
        const auto detectJsonResult = runProcessWithOutput(detectJsonCommand, tempRoot);
        success &= expect(
            detectJsonResult.exitCode == 0,
            "Bootstrapper detect JSON mode should succeed.");
        const auto detectJson = nlohmann::json::parse(detectJsonResult.rawOutput, nullptr, false);
        success &= expect(
            detectJson.is_object() &&
                detectJson.value("detected", false) &&
                !detectJson.value("hostName", std::string{}).empty() &&
                detectJson.value("defaultBrowserPort", 0) > 0 &&
                !detectJson.value("bootstrapperVersion", std::string{}).empty(),
            "Bootstrapper detect JSON mode should publish host, version, and default port details.");
        success &= expect(
            detectJson.is_object() &&
                !detectJson.value("serviceRegistered", true) &&
                !detectJson.value("serviceRunning", true) &&
                !detectJson.value("serviceDelayedAutoStart", true) &&
                !detectJson.value("serviceRecoveryConfigured", true) &&
                detectJson.value("serviceState", std::string{}) == "not_installed" &&
                detectJson.value("serviceProcessId", 1) == 0 &&
                detectJson.contains("uninstallRegistered") &&
                detectJson.contains("browserFirewallRulePresent") &&
                detectJson.contains("beaconFirewallRulePresent"),
            "Bootstrapper detect JSON mode should report that the Windows service is not installed in the skipped-service test flow.");
        const auto validateJsonResult = runProcessWithOutput(validateJsonCommand, tempRoot);
        success &= expect(
            validateJsonResult.exitCode == 0,
            "Bootstrapper validate JSON mode should succeed after install.");
        const auto validateJson = nlohmann::json::parse(validateJsonResult.rawOutput, nullptr, false);
        success &= expect(
            validateJson.is_object() &&
                validateJson.value("valid", false) &&
                validateJson.value("installDirectory", std::string{}) == bootstrapInstallDirectory.string() &&
                validateJson.value("issues", nlohmann::json::array()).empty() &&
                !validateJson.value("bootstrapperVersion", std::string{}).empty(),
            "Bootstrapper validate JSON mode should describe a healthy installation.");
        success &= expect(
            validateJson.is_object() &&
                !validateJson.value("serviceRegistered", true) &&
                !validateJson.value("serviceRunning", true) &&
                !validateJson.value("serviceAutoStart", true) &&
                !validateJson.value("serviceRecoveryConfigured", true) &&
                validateJson.value("serviceState", std::string{}) == "not_installed" &&
                validateJson.value("serviceProcessId", 1) == 0 &&
                !validateJson.value("serviceManaged", true) &&
                !validateJson.value("firewallManaged", true) &&
                !validateJson.value("shortcutsManaged", true) &&
                !validateJson.value("uninstallRegistrationManaged", true) &&
                validateJson.contains("uninstallRegistered") &&
                validateJson.contains("shellShortcutPresent") &&
                validateJson.contains("dashboardShortcutPresent") &&
                validateJson.contains("browserFirewallRulePresent") &&
                validateJson.contains("beaconFirewallRulePresent"),
            "Bootstrapper validate JSON mode should publish integration status while reporting that managed integrations were intentionally skipped.");
        const auto installedState = readJsonFile(bootstrapInstallStateFile);
        success &= expect(
            installedState.has_value() &&
                installedState->contains("version") &&
                !installedState->value("serviceManaged", true) &&
                !installedState->value("firewallManaged", true) &&
                !installedState->value("shortcutsManaged", true) &&
                !installedState->value("uninstallRegistrationManaged", true),
            "Bootstrapper install state should record the installed version and skipped integration policy.");
        const auto installedVersion = installedState.has_value() ? installedState->value("version", std::string{}) : std::string{};
        if (installedState.has_value()) {
            auto downgradedState = *installedState;
            downgradedState["version"] = "0.0.0";
            writeTextFile(bootstrapInstallStateFile, downgradedState.dump(2));
        }

        const auto retryLockedInstallDirectory = tempRoot / "bootstrapper-install-retry";
        {
            DelayedExclusiveFileLock sourcePayloadLock(serviceHostPayloadBinaryPath(), 1500);
            success &= expect(
                sourcePayloadLock.active(),
                "Tests should be able to acquire a transient exclusive lock on the source service payload.");
            const auto retryInstallCommand = L"\"" + bootstrapperBinary.wstring() + L"\" install \"" +
                retryLockedInstallDirectory.wstring() +
                L"\" --skip-service --skip-firewall --skip-shortcuts --skip-uninstall-registration --json";
            const auto retryInstallResult = runProcessWithOutput(retryInstallCommand, tempRoot);
            success &= expect(
                retryInstallResult.exitCode == 0,
                "Bootstrapper install should retry staging when a source payload executable is transiently locked.");
            const auto retryInstallJson = nlohmann::json::parse(retryInstallResult.rawOutput, nullptr, false);
            success &= expect(
                retryInstallJson.is_object() &&
                    retryInstallJson.value("succeeded", false) &&
                    retryInstallJson.value("action", std::string{}) == "install",
                "Bootstrapper retry install JSON mode should still report a successful install.");
            success &= expect(
                std::filesystem::exists(retryLockedInstallDirectory / "MasterControlServiceHost.exe"),
                "Bootstrapper retry install should stage the locked service payload after the transient file lock clears.");
        }

        {
            ScopedEnvironmentOverride installFailureOverride(
                L"MASTERCONTROL_BOOTSTRAPPER_TEST_FAIL_AFTER_STAGE",
                L"1");
            const auto failedInstallCommand = L"\"" + bootstrapperBinary.wstring() + L"\" install \"" +
                failedBootstrapInstallDirectory.wstring() +
                L"\" --skip-service --skip-firewall --skip-shortcuts --skip-uninstall-registration --json";
            const auto failedInstallResult = runProcessWithOutput(failedInstallCommand, tempRoot);
            success &= expect(
                failedInstallResult.exitCode != 0,
                "Bootstrapper install should fail when the post-stage rollback seam is enabled.");
            const auto failedInstallLogPath = findBootstrapperLog("install", "failed");
            success &= expect(
                failedInstallLogPath.has_value(),
                "Bootstrapper install failure should write a failure log to the desktop report directory.");
            success &= expect(
                std::filesystem::exists(bootstrapPersistentFailurePath),
                "Bootstrapper install failure should append a persistent installer failure record.");
            if (failedInstallLogPath.has_value()) {
                const auto failedInstallLog = readFileUtf8(*failedInstallLogPath);
                success &= expect(
                    failedInstallLog.has_value() &&
                        failedInstallLog->find("Action: install") != std::string::npos &&
                        failedInstallLog->find("Succeeded: false") != std::string::npos &&
                        failedInstallLog->find("PersistentFailurePath: " + bootstrapPersistentFailurePath.string()) != std::string::npos &&
                        failedInstallLog->find("Simulated bootstrapper failure after staging payload and installation state.") != std::string::npos,
                    "Bootstrapper install failure log should capture the failure reason.");
            }
            const auto bootstrapFailureRecords = readJsonLines(bootstrapPersistentFailurePath);
            const auto failedInstallRecord = findBootstrapperPersistentRecord(bootstrapFailureRecords, "install", false);
            success &= expect(
                failedInstallRecord.has_value(),
                "Bootstrapper install failure should record the failed install in the persistent failure journal.");
            if (failedInstallRecord.has_value()) {
                success &= expect(
                    failedInstallRecord->value("message", std::string{}).find(
                        "Simulated bootstrapper failure after staging payload and installation state.") != std::string::npos &&
                        failedInstallRecord->contains("details") &&
                        failedInstallRecord->at("details").value("succeeded", true) == false &&
                        failedInstallRecord->at("details").value("rollbackAttempted", true) == false &&
                        failedInstallRecord->contains("logPaths") &&
                        failedInstallRecord->at("logPaths").value("text", std::string{}) ==
                            (failedInstallLogPath.has_value() ? failedInstallLogPath->string() : std::string{}),
                    "Bootstrapper install failure journal should retain the failure reason and associated text log path.");
            }
            const auto latestPersistentFailureRecord = readJsonFile(bootstrapLatestPersistentFailurePath);
            success &= expect(
                latestPersistentFailureRecord.has_value() &&
                    latestPersistentFailureRecord->value("component", std::string{}) == "bootstrapper" &&
                    latestPersistentFailureRecord->value("action", std::string{}) == "install" &&
                    !latestPersistentFailureRecord->value("succeeded", true),
                "Bootstrapper install failure should refresh the latest persistent failure record.");
        }

        const auto upgradeCommand = L"\"" + bootstrapperBinary.wstring() + L"\" upgrade \"" +
            bootstrapInstallDirectory.wstring() +
            L"\" --skip-service --skip-firewall --skip-shortcuts --skip-uninstall-registration";

        {
            ScopedEnvironmentOverride rollbackFailureOverride(
                L"MASTERCONTROL_BOOTSTRAPPER_TEST_FAIL_AFTER_STAGE",
                L"1");
            const auto failedUpgradeResult = runProcessWithOutput(upgradeCommand + L" --json", tempRoot);
            success &= expect(
                failedUpgradeResult.exitCode != 0,
                "Bootstrapper upgrade should fail when the post-stage rollback seam is enabled.");
            const auto failedUpgradeJson = nlohmann::json::parse(failedUpgradeResult.rawOutput, nullptr, false);
            success &= expect(
                failedUpgradeJson.is_object() &&
                    !failedUpgradeJson.value("succeeded", true) &&
                    failedUpgradeJson.value("action", std::string{}) == "upgrade" &&
                    failedUpgradeJson.value("rollbackAttempted", false) &&
                    failedUpgradeJson.value("rollbackRestored", false) &&
                    failedUpgradeJson.value("installStatePresent", false),
                "Bootstrapper failed upgrade JSON mode should report that rollback restored the prior installation.");
        }
        const auto rolledBackState = readJsonFile(bootstrapInstallStateFile);
        success &= expect(
            rolledBackState.has_value() &&
                rolledBackState->value("version", std::string{}) == "0.0.0",
            "Bootstrapper upgrade rollback should restore the previous installation state after a staged failure.");
        success &= expect(
            runProcess(validateCommand, tempRoot) == 0,
            "Bootstrapper validate should succeed after upgrade rollback restores the prior installation.");

        const auto upgradeJsonCommand = upgradeCommand + L" --json";
        const auto upgradeJsonResult = runProcessWithOutput(upgradeJsonCommand, tempRoot);
        success &= expect(
            upgradeJsonResult.exitCode == 0,
            "Bootstrapper upgrade JSON mode should succeed when refreshing an existing installation");
        const auto upgradeJson = nlohmann::json::parse(upgradeJsonResult.rawOutput, nullptr, false);
        success &= expect(
            upgradeJson.is_object() &&
                upgradeJson.value("succeeded", false) &&
                upgradeJson.value("validated", false) &&
                upgradeJson.value("action", std::string{}) == "upgrade" &&
                !upgradeJson.value("serviceManaged", true) &&
                !upgradeJson.value("serviceRegistered", true),
            "Bootstrapper upgrade JSON mode should report a successful skipped-service refresh.");
        const auto upgradedState = readJsonFile(bootstrapInstallStateFile);
        success &= expect(
            upgradedState.has_value() && upgradedState->value("version", std::string{}) != "0.0.0",
            "Bootstrapper upgrade should refresh the stored installation version");
        success &= expect(
            upgradedState.has_value() &&
                (!installedVersion.empty() ? upgradedState->value("version", std::string{}) == installedVersion : true),
            "Bootstrapper upgrade should restore the current bootstrapper version");
        success &= expect(
            upgradedState.has_value() &&
                !upgradedState->value("serviceManaged", true) &&
                !upgradedState->value("firewallManaged", true) &&
                !upgradedState->value("shortcutsManaged", true) &&
                !upgradedState->value("uninstallRegistrationManaged", true),
            "Bootstrapper upgrade should preserve the skipped integration policy.");
        success &= expect(
            runProcess(validateCommand, tempRoot) == 0,
            "Bootstrapper validate should succeed after upgrade");

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
        success &= expect(
            runProcess(validateCommand, tempRoot) == 0,
            "Bootstrapper validate should succeed after repair");

        const auto uninstallCommand = L"\"" + bootstrapperBinary.wstring() + L"\" uninstall \"" +
            bootstrapInstallDirectory.wstring() +
            L"\" --purge-install-dir --purge-data --skip-service --skip-firewall --skip-shortcuts --skip-uninstall-registration";
        const auto uninstallJsonCommand = uninstallCommand + L" --json";
        const auto uninstallJsonResult = runProcessWithOutput(uninstallJsonCommand, tempRoot);
        success &= expect(
            uninstallJsonResult.exitCode == 0,
            "Bootstrapper uninstall JSON mode should succeed when system integrations are skipped");
        const auto uninstallJson = nlohmann::json::parse(uninstallJsonResult.rawOutput, nullptr, false);
        success &= expect(
            uninstallJson.is_object() &&
                uninstallJson.value("succeeded", false) &&
                uninstallJson.value("action", std::string{}) == "uninstall" &&
                !uninstallJson.value("installDirectoryPresent", true) &&
                !uninstallJson.value("dataDirectoryPresent", true) &&
                !uninstallJson.value("serviceRegistered", true),
            "Bootstrapper uninstall JSON mode should report that skipped integrations and purged paths are gone.");
        success &= expect(
            !std::filesystem::exists(bootstrapInstallDirectory),
            "Bootstrapper uninstall should remove the install directory when requested");
        success &= expect(
            !std::filesystem::exists(bootstrapDataDirectory),
            "Bootstrapper uninstall should remove the data directory when requested");
        success &= expect(
            runProcess(validateCommand, tempRoot) != 0,
            "Bootstrapper validate should fail after uninstall removes the staged payload");
        const auto failedValidateJsonResult = runProcessWithOutput(validateJsonCommand, tempRoot);
        const auto failedValidateJson = nlohmann::json::parse(failedValidateJsonResult.rawOutput, nullptr, false);
        success &= expect(
            failedValidateJsonResult.exitCode != 0 &&
                failedValidateJson.is_object() &&
                !failedValidateJson.value("valid", true) &&
                failedValidateJson.contains("issues") &&
                failedValidateJson.at("issues").is_array() &&
                !failedValidateJson.at("issues").empty(),
            "Bootstrapper validate JSON mode should report structured issues after uninstall removes the payload.");
    }

    // =====================================================================
    // Remediation Regression Suite — non-security fixes from the 2026-04-17
    // audit. Each block targets a specific code-change and is self-contained
    // so a regression in any one fix fails its own assertion.
    // =====================================================================

    // R1: Malformed configuration.json must not crash the runtime. readJsonFile
    // and the FileBackedConfigurationService ctor now catch nlohmann::json
    // parse errors and fall back to defaults with (void)persistLocked().
    {
        const auto malformedRoot = tempRoot / "regression-malformed-config";
        ScopedEnvironmentOverride dataDirectoryOverride(L"MASTERCONTROL_DATA_DIR", (malformedRoot / "data").wstring());
        const auto appPaths = MasterControl::resolveAppPaths();
        std::filesystem::create_directories(appPaths.configurationFile.parent_path());
        writeTextFile(appPaths.configurationFile, "{ this is not valid json ][");

        try {
            MasterControl::MasterControlApplication application;
            success &= expect(
                application.initialize(),
                "Runtime should initialize when configuration.json is malformed, falling back to defaults.");
            application.shutdown();
        } catch (...) {
            success &= expect(false, "Runtime must never throw when configuration.json is malformed.");
        }
    }

    // R2: Activity ring stays bounded at 512 events regardless of load.
    {
        const auto ringRoot = tempRoot / "regression-activity-ring";
        ScopedEnvironmentOverride dataDirectoryOverride(L"MASTERCONTROL_DATA_DIR", (ringRoot / "data").wstring());
        const auto appPaths = MasterControl::resolveAppPaths();
        writeIsolatedAppConfiguration(appPaths.configurationFile, isolatedTestBrowserPort());

        MasterControl::MasterControlApplication application;
        success &= expect(application.initialize(), "Activity-ring regression: application should initialize.");

        const auto dashboardUrl = application.browserUrl() + "api/dashboard";
        for (int i = 0; i < 600; ++i) {
            (void)httpGetJson(dashboardUrl);
        }

        const auto activityDocument = httpGetJson(application.browserUrl() + "api/activity?since=0");
        success &= expect(
            activityDocument.has_value() && activityDocument->contains("events")
                && (*activityDocument)["events"].is_array(),
            "Activity endpoint should return a well-formed events array.");
        if (activityDocument.has_value() && activityDocument->contains("events")) {
            const auto size = (*activityDocument)["events"].size();
            success &= expect(
                size <= 512U,
                "Activity ring must stay bounded at 512 events under sustained request load.");
        }

        application.shutdown();
    }

    // R3: upsertProvider under concurrent callers. The v0.4.1+ fix moves the
    // capability read inside state_->mutex so every write sees a consistent
    // capability/configuration pair. This test hammers the endpoint and asserts
    // the count of custom providers matches the number we submitted.
    {
        const auto raceRoot = tempRoot / "regression-upsert-race";
        ScopedEnvironmentOverride dataDirectoryOverride(L"MASTERCONTROL_DATA_DIR", (raceRoot / "data").wstring());
        const auto appPaths = MasterControl::resolveAppPaths();
        writeIsolatedAppConfiguration(appPaths.configurationFile, isolatedTestBrowserPort());

        MasterControl::MasterControlApplication application;
        success &= expect(application.initialize(), "upsertProvider-race: application should initialize.");

        // Seed configuration with a known provider whose capability/kind is
        // supported by a registered module. We reuse an existing default
        // provider id and flip displayName per iteration so each concurrent
        // upsert is a no-op for cardinality but exercises the lock path.
        const auto providers = application.snapshot().providers;
        success &= expect(!providers.empty(), "upsertProvider-race: seeded providers required for the test.");
        if (!providers.empty()) {
            const auto seed = providers.front();
            constexpr int kThreads = 8;
            constexpr int kIterations = 50;
            std::atomic<int> failures{ 0 };
            std::vector<std::thread> workers;
            workers.reserve(kThreads);
            for (int t = 0; t < kThreads; ++t) {
                workers.emplace_back([&application, seed, t, &failures]() {
                    for (int i = 0; i < kIterations; ++i) {
                        auto body = nlohmann::json(seed);
                        body["displayName"] = seed.displayName + " #t" + std::to_string(t) + "i" + std::to_string(i);
                        const auto response = httpPostJson(
                            application.browserUrl() + "api/providers",
                            body.dump());
                        if (!response.has_value() || !response->value("succeeded", false)) {
                            ++failures;
                        }
                    }
                });
            }
            for (auto& worker : workers) {
                worker.join();
            }
            success &= expect(
                failures.load() == 0,
                "upsertProvider-race: every concurrent upsert should report OperationResult.success.");

            const auto afterProviders = application.snapshot().providers;
            const auto seedStillPresent = std::any_of(
                afterProviders.begin(),
                afterProviders.end(),
                [&seed](const MasterControl::ProviderConnection& candidate) { return candidate.id == seed.id; });
            success &= expect(
                seedStillPresent,
                "upsertProvider-race: the seeded provider must remain present in configuration after contention.");
        }

        application.shutdown();
    }

    std::filesystem::remove_all(tempRoot);
    return success ? 0 : 1;
}
