// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.
//
// Per-request context resolved by Phase 6 middleware. The struct's name is
// kept for semantic continuity with multi-tenant orchestration platforms,
// but per ADR-001 there is no authentication on a trusted LAN: identity is
// just the X-MCOS-Client-Id header value, looked up against the LanClient
// registry. Missing or unknown headers yield a synthetic operator context
// with all privileges (so the browser dashboard and ad-hoc curl keep
// working). A header naming a disabled client is rejected with 403 before
// the route ever runs.

#pragma once

#include "MasterControl/LanClient.h"

#include <optional>
#include <string>

namespace MasterControl {

struct AuthenticatedRequestContext final {
    // Empty when no header was supplied or the header named an unknown
    // client. Populated when the header resolved to a registered client.
    std::optional<LanClient> client;

    // The privilege set carried by this request. For real clients this is
    // the resolved client's privileges. For the synthetic operator context
    // every flag is true.
    LanClientPrivileges privileges{};

    // Mirrors the resolved client's autonomousMode, or true for the
    // operator context. Phase 7 expands the meaning; until then the only
    // gate that consults it is canCreate{Mcp,SubAgent}* bypass.
    bool autonomousMode = false;

    // The resolved client's id (lower-case-normalized) when present, else
    // the literal "operator". Used for activity-event attribution.
    std::string actor = "operator";

    // True when this request used the synthetic full-privilege fallback.
    // Useful for diagnostics; should never be allowed to route work that
    // assumed a real client identity.
    bool isOperatorFallback = true;
};

// Returns a context that grants every privilege; the missing-header /
// unknown-header fallback used by the Phase 6 middleware.
inline AuthenticatedRequestContext makeOperatorContext() {
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
    context.autonomousMode = true;
    context.actor = "operator";
    context.isOperatorFallback = true;
    return context;
}

} // namespace MasterControl
