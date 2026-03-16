// Forsetti Framework for Windows
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential. Patent Pending.

#include "ForsettiCore/ForsettiEventBus.h"

#include <vector>
#include <utility>

namespace Forsetti {

// --- SubscriptionToken ---

SubscriptionToken::SubscriptionToken(IForsettiEventBus* bus, uint64_t id)
    : bus_(bus), id_(id), active_(true) {}

SubscriptionToken::~SubscriptionToken() {
    cancel();
}

SubscriptionToken::SubscriptionToken(SubscriptionToken&& other) noexcept {
    std::lock_guard<std::mutex> lock(other.mutex_);
    bus_ = other.bus_;
    id_ = other.id_;
    active_ = other.active_;
    other.bus_ = nullptr;
    other.id_ = 0;
    other.active_ = false;
}

SubscriptionToken& SubscriptionToken::operator=(SubscriptionToken&& other) noexcept {
    if (this != &other) {
        cancel();

        std::lock_guard<std::mutex> lock(other.mutex_);
        bus_ = other.bus_;
        id_ = other.id_;
        active_ = other.active_;
        other.bus_ = nullptr;
        other.id_ = 0;
        other.active_ = false;
    }
    return *this;
}

void SubscriptionToken::cancel() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (active_ && bus_) {
        bus_->unsubscribe(id_);
        active_ = false;
        bus_ = nullptr;
    }
}

// --- InMemoryEventBus ---

void InMemoryEventBus::publish(const ForsettiEvent& event) {
    std::vector<std::function<void(const ForsettiEvent&)>> handlersCopy;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = handlers_.find(event.type);
        if (it != handlers_.end()) {
            handlersCopy.reserve(it->second.size());
            for (const auto& [id, handler] : it->second) {
                handlersCopy.push_back(handler);
            }
        }
    }

    for (const auto& handler : handlersCopy) {
        handler(event);
    }
}

uint64_t InMemoryEventBus::subscribe(const std::string& eventType,
                                     std::function<void(const ForsettiEvent&)> handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    uint64_t id = nextTokenID_++;
    handlers_[eventType][id] = std::move(handler);
    return id;
}

void InMemoryEventBus::unsubscribe(uint64_t tokenID) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [eventType, handlerMap] : handlers_) {
        auto it = handlerMap.find(tokenID);
        if (it != handlerMap.end()) {
            handlerMap.erase(it);
            return;
        }
    }
}

SubscriptionToken InMemoryEventBus::makeToken(uint64_t id) {
    return SubscriptionToken(this, id);
}

} // namespace Forsetti
