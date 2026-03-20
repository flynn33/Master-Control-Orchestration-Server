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
    void PopulateCustomSubAgentEditor(size_t index);
    void ClearCustomSubAgentEditor();
    void RefreshCustomSubAgentSelector();
    void UpdateEditorState();
    std::optional<::MasterControlShell::ShellRuntimeEndpoint> BuildCustomSubAgentFromEditor();
    winrt::Windows::Foundation::IAsyncAction SaveCustomSubAgentAsync();
    winrt::Windows::Foundation::IAsyncAction RemoveCustomSubAgentAsync();

    ::MasterControlShell::ShellRuntime* runtime_ = nullptr;
    std::function<void()> refreshRequested_;
    std::vector<::MasterControlShell::ShellRuntimeEndpoint> customSubAgents_;
    bool customSubAgentDirty_ = false;
    bool suspendDirtyTracking_ = false;
    int selectedCustomSubAgentIndex_ = -1;
    std::wstring selectedCustomSubAgentId_;
};

} // namespace winrt::MasterControlShell::implementation

namespace winrt::MasterControlShell::factory_implementation {

struct RuntimeSectionControl : RuntimeSectionControlT<RuntimeSectionControl, implementation::RuntimeSectionControl> {
};

} // namespace winrt::MasterControlShell::factory_implementation
