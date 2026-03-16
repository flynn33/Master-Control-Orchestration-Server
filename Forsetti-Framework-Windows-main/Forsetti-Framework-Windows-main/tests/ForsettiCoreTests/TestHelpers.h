// Forsetti Framework for Windows
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential. Patent Pending.
//
// Test helper classes — mock/stub doubles for use with Microsoft CppUnitTest.

#pragma once

#include "ForsettiCore/ForsettiEventBus.h"
#include "ForsettiCore/ActivationStore.h"
#include "ForsettiCore/ForsettiProtocols.h"
#include "ForsettiCore/ForsettiLogger.h"
#include "ForsettiCore/ForsettiContext.h"
#include "ForsettiCore/ModuleModels.h"
#include "ForsettiCore/UIModels.h"

#include <set>
#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <map>

namespace Forsetti::Tests {

// -----------------------------------------------------------------------
// In-memory activation store for testing
// -----------------------------------------------------------------------
class InMemoryActivationStore final : public IActivationStore {
    ActivationState state_;
public:
    ActivationState loadState() const override { return state_; }
    void saveState(const ActivationState& state) override { state_ = state; }
};

// -----------------------------------------------------------------------
// Mock entitlement provider — controllable unlock state + change broadcast
// -----------------------------------------------------------------------
class MockEntitlementProvider final : public IEntitlementProvider {
    std::set<std::string> unlockedIDs_;
    std::vector<std::function<void()>> callbacks_;
    mutable std::mutex mutex_;
public:
    bool isUnlocked(const std::string& id) const override {
        std::lock_guard lock(mutex_);
        return unlockedIDs_.contains(id);
    }

    void setUnlocked(std::set<std::string> ids) {
        {
            std::lock_guard lock(mutex_);
            unlockedIDs_ = std::move(ids);
        }
        broadcastChange();
    }

    void refreshEntitlements() override { broadcastChange(); }

    void onEntitlementsChanged(std::function<void()> callback) override {
        std::lock_guard lock(mutex_);
        callbacks_.push_back(std::move(callback));
    }

    void restorePurchases() override {}

private:
    void broadcastChange() {
        std::vector<std::function<void()>> cbs;
        { std::lock_guard lock(mutex_); cbs = callbacks_; }
        for (auto& cb : cbs) cb();
    }
};

// -----------------------------------------------------------------------
// Recording logger — captures log entries for assertion
// -----------------------------------------------------------------------
class RecordingLogger final : public IForsettiLogger {
public:
    struct LogEntry {
        LogLevel level;
        std::string message;
        std::string sourceModuleID;
    };

    std::vector<LogEntry> entries;

    void log(LogLevel level, const std::string& message,
             const std::string& sourceModuleID = "",
             const std::map<std::string, std::string>& /*metadata*/ = {}) override {
        entries.push_back({level, message, sourceModuleID});
    }
};

// -----------------------------------------------------------------------
// Stub module for testing (service type)
// -----------------------------------------------------------------------
class StubForsettiModule final : public IForsettiModule {
    ModuleDescriptor desc_;
    ModuleManifest manifest_;
    bool started_ = false;
public:
    StubForsettiModule(ModuleDescriptor desc, ModuleManifest manifest)
        : desc_(std::move(desc)), manifest_(std::move(manifest)) {}

    ModuleDescriptor descriptor() const override { return desc_; }
    ModuleManifest manifest() const override { return manifest_; }
    void start(ForsettiContext& /*ctx*/) override { started_ = true; }
    void stop(ForsettiContext& /*ctx*/) override { started_ = false; }
    bool isStarted() const { return started_; }
};

// -----------------------------------------------------------------------
// Stub UI module for testing
// -----------------------------------------------------------------------
class StubForsettiUIModule final : public IForsettiUIModule {
    ModuleDescriptor desc_;
    ModuleManifest manifest_;
    UIContributions contributions_;
    bool started_ = false;
public:
    StubForsettiUIModule(ModuleDescriptor desc, ModuleManifest manifest,
                         UIContributions contributions = {})
        : desc_(std::move(desc)), manifest_(std::move(manifest)),
          contributions_(std::move(contributions)) {}

    ModuleDescriptor descriptor() const override { return desc_; }
    ModuleManifest manifest() const override { return manifest_; }
    void start(ForsettiContext& /*ctx*/) override { started_ = true; }
    void stop(ForsettiContext& /*ctx*/) override { started_ = false; }
    UIContributions uiContributions() const override { return contributions_; }
    bool isStarted() const { return started_; }
};

} // namespace Forsetti::Tests
