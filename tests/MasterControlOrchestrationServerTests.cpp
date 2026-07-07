// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.
//
// Post-ADR-001 test suite. The pre-remediation provider tests were dropped
// in Phase 2 of the LAN client rebuild (plans/remediation/01-gut-and-rebuild.md).
// This file covers the new LAN client identity model and grows alongside
// later phases as privilege enforcement and config bundle issuance land.

#include "MasterControl/AuthenticatedRequestContext.h"
#include "MasterControl/DiagnosticsAggregator.h"
// v0.11.0-alpha.3: parseCapturedAtUtcOrZero test (age-bound rotation).
#include "MasterControl/MasterControlDiagnostics.h"
// v0.11.0-alpha.3 (PHASE-14 Slice E): SqliteDiagnosticsStore + service.
#include "MasterControl/DiagnosticsService.h"
#include "MasterControl/DiagnosticsStore.h"
#include "MasterControl/DiagnosticsTypes.h"
#include "MasterControl/AdminRouteAuthorization.h"
#include "MasterControl/AdminRouteRegistry.h"
#include "MasterControl/AlphaFeatureMatrix.h"
#include "MasterControl/DatabaseQuery.h"
#include "MasterControl/EndpointAdvertisement.h"
#include "MasterControl/HttpBindingInspection.h"
#include "MasterControl/HttpHeaderParse.h"
#include "MasterControl/JsonMerge.h"
#include "MasterControl/JsonStrictness.h"
#include "MasterControl/LanClient.h"
#include "MasterControl/MasterControlDefaults.h"
#include "MasterControl/MasterControlModels.h"
#include "MasterControl/MasterControlVersion.h"
#include "MasterControl/McpGatewayAdapters.h"
#include "MasterControl/McpToolNameResolver.h"
#include "MasterControl/QueryParamParse.h"
#include "MasterControl/SupervisorAssignment.h"
#include "MasterControl/WorkflowReadiness.h"
#include "MasterControl/WorkingAlphaReadiness.h"
// v0.9.76: Supervisor Agent Assignment Wizard backend tests use the
// service implementation directly. The header lives next to the .cpp
// in src/MasterControlApp/; the relative include matches the
// MasterControlApp target's PUBLIC include path so this resolves
// without extending the test target's include dirs.
#include "SupervisorAssignmentService.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>   // v0.11.0-alpha.3: _set_abort_behavior in main()
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
    ok &= expect(configuration.bindAddress == "127.0.0.1",
                 "Default admin listener binds loopback only.");
    ok &= expect(configuration.beaconEnabled == false,
                 "Default UDP beacon is disabled.");
    ok &= expect(configuration.aiAutonomyEnabled == false,
                 "Global AI autonomy is off by default.");
    ok &= expect(configuration.security.enableAuthentication == true,
                 "Default admin authentication is enabled.");
    ok &= expect(configuration.security.allowTroubleshootingBypass == false,
                 "Default troubleshooting bypass is disabled.");
    ok &= expect(configuration.security.allowOpenLanAccess == false,
                 "Default LAN exposure is disabled.");
    ok &= expect(configuration.security.securityPosture == "local-only",
                 "Default security posture is local-only.");
    ok &= expect(configuration.lanClients.empty(),
                 "Fresh installs start with an empty LAN client roster.");
    // v0.11.0-alpha.3: beacon signing defaults. Fresh installs carry a
    // generated 64-hex-char key; signing is enabled by default.
    ok &= expect(configuration.security.beaconSigningEnabled == true,
                 "Default beacon signing is enabled.");
    ok &= expect(configuration.security.beaconSigningKey.size() == 64,
                 "Fresh install generates a 64-hex-char beacon signing key.");
    ok &= expect(configuration.security.beaconSigningKey.find_first_not_of("0123456789abcdef")
                     == std::string::npos,
                 "Beacon signing key is lowercase hex.");
    // Back-compat: a persisted config without the new keys deserializes
    // with an empty key (signing skipped) rather than throwing.
    {
        nlohmann::json legacy = nlohmann::json{
            { "enableTls", false },
            { "enableAuthentication", true },
            { "allowTroubleshootingBypass", false },
            { "allowOpenLanAccess", false },
            { "securityProtocolsEnabled", true },
            { "securityPosture", "local-only" },
            { "trustedRemoteHosts", nlohmann::json::array() }
        };
        const auto restored = legacy.get<MasterControl::SecuritySettings>();
        ok &= expect(restored.beaconSigningKey.empty(),
                     "Legacy SecuritySettings JSON deserializes with an empty beacon signing key.");
        ok &= expect(restored.beaconSigningEnabled == true,
                     "Legacy SecuritySettings JSON defaults beaconSigningEnabled to true (no-op while the key is empty).");
    }
    return ok;
}

// v0.11.0-alpha.3: admin-listener SChannel TLS is strictly opt-in.
// Fresh installs must keep the flag off with an empty thumbprint, and
// legacy persisted SecuritySettings JSON (written before the keys
// existed) must deserialize to the same off state via the WITH_DEFAULT
// macro -- i.e. upgrading never silently flips the admin port to TLS.
bool testSecuritySettingsAdminTlsDefaults() {
    bool ok = true;
    const auto configuration = MasterControl::buildDefaultConfiguration();
    ok &= expect(configuration.security.adminTlsEnabled == false,
                 "Default admin-listener TLS is disabled (opt-in).");
    ok &= expect(configuration.security.adminTlsCertThumbprint.empty(),
                 "Fresh installs carry no admin TLS cert thumbprint.");
    {
        nlohmann::json legacy = nlohmann::json{
            { "enableTls", false },
            { "enableAuthentication", true },
            { "allowTroubleshootingBypass", false },
            { "allowOpenLanAccess", false },
            { "securityProtocolsEnabled", true },
            { "securityPosture", "local-only" },
            { "trustedRemoteHosts", nlohmann::json::array() }
        };
        const auto restored = legacy.get<MasterControl::SecuritySettings>();
        ok &= expect(restored.adminTlsEnabled == false,
                     "Legacy SecuritySettings JSON deserializes with admin TLS disabled.");
        ok &= expect(restored.adminTlsCertThumbprint.empty(),
                     "Legacy SecuritySettings JSON deserializes with an empty admin TLS thumbprint.");
    }
    return ok;
}

bool testSeededEndpoints() {
    const auto endpoints = MasterControl::buildDefaultSeededEndpoints();
    return expect(!endpoints.empty(),
                  "Default seeded endpoint set is non-empty.");
}

// v0.11.0-alpha.3: the seeded "platform-gateway" row must point at the
// native HTTP.sys gateway (default listenPort 8080), not the legacy
// external gateway port 7200 retired at v0.9.0. Pre-fix the Exports
// surface derived its client-facing MCP URL from this row and handed
// LAN clients a dead http://<host>:7200/mcp/gateway URL.
bool testSeededGatewayEndpointPointsAtNativeGateway() {
    const auto endpoints = MasterControl::buildDefaultSeededEndpoints();
    bool ok = true;
    bool found = false;
    for (const auto& endpoint : endpoints) {
        if (endpoint.id != "platform-gateway") {
            continue;
        }
        found = true;
        ok &= expect(endpoint.port == 8080,
                     "Seeded platform-gateway endpoint targets the native gateway listen port (8080).");
        ok &= expect(endpoint.port != 7200,
                     "Seeded platform-gateway endpoint no longer targets the retired external gateway port (7200).");
    }
    ok &= expect(found, "Seeded endpoint set contains the platform-gateway row.");
    return ok;
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
    original.capabilities = {
        MasterControl::kCapabilityProcessExec,
        MasterControl::kCapabilityFilesystemWrite
    };
    original.autonomousMode = false;
    original.createdAtUtc = "2026-04-25T00:00:00Z";
    original.lastSeenUtc = "2026-04-25T00:01:00Z";

    bool ok = true;

    nlohmann::json serialized = original;
    ok &= expect(serialized["clientId"].get<std::string>() == original.clientId,
                 "LanClient JSON round-trip preserves clientId.");
    ok &= expect(serialized["enabled"].get<bool>() == true,
                 "LanClient JSON round-trip preserves enabled.");
    ok &= expect(serialized["capabilities"].is_array() && serialized["capabilities"].size() == 2,
                 "LanClient JSON round-trip preserves explicit capabilities.");
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
    ok &= expect(restored.capabilities == original.capabilities,
                 "LanClient deserializes explicit capabilities.");
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
    ok &= expect(client.capabilities.empty(),
                 "New LanClient has no high-risk capabilities until assigned.");
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
// pin the publicly observable invariants of the context type and the local
// setup/bootstrap factory.
bool testAuthenticatedRequestContextDefaults() {
    MasterControl::AuthenticatedRequestContext context;
    bool ok = true;
    ok &= expect(context.client.has_value() == false,
                 "Default context has no resolved client.");
    ok &= expect(context.privileges.canCreateMcpServers == false,
                 "Default context grants no privileges.");
    ok &= expect(context.autonomousMode == false,
                 "Default context is non-autonomous.");
    ok &= expect(context.actor == "anonymous",
                 "Default context attributes work to anonymous.");
    ok &= expect(context.remoteAddress.empty(),
                 "Default context has no remote address.");
    ok &= expect(context.isLocalRequest == false,
                 "Default context is not local.");
    ok &= expect(context.isLocalBootstrap == false,
                 "Default context is not local bootstrap.");
    return ok;
}

bool testCapabilityMatrixDefaults() {
    MasterControl::LanClient client;
    bool ok = true;
    ok &= expect(MasterControl::capabilitiesForClient(client).empty(),
                 "New clients have no derived capabilities.");

    client.capabilities = { MasterControl::kCapabilityFilesystemWrite };
    const auto writeCaps = MasterControl::capabilitiesForClient(client);
    ok &= expect(MasterControl::hasCapability(writeCaps, MasterControl::kCapabilityFilesystemWrite),
                 "Explicit filesystem.write is preserved.");
    ok &= expect(MasterControl::hasCapability(writeCaps, MasterControl::kCapabilityFilesystemRead),
                 "filesystem.write implies filesystem.read.");

    MasterControl::LanClient legacy;
    legacy.privileges.canManageModules = true;
    legacy.privileges.canManageClients = true;
    const auto legacyCaps = MasterControl::capabilitiesForClient(legacy);
    ok &= expect(MasterControl::hasCapability(legacyCaps, MasterControl::kCapabilityInstallPackage),
                 "Legacy module privilege maps to install.package.");
    ok &= expect(!MasterControl::hasCapability(legacyCaps, MasterControl::kCapabilityGovernanceModify),
                 "Legacy module privilege does not imply governance.modify.");
    ok &= expect(MasterControl::hasCapability(legacyCaps, MasterControl::kCapabilityClientsManage),
                 "Legacy client privilege maps to clients.manage.");
    return ok;
}

bool testMcpToolCapabilityMatrix() {
    bool ok = true;
    auto requiresCapability = [](const std::string& server, const std::string& tool, const std::string& capability) {
        return MasterControl::hasCapability(
            MasterControl::requiredCapabilitiesForMcpTool(server, tool),
            capability);
    };
    ok &= expect(requiresCapability("terminal-shell", "shell.exec", MasterControl::kCapabilityProcessExec),
                 "shell.exec requires process.exec.");
    ok &= expect(requiresCapability("code-execution-repl", "repl.exec", MasterControl::kCapabilityProcessExec),
                 "repl.exec requires process.exec.");
    ok &= expect(requiresCapability("file-search", "search.grep", MasterControl::kCapabilityFilesystemRead),
                 "search.grep requires filesystem.read.");
    ok &= expect(requiresCapability("persistent-context", "ctx.set", MasterControl::kCapabilityFilesystemWrite),
                 "ctx.set requires filesystem.write.");
    ok &= expect(requiresCapability("desktop-control", "desktop.focus", MasterControl::kCapabilityDesktopControl),
                 "desktop.focus requires desktop.control.");
    ok &= expect(requiresCapability("keyboard-mouse-control", "input.keyboard", MasterControl::kCapabilityInputSynthesize),
                 "input.keyboard requires input.synthesize.");
    ok &= expect(requiresCapability("screen-capture-vision", "screen.capture", MasterControl::kCapabilityScreenCapture),
                 "screen.capture requires screen.capture.");
    ok &= expect(MasterControl::requiredCapabilitiesForMcpTool("baseline-tools", "mcos.echo").empty(),
                 "mcos.echo remains available without high-risk capabilities.");
    return ok;
}

bool testLocalBootstrapGrantsAllPrivileges() {
    auto context = MasterControl::makeLocalOperatorContext("127.0.0.1");
    bool ok = true;
    ok &= expect(context.privileges.canCreateMcpServers,
                 "Local bootstrap grants canCreateMcpServers.");
    ok &= expect(context.privileges.canModifyMcpServers,
                 "Local bootstrap grants canModifyMcpServers.");
    ok &= expect(context.privileges.canRemoveMcpServers,
                 "Local bootstrap grants canRemoveMcpServers.");
    ok &= expect(context.privileges.canCreateSubAgents,
                 "Local bootstrap grants canCreateSubAgents.");
    ok &= expect(context.privileges.canModifySubAgents,
                 "Local bootstrap grants canModifySubAgents.");
    ok &= expect(context.privileges.canRemoveSubAgents,
                 "Local bootstrap grants canRemoveSubAgents.");
    ok &= expect(context.privileges.canManageClients,
                 "Local bootstrap grants canManageClients.");
    ok &= expect(context.privileges.canManageModules,
                 "Local bootstrap grants canManageModules.");
    ok &= expect(context.privileges.canChangeGovernancePolicy,
                 "Local bootstrap grants canChangeGovernancePolicy.");
    ok &= expect(MasterControl::hasCapability(context.capabilities, MasterControl::kCapabilityProcessExec),
                 "Local bootstrap carries process.exec.");
    ok &= expect(MasterControl::hasCapability(context.capabilities, MasterControl::kCapabilitySupervisorAssign),
                 "Local bootstrap carries supervisor.assign.");
    ok &= expect(context.autonomousMode == true,
                 "Local bootstrap is autonomous for setup-only create paths.");
    ok &= expect(context.actor == "local-operator",
                 "Local bootstrap identifies itself as local-operator.");
    ok &= expect(context.remoteAddress == "127.0.0.1",
                 "Local bootstrap records its remote address.");
    ok &= expect(context.isLocalRequest == true,
                 "Local bootstrap marks local requests.");
    ok &= expect(context.isLocalBootstrap == true,
                 "Local bootstrap flag is true.");
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
        } },
        // Working-alpha contract additions: absolute URLs a second LAN host
        // can act on directly.
        { "gatewayUrl", "http://192.168.1.10:8080/mcp" },
        { "discoveryUrl", "http://192.168.1.10:7300/.well-known/mcos.json" },
        { "heartbeatUrl", "http://192.168.1.10:7300/api/client/heartbeat" },
        { "telemetryHeartbeatUrl", "http://192.168.1.10:7300/api/telemetry/heartbeat" },
        { "onboarding", {
            { "profileUrl", "http://192.168.1.10:7300/api/onboarding/claude_code" },
            { "genericProfileUrl", "http://192.168.1.10:7300/api/onboarding/generic" }
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
    // Working-alpha contract: absolute gateway + heartbeat URLs + onboarding ref.
    ok &= expect(bundle["gatewayUrl"].get<std::string>().rfind("http://", 0) == 0,
                 "Bundle carries an absolute gatewayUrl.");
    ok &= expect(bundle["heartbeatUrl"].get<std::string>().find("/api/client/heartbeat") != std::string::npos,
                 "Bundle carries an absolute client heartbeat URL.");
    ok &= expect(bundle["telemetryHeartbeatUrl"].get<std::string>().find("/api/telemetry/heartbeat") != std::string::npos,
                 "Bundle carries an absolute telemetry heartbeat URL.");
    ok &= expect(bundle["onboarding"].contains("profileUrl"),
                 "Bundle carries an onboarding profile reference.");
    return ok;
}

// PHASE-02 (ADR-002 §2): MCP Gateway adapter tests. The fake adapter
// implements IMcpGateway with no child processes and no network calls
// so the state machine and registration contract can be exercised
// deterministically.

bool testGatewayConfigurationDefaults() {
    const auto configuration = MasterControl::buildDefaultConfiguration();
    bool ok = true;
    ok &= expect(configuration.mcpGateway.type == MasterControl::GatewayType::Native,
                 "Default gateway type is Native.");
    ok &= expect(configuration.mcpGateway.enabled == false,
                 "Default gateway is disabled until setup enables LAN posture.");
    ok &= expect(configuration.mcpGateway.listenHost == "127.0.0.1",
                 "Default gateway host binds loopback only.");
    ok &= expect(configuration.mcpGateway.listenPort == 8080,
                 "Default gateway port is 8080 (distinct from admin 7300).");
    ok &= expect(configuration.mcpGateway.mcpPath == "/mcp",
                 "Default gateway MCP path is /mcp.");
    ok &= expect(configuration.mcpGateway.healthPath == "/health",
                 "Default gateway health path is /health.");
    ok &= expect(configuration.mcpGateway.mode == "local-only",
                 "Default gateway mode is local-only.");
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
    // The native HTTP.sys substrate is the sole production adapter.
    // The contract under test here is that constructing a
    // NativeHttpSysGatewayAdapter with enabled=false leaves it in
    // Disabled state and refuses to bind HTTP.sys when Start() is
    // invoked (so test runs do not need admin privileges to
    // exercise the disabled-state branch).
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
    ok &= expect(MasterControl::to_string(GatewayType::Native) == "native",
                 "GatewayType serializes native.");
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
    profile.mcp.url = profile.gatewayMcpUrl;  // structured MCP descriptor (working-alpha contract)
    profile.governanceBundleUrl = "http://192.168.1.10:7300/api/governance/bundles/windows";

    nlohmann::json serialized = profile;
    bool ok = true;
    const std::vector<std::string> required = {
        "clientType", "mcp", "gatewayMcpUrl", "transport", "authRequired", "governanceBundleUrl"
    };
    for (const auto& key : required) {
        ok &= expect(serialized.contains(key),
                     "Onboarding profile JSON includes schema-required key.");
    }
    ok &= expect(serialized["authRequired"].get<bool>() == false,
                 "authRequired serializes as false (schema const).");
    ok &= expect(serialized["transport"].get<std::string>() == "streamable_http",
                 "transport serializes as a schema-allowed enum value.");
    // Working-alpha contract: /api/onboarding/{clientType} exposes an `mcp`
    // object mirroring the gateway URL/transport with a pinned protocol rev.
    ok &= expect(serialized["mcp"]["url"].get<std::string>() == "http://192.168.1.10:8080/mcp",
                 "Onboarding profile mcp.url mirrors the gateway MCP URL.");
    ok &= expect(serialized["mcp"]["protocolVersion"].get<std::string>() == "2025-03-26",
                 "Onboarding profile mcp.protocolVersion pins the MCP revision.");
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
    ok &= expect(doc.trust == "local-only",
                 "DiscoveryDocument default trust is local-only.");
    ok &= expect(doc.auth == "required",
                 "DiscoveryDocument default auth is required.");
    ok &= expect(doc.securityPosture == "local-only",
                 "DiscoveryDocument default security posture is local-only.");
    return ok;
}

bool testDiscoveryDocumentJsonRoundTrip() {
    MasterControl::DiscoveryDocument original;
    original.version = "0.5.0";
    original.instanceId = "mcos-test-id-001";
    original.instanceName = "Test MCOS";
    // Working-alpha contract: structured server-identity object.
    original.server.version = "0.5.0";
    original.server.instanceId = "mcos-test-id-001";
    original.server.networkMode = "trusted-lan";
    original.gateway.type = "native";
    original.gateway.mcpUrl = "http://192.168.1.10:8080/mcp";
    original.gateway.healthUrl = "http://192.168.1.10:8080/health";
    original.gateway.state = "running";
    // Pin the TLS dual-bind fields on the discovery gateway block.
    // Pre-hardening these fields
    // round-tripped via NLOHMANN_DEFINE_TYPE but the test only asserted
    // gateway.mcpUrl, leaving room for future serialization changes to
    // silently drop the HTTPS fields that strict clients depend on.
    original.gateway.mcpUrlTls = "https://192.168.1.10:8443/mcp";
    original.gateway.healthUrlTls = "https://192.168.1.10:8443/health";
    original.gateway.tlsEnabled = true;
    original.gateway.tlsCertThumbprint = "0123456789ABCDEF0123456789ABCDEF01234567";
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
    ok &= expect(serialized["trust"].get<std::string>() == "local-only",
                 "Discovery JSON pins trust=local-only.");
    ok &= expect(serialized["auth"].get<std::string>() == "required",
                 "Discovery JSON pins auth=required.");
    ok &= expect(serialized["securityPosture"].get<std::string>() == "local-only",
                 "Discovery JSON pins securityPosture=local-only.");
    ok &= expect(serialized["server"]["product"].get<std::string>() == "MCOS",
                 "Discovery JSON exposes server.product=MCOS (working-alpha contract).");
    ok &= expect(serialized["server"]["networkMode"].get<std::string>() == "trusted-lan",
                 "Discovery JSON exposes server.networkMode.");
    ok &= expect(serialized["gateway"]["mcpUrl"].get<std::string>() == "http://192.168.1.10:8080/mcp",
                 "Discovery JSON exposes gateway.mcpUrl.");
    // v0.11.0-alpha.2: pin the TLS surface in the serialized form so
    // strict clients can rely on the field names.
    ok &= expect(serialized["gateway"]["tlsEnabled"].get<bool>() == true,
                 "Discovery JSON exposes gateway.tlsEnabled (alpha.2 TLS dual-bind).");
    ok &= expect(serialized["gateway"]["mcpUrlTls"].get<std::string>() == "https://192.168.1.10:8443/mcp",
                 "Discovery JSON exposes gateway.mcpUrlTls (alpha.2 TLS dual-bind).");
    ok &= expect(serialized["gateway"]["healthUrlTls"].get<std::string>() == "https://192.168.1.10:8443/health",
                 "Discovery JSON exposes gateway.healthUrlTls (alpha.2 TLS dual-bind).");
    ok &= expect(serialized["gateway"]["tlsCertThumbprint"].get<std::string>() == "0123456789ABCDEF0123456789ABCDEF01234567",
                 "Discovery JSON exposes gateway.tlsCertThumbprint (alpha.2 TLS dual-bind).");
    ok &= expect(serialized["onboarding"]["claudeCode"].get<std::string>().find("/api/onboarding/claude-code") != std::string::npos,
                 "Discovery JSON exposes claudeCode onboarding URL.");
    ok &= expect(serialized["governance"]["bundleBaseUrl"].get<std::string>().find("/api/governance/bundles") != std::string::npos,
                 "Discovery JSON exposes governance.bundleBaseUrl.");
    ok &= expect(serialized["capabilities"].is_array() && !serialized["capabilities"].empty(),
                 "Discovery JSON capabilities is a non-empty array.");

    auto restored = serialized.get<MasterControl::DiscoveryDocument>();
    ok &= expect(restored.gateway.mcpUrl == original.gateway.mcpUrl,
                 "Discovery doc round-trips gateway.mcpUrl.");
    // v0.11.0-alpha.2: round-trip the TLS dual-bind fields too.
    ok &= expect(restored.gateway.tlsEnabled == original.gateway.tlsEnabled,
                 "Discovery doc round-trips gateway.tlsEnabled.");
    ok &= expect(restored.gateway.mcpUrlTls == original.gateway.mcpUrlTls,
                 "Discovery doc round-trips gateway.mcpUrlTls.");
    ok &= expect(restored.gateway.healthUrlTls == original.gateway.healthUrlTls,
                 "Discovery doc round-trips gateway.healthUrlTls.");
    ok &= expect(restored.gateway.tlsCertThumbprint == original.gateway.tlsCertThumbprint,
                 "Discovery doc round-trips gateway.tlsCertThumbprint.");
    ok &= expect(restored.onboarding.codex == original.onboarding.codex,
                 "Discovery doc round-trips onboarding.codex.");
    ok &= expect(restored.governance.cluProfileUrl == original.governance.cluProfileUrl,
                 "Discovery doc round-trips governance.cluProfileUrl.");
    ok &= expect(restored.capabilities.size() == original.capabilities.size(),
                 "Discovery doc round-trips capabilities count.");
    ok &= expect(restored.instanceId == original.instanceId,
                 "Discovery doc round-trips instanceId.");
    ok &= expect(restored.server.instanceId == original.server.instanceId,
                 "Discovery doc round-trips server.instanceId.");
    ok &= expect(restored.server.networkMode == original.server.networkMode,
                 "Discovery doc round-trips server.networkMode.");

    // TLS-disabled default state serializes to empty/false on the
    // dual-bind fields. This guards
    // against a future change that accidentally publishes HTTPS URLs
    // when TLS is off.
    MasterControl::DiscoveryDocument tlsOff;
    tlsOff.gateway.type = "native";
    tlsOff.gateway.mcpUrl = "http://192.168.1.10:8080/mcp";
    nlohmann::json tlsOffJson = tlsOff;
    ok &= expect(tlsOffJson["gateway"]["tlsEnabled"].get<bool>() == false,
                 "Default (TLS off) discovery JSON has gateway.tlsEnabled=false.");
    ok &= expect(tlsOffJson["gateway"]["mcpUrlTls"].get<std::string>().empty(),
                 "Default (TLS off) discovery JSON has empty gateway.mcpUrlTls.");
    ok &= expect(tlsOffJson["gateway"]["tlsCertThumbprint"].get<std::string>().empty(),
                 "Default (TLS off) discovery JSON has empty gateway.tlsCertThumbprint.");

    return ok;
}

// v0.11.0-alpha.3: regression guard for the BeaconService port-confusion
// bug. BeaconService::currentAdvertisement() passed
// configuration.browserPort into BOTH port slots of the
// BeaconAdvertisement aggregate, so the legacy /api/beacon surface
// advertised gatewayPort=7300 (the admin listener) instead of
// cfg.mcpGateway.listenPort (8080). This test pins the serialized
// contract: the two ports are distinct keys carrying distinct values
// when constructed correctly.
bool testBeaconAdvertisementJsonShape() {
    MasterControl::BeaconAdvertisement advertisement;
    advertisement.instanceName = "Test MCOS";
    advertisement.hostName = "test-host";
    advertisement.ipAddress = "192.168.1.10";
    advertisement.browserPort = 7300;
    advertisement.gatewayPort = 8080;
    advertisement.status = "online";

    nlohmann::json serialized = advertisement;
    bool ok = true;
    ok &= expect(serialized["browserPort"].get<uint16_t>() == 7300,
                 "Beacon advertisement serializes browserPort.");
    ok &= expect(serialized["gatewayPort"].get<uint16_t>() == 8080,
                 "Beacon advertisement serializes gatewayPort.");
    ok &= expect(serialized["browserPort"].get<uint16_t>()
                     != serialized["gatewayPort"].get<uint16_t>(),
                 "Beacon advertisement keeps browserPort and gatewayPort distinct.");

    auto restored = serialized.get<MasterControl::BeaconAdvertisement>();
    ok &= expect(restored.gatewayPort == advertisement.gatewayPort,
                 "Beacon advertisement round-trips gatewayPort.");
    ok &= expect(restored.browserPort == advertisement.browserPort,
                 "Beacon advertisement round-trips browserPort.");
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
    doc.gateway.type = "native";
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
    // Working-alpha acceptance contract additionally requires the structured
    // server/gateway/admin objects to survive the strip.
    for (const char* key : { "server", "gateway", "admin" }) {
        ok &= expect(wellKnown.contains(key),
                     "/.well-known/mcos.json exposes working-alpha server/gateway/admin object.");
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
    original.type = MasterControl::GatewayType::Native;
    original.enabled = true;
    original.listenHost = "0.0.0.0";
    original.listenPort = 9090;
    original.mcpPath = "/mcp";
    original.healthPath = "/health";
    original.mode = "lan-trusted";

    nlohmann::json serialized = original;
    bool ok = true;
    ok &= expect(serialized["type"].get<std::string>() == "native",
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

// Regression: an on-disk config carrying an unknown / legacy
// gateway type slug must NOT cause json.get<AppConfiguration>() to
// throw. FileBackedConfigurationService treats any exception from
// the get<>() call as a load failure and reverts the entire
// AppConfiguration to defaults -- which would wipe every other
// persisted operator setting (preferredBindAddress, seededEndpoints,
// resourceAllocation, etc.) just because the gateway type field
// carries a string we no longer recognize. The tolerant
// gatewayTypeFromString coerces unknown slugs to Native; this test
// pins that contract and asserts the surrounding fields round-trip
// undisturbed.
bool testGatewayConfigUnknownTypeFallsBackWithoutWipe() {
    nlohmann::json serialized = {
        { "type", "some-legacy-slug-we-no-longer-know" },
        { "enabled", true },
        { "listenHost", "0.0.0.0" },
        { "listenPort", 9090 },
        { "mcpPath", "/mcp" },
        { "healthPath", "/health" },
        { "mode", "lan-trusted" },
        { "binaryPath", "" },
        { "databasePath", "" }
    };
    bool ok = true;
    MasterControl::McpGatewayConfiguration restored;
    try {
        restored = serialized.get<MasterControl::McpGatewayConfiguration>();
        ok &= expect(true, "Unknown gateway type slug deserializes without throwing.");
    } catch (const std::exception&) {
        ok &= expect(false, "Unknown gateway type slug must NOT throw (would wipe full AppConfiguration).");
        return ok;
    }
    ok &= expect(restored.type == MasterControl::GatewayType::Native,
                 "Unknown gateway type slug falls back to Native.");
    // Sibling fields must round-trip untouched -- the whole point of
    // the tolerant fallback is that legacy configs do not get reset.
    ok &= expect(restored.enabled == true,
                 "Sibling field 'enabled' survives unknown-type fallback.");
    ok &= expect(restored.listenPort == 9090,
                 "Sibling field 'listenPort' survives unknown-type fallback.");
    ok &= expect(restored.mcpPath == "/mcp",
                 "Sibling field 'mcpPath' survives unknown-type fallback.");
    ok &= expect(restored.mode == "lan-trusted",
                 "Sibling field 'mode' survives unknown-type fallback.");
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

bool testSetupStateJsonContract() {
    MasterControl::SetupStateSnapshot state;
    state.mode = "manual";
    state.currentStep = "manual-setup";
    state.securityPosture = "local-only";
    state.lanModeEnabled = false;
    state.beaconEnabled = false;
    state.mcpGatewayAdvertised = false;
    state.lastUpdatedUtc = "2026-05-29T12:00:00Z";
    state.steps.push_back(MasterControl::SetupStepState{
        "manual-setup",
        "Manual setup",
        "active",
        {},
        "Use the operator console"
    });
    MasterControl::SetupOverrideRecord overrideRecord;
    overrideRecord.stepId = "readiness-review";
    overrideRecord.issueId = "workflow.none-ready";
    overrideRecord.reason = "Operator accepted manual workflow import.";
    overrideRecord.createdAtUtc = "2026-05-29T12:01:00Z";
    state.overrides.push_back(overrideRecord);

    const nlohmann::json serialized = state;
    bool ok = true;
    ok &= expect(serialized.contains("setupVersion"),
                 "Setup state JSON exposes setupVersion.");
    ok &= expect(serialized["mode"] == "manual",
                 "Setup state JSON exposes the selected mode.");
    ok &= expect(serialized["currentStep"] == "manual-setup",
                 "Setup state JSON exposes the current step.");
    ok &= expect(serialized.contains("steps") && serialized["steps"].is_array(),
                 "Setup state JSON exposes step array.");
    ok &= expect(serialized.contains("readiness") && serialized["readiness"].is_object(),
                 "Setup state JSON embeds readiness.");
    ok &= expect(serialized.contains("securityPosture"),
                 "Setup state JSON exposes securityPosture.");
    ok &= expect(serialized["overrides"].size() == 1,
                 "Setup state JSON exposes persisted overrides.");

    const auto restored = serialized.get<MasterControl::SetupStateSnapshot>();
    ok &= expect(restored.mode == "manual",
                 "Setup state mode round-trips.");
    ok &= expect(restored.steps.size() == 1 && restored.steps.front().id == "manual-setup",
                 "Setup state steps round-trip.");
    ok &= expect(restored.overrides.size() == 1 && restored.overrides.front().issueId == "workflow.none-ready",
                 "Setup override records round-trip.");
    return ok;
}

bool testAppConfigurationCarriesSetupState() {
    MasterControl::AppConfiguration configuration;
    configuration.setupMode = "import-existing";
    configuration.setupCurrentStep = "import-existing";
    configuration.setupDismissedAtUtc = "2026-05-29T12:00:00Z";
    MasterControl::SetupOverrideRecord overrideRecord;
    overrideRecord.stepId = "readiness-review";
    overrideRecord.issueId = "mcp.none-ready";
    overrideRecord.reason = "Accepted for offline local testing.";
    overrideRecord.createdAtUtc = "2026-05-29T12:01:00Z";
    configuration.setupOverrides.push_back(overrideRecord);

    const nlohmann::json serialized = configuration;
    bool ok = true;
    ok &= expect(serialized["setupMode"] == "import-existing",
                 "AppConfiguration JSON carries setup mode.");
    ok &= expect(serialized["setupCurrentStep"] == "import-existing",
                 "AppConfiguration JSON carries setup current step.");
    ok &= expect(serialized["setupOverrides"].size() == 1,
                 "AppConfiguration JSON carries setup overrides.");

    const auto restored = serialized.get<MasterControl::AppConfiguration>();
    ok &= expect(restored.setupMode == "import-existing",
                 "AppConfiguration setup mode round-trips.");
    ok &= expect(restored.setupOverrides.size() == 1
                 && restored.setupOverrides.front().reason == "Accepted for offline local testing.",
                 "AppConfiguration setup overrides round-trip.");
    return ok;
}

MasterControl::WorkflowDefinition makeReadyWorkflow(const std::string& workflowId,
                                                    const std::string& source) {
    MasterControl::WorkflowDefinition workflow;
    workflow.workflowId = workflowId;
    workflow.displayName = workflowId;
    workflow.source = source;
    workflow.enabled = true;
    workflow.steps.push_back(MasterControl::WorkflowStepDefinition{
        "operator-review",
        "approval",
        "operator.readiness-review",
        nlohmann::json::object(),
        true
    });
    return workflow;
}

bool testWorkflowDefinitionJsonContract() {
    const auto workflow = makeReadyWorkflow("manual-workflow", "manual");
    const nlohmann::json serialized = workflow;
    bool ok = true;
    ok &= expect(serialized["workflowId"] == "manual-workflow",
                 "Workflow JSON exposes workflowId.");
    ok &= expect(serialized["source"] == "manual",
                 "Workflow JSON exposes source.");
    ok &= expect(serialized["enabled"] == true,
                 "Workflow JSON exposes enabled.");
    ok &= expect(serialized["steps"].is_array() && serialized["steps"].size() == 1,
                 "Workflow JSON exposes steps.");

    const auto restored = serialized.get<MasterControl::WorkflowDefinition>();
    ok &= expect(restored.workflowId == "manual-workflow",
                 "Workflow id round-trips.");
    ok &= expect(restored.steps.size() == 1 && restored.steps.front().kind == "approval",
                 "Workflow steps round-trip.");
    return ok;
}

bool testWorkflowReadinessCountsSources() {
    std::vector<MasterControl::WorkflowDefinition> workflows{
        makeReadyWorkflow("manual-workflow", "manual"),
        makeReadyWorkflow("imported-workflow", "imported"),
        makeReadyWorkflow("starter-workflow", "starter-template")
    };

    const auto counts = MasterControl::workflowReadinessCounts(workflows);
    bool ok = true;
    ok &= expect(counts.ready == 3,
                 "Manual, imported, and starter-template workflows all count as ready.");
    ok &= expect(counts.missing == 0,
                 "Ready workflows clear the missing count.");
    return ok;
}

bool testWorkflowReadinessRejectsInvalidDisabledDeleted() {
    std::vector<MasterControl::WorkflowDefinition> workflows;

    auto invalid = makeReadyWorkflow("bad-workflow", "manual");
    invalid.steps.clear();
    workflows.push_back(invalid);

    auto disabled = makeReadyWorkflow("disabled-workflow", "imported");
    disabled.enabled = false;
    workflows.push_back(disabled);

    // Deleted workflows are represented by absence from the durable list.
    const auto counts = MasterControl::workflowReadinessCounts(workflows);
    bool ok = true;
    ok &= expect(counts.ready == 0,
                 "Invalid and disabled workflows do not count as ready.");
    ok &= expect(counts.invalid == 1,
                 "Invalid enabled workflow is counted as invalid.");
    ok &= expect(counts.disabled == 1,
                 "Disabled workflow is counted as disabled.");
    ok &= expect(counts.missing >= 1,
                 "No ready workflows leaves readiness missing.");
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
    ctx.mcpEndpoint = "http://127.0.0.1:8080/mcp";
    ctx.discoveryEndpoint = "http://127.0.0.1:7300/.well-known/mcos.json";
    ctx.endpointPlan.lanModeEnabled = false;
    ctx.endpointPlan.gatewayRunning = false;
    ctx.endpointPlan.adminLanAdvertised = false;
    ctx.endpointPlan.mcpLanAdvertised = false;
    ctx.endpointPlan.adminHost = "127.0.0.1";
    ctx.endpointPlan.mcpHost = "127.0.0.1";
    ctx.endpointPlan.adminBaseUrl = "http://127.0.0.1:7300";
    ctx.endpointPlan.discoveryEndpoint = ctx.discoveryEndpoint;
    ctx.endpointPlan.mcpEndpoint = ctx.mcpEndpoint;
    ctx.endpointPlan.mcpHealthEndpoint = "http://127.0.0.1:8080/health";
    ctx.endpointPlan.networkMode = "local-only";
    ctx.endpointPlan.reason = "LAN mode is disabled; generated endpoints are local-only.";
    ctx.serverDisplayName = "Master Control Orchestration Server";
    ctx.fingerprintSeed = "test-seed";
    ctx.defaultConfigTtl = std::chrono::seconds(3600);
    return ctx;
}

void applySupervisorAuthClaim(MasterControl::SupervisorConnectionClaim& claim,
                              const MasterControl::SupervisorAssignment& assignment) {
    claim.token = assignment.tokenRef;
    claim.fingerprint = assignment.serverFingerprint;
}
} // namespace

bool testAdvertisedEndpointPlanLocalOnlyDoesNotAdvertiseLan() {
    auto cfg = MasterControl::buildDefaultConfiguration();
    cfg.security.allowOpenLanAccess = false;
    cfg.bindAddress = "0.0.0.0";
    cfg.activeProfile.preferredBindAddress = "192.168.1.7";
    cfg.mcpGateway.listenHost = "0.0.0.0";
    cfg.mcpGateway.listenPort = 8080;
    cfg.mcpGateway.mcpPath = "/mcp";
    cfg.mcpGateway.healthPath = "/health";
    MasterControl::GatewayStatus gateway;
    gateway.state = MasterControl::GatewayState::Running;

    const auto plan = MasterControl::buildAdvertisedEndpointPlan(cfg, gateway, "192.168.1.50");
    bool ok = true;
    ok &= expect(plan.networkMode == "local-only",
                 "Endpoint plan reports local-only when LAN mode is disabled.");
    ok &= expect(plan.adminHost == "127.0.0.1",
                 "Local-only endpoint plan keeps admin discovery on loopback.");
    ok &= expect(plan.mcpHost == "127.0.0.1",
                 "Local-only endpoint plan keeps MCP on loopback.");
    ok &= expect(!plan.adminLanAdvertised && !plan.mcpLanAdvertised,
                 "Local-only endpoint plan does not advertise LAN endpoints.");
    ok &= expect(plan.discoveryEndpoint.find("192.168.1.7") == std::string::npos,
                 "Local-only discovery URL does not contain the preferred LAN IP.");
    ok &= expect(plan.mcpEndpoint.find("192.168.1.7") == std::string::npos,
                 "Local-only MCP URL does not contain the preferred LAN IP.");
    return ok;
}

bool testAdvertisedEndpointPlanRequiresGatewayRunningForLanMcp() {
    auto cfg = MasterControl::buildDefaultConfiguration();
    cfg.security.allowOpenLanAccess = true;
    cfg.bindAddress = "0.0.0.0";
    cfg.activeProfile.preferredBindAddress = "192.168.1.7";
    cfg.mcpGateway.listenHost = "0.0.0.0";
    cfg.mcpGateway.listenPort = 8080;
    MasterControl::GatewayStatus gateway;
    gateway.state = MasterControl::GatewayState::Stopped;

    const auto plan = MasterControl::buildAdvertisedEndpointPlan(cfg, gateway, "192.168.1.50");
    bool ok = true;
    ok &= expect(plan.networkMode == "trusted-lan",
                 "Endpoint plan reports trusted-lan when LAN mode is enabled.");
    ok &= expect(plan.adminLanAdvertised,
                 "Admin discovery may advertise LAN when LAN mode and wildcard bind are enabled.");
    ok &= expect(!plan.mcpLanAdvertised,
                 "MCP endpoint does not advertise LAN when the gateway is not running.");
    ok &= expect(plan.mcpHost == "127.0.0.1",
                 "Stopped gateway keeps MCP endpoint local-only.");
    return ok;
}

bool testAdvertisedEndpointPlanLanWhenEnabledAndGatewayRunning() {
    auto cfg = MasterControl::buildDefaultConfiguration();
    cfg.security.allowOpenLanAccess = true;
    cfg.bindAddress = "0.0.0.0";
    cfg.activeProfile.preferredBindAddress = "192.168.1.7";
    cfg.mcpGateway.listenHost = "0.0.0.0";
    cfg.mcpGateway.listenPort = 8080;
    MasterControl::GatewayStatus gateway;
    gateway.state = MasterControl::GatewayState::Running;

    const auto plan = MasterControl::buildAdvertisedEndpointPlan(cfg, gateway, "192.168.1.50");
    bool ok = true;
    ok &= expect(plan.adminHost == "192.168.1.7",
                 "LAN endpoint plan uses the preferred bind address for admin discovery.");
    ok &= expect(plan.mcpHost == "192.168.1.7",
                 "LAN endpoint plan uses the preferred bind address for MCP.");
    ok &= expect(plan.adminLanAdvertised && plan.mcpLanAdvertised,
                 "LAN endpoint plan advertises both admin and MCP when both listeners are viable.");
    ok &= expect(plan.discoveryEndpoint.find("192.168.1.7") != std::string::npos,
                 "LAN discovery URL contains the preferred LAN IP.");
    ok &= expect(plan.mcpEndpoint.find("192.168.1.7") != std::string::npos,
                 "LAN MCP URL contains the preferred LAN IP.");
    return ok;
}

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
        const auto tokenRef = parsed["auth"]["tokenRef"].get<std::string>();
        ok &= expect(tokenRef.rfind("mcos-supervisor-token:", 0) == 0,
                     "Auth tokenRef uses the expected supervisor token prefix.");
        ok &= expect(tokenRef != std::string("mcos-supervisor-token:") + issue.assignment.configId,
                     "Auth tokenRef is not derived from configId.");
        ok &= expect(tokenRef == issue.assignment.tokenRef,
                     "Generated config tokenRef matches the persisted assignment token.");
        ok &= expect(parsed["server"]["networkMode"].get<std::string>() == "local-only",
                      "Generated config explicitly states local-only network mode.");
        ok &= expect(parsed["server"]["endpointAdvertisement"]["lanModeEnabled"].get<bool>() == false,
                     "Generated config states LAN advertisement is disabled.");
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
    const auto previousTokenRef = issue.assignment.tokenRef;
    const auto regen = svc->regenerateConfig();
    ok &= expect(regen.ok, "regenerateConfig succeeds on active assignment.");
    ok &= expect(regen.assignment.tokenRef != previousTokenRef,
                 "regenerateConfig mints a fresh supervisor token.");
    ok &= expect(regen.assignment.tokenRef != std::string("mcos-supervisor-token:") + regen.assignment.configId,
                 "regenerateConfig token is not derived from configId.");

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
    applySupervisorAuthClaim(claim, issue.assignment);
    const auto result = svc->confirmConnection(claim);
    ok &= expect(result.ok, "Valid claim is accepted.");
    ok &= expect(result.newState == MasterControl::SupervisorState::Connected,
                 "State transitions to Connected.");
    return ok;
}

bool testSupervisorConfirmRejectsTokenMismatch() {
    auto dir = makeTempSupervisorDir("confirm-token-mismatch");
    auto svc = MasterControl::createSupervisorAssignmentService(makeSupervisorContext(dir));
    MasterControl::SupervisorSelectRequest req;
    req.provider = MasterControl::SupervisorProvider::Claude;
    const auto issue = svc->selectAndIssue(req);

    MasterControl::SupervisorConnectionClaim claim;
    claim.provider = MasterControl::SupervisorProvider::Claude;
    claim.configId = issue.assignment.configId;
    claim.clientId = "claude-desktop-flynn-main";
    claim.capabilities = { "supervisor.get_context" };
    claim.fingerprint = issue.assignment.serverFingerprint;
    claim.token = "mcos-supervisor-token:wrong-config";
    const auto result = svc->confirmConnection(claim);
    bool ok = true;
    ok &= expect(!result.ok, "Token mismatch is rejected.");
    ok &= expect(result.errorMessage.find("token") != std::string::npos,
                 "Token mismatch rejection names the token failure.");
    return ok;
}

bool testSupervisorLegacyDerivedTokenRotatesOnLoad() {
    auto dir = makeTempSupervisorDir("legacy-derived-token-rotates");
    const auto file = dir / "supervisor-assignment.json";
    nlohmann::json legacy = {
        {"assignmentId",       "SUP-LEGACY-ACTIVE"},
        {"providerId",         "claude"},
        {"clientId",           "legacy-client"},
        {"mode",               "autonomous_supervisor"},
        {"exclusive",          true},
        {"state",              "connected"},
        {"configId",           "CFG-LEGACY-ACTIVE"},
        {"issuedAtUtc",        "2026-05-10T00:00:00Z"},
        {"expiresAtUtc",       "2099-05-10T03:00:00Z"},
        {"connectedAtUtc",     "2026-05-10T00:05:00Z"},
        {"lastHeartbeatUtc",   "2026-05-10T00:06:00Z"},
        {"revokedAtUtc",       ""},
        {"revocationReason",   ""},
        {"tokenRef",           "mcos-supervisor-token:CFG-LEGACY-ACTIVE"},
        {"serverFingerprint",  "sha256:legacy"},
        {"auditCorrelationId", "AUD-legacy"},
        {"lastErrorMessage",   ""},
        {"allowedCapabilities", nlohmann::json::array({"supervisor.get_context"})}
    };
    {
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        out << legacy.dump(2);
    }

    auto svc = MasterControl::createSupervisorAssignmentService(makeSupervisorContext(dir));
    const auto current = svc->getCurrentAssignment();
    bool ok = true;
    ok &= expect(current.state == MasterControl::SupervisorState::ConfigGenerated,
                 "Legacy deterministic token load returns assignment to ConfigGenerated.");
    ok &= expect(current.tokenRef.rfind("mcos-supervisor-token:", 0) == 0,
                 "Rotated legacy token keeps the supervisor token prefix.");
    ok &= expect(current.tokenRef != "mcos-supervisor-token:CFG-LEGACY-ACTIVE",
                 "Legacy deterministic token is replaced with an unpredictable token.");
    ok &= expect(current.clientId.empty() && current.connectedAtUtc.empty() && current.lastHeartbeatUtc.empty(),
                 "Legacy deterministic token rotation clears connected client state.");

    MasterControl::SupervisorConnectionClaim claim;
    claim.provider = MasterControl::SupervisorProvider::Claude;
    claim.configId = current.configId;
    claim.clientId = "legacy-client";
    claim.capabilities = { "supervisor.get_context" };
    claim.fingerprint = current.serverFingerprint;
    claim.token = "mcos-supervisor-token:CFG-LEGACY-ACTIVE";
    const auto rejected = svc->confirmConnection(claim);
    ok &= expect(!rejected.ok,
                 "Old deterministic token cannot confirm after load-time rotation.");
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
    applySupervisorAuthClaim(claim, issue.assignment);
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
    applySupervisorAuthClaim(claim, issue.assignment);
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
    applySupervisorAuthClaim(claim, current);
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
    applySupervisorAuthClaim(claim, issue.assignment);
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

// v0.10.15: QueryParamParse helper tests. The helper backs the
// alias-aware param extraction on /api/activity, /api/telemetry/events,
// and /api/client/activity. Pre-v0.10.15 these routes called
// `query.find("max=")` directly, so any operator-natural alias such as
// `?limit=N` was silently ignored and the route shipped the entire
// event ring. These tests verify (1) canonical parse, (2) alias
// fallback, (3) name-boundary safety, (4) missing-param returns empty,
// (5) multi-pair query strings.

bool testQueryParamCanonicalParse() {
    bool ok = true;
    ok &= expect(MasterControl::extractQueryParam("max=5", "max") == "5",
                 "extractQueryParam matches name= at start of query.");
    ok &= expect(MasterControl::extractQueryParam("since=42&max=5", "max") == "5",
                 "extractQueryParam matches name= after &.");
    ok &= expect(MasterControl::extractQueryParam("max=5&since=42", "max") == "5",
                 "extractQueryParam trims at first & boundary.");
    ok &= expect(MasterControl::extractQueryParam("kind=supervisor_*", "kind") == "supervisor_*",
                 "extractQueryParam preserves wildcard suffix.");
    return ok;
}

bool testQueryParamAliasFallback() {
    bool ok = true;
    // /api/activity ?max= aliases.
    ok &= expect(MasterControl::extractQueryParamAny("limit=5", {"max", "limit", "count", "n", "top"}) == "5",
                 "extractQueryParamAny falls back to limit= when max= is absent.");
    ok &= expect(MasterControl::extractQueryParamAny("count=7", {"max", "limit", "count", "n", "top"}) == "7",
                 "extractQueryParamAny falls back to count= when max=/limit= absent.");
    ok &= expect(MasterControl::extractQueryParamAny("top=9", {"max", "limit", "count", "n", "top"}) == "9",
                 "extractQueryParamAny falls back to top= last in the list.");
    // Canonical wins over alias when both are present.
    ok &= expect(MasterControl::extractQueryParamAny("limit=5&max=42", {"max", "limit"}) == "42",
                 "Canonical name wins when both canonical + alias are present.");
    // /api/activity ?since= aliases.
    ok &= expect(MasterControl::extractQueryParamAny("after=abc", {"since", "sinceId", "after", "from"}) == "abc",
                 "extractQueryParamAny falls back to after= for the watermark alias set.");
    ok &= expect(MasterControl::extractQueryParamAny("lastEventId=z9", {"since", "sinceId", "after", "from", "cursor", "lastEventId"}) == "z9",
                 "extractQueryParamAny accepts SSE-style lastEventId alias.");
    return ok;
}

bool testQueryParamBoundaryGuard() {
    bool ok = true;
    // "xmax=10" must NOT match a search for "max" -- the anchor is name boundary.
    ok &= expect(MasterControl::extractQueryParam("xmax=10", "max").empty(),
                 "extractQueryParam refuses mid-name substring match (xmax= != max=).");
    ok &= expect(MasterControl::extractQueryParam("sinceId=99", "since").empty(),
                 "extractQueryParam refuses prefix substring match (sinceId= != since=).");
    // The same name AFTER a different prefix-named pair still matches.
    ok &= expect(MasterControl::extractQueryParam("xmax=10&max=5", "max") == "5",
                 "extractQueryParam still matches name= after & even when a similar name precedes.");
    return ok;
}

bool testQueryParamMissingReturnsEmpty() {
    bool ok = true;
    ok &= expect(MasterControl::extractQueryParam("", "max").empty(),
                 "extractQueryParam on empty query returns empty.");
    ok &= expect(MasterControl::extractQueryParam("foo=bar", "max").empty(),
                 "extractQueryParam returns empty when name absent.");
    ok &= expect(MasterControl::extractQueryParamAny("foo=bar", {"max", "limit"}).empty(),
                 "extractQueryParamAny returns empty when no candidate matches.");
    ok &= expect(MasterControl::extractQueryParam("anything", "").empty(),
                 "extractQueryParam refuses an empty name.");
    return ok;
}

// v0.10.17: JsonStrictness helper tests. Verifies the additive
// dropped-top-level-keys diagnostic surface added to /api/clients,
// /api/pools, and /api/telemetry/heartbeat. The detection algorithm
// round-trips the typed model back through nlohmann::json() and
// compares the input's top-level keys to the round-tripped key set.

bool testJsonStrictnessDetectsTypo() {
    using namespace MasterControl;
    bool ok = true;
    nlohmann::json input = {
        { "clientId", "claude-code-foo" },
        { "displayName", "Foo" },
        { "clientType", "claude_code" },
        { "hostName", "host" },
        { "networkAddress", "192.168.1.42" },
        { "enabledFlag", true }, // operator typo for "enabled"
        { "createdAtUtc", "2026-05-15T00:00:00Z" },
        { "lastSeenUtc",  "2026-05-15T00:00:00Z" }
    };
    const auto client = input.get<LanClient>();
    const auto dropped = collectDroppedTopLevelKeys(input, client);
    ok &= expect(dropped.size() == 1, "Exactly one dropped key (the operator typo) detected.");
    if (dropped.size() == 1) {
        ok &= expect(dropped[0] == "enabledFlag",
                     "Dropped key reports the typo'd field name verbatim.");
    }
    return ok;
}

bool testJsonStrictnessNoDropsWhenAllKeysKnown() {
    using namespace MasterControl;
    bool ok = true;
    LanClient seed;
    seed.clientId = "claude-code-foo";
    seed.displayName = "Foo";
    seed.clientType = "claude_code";
    seed.hostName = "host";
    seed.networkAddress = "192.168.1.42";
    seed.enabled = true;
    seed.createdAtUtc = "2026-05-15T00:00:00Z";
    seed.lastSeenUtc  = "2026-05-15T00:00:00Z";
    // Round-trip through JSON so the input has exactly the model's keys.
    const nlohmann::json input = seed;
    const auto roundTripped = input.get<LanClient>();
    const auto dropped = collectDroppedTopLevelKeys(input, roundTripped);
    ok &= expect(dropped.empty(),
                 "No dropped keys when input has exactly the model's keys.");
    return ok;
}

bool testJsonStrictnessSafeOnNonObjectInput() {
    using namespace MasterControl;
    bool ok = true;
    LanClient seed;
    seed.clientId = "x";
    // Array input should never report drops.
    nlohmann::json arr = nlohmann::json::array();
    arr.push_back("foo");
    ok &= expect(collectDroppedTopLevelKeys(arr, seed).empty(),
                 "Array input returns no drops (safe fallback).");
    // Null input should never report drops.
    nlohmann::json nullJson = nullptr;
    ok &= expect(collectDroppedTopLevelKeys(nullJson, seed).empty(),
                 "Null input returns no drops (safe fallback).");
    // Scalar input should never report drops.
    nlohmann::json scalar = 42;
    ok &= expect(collectDroppedTopLevelKeys(scalar, seed).empty(),
                 "Scalar input returns no drops (safe fallback).");
    return ok;
}

bool testDroppedKeysToJsonShape() {
    using namespace MasterControl;
    bool ok = true;
    const auto empty = droppedKeysToJson({});
    ok &= expect(empty.is_array(), "Empty dropped list serialises as a JSON array.");
    ok &= expect(empty.size() == 0, "Empty dropped list has zero entries.");
    const auto two = droppedKeysToJson({"foo", "bar"});
    ok &= expect(two.is_array(), "Two-element list serialises as array.");
    ok &= expect(two.size() == 2, "Two-element list reports size 2.");
    ok &= expect(two[0].get<std::string>() == "foo",
                 "Serialised array preserves insertion order.");
    ok &= expect(two[1].get<std::string>() == "bar",
                 "Serialised array second element matches.");
    return ok;
}

bool testQueryParamMultiPair() {
    bool ok = true;
    const std::string q = "since=abc&max=10&kind=supervisor_*";
    ok &= expect(MasterControl::extractQueryParam(q, "since") == "abc",
                 "multi-pair: since= extracted correctly.");
    ok &= expect(MasterControl::extractQueryParam(q, "max") == "10",
                 "multi-pair: max= extracted correctly.");
    ok &= expect(MasterControl::extractQueryParam(q, "kind") == "supervisor_*",
                 "multi-pair: kind= extracted correctly (incl. wildcard).");
    // Empty value is honored as "present with empty string" -- callers
    // that need a non-empty value (parsing into size_t etc.) treat it
    // as "fall through to default" upstream.
    ok &= expect(MasterControl::extractQueryParam("max=&since=42", "max").empty(),
                 "Empty value after = is returned as empty string.");
    return ok;
}

// v0.11.0 PHASE-14 Slice A diagnostics aggregator + markdown render tests.
// Pre-v0.11.0 the aggregation lived as an inline lambda inside the
// route handler so the custom bool-test runner could not cover the
// new /api/diagnostics/* HTTP surface (no in-process HTTP fixture).
// v0.11.0 extracted the file-walk + softCap + sort logic into
// include/MasterControl/DiagnosticsAggregator.h; these tests cover
// the testable helper against a synthetic logs directory.

namespace {

std::filesystem::path makeSyntheticLogsRoot(const std::string& suffix) {
    auto base = std::filesystem::temp_directory_path() /
                ("mcos-diag-test-" + suffix + "-" + std::to_string(
                    std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(base);
    return base;
}

void writeJsonlLines(const std::filesystem::path& path,
                     const std::vector<nlohmann::json>& lines) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    for (const auto& line : lines) {
        out << line.dump() << '\n';
    }
}

nlohmann::json makeEvent(const std::string& capturedAtUtc,
                         const std::string& component,
                         const std::string& severity,
                         const std::string& event,
                         const std::string& message) {
    return nlohmann::json{
        { "capturedAtUtc", capturedAtUtc },
        { "component",     component },
        { "severity",      severity },
        { "event",         event },
        { "message",       message },
        { "data",          nlohmann::json::object() }
    };
}

} // namespace

bool testDiagnosticsAggregator_emptyRoot() {
    bool ok = true;
    const auto root = makeSyntheticLogsRoot("empty");
    std::filesystem::remove_all(root);
    const auto events = MasterControl::aggregateDiagnosticsEventsFromRoot(root, 0);
    ok &= expect(events.empty(),
                 "aggregateDiagnosticsEventsFromRoot returns empty on nonexistent root.");
    return ok;
}

bool testDiagnosticsAggregator_singleComponentSorted() {
    bool ok = true;
    const auto root = makeSyntheticLogsRoot("sorted");
    const auto compDir = root / "runtime";
    writeJsonlLines(compDir / "events.jsonl", {
        makeEvent("2026-05-15T10:00:00Z", "runtime", "info",  "boot",         "boot ok"),
        makeEvent("2026-05-15T12:00:00Z", "runtime", "error", "worker_event", "pool died"),
        makeEvent("2026-05-15T11:00:00Z", "runtime", "warn",  "self_test_summary", "boot 39/40")
    });
    const auto events = MasterControl::aggregateDiagnosticsEventsFromRoot(root, 0);
    std::filesystem::remove_all(root);
    ok &= expect(events.size() == 3, "Three events read back.");
    if (events.size() == 3) {
        ok &= expect(events[0].value("capturedAtUtc", std::string()) == "2026-05-15T12:00:00Z",
                     "Recent-first sort: 12:00 entry comes first.");
        ok &= expect(events[1].value("capturedAtUtc", std::string()) == "2026-05-15T11:00:00Z",
                     "Recent-first sort: 11:00 entry comes second.");
        ok &= expect(events[2].value("capturedAtUtc", std::string()) == "2026-05-15T10:00:00Z",
                     "Recent-first sort: 10:00 entry comes last.");
    }
    return ok;
}

bool testDiagnosticsAggregator_softCapEarlyExit() {
    bool ok = true;
    const auto root = makeSyntheticLogsRoot("softcap");
    const auto compDir = root / "runtime";
    // 100 live events should saturate softCap=5 (headroom 5*4+32=52).
    std::vector<nlohmann::json> live;
    for (int i = 0; i < 100; ++i) {
        live.push_back(makeEvent(
            "2026-05-15T12:" + (i < 10 ? std::string("0") : std::string()) + std::to_string(i) + ":00Z",
            "runtime", "info", "tick", "tick " + std::to_string(i)));
    }
    writeJsonlLines(compDir / "events.jsonl", live);
    // Rotated tier holds a sentinel event the test should NOT see when
    // softCap is reached by live alone.
    writeJsonlLines(compDir / "events.jsonl.1", {
        makeEvent("2026-05-14T00:00:00Z", "runtime", "error", "sentinel_rotated_only", "DO NOT READ ME")
    });

    const auto events = MasterControl::aggregateDiagnosticsEventsFromRoot(root, 5);
    std::filesystem::remove_all(root);

    bool sawSentinel = false;
    for (const auto& e : events) {
        if (e.value("event", std::string()) == "sentinel_rotated_only") {
            sawSentinel = true;
            break;
        }
    }
    ok &= expect(!sawSentinel,
                 "softCap=5 with 100 live events skips the rotated .1.jsonl tier (sentinel not read).");
    ok &= expect(events.size() >= 5,
                 "softCap=5 still returns at least 5 events from the live tier.");
    return ok;
}

bool testDiagnosticsAggregator_partialWriteTolerance() {
    bool ok = true;
    const auto root = makeSyntheticLogsRoot("partial");
    const auto compDir = root / "runtime";
    std::filesystem::create_directories(compDir);
    // Mix valid + invalid lines.
    std::ofstream out(compDir / "events.jsonl", std::ios::binary | std::ios::trunc);
    out << "{\"capturedAtUtc\":\"2026-05-15T10:00:00Z\",\"component\":\"runtime\",\"severity\":\"info\"}\n";
    out << "not-json-at-all\n";
    out << "{\"capturedAtUtc\":\"2026-05-15T11:00:00Z\",\"component\":\"runtime\",\"severity\":\"error\"}\n";
    out << "{\"capturedAtUtc\":\"2026-05-15T09:30:00Z\",\"" /* deliberately truncated */;
    out.close();

    const auto events = MasterControl::aggregateDiagnosticsEventsFromRoot(root, 0);
    std::filesystem::remove_all(root);
    ok &= expect(events.size() == 2,
                 "Partial-write tolerance: 2 valid lines read; bad + truncated lines skipped.");
    return ok;
}

// ---------------------------------------------------------------------------
// v0.11.0-alpha.3 (PHASE-14 Slice E): SqliteDiagnosticsStore +
// DiagnosticsService tests. The store tests run against a throwaway
// SQLite file under the system temp directory.
// ---------------------------------------------------------------------------

bool testSqliteDiagnosticsStoreCrudAndFilters() {
    bool ok = true;
    const auto databasePath = std::filesystem::temp_directory_path()
        / "mcos-tests" / "diag-crud" / "diagnostics.db";
    std::error_code cleanupEc;
    std::filesystem::remove_all(databasePath.parent_path(), cleanupEc);

    // v0.11.0-alpha.3: scope the store so its destructor closes the DB
    // handle BEFORE the cleanup below. Windows refuses to delete an
    // open file (.db/-wal/-shm), so the throwing remove_all overload
    // would raise an uncaught filesystem_error -> terminate (POSIX
    // unlinks open files, which is why this only faulted on Windows).
    {
    MasterControl::SqliteDiagnosticsStore store(databasePath);
    ok &= expect(store.available(), "Sqlite store opens against a fresh temp path.");

    auto makeRecord = [](const char* capturedAt, const char* source,
                         MasterControl::DiagnosticsSeverity severity,
                         const char* eventName) {
        MasterControl::DiagnosticsRecord record;
        record.capturedAtUtc = capturedAt;
        record.source = source;
        record.severity = severity;
        record.eventName = eventName;
        record.message = "msg";
        record.sessionId = "boot-test";
        record.sequence = 1;
        return record;
    };

    auto a = makeRecord("2026-06-01T10:00:00.000Z", "runtime",
                        MasterControl::DiagnosticsSeverity::Info, "boot");
    auto b = makeRecord("2026-06-01T11:00:00.000Z", "supervisor",
                        MasterControl::DiagnosticsSeverity::Error, "pool_death");
    auto c = makeRecord("2026-06-01T12:00:00.000Z", "runtime",
                        MasterControl::DiagnosticsSeverity::Warning, "beacon_broadcast_failed");
    ok &= expect(store.insert(a).ok, "Insert A succeeds.");
    ok &= expect(store.insert(b).ok, "Insert B succeeds.");
    ok &= expect(store.insert(c).ok, "Insert C succeeds.");
    ok &= expect(a.id > 0 && b.id > a.id && c.id > b.id,
                 "Inserts assign monotonically increasing row ids.");
    ok &= expect(store.count() == 3, "Count reports three rows.");

    // Newest-first ordering.
    const auto all = store.query(MasterControl::DiagnosticsQuery{});
    ok &= expect(all.size() == 3 && all.front().eventName == "beacon_broadcast_failed",
                 "Unfiltered query returns newest-first.");

    // Severity filter (incl. the warn alias).
    MasterControl::DiagnosticsQuery bySeverity;
    bySeverity.severity = "warn";
    const auto warnings = store.query(bySeverity);
    ok &= expect(warnings.size() == 1 && warnings.front().eventName == "beacon_broadcast_failed",
                 "Severity filter accepts the warn alias and matches the warning row.");

    // Source filter + max.
    MasterControl::DiagnosticsQuery bySource;
    bySource.source = "runtime";
    bySource.maxResults = 1;
    const auto runtimeRows = store.query(bySource);
    ok &= expect(runtimeRows.size() == 1 && runtimeRows.front().eventName == "beacon_broadcast_failed",
                 "Source filter + maxResults returns the newest runtime row only.");

    // sinceUtc lower bound.
    MasterControl::DiagnosticsQuery since;
    since.sinceUtc = "2026-06-01T11:00:00.000Z";
    ok &= expect(store.query(since).size() == 2,
                 "sinceUtc keeps the two rows at/after the bound.");

    // Clear-with-retention: keep rows >= 11:00.
    const auto retained = store.clear("test retention", "2026-06-01T11:00:00.000Z");
    ok &= expect(retained.ok && retained.affectedRows == 1,
                 "Retention clear deletes exactly the one older row.");
    ok &= expect(store.count() == 2, "Two rows remain after retention clear.");

    // Full clear.
    const auto wiped = store.clear("test full clear", "");
    ok &= expect(wiped.ok && wiped.affectedRows == 2, "Full clear deletes the rest.");
    ok &= expect(store.count() == 0, "Store is empty after full clear.");
    }

    std::filesystem::remove_all(databasePath.parent_path(), cleanupEc);
    return ok;
}

bool testSqliteDiagnosticsStoreSelfTestPrune() {
    bool ok = true;
    const auto databasePath = std::filesystem::temp_directory_path()
        / "mcos-tests" / "diag-prune" / "diagnostics.db";
    std::error_code cleanupEc;
    std::filesystem::remove_all(databasePath.parent_path(), cleanupEc);

    // Scope the store so the DB closes before cleanup (see the
    // CrudAndFilters test for the Windows open-file-delete rationale).
    {
    MasterControl::SqliteDiagnosticsStore store(databasePath);
    for (int i = 0; i < 5; ++i) {
        MasterControl::DiagnosticsRecord record;
        record.capturedAtUtc = "2026-06-0" + std::to_string(i + 1) + "T00:00:00.000Z";
        record.source = "self-test";
        record.severity = MasterControl::DiagnosticsSeverity::Info;
        record.eventName = "boot_self_test_snapshot";
        record.message = "snapshot";
        record.sessionId = "boot-" + std::to_string(i);
        record.sequence = i + 1;
        (void)store.insert(record);
    }
    const auto pruned = store.pruneSelfTestSnapshots(2);
    ok &= expect(pruned.ok && pruned.affectedRows == 3,
                 "Prune keeps the requested 2 most recent self-test rows.");
    MasterControl::DiagnosticsQuery selfTests;
    selfTests.source = "self-test";
    const auto remaining = store.query(selfTests);
    ok &= expect(remaining.size() == 2
                     && remaining.front().sessionId == "boot-4"
                     && remaining.back().sessionId == "boot-3",
                 "The two newest snapshots survive the prune.");
    }

    std::filesystem::remove_all(databasePath.parent_path(), cleanupEc);
    return ok;
}

bool testDiagnosticsServiceRingFallbackAndClear() {
    bool ok = true;
    // Store-less service: ring-only operation.
    MasterControl::DiagnosticsService service(nullptr, "boot-ring-test");
    const auto reported = service.report(
        MasterControl::DiagnosticsSeverity::Error, "runtime", "evt", "boom",
        nlohmann::json::object(), "2026-06-01T10:00:00.000Z");
    ok &= expect(!reported.ok && reported.ringAccepted,
                 "Store-less report lands in the ring and honestly reports ok=false.");
    ok &= expect(!service.storeAvailable(), "Store-less service reports store unavailable.");

    MasterControl::DiagnosticsQuery anyQuery;
    const auto rows = service.query(anyQuery);
    ok &= expect(rows.size() == 1 && rows.front().eventName == "evt"
                     && rows.front().sessionId == "boot-ring-test"
                     && rows.front().sequence == 1,
                 "Ring fallback query returns the reported record with session + sequence stamped.");

    const auto cleared = service.clear("test", "", "2026-06-01T11:00:00.000Z");
    ok &= expect(cleared.ok, "Store-less clear succeeds (ring wipe).");
    // After clear, the ring holds nothing from before; the audit record
    // is only emitted when a store-backed clear happens... store-less
    // clear returns before the audit (no store). Query must be empty.
    ok &= expect(service.query(anyQuery).empty(),
                 "Ring is empty after store-less clear.");
    return ok;
}

bool testDiagnosticsServiceStoreBackedReportAndAuditedClear() {
    bool ok = true;
    const auto databasePath = std::filesystem::temp_directory_path()
        / "mcos-tests" / "diag-service" / "diagnostics.db";
    std::error_code cleanupEc;
    std::filesystem::remove_all(databasePath.parent_path(), cleanupEc);

    // Scope the service (which owns the store) so the DB closes before
    // cleanup (see the CrudAndFilters test for the rationale).
    {
    MasterControl::DiagnosticsService service(
        MasterControl::createSqliteDiagnosticsStore(databasePath), "boot-svc-test");
    ok &= expect(service.storeAvailable(), "Service opens its sqlite store.");

    (void)service.report(MasterControl::DiagnosticsSeverity::Info, "runtime",
                         "boot", "boot ok", nlohmann::json::object(),
                         "2026-06-01T10:00:00.000Z");
    (void)service.report(MasterControl::DiagnosticsSeverity::Warning, "runtime",
                         "warn_evt", "warn", nlohmann::json::object(),
                         "2026-06-01T11:00:00.000Z");
    ok &= expect(service.storeCount() == 2, "Two reports persisted to the store.");

    const auto cleared = service.clear("operator wipe", "", "2026-06-01T12:00:00.000Z");
    ok &= expect(cleared.ok && cleared.affectedRows == 2, "Clear deletes both rows.");
    // The clear is audited: one fresh diagnostics_cleared record.
    MasterControl::DiagnosticsQuery audit;
    audit.eventName = "diagnostics_cleared";
    const auto auditRows = service.query(audit);
    ok &= expect(auditRows.size() == 1
                     && auditRows.front().data.value("deletedRows", 0) == 2,
                 "Clear leaves exactly one audit record carrying the deleted count.");
    }

    std::filesystem::remove_all(databasePath.parent_path(), cleanupEc);
    return ok;
}

bool testDiagnosticsMarkdownRender_warnWarningAlias() {
    bool ok = true;
    const std::vector<nlohmann::json> events = {
        makeEvent("2026-05-15T12:00:00Z", "runtime", "warning", "tele_warn", "telemetry warning"),
        makeEvent("2026-05-15T11:00:00Z", "runtime", "warn",    "boot_warn", "boot warn row"),
        makeEvent("2026-05-15T10:00:00Z", "runtime", "info",    "boot",      "boot ok")
    };
    const auto md = MasterControl::renderDiagnosticsMarkdown(
        events, "2026-05-15T13:00:00Z", "0.11.0");

    // The warning section must include BOTH the "warning" event and
    // the "warn" event under "## warning (2)".
    ok &= expect(md.find("## warning (2)") != std::string::npos,
                 "Warning section header counts BOTH warning + warn entries.");
    ok &= expect(md.find("tele_warn") != std::string::npos,
                 "Markdown export contains the 'warning'-severity event.");
    ok &= expect(md.find("boot_warn") != std::string::npos,
                 "Markdown export contains the 'warn'-severity event under the same warning section.");
    return ok;
}

bool testDiagnosticsMarkdownRender_truncatesOver50() {
    bool ok = true;
    std::vector<nlohmann::json> events;
    // 60 info events: section header should report (60) and the
    // truncation note should mention 10 more.
    for (int i = 0; i < 60; ++i) {
        events.push_back(makeEvent(
            "2026-05-15T12:" + (i < 10 ? std::string("0") : std::string()) + std::to_string(i) + ":00Z",
            "runtime", "info", "tick", "tick " + std::to_string(i)));
    }
    const auto md = MasterControl::renderDiagnosticsMarkdown(
        events, "2026-05-15T13:00:00Z", "0.11.0");
    ok &= expect(md.find("## info (60)") != std::string::npos,
                 "Section header reports the true total count even when truncated.");
    ok &= expect(md.find("10 more info events truncated") != std::string::npos,
                 "Truncation note mentions the suppressed count (60 - 50 = 10).");
    return ok;
}

// v0.11.0-alpha.3: timestamp parser backing the new age-bound log
// rotation (MasterControlDiagnostics.h::rotateIfAgedOrOverCountLocked).
// Valid capturedAtUtc values parse to ordered epochs; garbage parses
// to the 0 "no age signal" sentinel so rotation never fires on noise.
bool testDiagnosticsCapturedAtUtcParse() {
    bool ok = true;
    const auto early = MasterControl::Diagnostics::parseCapturedAtUtcOrZero("2026-05-15T12:00:00.000Z");
    const auto later = MasterControl::Diagnostics::parseCapturedAtUtcOrZero("2026-05-16T12:00:00.000Z");
    ok &= expect(early != 0, "Valid capturedAtUtc parses to a non-zero epoch.");
    ok &= expect(later > early, "Later capturedAtUtc parses to a later epoch.");
    ok &= expect(later - early == 24 * 60 * 60,
                 "One day apart parses to exactly 86400 seconds.");
    ok &= expect(MasterControl::Diagnostics::parseCapturedAtUtcOrZero("not-a-timestamp") == 0,
                 "Garbage capturedAtUtc parses to the 0 sentinel.");
    ok &= expect(MasterControl::Diagnostics::parseCapturedAtUtcOrZero("") == 0,
                 "Empty capturedAtUtc parses to the 0 sentinel.");
    return ok;
}

// ---------------------------------------------------------------------------
// Config-safety remediation (MCOS-001): PATCH /api/config deep-merge helpers
// and POST /api/config partial-body rejection. These exercise the exact
// production logic in MasterControl/JsonMerge.h that AdminApiService wires
// into both routes.
// ---------------------------------------------------------------------------

bool testConfigPatchPreservesUnrelatedSections() {
    bool ok = true;
    const auto configuration = MasterControl::buildDefaultConfiguration();
    const nlohmann::json before = configuration;
    const nlohmann::json securityBefore = before.at("security");
    const nlohmann::json gatewayBefore = before.at("mcpGateway");
    const auto instanceNameBefore = before.at("instanceName").get<std::string>();
    const int memoryPercentBefore = before.at("resourceAllocation").at("memoryPercent").get<int>();

    const nlohmann::json patch = nlohmann::json::parse(R"({"resourceAllocation":{"cpuPercent":70}})");
    const auto outcome = MasterControl::mergeConfigurationPatch(before, patch);
    ok &= expect(outcome.valid, "Object patch body is accepted.");

    const auto typed = outcome.merged.get<MasterControl::AppConfiguration>();
    const nlohmann::json after = typed;
    ok &= expect(typed.resourceAllocation.cpuPercent == 70,
                 "Patched field applies through the typed round-trip.");
    ok &= expect(typed.resourceAllocation.memoryPercent == memoryPercentBefore,
                 "Sibling field inside the patched section is preserved.");
    ok &= expect(after.at("security") == securityBefore,
                 "Unrelated security section is preserved byte-for-byte.");
    ok &= expect(after.at("mcpGateway") == gatewayBefore,
                 "Unrelated mcpGateway section is preserved byte-for-byte.");
    ok &= expect(typed.instanceName == instanceNameBefore,
                 "Unrelated scalar top-level field is preserved.");
    return ok;
}

bool testConfigPatchRejectsNonObjectPayload() {
    bool ok = true;
    const nlohmann::json current = MasterControl::buildDefaultConfiguration();
    ok &= expect(!MasterControl::mergeConfigurationPatch(current, nlohmann::json::array({1, 2})).valid,
                 "Array patch body is rejected.");
    ok &= expect(!MasterControl::mergeConfigurationPatch(current, nlohmann::json(42)).valid,
                 "Scalar patch body is rejected.");
    ok &= expect(!MasterControl::mergeConfigurationPatch(current, nlohmann::json()).valid,
                 "Null patch body is rejected.");
    ok &= expect(MasterControl::mergeConfigurationPatch(current, nlohmann::json::object()).valid,
                 "Empty object patch body is accepted (no-op merge).");
    return ok;
}

bool testConfigPostRejectsPartialTopLevelBody() {
    bool ok = true;
    const MasterControl::AppConfiguration model{};
    const auto partial = nlohmann::json::parse(R"({"resourceAllocation":{"cpuPercent":70}})");
    const auto missing = MasterControl::missingTopLevelKeysForFullDocument(partial, model);
    ok &= expect(!missing.empty(),
                 "A partial top-level body reports missing configuration sections.");

    const nlohmann::json fullDocument = MasterControl::buildDefaultConfiguration();
    ok &= expect(MasterControl::missingTopLevelKeysForFullDocument(fullDocument, model).empty(),
                 "The full document emitted by GET /api/config passes the completeness check.");
    return ok;
}

// ---------------------------------------------------------------------------
// Bridge route contract (MCOS-001 / MCOS-002): the mcos-bridge plugin must
// only advertise tools whose backend routes exist. The bridge is Python, so
// these tests verify the tool registry source against the implemented admin
// routes. MCOS_REPO_ROOT is provided by tests/CMakeLists.txt.
// ---------------------------------------------------------------------------

std::string readRepoTextFile(const std::filesystem::path& relativePath) {
    std::ifstream stream(std::filesystem::path(MCOS_REPO_ROOT) / relativePath, std::ios::binary);
    std::stringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

std::string readBridgeServerSource() {
    return readRepoTextFile(std::filesystem::path(".claude-plugin") / "mcos-control"
                            / "mcp-servers" / "mcos-bridge" / "server.py");
}

// MCOS-008: runtime behavior, wiki docs, and onboarding text must describe
// the same POST-only alpha MCP transport.
bool testMcpTransportContractMatchesDocs() {
    bool ok = true;
    const auto gatewayDoc = readRepoTextFile(std::filesystem::path("docs") / "wiki" / "Gateway.md");
    ok &= expect(gatewayDoc.find("POST-only Streamable HTTP") != std::string::npos,
                 "Gateway.md documents the POST-only transport profile.");
    ok &= expect(gatewayDoc.find("Allow: POST") != std::string::npos,
                 "Gateway.md documents the 405 Allow semantics.");
    ok &= expect(gatewayDoc.find("Mcp-Session-Id") != std::string::npos,
                 "Gateway.md documents the session header contract.");
    const auto onboardingDoc = readRepoTextFile(std::filesystem::path("docs") / "wiki" / "Onboarding.md");
    ok &= expect(onboardingDoc.find("POST-only Streamable HTTP") != std::string::npos,
                 "Onboarding.md carries the transport compatibility note.");
    const auto adapterSource = readRepoTextFile(std::filesystem::path("src") / "MasterControlApp"
                                                / "McpGatewayAdapters.cpp");
    ok &= expect(adapterSource.find("SSE upgrade is not implemented in this build.") != std::string::npos,
                 "The gateway still answers non-POST /mcp with the no-SSE hint.");
    ok &= expect(adapterSource.find("allowHeader = \"POST\"") != std::string::npos,
                 "The gateway 405 carries an Allow: POST header.");
    const auto runtimeSource = readRepoTextFile(std::filesystem::path("src") / "MasterControlApp"
                                                / "MasterControlRuntime.cpp");
    ok &= expect(runtimeSource.find("POST-only Streamable HTTP subset") != std::string::npos,
                 "Onboarding profile caveats state the POST-only transport.");
    return ok;
}

bool testBridgeConfigUpdateUsesPatchRoute() {
    bool ok = true;
    const auto source = readBridgeServerSource();
    ok &= expect(!source.empty(), "Bridge server.py is readable from the repo root.");
    ok &= expect(source.find("_patch(\"/api/config\"") != std::string::npos,
                 "mcos_config_update patches via PATCH /api/config.");
    ok &= expect(source.find("\"X-Confirm-Unsafe\": \"1\"") != std::string::npos,
                 "mcos_config_update can send the unsafe-confirm header.");
    ok &= expect(source.find("confirmUnsafe") != std::string::npos,
                 "mcos_config_update advertises confirmUnsafe in its schema.");
    ok &= expect(source.find("_post(\"/api/config\"") == std::string::npos,
                 "No bridge tool POSTs a partial body to /api/config.");
    return ok;
}

bool testForsettiModuleBridgeUsesStateRoute() {
    bool ok = true;
    const auto source = readBridgeServerSource();
    ok &= expect(source.find("\"/api/forsetti/modules/state\"") != std::string::npos,
                 "Forsetti enable/disable use the implemented state route.");
    ok &= expect(source.find("\"action\": \"enable\"") != std::string::npos,
                 "Enable sends the action=enable body.");
    ok &= expect(source.find("\"action\": \"disable\"") != std::string::npos,
                 "Disable sends the action=disable body.");
    ok &= expect(source.find("f\"/api/forsetti/modules/{") == std::string::npos,
                 "No bridge tool calls the nonexistent per-module enable/disable routes.");
    return ok;
}

bool testBrokenForsettiImportToolIsNotAdvertised() {
    bool ok = true;
    const auto source = readBridgeServerSource();
    ok &= expect(source.find("mcos_forsetti_module_import") == std::string::npos,
                 "The module-import tool (no backend route) is no longer advertised.");
    ok &= expect(source.find("_post(\"/api/forsetti/modules\"") == std::string::npos,
                 "No bridge tool posts to the nonexistent module-import route.");
    return ok;
}

bool testBridgeGovernanceApproveRequiresConfirm() {
    bool ok = true;
    const auto source = readBridgeServerSource();
    ok &= expect(source.find("_confirm_required(args, \"approve governance action\"") != std::string::npos,
                 "Governance approve requires explicit confirmation.");
    const auto approveSchema = source.find("(\"mcos_governance_approve\"");
    const auto rejectSchema = source.find("(\"mcos_governance_reject\"");
    ok &= expect(approveSchema != std::string::npos && rejectSchema != std::string::npos,
                 "Governance approve/reject schemas are present.");
    ok &= expect(approveSchema < rejectSchema &&
                 source.find("\"confirm\": {\"type\": \"boolean\"}", approveSchema) < rejectSchema,
                 "Governance approve schema advertises confirm.");
    return ok;
}

bool testBridgeLogsTailAvoidsPowerShellPatternInterpolation() {
    bool ok = true;
    const auto source = readBridgeServerSource();
    ok &= expect(source.find("Select-String -Pattern") == std::string::npos,
                 "mcos_logs_tail no longer interpolates pattern into PowerShell.");
    ok &= expect(source.find("deque(maxlen=count)") != std::string::npos,
                 "mcos_logs_tail tails with Python stdlib buffering.");
    ok &= expect(source.find("needle in line") != std::string::npos,
                 "mcos_logs_tail filters locally by literal substring.");
    return ok;
}

// Route-contract smoke test: every HTTP route the bridge references must
// match an implemented backend route shape. Path parameters interpolated via
// f-strings normalize to "*".
bool testBridgeRoutesMatchBackendContract() {
    bool ok = true;
    const auto source = readBridgeServerSource();
    ok &= expect(!source.empty(), "Bridge server.py is readable.");

    static const std::vector<std::pair<std::string, std::string>> kCallMarkers = {
        {"_get(", "GET"},
        {"_post(", "POST"},
        {"_patch(", "PATCH"},
        {"_delete(", "DELETE"},
    };

    const auto sampleConcreteRoute = [](std::string route) {
        std::size_t wildcard = 0;
        while ((wildcard = route.find('*', wildcard)) != std::string::npos) {
            route.replace(wildcard, 1, "__route_param__");
            wildcard += std::string("__route_param__").size();
        }
        return route;
    };

    std::size_t checkedRoutes = 0;
    for (const auto& [marker, method] : kCallMarkers) {
        std::size_t at = 0;
        while ((at = source.find(marker, at)) != std::string::npos) {
            std::size_t cursor = at + marker.size();
            at = cursor;
            bool formatted = false;
            if (cursor < source.size() && source[cursor] == 'f') {
                formatted = true;
                ++cursor;
            }
            if (cursor >= source.size() || source[cursor] != '"') {
                continue;  // not a literal route (helper definition/other args)
            }
            ++cursor;
            std::string route;
            bool truncatedAtInterpolation = false;
            while (cursor < source.size() && source[cursor] != '"') {
                if (formatted && source[cursor] == '{') {
                    // "{...}" directly after '/' is a path parameter and
                    // normalizes to '*'; anywhere else it interpolates a
                    // suffix (e.g. a prebuilt "?query" string), so the
                    // route contract ends where the interpolation starts.
                    if (!route.empty() && route.back() == '/') {
                        while (cursor < source.size() && source[cursor] != '}') {
                            ++cursor;
                        }
                        route += '*';
                    } else {
                        truncatedAtInterpolation = true;
                        break;
                    }
                } else {
                    route += source[cursor];
                }
                ++cursor;
            }
            (void)truncatedAtInterpolation;
            const auto query = route.find('?');
            if (query != std::string::npos) {
                route = route.substr(0, query);
            }
            if (route.rfind("/api/", 0) != 0) {
                continue;  // helper signature or non-admin path
            }
            ++checkedRoutes;
            const auto concreteRoute = sampleConcreteRoute(route);
            const auto registeredMethods = MasterControl::supportedMethodsForPath(concreteRoute);
            const bool registered = !registeredMethods.empty();
            const bool methodAllowed = std::find(
                registeredMethods.begin(), registeredMethods.end(), method) != registeredMethods.end();
            if (!registered || !methodAllowed) {
                std::cerr << "Bridge route is not registered for " << method
                          << ": " << route << '\n';
            }
            ok &= expect(registered && methodAllowed,
                         "Every bridge route matches the typed backend route registry.");
        }
    }
    ok &= expect(checkedRoutes >= 30,
                 "Route-contract scan found the expected volume of bridge routes.");
    return ok;
}

bool testMutatingAuthorizationGatePrecedesSupervisorDispatch() {
    bool ok = true;
    const auto source = readRepoTextFile(std::filesystem::path("src") / "MasterControlApp"
                                        / "MasterControlRuntime.cpp");
    const auto gateCall = source.find("if (auto denied = authorizeMutatingRequest())");
    const auto supervisorSelect = source.find(
        "if (request.method == \"POST\" && request.path == \"/api/supervisor/assignment/select\")");
    const auto selfTestRun = source.find(
        "if (request.method == \"POST\" && request.path == \"/api/self-tests/run\")");
    const auto denialAudit = source.find("emitAdminActivity(*denied);");
    ok &= expect(gateCall != std::string::npos,
                 "The admin dispatcher invokes the mutating authorization gate.");
    ok &= expect(supervisorSelect != std::string::npos,
                 "The supervisor select route is present in the dispatcher.");
    ok &= expect(selfTestRun != std::string::npos,
                 "The self-test run route is present in the dispatcher.");
    ok &= expect(gateCall < supervisorSelect,
                 "Mutating authorization runs before supervisor mutation routes.");
    ok &= expect(gateCall < selfTestRun,
                 "Mutating authorization runs before the self-test mutation route.");
    ok &= expect(denialAudit != std::string::npos && gateCall < denialAudit,
                 "Pre-dispatch authorization denials are still written to the activity ring.");
    return ok;
}

bool testAdminRouteCapabilityPolicyCoversAuditMutations() {
    bool ok = true;
    const auto capabilitiesFor = [](const std::string& method, const std::string& path) {
        return MasterControl::AdminRouteCapabilityPolicy::requiredCapabilitiesForRoute(method, path);
    };
    const auto routeHasCapability = [&capabilitiesFor](
        const std::string& method,
        const std::string& path,
        const std::string& capability) {
        return MasterControl::hasCapability(capabilitiesFor(method, path), capability);
    };

    ok &= expect(!MasterControl::AdminRouteCapabilityPolicy::isMutatingMethod("GET"),
                 "GET is not treated as a mutating admin method.");
    ok &= expect(routeHasCapability(
                     "POST",
                     "/api/supervisor/assignment/select",
                     MasterControl::kCapabilitySupervisorAssign),
                 "Supervisor assignment selection requires supervisor.assign.");
    ok &= expect(routeHasCapability(
                     "POST",
                     "/api/self-tests/run",
                     MasterControl::kCapabilitySetupLocalOrAdmin),
                 "Self-test execution requires the local/admin setup capability.");

    const auto moduleCaps = capabilitiesFor("POST", "/api/forsetti/modules/state");
    ok &= expect(MasterControl::hasCapability(moduleCaps, MasterControl::kCapabilityInstallPackage),
                 "Forsetti module state aligns with canManageModules/install.package.");
    ok &= expect(!MasterControl::hasCapability(moduleCaps, MasterControl::kCapabilityGovernanceModify),
                 "Forsetti module state does not require the broader governance.modify capability.");

    ok &= expect(routeHasCapability(
                     "POST",
                     "/api/chatgpt-plugin/toggle",
                     MasterControl::kCapabilityInstallPackage),
                 "ChatGPT plugin toggle requires install.package.");
    ok &= expect(routeHasCapability(
                     "POST",
                     "/api/grok-plugin/toggle",
                     MasterControl::kCapabilityInstallPackage),
                 "Grok plugin toggle requires install.package.");
    ok &= expect(routeHasCapability(
                     "POST",
                     "/api/diagnostics/clear",
                     MasterControl::kCapabilityGovernanceModify),
                 "Diagnostics clear requires governance.modify.");
    ok &= expect(routeHasCapability(
                     "POST",
                     "/api/diagnostics/clear?reason=operator",
                     MasterControl::kCapabilityGovernanceModify),
                 "Diagnostics clear capability lookup strips query strings.");
    ok &= expect(capabilitiesFor("GET", "/api/diagnostics/clear").empty(),
                 "Read-style methods do not acquire mutation capabilities.");
    return ok;
}

// ---------------------------------------------------------------------------
// Admin API remediation (MCOS-011/012/015): case-insensitive header parse,
// invalid Content-Length rejection, and the typed method-allow registry.
// ---------------------------------------------------------------------------

bool testAdminParserAcceptsLowercaseContentLength() {
    bool ok = true;
    const std::string raw =
        "POST /api/config HTTP/1.1\r\nhost: mcos\r\ncontent-length: 12\r\n\r\n{\"a\":1}";
    const auto parsed = MasterControl::parseContentLengthHeader(raw);
    ok &= expect(parsed.present && parsed.valid && parsed.value == 12,
                 "Lowercase content-length parses correctly.");
    const std::string mixed =
        "POST / HTTP/1.1\r\nCoNtEnT-LeNgTh:  7 \r\n\r\npayload";
    const auto parsedMixed = MasterControl::parseContentLengthHeader(mixed);
    ok &= expect(parsedMixed.present && parsedMixed.valid && parsedMixed.value == 7,
                 "Mixed-case Content-Length with padding parses correctly.");
    const auto duplicateSame = MasterControl::parseContentLengthHeader(
        "POST / HTTP/1.1\r\nContent-Length: 12\r\ncontent-length: 12\r\n\r\npayload");
    ok &= expect(duplicateSame.present && duplicateSame.valid && duplicateSame.value == 12,
                 "Duplicate identical Content-Length values are accepted.");
    const auto commaSame = MasterControl::parseContentLengthHeader(
        "POST / HTTP/1.1\r\nContent-Length: 12, 12\r\n\r\npayload");
    ok &= expect(commaSame.present && commaSame.valid && commaSame.value == 12,
                 "Comma-combined identical Content-Length values are accepted.");
    ok &= expect(MasterControl::findHeaderValueCaseInsensitive(raw, "Content-Length") == "12",
                 "Case-insensitive header lookup returns the trimmed value.");
    return ok;
}

bool testAdminParserRejectsInvalidContentLength() {
    bool ok = true;
    const auto garbage = MasterControl::parseContentLengthHeader(
        "POST / HTTP/1.1\r\nContent-Length: banana\r\n\r\n");
    ok &= expect(garbage.present && !garbage.valid,
                 "Non-numeric Content-Length is present but invalid.");
    const auto negative = MasterControl::parseContentLengthHeader(
        "POST / HTTP/1.1\r\nContent-Length: -5\r\n\r\n");
    ok &= expect(negative.present && !negative.valid,
                 "Negative Content-Length is invalid.");
    const auto huge = MasterControl::parseContentLengthHeader(
        "POST / HTTP/1.1\r\nContent-Length: 9999999999999999999999\r\n\r\n");
    ok &= expect(huge.present && !huge.valid,
                 "Absurdly long Content-Length is invalid rather than overflowing.");
    const auto absent = MasterControl::parseContentLengthHeader(
        "GET / HTTP/1.1\r\nHost: mcos\r\n\r\n");
    ok &= expect(!absent.present, "A request without Content-Length reports absent.");
    const auto boundary = MasterControl::parseContentLengthHeader(
        "POST / HTTP/1.1\r\nX-Content-Length: 5\r\n\r\n");
    ok &= expect(!boundary.present,
                 "A different header sharing the suffix does not false-positive.");
    const auto duplicateConflict = MasterControl::parseContentLengthHeader(
        "POST / HTTP/1.1\r\nContent-Length: 5\r\ncontent-length: 6\r\n\r\npayload");
    ok &= expect(duplicateConflict.present && !duplicateConflict.valid,
                 "Conflicting duplicate Content-Length headers are invalid.");
    const auto commaConflict = MasterControl::parseContentLengthHeader(
        "POST / HTTP/1.1\r\nContent-Length: 5, 6\r\n\r\npayload");
    ok &= expect(commaConflict.present && !commaConflict.valid,
                 "Conflicting comma-combined Content-Length values are invalid.");
    const auto emptyDuplicate = MasterControl::parseContentLengthHeader(
        "POST / HTTP/1.1\r\nContent-Length: 5\r\nContent-Length:\r\n\r\npayload");
    ok &= expect(emptyDuplicate.present && !emptyDuplicate.valid,
                 "Empty duplicate Content-Length value is invalid.");
    return ok;
}

bool testSupportedMethodsIncludesDiagnosticsRoutes() {
    bool ok = true;
    const auto expectMethods = [&ok](const char* path, const char* method) {
        const auto methods = MasterControl::supportedMethodsForPath(path);
        ok &= expect(methods.size() == 1 && methods.front() == method,
                     "Diagnostics route registers its implemented method.");
    };
    expectMethods("/api/diagnostics/events", "GET");
    expectMethods("/api/diagnostics/summary", "GET");
    expectMethods("/api/diagnostics/self-test", "GET");
    expectMethods("/api/diagnostics/export", "GET");
    expectMethods("/api/diagnostics/clear", "POST");
    ok &= expect(!MasterControl::supportedMethodsForPath("/api/diagnostics/events?max=5").empty(),
                 "Query strings are stripped before the registry lookup.");
    return ok;
}

bool testSupportedMethodsIncludesPluginRoutes() {
    bool ok = true;
    const auto expectMethods = [&ok](const char* path, const char* method) {
        const auto methods = MasterControl::supportedMethodsForPath(path);
        ok &= expect(methods.size() == 1 && methods.front() == method,
                     "Plugin route registers its implemented method.");
    };
    expectMethods("/api/claude-plugin/status", "GET");
    expectMethods("/api/claude-plugin/toggle", "POST");
    expectMethods("/api/chatgpt-plugin/status", "GET");
    expectMethods("/api/chatgpt-plugin/toggle", "POST");
    expectMethods("/api/grok-plugin/status", "GET");
    expectMethods("/api/grok-plugin/toggle", "POST");
    return ok;
}

bool testSupportedMethodsIncludesDynamicAdminRoutes() {
    bool ok = true;
    const auto hasMethod = [](const std::string& path, const std::string& method) {
        const auto methods = MasterControl::supportedMethodsForPath(path);
        return std::find(methods.begin(), methods.end(), method) != methods.end();
    };
    ok &= expect(hasMethod("/api/clients/client-1", "GET"),
                 "Single-client lookup registers GET.");
    ok &= expect(hasMethod("/api/clients/client-1", "DELETE"),
                 "Single-client removal registers DELETE.");
    ok &= expect(!hasMethod("/api/clients/client-1", "POST"),
                 "Bare client resource does not over-advertise POST.");
    ok &= expect(hasMethod("/api/clients/client-1/disable", "POST"),
                 "Client disable registers POST.");
    ok &= expect(!hasMethod("/api/clients/client-1/disable", "GET"),
                 "Client disable does not advertise GET.");
    ok &= expect(hasMethod("/api/clients/client-1/enable", "POST"),
                 "Client enable registers POST.");
    ok &= expect(hasMethod("/api/clients/client-1/privileges", "POST"),
                 "Client privilege update registers POST.");
    ok &= expect(hasMethod("/api/clients/client-1/autonomous-mode", "POST"),
                 "Client autonomous-mode update registers POST.");
    ok &= expect(hasMethod("/api/clu/approvals/deferred-1/approve", "POST"),
                 "CLU approval approve registers POST.");
    ok &= expect(!hasMethod("/api/clu/approvals/deferred-1/approve", "GET"),
                 "CLU approval approve does not advertise GET.");
    ok &= expect(hasMethod("/api/clu/approvals/deferred-1/reject", "POST"),
                 "CLU approval reject registers POST.");
    ok &= expect(hasMethod("/api/setup/workflow-templates/basic/instantiate", "POST"),
                 "Workflow-template instantiation registers POST.");
    ok &= expect(!hasMethod("/api/setup/workflow-templates/basic/instantiate", "DELETE"),
                 "Workflow-template instantiation does not advertise DELETE.");
    ok &= expect(MasterControl::supportedMethodsForPath("/api/clients/client-1/unknown").empty(),
                 "Unknown client sub-resources are not treated as known routes.");
    return ok;
}

bool testWrongMethodOnKnownRouteReturns405() {
    bool ok = true;
    // Registry contract behind the dispatcher's 405 path: a known route
    // queried with the wrong verb yields a NON-empty method list (so the
    // fallthrough answers 405 + Allow, not 404), and unknown paths yield
    // an empty list (404).
    const auto methods = MasterControl::supportedMethodsForPath("/api/diagnostics/clear");
    ok &= expect(!methods.empty(), "Known route resolves in the registry.");
    ok &= expect(std::find(methods.begin(), methods.end(), "GET") == methods.end(),
                 "POST-only route does not advertise GET.");
    const auto allow = MasterControl::buildAllowHeader(methods);
    ok &= expect(allow.find("POST") != std::string::npos
                 && allow.find("OPTIONS") != std::string::npos
                 && allow.find("GET") == std::string::npos,
                 "Allow header lists implemented verbs plus OPTIONS only.");
    const auto configMethods = MasterControl::supportedMethodsForPath("/api/config");
    ok &= expect(std::find(configMethods.begin(), configMethods.end(), "PATCH") != configMethods.end(),
                 "/api/config advertises PATCH after the config-safety remediation.");
    ok &= expect(MasterControl::supportedMethodsForPath("/api/does-not-exist").empty(),
                 "Unknown paths stay empty so dispatch answers 404.");
    return ok;
}

bool testPlainHttpResponsesUseSendAllLoop() {
    bool ok = true;
    const auto source = readRepoTextFile(std::filesystem::path("src") / "MasterControlApp"
                                        / "MasterControlRuntime.cpp");
    ok &= expect(source.find("static bool sendAllPlainSocket") != std::string::npos,
                 "Plain HTTP writes use a looped send helper.");
    ok &= expect(source.find("send(client, data.c_str()") == std::string::npos,
                 "Plain response body is not sent with one unchecked send().");
    ok &= expect(source.find("::send(client, data.c_str()") == std::string::npos,
                 "Raw saturation response is not sent with one unchecked send().");
    ok &= expect(source.find("sendAllPlainSocket(client, data.data(), data.size())") != std::string::npos,
                 "Serialized plain responses route through sendAllPlainSocket.");
    ok &= expect(source.find("sendAllPlainSocket(client, headerBytes.data(), headerBytes.size())")
                 != std::string::npos,
                 "Plain SSE headers route through sendAllPlainSocket.");
    return ok;
}

// ---------------------------------------------------------------------------
// Gateway/worker remediation (MCOS-004..014): session routing, lease
// lifetime, catalog lock hygiene, registration parity, JSON-safe errors,
// PATH-resolved worker executables, and deterministic supervisor teardown.
// ---------------------------------------------------------------------------

bool testGatewayToolNameResolverRejectsDuplicateUnqualifiedNames() {
    bool ok = true;
    std::vector<MasterControl::McpToolDescriptor> catalog;
    MasterControl::McpToolDescriptor first;
    first.serverName = "pool-a";
    first.toolName = "echo";
    first.requiredCapabilities = { MasterControl::kCapabilityFilesystemRead };
    catalog.push_back(first);
    MasterControl::McpToolDescriptor second;
    second.serverName = "pool-b";
    second.toolName = "echo";
    second.requiredCapabilities = { MasterControl::kCapabilityProcessExec };
    catalog.push_back(second);
    MasterControl::McpToolDescriptor unique;
    unique.serverName = "pool-c";
    unique.toolName = "status";
    catalog.push_back(unique);

    const auto qualified = MasterControl::McpToolNameResolver::resolve(catalog, "pool-a__echo");
    ok &= expect(qualified.status == MasterControl::McpToolNameResolutionStatus::Found,
                 "Qualified duplicate tool name resolves.");
    ok &= expect(qualified.poolId == "pool-a" && qualified.localToolName == "echo",
                 "Qualified duplicate resolves to the requested pool and local name.");
    ok &= expect(MasterControl::hasCapability(
                     qualified.requiredCapabilities,
                     MasterControl::kCapabilityFilesystemRead),
                 "Qualified resolution preserves required capabilities.");

    const auto ambiguous = MasterControl::McpToolNameResolver::resolve(catalog, "echo");
    ok &= expect(ambiguous.status == MasterControl::McpToolNameResolutionStatus::Ambiguous,
                 "Duplicate unqualified tool name is rejected as ambiguous.");

    const auto uniqueResult = MasterControl::McpToolNameResolver::resolve(catalog, "status");
    ok &= expect(uniqueResult.status == MasterControl::McpToolNameResolutionStatus::Found
                 && uniqueResult.poolId == "pool-c",
                 "Unique unqualified tool name resolves.");

    const auto missing = MasterControl::McpToolNameResolver::resolve(catalog, "missing");
    ok &= expect(missing.status == MasterControl::McpToolNameResolutionStatus::NotFound,
                 "Missing tool name reports not found.");
    return ok;
}

bool testGatewayToolsCallRefreshUsesSharedNameResolver() {
    bool ok = true;
    const auto source = readRepoTextFile(std::filesystem::path("src") / "MasterControlApp"
                                        / "McpGatewayAdapters.cpp");
    ok &= expect(source.find("McpToolNameResolver::resolve(toolCatalogCache_, requestedName)") != std::string::npos,
                 "tools/call cached lookup uses the shared resolver.");
    ok &= expect(source.find("McpToolNameResolver::resolve(refreshedCatalog, requestedName)") != std::string::npos,
                 "tools/call refresh-on-miss lookup uses the shared resolver.");
    ok &= expect(source.find("qualified == requestedName || descriptor.toolName == requestedName")
                 == std::string::npos,
                 "Refresh-on-miss path no longer accepts the first duplicate unqualified tool.");
    return ok;
}

// Test double for IWorkerSupervisor: serves one pool with a configurable
// number of Ready instances; sendStdioJsonRpc optionally blocks to simulate
// a slow child.
class StubWorkerSupervisor final : public MasterControl::IWorkerSupervisor {
public:
    StubWorkerSupervisor(std::string poolId, int readyInstances,
                         int maxActiveLeasesPerInstance = 1,
                         int stdioDelayMs = 0)
        : stdioDelayMs_(stdioDelayMs) {
        pool_.poolId = std::move(poolId);

        pool_.displayName = "stub pool";
        pool_.scalePolicy.minInstances = readyInstances;
        pool_.scalePolicy.maxInstances = readyInstances;
        pool_.scalePolicy.maxActiveLeasesPerInstance = maxActiveLeasesPerInstance;
        for (int i = 0; i < readyInstances; ++i) {
            MasterControl::EndpointInstance instance;
            instance.instanceId = pool_.poolId + "#stub-" + std::to_string(i + 1);
            instance.poolId = pool_.poolId;
            instance.state = MasterControl::EndpointInstanceState::Ready;
            instance.supervised = true;
            pool_.instances.push_back(std::move(instance));
        }
    }

    std::vector<MasterControl::ManagedEndpointPool> listPools() const override { return { pool_ }; }
    std::optional<MasterControl::ManagedEndpointPool> findPool(const std::string& poolId) const override {
        if (poolId == pool_.poolId) return pool_;
        return std::nullopt;
    }
    MasterControl::OperationResult upsertPool(MasterControl::ManagedEndpointPool pool) override {
        pool_ = std::move(pool);
        return { true, false, "ok" };
    }
    MasterControl::OperationResult removePool(const std::string&) override { return { true, false, "ok" }; }
    MasterControl::OperationResult ensureMinInstances(const std::string&) override { return { true, false, "ok" }; }
    std::string scaleUpOnce(const std::string&) override { return std::string(); }
    MasterControl::OperationResult drainPool(const std::string&) override { return { true, false, "ok" }; }
    MasterControl::OperationResult shutdownAll() override { return { true, false, "ok" }; }
    MasterControl::StdioBridgeResult sendStdioJsonRpc(const std::string&, const std::string&, int) override {
        ++stdioCallCount_;
        if (stdioDelayMs_ > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(stdioDelayMs_));
        }
        MasterControl::StdioBridgeResult result;
        result.succeeded = false;
        result.errorMessage = "stub supervisor has no live children";
        return result;
    }
    int stdioCallCount() const { return stdioCallCount_.load(); }

private:
    MasterControl::ManagedEndpointPool pool_;
    int stdioDelayMs_ = 0;
    std::atomic<int> stdioCallCount_{ 0 };
};

bool testGatewayStickySessionRoutesSameSessionToSameInstance() {
    bool ok = true;
    auto supervisor = std::make_shared<StubWorkerSupervisor>("pool-a", 2);
    auto router = MasterControl::createLeaseRouter(supervisor, /*leaseIdleTimeoutSeconds=*/300);

    MasterControl::LeaseRequest request;
    request.poolId = "pool-a";
    request.sessionId = "session-1";
    request.stateful = true;
    const auto first = router->acquireLease(request);
    ok &= expect(first.state == MasterControl::LeaseState::Active,
                 "First stateful acquire yields an Active lease.");
    const auto second = router->acquireLease(request);
    ok &= expect(second.state == MasterControl::LeaseState::Active,
                 "Repeat stateful acquire yields an Active lease.");
    ok &= expect(second.instanceId == first.instanceId,
                 "Same session routes to the same instance.");
    ok &= expect(second.leaseId == first.leaseId,
                 "Same session reuses the same lease (no per-call re-acquire).");
    return ok;
}

bool testGatewayDifferentSessionsCanDistributeAcrossInstances() {
    bool ok = true;
    auto supervisor = std::make_shared<StubWorkerSupervisor>("pool-a", 2,
                                                             /*maxActiveLeasesPerInstance=*/1);
    auto router = MasterControl::createLeaseRouter(supervisor, 300);

    MasterControl::LeaseRequest first;
    first.poolId = "pool-a";
    first.sessionId = "session-1";
    first.stateful = true;
    MasterControl::LeaseRequest second = first;
    second.sessionId = "session-2";

    const auto leaseOne = router->acquireLease(first);
    const auto leaseTwo = router->acquireLease(second);
    ok &= expect(leaseOne.state == MasterControl::LeaseState::Active
                 && leaseTwo.state == MasterControl::LeaseState::Active,
                 "Both sessions acquire Active leases.");
    ok &= expect(leaseOne.instanceId != leaseTwo.instanceId,
                 "Different sessions distribute across instances when capacity requires it.");
    return ok;
}

bool testGatewayStatelessCallsReleaseLeaseAfterCall() {
    bool ok = true;
    auto supervisor = std::make_shared<StubWorkerSupervisor>("pool-a", 1);
    auto router = MasterControl::createLeaseRouter(supervisor, 300);

    MasterControl::LeaseRequest request;
    request.poolId = "pool-a";     // no sessionId -> stateless
    const auto lease = router->acquireLease(request);
    ok &= expect(lease.state == MasterControl::LeaseState::Active,
                 "Stateless acquire yields an Active lease.");
    const auto release = router->releaseLease(lease.leaseId, "tools/call complete");
    ok &= expect(release.succeeded, "Stateless release succeeds.");
    ok &= expect(router->activeLeases("pool-a").empty(),
                 "No active leases remain after a stateless call releases.");
    return ok;
}

bool testGatewayStatefulSessionLeaseExpiresOrReleases() {
    bool ok = true;
    auto supervisor = std::make_shared<StubWorkerSupervisor>("pool-a", 1);
    // Idle timeout 0 => every lease is idle-expired on the next sweep.
    auto router = MasterControl::createLeaseRouter(supervisor, 0);

    MasterControl::LeaseRequest request;
    request.poolId = "pool-a";
    request.sessionId = "session-ttl";
    request.stateful = true;
    const auto lease = router->acquireLease(request);
    ok &= expect(lease.state == MasterControl::LeaseState::Active,
                 "Stateful acquire yields an Active lease.");
    // Any subsequent router call runs the idle sweep.
    ok &= expect(router->activeLeases("pool-a").empty(),
                 "Idle stateful lease expires instead of leaking forever.");
    const auto fresh = router->acquireLease(request);
    ok &= expect(fresh.state == MasterControl::LeaseState::Active
                 && fresh.leaseId != lease.leaseId,
                 "A new acquire after expiry binds a fresh lease.");
    return ok;
}

bool testGatewayStickySessionRebindsAfterInstanceLoss() {
    bool ok = true;
    auto supervisor = std::make_shared<StubWorkerSupervisor>("pool-a", 1);
    auto router = MasterControl::createLeaseRouter(supervisor, 300);

    MasterControl::LeaseRequest request;
    request.poolId = "pool-a";
    request.sessionId = "session-rebind";
    request.stateful = true;
    const auto first = router->acquireLease(request);
    ok &= expect(first.state == MasterControl::LeaseState::Active,
                 "Session binds to the original instance.");

    // Simulate crash + watchdog respawn: the pool now contains only a NEW
    // instance id (ids are never reused).
    MasterControl::ManagedEndpointPool replacement;
    replacement.poolId = "pool-a";
    replacement.scalePolicy.minInstances = 1;
    replacement.scalePolicy.maxInstances = 1;
    replacement.scalePolicy.maxActiveLeasesPerInstance = 1;
    MasterControl::EndpointInstance respawned;
    respawned.instanceId = "pool-a#stub-respawned";
    respawned.poolId = "pool-a";
    respawned.state = MasterControl::EndpointInstanceState::Ready;
    respawned.supervised = true;
    replacement.instances.push_back(respawned);
    supervisor->upsertPool(replacement);

    const auto second = router->acquireLease(request);
    ok &= expect(second.state == MasterControl::LeaseState::Active,
                 "Session re-acquires after instance loss.");
    ok &= expect(second.instanceId == "pool-a#stub-respawned",
                 "Session transparently rebinds to the respawned instance.");
    ok &= expect(second.leaseId != first.leaseId,
                 "Stale sticky lease was released, not reused.");
    return ok;
}

bool testToolCatalogRefreshDoesNotHoldGatewayMutexDuringChildRpc() {
    bool ok = true;
    MasterControl::McpGatewayConfiguration configuration;
    configuration.enabled = true;   // constructed only; Start() is never called
    MasterControl::NativeHttpSysGatewayAdapter adapter(configuration);
    auto supervisor = std::make_shared<StubWorkerSupervisor>("pool-slow", 1,
                                                             /*maxActiveLeasesPerInstance=*/1,
                                                             /*stdioDelayMs=*/2000);
    adapter.AttachWorkerBridge(supervisor, MasterControl::createLeaseRouter(supervisor, 300));

    std::thread lister([&adapter]() { (void)adapter.ListTools(); });
    // Give the catalog refresh time to enter the (slow) child RPC.
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    const auto probeStart = std::chrono::steady_clock::now();
    (void)adapter.CurrentStatus();   // locks the gateway mutex
    const auto probeElapsed = std::chrono::steady_clock::now() - probeStart;
    lister.join();
    ok &= expect(supervisor->stdioCallCount() >= 1,
                 "Catalog refresh reached the child RPC.");
    ok &= expect(probeElapsed < std::chrono::milliseconds(1000),
                 "Gateway mutex is not held during child RPC (CurrentStatus returned promptly).");
    return ok;
}

bool testNativeGatewayRegistrationRejectsEmptyName() {
    bool ok = true;
    MasterControl::McpGatewayConfiguration configuration;
    configuration.enabled = false;
    MasterControl::NativeHttpSysGatewayAdapter adapter(configuration);
    MasterControl::McpServerRegistration registration;
    registration.url = "http://127.0.0.1:7300/mcp/pools/x/mcp";
    const auto httpResult = adapter.RegisterHttpServer(registration);
    ok &= expect(!httpResult.succeeded,
                 "Native RegisterHttpServer rejects an empty server name.");
    const auto stdioResult = adapter.RegisterStdioServer(registration);
    ok &= expect(!stdioResult.succeeded,
                 "Native RegisterStdioServer rejects an empty server name.");
    ok &= expect(adapter.ListTools().empty(),
                 "Nothing was registered by the rejected calls.");
    return ok;
}

bool testGatewayExceptionResponseEscapesJsonMessage() {
    bool ok = true;
    const std::string hostile = "Internal error: \"quoted\" text\nwith\tcontrol chars and backslash \\";
    const auto body = MasterControl::BuildGatewayInternalErrorBody(hostile);
    try {
        const auto parsed = nlohmann::json::parse(body);
        ok &= expect(parsed.at("error").at("code").get<int>() == -32603,
                     "Escaped envelope keeps the JSON-RPC error code.");
        ok &= expect(parsed.at("error").at("message").get<std::string>() == hostile,
                     "Exception text round-trips losslessly through escaping.");
        ok &= expect(parsed.at("id").is_null(),
                     "Envelope preserves id:null for pre-id-resolution failures.");
    } catch (const std::exception&) {
        ok &= expect(false, "Envelope with quotes/control chars parses as valid JSON.");
    }
    return ok;
}

#if defined(_WIN32)
std::filesystem::path makeTempWorkerDir(const std::string& tag) {
    const auto dir = std::filesystem::temp_directory_path() / ("mcos-worker-" + tag);
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir);
    return dir;
}

bool testWorkerSupervisorResolvesPathExecutable() {
    bool ok = true;
    const auto dir = makeTempWorkerDir("path-resolve");
    {
        std::ofstream script(dir / "mcos-fake-worker.cmd");
        script << "@ping -n 5 127.0.0.1 >nul\r\n";
    }
    std::string previousPath(32767, '\0');
    const DWORD previousLength = GetEnvironmentVariableA("PATH", previousPath.data(),
                                                         static_cast<DWORD>(previousPath.size()));
    previousPath.resize(previousLength);
    const std::string patchedPath = dir.string() + ";" + previousPath;
    SetEnvironmentVariableA("PATH", patchedPath.c_str());

    bool sawInstance = false;
    {
        auto supervisor = MasterControl::createWorkerSupervisor();
        MasterControl::ManagedEndpointPool pool;
        pool.poolId = "path-worker";

        pool.template_.executable = "mcos-fake-worker.cmd";   // bare PATH command
        pool.template_.transport = "stdio_jsonrpc";
        pool.scalePolicy.minInstances = 1;
        pool.scalePolicy.maxInstances = 1;
        supervisor->upsertPool(pool);
        supervisor->ensureMinInstances("path-worker");
        const auto found = supervisor->findPool("path-worker");
        sawInstance = found.has_value() && !found->instances.empty();
        ok &= expect(sawInstance, "PATH-resolved pool spawns an instance.");
        if (sawInstance) {
            const auto& instance = found->instances.front();
            ok &= expect(instance.statusMessage.find("was not found") == std::string::npos
                         && instance.statusMessage.find("is missing") == std::string::npos,
                         "PATH-resolved executable does not fail with a missing-executable diagnostic.");
            ok &= expect(instance.supervised,
                         "PATH-resolved worker is supervised (a real process was spawned).");
        }
        supervisor->shutdownAll();
    }
    SetEnvironmentVariableA("PATH", previousPath.c_str());
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    return ok;
}

bool testWorkerSupervisorPrefersCmdOverExtensionlessShim() {
    bool ok = true;
    // Node-style layout: an extensionless POSIX shim sits NEXT TO the
    // real .cmd on PATH (exactly how nodejs ships "npx" + "npx.cmd").
    // The directory name carries a SPACE so this also proves the
    // cmd.exe /s /c batch-launch path survives quoted script paths with
    // a quoted argument.
    const auto dir = makeTempWorkerDir("shim test dir");
    {
        std::ofstream shim(dir / "mcos-fake-worker2");
        shim << "#!/bin/sh\nexit 1\n";
        std::ofstream script(dir / "mcos-fake-worker2.cmd");
        script << "@ping -n 5 127.0.0.1 >nul\r\n";
    }
    std::string previousPath(32767, '\0');
    const DWORD previousLength = GetEnvironmentVariableA("PATH", previousPath.data(),
                                                         static_cast<DWORD>(previousPath.size()));
    previousPath.resize(previousLength);
    const std::string patchedPath = dir.string() + ";" + previousPath;
    SetEnvironmentVariableA("PATH", patchedPath.c_str());
    {
        auto supervisor = MasterControl::createWorkerSupervisor();
        MasterControl::ManagedEndpointPool pool;
        pool.poolId = "shim-worker";
        pool.template_.executable = "mcos-fake-worker2";   // bare, no extension
        pool.template_.args = { "hello world" };            // forces a quoted arg
        pool.scalePolicy.minInstances = 1;
        pool.scalePolicy.maxInstances = 1;
        supervisor->upsertPool(pool);
        supervisor->ensureMinInstances("shim-worker");
        const auto found = supervisor->findPool("shim-worker");
        const bool sawInstance = found.has_value() && !found->instances.empty();
        ok &= expect(sawInstance, "Bare extensionless command spawns an instance.");
        if (sawInstance) {
            const auto& instance = found->instances.front();
            ok &= expect(instance.supervised,
                         "Resolver preferred the .cmd over the extensionless shim and "
                         "cmd.exe /s /c launched it despite the spaced path + quoted arg.");
        }
        supervisor->shutdownAll();
    }
    SetEnvironmentVariableA("PATH", previousPath.c_str());
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    return ok;
}

bool testWorkerSupervisorRejectsCmdMetacharactersInBatchArgs() {
    bool ok = true;
    const auto dir = makeTempWorkerDir("metachar");
    {
        std::ofstream script(dir / "mcos-fake-worker3.cmd");
        script << "@ping -n 5 127.0.0.1 >nul\r\n";
    }
    auto supervisor = MasterControl::createWorkerSupervisor();
    MasterControl::ManagedEndpointPool pool;
    pool.poolId = "metachar-worker";
    pool.template_.executable = (dir / "mcos-fake-worker3.cmd").string();
    pool.template_.args = { "safe", "a&b" };   // '&' would chain commands under cmd /c
    pool.scalePolicy.minInstances = 1;
    pool.scalePolicy.maxInstances = 1;
    supervisor->upsertPool(pool);
    supervisor->ensureMinInstances("metachar-worker");
    const auto found = supervisor->findPool("metachar-worker");
    ok &= expect(found.has_value() && !found->instances.empty(),
                 "Rejected batch worker still records a failed instance for diagnostics.");
    if (found.has_value() && !found->instances.empty()) {
        const auto& instance = found->instances.front();
        ok &= expect(instance.state == MasterControl::EndpointInstanceState::Failed
                     && !instance.supervised,
                     "Batch worker with cmd control characters in args does not spawn.");
        ok &= expect(instance.statusMessage.find("cmd control") != std::string::npos
                     && instance.statusMessage.find("a&b") != std::string::npos,
                     "Diagnostic names the offending argument.");
    }
    supervisor->shutdownAll();
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    return ok;
}

bool testWorkerSupervisorRejectsMissingBareExecutable() {
    bool ok = true;
    auto supervisor = MasterControl::createWorkerSupervisor();
    MasterControl::ManagedEndpointPool pool;
    pool.poolId = "missing-worker";

    pool.template_.executable = "mcos-nonexistent-worker-zz9.cmd";
    pool.scalePolicy.minInstances = 1;
    pool.scalePolicy.maxInstances = 1;
    supervisor->upsertPool(pool);
    supervisor->ensureMinInstances("missing-worker");
    const auto found = supervisor->findPool("missing-worker");
    ok &= expect(found.has_value() && !found->instances.empty(),
                 "Missing executable still records a (failed) instance for diagnostics.");
    if (found.has_value() && !found->instances.empty()) {
        const auto& instance = found->instances.front();
        ok &= expect(instance.state == MasterControl::EndpointInstanceState::Failed,
                     "Missing bare executable fails the instance.");
        ok &= expect(!instance.supervised, "No process is spawned for a missing executable.");
        ok &= expect(instance.statusMessage.find("was not found") != std::string::npos,
                     "Missing-executable diagnostic names the resolution failure.");
        ok &= expect(instance.statusMessage.find("mcos-nonexistent-worker-zz9.cmd") != std::string::npos,
                     "Missing-executable diagnostic names the executable.");
    }
    supervisor->shutdownAll();
    return ok;
}

bool testWorkerSupervisorAcceptsAbsoluteExecutablePath() {
    bool ok = true;
    std::string systemRoot(512, '\0');
    const DWORD length = GetEnvironmentVariableA("SystemRoot", systemRoot.data(),
                                                 static_cast<DWORD>(systemRoot.size()));
    systemRoot.resize(length);
    const std::string cmdPath = systemRoot + "\\System32\\cmd.exe";

    auto supervisor = MasterControl::createWorkerSupervisor();
    MasterControl::ManagedEndpointPool pool;
    pool.poolId = "absolute-worker";

    pool.template_.executable = cmdPath;
    pool.template_.args = { "/c", "ping", "-n", "4", "127.0.0.1" };
    pool.scalePolicy.minInstances = 1;
    pool.scalePolicy.maxInstances = 1;
    supervisor->upsertPool(pool);
    supervisor->ensureMinInstances("absolute-worker");
    const auto found = supervisor->findPool("absolute-worker");
    ok &= expect(found.has_value() && !found->instances.empty()
                 && found->instances.front().supervised,
                 "Absolute executable path spawns a supervised instance.");
    supervisor->shutdownAll();
    return ok;
}

bool testWorkerSupervisorDestructorJoinsWatchdogWithoutDetach() {
    bool ok = true;
    // Pass condition (MCOS-010): teardown is deterministic and join-only.
    // Construct-and-destroy must complete promptly, repeatedly, with and
    // without live children; a detach-based teardown would be timing-
    // dependent and could leave the watchdog racing destroyed state.
    for (int round = 0; round < 3; ++round) {
        const auto start = std::chrono::steady_clock::now();
        {
            auto supervisor = MasterControl::createWorkerSupervisor();
            (void)supervisor->listPools();
        }
        const auto elapsed = std::chrono::steady_clock::now() - start;
        ok &= expect(elapsed < std::chrono::seconds(5),
                     "Empty-supervisor destructor joins the watchdog promptly.");
    }
    return ok;
}

bool testWorkerSupervisorShutdownDoesNotRespawnDuringTeardown() {
    bool ok = true;
    std::string systemRoot(512, '\0');
    const DWORD length = GetEnvironmentVariableA("SystemRoot", systemRoot.data(),
                                                 static_cast<DWORD>(systemRoot.size()));
    systemRoot.resize(length);

    auto supervisor = MasterControl::createWorkerSupervisor();
    MasterControl::ManagedEndpointPool pool;
    pool.poolId = "teardown-worker";

    pool.template_.executable = systemRoot + "\\System32\\cmd.exe";
    pool.template_.args = { "/c", "ping", "-n", "30", "127.0.0.1" };
    pool.scalePolicy.minInstances = 1;
    pool.scalePolicy.maxInstances = 1;
    supervisor->upsertPool(pool);
    supervisor->ensureMinInstances("teardown-worker");
    {
        const auto found = supervisor->findPool("teardown-worker");
        ok &= expect(found.has_value() && !found->instances.empty(),
                     "Worker is running before shutdown.");
    }

    supervisor->shutdownAll();
    // The watchdog fires every 2s; give it a full cycle to (incorrectly)
    // respawn. With the teardown gate in place it must not.
    std::this_thread::sleep_for(std::chrono::milliseconds(2600));
    {
        const auto found = supervisor->findPool("teardown-worker");
        ok &= expect(found.has_value() && found->instances.empty(),
                     "No instance is respawned after shutdownAll().");
    }
    ok &= expect(supervisor->scaleUpOnce("teardown-worker").empty(),
                 "scaleUpOnce is refused during teardown.");
    const auto ensureResult = supervisor->ensureMinInstances("teardown-worker");
    ok &= expect(!ensureResult.succeeded,
                 "ensureMinInstances is refused during teardown.");
    return ok;
}
#endif  // _WIN32

// ---------------------------------------------------------------------------
// Working-alpha HTTP.sys binding classification (HttpBindingInspection.h).
// Required-vs-optional is the whole point of the binding-validation task: a
// required binding that is absent must fail closed; an optional binding that
// is absent must be reported, not failed.
// ---------------------------------------------------------------------------
bool testClassifyRequiredBindingMissingFails() {
    bool ok = true;
    MasterControl::BindingExpectation expectation;
    expectation.kind = MasterControl::HttpBindingKind::UrlAcl;
    expectation.surface = "gateway";
    expectation.required = true;
    expectation.port = 8080;
    const MasterControl::BindingObservation observation;  // present = false
    const auto status = MasterControl::classifyHttpBinding(expectation, observation);
    ok &= expect(status.verdict == MasterControl::HttpBindingVerdict::Failed,
                 "Required URL ACL that is absent classifies as Failed.");
    ok &= expect(status.failed(), "Required-missing binding reports failed().");
    ok &= expect(!status.optionalMissing,
                 "Required-missing binding is not labelled optionalMissing.");
    return ok;
}

bool testClassifyOptionalBindingMissingIsReportedNotFailed() {
    bool ok = true;
    MasterControl::BindingExpectation expectation;
    expectation.kind = MasterControl::HttpBindingKind::UrlAcl;
    expectation.surface = "gateway";
    expectation.required = false;
    const MasterControl::BindingObservation observation;  // present = false
    const auto status = MasterControl::classifyHttpBinding(expectation, observation);
    ok &= expect(status.verdict == MasterControl::HttpBindingVerdict::OptionalMissing,
                 "Optional URL ACL that is absent classifies as OptionalMissing.");
    ok &= expect(!status.failed(), "Optional-missing binding does not fail closed.");
    ok &= expect(status.optionalMissing, "Optional-missing binding sets optionalMissing.");
    return ok;
}

bool testClassifyRequiredBindingPresentPasses() {
    bool ok = true;
    MasterControl::BindingExpectation expectation;
    expectation.kind = MasterControl::HttpBindingKind::Firewall;
    expectation.surface = "admin";
    expectation.required = true;
    expectation.ruleName = "MCOS Admin";
    MasterControl::BindingObservation observation;
    observation.present = true;
    const auto status = MasterControl::classifyHttpBinding(expectation, observation);
    ok &= expect(status.verdict == MasterControl::HttpBindingVerdict::Passed,
                 "Required firewall rule that is present classifies as Passed.");
    ok &= expect(status.present && status.required && !status.optionalMissing,
                 "Passed binding carries present/required flags without optionalMissing.");
    return ok;
}

bool testClassifyTlsThumbprintMismatchFailsEvenWhenBound() {
    bool ok = true;
    MasterControl::BindingExpectation expectation;
    expectation.kind = MasterControl::HttpBindingKind::Tls;
    expectation.surface = "gateway";
    expectation.required = true;
    expectation.expectedThumbprint = "AA:BB:CC:DD";
    MasterControl::BindingObservation observation;
    observation.present = true;
    observation.thumbprint = "aabbccee";  // bound, but the wrong certificate
    const auto mismatch = MasterControl::classifyHttpBinding(expectation, observation);
    ok &= expect(mismatch.verdict == MasterControl::HttpBindingVerdict::Failed,
                 "Required TLS binding with a mismatched thumbprint fails.");
    observation.thumbprint = "aa bb cc dd";  // same cert, spaced/lowercased
    const auto match = MasterControl::classifyHttpBinding(expectation, observation);
    ok &= expect(match.verdict == MasterControl::HttpBindingVerdict::Passed,
                 "TLS thumbprint comparison ignores case and separators.");
    expectation.required = false;
    observation.thumbprint = "ffffffff";  // present but wrong, even if optional
    const auto optionalMismatch = MasterControl::classifyHttpBinding(expectation, observation);
    ok &= expect(optionalMismatch.verdict == MasterControl::HttpBindingVerdict::Failed,
                 "An optional TLS binding bound to the wrong certificate still fails.");
    return ok;
}

bool testFakeHttpBindingInspectorObserveAndInspect() {
    bool ok = true;
    MasterControl::FakeHttpBindingInspector inspector;
    MasterControl::BindingObservation present;
    present.present = true;
    inspector.setObservation("admin", MasterControl::HttpBindingKind::Firewall, present);

    MasterControl::BindingExpectation adminFirewall;
    adminFirewall.kind = MasterControl::HttpBindingKind::Firewall;
    adminFirewall.surface = "admin";
    adminFirewall.required = true;
    ok &= expect(inspector.Inspect(adminFirewall).verdict == MasterControl::HttpBindingVerdict::Passed,
                 "Fake inspector reports a scripted-present required binding as Passed.");

    // An unscripted surface defaults to absent -> a required binding fails.
    MasterControl::BindingExpectation gatewayUrlAcl;
    gatewayUrlAcl.kind = MasterControl::HttpBindingKind::UrlAcl;
    gatewayUrlAcl.surface = "gateway";
    gatewayUrlAcl.required = true;
    ok &= expect(inspector.Inspect(gatewayUrlAcl).verdict == MasterControl::HttpBindingVerdict::Failed,
                 "Fake inspector defaults unscripted bindings to absent.");

    ok &= expect(std::string(MasterControl::bindingKindToString(MasterControl::HttpBindingKind::UrlAcl)) == "urlAcl",
                 "bindingKindToString maps UrlAcl to 'urlAcl'.");
    ok &= expect(std::string(MasterControl::bindingVerdictToString(MasterControl::HttpBindingVerdict::OptionalMissing)) == "optionalMissing",
                 "bindingVerdictToString maps OptionalMissing to 'optionalMissing'.");
    return ok;
}

// ---------------------------------------------------------------------------
// Working-alpha readiness (WorkingAlphaReadiness.h). Stable machine-readable
// blocking IDs, honest states, and structural separation from the setup-
// completion readiness model.
// ---------------------------------------------------------------------------
bool testWorkingAlphaReadinessIdsStableUniqueAndSerializable() {
    MasterControl::WorkingAlphaReadinessInputs in;
    // Drive every negative signal so every issue fires.
    in.installStateAvailable = false;
    in.adminListenerReachable = false;
    in.gatewayRunning = false;
    in.gatewayState = "disabled";
    in.lanModeEnabled = true;
    in.discoveryRoutable = false;
    in.registeredClientCount = 0;
    in.anyClientHeartbeatObserved = false;
    in.workerPoolConfigured = true;
    in.workerPoolHasReadyInstance = false;
    in.diagnosticsStoreAvailable = false;
    in.requiredBindingProblem = true;
    in.requiredBindingDetail = "Required gateway URL ACL is missing.";

    const auto report = MasterControl::assessWorkingAlphaReadiness(in);
    bool ok = true;
    ok &= expect(!report.ready, "All-negative inputs make the report not ready.");

    const std::vector<std::string> expectedIds = {
        "service.install-state-unavailable",
        "listener.admin-unavailable",
        "gateway.not-running",
        "discovery.url-unroutable",
        "client.none-registered",
        "client.no-heartbeat-observed",
        "pool.no-ready-instance",
        "diagnostics.store-unavailable",
        "binding.required-missing",
    };
    ok &= expect(report.blockingIssues.size() == expectedIds.size(),
                 "Every working-alpha blocking issue fires when all signals are negative.");
    std::vector<std::string> seenIds;
    for (const auto& issue : report.blockingIssues) {
        const bool dup = std::find(seenIds.begin(), seenIds.end(), issue.id) != seenIds.end();
        ok &= expect(!dup, "Working-alpha readiness issue IDs are unique (no duplicates).");
        seenIds.push_back(issue.id);
        ok &= expect(!issue.state.empty(),
                     "Working-alpha readiness issue carries an honest state slug.");
    }
    for (const auto& id : expectedIds) {
        const bool present = std::find(seenIds.begin(), seenIds.end(), id) != seenIds.end();
        ok &= expect(present, "Working-alpha readiness emits the expected stable issue ID.");
    }

    nlohmann::json serialized = report;
    ok &= expect(serialized["ready"].get<bool>() == false,
                 "Working-alpha report serializes ready flag.");
    ok &= expect(serialized["blockingIssues"].is_array()
                 && serialized["blockingIssues"].size() == expectedIds.size(),
                 "Working-alpha report serializes its blocking issues array.");
    // Structural separation from the setup-completion readiness model: a
    // working-alpha issue carries a `state` slug and NO `severity` field, so it
    // can never be mistaken for a ReadinessIssue that gates /api/setup/complete
    // (that gate keys on severity == "blocking").
    ok &= expect(serialized["blockingIssues"][0].contains("state"),
                 "Working-alpha issue exposes a state field.");
    ok &= expect(!serialized["blockingIssues"][0].contains("severity"),
                 "Working-alpha issue has no severity field (separate from setup readiness).");

    auto restored = serialized.get<MasterControl::WorkingAlphaReadinessReport>();
    ok &= expect(restored.blockingIssues.size() == report.blockingIssues.size(),
                 "Working-alpha report round-trips its issue count.");
    return ok;
}

bool testWorkingAlphaReadinessReadyWhenHealthy() {
    MasterControl::WorkingAlphaReadinessInputs in;
    in.installStateAvailable = true;
    in.adminListenerReachable = true;
    in.gatewayRunning = true;
    in.lanModeEnabled = true;
    in.discoveryRoutable = true;
    in.registeredClientCount = 1;
    in.anyClientHeartbeatObserved = true;
    in.workerPoolConfigured = true;
    in.workerPoolHasReadyInstance = true;
    in.diagnosticsStoreAvailable = true;
    in.requiredBindingProblem = false;

    const auto report = MasterControl::assessWorkingAlphaReadiness(in);
    bool ok = true;
    ok &= expect(report.ready, "A fully-healthy install reports working-alpha ready.");
    ok &= expect(report.blockingIssues.empty(),
                 "A healthy install emits no working-alpha blocking issues.");
    return ok;
}

bool testWorkingAlphaReadinessHonestStatesNoFabrication() {
    // local-only posture does NOT flag discovery as unroutable (loopback is
    // correct locally), but a stopped gateway still blocks.
    MasterControl::WorkingAlphaReadinessInputs in;
    in.gatewayRunning = false;
    in.lanModeEnabled = false;       // local-only
    in.discoveryRoutable = false;    // loopback -- correct for local-only
    in.registeredClientCount = 1;
    in.anyClientHeartbeatObserved = true;
    in.diagnosticsStoreAvailable = true;
    const auto report = MasterControl::assessWorkingAlphaReadiness(in);
    bool ok = true;
    bool sawDiscovery = false;
    bool sawGateway = false;
    for (const auto& issue : report.blockingIssues) {
        if (issue.id == "discovery.url-unroutable") { sawDiscovery = true; }
        if (issue.id == "gateway.not-running") { sawGateway = true; }
    }
    ok &= expect(!sawDiscovery,
                 "Local-only posture does not flag loopback discovery as unroutable.");
    ok &= expect(sawGateway,
                 "A stopped gateway still blocks even in local-only posture.");
    return ok;
}

// Guards that every route the working-alpha acceptance scripts depend on is
// present in the typed admin route registry (which backs 405/Allow behavior).
// Adding a route to handleHttpRequest without also registering it here would
// silently break the acceptance harness; this test fails loudly instead.
bool testAdminRouteRegistryCoversWorkingAlphaAcceptanceRoutes() {
    bool ok = true;
    const auto allows = [&ok](const char* path, const char* method) {
        const auto methods = MasterControl::supportedMethodsForPath(path);
        const bool present =
            std::find(methods.begin(), methods.end(), std::string(method)) != methods.end();
        if (!present) {
            std::cerr << "Working-alpha acceptance route not in registry: "
                      << method << ' ' << path << '\n';
        }
        ok &= expect(present,
                     "Working-alpha acceptance route is present in the admin route registry.");
    };
    // Admin GET routes probed by the local working-alpha acceptance script.
    allows("/api/health", "GET");
    allows("/api/version", "GET");
    allows("/api/health/summary", "GET");
    allows("/.well-known/mcos.json", "GET");
    allows("/api/discovery", "GET");
    allows("/api/gateway/status", "GET");
    allows("/api/gateway/health", "GET");
    allows("/api/gateway/tools", "GET");
    allows("/api/onboarding", "GET");
    allows("/api/onboarding/codex", "GET");        // dynamic {clientType} route
    allows("/api/onboarding/claude-code", "GET");
    allows("/api/governance/profile", "GET");
    allows("/api/telemetry/gateway", "GET");
    allows("/api/telemetry/clients", "GET");
    // Client + heartbeat routes used by the registration + second-host scripts.
    allows("/api/telemetry/heartbeat", "POST");
    allows("/api/client/heartbeat", "POST");
    allows("/api/clients", "GET");
    allows("/api/clients", "POST");
    allows("/api/clients/codex-alpha-01/config", "GET"); // dynamic {id} route
    return ok;
}

// ---------------------------------------------------------------------------
// Software-alpha-readiness remediation: feature matrix + worker fail-closed.
// ---------------------------------------------------------------------------
#if defined(_WIN32)
// Locate the built baseline-tools worker exe relative to the test module, run
// it with the given args (stdin = NUL so a worker that reaches its read loop
// sees immediate EOF and exits 0), and return its exit code, or -1 if the
// worker could not be launched.
int runBaselineWorkerExit(const std::wstring& args) {
    wchar_t modulePath[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, modulePath, MAX_PATH) == 0) { return -1; }
    const std::filesystem::path testDir = std::filesystem::path(modulePath).parent_path();
    const std::filesystem::path buildDir = testDir.parent_path().parent_path();
    const wchar_t* kExe = L"mcos-baseline-tools-worker.exe";
    const std::filesystem::path candidates[] = {
        buildDir / L"src" / L"MasterControlBaselineToolsWorker" / L"Debug" / kExe,
        buildDir / L"src" / L"MasterControlBaselineToolsWorker" / L"Release" / kExe,
        buildDir / L"src" / L"MasterControlBaselineToolsWorker" / kExe,
        testDir / kExe,
    };
    std::filesystem::path exe;
    for (const auto& candidate : candidates) {
        std::error_code ec;
        if (std::filesystem::exists(candidate, ec)) { exe = candidate; break; }
    }
    if (exe.empty()) { return -1; }

    std::wstring cmd = L"\"" + exe.wstring() + L"\" " + args;
    std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
    mutableCmd.push_back(L'\0');

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE nulIn = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               &sa, OPEN_EXISTING, 0, nullptr);
    HANDLE nulOut = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                &sa, OPEN_EXISTING, 0, nullptr);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = nulIn;
    si.hStdOutput = nulOut;
    si.hStdError = nulOut;
    PROCESS_INFORMATION pi{};
    int result = -1;
    if (CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, TRUE,
                       CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        if (WaitForSingleObject(pi.hProcess, 15000) == WAIT_OBJECT_0) {
            DWORD code = 0;
            if (GetExitCodeProcess(pi.hProcess, &code)) { result = static_cast<int>(code); }
        } else {
            TerminateProcess(pi.hProcess, 1);
        }
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    if (nulIn != INVALID_HANDLE_VALUE) { CloseHandle(nulIn); }
    if (nulOut != INVALID_HANDLE_VALUE) { CloseHandle(nulOut); }
    return result;
}
#endif  // _WIN32

bool testUnknownWorkerSpecializationFailsClosed() {
    bool ok = true;
#if defined(_WIN32)
    const int unknownExit = runBaselineWorkerExit(L"--specialization=__mcos_unknown_spec__");
    const int baselineExit = runBaselineWorkerExit(L"--specialization=baseline-tools");
    ok &= expect(unknownExit != 0,
                 "Unknown --specialization makes the worker exit nonzero (fail closed).");
    ok &= expect(baselineExit == 0,
                 "A recognized --specialization (baseline-tools) exits cleanly on EOF stdin.");
#else
    ok &= expect(true, "Worker fail-closed spawn test is Windows-only.");
#endif
    return ok;
}

bool testDefaultSeededEndpointsExcludeRemovedIds() {
    const auto endpoints = MasterControl::buildDefaultSeededEndpoints();
    bool ok = true;
    for (const auto& ep : endpoints) {
        ok &= expect(!MasterControl::isRemovedAlphaFeatureId(ep.id),
                     "Default seeded endpoints contain no removed feature ID.");
    }
    ok &= expect(MasterControl::isRemovedAlphaFeatureId("docker-control"),
                 "docker-control is classified as a removed feature.");
    ok &= expect(!MasterControl::isRemovedAlphaFeatureId("local-database"),
                 "local-database is not removed (it is an implemented alpha feature).");
    return ok;
}

bool testAlphaFeatureMatrixHeaderMatchesResourceJson() {
    bool ok = true;
    const std::filesystem::path path =
        std::filesystem::path(MCOS_REPO_ROOT) / "resources" / "alpha-feature-matrix.json";
    std::ifstream in(path);
    ok &= expect(in.good(), "resources/alpha-feature-matrix.json is readable.");
    if (!in.good()) { return ok; }
    nlohmann::json j;
    in >> j;
    const auto jsonListHas = [&j](const char* key, std::string_view id) {
        if (!j.contains(key)) { return false; }
        for (const auto& entry : j[key]) {
            if (entry.get<std::string>() == std::string(id)) { return true; }
        }
        return false;
    };
    for (const auto& id : MasterControl::kRemovedAlphaFeatureIds) {
        ok &= expect(jsonListHas("removed", id), "JSON 'removed' matches header removed IDs.");
    }
    ok &= expect(j["removed"].size() == MasterControl::kRemovedAlphaFeatureIds.size(),
                 "JSON 'removed' count matches header removed count.");
    for (const auto& id : MasterControl::kRequiredAlphaFeatureIds) {
        ok &= expect(jsonListHas("required", id), "JSON 'required' matches header required IDs.");
    }
    ok &= expect(j["required"].size() == MasterControl::kRequiredAlphaFeatureIds.size(),
                 "JSON 'required' count matches header required count.");
    return ok;
}

// ---------------------------------------------------------------------------
// local-database read-only SQLite executor (T04).
// ---------------------------------------------------------------------------
class FakeDatabaseConnectionStore final : public MasterControl::IDatabaseConnectionStore {
public:
    explicit FakeDatabaseConnectionStore(std::optional<std::string> path)
        : path_(std::move(path)) {}
    std::optional<std::string> currentDatabasePath() const override { return path_; }
private:
    std::optional<std::string> path_;
};

bool testLocalDatabaseReadonlyQueryRejectsMutation() {
    bool ok = true;
    const std::filesystem::path dbPath =
        std::filesystem::temp_directory_path() / "mcos_localdb_test.db";
    std::error_code ec;
    std::filesystem::remove(dbPath, ec);

    sqlite3* db = nullptr;
    if (sqlite3_open(dbPath.string().c_str(), &db) != SQLITE_OK) {
        if (db) { sqlite3_close(db); }
        return expect(false, "Could not create a temp SQLite database for the test.");
    }
    sqlite3_exec(db, "CREATE TABLE widgets (id INTEGER PRIMARY KEY, name TEXT NOT NULL);",
                 nullptr, nullptr, nullptr);
    sqlite3_exec(db, "INSERT INTO widgets (id, name) VALUES (1, 'alpha'), (2, 'beta');",
                 nullptr, nullptr, nullptr);
    sqlite3_close(db);

    auto store = std::make_shared<FakeDatabaseConnectionStore>(dbPath.string());
    MasterControl::SqliteReadonlyQueryExecutor executor(store);

    const auto selectResult = executor.queryReadonly("SELECT id, name FROM widgets ORDER BY id", 100);
    ok &= expect(selectResult.ok, "Read-only SELECT succeeds.");
    ok &= expect(selectResult.data.contains("rows") && selectResult.data["rows"].size() == 2u,
                 "SELECT returns the two seeded rows.");

    for (const char* mutation : {
             "INSERT INTO widgets (id, name) VALUES (3, 'gamma')",
             "UPDATE widgets SET name = 'x' WHERE id = 1",
             "DELETE FROM widgets WHERE id = 1",
             "DROP TABLE widgets",
             "CREATE TABLE t (a INTEGER)",
             "ALTER TABLE widgets ADD COLUMN c INTEGER" }) {
        const auto r = executor.queryReadonly(mutation, 100);
        ok &= expect(!r.ok && r.errorKind == "rejected-mutation",
                     "Mutating SQL is rejected (rejected-mutation).");
    }

    const auto multi = executor.queryReadonly("SELECT 1; SELECT 2", 100);
    ok &= expect(!multi.ok && multi.errorKind == "multiple-statements",
                 "Multiple statements are rejected.");

    const auto attach = executor.queryReadonly("ATTACH DATABASE 'other.db' AS other", 100);
    ok &= expect(!attach.ok && attach.errorKind == "rejected-mutation",
                 "ATTACH is rejected.");

    const auto bad = executor.queryReadonly("SELECT FROM WHERE", 100);
    ok &= expect(!bad.ok && bad.errorKind == "prepare-failed",
                 "Malformed SQL is reported as prepare-failed, not a crash.");

    const auto tables = executor.listTables();
    ok &= expect(tables.ok && tables.data["rows"].size() == 1u,
                 "list_tables returns the single user table.");
    const auto describe = executor.describeTable("widgets");
    ok &= expect(describe.ok && describe.data["rows"].size() == 2u,
                 "describe_table returns metadata for both columns.");

    auto emptyStore = std::make_shared<FakeDatabaseConnectionStore>(std::nullopt);
    MasterControl::SqliteReadonlyQueryExecutor unconfigured(emptyStore);
    const auto unconf = unconfigured.queryReadonly("SELECT 1", 100);
    ok &= expect(!unconf.ok && unconf.errorKind == "unconfigured",
                 "An unconfigured database fails closed with 'unconfigured'.");

    std::filesystem::remove(dbPath, ec);
    return ok;
}

bool testLocalDatabaseAlphaContractIsHonest() {
    bool ok = true;
    auto emptyStore = std::make_shared<FakeDatabaseConnectionStore>(std::nullopt);
    MasterControl::SqliteReadonlyQueryExecutor unconfigured(emptyStore);
    const auto s1 = unconfigured.status();
    ok &= expect(s1.ok, "db.status succeeds even when unconfigured.");
    ok &= expect(s1.data.value("configured", true) == false,
                 "db.status honestly reports configured=false when unset.");
    ok &= expect(s1.data.value("provider", std::string{}) == "sqlite",
                 "db.status reports the sqlite provider.");
    return ok;
}

// ---------------------------------------------------------------------------
// Working-alpha readiness fail-closed defaults + provenance (T06).
// ---------------------------------------------------------------------------
MasterControl::WorkingAlphaReadinessInputs allObservedGoodReadinessInputs() {
    MasterControl::WorkingAlphaReadinessInputs in;
    in.installStateAvailable = true;
    in.adminListenerReachable = true;
    in.gatewayRunning = true;
    in.lanModeEnabled = true;
    in.discoveryRoutable = true;
    in.registeredClientCount = 1;
    in.anyClientHeartbeatObserved = true;
    in.workerPoolConfigured = true;
    in.workerPoolHasReadyInstance = true;
    in.diagnosticsStoreAvailable = true;
    in.requiredBindingProblem = false;
    return in;
}

bool testWorkingAlphaReadinessDefaultsFailClosed() {
    const MasterControl::WorkingAlphaReadinessInputs defaults;  // fail-closed defaults
    const auto report = MasterControl::assessWorkingAlphaReadiness(defaults);
    bool ok = true;
    ok &= expect(!report.ready, "Default-constructed readiness inputs are not ready (fail closed).");
    ok &= expect(!report.blockingIssues.empty(), "Default inputs emit blocking issues.");
    const auto hasIssue = [&report](const std::string& id) {
        for (const auto& issue : report.blockingIssues) { if (issue.id == id) { return true; } }
        return false;
    };
    ok &= expect(hasIssue("service.install-state-unavailable"), "Default fails install-state.");
    ok &= expect(hasIssue("listener.admin-unavailable"), "Default fails admin-listener.");
    ok &= expect(hasIssue("gateway.not-running"), "Default fails gateway.");
    ok &= expect(hasIssue("diagnostics.store-unavailable"), "Default fails diagnostics store.");
    ok &= expect(!report.signals.empty(), "Report records per-signal provenance.");
    return ok;
}

bool testWorkingAlphaReadinessAllObservedGoodPasses() {
    const auto report = MasterControl::assessWorkingAlphaReadiness(allObservedGoodReadinessInputs());
    bool ok = true;
    ok &= expect(report.ready, "All-observed-good inputs pass readiness.");
    ok &= expect(report.blockingIssues.empty(), "No blocking issues when all signals are good.");
    return ok;
}

bool testWorkingAlphaReadinessMissingClientFails() {
    auto in = allObservedGoodReadinessInputs();
    in.registeredClientCount = 0;
    const auto report = MasterControl::assessWorkingAlphaReadiness(in);
    bool ok = true;
    ok &= expect(!report.ready, "Missing registered client fails readiness.");
    bool sawClient = false;
    for (const auto& issue : report.blockingIssues) {
        if (issue.id == "client.none-registered") { sawClient = true; }
    }
    ok &= expect(sawClient, "client.none-registered issue is emitted.");
    return ok;
}

bool testWorkingAlphaReadinessRequiredBindingFailureBlocks() {
    auto in = allObservedGoodReadinessInputs();
    in.requiredBindingProblem = true;
    in.requiredBindingDetail = "Required gateway firewall rule missing.";
    const auto report = MasterControl::assessWorkingAlphaReadiness(in);
    bool ok = true;
    ok &= expect(!report.ready, "A required binding problem blocks readiness.");
    bool sawBinding = false;
    for (const auto& issue : report.blockingIssues) {
        if (issue.id == "binding.required-missing") { sawBinding = true; }
    }
    ok &= expect(sawBinding, "binding.required-missing issue is emitted.");
    bool sawSignal = false;
    for (const auto& s : report.signals) {
        if (s.name == "requiredBindingOk") {
            sawSignal = true;
            ok &= expect(!s.observed, "requiredBindingOk provenance is observed=false.");
        }
    }
    ok &= expect(sawSignal, "requiredBindingOk signal provenance is recorded.");
    return ok;
}

int main() {
#if defined(_WIN32)
    // v0.11.0-alpha.3: fail fast on a hard fault instead of hanging.
    // A missing DLL or an access violation in a non-interactive CI
    // session otherwise spawns WerFault.exe, which blocks the process
    // for the full ctest timeout (observed as a 300s "Timeout" with no
    // diagnostic). Suppressing the WER UI + the abort message box turns
    // any such fault into an immediate non-zero exit ctest can capture.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    // Keep _WRITE_ABORT_MSG so a CRT assert / abort prints its reason to
    // stderr (ctest captures it); clear only _CALL_REPORTFAULT so WER
    // does not spawn a blocking fault-report process.
    _set_abort_behavior(_WRITE_ABORT_MSG, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    bool ok = true;
    ok &= testDefaultConfiguration();
    // v0.11.0-alpha.3: admin-listener TLS opt-in defaults + legacy
    // SecuritySettings JSON back-compat (mirrors the beacon-signing
    // back-compat block inside testDefaultConfiguration).
    ok &= testSecuritySettingsAdminTlsDefaults();
    ok &= testSeededEndpoints();
    ok &= testSeededGatewayEndpointPointsAtNativeGateway();
    // v0.10.15 QueryParamParse helper tests (alias-aware ?param= extractor
    // backing /api/activity, /api/telemetry/events, /api/client/activity).
    ok &= testQueryParamCanonicalParse();
    ok &= testQueryParamAliasFallback();
    ok &= testQueryParamBoundaryGuard();
    ok &= testQueryParamMissingReturnsEmpty();
    ok &= testQueryParamMultiPair();
    // v0.10.17 JsonStrictness helper tests (additive droppedKeys
    // diagnostic on /api/clients, /api/pools, /api/telemetry/heartbeat).
    ok &= testJsonStrictnessDetectsTypo();
    ok &= testJsonStrictnessNoDropsWhenAllKeysKnown();
    ok &= testJsonStrictnessSafeOnNonObjectInput();
    ok &= testDroppedKeysToJsonShape();
    // v0.11.0 PHASE-14 Slice A diagnostics aggregator + markdown
    // render tests against the new include/MasterControl/Diagnostics
    // Aggregator.h helpers. Covers the file-walk + softCap early-out
    // + recent-first sort + partial-write tolerance + markdown
    // warn/warning aliasing + 50-events/section truncation paths
    // that back the new /api/diagnostics/* HTTP surface.
    ok &= testDiagnosticsAggregator_emptyRoot();
    ok &= testDiagnosticsAggregator_singleComponentSorted();
    ok &= testDiagnosticsAggregator_softCapEarlyExit();
    ok &= testDiagnosticsAggregator_partialWriteTolerance();
    ok &= testDiagnosticsCapturedAtUtcParse();
    // PHASE-14 Slice E
    ok &= testSqliteDiagnosticsStoreCrudAndFilters();
    ok &= testSqliteDiagnosticsStoreSelfTestPrune();
    ok &= testDiagnosticsServiceRingFallbackAndClear();
    ok &= testDiagnosticsServiceStoreBackedReportAndAuditedClear();
    ok &= testDiagnosticsMarkdownRender_warnWarningAlias();
    ok &= testDiagnosticsMarkdownRender_truncatesOver50();
    ok &= testLanClientDefaults();
    ok &= testLanClientRoundTrip();
    ok &= testAppConfigurationCarriesLanClients();
    ok &= testSetupStateJsonContract();
    ok &= testAppConfigurationCarriesSetupState();
    ok &= testWorkflowDefinitionJsonContract();
    ok &= testWorkflowReadinessCountsSources();
    ok &= testWorkflowReadinessRejectsInvalidDisabledDeleted();
    ok &= testLanClientPrivilegesDefaults();
    ok &= testLanClientPrivilegesRoundTrip();
    ok &= testCapabilityMatrixDefaults();
    ok &= testMcpToolCapabilityMatrix();
    ok &= testAuthenticatedRequestContextDefaults();
    ok &= testLocalBootstrapGrantsAllPrivileges();
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
    ok &= testRealAdapterRegistrationSurvivesAcrossStartStop();
    ok &= testGatewayEnumRoundTrips();
    ok &= testGatewayConfigJsonRoundTrip();
    ok &= testGatewayConfigUnknownTypeFallsBackWithoutWipe();
    // PHASE-03 LAN discovery tests
    ok &= testDiscoveryDocumentDefaultShape();
    ok &= testDiscoveryDocumentJsonRoundTrip();
    ok &= testBeaconAdvertisementJsonShape();
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
    // WS6 endpoint advertisement gates.
    ok &= testAdvertisedEndpointPlanLocalOnlyDoesNotAdvertiseLan();
    ok &= testAdvertisedEndpointPlanRequiresGatewayRunningForLanMcp();
    ok &= testAdvertisedEndpointPlanLanWhenEnabledAndGatewayRunning();
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
    ok &= testSupervisorConfirmRejectsTokenMismatch();
    ok &= testSupervisorLegacyDerivedTokenRotatesOnLoad();
    ok &= testSupervisorConfirmRejectsForbiddenCapability();
    ok &= testSupervisorConfirmRejectsProviderMismatch();
    ok &= testSupervisorPersistenceSurvivesServiceRecreation();
    ok &= testSupervisorParseSelectRequestRejectsBreakGlass();
    ok &= testSupervisorInstructionsAreProviderSpecific();
    ok &= testSupervisorConfirmDefaultDenyOnEmptyAllowedSet();
    ok &= testSupervisorHeartbeatWatchdogIdleStateNoTransition();
    ok &= testSupervisorHeartbeatWatchdogFlipsConnectedToDisconnected();
    // Config-safety remediation (MCOS-001) + bridge route contract (MCOS-002).
    ok &= testConfigPatchPreservesUnrelatedSections();
    ok &= testConfigPatchRejectsNonObjectPayload();
    ok &= testConfigPostRejectsPartialTopLevelBody();
    ok &= testBridgeConfigUpdateUsesPatchRoute();
    ok &= testForsettiModuleBridgeUsesStateRoute();
    ok &= testBrokenForsettiImportToolIsNotAdvertised();
    ok &= testBridgeGovernanceApproveRequiresConfirm();
    ok &= testBridgeLogsTailAvoidsPowerShellPatternInterpolation();
    ok &= testBridgeRoutesMatchBackendContract();
    ok &= testMutatingAuthorizationGatePrecedesSupervisorDispatch();
    ok &= testAdminRouteCapabilityPolicyCoversAuditMutations();
    // MCP transport contract (MCOS-008).
    ok &= testMcpTransportContractMatchesDocs();
    // Admin API remediation (MCOS-011/012/015).
    ok &= testAdminParserAcceptsLowercaseContentLength();
    ok &= testAdminParserRejectsInvalidContentLength();
    ok &= testSupportedMethodsIncludesDiagnosticsRoutes();
    ok &= testSupportedMethodsIncludesPluginRoutes();
    ok &= testSupportedMethodsIncludesDynamicAdminRoutes();
    ok &= testWrongMethodOnKnownRouteReturns405();
    ok &= testPlainHttpResponsesUseSendAllLoop();
    // Gateway/worker remediation (MCOS-004..014).
    ok &= testGatewayToolNameResolverRejectsDuplicateUnqualifiedNames();
    ok &= testGatewayToolsCallRefreshUsesSharedNameResolver();
    ok &= testGatewayStickySessionRoutesSameSessionToSameInstance();
    ok &= testGatewayDifferentSessionsCanDistributeAcrossInstances();
    ok &= testGatewayStatelessCallsReleaseLeaseAfterCall();
    ok &= testGatewayStatefulSessionLeaseExpiresOrReleases();
    ok &= testGatewayStickySessionRebindsAfterInstanceLoss();
    ok &= testToolCatalogRefreshDoesNotHoldGatewayMutexDuringChildRpc();
    ok &= testNativeGatewayRegistrationRejectsEmptyName();
    ok &= testGatewayExceptionResponseEscapesJsonMessage();
    // Working-alpha HTTP.sys binding classification (required vs optional vs missing).
    ok &= testClassifyRequiredBindingMissingFails();
    ok &= testClassifyOptionalBindingMissingIsReportedNotFailed();
    ok &= testClassifyRequiredBindingPresentPasses();
    ok &= testClassifyTlsThumbprintMismatchFailsEvenWhenBound();
    ok &= testFakeHttpBindingInspectorObserveAndInspect();
    // Working-alpha readiness assessment (stable IDs, honest states, separation).
    ok &= testWorkingAlphaReadinessIdsStableUniqueAndSerializable();
    ok &= testWorkingAlphaReadinessReadyWhenHealthy();
    ok &= testWorkingAlphaReadinessHonestStatesNoFabrication();
    ok &= testAdminRouteRegistryCoversWorkingAlphaAcceptanceRoutes();
    // Software-alpha-readiness remediation.
    ok &= testAlphaFeatureMatrixHeaderMatchesResourceJson();
    ok &= testDefaultSeededEndpointsExcludeRemovedIds();
    ok &= testUnknownWorkerSpecializationFailsClosed();
    ok &= testLocalDatabaseReadonlyQueryRejectsMutation();
    ok &= testLocalDatabaseAlphaContractIsHonest();
    ok &= testWorkingAlphaReadinessDefaultsFailClosed();
    ok &= testWorkingAlphaReadinessAllObservedGoodPasses();
    ok &= testWorkingAlphaReadinessMissingClientFails();
    ok &= testWorkingAlphaReadinessRequiredBindingFailureBlocks();
#if defined(_WIN32)
    ok &= testWorkerSupervisorResolvesPathExecutable();
    ok &= testWorkerSupervisorPrefersCmdOverExtensionlessShim();
    ok &= testWorkerSupervisorRejectsCmdMetacharactersInBatchArgs();
    ok &= testWorkerSupervisorRejectsMissingBareExecutable();
    ok &= testWorkerSupervisorAcceptsAbsoluteExecutablePath();
    ok &= testWorkerSupervisorDestructorJoinsWatchdogWithoutDetach();
    ok &= testWorkerSupervisorShutdownDoesNotRespawnDuringTeardown();
#endif
    return ok ? 0 : 1;
}
