// Forsetti Framework for Windows
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential. Patent Pending.

#include "CppUnitTest.h"
#include "ForsettiCore/ForsettiContext.h"
#include "ForsettiCore/ForsettiEventBus.h"
#include "ForsettiCore/ForsettiServiceContainer.h"
#include "ForsettiCore/ForsettiLogger.h"
#include <string>
#include <vector>
#include <memory>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Forsetti;

namespace {
    std::shared_ptr<ForsettiContext> makeTestContext(
        std::shared_ptr<IForsettiEventBus> bus = nullptr,
        std::shared_ptr<IModuleCommunicationGuard> guard = nullptr)
    {
        auto services = std::make_shared<ServiceContainer>();
        if (!bus) bus = std::make_shared<InMemoryEventBus>();
        auto logger = std::make_shared<ConsoleLogger>();
        auto router = std::make_shared<NoopOverlayRouter>();
        if (!guard) guard = std::make_shared<DefaultModuleCommunicationGuard>();

        return std::make_shared<ForsettiContext>(services, bus, logger, router, guard);
    }
}

TEST_CLASS(EventBusTests)
{
public:

    TEST_METHOD(Publish_SubscriberReceivesEvent)
    {
        InMemoryEventBus bus;
        std::string received;

        bus.subscribe("test.event", [&received](const ForsettiEvent& e) {
            received = e.type;
        });

        ForsettiEvent event;
        event.type = "test.event";
        bus.publish(event);

        Assert::AreEqual(std::string("test.event"), received);
    }

    TEST_METHOD(Publish_OnlyMatchingSubscribersReceive)
    {
        InMemoryEventBus bus;
        int count = 0;

        bus.subscribe("target.event", [&count](const ForsettiEvent&) { count++; });
        bus.subscribe("other.event", [&count](const ForsettiEvent&) { count += 100; });

        ForsettiEvent event;
        event.type = "target.event";
        bus.publish(event);

        Assert::AreEqual(1, count);
    }

    TEST_METHOD(Publish_MultipleSubscribersReceive)
    {
        InMemoryEventBus bus;
        int count = 0;

        bus.subscribe("test.event", [&count](const ForsettiEvent&) { count++; });
        bus.subscribe("test.event", [&count](const ForsettiEvent&) { count++; });

        ForsettiEvent event;
        event.type = "test.event";
        bus.publish(event);

        Assert::AreEqual(2, count);
    }

    TEST_METHOD(Unsubscribe_StopsReceiving)
    {
        InMemoryEventBus bus;
        int count = 0;

        auto tokenID = bus.subscribe("test.event", [&count](const ForsettiEvent&) { count++; });

        ForsettiEvent event;
        event.type = "test.event";
        bus.publish(event);
        Assert::AreEqual(1, count);

        bus.unsubscribe(tokenID);
        bus.publish(event);
        Assert::AreEqual(1, count);  // Should not have incremented
    }

    TEST_METHOD(Publish_PayloadPreserved)
    {
        InMemoryEventBus bus;
        std::string value;

        bus.subscribe("data.event", [&value](const ForsettiEvent& e) {
            auto it = e.payload.find("key");
            if (it != e.payload.end()) value = it->second;
        });

        ForsettiEvent event;
        event.type = "data.event";
        event.payload["key"] = "hello";
        bus.publish(event);

        Assert::AreEqual(std::string("hello"), value);
    }

    TEST_METHOD(SubscriptionToken_CancelStopsReceiving)
    {
        InMemoryEventBus bus;
        int count = 0;

        auto id = bus.subscribe("test.event", [&count](const ForsettiEvent&) { count++; });
        auto token = bus.makeToken(id);

        ForsettiEvent event;
        event.type = "test.event";
        bus.publish(event);
        Assert::AreEqual(1, count);

        token.cancel();
        bus.publish(event);
        Assert::AreEqual(1, count);
    }

    TEST_METHOD(SubscriptionToken_DestructorCancels)
    {
        InMemoryEventBus bus;
        int count = 0;

        {
            auto id = bus.subscribe("test.event", [&count](const ForsettiEvent&) { count++; });
            auto token = bus.makeToken(id);
            // token goes out of scope here
        }

        ForsettiEvent event;
        event.type = "test.event";
        bus.publish(event);
        Assert::AreEqual(0, count);
    }
};

TEST_CLASS(CommunicationGuardTests)
{
public:

    TEST_METHOD(ValidMessage_NoException)
    {
        DefaultModuleCommunicationGuard guard;
        guard.validate("module.a", "module.b", "custom.event");
        // No exception = success
        Assert::IsTrue(true);
    }

    TEST_METHOD(EmptySourceID_Throws)
    {
        DefaultModuleCommunicationGuard guard;
        Assert::ExpectException<ForsettiContextException>([&guard]() {
            guard.validate("", "module.b", "custom.event");
        });
    }

    TEST_METHOD(EmptyTargetID_Throws)
    {
        DefaultModuleCommunicationGuard guard;
        Assert::ExpectException<ForsettiContextException>([&guard]() {
            guard.validate("module.a", "", "custom.event");
        });
    }

    TEST_METHOD(BothIDsEmpty_Throws)
    {
        DefaultModuleCommunicationGuard guard;
        Assert::ExpectException<ForsettiContextException>([&guard]() {
            guard.validate("", "", "custom.event");
        });
    }

    TEST_METHOD(SelfMessage_Throws)
    {
        DefaultModuleCommunicationGuard guard;
        Assert::ExpectException<ForsettiContextException>([&guard]() {
            guard.validate("module.a", "module.a", "custom.event");
        });
    }

    TEST_METHOD(ReservedNamespace_Throws)
    {
        DefaultModuleCommunicationGuard guard;
        Assert::ExpectException<ForsettiContextException>([&guard]() {
            guard.validate("module.a", "module.b", "forsetti.internal.shutdown");
        });
    }

    TEST_METHOD(ReservedNamespace_PrefixOnly)
    {
        DefaultModuleCommunicationGuard guard;
        // "forsetti.internal" without trailing dot should NOT throw
        guard.validate("module.a", "module.b", "forsetti.internal");
        Assert::IsTrue(true);
    }

    TEST_METHOD(ErrorCode_InvalidModuleID)
    {
        DefaultModuleCommunicationGuard guard;
        try {
            guard.validate("", "module.b", "event");
            Assert::Fail(L"Expected exception");
        } catch (const ForsettiContextException& e) {
            Assert::IsTrue(e.error() == ForsettiContextError::InvalidModuleID);
        }
    }

    TEST_METHOD(ErrorCode_SelfMessage)
    {
        DefaultModuleCommunicationGuard guard;
        try {
            guard.validate("module.a", "module.a", "event");
            Assert::Fail(L"Expected exception");
        } catch (const ForsettiContextException& e) {
            Assert::IsTrue(e.error() == ForsettiContextError::SelfMessageNotAllowed);
        }
    }

    TEST_METHOD(ErrorCode_ReservedNamespace)
    {
        DefaultModuleCommunicationGuard guard;
        try {
            guard.validate("module.a", "module.b", "forsetti.internal.test");
            Assert::Fail(L"Expected exception");
        } catch (const ForsettiContextException& e) {
            Assert::IsTrue(e.error() == ForsettiContextError::ReservedNamespace);
        }
    }
};

TEST_CLASS(ForsettiContextMessagingTests)
{
public:

    TEST_METHOD(SendModuleMessage_PublishesToEventBus)
    {
        auto bus = std::make_shared<InMemoryEventBus>();
        auto ctx = makeTestContext(bus);

        std::string receivedType;
        bus->subscribe("custom.msg", [&receivedType](const ForsettiEvent& e) {
            receivedType = e.type;
        });

        ctx->sendModuleMessage("source.module", "target.module", "custom.msg");
        Assert::AreEqual(std::string("custom.msg"), receivedType);
    }

    TEST_METHOD(SendModuleMessage_InjectsTargetModuleID)
    {
        auto bus = std::make_shared<InMemoryEventBus>();
        auto ctx = makeTestContext(bus);

        std::string targetID;
        bus->subscribe("msg.type", [&targetID](const ForsettiEvent& e) {
            auto it = e.payload.find("targetModuleID");
            if (it != e.payload.end()) targetID = it->second;
        });

        ctx->sendModuleMessage("source", "target.mod", "msg.type");
        Assert::AreEqual(std::string("target.mod"), targetID);
    }

    TEST_METHOD(SendModuleMessage_SetsSourceModuleID)
    {
        auto bus = std::make_shared<InMemoryEventBus>();
        auto ctx = makeTestContext(bus);

        std::string sourceID;
        bus->subscribe("msg.type", [&sourceID](const ForsettiEvent& e) {
            if (e.sourceModuleID.has_value()) sourceID = e.sourceModuleID.value();
        });

        ctx->sendModuleMessage("source.mod", "target.mod", "msg.type");
        Assert::AreEqual(std::string("source.mod"), sourceID);
    }

    TEST_METHOD(SendModuleMessage_InvalidIDs_Throws)
    {
        auto ctx = makeTestContext();
        Assert::ExpectException<ForsettiContextException>([&ctx]() {
            ctx->sendModuleMessage("", "target", "event");
        });
    }

    TEST_METHOD(PublishFrameworkEvent_BypassesGuard)
    {
        auto bus = std::make_shared<InMemoryEventBus>();
        auto ctx = makeTestContext(bus);

        std::string received;
        bus->subscribe("forsetti.internal.boot", [&received](const ForsettiEvent& e) {
            received = e.type;
        });

        // Framework events can use reserved namespace
        ForsettiEvent event;
        event.type = "forsetti.internal.boot";
        ctx->publishFrameworkEvent(event);
        Assert::AreEqual(std::string("forsetti.internal.boot"), received);
    }
};
