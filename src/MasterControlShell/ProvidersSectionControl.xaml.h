// Master Control Program
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#pragma once

#include "ProvidersSectionControl.g.h"
#include "pch.h"

#include "ShellRuntime.h"

namespace winrt::MasterControlShell::implementation {

struct ProvidersSectionControl : ProvidersSectionControlT<ProvidersSectionControl> {
    ProvidersSectionControl();

    void AttachRuntime(::MasterControlShell::ShellRuntime* runtime,
                       std::function<void()> refreshRequested);
    void ApplySnapshot(const ::MasterControlShell::ShellSnapshot& snapshot);
    void ProviderSelector_SelectionChanged(Windows::Foundation::IInspectable const&,
                                           Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
    void ProviderEditor_TextChanged(Windows::Foundation::IInspectable const&,
                                    Microsoft::UI::Xaml::Controls::TextChangedEventArgs const&);
    void ProviderKind_SelectionChanged(Windows::Foundation::IInspectable const&,
                                       Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
    void ProviderToggle_Toggled(Windows::Foundation::IInspectable const&,
                                Microsoft::UI::Xaml::RoutedEventArgs const&);
    void ProviderCredentialEditor_PasswordChanged(Windows::Foundation::IInspectable const&,
                                                  Microsoft::UI::Xaml::RoutedEventArgs const&);
    void ProviderAssignmentTargetSelector_SelectionChanged(Windows::Foundation::IInspectable const&,
                                                           Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
    void ProviderAssignmentProviderSelector_SelectionChanged(Windows::Foundation::IInspectable const&,
                                                             Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
    void ProviderExecutionTargetSelector_SelectionChanged(Windows::Foundation::IInspectable const&,
                                                          Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
    void ProviderExecutionEditor_Changed(Windows::Foundation::IInspectable const&,
                                         Microsoft::UI::Xaml::RoutedEventArgs const&);
    void ProviderExecutionEditor_Changed(Windows::Foundation::IInspectable const&,
                                         Microsoft::UI::Xaml::Controls::TextChangedEventArgs const&);
    void AiAutonomyToggle_Toggled(Windows::Foundation::IInspectable const&,
                                  Microsoft::UI::Xaml::RoutedEventArgs const&);
    void SaveProviderButton_Click(Windows::Foundation::IInspectable const&,
                                  Microsoft::UI::Xaml::RoutedEventArgs const&);
    void SaveProviderCredentialsButton_Click(Windows::Foundation::IInspectable const&,
                                             Microsoft::UI::Xaml::RoutedEventArgs const&);
    void SaveProviderAssignmentButton_Click(Windows::Foundation::IInspectable const&,
                                            Microsoft::UI::Xaml::RoutedEventArgs const&);
    void NewProviderButton_Click(Windows::Foundation::IInspectable const&,
                                 Microsoft::UI::Xaml::RoutedEventArgs const&);
    void ExecuteProviderTaskButton_Click(Windows::Foundation::IInspectable const&,
                                         Microsoft::UI::Xaml::RoutedEventArgs const&);
    void SaveAiAutonomyButton_Click(Windows::Foundation::IInspectable const&,
                                    Microsoft::UI::Xaml::RoutedEventArgs const&);

private:
    void PopulateProviderEditor(size_t index);
    void ClearProviderEditor();
    void RefreshProviderSelector();
    void RefreshAssignmentSelectors();
    void RefreshExecutionTargetSelector();
    void ApplyCredentialFields();
    void UpdateEditorState();
    void SetStatus(winrt::hstring const& message);
    std::optional<::MasterControlShell::ShellProviderConnection> BuildProviderFromEditor();
    winrt::Windows::Foundation::IAsyncAction SaveProviderAsync();
    winrt::Windows::Foundation::IAsyncAction SaveProviderCredentialsAsync();
    winrt::Windows::Foundation::IAsyncAction SaveProviderAssignmentAsync();
    winrt::Windows::Foundation::IAsyncAction ExecuteProviderTaskAsync();
    winrt::Windows::Foundation::IAsyncAction SaveAiAutonomyAsync();

    ::MasterControlShell::ShellRuntime* runtime_ = nullptr;
    std::function<void()> refreshRequested_;
    std::vector<::MasterControlShell::ShellProviderConnection> providers_;
    std::vector<::MasterControlShell::ShellProviderCapability> providerCapabilities_;
    std::vector<::MasterControlShell::ShellProviderCredentialStatus> providerCredentialStatuses_;
    std::vector<::MasterControlShell::ShellProviderAssignmentTarget> providerAssignmentTargets_;
    std::vector<::MasterControlShell::ShellProviderAssignment> providerAssignments_;
    std::vector<::MasterControlShell::ShellProviderExecutionRegistration> providerExecutionRegistrations_;
    std::vector<::MasterControlShell::ShellProviderExecutionRecord> providerExecutionHistory_;
    bool aiAutonomyEnabled_ = false;
    bool providerDirty_ = false;
    bool providerCredentialsDirty_ = false;
    bool providerAssignmentDirty_ = false;
    bool providerExecutionDirty_ = false;
    bool autonomyDirty_ = false;
    bool suspendDirtyTracking_ = false;
    int selectedProviderIndex_ = -1;
    std::wstring selectedProviderId_;
    int selectedAssignmentTargetIndex_ = -1;
    int selectedExecutionTargetIndex_ = -1;
};

} // namespace winrt::MasterControlShell::implementation

namespace winrt::MasterControlShell::factory_implementation {

struct ProvidersSectionControl : ProvidersSectionControlT<ProvidersSectionControl, implementation::ProvidersSectionControl> {
};

} // namespace winrt::MasterControlShell::factory_implementation
