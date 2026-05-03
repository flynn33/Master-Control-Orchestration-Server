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
    void ClaudePluginToggleButton_Click(Windows::Foundation::IInspectable const&,
                                        Microsoft::UI::Xaml::RoutedEventArgs const&);

private:
    void PopulateEditorFromSnapshot();
    void UpdateSummary();
    void UpdateEditorState();
    void UpdateFirewallRuleSnippets();
    void CopyTextToClipboard(const std::wstring& text, const std::wstring& successMessage);
    winrt::Windows::Foundation::IAsyncAction ApplySettingsAsync();
    winrt::Windows::Foundation::IAsyncAction RefreshClaudePluginAsync();
    winrt::Windows::Foundation::IAsyncAction ToggleClaudePluginAsync();
    void RenderClaudePluginStatus(const ::MasterControlShell::ShellClaudePluginStatus& status);

    ::MasterControlShell::ShellRuntime* runtime_ = nullptr;
    std::function<void()> refreshRequested_;
    std::function<void(const std::wstring&)> actionRequested_;
    ::MasterControlShell::ShellSnapshot snapshot_{};
    bool dirty_ = false;
    bool suspendDirtyTracking_ = false;
    bool claudePluginBusy_ = false;
};

} // namespace winrt::MasterControlShell::implementation

namespace winrt::MasterControlShell::factory_implementation {

struct SettingsSectionControl : SettingsSectionControlT<SettingsSectionControl, implementation::SettingsSectionControl> {
};

} // namespace winrt::MasterControlShell::factory_implementation
