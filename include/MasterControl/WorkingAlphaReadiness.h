// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.
//
// Working-alpha readiness assessment. Answers "is this install usable as a
// working alpha right now?" as a pure function over signals the caller has
// already gathered from live runtime services. This mirrors the existing
// readiness pattern (computeReadinessSnapshot, validateWorkflowDefinition):
// the decision is a platform-agnostic, header-only pure function; the live
// observation happens in the caller, which holds the runtime collaborators.
//
// It is deliberately independent of ReadinessSnapshot/ReadinessIssue: those
// drive the first-run setup wizard's completion gate, whereas this report is
// surfaced read-only on /api/health/summary and must never influence setup
// completion.

#pragma once

#include "MasterControl/MasterControlModels.h"

#include <string>
#include <vector>

namespace MasterControl {

// Signals gathered from live runtime services by the caller. Kept as a plain
// aggregate so the decision stays pure and unit-testable without collaborators.
struct WorkingAlphaReadinessInputs final {
    bool installStateAvailable = true;      // install/service state is confirmable
    bool adminListenerReachable = true;     // admin HTTP listener is answering
    bool gatewayRunning = false;            // MCP gateway is in the Running state
    std::string gatewayState;               // honest gateway state slug for the detail
    bool lanModeEnabled = false;            // trusted-LAN posture is enabled
    bool discoveryRoutable = true;          // advertised gateway URL is LAN-routable in LAN mode
    int registeredClientCount = 0;          // number of registered LAN client identities
    bool anyClientHeartbeatObserved = false;// at least one client heartbeat has been seen
    bool workerPoolConfigured = false;      // >=1 pool with minInstances > 0
    bool workerPoolHasReadyInstance = true; // every required pool has a ready instance
    bool diagnosticsStoreAvailable = true;  // failure evidence can be persisted
    bool requiredBindingProblem = false;    // a required firewall/URL ACL/TLS binding is missing
    std::string requiredBindingDetail;      // detail for the binding issue, when known
};

// Pure decision. Produces stable, machine-readable blocking issue IDs. No
// fabricated success: each issue is emitted only when its input signal is
// honestly negative. Callers set report.evaluatedAtUtc after invoking (the
// timestamp source is intentionally left out of this pure function).
inline WorkingAlphaReadinessReport assessWorkingAlphaReadiness(
    const WorkingAlphaReadinessInputs& in) {
    WorkingAlphaReadinessReport report;
    const auto add = [&report](const char* id, const char* category,
                               std::string state, std::string detail) {
        report.blockingIssues.push_back(
            WorkingAlphaReadinessIssue{ id, category, std::move(state), std::move(detail) });
    };

    if (!in.installStateAvailable) {
        add("service.install-state-unavailable", "service", "unavailable",
            "Installation/service state is not available; the running product cannot be confirmed.");
    }
    if (!in.adminListenerReachable) {
        add("listener.admin-unavailable", "listener", "unavailable",
            "The admin HTTP listener is not answering.");
    }
    if (!in.gatewayRunning) {
        add("gateway.not-running", "gateway",
            in.gatewayState.empty() ? std::string("disabled") : in.gatewayState,
            "The MCP gateway is not running; clients cannot complete initialize/ping/tools-list.");
    }
    if (in.lanModeEnabled && !in.discoveryRoutable) {
        add("discovery.url-unroutable", "discovery", "unroutable",
            "LAN mode is enabled but the advertised gateway URL is loopback/wildcard and not LAN-routable.");
    }
    if (in.registeredClientCount <= 0) {
        add("client.none-registered", "client", "notObserved",
            "No LAN client identities are registered.");
    }
    if (!in.anyClientHeartbeatObserved) {
        add("client.no-heartbeat-observed", "client", "notObserved",
            "No LAN client heartbeat has been observed.");
    }
    if (in.workerPoolConfigured && !in.workerPoolHasReadyInstance) {
        add("pool.no-ready-instance", "pool", "notObserved",
            "A worker pool is configured with minInstances>0 but has no ready instance.");
    }
    if (!in.diagnosticsStoreAvailable) {
        add("diagnostics.store-unavailable", "diagnostics", "unavailable",
            "The diagnostics store is unavailable; failure evidence cannot be persisted.");
    }
    if (in.requiredBindingProblem) {
        add("binding.required-missing", "binding", "error",
            in.requiredBindingDetail.empty()
                ? std::string("A required firewall/URL ACL/TLS binding is missing.")
                : in.requiredBindingDetail);
    }

    report.ready = report.blockingIssues.empty();
    report.summary = report.ready
        ? std::string("All working-alpha readiness checks passed.")
        : (std::to_string(report.blockingIssues.size())
           + " working-alpha readiness issue(s) block acceptance.");
    return report;
}

} // namespace MasterControl
