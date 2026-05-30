// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.
//
// LAN client identity model. Per ADR-001, AI clients run on their own LAN
// machines and connect to MCOS as governed users of the shared MCP and
// sub-agent fabric. A LanClient is the server-side record of one such
// connection. Identity is by `clientId` alone; no tokens or shared secrets
// are exchanged on a trusted LAN. Phase 4 fills in the LanClientPrivileges
// boolean field set; Phase 5 issues the configuration bundle that an AI
// client downloads to learn its identity, server URL, and operating rules.

#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace MasterControl {

// LanClientPrivileges is the per-client allow-list for privileged mutations
// on shared resources. Use is never gated by these flags - any authenticated
// LAN client can read every MCP server and sub-agent in the catalog. Only
// creation, modification, and removal are privilege-gated. Defaults are
// all-false so newly registered clients are read-only until an operator
// explicitly grants capability.
//
// Autonomous mode (LanClient::autonomousMode) is a separate axis: when true,
// a client may create MCP servers and sub-agents without holding the
// matching `canCreate*` flag. It does not widen modify/remove or
// administrative privileges. See ADR-001 for the locked semantics.
struct LanClientPrivileges final {
    bool canCreateMcpServers = false;
    bool canModifyMcpServers = false;
    bool canRemoveMcpServers = false;
    bool canCreateSubAgents = false;
    bool canModifySubAgents = false;
    bool canRemoveSubAgents = false;
    bool canManageClients = false;          // register and modify other LAN clients
    bool canManageModules = false;          // enable / disable Forsetti modules
    bool canChangeGovernancePolicy = false; // edit CLU governance profile
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    LanClientPrivileges,
    canCreateMcpServers,
    canModifyMcpServers,
    canRemoveMcpServers,
    canCreateSubAgents,
    canModifySubAgents,
    canRemoveSubAgents,
    canManageClients,
    canManageModules,
    canChangeGovernancePolicy)

struct LanClient final {
    std::string clientId;          // operator-authored slug, the only identity token
    std::string displayName;
    std::string clientType;        // free-form: "claude_code", "codex", "grok", any future AI client
    std::string hostName;          // informational, last self-reported
    std::string networkAddress;    // informational, last observed
    bool enabled = true;
    LanClientPrivileges privileges{};
    std::vector<std::string> capabilities; // explicit high-risk capability allow-list
    bool autonomousMode = false;   // Phase 7 ties this into CLU bypass for create actions
    std::string createdAtUtc;
    std::string lastSeenUtc;
    std::string disabledAtUtc;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    LanClient,
    clientId,
    displayName,
    clientType,
    hostName,
    networkAddress,
    enabled,
    privileges,
    capabilities,
    autonomousMode,
    createdAtUtc,
    lastSeenUtc,
    disabledAtUtc)

} // namespace MasterControl
