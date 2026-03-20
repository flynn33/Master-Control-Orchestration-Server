// Master Control Program
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#pragma once

#include "pch.h"

namespace MasterControlShell {

enum class ServiceState {
    Missing,
    Stopped,
    StartPending,
    StopPending,
    Running,
    Paused,
    Unknown
};

enum class ShellInstallerKind {
    Msi,
    Exe,
    PowerShell
};

struct ShellProviderConnection final {
    std::wstring id;
    std::wstring kind;
    std::wstring displayName;
    std::wstring baseUrl;
    std::wstring modelId;
    bool enabled = true;
    bool allowAutonomousControl = false;
    bool credentialsConfigured = false;
};

struct ShellRuntimeEndpoint final {
    std::wstring id;
    std::wstring displayName;
    std::wstring kind;
    std::wstring host;
    uint16_t port = 0;
    std::wstring protocol;
    std::wstring status;
    std::wstring description;
    std::wstring routePath;
    std::wstring specialization;
    bool userDefined = false;
};

struct ShellProviderCredentialField final {
    std::wstring fieldId;
    std::wstring label;
    std::wstring kind;
    std::wstring helpText;
    std::wstring placeholder;
    std::wstring environmentVariableHint;
    std::wstring requirementGroup;
    bool secret = false;
    bool required = false;
};

struct ShellProviderCapability final {
    std::wstring moduleId;
    std::wstring providerId;
    std::wstring kind;
    std::wstring displayName;
    std::wstring description;
    std::wstring defaultBaseUrl;
    std::wstring recommendedModel;
    std::vector<ShellProviderCredentialField> credentialFields;
    std::vector<std::wstring> runtimeRequirements;
    std::vector<std::wstring> supportedTargets;
    bool supportsSharedMcpAccess = true;
    bool supportsAutonomousControl = true;
};

struct ShellProviderCredentialStatus final {
    std::wstring providerId;
    bool configured = false;
    std::vector<std::wstring> configuredFieldIds;
    std::wstring updatedAtUtc;
    std::wstring message;
};

struct ShellSubAgentGroupDefinition final {
    std::wstring groupId;
    std::wstring displayName;
    std::wstring description;
    std::vector<std::wstring> memberTargetIds;
    std::wstring updatedAtUtc;
};

struct ShellProviderAssignmentTarget final {
    std::wstring targetId;
    std::wstring kind;
    std::wstring displayName;
    std::wstring description;
    std::vector<std::wstring> memberTargetIds;
};

struct ShellProviderAssignment final {
    std::wstring targetId;
    std::wstring kind;
    std::wstring providerId;
    std::wstring updatedAtUtc;
};

struct ShellProviderExecutionRegistration final {
    std::wstring moduleId;
    std::wstring providerId;
    std::wstring kind;
    std::wstring displayName;
    std::wstring transport;
    bool supportsSharedMcpAccess = true;
    bool supportsDirectMcpConfig = false;
};

struct ShellProviderExecutionRequest final {
    std::wstring targetId;
    std::wstring prompt;
    bool allowToolAccess = true;
    int maxTurns = 4;
};

struct ShellProviderExecutionRecord final {
    std::wstring executionId;
    std::wstring targetId;
    std::wstring targetDisplayName;
    std::wstring providerId;
    std::wstring providerDisplayName;
    std::wstring status;
    std::wstring modelId;
    std::vector<std::wstring> referencedMcpServerIds;
    std::vector<std::wstring> toolEvents;
    std::wstring outputText;
    std::wstring rawResponse;
    std::wstring startedAtUtc;
    std::wstring completedAtUtc;
    std::wstring errorMessage;
};

struct ShellSecuritySettings final {
    bool enableTls = false;
    bool enableAuthentication = false;
    bool allowTroubleshootingBypass = false;
    bool allowOpenLanAccess = true;
    bool securityProtocolsEnabled = true;
    std::vector<std::wstring> trustedRemoteHosts;
};

struct ShellOperationResult final {
    bool succeeded = false;
    bool requiresConfirmation = false;
    std::wstring message;
};

struct ShellInstallerPackageSpec final {
    ShellInstallerKind kind = ShellInstallerKind::Exe;
    std::wstring source;
    std::wstring arguments;
    bool allowUntrustedExecution = false;
};

struct ShellBootstrapRepoSpec final {
    std::wstring repositoryUrl;
    std::wstring branch = L"main";
    std::wstring manifestFile = L"mcp-bootstrap.json";
    bool allowUntrustedExecution = false;
};

struct ShellZipBundleSpec final {
    std::wstring source;
    std::wstring manifestFile = L"mcp-bootstrap.json";
    bool allowUntrustedExecution = false;
};

struct ShellExportArtifact final {
    std::wstring id;
    std::wstring fileName;
    std::wstring mediaType;
    std::wstring content;
};

struct ShellExportFetchResult final {
    bool succeeded = false;
    std::wstring message;
    std::vector<ShellExportArtifact> artifacts;
};

struct ShellNavigationPointer final {
    std::wstring id;
    std::wstring label;
    std::wstring destinationId;
};

enum class ShellToolbarActionKind {
    Unknown,
    Navigate,
    OpenOverlay,
    PublishEvent
};

struct ShellToolbarItem final {
    std::wstring id;
    std::wstring title;
    std::wstring systemImageName;
    ShellToolbarActionKind actionKind = ShellToolbarActionKind::Unknown;
    std::wstring targetId;
};

enum class ShellOverlayPresentation {
    Sheet,
    FullScreen,
    Popover
};

struct ShellOverlayRoute final {
    std::wstring id;
    std::wstring label;
    ShellOverlayPresentation presentation = ShellOverlayPresentation::Sheet;
    bool targetsModuleView = false;
    std::wstring destinationId;
    std::wstring moduleId;
    std::wstring viewId;
};

struct ShellViewInjection final {
    std::wstring id;
    std::wstring slot;
    std::wstring viewId;
    int priority = 0;
};

struct ShellSnapshot final {
    ServiceState serviceState = ServiceState::Missing;
    uint32_t serviceProcessId = 0;
    bool apiHealthy = false;
    bool canStartService = true;
    bool canStopService = false;
    double cpuPercent = 0.0;
    double memoryPercent = 0.0;
    double diskPercent = 0.0;
    uint64_t bytesSentPerSecond = 0;
    uint64_t bytesReceivedPerSecond = 0;
    uint16_t browserPort = 7300;
    bool beaconEnabled = true;
    bool aiAutonomyEnabled = false;
    bool securityProtocolsEnabled = true;
    bool openLanAccess = true;
    int cpuAllocationPercent = 50;
    int memoryAllocationPercent = 50;
    int bandwidthAllocationPercent = 50;
    int storageAllocationPercent = 50;
    size_t endpointCount = 0;
    size_t providerCount = 0;
    size_t installCount = 0;
    size_t exportCount = 0;
    size_t governanceRoleCount = 0;
    size_t governanceRuleCount = 0;
    size_t governanceDocumentCount = 0;
    size_t governanceFindingCount = 0;
    std::wstring dashboardUrl;
    std::wstring configPath;
    std::wstring dataDirectory;
    std::wstring environmentName;
    std::wstring hostName;
    std::wstring operatingSystem;
    std::wstring primaryIpAddress;
    std::wstring primaryMacAddress;
    std::wstring bindAddress;
    std::wstring overviewText;
    std::wstring telemetryText;
    std::wstring environmentText;
    std::wstring configurationText;
    std::wstring governancePosture;
    std::wstring governanceDoctrine;
    std::wstring governanceNarrative;
    std::wstring governanceLastEvaluatedUtc;
    ShellSecuritySettings securitySettings;
    std::vector<ShellRuntimeEndpoint> endpoints;
    std::vector<ShellProviderConnection> providers;
    std::vector<ShellProviderCapability> providerCapabilities;
    std::vector<ShellProviderCredentialStatus> providerCredentialStatuses;
    std::vector<ShellSubAgentGroupDefinition> subAgentGroups;
    std::vector<ShellProviderAssignmentTarget> providerAssignmentTargets;
    std::vector<ShellProviderAssignment> providerAssignments;
    std::vector<ShellProviderExecutionRegistration> providerExecutionRegistrations;
    std::vector<ShellProviderExecutionRecord> providerExecutionHistory;
    std::vector<ShellNavigationPointer> navigationPointers;
    std::vector<ShellToolbarItem> toolbarItems;
    std::vector<ShellOverlayRoute> overlayRoutes;
    std::map<std::wstring, std::vector<ShellViewInjection>> viewInjectionsBySlot;
    std::vector<std::wstring> endpointRows;
    std::vector<std::wstring> providerRows;
    std::vector<std::wstring> providerCapabilityRows;
    std::vector<std::wstring> providerAssignmentRows;
    std::vector<std::wstring> installRows;
    std::vector<std::wstring> exportRows;
    std::vector<std::wstring> governanceFindingRows;
    std::vector<std::wstring> governanceRoleRows;
    std::vector<std::wstring> governanceRuleRows;
    std::vector<std::wstring> governanceDocumentRows;
    std::vector<std::wstring> governanceActionRows;
    std::wstring statusMessage;
};

class ShellRuntime final {
public:
    ShellRuntime() = default;

    [[nodiscard]] ShellSnapshot CaptureSnapshot() const;
    [[nodiscard]] bool StartService(std::wstring& message) const;
    [[nodiscard]] bool StopService(std::wstring& message) const;
    [[nodiscard]] ShellOperationResult UpsertSubAgent(const ShellRuntimeEndpoint& subAgent) const;
    [[nodiscard]] ShellOperationResult RemoveSubAgent(const std::wstring& subAgentId) const;
    [[nodiscard]] ShellOperationResult UpsertProvider(const ShellProviderConnection& provider) const;
    [[nodiscard]] ShellOperationResult UpsertProviderCredentials(
        const std::wstring& providerId,
        const std::vector<std::pair<std::wstring, std::wstring>>& values) const;
    [[nodiscard]] ShellOperationResult UpsertSubAgentGroup(const ShellSubAgentGroupDefinition& group) const;
    [[nodiscard]] ShellOperationResult RemoveSubAgentGroup(const std::wstring& groupId) const;
    [[nodiscard]] ShellOperationResult UpsertProviderAssignment(const ShellProviderAssignment& assignment) const;
    [[nodiscard]] ShellProviderExecutionRecord ExecuteProviderTask(const ShellProviderExecutionRequest& request) const;
    [[nodiscard]] ShellOperationResult UpdateAiAutonomyEnabled(bool enabled) const;
    [[nodiscard]] ShellOperationResult UpdateSecuritySettings(const ShellSecuritySettings& settings,
                                                             bool confirmUnsafeChanges) const;
    [[nodiscard]] ShellOperationResult InstallPackage(const ShellInstallerPackageSpec& spec) const;
    [[nodiscard]] ShellOperationResult InstallRepository(const ShellBootstrapRepoSpec& spec) const;
    [[nodiscard]] ShellOperationResult InstallZipBundle(const ShellZipBundleSpec& spec) const;
    [[nodiscard]] ShellExportFetchResult FetchExports() const;
    [[nodiscard]] ShellOperationResult MaterializeExports(const std::vector<std::wstring>& artifactIds) const;
    void OpenDashboard(const ShellSnapshot& snapshot) const;
    void OpenConfig() const;
    void OpenDataDirectory() const;
    void OpenExportsDirectory() const;

private:
    [[nodiscard]] std::filesystem::path ResolveDataDirectory() const;
    [[nodiscard]] std::filesystem::path ResolveConfigurationFile() const;
    [[nodiscard]] std::filesystem::path ResolveExportsDirectory() const;
};

} // namespace MasterControlShell
