// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.
//
// Shared capability strings and policy helpers used by admin route auth,
// MCP gateway catalog filtering, and tests.

#pragma once

#include "MasterControl/LanClient.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <string>
#include <vector>

namespace MasterControl {

inline constexpr const char* kCapabilityProcessExec = "process.exec";
inline constexpr const char* kCapabilityFilesystemRead = "filesystem.read";
inline constexpr const char* kCapabilityFilesystemWrite = "filesystem.write";
inline constexpr const char* kCapabilityDesktopControl = "desktop.control";
inline constexpr const char* kCapabilityInputSynthesize = "input.synthesize";
inline constexpr const char* kCapabilityScreenCapture = "screen.capture";
inline constexpr const char* kCapabilityNetworkAdmin = "network.admin";
inline constexpr const char* kCapabilityInstallPackage = "install.package";
inline constexpr const char* kCapabilityGovernanceModify = "governance.modify";
inline constexpr const char* kCapabilityClientsManage = "clients.manage";
inline constexpr const char* kCapabilitySupervisorAssign = "supervisor.assign";
inline constexpr const char* kCapabilitySetupLocalOrAdmin = "setup.local_or_admin";

struct CapabilityAuthorizationContext final {
    std::string actor = "anonymous";
    std::string clientId;
    std::vector<std::string> capabilities;
    bool authenticated = false;
    bool localRequest = false;
    bool localBootstrap = false;
    std::string denialReason;
};

struct CapabilityDenialAuditEvent final {
    std::string actor = "anonymous";
    std::string clientId;
    std::string remoteAddress;
    std::string method;
    std::string toolName;
    std::vector<std::string> requiredCapabilities;
    std::string reason;
};

using CapabilityResolver =
    std::function<CapabilityAuthorizationContext(const std::string& clientId,
                                                 const std::string& remoteAddress)>;
using CapabilityDenialAuditSink =
    std::function<void(const CapabilityDenialAuditEvent& event)>;

inline std::string normalizeCapability(std::string capability) {
    const auto first = std::find_if_not(
        capability.begin(), capability.end(),
        [](unsigned char ch) { return std::isspace(ch) != 0; });
    const auto last = std::find_if_not(
        capability.rbegin(), capability.rend(),
        [](unsigned char ch) { return std::isspace(ch) != 0; }).base();
    if (first >= last) {
        return {};
    }
    capability = std::string(first, last);
    std::transform(capability.begin(), capability.end(), capability.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return capability;
}

inline bool hasCapability(const std::vector<std::string>& capabilities,
                          const std::string& capability) {
    const auto normalized = normalizeCapability(capability);
    return std::find(capabilities.begin(), capabilities.end(), normalized) != capabilities.end();
}

inline void addCapability(std::vector<std::string>& capabilities,
                          std::string capability) {
    capability = normalizeCapability(std::move(capability));
    if (capability.empty() || hasCapability(capabilities, capability)) {
        return;
    }
    capabilities.push_back(std::move(capability));
}

inline std::vector<std::string> allOperatorCapabilities() {
    return {
        kCapabilityProcessExec,
        kCapabilityFilesystemRead,
        kCapabilityFilesystemWrite,
        kCapabilityDesktopControl,
        kCapabilityInputSynthesize,
        kCapabilityScreenCapture,
        kCapabilityNetworkAdmin,
        kCapabilityInstallPackage,
        kCapabilityGovernanceModify,
        kCapabilityClientsManage,
        kCapabilitySupervisorAssign
    };
}

inline std::vector<std::string> capabilitiesFromPrivileges(
    const LanClientPrivileges& privileges,
    const bool autonomousMode) {
    std::vector<std::string> capabilities;
    if (privileges.canCreateMcpServers ||
        privileges.canModifyMcpServers ||
        privileges.canRemoveMcpServers ||
        privileges.canCreateSubAgents ||
        privileges.canModifySubAgents ||
        privileges.canRemoveSubAgents ||
        autonomousMode) {
        addCapability(capabilities, kCapabilityProcessExec);
    }
    if (privileges.canManageModules) {
        addCapability(capabilities, kCapabilityInstallPackage);
    }
    if (privileges.canChangeGovernancePolicy) {
        addCapability(capabilities, kCapabilityGovernanceModify);
        addCapability(capabilities, kCapabilityNetworkAdmin);
        addCapability(capabilities, kCapabilitySupervisorAssign);
    }
    if (privileges.canManageClients) {
        addCapability(capabilities, kCapabilityClientsManage);
    }
    return capabilities;
}

inline std::vector<std::string> capabilitiesForClient(const LanClient& client) {
    std::vector<std::string> capabilities;
    for (const auto& capability : client.capabilities) {
        addCapability(capabilities, capability);
    }
    for (const auto& capability : capabilitiesFromPrivileges(client.privileges, client.autonomousMode)) {
        addCapability(capabilities, capability);
    }
    if (hasCapability(capabilities, kCapabilityFilesystemWrite)) {
        addCapability(capabilities, kCapabilityFilesystemRead);
    }
    return capabilities;
}

inline LanClientPrivileges privilegesWithCapabilities(
    LanClientPrivileges privileges,
    const std::vector<std::string>& capabilities) {
    if (hasCapability(capabilities, kCapabilityProcessExec)) {
        privileges.canCreateMcpServers = true;
        privileges.canModifyMcpServers = true;
        privileges.canRemoveMcpServers = true;
        privileges.canCreateSubAgents = true;
        privileges.canModifySubAgents = true;
        privileges.canRemoveSubAgents = true;
    }
    if (hasCapability(capabilities, kCapabilityInstallPackage)) {
        privileges.canManageModules = true;
    }
    if (hasCapability(capabilities, kCapabilityGovernanceModify) ||
        hasCapability(capabilities, kCapabilityNetworkAdmin) ||
        hasCapability(capabilities, kCapabilitySupervisorAssign)) {
        privileges.canChangeGovernancePolicy = true;
    }
    if (hasCapability(capabilities, kCapabilityClientsManage)) {
        privileges.canManageClients = true;
    }
    return privileges;
}

inline std::string capabilityRisk(const std::string& capability) {
    const auto normalized = normalizeCapability(capability);
    if (normalized == kCapabilityFilesystemRead) {
        return "medium";
    }
    if (normalized == kCapabilityFilesystemWrite ||
        normalized == kCapabilityScreenCapture ||
        normalized == kCapabilityNetworkAdmin ||
        normalized == kCapabilityGovernanceModify ||
        normalized == kCapabilityClientsManage) {
        return "high";
    }
    if (normalized == kCapabilityProcessExec ||
        normalized == kCapabilityDesktopControl ||
        normalized == kCapabilityInputSynthesize ||
        normalized == kCapabilityInstallPackage ||
        normalized == kCapabilitySupervisorAssign) {
        return "critical";
    }
    return {};
}

inline std::string highestCapabilityRisk(const std::vector<std::string>& capabilities) {
    std::string risk;
    for (const auto& capability : capabilities) {
        const auto candidate = capabilityRisk(capability);
        if (candidate == "critical") {
            return candidate;
        }
        if (candidate == "high") {
            risk = "high";
        } else if (candidate == "medium" && risk.empty()) {
            risk = "medium";
        }
    }
    return risk;
}

inline std::vector<std::string> requiredCapabilitiesForMcpTool(
    const std::string& serverName,
    const std::string& toolName) {
    std::vector<std::string> capabilities;
    const auto server = normalizeCapability(serverName);
    const auto tool = normalizeCapability(toolName);

    if (tool == "shell.exec" ||
        tool == "repl.exec" ||
        tool == "test.run" ||
        tool == "build.run" ||
        tool == "lint.run" ||
        tool == "git.run" ||
        tool == "desktop.launch" ||
        server == "terminal-shell" ||
        server == "local-git" ||
        server == "code-execution-repl" ||
        server == "local-test-runner" ||
        server == "local-build-tool" ||
        server == "local-linter") {
        addCapability(capabilities, kCapabilityProcessExec);
    }

    if (tool == "search.grep" ||
        tool == "index.list_files" ||
        tool == "ctx.get" ||
        tool == "ctx.list" ||
        tool == "kg.search" ||
        tool == "kg.read_graph" ||
        tool == "watch.add" ||
        tool == "watch.poll" ||
        tool == "watch.list" ||
        tool == "watch.remove" ||
        tool == "scribe.list_release_reports" ||
        tool == "scribe.version_state" ||
        server == "filesystem" ||
        server == "file-search" ||
        server == "file-watcher" ||
        server == "local-indexer") {
        addCapability(capabilities, kCapabilityFilesystemRead);
    }

    if (tool == "ctx.set" ||
        tool == "ctx.delete" ||
        tool == "kg.entity.upsert" ||
        tool == "kg.relation.create" ||
        tool == "kg.delete_entity" ||
        tool == "db.set_connection_string" ||
        server == "filesystem" ||
        server == "local-database" ||
        server == "persistent-context" ||
        server == "knowledge-graph") {
        addCapability(capabilities, kCapabilityFilesystemWrite);
        addCapability(capabilities, kCapabilityFilesystemRead);
    }

    if (tool == "desktop.list_windows" ||
        tool == "desktop.focus" ||
        tool == "desktop.launch" ||
        tool == "desktop.foreground" ||
        tool == "computer.window_list" ||
        server == "computer-use" ||
        server == "desktop-control") {
        addCapability(capabilities, kCapabilityDesktopControl);
    }

    if (tool == "input.keyboard" ||
        tool == "input.click" ||
        tool == "input.move" ||
        tool == "input.scroll" ||
        tool == "computer.type" ||
        tool == "computer.click" ||
        tool == "computer.move_mouse" ||
        server == "computer-use" ||
        server == "keyboard-mouse-control") {
        addCapability(capabilities, kCapabilityInputSynthesize);
    }

    if (tool == "screen.capture" ||
        tool == "screen.size" ||
        tool == "computer.screenshot" ||
        server == "computer-use" ||
        server == "screen-capture-vision") {
        addCapability(capabilities, kCapabilityScreenCapture);
    }

    return capabilities;
}

inline bool hasAllCapabilities(const std::vector<std::string>& granted,
                               const std::vector<std::string>& required) {
    for (const auto& capability : required) {
        if (!hasCapability(granted, capability)) {
            return false;
        }
    }
    return true;
}

} // namespace MasterControl
