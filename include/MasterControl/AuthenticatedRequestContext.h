// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.
//
// Per-request context resolved by the admin route layer. Missing or unknown
// remote identities never become operator. Local loopback requests may enter
// an explicit setup/bootstrap operator context so the shell and browser can
// finish first-run setup without exposing that privilege to the LAN.

#pragma once

#include "MasterControl/CapabilityAuthorization.h"
#include "MasterControl/LanClient.h"

#include <optional>
#include <string>
#include <utility>

namespace MasterControl {

struct AuthenticatedRequestContext final {
    // Populated when X-MCOS-Client-Id resolves to a registered client.
    std::optional<LanClient> client;

    // The privilege set carried by this request.
    LanClientPrivileges privileges{};
    std::vector<std::string> capabilities;

    // Mirrors the resolved client's autonomousMode. Local bootstrap can
    // opt into the same operator-only bypass explicitly.
    bool autonomousMode = false;

    // The resolved client's id, "local-operator", or "anonymous".
    std::string actor = "anonymous";

    std::string remoteAddress;
    bool isLocalRequest = false;
    bool isLocalBootstrap = false;
};

inline AuthenticatedRequestContext makeLocalOperatorContext(std::string remoteAddress = {}) {
    AuthenticatedRequestContext context;
    context.privileges.canCreateMcpServers = true;
    context.privileges.canModifyMcpServers = true;
    context.privileges.canRemoveMcpServers = true;
    context.privileges.canCreateSubAgents = true;
    context.privileges.canModifySubAgents = true;
    context.privileges.canRemoveSubAgents = true;
    context.privileges.canManageClients = true;
    context.privileges.canManageModules = true;
    context.privileges.canChangeGovernancePolicy = true;
    context.capabilities = allOperatorCapabilities();
    context.autonomousMode = true;
    context.actor = "local-operator";
    context.remoteAddress = std::move(remoteAddress);
    context.isLocalRequest = true;
    context.isLocalBootstrap = true;
    return context;
}

} // namespace MasterControl
