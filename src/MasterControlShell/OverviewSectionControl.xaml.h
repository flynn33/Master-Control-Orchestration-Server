// Master Control Program
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#pragma once

#include "OverviewSectionControl.g.h"
#include "pch.h"

#include "ShellRuntime.h"

namespace winrt::MasterControlShell::implementation {

struct OverviewSectionControl : OverviewSectionControlT<OverviewSectionControl> {
    OverviewSectionControl();

    void ApplySnapshot(const ::MasterControlShell::ShellSnapshot& snapshot);
};

} // namespace winrt::MasterControlShell::implementation

namespace winrt::MasterControlShell::factory_implementation {

struct OverviewSectionControl : OverviewSectionControlT<OverviewSectionControl, implementation::OverviewSectionControl> {
};

} // namespace winrt::MasterControlShell::factory_implementation
