// Forsetti Framework for Windows
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential. Patent Pending.

#include "CppUnitTest.h"
#include "TestHelpers.h"
#include "ForsettiCore/ForsettiRuntime.h"
#include "ForsettiCore/ForsettiVersion.h"
#include "ForsettiCore/ManifestLoader.h"
#include "ForsettiCore/UISurfaceManager.h"
#include <filesystem>
#include <fstream>
#include <memory>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Forsetti;
using namespace Forsetti::Tests;

namespace {
    // Creates a temporary manifest directory with example module manifests
    class TempRuntimeDir {
        std::filesystem::path dir_;
    public:
        TempRuntimeDir() {
            dir_ = std::filesystem::temp_directory_path() / "forsetti_runtime_test";
            std::filesystem::create_directories(dir_);
        }
        ~TempRuntimeDir() { std::filesystem::remove_all(dir_); }
        std::string path() const { return dir_.string(); }

        void writeManifest(const std::string& filename, const nlohmann::json& j) {
            std::ofstream f(dir_ / filename);
            f << j.dump(2);
        }
    };

    nlohmann::json makeServiceManifestJSON() {
        return nlohmann::json{
            {"schemaVersion", "1.0"},
            {"moduleID", "com.test.service"},
            {"displayName", "Test Service"},
            {"moduleVersion", {{"major", 0}, {"minor", 1}, {"patch", 0}, {"prerelease", nullptr}}},
            {"moduleType", "service"},
            {"supportedPlatforms", nlohmann::json::array({"Windows"})},
            {"minForsettiVersion", {{"major", 0}, {"minor", 1}, {"patch", 0}, {"prerelease", nullptr}}},
            {"maxForsettiVersion", nullptr},
            {"capabilitiesRequested", nlohmann::json::array()},
            {"iapProductID", nullptr},
            {"entryPoint", "TestServiceModule"}
        };
    }

    struct RuntimeTestFixture {
        std::shared_ptr<MockEntitlementProvider> entitlements;
        std::shared_ptr<InMemoryEventBus> eventBus;
        std::shared_ptr<InMemoryActivationStore> store;
        std::shared_ptr<UISurfaceManager> surfaceManager;
        std::shared_ptr<ForsettiContext> context;
        std::shared_ptr<CompatibilityChecker> checker;

        RuntimeTestFixture() {
            entitlements = std::make_shared<MockEntitlementProvider>();
            entitlements->setUnlocked({"com.test.service"});
            eventBus = std::make_shared<InMemoryEventBus>();
            store = std::make_shared<InMemoryActivationStore>();
            surfaceManager = std::make_shared<UISurfaceManager>();

            auto services = std::make_shared<ServiceContainer>();
            auto logger = std::make_shared<ConsoleLogger>();
            auto router = std::make_shared<NoopOverlayRouter>();
            auto guard = std::make_shared<DefaultModuleCommunicationGuard>();
            context = std::make_shared<ForsettiContext>(services, eventBus, logger, router, guard);

            auto policy = std::make_shared<AllowAllCapabilityPolicy>();
            checker = std::make_shared<CompatibilityChecker>(ForsettiVersion::current, policy);
        }

        std::unique_ptr<ModuleManager> makeModuleManager(ModuleRegistry registry) {
            return std::make_unique<ModuleManager>(
                std::move(registry), checker, entitlements, store, surfaceManager, context);
        }
    };
}

TEST_CLASS(RuntimeLifecycleTests)
{
public:

    TEST_METHOD(Runtime_NotBootedByDefault)
    {
        RuntimeTestFixture fix;
        TempRuntimeDir dir;

        ModuleRegistry registry;
        auto runtime = ForsettiRuntime(
            fix.makeModuleManager(std::move(registry)),
            fix.entitlements, fix.eventBus, dir.path());

        Assert::IsFalse(runtime.isBooted());
    }

    TEST_METHOD(Runtime_BootSetsBootedFlag)
    {
        RuntimeTestFixture fix;
        TempRuntimeDir dir;

        ModuleRegistry registry;
        auto runtime = ForsettiRuntime(
            fix.makeModuleManager(std::move(registry)),
            fix.entitlements, fix.eventBus, dir.path());

        runtime.boot();
        Assert::IsTrue(runtime.isBooted());
    }

    TEST_METHOD(Runtime_ShutdownClearsBootedFlag)
    {
        RuntimeTestFixture fix;
        TempRuntimeDir dir;

        ModuleRegistry registry;
        auto runtime = ForsettiRuntime(
            fix.makeModuleManager(std::move(registry)),
            fix.entitlements, fix.eventBus, dir.path());

        runtime.boot();
        Assert::IsTrue(runtime.isBooted());

        runtime.shutdown();
        Assert::IsFalse(runtime.isBooted());
    }

    TEST_METHOD(Runtime_BootDiscoversManifests)
    {
        RuntimeTestFixture fix;
        TempRuntimeDir dir;
        dir.writeManifest("service.json", makeServiceManifestJSON());

        ModuleRegistry registry;
        registry.registerModule("TestServiceModule", []() -> std::unique_ptr<IForsettiModule> {
            auto desc = ModuleDescriptor{
                .moduleID = "com.test.service", .displayName = "Test Service",
                .version = SemVer{0,1,0}, .type = ModuleType::Service};
            auto manifest = ModuleManifest{
                .schemaVersion = "1.0", .moduleID = "com.test.service",
                .displayName = "Test Service", .moduleVersion = SemVer{0,1,0},
                .moduleType = ModuleType::Service,
                .supportedPlatforms = {Platform::Windows},
                .minForsettiVersion = SemVer{0,1,0},
                .capabilitiesRequested = {},
                .entryPoint = "TestServiceModule"};
            return std::make_unique<StubForsettiModule>(desc, manifest);
        });

        auto runtime = ForsettiRuntime(
            fix.makeModuleManager(std::move(registry)),
            fix.entitlements, fix.eventBus, dir.path());

        runtime.boot();

        const auto& manifests = runtime.moduleManager().manifestsByID();
        Assert::AreEqual(size_t(1), manifests.size());
        Assert::IsTrue(manifests.count("com.test.service") > 0);
    }

    TEST_METHOD(Runtime_ActivateAndDeactivateModule)
    {
        RuntimeTestFixture fix;
        TempRuntimeDir dir;
        dir.writeManifest("service.json", makeServiceManifestJSON());

        ModuleRegistry registry;
        registry.registerModule("TestServiceModule", []() -> std::unique_ptr<IForsettiModule> {
            auto desc = ModuleDescriptor{
                .moduleID = "com.test.service", .displayName = "Test Service",
                .version = SemVer{0,1,0}, .type = ModuleType::Service};
            auto manifest = ModuleManifest{
                .schemaVersion = "1.0", .moduleID = "com.test.service",
                .displayName = "Test Service", .moduleVersion = SemVer{0,1,0},
                .moduleType = ModuleType::Service,
                .supportedPlatforms = {Platform::Windows},
                .minForsettiVersion = SemVer{0,1,0},
                .capabilitiesRequested = {},
                .entryPoint = "TestServiceModule"};
            return std::make_unique<StubForsettiModule>(desc, manifest);
        });

        auto runtime = ForsettiRuntime(
            fix.makeModuleManager(std::move(registry)),
            fix.entitlements, fix.eventBus, dir.path());

        runtime.boot();
        runtime.activateModule("com.test.service");
        Assert::IsTrue(runtime.moduleManager().isModuleActive("com.test.service"));

        runtime.deactivateModule("com.test.service");
        Assert::IsFalse(runtime.moduleManager().isModuleActive("com.test.service"));
    }

    TEST_METHOD(Runtime_Reconciliation_DeactivatesRevokedModules)
    {
        RuntimeTestFixture fix;
        TempRuntimeDir dir;
        dir.writeManifest("service.json", makeServiceManifestJSON());

        ModuleRegistry registry;
        registry.registerModule("TestServiceModule", []() -> std::unique_ptr<IForsettiModule> {
            auto desc = ModuleDescriptor{
                .moduleID = "com.test.service", .displayName = "Test Service",
                .version = SemVer{0,1,0}, .type = ModuleType::Service};
            auto manifest = ModuleManifest{
                .schemaVersion = "1.0", .moduleID = "com.test.service",
                .displayName = "Test Service", .moduleVersion = SemVer{0,1,0},
                .moduleType = ModuleType::Service,
                .supportedPlatforms = {Platform::Windows},
                .minForsettiVersion = SemVer{0,1,0},
                .capabilitiesRequested = {},
                .entryPoint = "TestServiceModule"};
            return std::make_unique<StubForsettiModule>(desc, manifest);
        });

        auto runtime = ForsettiRuntime(
            fix.makeModuleManager(std::move(registry)),
            fix.entitlements, fix.eventBus, dir.path());

        runtime.boot();
        runtime.activateModule("com.test.service");
        Assert::IsTrue(runtime.moduleManager().isModuleActive("com.test.service"));

        // Revoke entitlement
        fix.entitlements->setUnlocked({});
        runtime.reconcileActiveModulesWithEntitlements();
        Assert::IsFalse(runtime.moduleManager().isModuleActive("com.test.service"));
    }
};
