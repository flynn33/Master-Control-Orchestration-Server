// Forsetti Framework for Windows
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential. Patent Pending.

#include "CppUnitTest.h"
#include "ForsettiPlatform/PlatformServices.h"
#include "ForsettiPlatform/DefaultPlatformServices.h"
#include "ForsettiCore/ForsettiServiceContainer.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Forsetti;

TEST_CLASS(RegistryStorageServiceTests)
{
public:

    TEST_METHOD(SetAndGet)
    {
        RegistryStorageService service;
        service.set("key1", "value1");
        auto result = service.get("key1");
        Assert::IsTrue(result.has_value());
        Assert::AreEqual(std::string("value1"), result.value());
    }

    TEST_METHOD(GetNonexistent_ReturnsNullopt)
    {
        RegistryStorageService service;
        auto result = service.get("nonexistent");
        Assert::IsFalse(result.has_value());
    }

    TEST_METHOD(SetOverwrites)
    {
        RegistryStorageService service;
        service.set("key", "first");
        service.set("key", "second");
        Assert::AreEqual(std::string("second"), service.get("key").value());
    }

    TEST_METHOD(Remove)
    {
        RegistryStorageService service;
        service.set("key", "value");
        service.remove("key");
        Assert::IsFalse(service.get("key").has_value());
    }

    TEST_METHOD(RemoveNonexistent_NoError)
    {
        RegistryStorageService service;
        service.remove("nonexistent");
        Assert::IsTrue(true);
    }

    TEST_METHOD(MultipleKeys)
    {
        RegistryStorageService service;
        service.set("a", "1");
        service.set("b", "2");
        service.set("c", "3");

        Assert::AreEqual(std::string("1"), service.get("a").value());
        Assert::AreEqual(std::string("2"), service.get("b").value());
        Assert::AreEqual(std::string("3"), service.get("c").value());
    }
};

TEST_CLASS(DpapiSecureStorageServiceTests)
{
public:

    TEST_METHOD(SetAndGet)
    {
        DpapiSecureStorageService service;
        std::vector<uint8_t> data = {0x01, 0x02, 0x03};
        service.set("secret", data);

        auto result = service.get("secret");
        Assert::IsTrue(result.has_value());
        Assert::AreEqual(size_t(3), result.value().size());
        Assert::AreEqual(uint8_t(0x01), result.value()[0]);
        Assert::AreEqual(uint8_t(0x02), result.value()[1]);
        Assert::AreEqual(uint8_t(0x03), result.value()[2]);
    }

    TEST_METHOD(GetNonexistent_ReturnsNullopt)
    {
        DpapiSecureStorageService service;
        Assert::IsFalse(service.get("nonexistent").has_value());
    }

    TEST_METHOD(Remove)
    {
        DpapiSecureStorageService service;
        service.set("key", {0xFF});
        service.remove("key");
        Assert::IsFalse(service.get("key").has_value());
    }

    TEST_METHOD(EmptyData)
    {
        DpapiSecureStorageService service;
        service.set("empty", {});
        auto result = service.get("empty");
        Assert::IsTrue(result.has_value());
        Assert::AreEqual(size_t(0), result.value().size());
    }
};

TEST_CLASS(WinHttpNetworkingServiceTests)
{
public:

    TEST_METHOD(Data_ReturnsEmptyVector)
    {
        WinHttpNetworkingService service;
        auto future = service.data("https://example.com");
        auto result = future.get();
        Assert::AreEqual(size_t(0), result.size());
    }
};

TEST_CLASS(LocalFileExportServiceTests)
{
public:

    TEST_METHOD(ExportData_ReturnsFalseStub)
    {
        LocalFileExportService service;
        auto result = service.exportData({0x01, 0x02}, "test.bin");
        Assert::IsFalse(result);
    }
};

TEST_CLASS(NoopTelemetryServiceTests)
{
public:

    TEST_METHOD(TrackEvent_DoesNotThrow)
    {
        NoopTelemetryService service;
        service.trackEvent("test_event", {{"prop", "value"}});
        Assert::IsTrue(true);
    }
};

TEST_CLASS(DefaultPlatformServicesTests)
{
public:

    TEST_METHOD(RegisterAll_ServicesResolvable)
    {
        ServiceContainer container;
        DefaultForsettiPlatformServices::registerAll(container);

        Assert::IsNotNull(container.resolve<INetworkingService>().get());
        Assert::IsNotNull(container.resolve<IStorageService>().get());
        Assert::IsNotNull(container.resolve<ISecureStorageService>().get());
        Assert::IsNotNull(container.resolve<IFileExportService>().get());
        Assert::IsNotNull(container.resolve<ITelemetryService>().get());
    }
};
