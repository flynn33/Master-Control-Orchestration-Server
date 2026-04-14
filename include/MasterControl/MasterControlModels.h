// Master Control Orchestration Server
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
    Degraded,
    Template
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

enum class PlatformTarget {
    Windows,
    MacOS,
    IOS,
    Unknown
};

enum class AppleRemoteTransport {
    Unknown,
    Ssh,
    CompanionService
};

enum class GovernanceToolStatus {
    Passed,
    Warning,
    Failed,
    Unsupported
};

enum class AppleOperationStatus {
    Queued,
    Running,
    Succeeded,
    Failed,
    Blocked,
    Canceled
};

enum class GovernanceActionKind {
    Unknown,
    ProviderExecution,
    ProviderAutonomyEnable,
    RemoteInstall
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
    std::string specialization;
    bool userDefined = false;
    bool isTemplate = false;
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
    bool isTemplate = false;
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

struct SubAgentRemovalRequest final {
    std::string subAgentId;
};

struct McpServerRemovalRequest final {
    std::string mcpServerId;
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

// ---------------------------------------------------------------------------
// Auto-Connect AI Model
// ---------------------------------------------------------------------------
// The user-facing surface for adding an AI model is reduced to:
//   (1) pick a provider kind, (2) enter credentials, (3) choose role targets.
// Everything else — route id generation, display name, base URL, recommended
// model selection, HTTP connectivity probe, remote model discovery, credential
// encryption, provider registration, and role fan-out — is performed by the
// runtime via AutoConnectProvider(). The result reports every step so the
// shell and browser surfaces can render a transparent progress log.
// ---------------------------------------------------------------------------

struct AutoConnectRequest final {
    ProviderKind kind = ProviderKind::Generic;
    std::string providerId;
    std::map<std::string, std::string> credentials;
    // Optional overrides — leave empty to use capability defaults.
    std::string displayNameOverride;
    std::string baseUrlOverride;
    std::string modelIdOverride;
    // Role/group/sub-agent targets to assign this provider to on creation.
    // Each entry must be a valid ProviderAssignmentTarget::targetId.
    std::vector<std::string> assignmentTargetIds;
    bool allowAutonomousControl = false;
    // When true, the runtime will call the provider's models endpoint to
    // discover available models. When false, capability.recommendedModel is
    // used directly without a network call.
    bool discoverModels = true;
};

struct DiscoveredModel final {
    std::string id;
    std::string displayName;
    std::string description;
};

struct AutoConnectStep final {
    std::string stage;       // e.g. "resolve-capability", "probe", "discover-models"
    bool succeeded = false;
    std::string message;
    int latencyMs = 0;
};

struct AutoConnectResult final {
    bool succeeded = false;
    std::string providerId;
    std::string displayName;
    std::string baseUrl;
    std::string selectedModelId;
    std::vector<DiscoveredModel> discoveredModels;
    std::vector<AutoConnectStep> steps;
    std::vector<std::string> assignmentsApplied;
    std::vector<std::string> assignmentsFailed;
    int totalLatencyMs = 0;
    std::string errorMessage;
    std::string summary; // human-readable one-liner
};

// ---------------------------------------------------------------------------
// First-Run Setup & Readiness
// ---------------------------------------------------------------------------
// ReadinessSnapshot is the runtime-backed state model consumed by both the
// browser and the shell to drive first-run routing, readiness review, and
// "Fix now" remediation buttons. Workflow readiness is source-neutral: a
// manually created workflow satisfies the readiness rule exactly as a
// starter template instantiated by the wizard would.
// ---------------------------------------------------------------------------

struct ReadinessIssue final {
    std::string id;                     // stable issue id (e.g. "providers.none-ready")
    std::string category;               // "providers" | "mcp" | "workflows" | "specialists"
    std::string severity;               // "info" | "warning" | "blocking"
    std::string title;                  // plain-language short phrase
    std::string detail;                 // one-sentence explanation
    std::string remediationDestination; // e.g. "providers" | "runtime" | "setup/providers"
    std::string remediationLabel;       // button label e.g. "Connect a provider"
};

struct ReadinessSnapshot final {
    bool setupStarted = false;
    bool firstRunCompleted = false;
    int providersReadyCount = 0;
    int providersMissingCount = 0;
    int mcpReadyCount = 0;
    int mcpMissingCount = 0;
    int workflowsReadyCount = 0;
    int workflowsMissingCount = 0;
    int specialistsReadyCount = 0;
    int specialistsMissingCount = 0;
    std::vector<ReadinessIssue> blockingIssues;
    // "connect-first-provider" | "add-mcp" | "create-specialist" |
    // "create-starter-workflow" | "review" | "complete"
    std::string recommendedNextStep;
    std::string updatedAtUtc;
};

// ---------------------------------------------------------------------------
// Setup Dependencies (WS4 Provider Install Automation)
// ---------------------------------------------------------------------------
// Used for provider-ecosystem dependency orchestration such as the Claude
// Code CLI. Each dependency follows an explicit three-branch preflight:
//   ready           — the dependency is installed and callable
//   installable     — the dependency is missing but its prerequisite (npm)
//                     is present, so an install command can run safely
//   prerequisite-missing — the prerequisite itself (e.g. Node.js/npm) is
//                     missing; no install command is ever attempted
// ---------------------------------------------------------------------------

struct SupportedDependency final {
    std::string id;                 // stable id (e.g. "claude-code-cli")
    std::string displayName;        // user-facing name
    std::string description;        // short sentence explaining purpose
    std::string detectCommand;      // commandline that reports version on stdout (exit 0)
    std::string installMethod;      // install commandline (documented to user)
    std::string docsUrl;            // manual-install fallback link
    bool requiresElevation = false; // informational only — runtime never auto-elevates
    int installTimeoutSeconds = 300;
};

struct DependencyDetection final {
    std::string id;
    std::string state;              // enum-as-string: see DependencyState below
    std::string preflight;          // "ready" | "installable" | "prerequisite-missing"
    std::string detectedVersion;
    std::string detail;             // plain-language explanation / remediation hint
    std::string detectedAtUtc;
};

struct DependencyInstallResult final {
    std::string id;
    bool succeeded = false;
    std::string finalState;         // "ready" | "failed" | "manual-action-required"
    std::string summary;            // user-facing one-liner
    std::string stdoutTail;         // last ~2KB of install stdout (for diagnostics)
    std::string stderrTail;         // last ~2KB of install stderr
    int exitCode = 0;
    int totalLatencyMs = 0;
    DependencyDetection postInstallDetection;
};

// ---------------------------------------------------------------------------
// Starter Workflow Templates (WS6)
// ---------------------------------------------------------------------------

struct StarterWorkflowTemplate final {
    std::string id;
    std::string displayName;
    std::string description;
    int requiresProviders = 0;
    int requiresMcp = 0;
    int requiresSpecialists = 0;
};

struct StarterWorkflowInstantiateRequest final {
    std::string displayNameOverride;
};

struct StarterWorkflowInstantiateResult final {
    bool succeeded = false;
    std::string workflowId;
    std::string message;
};

// ---------------------------------------------------------------------------
// Live Activity Stream
// ---------------------------------------------------------------------------
// Every incoming admin API request, outgoing provider execution, governance
// decision, and auto-connect step is appended to an in-memory ring buffer
// (ActivityEventRing) so the shell can render a live command/request stream.
// Events are also surfaced via GET /api/activity so the browser dashboard and
// remote observers can subscribe.
// ---------------------------------------------------------------------------

enum class ActivityEventKind {
    AdminApiRequest,
    ProviderExecution,
    GovernanceDecision,
    AutoConnect,
    ServiceLifecycle,
    Telemetry
};

struct ActivityEvent final {
    std::string id;                 // monotonically increasing event id (string for JS safety)
    std::string kind;               // enum-as-string for JSON: "admin_api_request", etc.
    std::string timestampUtc;
    std::string actor;              // "shell", "dashboard", "cli", etc.
    std::string method;             // HTTP verb or operation verb
    std::string target;             // path, provider id, rule id, etc.
    int statusCode = 0;             // HTTP status or custom numeric outcome
    int latencyMs = 0;
    std::string message;            // short human-readable summary
    std::string detail;             // optional expanded payload (JSON-as-string)
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

struct AppleToolchainState final {
    bool reachable = false;
    bool xcodeInstalled = false;
    std::string xcodeVersion;
    std::string developerDirectory;
    bool macosSdkAvailable = false;
    bool iosSdkAvailable = false;
    bool simulatorControlAvailable = false;
    bool deviceControlAvailable = false;
    std::vector<std::string> simulatorRuntimes;
    std::string status = "unknown";
    std::string message;
    std::string checkedAtUtc;
};

struct AppleSigningState final {
    bool signingReady = false;
    bool developmentSigningReady = false;
    bool distributionSigningReady = false;
    std::vector<std::string> availableTeams;
    std::string status = "unknown";
    std::string message;
};

struct AppleRemoteHost final {
    std::string hostId;
    std::string displayName;
    AppleRemoteTransport transport = AppleRemoteTransport::Unknown;
    std::vector<PlatformTarget> platforms;
    std::string address;
    uint16_t port = 0;
    std::string username;
    std::string serviceBaseUrl;
    std::string companionHealthPath = "/healthz";
    std::string companionExecutePath = "/execute";
    std::string preferredDeveloperDirectory;
    std::string defaultSigningIdentity;
    std::string defaultNotaryKeychainProfile;
    std::string defaultNotaryTeamId;
    bool enabled = true;
    AppleToolchainState toolchain;
    AppleSigningState signing;
    std::string transportSummary;
    std::string credentialProfileSummary;
    std::vector<std::string> readinessIssues;
};

struct AppleRemoteHostRemovalRequest final {
    std::string hostId;
};

struct AppleRemoteCommandRequest final {
    std::string executable;
    std::vector<std::string> arguments;
    std::string workingDirectory;
    std::map<std::string, std::string> environment;
    int timeoutSeconds = 900;
};

struct AppleRemoteCommandResult final {
    std::string hostId;
    AppleRemoteTransport transport = AppleRemoteTransport::Unknown;
    bool launched = false;
    bool succeeded = false;
    int exitCode = -1;
    std::string stdoutText;
    std::string stderrText;
    std::string rawResponse;
    std::string errorMessage;
};

struct PlatformGatewayDescriptor final {
    std::string moduleId;
    std::string serviceId;
    PlatformTarget platform = PlatformTarget::Unknown;
    std::string displayName;
    std::string serviceType;
    std::string instanceLabel;
    std::string hostName;
    std::string ipAddress;
    uint16_t port = 0;
    std::string gatewayPath;
    std::string configPath;
    std::map<std::string, std::string> properties;
    std::string status = "unknown";
    bool lanAdvertisementEnabled = true;
};

struct GovernanceServerDescriptor final {
    std::string moduleId;
    std::string serviceId;
    PlatformTarget platform = PlatformTarget::Unknown;
    std::string displayName;
    std::string gatewayServiceId;
    std::string routePath;
    std::vector<std::string> toolIds;
    bool requiresRemoteToolchain = false;
    std::string status = "unknown";
};

struct GovernanceToolDescriptor final {
    std::string moduleId;
    std::string serviceId;
    PlatformTarget platform = PlatformTarget::Unknown;
    std::string toolId;
    std::string displayName;
    std::string description;
    bool requiresRemoteToolchain = false;
};

struct GovernanceToolRequest final {
    PlatformTarget platform = PlatformTarget::Unknown;
    std::string toolId;
    std::string targetPath;
    std::map<std::string, std::string> options;
};

struct GovernanceToolResult final {
    PlatformTarget platform = PlatformTarget::Unknown;
    std::string toolId;
    std::string displayName;
    GovernanceToolStatus status = GovernanceToolStatus::Failed;
    bool succeeded = false;
    std::string summary;
    std::vector<GovernanceFinding> findings;
    std::string rawOutput;
    std::string startedAtUtc;
    std::string completedAtUtc;
    std::string routeReason;
    std::string diagnosticSummary;
    std::vector<std::string> readinessIssues;
};

struct AppleOperationRecord final {
    std::string operationId;
    PlatformTarget platform = PlatformTarget::Unknown;
    std::string toolId;
    std::string displayName;
    std::string hostId;
    std::string hostDisplayName;
    AppleRemoteTransport transport = AppleRemoteTransport::Unknown;
    AppleOperationStatus status = AppleOperationStatus::Queued;
    std::string workingDirectory;
    std::string artifactPath;
    std::string targetPath;
    std::string summary;
    std::string rawOutput;
    std::string errorMessage;
    std::string queuedAtUtc;
    std::string startedAtUtc;
    std::string completedAtUtc;
    std::map<std::string, std::string> requestOptions;
    std::string routeReason;
    std::string diagnosticSummary;
    std::string selectedDeveloperDirectory;
    std::string credentialProfileSummary;
    std::vector<std::string> readinessIssues;
    std::vector<std::string> redactedRequestOptionKeys;
    bool rerunReady = false;
    std::string rerunReadinessMessage;
};

struct AppleOperationCancelRequest final {
    std::string operationId;
};

struct GovernanceEnforcementRequest final {
    GovernanceActionKind action = GovernanceActionKind::Unknown;
    std::string targetId;
    std::string providerId;
    std::string source;
    bool allowUntrustedExecution = false;
};

struct GovernanceEnforcementDecision final {
    GovernanceActionKind action = GovernanceActionKind::Unknown;
    bool allowed = true;
    std::string posture = "pass";
    std::string message;
    std::string ruleId;
    std::vector<std::string> blockingFindings;
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
    std::vector<AppleRemoteHost> appleRemoteHosts;
    std::vector<PlatformGatewayDescriptor> platformGateways;
    std::vector<GovernanceServerDescriptor> governanceServers;
    std::vector<GovernanceToolDescriptor> availableTools;
    std::vector<GovernanceToolResult> recentExecutions;
    std::vector<AppleOperationRecord> appleOperations;
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
    std::vector<PlatformGatewayDescriptor> platformGateways;
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
    std::vector<AppleRemoteHost> appleRemoteHosts;
    std::vector<PlatformGatewayDescriptor> platformGateways;
    std::vector<GovernanceServerDescriptor> governanceServers;
    ForsettiSurfaceSnapshot surface;
};

struct AppConfiguration final {
    std::string instanceName = "Master Control Orchestration Server";
    std::string bindAddress = "0.0.0.0";
    uint16_t browserPort = 7300;
    uint16_t beaconPort = 7301;
    uint16_t beaconBroadcastIntervalSeconds = 15;
    bool beaconEnabled = true;
    bool aiAutonomyEnabled = false;
    bool advancedMode = false;
    bool firstRunCompleted = false;
    std::string firstRunStartedAtUtc;
    std::string firstRunCompletedAtUtc;
    std::vector<std::string> firstRunSkippedSteps;
    SecuritySettings security;
    ResourceAllocationProfile resourceAllocation;
    std::vector<ProviderConnection> providers;
    std::vector<SubAgentGroupDefinition> subAgentGroups;
    std::vector<ProviderAssignment> providerAssignments;
    std::vector<AppleRemoteHost> appleRemoteHosts;
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
std::string to_string(PlatformTarget value);
std::string to_string(AppleRemoteTransport value);
std::string to_string(GovernanceToolStatus value);
std::string to_string(AppleOperationStatus value);
std::string to_string(GovernanceActionKind value);

EndpointKind endpointKindFromString(const std::string& value);
EndpointStatus endpointStatusFromString(const std::string& value);
ProviderKind providerKindFromString(const std::string& value);
ProviderCredentialFieldKind providerCredentialFieldKindFromString(const std::string& value);
ProviderAssignmentTargetKind providerAssignmentTargetKindFromString(const std::string& value);
ProviderExecutionStatus providerExecutionStatusFromString(const std::string& value);
ProviderExecutionTransport providerExecutionTransportFromString(const std::string& value);
InstallerKind installerKindFromString(const std::string& value);
ControlSurfaceToolbarAction controlSurfaceToolbarActionFromString(const std::string& value);
PlatformTarget platformTargetFromString(const std::string& value);
AppleRemoteTransport appleRemoteTransportFromString(const std::string& value);
GovernanceToolStatus governanceToolStatusFromString(const std::string& value);
AppleOperationStatus appleOperationStatusFromString(const std::string& value);
GovernanceActionKind governanceActionKindFromString(const std::string& value);

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

void to_json(nlohmann::json& json, PlatformTarget value);
void from_json(const nlohmann::json& json, PlatformTarget& value);

void to_json(nlohmann::json& json, AppleRemoteTransport value);
void from_json(const nlohmann::json& json, AppleRemoteTransport& value);

void to_json(nlohmann::json& json, GovernanceToolStatus value);
void from_json(const nlohmann::json& json, GovernanceToolStatus& value);

void to_json(nlohmann::json& json, AppleOperationStatus value);
void from_json(const nlohmann::json& json, AppleOperationStatus& value);

void to_json(nlohmann::json& json, GovernanceActionKind value);
void from_json(const nlohmann::json& json, GovernanceActionKind& value);

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
    routePath,
    specialization,
    userDefined,
    isTemplate)

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
    credentialsConfigured,
    isTemplate)

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
    SubAgentRemovalRequest,
    subAgentId)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    McpServerRemovalRequest,
    mcpServerId)

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
    AutoConnectRequest,
    kind,
    providerId,
    credentials,
    displayNameOverride,
    baseUrlOverride,
    modelIdOverride,
    assignmentTargetIds,
    allowAutonomousControl,
    discoverModels)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    DiscoveredModel,
    id,
    displayName,
    description)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    AutoConnectStep,
    stage,
    succeeded,
    message,
    latencyMs)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    AutoConnectResult,
    succeeded,
    providerId,
    displayName,
    baseUrl,
    selectedModelId,
    discoveredModels,
    steps,
    assignmentsApplied,
    assignmentsFailed,
    totalLatencyMs,
    errorMessage,
    summary)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    ReadinessIssue,
    id,
    category,
    severity,
    title,
    detail,
    remediationDestination,
    remediationLabel)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    ReadinessSnapshot,
    setupStarted,
    firstRunCompleted,
    providersReadyCount,
    providersMissingCount,
    mcpReadyCount,
    mcpMissingCount,
    workflowsReadyCount,
    workflowsMissingCount,
    specialistsReadyCount,
    specialistsMissingCount,
    blockingIssues,
    recommendedNextStep,
    updatedAtUtc)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    SupportedDependency,
    id,
    displayName,
    description,
    detectCommand,
    installMethod,
    docsUrl,
    requiresElevation,
    installTimeoutSeconds)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    DependencyDetection,
    id,
    state,
    preflight,
    detectedVersion,
    detail,
    detectedAtUtc)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    DependencyInstallResult,
    id,
    succeeded,
    finalState,
    summary,
    stdoutTail,
    stderrTail,
    exitCode,
    totalLatencyMs,
    postInstallDetection)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    StarterWorkflowTemplate,
    id,
    displayName,
    description,
    requiresProviders,
    requiresMcp,
    requiresSpecialists)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    StarterWorkflowInstantiateRequest,
    displayNameOverride)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    StarterWorkflowInstantiateResult,
    succeeded,
    workflowId,
    message)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    ActivityEvent,
    id,
    kind,
    timestampUtc,
    actor,
    method,
    target,
    statusCode,
    latencyMs,
    message,
    detail)

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
    AppleToolchainState,
    reachable,
    xcodeInstalled,
    xcodeVersion,
    developerDirectory,
    macosSdkAvailable,
    iosSdkAvailable,
    simulatorControlAvailable,
    deviceControlAvailable,
    simulatorRuntimes,
    status,
    message,
    checkedAtUtc)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    AppleSigningState,
    signingReady,
    developmentSigningReady,
    distributionSigningReady,
    availableTeams,
    status,
    message)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    AppleRemoteHost,
    hostId,
    displayName,
    transport,
    platforms,
    address,
    port,
    username,
    serviceBaseUrl,
    companionHealthPath,
    companionExecutePath,
    preferredDeveloperDirectory,
    defaultSigningIdentity,
    defaultNotaryKeychainProfile,
    defaultNotaryTeamId,
    enabled,
    toolchain,
    signing,
    transportSummary,
    credentialProfileSummary,
    readinessIssues)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    AppleRemoteHostRemovalRequest,
    hostId)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    AppleRemoteCommandRequest,
    executable,
    arguments,
    workingDirectory,
    environment,
    timeoutSeconds)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    AppleRemoteCommandResult,
    hostId,
    transport,
    launched,
    succeeded,
    exitCode,
    stdoutText,
    stderrText,
    rawResponse,
    errorMessage)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    GovernanceToolDescriptor,
    moduleId,
    serviceId,
    platform,
    toolId,
    displayName,
    description,
    requiresRemoteToolchain)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    GovernanceToolRequest,
    platform,
    toolId,
    targetPath,
    options)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    GovernanceToolResult,
    platform,
    toolId,
    displayName,
    status,
    succeeded,
    summary,
    findings,
    rawOutput,
    startedAtUtc,
    completedAtUtc,
    routeReason,
    diagnosticSummary,
    readinessIssues)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    AppleOperationRecord,
    operationId,
    platform,
    toolId,
    displayName,
    hostId,
    hostDisplayName,
    transport,
    status,
    workingDirectory,
    artifactPath,
    targetPath,
    summary,
    rawOutput,
    errorMessage,
    queuedAtUtc,
    startedAtUtc,
    completedAtUtc,
    requestOptions,
    routeReason,
    diagnosticSummary,
    selectedDeveloperDirectory,
    credentialProfileSummary,
    readinessIssues,
    redactedRequestOptionKeys,
    rerunReady,
    rerunReadinessMessage)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    AppleOperationCancelRequest,
    operationId)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    GovernanceEnforcementRequest,
    action,
    targetId,
    providerId,
    source,
    allowUntrustedExecution)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    GovernanceEnforcementDecision,
    action,
    allowed,
    posture,
    message,
    ruleId,
    blockingFindings)

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
    recommendedActions,
    appleRemoteHosts,
    platformGateways,
    governanceServers,
    availableTools,
    recentExecutions,
    appleOperations)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    PlatformGatewayDescriptor,
    moduleId,
    serviceId,
    platform,
    displayName,
    serviceType,
    instanceLabel,
    hostName,
    ipAddress,
    port,
    gatewayPath,
    configPath,
    properties,
    status,
    lanAdvertisementEnabled)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    GovernanceServerDescriptor,
    moduleId,
    serviceId,
    platform,
    displayName,
    gatewayServiceId,
    routePath,
    toolIds,
    requiresRemoteToolchain,
    status)

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
    status,
    platformGateways)

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
    appleRemoteHosts,
    platformGateways,
    governanceServers,
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
    advancedMode,
    firstRunCompleted,
    firstRunStartedAtUtc,
    firstRunCompletedAtUtc,
    firstRunSkippedSteps,
    security,
    resourceAllocation,
    providers,
    subAgentGroups,
    providerAssignments,
    appleRemoteHosts,
    activeProfile)

} // namespace MasterControl
