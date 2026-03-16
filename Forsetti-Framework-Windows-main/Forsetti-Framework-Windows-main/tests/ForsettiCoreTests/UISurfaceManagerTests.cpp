// Forsetti Framework for Windows
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential. Patent Pending.

#include "CppUnitTest.h"
#include "ForsettiCore/UISurfaceManager.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Forsetti;

namespace {
    UIContributions makeContributions(
        std::vector<ToolbarItemDescriptor> toolbar = {},
        std::vector<ViewInjectionDescriptor> injections = {},
        std::optional<ThemeMask> theme = std::nullopt,
        std::optional<OverlaySchema> overlay = std::nullopt)
    {
        return UIContributions{
            .themeMask      = std::move(theme),
            .toolbarItems   = std::move(toolbar),
            .viewInjections = std::move(injections),
            .overlaySchema  = std::move(overlay)
        };
    }
}

TEST_CLASS(UISurfaceManagerTests)
{
public:

    TEST_METHOD(Empty_NoContributions)
    {
        UISurfaceManager mgr;
        mgr.rebuildSurfaceState();

        Assert::AreEqual(size_t(0), mgr.currentToolbarItems().size());
        Assert::AreEqual(size_t(0), mgr.currentViewInjectionsBySlot().size());
        Assert::IsFalse(mgr.currentThemeMask().has_value());
        Assert::IsFalse(mgr.currentOverlaySchema().has_value());
    }

    TEST_METHOD(ToolbarItems_Concatenated)
    {
        UISurfaceManager mgr;

        ToolbarItemDescriptor item1{
            .itemID = "item1", .title = "Item 1",
            .systemImageName = "icon1", .action = NavigateAction{"home"}};
        ToolbarItemDescriptor item2{
            .itemID = "item2", .title = "Item 2",
            .systemImageName = "icon2", .action = NavigateAction{"settings"}};

        mgr.addModuleContributions("mod.a", makeContributions({item1}));
        mgr.addModuleContributions("mod.b", makeContributions({item2}));
        mgr.rebuildSurfaceState();

        Assert::AreEqual(size_t(2), mgr.currentToolbarItems().size());
    }

    TEST_METHOD(ViewInjections_GroupedBySlot)
    {
        UISurfaceManager mgr;

        ViewInjectionDescriptor inj1{
            .injectionID = "inj1", .slot = "homeBanner",
            .viewID = "view1", .priority = 100};
        ViewInjectionDescriptor inj2{
            .injectionID = "inj2", .slot = "homeBanner",
            .viewID = "view2", .priority = 50};
        ViewInjectionDescriptor inj3{
            .injectionID = "inj3", .slot = "sidebar",
            .viewID = "view3", .priority = 0};

        mgr.addModuleContributions("mod.a", makeContributions({}, {inj1, inj3}));
        mgr.addModuleContributions("mod.b", makeContributions({}, {inj2}));
        mgr.rebuildSurfaceState();

        const auto& bySlot = mgr.currentViewInjectionsBySlot();
        Assert::AreEqual(size_t(2), bySlot.size());

        auto homeIt = bySlot.find("homeBanner");
        Assert::IsTrue(homeIt != bySlot.end());
        Assert::AreEqual(size_t(2), homeIt->second.size());

        auto sidebarIt = bySlot.find("sidebar");
        Assert::IsTrue(sidebarIt != bySlot.end());
        Assert::AreEqual(size_t(1), sidebarIt->second.size());
    }

    TEST_METHOD(ViewInjections_SortedByPriorityDescending)
    {
        UISurfaceManager mgr;

        ViewInjectionDescriptor low{
            .injectionID = "low", .slot = "slot1",
            .viewID = "viewLow", .priority = 10};
        ViewInjectionDescriptor high{
            .injectionID = "high", .slot = "slot1",
            .viewID = "viewHigh", .priority = 100};
        ViewInjectionDescriptor mid{
            .injectionID = "mid", .slot = "slot1",
            .viewID = "viewMid", .priority = 50};

        mgr.addModuleContributions("mod.a", makeContributions({}, {low, high, mid}));
        mgr.rebuildSurfaceState();

        const auto& injections = mgr.currentViewInjectionsBySlot().at("slot1");
        Assert::AreEqual(size_t(3), injections.size());
        Assert::AreEqual(100, injections[0].priority);
        Assert::AreEqual(50, injections[1].priority);
        Assert::AreEqual(10, injections[2].priority);
    }

    TEST_METHOD(ThemeMask_LastModuleWins)
    {
        UISurfaceManager mgr;

        ThemeMask theme1{.tokens = {{.key = "bg", .value = "red"}}};
        ThemeMask theme2{.tokens = {{.key = "bg", .value = "blue"}}};

        // std::map iterates alphabetically, so mod.a comes before mod.b
        mgr.addModuleContributions("mod.a", makeContributions({}, {}, theme1));
        mgr.addModuleContributions("mod.b", makeContributions({}, {}, theme2));
        mgr.rebuildSurfaceState();

        Assert::IsTrue(mgr.currentThemeMask().has_value());
        Assert::AreEqual(std::string("blue"), mgr.currentThemeMask()->tokens[0].value);
    }

    TEST_METHOD(OverlaySchema_Merged)
    {
        UISurfaceManager mgr;

        OverlaySchema schema1;
        schema1.navigationPointers.push_back(
            NavigationPointer{.pointerID = "p1", .label = "Ptr 1", .baseDestinationID = "home"});
        schema1.overlayRoutes.push_back(
            OverlayRoute{.routeID = "r1", .label = "Route 1",
                .presentation = OverlayPresentation::Sheet,
                .destination = BaseOverlayDestination{"home"}});

        OverlaySchema schema2;
        schema2.navigationPointers.push_back(
            NavigationPointer{.pointerID = "p2", .label = "Ptr 2", .baseDestinationID = "settings"});
        schema2.overlayRoutes.push_back(
            OverlayRoute{.routeID = "r2", .label = "Route 2",
                .presentation = OverlayPresentation::FullScreen,
                .destination = BaseOverlayDestination{"settings"}});

        mgr.addModuleContributions("mod.a", makeContributions({}, {}, std::nullopt, schema1));
        mgr.addModuleContributions("mod.b", makeContributions({}, {}, std::nullopt, schema2));
        mgr.rebuildSurfaceState();

        Assert::IsTrue(mgr.currentOverlaySchema().has_value());
        Assert::AreEqual(size_t(2), mgr.currentOverlaySchema()->navigationPointers.size());
        Assert::AreEqual(size_t(2), mgr.currentOverlaySchema()->overlayRoutes.size());
    }

    TEST_METHOD(OverlaySchema_DeduplicatesByID)
    {
        UISurfaceManager mgr;

        OverlaySchema schema1;
        schema1.navigationPointers.push_back(
            NavigationPointer{.pointerID = "p1", .label = "Ptr 1", .baseDestinationID = "home"});

        OverlaySchema schema2;
        schema2.navigationPointers.push_back(
            NavigationPointer{.pointerID = "p1", .label = "Ptr 1 Dupe", .baseDestinationID = "home"});

        mgr.addModuleContributions("mod.a", makeContributions({}, {}, std::nullopt, schema1));
        mgr.addModuleContributions("mod.b", makeContributions({}, {}, std::nullopt, schema2));
        mgr.rebuildSurfaceState();

        Assert::AreEqual(size_t(1), mgr.currentOverlaySchema()->navigationPointers.size());
    }

    TEST_METHOD(RemoveModuleContributions_ClearsModule)
    {
        UISurfaceManager mgr;

        ToolbarItemDescriptor item{
            .itemID = "item1", .title = "Item 1",
            .systemImageName = "icon", .action = NavigateAction{"home"}};

        mgr.addModuleContributions("mod.a", makeContributions({item}));
        mgr.rebuildSurfaceState();
        Assert::AreEqual(size_t(1), mgr.currentToolbarItems().size());

        mgr.removeModuleContributions("mod.a");
        mgr.rebuildSurfaceState();
        Assert::AreEqual(size_t(0), mgr.currentToolbarItems().size());
    }

    TEST_METHOD(OnChanged_CallbackInvoked)
    {
        UISurfaceManager mgr;
        int callCount = 0;
        mgr.onChanged([&callCount]() { callCount++; });

        mgr.rebuildSurfaceState();
        Assert::AreEqual(1, callCount);

        mgr.rebuildSurfaceState();
        Assert::AreEqual(2, callCount);
    }
};
