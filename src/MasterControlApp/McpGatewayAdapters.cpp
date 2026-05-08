// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

// winsock2 MUST come before windows.h. The header (McpGatewayAdapters.h)
// includes them in the right order; doing it here too defends against any
// future TU-level include-order regression.
//
// v0.9.1 removed the WinHTTP-based health probe (it lived inside the now-
// deleted McpJungleGatewayAdapter::probeOverHttp). The native gateway
// uses HTTP.sys to listen and an in-process state check for Probe(), so
// winhttp.h / winhttp.lib are no longer needed.
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <http.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "httpapi.lib")
#endif

#include "MasterControl/McpGatewayAdapters.h"

#include <cctype>
#include <chrono>
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

} // namespace

// ---------------------------------------------------------------------------
// McpJungleGatewayAdapter — REMOVED in v0.9.1
// ---------------------------------------------------------------------------
// Pre-v0.9.0 the production adapter supervised an external MCPJungle binary
// over its HTTP API. v0.9.0 retired that substrate in favor of the
// native HTTP.sys implementation below; the McpJungle implementation
// remained in-tree as inert dead code for one release cycle. v0.9.1
// deletes it. The runtime continues to accept the legacy
// mcpGateway.type='mcpjungle' value in persisted configs and transparently
// resolves to NativeHttpSysGatewayAdapter at construction time.
// ---------------------------------------------------------------------------

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

std::string NativeHttpSysGatewayAdapter::handleMcpRequest(const std::string& path,
                                                          const std::string& body,
                                                          const std::string& clientIpAddress,
                                                          const std::string& clientType) {
    // path is reserved for v0.6.11 path-based pool scoping (/mcp/{poolId}).
    // v0.6.10 routes by `params.name` -> cached toolCatalog -> serverName.
    // v0.7.2: clientIpAddress + clientType are best-effort attribution
    // fields used only when forwarding tools/call so the lease carries
    // who-acquired-it metadata into the dashboard.
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
                { "version", "0.7.2" }
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

    // v0.9.1: ping is part of the MCP wire protocol (used as a liveness
    // check by Claude Code, Codex, and the dashboard). Per spec the
    // response is an empty result object on a JSON-RPC 2.0 envelope.
    // Pre-v0.9.1 the gateway returned -32601 which made every ping
    // probe count as a failed call in the activity log even though
    // the gateway was demonstrably alive.
    if (method == "ping") {
        nlohmann::json envelope = {
            { "jsonrpc", "2.0" },
            { "id", id },
            { "result", nlohmann::json::object() }
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
        // v0.7.2: stamp client identity onto the lease request so
        // LeaseRouter::bindLeaseLocked carries it into the bound lease
        // and the dashboard's per-sub-agent active-clients panel can
        // attribute usage. Either field may be empty -- the dashboard
        // handles 'unknown' gracefully.
        leaseRequest.clientIpAddress = clientIpAddress;
        leaseRequest.clientType      = clientType;
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

        // v0.7.2: extract LAN-client identity from the HTTP request so
        // tools/call leases can carry attribution into the dashboard's
        // per-sub-agent active-clients panel. clientIpAddress comes from
        // HTTP_REQUEST::Address.pRemoteAddress (sockaddr* over AF_INET /
        // AF_INET6), formatted to a printable form via inet_ntop. clientType
        // is read from X-MCOS-Client-Type or X-MCOS-Client-Id header
        // (operator-set), falling back to best-effort User-Agent inference
        // (claude-code / codex / grok / chatgpt / generic-mcp). Both fields
        // are best-effort: empty strings flow through fine and the
        // dashboard simply renders 'unknown'.
        std::string clientIpAddress;
        if (request->Address.pRemoteAddress != nullptr) {
            char buffer[64] = { 0 };
            const auto* family = request->Address.pRemoteAddress;
            if (family->sa_family == AF_INET) {
                const auto* in4 = reinterpret_cast<const sockaddr_in*>(family);
                if (inet_ntop(AF_INET, &in4->sin_addr, buffer, sizeof(buffer)) != nullptr) {
                    clientIpAddress.assign(buffer);
                }
            } else if (family->sa_family == AF_INET6) {
                const auto* in6 = reinterpret_cast<const sockaddr_in6*>(family);
                if (inet_ntop(AF_INET6, &in6->sin6_addr, buffer, sizeof(buffer)) != nullptr) {
                    clientIpAddress.assign(buffer);
                }
            }
        }

        std::string clientType;
        // Walk the unknown-headers list (HTTP.sys puts custom X- headers
        // here) and the User-Agent slot in known-headers.
        for (USHORT h = 0; h < request->Headers.UnknownHeaderCount; ++h) {
            const auto& uh = request->Headers.pUnknownHeaders[h];
            if (uh.pName == nullptr || uh.pRawValue == nullptr) continue;
            std::string name(uh.pName, uh.NameLength);
            // Case-insensitive compare against X-MCOS-Client-Type / -Id.
            std::string lower;
            lower.reserve(name.size());
            for (char c : name) {
                lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            }
            if (lower == "x-mcos-client-type" || lower == "x-mcos-client-id") {
                clientType.assign(uh.pRawValue, uh.RawValueLength);
                break;
            }
        }
        if (clientType.empty()) {
            const auto& uaHeader = request->Headers.KnownHeaders[HttpHeaderUserAgent];
            if (uaHeader.pRawValue != nullptr && uaHeader.RawValueLength > 0) {
                std::string ua(uaHeader.pRawValue, uaHeader.RawValueLength);
                std::string uaLower;
                uaLower.reserve(ua.size());
                for (char c : ua) {
                    uaLower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
                }
                if (uaLower.find("claude") != std::string::npos)        clientType = "claude-code";
                else if (uaLower.find("codex") != std::string::npos)    clientType = "codex";
                else if (uaLower.find("grok") != std::string::npos)     clientType = "grok";
                else if (uaLower.find("chatgpt") != std::string::npos
                      || uaLower.find("openai") != std::string::npos)   clientType = "chatgpt";
                else                                                    clientType = "generic-mcp";
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
            responseBody = handleMcpRequest(path, body, clientIpAddress, clientType);
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
