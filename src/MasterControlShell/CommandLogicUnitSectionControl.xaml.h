// Master Control Program
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#pragma once

#include "CommandLogicUnitSectionControl.g.h"
#include "pch.h"

#include "ShellRuntime.h"

namespace winrt::MasterControlShell::implementation {

struct CommandLogicUnitSectionControl : CommandLogicUnitSectionControlT<CommandLogicUnitSectionControl> {
    CommandLogicUnitSectionControl();

    void AttachRuntime(::MasterControlShell::ShellRuntime* runtime,
                       std::function<void()> refreshRequested);
    void ApplySnapshot(const ::MasterControlShell::ShellSnapshot& snapshot);
    void AppleOperationFilterSelector_SelectionChanged(Windows::Foundation::IInspectable const&,
                                                       Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
    void AppleOperationSelector_SelectionChanged(Windows::Foundation::IInspectable const&,
                                                 Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
    void RetryAttentionAppleOperationsButton_Click(Windows::Foundation::IInspectable const&,
                                                   Microsoft::UI::Xaml::RoutedEventArgs const&);
    void RerunAppleOperationButton_Click(Windows::Foundation::IInspectable const&,
                                         Microsoft::UI::Xaml::RoutedEventArgs const&);

private:
    void RefreshAppleOperationList();
    void RefreshAppleOperationSelector();
    void UpdateOperationState();
    winrt::Windows::Foundation::IAsyncAction RetryAttentionAppleOperationsAsync();
    winrt::Windows::Foundation::IAsyncAction RerunAppleOperationAsync();
    std::vector<::MasterControlShell::ShellAppleOperationRecord> FilteredAppleOperations() const;

    ::MasterControlShell::ShellRuntime* runtime_ = nullptr;
    std::function<void()> refreshRequested_;
    std::vector<::MasterControlShell::ShellAppleOperationRecord> appleOperations_;
    std::wstring selectedAppleOperationId_;
    std::wstring selectedAppleOperationFilter_ = L"attention";
};

} // namespace winrt::MasterControlShell::implementation

namespace winrt::MasterControlShell::factory_implementation {

struct CommandLogicUnitSectionControl : CommandLogicUnitSectionControlT<CommandLogicUnitSectionControl, implementation::CommandLogicUnitSectionControl> {
};

} // namespace winrt::MasterControlShell::factory_implementation
