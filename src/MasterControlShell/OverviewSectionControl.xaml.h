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
    // v0.8.7: export the visible error set to a timestamped JSON file
    // under %PUBLIC%\Documents\Master Control Orchestration Server\.
    void ExportErrorsButton_Click(Windows::Foundation::IInspectable const&,
                                  Microsoft::UI::Xaml::RoutedEventArgs const&);

private:
    winrt::Windows::Foundation::IAsyncAction RefreshClaudePluginAsync();
    winrt::Windows::Foundation::IAsyncAction ToggleClaudePluginAsync(bool requestedOn);
    void RenderClaudePluginStatus(const ::MasterControlShell::ShellClaudePluginStatus& status);
    // v0.8.7: per-card status writers driven by ApplySnapshot.
    void ApplyApisAndServicesCard(const ::MasterControlShell::ShellSnapshot& snapshot);
    void ApplySecurityStanceCard(const ::MasterControlShell::ShellSnapshot& snapshot);
    void ApplyMcpServersCard(const ::MasterControlShell::ShellSnapshot& snapshot);
    void ApplySubAgentsCard(const ::MasterControlShell::ShellSnapshot& snapshot);
    void ApplyErrorReportingCard(const ::MasterControlShell::ShellSnapshot& snapshot);

    ::MasterControlShell::ShellRuntime* runtime_ = nullptr;
    bool claudePluginBusy_ = false;
    bool suspendClaudePluginToggleHandler_ = false;
    // v0.8.7: cache of the most recent snapshot's error events so the
    // Export button writes the same set the operator currently sees,
    // even if a fresh snapshot lands between render and click.
    std::vector<::MasterControlShell::ShellErrorEvent> lastErrorEvents_;
};

} // namespace winrt::MasterControlShell::implementation

namespace winrt::MasterControlShell::factory_implementation {

struct OverviewSectionControl : OverviewSectionControlT<OverviewSectionControl, implementation::OverviewSectionControl> {
};

} // namespace winrt::MasterControlShell::factory_implementation
