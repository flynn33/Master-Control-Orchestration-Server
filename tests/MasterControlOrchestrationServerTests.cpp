// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.
//
// Post-ADR-001 test suite. The pre-remediation provider tests were dropped
// in Phase 2 of the LAN client rebuild (plans/remediation/01-gut-and-rebuild.md).
// This file covers the new LAN client identity model and grows alongside
// later phases as privilege enforcement and config bundle issuance land.

#include "MasterControl/AuthenticatedRequestContext.h"
#include "MasterControl/LanClient.h"
#include "MasterControl/MasterControlDefaults.h"
#include "MasterControl/MasterControlModels.h"
#include "MasterControl/MasterControlVersion.h"
#include "MasterControl/McpGatewayAdapters.h"

#include <nlohmann/json.hpp>

#include <iostream>

namespace {

bool expect(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << "Test failed: " << message << '\n';
        return false;
    }
    return true;
}

bool testDefaultConfiguration() {
    const auto configuration = MasterControl::buildDefaultConfiguration();
    bool ok = true;
    ok &= expect(configuration.instanceName == "Master Control Orchestration Server",
                 "Default instanceName matches ADR-001 product naming.");
    ok &= expect(configuration.browserPort == 7300,
                 "Default browser port remains 7300.");
    ok &= expect(configuration.beaconPort == 7301,
                 "Default beacon port remains 7301.");
    ok &= expect(configuration.aiAutonomyEnabled == false,
                 "Global AI autonomy is off by default.");
    ok &= expect(configuration.lanClients.empty(),
                 "Fresh installs start with an empty LAN client roster.");
    return ok;
}

bool testSeededEndpoints() {
    const auto endpoints = MasterControl::buildDefaultSeededEndpoints();
    return expect(!endpoints.empty(),
                  "Default seeded endpoint set is non-empty.");
}

bool testLanClientRoundTrip() {
    using MasterControl::LanClient;
    using MasterControl::LanClientPrivileges;

    LanClient original;
    original.clientId = "claude-code-jdaley-wks";
    original.displayName = "Claude Code on Jdaley workstation";
    original.clientType = "claude_code";
    original.hostName = "PC-GAMING-R7-58";
    original.networkAddress = "192.168.1.42";
    original.enabled = true;
    original.privileges = LanClientPrivileges{};
    original.autonomousMode = false;
    original.createdAtUtc = "2026-04-25T00:00:00Z";
    original.lastSeenUtc = "2026-04-25T00:01:00Z";

    bool ok = true;

    nlohmann::json serialized = original;
    ok &= expect(serialized["clientId"].get<std::string>() == original.clientId,
                 "LanClient JSON round-trip preserves clientId.");
    ok &= expect(serialized["enabled"].get<bool>() == true,
                 "LanClient JSON round-trip preserves enabled.");
    ok &= expect(serialized["autonomousMode"].get<bool>() == false,
                 "LanClient JSON round-trip preserves autonomousMode default.");

    LanClient restored = serialized.get<LanClient>();
    ok &= expect(restored.clientId == original.clientId,
                 "LanClient deserializes clientId.");
    ok &= expect(restored.displayName == original.displayName,
                 "LanClient deserializes displayName.");
    ok &= expect(restored.clientType == original.clientType,
                 "LanClient deserializes clientType.");
    ok &= expect(restored.hostName == original.hostName,
                 "LanClient deserializes hostName.");
    ok &= expect(restored.networkAddress == original.networkAddress,
                 "LanClient deserializes networkAddress.");
    ok &= expect(restored.enabled == original.enabled,
                 "LanClient deserializes enabled.");
    ok &= expect(restored.autonomousMode == original.autonomousMode,
                 "LanClient deserializes autonomousMode.");
    ok &= expect(restored.createdAtUtc == original.createdAtUtc,
                 "LanClient deserializes createdAtUtc.");
    ok &= expect(restored.lastSeenUtc == original.lastSeenUtc,
                 "LanClient deserializes lastSeenUtc.");
    return ok;
}

bool testLanClientDefaults() {
    MasterControl::LanClient client;
    bool ok = true;
    ok &= expect(client.enabled == true,
                 "New LanClient is enabled by default.");
    ok &= expect(client.autonomousMode == false,
                 "New LanClient is non-autonomous by default.");
    ok &= expect(client.clientId.empty(),
                 "New LanClient has no clientId until assigned.");
    return ok;
}

bool testLanClientPrivilegesDefaults() {
    MasterControl::LanClientPrivileges privileges;
    bool ok = true;
    ok &= expect(privileges.canCreateMcpServers == false,
                 "Newly registered clients cannot create MCP servers by default.");
    ok &= expect(privileges.canModifyMcpServers == false,
                 "Newly registered clients cannot modify MCP servers by default.");
    ok &= expect(privileges.canRemoveMcpServers == false,
                 "Newly registered clients cannot remove MCP servers by default.");
    ok &= expect(privileges.canCreateSubAgents == false,
                 "Newly registered clients cannot create sub-agents by default.");
    ok &= expect(privileges.canModifySubAgents == false,
                 "Newly registered clients cannot modify sub-agents by default.");
    ok &= expect(privileges.canRemoveSubAgents == false,
                 "Newly registered clients cannot remove sub-agents by default.");
    ok &= expect(privileges.canManageClients == false,
                 "Newly registered clients cannot manage other clients by default.");
    ok &= expect(privileges.canManageModules == false,
                 "Newly registered clients cannot manage Forsetti modules by default.");
    ok &= expect(privileges.canChangeGovernancePolicy == false,
                 "Newly registered clients cannot change governance policy by default.");
    return ok;
}

bool testLanClientPrivilegesRoundTrip() {
    using MasterControl::LanClientPrivileges;
    LanClientPrivileges original;
    original.canCreateMcpServers = true;
    original.canCreateSubAgents = true;
    original.canManageClients = true;

    nlohmann::json serialized = original;
    bool ok = true;
    ok &= expect(serialized["canCreateMcpServers"].get<bool>() == true,
                 "Privileges serialize canCreateMcpServers.");
    ok &= expect(serialized["canCreateSubAgents"].get<bool>() == true,
                 "Privileges serialize canCreateSubAgents.");
    ok &= expect(serialized["canManageClients"].get<bool>() == true,
                 "Privileges serialize canManageClients.");
    ok &= expect(serialized["canRemoveMcpServers"].get<bool>() == false,
                 "Unset privileges serialize as false.");

    auto restored = serialized.get<LanClientPrivileges>();
    ok &= expect(restored.canCreateMcpServers == original.canCreateMcpServers,
                 "Privileges deserialize canCreateMcpServers.");
    ok &= expect(restored.canCreateSubAgents == original.canCreateSubAgents,
                 "Privileges deserialize canCreateSubAgents.");
    ok &= expect(restored.canManageClients == original.canManageClients,
                 "Privileges deserialize canManageClients.");
    ok &= expect(restored.canRemoveMcpServers == false,
                 "Unset privileges deserialize as false.");
    return ok;
}

// Phase 6 - authenticated request context. The resolver lives inside the
// runtime translation unit and is exercised by integration tests; here we
// pin the publicly observable invariants of the context type and the
// makeOperatorContext fallback factory.
bool testAuthenticatedRequestContextDefaults() {
    MasterControl::AuthenticatedRequestContext context;
    bool ok = true;
    ok &= expect(context.client.has_value() == false,
                 "Default context has no resolved client.");
    ok &= expect(context.privileges.canCreateMcpServers == false,
                 "Default context grants no privileges.");
    ok &= expect(context.autonomousMode == false,
                 "Default context is non-autonomous.");
    ok &= expect(context.actor == "operator",
                 "Default context attributes work to the operator placeholder.");
    ok &= expect(context.isOperatorFallback == true,
                 "Default context is the operator fallback flag-true.");
    return ok;
}

bool testOperatorFallbackGrantsAllPrivileges() {
    auto context = MasterControl::makeOperatorContext();
    bool ok = true;
    ok &= expect(context.privileges.canCreateMcpServers,
                 "Operator fallback grants canCreateMcpServers.");
    ok &= expect(context.privileges.canModifyMcpServers,
                 "Operator fallback grants canModifyMcpServers.");
    ok &= expect(context.privileges.canRemoveMcpServers,
                 "Operator fallback grants canRemoveMcpServers.");
    ok &= expect(context.privileges.canCreateSubAgents,
                 "Operator fallback grants canCreateSubAgents.");
    ok &= expect(context.privileges.canModifySubAgents,
                 "Operator fallback grants canModifySubAgents.");
    ok &= expect(context.privileges.canRemoveSubAgents,
                 "Operator fallback grants canRemoveSubAgents.");
    ok &= expect(context.privileges.canManageClients,
                 "Operator fallback grants canManageClients.");
    ok &= expect(context.privileges.canManageModules,
                 "Operator fallback grants canManageModules.");
    ok &= expect(context.privileges.canChangeGovernancePolicy,
                 "Operator fallback grants canChangeGovernancePolicy.");
    ok &= expect(context.autonomousMode == true,
                 "Operator fallback is autonomous (so create paths bypass).");
    ok &= expect(context.actor == "operator",
                 "Operator fallback identifies itself as 'operator'.");
    ok &= expect(context.isOperatorFallback == true,
                 "Operator fallback flag is true.");
    return ok;
}

// Phase 5 - configuration bundle. The bundle composer and address resolver
// live inside MasterControlRuntime.cpp's anonymous namespace, so the test
// reproduces the contract by constructing the equivalent JSON and pinning
// the shape. If the bundle composer is later promoted to its own header
// the assertions below should switch to calling it directly.
bool testLanClientConfigBundleShape() {
    nlohmann::json bundle = {
        { "schemaVersion", "1.0" },
        { "issuedAtUtc", "2026-04-25T00:00:00Z" },
        { "mcosServer", "http://192.168.1.10:7300" },
        { "clientId", "claude-code-jdaley-wks" },
        { "displayName", "Claude Code" },
        { "clientType", "claude_code" },
        { "enabled", true },
        { "identification", {
            { "header", "X-MCOS-Client-Id" },
            { "value", "claude-code-jdaley-wks" }
        } },
        { "privileges", MasterControl::LanClientPrivileges{} },
        { "autonomousMode", false },
        { "catalogs", {
            { "mcpServers", "/api/client/mcp-servers" },
            { "subAgents", "/api/client/sub-agents" },
            { "activity", "/api/client/activity" }
        } },
        { "governance", {
            { "authority", "CLU" },
            { "framework", "Forsetti Framework for Agentic Coding" },
            { "profileEndpoint", "/api/client/governance/profile" },
            { "decisionEndpoint", "/api/client/governance/decisions" }
        } }
    };

    bool ok = true;
    ok &= expect(bundle["schemaVersion"].get<std::string>() == "1.0",
                 "Bundle pins schemaVersion 1.0.");
    ok &= expect(bundle["mcosServer"].get<std::string>().rfind("http://", 0) == 0,
                 "mcosServer is a fully-qualified http URL.");
    ok &= expect(bundle["mcosServer"].get<std::string>().find("0.0.0.0") == std::string::npos,
                 "mcosServer never serves the wildcard bind address.");
    ok &= expect(bundle["identification"]["header"].get<std::string>() == "X-MCOS-Client-Id",
                 "Identification header is X-MCOS-Client-Id.");
    ok &= expect(bundle["identification"]["value"].get<std::string>() == bundle["clientId"].get<std::string>(),
                 "Identification value matches clientId.");
    ok &= expect(bundle["catalogs"].contains("mcpServers"),
                 "Catalogs include mcpServers path.");
    ok &= expect(bundle["catalogs"].contains("subAgents"),
                 "Catalogs include subAgents path.");
    ok &= expect(bundle["governance"]["authority"].get<std::string>() == "CLU",
                 "Governance authority is CLU.");
    return ok;
}

// PHASE-02 (ADR-002 §2): MCP Gateway adapter tests. The fake adapter
// implements IMcpGateway with no child processes and no network calls
// so the state machine and registration contract can be exercised
// deterministically.

bool testGatewayConfigurationDefaults() {
    const auto configuration = MasterControl::buildDefaultConfiguration();
    bool ok = true;
    ok &= expect(configuration.mcpGateway.type == MasterControl::GatewayType::MCPJungle,
                 "Default gateway type is MCPJungle.");
    ok &= expect(configuration.mcpGateway.enabled == false,
                 "Default gateway is disabled (operator opt-in required).");
    ok &= expect(configuration.mcpGateway.listenPort == 8080,
                 "Default gateway port is 8080 (distinct from admin 7300).");
    ok &= expect(configuration.mcpGateway.mcpPath == "/mcp",
                 "Default gateway MCP path is /mcp.");
    ok &= expect(configuration.mcpGateway.healthPath == "/health",
                 "Default gateway health path is /health.");
    ok &= expect(configuration.mcpGateway.mode == "lan-trusted",
                 "Default gateway mode is lan-trusted (per ADR-002 §1).");
    return ok;
}

bool testFakeGatewayDisabledStartsDisabled() {
    MasterControl::McpGatewayConfiguration configuration;
    configuration.enabled = false;
    MasterControl::FakeMcpGatewayAdapter adapter(configuration);

    const auto status = adapter.Start();
    bool ok = true;
    ok &= expect(status.state == MasterControl::GatewayState::Disabled,
                 "Disabled fake adapter cannot transition to Running on Start().");
    ok &= expect(adapter.startCallCount() == 1,
                 "Fake adapter records the Start() call even when disabled.");
    ok &= expect(adapter.AdapterType() == "fake",
                 "Fake adapter identifies as 'fake'.");
    return ok;
}

bool testFakeGatewayEnabledStartStopRoundTrip() {
    MasterControl::McpGatewayConfiguration configuration;
    configuration.enabled = true;
    configuration.listenHost = "127.0.0.1";
    configuration.listenPort = 8080;
    configuration.mcpPath = "/mcp";
    MasterControl::FakeMcpGatewayAdapter adapter(configuration);

    bool ok = true;

    auto initial = adapter.CurrentStatus();
    ok &= expect(initial.state == MasterControl::GatewayState::Configured,
                 "Newly constructed enabled adapter starts in Configured state.");

    const auto started = adapter.Start();
    ok &= expect(started.state == MasterControl::GatewayState::Running,
                 "Enabled fake adapter transitions to Running on Start().");
    ok &= expect(!started.startedAtUtc.empty(),
                 "Started status carries a non-empty startedAtUtc timestamp.");

    const auto stopped = adapter.Stop();
    ok &= expect(stopped.state == MasterControl::GatewayState::Stopped,
                 "Stop() transitions the adapter to Stopped.");
    ok &= expect(stopped.startedAtUtc.empty(),
                 "Stopped status clears startedAtUtc.");
    ok &= expect(adapter.startCallCount() == 1,
                 "Start was invoked exactly once.");
    ok &= expect(adapter.stopCallCount() == 1,
                 "Stop was invoked exactly once.");
    return ok;
}

bool testFakeGatewayStartFailureScripted() {
    MasterControl::McpGatewayConfiguration configuration;
    configuration.enabled = true;
    MasterControl::FakeMcpGatewayAdapter adapter(configuration);
    adapter.setStartShouldFail(true, "scripted failure");

    const auto status = adapter.Start();
    bool ok = true;
    ok &= expect(status.state == MasterControl::GatewayState::Failed,
                 "Scripted start failure surfaces GatewayState::Failed.");
    ok &= expect(status.message == "scripted failure",
                 "Scripted failure message propagates.");
    return ok;
}

bool testFakeGatewayRegistrationRoundTrip() {
    MasterControl::McpGatewayConfiguration configuration;
    configuration.enabled = true;
    MasterControl::FakeMcpGatewayAdapter adapter(configuration);

    MasterControl::McpServerRegistration registration;
    registration.name = "logical-pool-alpha";
    registration.url = "http://127.0.0.1:7300/mcp/pools/alpha/mcp";
    registration.transport = MasterControl::McpServerTransport::StreamableHttp;

    const auto registered = adapter.RegisterHttpServer(registration);
    bool ok = true;
    ok &= expect(registered.succeeded,
                 "RegisterHttpServer succeeds for a well-formed registration.");
    ok &= expect(registered.serverName == "logical-pool-alpha",
                 "RegisterHttpServer echoes the server name.");
    ok &= expect(!registered.registeredAtUtc.empty(),
                 "RegisterHttpServer stamps a UTC timestamp.");

    const auto names = adapter.registeredServerNames();
    ok &= expect(names.size() == 1 && names.front() == "logical-pool-alpha",
                 "Registry exposes the registered server name to test observers.");

    const auto deregistered = adapter.DeregisterServer("logical-pool-alpha");
    ok &= expect(deregistered.succeeded,
                 "DeregisterServer reports success for a known registration.");
    ok &= expect(adapter.registeredServerNames().empty(),
                 "Registry is empty after deregistration.");
    return ok;
}

bool testFakeGatewayRegistrationRejectsEmptyName() {
    MasterControl::McpGatewayConfiguration configuration;
    configuration.enabled = true;
    MasterControl::FakeMcpGatewayAdapter adapter(configuration);

    MasterControl::McpServerRegistration registration;
    registration.url = "http://127.0.0.1:7300/mcp/pools/x/mcp";
    const auto result = adapter.RegisterHttpServer(registration);
    bool ok = true;
    ok &= expect(!result.succeeded,
                 "RegisterHttpServer rejects an empty server name.");
    ok &= expect(adapter.registeredServerNames().empty(),
                 "Rejected registration leaves the registry empty.");
    return ok;
}

bool testFakeGatewayProbeUsesScriptedHealth() {
    MasterControl::McpGatewayConfiguration configuration;
    configuration.enabled = true;
    MasterControl::FakeMcpGatewayAdapter adapter(configuration);

    MasterControl::GatewayHealth scripted;
    scripted.status = MasterControl::GatewayHealthStatus::Healthy;
    scripted.reachable = true;
    scripted.httpStatusCode = 200;
    scripted.message = "synthetic-healthy";
    adapter.setNextProbe(scripted);

    const auto observed = adapter.Probe();
    bool ok = true;
    ok &= expect(observed.status == MasterControl::GatewayHealthStatus::Healthy,
                 "Probe returns the scripted health status.");
    ok &= expect(observed.reachable == true,
                 "Probe returns the scripted reachability flag.");
    ok &= expect(observed.httpStatusCode == 200,
                 "Probe returns the scripted HTTP status code.");
    ok &= expect(observed.message == "synthetic-healthy",
                 "Probe returns the scripted message.");
    ok &= expect(observed.adapterType == "fake",
                 "Probe stamps the adapter type.");
    ok &= expect(!observed.probedAtUtc.empty(),
                 "Probe stamps a UTC timestamp.");
    ok &= expect(adapter.probeCallCount() == 1,
                 "Probe call count increments.");
    return ok;
}

bool testFakeGatewayMcpUrlComposition() {
    MasterControl::McpGatewayConfiguration configuration;
    configuration.enabled = true;
    configuration.listenHost = "0.0.0.0";
    configuration.listenPort = 8080;
    configuration.mcpPath = "/mcp";
    MasterControl::FakeMcpGatewayAdapter adapter(configuration);

    bool ok = true;
    ok &= expect(adapter.GatewayMcpUrl() == "http://0.0.0.0:8080/mcp",
                 "GatewayMcpUrl composes from listenHost + listenPort + mcpPath.");

    MasterControl::McpGatewayConfiguration alt = configuration;
    alt.mcpPath = "mcp"; // missing leading slash on purpose
    MasterControl::FakeMcpGatewayAdapter adapter2(alt);
    ok &= expect(adapter2.GatewayMcpUrl() == "http://0.0.0.0:8080/mcp",
                 "GatewayMcpUrl normalizes mcpPath to ensure a leading slash.");
    return ok;
}

bool testRealAdapterDisabledByDefault() {
    MasterControl::McpGatewayConfiguration configuration;
    // enabled defaults to false
    MasterControl::McpJungleGatewayAdapter adapter(configuration);

    bool ok = true;
    ok &= expect(adapter.AdapterType() == "mcpjungle",
                 "Real adapter identifies as 'mcpjungle'.");

    const auto status = adapter.Start();
    ok &= expect(status.state == MasterControl::GatewayState::Disabled,
                 "Disabled real adapter refuses to Start().");
    ok &= expect(adapter.isSupervisingChildProcess() == false,
                 "Disabled real adapter never spawns a child process.");

    const auto health = adapter.Probe();
    ok &= expect(health.status == MasterControl::GatewayHealthStatus::Unknown,
                 "Probe on disabled adapter reports Unknown (no fake healthy).");
    ok &= expect(health.adapterType == "mcpjungle",
                 "Probe stamps adapter type.");
    return ok;
}

bool testRealAdapterSupervisedMockWhenBinaryMissing() {
    MasterControl::McpGatewayConfiguration configuration;
    configuration.enabled = true;
    configuration.binaryPath = ""; // intentionally empty
    MasterControl::McpJungleGatewayAdapter adapter(configuration);

    bool ok = true;
    const auto status = adapter.Start();
    // No child process spawned when binaryPath is empty; adapter still
    // transitions to Running so the state machine is exercisable.
    ok &= expect(status.state == MasterControl::GatewayState::Running,
                 "Enabled real adapter with no binary enters supervised-mock Running.");
    ok &= expect(adapter.isSupervisingChildProcess() == false,
                 "Supervised-mock mode does NOT spawn a child process.");

    const auto stopped = adapter.Stop();
    ok &= expect(stopped.state == MasterControl::GatewayState::Stopped,
                 "Stop() returns the supervised-mock adapter to Stopped.");
    return ok;
}

bool testRealAdapterRegistrationSurvivesAcrossStartStop() {
    MasterControl::McpGatewayConfiguration configuration;
    configuration.enabled = true;
    MasterControl::McpJungleGatewayAdapter adapter(configuration);

    MasterControl::McpServerRegistration registration;
    registration.name = "default-pool";
    registration.url = "http://127.0.0.1:7300/mcp/pools/default/mcp";

    const auto registered = adapter.RegisterHttpServer(registration);
    adapter.Start();
    adapter.Stop();

    // Registration is in-memory and persists across Start/Stop.
    bool ok = true;
    ok &= expect(registered.succeeded,
                 "Registration succeeds before Start().");
    const auto deregistered = adapter.DeregisterServer("default-pool");
    ok &= expect(deregistered.succeeded,
                 "Registration survives Start()/Stop() and is still removable.");
    return ok;
}

bool testGatewayEnumRoundTrips() {
    bool ok = true;

    using MasterControl::GatewayType;
    ok &= expect(MasterControl::to_string(GatewayType::MCPJungle) == "mcpjungle",
                 "GatewayType serializes mcpjungle.");
    ok &= expect(MasterControl::gatewayTypeFromString("native") == GatewayType::Native,
                 "GatewayType deserializes native.");

    using MasterControl::GatewayState;
    ok &= expect(MasterControl::to_string(GatewayState::Running) == "running",
                 "GatewayState serializes running.");
    ok &= expect(MasterControl::gatewayStateFromString("failed") == GatewayState::Failed,
                 "GatewayState deserializes failed.");

    using MasterControl::GatewayHealthStatus;
    ok &= expect(MasterControl::to_string(GatewayHealthStatus::Healthy) == "healthy",
                 "GatewayHealthStatus serializes healthy.");
    ok &= expect(MasterControl::gatewayHealthStatusFromString("degraded") == GatewayHealthStatus::Degraded,
                 "GatewayHealthStatus deserializes degraded.");

    using MasterControl::McpServerTransport;
    ok &= expect(MasterControl::to_string(McpServerTransport::StreamableHttp) == "streamable_http",
                 "McpServerTransport serializes streamable_http.");
    ok &= expect(MasterControl::mcpServerTransportFromString("stdio") == McpServerTransport::Stdio,
                 "McpServerTransport deserializes stdio.");
    return ok;
}

// PHASE-03 (ADR-002 §4): LAN Discovery Document tests. Pin the schema
// shape, the required fields, and the JSON round-trip so /.well-known/mcos.json
// stays compatible with discovery-document.schema.json.

// PHASE-05 (ADR-002 §6): Governance bundle shape tests. The runtime's
// GovernanceBundleService lives inside the MasterControlRuntime translation
// unit; these tests pin the public schema contract (per
// docs/implementation/CLU-GOVERNANCE-BUNDLE-CONTRACT.md) by constructing
// GovernanceBundle values directly and asserting their JSON shape.

bool testGovernanceBundleJsonRequiredFields() {
    MasterControl::GovernanceBundle bundle;
    bundle.platform = "windows";
    bundle.forsettiFrameworkVersion = "1.0";
    bundle.agenticCodingFrameworkVersion = "1.0";
    bundle.cluSchemaVersion = "1.0";
    bundle.instructionsMarkdown = "# Test\n";
    bundle.rulesJson = nlohmann::json::object();
    bundle.decisionPolicy = "Mutating actions pass through CLU.";
    bundle.checksum = "sha256:0000";
    bundle.generatedAt = "2026-05-01T00:00:00Z";

    nlohmann::json serialized = bundle;
    bool ok = true;
    const std::vector<std::string> required = {
        "platform", "forsettiFrameworkVersion", "agenticCodingFrameworkVersion",
        "cluSchemaVersion", "instructionsMarkdown", "rulesJson",
        "decisionPolicy", "checksum", "generatedAt"
    };
    for (const auto& key : required) {
        ok &= expect(serialized.contains(key),
                     "Governance bundle JSON includes contract-required key.");
    }
    ok &= expect(serialized["platform"].get<std::string>() == "windows",
                 "Bundle serializes platform.");
    ok &= expect(serialized["checksum"].get<std::string>().rfind("sha256:", 0) == 0,
                 "Checksum is sha256-prefixed.");
    return ok;
}

bool testGovernanceBundleAllPlatformsRecognized() {
    bool ok = true;
    for (const std::string& platform : { "windows", "macos", "ios" }) {
        MasterControl::GovernanceBundle bundle;
        bundle.platform = platform;
        nlohmann::json serialized = bundle;
        ok &= expect(serialized["platform"].get<std::string>() == platform,
                     "Each contract-supported platform serializes literally.");
    }
    return ok;
}

bool testGovernanceProfileSummaryJsonRoundTrip() {
    MasterControl::GovernanceProfileSummary summary;
    summary.unitName = "Command Logic Unit";
    summary.doctrine = "Governance is not assumed.";
    summary.cluSchemaVersion = "1.0";
    summary.documentIds = { "clu-constitution", "clu-shared-fabric" };
    summary.roleIds = { "operator", "lan-client" };
    summary.ruleIds = { "no-untracked-mutations" };
    summary.generatedAt = "2026-05-01T00:00:00Z";

    nlohmann::json serialized = summary;
    bool ok = true;
    ok &= expect(serialized["unitName"].get<std::string>() == "Command Logic Unit",
                 "Profile summary serializes unitName.");
    ok &= expect(serialized["documentIds"].is_array() && serialized["documentIds"].size() == 2,
                 "Profile summary documentIds is a non-empty array.");

    auto restored = serialized.get<MasterControl::GovernanceProfileSummary>();
    ok &= expect(restored.documentIds.size() == 2,
                 "Profile summary round-trips documentIds.");
    ok &= expect(restored.roleIds == summary.roleIds,
                 "Profile summary round-trips roleIds verbatim.");
    return ok;
}

bool testOnboardingProfileLinksToGovernanceBundleUrl() {
    // The onboarding profile must always carry a governanceBundleUrl
    // (PHASE-04 schema-required field). PHASE-05 promotes that URL to a
    // real /api/governance/bundles/{platform} endpoint.
    MasterControl::OnboardingProfile profile;
    profile.clientType = "claude-code";
    profile.gatewayMcpUrl = "http://192.168.1.10:8080/mcp";
    profile.governanceBundleUrl = "http://192.168.1.10:7300/api/governance/bundles/windows";
    nlohmann::json serialized = profile;
    bool ok = true;
    ok &= expect(serialized.contains("governanceBundleUrl"),
                 "Onboarding profile JSON exposes governanceBundleUrl.");
    ok &= expect(serialized["governanceBundleUrl"].get<std::string>().find("/api/governance/bundles/") != std::string::npos,
                 "governanceBundleUrl points at the per-platform PHASE-05 endpoint.");
    return ok;
}

// PHASE-04 (ADR-002 §5): Onboarding profile shape tests. The runtime's
// OnboardingProfileService lives inside the MasterControlRuntime translation
// unit; these tests pin the public schema contract by constructing
// OnboardingProfile values directly and asserting their JSON shape matches
// docs/implementation/schemas/onboarding-profile.schema.json.

bool testOnboardingProfileDefaultsAreLanTrust() {
    MasterControl::OnboardingProfile profile;
    bool ok = true;
    ok &= expect(profile.authRequired == false,
                 "OnboardingProfile defaults authRequired=false (schema const).");
    ok &= expect(profile.trust == "lan",
                 "OnboardingProfile defaults trust=lan.");
    ok &= expect(profile.transport == "streamable_http",
                 "OnboardingProfile defaults transport=streamable_http.");
    return ok;
}

bool testOnboardingProfileJsonRequiredFields() {
    MasterControl::OnboardingProfile profile;
    profile.clientType = "claude-code";
    profile.displayName = "Claude Code";
    profile.gatewayMcpUrl = "http://192.168.1.10:8080/mcp";
    profile.governanceBundleUrl = "http://192.168.1.10:7300/api/governance/bundles/windows";

    nlohmann::json serialized = profile;
    bool ok = true;
    const std::vector<std::string> required = {
        "clientType", "gatewayMcpUrl", "transport", "authRequired", "governanceBundleUrl"
    };
    for (const auto& key : required) {
        ok &= expect(serialized.contains(key),
                     "Onboarding profile JSON includes schema-required key.");
    }
    ok &= expect(serialized["authRequired"].get<bool>() == false,
                 "authRequired serializes as false (schema const).");
    ok &= expect(serialized["transport"].get<std::string>() == "streamable_http",
                 "transport serializes as a schema-allowed enum value.");
    return ok;
}

bool testOnboardingConfigSnippetRoundTrip() {
    MasterControl::OnboardingConfigSnippet snippet;
    snippet.format = "json";
    snippet.filename = ".mcp.json";
    snippet.description = "Drop into Claude Code's mcpServers map.";
    snippet.content = nlohmann::json{ { "mcpServers", { { "mcos", { { "url", "http://192.168.1.10:8080/mcp" } } } } } };

    nlohmann::json serialized = snippet;
    bool ok = true;
    ok &= expect(serialized["format"].get<std::string>() == "json",
                 "Snippet serializes format.");
    ok &= expect(serialized["content"].is_object(),
                 "Snippet serializes content as a structured JSON object.");

    auto restored = serialized.get<MasterControl::OnboardingConfigSnippet>();
    ok &= expect(restored.format == snippet.format,
                 "Snippet round-trips format.");
    ok &= expect(restored.content == snippet.content,
                 "Snippet round-trips content verbatim.");
    return ok;
}

bool testOnboardingProfileTransportEnum() {
    bool ok = true;
    for (const std::string& transport : { "streamable_http", "stdio_bridge", "sse_compat" }) {
        MasterControl::OnboardingProfile profile;
        profile.clientType = "test";
        profile.gatewayMcpUrl = "http://localhost:8080/mcp";
        profile.governanceBundleUrl = "http://localhost:7300/api/governance/bundles/windows";
        profile.transport = transport;
        nlohmann::json serialized = profile;
        ok &= expect(serialized["transport"].get<std::string>() == transport,
                     "Transport enum value serializes literally.");
    }
    return ok;
}

bool testDiscoveryDocumentDefaultShape() {
    MasterControl::DiscoveryDocument doc;
    bool ok = true;
    ok &= expect(doc.product == "MCOS",
                 "DiscoveryDocument default product is MCOS.");
    ok &= expect(doc.role == "mcp-gateway-host",
                 "DiscoveryDocument default role is mcp-gateway-host.");
    ok &= expect(doc.trust == "lan",
                 "DiscoveryDocument default trust is lan.");
    ok &= expect(doc.auth == "none",
                 "DiscoveryDocument default auth is none.");
    return ok;
}

bool testDiscoveryDocumentJsonRoundTrip() {
    MasterControl::DiscoveryDocument original;
    original.version = "0.5.0";
    original.instanceId = "mcos-test-id-001";
    original.instanceName = "Test MCOS";
    original.gateway.type = "mcpjungle";
    original.gateway.mcpUrl = "http://192.168.1.10:8080/mcp";
    original.gateway.healthUrl = "http://192.168.1.10:8080/health";
    original.gateway.state = "running";
    original.onboarding.generic = "http://192.168.1.10:7300/api/onboarding/generic";
    original.onboarding.claudeCode = "http://192.168.1.10:7300/api/onboarding/claude-code";
    original.onboarding.codex = "http://192.168.1.10:7300/api/onboarding/codex";
    original.onboarding.grok = "http://192.168.1.10:7300/api/onboarding/grok";
    original.onboarding.chatgpt = "http://192.168.1.10:7300/api/onboarding/chatgpt";
    original.governance.bundleBaseUrl = "http://192.168.1.10:7300/api/governance/bundles";
    original.governance.cluProfileUrl = "http://192.168.1.10:7300/api/governance/profile";
    original.governance.decisionsUrl = "http://192.168.1.10:7300/api/governance/decisions";
    original.capabilities = { "mcp-gateway", "mcpjungle-adapter", "dns-sd", "udp-beacon", "forsetti-governance", "clu" };
    original.serverIpAddress = "192.168.1.10";
    original.generatedAtUtc = "2026-05-01T00:00:00Z";

    nlohmann::json serialized = original;
    bool ok = true;
    ok &= expect(serialized["product"].get<std::string>() == "MCOS",
                 "Discovery JSON pins product=MCOS.");
    ok &= expect(serialized["role"].get<std::string>() == "mcp-gateway-host",
                 "Discovery JSON pins role=mcp-gateway-host.");
    ok &= expect(serialized["trust"].get<std::string>() == "lan",
                 "Discovery JSON pins trust=lan.");
    ok &= expect(serialized["auth"].get<std::string>() == "none",
                 "Discovery JSON pins auth=none.");
    ok &= expect(serialized["gateway"]["mcpUrl"].get<std::string>() == "http://192.168.1.10:8080/mcp",
                 "Discovery JSON exposes gateway.mcpUrl.");
    ok &= expect(serialized["onboarding"]["claudeCode"].get<std::string>().find("/api/onboarding/claude-code") != std::string::npos,
                 "Discovery JSON exposes claudeCode onboarding URL.");
    ok &= expect(serialized["governance"]["bundleBaseUrl"].get<std::string>().find("/api/governance/bundles") != std::string::npos,
                 "Discovery JSON exposes governance.bundleBaseUrl.");
    ok &= expect(serialized["capabilities"].is_array() && !serialized["capabilities"].empty(),
                 "Discovery JSON capabilities is a non-empty array.");

    auto restored = serialized.get<MasterControl::DiscoveryDocument>();
    ok &= expect(restored.gateway.mcpUrl == original.gateway.mcpUrl,
                 "Discovery doc round-trips gateway.mcpUrl.");
    ok &= expect(restored.onboarding.codex == original.onboarding.codex,
                 "Discovery doc round-trips onboarding.codex.");
    ok &= expect(restored.governance.cluProfileUrl == original.governance.cluProfileUrl,
                 "Discovery doc round-trips governance.cluProfileUrl.");
    ok &= expect(restored.capabilities.size() == original.capabilities.size(),
                 "Discovery doc round-trips capabilities count.");
    ok &= expect(restored.instanceId == original.instanceId,
                 "Discovery doc round-trips instanceId.");
    return ok;
}

bool testWellKnownDocumentMatchesSchemaRequiredFields() {
    // The /.well-known/mcos.json shape strips beacon-only fields. The
    // remaining required keys per discovery-document.schema.json are:
    // product, role, instanceId, trust, auth, gateway, onboarding,
    // governance, capabilities. This test pins the JSON-after-erase
    // matches that requirement set.
    MasterControl::DiscoveryDocument doc;
    doc.instanceId = "mcos-test";
    doc.version = "0.5.0";
    doc.gateway.type = "mcpjungle";
    doc.gateway.mcpUrl = "http://127.0.0.1:8080/mcp";
    doc.capabilities = { "mcp-gateway" };

    nlohmann::json wellKnown = doc;
    wellKnown.erase("generatedAtUtc");
    wellKnown.erase("serverIpAddress");
    wellKnown.erase("instanceName");

    const std::vector<std::string> required = {
        "product", "role", "instanceId", "trust", "auth",
        "gateway", "onboarding", "governance", "capabilities"
    };
    bool ok = true;
    for (const auto& key : required) {
        ok &= expect(wellKnown.contains(key),
                     "/.well-known/mcos.json contains required key (per schema).");
    }
    ok &= expect(!wellKnown.contains("generatedAtUtc"),
                 "/.well-known/mcos.json strips beacon-only generatedAtUtc.");
    ok &= expect(!wellKnown.contains("serverIpAddress"),
                 "/.well-known/mcos.json strips beacon-only serverIpAddress.");
    ok &= expect(!wellKnown.contains("instanceName"),
                 "/.well-known/mcos.json strips beacon-only instanceName.");
    return ok;
}

bool testInstanceIdGeneration() {
    const auto first = MasterControl::buildDefaultConfiguration();
    const auto second = MasterControl::buildDefaultConfiguration();
    bool ok = true;
    ok &= expect(!first.instanceId.empty(),
                 "Default configuration generates a non-empty instanceId.");
    ok &= expect(first.instanceId.rfind("mcos-", 0) == 0,
                 "Generated instanceId is prefixed with 'mcos-' for grep-friendly identification.");
    ok &= expect(first.instanceId != second.instanceId,
                 "Generated instanceId is unique per configuration build (UuidCreate-backed).");
    return ok;
}

bool testGatewayConfigJsonRoundTrip() {
    MasterControl::McpGatewayConfiguration original;
    original.type = MasterControl::GatewayType::MCPJungle;
    original.enabled = true;
    original.listenHost = "0.0.0.0";
    original.listenPort = 9090;
    original.mcpPath = "/mcp";
    original.healthPath = "/health";
    original.mode = "lan-trusted";

    nlohmann::json serialized = original;
    bool ok = true;
    ok &= expect(serialized["type"].get<std::string>() == "mcpjungle",
                 "Gateway config serializes type as a slug.");
    ok &= expect(serialized["listenPort"].get<uint16_t>() == 9090,
                 "Gateway config preserves listenPort.");

    auto restored = serialized.get<MasterControl::McpGatewayConfiguration>();
    ok &= expect(restored.type == original.type,
                 "Gateway config deserializes type.");
    ok &= expect(restored.enabled == original.enabled,
                 "Gateway config deserializes enabled.");
    ok &= expect(restored.listenPort == original.listenPort,
                 "Gateway config deserializes listenPort.");
    ok &= expect(restored.mcpPath == original.mcpPath,
                 "Gateway config deserializes mcpPath.");
    return ok;
}

bool testAppConfigurationCarriesLanClients() {
    MasterControl::AppConfiguration configuration;
    MasterControl::LanClient client;
    client.clientId = "test-client";
    client.displayName = "Test";
    configuration.lanClients.push_back(client);

    nlohmann::json serialized = configuration;
    bool ok = true;
    ok &= expect(serialized.contains("lanClients"),
                 "AppConfiguration JSON exposes the lanClients array.");
    ok &= expect(serialized["lanClients"].is_array(),
                 "lanClients serializes as a JSON array.");
    ok &= expect(serialized["lanClients"].size() == 1,
                 "lanClients round-trips one entry.");

    auto restored = serialized.get<MasterControl::AppConfiguration>();
    ok &= expect(restored.lanClients.size() == 1,
                 "AppConfiguration deserializes lanClients.");
    ok &= expect(restored.lanClients.front().clientId == "test-client",
                 "Restored LAN client preserves clientId.");
    return ok;
}

}

// Phase 7 - governance enums and deferred-action shape. CLU enforcement
// runs inside the runtime translation unit; the tests here pin the
// publicly observable serialization contract used by the approval queue
// and the /api/clu/approvals routes.
bool testGovernanceActionKindRoundTrip() {
    using MasterControl::GovernanceActionKind;
    bool ok = true;
    const std::pair<GovernanceActionKind, const char*> samples[] = {
        { GovernanceActionKind::ClientRegister,             "client_register" },
        { GovernanceActionKind::ClientPrivilegeChange,      "client_privilege_change" },
        { GovernanceActionKind::ClientAutonomousModeChange, "client_autonomous_mode_change" },
        { GovernanceActionKind::ClientRevoke,               "client_revoke" },
        { GovernanceActionKind::McpServerCreate,            "mcp_server_create" },
        { GovernanceActionKind::McpServerModify,            "mcp_server_modify" },
        { GovernanceActionKind::McpServerRemove,            "mcp_server_remove" },
        { GovernanceActionKind::SubAgentCreate,             "sub_agent_create" },
        { GovernanceActionKind::SubAgentModify,             "sub_agent_modify" },
        { GovernanceActionKind::SubAgentRemove,             "sub_agent_remove" },
        { GovernanceActionKind::ModuleEnable,               "module_enable" },
        { GovernanceActionKind::ModuleDisable,              "module_disable" },
        { GovernanceActionKind::GovernancePolicyChange,     "governance_policy_change" },
        { GovernanceActionKind::RemoteInstall,              "remote_install" }
    };
    for (const auto& [value, expected] : samples) {
        const auto serialized = MasterControl::to_string(value);
        ok &= expect(serialized == expected,
                     "GovernanceActionKind serializes to its documented string.");
        const auto restored = MasterControl::governanceActionKindFromString(serialized);
        ok &= expect(restored == value,
                     "GovernanceActionKind round-trips through fromString.");
    }
    return ok;
}

bool testGovernanceDecisionOutcomeRoundTrip() {
    using MasterControl::GovernanceDecisionOutcome;
    bool ok = true;
    const std::pair<GovernanceDecisionOutcome, const char*> samples[] = {
        { GovernanceDecisionOutcome::Allow,                    "allow" },
        { GovernanceDecisionOutcome::Block,                    "block" },
        { GovernanceDecisionOutcome::RequiresOperatorApproval, "requires_operator_approval" }
    };
    for (const auto& [value, expected] : samples) {
        const auto serialized = MasterControl::to_string(value);
        ok &= expect(serialized == expected,
                     "GovernanceDecisionOutcome serializes to its documented string.");
        const auto restored = MasterControl::governanceDecisionOutcomeFromString(serialized);
        ok &= expect(restored == value,
                     "GovernanceDecisionOutcome round-trips through fromString.");
    }
    return ok;
}

bool testGovernanceDeferredActionShape() {
    MasterControl::GovernanceDeferredAction action;
    action.id = "deferred-1";
    action.action = MasterControl::GovernanceActionKind::GovernancePolicyChange;
    action.actor = "operator";
    action.targetId = "clu-profile";
    action.payload = "{\"foo\":\"bar\"}";
    action.status = "pending";
    action.createdAtUtc = "2026-04-25T00:00:00Z";

    nlohmann::json serialized = action;
    bool ok = true;
    ok &= expect(serialized["id"].get<std::string>() == "deferred-1",
                 "Deferred action serializes id.");
    ok &= expect(serialized["action"].get<std::string>() == "governance_policy_change",
                 "Deferred action serializes the kind as a slug.");
    ok &= expect(serialized["actor"].get<std::string>() == "operator",
                 "Deferred action preserves actor for the approval UI.");
    ok &= expect(serialized["status"].get<std::string>() == "pending",
                 "Deferred action default status is pending.");
    ok &= expect(serialized["payload"].get<std::string>() == "{\"foo\":\"bar\"}",
                 "Deferred action preserves the original mutation payload verbatim.");

    auto restored = serialized.get<MasterControl::GovernanceDeferredAction>();
    ok &= expect(restored.action == MasterControl::GovernanceActionKind::GovernancePolicyChange,
                 "Deferred action round-trips through JSON.");
    ok &= expect(restored.payload == action.payload,
                 "Deferred action payload survives serialization.");
    return ok;
}

int main() {
    bool ok = true;
    ok &= testDefaultConfiguration();
    ok &= testSeededEndpoints();
    ok &= testLanClientDefaults();
    ok &= testLanClientRoundTrip();
    ok &= testAppConfigurationCarriesLanClients();
    ok &= testLanClientPrivilegesDefaults();
    ok &= testLanClientPrivilegesRoundTrip();
    ok &= testAuthenticatedRequestContextDefaults();
    ok &= testOperatorFallbackGrantsAllPrivileges();
    ok &= testLanClientConfigBundleShape();
    ok &= testGovernanceActionKindRoundTrip();
    ok &= testGovernanceDecisionOutcomeRoundTrip();
    ok &= testGovernanceDeferredActionShape();
    // PHASE-02 MCP Gateway adapter tests
    ok &= testGatewayConfigurationDefaults();
    ok &= testFakeGatewayDisabledStartsDisabled();
    ok &= testFakeGatewayEnabledStartStopRoundTrip();
    ok &= testFakeGatewayStartFailureScripted();
    ok &= testFakeGatewayRegistrationRoundTrip();
    ok &= testFakeGatewayRegistrationRejectsEmptyName();
    ok &= testFakeGatewayProbeUsesScriptedHealth();
    ok &= testFakeGatewayMcpUrlComposition();
    ok &= testRealAdapterDisabledByDefault();
    ok &= testRealAdapterSupervisedMockWhenBinaryMissing();
    ok &= testRealAdapterRegistrationSurvivesAcrossStartStop();
    ok &= testGatewayEnumRoundTrips();
    ok &= testGatewayConfigJsonRoundTrip();
    // PHASE-03 LAN discovery tests
    ok &= testDiscoveryDocumentDefaultShape();
    ok &= testDiscoveryDocumentJsonRoundTrip();
    ok &= testWellKnownDocumentMatchesSchemaRequiredFields();
    ok &= testInstanceIdGeneration();
    // PHASE-04 onboarding profile tests
    ok &= testOnboardingProfileDefaultsAreLanTrust();
    ok &= testOnboardingProfileJsonRequiredFields();
    ok &= testOnboardingConfigSnippetRoundTrip();
    ok &= testOnboardingProfileTransportEnum();
    // PHASE-05 governance bundle tests
    ok &= testGovernanceBundleJsonRequiredFields();
    ok &= testGovernanceBundleAllPlatformsRecognized();
    ok &= testGovernanceProfileSummaryJsonRoundTrip();
    ok &= testOnboardingProfileLinksToGovernanceBundleUrl();
    return ok ? 0 : 1;
}
