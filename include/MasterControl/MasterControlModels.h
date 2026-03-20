// Master Control Program
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#pragma once

#include "ForsettiCore/UIModels.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace MasterControl {

enum class EndpointKind {
    Host,
    Gateway,
    MCPServer,
    SubAgent,
    BrowserGateway,
    Beacon
};

enum class EndpointStatus {
    Unknown,
    Online,
    Offline,
    Degraded
};

enum class ProviderKind {
    Codex,
    ClaudeCode,
    OpenAI,
    XAI,
    Generic
};

enum class ProviderCredentialFieldKind {
    ApiKey,
    AuthToken,
    Model,
    Text
};

enum class ProviderAssignmentTargetKind {
    Role,
    SubAgentGroup,
    SubAgent
};

enum class ProviderExecutionStatus {
    Pending,
    Running,
    Succeeded,
    Failed
};

enum class ProviderExecutionTransport {
    OpenAICompatibleChat,
    ClaudeCodeCli
};

enum class InstallerKind {
    Msi,
    Exe,
    PowerShell,
    GitBootstrapRepo,
    ZipBundle
};

enum class ControlSurfaceToolbarAction {
    Navigate,
    OpenOverlay
};

struct HostTelemetrySnapshot final {
    double cpuPercent = 0.0;
    double memoryPercent = 0.0;
    double diskPercent = 0.0;
    uint64_t totalMemoryBytes = 0;
    uint64_t freeMemoryBytes = 0;
    uint64_t totalDiskBytes = 0;
    uint64_t freeDiskBytes = 0;
    uint64_t bytesSentPerSecond = 0;
    uint64_t bytesReceivedPerSecond = 0;
    std::string hostName;
    std::string primaryIpAddress;
    std::string primaryMacAddress;
    std::string operatingSystem;
    std::string capturedAtUtc;
};

struct RuntimeEndpoint final {
    std::string id;
    std::string displayName;
    EndpointKind kind = EndpointKind::MCPServer;
    std::string host;
    uint16_t port = 0;
    std::string protocol = "http";
    EndpointStatus status = EndpointStatus::Unknown;
    std::string description;
    std::string lastCheckedUtc;
    std::string routePath;
};

struct ManagedNodeProfile final {
    std::string environmentName;
    std::string preferredBindAddress;
    std::string macAddress;
    std::vector<RuntimeEndpoint> seededEndpoints;
};

struct SecuritySettings final {
    bool enableTls = false;
    bool enableAuthentication = false;
    bool allowTroubleshootingBypass = false;
    bool allowOpenLanAccess = true;
    bool securityProtocolsEnabled = true;
    std::vector<std::string> trustedRemoteHosts;
};

struct ResourceAllocationProfile final {
    int cpuPercent = 50;
    int memoryPercent = 50;
    int bandwidthPercent = 50;
    int storagePercent = 50;
};

struct ProviderConnection final {
    std::string id;
    ProviderKind kind = ProviderKind::Generic;
    std::string displayName;
    std::string baseUrl;
    std::string modelId;
    bool enabled = true;
    bool allowAutonomousControl = false;
    bool credentialsConfigured = false;
};

struct ProviderCredentialFieldDescriptor final {
    std::string fieldId;
    std::string label;
    ProviderCredentialFieldKind kind = ProviderCredentialFieldKind::Text;
    std::string helpText;
    std::string placeholder;
    std::string environmentVariableHint;
    std::string requirementGroup;
    bool secret = false;
    bool required = false;
};

struct ProviderCapabilityDescriptor final {
    std::string moduleId;
    std::string providerId;
    ProviderKind kind = ProviderKind::Generic;
    std::string displayName;
    std::string description;
    std::string defaultBaseUrl;
    std::string recommendedModel;
    std::vector<ProviderCredentialFieldDescriptor> credentialFields;
    std::vector<std::string> runtimeRequirements;
    std::vector<std::string> supportedTargets;
    bool supportsSharedMcpAccess = true;
    bool supportsAutonomousControl = true;
};

struct ProviderCredentialStatus final {
    std::string providerId;
    bool configured = false;
    std::vector<std::string> configuredFieldIds;
    std::string updatedAtUtc;
    std::string message;
};

struct ProviderCredentialUpdate final {
    std::string providerId;
    std::map<std::string, std::string> values;
};

struct SubAgentGroupDefinition final {
    std::string groupId;
    std::string displayName;
    std::string description;
    std::vector<std::string> memberTargetIds;
    std::string updatedAtUtc;
};

struct SubAgentGroupRemovalRequest final {
    std::string groupId;
};

struct ProviderAssignmentTarget final {
    std::string targetId;
    ProviderAssignmentTargetKind kind = ProviderAssignmentTargetKind::Role;
    std::string displayName;
    std::string description;
    std::vector<std::string> memberTargetIds;
};

struct ProviderAssignment final {
    std::string targetId;
    ProviderAssignmentTargetKind kind = ProviderAssignmentTargetKind::Role;
    std::string providerId;
    std::string updatedAtUtc;
    std::string sourceGroupId;
};

struct ProviderExecutionRegistration final {
    std::string moduleId;
    std::string providerId;
    ProviderKind kind = ProviderKind::Generic;
    std::string displayName;
    ProviderExecutionTransport transport = ProviderExecutionTransport::OpenAICompatibleChat;
    bool supportsSharedMcpAccess = true;
    bool supportsDirectMcpConfig = false;
};

struct ProviderExecutionRequest final {
    std::string targetId;
    std::string prompt;
    std::vector<std::string> preferredMcpServerIds;
    std::string workingDirectory;
    bool allowToolAccess = true;
    int maxTurns = 4;
};

struct ProviderExecutionRecord final {
    std::string executionId;
    std::string targetId;
    std::string targetDisplayName;
    std::string providerId;
    ProviderKind providerKind = ProviderKind::Generic;
    std::string providerDisplayName;
    ProviderExecutionStatus status = ProviderExecutionStatus::Pending;
    std::string modelId;
    std::vector<std::string> referencedMcpServerIds;
    std::vector<std::string> toolEvents;
    std::string outputText;
    std::string rawResponse;
    std::string startedAtUtc;
    std::string completedAtUtc;
    std::string errorMessage;
};

struct InstallerPackageSpec final {
    InstallerKind kind = InstallerKind::Exe;
    std::string source;
    std::string localPath;
    std::string arguments;
    bool allowUntrustedExecution = false;
};

struct BootstrapRepoSpec final {
    std::string repositoryUrl;
    std::string branch = "main";
    std::string manifestFile = "mcp-bootstrap.json";
    bool allowUntrustedExecution = false;
};

struct ZipBundleSpec final {
    std::string source;
    std::string manifestFile = "mcp-bootstrap.json";
    bool allowUntrustedExecution = false;
};

struct PackageTrustDecision final {
    bool isTrusted = false;
    bool requiresExplicitApproval = true;
    std::string reason;
};

struct ExportArtifact final {
    std::string id;
    std::string fileName;
    std::string mediaType;
    std::string content;
};

struct GovernanceDocument final {
    std::string id;
    std::string title;
    std::string category;
    std::string summary;
    std::string content;
};

struct GovernanceRole final {
    std::string roleId;
    std::string name;
    std::string authorityLevel;
    std::vector<std::string> responsibilities;
    std::vector<std::string> forbiddenActions;
    std::vector<std::string> requiredOutputs;
};

struct GovernanceRule final {
    std::string ruleId;
    std::string title;
    std::string severity;
    std::string failureConsequence;
    std::string description;
};

struct GovernanceFinding final {
    std::string ruleId;
    std::string title;
    std::string severity;
    std::string status;
    std::string message;
};

struct GovernanceProfile final {
    std::string unitName;
    std::string doctrine;
    std::vector<GovernanceDocument> documents;
    std::vector<GovernanceRole> roles;
    std::vector<GovernanceRule> rules;
    std::vector<std::string> operatorChecklist;
};

struct GovernanceSnapshot final {
    std::string unitName;
    std::string posture;
    std::string doctrine;
    std::string lastEvaluatedUtc;
    std::vector<GovernanceDocument> documents;
    std::vector<GovernanceRole> roles;
    std::vector<GovernanceRule> rules;
    std::vector<GovernanceFinding> findings;
    std::vector<std::string> operatorChecklist;
    std::vector<std::string> recommendedActions;
};

struct ModuleControlSurfaceRequest final {
    std::string moduleId;
    std::string featureId;
    std::string displayName;
    std::string destinationId;
    std::string surfaceTemplateId;
    std::string toolbarIcon;
    std::string overlayRouteId;
    ControlSurfaceToolbarAction toolbarAction = ControlSurfaceToolbarAction::Navigate;
    Forsetti::OverlayPresentation overlayPresentation = Forsetti::OverlayPresentation::Sheet;
    bool includeToolbarShortcut = true;
    bool includeNavigationLane = true;
    int sortOrder = 100;
};

struct ForsettiSurfaceSnapshot final {
    std::optional<Forsetti::ThemeMask> themeMask;
    std::vector<Forsetti::ToolbarItemDescriptor> toolbarItems;
    std::map<std::string, std::vector<Forsetti::ViewInjectionDescriptor>> viewInjectionsBySlot;
    std::optional<Forsetti::OverlaySchema> overlaySchema;
    std::vector<ModuleControlSurfaceRequest> registeredControlSurfaceRequests;
    std::string publishedByModuleId;
    std::string publishedAtUtc;
};

struct OperationResult final {
    bool succeeded = false;
    bool requiresConfirmation = false;
    std::string message;
};

struct InstallProvenance final {
    InstallerKind kind = InstallerKind::Exe;
    std::string source;
    std::string installedAtUtc;
    std::string version;
    bool trusted = false;
    std::string executionSummary;
};

struct BeaconAdvertisement final {
    std::string instanceName;
    std::string hostName;
    std::string ipAddress;
    uint16_t browserPort = 0;
    uint16_t gatewayPort = 0;
    std::string status = "online";
};

struct DashboardSnapshot final {
    HostTelemetrySnapshot telemetry;
    std::vector<RuntimeEndpoint> endpoints;
    std::vector<ProviderConnection> providers;
    std::vector<ProviderCapabilityDescriptor> providerCapabilities;
    std::vector<ProviderCredentialStatus> providerCredentialStatuses;
    std::vector<SubAgentGroupDefinition> subAgentGroups;
    std::vector<ProviderAssignmentTarget> providerAssignmentTargets;
    std::vector<ProviderAssignment> providerAssignments;
    std::vector<ProviderExecutionRegistration> providerExecutionRegistrations;
    std::vector<ProviderExecutionRecord> providerExecutionHistory;
    ResourceAllocationProfile resourceAllocation;
    SecuritySettings security;
    std::vector<InstallProvenance> installHistory;
    std::vector<ExportArtifact> exports;
    GovernanceSnapshot governance;
    ForsettiSurfaceSnapshot surface;
};

struct AppConfiguration final {
    std::string instanceName = "Master Control Program";
    std::string bindAddress = "0.0.0.0";
    uint16_t browserPort = 7300;
    uint16_t beaconPort = 7301;
    uint16_t beaconBroadcastIntervalSeconds = 15;
    bool beaconEnabled = true;
    bool aiAutonomyEnabled = false;
    SecuritySettings security;
    ResourceAllocationProfile resourceAllocation;
    std::vector<ProviderConnection> providers;
    std::vector<SubAgentGroupDefinition> subAgentGroups;
    std::vector<ProviderAssignment> providerAssignments;
    ManagedNodeProfile activeProfile;
};

std::string to_string(EndpointKind value);
std::string to_string(EndpointStatus value);
std::string to_string(ProviderKind value);
std::string to_string(ProviderCredentialFieldKind value);
std::string to_string(ProviderAssignmentTargetKind value);
std::string to_string(ProviderExecutionStatus value);
std::string to_string(ProviderExecutionTransport value);
std::string to_string(InstallerKind value);
std::string to_string(ControlSurfaceToolbarAction value);

EndpointKind endpointKindFromString(const std::string& value);
EndpointStatus endpointStatusFromString(const std::string& value);
ProviderKind providerKindFromString(const std::string& value);
ProviderCredentialFieldKind providerCredentialFieldKindFromString(const std::string& value);
ProviderAssignmentTargetKind providerAssignmentTargetKindFromString(const std::string& value);
ProviderExecutionStatus providerExecutionStatusFromString(const std::string& value);
ProviderExecutionTransport providerExecutionTransportFromString(const std::string& value);
InstallerKind installerKindFromString(const std::string& value);
ControlSurfaceToolbarAction controlSurfaceToolbarActionFromString(const std::string& value);

void to_json(nlohmann::json& json, EndpointKind value);
void from_json(const nlohmann::json& json, EndpointKind& value);

void to_json(nlohmann::json& json, EndpointStatus value);
void from_json(const nlohmann::json& json, EndpointStatus& value);

void to_json(nlohmann::json& json, ProviderKind value);
void from_json(const nlohmann::json& json, ProviderKind& value);

void to_json(nlohmann::json& json, ProviderCredentialFieldKind value);
void from_json(const nlohmann::json& json, ProviderCredentialFieldKind& value);

void to_json(nlohmann::json& json, ProviderAssignmentTargetKind value);
void from_json(const nlohmann::json& json, ProviderAssignmentTargetKind& value);

void to_json(nlohmann::json& json, ProviderExecutionStatus value);
void from_json(const nlohmann::json& json, ProviderExecutionStatus& value);

void to_json(nlohmann::json& json, ProviderExecutionTransport value);
void from_json(const nlohmann::json& json, ProviderExecutionTransport& value);

void to_json(nlohmann::json& json, InstallerKind value);
void from_json(const nlohmann::json& json, InstallerKind& value);

void to_json(nlohmann::json& json, ControlSurfaceToolbarAction value);
void from_json(const nlohmann::json& json, ControlSurfaceToolbarAction& value);

std::string toPrettyJson(const nlohmann::json& json);
std::string timestampNowUtc();

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    HostTelemetrySnapshot,
    cpuPercent,
    memoryPercent,
    diskPercent,
    totalMemoryBytes,
    freeMemoryBytes,
    totalDiskBytes,
    freeDiskBytes,
    bytesSentPerSecond,
    bytesReceivedPerSecond,
    hostName,
    primaryIpAddress,
    primaryMacAddress,
    operatingSystem,
    capturedAtUtc)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    RuntimeEndpoint,
    id,
    displayName,
    kind,
    host,
    port,
    protocol,
    status,
    description,
    lastCheckedUtc,
    routePath)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    ManagedNodeProfile,
    environmentName,
    preferredBindAddress,
    macAddress,
    seededEndpoints)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    SecuritySettings,
    enableTls,
    enableAuthentication,
    allowTroubleshootingBypass,
    allowOpenLanAccess,
    securityProtocolsEnabled,
    trustedRemoteHosts)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    ResourceAllocationProfile,
    cpuPercent,
    memoryPercent,
    bandwidthPercent,
    storagePercent)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    ProviderConnection,
    id,
    kind,
    displayName,
    baseUrl,
    modelId,
    enabled,
    allowAutonomousControl,
    credentialsConfigured)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    ProviderCredentialFieldDescriptor,
    fieldId,
    label,
    kind,
    helpText,
    placeholder,
    environmentVariableHint,
    requirementGroup,
    secret,
    required)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    ProviderCapabilityDescriptor,
    moduleId,
    providerId,
    kind,
    displayName,
    description,
    defaultBaseUrl,
    recommendedModel,
    credentialFields,
    runtimeRequirements,
    supportedTargets,
    supportsSharedMcpAccess,
    supportsAutonomousControl)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    ProviderCredentialStatus,
    providerId,
    configured,
    configuredFieldIds,
    updatedAtUtc,
    message)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    ProviderCredentialUpdate,
    providerId,
    values)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    SubAgentGroupDefinition,
    groupId,
    displayName,
    description,
    memberTargetIds,
    updatedAtUtc)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    SubAgentGroupRemovalRequest,
    groupId)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    ProviderAssignmentTarget,
    targetId,
    kind,
    displayName,
    description,
    memberTargetIds)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    ProviderAssignment,
    targetId,
    kind,
    providerId,
    updatedAtUtc,
    sourceGroupId)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    ProviderExecutionRegistration,
    moduleId,
    providerId,
    kind,
    displayName,
    transport,
    supportsSharedMcpAccess,
    supportsDirectMcpConfig)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    ProviderExecutionRequest,
    targetId,
    prompt,
    preferredMcpServerIds,
    workingDirectory,
    allowToolAccess,
    maxTurns)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    ProviderExecutionRecord,
    executionId,
    targetId,
    targetDisplayName,
    providerId,
    providerKind,
    providerDisplayName,
    status,
    modelId,
    referencedMcpServerIds,
    toolEvents,
    outputText,
    rawResponse,
    startedAtUtc,
    completedAtUtc,
    errorMessage)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    InstallerPackageSpec,
    kind,
    source,
    localPath,
    arguments,
    allowUntrustedExecution)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    BootstrapRepoSpec,
    repositoryUrl,
    branch,
    manifestFile,
    allowUntrustedExecution)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    ZipBundleSpec,
    source,
    manifestFile,
    allowUntrustedExecution)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    PackageTrustDecision,
    isTrusted,
    requiresExplicitApproval,
    reason)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    ExportArtifact,
    id,
    fileName,
    mediaType,
    content)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    GovernanceDocument,
    id,
    title,
    category,
    summary,
    content)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    GovernanceRole,
    roleId,
    name,
    authorityLevel,
    responsibilities,
    forbiddenActions,
    requiredOutputs)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    GovernanceRule,
    ruleId,
    title,
    severity,
    failureConsequence,
    description)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    GovernanceFinding,
    ruleId,
    title,
    severity,
    status,
    message)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    GovernanceProfile,
    unitName,
    doctrine,
    documents,
    roles,
    rules,
    operatorChecklist)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    GovernanceSnapshot,
    unitName,
    posture,
    doctrine,
    lastEvaluatedUtc,
    documents,
    roles,
    rules,
    findings,
    operatorChecklist,
    recommendedActions)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    ModuleControlSurfaceRequest,
    moduleId,
    featureId,
    displayName,
    destinationId,
    surfaceTemplateId,
    toolbarIcon,
    overlayRouteId,
    toolbarAction,
    overlayPresentation,
    includeToolbarShortcut,
    includeNavigationLane,
    sortOrder)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    ForsettiSurfaceSnapshot,
    themeMask,
    toolbarItems,
    viewInjectionsBySlot,
    overlaySchema,
    registeredControlSurfaceRequests,
    publishedByModuleId,
    publishedAtUtc)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    OperationResult,
    succeeded,
    requiresConfirmation,
    message)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    InstallProvenance,
    kind,
    source,
    installedAtUtc,
    version,
    trusted,
    executionSummary)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    BeaconAdvertisement,
    instanceName,
    hostName,
    ipAddress,
    browserPort,
    gatewayPort,
    status)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    DashboardSnapshot,
    telemetry,
    endpoints,
    providers,
    providerCapabilities,
    providerCredentialStatuses,
    subAgentGroups,
    providerAssignmentTargets,
    providerAssignments,
    providerExecutionRegistrations,
    providerExecutionHistory,
    resourceAllocation,
    security,
    installHistory,
    exports,
    governance,
    surface)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    AppConfiguration,
    instanceName,
    bindAddress,
    browserPort,
    beaconPort,
    beaconBroadcastIntervalSeconds,
    beaconEnabled,
    aiAutonomyEnabled,
    security,
    resourceAllocation,
    providers,
    subAgentGroups,
    providerAssignments,
    activeProfile)

} // namespace MasterControl
