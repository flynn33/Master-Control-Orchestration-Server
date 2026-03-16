// Forsetti Framework for Windows
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential. Patent Pending.

#pragma once
#include "ForsettiCore/UIModels.h"
#include <vector>
#include <map>
#include <string>
#include <optional>
#include <functional>
#include <algorithm>

namespace Forsetti {

class UISurfaceManager final {
public:
    // Module contribution management
    void addModuleContributions(const std::string& moduleID,
                                const UIContributions& contributions);
    void removeModuleContributions(const std::string& moduleID);

    // Rebuild the merged surface state from all module contributions
    void rebuildSurfaceState();

    // Register a change callback
    void onChanged(std::function<void()> callback);

    // Getters for current merged state
    [[nodiscard]] const std::optional<ThemeMask>& currentThemeMask() const;
    [[nodiscard]] const std::vector<ToolbarItemDescriptor>& currentToolbarItems() const;
    [[nodiscard]] const std::map<std::string, std::vector<ViewInjectionDescriptor>>& currentViewInjectionsBySlot() const;
    [[nodiscard]] const std::optional<OverlaySchema>& currentOverlaySchema() const;

private:
    // Per-module contributions, keyed by module ID
    std::map<std::string, UIContributions> contributionsByModule_;

    // Cached merged state
    std::optional<ThemeMask> currentThemeMask_;
    std::vector<ToolbarItemDescriptor> currentToolbarItems_;
    std::map<std::string, std::vector<ViewInjectionDescriptor>> currentViewInjectionsBySlot_;
    std::optional<OverlaySchema> currentOverlaySchema_;

    // Change notification
    std::vector<std::function<void()>> changeCallbacks_;
    void notifyChanged();
};

} // namespace Forsetti
