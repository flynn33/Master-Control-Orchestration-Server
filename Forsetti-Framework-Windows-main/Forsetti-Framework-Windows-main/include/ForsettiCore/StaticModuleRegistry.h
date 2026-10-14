// Forsetti Framework for Windows
// Copyright (c) 2026 James Daley, Raven Forge Software.
// SPDX-License-Identifier: MIT Patent Pending.

#pragma once

#include "ForsettiCore/ModuleRegistry.h"

namespace Forsetti {

class ForsettiStaticModuleRegistry final {
public:
    static ModuleRegistry buildRegistry(std::function<void(ModuleRegistry&)> configure) {
        ModuleRegistry registry;
        configure(registry);
        return registry;
    }
};

} // namespace Forsetti
