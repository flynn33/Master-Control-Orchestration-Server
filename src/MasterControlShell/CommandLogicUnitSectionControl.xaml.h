// Master Control Program
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#pragma once

#include "CommandLogicUnitSectionControl.g.h"
#include "pch.h"

#include "ShellRuntime.h"

namespace winrt::MasterControlShell::implementation {

struct CommandLogicUnitSectionControl : CommandLogicUnitSectionControlT<CommandLogicUnitSectionControl> {
    CommandLogicUnitSectionControl();

    void ApplySnapshot(const ::MasterControlShell::ShellSnapshot& snapshot);
};

} // namespace winrt::MasterControlShell::implementation

namespace winrt::MasterControlShell::factory_implementation {

struct CommandLogicUnitSectionControl : CommandLogicUnitSectionControlT<CommandLogicUnitSectionControl, implementation::CommandLogicUnitSectionControl> {
};

} // namespace winrt::MasterControlShell::factory_implementation
