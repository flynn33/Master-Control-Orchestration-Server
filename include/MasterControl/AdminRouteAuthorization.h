// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.
//
// AdminRouteAuthorization.h - capability policy for mutating admin routes.

#pragma once

#include "MasterControl/CapabilityAuthorization.h"

#include <string>
#include <vector>

namespace MasterControl {

class AdminRouteCapabilityPolicy final {
public:
    static bool isMutatingMethod(const std::string& method) noexcept {
        return method == "POST" || method == "PUT" ||
               method == "PATCH" || method == "DELETE";
    }

    static std::vector<std::string> requiredCapabilitiesForRoute(
        const std::string& method,
        const std::string& path) {
        if (!isMutatingMethod(method)) {
            return {};
        }

        const auto normalizedPath = stripQuery(path);

        if (normalizedPath == "/api/config" ||
            normalizedPath == "/api/governance/decisions" ||
            normalizedPath == "/api/client/governance/decisions" ||
            normalizedPath == "/api/clu/execute" ||
            normalizedPath == "/api/clu/apple-operations/cancel" ||
            normalizedPath == "/api/diagnostics/clear" ||
            normalizedPath == "/api/platform-services/apple-hosts" ||
            normalizedPath == "/api/platform-services/apple-hosts/remove" ||
            startsWith(normalizedPath, "/mcp/governance/")) {
            return { kCapabilityGovernanceModify };
        }

        if (normalizedPath == "/api/gateway/start" ||
            normalizedPath == "/api/gateway/stop" ||
            normalizedPath == "/api/gateway/restart") {
            return { kCapabilityNetworkAdmin };
        }

        if (normalizedPath == "/api/clients" ||
            startsWith(normalizedPath, "/api/clients/")) {
            return { kCapabilityClientsManage };
        }

        if (normalizedPath == "/api/setup/start" ||
            normalizedPath == "/api/setup/complete" ||
            normalizedPath == "/api/setup/dismiss" ||
            normalizedPath == "/api/setup/reset" ||
            startsWith(normalizedPath, "/api/setup/workflow-templates/") ||
            normalizedPath == "/api/settings/advanced-mode" ||
            normalizedPath == "/api/self-tests/run") {
            return { kCapabilitySetupLocalOrAdmin };
        }

        if (startsWith(normalizedPath, "/api/setup/dependencies/") ||
            normalizedPath == "/api/install/package" ||
            normalizedPath == "/api/install/repo" ||
            normalizedPath == "/api/install/zip" ||
            normalizedPath == "/api/forsetti/modules/state" ||
            normalizedPath == "/api/claude-plugin/toggle" ||
            normalizedPath == "/api/chatgpt-plugin/toggle" ||
            normalizedPath == "/api/grok-plugin/toggle") {
            return { kCapabilityInstallPackage };
        }

        if (normalizedPath == "/api/pools" ||
            startsWith(normalizedPath, "/api/pools/") ||
            startsWith(normalizedPath, "/api/leases/") ||
            normalizedPath == "/api/runtime/mcp-servers" ||
            normalizedPath == "/api/runtime/mcp-servers/remove" ||
            normalizedPath == "/api/runtime/subagents" ||
            normalizedPath == "/api/runtime/subagents/remove" ||
            normalizedPath == "/api/runtime/subagent-groups" ||
            normalizedPath == "/api/runtime/subagent-groups/remove" ||
            normalizedPath == "/api/workflows" ||
            startsWith(normalizedPath, "/api/workflows/")) {
            return { kCapabilityProcessExec };
        }

        if (normalizedPath == "/api/supervisor/assignment/select" ||
            normalizedPath == "/api/supervisor/assignment/revoke" ||
            normalizedPath == "/api/supervisor/config/generate" ||
            normalizedPath == "/api/supervisor/connect/confirm" ||
            normalizedPath == "/api/supervisor/heartbeat") {
            return { kCapabilitySupervisorAssign };
        }

        return {};
    }

private:
    static bool startsWith(const std::string& value, const char* prefix) {
        const std::string prefixValue(prefix);
        return value.size() >= prefixValue.size() &&
               value.compare(0, prefixValue.size(), prefixValue) == 0;
    }

    static std::string stripQuery(const std::string& path) {
        const auto query = path.find('?');
        return query == std::string::npos ? path : path.substr(0, query);
    }
};

} // namespace MasterControl
