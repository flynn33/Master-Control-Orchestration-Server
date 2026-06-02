// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.
//
// Source-neutral endpoint advertisement helpers. Discovery documents and
// supervisor configuration generation must advertise only endpoints that the
// current runtime posture can plausibly serve.

#pragma once

#include "MasterControl/MasterControlModels.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>

namespace MasterControl {

struct AdvertisedEndpointPlan final {
    bool lanModeEnabled = false;
    bool gatewayRunning = false;
    bool adminLanAdvertised = false;
    bool mcpLanAdvertised = false;
    std::string adminHost = "127.0.0.1";
    std::string mcpHost = "127.0.0.1";
    std::string adminBaseUrl;
    std::string discoveryEndpoint;
    std::string mcpEndpoint;
    std::string mcpHealthEndpoint;
    std::string networkMode = "local-only";
    std::string reason;
};

inline std::string normalizeAdvertisementHost(std::string host) {
    if (host.size() >= 2 && host.front() == '[' && host.back() == ']') {
        host = host.substr(1, host.size() - 2);
    }
    std::transform(host.begin(), host.end(), host.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return host;
}

inline bool isWildcardAdvertisementHost(const std::string& host) {
    const auto normalized = normalizeAdvertisementHost(host);
    return normalized.empty() ||
        normalized == "0.0.0.0" ||
        normalized == "::" ||
        normalized == "::0" ||
        normalized == "*";
}

inline bool isLoopbackAdvertisementHost(const std::string& host) {
    const auto normalized = normalizeAdvertisementHost(host);
    return normalized == "localhost" ||
        normalized == "::1" ||
        normalized.rfind("127.", 0) == 0 ||
        normalized.rfind("::ffff:127.", 0) == 0;
}

inline bool isLanAdvertisementCandidate(const std::string& host) {
    return !isWildcardAdvertisementHost(host) && !isLoopbackAdvertisementHost(host);
}

inline std::string bracketAdvertisementHost(const std::string& host) {
    if (host.empty()) {
        return host;
    }
    if (host.front() == '[' && host.back() == ']') {
        return host;
    }
    return host.find(':') == std::string::npos ? host : "[" + host + "]";
}

inline std::string ensureAdvertisementPath(std::string path, const char* fallback) {
    if (path.empty()) {
        path = fallback;
    }
    if (path.empty() || path.front() == '/') {
        return path;
    }
    return "/" + path;
}

inline std::string firstLanAdvertisementCandidate(const AppConfiguration& configuration,
                                                  const std::string& runtimePrimaryIp) {
    if (isLanAdvertisementCandidate(configuration.activeProfile.preferredBindAddress)) {
        return configuration.activeProfile.preferredBindAddress;
    }
    if (isLanAdvertisementCandidate(runtimePrimaryIp)) {
        return runtimePrimaryIp;
    }
    return {};
}

inline std::string resolveAdminAdvertisementHost(const AppConfiguration& configuration,
                                                 const std::string& runtimePrimaryIp) {
    if (!configuration.security.allowOpenLanAccess) {
        return "127.0.0.1";
    }
    if (!isWildcardAdvertisementHost(configuration.bindAddress)) {
        return isLanAdvertisementCandidate(configuration.bindAddress)
            ? configuration.bindAddress
            : std::string("127.0.0.1");
    }
    const auto candidate = firstLanAdvertisementCandidate(configuration, runtimePrimaryIp);
    return candidate.empty() ? std::string("127.0.0.1") : candidate;
}

inline std::string resolveMcpAdvertisementHost(const AppConfiguration& configuration,
                                               const GatewayStatus& gatewayStatus,
                                               const std::string& runtimePrimaryIp) {
    if (!configuration.security.allowOpenLanAccess ||
        gatewayStatus.state != GatewayState::Running) {
        return "127.0.0.1";
    }
    const auto& listenHost = configuration.mcpGateway.listenHost;
    if (!isWildcardAdvertisementHost(listenHost)) {
        return isLanAdvertisementCandidate(listenHost)
            ? listenHost
            : std::string("127.0.0.1");
    }
    const auto candidate = firstLanAdvertisementCandidate(configuration, runtimePrimaryIp);
    if (!candidate.empty()) {
        return candidate;
    }
    return resolveAdminAdvertisementHost(configuration, runtimePrimaryIp);
}

inline AdvertisedEndpointPlan buildAdvertisedEndpointPlan(
    const AppConfiguration& configuration,
    const GatewayStatus& gatewayStatus,
    const std::string& runtimePrimaryIp) {
    AdvertisedEndpointPlan plan;
    plan.lanModeEnabled = configuration.security.allowOpenLanAccess;
    plan.gatewayRunning = gatewayStatus.state == GatewayState::Running;
    plan.networkMode = plan.lanModeEnabled ? "trusted-lan" : "local-only";
    plan.adminHost = resolveAdminAdvertisementHost(configuration, runtimePrimaryIp);
    plan.mcpHost = resolveMcpAdvertisementHost(configuration, gatewayStatus, runtimePrimaryIp);
    plan.adminLanAdvertised = isLanAdvertisementCandidate(plan.adminHost);
    plan.mcpLanAdvertised = isLanAdvertisementCandidate(plan.mcpHost);

    const auto adminHostInUrl = bracketAdvertisementHost(plan.adminHost);
    const auto mcpHostInUrl = bracketAdvertisementHost(plan.mcpHost);
    const auto mcpPath = ensureAdvertisementPath(configuration.mcpGateway.mcpPath, "/mcp");
    const auto healthPath = ensureAdvertisementPath(configuration.mcpGateway.healthPath, "/health");

    plan.adminBaseUrl = "http://" + adminHostInUrl + ":" + std::to_string(configuration.browserPort);
    plan.discoveryEndpoint = plan.adminBaseUrl + "/.well-known/mcos.json";
    plan.mcpEndpoint = "http://" + mcpHostInUrl + ":" +
        std::to_string(configuration.mcpGateway.listenPort) + mcpPath;
    plan.mcpHealthEndpoint = "http://" + mcpHostInUrl + ":" +
        std::to_string(configuration.mcpGateway.listenPort) + healthPath;

    if (!plan.lanModeEnabled) {
        plan.reason = "LAN mode is disabled; generated endpoints are local-only.";
    } else if (!plan.adminLanAdvertised) {
        plan.reason = "LAN mode is enabled, but the admin listener is not bound to a LAN-routable host.";
    } else if (!plan.gatewayRunning) {
        plan.reason = "LAN mode is enabled, but the MCP gateway is not running; MCP endpoint remains local-only.";
    } else if (!plan.mcpLanAdvertised) {
        plan.reason = "LAN mode is enabled, but the MCP gateway listener is not LAN-routable.";
    } else {
        plan.reason = "LAN mode is enabled and advertised endpoints are LAN-routable.";
    }
    return plan;
}

} // namespace MasterControl
