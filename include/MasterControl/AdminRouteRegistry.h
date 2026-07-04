// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.
//
// AdminRouteRegistry.h - typed in-process route registry backing the
// admin API's method-allow handling (405 + Allow header).
//
// Route-drift remediation: this table previously lived as a static
// function inside MasterControlRuntime.cpp and had drifted from the
// implemented dispatch -- the PHASE-14 diagnostics routes and the
// v0.10.12 chatgpt/grok plugin routes were missing, so a wrong-verb
// request to them fell through to a 404 instead of a 405. Hoisting the
// registry into a header makes it directly testable, so the bool-style
// suite can pin the table to the implemented surface.
//
// Maintenance: when adding a new route to handleHttpRequest, add the
// (path, methods) pair here so verb-mismatch on the new route returns
// 405. The exact-match table covers literal path matches; the prefix
// table covers the routes that use startsWith()-style dispatch. HEAD
// and OPTIONS are added to every entry's Allow header automatically
// because the HEAD->GET rewrite and OPTIONS preflight short-circuit
// live in SimpleHttpServer::handleClient and apply universally.

#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace MasterControl {

// v0.9.28 origin: enumerate the supported methods for known operator API
// paths so an unsupported method on a known path returns 405 with an
// Allow header per RFC 7231 §6.5.5, instead of a 404 that hides whether
// the path exists at all. Returns an empty vector for unknown paths.
inline std::vector<std::string> supportedMethodsForPath(const std::string& path) {
    // Strip query string for the lookup -- /api/foo?bar=baz is the
    // same logical resource as /api/foo for method-allow purposes.
    std::string p = path;
    const auto q = p.find('?');
    if (q != std::string::npos) {
        p = p.substr(0, q);
    }

    static const std::unordered_map<std::string, std::vector<std::string>> kExact = {
        // Liveness / version
        {"/api/health", {"GET"}},
        {"/api/version", {"GET"}},
        {"/api/health/summary", {"GET"}},
        {"/api/host/telemetry", {"GET"}},
        {"/api/readiness", {"GET"}},
        {"/api/activity/health", {"GET"}},
        // Discovery / onboarding / governance
        {"/.well-known/mcos.json", {"GET"}},
        {"/api/discovery", {"GET"}},
        {"/api/onboarding", {"GET"}},
        {"/api/beacon", {"GET"}},
        {"/api/environment-hints", {"GET"}},
        {"/api/governance/profile", {"GET"}},
        {"/api/governance/bundles", {"GET"}},
        {"/api/governance/decisions", {"GET", "POST"}},
        // Snapshot / config
        {"/api/dashboard", {"GET"}},
        {"/api/config", {"GET", "POST", "PATCH"}},
        {"/api/exports", {"GET"}},
        {"/api/workflows", {"GET", "POST"}},
        {"/api/clu", {"GET"}},
        {"/api/clu/tools", {"GET"}},
        {"/api/clu/apple-operations", {"GET"}},
        {"/api/clu/apple-operations/cancel", {"POST"}},
        {"/api/clu/approvals", {"GET"}},
        {"/api/clu/execute", {"POST"}},
        {"/api/forsetti/surface", {"GET"}},
        {"/api/forsetti/modules", {"GET"}},
        {"/api/forsetti/modules/state", {"POST"}},
        // Pools
        {"/api/pools", {"GET", "POST"}},
        // Telemetry
        {"/api/telemetry/clients", {"GET"}},
        {"/api/telemetry/gateway", {"GET"}},
        {"/api/telemetry/heartbeat", {"POST"}},
        // Gateway control
        {"/api/gateway/status", {"GET"}},
        {"/api/gateway/health", {"GET"}},
        {"/api/gateway/tools", {"GET"}},
        {"/api/gateway/start", {"POST"}},
        {"/api/gateway/stop", {"POST"}},
        {"/api/gateway/restart", {"POST"}},
        // Client surfaces
        {"/api/client/mcp-servers", {"GET"}},
        {"/api/client/sub-agents", {"GET"}},
        {"/api/client/activity", {"GET"}},
        {"/api/client/governance/profile", {"GET"}},
        {"/api/client/governance/decisions", {"POST"}},
        {"/api/client/heartbeat", {"POST"}},
        {"/api/clients", {"GET", "POST"}},
        // Setup / install
        {"/api/setup/state", {"GET"}},
        {"/api/setup/start", {"POST"}},
        {"/api/setup/complete", {"POST"}},
        {"/api/setup/dismiss", {"POST"}},
        {"/api/setup/reset", {"POST"}},
        {"/api/setup/dependencies", {"GET"}},
        {"/api/setup/workflow-templates", {"GET"}},
        {"/api/install/history", {"GET"}},
        {"/api/install/package", {"POST"}},
        {"/api/install/repo", {"POST"}},
        {"/api/install/zip", {"POST"}},
        // Settings / runtime registration
        {"/api/settings/advanced-mode", {"POST"}},
        {"/api/runtime/mcp-servers", {"POST"}},
        {"/api/runtime/mcp-servers/remove", {"POST"}},
        {"/api/runtime/subagents", {"POST"}},
        {"/api/runtime/subagents/remove", {"POST"}},
        {"/api/runtime/subagent-groups", {"POST"}},
        {"/api/runtime/subagent-groups/remove", {"POST"}},
        // Platform services
        {"/api/platform-services", {"GET"}},
        {"/api/platform-services/gateways", {"GET"}},
        {"/api/platform-services/governance", {"GET"}},
        {"/api/platform-services/apple-hosts", {"GET", "POST"}},
        {"/api/platform-services/apple-hosts/remove", {"POST"}},
        // Plugin surfaces. The chatgpt/grok provider routes were missing
        // from the registry (route drift) even though dispatch implements
        // them; wrong verbs used to fall through to 404.
        {"/api/claude-plugin/status", {"GET"}},
        {"/api/claude-plugin/toggle", {"POST"}},
        {"/api/chatgpt-plugin/status", {"GET"}},
        {"/api/chatgpt-plugin/toggle", {"POST"}},
        {"/api/grok-plugin/status", {"GET"}},
        {"/api/grok-plugin/toggle", {"POST"}},
        // v0.9.56: operator-facing diagnostic surface. Returns the
        // same per-entry runtime stats as /api/dashboard but bundled
        // with an aggregate count by installState so the operator
        // can answer "how many of my catalog entries are actually
        // live, supervised, or just placeholders?" in one request.
        {"/api/diagnostics/runtime-stats", {"GET"}},
        // PHASE-14 diagnostics module. Previously missing from the
        // registry (route drift) even though dispatch implements all
        // five routes.
        {"/api/diagnostics/events", {"GET"}},
        {"/api/diagnostics/summary", {"GET"}},
        {"/api/diagnostics/self-test", {"GET"}},
        {"/api/diagnostics/export", {"GET"}},
        {"/api/diagnostics/clear", {"POST"}},
        // v0.9.69: boot-time self-test snapshot + re-run trigger.
        {"/api/self-tests",     {"GET"}},
        {"/api/self-tests/run", {"POST"}},
        // v0.9.71: real-time SSE push channel for dashboard +
        // activity events. Connection stays open until the client
        // disconnects or the server stops.
        {"/api/events",         {"GET"}},
        // v0.9.73: Forsetti Agentic Edition governance surface.
        // The manifest endpoint catalogs vendored documents +
        // policies + agents + contracts + schemas + standards by
        // path; the document endpoint serves them by relative path.
        {"/api/governance/agentic-edition/manifest", {"GET"}},
        // v0.9.76: Supervisor Agent Assignment Wizard surface.
        {"/api/supervisor/assignment",        {"GET"}},
        {"/api/supervisor/assignment/select", {"POST"}},
        {"/api/supervisor/assignment/revoke", {"POST"}},
        {"/api/supervisor/config/generate",   {"POST"}},
        {"/api/supervisor/connect/confirm",   {"POST"}},
        {"/api/supervisor/heartbeat",         {"POST"}},
        {"/api/supervisor/status",            {"GET"}},
        // v0.10.13: server-side reachability self-check. Probes the
        // URLs the wizard would issue, from inside the runtime, so the
        // operator can prove MCOS itself is reachable on the LAN IP
        // even when a cloud supervisor reports connection_refused.
        {"/api/supervisor/reachability-check", {"GET"}},
    };

    static const std::vector<std::pair<std::string, std::vector<std::string>>> kPrefix = {
        // Note: longer prefixes first so "/api/setup/dependencies/" wins
        // over "/api/setup/" if both ever overlap.
        {"/api/onboarding/", {"GET"}},
        {"/api/governance/bundles/", {"GET"}},
        {"/api/setup/dependencies/", {"POST"}},
        {"/api/platform-services/config/", {"GET"}},
        {"/api/clients/", {"GET", "DELETE"}},
        {"/api/leases/", {"POST"}},
        {"/api/pools/", {"GET", "POST"}},
        {"/api/telemetry/events", {"GET"}},
        {"/api/activity", {"GET"}},
        {"/api/workflows/", {"GET", "POST", "DELETE"}},
        {"/mcp/gateway/", {"GET"}},
        {"/mcp/governance/", {"GET", "POST"}},
        // v0.9.73: prefix route for fetching individual Forsetti
        // Agentic Edition documents by relative path. Manifest
        // endpoint is exact-match above.
        {"/api/governance/agentic-edition/document/", {"GET"}},
    };

    auto exact = kExact.find(p);
    if (exact != kExact.end()) {
        return exact->second;
    }
    for (const auto& entry : kPrefix) {
        const auto& prefix = entry.first;
        if (p.size() >= prefix.size()
            && p.compare(0, prefix.size(), prefix) == 0) {
            return entry.second;
        }
    }
    return {};
}

// v0.9.28: build a canonical Allow-header value from a route's
// supported method list. Always surfaces HEAD when GET is supported
// (the HEAD->GET rewrite makes this transparently true) and always
// surfaces OPTIONS (preflight short-circuit handles it for every
// path). Keeps the header in stable verb order so caches don't see
// spurious vary churn between requests.
inline std::string buildAllowHeader(const std::vector<std::string>& methods) {
    static const std::vector<std::string> kOrder = {
        "GET", "HEAD", "POST", "PUT", "PATCH", "DELETE", "OPTIONS"
    };
    std::unordered_set<std::string> set(methods.begin(), methods.end());
    if (set.count("GET")) {
        set.insert("HEAD");
    }
    set.insert("OPTIONS");
    std::string allow;
    for (const auto& verb : kOrder) {
        if (set.count(verb)) {
            if (!allow.empty()) {
                allow += ", ";
            }
            allow += verb;
        }
    }
    return allow;
}

} // namespace MasterControl
