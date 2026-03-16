// Forsetti Framework for Windows
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential. Patent Pending.

#pragma once
#include "ForsettiCore/ForsettiEventBus.h"
#include "ForsettiCore/ForsettiLogger.h"
#include "ForsettiCore/ForsettiServiceContainer.h"
#include <string>
#include <memory>
#include <functional>
#include <stdexcept>
#include <map>

namespace Forsetti {

// ---------------------------------------------------------------------------
// IOverlayRouter — abstract interface for overlay navigation
// ---------------------------------------------------------------------------
class IOverlayRouter {
public:
    virtual void openPointer(const std::string& pointerID) = 0;
    virtual void openRoute(const std::string& routeID) = 0;
    virtual ~IOverlayRouter() = default;
};

// ---------------------------------------------------------------------------
// NoopOverlayRouter — default no-op implementation
// ---------------------------------------------------------------------------
class NoopOverlayRouter final : public IOverlayRouter {
public:
    void openPointer(const std::string& /*pointerID*/) override {}
    void openRoute(const std::string& /*routeID*/) override {}
};

// ---------------------------------------------------------------------------
// IModuleCommunicationGuard — abstract validation for module messaging
// ---------------------------------------------------------------------------
class IModuleCommunicationGuard {
public:
    virtual void validate(const std::string& sourceModuleID,
                          const std::string& targetModuleID,
                          const std::string& eventType) = 0;
    virtual ~IModuleCommunicationGuard() = default;
};

// ---------------------------------------------------------------------------
// ForsettiContextError / ForsettiContextException
// ---------------------------------------------------------------------------
enum class ForsettiContextError {
    InvalidModuleID,
    SelfMessageNotAllowed,
    ReservedNamespace
};

class ForsettiContextException final : public std::runtime_error {
public:
    explicit ForsettiContextException(ForsettiContextError error)
        : std::runtime_error(messageForError(error)), error_(error) {}

    [[nodiscard]] ForsettiContextError error() const noexcept { return error_; }

private:
    ForsettiContextError error_;

    static std::string messageForError(ForsettiContextError error) {
        switch (error) {
            case ForsettiContextError::InvalidModuleID:
                return "Invalid module ID: source and target module IDs must not be empty.";
            case ForsettiContextError::SelfMessageNotAllowed:
                return "A module cannot send a message to itself.";
            case ForsettiContextError::ReservedNamespace:
                return "The 'forsetti.internal.' event namespace is reserved.";
        }
        return "Unknown ForsettiContext error.";
    }
};

// ---------------------------------------------------------------------------
// DefaultModuleCommunicationGuard — standard validation rules
// ---------------------------------------------------------------------------
class DefaultModuleCommunicationGuard final : public IModuleCommunicationGuard {
public:
    void validate(const std::string& sourceModuleID,
                  const std::string& targetModuleID,
                  const std::string& eventType) override;
};

// ---------------------------------------------------------------------------
// ForsettiContext — the dependency-injection hub given to modules
// ---------------------------------------------------------------------------
class ForsettiContext final {
public:
    ForsettiContext(std::shared_ptr<ServiceContainer> services,
                    std::shared_ptr<IForsettiEventBus> eventBus,
                    std::shared_ptr<IForsettiLogger> logger,
                    std::shared_ptr<IOverlayRouter> router,
                    std::shared_ptr<IModuleCommunicationGuard> guard);

    // Accessors
    [[nodiscard]] std::shared_ptr<ServiceContainer> services() const;
    [[nodiscard]] std::shared_ptr<IForsettiEventBus> eventBus() const;
    [[nodiscard]] std::shared_ptr<IForsettiLogger> logger() const;
    [[nodiscard]] std::shared_ptr<IOverlayRouter> router() const;

    // Framework event publishing (direct, no guard)
    void publishFrameworkEvent(const ForsettiEvent& event);

    // Module-to-module messaging (validated via guard)
    void sendModuleMessage(const std::string& sourceModuleID,
                           const std::string& targetModuleID,
                           const std::string& eventType,
                           const std::map<std::string, std::string>& payload = {});

    // Subscribe to module messages filtered by targetModuleID
    SubscriptionToken subscribeToModuleMessages(
        const std::string& targetModuleID,
        const std::string& eventType,
        std::function<void(const ForsettiEvent&)> handler);

    // Subscribe to framework events (direct, no filtering)
    SubscriptionToken subscribeToFrameworkEvents(
        const std::string& eventType,
        std::function<void(const ForsettiEvent&)> handler);

private:
    std::shared_ptr<ServiceContainer> services_;
    std::shared_ptr<IForsettiEventBus> eventBus_;
    std::shared_ptr<IForsettiLogger> logger_;
    std::shared_ptr<IOverlayRouter> router_;
    std::shared_ptr<IModuleCommunicationGuard> guard_;
};

} // namespace Forsetti
