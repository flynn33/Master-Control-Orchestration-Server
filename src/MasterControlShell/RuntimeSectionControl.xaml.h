// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#pragma once

#include "RuntimeSectionControl.g.h"
#include "pch.h"

#include "ShellRuntime.h"

namespace winrt::MasterControlShell::implementation {

struct RuntimeSectionControl : RuntimeSectionControlT<RuntimeSectionControl> {
    RuntimeSectionControl();

    void AttachRuntime(::MasterControlShell::ShellRuntime* runtime,
                       std::function<void()> refreshRequested,
                       std::function<void(const std::wstring&)> actionRequested);
    void ApplySnapshot(const ::MasterControlShell::ShellSnapshot& snapshot);
    void GuidedRuntimeActionButton_Click(Windows::Foundation::IInspectable const&,
                                         Microsoft::UI::Xaml::RoutedEventArgs const&);
    void CustomMcpServerSelector_SelectionChanged(Windows::Foundation::IInspectable const&,
                                                  Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
    void CustomMcpServerEditor_TextChanged(Windows::Foundation::IInspectable const&,
                                           Microsoft::UI::Xaml::Controls::TextChangedEventArgs const&);
    void SaveCustomMcpServerButton_Click(Windows::Foundation::IInspectable const&,
                                         Microsoft::UI::Xaml::RoutedEventArgs const&);
    void NewCustomMcpServerButton_Click(Windows::Foundation::IInspectable const&,
                                        Microsoft::UI::Xaml::RoutedEventArgs const&);
    void RemoveCustomMcpServerButton_Click(Windows::Foundation::IInspectable const&,
                                           Microsoft::UI::Xaml::RoutedEventArgs const&);
    void CustomSubAgentSelector_SelectionChanged(Windows::Foundation::IInspectable const&,
                                                 Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
    void CustomSubAgentEditor_TextChanged(Windows::Foundation::IInspectable const&,
                                          Microsoft::UI::Xaml::Controls::TextChangedEventArgs const&);
    void SaveCustomSubAgentButton_Click(Windows::Foundation::IInspectable const&,
                                        Microsoft::UI::Xaml::RoutedEventArgs const&);
    void NewCustomSubAgentButton_Click(Windows::Foundation::IInspectable const&,
                                       Microsoft::UI::Xaml::RoutedEventArgs const&);
    void RemoveCustomSubAgentButton_Click(Windows::Foundation::IInspectable const&,
                                          Microsoft::UI::Xaml::RoutedEventArgs const&);
    void AppleHostSelector_SelectionChanged(Windows::Foundation::IInspectable const&,
                                            Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
    void AppleHostEditor_TextChanged(Windows::Foundation::IInspectable const&,
                                     Microsoft::UI::Xaml::Controls::TextChangedEventArgs const&);
    void AppleHostTransportComboBox_SelectionChanged(Windows::Foundation::IInspectable const&,
                                                     Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
    void AppleHostEnabledCheckBox_Click(Windows::Foundation::IInspectable const&,
                                        Microsoft::UI::Xaml::RoutedEventArgs const&);
    void SaveAppleHostButton_Click(Windows::Foundation::IInspectable const&,
                                   Microsoft::UI::Xaml::RoutedEventArgs const&);
    void NewAppleHostButton_Click(Windows::Foundation::IInspectable const&,
                                  Microsoft::UI::Xaml::RoutedEventArgs const&);
    void RemoveAppleHostButton_Click(Windows::Foundation::IInspectable const&,
                                     Microsoft::UI::Xaml::RoutedEventArgs const&);

private:
    void PopulateCustomMcpServerEditor(size_t index);
    void ClearCustomMcpServerEditor();
    void RefreshCustomMcpServerSelector();
    std::optional<::MasterControlShell::ShellRuntimeEndpoint> BuildCustomMcpServerFromEditor();
    winrt::Windows::Foundation::IAsyncAction SaveCustomMcpServerAsync();
    winrt::Windows::Foundation::IAsyncAction RemoveCustomMcpServerAsync();
    void PopulateCustomSubAgentEditor(size_t index);
    void ClearCustomSubAgentEditor();
    void RefreshCustomSubAgentSelector();
    void UpdateEditorState();
    std::optional<::MasterControlShell::ShellRuntimeEndpoint> BuildCustomSubAgentFromEditor();
    winrt::Windows::Foundation::IAsyncAction SaveCustomSubAgentAsync();
    winrt::Windows::Foundation::IAsyncAction RemoveCustomSubAgentAsync();
    void PopulateAppleHostEditor(size_t index);
    void ClearAppleHostEditor();
    void RefreshAppleHostSelector();
    std::optional<::MasterControlShell::ShellAppleRemoteHost> BuildAppleHostFromEditor();
    winrt::Windows::Foundation::IAsyncAction SaveAppleHostAsync();
    winrt::Windows::Foundation::IAsyncAction RemoveAppleHostAsync();
    // v0.7.6: rebuild the Sub-Agents card grid imperatively from the
    // ShellSnapshot.subAgentRuntimeStats vector. Each card is a Border
    // containing the agent name, utilization bar (ProgressBar), reachability
    // dot, host:port endpoint, pool note, and active-client list.
    void PopulateSubAgentCards(const ::MasterControlShell::ShellSnapshot& snapshot);

    ::MasterControlShell::ShellRuntime* runtime_ = nullptr;
    std::function<void()> refreshRequested_;
    std::function<void(const std::wstring&)> actionRequested_;
    std::vector<::MasterControlShell::ShellRuntimeEndpoint> customMcpServers_;
    std::vector<::MasterControlShell::ShellRuntimeEndpoint> customSubAgents_;
    std::vector<::MasterControlShell::ShellAppleRemoteHost> appleRemoteHosts_;
    bool customMcpServerDirty_ = false;
    bool customSubAgentDirty_ = false;
    bool appleHostDirty_ = false;
    bool suspendDirtyTracking_ = false;
    int selectedCustomMcpServerIndex_ = -1;
    std::wstring selectedCustomMcpServerId_;
    int selectedCustomSubAgentIndex_ = -1;
    std::wstring selectedCustomSubAgentId_;
    int selectedAppleHostIndex_ = -1;
    std::wstring selectedAppleHostId_;
};

} // namespace winrt::MasterControlShell::implementation

namespace winrt::MasterControlShell::factory_implementation {

struct RuntimeSectionControl : RuntimeSectionControlT<RuntimeSectionControl, implementation::RuntimeSectionControl> {
};

} // namespace winrt::MasterControlShell::factory_implementation
