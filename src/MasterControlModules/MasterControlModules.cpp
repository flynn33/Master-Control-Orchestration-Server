// Master Control Program
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#include "MasterControl/MasterControlModules.h"

#include "ForsettiCore/ForsettiContext.h"
#include "MasterControl/MasterControlContracts.h"

#include <utility>

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
        std::nullopt,
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
        "EnvironmentDiscoveryModule");
}

void EnvironmentDiscoveryModule::start(Forsetti::ForsettiContext& context) {
    publishLifecycleEvent(context, "mastercontrol.environment.started", descriptor().moduleID);
}

void EnvironmentDiscoveryModule::stop(Forsetti::ForsettiContext& context) {
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
        "HostTelemetryModule");
}

void HostTelemetryModule::start(Forsetti::ForsettiContext& context) {
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
        "RuntimeInventoryModule");
}

void RuntimeInventoryModule::start(Forsetti::ForsettiContext& context) {
    if (const auto service = context.services()->resolve<IRuntimeInventoryService>()) {
        service->refresh();
    }
    publishLifecycleEvent(context, "mastercontrol.inventory.started", descriptor().moduleID);
}

void RuntimeInventoryModule::stop(Forsetti::ForsettiContext& context) {
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
        "ConfigurationModule");
}

void ConfigurationModule::start(Forsetti::ForsettiContext& context) {
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
        "InstallerImportModule");
}

void InstallerImportModule::start(Forsetti::ForsettiContext& context) {
    publishLifecycleEvent(context, "mastercontrol.installer.started", descriptor().moduleID);
}

void InstallerImportModule::stop(Forsetti::ForsettiContext& context) {
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
        "ProviderIntegrationModule");
}

void ProviderIntegrationModule::start(Forsetti::ForsettiContext& context) {
    publishLifecycleEvent(context, "mastercontrol.providers.started", descriptor().moduleID);
}

void ProviderIntegrationModule::stop(Forsetti::ForsettiContext& context) {
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
        "ExportModule");
}

void ExportModule::start(Forsetti::ForsettiContext& context) {
    publishLifecycleEvent(context, "mastercontrol.export.started", descriptor().moduleID);
}

void ExportModule::stop(Forsetti::ForsettiContext& context) {
    publishLifecycleEvent(context, "mastercontrol.export.stopped", descriptor().moduleID);
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
        "BeaconGatewayModule");
}

void BeaconGatewayModule::start(Forsetti::ForsettiContext& context) {
    publishLifecycleEvent(context, "mastercontrol.beacon.started", descriptor().moduleID);
}

void BeaconGatewayModule::stop(Forsetti::ForsettiContext& context) {
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
        "DashboardUIModule");
}

Forsetti::UIContributions DashboardUIModule::uiContributions() const {
    return Forsetti::UIContributions{
        std::nullopt,
        {
            Forsetti::ToolbarItemDescriptor{ "dashboard-home", "Overview", "network", Forsetti::NavigateAction{ "overview" } },
            Forsetti::ToolbarItemDescriptor{ "dashboard-telemetry", "Telemetry", "trackers", Forsetti::NavigateAction{ "telemetry" } },
            Forsetti::ToolbarItemDescriptor{ "dashboard-runtime", "Runtime", "globe", Forsetti::NavigateAction{ "runtime" } },
            Forsetti::ToolbarItemDescriptor{ "dashboard-import", "Imports", "arrow.down", Forsetti::OpenOverlayAction{ "imports-overlay" } },
            Forsetti::ToolbarItemDescriptor{ "dashboard-export", "Exports", "share", Forsetti::OpenOverlayAction{ "exports-overlay" } },
            Forsetti::ToolbarItemDescriptor{ "dashboard-settings", "Settings", "gear", Forsetti::OpenOverlayAction{ "settings-overlay" } }
        },
        {
            Forsetti::ViewInjectionDescriptor{ "dashboard-hero", "dashboardPrimary", "MasterControlDashboardView", 100 },
            Forsetti::ViewInjectionDescriptor{ "dashboard-grid", "dashboardMetrics", "MasterControlMetricsGrid", 90 },
            Forsetti::ViewInjectionDescriptor{ "runtime-grid", "runtimePrimary", "MasterControlRuntimeGrid", 80 },
            Forsetti::ViewInjectionDescriptor{ "provider-grid", "providerPrimary", "MasterControlProviderGrid", 80 }
        },
        Forsetti::OverlaySchema{
            {
                Forsetti::NavigationPointer{ "overview-nav", "Overview", "overview" },
                Forsetti::NavigationPointer{ "telemetry-nav", "Telemetry", "telemetry" },
                Forsetti::NavigationPointer{ "runtime-nav", "Runtime", "runtime" },
                Forsetti::NavigationPointer{ "providers-nav", "Providers", "providers" },
                Forsetti::NavigationPointer{ "imports-nav", "Imports", "imports" },
                Forsetti::NavigationPointer{ "exports-nav", "Exports", "exports" },
                Forsetti::NavigationPointer{ "security-nav", "Security", "security" },
                Forsetti::NavigationPointer{ "settings-nav", "Settings", "settings" }
            },
            {
                Forsetti::OverlayRoute{
                    "settings-overlay",
                    "Settings",
                    Forsetti::OverlayPresentation::Sheet,
                    Forsetti::ModuleOverlayDestination{ descriptor().moduleID, "SettingsOverlayView" }
                },
                Forsetti::OverlayRoute{
                    "imports-overlay",
                    "Imports",
                    Forsetti::OverlayPresentation::Sheet,
                    Forsetti::ModuleOverlayDestination{ descriptor().moduleID, "ImportOverlayView" }
                },
                Forsetti::OverlayRoute{
                    "exports-overlay",
                    "Exports",
                    Forsetti::OverlayPresentation::Sheet,
                    Forsetti::ModuleOverlayDestination{ descriptor().moduleID, "ExportOverlayView" }
                },
                Forsetti::OverlayRoute{
                    "security-overlay",
                    "Security",
                    Forsetti::OverlayPresentation::Sheet,
                    Forsetti::ModuleOverlayDestination{ descriptor().moduleID, "SecurityOverlayView" }
                }
            }
        }
    };
}

void DashboardUIModule::start(Forsetti::ForsettiContext& context) {
    publishLifecycleEvent(context, "mastercontrol.dashboard.started", descriptor().moduleID);
}

void DashboardUIModule::stop(Forsetti::ForsettiContext& context) {
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
    registry.registerModule("BeaconGatewayModule", []() -> std::unique_ptr<Forsetti::IForsettiModule> {
        return std::make_unique<BeaconGatewayModule>();
    });
    registry.registerModule("DashboardUIModule", []() -> std::unique_ptr<Forsetti::IForsettiModule> {
        return std::make_unique<DashboardUIModule>();
    });
}

} // namespace MasterControl
