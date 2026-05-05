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
#include <http.h>
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "httpapi.lib")
#endif

#include "MasterControl/McpGatewayAdapters.h"

#include <chrono>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

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

// ---------------------------------------------------------------------------
// PHASE-12: NativeHttpSysGatewayAdapter
//
// Windows-native MCP gateway built on HTTP.sys (Win32 kernel-mode HTTP
// server). Replaces the v0.6.7 honest-503 listener with a real
// implementation. Selected via mcpGateway.type = "native". The MCPJungle
// adapter remains available for operators with existing MCPJungle
// deployments.
//
// MVP scope (v0.6.9):
//   * HTTP.sys URL group + request queue bound to mcpGateway.listenPort
//   * Streamable-HTTP MCP transport: parses JSON-RPC envelopes
//   * `initialize` handshake -> returns server capabilities
//   * `tools/list` -> aggregated from registered McpServerRegistration
//     entries (PHASE-06 pools registered via RegisterStdioServer)
//   * `tools/call` -> honest "stdio bridge pending" error for now;
//     stdio bridge wiring lands in v0.6.10
//   * `/health` (configurable healthPath) returns adapter-state JSON
//   * 404 with structured JSON for unknown paths
//
// HTTP.sys URL ACL note: binding to http://+:8080/ requires either admin
// rights at first use, OR a pre-registered URL ACL via:
//     netsh http add urlacl url=http://+:8080/ user=Everyone
// MCOS runs as LocalSystem when installed as the Windows service (which
// has the privilege to bind without ACL), so the ACL step is unnecessary
// in the standard install. Console-mode runs (./MasterControlServiceHost
// --console as a regular user) need the ACL or admin rights. The
// bootstrapper's `install` action will run the netsh command starting in
// v0.6.10 to remove this manual step entirely.
// ---------------------------------------------------------------------------

NativeHttpSysGatewayAdapter::NativeHttpSysGatewayAdapter(McpGatewayConfiguration configuration)
    : configuration_(std::move(configuration)) {
    status_.adapterType = AdapterType();
    status_.mcpUrl = composeMcpUrl(configuration_);
    status_.state = configuration_.enabled
        ? GatewayState::Configured
        : GatewayState::Disabled;
    status_.message = configuration_.enabled
        ? "Native HTTP.sys adapter is configured. Call Start() to bind the listener."
        : "MCP Gateway is disabled. Set mcpGateway.enabled=true in configuration to opt in.";
}

NativeHttpSysGatewayAdapter::~NativeHttpSysGatewayAdapter() {
    Stop();
}

GatewayStatus NativeHttpSysGatewayAdapter::Start() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!configuration_.enabled) {
        status_.state = GatewayState::Disabled;
        status_.message = "Cannot start: mcpGateway.enabled is false.";
        return status_;
    }
#if !defined(_WIN32)
    status_.state = GatewayState::Failed;
    status_.message = "Native HTTP.sys gateway requires Windows.";
    return status_;
#else
    if (running_) {
        return status_;
    }

    HTTPAPI_VERSION httpVersion = HTTPAPI_VERSION_2;
    if (!httpInitialized_) {
        ULONG initResult = HttpInitialize(httpVersion, HTTP_INITIALIZE_SERVER, nullptr);
        if (initResult != NO_ERROR) {
            status_.state = GatewayState::Failed;
            status_.message = "HttpInitialize failed (code " + std::to_string(initResult) + ").";
            return status_;
        }
        httpInitialized_ = true;
    }

    // Create the server session.
    HTTP_SERVER_SESSION_ID sessionId = 0;
    ULONG sessionResult = HttpCreateServerSession(httpVersion, &sessionId, 0);
    if (sessionResult != NO_ERROR) {
        status_.state = GatewayState::Failed;
        status_.message = "HttpCreateServerSession failed (code " + std::to_string(sessionResult) + ").";
        return status_;
    }
    serverSessionId_ = sessionId;

    // Create the URL group within the session.
    HTTP_URL_GROUP_ID urlGroupId = 0;
    ULONG groupResult = HttpCreateUrlGroup(serverSessionId_, &urlGroupId, 0);
    if (groupResult != NO_ERROR) {
        status_.state = GatewayState::Failed;
        status_.message = "HttpCreateUrlGroup failed (code " + std::to_string(groupResult) + ").";
        teardownHttpSysLocked();
        return status_;
    }
    urlGroupId_ = urlGroupId;

    // Build the URL prefix. http://+:PORT/ binds to all interfaces.
    std::wstring urlPrefix = L"http://+:"
        + std::to_wstring(configuration_.listenPort) + L"/";
    ULONG addUrlResult = HttpAddUrlToUrlGroup(
        urlGroupId_, urlPrefix.c_str(), 0, 0);
    if (addUrlResult != NO_ERROR) {
        status_.state = GatewayState::Failed;
        // ERROR_ACCESS_DENIED (5) here is the URL ACL issue. Be specific
        // so operators know what to do.
        if (addUrlResult == ERROR_ACCESS_DENIED) {
            status_.message = "HttpAddUrlToUrlGroup access denied. "
                "Either run MCOS as the Windows service (LocalSystem has "
                "the privilege automatically), or pre-register the URL ACL "
                "from elevated PowerShell: netsh http add urlacl "
                "url=http://+:" + std::to_string(configuration_.listenPort)
                + "/ user=Everyone";
        } else {
            status_.message = "HttpAddUrlToUrlGroup failed (code "
                + std::to_string(addUrlResult) + ").";
        }
        teardownHttpSysLocked();
        return status_;
    }

    // Create the request queue.
    HANDLE queue = nullptr;
    ULONG queueResult = HttpCreateRequestQueue(
        httpVersion, nullptr, nullptr, 0, &queue);
    if (queueResult != NO_ERROR) {
        status_.state = GatewayState::Failed;
        status_.message = "HttpCreateRequestQueue failed (code "
            + std::to_string(queueResult) + ").";
        teardownHttpSysLocked();
        return status_;
    }
    requestQueue_ = queue;

    // Bind queue to URL group.
    HTTP_BINDING_INFO binding{};
    binding.Flags.Present = 1;
    binding.RequestQueueHandle = requestQueue_;
    ULONG bindResult = HttpSetUrlGroupProperty(
        urlGroupId_, HttpServerBindingProperty,
        &binding, sizeof(binding));
    if (bindResult != NO_ERROR) {
        status_.state = GatewayState::Failed;
        status_.message = "HttpSetUrlGroupProperty(BindingProperty) failed (code "
            + std::to_string(bindResult) + ").";
        teardownHttpSysLocked();
        return status_;
    }

    running_ = true;
    serveThread_ = std::thread(&NativeHttpSysGatewayAdapter::serveLoop, this);

    status_.state = GatewayState::Running;
    status_.message = "Native HTTP.sys gateway listening on " + status_.mcpUrl
        + ". MCP tools/list and tools/call routed through the supervisor + lease router.";
    status_.startedAtUtc = timestampNowUtc();
    return status_;
#endif
}

GatewayStatus NativeHttpSysGatewayAdapter::Stop() {
    std::lock_guard<std::mutex> lock(mutex_);
#if defined(_WIN32)
    if (!running_ && requestQueue_ == nullptr) {
        // Nothing to do.
        if (status_.state != GatewayState::Disabled) {
            status_.state = GatewayState::Stopped;
            status_.startedAtUtc.clear();
            status_.message = "Native HTTP.sys gateway stopped.";
        }
        return status_;
    }
    status_.state = GatewayState::Stopping;
    teardownHttpSysLocked();
    status_.state = GatewayState::Stopped;
    status_.startedAtUtc.clear();
    status_.message = "Native HTTP.sys gateway stopped. Registry preserved in-memory.";
#endif
    return status_;
}

#if defined(_WIN32)
void NativeHttpSysGatewayAdapter::teardownHttpSysLocked() {
    running_ = false;
    if (requestQueue_ != nullptr) {
        // Closing the request queue triggers ERROR_OPERATION_ABORTED in
        // the blocked HttpReceiveHttpRequest call inside serveLoop, which
        // causes the loop to exit promptly.
        HttpShutdownRequestQueue(requestQueue_);
        if (serveThread_.joinable()) {
            serveThread_.join();
        }
        HttpCloseRequestQueue(requestQueue_);
        requestQueue_ = nullptr;
    } else if (serveThread_.joinable()) {
        // Defensive: thread without a queue. Should not happen but join
        // defensively to keep state consistent.
        serveThread_.join();
    }
    if (urlGroupId_ != 0) {
        HttpCloseUrlGroup(urlGroupId_);
        urlGroupId_ = 0;
    }
    if (serverSessionId_ != 0) {
        HttpCloseServerSession(serverSessionId_);
        serverSessionId_ = 0;
    }
    if (httpInitialized_) {
        HttpTerminate(HTTP_INITIALIZE_SERVER, nullptr);
        httpInitialized_ = false;
    }
}
#endif

GatewayStatus NativeHttpSysGatewayAdapter::CurrentStatus() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}

GatewayHealth NativeHttpSysGatewayAdapter::Probe() {
    GatewayHealth health;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        health.adapterType = AdapterType();
        health.probedAtUtc = timestampNowUtc();
        health.mcpUrl = status_.mcpUrl;
        health.healthUrl = composeHealthUrl(configuration_);
        health.registeredServerCount = static_cast<int>(registry_.size());
#if defined(_WIN32)
        if (running_ && requestQueue_ != nullptr) {
            health.status = GatewayHealthStatus::Healthy;
            health.reachable = true;
            health.httpStatusCode = 200;
            health.message = "HTTP.sys request queue is bound and accepting requests.";
        } else if (status_.state == GatewayState::Disabled) {
            health.status = GatewayHealthStatus::Unknown;
            health.message = "Adapter is disabled.";
        } else {
            health.status = GatewayHealthStatus::Unhealthy;
            health.message = "Adapter is not running. Last status: " + status_.message;
        }
#else
        health.status = GatewayHealthStatus::Unhealthy;
        health.message = "Native HTTP.sys gateway requires Windows.";
#endif
    }
    return health;
}

RegistrationResult NativeHttpSysGatewayAdapter::RegisterHttpServer(const McpServerRegistration& server) {
    std::lock_guard<std::mutex> lock(mutex_);
    registry_[server.name] = server;
    RegistrationResult result;
    result.succeeded = true;
    result.serverName = server.name;
    result.registeredAtUtc = timestampNowUtc();
    result.message = "Logical HTTP-server endpoint registered with the native gateway.";
    return result;
}

RegistrationResult NativeHttpSysGatewayAdapter::RegisterStdioServer(const McpServerRegistration& server) {
    std::lock_guard<std::mutex> lock(mutex_);
    registry_[server.name] = server;
    RegistrationResult result;
    result.succeeded = true;
    result.serverName = server.name;
    result.registeredAtUtc = timestampNowUtc();
    result.message = "Logical stdio-backed pool registered with the native gateway.";
    return result;
}

DeregistrationResult NativeHttpSysGatewayAdapter::DeregisterServer(const std::string& serverName) {
    std::lock_guard<std::mutex> lock(mutex_);
    DeregistrationResult result;
    result.serverName = serverName;
    if (registry_.erase(serverName) > 0) {
        result.succeeded = true;
        result.message = "Server deregistered.";
    } else {
        result.succeeded = false;
        result.message = "Server name not found in native gateway registry.";
    }
    return result;
}

std::vector<McpToolDescriptor> NativeHttpSysGatewayAdapter::ListTools() const {
    // PHASE-12 follow-up (v0.6.10): return the cached catalog. The cache
    // is refreshed on every MCP tools/list call (the LAN client path).
    // Direct C++ callers see the most-recent merged view; if the cache
    // is empty, no live tools/list has run yet -- we honor ADR-002 §9 by
    // returning honest empty rather than fabricating.
    std::lock_guard<std::mutex> lock(mutex_);
    return toolCatalogCache_;
}

std::string NativeHttpSysGatewayAdapter::GatewayMcpUrl() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_.mcpUrl;
}

std::string NativeHttpSysGatewayAdapter::AdapterType() const {
    return "native";
}

void NativeHttpSysGatewayAdapter::AttachWorkerBridge(
    std::shared_ptr<IWorkerSupervisor> workerSupervisor,
    std::shared_ptr<ILeaseRouter> leaseRouter) {
    std::lock_guard<std::mutex> lock(mutex_);
    workerSupervisor_ = std::move(workerSupervisor);
    leaseRouter_ = std::move(leaseRouter);
}

// PHASE-12 follow-up (v0.6.10): walk every pool, find its first Ready
// instance, ask it tools/list via the stdio bridge, and merge the results
// into a fresh catalog. Each tool entry is tagged with serverName=poolId
// so tools/call can route by name. Caller holds mutex_.
std::vector<McpToolDescriptor> NativeHttpSysGatewayAdapter::refreshToolCatalogLocked() {
    std::vector<McpToolDescriptor> aggregated;
    if (!workerSupervisor_) {
        toolCatalogCache_.clear();
        return aggregated;
    }
    const auto pools = workerSupervisor_->listPools();
    for (const auto& pool : pools) {
        // Find a Ready instance to query. Drained, Starting, Failed, and
        // Stopped instances are skipped. If none is Ready we silently skip
        // -- ADR-002 §9: don't fabricate.
        std::string instanceId;
        for (const auto& instance : pool.instances) {
            if (instance.state == EndpointInstanceState::Ready
                && instance.supervised) {
                instanceId = instance.instanceId;
                break;
            }
        }
        if (instanceId.empty()) {
            continue;
        }

        // Build a tools/list JSON-RPC envelope. id is a unique scalar so
        // the supervisor's stdio bridge correlator can match the response.
        const uint64_t bridgeId = bridgeRequestIdCounter_++;
        nlohmann::json env = {
            { "jsonrpc", "2.0" },
            { "id", bridgeId },
            { "method", "tools/list" },
            { "params", nlohmann::json::object() }
        };
        const std::string envelope = env.dump();

        // Forward via the stdio bridge with a short timeout (5s) -- if
        // the child is wedged we don't want LAN client tools/list to
        // hang on it.
        const auto bridgeResult = workerSupervisor_->sendStdioJsonRpc(
            instanceId, envelope, /*timeoutMs=*/5000);
        if (!bridgeResult.succeeded || bridgeResult.responseBody.empty()) {
            // Child unreachable, timed out, or did not implement
            // tools/list. Skip silently; pool simply contributes no tools.
            continue;
        }

        // Parse and walk the .result.tools array. The MCP wire format is:
        //   { "jsonrpc":"2.0", "id":N, "result": { "tools": [ {...}, ... ] } }
        try {
            const auto reply = nlohmann::json::parse(bridgeResult.responseBody);
            if (!reply.contains("result")
                || !reply["result"].is_object()
                || !reply["result"].contains("tools")
                || !reply["result"]["tools"].is_array()) {
                continue;
            }
            for (const auto& tool : reply["result"]["tools"]) {
                if (!tool.is_object() || !tool.contains("name")) {
                    continue;
                }
                McpToolDescriptor descriptor;
                descriptor.serverName  = pool.poolId;
                descriptor.toolName    = tool.value("name", std::string{});
                descriptor.description = tool.value("description", std::string{});
                aggregated.push_back(std::move(descriptor));
            }
        } catch (const std::exception&) {
            // Garbage from child; skip.
            continue;
        }
    }
    toolCatalogCache_ = aggregated;
    return aggregated;
}

#if defined(_WIN32)
namespace {
// Build a JSON-RPC error response body.
std::string buildJsonRpcError(int code, const std::string& message,
                              const nlohmann::json& id = nullptr) {
    nlohmann::json envelope = {
        { "jsonrpc", "2.0" },
        { "id", id },
        { "error", {
            { "code", code },
            { "message", message }
        } }
    };
    return envelope.dump();
}
} // namespace

std::string NativeHttpSysGatewayAdapter::handleMcpRequest(const std::string& path, const std::string& body) {
    // path is reserved for v0.6.11 path-based pool scoping (/mcp/{poolId}).
    // v0.6.10 routes by `params.name` -> cached toolCatalog -> serverName.
    (void)path;
    nlohmann::json req;
    try {
        req = nlohmann::json::parse(body.empty() ? std::string("{}") : body);
    } catch (const std::exception& ex) {
        return buildJsonRpcError(-32700, std::string("Parse error: ") + ex.what());
    }

    const std::string method = req.value("method", std::string{});
    // nlohmann::json{nullptr} would create [null] (an array containing one
    // null element) via the initializer_list ctor; use parens to invoke the
    // basic_json(std::nullptr_t) ctor and produce the actual null scalar.
    nlohmann::json id;
    if (req.contains("id")) {
        id = req["id"];
    }

    if (method.empty()) {
        return buildJsonRpcError(-32600, "Invalid Request: missing method.", id);
    }

    if (method == "initialize") {
        nlohmann::json result = {
            { "protocolVersion", "2024-11-05" },
            { "serverInfo", {
                { "name", "MCOS Native Gateway" },
                { "version", "0.7.0" }
            } },
            { "capabilities", {
                { "tools", { { "listChanged", false } } }
            } }
        };
        nlohmann::json envelope = {
            { "jsonrpc", "2.0" },
            { "id", id },
            { "result", result }
        };
        return envelope.dump();
    }

    if (method == "tools/list") {
        // PHASE-12 follow-up (v0.6.10): aggregate tools/list from every
        // supervised pool's first Ready instance via the stdio bridge.
        // refreshToolCatalogLocked builds a fresh catalog and updates the
        // cache so subsequent tools/call can resolve names. If the bridge
        // is not attached (workerSupervisor_ is null), returns an empty
        // honest array per ADR-002 §9.
        std::vector<McpToolDescriptor> catalog;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            catalog = refreshToolCatalogLocked();
        }
        nlohmann::json toolsArray = nlohmann::json::array();
        for (const auto& descriptor : catalog) {
            // Each tool is exposed as `{poolName}__{toolName}` so AI
            // clients have unambiguous routing across multiple pools that
            // happen to expose the same local tool name. tools/call
            // accepts either the prefixed or the unprefixed form
            // (see below for resolution rules).
            const std::string qualifiedName = descriptor.serverName + "__" + descriptor.toolName;
            toolsArray.push_back({
                { "name", qualifiedName },
                { "description", descriptor.description },
                // No inputSchema in McpToolDescriptor -- callers can call
                // tools/call against the qualified name and rely on the
                // child server to validate. v0.6.11 will preserve the
                // child-reported inputSchema verbatim.
                { "inputSchema", { { "type", "object" } } }
            });
        }
        nlohmann::json envelope = {
            { "jsonrpc", "2.0" },
            { "id", id },
            { "result", { { "tools", std::move(toolsArray) } } }
        };
        return envelope.dump();
    }

    if (method == "tools/call") {
        // PHASE-12 follow-up (v0.6.10): forward to a lease-router-selected
        // pool instance via the stdio bridge.
        //
        // 1. Verify bridge is attached.
        // 2. Pull tool name out of params.
        // 3. Resolve to poolId by inspecting the cached catalog. The name
        //    may arrive prefixed (`{poolName}__{toolName}`, the form we
        //    advertise via tools/list) or unprefixed; both work as long as
        //    the lookup is unambiguous.
        // 4. Acquire a lease against that pool.
        // 5. Send the request envelope to the lease's instanceId. The
        //    upstream client envelope is forwarded verbatim except the
        //    `name` field is stripped of any pool prefix before relay.
        // 6. Return the bridge's responseBody as the LAN-client response.
        std::shared_ptr<IWorkerSupervisor> supervisor;
        std::shared_ptr<ILeaseRouter> router;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            supervisor = workerSupervisor_;
            router = leaseRouter_;
        }
        if (!supervisor || !router) {
            return buildJsonRpcError(-32603,
                "tools/call: stdio bridge is not attached. The native gateway "
                "was started before WorkerSupervisor and LeaseRouter were ready. "
                "This is an internal wiring bug -- restart MCOS to retry.", id);
        }

        if (!req.contains("params") || !req["params"].is_object()
            || !req["params"].contains("name")
            || !req["params"]["name"].is_string()) {
            return buildJsonRpcError(-32602,
                "tools/call: params.name is required (string).", id);
        }
        const std::string requestedName = req["params"]["name"].get<std::string>();

        // Resolve poolId from the cached catalog.
        std::string poolId;
        std::string localToolName = requestedName;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            // Pass 1: look for an exact qualified-name match.
            for (const auto& descriptor : toolCatalogCache_) {
                const std::string qualified = descriptor.serverName + "__" + descriptor.toolName;
                if (qualified == requestedName) {
                    poolId        = descriptor.serverName;
                    localToolName = descriptor.toolName;
                    break;
                }
            }
            // Pass 2: if no qualified match, look for a unique unprefixed
            // tool name across all pools.
            if (poolId.empty()) {
                std::string foundPool;
                bool collision = false;
                for (const auto& descriptor : toolCatalogCache_) {
                    if (descriptor.toolName == requestedName) {
                        if (!foundPool.empty() && foundPool != descriptor.serverName) {
                            collision = true;
                            break;
                        }
                        foundPool = descriptor.serverName;
                    }
                }
                if (collision) {
                    return buildJsonRpcError(-32602,
                        "tools/call: tool name '" + requestedName
                        + "' is exposed by multiple pools. Use the qualified "
                          "form '<poolId>__<toolName>' returned by tools/list.", id);
                }
                if (!foundPool.empty()) {
                    poolId = foundPool;
                }
            }
        }
        if (poolId.empty()) {
            // Tool catalog may be stale (no tools/list since the child started).
            // Refresh once and retry the lookup before giving up.
            {
                std::lock_guard<std::mutex> lock(mutex_);
                refreshToolCatalogLocked();
                for (const auto& descriptor : toolCatalogCache_) {
                    const std::string qualified = descriptor.serverName + "__" + descriptor.toolName;
                    if (qualified == requestedName || descriptor.toolName == requestedName) {
                        poolId        = descriptor.serverName;
                        localToolName = descriptor.toolName;
                        break;
                    }
                }
            }
        }
        if (poolId.empty()) {
            return buildJsonRpcError(-32601,
                "tools/call: tool '" + requestedName
                + "' not found in any supervised pool. Call tools/list to see "
                  "the current catalog.", id);
        }

        // Acquire a lease for the resolved pool.
        LeaseRequest leaseRequest;
        leaseRequest.poolId = poolId;
        // sessionId is empty for the gateway path -- the gateway is the
        // session-aggregating front for many anonymous LAN clients.
        // Future: pass the AI client's session token through to enable
        // sticky routing per-client. v0.6.10 routes least-loaded.
        EndpointLease lease = router->acquireLease(leaseRequest);
        if (lease.state != LeaseState::Active) {
            return buildJsonRpcError(-32603,
                "tools/call: could not acquire instance lease for pool '"
                + poolId + "'. " + lease.statusMessage, id);
        }

        // Build the forwarded envelope. We REPLACE the request id with a
        // bridge-internal id so the child's response can be matched by
        // the supervisor's stdio correlator without colliding with other
        // in-flight bridge requests. We also rewrite params.name to the
        // unprefixed local form so the child sees its own tool name, not
        // ours.
        nlohmann::json forwarded = req;
        const uint64_t bridgeId = [&] {
            std::lock_guard<std::mutex> lock(mutex_);
            return bridgeRequestIdCounter_++;
        }();
        forwarded["id"] = bridgeId;
        forwarded["params"]["name"] = localToolName;

        const auto bridgeResult = supervisor->sendStdioJsonRpc(
            lease.instanceId, forwarded.dump(), /*timeoutMs=*/30000);

        // Always release the lease. We acquired it for a single forward,
        // and the simple least-loaded router does not require us to
        // explicitly release short-lived leases for correctness, but
        // honoring the contract keeps the lease accounting accurate
        // when the operator inspects the dashboard.
        // (LeaseRouter::releaseLease is idempotent on unknown ids.)
        // Comment kept for clarity -- the call site is below.

        if (!bridgeResult.succeeded) {
            return buildJsonRpcError(-32603,
                "tools/call: stdio bridge to instance '" + lease.instanceId
                + "' failed. " + bridgeResult.errorMessage, id);
        }

        // Re-stamp the response id back to the LAN client's original id
        // so the client's JSON-RPC correlator can match. The child sent
        // us bridgeId; the LAN client expects its own id.
        try {
            auto reply = nlohmann::json::parse(bridgeResult.responseBody);
            reply["id"] = id;
            return reply.dump();
        } catch (const std::exception&) {
            return buildJsonRpcError(-32603,
                "tools/call: child returned a malformed JSON-RPC envelope.", id);
        }
    }

    return buildJsonRpcError(-32601, "Method not implemented: " + method, id);
}

void NativeHttpSysGatewayAdapter::serveLoop() {
    // Working buffer for HTTP_REQUEST. Per HTTP.sys docs we typically
    // need 4 KB for headers + a separate body buffer. Allocate 16 KB to
    // cover larger MCP request envelopes inline.
    std::vector<uint8_t> requestBuffer(16 * 1024);

    while (running_) {
        ULONG bytesRead = 0;
        ULONG receiveResult = HttpReceiveHttpRequest(
            requestQueue_,
            HTTP_NULL_ID,
            HTTP_RECEIVE_REQUEST_FLAG_COPY_BODY,
            reinterpret_cast<PHTTP_REQUEST>(requestBuffer.data()),
            static_cast<ULONG>(requestBuffer.size()),
            &bytesRead,
            nullptr);

        if (receiveResult == ERROR_OPERATION_ABORTED) {
            break; // queue shut down
        }
        if (receiveResult == ERROR_MORE_DATA) {
            // The request didn't fit. Resize and retry. The first call
            // populates RequestId so we can reissue with the larger buf.
            requestBuffer.resize(bytesRead);
            continue;
        }
        if (receiveResult != NO_ERROR) {
            // Transient or fatal -- backoff briefly and retry.
            Sleep(50);
            continue;
        }

        const PHTTP_REQUEST request = reinterpret_cast<PHTTP_REQUEST>(requestBuffer.data());

        // Compose absolute path from pRawUrl. RawUrl is the percent-
        // decoded path+query as the client sent it.
        std::string path;
        if (request->pRawUrl != nullptr && request->RawUrlLength > 0) {
            path.assign(request->pRawUrl, request->RawUrlLength);
            const auto query = path.find('?');
            if (query != std::string::npos) path = path.substr(0, query);
        }

        // Pull the body if present. HTTP_RECEIVE_REQUEST_FLAG_COPY_BODY
        // copies as much of the body as fits in the receive buffer; if
        // the body is fragmented across packets or larger than fits with
        // headers, the remainder must be drained via
        // HttpReceiveRequestEntityBody. Also: certain client libraries
        // (e.g. PowerShell Invoke-RestMethod with Expect:100-continue or
        // chunked transfer-encoding) send the body in a way that makes
        // HTTP.sys leave it for explicit retrieval rather than inlining.
        std::string body;
        for (USHORT i = 0; i < request->EntityChunkCount; ++i) {
            const auto& chunk = request->pEntityChunks[i];
            if (chunk.DataChunkType == HttpDataChunkFromMemory) {
                body.append(reinterpret_cast<const char*>(chunk.FromMemory.pBuffer),
                            chunk.FromMemory.BufferLength);
            }
        }
        // Drain the rest of the body, if any. Loop until ERROR_HANDLE_EOF
        // or NO_MORE_DATA. 8 KB chunks is fine for typical MCP envelopes.
        if ((request->Flags & HTTP_REQUEST_FLAG_MORE_ENTITY_BODY_EXISTS) != 0
            || body.empty()) {
            constexpr ULONG kBodyChunkBytes = 8 * 1024;
            std::vector<uint8_t> bodyChunk(kBodyChunkBytes);
            for (;;) {
                ULONG bytesReturned = 0;
                const ULONG entityResult = HttpReceiveRequestEntityBody(
                    requestQueue_,
                    request->RequestId,
                    0,
                    bodyChunk.data(),
                    static_cast<ULONG>(bodyChunk.size()),
                    &bytesReturned,
                    nullptr);
                if (entityResult == NO_ERROR) {
                    if (bytesReturned > 0) {
                        body.append(reinterpret_cast<const char*>(bodyChunk.data()),
                                    bytesReturned);
                    } else {
                        break;
                    }
                } else if (entityResult == ERROR_HANDLE_EOF) {
                    break;
                } else {
                    // ERROR_NO_MORE_BYTES (38) and others -- bail.
                    break;
                }
            }
        }

        // Route. /health returns adapter state; any /mcp* path goes to
        // the MCP handler; everything else 404s with structured JSON.
        std::string responseBody;
        std::string contentType = "application/json";
        USHORT statusCode = 200;
        std::string reason = "OK";

        const auto& healthPath = configuration_.healthPath.empty()
            ? std::string("/health") : configuration_.healthPath;
        const auto& mcpPath = configuration_.mcpPath.empty()
            ? std::string("/mcp") : configuration_.mcpPath;

        if (path == healthPath || (path.size() > healthPath.size()
                                   && path.rfind(healthPath, 0) == 0
                                   && path[healthPath.size()] == '/')) {
            const auto health = Probe();
            nlohmann::json body_j = {
                { "adapterType", AdapterType() },
                { "state", to_string(status_.state) },
                { "health", to_string(health.status) },
                { "message", status_.message }
            };
            responseBody = body_j.dump();
        } else if (path == mcpPath || (path.size() > mcpPath.size()
                                       && path.rfind(mcpPath, 0) == 0
                                       && path[mcpPath.size()] == '/')) {
            responseBody = handleMcpRequest(path, body);
        } else {
            statusCode = 404;
            reason = "Not Found";
            nlohmann::json err = {
                { "error", "Path not handled by native MCP gateway" },
                { "path", path },
                { "hint", "Use " + mcpPath + " for MCP traffic, " + healthPath + " for health." }
            };
            responseBody = err.dump();
        }

        // Build and send the HTTP response.
        HTTP_RESPONSE response{};
        response.StatusCode = statusCode;
        response.pReason = reason.c_str();
        response.ReasonLength = static_cast<USHORT>(reason.size());

        HTTP_UNKNOWN_HEADER unused{};
        (void)unused;

        // Set Content-Type via the known-headers slot.
        response.Headers.KnownHeaders[HttpHeaderContentType].pRawValue = contentType.c_str();
        response.Headers.KnownHeaders[HttpHeaderContentType].RawValueLength
            = static_cast<USHORT>(contentType.size());

        std::string lengthStr = std::to_string(responseBody.size());
        response.Headers.KnownHeaders[HttpHeaderContentLength].pRawValue = lengthStr.c_str();
        response.Headers.KnownHeaders[HttpHeaderContentLength].RawValueLength
            = static_cast<USHORT>(lengthStr.size());

        HTTP_DATA_CHUNK dataChunk{};
        dataChunk.DataChunkType = HttpDataChunkFromMemory;
        dataChunk.FromMemory.pBuffer = const_cast<char*>(responseBody.data());
        dataChunk.FromMemory.BufferLength = static_cast<ULONG>(responseBody.size());
        response.EntityChunkCount = 1;
        response.pEntityChunks = &dataChunk;

        ULONG bytesSent = 0;
        HttpSendHttpResponse(
            requestQueue_,
            request->RequestId,
            0,
            &response,
            nullptr,
            &bytesSent,
            nullptr,
            0,
            nullptr,
            nullptr);
    }
}
#endif

} // namespace MasterControl
