// Forsetti Framework for Windows
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential. Patent Pending.

#include "ForsettiCore/ModuleModels.h"

#include <stdexcept>

namespace Forsetti {

// MARK: - Platform

std::string to_string(Platform platform) {
    switch (platform) {
        case Platform::Windows: return "Windows";
    }
    throw std::invalid_argument("Unknown Platform value");
}

Platform platformFromString(const std::string& str) {
    if (str == "Windows") return Platform::Windows;
    if (str == "iOS")     return Platform::Windows; // Map to Windows on this port
    if (str == "macOS")   return Platform::Windows; // Map to Windows on this port
    throw std::invalid_argument("Unknown Platform string: " + str);
}

void to_json(nlohmann::json& j, Platform platform) {
    j = to_string(platform);
}

void from_json(const nlohmann::json& j, Platform& platform) {
    platform = platformFromString(j.get<std::string>());
}

// MARK: - ModuleType

std::string to_string(ModuleType type) {
    switch (type) {
        case ModuleType::Service: return "service";
        case ModuleType::UI:      return "ui";
        case ModuleType::App:     return "app";
    }
    throw std::invalid_argument("Unknown ModuleType value");
}

ModuleType moduleTypeFromString(const std::string& str) {
    if (str == "service") return ModuleType::Service;
    if (str == "ui")      return ModuleType::UI;
    if (str == "app")     return ModuleType::App;
    throw std::invalid_argument("Unknown ModuleType string: " + str);
}

void to_json(nlohmann::json& j, ModuleType type) {
    j = to_string(type);
}

void from_json(const nlohmann::json& j, ModuleType& type) {
    type = moduleTypeFromString(j.get<std::string>());
}

// MARK: - Capability

std::string to_string(Capability capability) {
    switch (capability) {
        case Capability::Networking:      return "networking";
        case Capability::Storage:         return "storage";
        case Capability::SecureStorage:   return "secure_storage";
        case Capability::FileExport:      return "file_export";
        case Capability::Telemetry:       return "telemetry";
        case Capability::RoutingOverlay:  return "routing_overlay";
        case Capability::ToolbarItems:    return "toolbar_items";
        case Capability::ViewInjection:   return "view_injection";
        case Capability::UIThemeMask:     return "ui_theme_mask";
        case Capability::EventPublishing: return "event_publishing";
    }
    throw std::invalid_argument("Unknown Capability value");
}

Capability capabilityFromString(const std::string& str) {
    if (str == "networking")       return Capability::Networking;
    if (str == "storage")          return Capability::Storage;
    if (str == "secure_storage")   return Capability::SecureStorage;
    if (str == "file_export")      return Capability::FileExport;
    if (str == "telemetry")        return Capability::Telemetry;
    if (str == "routing_overlay")  return Capability::RoutingOverlay;
    if (str == "toolbar_items")    return Capability::ToolbarItems;
    if (str == "view_injection")   return Capability::ViewInjection;
    if (str == "ui_theme_mask")    return Capability::UIThemeMask;
    if (str == "event_publishing") return Capability::EventPublishing;
    throw std::invalid_argument("Unknown Capability string: " + str);
}

void to_json(nlohmann::json& j, Capability capability) {
    j = to_string(capability);
}

void from_json(const nlohmann::json& j, Capability& capability) {
    capability = capabilityFromString(j.get<std::string>());
}

// MARK: - ModuleDescriptor

void to_json(nlohmann::json& j, const ModuleDescriptor& descriptor) {
    j = nlohmann::json{
        {"moduleID",    descriptor.moduleID},
        {"displayName", descriptor.displayName},
        {"version",     descriptor.version},
        {"type",        descriptor.type}
    };
}

void from_json(const nlohmann::json& j, ModuleDescriptor& descriptor) {
    j.at("moduleID").get_to(descriptor.moduleID);
    j.at("displayName").get_to(descriptor.displayName);
    j.at("version").get_to(descriptor.version);
    j.at("type").get_to(descriptor.type);
}

// MARK: - ModuleManifest

void to_json(nlohmann::json& j, const ModuleManifest& manifest) {
    j = nlohmann::json{
        {"schemaVersion",        manifest.schemaVersion},
        {"moduleID",             manifest.moduleID},
        {"displayName",          manifest.displayName},
        {"moduleVersion",        manifest.moduleVersion},
        {"moduleType",           manifest.moduleType},
        {"supportedPlatforms",   manifest.supportedPlatforms},
        {"minForsettiVersion",   manifest.minForsettiVersion},
        {"capabilitiesRequested", manifest.capabilitiesRequested},
        {"entryPoint",           manifest.entryPoint}
    };

    if (manifest.maxForsettiVersion.has_value()) {
        j["maxForsettiVersion"] = manifest.maxForsettiVersion.value();
    } else {
        j["maxForsettiVersion"] = nullptr;
    }

    if (manifest.iapProductID.has_value()) {
        j["iapProductID"] = manifest.iapProductID.value();
    } else {
        j["iapProductID"] = nullptr;
    }
}

void from_json(const nlohmann::json& j, ModuleManifest& manifest) {
    j.at("schemaVersion").get_to(manifest.schemaVersion);
    j.at("moduleID").get_to(manifest.moduleID);
    j.at("displayName").get_to(manifest.displayName);
    j.at("moduleVersion").get_to(manifest.moduleVersion);
    j.at("moduleType").get_to(manifest.moduleType);
    j.at("supportedPlatforms").get_to(manifest.supportedPlatforms);
    j.at("minForsettiVersion").get_to(manifest.minForsettiVersion);
    j.at("capabilitiesRequested").get_to(manifest.capabilitiesRequested);
    j.at("entryPoint").get_to(manifest.entryPoint);

    if (j.contains("maxForsettiVersion") && !j.at("maxForsettiVersion").is_null()) {
        manifest.maxForsettiVersion = j.at("maxForsettiVersion").get<SemVer>();
    } else {
        manifest.maxForsettiVersion = std::nullopt;
    }

    if (j.contains("iapProductID") && !j.at("iapProductID").is_null()) {
        manifest.iapProductID = j.at("iapProductID").get<std::string>();
    } else {
        manifest.iapProductID = std::nullopt;
    }
}

} // namespace Forsetti
