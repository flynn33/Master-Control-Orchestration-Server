// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#pragma once

#include "SettingsSectionControl.g.h"
#include "pch.h"

#include "ShellRuntime.h"

namespace winrt::MasterControlShell::implementation {

struct SettingsSectionControl : SettingsSectionControlT<SettingsSectionControl> {
    SettingsSectionControl();

    void ApplySnapshot(const ::MasterControlShell::ShellSnapshot& snapshot);
};

} // namespace winrt::MasterControlShell::implementation

namespace winrt::MasterControlShell::factory_implementation {

struct SettingsSectionControl : SettingsSectionControlT<SettingsSectionControl, implementation::SettingsSectionControl> {
};

} // namespace winrt::MasterControlShell::factory_implementation
