// Forsetti Framework for Windows
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential. Patent Pending.

#pragma once
#include "ForsettiCore/ForsettiProtocols.h"
#include <mutex>
#include <set>
#include <vector>
#include <functional>

namespace Forsetti {

// ---------------------------------------------------------------------------
// AllowAllEntitlementProvider – every module / product is considered unlocked.
// ---------------------------------------------------------------------------
class AllowAllEntitlementProvider final : public IEntitlementProvider {
public:
    bool isUnlocked(const std::string& moduleIDOrProductID) const override;
    void refreshEntitlements() override;
    void onEntitlementsChanged(std::function<void()> callback) override;
    void restorePurchases() override;

private:
    mutable std::mutex                   mutex_;
    std::vector<std::function<void()>>   callbacks_;
};

// ---------------------------------------------------------------------------
// StaticEntitlementProvider – unlocked set is controlled programmatically.
// ---------------------------------------------------------------------------
class StaticEntitlementProvider final : public IEntitlementProvider {
public:
    bool isUnlocked(const std::string& moduleIDOrProductID) const override;
    void refreshEntitlements() override;
    void onEntitlementsChanged(std::function<void()> callback) override;
    void restorePurchases() override;

    void setUnlockedModules(std::set<std::string> ids);
    void setUnlockedProducts(std::set<std::string> ids);

private:
    void broadcastChange();

    mutable std::mutex                   mutex_;
    std::set<std::string>                unlockedModuleIDs_;
    std::set<std::string>                unlockedProductIDs_;
    std::vector<std::function<void()>>   callbacks_;
};

} // namespace Forsetti
