// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.
//
// PHASE-12 (ADR-002 §2 / ADR-003): MCP Gateway adapters. The gateway
// abstraction (`IMcpGateway`) lives in
// `MasterControl/MasterControlContracts.h`. This header declares the
// production adapter and a test fake that implements the same interface
// without binding HTTP.sys or making network calls.
//
// History:
//   * v0.6.x shipped `McpJungleGatewayAdapter`, which supervised an
//     external MCPJungle binary. PHASE-12 replaced that substrate with a
//     native HTTP.sys implementation (NativeHttpSysGatewayAdapter).
//   * v0.9.0 retired McpJungleGatewayAdapter as the default but kept its
//     source in-tree as inert dead code for one release cycle.
//   * v0.9.1 deletes McpJungleGatewayAdapter outright. Persisted configs
//     that still carry mcpGateway.type='mcpjungle' transparently resolve
//     to the native substrate at runtime construction (the GatewayType
//     enum value is retained only so old JSON deserializes without
//     rejection).

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
    // v0.7.2: handleMcpRequest gains optional clientIpAddress + clientType
    // arguments so the gateway can attribute leases to the originating
    // LAN client. serveLoop fills both from HTTP_REQUEST::Address and
    // headers (X-MCOS-Client-Id, X-MCOS-Client-Type, falling back to
    // best-effort User-Agent inference). Empty strings are valid -- the
    // dashboard renders 'unknown' honestly when either field is absent.
    std::string handleMcpRequest(const std::string& path,
                                 const std::string& body,
                                 const std::string& clientIpAddress = std::string{},
                                 const std::string& clientType = std::string{});

    // PHASE-12 follow-up (v0.6.10): walk every pool's first Ready instance,
    // ask it tools/list via the stdio bridge, and rebuild the cached
    // catalog. Returns the full catalog (serverName-attributed). Caller
    // holds mutex_.
    // v0.9.3: const so it can be called from ListTools() (const).
    // Mutates only `toolCatalogCache_` and `bridgeRequestIdCounter_`,
    // both already declared `mutable`.
    std::vector<McpToolDescriptor> refreshToolCatalogLocked() const;

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

// In-process fake. State transitions and registry behavior match the
// IMcpGateway contract; Probe() returns whatever the test scripts via
// `setNextProbe(...)`. Used by `MasterControlOrchestrationServerTests`
// and any future adapter consumer that needs to exercise IMcpGateway
// without binding HTTP.sys.
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
