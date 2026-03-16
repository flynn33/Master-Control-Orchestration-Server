// Forsetti Framework for Windows
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential. Patent Pending.

#include "ForsettiCore/UIModels.h"

#include <stdexcept>

namespace Forsetti {

// ─────────────────────────────────────────────
// ThemeToken
// ─────────────────────────────────────────────

void to_json(nlohmann::json& j, const ThemeToken& t) {
    j = nlohmann::json{
        {"key", t.key},
        {"value", t.value}
    };
}

void from_json(const nlohmann::json& j, ThemeToken& t) {
    j.at("key").get_to(t.key);
    j.at("value").get_to(t.value);
}

// ─────────────────────────────────────────────
// ThemeMask
// ─────────────────────────────────────────────

void to_json(nlohmann::json& j, const ThemeMask& t) {
    j = nlohmann::json{
        {"tokens", t.tokens}
    };
}

void from_json(const nlohmann::json& j, ThemeMask& t) {
    j.at("tokens").get_to(t.tokens);
}

// ─────────────────────────────────────────────
// OverlayPresentation
// ─────────────────────────────────────────────

void to_json(nlohmann::json& j, const OverlayPresentation& p) {
    switch (p) {
        case OverlayPresentation::Sheet:
            j = "sheet";
            break;
        case OverlayPresentation::FullScreen:
            j = "fullScreen";
            break;
        case OverlayPresentation::Popover:
            j = "popover";
            break;
    }
}

void from_json(const nlohmann::json& j, OverlayPresentation& p) {
    const auto& s = j.get<std::string>();
    if (s == "sheet") {
        p = OverlayPresentation::Sheet;
    } else if (s == "fullScreen") {
        p = OverlayPresentation::FullScreen;
    } else if (s == "popover") {
        p = OverlayPresentation::Popover;
    } else {
        throw std::invalid_argument("Unknown OverlayPresentation value: " + s);
    }
}

// ─────────────────────────────────────────────
// BaseDestinationRef
// ─────────────────────────────────────────────

void to_json(nlohmann::json& j, const BaseDestinationRef& r) {
    j = nlohmann::json{
        {"destinationID", r.destinationID}
    };
}

void from_json(const nlohmann::json& j, BaseDestinationRef& r) {
    j.at("destinationID").get_to(r.destinationID);
}

// ─────────────────────────────────────────────
// NavigationPointer
// ─────────────────────────────────────────────

void to_json(nlohmann::json& j, const NavigationPointer& n) {
    j = nlohmann::json{
        {"pointerID", n.pointerID},
        {"label", n.label},
        {"baseDestinationID", n.baseDestinationID}
    };
}

void from_json(const nlohmann::json& j, NavigationPointer& n) {
    j.at("pointerID").get_to(n.pointerID);
    j.at("label").get_to(n.label);
    j.at("baseDestinationID").get_to(n.baseDestinationID);
}

// ─────────────────────────────────────────────
// OverlayDestination variant types
// ─────────────────────────────────────────────

void to_json(nlohmann::json& j, const BaseOverlayDestination& d) {
    j = nlohmann::json{
        {"type", "base"},
        {"destinationID", d.destinationID}
    };
}

void from_json(const nlohmann::json& j, BaseOverlayDestination& d) {
    j.at("destinationID").get_to(d.destinationID);
}

void to_json(nlohmann::json& j, const ModuleOverlayDestination& d) {
    j = nlohmann::json{
        {"type", "moduleOverlay"},
        {"moduleID", d.moduleID},
        {"viewID", d.viewID}
    };
}

void from_json(const nlohmann::json& j, ModuleOverlayDestination& d) {
    j.at("moduleID").get_to(d.moduleID);
    j.at("viewID").get_to(d.viewID);
}

void to_json(nlohmann::json& j, const OverlayDestination& d) {
    std::visit([&j](const auto& val) {
        to_json(j, val);
    }, d);
}

void from_json(const nlohmann::json& j, OverlayDestination& d) {
    const auto& type = j.at("type").get<std::string>();
    if (type == "base") {
        BaseOverlayDestination dest;
        from_json(j, dest);
        d = dest;
    } else if (type == "moduleOverlay") {
        ModuleOverlayDestination dest;
        from_json(j, dest);
        d = dest;
    } else {
        throw std::invalid_argument("Unknown OverlayDestination type: " + type);
    }
}

// ─────────────────────────────────────────────
// OverlayRoute
// ─────────────────────────────────────────────

void to_json(nlohmann::json& j, const OverlayRoute& r) {
    j = nlohmann::json{
        {"routeID", r.routeID},
        {"label", r.label},
        {"presentation", r.presentation},
        {"destination", r.destination}
    };
}

void from_json(const nlohmann::json& j, OverlayRoute& r) {
    j.at("routeID").get_to(r.routeID);
    j.at("label").get_to(r.label);
    j.at("presentation").get_to(r.presentation);
    j.at("destination").get_to(r.destination);
}

// ─────────────────────────────────────────────
// OverlaySchema
// ─────────────────────────────────────────────

void to_json(nlohmann::json& j, const OverlaySchema& s) {
    j = nlohmann::json{
        {"navigationPointers", s.navigationPointers},
        {"overlayRoutes", s.overlayRoutes}
    };
}

void from_json(const nlohmann::json& j, OverlaySchema& s) {
    j.at("navigationPointers").get_to(s.navigationPointers);
    j.at("overlayRoutes").get_to(s.overlayRoutes);
}

// ─────────────────────────────────────────────
// ToolbarAction variant types
// ─────────────────────────────────────────────

void to_json(nlohmann::json& j, const NavigateAction& a) {
    j = nlohmann::json{
        {"type", "navigate"},
        {"destinationID", a.destinationID}
    };
}

void from_json(const nlohmann::json& j, NavigateAction& a) {
    j.at("destinationID").get_to(a.destinationID);
}

void to_json(nlohmann::json& j, const OpenOverlayAction& a) {
    j = nlohmann::json{
        {"type", "openOverlay"},
        {"routeID", a.routeID}
    };
}

void from_json(const nlohmann::json& j, OpenOverlayAction& a) {
    j.at("routeID").get_to(a.routeID);
}

void to_json(nlohmann::json& j, const PublishEventAction& a) {
    j = nlohmann::json{
        {"type", "publishEvent"},
        {"eventType", a.eventType}
    };
}

void from_json(const nlohmann::json& j, PublishEventAction& a) {
    j.at("eventType").get_to(a.eventType);
}

void to_json(nlohmann::json& j, const ToolbarAction& a) {
    std::visit([&j](const auto& val) {
        to_json(j, val);
    }, a);
}

void from_json(const nlohmann::json& j, ToolbarAction& a) {
    const auto& type = j.at("type").get<std::string>();
    if (type == "navigate") {
        NavigateAction action;
        from_json(j, action);
        a = action;
    } else if (type == "openOverlay") {
        OpenOverlayAction action;
        from_json(j, action);
        a = action;
    } else if (type == "publishEvent") {
        PublishEventAction action;
        from_json(j, action);
        a = action;
    } else {
        throw std::invalid_argument("Unknown ToolbarAction type: " + type);
    }
}

// ─────────────────────────────────────────────
// ToolbarItemDescriptor
// ─────────────────────────────────────────────

void to_json(nlohmann::json& j, const ToolbarItemDescriptor& d) {
    j = nlohmann::json{
        {"itemID", d.itemID},
        {"title", d.title},
        {"systemImageName", d.systemImageName},
        {"action", d.action}
    };
}

void from_json(const nlohmann::json& j, ToolbarItemDescriptor& d) {
    j.at("itemID").get_to(d.itemID);
    j.at("title").get_to(d.title);
    j.at("systemImageName").get_to(d.systemImageName);
    j.at("action").get_to(d.action);
}

// ─────────────────────────────────────────────
// ViewInjectionDescriptor
// ─────────────────────────────────────────────

void to_json(nlohmann::json& j, const ViewInjectionDescriptor& d) {
    j = nlohmann::json{
        {"injectionID", d.injectionID},
        {"slot", d.slot},
        {"viewID", d.viewID},
        {"priority", d.priority}
    };
}

void from_json(const nlohmann::json& j, ViewInjectionDescriptor& d) {
    j.at("injectionID").get_to(d.injectionID);
    j.at("slot").get_to(d.slot);
    j.at("viewID").get_to(d.viewID);
    d.priority = j.value("priority", 0);
}

// ─────────────────────────────────────────────
// UIContributions
// ─────────────────────────────────────────────

void to_json(nlohmann::json& j, const UIContributions& c) {
    j = nlohmann::json{
        {"toolbarItems", c.toolbarItems},
        {"viewInjections", c.viewInjections}
    };

    if (c.themeMask.has_value()) {
        j["themeMask"] = c.themeMask.value();
    }

    if (c.overlaySchema.has_value()) {
        j["overlaySchema"] = c.overlaySchema.value();
    }
}

void from_json(const nlohmann::json& j, UIContributions& c) {
    j.at("toolbarItems").get_to(c.toolbarItems);
    j.at("viewInjections").get_to(c.viewInjections);

    if (j.contains("themeMask") && !j["themeMask"].is_null()) {
        c.themeMask = j["themeMask"].get<ThemeMask>();
    } else {
        c.themeMask = std::nullopt;
    }

    if (j.contains("overlaySchema") && !j["overlaySchema"].is_null()) {
        c.overlaySchema = j["overlaySchema"].get<OverlaySchema>();
    } else {
        c.overlaySchema = std::nullopt;
    }
}

} // namespace Forsetti
