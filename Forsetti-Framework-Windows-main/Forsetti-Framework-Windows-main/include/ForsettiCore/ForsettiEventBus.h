// Forsetti Framework for Windows
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential. Patent Pending.

#pragma once

#include <string>
#include <map>
#include <functional>
#include <mutex>
#include <optional>
#include <chrono>
#include <cstdint>
#include <memory>

namespace Forsetti {

struct ForsettiEvent final {
    std::string type;
    std::map<std::string, std::string> payload;
    std::optional<std::string> sourceModuleID;
    std::chrono::system_clock::time_point timestamp = std::chrono::system_clock::now();
};

class IForsettiEventBus;

class SubscriptionToken final {
public:
    SubscriptionToken(IForsettiEventBus* bus, uint64_t id);
    ~SubscriptionToken();

    // Move-only
    SubscriptionToken(const SubscriptionToken&) = delete;
    SubscriptionToken& operator=(const SubscriptionToken&) = delete;
    SubscriptionToken(SubscriptionToken&& other) noexcept;
    SubscriptionToken& operator=(SubscriptionToken&& other) noexcept;

    void cancel();

private:
    std::mutex mutex_;
    IForsettiEventBus* bus_ = nullptr;
    uint64_t id_ = 0;
    bool active_ = false;
};

class IForsettiEventBus {
public:
    virtual void publish(const ForsettiEvent& event) = 0;
    virtual uint64_t subscribe(const std::string& eventType,
                               std::function<void(const ForsettiEvent&)> handler) = 0;
    virtual void unsubscribe(uint64_t tokenID) = 0;
    virtual ~IForsettiEventBus() = default;
};

class InMemoryEventBus final : public IForsettiEventBus {
public:
    void publish(const ForsettiEvent& event) override;
    uint64_t subscribe(const std::string& eventType,
                       std::function<void(const ForsettiEvent&)> handler) override;
    void unsubscribe(uint64_t tokenID) override;

    SubscriptionToken makeToken(uint64_t id);

private:
    mutable std::mutex mutex_;
    std::map<std::string, std::map<uint64_t, std::function<void(const ForsettiEvent&)>>> handlers_;
    uint64_t nextTokenID_ = 1;
};

} // namespace Forsetti
