// Forsetti Framework for Windows
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential. Patent Pending.

#include "ExampleModules.h"

namespace Forsetti {

// ===========================================================================
// ExampleServiceModule
// ===========================================================================

ModuleDescriptor ExampleServiceModule::descriptor() const {
    return ModuleDescriptor{
        .moduleID    = "com.forsetti.module.example-service",
        .displayName = "Example Service",
        .version     = SemVer{0, 1, 0},
        .type        = ModuleType::Service
    };
}

ModuleManifest ExampleServiceModule::manifest() const {
    return ModuleManifest{
        .schemaVersion          = "1.0",
        .moduleID               = "com.forsetti.module.example-service",
        .displayName            = "Example Service",
        .moduleVersion          = SemVer{0, 1, 0},
        .moduleType             = ModuleType::Service,
        .supportedPlatforms     = { Platform::Windows },
        .minForsettiVersion     = SemVer{0, 1, 0},
        .maxForsettiVersion     = std::nullopt,
        .capabilitiesRequested  = { Capability::Storage, Capability::Telemetry },
        .iapProductID           = std::nullopt,
        .entryPoint             = "ExampleServiceModule"
    };
}

void ExampleServiceModule::start(ForsettiContext& context) {
    context.publishFrameworkEvent(ForsettiEvent{
        .type    = "example.service.started",
        .payload = {}
    });
}

void ExampleServiceModule::stop(ForsettiContext& context) {
    context.publishFrameworkEvent(ForsettiEvent{
        .type    = "example.service.stopped",
        .payload = {}
    });
}

// ===========================================================================
// ExampleUIModule
// ===========================================================================

ModuleDescriptor ExampleUIModule::descriptor() const {
    return ModuleDescriptor{
        .moduleID    = "com.forsetti.module.example-ui",
        .displayName = "Example UI",
        .version     = SemVer{0, 1, 0},
        .type        = ModuleType::UI
    };
}

ModuleManifest ExampleUIModule::manifest() const {
    return ModuleManifest{
        .schemaVersion          = "1.0",
        .moduleID               = "com.forsetti.module.example-ui",
        .displayName            = "Example UI",
        .moduleVersion          = SemVer{0, 1, 0},
        .moduleType             = ModuleType::UI,
        .supportedPlatforms     = { Platform::Windows },
        .minForsettiVersion     = SemVer{0, 1, 0},
        .maxForsettiVersion     = std::nullopt,
        .capabilitiesRequested  = {
            Capability::RoutingOverlay,
            Capability::ToolbarItems,
            Capability::ViewInjection
        },
        .iapProductID           = "com.forsetti.iap.example-ui",
        .entryPoint             = "ExampleUIModule"
    };
}

UIContributions ExampleUIModule::uiContributions() const {
    // Toolbar items
    std::vector<ToolbarItemDescriptor> toolbarItems = {
        ToolbarItemDescriptor{
            .itemID          = "example-toolbar-1",
            .title           = "Example Action",
            .systemImageName = "star.fill",
            .action          = NavigateAction{"home"}
        }
    };

    // View injections
    std::vector<ViewInjectionDescriptor> viewInjections = {
        ViewInjectionDescriptor{
            .injectionID = "example-home-banner",
            .slot        = "homeBanner",
            .viewID      = "ExampleHomeBannerView",
            .priority    = 100
        },
        ViewInjectionDescriptor{
            .injectionID = "example-dashboard",
            .slot        = "dashboardPrimary",
            .viewID      = "ExampleDashboardView",
            .priority    = 50
        },
        ViewInjectionDescriptor{
            .injectionID = "example-overlay",
            .slot        = "overlayMain",
            .viewID      = "ExampleOverlayView",
            .priority    = 0
        }
    };

    // Overlay schema
    OverlaySchema overlaySchema{
        .navigationPointers = {
            NavigationPointer{
                .pointerID         = "example-pointer-1",
                .label             = "Example Pointer",
                .baseDestinationID = "home"
            }
        },
        .overlayRoutes = {
            OverlayRoute{
                .routeID      = "example-route-1",
                .label        = "Example Route",
                .presentation = OverlayPresentation::Sheet,
                .destination  = ModuleOverlayDestination{
                    "com.forsetti.module.example-ui",
                    "ExampleOverlayView"
                }
            }
        }
    };

    return UIContributions{
        .themeMask      = std::nullopt,
        .toolbarItems   = std::move(toolbarItems),
        .viewInjections = std::move(viewInjections),
        .overlaySchema  = std::move(overlaySchema)
    };
}

void ExampleUIModule::start(ForsettiContext& context) {
    context.publishFrameworkEvent(ForsettiEvent{
        .type    = "example.ui.started",
        .payload = {}
    });
}

void ExampleUIModule::stop(ForsettiContext& context) {
    context.publishFrameworkEvent(ForsettiEvent{
        .type    = "example.ui.stopped",
        .payload = {}
    });
}

} // namespace Forsetti
