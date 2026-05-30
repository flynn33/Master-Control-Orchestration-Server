// Master Control Orchestration Server
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
    std::vector<std::wstring> requiredCapabilities;
    std::wstring risk;
    bool highRisk = false;
};




struct ShellSubAgentGroupDefinition final {
    std::wstring groupId;
    std::wstring displayName;
    std::wstring description;
    std::vector<std::wstring> memberTargetIds;
    std::wstring updatedAtUtc;
};

// v0.7.6: per-sub-agent live runtime statistics (mirrors the runtime's
// SubAgentRuntimeStat). Populated by ShellRuntime::CaptureSnapshot from
// /api/dashboard.subAgentRuntimeStats. Powers the Sub-Agents card grid
// on the WinUI shell's Runtime section.
struct ShellSubAgentLeaseHolder final {
    std::wstring ipAddress;
    std::wstring clientType;
    std::wstring sessionId;
    std::wstring acquiredAtUtc;
};

struct ShellSubAgentRuntimeStat final {
    std::wstring subAgentId;
    std::wstring displayName;
    std::wstring specialization;
    std::wstring poolId;            // empty if no managed pool wraps this sub-agent
    int readyInstanceCount = 0;
    int totalInstanceCount = 0;
    int activeLeaseCount = 0;
    int leaseCapacity = 0;
    int maxInstancesAllowed = 0;
    double utilizationPercent = 0.0;
    bool autoscaleEnabled = false;
    bool reachable = false;
    std::wstring endpointHostPort;
    std::wstring lastProbedAtUtc;
    std::wstring status;
    std::vector<ShellSubAgentLeaseHolder> activeClients;
};

// v0.8.3: parallel struct for MCP servers. Identical fields to
// ShellSubAgentRuntimeStat -- the only difference is the id field's
// name (mcpServerId vs subAgentId). ShellRuntime::CaptureSnapshot
// parses this from /api/dashboard.mcpServerRuntimeStats; the WinUI
// shell's Runtime section renders an MCP Servers card grid mirror of
// the Sub-Agents card grid built in v0.7.6. Reusing the lease-holder
// type because LAN-client attribution is identical regardless of
// endpoint kind.
using ShellMcpServerLeaseHolder = ShellSubAgentLeaseHolder;

struct ShellMcpServerRuntimeStat final {
    std::wstring mcpServerId;
    std::wstring displayName;
    std::wstring specialization;
    std::wstring poolId;
    int readyInstanceCount = 0;
    int totalInstanceCount = 0;
    int activeLeaseCount = 0;
    int leaseCapacity = 0;
    int maxInstancesAllowed = 0;
    double utilizationPercent = 0.0;
    bool autoscaleEnabled = false;
    bool reachable = false;
    std::wstring endpointHostPort;
    std::wstring lastProbedAtUtc;
    std::wstring status;
    std::vector<ShellMcpServerLeaseHolder> activeClients;
};



// Live activity stream — every incoming admin API request, governance
// execution, and governance decision flows through ActivityEventRing in the
// service host. The shell polls /api/activity?since={id} to fetch the latest
// events and render them in a scrolling command log.
struct ShellActivityEvent final {
    std::wstring id;
    std::wstring kind;
    std::wstring timestampUtc;
    std::wstring actor;
    std::wstring method;
    std::wstring target;
    int statusCode = 0;
    int latencyMs = 0;
    std::wstring message;
    std::wstring detail;
};

// v0.8.7: error event surfaced to the Overview tab's Error Reporting
// card. Sourced from /api/activity events with statusCode >= 400 OR
// kind/severity that conveys an error. ShellRuntime::CaptureSnapshot
// fills snapshot.errorEvents (capped at 50 most recent) so the
// Overview can render and the Export-to-JSON button can serialize.
struct ShellErrorEvent final {
    std::wstring id;
    std::wstring timestampUtc;
    std::wstring kind;       // activity kind (admin_api_request / governance_decision / ...)
    std::wstring severity;   // "warn" / "error" / "critical" -- inferred from statusCode if absent
    std::wstring source;     // actor + method + target where applicable
    int statusCode = 0;
    std::wstring message;
    std::wstring detail;
};

struct ShellActivityStreamResult final {
    std::wstring highWaterMarkId;
    std::vector<ShellActivityEvent> events;
    bool succeeded = false;
    std::wstring errorMessage;
};

// v0.9.75: visible self-test snapshot. ShellRuntime fetches GET
// /api/self-tests on every snapshot tick and parses the JSON body
// into this shape. The Overview surface renders it as a dedicated
// card (see OverviewSectionControl::ApplySelfTestCard) so the
// operator sees the live pass / fail breakdown -- not just the
// error rows that the existing v0.8.7 Error Reporting card already
// surfaces.
struct ShellSelfTestResult final {
    std::wstring name;
    std::wstring category;
    bool ok = false;
    std::wstring message;
    int durationMs = 0;
    std::wstring ranAtUtc;
};
struct ShellSelfTestSnapshot final {
    bool available = false;
    bool pending = true;       // true until the first sweep finishes
    std::wstring startedAtUtc;
    std::wstring finishedAtUtc;
    int totalCount = 0;
    int passedCount = 0;
    int failedCount = 0;
    std::vector<ShellSelfTestResult> results;
    std::wstring fetchError;   // non-empty when the HTTP fetch itself failed
};

// Auto-Connect AI Model — the shell hands the runtime just the credentials
// and optional role targets. Everything else (route id, display name, base
// url, model discovery, dpapi encryption, assignment fan-out) is automated.







struct ShellSecuritySettings final {
    bool enableTls = false;
    bool enableAuthentication = true;
    bool allowTroubleshootingBypass = false;
    bool allowOpenLanAccess = false;
    bool securityProtocolsEnabled = true;
    std::vector<std::wstring> trustedRemoteHosts;
};

struct ShellHostSettings final {
    std::wstring instanceName;
    std::wstring bindAddress;
    uint16_t browserPort = 7300;
    uint16_t beaconPort = 7301;
    bool beaconEnabled = false;
    int cpuAllocationPercent = 50;
    int memoryAllocationPercent = 50;
    int bandwidthAllocationPercent = 50;
    int storageAllocationPercent = 50;
};

struct ShellAppleRemoteHost final {
    std::wstring hostId;
    std::wstring displayName;
    std::wstring transport;
    std::vector<std::wstring> platforms;
    std::wstring address;
    uint16_t port = 0;
    std::wstring username;
    std::wstring serviceBaseUrl;
    std::wstring companionHealthPath;
    std::wstring companionExecutePath;
    std::wstring preferredDeveloperDirectory;
    std::wstring defaultSigningIdentity;
    std::wstring defaultNotaryKeychainProfile;
    std::wstring defaultNotaryTeamId;
    bool enabled = true;
    bool reachable = false;
    bool xcodeInstalled = false;
    std::wstring xcodeVersion;
    std::wstring developerDirectory;
    bool macosSdkAvailable = false;
    bool iosSdkAvailable = false;
    bool simulatorControlAvailable = false;
    bool deviceControlAvailable = false;
    std::wstring toolchainCheckedAtUtc;
    std::wstring toolchainStatus;
    std::wstring toolchainMessage;
    bool signingReady = false;
    bool developmentSigningReady = false;
    bool distributionSigningReady = false;
    std::vector<std::wstring> availableTeams;
    std::wstring signingStatus;
    std::wstring signingMessage;
    std::vector<std::wstring> simulatorRuntimes;
    std::wstring transportSummary;
    std::wstring credentialProfileSummary;
    std::vector<std::wstring> readinessIssues;
};

struct ShellAppleOperationRecord final {
    std::wstring operationId;
    std::wstring platform;
    std::wstring toolId;
    std::wstring displayName;
    std::wstring hostId;
    std::wstring hostDisplayName;
    std::wstring transport;
    std::wstring status;
    std::wstring workingDirectory;
    std::wstring artifactPath;
    std::wstring targetPath;
    std::wstring summary;
    std::wstring rawOutput;
    std::wstring errorMessage;
    std::wstring queuedAtUtc;
    std::wstring startedAtUtc;
    std::wstring completedAtUtc;
    std::map<std::wstring, std::wstring> requestOptions;
    std::wstring routeReason;
    std::wstring diagnosticSummary;
    std::wstring selectedDeveloperDirectory;
    std::wstring credentialProfileSummary;
    std::vector<std::wstring> readinessIssues;
    std::vector<std::wstring> redactedRequestOptionKeys;
    bool rerunReady = false;
    std::wstring rerunReadinessMessage;
};

struct ShellForsettiModule final {
    std::wstring moduleId;
    std::wstring displayName;
    std::wstring moduleType;
    std::wstring version;
    std::wstring entryPoint;
    std::vector<std::wstring> supportedPlatforms;
    std::vector<std::wstring> capabilitiesRequested;
    bool active = false;
    bool unlocked = false;
    bool protectedModule = false;
    std::wstring recommendedAction;
    std::wstring statusSummary;
};

struct ShellForsettiModuleCatalogResult final {
    bool succeeded = false;
    std::wstring message;
    std::vector<ShellForsettiModule> modules;
};

struct ShellOperationResult final {
    bool succeeded = false;
    bool requiresConfirmation = false;
    std::wstring message;
};

// Claude Code plugin (mcos-control) registration status surfaced from
// /api/claude-plugin/status. The runtime resolves the active console user
// via WTSGetActiveConsoleSessionId and reports back whether a junction
// already exists at <USERPROFILE>\.claude\plugins\mcos-control.
struct ShellClaudePluginStatus final {
    bool reachable = false;
    bool registered = false;
    bool activeUserResolved = false;
    std::wstring userName;
    std::wstring profileDir;
    std::wstring source;
    std::wstring target;
    std::wstring lastError;
    std::wstring transportError; // network / parse error reaching the API
};

// v0.10.12: ChatGPT / Grok "Direct AI Plugin Connection" status surfaced
// from /api/chatgpt-plugin/status and /api/grok-plugin/status. Same
// shape as ShellClaudePluginStatus but the registered=true state means
// a connector JSON file is present at
// <USERPROFILE>\Documents\MCOS\DirectAIControl\<providerId>-mcos-control.json
// instead of a directory junction.
struct ShellDirectAIPluginStatus final {
    bool reachable = false;
    bool registered = false;
    bool activeUserResolved = false;
    std::wstring providerId;
    std::wstring userName;
    std::wstring profileDir;
    std::wstring target;
    std::wstring lastError;
    std::wstring transportError;
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
    uint16_t beaconPort = 7301;
    bool beaconEnabled = false;
    bool aiAutonomyEnabled = false;
    bool advancedMode = false;          // WS5 — parity with browser progressive disclosure
    bool firstRunCompleted = false;     // WS1 — shell can route to Setup Readiness when false
    std::wstring setupMode = L"guided";
    std::wstring setupCurrentStep = L"welcome";
    std::wstring setupSecurityPosture = L"local-only";
    int setupMcpReadyCount = 0;
    int setupMcpMissingCount = 0;
    int setupWorkflowsReadyCount = 0;
    int setupWorkflowsMissingCount = 0;
    int setupSpecialistsReadyCount = 0;
    int setupSpecialistsMissingCount = 0;
    std::vector<std::wstring> setupReadinessIssues;
    bool securityProtocolsEnabled = true;
    bool openLanAccess = false;
    int cpuAllocationPercent = 50;
    int memoryAllocationPercent = 50;
    int bandwidthAllocationPercent = 50;
    int storageAllocationPercent = 50;
    size_t endpointCount = 0;
    size_t installCount = 0;
    size_t exportCount = 0;
    size_t governanceRoleCount = 0;
    size_t governanceRuleCount = 0;
    size_t governanceDocumentCount = 0;
    size_t governanceFindingCount = 0;
    size_t appleRemoteHostCount = 0;
    size_t appleOperationCount = 0;
    size_t platformGatewayCount = 0;
    size_t governanceServerCount = 0;
    size_t governanceExecutionCount = 0;
    std::wstring dashboardUrl;
    std::wstring configPath;
    std::wstring dataDirectory;
    std::wstring environmentName;
    std::wstring hostName;
    std::wstring operatingSystem;
    std::wstring primaryIpAddress;
    std::wstring primaryMacAddress;
    std::wstring telemetryCapturedAtUtc;
    std::wstring instanceName;
    std::wstring bindAddress;
    // v0.10.12: native MCP gateway URL captured from /api/dashboard
    // mcpGatewayStatus.mcpUrl. Raw value carries the configured bind
    // (typically wildcard "http://0.0.0.0:8080/mcp"); display surfaces
    // substitute the wildcard for snapshot.primaryIpAddress so the
    // Overview "APIs & Services + Gateway" card shows an address
    // external clients can actually reach. Empty when the service
    // hasn't surfaced a gateway status block yet (boot or
    // disabled-gateway configuration).
    std::wstring mcpGatewayUrl;
    std::wstring mcpGatewayState;
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
    std::vector<ShellSubAgentGroupDefinition> subAgentGroups;
    std::vector<ShellSubAgentRuntimeStat> subAgentRuntimeStats;
    // v0.8.3: parallel array for MCP servers. Same parser, same render
    // pipeline as sub-agents above.
    std::vector<ShellMcpServerRuntimeStat> mcpServerRuntimeStats;
    // v0.8.7: recent error events for the Overview tab Error Reporting
    // card. Capped at 50 entries; harvested from /api/activity by
    // ShellRuntime::CaptureSnapshot.
    std::vector<ShellErrorEvent> errorEvents;
    // v0.9.75: visible self-test card on the Overview surface. Populated
    // by ShellRuntime::CaptureSnapshot via GET /api/self-tests on
    // every tick so the per-probe pass/fail roster updates in real time.
    ShellSelfTestSnapshot selfTests;
    // v0.9.76: Supervisor Agent Assignment Wizard status snapshot.
    // Populated by ShellRuntime::CaptureSnapshot via GET /api/supervisor/status
    // on every tick so the Supervisor Agent card on the Overview surface
    // mirrors the running supervisor's lifecycle. activeProviderId is
    // empty when no supervisor is assigned (state == "off"); otherwise
    // it carries one of "chatgpt" / "claude" / "grok" -- the
    // single-selection wizard contract.
    struct SupervisorStatusFields {
        std::wstring activeProviderId;
        std::wstring providerDisplayName;
        std::wstring mode;
        std::wstring state;
        std::wstring assignmentId;
        std::wstring configId;
        std::wstring clientId;
        std::wstring issuedAtUtc;
        std::wstring expiresAtUtc;
        std::wstring connectedAtUtc;
        std::wstring lastHeartbeatUtc;
        std::wstring lastErrorMessage;
        bool active = false;
    } supervisorStatus;
    std::vector<ShellAppleRemoteHost> appleRemoteHosts;
    std::vector<ShellAppleOperationRecord> appleOperations;
    std::vector<ShellNavigationPointer> navigationPointers;
    std::vector<ShellToolbarItem> toolbarItems;
    std::vector<ShellOverlayRoute> overlayRoutes;
    std::map<std::wstring, std::vector<ShellViewInjection>> viewInjectionsBySlot;
    std::vector<std::wstring> endpointRows;
    std::vector<std::wstring> installRows;
    std::vector<std::wstring> exportRows;
    std::vector<std::wstring> governanceFindingRows;
    std::vector<std::wstring> governanceRoleRows;
    std::vector<std::wstring> governanceRuleRows;
    std::vector<std::wstring> governanceDocumentRows;
    std::vector<std::wstring> governanceActionRows;
    std::vector<std::wstring> appleRemoteHostRows;
    std::vector<std::wstring> appleOperationRows;
    std::vector<std::wstring> platformGatewayRows;
    std::vector<std::wstring> governanceServerRows;
    std::vector<std::wstring> governanceExecutionRows;
    std::wstring statusMessage;
};

class ShellRuntime final {
public:
    ShellRuntime() = default;

    [[nodiscard]] ShellSnapshot CaptureSnapshot() const;
    [[nodiscard]] bool StartService(std::wstring& message) const;
    [[nodiscard]] bool StopService(std::wstring& message) const;
    [[nodiscard]] ShellOperationResult UpsertMcpServer(const ShellRuntimeEndpoint& mcpServer) const;
    [[nodiscard]] ShellOperationResult RemoveMcpServer(const std::wstring& mcpServerId) const;
    [[nodiscard]] ShellOperationResult UpsertSubAgent(const ShellRuntimeEndpoint& subAgent) const;
    [[nodiscard]] ShellOperationResult RemoveSubAgent(const std::wstring& subAgentId) const;
    [[nodiscard]] ShellOperationResult UpsertAppleRemoteHost(const ShellAppleRemoteHost& host) const;
    [[nodiscard]] ShellOperationResult RemoveAppleRemoteHost(const std::wstring& hostId) const;
    [[nodiscard]] ShellOperationResult UpsertSubAgentGroup(const ShellSubAgentGroupDefinition& group) const;
    [[nodiscard]] ShellOperationResult RemoveSubAgentGroup(const std::wstring& groupId) const;
    [[nodiscard]] ShellOperationResult BeginSetup(const std::wstring& mode) const;
    [[nodiscard]] ShellOperationResult CompleteSetup() const;
    [[nodiscard]] ShellOperationResult DismissSetup() const;

    // Install a host-side AI CLI dependency (e.g. Claude Code CLI) on demand.
    // `bridge` is the dependency id; POSTs to /api/setup/dependencies/{id}/install.
    struct ShellCliDependencyInstallResult {
        bool succeeded = false;
        std::wstring status;         // "ready" | "installable" | "failed"
        std::wstring summary;
        std::wstring detail;
        std::wstring detectedVersion;
        std::wstring bridge;
        int exitCode = -1;
    };
    [[nodiscard]] ShellCliDependencyInstallResult InstallCliDependency(const std::wstring& bridge) const;

    // Claude Code plugin (mcos-control) registration toggle, mirroring
    // /api/claude-plugin/{status,toggle}. Status is read-only; Toggle flips
    // register <-> unregister and returns the new state.
    [[nodiscard]] ShellClaudePluginStatus FetchClaudePluginStatus() const;
    [[nodiscard]] ShellClaudePluginStatus ToggleClaudePlugin() const;
    // v0.10.12: ChatGPT / Grok Direct AI Plugin Connection toggles.
    // providerId is "chatgpt" or "grok"; any other value hits the
    // generic provider fallback on the route side. Both Fetch and
    // Toggle hit the /api/<providerId>-plugin/<status|toggle> route
    // and parse the JSON shape returned by directAIPluginStatusJson.
    [[nodiscard]] ShellDirectAIPluginStatus FetchDirectAIPluginStatus(const std::wstring& providerId) const;
    [[nodiscard]] ShellDirectAIPluginStatus ToggleDirectAIPlugin(const std::wstring& providerId) const;
    // v0.10.13: GET /api/supervisor/reachability-check. Returns the
    // raw response body so the caller can render the per-probe roster
    // verbatim, plus a small parsed-summary tuple for the headline.
    struct ShellSupervisorReachabilityResult {
        bool ok = false;
        bool allReachable = false;
        std::wstring transportError;
        std::wstring bodyText; // pretty-printable summary built from parsed JSON
    };
    [[nodiscard]] ShellSupervisorReachabilityResult CheckSupervisorReachability() const;
    // v0.9.75: re-run the boot self-test sweep on demand. POSTs
    // /api/self-tests/run and returns the freshly-computed snapshot.
    [[nodiscard]] ShellSelfTestSnapshot RunSelfTestsNow() const;

    // v0.9.76: Supervisor Agent Assignment Wizard surface. The Shell's
    // OverviewSectionControl drives the popup, single-toggle group, and
    // FileSavePicker; these helpers post to /api/supervisor/* and unpack
    // the response into Shell-side wstrings without dragging WinHTTP /
    // JSON parsing into the section control.
    struct ShellSupervisorIssueResult {
        bool ok = false;
        std::wstring errorMessage;
        std::wstring providerId;
        std::wstring assignmentId;
        std::wstring configId;
        std::wstring fileName;       // suggested default for the FileSavePicker
        std::wstring configJson;     // exact body to write through the picker
        std::wstring expiresAtUtc;
    };
    [[nodiscard]] ShellSupervisorIssueResult GenerateSupervisorConfig(const std::wstring& providerId) const;
    [[nodiscard]] bool RevokeSupervisor(const std::wstring& reason, std::wstring& errorOut) const;

    // Live activity stream: poll the service's ActivityEventRing for events
    // strictly newer than `sinceId`. Pass an empty string on first call to
    // receive the most recent buffer window. Subsequent calls should use
    // the `highWaterMarkId` returned by the previous call.
    [[nodiscard]] ShellActivityStreamResult FetchActivityEvents(const std::wstring& sinceId) const;
    [[nodiscard]] ShellForsettiModuleCatalogResult FetchForsettiModules() const;
    [[nodiscard]] ShellOperationResult ManageForsettiModule(const std::wstring& moduleId,
                                                           const std::wstring& action) const;
    [[nodiscard]] ShellOperationResult ExecuteGovernanceTool(const std::wstring& platform,
                                                            const std::wstring& toolId,
                                                            const std::wstring& targetPath,
                                                            const std::map<std::wstring, std::wstring>& options) const;
    [[nodiscard]] ShellOperationResult UpdateAiAutonomyEnabled(bool enabled) const;
    [[nodiscard]] ShellOperationResult UpdateSecuritySettings(const ShellSecuritySettings& settings,
                                                             bool confirmUnsafeChanges) const;
    [[nodiscard]] ShellOperationResult UpdateHostSettings(const ShellHostSettings& settings) const;
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
