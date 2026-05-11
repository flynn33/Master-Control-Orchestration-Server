// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.
//
// Supervisor Agent Assignment Wizard data model. Implements the contracts
// described in the Supervisor Agent Assignment Wizard Implementation
// Package (docs/IMPLEMENTATION_INSTRUCTIONS.md, BACKEND_SPEC.md,
// CONFIG_GENERATION_SPEC.md, SECURITY_AND_CAPABILITY_MODEL.md).
//
// One supervisor at a time per MCOS host. The selected provider is one of
// ChatGPT, Claude, or Grok. MCOS generates a model-specific configuration
// JSON (schema mcos.supervisor.config.v1) and exposes the lifecycle
// through /api/supervisor/* HTTP routes. The MasterControlShell wizard
// drives selection + native save dialog + status display.
//
// Trust model: the AI-client surface is LAN-trust; supervisor tokens are
// short-lived bearer/token-reference values issued at config-generate
// time and validated server-side on connect/confirm. MCOS enforces
// capability allow/deny per mode -- unrestricted admin tools remain
// forbidden by default.

#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace MasterControl {

enum class SupervisorProvider {
    Unknown,
    ChatGpt,
    Claude,
    Grok
};

enum class SupervisorMode {
    Disabled,
    ReadOnlySupervisor,
    DecisionSupervisor,
    AutonomousSupervisor,
    BreakGlassAdmin
};

enum class SupervisorState {
    Off,
    ConfigGenerated,
    PendingConnection,
    Connected,
    Disconnected,
    Revoked,
    Error
};

// Wire-format string conversions (single source of truth so /api/* and
// the Shell agree on enum names exactly). Provider IDs match the
// lowercase values in supervisor-config.schema.json:
//     chatgpt | claude | grok
const char* providerIdString(SupervisorProvider provider);
SupervisorProvider providerFromString(const std::string& id);
const char* providerDisplayName(SupervisorProvider provider);

const char* supervisorModeString(SupervisorMode mode);
SupervisorMode supervisorModeFromString(const std::string& mode);

const char* supervisorStateString(SupervisorState state);
SupervisorState supervisorStateFromString(const std::string& state);

// Default capabilities for autonomous_supervisor mode (per
// SECURITY_AND_CAPABILITY_MODEL.md). The set is server-enforced; the
// generated config carries a copy so the supervisor client knows
// up-front what tools it will be authorized to invoke.
std::vector<std::string> defaultAutonomousSupervisorCapabilities();

// Capabilities forbidden for autonomous_supervisor mode. Used by the
// connection-confirm path to reject clients that request raw admin /
// shell tools; never exposed in the generated config.
std::vector<std::string> forbiddenAutonomousSupervisorCapabilities();

// One persisted assignment record. Conforms to
// schemas/supervisor-assignment.schema.json with a few internal-only
// fields (revocation reason, audit correlation) that the wire payloads
// also carry.
struct SupervisorAssignment {
    std::string assignmentId;
    SupervisorProvider provider = SupervisorProvider::Unknown;
    std::string clientId;
    SupervisorMode mode = SupervisorMode::AutonomousSupervisor;
    bool exclusive = true;
    SupervisorState state = SupervisorState::Off;
    std::string configId;
    std::string issuedAtUtc;       // ISO-8601 with trailing Z
    std::string expiresAtUtc;      // ISO-8601 with trailing Z
    std::string connectedAtUtc;    // empty until Connected
    std::string lastHeartbeatUtc;  // empty until first heartbeat
    std::string revokedAtUtc;      // empty unless state == Revoked
    std::string revocationReason;  // empty unless state == Revoked
    std::vector<std::string> allowedCapabilities;
    std::string auditCorrelationId;
    std::string lastErrorMessage;  // empty unless state == Error
    std::string tokenRef;          // mcos-supervisor-token:CFG-...
    std::string serverFingerprint; // sha256 hash of issuance host context
};

// Request payload for POST /api/supervisor/assignment/select.
struct SupervisorSelectRequest {
    SupervisorProvider provider = SupervisorProvider::Unknown;
    SupervisorMode mode = SupervisorMode::AutonomousSupervisor;
    bool exclusive = true;
};

// Response/result of a select-provider call. Bundles the new assignment
// state plus the freshly-issued config for the caller's convenience so
// the WinUI wizard does not have to immediately follow up with a
// /api/supervisor/config/generate round-trip.
struct SupervisorIssueResult {
    bool ok = false;
    std::string errorMessage;
    SupervisorAssignment assignment;
    std::string configJson;     // exact body that conforms to mcos.supervisor.config.v1
    std::string fileName;       // mcos-supervisor-{provider}.config.json
    std::string contentType;    // application/json
};

// Connection-claim payload for POST /api/supervisor/connect/confirm.
// Mirrors the client-side handshake: the supervisor tells MCOS what
// configId it has and which capabilities it intends to use. Server-
// side validation rejects mismatches and forbidden capability requests.
struct SupervisorConnectionClaim {
    std::string configId;
    SupervisorProvider provider = SupervisorProvider::Unknown;
    std::string clientId;
    std::vector<std::string> capabilities;
    std::string fingerprint;
    std::string token;            // optional: full bearer if not using ref-only flow
};

struct SupervisorConnectionResult {
    bool ok = false;
    SupervisorState newState = SupervisorState::Error;
    std::string errorMessage;
    std::string assignmentId;
    std::string clientId;
    std::string connectedAtUtc;
};

// Compact summary for /api/supervisor/status (UI status card payload).
struct SupervisorStatus {
    SupervisorProvider provider = SupervisorProvider::Unknown;
    std::string providerDisplayName;
    SupervisorMode mode = SupervisorMode::Disabled;
    SupervisorState state = SupervisorState::Off;
    std::string assignmentId;
    std::string configId;
    std::string clientId;
    std::string issuedAtUtc;
    std::string expiresAtUtc;
    std::string connectedAtUtc;
    std::string lastHeartbeatUtc;
    std::string lastErrorMessage;
    bool active = false;     // true iff state in {ConfigGenerated, PendingConnection, Connected}
};

} // namespace MasterControl
