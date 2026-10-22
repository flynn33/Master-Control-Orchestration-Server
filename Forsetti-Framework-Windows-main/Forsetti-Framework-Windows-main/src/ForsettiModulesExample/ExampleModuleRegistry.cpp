// Forsetti Framework for Windows
// Copyright (c) 2026 James Daley, Raven Forge Software.
// SPDX-License-Identifier: Apache-2.0
// Patent pending.

#include "ExampleModules.h"
#include "ForsettiCore/ModuleRegistry.h"

namespace Forsetti {

void registerExampleModules(ModuleRegistry& registry) {
    registry.registerModule("ExampleServiceModule", []() -> std::unique_ptr<IForsettiModule> {
        return std::make_unique<ExampleServiceModule>();
    });
    registry.registerModule("ExampleUIModule", []() -> std::unique_ptr<IForsettiModule> {
        return std::make_unique<ExampleUIModule>();
    });
}

} // namespace Forsetti
