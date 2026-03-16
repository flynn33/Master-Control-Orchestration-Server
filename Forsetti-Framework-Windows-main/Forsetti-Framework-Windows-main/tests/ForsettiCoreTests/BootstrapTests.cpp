// Forsetti Framework for Windows
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential. Patent Pending.

#include "CppUnitTest.h"
#include "TestHelpers.h"
#include "ForsettiCore/ForsettiRuntime.h"
#include "ForsettiCore/ForsettiVersion.h"
#include "ForsettiCore/ForsettiServiceContainer.h"
#include "ForsettiCore/EntitlementProviders.h"
#include "ForsettiCore/UISurfaceManager.h"
#include "ForsettiCore/StaticModuleRegistry.h"
#include <memory>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Forsetti;
using namespace Forsetti::Tests;

TEST_CLASS(BootstrapTests)
{
public:

    TEST_METHOD(FullBootstrap_DefaultProviders)
    {
        // Verify that all default components can be wired together without errors
        auto entitlements = std::make_shared<AllowAllEntitlementProvider>();
        auto eventBus = std::make_shared<InMemoryEventBus>();
        auto store = std::make_shared<InMemoryActivationStore>();
        auto surfaceManager = std::make_shared<UISurfaceManager>();

        auto services = std::make_shared<ServiceContainer>();
        auto logger = std::make_shared<ConsoleLogger>();
        auto router = std::make_shared<NoopOverlayRouter>();
        auto guard = std::make_shared<DefaultModuleCommunicationGuard>();
        auto context = std::make_shared<ForsettiContext>(
            services, eventBus, logger, router, guard);

        auto policy = std::make_shared<AllowAllCapabilityPolicy>();
        auto checker = std::make_shared<CompatibilityChecker>(
            ForsettiVersion::current, policy);

        ModuleRegistry registry;
        auto moduleManager = std::make_unique<ModuleManager>(
            std::move(registry), checker, entitlements, store, surfaceManager, context);

        ForsettiRuntime runtime(
            std::move(moduleManager), entitlements, eventBus, ".");

        Assert::IsFalse(runtime.isBooted());
    }

    TEST_METHOD(Bootstrap_StaticModuleRegistry)
    {
        // Verify ForsettiStaticModuleRegistry factory pattern works
        auto registry = ForsettiStaticModuleRegistry::buildRegistry(
            [](ModuleRegistry& reg) {
                reg.registerModule("StubModule", []() -> std::unique_ptr<IForsettiModule> {
                    auto desc = ModuleDescriptor{
                        .moduleID = "com.test.stub", .displayName = "Stub",
                        .version = SemVer{1,0,0}, .type = ModuleType::Service};
                    auto manifest = ModuleManifest{
                        .schemaVersion = "1.0", .moduleID = "com.test.stub",
                        .displayName = "Stub", .moduleVersion = SemVer{1,0,0},
                        .moduleType = ModuleType::Service,
                        .supportedPlatforms = {Platform::Windows},
                        .minForsettiVersion = SemVer{0,1,0},
                        .capabilitiesRequested = {},
                        .entryPoint = "StubModule"};
                    return std::make_unique<StubForsettiModule>(desc, manifest);
                });
            });

        Assert::IsTrue(registry.hasEntryPoint("StubModule"));
        Assert::IsFalse(registry.hasEntryPoint("NonExistent"));
    }

    TEST_METHOD(Bootstrap_ServiceContainer_RegisterAndResolve)
    {
        auto container = std::make_shared<ServiceContainer>();

        auto logger = std::make_shared<ConsoleLogger>();
        container->registerService<IForsettiLogger>(logger);

        auto resolved = container->resolve<IForsettiLogger>();
        Assert::IsNotNull(resolved.get());
        Assert::IsTrue(resolved == logger);
    }

    TEST_METHOD(Bootstrap_ServiceContainer_ResolveUnregistered)
    {
        auto container = std::make_shared<ServiceContainer>();
        auto resolved = container->resolve<IForsettiLogger>();
        Assert::IsNull(resolved.get());
    }

    TEST_METHOD(Bootstrap_ContextAccessors)
    {
        auto services = std::make_shared<ServiceContainer>();
        auto eventBus = std::make_shared<InMemoryEventBus>();
        auto logger = std::make_shared<ConsoleLogger>();
        auto router = std::make_shared<NoopOverlayRouter>();
        auto guard = std::make_shared<DefaultModuleCommunicationGuard>();

        ForsettiContext context(services, eventBus, logger, router, guard);

        Assert::IsNotNull(context.services().get());
        Assert::IsNotNull(context.eventBus().get());
        Assert::IsNotNull(context.logger().get());
        Assert::IsNotNull(context.router().get());
    }

    TEST_METHOD(Bootstrap_ModuleRegistry_DuplicateEntryPoint_Throws)
    {
        ModuleRegistry registry;
        registry.registerModule("TestEntry", []() -> std::unique_ptr<IForsettiModule> {
            return nullptr;
        });

        Assert::ExpectException<ModuleRegistryException>([&registry]() {
            registry.registerModule("TestEntry", []() -> std::unique_ptr<IForsettiModule> {
                return nullptr;
            });
        });
    }

    TEST_METHOD(Bootstrap_ModuleRegistry_UnknownEntryPoint_Throws)
    {
        ModuleRegistry registry;
        Assert::ExpectException<ModuleRegistryException>([&registry]() {
            registry.makeModule("NonExistent");
        });
    }

    TEST_METHOD(Bootstrap_ActivationStore_PersistAndRestore)
    {
        InMemoryActivationStore store;

        ActivationState state;
        state.enabledServiceModuleIDs = {"mod.a", "mod.b"};
        state.enabledUIModuleIDs = {"mod.ui"};
        state.selectedUIModuleID = "mod.ui";

        store.saveState(state);
        auto restored = store.loadState();

        Assert::IsTrue(restored == state);
    }
};
