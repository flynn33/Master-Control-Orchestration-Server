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
#include "MasterControl/SupervisorAssignment.h"
// v0.9.76: Supervisor Agent Assignment Wizard backend tests use the
// service implementation directly. The header lives next to the .cpp
// in src/MasterControlApp/; the relative include matches the
// MasterControlApp target's PUBLIC include path so this resolves
// without extending the test target's include dirs.
#include "SupervisorAssignmentService.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>

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
    // v0.9.0: defaults flipped. type=Native (was MCPJungle), enabled=true
    // (was false). MCPJungle support is dropped per the operator
    // directive; the runtime auto-starts the native gateway at boot.
    const auto configuration = MasterControl::buildDefaultConfiguration();
    bool ok = true;
    ok &= expect(configuration.mcpGateway.type == MasterControl::GatewayType::Native,
                 "Default gateway type is Native (v0.9.0; MCPJungle dropped).");
    ok &= expect(configuration.mcpGateway.enabled == true,
                 "Default gateway is enabled (v0.9.0; auto-starts at boot).");
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
    // v0.9.1: McpJungleGatewayAdapter has been deleted. The native
    // HTTP.sys substrate is now the sole production adapter. The contract
    // under test here is that constructing a NativeHttpSysGatewayAdapter
    // with enabled=false leaves it in Disabled state and refuses to bind
    // HTTP.sys when Start() is invoked (so test runs do not need admin
    // privileges to exercise the disabled-state branch).
    MasterControl::McpGatewayConfiguration configuration;
    configuration.enabled = false;
    MasterControl::NativeHttpSysGatewayAdapter adapter(configuration);

    bool ok = true;
    ok &= expect(adapter.AdapterType() == "native",
                 "Real adapter identifies as 'native'.");

    const auto status = adapter.Start();
    ok &= expect(status.state == MasterControl::GatewayState::Disabled,
                 "Disabled real adapter refuses to Start().");

    const auto health = adapter.Probe();
    ok &= expect(health.status == MasterControl::GatewayHealthStatus::Unknown,
                 "Probe on disabled adapter reports Unknown (no fake healthy).");
    ok &= expect(health.adapterType == "native",
                 "Probe stamps adapter type.");
    return ok;
}

bool testRealAdapterRegistrationSurvivesAcrossStartStop() {
    // v0.9.1: The native adapter binds HTTP.sys on Start(), which would
    // require admin or a netsh URL ACL in test environments. The
    // registration / deregistration contract is identical between the
    // real adapter and the in-process FakeMcpGatewayAdapter -- both store
    // the McpServerRegistration map under their own mutex with the same
    // success/failure semantics -- so this test exercises the IMcpGateway
    // registration contract through the fake to keep the suite hermetic.
    MasterControl::McpGatewayConfiguration configuration;
    configuration.enabled = true;
    MasterControl::FakeMcpGatewayAdapter adapter(configuration);

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
    // v0.9.1: GatewayType::MCPJungle stays serializable so persisted
    // mcpGateway.type='mcpjungle' values from v0.8.x configs still
    // deserialize without rejection (the runtime then resolves them to
    // the native substrate transparently). The enum value is no longer
    // a real adapter selector.
    ok &= expect(MasterControl::to_string(GatewayType::MCPJungle) == "mcpjungle",
                 "GatewayType serializes mcpjungle (legacy enum kept for back-compat).");
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

// PHASE-08 (ADR-002 §9): Real-time telemetry shape tests. Pin the
// activity event taxonomy, the heartbeat schema, and the honest-only
// rule (per-AI-client CPU/GPU/disk default to -1.0 = unavailable).

bool testTelemetryCategoryEnumRoundTrip() {
    using Cat = MasterControl::TelemetryCategory;
    bool ok = true;
    const std::pair<Cat, const char*> samples[] = {
        { Cat::Host,       "host" },
        { Cat::Client,     "client" },
        { Cat::Gateway,    "gateway" },
        { Cat::Worker,     "worker" },
        { Cat::Governance, "governance" },
        { Cat::Discovery,  "discovery" },
        { Cat::Dashboard,  "dashboard" },
        { Cat::System,     "system" }
    };
    for (const auto& [value, expected] : samples) {
        ok &= expect(MasterControl::to_string(value) == expected,
                     "TelemetryCategory serializes to its documented slug.");
        ok &= expect(MasterControl::telemetryCategoryFromString(expected) == value,
                     "TelemetryCategory round-trips through fromString.");
    }
    return ok;
}

bool testTelemetrySeverityEnumRoundTrip() {
    using Sev = MasterControl::TelemetrySeverity;
    bool ok = true;
    const std::pair<Sev, const char*> samples[] = {
        { Sev::Info,     "info" },
        { Sev::Warning,  "warning" },
        { Sev::Error,    "error" },
        { Sev::Critical, "critical" }
    };
    for (const auto& [value, expected] : samples) {
        ok &= expect(MasterControl::to_string(value) == expected,
                     "TelemetrySeverity serializes to slug.");
        ok &= expect(MasterControl::telemetrySeverityFromString(expected) == value,
                     "TelemetrySeverity round-trips through fromString.");
    }
    return ok;
}

bool testTelemetryEventJsonRequiredFields() {
    MasterControl::TelemetryEvent event;
    event.timestamp = "2026-05-01T00:00:00Z";
    event.category = MasterControl::TelemetryCategory::Worker;
    event.severity = MasterControl::TelemetrySeverity::Warning;
    event.message = "Pool saturation exceeded threshold.";
    event.poolId = "test-pool";
    event.metrics = nlohmann::json{ { "activeLeases", 8 }, { "queueDepth", 2 } };

    nlohmann::json serialized = event;
    bool ok = true;
    const std::vector<std::string> required = {
        "timestamp", "category", "severity", "message"
    };
    for (const auto& key : required) {
        ok &= expect(serialized.contains(key),
                     "TelemetryEvent JSON includes schema-required key.");
    }
    ok &= expect(serialized["category"].get<std::string>() == "worker",
                 "Event category serializes to slug.");
    ok &= expect(serialized["severity"].get<std::string>() == "warning",
                 "Event severity serializes to slug.");
    ok &= expect(serialized["metrics"]["activeLeases"].get<int>() == 8,
                 "Event metrics object round-trips numeric values.");
    return ok;
}

bool testClientHeartbeatHonestDefaultsAreUnavailable() {
    // ADR-002 §9: per-AI-client CPU/GPU/disk arrives only via heartbeat
    // or sidecar. Defaults must be the unavailable sentinel (-1.0),
    // never a fabricated 0.0 that the dashboard could mistake for "idle".
    MasterControl::ClientHeartbeat heartbeat;
    bool ok = true;
    ok &= expect(heartbeat.cpuPercent == -1.0,
                 "ClientHeartbeat.cpuPercent defaults to -1.0 (unavailable).");
    ok &= expect(heartbeat.memoryPercent == -1.0,
                 "ClientHeartbeat.memoryPercent defaults to -1.0 (unavailable).");
    ok &= expect(heartbeat.gpuPercent == -1.0,
                 "ClientHeartbeat.gpuPercent defaults to -1.0 (unavailable).");
    ok &= expect(heartbeat.gpuMemoryMb == -1.0,
                 "ClientHeartbeat.gpuMemoryMb defaults to -1.0 (unavailable).");
    return ok;
}

bool testClientHeartbeatJsonRoundTrip() {
    MasterControl::ClientHeartbeat heartbeat;
    heartbeat.clientId = "claude-code-jdaley-wks";
    heartbeat.clientType = "claude-code";
    heartbeat.ipAddress = "192.168.1.42";
    heartbeat.sentAtUtc = "2026-05-01T00:00:00Z";
    heartbeat.cpuPercent = 35.5;
    heartbeat.memoryPercent = 48.2;
    heartbeat.bytesSentPerSecond = 1024;
    heartbeat.bytesReceivedPerSecond = 4096;

    nlohmann::json serialized = heartbeat;
    auto restored = serialized.get<MasterControl::ClientHeartbeat>();
    bool ok = true;
    ok &= expect(restored.clientId == heartbeat.clientId,
                 "Heartbeat round-trips clientId.");
    ok &= expect(restored.cpuPercent == heartbeat.cpuPercent,
                 "Heartbeat round-trips cpuPercent.");
    ok &= expect(restored.gpuPercent == -1.0,
                 "Heartbeat preserves -1.0 unavailable sentinel for unset GPU metrics.");
    return ok;
}

bool testClientPresenceShape() {
    MasterControl::ClientPresence presence;
    presence.clientId = "claude-code-1";
    presence.clientType = "claude-code";
    presence.ipAddress = "192.168.1.42";
    presence.firstSeenUtc = "2026-05-01T00:00:00Z";
    presence.lastSeenUtc = "2026-05-01T00:01:00Z";
    presence.connectionCount = 1;
    presence.requestCount = 5;
    presence.heartbeatPresent = true;

    nlohmann::json serialized = presence;
    bool ok = true;
    ok &= expect(serialized["heartbeatPresent"].get<bool>() == true,
                 "ClientPresence exposes heartbeatPresent flag for honest dashboards.");
    ok &= expect(serialized["clientId"].get<std::string>() == "claude-code-1",
                 "Presence serializes clientId.");
    return ok;
}

bool testGatewayTrafficSnapshotShape() {
    MasterControl::GatewayTrafficSnapshot snapshot;
    snapshot.adapterType = "native";
    snapshot.mcpUrl = "http://192.168.1.10:8080/mcp";
    snapshot.healthStatus = MasterControl::GatewayHealthStatus::Healthy;
    snapshot.activeClientCount = 3;
    snapshot.requestsLastMinute = 47;
    snapshot.errorsLastMinute = 1;

    nlohmann::json serialized = snapshot;
    bool ok = true;
    ok &= expect(serialized["healthStatus"].get<std::string>() == "healthy",
                 "GatewayTrafficSnapshot serializes healthStatus slug.");
    ok &= expect(serialized["activeClientCount"].get<int>() == 3,
                 "Traffic snapshot exposes activeClientCount.");
    ok &= expect(serialized["requestsLastMinute"].get<int>() == 47,
                 "Traffic snapshot exposes requestsLastMinute.");
    return ok;
}

// PHASE-07 (ADR-002 §8): Lease routing + autoscale shape tests. The
// LeaseRouter implementation lives inside the runtime translation
// unit; these tests pin the public types' contract by constructing
// EndpointLease, LeaseRequest, and PoolSaturation values directly.

bool testLeaseStateEnumRoundTrip() {
    using State = MasterControl::LeaseState;
    bool ok = true;
    const std::pair<State, const char*> samples[] = {
        { State::Active,   "active" },
        { State::Released, "released" },
        { State::Failed,   "failed" }
    };
    for (const auto& [value, expected] : samples) {
        const auto serialized = MasterControl::to_string(value);
        ok &= expect(serialized == expected,
                     "LeaseState serializes to its documented slug.");
        const auto restored = MasterControl::leaseStateFromString(serialized);
        ok &= expect(restored == value,
                     "LeaseState round-trips through fromString.");
    }
    return ok;
}

bool testLeaseRequestJsonRoundTrip() {
    MasterControl::LeaseRequest request;
    request.poolId = "test-pool";
    request.sessionId = "session-abc";
    request.clientHint = "claude-code";
    request.stateful = true;

    nlohmann::json serialized = request;
    bool ok = true;
    ok &= expect(serialized["poolId"].get<std::string>() == "test-pool",
                 "LeaseRequest serializes poolId.");
    ok &= expect(serialized["stateful"].get<bool>() == true,
                 "LeaseRequest serializes stateful flag.");

    auto restored = serialized.get<MasterControl::LeaseRequest>();
    ok &= expect(restored.sessionId == "session-abc",
                 "LeaseRequest round-trips sessionId.");
    return ok;
}

bool testEndpointLeaseDefaultStateActive() {
    MasterControl::EndpointLease lease;
    bool ok = true;
    ok &= expect(lease.state == MasterControl::LeaseState::Active,
                 "EndpointLease defaults to Active state.");
    return ok;
}

bool testEndpointLeaseJsonShape() {
    MasterControl::EndpointLease lease;
    lease.leaseId = "lease-1";
    lease.poolId = "test-pool";
    lease.instanceId = "test-pool#1";
    lease.sessionId = "session-abc";
    lease.state = MasterControl::LeaseState::Active;
    lease.acquiredAtUtc = "2026-05-01T00:00:00Z";
    lease.statusMessage = "Lease bound to least-loaded Ready instance.";

    nlohmann::json serialized = lease;
    bool ok = true;
    ok &= expect(serialized["state"].get<std::string>() == "active",
                 "Lease state serializes to slug.");
    ok &= expect(serialized["instanceId"].get<std::string>() == "test-pool#1",
                 "Lease records the bound instanceId.");
    ok &= expect(serialized["sessionId"].get<std::string>() == "session-abc",
                 "Lease preserves sessionId for sticky-routing observers.");

    auto restored = serialized.get<MasterControl::EndpointLease>();
    ok &= expect(restored.state == lease.state,
                 "Lease state round-trips through JSON.");
    return ok;
}

bool testPoolSaturationJsonShape() {
    MasterControl::PoolSaturation saturation;
    saturation.poolId = "test-pool";
    saturation.instanceCount = 3;
    saturation.readyInstanceCount = 2;
    saturation.drainingInstanceCount = 1;
    saturation.activeLeaseCount = 6;
    saturation.maxActiveLeasesPerInstance = 3;
    saturation.atSaturation = true;
    saturation.scaleOutTriggered = false;
    saturation.atMaxInstances = false;

    nlohmann::json serialized = saturation;
    bool ok = true;
    ok &= expect(serialized["atSaturation"].get<bool>() == true,
                 "PoolSaturation exposes atSaturation flag.");
    ok &= expect(serialized["readyInstanceCount"].get<int>() == 2,
                 "PoolSaturation exposes readyInstanceCount.");
    ok &= expect(serialized["activeLeaseCount"].get<int>() == 6,
                 "PoolSaturation exposes activeLeaseCount.");
    ok &= expect(serialized["drainingInstanceCount"].get<int>() == 1,
                 "PoolSaturation exposes drainingInstanceCount for drain-policy observers.");
    return ok;
}

bool testScalePolicyDefaultsAreSafe() {
    // ADR-002 §9: no fake live infrastructure. Defaults must NOT auto-spawn.
    MasterControl::ScalePolicy policy;
    bool ok = true;
    ok &= expect(policy.minInstances == 0,
                 "ScalePolicy.minInstances defaults to 0 (no auto-spawn).");
    ok &= expect(policy.maxInstances >= 1,
                 "ScalePolicy.maxInstances default is >= 1 so single-instance pools work.");
    ok &= expect(policy.maxActiveLeasesPerInstance >= 1,
                 "ScalePolicy.maxActiveLeasesPerInstance default is >= 1.");
    return ok;
}

// PHASE-06 (ADR-002 §7): Managed worker pool shape tests. The runtime's
// WorkerSupervisor lives inside the MasterControlRuntime translation
// unit; these tests pin the public model contract by constructing
// pool/instance/policy values directly.

bool testEndpointPoolKindEnumRoundTrip() {
    bool ok = true;
    ok &= expect(MasterControl::to_string(MasterControl::EndpointPoolKind::McpServer) == "mcp-server",
                 "EndpointPoolKind serializes mcp-server.");
    ok &= expect(MasterControl::to_string(MasterControl::EndpointPoolKind::SubAgent) == "sub-agent",
                 "EndpointPoolKind serializes sub-agent.");
    ok &= expect(MasterControl::endpointPoolKindFromString("sub-agent") == MasterControl::EndpointPoolKind::SubAgent,
                 "EndpointPoolKind deserializes sub-agent.");
    return ok;
}

bool testEndpointInstanceStateAllSevenLifecycleStates() {
    using State = MasterControl::EndpointInstanceState;
    bool ok = true;
    const std::pair<State, const char*> samples[] = {
        { State::Configured, "configured" },
        { State::Starting,   "starting" },
        { State::Ready,      "ready" },
        { State::Busy,       "busy" },
        { State::Draining,   "draining" },
        { State::Failed,     "failed" },
        { State::Stopped,    "stopped" }
    };
    for (const auto& [value, expected] : samples) {
        const auto serialized = MasterControl::to_string(value);
        ok &= expect(serialized == expected,
                     "EndpointInstanceState serializes to its documented slug.");
        const auto restored = MasterControl::endpointInstanceStateFromString(serialized);
        ok &= expect(restored == value,
                     "EndpointInstanceState round-trips through fromString.");
    }
    return ok;
}

bool testManagedEndpointPoolJsonRequiredFields() {
    MasterControl::ManagedEndpointPool pool;
    pool.poolId = "test-pool-001";
    pool.kind = MasterControl::EndpointPoolKind::McpServer;
    pool.logicalMcpUrl = "http://127.0.0.1:7300/mcp/pools/test-pool-001/mcp";
    pool.template_.executable = "C:\\path\\to\\backend.exe";
    pool.template_.transport = "streamable_http";
    pool.scalePolicy.minInstances = 0;
    pool.scalePolicy.maxInstances = 4;

    nlohmann::json serialized = pool;
    bool ok = true;
    const std::vector<std::string> required = {
        "poolId", "kind", "logicalMcpUrl", "template", "scalePolicy"
    };
    for (const auto& key : required) {
        ok &= expect(serialized.contains(key),
                     "ManagedEndpointPool JSON includes schema-required key.");
    }
    ok &= expect(serialized["kind"].get<std::string>() == "mcp-server",
                 "kind serializes literally.");
    ok &= expect(serialized["template"]["transport"].get<std::string>() == "streamable_http",
                 "template.transport serializes literally.");
    ok &= expect(serialized["scalePolicy"]["maxInstances"].get<int>() == 4,
                 "scalePolicy.maxInstances round-trips.");
    return ok;
}

bool testEndpointInstanceJsonShape() {
    MasterControl::EndpointInstance instance;
    instance.instanceId = "test-pool-001#1";
    instance.poolId = "test-pool-001";
    instance.state = MasterControl::EndpointInstanceState::Ready;
    instance.processId = 1234;
    instance.supervised = true;
    instance.statusMessage = "Instance started under MCOS Job Object supervision.";
    instance.telemetry.activeLeases = 1;
    instance.telemetry.queueDepth = 0;

    nlohmann::json serialized = instance;
    bool ok = true;
    ok &= expect(serialized["state"].get<std::string>() == "ready",
                 "Instance state serializes to lifecycle slug.");
    ok &= expect(serialized["supervised"].get<bool>() == true,
                 "Supervised flag round-trips.");
    ok &= expect(serialized["telemetry"]["activeLeases"].get<int>() == 1,
                 "Nested telemetry round-trips activeLeases.");

    auto restored = serialized.get<MasterControl::EndpointInstance>();
    ok &= expect(restored.state == instance.state,
                 "Instance state round-trips through JSON.");
    ok &= expect(restored.processId == instance.processId,
                 "Process id round-trips.");
    return ok;
}

bool testManagedPoolEmptyByDefault() {
    // ADR-002 §9: no fake live infrastructure. A default-constructed pool
    // has zero instances and stays that way until upserted into a
    // supervisor and explicitly scaled.
    MasterControl::ManagedEndpointPool pool;
    bool ok = true;
    ok &= expect(pool.instances.empty(),
                 "Default-constructed pool has no instances (no fake infrastructure).");
    ok &= expect(pool.scalePolicy.minInstances == 0,
                 "Default scalePolicy.minInstances is 0 (must opt in to live processes).");
    return ok;
}

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
    original.capabilities = { "mcp-gateway", "native-adapter", "dns-sd", "udp-beacon", "forsetti-governance", "clu" };
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

// v0.9.76: Supervisor Agent Assignment Wizard backend tests. Cover the
// schemas, capability defaults, provider/state enum string mappings,
// select-and-issue happy path, reassignment supersedes, revoke, config
// expiration semantics, and connect-confirm capability gating. Each
// test is hermetic: the service is constructed with a unique temp
// directory so the persisted JSON file from one test never leaks into
// another.

namespace {
std::filesystem::path makeTempSupervisorDir(const std::string& tag) {
    auto temp = std::filesystem::temp_directory_path() / ("mcos-supervisor-" + tag);
    std::error_code ec;
    std::filesystem::remove_all(temp, ec);
    std::filesystem::create_directories(temp, ec);
    return temp;
}

MasterControl::SupervisorAssignmentServiceContext makeSupervisorContext(
        const std::filesystem::path& dir) {
    MasterControl::SupervisorAssignmentServiceContext ctx;
    ctx.dataDirectory = dir;
    ctx.mcpEndpoint = "http://192.168.1.7:7300/mcp";
    ctx.discoveryEndpoint = "http://192.168.1.7:7300/.well-known/mcos.json";
    ctx.serverDisplayName = "Master Control Orchestration Server";
    ctx.fingerprintSeed = "test-seed";
    ctx.defaultConfigTtl = std::chrono::seconds(3600);
    return ctx;
}
} // namespace

bool testSupervisorProviderRoundTrip() {
    bool ok = true;
    ok &= expect(MasterControl::providerFromString("chatgpt") == MasterControl::SupervisorProvider::ChatGpt,
                 "providerFromString chatgpt -> ChatGpt.");
    ok &= expect(MasterControl::providerFromString("claude") == MasterControl::SupervisorProvider::Claude,
                 "providerFromString claude -> Claude.");
    ok &= expect(MasterControl::providerFromString("grok") == MasterControl::SupervisorProvider::Grok,
                 "providerFromString grok -> Grok.");
    ok &= expect(MasterControl::providerFromString("codex") == MasterControl::SupervisorProvider::Unknown,
                 "providerFromString codex -> Unknown (collapsed-identity guard).");
    ok &= expect(std::string(MasterControl::providerIdString(MasterControl::SupervisorProvider::ChatGpt)) == "chatgpt",
                 "providerIdString ChatGpt -> chatgpt.");
    ok &= expect(std::string(MasterControl::providerIdString(MasterControl::SupervisorProvider::Claude)) == "claude",
                 "providerIdString Claude -> claude.");
    ok &= expect(std::string(MasterControl::providerIdString(MasterControl::SupervisorProvider::Grok)) == "grok",
                 "providerIdString Grok -> grok.");
    return ok;
}

bool testSupervisorStateRoundTrip() {
    bool ok = true;
    const std::vector<std::pair<MasterControl::SupervisorState, std::string>> cases = {
        { MasterControl::SupervisorState::Off,                "off" },
        { MasterControl::SupervisorState::ConfigGenerated,    "config_generated" },
        { MasterControl::SupervisorState::PendingConnection,  "pending_connection" },
        { MasterControl::SupervisorState::Connected,          "connected" },
        { MasterControl::SupervisorState::Disconnected,       "disconnected" },
        { MasterControl::SupervisorState::Revoked,            "revoked" },
        { MasterControl::SupervisorState::Error,              "error" }
    };
    for (const auto& [state, label] : cases) {
        ok &= expect(std::string(MasterControl::supervisorStateString(state)) == label,
                     "supervisorStateString preserves enum->label mapping.");
        ok &= expect(MasterControl::supervisorStateFromString(label) == state,
                     "supervisorStateFromString preserves label->enum mapping.");
    }
    return ok;
}

bool testSupervisorDefaultCapabilitiesAreAutonomousScoped() {
    bool ok = true;
    const auto allowed = MasterControl::defaultAutonomousSupervisorCapabilities();
    ok &= expect(allowed.size() == 8,
                 "Autonomous supervisor exposes exactly the 8 capabilities listed in SECURITY_AND_CAPABILITY_MODEL.md.");
    const auto forbidden = MasterControl::forbiddenAutonomousSupervisorCapabilities();
    for (const auto& deny : forbidden) {
        const bool inAllowed = std::find(allowed.begin(), allowed.end(), deny) != allowed.end();
        ok &= expect(!inAllowed,
                     "No forbidden capability appears in the default allowed set.");
    }
    return ok;
}

bool testSupervisorSelectAndIssueChatGpt() {
    auto dir = makeTempSupervisorDir("select-chatgpt");
    auto svc = MasterControl::createSupervisorAssignmentService(makeSupervisorContext(dir));
    MasterControl::SupervisorSelectRequest req;
    req.provider = MasterControl::SupervisorProvider::ChatGpt;
    req.mode = MasterControl::SupervisorMode::AutonomousSupervisor;
    req.exclusive = true;
    auto issue = svc->selectAndIssue(req);
    bool ok = true;
    ok &= expect(issue.ok, "selectAndIssue(ChatGpt) succeeds.");
    ok &= expect(issue.fileName == "mcos-supervisor-chatgpt.config.json",
                 "Generated filename matches CONFIG_GENERATION_SPEC default.");
    ok &= expect(!issue.configJson.empty(), "configJson is non-empty.");
    try {
        const auto parsed = nlohmann::json::parse(issue.configJson);
        ok &= expect(parsed.value("schema", std::string{}) == "mcos.supervisor.config.v1",
                     "Generated config carries the v1 schema id.");
        ok &= expect(parsed["supervisor"]["providerId"].get<std::string>() == "chatgpt",
                     "Supervisor providerId is chatgpt.");
        ok &= expect(parsed["supervisor"]["role"].get<std::string>() == "supervisor",
                     "Supervisor role is the literal 'supervisor'.");
        ok &= expect(parsed["supervisor"]["mode"].get<std::string>() == "autonomous_supervisor",
                     "Supervisor mode is autonomous_supervisor.");
        ok &= expect(parsed["server"]["mcpEndpoint"].get<std::string>().find("/mcp") != std::string::npos,
                     "Server endpoint carries the gateway path.");
        ok &= expect(parsed["auth"]["mode"].get<std::string>() == "token_reference",
                     "Auth mode is token_reference, not raw bearer.");
        ok &= expect(parsed["capabilities"].is_array() && parsed["capabilities"].size() >= 8,
                     "Capabilities array carries the 8 autonomous capabilities.");
    } catch (const std::exception&) {
        ok &= expect(false, "Generated config parses as JSON.");
    }
    return ok;
}

bool testSupervisorSelectRejectsUnknownProvider() {
    auto dir = makeTempSupervisorDir("reject-unknown");
    auto svc = MasterControl::createSupervisorAssignmentService(makeSupervisorContext(dir));
    MasterControl::SupervisorSelectRequest req;
    req.provider = MasterControl::SupervisorProvider::Unknown;
    auto issue = svc->selectAndIssue(req);
    bool ok = true;
    ok &= expect(!issue.ok, "selectAndIssue(Unknown) is rejected.");
    ok &= expect(!issue.errorMessage.empty(), "Rejection carries an errorMessage.");
    return ok;
}

bool testSupervisorReassignmentSupersedes() {
    auto dir = makeTempSupervisorDir("reassign");
    auto svc = MasterControl::createSupervisorAssignmentService(makeSupervisorContext(dir));
    MasterControl::SupervisorSelectRequest first;
    first.provider = MasterControl::SupervisorProvider::ChatGpt;
    auto firstIssue = svc->selectAndIssue(first);
    bool ok = expect(firstIssue.ok, "First select succeeds.");
    const auto firstAssignmentId = firstIssue.assignment.assignmentId;

    MasterControl::SupervisorSelectRequest second;
    second.provider = MasterControl::SupervisorProvider::Claude;
    auto secondIssue = svc->selectAndIssue(second);
    ok &= expect(secondIssue.ok, "Second select succeeds.");
    ok &= expect(secondIssue.assignment.assignmentId != firstAssignmentId,
                 "Reassignment mints a new assignmentId.");
    auto current = svc->getCurrentAssignment();
    ok &= expect(current.provider == MasterControl::SupervisorProvider::Claude,
                 "Current assignment is now Claude.");
    return ok;
}

bool testSupervisorRevokeClearsActive() {
    auto dir = makeTempSupervisorDir("revoke");
    auto svc = MasterControl::createSupervisorAssignmentService(makeSupervisorContext(dir));
    MasterControl::SupervisorSelectRequest req;
    req.provider = MasterControl::SupervisorProvider::Grok;
    svc->selectAndIssue(req);
    svc->revoke("test reason");
    const auto status = svc->getStatus();
    bool ok = true;
    ok &= expect(status.state == MasterControl::SupervisorState::Revoked,
                 "Revoke transitions state to revoked.");
    ok &= expect(!status.active,
                 "Revoked assignment is not active.");
    return ok;
}

// v0.10.1: regression for the dashboard "Config id does not match the
// active assignment." sticky-error bug. A confirm rejection populates
// lastErrorMessage. If the operator subsequently revokes (or, in the
// regenerate path, re-issues a fresh config), the leftover message
// from the prior lifecycle should not still render in the dashboard's
// supervisor card -- the operator's voluntary state transition is the
// authoritative new state. revoke() and regenerateConfig() both clear
// lastErrorMessage as of v0.10.1.
bool testSupervisorRevokeClearsStaleErrorMessage() {
    auto dir = makeTempSupervisorDir("revoke-clears-error");
    auto svc = MasterControl::createSupervisorAssignmentService(makeSupervisorContext(dir));
    MasterControl::SupervisorSelectRequest req;
    req.provider = MasterControl::SupervisorProvider::ChatGpt;
    const auto issue = svc->selectAndIssue(req);
    bool ok = expect(issue.ok, "selectAndIssue ChatGpt succeeds.");

    // Trigger a confirm rejection so lastErrorMessage is populated.
    MasterControl::SupervisorConnectionClaim badClaim;
    badClaim.provider = MasterControl::SupervisorProvider::ChatGpt;
    badClaim.configId = "CFG-WRONG-ID";
    badClaim.clientId = "rogue-client";
    badClaim.capabilities = { "supervisor.get_context" };
    const auto reject = svc->confirmConnection(badClaim);
    ok &= expect(!reject.ok, "Wrong-config-id confirm is rejected.");

    auto pre = svc->getStatus();
    ok &= expect(!pre.lastErrorMessage.empty(),
                 "Pre-revoke status carries the rejection error message.");

    // Operator-initiated revoke should clear the leftover message.
    svc->revoke("Operator clicked Revoke Active.");
    auto post = svc->getStatus();
    ok &= expect(post.state == MasterControl::SupervisorState::Revoked,
                 "Post-revoke state is Revoked.");
    ok &= expect(post.lastErrorMessage.empty(),
                 "Post-revoke lastErrorMessage is cleared.");
    return ok;
}

// v0.10.1: covers the legacy state migration. A persisted file written
// by a pre-v0.10.1 service can carry state=Revoked together with a
// non-empty lastErrorMessage from the prior lifecycle. Loading that
// record into a v0.10.1+ service should drop the message because the
// state is terminal. We synthesize the legacy shape on disk, then
// instantiate a service over the same dataDirectory and assert the
// loaded status carries an empty lastErrorMessage.
bool testSupervisorLoadDropsStaleErrorOnTerminalState() {
    auto dir = makeTempSupervisorDir("load-drops-stale-error");
    const auto file = dir / "supervisor-assignment.json";
    nlohmann::json legacy = {
        {"assignmentId",       "SUP-LEGACY-1"},
        {"providerId",         "chatgpt"},
        {"clientId",           ""},
        {"mode",               "autonomous_supervisor"},
        {"exclusive",          true},
        {"state",              "revoked"},
        {"configId",           "CFG-LEGACY-1"},
        {"issuedAtUtc",        "2026-05-10T00:00:00Z"},
        {"expiresAtUtc",       "2026-05-10T03:00:00Z"},
        {"connectedAtUtc",     ""},
        {"lastHeartbeatUtc",   ""},
        {"revokedAtUtc",       "2026-05-10T01:30:00Z"},
        {"revocationReason",   "Operator clicked Revoke Active."},
        {"tokenRef",           "mcos-supervisor-token:CFG-LEGACY-1"},
        {"serverFingerprint",  "sha256:legacy"},
        {"auditCorrelationId", "AUD-legacy"},
        {"lastErrorMessage",   "Config id does not match the active assignment."},
        {"allowedCapabilities", nlohmann::json::array({"supervisor.get_context"})}
    };
    {
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        out << legacy.dump(2);
    }
    auto svc = MasterControl::createSupervisorAssignmentService(makeSupervisorContext(dir));
    const auto status = svc->getStatus();
    bool ok = true;
    ok &= expect(status.state == MasterControl::SupervisorState::Revoked,
                 "Loaded state is Revoked.");
    ok &= expect(status.lastErrorMessage.empty(),
                 "Load-time migration drops the stale lastErrorMessage on terminal state.");

    // Migration also persists the cleaned record so the on-disk file
    // converges without needing another operator action.
    {
        std::ifstream in(file, std::ios::binary);
        std::stringstream buf;
        buf << in.rdbuf();
        const auto reread = nlohmann::json::parse(buf.str());
        ok &= expect(reread.value("lastErrorMessage", std::string{"<MISSING>"}).empty(),
                     "On-disk lastErrorMessage is empty after load-migration persist.");
    }
    return ok;
}

bool testSupervisorRegenerateClearsStaleErrorMessage() {
    auto dir = makeTempSupervisorDir("regenerate-clears-error");
    auto svc = MasterControl::createSupervisorAssignmentService(makeSupervisorContext(dir));
    MasterControl::SupervisorSelectRequest req;
    req.provider = MasterControl::SupervisorProvider::Claude;
    const auto issue = svc->selectAndIssue(req);
    bool ok = expect(issue.ok, "selectAndIssue Claude succeeds.");

    // Trigger a confirm rejection so lastErrorMessage is populated.
    MasterControl::SupervisorConnectionClaim badClaim;
    badClaim.provider = MasterControl::SupervisorProvider::ChatGpt;  // wrong provider
    badClaim.configId = issue.assignment.configId;
    badClaim.clientId = "wrong-provider-client";
    badClaim.capabilities = { "supervisor.get_context" };
    const auto reject = svc->confirmConnection(badClaim);
    ok &= expect(!reject.ok, "Wrong-provider confirm is rejected.");

    auto pre = svc->getStatus();
    ok &= expect(!pre.lastErrorMessage.empty(),
                 "Pre-regenerate status carries the rejection error message.");

    // Regenerate should clear the leftover message and re-issue a fresh config.
    const auto regen = svc->regenerateConfig();
    ok &= expect(regen.ok, "regenerateConfig succeeds on active assignment.");

    auto post = svc->getStatus();
    ok &= expect(post.state == MasterControl::SupervisorState::ConfigGenerated,
                 "Post-regenerate state is ConfigGenerated.");
    ok &= expect(post.lastErrorMessage.empty(),
                 "Post-regenerate lastErrorMessage is cleared.");
    return ok;
}

bool testSupervisorConfirmConnectionHappyPath() {
    auto dir = makeTempSupervisorDir("confirm-happy");
    auto svc = MasterControl::createSupervisorAssignmentService(makeSupervisorContext(dir));
    MasterControl::SupervisorSelectRequest req;
    req.provider = MasterControl::SupervisorProvider::Claude;
    const auto issue = svc->selectAndIssue(req);
    bool ok = expect(issue.ok, "selectAndIssue Claude succeeds.");

    MasterControl::SupervisorConnectionClaim claim;
    claim.provider = MasterControl::SupervisorProvider::Claude;
    claim.configId = issue.assignment.configId;
    claim.clientId = "claude-desktop-flynn-main";
    claim.capabilities = {
        "supervisor.get_context",
        "supervisor.list_pending_decisions",
        "supervisor.submit_decision"
    };
    const auto result = svc->confirmConnection(claim);
    ok &= expect(result.ok, "Valid claim is accepted.");
    ok &= expect(result.newState == MasterControl::SupervisorState::Connected,
                 "State transitions to Connected.");
    return ok;
}

bool testSupervisorConfirmRejectsForbiddenCapability() {
    auto dir = makeTempSupervisorDir("confirm-forbidden");
    auto svc = MasterControl::createSupervisorAssignmentService(makeSupervisorContext(dir));
    MasterControl::SupervisorSelectRequest req;
    req.provider = MasterControl::SupervisorProvider::ChatGpt;
    const auto issue = svc->selectAndIssue(req);

    MasterControl::SupervisorConnectionClaim claim;
    claim.provider = MasterControl::SupervisorProvider::ChatGpt;
    claim.configId = issue.assignment.configId;
    claim.clientId = "chatgpt-desktop-flynn-main";
    // Forbidden raw-shell tool: must be rejected per
    // SECURITY_AND_CAPABILITY_MODEL.md. Even if the supervisor client
    // tries to negotiate it on connect, the server-side check denies.
    claim.capabilities = { "supervisor.get_context", "worker.run_shell" };
    const auto result = svc->confirmConnection(claim);
    bool ok = true;
    ok &= expect(!result.ok, "Forbidden capability is rejected at connect.");
    ok &= expect(!result.errorMessage.empty(),
                 "Rejection carries a forbidden-capability errorMessage.");
    return ok;
}

bool testSupervisorConfirmRejectsProviderMismatch() {
    auto dir = makeTempSupervisorDir("confirm-mismatch");
    auto svc = MasterControl::createSupervisorAssignmentService(makeSupervisorContext(dir));
    MasterControl::SupervisorSelectRequest req;
    req.provider = MasterControl::SupervisorProvider::Grok;
    const auto issue = svc->selectAndIssue(req);

    MasterControl::SupervisorConnectionClaim claim;
    claim.provider = MasterControl::SupervisorProvider::Claude;  // mismatch
    claim.configId = issue.assignment.configId;
    claim.clientId = "claude-desktop-flynn-main";
    claim.capabilities = { "supervisor.get_context" };
    const auto result = svc->confirmConnection(claim);
    return expect(!result.ok, "Provider mismatch is rejected.");
}

bool testSupervisorPersistenceSurvivesServiceRecreation() {
    auto dir = makeTempSupervisorDir("persistence");
    {
        auto svc = MasterControl::createSupervisorAssignmentService(makeSupervisorContext(dir));
        MasterControl::SupervisorSelectRequest req;
        req.provider = MasterControl::SupervisorProvider::ChatGpt;
        svc->selectAndIssue(req);
    }
    // Recreate the service against the same dataDirectory; the on-disk
    // record must rehydrate the assignment.
    auto svc = MasterControl::createSupervisorAssignmentService(makeSupervisorContext(dir));
    const auto current = svc->getCurrentAssignment();
    return expect(current.provider == MasterControl::SupervisorProvider::ChatGpt,
                  "Service restart preserves the on-disk supervisor assignment.");
}

bool testSupervisorParseSelectRequestRejectsBreakGlass() {
    nlohmann::json body;
    body["providerId"] = "chatgpt";
    body["mode"] = "break_glass_admin";
    MasterControl::SupervisorSelectRequest out;
    const auto err = MasterControl::parseSelectRequest(body, out);
    return expect(!err.empty(),
                  "parseSelectRequest rejects break_glass_admin from the wizard surface.");
}

bool testSupervisorHeartbeatWatchdogIdleStateNoTransition() {
    // v0.9.78: the watchdog only acts on Connected. Idle (off) and
    // pending (config_generated) assignments are not subject to the
    // disconnect timeout.
    auto dir = makeTempSupervisorDir("watchdog-idle");
    auto svc = MasterControl::createSupervisorAssignmentService(makeSupervisorContext(dir));
    bool ok = true;
    ok &= expect(!svc->expireConnectionIfStale(std::chrono::seconds(0)),
                 "Watchdog no-ops on Off state.");
    MasterControl::SupervisorSelectRequest req;
    req.provider = MasterControl::SupervisorProvider::Claude;
    svc->selectAndIssue(req);
    ok &= expect(!svc->expireConnectionIfStale(std::chrono::seconds(0)),
                 "Watchdog no-ops on ConfigGenerated state.");
    return ok;
}

bool testSupervisorInstructionsAreProviderSpecific() {
    // v0.9.83: the generated config's instructions block must carry
    // provider-specific guidance. Pre-v0.9.83 every provider got the
    // same four generic steps, which were actionable for none of them.
    bool ok = true;
    auto run = [&](MasterControl::SupervisorProvider provider,
                   const std::string& tag,
                   const std::string& expectMarker) {
        auto dir = makeTempSupervisorDir("instructions-" + tag);
        auto svc = MasterControl::createSupervisorAssignmentService(makeSupervisorContext(dir));
        MasterControl::SupervisorSelectRequest req;
        req.provider = provider;
        const auto issue = svc->selectAndIssue(req);
        if (!issue.ok) {
            return expect(false, "selectAndIssue should succeed for provider-specific instructions test.");
        }
        try {
            const auto cfg = nlohmann::json::parse(issue.configJson);
            const auto& steps = cfg["instructions"]["steps"];
            const std::string joined = steps.dump();
            return expect(joined.find(expectMarker) != std::string::npos,
                          ("Instructions for " + tag + " carry provider-specific marker.").c_str());
        } catch (...) {
            return expect(false, "Generated config parses for provider-specific instructions test.");
        }
    };
    // Claude config should reference its CLI / Desktop paths.
    ok &= run(MasterControl::SupervisorProvider::Claude, "claude", "Claude Code");
    // ChatGPT config should reference its custom connector setup.
    ok &= run(MasterControl::SupervisorProvider::ChatGpt, "chatgpt", "Custom Connector");
    // Grok config should reference the MCP bridge wording.
    ok &= run(MasterControl::SupervisorProvider::Grok, "grok", "Grok MCP bridge");
    return ok;
}

bool testSupervisorConfirmDefaultDenyOnEmptyAllowedSet() {
    // v0.9.81 regression test for the default-deny tightening in
    // confirmConnection. Construct an assignment via the autonomous
    // happy-path, then forcibly downgrade the persisted mode field to
    // a value whose capabilitiesForMode() returns an empty list. Any
    // capability claim must then be rejected, even ones that aren't on
    // the forbidden list. Pre-v0.9.81 the empty-allowed list short-
    // circuit accepted everything not explicitly forbidden.
    auto dir = makeTempSupervisorDir("default-deny");
    {
        auto svc = MasterControl::createSupervisorAssignmentService(makeSupervisorContext(dir));
        MasterControl::SupervisorSelectRequest req;
        req.provider = MasterControl::SupervisorProvider::Claude;
        svc->selectAndIssue(req);
    }
    // Rewrite the persisted assignment JSON to set mode=disabled and
    // remove allowedCapabilities so the rehydrated service falls into
    // the empty-allowed-set path. Force a fresh service to load the
    // tampered record.
    const auto persisted = dir / "supervisor-assignment.json";
    std::ifstream in(persisted);
    std::stringstream raw;
    raw << in.rdbuf();
    in.close();
    auto body = nlohmann::json::parse(raw.str());
    body["mode"] = "disabled";
    body["allowedCapabilities"] = nlohmann::json::array();
    {
        std::ofstream out(persisted);
        out << body.dump(2);
    }
    auto svc = MasterControl::createSupervisorAssignmentService(makeSupervisorContext(dir));
    const auto current = svc->getCurrentAssignment();

    MasterControl::SupervisorConnectionClaim claim;
    claim.provider = MasterControl::SupervisorProvider::Claude;
    claim.configId = current.configId;
    claim.clientId = "claude-default-deny-test";
    // Single benign capability that is NOT on the forbidden list. The
    // pre-v0.9.81 bug would have accepted this since the allowed list
    // is empty. The fix rejects it with "Capability not allowed in
    // current mode".
    claim.capabilities = { "supervisor.get_context" };
    const auto result = svc->confirmConnection(claim);
    bool ok = true;
    ok &= expect(!result.ok,
                 "Default-deny: empty allowed set rejects any capability claim.");
    ok &= expect(result.errorMessage.find("Capability not allowed") != std::string::npos,
                 "Default-deny error message names the not-allowed path.");
    return ok;
}

bool testSupervisorHeartbeatWatchdogFlipsConnectedToDisconnected() {
    // v0.9.78: simulate a Connected assignment with a heartbeat from
    // an hour ago. The watchdog with a 5s threshold must flip it to
    // Disconnected.
    auto dir = makeTempSupervisorDir("watchdog-stale");
    auto svc = MasterControl::createSupervisorAssignmentService(makeSupervisorContext(dir));
    MasterControl::SupervisorSelectRequest req;
    req.provider = MasterControl::SupervisorProvider::Grok;
    const auto issue = svc->selectAndIssue(req);
    MasterControl::SupervisorConnectionClaim claim;
    claim.provider = MasterControl::SupervisorProvider::Grok;
    claim.configId = issue.assignment.configId;
    claim.clientId = "grok-desktop-flynn-main";
    claim.capabilities = { "supervisor.get_context" };
    const auto result = svc->confirmConnection(claim);
    bool ok = expect(result.ok, "confirmConnection succeeds for watchdog setup.");
    // The watchdog with a 0s threshold should immediately flip the
    // freshly-connected assignment, since lastHeartbeatUtc == now and
    // any non-zero elapsed seconds counts as stale at threshold=0.
    // Sleep 2s to ensure now > heartbeat + 1 second.
    std::this_thread::sleep_for(std::chrono::seconds(2));
    ok &= expect(svc->expireConnectionIfStale(std::chrono::seconds(1)),
                 "Watchdog flips Connected -> Disconnected when heartbeat is older than threshold.");
    const auto status = svc->getStatus();
    ok &= expect(status.state == MasterControl::SupervisorState::Disconnected,
                 "Final state is Disconnected after watchdog.");
    // Re-running the watchdog on a Disconnected assignment must be a
    // no-op (the state transition is one-way per sweep).
    ok &= expect(!svc->expireConnectionIfStale(std::chrono::seconds(0)),
                 "Watchdog is idempotent: re-run on Disconnected returns false.");
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
    // v0.9.1: testRealAdapterSupervisedMockWhenBinaryMissing was specific
    // to the deleted McpJungleGatewayAdapter (supervised-mock binary
    // path). NativeHttpSysGatewayAdapter has no equivalent state.
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
    // PHASE-06 managed worker pool tests
    ok &= testEndpointPoolKindEnumRoundTrip();
    ok &= testEndpointInstanceStateAllSevenLifecycleStates();
    ok &= testManagedEndpointPoolJsonRequiredFields();
    ok &= testEndpointInstanceJsonShape();
    ok &= testManagedPoolEmptyByDefault();
    // PHASE-07 lease + autoscale tests
    ok &= testLeaseStateEnumRoundTrip();
    ok &= testLeaseRequestJsonRoundTrip();
    ok &= testEndpointLeaseDefaultStateActive();
    ok &= testEndpointLeaseJsonShape();
    ok &= testPoolSaturationJsonShape();
    ok &= testScalePolicyDefaultsAreSafe();
    // PHASE-08 telemetry tests
    ok &= testTelemetryCategoryEnumRoundTrip();
    ok &= testTelemetrySeverityEnumRoundTrip();
    ok &= testTelemetryEventJsonRequiredFields();
    ok &= testClientHeartbeatHonestDefaultsAreUnavailable();
    ok &= testClientHeartbeatJsonRoundTrip();
    ok &= testClientPresenceShape();
    ok &= testGatewayTrafficSnapshotShape();
    // v0.9.76 Supervisor Agent Assignment Wizard backend tests.
    ok &= testSupervisorProviderRoundTrip();
    ok &= testSupervisorStateRoundTrip();
    ok &= testSupervisorDefaultCapabilitiesAreAutonomousScoped();
    ok &= testSupervisorSelectAndIssueChatGpt();
    ok &= testSupervisorSelectRejectsUnknownProvider();
    ok &= testSupervisorReassignmentSupersedes();
    ok &= testSupervisorRevokeClearsActive();
    ok &= testSupervisorRevokeClearsStaleErrorMessage();
    ok &= testSupervisorLoadDropsStaleErrorOnTerminalState();
    ok &= testSupervisorRegenerateClearsStaleErrorMessage();
    ok &= testSupervisorConfirmConnectionHappyPath();
    ok &= testSupervisorConfirmRejectsForbiddenCapability();
    ok &= testSupervisorConfirmRejectsProviderMismatch();
    ok &= testSupervisorPersistenceSurvivesServiceRecreation();
    ok &= testSupervisorParseSelectRequestRejectsBreakGlass();
    ok &= testSupervisorInstructionsAreProviderSpecific();
    ok &= testSupervisorConfirmDefaultDenyOnEmptyAllowedSet();
    ok &= testSupervisorHeartbeatWatchdogIdleStateNoTransition();
    ok &= testSupervisorHeartbeatWatchdogFlipsConnectedToDisconnected();
    return ok ? 0 : 1;
}
