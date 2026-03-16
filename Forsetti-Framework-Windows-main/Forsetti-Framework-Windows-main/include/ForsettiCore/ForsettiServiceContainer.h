// Forsetti Framework for Windows
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential. Patent Pending.

#pragma once

#include <any>
#include <typeindex>
#include <unordered_map>
#include <mutex>
#include <memory>

namespace Forsetti {

class IServiceProvider {
public:
    virtual std::any resolveAny(const std::type_index& type) const = 0;

    template<typename T>
    std::shared_ptr<T> resolve() const {
        auto result = resolveAny(std::type_index(typeid(T)));
        if (!result.has_value()) {
            return nullptr;
        }
        return std::any_cast<std::shared_ptr<T>>(result);
    }

    virtual ~IServiceProvider() = default;
};

class ServiceContainer final : public IServiceProvider {
public:
    template<typename T>
    void registerService(std::shared_ptr<T> service) {
        std::lock_guard<std::mutex> lock(mutex_);
        services_[std::type_index(typeid(T))] = std::move(service);
    }

    std::any resolveAny(const std::type_index& type) const override;

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::type_index, std::any> services_;
};

} // namespace Forsetti
