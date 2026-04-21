// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#pragma once

#include "ProvidersSectionControl.g.h"
#include "pch.h"

#include "ProviderAssignmentOptions.h"
#include "ShellRuntime.h"

namespace winrt::MasterControlShell::implementation {

struct ProvidersSectionControl : ProvidersSectionControlT<ProvidersSectionControl> {
    ProvidersSectionControl();

    void AttachRuntime(::MasterControlShell::ShellRuntime* runtime,
                       std::function<void()> refreshRequested,
                       std::function<void(const std::wstring&)> actionRequested);
    void ApplySnapshot(const ::MasterControlShell::ShellSnapshot& snapshot);
    void GuidedProviderActionButton_Click(Windows::Foundation::IInspectable const&,
                                          Microsoft::UI::Xaml::RoutedEventArgs const&);
    void QuickConnectProviderSelector_SelectionChanged(
        Windows::Foundation::IInspectable const&,
        Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
    void QuickConnectResponsibilitySelector_SelectionChanged(
        Windows::Foundation::IInspectable const&,
        Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
    void QuickConnectCredentialEditor_PasswordChanged(
        Windows::Foundation::IInspectable const&,
        Microsoft::UI::Xaml::RoutedEventArgs const&);
    void ConnectQuickProviderButton_Click(Windows::Foundation::IInspectable const&,
                                          Microsoft::UI::Xaml::RoutedEventArgs const&);
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
    void SubAgentGroupSelector_SelectionChanged(Windows::Foundation::IInspectable const&,
                                                Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
    void SubAgentGroupEditor_TextChanged(Windows::Foundation::IInspectable const&,
                                         Microsoft::UI::Xaml::Controls::TextChangedEventArgs const&);
    void SubAgentGroupMembersListView_SelectionChanged(Windows::Foundation::IInspectable const&,
                                                       Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
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
    void SaveSubAgentGroupButton_Click(Windows::Foundation::IInspectable const&,
                                       Microsoft::UI::Xaml::RoutedEventArgs const&);
    void RemoveSubAgentGroupButton_Click(Windows::Foundation::IInspectable const&,
                                         Microsoft::UI::Xaml::RoutedEventArgs const&);
    void SaveProviderAssignmentButton_Click(Windows::Foundation::IInspectable const&,
                                            Microsoft::UI::Xaml::RoutedEventArgs const&);
    void NewProviderButton_Click(Windows::Foundation::IInspectable const&,
                                 Microsoft::UI::Xaml::RoutedEventArgs const&);
    void NewSubAgentGroupButton_Click(Windows::Foundation::IInspectable const&,
                                      Microsoft::UI::Xaml::RoutedEventArgs const&);
    void ExecuteProviderTaskButton_Click(Windows::Foundation::IInspectable const&,
                                         Microsoft::UI::Xaml::RoutedEventArgs const&);
    void SaveAiAutonomyButton_Click(Windows::Foundation::IInspectable const&,
                                    Microsoft::UI::Xaml::RoutedEventArgs const&);
    void SignInWithClaudeButton_Click(Windows::Foundation::IInspectable const&,
                                      Microsoft::UI::Xaml::RoutedEventArgs const&);
    void SignInWithChatGptButton_Click(Windows::Foundation::IInspectable const&,
                                       Microsoft::UI::Xaml::RoutedEventArgs const&);
    void InstallClaudeCliButton_Click(Windows::Foundation::IInspectable const&,
                                      Microsoft::UI::Xaml::RoutedEventArgs const&);
    void InstallCodexCliButton_Click(Windows::Foundation::IInspectable const&,
                                     Microsoft::UI::Xaml::RoutedEventArgs const&);
    void ConnectGrokButton_Click(Windows::Foundation::IInspectable const&,
                                 Microsoft::UI::Xaml::RoutedEventArgs const&);

private:
    void SelectProviderById(const std::wstring& providerId, bool preselectAssignmentOwner);
    void OpenProviderManagementSurface(const std::wstring& workflowId);
    void RevealConnectedProvider(const std::wstring& providerId, const std::wstring& guidanceMessage);
    void PopulateProviderEditor(size_t index);
    void ClearProviderEditor();
    void RefreshProviderSelector();
    void RefreshQuickConnectProviderSelector();
    void RefreshQuickConnectResponsibilitySelector();
    void PopulateSubAgentGroupEditor(size_t index);
    void ClearSubAgentGroupEditor();
    void RefreshSubAgentGroupSelector();
    void RefreshSubAgentMemberSelector();
    void RefreshAssignmentSelectors();
    void RefreshExecutionTargetSelector();
    void ApplyCredentialFields();
    void ApplyQuickConnectFields();
    void UpdateEditorState();
    void SetStatus(winrt::hstring const& message);
    std::optional<::MasterControlShell::ShellProviderConnection> BuildProviderFromEditor();
    std::optional<::MasterControlShell::ShellSubAgentGroupDefinition> BuildSubAgentGroupFromEditor();
    winrt::Windows::Foundation::IAsyncAction ConnectQuickProviderAsync();
    winrt::Windows::Foundation::IAsyncAction SaveProviderAsync();
    winrt::Windows::Foundation::IAsyncAction SaveProviderCredentialsAsync();
    winrt::Windows::Foundation::IAsyncAction SaveSubAgentGroupAsync();
    winrt::Windows::Foundation::IAsyncAction RemoveSubAgentGroupAsync();
    winrt::Windows::Foundation::IAsyncAction SaveProviderAssignmentAsync();
    winrt::Windows::Foundation::IAsyncAction ExecuteProviderTaskAsync();
    winrt::Windows::Foundation::IAsyncAction SaveAiAutonomyAsync();
    winrt::Windows::Foundation::IAsyncAction RunCliSignInAsync(std::wstring bridge, std::wstring providerId);
    winrt::Windows::Foundation::IAsyncAction InstallCliDependencyAsync(std::wstring bridge);
    winrt::Windows::Foundation::IAsyncAction RefreshCliInstallStateAsync();
    winrt::Windows::Foundation::IAsyncAction ConnectGrokAsync(std::wstring apiKey);

    ::MasterControlShell::ShellRuntime* runtime_ = nullptr;
    std::function<void()> refreshRequested_;
    std::function<void(const std::wstring&)> actionRequested_;
    std::vector<::MasterControlShell::ShellProviderConnection> providers_;
    std::vector<::MasterControlShell::ShellProviderCapability> providerCapabilities_;
    std::vector<::MasterControlShell::ShellProviderCredentialStatus> providerCredentialStatuses_;
    std::vector<::MasterControlShell::ShellSubAgentGroupDefinition> subAgentGroups_;
    std::vector<::MasterControlShell::ShellProviderAssignmentTarget> providerAssignmentTargets_;
    std::vector<::MasterControlShell::ShellProviderAssignment> providerAssignments_;
    std::vector<::MasterControlShell::ShellProviderExecutionRegistration> providerExecutionRegistrations_;
    std::vector<::MasterControlShell::ShellProviderExecutionRecord> providerExecutionHistory_;
    bool aiAutonomyEnabled_ = false;
    bool providerDirty_ = false;
    bool providerCredentialsDirty_ = false;
    bool subAgentGroupDirty_ = false;
    bool providerAssignmentDirty_ = false;
    bool providerExecutionDirty_ = false;
    bool autonomyDirty_ = false;
    bool suspendDirtyTracking_ = false;
    int selectedProviderIndex_ = -1;
    std::wstring selectedProviderId_;
    std::wstring selectedQuickConnectProviderId_;
    // Multi-select target ids chosen in the AutoConnectRoleSelector ListView.
    // Replaces the former single-responsibility ComboBox selection.
    std::vector<std::wstring> selectedAutoConnectRoleTargetIds_;
    bool autoConnectRoleHandlerWired_ = false;
    bool quickConnectActive_ = false; // true while user is editing the Auto-Connect card; blocks ApplySnapshot from touching those controls
    int selectedSubAgentGroupIndex_ = -1;
    std::wstring selectedSubAgentGroupId_;
    int selectedAssignmentTargetIndex_ = -1;
    std::wstring selectedAssignmentProviderId_;
    std::vector<::MasterControlShell::AssignableProviderOption> assignmentProviderOptions_;
    int selectedExecutionTargetIndex_ = -1;
};

} // namespace winrt::MasterControlShell::implementation

namespace winrt::MasterControlShell::factory_implementation {

struct ProvidersSectionControl : ProvidersSectionControlT<ProvidersSectionControl, implementation::ProvidersSectionControl> {
};

} // namespace winrt::MasterControlShell::factory_implementation
