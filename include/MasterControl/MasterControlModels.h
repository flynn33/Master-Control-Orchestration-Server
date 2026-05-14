// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#pragma once

#include "ForsettiCore/UIModels.h"
#include "MasterControl/LanClient.h"

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
    ClientRegister,
    ClientPrivilegeChange,
    ClientAutonomousModeChange,
    ClientRevoke,
    McpServerCreate,
    McpServerModify,
    McpServerRemove,
    SubAgentCreate,
    SubAgentModify,
    SubAgentRemove,
    ModuleEnable,
    ModuleDisable,
    GovernancePolicyChange,
    RemoteInstall
};

enum class GovernanceDecisionOutcome {
    Allow,
    Block,
    RequiresOperatorApproval
};

// MCP Gateway abstraction. MCOS exposes one MCP endpoint to AI
// clients; the substrate behind that endpoint is pluggable through
// IMcpGateway. The shipping implementation is the in-process
// NativeHttpSysGatewayAdapter (Win32 HTTP.sys). The enum is kept as
// an open-ended set so a future substrate can be added without
// changing every call site that checks the gateway type.
enum class GatewayType {
    Native
};

enum class GatewayState {
    Disabled,
    Configured,
    Starting,
    Running,
    Stopping,
    Stopped,
    Failed
};

enum class GatewayHealthStatus {
    Unknown,
    Healthy,
    Degraded,
    Unhealthy
};

enum class McpServerTransport {
    StreamableHttp,
    Stdio
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
    // v0.7.2: optional process-template fields. When `command` is non-empty
    // on a SubAgent registration, the runtime auto-promotes it to a
    // managed pool (poolId == endpoint.id) so autoscale + utilization
    // light up out of the box. Operators registering a network-addressable
    // sub-agent (host:port) leave `command` empty and the auto-promote
    // path is skipped. The default scale policy spawns up to
    // `autoscaleMaxInstances` instances with up to
    // `autoscaleMaxLeasesPerInstance` concurrent leases each, matching
    // the user's stated intent for v0.7.x ("Recon hits 100 % -> spawn a
    // second Recon and offload new requests to the new agent").
    std::string command;                        // executable path; empty = no auto-promote
    std::vector<std::string> args;
    std::string workingDirectory;
    std::map<std::string, std::string> environment;
    int autoscaleMaxInstances = 4;
    int autoscaleMaxLeasesPerInstance = 2;
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
    std::string id;                     // stable issue id (e.g. "clients.none-registered")
    std::string category;               // "clients" | "mcp" | "workflows" | "specialists"
    std::string severity;               // "info" | "warning" | "blocking"
    std::string title;                  // plain-language short phrase
    std::string detail;                 // one-sentence explanation
    std::string remediationDestination; // e.g. "clients" | "runtime" | "setup/clients"
    std::string remediationLabel;       // button label e.g. "Register a LAN client"
};

struct ReadinessSnapshot final {
    bool setupStarted = false;
    bool firstRunCompleted = false;
    int mcpReadyCount = 0;
    int mcpMissingCount = 0;
    int workflowsReadyCount = 0;
    int workflowsMissingCount = 0;
    int specialistsReadyCount = 0;
    int specialistsMissingCount = 0;
    std::vector<ReadinessIssue> blockingIssues;
    // "register-first-client" | "add-mcp" | "create-specialist" |
    // "create-starter-workflow" | "review" | "complete"
    std::string recommendedNextStep;
    std::string updatedAtUtc;
};

// ---------------------------------------------------------------------------
// Setup Dependencies
// ---------------------------------------------------------------------------
// Used for optional host-side dependency installation (for example, an AI CLI
// that operators may want staged locally so a LAN client machine can invoke
// it after retrieving its MCOS configuration bundle). Each dependency follows
// an explicit three-branch preflight:
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
    // Optional prerequisite probe: if non-empty and exits non-zero, the install
    // is blocked with preflight="prerequisite-missing". Historically the
    // handler hardcoded `npm --version` as the prerequisite for every
    // dependency, which prevented installing the source of npm itself
    // (Node.js). Keep empty for dependencies that have no prerequisites
    // (e.g. nodejs via winget).
    std::string prerequisiteProbeCommand;
    std::string prerequisiteName;   // human-readable label used in error messages when prerequisite is missing
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
    int requiresClients = 0;
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
// Every incoming admin API request, client-facing mutation, and governance
// decision is appended to an in-memory ring buffer (ActivityEventRing) so the
// shell and browser dashboard can render a live command/request stream.
// Events are also surfaced via GET /api/activity so the browser dashboard and
// remote observers can subscribe.
// ---------------------------------------------------------------------------

enum class ActivityEventKind {
    AdminApiRequest,
    GovernanceDecision,
    ServiceLifecycle,
    Telemetry
};

struct ActivityEvent final {
    std::string id;                 // monotonically increasing event id (string for JS safety)
    std::string kind;               // enum-as-string for JSON: "admin_api_request", etc.
    std::string timestampUtc;
    std::string actor;              // "shell", "dashboard", "cli", etc.
    std::string method;             // HTTP verb or operation verb
    std::string target;             // path, client id, rule id, etc.
    int statusCode = 0;             // HTTP status or custom numeric outcome
    int latencyMs = 0;
    std::string message;            // short human-readable summary
    std::string detail;             // optional expanded payload (JSON-as-string)
};

// v0.9.69: boot-time self-test result. Each probe (HTTP endpoint
// reachability, supervised pool handshake, gateway state, activity-ring
// persistence, telemetry sampler liveness, on-disk worker exe presence)
// produces one of these. Failures are also pushed to the activity ring
// with kind="self_test" + statusCode=500 so the existing v0.8.7 Error
// Reporting frame on the WinUI Overview surface picks them up.
struct SelfTestResult final {
    std::string name;            // short stable identifier, e.g. "http.api_health"
    std::string category;        // "http_endpoint" / "pool_handshake" / "gateway" / "process" / "telemetry"
    bool ok = false;
    std::string message;         // human-readable line for the dashboard
    int durationMs = 0;
    std::string ranAtUtc;        // ISO-8601 UTC stamp of when the probe ran
};

struct SelfTestSnapshot final {
    std::string startedAtUtc;
    std::string finishedAtUtc;
    int totalCount = 0;
    int passedCount = 0;
    int failedCount = 0;
    std::vector<SelfTestResult> results;
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
    std::string source;
    bool allowUntrustedExecution = false;
    // Phase 7: actor of the proposed action. Operator-fallback context
    // sets this to "operator"; identified LAN clients set their clientId.
    // Persisted into deferred-action records so the operator approval UI
    // can show who asked for the change.
    std::string actor;
};

struct GovernanceEnforcementDecision final {
    GovernanceActionKind action = GovernanceActionKind::Unknown;
    GovernanceDecisionOutcome outcome = GovernanceDecisionOutcome::Allow;
    bool allowed = true;          // Phase 6 callers still consult this; Allow == true.
    std::string posture = "pass";
    std::string message;
    std::string ruleId;
    std::vector<std::string> blockingFindings;
    // When outcome == RequiresOperatorApproval the deferred record is
    // staged in the approval queue; the id is returned so the HTTP layer
    // can hand it back to the caller in a 202 response.
    std::string deferredActionId;
};

// Phase 7: actions whose CLU outcome is RequiresOperatorApproval are
// staged here until an operator approves or rejects them.
struct GovernanceDeferredAction final {
    std::string id;
    GovernanceActionKind action = GovernanceActionKind::Unknown;
    std::string actor;
    std::string targetId;
    std::string payload;          // JSON body of the original mutation, opaque to CLU
    std::string status = "pending"; // "pending" | "approved" | "rejected"
    std::string reason;
    std::string createdAtUtc;
    std::string decidedAtUtc;
    std::string decidedBy;        // "operator" by default in Phase 7
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

// PHASE-02 / v0.9.0: MCP Gateway configuration consumed by IMcpGateway
// adapters. v0.9.0 drops the in-process HTTP.sys adapter support entirely -- the runtime no
// longer supervises an external gateway binary. The native HTTP.sys
// implementation (NativeHttpSysGatewayAdapter) is the only substrate
// and starts enabled by default. binaryPath / databasePath stay in
// the schema as no-op fields so persisted configs from v0.8.x and
// older deserialize cleanly.
struct McpGatewayConfiguration final {
    GatewayType type = GatewayType::Native;
    bool enabled = true;             // v0.9.0 ships enabled-by-default
    std::string binaryPath;          // unused since v0.9.0 -- kept for back-compat
    std::string listenHost = "0.0.0.0";
    uint16_t listenPort = 8080;      // distinct from admin browserPort=7300
    std::string mcpPath = "/mcp";
    std::string healthPath = "/health";
    std::string databasePath;        // unused since v0.9.0 -- kept for back-compat
    std::string mode = "lan-trusted"; // development | enterprise | lan-trusted
};

// PHASE-02: opaque registration request handed to IMcpGateway when MCOS
// publishes a stable logical endpoint backed by a managed worker pool.
struct McpServerRegistration final {
    std::string name;                // logical pool/server identifier
    std::string description;
    McpServerTransport transport = McpServerTransport::StreamableHttp;
    std::string url;                 // streamable_http: full URL
    std::map<std::string, std::string> headers;
    std::string sessionMode = "stateful";
    std::string command;             // stdio: executable path
    std::vector<std::string> args;
    std::map<std::string, std::string> environment;
};

struct McpToolDescriptor final {
    std::string serverName;          // registered logical server name
    std::string toolName;
    std::string description;
    // v0.9.4: child-reported JSON Schema for the tool's arguments,
    // serialized as a JSON object string. Captured verbatim from the
    // worker's `tools/list` response so the gateway can republish it
    // unchanged. Empty string means "no schema reported"; the gateway
    // emits the open `{"type":"object"}` placeholder in that case.
    // Pre-v0.9.4 the gateway hard-coded `{"type":"object"}` for every
    // tool, which left schema-driven LAN clients (Claude Code, the MCP
    // SDK code generators) unable to discover argument names from
    // `tools/list` alone. The worker-side schemas were always present
    // -- the descriptor model just didn't carry them through.
    std::string inputSchemaJson;
};

struct GatewayStatus final {
    GatewayState state = GatewayState::Disabled;
    std::string message;
    std::string mcpUrl;              // empty until Running
    std::string startedAtUtc;
    std::string adapterType;         // "native" | "fake"
};

struct GatewayHealth final {
    GatewayHealthStatus status = GatewayHealthStatus::Unknown;
    bool reachable = false;
    int httpStatusCode = 0;          // 0 if probe never landed
    std::string message;
    std::string probedAtUtc;
    std::string mcpUrl;
    std::string healthUrl;
    std::string adapterType;
    int registeredServerCount = 0;
};

struct RegistrationResult final {
    bool succeeded = false;
    std::string message;
    std::string serverName;
    std::string registeredAtUtc;
};

struct DeregistrationResult final {
    bool succeeded = false;
    std::string message;
    std::string serverName;
};

// PHASE-06 (ADR-002 §7): Managed endpoint pools. MCP servers and
// sub-agents become supervised process pools behind stable logical
// endpoints. Lifecycle: configured -> starting -> ready -> busy ->
// draining -> stopped, with `failed` reachable from any non-terminal
// state. Worker process trees are contained with Windows Job Objects
// (JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE) so terminating the supervisor
// kills the entire tree atomically. PHASE-07 layers leases + autoscale.
//
// Schema: docs/implementation/schemas/managed-endpoint-pool.schema.json
enum class EndpointPoolKind {
    McpServer,
    SubAgent
};

enum class EndpointInstanceState {
    Configured,
    Starting,
    Ready,
    Busy,
    Draining,
    Failed,
    Stopped
};

struct ScalePolicy final {
    int minInstances = 0;
    int maxInstances = 1;
    int maxActiveLeasesPerInstance = 1;
    int scaleOutQueueWaitMs = 1500;
    int scaleInIdleSeconds = 300;
};

struct DrainPolicy final {
    int drainTimeoutSeconds = 30;
    bool drainStickySessions = true;
    bool routeNewSessionsToReplacement = true;
};

struct HealthProbeSpec final {
    std::string transport;       // "http" | "stdio_handshake" | "none"
    std::string path;
    int intervalMs = 5000;
    int timeoutMs = 1500;
    int unhealthyThreshold = 3;
};

struct EndpointTemplate final {
    std::string executable;
    std::vector<std::string> args;
    std::string workingDirectory;
    std::map<std::string, std::string> environment;
    std::string transport = "streamable_http";
    HealthProbeSpec healthProbe;
};

struct WorkerTelemetry final {
    int activeLeases = 0;
    int queueDepth = 0;
    int inflightCalls = 0;
    double cpuPercent = -1.0;
    double memoryMbytes = -1.0;
    std::string lastProbedAtUtc;
    std::string lastHealthMessage;
};

struct EndpointInstance final {
    std::string instanceId;
    std::string poolId;
    EndpointInstanceState state = EndpointInstanceState::Configured;
    uint32_t processId = 0;
    bool supervised = false;
    std::string startedAtUtc;
    std::string lastTransitionAtUtc;
    std::string statusMessage;
    WorkerTelemetry telemetry;
};

struct ManagedEndpointPool final {
    std::string poolId;
    EndpointPoolKind kind = EndpointPoolKind::McpServer;
    std::string displayName;
    std::string logicalMcpUrl;
    EndpointTemplate template_;
    ScalePolicy scalePolicy;
    DrainPolicy drainPolicy;
    std::vector<EndpointInstance> instances;
    std::string createdAtUtc;
    std::string updatedAtUtc;
    // v0.9.40: when the per-pool crash circuit breaker has tripped
    // (kCrashThreshold=5 worker deaths within kCrashWindowSeconds=60),
    // the supervisor sets this to the UTC ISO-8601 timestamp at which
    // auto-respawn will resume. Empty when the pool is not quarantined.
    // The /api/health/summary "darkOrQuarantined" counter and the
    // /api/activity "pool_quarantine" event already exposed this state;
    // /api/pools/{poolId} now exposes it directly so a generic operator
    // dashboard can render a per-pool "quarantined for Ns" badge without
    // joining health/summary or scanning the activity ring.
    std::string quarantinedUntilUtc;
};

// PHASE-07 (ADR-002 §8): Lease routing + autoscale. Sessions/requests
// are assigned to instances behind stable logical pool endpoints via
// EndpointLease. Stateful sessions stay sticky to their owning
// instance for the lease's lifetime; new sessions route to the
// least-loaded Ready instance. When all Ready instances are at
// scalePolicy.maxActiveLeasesPerInstance and adding another instance
// is permitted, the LeaseRouter triggers a same-type scale-out and
// routes the new lease to the freshly-spawned instance. Drain transitions
// existing instances to Draining (active leases stay sticky); new
// sessions route elsewhere. Hot-migration is forbidden (ADR-002 §8).
enum class LeaseState {
    Active,
    Released,
    Failed
};

struct LeaseRequest final {
    std::string poolId;
    std::string sessionId;          // empty: stateless lease (any Ready)
    std::string clientHint;         // operator-provided routing hint
    bool stateful = false;
    // v0.7.1: optional LAN-client identity carried so the lease can
    // record who acquired it. Populated by the gateway adapter when the
    // tools/call envelope carries client identity (e.g., via X-MCOS-
    // Client-Id header or downstream session info). Empty for legacy
    // call sites; both fields are best-effort and never block routing.
    std::string clientIpAddress;
    std::string clientType;         // e.g. "claude-code", "codex", "grok"
};

struct EndpointLease final {
    std::string leaseId;
    std::string poolId;
    std::string instanceId;
    std::string sessionId;          // present iff stateful
    LeaseState state = LeaseState::Active;
    std::string acquiredAtUtc;
    std::string releasedAtUtc;
    std::string statusMessage;
    // v0.7.1: copied verbatim from the LeaseRequest at acquisition time
    // so the dashboard's per-sub-agent client-attribution panel can
    // surface the originating LAN client without keeping a side index.
    std::string clientIpAddress;
    std::string clientType;
};

struct PoolSaturation final {
    std::string poolId;
    int instanceCount = 0;
    int readyInstanceCount = 0;
    int drainingInstanceCount = 0;
    int activeLeaseCount = 0;
    int queueDepth = 0;
    int maxActiveLeasesPerInstance = 1;
    bool atSaturation = false;       // every Ready instance is at max leases
    bool scaleOutTriggered = false;  // last lease attempt forced scale-out
    bool atMaxInstances = false;     // pool already at scalePolicy.maxInstances
};

// PHASE-08 (ADR-002 §9): Real-time telemetry model. Honest only — host
// metrics come from Win32 PDH where available; per-AI-client CPU/GPU/
// disk are populated only when a client supplies a heartbeat or sidecar.
// Missing metrics report -1.0 / empty / "unavailable" rather than
// fabricated values.
//
// Schema: docs/implementation/schemas/telemetry-event.schema.json
enum class TelemetryCategory {
    Host,
    Client,
    Gateway,
    Worker,
    Governance,
    Discovery,
    Dashboard,
    System
};

enum class TelemetrySeverity {
    Info,
    Warning,
    Error,
    Critical
};

struct TelemetryEvent final {
    std::string timestamp;        // ISO-8601 UTC
    TelemetryCategory category = TelemetryCategory::System;
    TelemetrySeverity severity = TelemetrySeverity::Info;
    std::string message;
    std::string clientId;         // optional
    std::string poolId;           // optional
    std::string instanceId;       // optional
    nlohmann::json metrics = nlohmann::json::object();
};

struct ClientHeartbeat final {
    std::string clientId;
    std::string clientType;       // "claude-code" | "codex" | etc.
    std::string ipAddress;
    std::string sentAtUtc;
    // Optional self-reported metrics. Honest defaults: -1.0 = unavailable.
    double cpuPercent = -1.0;
    double memoryPercent = -1.0;
    double gpuPercent = -1.0;
    double gpuMemoryMb = -1.0;
    uint64_t bytesSentPerSecond = 0;
    uint64_t bytesReceivedPerSecond = 0;
    nlohmann::json sessionContext = nlohmann::json::object();
};

struct ClientPresence final {
    std::string clientId;
    std::string clientType;
    std::string ipAddress;
    std::string firstSeenUtc;
    std::string lastSeenUtc;
    int connectionCount = 0;
    int requestCount = 0;
    bool heartbeatPresent = false;
    ClientHeartbeat lastHeartbeat;
};

struct GatewayTrafficSnapshot final {
    std::string adapterType;
    std::string mcpUrl;
    GatewayHealthStatus healthStatus = GatewayHealthStatus::Unknown;
    int activeClientCount = 0;
    int requestsLastMinute = 0;
    int errorsLastMinute = 0;
    int registeredServerCount = 0;
    std::string lastEventAtUtc;
};

// PHASE-05 (ADR-002 §6): Per-platform CLU/Forsetti governance bundle.
// Contract: docs/implementation/CLU-GOVERNANCE-BUNDLE-CONTRACT.md.
// Each bundle wraps the Forsetti Framework + Forsetti Framework for
// Agentic Coding guidance for one client platform (Windows / macOS /
// iOS) plus CLU's machine-readable rules and decision policy. The
// bundle URL is embedded in OnboardingProfile.governanceBundleUrl
// (PHASE-04) so AI clients can fetch it during onboarding.
struct GovernanceBundle final {
    std::string platform;                       // "windows" | "macos" | "ios"
    std::string forsettiFrameworkVersion;
    std::string agenticCodingFrameworkVersion;
    std::string cluSchemaVersion;
    std::string instructionsMarkdown;
    nlohmann::json rulesJson;
    std::string decisionPolicy;
    std::string checksum;
    std::string generatedAt;
};

struct GovernanceProfileSummary final {
    std::string unitName;
    std::string doctrine;
    std::string cluSchemaVersion;
    std::vector<std::string> documentIds;
    std::vector<std::string> roleIds;
    std::vector<std::string> ruleIds;
    std::string generatedAt;
};

// PHASE-04 (ADR-002 §5): Per-client onboarding profile. Schema:
// docs/implementation/schemas/onboarding-profile.schema.json. Each
// profile points clients at exactly one MCP gateway URL with
// authRequired=false (LAN trust) and links the governance bundle.
// PHASE-09 will hydrate the dashboard's setup view from these.
struct OnboardingConfigSnippet final {
    std::string format;             // "json" | "toml" | "shell" | "powershell"
    std::string filename;           // optional default filename hint
    nlohmann::json content;         // structured content (object or string)
    std::string description;        // human-readable purpose
};

struct OnboardingProfile final {
    std::string clientType;
    std::string displayName;
    std::string gatewayMcpUrl;
    std::string transport = "streamable_http"; // streamable_http | stdio_bridge | sse_compat
    bool authRequired = false;       // schema const: must be false
    std::string trust = "lan";
    std::string governanceBundleUrl;
    std::string discoveryUrl;
    std::string instanceId;
    std::vector<OnboardingConfigSnippet> configSnippets;
    std::vector<std::string> manualInstructions;
    std::vector<std::string> verificationSteps;
    std::vector<std::string> caveats;
};

// PHASE-03 (ADR-002 §4): MCOS Discovery Document. Served at
// /.well-known/mcos.json and /api/discovery, broadcast via UDP beacon.
// DNS-SD TXT advertisement carries a flattened subset of these fields.
// Schema: docs/implementation/schemas/discovery-document.schema.json
struct DiscoveryGateway final {
    std::string type;        // "native" | "fake"
    std::string mcpUrl;      // e.g. http://192.168.1.10:8080/mcp
    std::string healthUrl;   // e.g. http://192.168.1.10:8080/health
    std::string state;       // GatewayState slug
};

struct DiscoveryOnboarding final {
    std::string generic;      // /api/onboarding/generic
    std::string claudeCode;   // /api/onboarding/claude-code
    std::string codex;        // /api/onboarding/codex
    std::string grok;         // /api/onboarding/grok
    std::string chatgpt;      // /api/onboarding/chatgpt
};

struct DiscoveryGovernance final {
    std::string bundleBaseUrl;   // /api/governance/bundles
    std::string cluProfileUrl;   // /api/governance/profile
    std::string decisionsUrl;    // /api/governance/decisions
};

// v0.9.4: discovery now advertises the operator-side admin surfaces
// (pools, clients, activity, host telemetry, gateway control). LAN
// operators / dashboards previously had to know these paths
// out-of-band even though the well-known doc is the canonical
// discovery surface; this block closes that loop.
struct DiscoveryAdmin final {
    std::string poolsUrl;        // /api/pools
    std::string clientsUrl;      // /api/clients
    std::string activityUrl;     // /api/activity
    std::string hostTelemetryUrl; // /api/host/telemetry
    std::string gatewayStatusUrl; // /api/gateway/status
    std::string gatewayToolsUrl;  // /api/gateway/tools
    std::string healthUrl;        // /api/health (operator-side)
    // v0.9.18: surface the v0.9.17 persistence-health endpoint so
    // operators reading the discovery doc can find it. Pre-v0.9.18
    // /api/activity/health was undocumented in the discovery
    // surface; deploy retros and post-mortem scripts had to know
    // the path out-of-band. With this entry, any tool that reads
    // discovery (e.g., the dashboard, the self-test, a future ops
    // CLI) can pull the URL without hard-coding.
    std::string activityHealthUrl; // /api/activity/health
    // v0.9.22: surface the v0.9.21 single-URL health-summary
    // endpoint so monitoring tools and deploy gates can find it
    // from the discovery doc instead of hard-coding the path.
    std::string healthSummaryUrl;  // /api/health/summary
};

struct DiscoveryDocument final {
    std::string product = "MCOS";
    std::string role = "mcp-gateway-host";
    std::string version;
    std::string instanceId;
    std::string trust = "lan";
    std::string auth = "none";
    DiscoveryGateway gateway;
    DiscoveryOnboarding onboarding;
    DiscoveryGovernance governance;
    DiscoveryAdmin admin;        // v0.9.4 -- operator surface URLs
    std::vector<std::string> capabilities;
    // Beacon-only metadata (omitted from /.well-known by convention).
    std::string generatedAtUtc;
    std::string serverIpAddress;
    std::string instanceName;
};

// v0.7.1: per-sub-agent runtime statistics used by the Overview and
// Telemetry decks of the browser dashboard. Each stat ties an inventory
// SubAgent endpoint (id + displayName + specialization) to the worker
// pool, instances, and active leases that back it. utilizationPercent is
// the load-relative-to-capacity figure operators see in the new
// utilization bars; -1.0 means "no managed pool registered for this
// sub-agent" and the dashboard renders it honestly as `unavailable`
// (ADR-002 §9 "no fake telemetry"). activeClients carries the LAN client
// IP + clientType pair for each currently-leased session, so the operator
// can see which AI client is using which sub-agent in real time.
struct SubAgentLeaseHolder final {
    std::string ipAddress;       // e.g. "192.168.1.42"
    std::string clientType;      // e.g. "claude-code", "codex", "grok"
    std::string sessionId;       // sticky-session token, may be empty
    std::string acquiredAtUtc;   // when the lease was bound
};

struct SubAgentRuntimeStat final {
    std::string subAgentId;
    std::string displayName;
    std::string specialization;
    std::string poolId;             // empty if no managed pool wraps this sub-agent
    int readyInstanceCount = 0;
    int totalInstanceCount = 0;
    int activeLeaseCount = 0;
    int leaseCapacity = 0;          // sum of (instances * maxActiveLeasesPerInstance)
    int maxInstancesAllowed = 0;    // scalePolicy.maxInstances
    double utilizationPercent = 0.0;
    bool autoscaleEnabled = false;
    std::vector<SubAgentLeaseHolder> activeClients;
    // v0.7.6: proxy telemetry. Sub-agents that aren't wrapped in a managed
    // pool still have a network endpoint we can probe. Reachability is the
    // primary "is this thing alive" signal; endpointHostPort is the
    // human-readable "host:port" the operator can see at a glance;
    // lastProbedAtUtc is the freshness stamp so the dashboard can render
    // "5s ago" without computing a delta itself. Reachability is computed
    // by AdminApiService::snapshot via a non-blocking TCP connect with a
    // 200ms timeout per sub-agent.
    bool reachable = false;
    std::string endpointHostPort;    // e.g. "127.0.0.1:9101"
    std::string lastProbedAtUtc;     // ISO-8601 UTC; empty until first probe
    std::string status;              // "online" / "offline" / "degraded" / "unknown" passthrough from inventory
    // v0.9.56: honest-unavailable diagnostic surface. Pre-v0.9.56 the
    // dashboard reported reachable=false with empty status, leaving
    // operators staring at a red dot with no actionable hint. The five
    // fields below close that gap by surfacing (a) WHY the entry is
    // unreachable, (b) what we last saw on the wire, (c) the install
    // posture so it can be triaged separately from runtime failure, and
    // (d) the next step the operator can take.
    //
    // unavailableReason values (categorized; empty when reachable):
    //   "stdio_supervised"           - reachable via supervised pool, not TCP
    //   "online_via_admin_port"      - listener is the MCOS admin port
    //   "no_listener"                - TCP connect refused / no process bound
    //   "dns_failed"                 - host name did not resolve
    //   "timeout"                    - TCP handshake exceeded the 200ms budget
    //   "supervised_pool_missing"    - inventory expects a stdio pool but none registered
    //   "no_endpoint_registered"     - sub-agent slot has no host:port and no pool
    // installState values:
    //   "installed_and_supervised"   - managed pool is wrapping this entry
    //   "online_via_admin_port"      - served from the MCOS admin port itself
    //   "awaiting_pool_registration" - catalog placeholder, no pool yet
    //   "unknown"                    - state could not be classified
    std::string unavailableReason;
    std::string lastErrorMessage;
    std::string lastErrorAtUtc;
    std::string installState;
    std::string installHint;
    // v0.9.58: optional install command (e.g.
    // "npm install -g chrome-devtools-mcp") for entries that map to
    // a well-known canonical package. Empty when no canonical install
    // path is known for the entry id. The dashboard renders this as
    // a copy-paste-able code block in the diagnostic surface.
    // installPackageDetected is true when the runtime found the
    // package in the global npm tree at boot, which means the
    // operator only has to register a pool (not install) to
    // promote the entry to installed_and_supervised.
    std::string installCommand;
    bool installPackageDetected = false;
};

// v0.8.3: per-MCP-server runtime stats. Mirrors SubAgentRuntimeStat
// exactly (utilization bar, reachability dot, host:port, active-client
// list) so the dashboard and the WinUI shell can render MCP server
// cards with the same telemetry surface that v0.7.6 added for
// sub-agents. Single shared lease-holder type because LAN-client
// attribution is identical regardless of endpoint kind.
using McpServerLeaseHolder = SubAgentLeaseHolder;

struct McpServerRuntimeStat final {
    std::string mcpServerId;
    std::string displayName;
    std::string specialization;
    std::string poolId;             // empty if no managed pool wraps this server
    int readyInstanceCount = 0;
    int totalInstanceCount = 0;
    int activeLeaseCount = 0;
    int leaseCapacity = 0;
    int maxInstancesAllowed = 0;
    double utilizationPercent = 0.0;
    bool autoscaleEnabled = false;
    std::vector<McpServerLeaseHolder> activeClients;
    bool reachable = false;
    std::string endpointHostPort;
    std::string lastProbedAtUtc;
    std::string status;
    // v0.9.56: honest-unavailable diagnostic surface (parallel to
    // SubAgentRuntimeStat above; same value semantics).
    std::string unavailableReason;
    std::string lastErrorMessage;
    std::string lastErrorAtUtc;
    std::string installState;
    std::string installHint;
    // v0.9.58: parallel to SubAgentRuntimeStat above.
    std::string installCommand;
    bool installPackageDetected = false;
};

struct DashboardSnapshot final {
    HostTelemetrySnapshot telemetry;
    std::vector<RuntimeEndpoint> endpoints;
    std::vector<SubAgentGroupDefinition> subAgentGroups;
    std::vector<SubAgentRuntimeStat> subAgentRuntimeStats;
    // v0.8.3: parallel array for MCP servers, populated by the same
    // probe + pool-lookup pipeline that produces subAgentRuntimeStats.
    std::vector<McpServerRuntimeStat> mcpServerRuntimeStats;
    ResourceAllocationProfile resourceAllocation;
    SecuritySettings security;
    std::vector<InstallProvenance> installHistory;
    std::vector<ExportArtifact> exports;
    GovernanceSnapshot governance;
    std::vector<AppleRemoteHost> appleRemoteHosts;
    std::vector<PlatformGatewayDescriptor> platformGateways;
    std::vector<GovernanceServerDescriptor> governanceServers;
    ForsettiSurfaceSnapshot surface;
    GatewayStatus mcpGatewayStatus;
    GatewayHealth mcpGatewayHealth;
    std::vector<McpToolDescriptor> mcpGatewayTools;
    DiscoveryDocument discovery;
};

struct AppConfiguration final {
    std::string instanceName = "Master Control Orchestration Server";
    std::string instanceId;          // PHASE-03: stable per-host id, generated on first run
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
    std::vector<SubAgentGroupDefinition> subAgentGroups;
    std::vector<LanClient> lanClients;
    std::vector<AppleRemoteHost> appleRemoteHosts;
    ManagedNodeProfile activeProfile;
    McpGatewayConfiguration mcpGateway;
    // PHASE-06 worker pool definitions, persisted across restart and
    // upgrade. WorkerSupervisor::pools_ is the in-memory authority; this
    // field is the on-disk mirror. Load-on-start: runtime reads
    // configuration.pools and calls workerSupervisor_->upsertPool() for
    // each. Write-on-upsert/remove: pool admin routes update both the
    // supervisor (immediate effect) and the configuration (next-restart
    // survival). Pool definitions stored here include scalePolicy /
    // drainPolicy / template / healthProbe / logicalMcpUrl. Instance
    // state (PIDs, ready/busy/draining lifecycle, telemetry) is
    // intentionally NOT persisted -- supervised processes don't survive
    // an MCOS restart by design (Job Object containment), so re-reading
    // stale instance rows would lie. The runtime re-creates instances
    // on demand from scalePolicy.minInstances after load.
    std::vector<ManagedEndpointPool> pools;
};

std::string to_string(EndpointKind value);
std::string to_string(EndpointStatus value);
std::string to_string(InstallerKind value);
std::string to_string(ControlSurfaceToolbarAction value);
std::string to_string(PlatformTarget value);
std::string to_string(AppleRemoteTransport value);
std::string to_string(GovernanceToolStatus value);
std::string to_string(AppleOperationStatus value);
std::string to_string(GovernanceActionKind value);
std::string to_string(GovernanceDecisionOutcome value);
std::string to_string(GatewayType value);
std::string to_string(GatewayState value);
std::string to_string(GatewayHealthStatus value);
std::string to_string(McpServerTransport value);
std::string to_string(EndpointPoolKind value);
std::string to_string(EndpointInstanceState value);
std::string to_string(LeaseState value);
std::string to_string(TelemetryCategory value);
std::string to_string(TelemetrySeverity value);

EndpointKind endpointKindFromString(const std::string& value);
EndpointStatus endpointStatusFromString(const std::string& value);
InstallerKind installerKindFromString(const std::string& value);
ControlSurfaceToolbarAction controlSurfaceToolbarActionFromString(const std::string& value);
PlatformTarget platformTargetFromString(const std::string& value);
AppleRemoteTransport appleRemoteTransportFromString(const std::string& value);
GovernanceToolStatus governanceToolStatusFromString(const std::string& value);
AppleOperationStatus appleOperationStatusFromString(const std::string& value);
GovernanceActionKind governanceActionKindFromString(const std::string& value);
GovernanceDecisionOutcome governanceDecisionOutcomeFromString(const std::string& value);
GatewayType gatewayTypeFromString(const std::string& value);
GatewayState gatewayStateFromString(const std::string& value);
GatewayHealthStatus gatewayHealthStatusFromString(const std::string& value);
McpServerTransport mcpServerTransportFromString(const std::string& value);
EndpointPoolKind endpointPoolKindFromString(const std::string& value);
EndpointInstanceState endpointInstanceStateFromString(const std::string& value);
LeaseState leaseStateFromString(const std::string& value);
TelemetryCategory telemetryCategoryFromString(const std::string& value);
TelemetrySeverity telemetrySeverityFromString(const std::string& value);

void to_json(nlohmann::json& json, EndpointKind value);
void from_json(const nlohmann::json& json, EndpointKind& value);

void to_json(nlohmann::json& json, EndpointStatus value);
void from_json(const nlohmann::json& json, EndpointStatus& value);

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

void to_json(nlohmann::json& json, GovernanceDecisionOutcome value);
void from_json(const nlohmann::json& json, GovernanceDecisionOutcome& value);

void to_json(nlohmann::json& json, GatewayType value);
void from_json(const nlohmann::json& json, GatewayType& value);

void to_json(nlohmann::json& json, GatewayState value);
void from_json(const nlohmann::json& json, GatewayState& value);

void to_json(nlohmann::json& json, GatewayHealthStatus value);
void from_json(const nlohmann::json& json, GatewayHealthStatus& value);

void to_json(nlohmann::json& json, McpServerTransport value);
void from_json(const nlohmann::json& json, McpServerTransport& value);

void to_json(nlohmann::json& json, EndpointPoolKind value);
void from_json(const nlohmann::json& json, EndpointPoolKind& value);

void to_json(nlohmann::json& json, EndpointInstanceState value);
void from_json(const nlohmann::json& json, EndpointInstanceState& value);

void to_json(nlohmann::json& json, LeaseState value);
void from_json(const nlohmann::json& json, LeaseState& value);

void to_json(nlohmann::json& json, TelemetryCategory value);
void from_json(const nlohmann::json& json, TelemetryCategory& value);

void to_json(nlohmann::json& json, TelemetrySeverity value);
void from_json(const nlohmann::json& json, TelemetrySeverity& value);

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
    isTemplate,
    command,
    args,
    workingDirectory,
    environment,
    autoscaleMaxInstances,
    autoscaleMaxLeasesPerInstance)

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
    requiresClients,
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
    SelfTestResult,
    name,
    category,
    ok,
    message,
    durationMs,
    ranAtUtc)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    SelfTestSnapshot,
    startedAtUtc,
    finishedAtUtc,
    totalCount,
    passedCount,
    failedCount,
    results)

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
    source,
    allowUntrustedExecution,
    actor)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    GovernanceEnforcementDecision,
    action,
    outcome,
    allowed,
    posture,
    message,
    ruleId,
    blockingFindings,
    deferredActionId)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    GovernanceDeferredAction,
    id,
    action,
    actor,
    targetId,
    payload,
    status,
    reason,
    createdAtUtc,
    decidedAtUtc,
    decidedBy)

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
    SubAgentLeaseHolder,
    ipAddress,
    clientType,
    sessionId,
    acquiredAtUtc)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    SubAgentRuntimeStat,
    subAgentId,
    displayName,
    specialization,
    poolId,
    readyInstanceCount,
    totalInstanceCount,
    activeLeaseCount,
    leaseCapacity,
    maxInstancesAllowed,
    utilizationPercent,
    autoscaleEnabled,
    activeClients,
    reachable,
    endpointHostPort,
    lastProbedAtUtc,
    status,
    unavailableReason,
    lastErrorMessage,
    lastErrorAtUtc,
    installState,
    installHint,
    installCommand,
    installPackageDetected)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    McpServerRuntimeStat,
    mcpServerId,
    displayName,
    specialization,
    poolId,
    readyInstanceCount,
    totalInstanceCount,
    activeLeaseCount,
    leaseCapacity,
    maxInstancesAllowed,
    utilizationPercent,
    autoscaleEnabled,
    activeClients,
    reachable,
    endpointHostPort,
    lastProbedAtUtc,
    status,
    unavailableReason,
    lastErrorMessage,
    lastErrorAtUtc,
    installState,
    installHint,
    installCommand,
    installPackageDetected)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    DashboardSnapshot,
    telemetry,
    endpoints,
    subAgentGroups,
    subAgentRuntimeStats,
    mcpServerRuntimeStats,
    resourceAllocation,
    security,
    installHistory,
    exports,
    governance,
    appleRemoteHosts,
    platformGateways,
    governanceServers,
    surface,
    mcpGatewayStatus,
    mcpGatewayHealth,
    mcpGatewayTools,
    discovery)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    AppConfiguration,
    instanceName,
    instanceId,
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
    subAgentGroups,
    lanClients,
    appleRemoteHosts,
    activeProfile,
    mcpGateway,
    pools)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    McpGatewayConfiguration,
    type,
    enabled,
    binaryPath,
    listenHost,
    listenPort,
    mcpPath,
    healthPath,
    databasePath,
    mode)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    McpServerRegistration,
    name,
    description,
    transport,
    url,
    headers,
    sessionMode,
    command,
    args,
    environment)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    McpToolDescriptor,
    serverName,
    toolName,
    description)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    GatewayStatus,
    state,
    message,
    mcpUrl,
    startedAtUtc,
    adapterType)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    GatewayHealth,
    status,
    reachable,
    httpStatusCode,
    message,
    probedAtUtc,
    mcpUrl,
    healthUrl,
    adapterType,
    registeredServerCount)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    RegistrationResult,
    succeeded,
    message,
    serverName,
    registeredAtUtc)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    DeregistrationResult,
    succeeded,
    message,
    serverName)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    DiscoveryGateway,
    type,
    mcpUrl,
    healthUrl,
    state)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    DiscoveryOnboarding,
    generic,
    claudeCode,
    codex,
    grok,
    chatgpt)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    DiscoveryGovernance,
    bundleBaseUrl,
    cluProfileUrl,
    decisionsUrl)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    DiscoveryAdmin,
    poolsUrl,
    clientsUrl,
    activityUrl,
    hostTelemetryUrl,
    gatewayStatusUrl,
    gatewayToolsUrl,
    healthUrl,
    activityHealthUrl,
    healthSummaryUrl)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    DiscoveryDocument,
    product,
    role,
    version,
    instanceId,
    trust,
    auth,
    gateway,
    onboarding,
    governance,
    admin,
    capabilities,
    generatedAtUtc,
    serverIpAddress,
    instanceName)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    OnboardingConfigSnippet,
    format,
    filename,
    content,
    description)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    OnboardingProfile,
    clientType,
    displayName,
    gatewayMcpUrl,
    transport,
    authRequired,
    trust,
    governanceBundleUrl,
    discoveryUrl,
    instanceId,
    configSnippets,
    manualInstructions,
    verificationSteps,
    caveats)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    GovernanceBundle,
    platform,
    forsettiFrameworkVersion,
    agenticCodingFrameworkVersion,
    cluSchemaVersion,
    instructionsMarkdown,
    rulesJson,
    decisionPolicy,
    checksum,
    generatedAt)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    GovernanceProfileSummary,
    unitName,
    doctrine,
    cluSchemaVersion,
    documentIds,
    roleIds,
    ruleIds,
    generatedAt)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    ScalePolicy,
    minInstances,
    maxInstances,
    maxActiveLeasesPerInstance,
    scaleOutQueueWaitMs,
    scaleInIdleSeconds)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    DrainPolicy,
    drainTimeoutSeconds,
    drainStickySessions,
    routeNewSessionsToReplacement)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    HealthProbeSpec,
    transport,
    path,
    intervalMs,
    timeoutMs,
    unhealthyThreshold)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    EndpointTemplate,
    executable,
    args,
    workingDirectory,
    environment,
    transport,
    healthProbe)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    WorkerTelemetry,
    activeLeases,
    queueDepth,
    inflightCalls,
    cpuPercent,
    memoryMbytes,
    lastProbedAtUtc,
    lastHealthMessage)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    EndpointInstance,
    instanceId,
    poolId,
    state,
    processId,
    supervised,
    startedAtUtc,
    lastTransitionAtUtc,
    statusMessage,
    telemetry)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    LeaseRequest,
    poolId,
    sessionId,
    clientHint,
    stateful,
    clientIpAddress,
    clientType)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    EndpointLease,
    leaseId,
    poolId,
    instanceId,
    sessionId,
    state,
    acquiredAtUtc,
    releasedAtUtc,
    statusMessage,
    clientIpAddress,
    clientType)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    PoolSaturation,
    poolId,
    instanceCount,
    readyInstanceCount,
    drainingInstanceCount,
    activeLeaseCount,
    queueDepth,
    maxActiveLeasesPerInstance,
    atSaturation,
    scaleOutTriggered,
    atMaxInstances)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    TelemetryEvent,
    timestamp,
    category,
    severity,
    message,
    clientId,
    poolId,
    instanceId,
    metrics)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    ClientHeartbeat,
    clientId,
    clientType,
    ipAddress,
    sentAtUtc,
    cpuPercent,
    memoryPercent,
    gpuPercent,
    gpuMemoryMb,
    bytesSentPerSecond,
    bytesReceivedPerSecond,
    sessionContext)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    ClientPresence,
    clientId,
    clientType,
    ipAddress,
    firstSeenUtc,
    lastSeenUtc,
    connectionCount,
    requestCount,
    heartbeatPresent,
    lastHeartbeat)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    GatewayTrafficSnapshot,
    adapterType,
    mcpUrl,
    healthStatus,
    activeClientCount,
    requestsLastMinute,
    errorsLastMinute,
    registeredServerCount,
    lastEventAtUtc)

// ManagedEndpointPool uses an explicit serializer because the
// EndpointTemplate field is named `template_` to avoid the `template`
// reserved word; the JSON key remains `template` for schema fidelity.
inline void to_json(nlohmann::json& json, const ManagedEndpointPool& pool) {
    json = {
        { "poolId", pool.poolId },
        { "kind", to_string(pool.kind) },
        { "displayName", pool.displayName },
        { "logicalMcpUrl", pool.logicalMcpUrl },
        { "template", pool.template_ },
        { "scalePolicy", pool.scalePolicy },
        { "drainPolicy", pool.drainPolicy },
        { "instances", pool.instances },
        { "createdAtUtc", pool.createdAtUtc },
        { "updatedAtUtc", pool.updatedAtUtc },
        // v0.9.40: empty string when not quarantined; ISO-8601 UTC
        // expiry when the crash circuit breaker has tripped.
        { "quarantinedUntilUtc", pool.quarantinedUntilUtc }
    };
}

inline void from_json(const nlohmann::json& json, ManagedEndpointPool& pool) {
    pool.poolId = json.value("poolId", std::string{});
    pool.kind = endpointPoolKindFromString(json.value("kind", std::string("mcp-server")));
    pool.displayName = json.value("displayName", std::string{});
    pool.logicalMcpUrl = json.value("logicalMcpUrl", std::string{});
    if (json.contains("template")) {
        pool.template_ = json.at("template").get<EndpointTemplate>();
    }
    pool.scalePolicy = json.value("scalePolicy", ScalePolicy{});
    pool.drainPolicy = json.value("drainPolicy", DrainPolicy{});
    pool.instances = json.value("instances", std::vector<EndpointInstance>{});
    pool.createdAtUtc = json.value("createdAtUtc", std::string{});
    pool.updatedAtUtc = json.value("updatedAtUtc", std::string{});
    // v0.9.40: forward-compat for round-trips through config persistence.
    // Disk-persisted pools never carry a real quarantine value (in-memory
    // state); the field is here so json::parse(...).get<ManagedEndpointPool>()
    // doesn't choke if a future caller hands us a payload with it.
    pool.quarantinedUntilUtc = json.value("quarantinedUntilUtc", std::string{});
}

} // namespace MasterControl
