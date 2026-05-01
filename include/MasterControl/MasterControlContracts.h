// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#pragma once

#include "MasterControl/MasterControlModels.h"

#include <map>
#include <string>
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

// PHASE-02 (ADR-002 §2): MCP Gateway abstraction. Implementations include
// `McpJungleGatewayAdapter` (supervises MCPJungle as a child process) and
// `FakeMcpGatewayAdapter` (used by tests). PHASE-11 evaluates a native
// HTTP.sys-backed gateway behind the same interface.
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
};

class IAdminApiService {
public:
    virtual DashboardSnapshot snapshot() = 0;
    virtual GovernanceSnapshot governance() const = 0;
    virtual GovernanceToolResult executeGovernanceToolJson(const std::string& requestBody) = 0;
    virtual OperationResult cancelAppleOperationJson(const std::string& requestBody) = 0;
    virtual OperationResult applyConfigurationJson(const std::string& requestBody,
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
