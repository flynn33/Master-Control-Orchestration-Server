// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.
//
// MCP Gateway adapters. The gateway abstraction (`IMcpGateway`)
// lives in `MasterControl/MasterControlContracts.h`. This header
// declares the production adapter (NativeHttpSysGatewayAdapter,
// the in-process Win32 HTTP.sys substrate) and a test fake that
// implements the same interface without binding HTTP.sys or
// making network calls.

#pragma once

#include "MasterControl/CapabilityAuthorization.h"
#include "MasterControl/MasterControlContracts.h"
#include "MasterControl/MasterControlModels.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
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
// an external gateway as an external child process. Operator selects via
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
    void AttachCapabilityResolver(CapabilityResolver resolver,
                                  CapabilityDenialAuditSink auditSink = {});

private:
    void serveLoop();
    // Closes HTTP.sys handles only. Caller holds mutex_. Thread joins
    // happen in Stop() OUTSIDE the gateway mutex -- never join a thread
    // that can itself acquire mutex_ (Probe from the /health route) while
    // holding it.
    void closeHttpSysHandlesLocked();
    // v0.7.2: handleMcpRequest gains optional clientIpAddress + clientType
    // arguments so the gateway can attribute leases to the originating
    // LAN client. serveLoop fills both from HTTP_REQUEST::Address and
    // headers (X-MCOS-Client-Id, X-MCOS-Client-Type, falling back to
    // best-effort User-Agent inference). Empty strings are valid -- the
    // dashboard renders 'unknown' honestly when either field is absent.
    //
    // Session remediation: sessionId carries the client-provided MCP
    // session identifier (Mcp-Session-Id header, X-MCOS-Session-Id as the
    // MCOS-specific fallback). Empty when the client sent neither -- the
    // gateway never invents session ids. A non-empty session id makes
    // tools/call leases stateful (sticky to one instance for the session
    // lifetime; expiry via the lease router's idle timeout).
    std::string handleMcpRequest(const std::string& path,
                                 const std::string& body,
                                 const std::string& clientIpAddress = std::string{},
                                 const std::string& clientType = std::string{},
                                 const std::string& clientId = std::string{},
                                 const std::string& sessionId = std::string{});
    CapabilityAuthorizationContext resolveCapabilities(
        const std::string& clientId,
        const std::string& clientIpAddress) const;
    void auditCapabilityDenial(const CapabilityDenialAuditEvent& event) const;

    // PHASE-12 follow-up (v0.6.10), lock-hygiene remediation: the catalog
    // path snapshots state under mutex_, performs every child stdio RPC
    // with NO gateway lock held, then reacquires mutex_ only to publish
    // the cache. Pre-remediation refreshToolCatalogLocked() ran up to 5s
    // of child tools/list RPC per pool while holding mutex_, stalling
    // every other gateway method.
    //
    // currentToolCatalog(): TTL-checked entry point (returns the cache or
    // refreshes it). collectToolCatalog(): pure aggregation over the
    // snapshotted supervisor -- caller must NOT hold mutex_.
    std::vector<McpToolDescriptor> currentToolCatalog() const;
    std::vector<McpToolDescriptor> collectToolCatalog(
        const std::shared_ptr<IWorkerSupervisor>& supervisor) const;

    // v0.9.6: explicit cache invalidation hook. Operator pool changes
    // (POST /api/pools, POST /api/pools/{id}/{remove,scale,drain})
    // bump this so the next LAN-client tools/list returns the
    // up-to-date catalog instead of waiting for the 30s TTL to
    // expire. Pre-v0.9.6 the TTL was the only revalidation path,
    // which meant operators registering a new pool waited up to 30s
    // before LAN clients could discover its tools.
    void InvalidateToolCatalog() override;

    mutable std::mutex mutex_;
    // Serializes Start()/Stop() end-to-end (review fix). Distinct from
    // mutex_ so state reads (CurrentStatus/Probe, i.e. /health) never wait
    // behind a Stop() that is joining threads. Lock order: lifecycleMutex_
    // before mutex_; nothing that holds mutex_ ever takes lifecycleMutex_.
    std::mutex lifecycleMutex_;
    McpGatewayConfiguration configuration_;
    GatewayStatus status_;
    std::map<std::string, McpServerRegistration> registry_;

    // PHASE-12 follow-up (v0.6.10): bridge to supervised pool instances.
    // workerSupervisor_ owns the stdio pipes; leaseRouter_ picks which
    // instance to forward to. Both are nullable until AttachWorkerBridge
    // is called.
    std::shared_ptr<IWorkerSupervisor> workerSupervisor_;
    std::shared_ptr<ILeaseRouter> leaseRouter_;
    CapabilityResolver capabilityResolver_;
    CapabilityDenialAuditSink capabilityDenialAuditSink_;

    // PHASE-12 follow-up (v0.6.10): cached tools catalog, refreshed on
    // every tools/list. Used by tools/call to resolve a tool name to the
    // pool that hosts it. serverName == poolId (worker pool name).
    //
    // v0.9.5: gained a TTL (toolCatalogCacheValidUntil_) so a burst of
    // LAN-client tools/list requests doesn't fan out 5 stdio
    // round-trips per call. Pre-v0.9.5 every tools/list call did a
    // full refresh; with five supervised pools that was up to 5x
    // (lock + write + poll-read) on every request, even when the
    // catalog hadn't changed. The cache TTL is short enough
    // (kToolCatalogCacheTtlSeconds, default 30s) that operator-driven
    // pool upserts surface within a window operators expect, and
    // schema changes from a child server (which require a full
    // refresh anyway) are still visible after at most one TTL.
    mutable std::vector<McpToolDescriptor> toolCatalogCache_;
    mutable std::chrono::steady_clock::time_point toolCatalogCacheValidUntil_{};
    // Guarded by mutex_. True while one thread runs the (unlocked) child
    // RPC fan-out; concurrent TTL-miss callers serve the stale cache
    // instead of duplicating the fan-out (review follow-up).
    mutable bool toolCatalogRefreshInFlight_ = false;
    // monotonic JSON-RPC id counter used when the gateway speaks to its
    // own pool children (out-of-band from the LAN-client request stream).
    // Atomic so id generation never requires the gateway mutex during
    // child RPC (lock-hygiene remediation).
    mutable std::atomic<std::uint64_t> bridgeRequestIdCounter_{ 1 };

#if defined(_WIN32)
    // Concurrency remediation: the receive loop and request execution are
    // split. serveLoop() only receives, answers /health and other cheap
    // routes inline, and enqueues MCP work as value-copied jobs; a small
    // bounded worker pool executes handleMcpRequest so one slow
    // tools/call can never block /health or other gateway requests. A
    // saturated queue answers with a structured HTTP 503 instead of
    // growing without bound.
    struct GatewayRequestJob final {
        unsigned long long requestId = 0;   // HTTP_REQUEST_ID
        std::string path;
        std::string body;                   // inline chunks copied at receive
        // Review follow-up: when HTTP.sys leaves body bytes for explicit
        // retrieval (fragmented body, Expect:100-continue, chunked), the
        // WORKER drains them via HttpReceiveRequestEntityBody before
        // executing -- the receive thread never blocks on a slow-trickle
        // client, so /health and request intake stay responsive.
        bool drainRemainingBody = false;
        std::string clientIpAddress;
        std::string clientType;
        std::string clientId;
        std::string sessionId;
    };
    void workerLoop();
    void processGatewayJob(const GatewayRequestJob& job);

    static constexpr std::size_t kGatewayWorkerCount = 4;
    static constexpr std::size_t kGatewayJobQueueMaxDepth = 64;
    // Upper bound on an MCP request body (declared Content-Length or
    // accumulated drain). Oversized requests answer 413 instead of
    // ballooning gateway memory.
    static constexpr std::size_t kMaxGatewayRequestBytes = 10 * 1024 * 1024;

    HANDLE requestQueue_ = nullptr;
    uint64_t serverSessionId_ = 0;     // HTTP_SERVER_SESSION_ID
    uint64_t urlGroupId_ = 0;          // HTTP_URL_GROUP_ID
    bool httpInitialized_ = false;
    std::thread serveThread_;
    std::vector<std::thread> workerThreads_;
    std::atomic<bool> running_{ false };
    // Job queue state guarded by jobQueueMutex_ (deliberately separate
    // from mutex_ so enqueue/dequeue never contends with gateway state).
    std::deque<GatewayRequestJob> jobQueue_;
    std::mutex jobQueueMutex_;
    std::condition_variable jobQueueCv_;
    bool jobQueueShutdown_ = false;
#endif
};

// JSON-safe internal-error envelope for the gateway's exception paths.
// Built with nlohmann::json so exception text containing quotes, control
// characters, or invalid UTF-8 still produces a parseable JSON-RPC error
// document. Free function so the bool-style test suite can verify the
// escaping without binding HTTP.sys.
std::string BuildGatewayInternalErrorBody(const std::string& detail);

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
