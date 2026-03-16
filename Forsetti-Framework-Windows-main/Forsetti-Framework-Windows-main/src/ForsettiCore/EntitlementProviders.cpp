// Forsetti Framework for Windows
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential. Patent Pending.

#include "ForsettiCore/EntitlementProviders.h"
#include <utility>

namespace Forsetti {

// ===========================================================================
// AllowAllEntitlementProvider
// ===========================================================================

bool AllowAllEntitlementProvider::isUnlocked(
    const std::string& /*moduleIDOrProductID*/) const
{
    return true;
}

void AllowAllEntitlementProvider::refreshEntitlements() {
    std::vector<std::function<void()>> snapshot;
    {
        std::lock_guard lock(mutex_);
        snapshot = callbacks_;
    }
    for (const auto& cb : snapshot) {
        cb();
    }
}

void AllowAllEntitlementProvider::onEntitlementsChanged(
    std::function<void()> callback)
{
    std::lock_guard lock(mutex_);
    callbacks_.push_back(std::move(callback));
}

void AllowAllEntitlementProvider::restorePurchases() {
    // No-op.
}

// ===========================================================================
// StaticEntitlementProvider
// ===========================================================================

bool StaticEntitlementProvider::isUnlocked(
    const std::string& moduleIDOrProductID) const
{
    std::lock_guard lock(mutex_);
    return unlockedModuleIDs_.contains(moduleIDOrProductID)
        || unlockedProductIDs_.contains(moduleIDOrProductID);
}

void StaticEntitlementProvider::refreshEntitlements() {
    broadcastChange();
}

void StaticEntitlementProvider::onEntitlementsChanged(
    std::function<void()> callback)
{
    std::lock_guard lock(mutex_);
    callbacks_.push_back(std::move(callback));
}

void StaticEntitlementProvider::restorePurchases() {
    // No-op.
}

void StaticEntitlementProvider::setUnlockedModules(std::set<std::string> ids) {
    {
        std::lock_guard lock(mutex_);
        unlockedModuleIDs_ = std::move(ids);
    }
    broadcastChange();
}

void StaticEntitlementProvider::setUnlockedProducts(std::set<std::string> ids) {
    {
        std::lock_guard lock(mutex_);
        unlockedProductIDs_ = std::move(ids);
    }
    broadcastChange();
}

void StaticEntitlementProvider::broadcastChange() {
    std::vector<std::function<void()>> snapshot;
    {
        std::lock_guard lock(mutex_);
        snapshot = callbacks_;
    }
    for (const auto& cb : snapshot) {
        cb();
    }
}

} // namespace Forsetti
