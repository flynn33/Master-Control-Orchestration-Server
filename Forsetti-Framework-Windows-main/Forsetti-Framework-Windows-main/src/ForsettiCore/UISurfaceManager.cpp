// Forsetti Framework for Windows
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential. Patent Pending.

#include "ForsettiCore/UISurfaceManager.h"
#include <set>

namespace Forsetti {

// ---------------------------------------------------------------------------
// addModuleContributions
// ---------------------------------------------------------------------------
void UISurfaceManager::addModuleContributions(const std::string& moduleID,
                                               const UIContributions& contributions) {
    contributionsByModule_[moduleID] = contributions;
}

// ---------------------------------------------------------------------------
// removeModuleContributions
// ---------------------------------------------------------------------------
void UISurfaceManager::removeModuleContributions(const std::string& moduleID) {
    contributionsByModule_.erase(moduleID);
}

// ---------------------------------------------------------------------------
// rebuildSurfaceState — merge all module contributions into cached state
// ---------------------------------------------------------------------------
void UISurfaceManager::rebuildSurfaceState() {
    // Clear all cached state
    currentThemeMask_.reset();
    currentToolbarItems_.clear();
    currentViewInjectionsBySlot_.clear();
    currentOverlaySchema_.reset();

    // Track seen pointer/route IDs for deduplication
    std::set<std::string> seenPointerIDs;
    std::set<std::string> seenRouteIDs;

    // Composite overlay schema built incrementally
    OverlaySchema compositeOverlay;
    bool hasOverlayContent = false;

    // Iterate in module ID order (std::map is sorted)
    for (const auto& [moduleID, contributions] : contributionsByModule_) {

        // Theme mask: last module with a theme wins
        if (contributions.themeMask.has_value()) {
            currentThemeMask_ = contributions.themeMask;
        }

        // Toolbar items: concatenate from all modules
        currentToolbarItems_.insert(currentToolbarItems_.end(),
                                    contributions.toolbarItems.begin(),
                                    contributions.toolbarItems.end());

        // View injections: group by slot
        for (const auto& injection : contributions.viewInjections) {
            currentViewInjectionsBySlot_[injection.slot].push_back(injection);
        }

        // Overlay schema: merge with deduplication
        if (contributions.overlaySchema.has_value()) {
            const auto& schema = contributions.overlaySchema.value();
            hasOverlayContent = true;

            // Merge navigation pointers, dedupe by pointerID
            for (const auto& pointer : schema.navigationPointers) {
                if (seenPointerIDs.insert(pointer.pointerID).second) {
                    compositeOverlay.navigationPointers.push_back(pointer);
                }
            }

            // Merge overlay routes, dedupe by routeID
            for (const auto& route : schema.overlayRoutes) {
                if (seenRouteIDs.insert(route.routeID).second) {
                    compositeOverlay.overlayRoutes.push_back(route);
                }
            }
        }
    }

    // Sort each slot's view injections by priority descending
    for (auto& [slot, injections] : currentViewInjectionsBySlot_) {
        std::sort(injections.begin(), injections.end(),
                  [](const ViewInjectionDescriptor& a, const ViewInjectionDescriptor& b) {
                      return a.priority > b.priority;
                  });
    }

    // Store the composite overlay schema if any module contributed
    if (hasOverlayContent) {
        currentOverlaySchema_ = std::move(compositeOverlay);
    }

    notifyChanged();
}

// ---------------------------------------------------------------------------
// onChanged — register a change callback
// ---------------------------------------------------------------------------
void UISurfaceManager::onChanged(std::function<void()> callback) {
    changeCallbacks_.push_back(std::move(callback));
}

// ---------------------------------------------------------------------------
// Getters
// ---------------------------------------------------------------------------
const std::optional<ThemeMask>& UISurfaceManager::currentThemeMask() const {
    return currentThemeMask_;
}

const std::vector<ToolbarItemDescriptor>& UISurfaceManager::currentToolbarItems() const {
    return currentToolbarItems_;
}

const std::map<std::string, std::vector<ViewInjectionDescriptor>>&
UISurfaceManager::currentViewInjectionsBySlot() const {
    return currentViewInjectionsBySlot_;
}

const std::optional<OverlaySchema>& UISurfaceManager::currentOverlaySchema() const {
    return currentOverlaySchema_;
}

// ---------------------------------------------------------------------------
// notifyChanged — fire all registered callbacks
// ---------------------------------------------------------------------------
void UISurfaceManager::notifyChanged() {
    for (const auto& callback : changeCallbacks_) {
        callback();
    }
}

} // namespace Forsetti
