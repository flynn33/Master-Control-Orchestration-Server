// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.
//
// SupervisorAssignmentService -- backs the /api/supervisor/* HTTP routes
// and the WinUI Shell's Supervisor Agent Assignment Wizard.
//
// Lifecycle:
//   1. Operator selects a provider (chatgpt | claude | grok) through
//      POST /api/supervisor/assignment/select. The service revokes any
//      previous active/pending assignment, mints a new assignment id +
//      config id, and synthesizes the model-specific JSON config that
//      conforms to schemas/supervisor-config.schema.json.
//   2. The Shell saves the generated config through a native FileSavePicker
//      and the assignment transitions to PendingConnection.
//   3. The remote supervisor client uses the saved config to call back
//      into POST /api/supervisor/connect/confirm. The service validates
//      provider/config-id/expiration/capability set; on success the
//      assignment transitions to Connected.
//   4. POST /api/supervisor/assignment/revoke (or selecting a different
//      provider) drops the connection and emits the revoke audit event.
//
// Persistence: a single JSON document at <dataDirectory>/supervisor-
// assignment.json holds the latest assignment record so a service
// restart preserves the supervisor identity (the operator's saved
// config file remains valid). The file is rewritten atomically on
// every mutation.
//
// LAN trust model: per .claude/rules/00-mcos-realignment.md, the AI-
// client surface is LAN-trust; we still issue revocable token
// references so MCOS can answer supervisor capability requests
// authoritatively, but no app-layer auth is required to *reach* the
// supervisor surface.

#pragma once

#include "MasterControl/SupervisorAssignment.h"

// nlohmann's json type is a template alias under an inline-namespace
// (json_abi_v3_12_0::basic_json<...>) that cannot be forward-declared
// as `class json`. Pull in the official forward header so this shim
// header doesn't shadow the alias and break translation units that
// also include the full nlohmann/json.hpp.
#include <nlohmann/json_fwd.hpp>

#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace MasterControl {

class ISupervisorAssignmentService {
public:
    virtual ~ISupervisorAssignmentService() = default;

    virtual SupervisorAssignment getCurrentAssignment() const = 0;

    // Select-supervisor + immediate config issuance. Atomic: any prior
    // assignment is revoked first, the new one is created and persisted,
    // then the config JSON is synthesized.
    virtual SupervisorIssueResult selectAndIssue(const SupervisorSelectRequest& request) = 0;

    // Generate (or re-emit) the config JSON for the current assignment.
    // Used when the operator wants to re-download the file without
    // revoking + re-selecting. Returns ok=false if no assignment exists.
    virtual SupervisorIssueResult regenerateConfig() = 0;

    virtual void revoke(const std::string& reason) = 0;

    virtual SupervisorConnectionResult confirmConnection(const SupervisorConnectionClaim& claim) = 0;

    virtual void recordHeartbeat(const std::string& clientId) = 0;

    virtual SupervisorStatus getStatus() const = 0;

    // v0.9.78: watchdog hook. Flips state Connected -> Disconnected when
    // lastHeartbeatUtc + threshold < now. Returns true if a transition
    // occurred so the caller can audit-log it. No-ops for any state other
    // than Connected, and when the assignment has no recorded heartbeat
    // (the supervisor never connected; the timeout doesn't apply).
    virtual bool expireConnectionIfStale(std::chrono::seconds threshold) = 0;

    // v0.10.8: late-binding endpoint refresh. The supervisor service is
    // constructed early in initialize() -- before the snapshot tick has
    // run and before the primary IPv4 LAN address is auto-detected --
    // so the initial mcpEndpoint / discoveryEndpoint in the context are
    // necessarily a fallback (typically 127.0.0.1 + browserPort). The
    // route layer calls this just before selectAndIssue / regenerateConfig
    // / confirmConnection with the freshly-resolved LAN-routable values
    // taken from the same logic the DiscoveryDocument uses, so the
    // generated config carries a URL a remote supervisor client can
    // actually reach (LAN IP + gateway port + /mcp instead of 127.0.0.1
    // + browser admin port). fingerprintSeed is also refreshed so the
    // server fingerprint reflects the now-known LAN identity.
    virtual void setEndpoints(const std::string& mcpEndpoint,
                              const std::string& discoveryEndpoint,
                              const std::string& fingerprintSeed) = 0;
};

// Construction parameters. dataDirectory is where the service persists
// the assignment JSON document. mcpEndpoint / discoveryEndpoint are
// LAN-routable URLs the gateway is currently advertising; the service
// stamps them into the generated config so the client knows where to
// connect. fingerprintSeed is mixed into the server-fingerprint hash so
// two MCOS hosts on the same LAN aren't indistinguishable.
struct SupervisorAssignmentServiceContext {
    std::filesystem::path dataDirectory;
    std::string mcpEndpoint;
    std::string discoveryEndpoint;
    std::string serverDisplayName;
    std::string fingerprintSeed;
    std::chrono::seconds defaultConfigTtl{ 3 * 60 * 60 };  // 3 hours
};

std::unique_ptr<ISupervisorAssignmentService> createSupervisorAssignmentService(
    SupervisorAssignmentServiceContext context);

// Free-function helpers used by the route layer. They serialize the
// supervisor model types to/from JSON shapes that match the schema
// files in the SupervisorAgent package.
nlohmann::json toJson(const SupervisorAssignment& assignment);
nlohmann::json toJson(const SupervisorStatus& status);
nlohmann::json toJson(const SupervisorIssueResult& result);
nlohmann::json toJson(const SupervisorConnectionResult& result);

// Parse helpers for inbound POST bodies. Each tries to populate the
// out-parameter from the json body and returns a non-empty error
// string on validation failure.
std::string parseSelectRequest(const nlohmann::json& body, SupervisorSelectRequest& out);
std::string parseConnectionClaim(const nlohmann::json& body, SupervisorConnectionClaim& out);

} // namespace MasterControl
