// Master Control Program
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

class IProviderRegistry {
public:
    virtual std::vector<ProviderConnection> listProviders() const = 0;
    virtual OperationResult upsertProvider(const ProviderConnection& provider) = 0;
    virtual ~IProviderRegistry() = default;
};

class IProviderCatalogService {
public:
    virtual std::vector<ProviderCapabilityDescriptor> listCapabilities() const = 0;
    virtual void upsertCapability(const ProviderCapabilityDescriptor& capability) = 0;
    virtual void removeCapability(const std::string& providerId) = 0;
    virtual ~IProviderCatalogService() = default;
};

class IProviderCredentialStore {
public:
    virtual std::vector<ProviderCredentialStatus> listStatuses() const = 0;
    virtual std::map<std::string, std::string> readCredentials(const std::string& providerId) const = 0;
    virtual OperationResult upsertCredentials(const ProviderCredentialUpdate& update) = 0;
    virtual ~IProviderCredentialStore() = default;
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

class IProviderAssignmentService {
public:
    virtual std::vector<ProviderAssignmentTarget> listTargets() const = 0;
    virtual std::vector<ProviderAssignment> listAssignments() const = 0;
    virtual OperationResult upsertAssignment(const ProviderAssignment& assignment) = 0;
    virtual ~IProviderAssignmentService() = default;
};

class IProviderExecutionCatalogService {
public:
    virtual std::vector<ProviderExecutionRegistration> listRegistrations() const = 0;
    virtual void upsertRegistration(const ProviderExecutionRegistration& registration) = 0;
    virtual void removeRegistration(const std::string& providerId) = 0;
    virtual ~IProviderExecutionCatalogService() = default;
};

class IProviderExecutionService {
public:
    virtual std::vector<ProviderExecutionRecord> history() const = 0;
    virtual ProviderExecutionRecord execute(const ProviderExecutionRequest& request) = 0;
    virtual ~IProviderExecutionService() = default;
};

class IPlatformGovernanceToolService {
public:
    virtual void upsertTool(const GovernanceToolDescriptor& descriptor) = 0;
    virtual void removeToolsForModule(const std::string& moduleId) = 0;
    virtual std::vector<GovernanceToolDescriptor> listTools() const = 0;
    virtual std::vector<GovernanceToolResult> recentExecutions() const = 0;
    virtual GovernanceToolResult execute(const GovernanceToolRequest& request) = 0;
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
    virtual GovernanceToolResult executeGovernanceTool(const GovernanceToolRequest& request) = 0;
    virtual ~ICommandLogicUnitService() = default;
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

class IAdminApiService {
public:
    virtual DashboardSnapshot snapshot() = 0;
    virtual GovernanceSnapshot governance() const = 0;
    virtual GovernanceToolResult executeGovernanceToolJson(const std::string& requestBody) = 0;
    virtual OperationResult applyConfigurationJson(const std::string& requestBody,
                                                   bool confirmUnsafeChanges) = 0;
    virtual OperationResult upsertProviderJson(const std::string& requestBody) = 0;
    virtual OperationResult upsertProviderCredentialsJson(const std::string& requestBody) = 0;
    virtual OperationResult upsertAppleRemoteHostJson(const std::string& requestBody) = 0;
    virtual OperationResult removeAppleRemoteHostJson(const std::string& requestBody) = 0;
    virtual OperationResult upsertMcpServerJson(const std::string& requestBody) = 0;
    virtual OperationResult removeMcpServerJson(const std::string& requestBody) = 0;
    virtual OperationResult upsertSubAgentJson(const std::string& requestBody) = 0;
    virtual OperationResult removeSubAgentJson(const std::string& requestBody) = 0;
    virtual OperationResult upsertSubAgentGroupJson(const std::string& requestBody) = 0;
    virtual OperationResult removeSubAgentGroupJson(const std::string& requestBody) = 0;
    virtual OperationResult upsertProviderAssignmentJson(const std::string& requestBody) = 0;
    virtual ProviderExecutionRecord executeProviderTaskJson(const std::string& requestBody) = 0;
    virtual OperationResult installPackageJson(const std::string& requestBody) = 0;
    virtual OperationResult installRepoJson(const std::string& requestBody) = 0;
    virtual OperationResult installZipJson(const std::string& requestBody) = 0;
    virtual ~IAdminApiService() = default;
};

} // namespace MasterControl
