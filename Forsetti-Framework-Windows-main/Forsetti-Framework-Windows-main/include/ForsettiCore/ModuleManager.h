// Forsetti Framework for Windows
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential. Patent Pending.

#pragma once
#include "ForsettiCore/ForsettiProtocols.h"
#include "ForsettiCore/ModuleModels.h"
#include "ForsettiCore/UIModels.h"
#include "ForsettiCore/ForsettiContext.h"
#include "ForsettiCore/ModuleRegistry.h"
#include "ForsettiCore/ActivationStore.h"
#include "ForsettiCore/CapabilityPolicy.h"
#include "ForsettiCore/CompatibilityChecker.h"
#include "ForsettiCore/UISurfaceManager.h"
#include <string>
#include <set>
#include <optional>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <vector>
#include <stdexcept>

namespace Forsetti {

// ---------------------------------------------------------------------------
// Error types
// ---------------------------------------------------------------------------

enum class ModuleManagerError {
    ModuleNotFound,
    IncompatibleModule,
    EntitlementRequired,
    AlreadyActive,
    NotActive
};

class ModuleManagerException final : public std::runtime_error {
public:
    explicit ModuleManagerException(ModuleManagerError error, const std::string& message = "");

    [[nodiscard]] ModuleManagerError error() const noexcept;

private:
    ModuleManagerError error_;
};

// ---------------------------------------------------------------------------
// ModuleManager — core activation logic
// ---------------------------------------------------------------------------

class ModuleManager final {
public:
    ModuleManager(
        ModuleRegistry registry,
        std::shared_ptr<CompatibilityChecker> checker,
        std::shared_ptr<IEntitlementProvider> entitlementProvider,
        std::shared_ptr<IActivationStore> store,
        std::shared_ptr<UISurfaceManager> surfaceManager,
        std::shared_ptr<ForsettiContext> context
    );

    // Discovery
    void discoverManifests(const std::string& manifestDirectory);

    // Activation / Deactivation
    void activateModule(const std::string& moduleID);
    void deactivateModule(const std::string& moduleID);

    // Persisted state restoration
    void restorePersistedActivation();

    // Getters
    [[nodiscard]] const std::set<std::string>& enabledServiceModuleIDs() const;
    [[nodiscard]] const std::set<std::string>& enabledUIModuleIDs() const;
    [[nodiscard]] const std::optional<std::string>& activeUIModuleID() const;
    [[nodiscard]] const std::unordered_map<std::string, ModuleManifest>& manifestsByID() const;
    [[nodiscard]] bool isModuleActive(const std::string& moduleID) const;

private:
    // UI-specific activation
    void activateUIModule(const std::string& moduleID, IForsettiUIModule* uiModule);

    // State persistence
    void persistState();

    // Sanitisation — strips themeMask (reserved for framework use)
    [[nodiscard]] UIContributions sanitizedUIContributions(const UIContributions& original) const;

    // Dependencies
    ModuleRegistry registry_;
    std::shared_ptr<CompatibilityChecker> checker_;
    std::shared_ptr<IEntitlementProvider> entitlementProvider_;
    std::shared_ptr<IActivationStore> store_;
    std::shared_ptr<UISurfaceManager> surfaceManager_;
    std::shared_ptr<ForsettiContext> context_;

    // Module state
    std::unordered_map<std::string, ModuleManifest> manifestsByID_;
    std::unordered_map<std::string, std::unique_ptr<IForsettiModule>> loadedModules_;
    std::set<std::string> enabledServiceModuleIDs_;
    std::set<std::string> enabledUIModuleIDs_;
    std::optional<std::string> activeUIModuleID_;

    mutable std::mutex mutex_;
};

} // namespace Forsetti
