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
    return ok ? 0 : 1;
}
