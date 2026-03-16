// Forsetti Framework for Windows
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential. Patent Pending.

#include "ForsettiCore/ModuleManager.h"
#include "ForsettiCore/ManifestLoader.h"
#include <filesystem>
#include <algorithm>

namespace Forsetti {

// ---------------------------------------------------------------------------
// ModuleManagerException
// ---------------------------------------------------------------------------

ModuleManagerException::ModuleManagerException(ModuleManagerError error, const std::string& message)
    : std::runtime_error(message.empty() ? "ModuleManagerError" : message)
    , error_(error)
{
}

ModuleManagerError ModuleManagerException::error() const noexcept
{
    return error_;
}

// ---------------------------------------------------------------------------
// ModuleManager — construction
// ---------------------------------------------------------------------------

ModuleManager::ModuleManager(
    ModuleRegistry registry,
    std::shared_ptr<CompatibilityChecker> checker,
    std::shared_ptr<IEntitlementProvider> entitlementProvider,
    std::shared_ptr<IActivationStore> store,
    std::shared_ptr<UISurfaceManager> surfaceManager,
    std::shared_ptr<ForsettiContext> context)
    : registry_(std::move(registry))
    , checker_(std::move(checker))
    , entitlementProvider_(std::move(entitlementProvider))
    , store_(std::move(store))
    , surfaceManager_(std::move(surfaceManager))
    , context_(std::move(context))
{
}

// ---------------------------------------------------------------------------
// Discovery
// ---------------------------------------------------------------------------

void ModuleManager::discoverManifests(const std::string& manifestDirectory)
{
    std::lock_guard<std::mutex> lock(mutex_);

    auto manifests = ManifestLoader::loadManifests(manifestDirectory);
    for (auto& manifest : manifests) {
        manifestsByID_[manifest.moduleID] = std::move(manifest);
    }
}

// ---------------------------------------------------------------------------
// Activation
// ---------------------------------------------------------------------------

void ModuleManager::activateModule(const std::string& moduleID)
{
    std::lock_guard<std::mutex> lock(mutex_);

    // 1. Find manifest
    auto manifestIt = manifestsByID_.find(moduleID);
    if (manifestIt == manifestsByID_.end()) {
        throw ModuleManagerException(
            ModuleManagerError::ModuleNotFound,
            "Module not found: " + moduleID);
    }
    const auto& manifest = manifestIt->second;

    // 2. Compatibility check
    auto report = checker_->checkCompatibility(manifest);
    if (!report.isCompatible()) {
        throw ModuleManagerException(
            ModuleManagerError::IncompatibleModule,
            "Module is incompatible: " + moduleID);
    }

    // 3. Entitlement check — unlocked by moduleID or iapProductID
    bool unlocked = entitlementProvider_->isUnlocked(moduleID);
    if (!unlocked && manifest.iapProductID.has_value()) {
        unlocked = entitlementProvider_->isUnlocked(manifest.iapProductID.value());
    }
    if (!unlocked) {
        throw ModuleManagerException(
            ModuleManagerError::EntitlementRequired,
            "Entitlement required for module: " + moduleID);
    }

    // 4. Already active?
    if (loadedModules_.find(moduleID) != loadedModules_.end()) {
        throw ModuleManagerException(
            ModuleManagerError::AlreadyActive,
            "Module already active: " + moduleID);
    }

    // 5. Create module via registry
    auto module = registry_.makeModule(manifest.entryPoint);
    if (!module) {
        throw ModuleManagerException(
            ModuleManagerError::ModuleNotFound,
            "Registry has no factory for entry point: " + manifest.entryPoint);
    }

    // 6. Route by type
    if (manifest.moduleType == ModuleType::Service) {
        enabledServiceModuleIDs_.insert(moduleID);
        module->start(*context_);
    } else {
        // UI or App — treat both as UI modules
        auto* uiModule = dynamic_cast<IForsettiUIModule*>(module.get());
        if (uiModule) {
            activateUIModule(moduleID, uiModule);
        } else {
            // Fallback: treat as service if dynamic_cast fails
            enabledServiceModuleIDs_.insert(moduleID);
            module->start(*context_);
        }
    }

    // 7. Store loaded module
    loadedModules_[moduleID] = std::move(module);

    // 8. Persist state
    persistState();
}

// ---------------------------------------------------------------------------
// UI Module Activation (private)
// ---------------------------------------------------------------------------

void ModuleManager::activateUIModule(const std::string& moduleID, IForsettiUIModule* uiModule)
{
    // 1. If there is already an active UI module, deactivate it first
    if (activeUIModuleID_.has_value()) {
        const auto& previousID = activeUIModuleID_.value();
        auto prevIt = loadedModules_.find(previousID);
        if (prevIt != loadedModules_.end()) {
            prevIt->second->stop(*context_);
            surfaceManager_->removeModuleContributions(previousID);
        }
        enabledUIModuleIDs_.erase(previousID);
        // Note: we don't remove from loadedModules_ here — full deactivation
        // is only done via deactivateModule(). We just switch the active UI.
        activeUIModuleID_.reset();
    }

    // 2. Get UI contributions from the module
    auto contributions = uiModule->uiContributions();

    // 3. Sanitize — strip themeMask (reserved for framework)
    auto sanitized = sanitizedUIContributions(contributions);

    // 4. Add to surface manager
    surfaceManager_->addModuleContributions(moduleID, sanitized);

    // 5. Rebuild surface state
    surfaceManager_->rebuildSurfaceState();

    // 6. Track as enabled UI module
    enabledUIModuleIDs_.insert(moduleID);

    // 7. Set as active UI module
    activeUIModuleID_ = moduleID;

    // 8. Start the module
    uiModule->start(*context_);
}

// ---------------------------------------------------------------------------
// Deactivation
// ---------------------------------------------------------------------------

void ModuleManager::deactivateModule(const std::string& moduleID)
{
    std::lock_guard<std::mutex> lock(mutex_);

    // 1. Find loaded module
    auto it = loadedModules_.find(moduleID);
    if (it == loadedModules_.end()) {
        throw ModuleManagerException(
            ModuleManagerError::NotActive,
            "Module is not active: " + moduleID);
    }

    // 2. Stop the module
    it->second->stop(*context_);

    // 3. Remove from the appropriate enabled set
    enabledServiceModuleIDs_.erase(moduleID);
    enabledUIModuleIDs_.erase(moduleID);

    // 4. If it was the active UI module, clean up surface contributions
    if (activeUIModuleID_.has_value() && activeUIModuleID_.value() == moduleID) {
        surfaceManager_->removeModuleContributions(moduleID);
        surfaceManager_->rebuildSurfaceState();
        activeUIModuleID_.reset();
    }

    // 5. Remove from loaded modules
    loadedModules_.erase(it);

    // 6. Persist state
    persistState();
}

// ---------------------------------------------------------------------------
// Persisted Activation Restoration
// ---------------------------------------------------------------------------

void ModuleManager::restorePersistedActivation()
{
    auto state = store_->loadState();

    // Restore service modules
    for (const auto& moduleID : state.enabledServiceModuleIDs) {
        try {
            activateModule(moduleID);
        } catch (...) {
            // Silently skip failures during restoration
        }
    }

    // Restore UI modules
    for (const auto& moduleID : state.enabledUIModuleIDs) {
        try {
            activateModule(moduleID);
        } catch (...) {
            // Silently skip failures during restoration
        }
    }
}

// ---------------------------------------------------------------------------
// Getters
// ---------------------------------------------------------------------------

const std::set<std::string>& ModuleManager::enabledServiceModuleIDs() const
{
    return enabledServiceModuleIDs_;
}

const std::set<std::string>& ModuleManager::enabledUIModuleIDs() const
{
    return enabledUIModuleIDs_;
}

const std::optional<std::string>& ModuleManager::activeUIModuleID() const
{
    return activeUIModuleID_;
}

const std::unordered_map<std::string, ModuleManifest>& ModuleManager::manifestsByID() const
{
    return manifestsByID_;
}

bool ModuleManager::isModuleActive(const std::string& moduleID) const
{
    return loadedModules_.find(moduleID) != loadedModules_.end();
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void ModuleManager::persistState()
{
    ActivationState state;
    state.enabledServiceModuleIDs = enabledServiceModuleIDs_;
    state.enabledUIModuleIDs = enabledUIModuleIDs_;
    state.selectedUIModuleID = activeUIModuleID_;

    store_->saveState(state);
}

UIContributions ModuleManager::sanitizedUIContributions(const UIContributions& original) const
{
    UIContributions sanitized = original;
    sanitized.themeMask = std::nullopt;
    return sanitized;
}

} // namespace Forsetti
