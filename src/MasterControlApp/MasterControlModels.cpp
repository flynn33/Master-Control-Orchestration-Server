// Master Control Program
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
        { EndpointStatus::Degraded, "degraded" }
    });
}

std::string to_string(ProviderKind value) {
    return enumToString(value, {
        { ProviderKind::Codex, "codex" },
        { ProviderKind::ClaudeCode, "claude_code" },
        { ProviderKind::OpenAI, "openai" },
        { ProviderKind::XAI, "xai" },
        { ProviderKind::Generic, "generic" }
    });
}

std::string to_string(ProviderCredentialFieldKind value) {
    return enumToString(value, {
        { ProviderCredentialFieldKind::ApiKey, "api_key" },
        { ProviderCredentialFieldKind::AuthToken, "auth_token" },
        { ProviderCredentialFieldKind::Model, "model" },
        { ProviderCredentialFieldKind::Text, "text" }
    });
}

std::string to_string(ProviderAssignmentTargetKind value) {
    return enumToString(value, {
        { ProviderAssignmentTargetKind::Role, "role" },
        { ProviderAssignmentTargetKind::SubAgentGroup, "sub_agent_group" },
        { ProviderAssignmentTargetKind::SubAgent, "sub_agent" }
    });
}

std::string to_string(ProviderExecutionStatus value) {
    return enumToString(value, {
        { ProviderExecutionStatus::Pending, "pending" },
        { ProviderExecutionStatus::Running, "running" },
        { ProviderExecutionStatus::Succeeded, "succeeded" },
        { ProviderExecutionStatus::Failed, "failed" }
    });
}

std::string to_string(ProviderExecutionTransport value) {
    return enumToString(value, {
        { ProviderExecutionTransport::OpenAICompatibleChat, "openai_compatible_chat" },
        { ProviderExecutionTransport::ClaudeCodeCli, "claude_code_cli" }
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
        { EndpointStatus::Degraded, "degraded" }
    });
}

ProviderKind providerKindFromString(const std::string& value) {
    return enumFromString<ProviderKind>(value, {
        { ProviderKind::Codex, "codex" },
        { ProviderKind::ClaudeCode, "claude_code" },
        { ProviderKind::OpenAI, "openai" },
        { ProviderKind::XAI, "xai" },
        { ProviderKind::Generic, "generic" }
    });
}

ProviderCredentialFieldKind providerCredentialFieldKindFromString(const std::string& value) {
    return enumFromString<ProviderCredentialFieldKind>(value, {
        { ProviderCredentialFieldKind::ApiKey, "api_key" },
        { ProviderCredentialFieldKind::AuthToken, "auth_token" },
        { ProviderCredentialFieldKind::Model, "model" },
        { ProviderCredentialFieldKind::Text, "text" }
    });
}

ProviderAssignmentTargetKind providerAssignmentTargetKindFromString(const std::string& value) {
    return enumFromString<ProviderAssignmentTargetKind>(value, {
        { ProviderAssignmentTargetKind::Role, "role" },
        { ProviderAssignmentTargetKind::SubAgentGroup, "sub_agent_group" },
        { ProviderAssignmentTargetKind::SubAgent, "sub_agent" }
    });
}

ProviderExecutionStatus providerExecutionStatusFromString(const std::string& value) {
    return enumFromString<ProviderExecutionStatus>(value, {
        { ProviderExecutionStatus::Pending, "pending" },
        { ProviderExecutionStatus::Running, "running" },
        { ProviderExecutionStatus::Succeeded, "succeeded" },
        { ProviderExecutionStatus::Failed, "failed" }
    });
}

ProviderExecutionTransport providerExecutionTransportFromString(const std::string& value) {
    return enumFromString<ProviderExecutionTransport>(value, {
        { ProviderExecutionTransport::OpenAICompatibleChat, "openai_compatible_chat" },
        { ProviderExecutionTransport::ClaudeCodeCli, "claude_code_cli" }
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

void to_json(nlohmann::json& json, ProviderKind value) {
    json = to_string(value);
}

void from_json(const nlohmann::json& json, ProviderKind& value) {
    value = providerKindFromString(json.get<std::string>());
}

void to_json(nlohmann::json& json, ProviderCredentialFieldKind value) {
    json = to_string(value);
}

void from_json(const nlohmann::json& json, ProviderCredentialFieldKind& value) {
    value = providerCredentialFieldKindFromString(json.get<std::string>());
}

void to_json(nlohmann::json& json, ProviderAssignmentTargetKind value) {
    json = to_string(value);
}

void from_json(const nlohmann::json& json, ProviderAssignmentTargetKind& value) {
    value = providerAssignmentTargetKindFromString(json.get<std::string>());
}

void to_json(nlohmann::json& json, ProviderExecutionStatus value) {
    json = to_string(value);
}

void from_json(const nlohmann::json& json, ProviderExecutionStatus& value) {
    value = providerExecutionStatusFromString(json.get<std::string>());
}

void to_json(nlohmann::json& json, ProviderExecutionTransport value) {
    json = to_string(value);
}

void from_json(const nlohmann::json& json, ProviderExecutionTransport& value) {
    value = providerExecutionTransportFromString(json.get<std::string>());
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
