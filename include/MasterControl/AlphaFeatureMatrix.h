// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.
//
// Product alpha feature matrix -- the single in-tree source of truth for which
// features are required / optional / deferred / removed for the working alpha.
// Runtime guards (deprecated-id prune, pool registration) and the test suite
// both consume these lists so runtime and tests cannot drift. The packaged
// artifact resources/alpha-feature-matrix.json mirrors this header and a test
// asserts they agree.
//
// Classification meaning:
//   required : must work end-to-end for the working alpha.
//   optional : may be present; diagnostics must honestly report unavailable.
//   deferred : not part of the alpha; must not be advertised as operational.
//   removed  : must never be registered or advertised on any runtime surface.

#pragma once

#include <array>
#include <string_view>

namespace MasterControl {

// Removed by operator decision. These IDs must not appear as a default pool,
// seeded endpoint, discovery entry, client catalog entry, gateway tool,
// onboarding profile, or dashboard surface, and must never be re-introduced by
// self-heal.
inline constexpr std::array<std::string_view, 2> kRemovedAlphaFeatureIds = {
    "docker-control",
    "playwright",
};

// Alpha-required feature IDs (kept in sync with resources/alpha-feature-matrix.json).
inline constexpr std::array<std::string_view, 17> kRequiredAlphaFeatureIds = {
    "windows-service-lifecycle",
    "admin-http-listener",
    "native-http-sys-mcp-gateway",
    "gateway-status-and-tools",
    "managed-endpoint-pools",
    "diagnostics-aggregator",
    "json-strictness-diagnostics",
    "lan-discovery",
    "lan-client-registration",
    "onboarding-profiles",
    "governance-bundles",
    "governance-decisions",
    "confirm-guards",
    "second-host-lan-acceptance",
    "working-alpha-readiness",
    "local-database",
    "model-parity",
};

// Optional integrations: present-but-honestly-unavailable is acceptable.
inline constexpr std::array<std::string_view, 1> kOptionalAlphaFeatureIds = {
    "external-npm-mcps",
};

// Deferred: not part of the alpha and must not be advertised as operational.
inline constexpr std::array<std::string_view, 1> kDeferredAlphaFeatureIds = {
    "sse-transport",
};

inline constexpr bool isRemovedAlphaFeatureId(std::string_view id) {
    for (const auto& removed : kRemovedAlphaFeatureIds) {
        if (id == removed) {
            return true;
        }
    }
    return false;
}

} // namespace MasterControl
