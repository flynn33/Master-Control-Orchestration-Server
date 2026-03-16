// Forsetti Framework for Windows
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential. Patent Pending.

#pragma once

#include "ForsettiCore/ForsettiProtocols.h"
#include <string>
#include <unordered_map>
#include <functional>
#include <mutex>
#include <memory>
#include <stdexcept>

namespace Forsetti {

// MARK: - ModuleFactory

using ModuleFactory = std::function<std::unique_ptr<IForsettiModule>()>;

// MARK: - ModuleRegistryError

enum class ModuleRegistryError {
    EntryPointAlreadyRegistered,
    EntryPointNotFound
};

class ModuleRegistryException final : public std::runtime_error {
public:
    explicit ModuleRegistryException(ModuleRegistryError code, const std::string& message)
        : std::runtime_error(message), code_(code) {}

    ModuleRegistryError code() const noexcept { return code_; }

private:
    ModuleRegistryError code_;
};

// MARK: - ModuleRegistry

class ModuleRegistry final {
public:
    ModuleRegistry() = default;

    // Move-only — std::mutex is not movable, so we manually move the data
    // and leave the source with a fresh (default-constructed) mutex.
    ModuleRegistry(ModuleRegistry&& other) noexcept {
        std::lock_guard<std::mutex> lock(other.mutex_);
        factories_ = std::move(other.factories_);
    }
    ModuleRegistry& operator=(ModuleRegistry&& other) noexcept {
        if (this != &other) {
            std::scoped_lock lock(mutex_, other.mutex_);
            factories_ = std::move(other.factories_);
        }
        return *this;
    }
    ModuleRegistry(const ModuleRegistry&) = delete;
    ModuleRegistry& operator=(const ModuleRegistry&) = delete;

    /// Registers a factory for the given entry point. Thread-safe.
    /// Throws ModuleRegistryException if the entry point is already registered.
    void registerModule(const std::string& entryPoint, ModuleFactory factory);

    /// Creates a module instance using the factory registered for the given entry point. Thread-safe.
    /// Throws ModuleRegistryException if no factory is registered for the entry point.
    std::unique_ptr<IForsettiModule> makeModule(const std::string& entryPoint);

    /// Returns true if a factory is registered for the given entry point. Thread-safe.
    bool hasEntryPoint(const std::string& entryPoint) const;

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, ModuleFactory> factories_;
};

} // namespace Forsetti
