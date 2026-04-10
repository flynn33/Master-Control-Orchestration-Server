// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#include "pch.h"

#include "ProvidersSectionControl.xaml.h"

#if __has_include("ProvidersSectionControl.g.cpp")
#include "ProvidersSectionControl.g.cpp"
#endif

#include "ShellFormatting.h"

#include <optional>

namespace winrt::MasterControlShell::implementation {

using namespace ::MasterControlShell::Presentation;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

namespace {

const ::MasterControlShell::ShellProviderCapability* findCapabilityForProvider(
    const std::vector<::MasterControlShell::ShellProviderCapability>& capabilities,
    const ::MasterControlShell::ShellProviderConnection& provider) {
    const auto iterator = std::find_if(
        capabilities.begin(),
        capabilities.end(),
        [&provider](const auto& capability) { return capability.kind == provider.kind; });
    return iterator == capabilities.end() ? nullptr : &(*iterator);
}

const ::MasterControlShell::ShellProviderCredentialStatus* findCredentialStatus(
    const std::vector<::MasterControlShell::ShellProviderCredentialStatus>& statuses,
    std::wstring_view providerId) {
    const auto iterator = std::find_if(
        statuses.begin(),
        statuses.end(),
        [providerId](const auto& status) { return status.providerId == providerId; });
    return iterator == statuses.end() ? nullptr : &(*iterator);
}

const ::MasterControlShell::ShellProviderCapability* findCapabilityByProviderId(
    const std::vector<::MasterControlShell::ShellProviderCapability>& capabilities,
    std::wstring_view providerId) {
    const auto iterator = std::find_if(
        capabilities.begin(),
        capabilities.end(),
        [providerId](const auto& capability) { return capability.providerId == providerId; });
    return iterator == capabilities.end() ? nullptr : &(*iterator);
}

bool providerSupportsTarget(const ::MasterControlShell::ShellProviderCapability& capability, std::wstring_view targetId) {
    return capability.supportedTargets.empty() ||
           std::find(capability.supportedTargets.begin(), capability.supportedTargets.end(), targetId) != capability.supportedTargets.end();
}

std::wstring executionRegistrationRow(const ::MasterControlShell::ShellProviderExecutionRegistration& registration) {
    std::wstring label = registration.displayName.empty() ? registration.providerId : registration.displayName;
    label += L"  |  ";
    label += registration.transport;
    label += registration.supportsSharedMcpAccess ? L"  |  shared MCP" : L"  |  isolated";
    if (registration.supportsDirectMcpConfig) {
        label += L"  |  direct config";
    }
    return label;
}

std::wstring executionHistoryRow(const ::MasterControlShell::ShellProviderExecutionRecord& record) {
    std::wstring label = L"[";
    label += record.status;
    label += L"] ";
    label += record.targetDisplayName.empty() ? record.targetId : record.targetDisplayName;
    label += L"  |  ";
    label += record.providerDisplayName.empty() ? record.providerId : record.providerDisplayName;
    if (!record.completedAtUtc.empty()) {
        label += L"  |  ";
        label += record.completedAtUtc;
    }
    return label;
}

std::wstring joinValues(const std::vector<std::wstring>& values, const wchar_t* separator = L", ") {
    std::wstring result;
    for (size_t index = 0; index < values.size(); ++index) {
        if (index > 0) {
            result += separator;
        }
        result += values[index];
    }
    return result;
}

} // namespace

ProvidersSectionControl::ProvidersSectionControl() {
    InitializeComponent();
}

void ProvidersSectionControl::AttachRuntime(::MasterControlShell::ShellRuntime* runtime,
                                            std::function<void()> refreshRequested,
                                            std::function<void(const std::wstring&)> actionRequested) {
    runtime_ = runtime;
    refreshRequested_ = std::move(refreshRequested);
    actionRequested_ = std::move(actionRequested);
    RefreshQuickConnectProviderSelector();
    RefreshQuickConnectResponsibilitySelector();
    ApplyQuickConnectFields();
    ApplyCredentialFields();
    RefreshSubAgentGroupSelector();
    RefreshSubAgentMemberSelector();
    RefreshAssignmentSelectors();
    RefreshExecutionTargetSelector();
    UpdateEditorState();
}

void ProvidersSectionControl::GuidedProviderActionButton_Click(
    Windows::Foundation::IInspectable const& sender,
    Microsoft::UI::Xaml::RoutedEventArgs const&) {
    const auto button = sender.try_as<Microsoft::UI::Xaml::Controls::Button>();
    if (button == nullptr) {
        return;
    }
    if (!actionRequested_) {
        SetStatus(L"Guided provider workflows are not attached to the shell yet.");
        return;
    }

    const auto workflowId = std::wstring(winrt::unbox_value_or<winrt::hstring>(button.Tag(), winrt::hstring()).c_str());
    if (workflowId.empty()) {
        SetStatus(L"Providers could not resolve the requested guided workflow.");
        return;
    }

    actionRequested_(workflowId);
}

void ProvidersSectionControl::QuickConnectProviderSelector_SelectionChanged(
    Windows::Foundation::IInspectable const&,
    SelectionChangedEventArgs const&) {
    if (suspendDirtyTracking_) {
        return;
    }

    selectedQuickConnectProviderId_.clear();
    if (const auto selectedItem = QuickConnectProviderSelector().SelectedItem().try_as<ComboBoxItem>()) {
        selectedQuickConnectProviderId_ =
            std::wstring(unbox_value_or<winrt::hstring>(selectedItem.Tag(), winrt::hstring()).c_str());
    }

    QuickConnectCredentialFieldOneValueBox().Password(L"");
    QuickConnectCredentialFieldTwoValueBox().Password(L"");
    selectedQuickConnectResponsibilityTargetId_.clear();
    RefreshQuickConnectResponsibilitySelector();
    ApplyQuickConnectFields();
    QuickConnectStatusText().Text(
        selectedQuickConnectProviderId_.empty()
            ? L"Choose a provider to start the quick-connect flow."
            : L"Paste the required credentials and click Connect Provider.");
    UpdateEditorState();
}

void ProvidersSectionControl::QuickConnectResponsibilitySelector_SelectionChanged(
    Windows::Foundation::IInspectable const&,
    SelectionChangedEventArgs const&) {
    selectedQuickConnectResponsibilityTargetId_.clear();
    if (const auto selectedItem = QuickConnectResponsibilitySelector().SelectedItem().try_as<ComboBoxItem>()) {
        selectedQuickConnectResponsibilityTargetId_ =
            std::wstring(unbox_value_or<winrt::hstring>(selectedItem.Tag(), winrt::hstring()).c_str());
    }
    UpdateEditorState();
}

void ProvidersSectionControl::QuickConnectCredentialEditor_PasswordChanged(
    Windows::Foundation::IInspectable const&,
    Microsoft::UI::Xaml::RoutedEventArgs const&) {
    UpdateEditorState();
}

void ProvidersSectionControl::ConnectQuickProviderButton_Click(
    Windows::Foundation::IInspectable const&,
    Microsoft::UI::Xaml::RoutedEventArgs const&) {
    ConnectQuickProviderAsync();
}

void ProvidersSectionControl::ApplySnapshot(const ::MasterControlShell::ShellSnapshot& snapshot) {
    providers_ = snapshot.providers;
    providerCapabilities_ = snapshot.providerCapabilities;
    providerCredentialStatuses_ = snapshot.providerCredentialStatuses;
    subAgentGroups_ = snapshot.subAgentGroups;
    providerAssignmentTargets_ = snapshot.providerAssignmentTargets;
    providerAssignments_ = snapshot.providerAssignments;
    providerExecutionRegistrations_ = snapshot.providerExecutionRegistrations;
    providerExecutionHistory_ = snapshot.providerExecutionHistory;
    aiAutonomyEnabled_ = snapshot.aiAutonomyEnabled;

    ProviderRouteCountText().Text(winrt::hstring(std::to_wstring(snapshot.providerCount)));
    ProviderAutonomyText().Text(snapshot.aiAutonomyEnabled ? L"Autonomy enabled" : L"Human-directed");
    ProvidersNarrativeText().Text(winrt::hstring(formatProvidersNarrative(snapshot)));
    populateListView(ProvidersListView(), snapshot.providerRows);
    populateListView(ProviderCapabilitiesListView(), snapshot.providerCapabilityRows);
    populateListView(ProviderAssignmentsListView(), snapshot.providerAssignmentRows);
    std::vector<std::wstring> executionHistoryRows;
    executionHistoryRows.reserve(providerExecutionHistory_.size());
    for (const auto& record : providerExecutionHistory_) {
        executionHistoryRows.push_back(executionHistoryRow(record));
    }
    if (executionHistoryRows.empty()) {
        executionHistoryRows.push_back(L"No provider execution history has been recorded yet.");
    }
    populateListView(ProviderExecutionHistoryListView(), executionHistoryRows);

    RefreshQuickConnectProviderSelector();
    RefreshProviderSelector();
    RefreshSubAgentGroupSelector();
    RefreshSubAgentMemberSelector();
    RefreshAssignmentSelectors();
    RefreshExecutionTargetSelector();
    RefreshQuickConnectResponsibilitySelector();

    if (!autonomyDirty_) {
        suspendDirtyTracking_ = true;
        AiAutonomyToggle().IsOn(snapshot.aiAutonomyEnabled);
        suspendDirtyTracking_ = false;
    }

    if (!providerDirty_) {
        if (!selectedProviderId_.empty()) {
            const auto iterator = std::find_if(
                providers_.begin(),
                providers_.end(),
                [this](const auto& provider) { return provider.id == selectedProviderId_; });
            if (iterator != providers_.end()) {
                PopulateProviderEditor(static_cast<size_t>(std::distance(providers_.begin(), iterator)));
            } else if (!providers_.empty()) {
                PopulateProviderEditor(0);
            } else {
                ClearProviderEditor();
            }
        } else if (!providers_.empty()) {
            PopulateProviderEditor(0);
        } else {
            ClearProviderEditor();
        }
    }

    if (!subAgentGroupDirty_) {
        if (!selectedSubAgentGroupId_.empty()) {
            const auto iterator = std::find_if(
                subAgentGroups_.begin(),
                subAgentGroups_.end(),
                [this](const auto& group) { return group.groupId == selectedSubAgentGroupId_; });
            if (iterator != subAgentGroups_.end()) {
                PopulateSubAgentGroupEditor(static_cast<size_t>(std::distance(subAgentGroups_.begin(), iterator)));
            } else if (!subAgentGroups_.empty()) {
                PopulateSubAgentGroupEditor(0);
            } else {
                ClearSubAgentGroupEditor();
            }
        } else if (!subAgentGroups_.empty()) {
            PopulateSubAgentGroupEditor(0);
        } else {
            ClearSubAgentGroupEditor();
        }
    }

    ApplyQuickConnectFields();
    ApplyCredentialFields();
    if (!providerExecutionHistory_.empty()) {
        const auto& lastRecord = providerExecutionHistory_.front();
        ProviderExecutionOutputTextBox().Text(winrt::hstring(
            lastRecord.outputText.empty() ? lastRecord.rawResponse : lastRecord.outputText));
        if (!providerExecutionDirty_) {
            ProviderExecutionStatusText().Text(winrt::hstring(
                lastRecord.errorMessage.empty() ? L"Last provider task completed." : lastRecord.errorMessage));
        }
    } else if (!providerExecutionDirty_) {
        ProviderExecutionOutputTextBox().Text(L"");
        ProviderExecutionStatusText().Text(L"No provider task has been executed yet.");
    }
    UpdateEditorState();
}

void ProvidersSectionControl::ProviderSelector_SelectionChanged(
    Windows::Foundation::IInspectable const&,
    SelectionChangedEventArgs const&) {
    if (suspendDirtyTracking_) {
        return;
    }

    const auto index = ProviderSelector().SelectedIndex();
    if (index < 0 || index >= static_cast<int>(providers_.size())) {
        return;
    }

    providerDirty_ = false;
    providerCredentialsDirty_ = false;
    PopulateProviderEditor(static_cast<size_t>(index));
    ApplyCredentialFields();
    SetStatus(L"Loaded provider route into the editor.");
    UpdateEditorState();
}

void ProvidersSectionControl::ProviderEditor_TextChanged(
    Windows::Foundation::IInspectable const&,
    TextChangedEventArgs const&) {
    if (!suspendDirtyTracking_) {
        providerDirty_ = true;
        UpdateEditorState();
    }
}

void ProvidersSectionControl::ProviderKind_SelectionChanged(
    Windows::Foundation::IInspectable const&,
    SelectionChangedEventArgs const&) {
    if (!suspendDirtyTracking_) {
        providerDirty_ = true;
        ApplyCredentialFields();
        UpdateEditorState();
    }
}

void ProvidersSectionControl::ProviderToggle_Toggled(
    Windows::Foundation::IInspectable const&,
    Microsoft::UI::Xaml::RoutedEventArgs const&) {
    if (!suspendDirtyTracking_) {
        providerDirty_ = true;
        UpdateEditorState();
    }
}

void ProvidersSectionControl::ProviderCredentialEditor_PasswordChanged(
    Windows::Foundation::IInspectable const&,
    Microsoft::UI::Xaml::RoutedEventArgs const&) {
    if (!suspendDirtyTracking_) {
        providerCredentialsDirty_ = true;
        UpdateEditorState();
    }
}

void ProvidersSectionControl::SubAgentGroupSelector_SelectionChanged(
    Windows::Foundation::IInspectable const&,
    SelectionChangedEventArgs const&) {
    if (suspendDirtyTracking_) {
        return;
    }

    const auto index = SubAgentGroupSelector().SelectedIndex();
    if (index < 0 || index >= static_cast<int>(subAgentGroups_.size())) {
        return;
    }

    subAgentGroupDirty_ = false;
    PopulateSubAgentGroupEditor(static_cast<size_t>(index));
    SubAgentGroupStatusText().Text(L"Loaded sub-agent group into the editor.");
    UpdateEditorState();
}

void ProvidersSectionControl::SubAgentGroupEditor_TextChanged(
    Windows::Foundation::IInspectable const&,
    TextChangedEventArgs const&) {
    if (!suspendDirtyTracking_) {
        subAgentGroupDirty_ = true;
        UpdateEditorState();
    }
}

void ProvidersSectionControl::SubAgentGroupMembersListView_SelectionChanged(
    Windows::Foundation::IInspectable const&,
    SelectionChangedEventArgs const&) {
    if (!suspendDirtyTracking_) {
        subAgentGroupDirty_ = true;
        UpdateEditorState();
    }
}

void ProvidersSectionControl::ProviderAssignmentTargetSelector_SelectionChanged(
    Windows::Foundation::IInspectable const&,
    SelectionChangedEventArgs const&) {
    if (!suspendDirtyTracking_) {
        selectedAssignmentTargetIndex_ = ProviderAssignmentTargetSelector().SelectedIndex();
        providerAssignmentDirty_ = true;
        UpdateEditorState();
    }
}

void ProvidersSectionControl::ProviderAssignmentProviderSelector_SelectionChanged(
    Windows::Foundation::IInspectable const&,
    SelectionChangedEventArgs const&) {
    if (!suspendDirtyTracking_) {
        providerAssignmentDirty_ = true;
        UpdateEditorState();
    }
}

void ProvidersSectionControl::ProviderExecutionTargetSelector_SelectionChanged(
    Windows::Foundation::IInspectable const&,
    SelectionChangedEventArgs const&) {
    if (!suspendDirtyTracking_) {
        selectedExecutionTargetIndex_ = ProviderExecutionTargetSelector().SelectedIndex();
        providerExecutionDirty_ = true;
        UpdateEditorState();
    }
}

void ProvidersSectionControl::ProviderExecutionEditor_Changed(
    Windows::Foundation::IInspectable const&,
    Microsoft::UI::Xaml::RoutedEventArgs const&) {
    if (!suspendDirtyTracking_) {
        providerExecutionDirty_ = true;
        UpdateEditorState();
    }
}

void ProvidersSectionControl::ProviderExecutionEditor_Changed(
    Windows::Foundation::IInspectable const&,
    TextChangedEventArgs const&) {
    if (!suspendDirtyTracking_) {
        providerExecutionDirty_ = true;
        UpdateEditorState();
    }
}

void ProvidersSectionControl::AiAutonomyToggle_Toggled(
    Windows::Foundation::IInspectable const&,
    Microsoft::UI::Xaml::RoutedEventArgs const&) {
    if (!suspendDirtyTracking_) {
        autonomyDirty_ = true;
        UpdateEditorState();
    }
}

void ProvidersSectionControl::SaveProviderButton_Click(
    Windows::Foundation::IInspectable const&,
    Microsoft::UI::Xaml::RoutedEventArgs const&) {
    SaveProviderAsync();
}

void ProvidersSectionControl::SaveProviderCredentialsButton_Click(
    Windows::Foundation::IInspectable const&,
    Microsoft::UI::Xaml::RoutedEventArgs const&) {
    SaveProviderCredentialsAsync();
}

void ProvidersSectionControl::SaveSubAgentGroupButton_Click(
    Windows::Foundation::IInspectable const&,
    Microsoft::UI::Xaml::RoutedEventArgs const&) {
    SaveSubAgentGroupAsync();
}

void ProvidersSectionControl::RemoveSubAgentGroupButton_Click(
    Windows::Foundation::IInspectable const&,
    Microsoft::UI::Xaml::RoutedEventArgs const&) {
    RemoveSubAgentGroupAsync();
}

void ProvidersSectionControl::SaveProviderAssignmentButton_Click(
    Windows::Foundation::IInspectable const&,
    Microsoft::UI::Xaml::RoutedEventArgs const&) {
    SaveProviderAssignmentAsync();
}

void ProvidersSectionControl::NewProviderButton_Click(
    Windows::Foundation::IInspectable const&,
    Microsoft::UI::Xaml::RoutedEventArgs const&) {
    selectedProviderIndex_ = -1;
    selectedProviderId_.clear();
    providerDirty_ = true;
    providerCredentialsDirty_ = false;
    ClearProviderEditor();
    ApplyCredentialFields();
    SetStatus(L"Staged a new provider route. Fill in the editor and save it.");
    UpdateEditorState();
}

void ProvidersSectionControl::NewSubAgentGroupButton_Click(
    Windows::Foundation::IInspectable const&,
    Microsoft::UI::Xaml::RoutedEventArgs const&) {
    selectedSubAgentGroupIndex_ = -1;
    selectedSubAgentGroupId_.clear();
    subAgentGroupDirty_ = true;
    ClearSubAgentGroupEditor();
    SubAgentGroupStatusText().Text(L"Staged a new sub-agent group. Select members and save it.");
    UpdateEditorState();
}

void ProvidersSectionControl::ExecuteProviderTaskButton_Click(
    Windows::Foundation::IInspectable const&,
    Microsoft::UI::Xaml::RoutedEventArgs const&) {
    ExecuteProviderTaskAsync();
}

void ProvidersSectionControl::SaveAiAutonomyButton_Click(
    Windows::Foundation::IInspectable const&,
    Microsoft::UI::Xaml::RoutedEventArgs const&) {
    SaveAiAutonomyAsync();
}

void ProvidersSectionControl::PopulateProviderEditor(const size_t index) {
    if (index >= providers_.size()) {
        ClearProviderEditor();
        return;
    }

    suspendDirtyTracking_ = true;
    selectedProviderIndex_ = static_cast<int>(index);
    selectedProviderId_ = providers_[index].id;
    selectedQuickConnectProviderId_ = providers_[index].id;

    const auto& provider = providers_[index];
    ProviderSelector().SelectedIndex(selectedProviderIndex_);
    ProviderIdTextBox().Text(winrt::hstring(provider.id));
    ProviderDisplayNameTextBox().Text(winrt::hstring(provider.displayName));
    ProviderBaseUrlTextBox().Text(winrt::hstring(provider.baseUrl));
    ProviderModelIdTextBox().Text(winrt::hstring(provider.modelId));
    ProviderEnabledToggle().IsOn(provider.enabled);
    ProviderAutonomousToggle().IsOn(provider.allowAutonomousControl);

    int selectedKindIndex = 4;
    const auto desiredKind = provider.kind.empty() ? std::wstring(L"generic") : provider.kind;
    for (uint32_t itemIndex = 0; itemIndex < ProviderKindComboBox().Items().Size(); ++itemIndex) {
        if (const auto item = ProviderKindComboBox().Items().GetAt(itemIndex).try_as<ComboBoxItem>()) {
            const auto tagValue =
                std::wstring(unbox_value_or<winrt::hstring>(item.Tag(), winrt::hstring(L"generic")).c_str());
            const auto contentValue =
                std::wstring(unbox_value_or<winrt::hstring>(item.Content(), winrt::hstring()).c_str());
            if (tagValue == desiredKind) {
                if (provider.id == L"chatgpt" && contentValue == L"ChatGPT") {
                    selectedKindIndex = static_cast<int>(itemIndex);
                    break;
                }
                if (provider.id == L"xai-grok" && contentValue == L"Grok") {
                    selectedKindIndex = static_cast<int>(itemIndex);
                    break;
                }
                if (provider.id != L"chatgpt" && provider.id != L"xai-grok") {
                    selectedKindIndex = static_cast<int>(itemIndex);
                    break;
                }
            }
        }
    }
    ProviderKindComboBox().SelectedIndex(selectedKindIndex);
    suspendDirtyTracking_ = false;
    RefreshQuickConnectProviderSelector();
    RefreshQuickConnectResponsibilitySelector();
    ApplyQuickConnectFields();
}

void ProvidersSectionControl::ClearProviderEditor() {
    suspendDirtyTracking_ = true;
    selectedProviderId_.clear();
    selectedProviderIndex_ = -1;
    ProviderSelector().SelectedIndex(-1);
    ProviderIdTextBox().Text(L"");
    ProviderDisplayNameTextBox().Text(L"");
    ProviderBaseUrlTextBox().Text(L"");
    ProviderModelIdTextBox().Text(L"");
    ProviderEnabledToggle().IsOn(true);
    ProviderAutonomousToggle().IsOn(false);
    ProviderKindComboBox().SelectedIndex(4);
    ProviderCredentialFieldOneValueBox().Password(L"");
    ProviderCredentialFieldTwoValueBox().Password(L"");
    suspendDirtyTracking_ = false;
}

void ProvidersSectionControl::PopulateSubAgentGroupEditor(const size_t index) {
    if (index >= subAgentGroups_.size()) {
        ClearSubAgentGroupEditor();
        return;
    }

    suspendDirtyTracking_ = true;
    const auto& group = subAgentGroups_[index];
    selectedSubAgentGroupIndex_ = static_cast<int>(index);
    selectedSubAgentGroupId_ = group.groupId;

    SubAgentGroupSelector().SelectedIndex(selectedSubAgentGroupIndex_);
    SubAgentGroupIdTextBox().Text(winrt::hstring(group.groupId));
    SubAgentGroupDisplayNameTextBox().Text(winrt::hstring(group.displayName));
    SubAgentGroupDescriptionTextBox().Text(winrt::hstring(group.description));

    for (uint32_t itemIndex = 0; itemIndex < SubAgentGroupMembersListView().Items().Size(); ++itemIndex) {
        if (const auto item = SubAgentGroupMembersListView().Items().GetAt(itemIndex).try_as<ListViewItem>()) {
            const auto targetId = std::wstring(unbox_value_or<winrt::hstring>(item.Tag(), winrt::hstring(L"")).c_str());
            item.IsSelected(
                std::find(group.memberTargetIds.begin(), group.memberTargetIds.end(), targetId) != group.memberTargetIds.end());
        }
    }

    suspendDirtyTracking_ = false;
}

void ProvidersSectionControl::ClearSubAgentGroupEditor() {
    suspendDirtyTracking_ = true;
    SubAgentGroupSelector().SelectedIndex(-1);
    SubAgentGroupIdTextBox().Text(L"");
    SubAgentGroupDisplayNameTextBox().Text(L"");
    SubAgentGroupDescriptionTextBox().Text(L"");
    for (uint32_t itemIndex = 0; itemIndex < SubAgentGroupMembersListView().Items().Size(); ++itemIndex) {
        if (const auto item = SubAgentGroupMembersListView().Items().GetAt(itemIndex).try_as<ListViewItem>()) {
            item.IsSelected(false);
        }
    }
    suspendDirtyTracking_ = false;
}

void ProvidersSectionControl::RefreshProviderSelector() {
    const auto previousSelection = selectedProviderId_;
    suspendDirtyTracking_ = true;
    ProviderSelector().Items().Clear();
    for (const auto& provider : providers_) {
        std::wstring label = provider.displayName.empty() ? provider.id : provider.displayName;
        if (!provider.baseUrl.empty()) {
            label += L"  |  ";
            label += provider.baseUrl;
        }
        ProviderSelector().Items().Append(winrt::box_value(winrt::hstring(label)));
    }

    if (!previousSelection.empty()) {
        for (size_t index = 0; index < providers_.size(); ++index) {
            if (providers_[index].id == previousSelection) {
                ProviderSelector().SelectedIndex(static_cast<int>(index));
                break;
            }
        }
    }
    suspendDirtyTracking_ = false;
}

void ProvidersSectionControl::RefreshQuickConnectProviderSelector() {
    const auto previousSelection = selectedQuickConnectProviderId_;
    suspendDirtyTracking_ = true;
    selectedQuickConnectProviderId_.clear();
    QuickConnectProviderSelector().Items().Clear();

    for (const auto& capability : providerCapabilities_) {
        ComboBoxItem item;
        item.Content(winrt::box_value(winrt::hstring(
            capability.displayName.empty() ? capability.providerId : capability.displayName)));
        item.Tag(winrt::box_value(winrt::hstring(capability.providerId)));
        QuickConnectProviderSelector().Items().Append(item);
    }

    int selectedIndex = -1;
    if (!previousSelection.empty()) {
        for (uint32_t itemIndex = 0; itemIndex < QuickConnectProviderSelector().Items().Size(); ++itemIndex) {
            if (const auto item = QuickConnectProviderSelector().Items().GetAt(itemIndex).try_as<ComboBoxItem>()) {
                const auto providerId =
                    std::wstring(unbox_value_or<winrt::hstring>(item.Tag(), winrt::hstring()).c_str());
                if (providerId == previousSelection) {
                    selectedIndex = static_cast<int>(itemIndex);
                    break;
                }
            }
        }
    }

    if (selectedIndex < 0 && QuickConnectProviderSelector().Items().Size() > 0) {
        selectedIndex = 0;
        if (const auto item = QuickConnectProviderSelector().Items().GetAt(0).try_as<ComboBoxItem>()) {
            selectedQuickConnectProviderId_ =
                std::wstring(unbox_value_or<winrt::hstring>(item.Tag(), winrt::hstring()).c_str());
        }
    }

    QuickConnectProviderSelector().SelectedIndex(selectedIndex);
    suspendDirtyTracking_ = false;
}

void ProvidersSectionControl::RefreshQuickConnectResponsibilitySelector() {
    const auto previousSelection = selectedQuickConnectResponsibilityTargetId_;
    suspendDirtyTracking_ = true;
    QuickConnectResponsibilitySelector().Items().Clear();

    ComboBoxItem unassigned;
    unassigned.Content(winrt::box_value(winrt::hstring(L"(Leave unassigned)")));
    unassigned.Tag(winrt::box_value(winrt::hstring(L"")));
    QuickConnectResponsibilitySelector().Items().Append(unassigned);

    const auto* capability = findCapabilityByProviderId(providerCapabilities_, selectedQuickConnectProviderId_);
    for (const auto& target : providerAssignmentTargets_) {
        if (target.kind != L"role") {
            continue;
        }
        if (capability != nullptr && !providerSupportsTarget(*capability, target.targetId)) {
            continue;
        }

        ComboBoxItem item;
        item.Content(winrt::box_value(winrt::hstring(target.displayName.empty() ? target.targetId : target.displayName)));
        item.Tag(winrt::box_value(winrt::hstring(target.targetId)));
        QuickConnectResponsibilitySelector().Items().Append(item);
    }

    int selectedIndex = 0;
    if (!previousSelection.empty()) {
        for (uint32_t itemIndex = 1; itemIndex < QuickConnectResponsibilitySelector().Items().Size(); ++itemIndex) {
            if (const auto item = QuickConnectResponsibilitySelector().Items().GetAt(itemIndex).try_as<ComboBoxItem>()) {
                const auto targetId =
                    std::wstring(unbox_value_or<winrt::hstring>(item.Tag(), winrt::hstring()).c_str());
                if (targetId == previousSelection) {
                    selectedIndex = static_cast<int>(itemIndex);
                    break;
                }
            }
        }
    }

    QuickConnectResponsibilitySelector().SelectedIndex(selectedIndex);
    selectedQuickConnectResponsibilityTargetId_.clear();
    if (selectedIndex > 0) {
        if (const auto item = QuickConnectResponsibilitySelector().SelectedItem().try_as<ComboBoxItem>()) {
            selectedQuickConnectResponsibilityTargetId_ =
                std::wstring(unbox_value_or<winrt::hstring>(item.Tag(), winrt::hstring()).c_str());
        }
    }
    suspendDirtyTracking_ = false;
}

void ProvidersSectionControl::RefreshSubAgentGroupSelector() {
    const auto previousSelection = selectedSubAgentGroupId_;
    suspendDirtyTracking_ = true;
    SubAgentGroupSelector().Items().Clear();
    for (const auto& group : subAgentGroups_) {
        std::wstring label = group.displayName.empty() ? group.groupId : group.displayName;
        label += L"  |  ";
        label += std::to_wstring(group.memberTargetIds.size());
        label += L" members";
        SubAgentGroupSelector().Items().Append(winrt::box_value(winrt::hstring(label)));
    }

    if (!previousSelection.empty()) {
        for (size_t index = 0; index < subAgentGroups_.size(); ++index) {
            if (subAgentGroups_[index].groupId == previousSelection) {
                SubAgentGroupSelector().SelectedIndex(static_cast<int>(index));
                break;
            }
        }
    }
    suspendDirtyTracking_ = false;
}

void ProvidersSectionControl::RefreshSubAgentMemberSelector() {
    suspendDirtyTracking_ = true;
    SubAgentGroupMembersListView().Items().Clear();
    for (const auto& target : providerAssignmentTargets_) {
        if (target.kind != L"sub_agent") {
            continue;
        }

        ListViewItem item;
        item.Content(winrt::box_value(winrt::hstring(target.displayName.empty() ? target.targetId : target.displayName)));
        item.Tag(winrt::box_value(winrt::hstring(target.targetId)));
        SubAgentGroupMembersListView().Items().Append(item);
    }
    suspendDirtyTracking_ = false;
}

void ProvidersSectionControl::RefreshAssignmentSelectors() {
    const auto previousTargetIndex = selectedAssignmentTargetIndex_;
    suspendDirtyTracking_ = true;

    ProviderAssignmentTargetSelector().Items().Clear();
    for (const auto& target : providerAssignmentTargets_) {
        std::wstring label = target.displayName;
        if (target.kind == L"sub_agent_group") {
            label += L"  |  ";
            label += std::to_wstring(target.memberTargetIds.size());
            label += L" members";
        }
        ProviderAssignmentTargetSelector().Items().Append(winrt::box_value(winrt::hstring(label)));
    }

    ProviderAssignmentProviderSelector().Items().Clear();
    ProviderAssignmentProviderSelector().Items().Append(winrt::box_value(winrt::hstring(L"(Unassigned)")));
    for (const auto& provider : providers_) {
        ProviderAssignmentProviderSelector().Items().Append(
            winrt::box_value(winrt::hstring(provider.displayName.empty() ? provider.id : provider.displayName)));
    }

    if (previousTargetIndex >= 0 && previousTargetIndex < static_cast<int>(providerAssignmentTargets_.size())) {
        ProviderAssignmentTargetSelector().SelectedIndex(previousTargetIndex);
    } else if (!providerAssignmentTargets_.empty()) {
        ProviderAssignmentTargetSelector().SelectedIndex(0);
        selectedAssignmentTargetIndex_ = 0;
    }

    if (selectedAssignmentTargetIndex_ >= 0 &&
        selectedAssignmentTargetIndex_ < static_cast<int>(providerAssignmentTargets_.size())) {
        const auto& target = providerAssignmentTargets_[static_cast<size_t>(selectedAssignmentTargetIndex_)];
        const auto assignmentIterator = std::find_if(
            providerAssignments_.begin(),
            providerAssignments_.end(),
            [&target](const auto& assignment) { return assignment.targetId == target.targetId; });
        if (assignmentIterator != providerAssignments_.end()) {
            const auto providerIterator = std::find_if(
                providers_.begin(),
                providers_.end(),
                [&assignmentIterator](const auto& provider) { return provider.id == assignmentIterator->providerId; });
            if (providerIterator != providers_.end()) {
                ProviderAssignmentProviderSelector().SelectedIndex(
                    static_cast<int>(std::distance(providers_.begin(), providerIterator)) + 1);
            } else {
                ProviderAssignmentProviderSelector().SelectedIndex(0);
            }
        } else {
            ProviderAssignmentProviderSelector().SelectedIndex(0);
        }
    }

    suspendDirtyTracking_ = false;
}

void ProvidersSectionControl::RefreshExecutionTargetSelector() {
    const auto previousTargetIndex = selectedExecutionTargetIndex_;
    suspendDirtyTracking_ = true;

    ProviderExecutionTargetSelector().Items().Clear();
    for (const auto& target : providerAssignmentTargets_) {
        std::wstring label = target.displayName;
        if (target.kind == L"sub_agent_group") {
            label += L"  |  ";
            label += std::to_wstring(target.memberTargetIds.size());
            label += L" members";
        }
        ProviderExecutionTargetSelector().Items().Append(winrt::box_value(winrt::hstring(label)));
    }

    if (previousTargetIndex >= 0 && previousTargetIndex < static_cast<int>(providerAssignmentTargets_.size())) {
        ProviderExecutionTargetSelector().SelectedIndex(previousTargetIndex);
    } else if (!providerAssignmentTargets_.empty()) {
        ProviderExecutionTargetSelector().SelectedIndex(0);
        selectedExecutionTargetIndex_ = 0;
    } else {
        selectedExecutionTargetIndex_ = -1;
    }

    suspendDirtyTracking_ = false;
}

void ProvidersSectionControl::ApplyCredentialFields() {
    auto setField = [](StackPanel const& panel,
                       TextBlock const& label,
                       PasswordBox const& box,
                       TextBlock const& hint,
                       const std::optional<::MasterControlShell::ShellProviderCredentialField>& field,
                       const std::wstring& fallbackLabel) {
        if (!field.has_value()) {
            panel.Visibility(Visibility::Collapsed);
            label.Text(fallbackLabel);
            box.Password(L"");
            hint.Text(L"");
            return;
        }

        panel.Visibility(Visibility::Visible);
        label.Text(winrt::hstring(field->label));
        box.PlaceholderText(winrt::hstring(field->placeholder.empty() ? L"Credential value" : field->placeholder));
        std::wstring hintText = field->helpText;
        if (!field->environmentVariableHint.empty()) {
            if (!hintText.empty()) {
                hintText += L" ";
            }
            hintText += L"Env: ";
            hintText += field->environmentVariableHint;
        }
        hint.Text(winrt::hstring(hintText));
    };

    if (selectedProviderId_.empty()) {
        ProviderCredentialStatusText().Text(L"Save or select a provider route to configure credentials.");
        setField(ProviderCredentialFieldOnePanel(), ProviderCredentialFieldOneLabel(), ProviderCredentialFieldOneValueBox(), ProviderCredentialFieldOneHint(), std::nullopt, L"Credential Field");
        setField(ProviderCredentialFieldTwoPanel(), ProviderCredentialFieldTwoLabel(), ProviderCredentialFieldTwoValueBox(), ProviderCredentialFieldTwoHint(), std::nullopt, L"Credential Field");
        return;
    }

    const auto providerIterator = std::find_if(
        providers_.begin(),
        providers_.end(),
        [this](const auto& provider) { return provider.id == selectedProviderId_; });
    if (providerIterator == providers_.end()) {
        return;
    }

    const auto* capability = findCapabilityForProvider(providerCapabilities_, *providerIterator);
    if (capability == nullptr) {
        ProviderCredentialStatusText().Text(L"No provider module has published credential requirements for this route.");
        setField(ProviderCredentialFieldOnePanel(), ProviderCredentialFieldOneLabel(), ProviderCredentialFieldOneValueBox(), ProviderCredentialFieldOneHint(), std::nullopt, L"Credential Field");
        setField(ProviderCredentialFieldTwoPanel(), ProviderCredentialFieldTwoLabel(), ProviderCredentialFieldTwoValueBox(), ProviderCredentialFieldTwoHint(), std::nullopt, L"Credential Field");
        return;
    }

    const auto* status = findCredentialStatus(providerCredentialStatuses_, selectedProviderId_);
    ProviderCredentialStatusText().Text(status == nullptr
            ? L"No secure credentials are saved yet."
            : winrt::hstring(status->message.empty() ? L"Credential status is available." : status->message));

    std::optional<::MasterControlShell::ShellProviderCredentialField> fieldOne;
    std::optional<::MasterControlShell::ShellProviderCredentialField> fieldTwo;
    if (!capability->credentialFields.empty()) {
        fieldOne = capability->credentialFields[0];
    }
    if (capability->credentialFields.size() > 1U) {
        fieldTwo = capability->credentialFields[1];
    }

    setField(ProviderCredentialFieldOnePanel(), ProviderCredentialFieldOneLabel(), ProviderCredentialFieldOneValueBox(), ProviderCredentialFieldOneHint(), fieldOne, L"Credential Field");
    setField(ProviderCredentialFieldTwoPanel(), ProviderCredentialFieldTwoLabel(), ProviderCredentialFieldTwoValueBox(), ProviderCredentialFieldTwoHint(), fieldTwo, L"Credential Field");
}

void ProvidersSectionControl::ApplyQuickConnectFields() {
    auto setField = [](StackPanel const& panel,
                       TextBlock const& label,
                       PasswordBox const& box,
                       TextBlock const& hint,
                       const std::optional<::MasterControlShell::ShellProviderCredentialField>& field,
                       const std::wstring& fallbackLabel) {
        if (!field.has_value()) {
            panel.Visibility(Visibility::Collapsed);
            label.Text(fallbackLabel);
            box.Password(L"");
            hint.Text(L"");
            return;
        }

        panel.Visibility(Visibility::Visible);
        label.Text(winrt::hstring(field->label));
        box.PlaceholderText(winrt::hstring(field->placeholder.empty() ? L"Credential value" : field->placeholder));
        std::wstring hintText = field->helpText;
        if (!field->environmentVariableHint.empty()) {
            if (!hintText.empty()) {
                hintText += L" ";
            }
            hintText += L"Env: ";
            hintText += field->environmentVariableHint;
        }
        hint.Text(winrt::hstring(hintText));
    };

    if (selectedQuickConnectProviderId_.empty()) {
        QuickConnectSummaryText().Text(L"Select a provider to load its recommended endpoint, model, and supported orchestration roles.");
        setField(
            QuickConnectCredentialFieldOnePanel(),
            QuickConnectCredentialFieldOneLabel(),
            QuickConnectCredentialFieldOneValueBox(),
            QuickConnectCredentialFieldOneHint(),
            std::nullopt,
            L"Credential");
        setField(
            QuickConnectCredentialFieldTwoPanel(),
            QuickConnectCredentialFieldTwoLabel(),
            QuickConnectCredentialFieldTwoValueBox(),
            QuickConnectCredentialFieldTwoHint(),
            std::nullopt,
            L"Credential");
        return;
    }

    const auto* capability = findCapabilityByProviderId(providerCapabilities_, selectedQuickConnectProviderId_);
    if (capability == nullptr) {
        QuickConnectSummaryText().Text(L"The selected provider does not have a published connection profile.");
        setField(
            QuickConnectCredentialFieldOnePanel(),
            QuickConnectCredentialFieldOneLabel(),
            QuickConnectCredentialFieldOneValueBox(),
            QuickConnectCredentialFieldOneHint(),
            std::nullopt,
            L"Credential");
        setField(
            QuickConnectCredentialFieldTwoPanel(),
            QuickConnectCredentialFieldTwoLabel(),
            QuickConnectCredentialFieldTwoValueBox(),
            QuickConnectCredentialFieldTwoHint(),
            std::nullopt,
            L"Credential");
        return;
    }

    std::wstring summary = capability->displayName.empty() ? capability->providerId : capability->displayName;
    if (!capability->defaultBaseUrl.empty()) {
        summary += L"  |  endpoint: ";
        summary += capability->defaultBaseUrl;
    }
    if (!capability->recommendedModel.empty()) {
        summary += L"  |  model: ";
        summary += capability->recommendedModel;
    }
    if (!capability->supportedTargets.empty()) {
        summary += L"  |  roles: ";
        summary += joinValues(capability->supportedTargets);
    }

    if (const auto* status = findCredentialStatus(providerCredentialStatuses_, capability->providerId)) {
        if (!status->message.empty()) {
            summary += L"\n";
            summary += status->message;
        }
    }

    QuickConnectSummaryText().Text(winrt::hstring(summary));

    std::optional<::MasterControlShell::ShellProviderCredentialField> fieldOne;
    std::optional<::MasterControlShell::ShellProviderCredentialField> fieldTwo;
    if (!capability->credentialFields.empty()) {
        fieldOne = capability->credentialFields[0];
    }
    if (capability->credentialFields.size() > 1U) {
        fieldTwo = capability->credentialFields[1];
    }

    setField(
        QuickConnectCredentialFieldOnePanel(),
        QuickConnectCredentialFieldOneLabel(),
        QuickConnectCredentialFieldOneValueBox(),
        QuickConnectCredentialFieldOneHint(),
        fieldOne,
        L"Credential");
    setField(
        QuickConnectCredentialFieldTwoPanel(),
        QuickConnectCredentialFieldTwoLabel(),
        QuickConnectCredentialFieldTwoValueBox(),
        QuickConnectCredentialFieldTwoHint(),
        fieldTwo,
        L"Credential");
}

void ProvidersSectionControl::UpdateEditorState() {
    const bool hasRuntime = runtime_ != nullptr;
    ConnectQuickProviderButton().IsEnabled(hasRuntime && !selectedQuickConnectProviderId_.empty());
    SaveProviderButton().IsEnabled(hasRuntime && providerDirty_);
    SaveProviderCredentialsButton().IsEnabled(hasRuntime && !selectedProviderId_.empty() && providerCredentialsDirty_);
    SaveSubAgentGroupButton().IsEnabled(hasRuntime && subAgentGroupDirty_);
    RemoveSubAgentGroupButton().IsEnabled(hasRuntime && !selectedSubAgentGroupId_.empty());
    SaveProviderAssignmentButton().IsEnabled(
        hasRuntime &&
        selectedAssignmentTargetIndex_ >= 0 &&
        selectedAssignmentTargetIndex_ < static_cast<int>(providerAssignmentTargets_.size()) &&
        providerAssignmentDirty_);
    ExecuteProviderTaskButton().IsEnabled(
        hasRuntime &&
        selectedExecutionTargetIndex_ >= 0 &&
        selectedExecutionTargetIndex_ < static_cast<int>(providerAssignmentTargets_.size()) &&
        !std::wstring(ProviderExecutionPromptTextBox().Text().c_str()).empty());
    SaveAiAutonomyButton().IsEnabled(hasRuntime && autonomyDirty_);
}

void ProvidersSectionControl::SetStatus(winrt::hstring const& message) {
    ProviderEditorStatusText().Text(message);
}

std::optional<::MasterControlShell::ShellProviderConnection> ProvidersSectionControl::BuildProviderFromEditor() {
    const auto id = std::wstring(ProviderIdTextBox().Text().c_str());
    const auto displayName = std::wstring(ProviderDisplayNameTextBox().Text().c_str());
    const auto baseUrl = std::wstring(ProviderBaseUrlTextBox().Text().c_str());
    const auto modelId = std::wstring(ProviderModelIdTextBox().Text().c_str());
    if (id.empty() || displayName.empty() || baseUrl.empty()) {
        return std::nullopt;
    }

    std::wstring kind = L"generic";
    if (const auto selectedItem = ProviderKindComboBox().SelectedItem().try_as<ComboBoxItem>()) {
        kind = std::wstring(unbox_value_or<winrt::hstring>(selectedItem.Tag(), winrt::hstring(L"generic")).c_str());
    }

    return ::MasterControlShell::ShellProviderConnection{
        id,
        kind,
        displayName,
        baseUrl,
        modelId,
        ProviderEnabledToggle().IsOn(),
        ProviderAutonomousToggle().IsOn(),
        false
    };
}

std::optional<::MasterControlShell::ShellSubAgentGroupDefinition> ProvidersSectionControl::BuildSubAgentGroupFromEditor() {
    const auto groupId = std::wstring(SubAgentGroupIdTextBox().Text().c_str());
    const auto displayName = std::wstring(SubAgentGroupDisplayNameTextBox().Text().c_str());
    const auto description = std::wstring(SubAgentGroupDescriptionTextBox().Text().c_str());
    if (groupId.empty() || displayName.empty()) {
        return std::nullopt;
    }

    std::vector<std::wstring> memberTargetIds;
    for (uint32_t itemIndex = 0; itemIndex < SubAgentGroupMembersListView().SelectedItems().Size(); ++itemIndex) {
        if (const auto item = SubAgentGroupMembersListView().SelectedItems().GetAt(itemIndex).try_as<ListViewItem>()) {
            const auto targetId = std::wstring(unbox_value_or<winrt::hstring>(item.Tag(), winrt::hstring(L"")).c_str());
            if (!targetId.empty()) {
                memberTargetIds.push_back(targetId);
            }
        }
    }

    if (memberTargetIds.empty()) {
        return std::nullopt;
    }

    return ::MasterControlShell::ShellSubAgentGroupDefinition{
        groupId,
        displayName,
        description,
        memberTargetIds,
        L""
    };
}

winrt::Windows::Foundation::IAsyncAction ProvidersSectionControl::ConnectQuickProviderAsync() {
    if (runtime_ == nullptr) {
        QuickConnectStatusText().Text(L"Quick connect is unavailable until the shell runtime is attached.");
        co_return;
    }

    if (selectedQuickConnectProviderId_.empty()) {
        QuickConnectStatusText().Text(L"Choose a provider before trying to connect it.");
        co_return;
    }

    const auto* capability = findCapabilityByProviderId(providerCapabilities_, selectedQuickConnectProviderId_);
    if (capability == nullptr) {
        QuickConnectStatusText().Text(L"The selected provider does not have a published connection profile.");
        co_return;
    }

    ::MasterControlShell::ShellProviderConnection provider{};
    if (const auto iterator = std::find_if(
            providers_.begin(),
            providers_.end(),
            [this](const auto& connection) { return connection.id == selectedQuickConnectProviderId_; });
        iterator != providers_.end()) {
        provider = *iterator;
    }

    provider.id = capability->providerId;
    provider.kind = capability->kind.empty() ? L"generic" : capability->kind;
    provider.displayName = capability->displayName.empty() ? capability->providerId : capability->displayName;
    if (provider.baseUrl.empty()) {
        provider.baseUrl = capability->defaultBaseUrl;
    }
    if (provider.modelId.empty()) {
        provider.modelId = capability->recommendedModel;
    }
    provider.enabled = true;
    provider.allowAutonomousControl = false;

    if (provider.baseUrl.empty()) {
        QuickConnectStatusText().Text(L"This provider does not publish a default endpoint yet, so it still needs the advanced editor.");
        co_return;
    }

    std::vector<std::pair<std::wstring, std::wstring>> credentialValues;
    const auto* existingStatus = findCredentialStatus(providerCredentialStatuses_, provider.id);
    for (size_t fieldIndex = 0; fieldIndex < capability->credentialFields.size(); ++fieldIndex) {
        const auto& field = capability->credentialFields[fieldIndex];
        const std::wstring value =
            fieldIndex == 0
                ? std::wstring(QuickConnectCredentialFieldOneValueBox().Password().c_str())
                : std::wstring(QuickConnectCredentialFieldTwoValueBox().Password().c_str());
        const bool alreadyConfigured =
            existingStatus != nullptr &&
            std::find(
                existingStatus->configuredFieldIds.begin(),
                existingStatus->configuredFieldIds.end(),
                field.fieldId) != existingStatus->configuredFieldIds.end();

        if (field.required && value.empty() && !alreadyConfigured) {
            std::wstring message = L"Enter the required provider credential before connecting.";
            if (!field.label.empty()) {
                message = L"Enter ";
                message += field.label;
                message += L" before connecting.";
            }
            QuickConnectStatusText().Text(winrt::hstring(message));
            co_return;
        }

        if (!value.empty()) {
            credentialValues.emplace_back(field.fieldId, value);
        }
    }

    std::wstring assignmentTargetId = selectedQuickConnectResponsibilityTargetId_;
    std::wstring assignmentTargetKind = L"role";
    if (!assignmentTargetId.empty()) {
        const auto targetIterator = std::find_if(
            providerAssignmentTargets_.begin(),
            providerAssignmentTargets_.end(),
            [&assignmentTargetId](const auto& target) { return target.targetId == assignmentTargetId; });
        if (targetIterator == providerAssignmentTargets_.end()) {
            QuickConnectStatusText().Text(L"The selected orchestration responsibility is no longer available.");
            co_return;
        }
        assignmentTargetKind = targetIterator->kind;
    }

    ConnectQuickProviderButton().IsEnabled(false);
    QuickConnectStatusText().Text(L"Connecting provider through the local admin API...");
    winrt::apartment_context uiThread;
    const auto providerValue = provider;
    const auto credentialValueCopy = credentialValues;
    const auto assignmentTargetIdCopy = assignmentTargetId;
    const auto assignmentTargetKindCopy = assignmentTargetKind;
    co_await winrt::resume_background();

    const auto providerResult = runtime_->UpsertProvider(providerValue);
    ::MasterControlShell::ShellOperationResult credentialResult{ true, false, L"" };
    ::MasterControlShell::ShellOperationResult assignmentResult{ true, false, L"" };

    if (providerResult.succeeded && !credentialValueCopy.empty()) {
        credentialResult = runtime_->UpsertProviderCredentials(providerValue.id, credentialValueCopy);
    }

    if (providerResult.succeeded && credentialResult.succeeded && !assignmentTargetIdCopy.empty()) {
        assignmentResult = runtime_->UpsertProviderAssignment(::MasterControlShell::ShellProviderAssignment{
            assignmentTargetIdCopy,
            assignmentTargetKindCopy,
            providerValue.id,
            L""
        });
    }

    co_await uiThread;

    if (!providerResult.succeeded) {
        QuickConnectStatusText().Text(winrt::hstring(providerResult.message));
        UpdateEditorState();
        co_return;
    }
    if (!credentialResult.succeeded) {
        QuickConnectStatusText().Text(winrt::hstring(credentialResult.message));
        UpdateEditorState();
        co_return;
    }
    if (!assignmentResult.succeeded) {
        QuickConnectStatusText().Text(winrt::hstring(assignmentResult.message));
        UpdateEditorState();
        co_return;
    }

    selectedProviderId_ = providerValue.id;
    selectedQuickConnectProviderId_ = providerValue.id;
    ProviderCredentialFieldOneValueBox().Password(L"");
    ProviderCredentialFieldTwoValueBox().Password(L"");
    QuickConnectCredentialFieldOneValueBox().Password(L"");
    QuickConnectCredentialFieldTwoValueBox().Password(L"");

    std::wstring message = L"Connected ";
    message += providerValue.displayName.empty() ? providerValue.id : providerValue.displayName;
    if (!assignmentTargetIdCopy.empty()) {
        const auto targetIterator = std::find_if(
            providerAssignmentTargets_.begin(),
            providerAssignmentTargets_.end(),
            [&assignmentTargetIdCopy](const auto& target) { return target.targetId == assignmentTargetIdCopy; });
        if (targetIterator != providerAssignmentTargets_.end()) {
            message += L" and assigned ";
            message += targetIterator->displayName.empty() ? targetIterator->targetId : targetIterator->displayName;
            message += L".";
        } else {
            message += L".";
        }
    } else {
        message += L".";
    }

    QuickConnectStatusText().Text(winrt::hstring(message));
    SetStatus(winrt::hstring(message));
    if (refreshRequested_) {
        refreshRequested_();
    }
    UpdateEditorState();
}

winrt::Windows::Foundation::IAsyncAction ProvidersSectionControl::SaveProviderAsync() {
    if (runtime_ == nullptr) {
        SetStatus(L"Provider editing is unavailable until the shell runtime is attached.");
        co_return;
    }

    const auto provider = BuildProviderFromEditor();
    if (!provider.has_value()) {
        SetStatus(L"Provider ID, display name, and base URL are all required.");
        co_return;
    }

    SaveProviderButton().IsEnabled(false);
    winrt::apartment_context uiThread;
    const auto providerValue = *provider;
    co_await winrt::resume_background();
    const auto result = runtime_->UpsertProvider(providerValue);
    co_await uiThread;

    SetStatus(winrt::hstring(result.message));
    if (result.succeeded) {
        providerDirty_ = false;
        selectedProviderId_ = providerValue.id;
        if (refreshRequested_) {
            refreshRequested_();
        }
    }
    UpdateEditorState();
}

winrt::Windows::Foundation::IAsyncAction ProvidersSectionControl::SaveProviderCredentialsAsync() {
    if (runtime_ == nullptr) {
        ProviderCredentialStatusText().Text(L"Credential editing is unavailable until the shell runtime is attached.");
        co_return;
    }

    const auto providerIterator = std::find_if(
        providers_.begin(),
        providers_.end(),
        [this](const auto& provider) { return provider.id == selectedProviderId_; });
    if (providerIterator == providers_.end()) {
        ProviderCredentialStatusText().Text(L"Select a saved provider route before saving credentials.");
        co_return;
    }

    const auto* capability = findCapabilityForProvider(providerCapabilities_, *providerIterator);
    if (capability == nullptr) {
        ProviderCredentialStatusText().Text(L"This provider route does not have an active provider module descriptor.");
        co_return;
    }

    std::vector<std::pair<std::wstring, std::wstring>> values;
    if (!capability->credentialFields.empty()) {
        values.emplace_back(capability->credentialFields[0].fieldId, std::wstring(ProviderCredentialFieldOneValueBox().Password().c_str()));
    }
    if (capability->credentialFields.size() > 1U) {
        values.emplace_back(capability->credentialFields[1].fieldId, std::wstring(ProviderCredentialFieldTwoValueBox().Password().c_str()));
    }

    SaveProviderCredentialsButton().IsEnabled(false);
    winrt::apartment_context uiThread;
    const auto providerId = selectedProviderId_;
    co_await winrt::resume_background();
    const auto result = runtime_->UpsertProviderCredentials(providerId, values);
    co_await uiThread;

    ProviderCredentialStatusText().Text(winrt::hstring(result.message));
    if (result.succeeded) {
        providerCredentialsDirty_ = false;
        ProviderCredentialFieldOneValueBox().Password(L"");
        ProviderCredentialFieldTwoValueBox().Password(L"");
        if (refreshRequested_) {
            refreshRequested_();
        }
    }
    UpdateEditorState();
}

winrt::Windows::Foundation::IAsyncAction ProvidersSectionControl::SaveSubAgentGroupAsync() {
    if (runtime_ == nullptr) {
        SubAgentGroupStatusText().Text(L"Sub-agent group editing is unavailable until the shell runtime is attached.");
        co_return;
    }

    const auto group = BuildSubAgentGroupFromEditor();
    if (!group.has_value()) {
        SubAgentGroupStatusText().Text(L"Group ID, display name, and at least one member are required.");
        co_return;
    }

    SaveSubAgentGroupButton().IsEnabled(false);
    winrt::apartment_context uiThread;
    const auto groupValue = *group;
    co_await winrt::resume_background();
    const auto result = runtime_->UpsertSubAgentGroup(groupValue);
    co_await uiThread;

    SubAgentGroupStatusText().Text(winrt::hstring(result.message));
    if (result.succeeded) {
        subAgentGroupDirty_ = false;
        selectedSubAgentGroupId_ = groupValue.groupId;
        if (refreshRequested_) {
            refreshRequested_();
        }
    }
    UpdateEditorState();
}

winrt::Windows::Foundation::IAsyncAction ProvidersSectionControl::RemoveSubAgentGroupAsync() {
    if (runtime_ == nullptr) {
        SubAgentGroupStatusText().Text(L"Sub-agent group removal is unavailable until the shell runtime is attached.");
        co_return;
    }
    if (selectedSubAgentGroupId_.empty()) {
        SubAgentGroupStatusText().Text(L"Select a saved sub-agent group before removing it.");
        co_return;
    }

    RemoveSubAgentGroupButton().IsEnabled(false);
    winrt::apartment_context uiThread;
    const auto groupId = selectedSubAgentGroupId_;
    co_await winrt::resume_background();
    const auto result = runtime_->RemoveSubAgentGroup(groupId);
    co_await uiThread;

    SubAgentGroupStatusText().Text(winrt::hstring(result.message));
    if (result.succeeded) {
        subAgentGroupDirty_ = false;
        selectedSubAgentGroupId_.clear();
        selectedSubAgentGroupIndex_ = -1;
        ClearSubAgentGroupEditor();
        if (refreshRequested_) {
            refreshRequested_();
        }
    }
    UpdateEditorState();
}

winrt::Windows::Foundation::IAsyncAction ProvidersSectionControl::SaveProviderAssignmentAsync() {
    if (runtime_ == nullptr) {
        ProviderAssignmentStatusText().Text(L"Provider ownership editing is unavailable until the shell runtime is attached.");
        co_return;
    }
    if (selectedAssignmentTargetIndex_ < 0 ||
        selectedAssignmentTargetIndex_ >= static_cast<int>(providerAssignmentTargets_.size())) {
        ProviderAssignmentStatusText().Text(L"Select a role or sub-agent before saving ownership.");
        co_return;
    }

    const auto& target = providerAssignmentTargets_[static_cast<size_t>(selectedAssignmentTargetIndex_)];
    std::wstring providerId;
    const auto providerSelectionIndex = ProviderAssignmentProviderSelector().SelectedIndex();
    if (providerSelectionIndex > 0 && providerSelectionIndex - 1 < static_cast<int>(providers_.size())) {
        providerId = providers_[static_cast<size_t>(providerSelectionIndex - 1)].id;
    }

    SaveProviderAssignmentButton().IsEnabled(false);
    winrt::apartment_context uiThread;
    co_await winrt::resume_background();
    const auto result = runtime_->UpsertProviderAssignment(::MasterControlShell::ShellProviderAssignment{
        target.targetId,
        target.kind,
        providerId,
        L""
    });
    co_await uiThread;

    ProviderAssignmentStatusText().Text(winrt::hstring(result.message));
    if (result.succeeded) {
        providerAssignmentDirty_ = false;
        if (refreshRequested_) {
            refreshRequested_();
        }
    }
    UpdateEditorState();
}

winrt::Windows::Foundation::IAsyncAction ProvidersSectionControl::ExecuteProviderTaskAsync() {
    if (runtime_ == nullptr) {
        ProviderExecutionStatusText().Text(L"Provider execution is unavailable until the shell runtime is attached.");
        co_return;
    }
    if (selectedExecutionTargetIndex_ < 0 ||
        selectedExecutionTargetIndex_ >= static_cast<int>(providerAssignmentTargets_.size())) {
        ProviderExecutionStatusText().Text(L"Select a role or sub-agent before running a provider task.");
        co_return;
    }

    const auto prompt = std::wstring(ProviderExecutionPromptTextBox().Text().c_str());
    if (prompt.empty()) {
        ProviderExecutionStatusText().Text(L"Enter a prompt before running a provider task.");
        co_return;
    }

    int maxTurns = 4;
    try {
        maxTurns = (std::max)(1, std::stoi(std::wstring(ProviderExecutionMaxTurnsTextBox().Text().c_str())));
    } catch (...) {
        maxTurns = 4;
    }

    ExecuteProviderTaskButton().IsEnabled(false);
    ProviderExecutionStatusText().Text(L"Dispatching provider task through the local admin API...");
    winrt::apartment_context uiThread;
    const auto request = ::MasterControlShell::ShellProviderExecutionRequest{
        providerAssignmentTargets_[static_cast<size_t>(selectedExecutionTargetIndex_)].targetId,
        prompt,
        ProviderExecutionToolAccessToggle().IsOn(),
        maxTurns
    };
    co_await winrt::resume_background();
    const auto record = runtime_->ExecuteProviderTask(request);
    co_await uiThread;

    providerExecutionDirty_ = false;
    ProviderExecutionStatusText().Text(winrt::hstring(
        record.errorMessage.empty()
            ? (record.status == L"succeeded" ? L"Provider task completed successfully." : L"Provider task finished.")
            : record.errorMessage));
    ProviderExecutionOutputTextBox().Text(winrt::hstring(
        !record.outputText.empty() ? record.outputText : record.rawResponse));
    if (refreshRequested_) {
        refreshRequested_();
    } else {
        UpdateEditorState();
    }
}

winrt::Windows::Foundation::IAsyncAction ProvidersSectionControl::SaveAiAutonomyAsync() {
    if (runtime_ == nullptr) {
        SetStatus(L"AI autonomy editing is unavailable until the shell runtime is attached.");
        co_return;
    }

    SaveAiAutonomyButton().IsEnabled(false);
    winrt::apartment_context uiThread;
    const bool requestedValue = AiAutonomyToggle().IsOn();
    co_await winrt::resume_background();
    const auto result = runtime_->UpdateAiAutonomyEnabled(requestedValue);
    co_await uiThread;

    SetStatus(winrt::hstring(result.message));
    if (result.succeeded) {
        autonomyDirty_ = false;
        aiAutonomyEnabled_ = requestedValue;
        if (refreshRequested_) {
            refreshRequested_();
        }
    }
    UpdateEditorState();
}

} // namespace winrt::MasterControlShell::implementation
