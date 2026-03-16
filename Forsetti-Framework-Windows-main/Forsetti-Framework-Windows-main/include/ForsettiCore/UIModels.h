// Forsetti Framework for Windows
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential. Patent Pending.

#pragma once

#include <string>
#include <vector>
#include <optional>
#include <variant>
#include <nlohmann/json.hpp>

namespace Forsetti {

// ─────────────────────────────────────────────
// 1. ThemeToken
// ─────────────────────────────────────────────

struct ThemeToken final {
    std::string key;
    std::string value;
};

void to_json(nlohmann::json& j, const ThemeToken& t);
void from_json(const nlohmann::json& j, ThemeToken& t);

// ─────────────────────────────────────────────
// 2. ThemeMask
// ─────────────────────────────────────────────

struct ThemeMask final {
    std::vector<ThemeToken> tokens;
};

void to_json(nlohmann::json& j, const ThemeMask& t);
void from_json(const nlohmann::json& j, ThemeMask& t);

// ─────────────────────────────────────────────
// 3. OverlayPresentation
// ─────────────────────────────────────────────

enum class OverlayPresentation {
    Sheet,
    FullScreen,
    Popover
};

void to_json(nlohmann::json& j, const OverlayPresentation& p);
void from_json(const nlohmann::json& j, OverlayPresentation& p);

// ─────────────────────────────────────────────
// 4. BaseDestinationRef
// ─────────────────────────────────────────────

struct BaseDestinationRef final {
    std::string destinationID;
};

void to_json(nlohmann::json& j, const BaseDestinationRef& r);
void from_json(const nlohmann::json& j, BaseDestinationRef& r);

// ─────────────────────────────────────────────
// 5. NavigationPointer
// ─────────────────────────────────────────────

struct NavigationPointer final {
    std::string pointerID;
    std::string label;
    std::string baseDestinationID;
};

void to_json(nlohmann::json& j, const NavigationPointer& n);
void from_json(const nlohmann::json& j, NavigationPointer& n);

// ─────────────────────────────────────────────
// 6. OverlayDestination (variant)
// ─────────────────────────────────────────────

struct BaseOverlayDestination final {
    std::string destinationID;
};

struct ModuleOverlayDestination final {
    std::string moduleID;
    std::string viewID;
};

using OverlayDestination = std::variant<BaseOverlayDestination, ModuleOverlayDestination>;

void to_json(nlohmann::json& j, const BaseOverlayDestination& d);
void from_json(const nlohmann::json& j, BaseOverlayDestination& d);

void to_json(nlohmann::json& j, const ModuleOverlayDestination& d);
void from_json(const nlohmann::json& j, ModuleOverlayDestination& d);

void to_json(nlohmann::json& j, const OverlayDestination& d);
void from_json(const nlohmann::json& j, OverlayDestination& d);

// ─────────────────────────────────────────────
// 7. OverlayRoute
// ─────────────────────────────────────────────

struct OverlayRoute final {
    std::string routeID;
    std::string label;
    OverlayPresentation presentation;
    OverlayDestination destination;
};

void to_json(nlohmann::json& j, const OverlayRoute& r);
void from_json(const nlohmann::json& j, OverlayRoute& r);

// ─────────────────────────────────────────────
// 8. OverlaySchema
// ─────────────────────────────────────────────

struct OverlaySchema final {
    std::vector<NavigationPointer> navigationPointers;
    std::vector<OverlayRoute> overlayRoutes;
};

void to_json(nlohmann::json& j, const OverlaySchema& s);
void from_json(const nlohmann::json& j, OverlaySchema& s);

// ─────────────────────────────────────────────
// 9. ToolbarAction (variant)
// ─────────────────────────────────────────────

struct NavigateAction final {
    std::string destinationID;
};

struct OpenOverlayAction final {
    std::string routeID;
};

struct PublishEventAction final {
    std::string eventType;
};

using ToolbarAction = std::variant<NavigateAction, OpenOverlayAction, PublishEventAction>;

void to_json(nlohmann::json& j, const NavigateAction& a);
void from_json(const nlohmann::json& j, NavigateAction& a);

void to_json(nlohmann::json& j, const OpenOverlayAction& a);
void from_json(const nlohmann::json& j, OpenOverlayAction& a);

void to_json(nlohmann::json& j, const PublishEventAction& a);
void from_json(const nlohmann::json& j, PublishEventAction& a);

void to_json(nlohmann::json& j, const ToolbarAction& a);
void from_json(const nlohmann::json& j, ToolbarAction& a);

// ─────────────────────────────────────────────
// 10. ToolbarItemDescriptor
// ─────────────────────────────────────────────

struct ToolbarItemDescriptor final {
    std::string itemID;
    std::string title;
    std::string systemImageName;
    ToolbarAction action;
};

void to_json(nlohmann::json& j, const ToolbarItemDescriptor& d);
void from_json(const nlohmann::json& j, ToolbarItemDescriptor& d);

// ─────────────────────────────────────────────
// 11. ViewInjectionDescriptor
// ─────────────────────────────────────────────

struct ViewInjectionDescriptor final {
    std::string injectionID;
    std::string slot;
    std::string viewID;
    int priority = 0;
};

void to_json(nlohmann::json& j, const ViewInjectionDescriptor& d);
void from_json(const nlohmann::json& j, ViewInjectionDescriptor& d);

// ─────────────────────────────────────────────
// 12. UIContributions
// ─────────────────────────────────────────────

struct UIContributions final {
    std::optional<ThemeMask> themeMask;
    std::vector<ToolbarItemDescriptor> toolbarItems;
    std::vector<ViewInjectionDescriptor> viewInjections;
    std::optional<OverlaySchema> overlaySchema;
};

void to_json(nlohmann::json& j, const UIContributions& c);
void from_json(const nlohmann::json& j, UIContributions& c);

} // namespace Forsetti
