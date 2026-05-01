// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#include "MasterControl/MasterControlModels.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace MasterControl {

namespace {

template <typename T>
std::string enumToString(T value, const std::initializer_list<std::pair<T, const char*>>& pairs) {
    const auto iterator = std::find_if(
        pairs.begin(),
        pairs.end(),
        [value](const auto& pair) { return pair.first == value; });
    if (iterator == pairs.end()) {
        throw std::invalid_argument("Unknown enum value");
    }
    return iterator->second;
}

template <typename T>
T enumFromString(const std::string& value, const std::initializer_list<std::pair<T, const char*>>& pairs) {
    const auto iterator = std::find_if(
        pairs.begin(),
        pairs.end(),
        [&value](const auto& pair) { return value == pair.second; });
    if (iterator == pairs.end()) {
        throw std::invalid_argument("Unknown enum string: " + value);
    }
    return iterator->first;
}

} // namespace

std::string to_string(EndpointKind value) {
    return enumToString(value, {
        { EndpointKind::Host, "host" },
        { EndpointKind::Gateway, "gateway" },
        { EndpointKind::MCPServer, "mcp_server" },
        { EndpointKind::SubAgent, "sub_agent" },
        { EndpointKind::BrowserGateway, "browser_gateway" },
        { EndpointKind::Beacon, "beacon" }
    });
}

std::string to_string(EndpointStatus value) {
    return enumToString(value, {
        { EndpointStatus::Unknown, "unknown" },
        { EndpointStatus::Online, "online" },
        { EndpointStatus::Offline, "offline" },
        { EndpointStatus::Degraded, "degraded" },
        { EndpointStatus::Template, "template" }
    });
}


std::string to_string(InstallerKind value) {
    return enumToString(value, {
        { InstallerKind::Msi, "msi" },
        { InstallerKind::Exe, "exe" },
        { InstallerKind::PowerShell, "powershell" },
        { InstallerKind::GitBootstrapRepo, "git_bootstrap_repo" },
        { InstallerKind::ZipBundle, "zip_bundle" }
    });
}

std::string to_string(ControlSurfaceToolbarAction value) {
    return enumToString(value, {
        { ControlSurfaceToolbarAction::Navigate, "navigate" },
        { ControlSurfaceToolbarAction::OpenOverlay, "open_overlay" }
    });
}

std::string to_string(PlatformTarget value) {
    return enumToString(value, {
        { PlatformTarget::Windows, "windows" },
        { PlatformTarget::MacOS, "macos" },
        { PlatformTarget::IOS, "ios" },
        { PlatformTarget::Unknown, "unknown" }
    });
}

std::string to_string(AppleRemoteTransport value) {
    return enumToString(value, {
        { AppleRemoteTransport::Unknown, "unknown" },
        { AppleRemoteTransport::Ssh, "ssh" },
        { AppleRemoteTransport::CompanionService, "companion_service" }
    });
}

std::string to_string(GovernanceToolStatus value) {
    return enumToString(value, {
        { GovernanceToolStatus::Passed, "passed" },
        { GovernanceToolStatus::Warning, "warning" },
        { GovernanceToolStatus::Failed, "failed" },
        { GovernanceToolStatus::Unsupported, "unsupported" }
    });
}

std::string to_string(AppleOperationStatus value) {
    return enumToString(value, {
        { AppleOperationStatus::Queued, "queued" },
        { AppleOperationStatus::Running, "running" },
        { AppleOperationStatus::Succeeded, "succeeded" },
        { AppleOperationStatus::Failed, "failed" },
        { AppleOperationStatus::Blocked, "blocked" },
        { AppleOperationStatus::Canceled, "canceled" }
    });
}

std::string to_string(GovernanceActionKind value) {
    return enumToString(value, {
        { GovernanceActionKind::Unknown, "unknown" },
        { GovernanceActionKind::ClientRegister, "client_register" },
        { GovernanceActionKind::ClientPrivilegeChange, "client_privilege_change" },
        { GovernanceActionKind::ClientAutonomousModeChange, "client_autonomous_mode_change" },
        { GovernanceActionKind::ClientRevoke, "client_revoke" },
        { GovernanceActionKind::McpServerCreate, "mcp_server_create" },
        { GovernanceActionKind::McpServerModify, "mcp_server_modify" },
        { GovernanceActionKind::McpServerRemove, "mcp_server_remove" },
        { GovernanceActionKind::SubAgentCreate, "sub_agent_create" },
        { GovernanceActionKind::SubAgentModify, "sub_agent_modify" },
        { GovernanceActionKind::SubAgentRemove, "sub_agent_remove" },
        { GovernanceActionKind::ModuleEnable, "module_enable" },
        { GovernanceActionKind::ModuleDisable, "module_disable" },
        { GovernanceActionKind::GovernancePolicyChange, "governance_policy_change" },
        { GovernanceActionKind::RemoteInstall, "remote_install" }
    });
}

std::string to_string(GovernanceDecisionOutcome value) {
    return enumToString(value, {
        { GovernanceDecisionOutcome::Allow, "allow" },
        { GovernanceDecisionOutcome::Block, "block" },
        { GovernanceDecisionOutcome::RequiresOperatorApproval, "requires_operator_approval" }
    });
}

std::string to_string(GatewayType value) {
    return enumToString(value, {
        { GatewayType::MCPJungle, "mcpjungle" },
        { GatewayType::Native, "native" }
    });
}

std::string to_string(GatewayState value) {
    return enumToString(value, {
        { GatewayState::Disabled, "disabled" },
        { GatewayState::Configured, "configured" },
        { GatewayState::Starting, "starting" },
        { GatewayState::Running, "running" },
        { GatewayState::Stopping, "stopping" },
        { GatewayState::Stopped, "stopped" },
        { GatewayState::Failed, "failed" }
    });
}

std::string to_string(GatewayHealthStatus value) {
    return enumToString(value, {
        { GatewayHealthStatus::Unknown, "unknown" },
        { GatewayHealthStatus::Healthy, "healthy" },
        { GatewayHealthStatus::Degraded, "degraded" },
        { GatewayHealthStatus::Unhealthy, "unhealthy" }
    });
}

std::string to_string(McpServerTransport value) {
    return enumToString(value, {
        { McpServerTransport::StreamableHttp, "streamable_http" },
        { McpServerTransport::Stdio, "stdio" }
    });
}

std::string to_string(EndpointPoolKind value) {
    return enumToString(value, {
        { EndpointPoolKind::McpServer, "mcp-server" },
        { EndpointPoolKind::SubAgent, "sub-agent" }
    });
}

std::string to_string(EndpointInstanceState value) {
    return enumToString(value, {
        { EndpointInstanceState::Configured, "configured" },
        { EndpointInstanceState::Starting, "starting" },
        { EndpointInstanceState::Ready, "ready" },
        { EndpointInstanceState::Busy, "busy" },
        { EndpointInstanceState::Draining, "draining" },
        { EndpointInstanceState::Failed, "failed" },
        { EndpointInstanceState::Stopped, "stopped" }
    });
}

std::string to_string(LeaseState value) {
    return enumToString(value, {
        { LeaseState::Active, "active" },
        { LeaseState::Released, "released" },
        { LeaseState::Failed, "failed" }
    });
}

std::string to_string(TelemetryCategory value) {
    return enumToString(value, {
        { TelemetryCategory::Host, "host" },
        { TelemetryCategory::Client, "client" },
        { TelemetryCategory::Gateway, "gateway" },
        { TelemetryCategory::Worker, "worker" },
        { TelemetryCategory::Governance, "governance" },
        { TelemetryCategory::Discovery, "discovery" },
        { TelemetryCategory::Dashboard, "dashboard" },
        { TelemetryCategory::System, "system" }
    });
}

std::string to_string(TelemetrySeverity value) {
    return enumToString(value, {
        { TelemetrySeverity::Info, "info" },
        { TelemetrySeverity::Warning, "warning" },
        { TelemetrySeverity::Error, "error" },
        { TelemetrySeverity::Critical, "critical" }
    });
}

EndpointKind endpointKindFromString(const std::string& value) {
    return enumFromString<EndpointKind>(value, {
        { EndpointKind::Host, "host" },
        { EndpointKind::Gateway, "gateway" },
        { EndpointKind::MCPServer, "mcp_server" },
        { EndpointKind::SubAgent, "sub_agent" },
        { EndpointKind::BrowserGateway, "browser_gateway" },
        { EndpointKind::Beacon, "beacon" }
    });
}

EndpointStatus endpointStatusFromString(const std::string& value) {
    return enumFromString<EndpointStatus>(value, {
        { EndpointStatus::Unknown, "unknown" },
        { EndpointStatus::Online, "online" },
        { EndpointStatus::Offline, "offline" },
        { EndpointStatus::Degraded, "degraded" },
        { EndpointStatus::Template, "template" }
    });
}

InstallerKind installerKindFromString(const std::string& value) {
    return enumFromString<InstallerKind>(value, {
        { InstallerKind::Msi, "msi" },
        { InstallerKind::Exe, "exe" },
        { InstallerKind::PowerShell, "powershell" },
        { InstallerKind::GitBootstrapRepo, "git_bootstrap_repo" },
        { InstallerKind::ZipBundle, "zip_bundle" }
    });
}

ControlSurfaceToolbarAction controlSurfaceToolbarActionFromString(const std::string& value) {
    return enumFromString<ControlSurfaceToolbarAction>(value, {
        { ControlSurfaceToolbarAction::Navigate, "navigate" },
        { ControlSurfaceToolbarAction::OpenOverlay, "open_overlay" }
    });
}

PlatformTarget platformTargetFromString(const std::string& value) {
    return enumFromString<PlatformTarget>(value, {
        { PlatformTarget::Windows, "windows" },
        { PlatformTarget::MacOS, "macos" },
        { PlatformTarget::IOS, "ios" },
        { PlatformTarget::Unknown, "unknown" }
    });
}

AppleRemoteTransport appleRemoteTransportFromString(const std::string& value) {
    return enumFromString<AppleRemoteTransport>(value, {
        { AppleRemoteTransport::Unknown, "unknown" },
        { AppleRemoteTransport::Ssh, "ssh" },
        { AppleRemoteTransport::CompanionService, "companion_service" }
    });
}

GovernanceToolStatus governanceToolStatusFromString(const std::string& value) {
    return enumFromString<GovernanceToolStatus>(value, {
        { GovernanceToolStatus::Passed, "passed" },
        { GovernanceToolStatus::Warning, "warning" },
        { GovernanceToolStatus::Failed, "failed" },
        { GovernanceToolStatus::Unsupported, "unsupported" }
    });
}

AppleOperationStatus appleOperationStatusFromString(const std::string& value) {
    return enumFromString<AppleOperationStatus>(value, {
        { AppleOperationStatus::Queued, "queued" },
        { AppleOperationStatus::Running, "running" },
        { AppleOperationStatus::Succeeded, "succeeded" },
        { AppleOperationStatus::Failed, "failed" },
        { AppleOperationStatus::Blocked, "blocked" },
        { AppleOperationStatus::Canceled, "canceled" }
    });
}

GovernanceActionKind governanceActionKindFromString(const std::string& value) {
    return enumFromString<GovernanceActionKind>(value, {
        { GovernanceActionKind::Unknown, "unknown" },
        { GovernanceActionKind::ClientRegister, "client_register" },
        { GovernanceActionKind::ClientPrivilegeChange, "client_privilege_change" },
        { GovernanceActionKind::ClientAutonomousModeChange, "client_autonomous_mode_change" },
        { GovernanceActionKind::ClientRevoke, "client_revoke" },
        { GovernanceActionKind::McpServerCreate, "mcp_server_create" },
        { GovernanceActionKind::McpServerModify, "mcp_server_modify" },
        { GovernanceActionKind::McpServerRemove, "mcp_server_remove" },
        { GovernanceActionKind::SubAgentCreate, "sub_agent_create" },
        { GovernanceActionKind::SubAgentModify, "sub_agent_modify" },
        { GovernanceActionKind::SubAgentRemove, "sub_agent_remove" },
        { GovernanceActionKind::ModuleEnable, "module_enable" },
        { GovernanceActionKind::ModuleDisable, "module_disable" },
        { GovernanceActionKind::GovernancePolicyChange, "governance_policy_change" },
        { GovernanceActionKind::RemoteInstall, "remote_install" }
    });
}

GovernanceDecisionOutcome governanceDecisionOutcomeFromString(const std::string& value) {
    return enumFromString<GovernanceDecisionOutcome>(value, {
        { GovernanceDecisionOutcome::Allow, "allow" },
        { GovernanceDecisionOutcome::Block, "block" },
        { GovernanceDecisionOutcome::RequiresOperatorApproval, "requires_operator_approval" }
    });
}

GatewayType gatewayTypeFromString(const std::string& value) {
    return enumFromString<GatewayType>(value, {
        { GatewayType::MCPJungle, "mcpjungle" },
        { GatewayType::Native, "native" }
    });
}

GatewayState gatewayStateFromString(const std::string& value) {
    return enumFromString<GatewayState>(value, {
        { GatewayState::Disabled, "disabled" },
        { GatewayState::Configured, "configured" },
        { GatewayState::Starting, "starting" },
        { GatewayState::Running, "running" },
        { GatewayState::Stopping, "stopping" },
        { GatewayState::Stopped, "stopped" },
        { GatewayState::Failed, "failed" }
    });
}

GatewayHealthStatus gatewayHealthStatusFromString(const std::string& value) {
    return enumFromString<GatewayHealthStatus>(value, {
        { GatewayHealthStatus::Unknown, "unknown" },
        { GatewayHealthStatus::Healthy, "healthy" },
        { GatewayHealthStatus::Degraded, "degraded" },
        { GatewayHealthStatus::Unhealthy, "unhealthy" }
    });
}

McpServerTransport mcpServerTransportFromString(const std::string& value) {
    return enumFromString<McpServerTransport>(value, {
        { McpServerTransport::StreamableHttp, "streamable_http" },
        { McpServerTransport::Stdio, "stdio" }
    });
}

EndpointPoolKind endpointPoolKindFromString(const std::string& value) {
    return enumFromString<EndpointPoolKind>(value, {
        { EndpointPoolKind::McpServer, "mcp-server" },
        { EndpointPoolKind::SubAgent, "sub-agent" }
    });
}

EndpointInstanceState endpointInstanceStateFromString(const std::string& value) {
    return enumFromString<EndpointInstanceState>(value, {
        { EndpointInstanceState::Configured, "configured" },
        { EndpointInstanceState::Starting, "starting" },
        { EndpointInstanceState::Ready, "ready" },
        { EndpointInstanceState::Busy, "busy" },
        { EndpointInstanceState::Draining, "draining" },
        { EndpointInstanceState::Failed, "failed" },
        { EndpointInstanceState::Stopped, "stopped" }
    });
}

LeaseState leaseStateFromString(const std::string& value) {
    return enumFromString<LeaseState>(value, {
        { LeaseState::Active, "active" },
        { LeaseState::Released, "released" },
        { LeaseState::Failed, "failed" }
    });
}

TelemetryCategory telemetryCategoryFromString(const std::string& value) {
    return enumFromString<TelemetryCategory>(value, {
        { TelemetryCategory::Host, "host" },
        { TelemetryCategory::Client, "client" },
        { TelemetryCategory::Gateway, "gateway" },
        { TelemetryCategory::Worker, "worker" },
        { TelemetryCategory::Governance, "governance" },
        { TelemetryCategory::Discovery, "discovery" },
        { TelemetryCategory::Dashboard, "dashboard" },
        { TelemetryCategory::System, "system" }
    });
}

TelemetrySeverity telemetrySeverityFromString(const std::string& value) {
    return enumFromString<TelemetrySeverity>(value, {
        { TelemetrySeverity::Info, "info" },
        { TelemetrySeverity::Warning, "warning" },
        { TelemetrySeverity::Error, "error" },
        { TelemetrySeverity::Critical, "critical" }
    });
}

void to_json(nlohmann::json& json, EndpointKind value) {
    json = to_string(value);
}

void from_json(const nlohmann::json& json, EndpointKind& value) {
    value = endpointKindFromString(json.get<std::string>());
}

void to_json(nlohmann::json& json, EndpointStatus value) {
    json = to_string(value);
}

void from_json(const nlohmann::json& json, EndpointStatus& value) {
    value = endpointStatusFromString(json.get<std::string>());
}


void to_json(nlohmann::json& json, InstallerKind value) {
    json = to_string(value);
}

void from_json(const nlohmann::json& json, InstallerKind& value) {
    value = installerKindFromString(json.get<std::string>());
}

void to_json(nlohmann::json& json, ControlSurfaceToolbarAction value) {
    json = to_string(value);
}

void from_json(const nlohmann::json& json, ControlSurfaceToolbarAction& value) {
    value = controlSurfaceToolbarActionFromString(json.get<std::string>());
}

void to_json(nlohmann::json& json, PlatformTarget value) {
    json = to_string(value);
}

void from_json(const nlohmann::json& json, PlatformTarget& value) {
    value = platformTargetFromString(json.get<std::string>());
}

void to_json(nlohmann::json& json, AppleRemoteTransport value) {
    json = to_string(value);
}

void from_json(const nlohmann::json& json, AppleRemoteTransport& value) {
    value = appleRemoteTransportFromString(json.get<std::string>());
}

void to_json(nlohmann::json& json, GovernanceToolStatus value) {
    json = to_string(value);
}

void from_json(const nlohmann::json& json, GovernanceToolStatus& value) {
    value = governanceToolStatusFromString(json.get<std::string>());
}

void to_json(nlohmann::json& json, AppleOperationStatus value) {
    json = to_string(value);
}

void from_json(const nlohmann::json& json, AppleOperationStatus& value) {
    value = appleOperationStatusFromString(json.get<std::string>());
}

void to_json(nlohmann::json& json, GovernanceActionKind value) {
    json = to_string(value);
}

void from_json(const nlohmann::json& json, GovernanceActionKind& value) {
    value = governanceActionKindFromString(json.get<std::string>());
}

void to_json(nlohmann::json& json, GovernanceDecisionOutcome value) {
    json = to_string(value);
}

void from_json(const nlohmann::json& json, GovernanceDecisionOutcome& value) {
    value = governanceDecisionOutcomeFromString(json.get<std::string>());
}

void to_json(nlohmann::json& json, GatewayType value) {
    json = to_string(value);
}

void from_json(const nlohmann::json& json, GatewayType& value) {
    value = gatewayTypeFromString(json.get<std::string>());
}

void to_json(nlohmann::json& json, GatewayState value) {
    json = to_string(value);
}

void from_json(const nlohmann::json& json, GatewayState& value) {
    value = gatewayStateFromString(json.get<std::string>());
}

void to_json(nlohmann::json& json, GatewayHealthStatus value) {
    json = to_string(value);
}

void from_json(const nlohmann::json& json, GatewayHealthStatus& value) {
    value = gatewayHealthStatusFromString(json.get<std::string>());
}

void to_json(nlohmann::json& json, McpServerTransport value) {
    json = to_string(value);
}

void from_json(const nlohmann::json& json, McpServerTransport& value) {
    value = mcpServerTransportFromString(json.get<std::string>());
}

void to_json(nlohmann::json& json, EndpointPoolKind value) {
    json = to_string(value);
}

void from_json(const nlohmann::json& json, EndpointPoolKind& value) {
    value = endpointPoolKindFromString(json.get<std::string>());
}

void to_json(nlohmann::json& json, EndpointInstanceState value) {
    json = to_string(value);
}

void from_json(const nlohmann::json& json, EndpointInstanceState& value) {
    value = endpointInstanceStateFromString(json.get<std::string>());
}

void to_json(nlohmann::json& json, LeaseState value) {
    json = to_string(value);
}

void from_json(const nlohmann::json& json, LeaseState& value) {
    value = leaseStateFromString(json.get<std::string>());
}

void to_json(nlohmann::json& json, TelemetryCategory value) {
    json = to_string(value);
}

void from_json(const nlohmann::json& json, TelemetryCategory& value) {
    value = telemetryCategoryFromString(json.get<std::string>());
}

void to_json(nlohmann::json& json, TelemetrySeverity value) {
    json = to_string(value);
}

void from_json(const nlohmann::json& json, TelemetrySeverity& value) {
    value = telemetrySeverityFromString(json.get<std::string>());
}

std::string toPrettyJson(const nlohmann::json& json) {
    return json.dump(2);
}

std::string timestampNowUtc() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);

    std::tm utcTime{};
#if defined(_WIN32)
    gmtime_s(&utcTime, &nowTime);
#else
    gmtime_r(&nowTime, &utcTime);
#endif

    std::ostringstream stream;
    stream << std::put_time(&utcTime, "%Y-%m-%dT%H:%M:%SZ");
    return stream.str();
}

} // namespace MasterControl
