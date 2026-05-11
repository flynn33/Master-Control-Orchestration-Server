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
    // v0.9.75: re-run the boot self-test sweep on demand. POSTs
    // /api/self-tests/run and immediately re-renders the card with
    // the freshly-computed snapshot the endpoint returns.
    void ReRunSelfTestsButton_Click(Windows::Foundation::IInspectable const&,
                                    Microsoft::UI::Xaml::RoutedEventArgs const&);
    // v0.9.76: Supervisor Agent Assignment Wizard handlers. Single-
    // selection toggle group across ChatGPT / Claude / Grok rows, plus
    // a Generate Config & Save button (calls /api/supervisor/config/
    // generate then opens FileSavePicker), plus a Revoke button.
    void SupervisorChatGptToggle_Toggled(Windows::Foundation::IInspectable const&,
                                         Microsoft::UI::Xaml::RoutedEventArgs const&);
    void SupervisorClaudeToggle_Toggled(Windows::Foundation::IInspectable const&,
                                        Microsoft::UI::Xaml::RoutedEventArgs const&);
    void SupervisorGrokToggle_Toggled(Windows::Foundation::IInspectable const&,
                                      Microsoft::UI::Xaml::RoutedEventArgs const&);
    void SupervisorGenerateButton_Click(Windows::Foundation::IInspectable const&,
                                        Microsoft::UI::Xaml::RoutedEventArgs const&);
    void SupervisorRevokeButton_Click(Windows::Foundation::IInspectable const&,
                                      Microsoft::UI::Xaml::RoutedEventArgs const&);

private:
    winrt::Windows::Foundation::IAsyncAction RefreshClaudePluginAsync();
    winrt::Windows::Foundation::IAsyncAction ToggleClaudePluginAsync(bool requestedOn);
    winrt::Windows::Foundation::IAsyncAction ReRunSelfTestsAsync();
    void RenderClaudePluginStatus(const ::MasterControlShell::ShellClaudePluginStatus& status);
    // v0.8.7: per-card status writers driven by ApplySnapshot.
    // v0.10.10: ApplyMcpServersCard + ApplySubAgentsCard removed; the
    // Overview deck no longer carries those summary cards (Runtime +
    // Telemetry own those decks via footer-style tile grids).
    void ApplyApisAndServicesCard(const ::MasterControlShell::ShellSnapshot& snapshot);
    void ApplySecurityStanceCard(const ::MasterControlShell::ShellSnapshot& snapshot);
    void ApplyErrorReportingCard(const ::MasterControlShell::ShellSnapshot& snapshot);
    // v0.9.75: render the per-probe pass / fail roster + headline + dot.
    void ApplySelfTestCard(const ::MasterControlShell::ShellSnapshot& snapshot);
    // v0.9.76: render the Supervisor Agent card and its toggle states.
    void ApplySupervisorCard(const ::MasterControlShell::ShellSnapshot& snapshot);
    winrt::Windows::Foundation::IAsyncAction GenerateSupervisorConfigAsync(std::wstring providerId);
    winrt::Windows::Foundation::IAsyncAction RevokeSupervisorAsync();
    // Single-selection radio behavior across the three supervisor toggles.
    void SetSupervisorSelection(const std::wstring& providerId);
    std::wstring CurrentSupervisorSelection();

    ::MasterControlShell::ShellRuntime* runtime_ = nullptr;
    bool claudePluginBusy_ = false;
    bool suspendClaudePluginToggleHandler_ = false;
    // v0.8.7: cache of the most recent snapshot's error events so the
    // Export button writes the same set the operator currently sees,
    // even if a fresh snapshot lands between render and click.
    std::vector<::MasterControlShell::ShellErrorEvent> lastErrorEvents_;
    // v0.9.76: supervisor wizard state. The bind- and bind-port are
    // captured at snapshot apply time so async handlers (FileSavePicker,
    // HTTP POST) don't have to re-resolve them. supervisorBusy_ guards
    // double-clicks while a request is in flight. suspendSupervisor-
    // ToggleHandler_ blocks the toggle event handlers while the
    // ApplySnapshot path syncs the UI from the server-side state.
    std::wstring supervisorBindHost_;
    uint16_t     supervisorBindPort_ = 0;
    bool         supervisorBusy_ = false;
    bool         suspendSupervisorToggleHandler_ = false;
    std::wstring lastSupervisorSelection_;
};

} // namespace winrt::MasterControlShell::implementation

namespace winrt::MasterControlShell::factory_implementation {

struct OverviewSectionControl : OverviewSectionControlT<OverviewSectionControl, implementation::OverviewSectionControl> {
};

} // namespace winrt::MasterControlShell::factory_implementation
