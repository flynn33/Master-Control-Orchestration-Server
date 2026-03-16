// Forsetti Framework for Windows
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential. Patent Pending.

#pragma once
#include "ForsettiCore/ForsettiProtocols.h"
#include "ForsettiCore/ForsettiContext.h"
#include "ForsettiCore/ModuleModels.h"
#include "ForsettiCore/UIModels.h"

namespace Forsetti {

// ---------------------------------------------------------------------------
// ExampleServiceModule — a minimal service module demonstrating the
// IForsettiModule interface. Port of ExampleServiceModule.swift.
// ---------------------------------------------------------------------------
class ExampleServiceModule final : public IForsettiModule {
public:
    ModuleDescriptor descriptor() const override;
    ModuleManifest   manifest()   const override;

    void start(ForsettiContext& context) override;
    void stop(ForsettiContext& context)  override;
};

// ---------------------------------------------------------------------------
// ExampleUIModule — a minimal UI module demonstrating the IForsettiUIModule
// interface with toolbar items, view injections, and overlay routing.
// Port of ExampleUIModule.swift.
// ---------------------------------------------------------------------------
class ExampleUIModule final : public IForsettiUIModule {
public:
    ModuleDescriptor  descriptor()       const override;
    ModuleManifest    manifest()         const override;
    UIContributions   uiContributions()  const override;

    void start(ForsettiContext& context) override;
    void stop(ForsettiContext& context)  override;
};

} // namespace Forsetti
