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

    void AttachRuntime(::MasterControlShell::ShellRuntime* runtime,
                       std::function<void()> refreshRequested,
                       std::function<void(const std::wstring&)> actionRequested);
    void ApplySnapshot(const ::MasterControlShell::ShellSnapshot& snapshot);
    void SettingsEditor_TextChanged(Windows::Foundation::IInspectable const&,
                                    Microsoft::UI::Xaml::Controls::TextChangedEventArgs const&);
    void SettingsToggle_Toggled(Windows::Foundation::IInspectable const&,
                                Microsoft::UI::Xaml::RoutedEventArgs const&);
    // v0.9.2: resource-allocation sliders. Each of CPU / Memory / Bandwidth /
    // Storage exposes a Slider (visual coarse adjustment) paired with a
    // small TextBox (exact entry). The two are kept two-way synchronized
    // -- moving the slider updates the TextBox and vice versa -- guarded
    // by `syncingAllocation_` to avoid feedback loops.
    void AllocationSlider_ValueChanged(Windows::Foundation::IInspectable const& sender,
                                       Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const&);
    void AllocationTextBox_TextChanged(Windows::Foundation::IInspectable const& sender,
                                       Microsoft::UI::Xaml::Controls::TextChangedEventArgs const&);
    void ApplySettingsButton_Click(Windows::Foundation::IInspectable const&,
                                   Microsoft::UI::Xaml::RoutedEventArgs const&);
    void CopyFirewallGatewayRuleButton_Click(Windows::Foundation::IInspectable const&,
                                             Microsoft::UI::Xaml::RoutedEventArgs const&);
    void CopyFirewallOperatorRuleButton_Click(Windows::Foundation::IInspectable const&,
                                              Microsoft::UI::Xaml::RoutedEventArgs const&);
    void CopyFirewallMDnsRuleButton_Click(Windows::Foundation::IInspectable const&,
                                          Microsoft::UI::Xaml::RoutedEventArgs const&);
    void CopyFirewallBeaconRuleButton_Click(Windows::Foundation::IInspectable const&,
                                            Microsoft::UI::Xaml::RoutedEventArgs const&);
    // v0.8.5: Host Controls + Guided Setup Wizards moved here from
    // MainWindow.xaml. Both handlers read the clicked Button's Tag
    // (e.g. "host-control:refresh" or "new-mcp") and forward to
    // MainWindow via the actionRequested_ callback.
    void HostControlButton_Click(Windows::Foundation::IInspectable const& sender,
                                 Microsoft::UI::Xaml::RoutedEventArgs const&);
    void GuidedWizardButton_Click(Windows::Foundation::IInspectable const& sender,
                                  Microsoft::UI::Xaml::RoutedEventArgs const&);

private:
    void PopulateEditorFromSnapshot();
    void UpdateSummary();
    void UpdateEditorState();
    void UpdateFirewallRuleSnippets();
    void CopyTextToClipboard(const std::wstring& text, const std::wstring& successMessage);
    winrt::Windows::Foundation::IAsyncAction ApplySettingsAsync();

    ::MasterControlShell::ShellRuntime* runtime_ = nullptr;
    std::function<void()> refreshRequested_;
    std::function<void(const std::wstring&)> actionRequested_;
    ::MasterControlShell::ShellSnapshot snapshot_{};
    bool dirty_ = false;
    bool suspendDirtyTracking_ = false;
    // v0.9.2: held during slider<->textbox two-way sync so the partner
    // control's change event does not bounce back and create a feedback
    // loop. Independent of suspendDirtyTracking_ which has a different
    // contract (covers the whole snapshot-load pass).
    bool syncingAllocation_ = false;
};

} // namespace winrt::MasterControlShell::implementation

namespace winrt::MasterControlShell::factory_implementation {

struct SettingsSectionControl : SettingsSectionControlT<SettingsSectionControl, implementation::SettingsSectionControl> {
};

} // namespace winrt::MasterControlShell::factory_implementation
