// Forsetti Framework for Windows
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential. Patent Pending.

#include "ForsettiCore/ForsettiContext.h"
#include <utility>

namespace Forsetti {

// ---------------------------------------------------------------------------
// DefaultModuleCommunicationGuard
// ---------------------------------------------------------------------------
void DefaultModuleCommunicationGuard::validate(const std::string& sourceModuleID,
                                                const std::string& targetModuleID,
                                                const std::string& eventType) {
    if (sourceModuleID.empty() || targetModuleID.empty()) {
        throw ForsettiContextException(ForsettiContextError::InvalidModuleID);
    }
    if (sourceModuleID == targetModuleID) {
        throw ForsettiContextException(ForsettiContextError::SelfMessageNotAllowed);
    }
    if (eventType.starts_with("forsetti.internal.")) {
        throw ForsettiContextException(ForsettiContextError::ReservedNamespace);
    }
}

// ---------------------------------------------------------------------------
// ForsettiContext — constructor
// ---------------------------------------------------------------------------
ForsettiContext::ForsettiContext(std::shared_ptr<ServiceContainer> services,
                                 std::shared_ptr<IForsettiEventBus> eventBus,
                                 std::shared_ptr<IForsettiLogger> logger,
                                 std::shared_ptr<IOverlayRouter> router,
                                 std::shared_ptr<IModuleCommunicationGuard> guard)
    : services_(std::move(services))
    , eventBus_(std::move(eventBus))
    , logger_(std::move(logger))
    , router_(std::move(router))
    , guard_(std::move(guard)) {}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------
std::shared_ptr<ServiceContainer> ForsettiContext::services() const {
    return services_;
}

std::shared_ptr<IForsettiEventBus> ForsettiContext::eventBus() const {
    return eventBus_;
}

std::shared_ptr<IForsettiLogger> ForsettiContext::logger() const {
    return logger_;
}

std::shared_ptr<IOverlayRouter> ForsettiContext::router() const {
    return router_;
}

// ---------------------------------------------------------------------------
// publishFrameworkEvent — direct publish, no guard
// ---------------------------------------------------------------------------
void ForsettiContext::publishFrameworkEvent(const ForsettiEvent& event) {
    eventBus_->publish(event);
}

// ---------------------------------------------------------------------------
// sendModuleMessage — validated module-to-module messaging
// ---------------------------------------------------------------------------
void ForsettiContext::sendModuleMessage(const std::string& sourceModuleID,
                                        const std::string& targetModuleID,
                                        const std::string& eventType,
                                        const std::map<std::string, std::string>& payload) {
    // Validate via the communication guard
    guard_->validate(sourceModuleID, targetModuleID, eventType);

    // Build the enriched event
    ForsettiEvent event;
    event.type = eventType;
    event.sourceModuleID = sourceModuleID;

    // Start with the caller-supplied payload, then inject targetModuleID
    event.payload = payload;
    event.payload["targetModuleID"] = targetModuleID;

    eventBus_->publish(event);
}

// ---------------------------------------------------------------------------
// subscribeToModuleMessages — filtered subscription by targetModuleID
// ---------------------------------------------------------------------------
SubscriptionToken ForsettiContext::subscribeToModuleMessages(
    const std::string& targetModuleID,
    const std::string& eventType,
    std::function<void(const ForsettiEvent&)> handler) {

    // Wrap the handler to filter by targetModuleID in the event payload
    auto filteredHandler = [targetModuleID, handler = std::move(handler)](const ForsettiEvent& event) {
        auto it = event.payload.find("targetModuleID");
        if (it != event.payload.end() && it->second == targetModuleID) {
            handler(event);
        }
    };

    auto id = eventBus_->subscribe(eventType, std::move(filteredHandler));
    return SubscriptionToken(eventBus_.get(), id);
}

// ---------------------------------------------------------------------------
// subscribeToFrameworkEvents — direct subscription, no filtering
// ---------------------------------------------------------------------------
SubscriptionToken ForsettiContext::subscribeToFrameworkEvents(
    const std::string& eventType,
    std::function<void(const ForsettiEvent&)> handler) {
    auto id = eventBus_->subscribe(eventType, std::move(handler));
    return SubscriptionToken(eventBus_.get(), id);
}

} // namespace Forsetti
