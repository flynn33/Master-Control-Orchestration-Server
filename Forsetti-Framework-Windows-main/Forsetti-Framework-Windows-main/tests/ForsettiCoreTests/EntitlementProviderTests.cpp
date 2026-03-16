// Forsetti Framework for Windows
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential. Patent Pending.

#include "CppUnitTest.h"
#include "ForsettiCore/EntitlementProviders.h"
#include <string>
#include <set>
#include <atomic>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Forsetti;

TEST_CLASS(AllowAllEntitlementProviderTests)
{
public:

    TEST_METHOD(IsUnlocked_AlwaysTrue)
    {
        AllowAllEntitlementProvider provider;
        Assert::IsTrue(provider.isUnlocked("any.module.id"));
        Assert::IsTrue(provider.isUnlocked(""));
        Assert::IsTrue(provider.isUnlocked("com.test.nonexistent"));
    }

    TEST_METHOD(RefreshEntitlements_CallsCallbacks)
    {
        AllowAllEntitlementProvider provider;
        int callCount = 0;
        provider.onEntitlementsChanged([&callCount]() { callCount++; });

        provider.refreshEntitlements();
        Assert::AreEqual(1, callCount);
    }

    TEST_METHOD(MultipleCallbacks_AllInvoked)
    {
        AllowAllEntitlementProvider provider;
        int count1 = 0, count2 = 0;
        provider.onEntitlementsChanged([&count1]() { count1++; });
        provider.onEntitlementsChanged([&count2]() { count2++; });

        provider.refreshEntitlements();
        Assert::AreEqual(1, count1);
        Assert::AreEqual(1, count2);
    }

    TEST_METHOD(RestorePurchases_NoOp)
    {
        AllowAllEntitlementProvider provider;
        // Should not throw
        provider.restorePurchases();
        Assert::IsTrue(true);
    }
};

TEST_CLASS(StaticEntitlementProviderTests)
{
public:

    TEST_METHOD(EmptyByDefault)
    {
        StaticEntitlementProvider provider;
        Assert::IsFalse(provider.isUnlocked("any.module"));
    }

    TEST_METHOD(UnlockedModules)
    {
        StaticEntitlementProvider provider;
        provider.setUnlockedModules({"mod.a", "mod.b"});

        Assert::IsTrue(provider.isUnlocked("mod.a"));
        Assert::IsTrue(provider.isUnlocked("mod.b"));
        Assert::IsFalse(provider.isUnlocked("mod.c"));
    }

    TEST_METHOD(UnlockedProducts)
    {
        StaticEntitlementProvider provider;
        provider.setUnlockedProducts({"prod.x"});

        Assert::IsTrue(provider.isUnlocked("prod.x"));
        Assert::IsFalse(provider.isUnlocked("prod.y"));
    }

    TEST_METHOD(ModulesAndProducts_BothChecked)
    {
        StaticEntitlementProvider provider;
        provider.setUnlockedModules({"mod.a"});
        provider.setUnlockedProducts({"prod.x"});

        Assert::IsTrue(provider.isUnlocked("mod.a"));
        Assert::IsTrue(provider.isUnlocked("prod.x"));
        Assert::IsFalse(provider.isUnlocked("other"));
    }

    TEST_METHOD(SetUnlockedModules_BroadcastsChange)
    {
        StaticEntitlementProvider provider;
        int callCount = 0;
        provider.onEntitlementsChanged([&callCount]() { callCount++; });

        provider.setUnlockedModules({"mod.a"});
        Assert::AreEqual(1, callCount);
    }

    TEST_METHOD(SetUnlockedProducts_BroadcastsChange)
    {
        StaticEntitlementProvider provider;
        int callCount = 0;
        provider.onEntitlementsChanged([&callCount]() { callCount++; });

        provider.setUnlockedProducts({"prod.x"});
        Assert::AreEqual(1, callCount);
    }

    TEST_METHOD(RefreshEntitlements_BroadcastsChange)
    {
        StaticEntitlementProvider provider;
        int callCount = 0;
        provider.onEntitlementsChanged([&callCount]() { callCount++; });

        provider.refreshEntitlements();
        Assert::AreEqual(1, callCount);
    }

    TEST_METHOD(SetUnlockedModules_ReplacesEntireSet)
    {
        StaticEntitlementProvider provider;
        provider.setUnlockedModules({"mod.a", "mod.b"});
        Assert::IsTrue(provider.isUnlocked("mod.a"));

        provider.setUnlockedModules({"mod.c"});
        Assert::IsFalse(provider.isUnlocked("mod.a"));
        Assert::IsFalse(provider.isUnlocked("mod.b"));
        Assert::IsTrue(provider.isUnlocked("mod.c"));
    }

    TEST_METHOD(Reconciliation_RevokedModule)
    {
        StaticEntitlementProvider provider;
        provider.setUnlockedModules({"mod.a", "mod.b"});

        // Simulate revocation
        bool changeNotified = false;
        provider.onEntitlementsChanged([&changeNotified]() { changeNotified = true; });

        provider.setUnlockedModules({"mod.a"});  // Revoke mod.b
        Assert::IsTrue(changeNotified);
        Assert::IsTrue(provider.isUnlocked("mod.a"));
        Assert::IsFalse(provider.isUnlocked("mod.b"));
    }
};
