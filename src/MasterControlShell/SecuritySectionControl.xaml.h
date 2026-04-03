// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#pragma once

#include "SecuritySectionControl.g.h"
#include "pch.h"

#include "ShellRuntime.h"

namespace winrt::MasterControlShell::implementation {

struct SecuritySectionControl : SecuritySectionControlT<SecuritySectionControl> {
    SecuritySectionControl();

    void AttachRuntime(::MasterControlShell::ShellRuntime* runtime,
                       std::function<void()> refreshRequested,
                       std::function<void(const std::wstring&)> actionRequested);
    void ApplySnapshot(const ::MasterControlShell::ShellSnapshot& snapshot);
    void GuidedSecurityActionButton_Click(Windows::Foundation::IInspectable const&,
                                          Microsoft::UI::Xaml::RoutedEventArgs const&);
    void SecurityToggle_Toggled(Windows::Foundation::IInspectable const&,
                                Microsoft::UI::Xaml::RoutedEventArgs const&);
    void TrustedHostsTextBox_TextChanged(Windows::Foundation::IInspectable const&,
                                         Microsoft::UI::Xaml::Controls::TextChangedEventArgs const&);
    void SaveSecurityButton_Click(Windows::Foundation::IInspectable const&,
                                  Microsoft::UI::Xaml::RoutedEventArgs const&);

private:
    void PopulateEditorFromSnapshot(const ::MasterControlShell::ShellSnapshot& snapshot);
    void UpdateEditorState();
    void SetStatus(winrt::hstring const& message);
    ::MasterControlShell::ShellSecuritySettings BuildSecuritySettings();
    winrt::Windows::Foundation::IAsyncAction SaveSecurityAsync(bool confirmUnsafeChanges);
    winrt::Windows::Foundation::IAsyncAction ShowUnsafeConfirmationAsync();

    ::MasterControlShell::ShellRuntime* runtime_ = nullptr;
    std::function<void()> refreshRequested_;
    std::function<void(const std::wstring&)> actionRequested_;
    bool suspendDirtyTracking_ = false;
    bool isDirty_ = false;
    ::MasterControlShell::ShellSnapshot lastSnapshot_{};
};

} // namespace winrt::MasterControlShell::implementation

namespace winrt::MasterControlShell::factory_implementation {

struct SecuritySectionControl : SecuritySectionControlT<SecuritySectionControl, implementation::SecuritySectionControl> {
};

} // namespace winrt::MasterControlShell::factory_implementation
