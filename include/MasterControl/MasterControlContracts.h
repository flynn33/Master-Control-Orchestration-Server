// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#pragma once

#include "MasterControl/MasterControlModels.h"

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace MasterControl {

class ITelemetryService {
public:
    virtual HostTelemetrySnapshot captureSnapshot() = 0;
    virtual ~ITelemetryService() = default;
};

class IRuntimeInventoryService {
public:
    virtual std::vector<RuntimeEndpoint> listEndpoints() = 0;
    virtual void refresh() = 0;
    // Non-blocking variant: fires the endpoint probe loop on a detached
    // background thread. Used by mutating handlers (upsertMcpServer,
    // upsertSubAgent, remove*) so the admin API doesn't block for 10+
    // seconds while N endpoints are TCP-probed sequentially.
    virtual void refreshAsync() = 0;
    virtual ~IRuntimeInventoryService() = default;
};

class IConfigurationService {
public:
    virtual AppConfiguration current() const = 0;
    virtual OperationResult update(const AppConfiguration& configuration,
                                   bool confirmUnsafeChanges) = 0;
    virtual ~IConfigurationService() = default;
};

class IResourceAllocationService {
public:
    virtual ResourceAllocationProfile current() const = 0;
    virtual OperationResult update(const ResourceAllocationProfile& profile) = 0;
    virtual ~IResourceAllocationService() = default;
};

class IPackageTrustEvaluator {
public:
    virtual PackageTrustDecision evaluate(const std::string& source,
                                          bool allowUntrustedExecution) const = 0;
    virtual ~IPackageTrustEvaluator() = default;
};

class IBootstrapRepoService {
public:
    virtual OperationResult installFromRepository(const BootstrapRepoSpec& spec) = 0;
    virtual ~IBootstrapRepoService() = default;
};

class IZipBundleService {
public:
    virtual OperationResult installFromZipBundle(const ZipBundleSpec& spec) = 0;
    virtual ~IZipBundleService() = default;
};

class IInstallerOrchestrator {
public:
    virtual OperationResult installPackage(const InstallerPackageSpec& spec) = 0;
    virtual std::vector<InstallProvenance> history() const = 0;
    virtual ~IInstallerOrchestrator() = default;
};

class ISubAgentGroupService {
public:
    virtual std::vector<SubAgentGroupDefinition> listGroups() const = 0;
    virtual OperationResult upsertGroup(const SubAgentGroupDefinition& group) = 0;
    virtual OperationResult removeGroup(const std::string& groupId) = 0;
    virtual ~ISubAgentGroupService() = default;
};

class ISubAgentCatalogService {
public:
    virtual std::vector<RuntimeEndpoint> listCustomSubAgents() const = 0;
    virtual OperationResult upsertSubAgent(const RuntimeEndpoint& subAgent) = 0;
    virtual OperationResult removeSubAgent(const std::string& subAgentId) = 0;
    virtual ~ISubAgentCatalogService() = default;
};

class IMcpServerCatalogService {
public:
    virtual std::vector<RuntimeEndpoint> listCustomMcpServers() const = 0;
    virtual OperationResult upsertMcpServer(const RuntimeEndpoint& mcpServer) = 0;
    virtual OperationResult removeMcpServer(const std::string& mcpServerId) = 0;
    virtual ~IMcpServerCatalogService() = default;
};

class IPlatformGovernanceToolService {
public:
    virtual void upsertTool(const GovernanceToolDescriptor& descriptor) = 0;
    virtual void removeToolsForModule(const std::string& moduleId) = 0;
    virtual std::vector<GovernanceToolDescriptor> listTools() const = 0;
    virtual std::vector<GovernanceToolResult> recentExecutions() const = 0;
    virtual std::vector<AppleOperationRecord> recentAppleOperations() const = 0;
    virtual GovernanceToolResult execute(const GovernanceToolRequest& request) = 0;
    virtual OperationResult cancelAppleOperation(const std::string& operationId) = 0;
    virtual ~IPlatformGovernanceToolService() = default;
};

class IAppleRemoteHostService {
public:
    virtual std::vector<AppleRemoteHost> listHosts() const = 0;
    virtual OperationResult upsertHost(const AppleRemoteHost& host) = 0;
    virtual OperationResult removeHost(const std::string& hostId) = 0;
    virtual std::optional<AppleRemoteHost> inspectHost(const std::string& hostId) = 0;
    virtual std::optional<AppleRemoteHost> selectHostForPlatform(PlatformTarget platform) = 0;
    virtual AppleRemoteCommandResult executeCommand(
        const std::string& hostId,
        const AppleRemoteCommandRequest& request) = 0;
    virtual ~IAppleRemoteHostService() = default;
};

class IExportService {
public:
    virtual std::vector<ExportArtifact> generateExports() const = 0;
    virtual ~IExportService() = default;
};

// ============================================================================
// Model Parity (A3.12.0): provider-neutral Client Integration Catalog.
// Each provider owns onboarding/export/validation for one external
// client/model surface. MCOS remains a governed MCP gateway: providers emit
// provider-native config artifacts, never embed provider SDK execution.
// ============================================================================

// One external client/model integration. Concrete providers are `final`, hold
// no mutable state, and compose the compatibility analyzer for validate().
class IClientIntegrationProvider {
public:
    virtual std::string id() const = 0;
    virtual ClientIntegrationDescriptor descriptor() const = 0;
    virtual OnboardingProfile createOnboardingProfile(
        const ClientIntegrationContext& context) const = 0;
    virtual std::vector<ClientExportArtifact> createExportArtifacts(
        const ClientIntegrationContext& context) const = 0;
    virtual ClientIntegrationValidationResult validate(
        const ClientIntegrationContext& context) const = 0;
    virtual ~IClientIntegrationProvider() = default;
};

// Deterministic registry of providers. findById resolves canonical ids AND
// compatibility aliases (claude, openai, xai, grok, generic) without collapsing
// product-specific behavior. No filesystem scanning, no hidden globals.
class IClientIntegrationCatalog {
public:
    virtual std::vector<ClientIntegrationDescriptor> list() const = 0;
    virtual std::shared_ptr<const IClientIntegrationProvider>
        findById(std::string_view id) const = 0;
    virtual ~IClientIntegrationCatalog() = default;
};

// Reasons over (integration, live gateway) to produce structured transport /
// auth / reachability warnings. Warnings are product features, never buried in
// prose: a LAN-only HTTP gateway must not silently emit a hosted-ChatGPT artifact.
class IRemoteMcpCompatibilityAnalyzer {
public:
    virtual RemoteMcpCompatibilityResult analyze(
        const ClientIntegrationDescriptor& integration,
        const GatewayRuntimeDescriptor& gateway) const = 0;
    virtual ~IRemoteMcpCompatibilityAnalyzer() = default;
};

class ICommandLogicUnitService {
public:
    virtual GovernanceSnapshot currentGovernance() const = 0;
    virtual GovernanceEnforcementDecision enforceAction(const GovernanceEnforcementRequest& request) const = 0;
    virtual GovernanceToolResult executeGovernanceTool(const GovernanceToolRequest& request) = 0;
    virtual OperationResult cancelAppleOperation(const std::string& operationId) = 0;
    virtual ~ICommandLogicUnitService() = default;
};

// Phase 7 of ADR-001. Stages mutations whose CLU outcome is
// RequiresOperatorApproval until an operator approves or rejects them.
// Implementations are in-memory only in Phase 7; persistence across
// restarts is intentionally deferred (Phase 9 may add disk backing if
// long-running deferrals become a real workflow).
class IGovernanceApprovalQueueService {
public:
    virtual std::vector<GovernanceDeferredAction> listPending() const = 0;
    virtual std::vector<GovernanceDeferredAction> listAll() const = 0;
    virtual GovernanceDeferredAction stage(const GovernanceDeferredAction& action) = 0;
    virtual OperationResult approve(const std::string& deferredActionId,
                                    const std::string& operatorActor) = 0;
    virtual OperationResult reject(const std::string& deferredActionId,
                                   const std::string& operatorActor,
                                   const std::string& reason) = 0;
    virtual ~IGovernanceApprovalQueueService() = default;
};

class IModuleControlSurfaceService {
public:
    virtual void upsertControlSurfaceRequest(const ModuleControlSurfaceRequest& request) = 0;
    virtual void removeControlSurfaceRequest(const std::string& moduleId,
                                             const std::string& featureId) = 0;
    virtual void removeControlSurfaceRequestsForModule(const std::string& moduleId) = 0;
    virtual std::vector<ModuleControlSurfaceRequest> listControlSurfaceRequests() const = 0;
    virtual ~IModuleControlSurfaceService() = default;
};

class IForsettiSurfaceService {
public:
    virtual ForsettiSurfaceSnapshot currentSurface() const = 0;
    virtual void publishModuleSurface(const std::string& moduleId,
                                      const Forsetti::UIContributions& contributions) = 0;
    virtual void removeModuleSurface(const std::string& moduleId) = 0;
    virtual ~IForsettiSurfaceService() = default;
};

class IBeaconService {
public:
    virtual BeaconAdvertisement currentAdvertisement() const = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual ~IBeaconService() = default;
};

class IPlatformServiceCatalogService {
public:
    virtual void upsertGateway(const PlatformGatewayDescriptor& descriptor) = 0;
    virtual void removeGateway(const std::string& moduleId) = 0;
    virtual std::vector<PlatformGatewayDescriptor> listGateways() const = 0;
    virtual void upsertGovernanceServer(const GovernanceServerDescriptor& descriptor) = 0;
    virtual void removeGovernanceServer(const std::string& moduleId) = 0;
    virtual std::vector<GovernanceServerDescriptor> listGovernanceServers() const = 0;
    virtual ~IPlatformServiceCatalogService() = default;
};

// PHASE-08 (ADR-002 §9): Telemetry Aggregator. Holds the activity event
// ring, the connected-client roster, and gateway traffic counters.
// Honest only: per-AI-client CPU/GPU/disk arrives via ClientHeartbeat
// (or sidecar) — never fabricated. The aggregator also provides the
// event sink that other services call when meaningful state changes.
class ITelemetryAggregator {
public:
    virtual void recordEvent(TelemetryEvent event) = 0;
    virtual std::vector<TelemetryEvent> recentEvents(std::size_t maxEvents) const = 0;
    virtual void recordHeartbeat(ClientHeartbeat heartbeat) = 0;
    virtual std::vector<ClientPresence> clientRoster() const = 0;
    virtual GatewayTrafficSnapshot gatewayTraffic() const = 0;
    virtual void incrementGatewayRequest(bool errored) = 0;
    virtual ~ITelemetryAggregator() = default;
};

// PHASE-07 (ADR-002 §8): Lease Router. Resolves a LeaseRequest into a
// concrete EndpointLease bound to one Ready instance. Stateful sessions
// are sticky for the lease's lifetime; new sessions/leases route to
// Ready instances that have headroom under the pool's
// scalePolicy.maxActiveLeasesPerInstance threshold. When all Ready
// instances are at capacity and the pool has not yet reached
// scalePolicy.maxInstances, the router triggers a same-type scale-out
// via IWorkerSupervisor::ensureMinInstances-style scaling and routes
// the new lease to the freshly-spawned instance. Hot-migration of
// active stateful sessions is forbidden (ADR-002 §8); Draining
// instances retain sticky leases but new sessions route elsewhere.
class ILeaseRouter {
public:
    virtual EndpointLease acquireLease(const LeaseRequest& request) = 0;
    virtual OperationResult releaseLease(const std::string& leaseId,
                                         const std::string& reason) = 0;
    virtual std::vector<EndpointLease> activeLeases(const std::string& poolId) const = 0;
    virtual PoolSaturation saturationFor(const std::string& poolId) const = 0;
    virtual ~ILeaseRouter() = default;
};

// PHASE-06 (ADR-002 §7): Worker Supervisor. Owns the lifecycle of
// `ManagedEndpointPool` records and their `EndpointInstance` children.
// Supervised process trees are contained with Windows Job Objects so
// the supervisor can terminate the entire tree atomically. PHASE-07
// adds the lease router; PHASE-08 wires per-instance telemetry.
//
// In-memory only at PHASE-06. Persistence across restarts is intentional
// later work (PHASE-08/PHASE-09 may add disk backing).
// PHASE-12 follow-up (v0.6.10): result of a synchronous stdio JSON-RPC
// request to a supervised pool instance. the in-process HTTP.sys adapter
// uses this when a LAN AI client invokes tools/call: lease-router-
// selected instance receives the JSON-RPC envelope on its stdin pipe,
// and its stdout response (one or more lines, until a line carrying
// the matching id arrives or timeout fires) comes back as `responseBody`.
// `succeeded=false` means timeout, dead pipe, or no-such-instance --
// `errorMessage` carries the diagnostic.
struct StdioBridgeResult final {
    bool succeeded = false;
    std::string responseBody;   // raw JSON line(s) from child stdout
    std::string errorMessage;
};

class IWorkerSupervisor {
public:
    virtual std::vector<ManagedEndpointPool> listPools() const = 0;
    virtual std::optional<ManagedEndpointPool> findPool(const std::string& poolId) const = 0;
    virtual OperationResult upsertPool(ManagedEndpointPool pool) = 0;
    virtual OperationResult removePool(const std::string& poolId) = 0;
    virtual OperationResult ensureMinInstances(const std::string& poolId) = 0;
    // PHASE-07: scale-out trigger called by the LeaseRouter when all
    // Ready instances are at maxActiveLeasesPerInstance and the pool
    // has not yet reached scalePolicy.maxInstances. Returns the newly
    // spawned instance id (empty on no-op).
    virtual std::string scaleUpOnce(const std::string& poolId) = 0;
    virtual OperationResult drainPool(const std::string& poolId) = 0;
    virtual OperationResult shutdownAll() = 0;
    // PHASE-12 follow-up (v0.6.10): synchronous JSON-RPC request/response
    // to a single supervised pool instance via its stdin/stdout pipes.
    // Caller composes the JSON-RPC envelope (with a unique numeric id);
    // supervisor writes "{request}\n" to the child's stdin, reads stdout
    // until a JSON line whose id matches arrives, returns it as
    // responseBody. Used by NativeHttpSysGatewayAdapter::handleMcpRequest
    // to forward tools/call calls. Times out after timeoutMs (default 30s).
    // Calls are serialized per-instance via an internal mutex; concurrent
    // calls to the SAME instanceId queue.
    virtual StdioBridgeResult sendStdioJsonRpc(const std::string& instanceId,
                                               const std::string& request,
                                               int timeoutMs = 30000) = 0;
    virtual ~IWorkerSupervisor() = default;
};

// PHASE-05 (ADR-002 §6): Governance Bundle Service. Composes per-platform
// (windows / macos / ios) CLU + Forsetti governance bundles served at
// /api/governance/bundles/{platform}. Hydrates rules/instructions from the
// existing CLU profile (resources/clu/governance-profile.json) and the
// vendored Forsetti-Framework-Windows-main metadata. Vendored Forsetti
// content is read-only (per .claude/rules/20-forsetti-clu-governance.md).
class IGovernanceBundleService {
public:
    virtual GovernanceBundle bundleFor(const std::string& platform) const = 0;
    virtual GovernanceProfileSummary profileSummary() const = 0;
    virtual std::vector<std::string> supportedPlatforms() const = 0;
    virtual ~IGovernanceBundleService() = default;
};

// PHASE-04 (ADR-002 §5): Onboarding Profile Service. Composes
// per-client-type configuration profiles consumed by AI clients
// (Claude Code, Codex, Grok, ChatGPT connector-edge) and by the
// generic MCP fallback. Each profile points at the single MCOS
// gateway URL surfaced by IDiscoveryService and IMcpGateway.
class IOnboardingProfileService {
public:
    // v0.11.0-alpha.3: `platform` selects which per-platform CLU/Forsetti
    // governance bundle URL the profile advertises
    // (windows | macos | ios; empty/unknown falls back to windows --
    // same normalization semantics as GovernanceBundleService).
    // Pre-alpha.3 the bundle URL was hardcoded to the windows bundle
    // for every client (CHANGELOG Unreleased item, now closed).
    virtual OnboardingProfile profileFor(const std::string& clientType,
                                         const std::string& platform = std::string()) const = 0;
    virtual std::vector<std::string> knownClientTypes() const = 0;
    virtual ~IOnboardingProfileService() = default;
};

// PHASE-03 (ADR-002 §4): LAN Discovery Service. Owns DNS-SD/mDNS
// registration of `_mcos._tcp.local`, `_mcos-mcp._tcp.local`, and
// `_mcos-onboarding._tcp.local` plus composition of the discovery
// document served at /.well-known/mcos.json and broadcast over UDP.
class IDiscoveryService {
public:
    virtual DiscoveryDocument currentDocument() const = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual ~IDiscoveryService() = default;
};

// MCP Gateway abstraction. The shipping implementation is
// `NativeHttpSysGatewayAdapter` (in-process Win32 HTTP.sys);
// `FakeMcpGatewayAdapter` implements the same interface for tests
// without binding HTTP.sys or making network calls.
class IMcpGateway {
public:
    virtual ~IMcpGateway() = default;
    virtual GatewayStatus Start() = 0;
    virtual GatewayStatus Stop() = 0;
    virtual GatewayStatus CurrentStatus() const = 0;
    virtual GatewayHealth Probe() = 0;
    virtual RegistrationResult RegisterHttpServer(const McpServerRegistration& server) = 0;
    virtual RegistrationResult RegisterStdioServer(const McpServerRegistration& server) = 0;
    virtual DeregistrationResult DeregisterServer(const std::string& serverName) = 0;
    virtual std::vector<McpToolDescriptor> ListTools() const = 0;
    virtual std::string GatewayMcpUrl() const = 0;
    virtual std::string AdapterType() const = 0;

    // v0.9.6: explicit cache invalidation hook. The runtime calls this
    // after operator pool changes (POST /api/pools, POST /api/pools/
    // {id}/{remove,scale,drain}) so the next LAN-client tools/list
    // returns the up-to-date catalog instead of the cached snapshot.
    // The default implementation is a no-op so adapters that don't
    // cache (FakeMcpGatewayAdapter) inherit safe behavior.
    virtual void InvalidateToolCatalog() {}
};

// Factories for the runtime-internal supervisor and lease-router
// implementations. Both return the interface so callers (runtime wiring,
// tests) never see the concrete types. leaseIdleTimeoutSeconds bounds the
// lifetime of idle leases (sticky stateful sessions expire and Released
// records are garbage-collected after this window); <= 0 expires idle
// leases on the next sweep, which tests use to drive expiry
// deterministically.
std::shared_ptr<IWorkerSupervisor> createWorkerSupervisor(
    std::shared_ptr<IConfigurationService> configurationService = {});
std::shared_ptr<ILeaseRouter> createLeaseRouter(
    std::shared_ptr<IWorkerSupervisor> workerSupervisor,
    int leaseIdleTimeoutSeconds = 900);

// Model Parity (A3.12.0): factories for the client integration catalog and the
// remote-MCP compatibility analyzer. Both return interfaces so runtime wiring
// and tests never see the concrete providers. The catalog composes the
// analyzer (constructor injection) so each provider's validate() reuses it.
std::shared_ptr<IRemoteMcpCompatibilityAnalyzer>
    createRemoteMcpCompatibilityAnalyzer();
std::shared_ptr<IClientIntegrationCatalog> createClientIntegrationCatalog(
    std::shared_ptr<const IRemoteMcpCompatibilityAnalyzer> analyzer = {});

class IAdminApiService {
public:
    virtual DashboardSnapshot snapshot() = 0;
    virtual GovernanceSnapshot governance() const = 0;
    virtual GovernanceToolResult executeGovernanceToolJson(const std::string& requestBody) = 0;
    virtual OperationResult cancelAppleOperationJson(const std::string& requestBody) = 0;
    virtual OperationResult applyConfigurationJson(const std::string& requestBody,
                                                   bool confirmUnsafeChanges) = 0;
    // Partial configuration update: deep-merges the patch body into the
    // current configuration document, then runs the same validation and
    // persistence path as a full replacement. Rejects non-object bodies.
    virtual OperationResult applyConfigurationPatchJson(const std::string& requestBody,
                                                        bool confirmUnsafeChanges) = 0;
    virtual OperationResult upsertAppleRemoteHostJson(const std::string& requestBody) = 0;
    virtual OperationResult removeAppleRemoteHostJson(const std::string& requestBody) = 0;
    virtual OperationResult upsertMcpServerJson(const std::string& requestBody) = 0;
    virtual OperationResult removeMcpServerJson(const std::string& requestBody) = 0;
    virtual OperationResult upsertSubAgentJson(const std::string& requestBody) = 0;
    virtual OperationResult removeSubAgentJson(const std::string& requestBody) = 0;
    virtual OperationResult upsertSubAgentGroupJson(const std::string& requestBody) = 0;
    virtual OperationResult removeSubAgentGroupJson(const std::string& requestBody) = 0;
    virtual OperationResult installPackageJson(const std::string& requestBody) = 0;
    virtual OperationResult installRepoJson(const std::string& requestBody) = 0;
    virtual OperationResult installZipJson(const std::string& requestBody) = 0;
    virtual ~IAdminApiService() = default;
};

} // namespace MasterControl
