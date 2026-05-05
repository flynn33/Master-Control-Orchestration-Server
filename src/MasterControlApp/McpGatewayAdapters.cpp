// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

// winsock2 MUST come before windows.h. The header (McpGatewayAdapters.h)
// includes them in the right order; doing it here too defends against any
// future TU-level include-order regression.
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "ws2_32.lib")
#endif

#include "MasterControl/McpGatewayAdapters.h"

#include <chrono>
#include <filesystem>
#include <sstream>
#include <string>

namespace MasterControl {

namespace {

std::string trimTrailingSlash(std::string value) {
    while (!value.empty() && value.back() == '/') {
        value.pop_back();
    }
    return value;
}

std::string ensureLeadingSlash(std::string value) {
    if (value.empty()) {
        return "/";
    }
    if (value.front() != '/') {
        return "/" + value;
    }
    return value;
}

std::string composeMcpUrl(const McpGatewayConfiguration& configuration) {
    std::ostringstream stream;
    stream << "http://"
           << (configuration.listenHost.empty() ? std::string("0.0.0.0") : configuration.listenHost)
           << ":"
           << configuration.listenPort
           << ensureLeadingSlash(configuration.mcpPath);
    return stream.str();
}

std::string composeHealthUrl(const McpGatewayConfiguration& configuration) {
    std::ostringstream stream;
    stream << "http://"
           << (configuration.listenHost.empty() ? std::string("0.0.0.0") : configuration.listenHost)
           << ":"
           << configuration.listenPort
           << ensureLeadingSlash(configuration.healthPath);
    return stream.str();
}

#if defined(_WIN32)
std::wstring widen(const std::string& utf8) {
    if (utf8.empty()) {
        return std::wstring();
    }
    const int length = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring wide(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), wide.data(), length);
    return wide;
}
#endif

} // namespace

// ---------------------------------------------------------------------------
// McpJungleGatewayAdapter
// ---------------------------------------------------------------------------

McpJungleGatewayAdapter::McpJungleGatewayAdapter(McpGatewayConfiguration configuration)
    : configuration_(std::move(configuration)) {
    status_.adapterType = AdapterType();
    status_.mcpUrl = composeMcpUrl(configuration_);
    status_.state = configuration_.enabled
        ? GatewayState::Configured
        : GatewayState::Disabled;
    status_.message = configuration_.enabled
        ? "MCPJungle adapter is configured. Call Start() to launch the supervised binary."
        : "MCP Gateway is disabled. Set mcpGateway.enabled=true in configuration to opt in.";
}

McpJungleGatewayAdapter::~McpJungleGatewayAdapter() {
    terminateChildProcessTreeIfRunning();
    // Make sure the honest-503 listener thread is joined cleanly before
    // members start tearing down. Guarded by the same mutex the start /
    // stop path uses.
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopHonestUnavailableListenerLocked();
    }
}

GatewayStatus McpJungleGatewayAdapter::Start() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!configuration_.enabled) {
        status_.state = GatewayState::Disabled;
        status_.message = "Cannot start: mcpGateway.enabled is false.";
        status_.startedAtUtc.clear();
        // Even when disabled, claim the gateway port with an honest 503
        // listener so LAN clients get a structured error instead of TCP
        // RST. Eliminates the "connection refused" confusion when remote
        // AI clients point at the advertised gateway URL before MCPJungle
        // is configured.
        startHonestUnavailableListenerLocked();
        return status_;
    }
    if (status_.state == GatewayState::Running || status_.state == GatewayState::Starting) {
        return status_;
    }

    status_.state = GatewayState::Starting;
    status_.message = "Resolving MCPJungle binary.";

    // PHASE-02 supervised-mock fallback: if a binary path is not configured
    // or not on disk, the adapter still transitions Configured->Running so
    // the rest of the runtime (status API, registration code paths, tests
    // covering state transitions) can exercise the IMcpGateway contract.
    // This intentionally does NOT fabricate health: Probe() will return
    // GatewayHealthStatus::Unknown until a real binary is reachable.
    const bool binaryConfigured = !configuration_.binaryPath.empty();
    const bool binaryPresent = binaryConfigured
        && std::filesystem::exists(std::filesystem::path(configuration_.binaryPath));

#if defined(_WIN32)
    if (binaryPresent) {
        // Real binary about to take the port — release our placeholder
        // listener first so MCPJungle's bind() doesn't fail with EADDRINUSE.
        stopHonestUnavailableListenerLocked();

        if (jobObject_ == nullptr) {
            jobObject_ = CreateJobObjectW(nullptr, nullptr);
            if (jobObject_ != nullptr) {
                JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
                limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
                SetInformationJobObject(jobObject_, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
            }
        }

        std::wstring commandLine = L"\"" + widen(configuration_.binaryPath) + L"\" start";
        if (!configuration_.databasePath.empty()) {
            commandLine += L" --db \"" + widen(configuration_.databasePath) + L"\"";
        }

        STARTUPINFOW startupInfo{};
        startupInfo.cb = sizeof(startupInfo);
        startupInfo.dwFlags = STARTF_USESHOWWINDOW;
        startupInfo.wShowWindow = SW_HIDE;

        std::vector<wchar_t> mutableCommandLine(commandLine.begin(), commandLine.end());
        mutableCommandLine.push_back(L'\0');

        PROCESS_INFORMATION processInfo{};
        const BOOL launched = CreateProcessW(
            nullptr,
            mutableCommandLine.data(),
            nullptr, nullptr,
            FALSE,
            CREATE_NO_WINDOW | CREATE_SUSPENDED,
            nullptr, nullptr,
            &startupInfo,
            &processInfo);
        if (!launched) {
            status_.state = GatewayState::Failed;
            status_.message = "CreateProcessW failed for the configured MCPJungle binary.";
            return status_;
        }
        if (jobObject_ != nullptr) {
            AssignProcessToJobObject(jobObject_, processInfo.hProcess);
        }
        ResumeThread(processInfo.hThread);

        processInfo_ = processInfo;
        childProcessActive_ = true;
        status_.state = GatewayState::Running;
        status_.message = "MCPJungle child process started under MCOS Job Object supervision.";
        status_.startedAtUtc = timestampNowUtc();
        return status_;
    }
#endif

    // Supervised-mock path.
    childProcessActive_ = false;
    status_.state = GatewayState::Running;
    status_.startedAtUtc = timestampNowUtc();
    if (binaryConfigured) {
        status_.message = "MCPJungle binary path is configured but the file was not found. Adapter is in supervised-mock mode; install the binary to enable real gateway traffic.";
    } else {
        status_.message = "No MCPJungle binary path configured. Adapter is in supervised-mock mode (state machine + registration only; health probe will report unknown).";
    }
    // Honest 503 listener for the supervised-mock window — same reasoning
    // as the disabled branch above: claim the port with a real listener
    // so LAN clients see a structured response instead of TCP RST.
    startHonestUnavailableListenerLocked();
    return status_;
}

GatewayStatus McpJungleGatewayAdapter::Stop() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (status_.state == GatewayState::Stopped || status_.state == GatewayState::Disabled) {
        // Already inert. Make sure the placeholder listener is also down
        // (Stop on an already-disabled adapter shouldn't leave the port
        // listening with a stale-state body).
        stopHonestUnavailableListenerLocked();
        return status_;
    }

    status_.state = GatewayState::Stopping;
    status_.message = "Stopping MCPJungle adapter.";

    terminateChildProcessTreeIfRunning();
    // Bring the placeholder listener back up so the gateway port keeps
    // its honest 503 response after Stop() completes.
    stopHonestUnavailableListenerLocked();

    status_.state = GatewayState::Stopped;
    status_.startedAtUtc.clear();
    status_.message = "MCPJungle adapter stopped. Registry preserved in-memory.";
    startHonestUnavailableListenerLocked();
    return status_;
}

GatewayStatus McpJungleGatewayAdapter::CurrentStatus() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}

GatewayHealth McpJungleGatewayAdapter::Probe() {
    GatewayHealth health = probeOverHttp();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        health.adapterType = AdapterType();
        health.mcpUrl = composeMcpUrl(configuration_);
        health.healthUrl = composeHealthUrl(configuration_);
        health.registeredServerCount = static_cast<int>(registry_.size());
    }
    health.probedAtUtc = timestampNowUtc();
    return health;
}

GatewayHealth McpJungleGatewayAdapter::probeOverHttp() const {
    GatewayHealth health;
    health.adapterType = AdapterType();

    GatewayState localState;
    bool enabled;
    std::string host;
    uint16_t port;
    std::string healthPath;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        localState = status_.state;
        enabled = configuration_.enabled;
        host = configuration_.listenHost.empty() ? std::string("127.0.0.1") : configuration_.listenHost;
        if (host == "0.0.0.0") {
            // 0.0.0.0 is a bind wildcard, not a connect target. Probe localhost.
            host = "127.0.0.1";
        }
        port = configuration_.listenPort;
        healthPath = ensureLeadingSlash(configuration_.healthPath);
    }

    if (!enabled) {
        health.status = GatewayHealthStatus::Unknown;
        health.message = "Gateway is disabled in configuration.";
        return health;
    }
    if (localState != GatewayState::Running) {
        health.status = GatewayHealthStatus::Unknown;
        health.message = "Gateway is not in Running state; skipping probe.";
        return health;
    }

#if defined(_WIN32)
    HINTERNET session = WinHttpOpen(
        L"MasterControlOrchestrationServer/Gateway-Probe",
        WINHTTP_ACCESS_TYPE_NO_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);
    if (session == nullptr) {
        health.status = GatewayHealthStatus::Unhealthy;
        health.message = "WinHttpOpen failed.";
        return health;
    }
    WinHttpSetTimeouts(session, 500, 500, 500, 1500);

    HINTERNET connection = WinHttpConnect(session, widen(host).c_str(), port, 0);
    if (connection == nullptr) {
        WinHttpCloseHandle(session);
        health.status = GatewayHealthStatus::Unhealthy;
        health.message = "WinHttpConnect failed (gateway port unreachable).";
        return health;
    }

    HINTERNET request = WinHttpOpenRequest(
        connection,
        L"GET",
        widen(healthPath).c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        0);
    if (request == nullptr) {
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        health.status = GatewayHealthStatus::Unhealthy;
        health.message = "WinHttpOpenRequest failed.";
        return health;
    }

    BOOL sent = WinHttpSendRequest(
        request,
        WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        WINHTTP_NO_REQUEST_DATA, 0,
        0,
        0);
    BOOL received = sent ? WinHttpReceiveResponse(request, nullptr) : FALSE;
    if (!received) {
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        health.status = GatewayHealthStatus::Unhealthy;
        health.reachable = false;
        health.message = "Gateway port did not respond to /health probe.";
        return health;
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

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);

    health.reachable = true;
    health.httpStatusCode = static_cast<int>(statusCode);
    if (statusCode == 200) {
        health.status = GatewayHealthStatus::Healthy;
        health.message = "Gateway health endpoint returned 200.";
    } else {
        health.status = GatewayHealthStatus::Degraded;
        health.message = "Gateway responded with non-200 status code.";
    }
    return health;
#else
    health.status = GatewayHealthStatus::Unknown;
    health.message = "Health probe is Windows-only in PHASE-02.";
    return health;
#endif
}

RegistrationResult McpJungleGatewayAdapter::registerInternal(McpServerRegistration server) {
    RegistrationResult result;
    result.serverName = server.name;
    if (server.name.empty()) {
        result.succeeded = false;
        result.message = "Logical server name is required.";
        return result;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    registry_[server.name] = std::move(server);
    result.succeeded = true;
    result.registeredAtUtc = timestampNowUtc();
    result.message = "Logical server registered with the gateway adapter (in-memory). Adapter will reconcile with MCPJungle on next Start().";
    return result;
}

RegistrationResult McpJungleGatewayAdapter::RegisterHttpServer(const McpServerRegistration& server) {
    McpServerRegistration copy = server;
    copy.transport = McpServerTransport::StreamableHttp;
    return registerInternal(std::move(copy));
}

RegistrationResult McpJungleGatewayAdapter::RegisterStdioServer(const McpServerRegistration& server) {
    McpServerRegistration copy = server;
    copy.transport = McpServerTransport::Stdio;
    return registerInternal(std::move(copy));
}

DeregistrationResult McpJungleGatewayAdapter::DeregisterServer(const std::string& serverName) {
    DeregistrationResult result;
    result.serverName = serverName;

    std::lock_guard<std::mutex> lock(mutex_);
    const auto erased = registry_.erase(serverName);
    result.succeeded = erased > 0;
    result.message = erased > 0
        ? "Logical server deregistered from gateway adapter."
        : "No logical server with that name was registered.";
    return result;
}

std::vector<McpToolDescriptor> McpJungleGatewayAdapter::ListTools() const {
    // PHASE-02 spike: returning an empty list until PHASE-06 wires real
    // pool-managed backends. Reporting fabricated tools would violate
    // ADR-002 §9 ("honest telemetry only"). Once a real MCPJungle binary
    // is installed and reachable, a future iteration will hydrate this
    // list from the gateway's `/list_tools` endpoint.
    return {};
}

std::string McpJungleGatewayAdapter::GatewayMcpUrl() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return composeMcpUrl(configuration_);
}

std::string McpJungleGatewayAdapter::AdapterType() const {
    return "mcpjungle";
}

McpGatewayConfiguration McpJungleGatewayAdapter::configuration() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return configuration_;
}

bool McpJungleGatewayAdapter::isSupervisingChildProcess() const {
    return childProcessActive_.load();
}

void McpJungleGatewayAdapter::terminateChildProcessTreeIfRunning() {
#if defined(_WIN32)
    if (jobObject_ != nullptr) {
        // Closing the Job Object terminates the child tree because
        // JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE is set.
        CloseHandle(jobObject_);
        jobObject_ = nullptr;
    }
    if (processInfo_.hProcess != nullptr) {
        CloseHandle(processInfo_.hProcess);
    }
    if (processInfo_.hThread != nullptr) {
        CloseHandle(processInfo_.hThread);
    }
    processInfo_ = {};
#endif
    childProcessActive_ = false;
}

// ---------------------------------------------------------------------------
// Honest "service unavailable" placeholder listener (Option D from the
// PHASE-11 / PHASE-12 evaluation). Binds the configured gateway port so a
// LAN AI client pointing at http://<host>:8080/mcp gets a structured 503
// JSON instead of TCP RST. Replaced wholesale by PHASE-12's native gateway
// when that lands.
// ---------------------------------------------------------------------------

void McpJungleGatewayAdapter::startHonestUnavailableListenerLocked() {
#if defined(_WIN32)
    if (honestListenerRunning_) {
        return;
    }
    if (configuration_.listenPort == 0) {
        return;
    }

    // Initialize Winsock — safe to call repeatedly; refcounted internally.
    WSADATA wsaData{};
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) {
        return;
    }

    int reuse = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(configuration_.listenPort);
    const std::string& host = configuration_.listenHost.empty()
        ? std::string("0.0.0.0")
        : configuration_.listenHost;
    if (host == "0.0.0.0") {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        inet_pton(AF_INET, host.c_str(), &addr.sin_addr);
    }

    if (bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        // Port already in use (often a lingering MCPJungle from a previous
        // run, or another process owns it). Don't escalate -- the gateway
        // adapter's own state surfaces in /api/gateway/status, and the
        // caller will see "no listener" the same way they did pre-v0.6.7.
        closesocket(listener);
        return;
    }
    if (listen(listener, 8) == SOCKET_ERROR) {
        closesocket(listener);
        return;
    }

    honestListenerSocket_ = listener;
    honestListenerRunning_ = true;
    honestListenerThread_ = std::thread(&McpJungleGatewayAdapter::honestUnavailableServeLoop, this);
#endif
}

void McpJungleGatewayAdapter::stopHonestUnavailableListenerLocked() {
#if defined(_WIN32)
    if (!honestListenerRunning_) {
        return;
    }
    honestListenerRunning_ = false;
    if (honestListenerSocket_ != INVALID_SOCKET) {
        // Closing the listening socket triggers WSAEINTR / WSAENOTSOCK in
        // the accept() blocked in honestUnavailableServeLoop, which causes
        // the loop to exit promptly.
        closesocket(honestListenerSocket_);
        honestListenerSocket_ = INVALID_SOCKET;
    }
    if (honestListenerThread_.joinable()) {
        honestListenerThread_.join();
    }
#endif
}

#if defined(_WIN32)
void McpJungleGatewayAdapter::honestUnavailableServeLoop() {
    // Snapshot listener socket once -- mutex is held at start and we
    // observe stop-flag without re-locking on the hot path.
    SOCKET listener = honestListenerSocket_;
    while (honestListenerRunning_ && listener != INVALID_SOCKET) {
        sockaddr_in clientAddr{};
        int clientLen = sizeof(clientAddr);
        SOCKET client = accept(listener, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);
        if (client == INVALID_SOCKET) {
            // Either we were stopped (stop flag clear), or a transient
            // error. Either way, exit the loop -- caller already cleaned
            // up on the stop path.
            break;
        }

        // Drain whatever the client sent (we don't actually parse the
        // request -- every method/path gets the same 503). 4 KB is
        // generous for a request line + headers.
        char drain[4096];
        recv(client, drain, sizeof(drain), 0);

        // Compose a structured JSON response so dashboards / debug tools
        // hitting this port see something useful.
        const std::string body =
            "{"
            "\"error\":\"MCP gateway not configured\","
            "\"adapterState\":\"disabled-or-supervised-mock\","
            "\"adapterType\":\"mcpjungle\","
            "\"guidance\":\"This MCOS instance has no live MCP gateway substrate "
            "behind the advertised mcpGateway.listenPort. Either configure "
            "mcpGateway.binaryPath to point at an MCPJungle binary and set "
            "mcpGateway.enabled=true, or wait for PHASE-12's native HTTP.sys "
            "gateway. See /api/gateway/status on the operator port for full "
            "diagnostic state.\","
            "\"hint\":\"GET http://<host>:7300/api/gateway/status\""
            "}";

        std::ostringstream stream;
        stream << "HTTP/1.1 503 Service Unavailable\r\n"
               << "Content-Type: application/json\r\n"
               << "Content-Length: " << body.size() << "\r\n"
               << "Cache-Control: no-store\r\n"
               << "Connection: close\r\n\r\n"
               << body;
        const auto raw = stream.str();
        send(client, raw.c_str(), static_cast<int>(raw.size()), 0);
        shutdown(client, SD_BOTH);
        closesocket(client);
    }
}
#endif

// ---------------------------------------------------------------------------
// FakeMcpGatewayAdapter
// ---------------------------------------------------------------------------

FakeMcpGatewayAdapter::FakeMcpGatewayAdapter(McpGatewayConfiguration configuration)
    : configuration_(std::move(configuration)) {
    status_.adapterType = AdapterType();
    status_.mcpUrl = composeMcpUrl(configuration_);
    status_.state = configuration_.enabled
        ? GatewayState::Configured
        : GatewayState::Disabled;
    status_.message = "FakeMcpGatewayAdapter constructed.";

    // Default probe: Unknown until a test scripts otherwise.
    nextProbe_.adapterType = AdapterType();
    nextProbe_.status = GatewayHealthStatus::Unknown;
    nextProbe_.message = "FakeMcpGatewayAdapter has no scripted probe; returning Unknown.";
}

GatewayStatus FakeMcpGatewayAdapter::Start() {
    std::lock_guard<std::mutex> lock(mutex_);
    ++startCalls_;
    if (startShouldFail_) {
        status_.state = GatewayState::Failed;
        status_.message = startFailureMessage_.empty()
            ? std::string("Fake gateway scripted Start() to fail.")
            : startFailureMessage_;
        return status_;
    }
    if (!configuration_.enabled) {
        status_.state = GatewayState::Disabled;
        status_.message = "Fake gateway is disabled in configuration.";
        return status_;
    }
    status_.state = GatewayState::Running;
    status_.startedAtUtc = timestampNowUtc();
    status_.message = "Fake gateway started.";
    return status_;
}

GatewayStatus FakeMcpGatewayAdapter::Stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    ++stopCalls_;
    status_.state = GatewayState::Stopped;
    status_.startedAtUtc.clear();
    status_.message = "Fake gateway stopped.";
    return status_;
}

GatewayStatus FakeMcpGatewayAdapter::CurrentStatus() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}

GatewayHealth FakeMcpGatewayAdapter::Probe() {
    std::lock_guard<std::mutex> lock(mutex_);
    ++probeCalls_;
    GatewayHealth health = nextProbe_;
    health.adapterType = AdapterType();
    health.mcpUrl = composeMcpUrl(configuration_);
    health.healthUrl = composeHealthUrl(configuration_);
    health.registeredServerCount = static_cast<int>(registry_.size());
    health.probedAtUtc = timestampNowUtc();
    return health;
}

RegistrationResult FakeMcpGatewayAdapter::RegisterHttpServer(const McpServerRegistration& server) {
    RegistrationResult result;
    result.serverName = server.name;
    if (server.name.empty()) {
        result.succeeded = false;
        result.message = "Logical server name is required.";
        return result;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    McpServerRegistration copy = server;
    copy.transport = McpServerTransport::StreamableHttp;
    registry_[copy.name] = std::move(copy);
    result.succeeded = true;
    result.registeredAtUtc = timestampNowUtc();
    result.message = "Registered (fake).";
    return result;
}

RegistrationResult FakeMcpGatewayAdapter::RegisterStdioServer(const McpServerRegistration& server) {
    RegistrationResult result;
    result.serverName = server.name;
    if (server.name.empty()) {
        result.succeeded = false;
        result.message = "Logical server name is required.";
        return result;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    McpServerRegistration copy = server;
    copy.transport = McpServerTransport::Stdio;
    registry_[copy.name] = std::move(copy);
    result.succeeded = true;
    result.registeredAtUtc = timestampNowUtc();
    result.message = "Registered (fake stdio).";
    return result;
}

DeregistrationResult FakeMcpGatewayAdapter::DeregisterServer(const std::string& serverName) {
    DeregistrationResult result;
    result.serverName = serverName;
    std::lock_guard<std::mutex> lock(mutex_);
    const auto erased = registry_.erase(serverName);
    result.succeeded = erased > 0;
    result.message = erased > 0 ? "Deregistered (fake)." : "No registration with that name.";
    return result;
}

std::vector<McpToolDescriptor> FakeMcpGatewayAdapter::ListTools() const {
    // FakeMcpGatewayAdapter does not invent tools. Tests that need to
    // simulate tool listings should subclass and override.
    return {};
}

std::string FakeMcpGatewayAdapter::GatewayMcpUrl() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return composeMcpUrl(configuration_);
}

std::string FakeMcpGatewayAdapter::AdapterType() const {
    return "fake";
}

void FakeMcpGatewayAdapter::setNextProbe(GatewayHealth probe) {
    std::lock_guard<std::mutex> lock(mutex_);
    nextProbe_ = std::move(probe);
}

void FakeMcpGatewayAdapter::setStartShouldFail(bool shouldFail, const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    startShouldFail_ = shouldFail;
    startFailureMessage_ = message;
}

std::size_t FakeMcpGatewayAdapter::startCallCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return startCalls_;
}

std::size_t FakeMcpGatewayAdapter::stopCallCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stopCalls_;
}

std::size_t FakeMcpGatewayAdapter::probeCallCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return probeCalls_;
}

std::vector<std::string> FakeMcpGatewayAdapter::registeredServerNames() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> names;
    names.reserve(registry_.size());
    for (const auto& [name, _] : registry_) {
        names.push_back(name);
    }
    return names;
}

// trimTrailingSlash is currently unused but retained as a helper for future
// adapters that need to compose URL bases without trailing slashes.
[[maybe_unused]] static std::string consumeTrimTrailingSlash(std::string value) {
    return trimTrailingSlash(std::move(value));
}

} // namespace MasterControl
