// Master Control Program
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#pragma once

#include "ForsettiCore/ForsettiProtocols.h"
#include "ForsettiCore/ForsettiEventBus.h"
#include "ForsettiCore/ModuleRegistry.h"
#include "MasterControl/MasterControlModels.h"

#include <optional>

namespace MasterControl {

class EnvironmentDiscoveryModule final : public Forsetti::IForsettiModule {
public:
    Forsetti::ModuleDescriptor descriptor() const override;
    Forsetti::ModuleManifest manifest() const override;
    void start(Forsetti::ForsettiContext& context) override;
    void stop(Forsetti::ForsettiContext& context) override;
};

class HostTelemetryModule final : public Forsetti::IForsettiModule {
public:
    Forsetti::ModuleDescriptor descriptor() const override;
    Forsetti::ModuleManifest manifest() const override;
    void start(Forsetti::ForsettiContext& context) override;
    void stop(Forsetti::ForsettiContext& context) override;
};

class RuntimeInventoryModule final : public Forsetti::IForsettiModule {
public:
    Forsetti::ModuleDescriptor descriptor() const override;
    Forsetti::ModuleManifest manifest() const override;
    void start(Forsetti::ForsettiContext& context) override;
    void stop(Forsetti::ForsettiContext& context) override;
};

class ConfigurationModule final : public Forsetti::IForsettiModule {
public:
    Forsetti::ModuleDescriptor descriptor() const override;
    Forsetti::ModuleManifest manifest() const override;
    void start(Forsetti::ForsettiContext& context) override;
    void stop(Forsetti::ForsettiContext& context) override;
};

class InstallerImportModule final : public Forsetti::IForsettiModule {
public:
    Forsetti::ModuleDescriptor descriptor() const override;
    Forsetti::ModuleManifest manifest() const override;
    void start(Forsetti::ForsettiContext& context) override;
    void stop(Forsetti::ForsettiContext& context) override;
};

class ProviderIntegrationModule final : public Forsetti::IForsettiModule {
public:
    Forsetti::ModuleDescriptor descriptor() const override;
    Forsetti::ModuleManifest manifest() const override;
    void start(Forsetti::ForsettiContext& context) override;
    void stop(Forsetti::ForsettiContext& context) override;
};

class CodexProviderModule final : public Forsetti::IForsettiModule {
public:
    Forsetti::ModuleDescriptor descriptor() const override;
    Forsetti::ModuleManifest manifest() const override;
    void start(Forsetti::ForsettiContext& context) override;
    void stop(Forsetti::ForsettiContext& context) override;
};

class ClaudeCodeProviderModule final : public Forsetti::IForsettiModule {
public:
    Forsetti::ModuleDescriptor descriptor() const override;
    Forsetti::ModuleManifest manifest() const override;
    void start(Forsetti::ForsettiContext& context) override;
    void stop(Forsetti::ForsettiContext& context) override;
};

class XAIProviderModule final : public Forsetti::IForsettiModule {
public:
    Forsetti::ModuleDescriptor descriptor() const override;
    Forsetti::ModuleManifest manifest() const override;
    void start(Forsetti::ForsettiContext& context) override;
    void stop(Forsetti::ForsettiContext& context) override;
};

class ExportModule final : public Forsetti::IForsettiModule {
public:
    Forsetti::ModuleDescriptor descriptor() const override;
    Forsetti::ModuleManifest manifest() const override;
    void start(Forsetti::ForsettiContext& context) override;
    void stop(Forsetti::ForsettiContext& context) override;
};

class CommandLogicUnitModule final : public Forsetti::IForsettiModule {
public:
    Forsetti::ModuleDescriptor descriptor() const override;
    Forsetti::ModuleManifest manifest() const override;
    void start(Forsetti::ForsettiContext& context) override;
    void stop(Forsetti::ForsettiContext& context) override;
};

class WindowsGatewayModule final : public Forsetti::IForsettiModule {
public:
    Forsetti::ModuleDescriptor descriptor() const override;
    Forsetti::ModuleManifest manifest() const override;
    void start(Forsetti::ForsettiContext& context) override;
    void stop(Forsetti::ForsettiContext& context) override;
};

class MacGatewayModule final : public Forsetti::IForsettiModule {
public:
    Forsetti::ModuleDescriptor descriptor() const override;
    Forsetti::ModuleManifest manifest() const override;
    void start(Forsetti::ForsettiContext& context) override;
    void stop(Forsetti::ForsettiContext& context) override;
};

class IOSGatewayModule final : public Forsetti::IForsettiModule {
public:
    Forsetti::ModuleDescriptor descriptor() const override;
    Forsetti::ModuleManifest manifest() const override;
    void start(Forsetti::ForsettiContext& context) override;
    void stop(Forsetti::ForsettiContext& context) override;
};

class WindowsGovernanceMcpServerModule final : public Forsetti::IForsettiModule {
public:
    Forsetti::ModuleDescriptor descriptor() const override;
    Forsetti::ModuleManifest manifest() const override;
    void start(Forsetti::ForsettiContext& context) override;
    void stop(Forsetti::ForsettiContext& context) override;
};

class MacGovernanceMcpServerModule final : public Forsetti::IForsettiModule {
public:
    Forsetti::ModuleDescriptor descriptor() const override;
    Forsetti::ModuleManifest manifest() const override;
    void start(Forsetti::ForsettiContext& context) override;
    void stop(Forsetti::ForsettiContext& context) override;
};

class IOSGovernanceMcpServerModule final : public Forsetti::IForsettiModule {
public:
    Forsetti::ModuleDescriptor descriptor() const override;
    Forsetti::ModuleManifest manifest() const override;
    void start(Forsetti::ForsettiContext& context) override;
    void stop(Forsetti::ForsettiContext& context) override;
};

class BeaconGatewayModule final : public Forsetti::IForsettiModule {
public:
    Forsetti::ModuleDescriptor descriptor() const override;
    Forsetti::ModuleManifest manifest() const override;
    void start(Forsetti::ForsettiContext& context) override;
    void stop(Forsetti::ForsettiContext& context) override;
};

class DashboardUIModule final : public Forsetti::IForsettiUIModule {
public:
    Forsetti::ModuleDescriptor descriptor() const override;
    Forsetti::ModuleManifest manifest() const override;
    Forsetti::UIContributions uiContributions() const override;
    void start(Forsetti::ForsettiContext& context) override;
    void stop(Forsetti::ForsettiContext& context) override;

private:
    std::optional<Forsetti::SubscriptionToken> controlSurfaceSubscription_;
};

void registerMasterControlModules(Forsetti::ModuleRegistry& registry);

} // namespace MasterControl
