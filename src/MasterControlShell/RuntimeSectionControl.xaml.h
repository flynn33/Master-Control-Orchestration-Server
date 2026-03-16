// Master Control Program
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#pragma once

#include "RuntimeSectionControl.g.h"
#include "pch.h"

#include "ShellRuntime.h"

namespace winrt::MasterControlShell::implementation {

struct RuntimeSectionControl : RuntimeSectionControlT<RuntimeSectionControl> {
    RuntimeSectionControl();

    void ApplySnapshot(const ::MasterControlShell::ShellSnapshot& snapshot);
};

} // namespace winrt::MasterControlShell::implementation

namespace winrt::MasterControlShell::factory_implementation {

struct RuntimeSectionControl : RuntimeSectionControlT<RuntimeSectionControl, implementation::RuntimeSectionControl> {
};

} // namespace winrt::MasterControlShell::factory_implementation
