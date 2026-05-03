// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#pragma once

#include "OverviewSectionControl.g.h"
#include "pch.h"

#include "ShellRuntime.h"

namespace winrt::MasterControlShell::implementation {

struct OverviewSectionControl : OverviewSectionControlT<OverviewSectionControl> {
    OverviewSectionControl();

    void AttachRuntime(::MasterControlShell::ShellRuntime* runtime);
    void ApplySnapshot(const ::MasterControlShell::ShellSnapshot& snapshot);

    void ClaudePluginToggle_Toggled(Windows::Foundation::IInspectable const&,
                                    Microsoft::UI::Xaml::RoutedEventArgs const&);

private:
    winrt::Windows::Foundation::IAsyncAction RefreshClaudePluginAsync();
    winrt::Windows::Foundation::IAsyncAction ToggleClaudePluginAsync(bool requestedOn);
    void RenderClaudePluginStatus(const ::MasterControlShell::ShellClaudePluginStatus& status);

    ::MasterControlShell::ShellRuntime* runtime_ = nullptr;
    bool claudePluginBusy_ = false;
    bool suspendClaudePluginToggleHandler_ = false;
};

} // namespace winrt::MasterControlShell::implementation

namespace winrt::MasterControlShell::factory_implementation {

struct OverviewSectionControl : OverviewSectionControlT<OverviewSectionControl, implementation::OverviewSectionControl> {
};

} // namespace winrt::MasterControlShell::factory_implementation
