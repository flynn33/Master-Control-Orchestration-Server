// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.
//
// PHASE-02 (ADR-002 §2): MCP Gateway adapters. The gateway abstraction
// (`IMcpGateway`) lives in `MasterControl/MasterControlContracts.h`. This
// header declares the production adapter and a test fake that implements
// the same interface without spawning a child process or making network
// calls.
//
// The `McpJungleGatewayAdapter` supervises an external MCPJungle binary
// when one is configured and reachable. When `enabled=false`, the binary
// path is empty, or the binary cannot be located, the adapter operates in
// "configured-but-not-running" mode: state transitions still happen,
// register/deregister calls still succeed against an in-memory registry,
// but no child process is spawned and Probe() reports
// `GatewayHealthStatus::Unknown`. This honors `.claude/rules/00-mcos-realignment.md`'s
// "no live-looking seeded infrastructure" rule — the dashboard sees an
// honest "configured, not running" state rather than a fake "healthy".

#pragma once

#include "MasterControl/MasterControlContracts.h"
#include "MasterControl/MasterControlModels.h"

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
// winsock2.h MUST come before windows.h. windows.h transitively includes
// the legacy winsock.h which collides with winsock2.h on every struct
// (sockaddr, fd_set, WSAData, ...) and every API (accept, bind, ...).
// Pulling winsock2.h first wins the typedef race; WIN32_LEAN_AND_MEAN
// then suppresses the legacy include from windows.h's transitive set
// for any TU that includes us before defining its own.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>
#endif

namespace MasterControl {

// Production adapter. Supervises MCPJungle when a binary path is provided.
// Health probe uses WinHTTP against `<listenHost>:<listenPort><healthPath>`.
class McpJungleGatewayAdapter final : public IMcpGateway {
public:
    explicit McpJungleGatewayAdapter(McpGatewayConfiguration configuration);
    ~McpJungleGatewayAdapter() override;

    McpJungleGatewayAdapter(const McpJungleGatewayAdapter&) = delete;
    McpJungleGatewayAdapter& operator=(const McpJungleGatewayAdapter&) = delete;

    GatewayStatus Start() override;
    GatewayStatus Stop() override;
    GatewayStatus CurrentStatus() const override;
    GatewayHealth Probe() override;
    RegistrationResult RegisterHttpServer(const McpServerRegistration& server) override;
    RegistrationResult RegisterStdioServer(const McpServerRegistration& server) override;
    DeregistrationResult DeregisterServer(const std::string& serverName) override;
    std::vector<McpToolDescriptor> ListTools() const override;
    std::string GatewayMcpUrl() const override;
    std::string AdapterType() const override;

    // Test/observability helpers — not part of IMcpGateway.
    McpGatewayConfiguration configuration() const;
    bool isSupervisingChildProcess() const;

private:
    RegistrationResult registerInternal(McpServerRegistration server);
    GatewayHealth probeOverHttp() const;
    void terminateChildProcessTreeIfRunning();

    // PHASE-12 prelude (Option D from v0.6.7 plan): when no real gateway
    // substrate is bound to mcpGateway.listenPort, run a small honest-503
    // listener so LAN clients see a structured "gateway unavailable"
    // response instead of TCP RST. This eliminates the connection-refused
    // confusion the operator's remote Claude Code instance hit when
    // pointing at http://<host>:8080/mcp.
    //
    // Lifecycle:
    //   - Started when adapter enters Disabled, Configured (no binary), or
    //     supervised-mock Running.
    //   - Stopped before spawning a real MCPJungle binary (so the binary
    //     can claim the port) and on Stop().
    //
    // The listener is intentionally tiny: a single accept loop on a
    // blocking socket, returning a fixed JSON 503 to every request.
    // Replaced wholesale by the PHASE-12 native gateway when that lands.
    void startHonestUnavailableListenerLocked();
    void stopHonestUnavailableListenerLocked();
#if defined(_WIN32)
    void honestUnavailableServeLoop();
    SOCKET honestListenerSocket_ = INVALID_SOCKET;
    std::thread honestListenerThread_;
    std::atomic<bool> honestListenerRunning_{ false };
#endif

    mutable std::mutex mutex_;
    McpGatewayConfiguration configuration_;
    GatewayStatus status_;
    std::map<std::string, McpServerRegistration> registry_;

#if defined(_WIN32)
    HANDLE jobObject_ = nullptr;
    PROCESS_INFORMATION processInfo_{};
#endif
    std::atomic<bool> childProcessActive_{ false };
};

// PHASE-12 native gateway substrate. Implements `IMcpGateway` directly
// using HTTP.sys (Win32 kernel-mode HTTP server) instead of supervising
// MCPJungle as an external child process. Operator selects via
// mcpGateway.type = "native". Replaces the v0.6.7 honest-503 listener
// when active.
//
// Coverage in v0.6.10:
//   * HTTP.sys URL group + request queue bound to mcpGateway.listenPort
//   * MCP Streamable-HTTP transport: parses POST {/mcp, /mcp/{poolId},
//     /agents/{poolId}} JSON-RPC envelopes
//   * `initialize` handshake
//   * `tools/list` aggregated from supervised pool instances via the
//     stdio bridge (PHASE-12 follow-up); each instance is asked for its
//     own tools/list and the merged catalog is returned with serverName
//     attribution. Cached so subsequent tools/call can resolve names.
//   * `tools/call` forwarded to a lease-router-selected pool instance via
//     stdio bridge JSON-RPC. Per-instance stdin/stdout pipes, request id
//     correlation, 30s default timeout.
//   * health probe at mcpGateway.healthPath returns adapter-state JSON
//
// Out of scope for v0.6.10 (PHASE-12 follow-ups):
//   * SSE streaming for long-running tool calls
//   * TLS termination via HTTP.sys binding (operator-side task)
//   * Multi-tenant LAN auth (LAN-trusted-only per ADR-002 §1)
//
// Wiring: the runtime constructs the adapter early (see
// MasterControlRuntime construction), then later calls AttachWorkerBridge
// once WorkerSupervisor and LeaseRouter exist. tools/call returns a
// structured -32603 if the bridge is not attached at request time.
class NativeHttpSysGatewayAdapter final : public IMcpGateway {
public:
    explicit NativeHttpSysGatewayAdapter(McpGatewayConfiguration configuration);
    ~NativeHttpSysGatewayAdapter() override;

    NativeHttpSysGatewayAdapter(const NativeHttpSysGatewayAdapter&) = delete;
    NativeHttpSysGatewayAdapter& operator=(const NativeHttpSysGatewayAdapter&) = delete;

    GatewayStatus Start() override;
    GatewayStatus Stop() override;
    GatewayStatus CurrentStatus() const override;
    GatewayHealth Probe() override;
    RegistrationResult RegisterHttpServer(const McpServerRegistration& server) override;
    RegistrationResult RegisterStdioServer(const McpServerRegistration& server) override;
    DeregistrationResult DeregisterServer(const std::string& serverName) override;
    std::vector<McpToolDescriptor> ListTools() const override;
    std::string GatewayMcpUrl() const override;
    std::string AdapterType() const override;

    // PHASE-12 follow-up (v0.6.10): late-bind the worker supervisor + lease
    // router after they exist in the runtime. Called exactly once during
    // construction. Until this is called, tools/call returns an honest
    // -32603 indicating the bridge is unconfigured. tools/list returns an
    // empty catalog rather than silently returning stale data.
    void AttachWorkerBridge(std::shared_ptr<IWorkerSupervisor> workerSupervisor,
                            std::shared_ptr<ILeaseRouter> leaseRouter);

private:
    void serveLoop();
    void teardownHttpSysLocked();
    std::string handleMcpRequest(const std::string& path, const std::string& body);

    // PHASE-12 follow-up (v0.6.10): walk every pool's first Ready instance,
    // ask it tools/list via the stdio bridge, and rebuild the cached
    // catalog. Returns the full catalog (serverName-attributed). Caller
    // holds mutex_.
    std::vector<McpToolDescriptor> refreshToolCatalogLocked();

    mutable std::mutex mutex_;
    McpGatewayConfiguration configuration_;
    GatewayStatus status_;
    std::map<std::string, McpServerRegistration> registry_;

    // PHASE-12 follow-up (v0.6.10): bridge to supervised pool instances.
    // workerSupervisor_ owns the stdio pipes; leaseRouter_ picks which
    // instance to forward to. Both are nullable until AttachWorkerBridge
    // is called.
    std::shared_ptr<IWorkerSupervisor> workerSupervisor_;
    std::shared_ptr<ILeaseRouter> leaseRouter_;

    // PHASE-12 follow-up (v0.6.10): cached tools catalog, refreshed on
    // every tools/list. Used by tools/call to resolve a tool name to the
    // pool that hosts it. serverName == poolId (worker pool name).
    mutable std::vector<McpToolDescriptor> toolCatalogCache_;
    // monotonic JSON-RPC id counter used when the gateway speaks to its
    // own pool children (out-of-band from the LAN-client request stream)
    mutable uint64_t bridgeRequestIdCounter_ = 1;

#if defined(_WIN32)
    HANDLE requestQueue_ = nullptr;
    uint64_t serverSessionId_ = 0;     // HTTP_SERVER_SESSION_ID
    uint64_t urlGroupId_ = 0;          // HTTP_URL_GROUP_ID
    bool httpInitialized_ = false;
    std::thread serveThread_;
    std::atomic<bool> running_{ false };
#endif
};

// In-process fake. State transitions and registry behavior match
// `McpJungleGatewayAdapter`'s contract, but Probe() returns whatever the
// test scripts via `setNextProbe(...)`. Used by
// `MasterControlOrchestrationServerTests` and any future adapter consumer
// that needs to exercise IMcpGateway without a real binary.
class FakeMcpGatewayAdapter final : public IMcpGateway {
public:
    explicit FakeMcpGatewayAdapter(McpGatewayConfiguration configuration);
    ~FakeMcpGatewayAdapter() override = default;

    GatewayStatus Start() override;
    GatewayStatus Stop() override;
    GatewayStatus CurrentStatus() const override;
    GatewayHealth Probe() override;
    RegistrationResult RegisterHttpServer(const McpServerRegistration& server) override;
    RegistrationResult RegisterStdioServer(const McpServerRegistration& server) override;
    DeregistrationResult DeregisterServer(const std::string& serverName) override;
    std::vector<McpToolDescriptor> ListTools() const override;
    std::string GatewayMcpUrl() const override;
    std::string AdapterType() const override;

    // Test scripting hooks.
    void setNextProbe(GatewayHealth probe);
    void setStartShouldFail(bool shouldFail, const std::string& message = "");
    std::size_t startCallCount() const;
    std::size_t stopCallCount() const;
    std::size_t probeCallCount() const;
    std::vector<std::string> registeredServerNames() const;

private:
    mutable std::mutex mutex_;
    McpGatewayConfiguration configuration_;
    GatewayStatus status_;
    std::map<std::string, McpServerRegistration> registry_;
    GatewayHealth nextProbe_;
    bool startShouldFail_ = false;
    std::string startFailureMessage_;
    std::size_t startCalls_ = 0;
    std::size_t stopCalls_ = 0;
    mutable std::size_t probeCalls_ = 0;
};

} // namespace MasterControl
