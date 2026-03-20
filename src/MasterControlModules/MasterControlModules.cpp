// Master Control Program
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#include "MasterControl/MasterControlModules.h"

#include "ForsettiCore/ForsettiContext.h"
#include "MasterControl/MasterControlContracts.h"

#include <algorithm>
#include <set>
#include <tuple>
#include <utility>
#include <vector>

namespace MasterControl {

namespace {

Forsetti::ModuleDescriptor makeDescriptor(const std::string& moduleId,
                                          const std::string& displayName,
                                          const Forsetti::ModuleType moduleType) {
    return Forsetti::ModuleDescriptor{
        moduleId,
        displayName,
        Forsetti::SemVer{ 0, 1, 0 },
        moduleType
    };
}

Forsetti::ModuleManifest makeManifest(const std::string& moduleId,
                                      const std::string& displayName,
                                      const Forsetti::ModuleType moduleType,
                                      std::vector<Forsetti::Capability> capabilities,
                                      std::optional<std::string> iapProductId,
                                      const std::string& entryPoint) {
    return Forsetti::ModuleManifest{
        "1.0",
        moduleId,
        displayName,
        Forsetti::SemVer{ 0, 1, 0 },
        moduleType,
        { Forsetti::Platform::Windows },
        Forsetti::SemVer{ 0, 1, 0 },
        std::nullopt,
        std::move(capabilities),
        std::move(iapProductId),
        entryPoint
    };
}

void publishLifecycleEvent(Forsetti::ForsettiContext& context,
                           const std::string& eventType,
                           const std::string& moduleId) {
    context.publishFrameworkEvent(Forsetti::ForsettiEvent{
        eventType,
        { { "moduleID", moduleId } },
        moduleId
    });
}

std::vector<ModuleControlSurfaceRequest> makeEnvironmentDiscoveryControlSurfaceRequests() {
    return {
        ModuleControlSurfaceRequest{
            "com.mastercontrol.environment-discovery",
            "environment-overview",
            "Overview",
            "overview",
            "OverviewSection",
            "network",
            {},
            ControlSurfaceToolbarAction::Navigate,
            Forsetti::OverlayPresentation::Sheet,
            true,
            true,
            10
        }
    };
}

std::vector<ModuleControlSurfaceRequest> makeHostTelemetryControlSurfaceRequests() {
    return {
        ModuleControlSurfaceRequest{
            "com.mastercontrol.host-telemetry",
            "host-telemetry",
            "Telemetry",
            "telemetry",
            "TelemetrySection",
            "trackers",
            {},
            ControlSurfaceToolbarAction::Navigate,
            Forsetti::OverlayPresentation::Sheet,
            true,
            true,
            20
        }
    };
}

std::vector<ModuleControlSurfaceRequest> makeRuntimeInventoryControlSurfaceRequests() {
    return {
        ModuleControlSurfaceRequest{
            "com.mastercontrol.runtime-inventory",
            "runtime-inventory",
            "Runtime",
            "runtime",
            "RuntimeSection",
            "globe",
            {},
            ControlSurfaceToolbarAction::Navigate,
            Forsetti::OverlayPresentation::Sheet,
            true,
            true,
            30
        }
    };
}

std::vector<ModuleControlSurfaceRequest> makeConfigurationControlSurfaceRequests() {
    return {
        ModuleControlSurfaceRequest{
            "com.mastercontrol.configuration",
            "security-control-plane",
            "Security",
            "security",
            "SecuritySection",
            "shield",
            "security-overlay",
            ControlSurfaceToolbarAction::OpenOverlay,
            Forsetti::OverlayPresentation::Sheet,
            false,
            true,
            70
        },
        ModuleControlSurfaceRequest{
            "com.mastercontrol.configuration",
            "settings-control-plane",
            "Settings",
            "settings",
            "SettingsSection",
            "gear",
            "settings-overlay",
            ControlSurfaceToolbarAction::OpenOverlay,
            Forsetti::OverlayPresentation::Sheet,
            true,
            true,
            80
        }
    };
}

std::vector<ModuleControlSurfaceRequest> makeInstallerImportControlSurfaceRequests() {
    return {
        ModuleControlSurfaceRequest{
            "com.mastercontrol.installer-import",
            "installer-import",
            "Imports",
            "imports",
            "ImportsSection",
            "arrow.down",
            "imports-overlay",
            ControlSurfaceToolbarAction::OpenOverlay,
            Forsetti::OverlayPresentation::Sheet,
            true,
            true,
            50
        }
    };
}

std::vector<ModuleControlSurfaceRequest> makeProviderIntegrationControlSurfaceRequests() {
    return {
        ModuleControlSurfaceRequest{
            "com.mastercontrol.provider-integration",
            "provider-integration",
            "Providers",
            "providers",
            "ProvidersSection",
            "plug",
            {},
            ControlSurfaceToolbarAction::Navigate,
            Forsetti::OverlayPresentation::Sheet,
            false,
            true,
            40
        }
    };
}

std::vector<ModuleControlSurfaceRequest> makeExportControlSurfaceRequests() {
    return {
        ModuleControlSurfaceRequest{
            "com.mastercontrol.export",
            "export-artifacts",
            "Exports",
            "exports",
            "ExportsSection",
            "share",
            "exports-overlay",
            ControlSurfaceToolbarAction::OpenOverlay,
            Forsetti::OverlayPresentation::Sheet,
            true,
            true,
            60
        }
    };
}

std::vector<ModuleControlSurfaceRequest> makeCommandLogicUnitControlSurfaceRequests() {
    return {
        ModuleControlSurfaceRequest{
            "com.mastercontrol.command-logic-unit",
            "command-logic-unit",
            "CLU",
            "clu",
            "GovernanceSection",
            "shield",
            {},
            ControlSurfaceToolbarAction::Navigate,
            Forsetti::OverlayPresentation::Sheet,
            true,
            true,
            90
        }
    };
}

std::vector<ModuleControlSurfaceRequest> makeBeaconGatewayControlSurfaceRequests() {
    return {
        ModuleControlSurfaceRequest{
            "com.mastercontrol.beacon-gateway",
            "beacon-runtime-status",
            "Gateway Status",
            "runtime",
            "RuntimeSection",
            "antenna.radiowaves.left.and.right",
            {},
            ControlSurfaceToolbarAction::Navigate,
            Forsetti::OverlayPresentation::Sheet,
            false,
            false,
            35
        }
    };
}

void registerControlSurfaceRequests(Forsetti::ForsettiContext& context,
                                    const std::vector<ModuleControlSurfaceRequest>& requests) {
    const auto controlSurfaceService = context.services()->resolve<IModuleControlSurfaceService>();
    if (!controlSurfaceService) {
        return;
    }

    for (const auto& request : requests) {
        controlSurfaceService->upsertControlSurfaceRequest(request);
        context.publishFrameworkEvent(Forsetti::ForsettiEvent{
            "mastercontrol.ui-contract.changed",
            {
                { "moduleID", request.moduleId },
                { "featureID", request.featureId },
                { "action", "upsert" }
            },
            request.moduleId
        });
    }
}

void unregisterControlSurfaceRequests(Forsetti::ForsettiContext& context,
                                      const std::string& moduleId) {
    const auto controlSurfaceService = context.services()->resolve<IModuleControlSurfaceService>();
    if (!controlSurfaceService) {
        return;
    }

    controlSurfaceService->removeControlSurfaceRequestsForModule(moduleId);
    context.publishFrameworkEvent(Forsetti::ForsettiEvent{
        "mastercontrol.ui-contract.changed",
        {
            { "moduleID", moduleId },
            { "action", "remove" }
        },
        moduleId
    });
}

std::string viewIdForSurfaceTemplate(const std::string& surfaceTemplateId) {
    if (surfaceTemplateId == "OverviewSection") {
        return "OverviewSectionView";
    }
    if (surfaceTemplateId == "TelemetrySection") {
        return "TelemetrySectionView";
    }
    if (surfaceTemplateId == "RuntimeSection") {
        return "RuntimeSectionView";
    }
    if (surfaceTemplateId == "ProvidersSection") {
        return "ProvidersSectionView";
    }
    if (surfaceTemplateId == "ImportsSection") {
        return "ImportsSectionView";
    }
    if (surfaceTemplateId == "ExportsSection") {
        return "ExportsSectionView";
    }
    if (surfaceTemplateId == "SecuritySection") {
        return "SecuritySectionView";
    }
    if (surfaceTemplateId == "SettingsSection") {
        return "SettingsSectionView";
    }
    if (surfaceTemplateId == "GovernanceSection") {
        return "CommandLogicUnitSectionView";
    }
    return {};
}

Forsetti::UIContributions composeDashboardSurface(
    const std::vector<ModuleControlSurfaceRequest>& controlSurfaceRequests) {
    auto requests = controlSurfaceRequests;
    std::sort(
        requests.begin(),
        requests.end(),
        [](const ModuleControlSurfaceRequest& left, const ModuleControlSurfaceRequest& right) {
            return std::tie(left.sortOrder, left.displayName, left.featureId) <
                std::tie(right.sortOrder, right.displayName, right.featureId);
        });

    Forsetti::UIContributions composed;
    composed.overlaySchema = Forsetti::OverlaySchema{};

    std::set<std::string> toolbarItemIds;
    std::set<std::string> navigationDestinations;
    std::set<std::string> overlayRouteIds;
    std::set<std::string> viewKeys;

    for (const auto& request : requests) {
        const auto viewId = viewIdForSurfaceTemplate(request.surfaceTemplateId);
        if (request.destinationId.empty() || viewId.empty()) {
            continue;
        }

        const auto viewKey = request.destinationId + "|" + viewId;
        if (viewKeys.insert(viewKey).second) {
            composed.viewInjections.push_back(Forsetti::ViewInjectionDescriptor{
                request.featureId + "-surface",
                request.destinationId,
                viewId,
                1000 - request.sortOrder
            });
        }

        if (!request.overlayRouteId.empty() && overlayRouteIds.insert(request.overlayRouteId).second) {
            composed.overlaySchema->overlayRoutes.push_back(Forsetti::OverlayRoute{
                request.overlayRouteId,
                request.displayName,
                request.overlayPresentation,
                Forsetti::ModuleOverlayDestination{ "com.mastercontrol.dashboard-ui", viewId }
            });
        }

        if (request.includeToolbarShortcut) {
            const auto toolbarItemId = request.featureId + "-dashboard";
            if (toolbarItemIds.insert(toolbarItemId).second) {
                const auto action = request.toolbarAction == ControlSurfaceToolbarAction::OpenOverlay &&
                        !request.overlayRouteId.empty()
                    ? Forsetti::ToolbarAction{ Forsetti::OpenOverlayAction{ request.overlayRouteId } }
                    : Forsetti::ToolbarAction{ Forsetti::NavigateAction{ request.destinationId } };

                composed.toolbarItems.push_back(Forsetti::ToolbarItemDescriptor{
                    toolbarItemId,
                    request.displayName,
                    request.toolbarIcon,
                    action
                });
            }
        }

        if (request.includeNavigationLane && navigationDestinations.insert(request.destinationId).second) {
            composed.overlaySchema->navigationPointers.push_back(Forsetti::NavigationPointer{
                request.featureId + "-nav",
                request.displayName,
                request.destinationId
            });
        }
    }

    return composed;
}

void publishDashboardSurface(const std::shared_ptr<Forsetti::ServiceContainer>& services,
                             const std::shared_ptr<Forsetti::IForsettiEventBus>& eventBus,
                             const std::string& moduleId) {
    const auto controlSurfaceService = services->resolve<IModuleControlSurfaceService>();
    const auto surfaceService = services->resolve<IForsettiSurfaceService>();
    if (!controlSurfaceService || !surfaceService) {
        return;
    }

    const auto requests = controlSurfaceService->listControlSurfaceRequests();
    const auto composed = composeDashboardSurface(requests);
    surfaceService->publishModuleSurface(
        moduleId,
        composed);

    if (eventBus) {
        eventBus->publish(Forsetti::ForsettiEvent{
            "mastercontrol.dashboard.surface.registered",
            {
                { "moduleID", moduleId },
                { "requestCount", std::to_string(requests.size()) },
                { "toolbarCount", std::to_string(composed.toolbarItems.size()) },
                { "viewInjectionCount", std::to_string(composed.viewInjections.size()) },
                { "overlayRouteCount", composed.overlaySchema.has_value()
                        ? std::to_string(composed.overlaySchema->overlayRoutes.size())
                        : "0" }
            },
            moduleId
        });
    }
}

} // namespace

Forsetti::ModuleDescriptor EnvironmentDiscoveryModule::descriptor() const {
    return makeDescriptor("com.mastercontrol.environment-discovery", "Environment Discovery", Forsetti::ModuleType::Service);
}

Forsetti::ModuleManifest EnvironmentDiscoveryModule::manifest() const {
    return makeManifest(
        "com.mastercontrol.environment-discovery",
        "Environment Discovery",
        Forsetti::ModuleType::Service,
        { Forsetti::Capability::Storage, Forsetti::Capability::Telemetry, Forsetti::Capability::EventPublishing },
        std::nullopt,
        "EnvironmentDiscoveryModule");
}

void EnvironmentDiscoveryModule::start(Forsetti::ForsettiContext& context) {
    registerControlSurfaceRequests(context, makeEnvironmentDiscoveryControlSurfaceRequests());
    publishLifecycleEvent(context, "mastercontrol.environment.started", descriptor().moduleID);
}

void EnvironmentDiscoveryModule::stop(Forsetti::ForsettiContext& context) {
    unregisterControlSurfaceRequests(context, descriptor().moduleID);
    publishLifecycleEvent(context, "mastercontrol.environment.stopped", descriptor().moduleID);
}

Forsetti::ModuleDescriptor HostTelemetryModule::descriptor() const {
    return makeDescriptor("com.mastercontrol.host-telemetry", "Host Telemetry", Forsetti::ModuleType::Service);
}

Forsetti::ModuleManifest HostTelemetryModule::manifest() const {
    return makeManifest(
        "com.mastercontrol.host-telemetry",
        "Host Telemetry",
        Forsetti::ModuleType::Service,
        { Forsetti::Capability::Telemetry, Forsetti::Capability::EventPublishing },
        std::nullopt,
        "HostTelemetryModule");
}

void HostTelemetryModule::start(Forsetti::ForsettiContext& context) {
    registerControlSurfaceRequests(context, makeHostTelemetryControlSurfaceRequests());
    if (const auto service = context.services()->resolve<ITelemetryService>()) {
        const auto snapshot = service->captureSnapshot();
        context.publishFrameworkEvent(Forsetti::ForsettiEvent{
            "mastercontrol.telemetry.snapshot",
            {
                { "hostName", snapshot.hostName },
                { "primaryIpAddress", snapshot.primaryIpAddress },
                { "capturedAtUtc", snapshot.capturedAtUtc }
            },
            descriptor().moduleID
        });
    }
    publishLifecycleEvent(context, "mastercontrol.telemetry.started", descriptor().moduleID);
}

void HostTelemetryModule::stop(Forsetti::ForsettiContext& context) {
    unregisterControlSurfaceRequests(context, descriptor().moduleID);
    publishLifecycleEvent(context, "mastercontrol.telemetry.stopped", descriptor().moduleID);
}

Forsetti::ModuleDescriptor RuntimeInventoryModule::descriptor() const {
    return makeDescriptor("com.mastercontrol.runtime-inventory", "Runtime Inventory", Forsetti::ModuleType::Service);
}

Forsetti::ModuleManifest RuntimeInventoryModule::manifest() const {
    return makeManifest(
        "com.mastercontrol.runtime-inventory",
        "Runtime Inventory",
        Forsetti::ModuleType::Service,
        { Forsetti::Capability::Networking, Forsetti::Capability::Telemetry, Forsetti::Capability::EventPublishing },
        std::nullopt,
        "RuntimeInventoryModule");
}

void RuntimeInventoryModule::start(Forsetti::ForsettiContext& context) {
    registerControlSurfaceRequests(context, makeRuntimeInventoryControlSurfaceRequests());
    if (const auto service = context.services()->resolve<IRuntimeInventoryService>()) {
        service->refresh();
    }
    publishLifecycleEvent(context, "mastercontrol.inventory.started", descriptor().moduleID);
}

void RuntimeInventoryModule::stop(Forsetti::ForsettiContext& context) {
    unregisterControlSurfaceRequests(context, descriptor().moduleID);
    publishLifecycleEvent(context, "mastercontrol.inventory.stopped", descriptor().moduleID);
}

Forsetti::ModuleDescriptor ConfigurationModule::descriptor() const {
    return makeDescriptor("com.mastercontrol.configuration", "Configuration", Forsetti::ModuleType::Service);
}

Forsetti::ModuleManifest ConfigurationModule::manifest() const {
    return makeManifest(
        "com.mastercontrol.configuration",
        "Configuration",
        Forsetti::ModuleType::Service,
        { Forsetti::Capability::Storage, Forsetti::Capability::SecureStorage, Forsetti::Capability::EventPublishing },
        std::nullopt,
        "ConfigurationModule");
}

void ConfigurationModule::start(Forsetti::ForsettiContext& context) {
    registerControlSurfaceRequests(context, makeConfigurationControlSurfaceRequests());
    if (const auto service = context.services()->resolve<IConfigurationService>()) {
        const auto configuration = service->current();
        context.publishFrameworkEvent(Forsetti::ForsettiEvent{
            "mastercontrol.configuration.loaded",
            {
                { "instanceName", configuration.instanceName },
                { "bindAddress", configuration.bindAddress },
                { "browserPort", std::to_string(configuration.browserPort) }
            },
            descriptor().moduleID
        });
    }
    publishLifecycleEvent(context, "mastercontrol.configuration.started", descriptor().moduleID);
}

void ConfigurationModule::stop(Forsetti::ForsettiContext& context) {
    unregisterControlSurfaceRequests(context, descriptor().moduleID);
    publishLifecycleEvent(context, "mastercontrol.configuration.stopped", descriptor().moduleID);
}

Forsetti::ModuleDescriptor InstallerImportModule::descriptor() const {
    return makeDescriptor("com.mastercontrol.installer-import", "Installer Import", Forsetti::ModuleType::Service);
}

Forsetti::ModuleManifest InstallerImportModule::manifest() const {
    return makeManifest(
        "com.mastercontrol.installer-import",
        "Installer Import",
        Forsetti::ModuleType::Service,
        { Forsetti::Capability::Networking, Forsetti::Capability::Storage, Forsetti::Capability::FileExport, Forsetti::Capability::EventPublishing },
        "mastercontrol.iap.installer-import",
        "InstallerImportModule");
}

void InstallerImportModule::start(Forsetti::ForsettiContext& context) {
    registerControlSurfaceRequests(context, makeInstallerImportControlSurfaceRequests());
    publishLifecycleEvent(context, "mastercontrol.installer.started", descriptor().moduleID);
}

void InstallerImportModule::stop(Forsetti::ForsettiContext& context) {
    unregisterControlSurfaceRequests(context, descriptor().moduleID);
    publishLifecycleEvent(context, "mastercontrol.installer.stopped", descriptor().moduleID);
}

Forsetti::ModuleDescriptor ProviderIntegrationModule::descriptor() const {
    return makeDescriptor("com.mastercontrol.provider-integration", "Provider Integration", Forsetti::ModuleType::Service);
}

Forsetti::ModuleManifest ProviderIntegrationModule::manifest() const {
    return makeManifest(
        "com.mastercontrol.provider-integration",
        "Provider Integration",
        Forsetti::ModuleType::Service,
        { Forsetti::Capability::Networking, Forsetti::Capability::Storage, Forsetti::Capability::EventPublishing },
        "mastercontrol.iap.provider-integration",
        "ProviderIntegrationModule");
}

void ProviderIntegrationModule::start(Forsetti::ForsettiContext& context) {
    registerControlSurfaceRequests(context, makeProviderIntegrationControlSurfaceRequests());
    publishLifecycleEvent(context, "mastercontrol.providers.started", descriptor().moduleID);
}

void ProviderIntegrationModule::stop(Forsetti::ForsettiContext& context) {
    unregisterControlSurfaceRequests(context, descriptor().moduleID);
    publishLifecycleEvent(context, "mastercontrol.providers.stopped", descriptor().moduleID);
}

Forsetti::ModuleDescriptor ExportModule::descriptor() const {
    return makeDescriptor("com.mastercontrol.export", "Export", Forsetti::ModuleType::Service);
}

Forsetti::ModuleManifest ExportModule::manifest() const {
    return makeManifest(
        "com.mastercontrol.export",
        "Export",
        Forsetti::ModuleType::Service,
        { Forsetti::Capability::FileExport, Forsetti::Capability::Storage, Forsetti::Capability::EventPublishing },
        "mastercontrol.iap.export",
        "ExportModule");
}

void ExportModule::start(Forsetti::ForsettiContext& context) {
    registerControlSurfaceRequests(context, makeExportControlSurfaceRequests());
    publishLifecycleEvent(context, "mastercontrol.export.started", descriptor().moduleID);
}

void ExportModule::stop(Forsetti::ForsettiContext& context) {
    unregisterControlSurfaceRequests(context, descriptor().moduleID);
    publishLifecycleEvent(context, "mastercontrol.export.stopped", descriptor().moduleID);
}

Forsetti::ModuleDescriptor CommandLogicUnitModule::descriptor() const {
    return makeDescriptor("com.mastercontrol.command-logic-unit", "Command Logic Unit", Forsetti::ModuleType::Service);
}

Forsetti::ModuleManifest CommandLogicUnitModule::manifest() const {
    return makeManifest(
        "com.mastercontrol.command-logic-unit",
        "Command Logic Unit",
        Forsetti::ModuleType::Service,
        { Forsetti::Capability::Storage, Forsetti::Capability::Telemetry, Forsetti::Capability::EventPublishing },
        "mastercontrol.iap.command-logic-unit",
        "CommandLogicUnitModule");
}

void CommandLogicUnitModule::start(Forsetti::ForsettiContext& context) {
    registerControlSurfaceRequests(context, makeCommandLogicUnitControlSurfaceRequests());
    if (const auto service = context.services()->resolve<ICommandLogicUnitService>()) {
        const auto governance = service->currentGovernance();
        context.publishFrameworkEvent(Forsetti::ForsettiEvent{
            "mastercontrol.clu.evaluated",
            {
                { "unitName", governance.unitName },
                { "posture", governance.posture },
                { "findingCount", std::to_string(governance.findings.size()) }
            },
            descriptor().moduleID
        });
    }
    publishLifecycleEvent(context, "mastercontrol.clu.started", descriptor().moduleID);
}

void CommandLogicUnitModule::stop(Forsetti::ForsettiContext& context) {
    unregisterControlSurfaceRequests(context, descriptor().moduleID);
    publishLifecycleEvent(context, "mastercontrol.clu.stopped", descriptor().moduleID);
}

Forsetti::ModuleDescriptor BeaconGatewayModule::descriptor() const {
    return makeDescriptor("com.mastercontrol.beacon-gateway", "Beacon Gateway", Forsetti::ModuleType::Service);
}

Forsetti::ModuleManifest BeaconGatewayModule::manifest() const {
    return makeManifest(
        "com.mastercontrol.beacon-gateway",
        "Beacon Gateway",
        Forsetti::ModuleType::Service,
        { Forsetti::Capability::Networking, Forsetti::Capability::Telemetry, Forsetti::Capability::EventPublishing },
        "mastercontrol.iap.beacon-gateway",
        "BeaconGatewayModule");
}

void BeaconGatewayModule::start(Forsetti::ForsettiContext& context) {
    registerControlSurfaceRequests(context, makeBeaconGatewayControlSurfaceRequests());
    publishLifecycleEvent(context, "mastercontrol.beacon.started", descriptor().moduleID);
}

void BeaconGatewayModule::stop(Forsetti::ForsettiContext& context) {
    unregisterControlSurfaceRequests(context, descriptor().moduleID);
    publishLifecycleEvent(context, "mastercontrol.beacon.stopped", descriptor().moduleID);
}

Forsetti::ModuleDescriptor DashboardUIModule::descriptor() const {
    return makeDescriptor("com.mastercontrol.dashboard-ui", "Dashboard UI", Forsetti::ModuleType::UI);
}

Forsetti::ModuleManifest DashboardUIModule::manifest() const {
    return makeManifest(
        "com.mastercontrol.dashboard-ui",
        "Dashboard UI",
        Forsetti::ModuleType::UI,
        {
            Forsetti::Capability::RoutingOverlay,
            Forsetti::Capability::ToolbarItems,
            Forsetti::Capability::ViewInjection,
            Forsetti::Capability::EventPublishing
        },
        std::nullopt,
        "DashboardUIModule");
}

Forsetti::UIContributions DashboardUIModule::uiContributions() const {
    return Forsetti::UIContributions{};
}

void DashboardUIModule::start(Forsetti::ForsettiContext& context) {
    publishDashboardSurface(context.services(), context.eventBus(), descriptor().moduleID);
    controlSurfaceSubscription_ = context.subscribeToFrameworkEvents(
        "mastercontrol.ui-contract.changed",
        [this, services = context.services(), eventBus = context.eventBus()](const Forsetti::ForsettiEvent&) {
            publishDashboardSurface(services, eventBus, descriptor().moduleID);
        });
    publishLifecycleEvent(context, "mastercontrol.dashboard.started", descriptor().moduleID);
}

void DashboardUIModule::stop(Forsetti::ForsettiContext& context) {
    if (controlSurfaceSubscription_.has_value()) {
        controlSurfaceSubscription_->cancel();
        controlSurfaceSubscription_.reset();
    }
    publishLifecycleEvent(context, "mastercontrol.dashboard.stopped", descriptor().moduleID);
}

void registerMasterControlModules(Forsetti::ModuleRegistry& registry) {
    registry.registerModule("EnvironmentDiscoveryModule", []() -> std::unique_ptr<Forsetti::IForsettiModule> {
        return std::make_unique<EnvironmentDiscoveryModule>();
    });
    registry.registerModule("HostTelemetryModule", []() -> std::unique_ptr<Forsetti::IForsettiModule> {
        return std::make_unique<HostTelemetryModule>();
    });
    registry.registerModule("RuntimeInventoryModule", []() -> std::unique_ptr<Forsetti::IForsettiModule> {
        return std::make_unique<RuntimeInventoryModule>();
    });
    registry.registerModule("ConfigurationModule", []() -> std::unique_ptr<Forsetti::IForsettiModule> {
        return std::make_unique<ConfigurationModule>();
    });
    registry.registerModule("InstallerImportModule", []() -> std::unique_ptr<Forsetti::IForsettiModule> {
        return std::make_unique<InstallerImportModule>();
    });
    registry.registerModule("ProviderIntegrationModule", []() -> std::unique_ptr<Forsetti::IForsettiModule> {
        return std::make_unique<ProviderIntegrationModule>();
    });
    registry.registerModule("ExportModule", []() -> std::unique_ptr<Forsetti::IForsettiModule> {
        return std::make_unique<ExportModule>();
    });
    registry.registerModule("CommandLogicUnitModule", []() -> std::unique_ptr<Forsetti::IForsettiModule> {
        return std::make_unique<CommandLogicUnitModule>();
    });
    registry.registerModule("BeaconGatewayModule", []() -> std::unique_ptr<Forsetti::IForsettiModule> {
        return std::make_unique<BeaconGatewayModule>();
    });
    registry.registerModule("DashboardUIModule", []() -> std::unique_ptr<Forsetti::IForsettiModule> {
        return std::make_unique<DashboardUIModule>();
    });
}

} // namespace MasterControl
