// Forsetti Framework for Windows
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential. Patent Pending.

#include "ForsettiCore/ModuleRegistry.h"

namespace Forsetti {

// MARK: - ModuleRegistry

void ModuleRegistry::registerModule(const std::string& entryPoint, ModuleFactory factory) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (factories_.count(entryPoint) > 0) {
        throw ModuleRegistryException(
            ModuleRegistryError::EntryPointAlreadyRegistered,
            "Entry point already registered: " + entryPoint
        );
    }

    factories_.emplace(entryPoint, std::move(factory));
}

std::unique_ptr<IForsettiModule> ModuleRegistry::makeModule(const std::string& entryPoint) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = factories_.find(entryPoint);
    if (it == factories_.end()) {
        throw ModuleRegistryException(
            ModuleRegistryError::EntryPointNotFound,
            "Entry point not found: " + entryPoint
        );
    }

    return it->second();
}

bool ModuleRegistry::hasEntryPoint(const std::string& entryPoint) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return factories_.count(entryPoint) > 0;
}

} // namespace Forsetti
