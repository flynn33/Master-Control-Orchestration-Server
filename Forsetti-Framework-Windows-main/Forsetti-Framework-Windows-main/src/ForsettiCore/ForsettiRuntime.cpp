// Forsetti Framework for Windows
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential. Patent Pending.

#include "ForsettiCore/ForsettiRuntime.h"
#include <vector>
#include <string>

namespace Forsetti {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

ForsettiRuntime::ForsettiRuntime(
    std::unique_ptr<ModuleManager> moduleManager,
    std::shared_ptr<IEntitlementProvider> entitlementProvider,
    std::shared_ptr<IForsettiEventBus> eventBus,
    std::string manifestDirectory)
    : moduleManager_(std::move(moduleManager))
    , entitlementProvider_(std::move(entitlementProvider))
    , eventBus_(std::move(eventBus))
    , manifestDirectory_(std::move(manifestDirectory))
{
}

// ---------------------------------------------------------------------------
// boot
// ---------------------------------------------------------------------------

void ForsettiRuntime::boot()
{
    // 1. Discover manifests from the configured directory
    moduleManager_->discoverManifests(manifestDirectory_);

    // 2. Refresh entitlements to get current state
    entitlementProvider_->refreshEntitlements();

    // 3. Start entitlement observation — register a callback that reconciles
    //    active modules whenever entitlements change.
    entitlementProvider_->onEntitlementsChanged(
        [this]() {
            reconcileActiveModulesWithEntitlements();
        });

    // 4. Restore previously persisted module activations
    moduleManager_->restorePersistedActivation();

    // 5. Mark as booted
    isBooted_.store(true, std::memory_order_release);
}

// ---------------------------------------------------------------------------
// shutdown
// ---------------------------------------------------------------------------

void ForsettiRuntime::shutdown()
{
    // 1. Stop entitlement observation (best-effort — set booted flag first
    //    so callback can check; provider may also support unregistering)
    isBooted_.store(false, std::memory_order_release);

    // 2. Deactivate all active modules.
    //    Take copies of the ID sets because deactivation mutates them.
    std::vector<std::string> serviceIDs(
        moduleManager_->enabledServiceModuleIDs().begin(),
        moduleManager_->enabledServiceModuleIDs().end());

    std::vector<std::string> uiIDs(
        moduleManager_->enabledUIModuleIDs().begin(),
        moduleManager_->enabledUIModuleIDs().end());

    // Deactivate services first
    for (const auto& moduleID : serviceIDs) {
        try {
            moduleManager_->deactivateModule(moduleID);
        } catch (...) {
            // Best-effort during shutdown
        }
    }

    // Then deactivate UI modules
    for (const auto& moduleID : uiIDs) {
        try {
            moduleManager_->deactivateModule(moduleID);
        } catch (...) {
            // Best-effort during shutdown
        }
    }

    // Note: we intentionally do NOT persist state on shutdown.
    // This matches the Swift behavior — the persisted state reflects the
    // user's desired configuration, not the shutdown-cleared state.
}

// ---------------------------------------------------------------------------
// reconcileActiveModulesWithEntitlements
// ---------------------------------------------------------------------------

void ForsettiRuntime::reconcileActiveModulesWithEntitlements()
{
    // Collect all currently active module IDs
    std::vector<std::string> activeModuleIDs;

    for (const auto& id : moduleManager_->enabledServiceModuleIDs()) {
        activeModuleIDs.push_back(id);
    }
    for (const auto& id : moduleManager_->enabledUIModuleIDs()) {
        activeModuleIDs.push_back(id);
    }

    // Determine which modules are no longer entitled
    std::vector<std::string> modulesToDeactivate;

    const auto& manifests = moduleManager_->manifestsByID();

    for (const auto& moduleID : activeModuleIDs) {
        bool unlocked = entitlementProvider_->isUnlocked(moduleID);

        // Also check by iapProductID if the manifest has one
        if (!unlocked) {
            auto manifestIt = manifests.find(moduleID);
            if (manifestIt != manifests.end() &&
                manifestIt->second.iapProductID.has_value()) {
                unlocked = entitlementProvider_->isUnlocked(
                    manifestIt->second.iapProductID.value());
            }
        }

        if (!unlocked) {
            modulesToDeactivate.push_back(moduleID);
        }
    }

    // Deactivate modules that lost their entitlement
    for (const auto& moduleID : modulesToDeactivate) {
        try {
            moduleManager_->deactivateModule(moduleID);
        } catch (...) {
            // Best-effort — module may have already been deactivated
        }
    }
}

// ---------------------------------------------------------------------------
// Module activation / deactivation (delegation)
// ---------------------------------------------------------------------------

void ForsettiRuntime::activateModule(const std::string& moduleID)
{
    moduleManager_->activateModule(moduleID);
}

void ForsettiRuntime::deactivateModule(const std::string& moduleID)
{
    moduleManager_->deactivateModule(moduleID);
}

// ---------------------------------------------------------------------------
// Getters
// ---------------------------------------------------------------------------

bool ForsettiRuntime::isBooted() const
{
    return isBooted_.load(std::memory_order_acquire);
}

ModuleManager& ForsettiRuntime::moduleManager()
{
    return *moduleManager_;
}

const ModuleManager& ForsettiRuntime::moduleManager() const
{
    return *moduleManager_;
}

} // namespace Forsetti
