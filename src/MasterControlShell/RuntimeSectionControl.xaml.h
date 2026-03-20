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

    void AttachRuntime(::MasterControlShell::ShellRuntime* runtime,
                       std::function<void()> refreshRequested);
    void ApplySnapshot(const ::MasterControlShell::ShellSnapshot& snapshot);
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

    ::MasterControlShell::ShellRuntime* runtime_ = nullptr;
    std::function<void()> refreshRequested_;
    std::vector<::MasterControlShell::ShellRuntimeEndpoint> customMcpServers_;
    std::vector<::MasterControlShell::ShellRuntimeEndpoint> customSubAgents_;
    bool customMcpServerDirty_ = false;
    bool customSubAgentDirty_ = false;
    bool suspendDirtyTracking_ = false;
    int selectedCustomMcpServerIndex_ = -1;
    std::wstring selectedCustomMcpServerId_;
    int selectedCustomSubAgentIndex_ = -1;
    std::wstring selectedCustomSubAgentId_;
};

} // namespace winrt::MasterControlShell::implementation

namespace winrt::MasterControlShell::factory_implementation {

struct RuntimeSectionControl : RuntimeSectionControlT<RuntimeSectionControl, implementation::RuntimeSectionControl> {
};

} // namespace winrt::MasterControlShell::factory_implementation
