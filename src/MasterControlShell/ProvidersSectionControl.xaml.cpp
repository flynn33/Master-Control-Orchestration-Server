// Master Control Program
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

} // namespace

ProvidersSectionControl::ProvidersSectionControl() {
    InitializeComponent();
}

void ProvidersSectionControl::AttachRuntime(::MasterControlShell::ShellRuntime* runtime,
                                            std::function<void()> refreshRequested) {
    runtime_ = runtime;
    refreshRequested_ = std::move(refreshRequested);
    ApplyCredentialFields();
    RefreshAssignmentSelectors();
    UpdateEditorState();
}

void ProvidersSectionControl::ApplySnapshot(const ::MasterControlShell::ShellSnapshot& snapshot) {
    providers_ = snapshot.providers;
    providerCapabilities_ = snapshot.providerCapabilities;
    providerCredentialStatuses_ = snapshot.providerCredentialStatuses;
    providerAssignmentTargets_ = snapshot.providerAssignmentTargets;
    providerAssignments_ = snapshot.providerAssignments;
    aiAutonomyEnabled_ = snapshot.aiAutonomyEnabled;

    ProviderRouteCountText().Text(winrt::hstring(std::to_wstring(snapshot.providerCount)));
    ProviderAutonomyText().Text(snapshot.aiAutonomyEnabled ? L"Autonomy enabled" : L"Human-directed");
    ProvidersNarrativeText().Text(winrt::hstring(formatProvidersNarrative(snapshot)));
    populateListView(ProvidersListView(), snapshot.providerRows);
    populateListView(ProviderCapabilitiesListView(), snapshot.providerCapabilityRows);
    populateListView(ProviderAssignmentsListView(), snapshot.providerAssignmentRows);

    RefreshProviderSelector();
    RefreshAssignmentSelectors();

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

    ApplyCredentialFields();
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

    const auto& provider = providers_[index];
    ProviderSelector().SelectedIndex(selectedProviderIndex_);
    ProviderIdTextBox().Text(winrt::hstring(provider.id));
    ProviderDisplayNameTextBox().Text(winrt::hstring(provider.displayName));
    ProviderBaseUrlTextBox().Text(winrt::hstring(provider.baseUrl));
    ProviderModelIdTextBox().Text(winrt::hstring(provider.modelId));
    ProviderEnabledToggle().IsOn(provider.enabled);
    ProviderAutonomousToggle().IsOn(provider.allowAutonomousControl);

    int selectedKindIndex = 3;
    const auto desiredKind = provider.kind.empty() ? std::wstring(L"generic") : provider.kind;
    for (uint32_t itemIndex = 0; itemIndex < ProviderKindComboBox().Items().Size(); ++itemIndex) {
        if (const auto item = ProviderKindComboBox().Items().GetAt(itemIndex).try_as<ComboBoxItem>()) {
            if (std::wstring(unbox_value_or<winrt::hstring>(item.Tag(), winrt::hstring(L"generic")).c_str()) == desiredKind) {
                selectedKindIndex = static_cast<int>(itemIndex);
                break;
            }
        }
    }
    ProviderKindComboBox().SelectedIndex(selectedKindIndex);
    suspendDirtyTracking_ = false;
}

void ProvidersSectionControl::ClearProviderEditor() {
    suspendDirtyTracking_ = true;
    ProviderSelector().SelectedIndex(-1);
    ProviderIdTextBox().Text(L"");
    ProviderDisplayNameTextBox().Text(L"");
    ProviderBaseUrlTextBox().Text(L"");
    ProviderModelIdTextBox().Text(L"");
    ProviderEnabledToggle().IsOn(true);
    ProviderAutonomousToggle().IsOn(false);
    ProviderKindComboBox().SelectedIndex(3);
    ProviderCredentialFieldOneValueBox().Password(L"");
    ProviderCredentialFieldTwoValueBox().Password(L"");
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

void ProvidersSectionControl::UpdateEditorState() {
    const bool hasRuntime = runtime_ != nullptr;
    SaveProviderButton().IsEnabled(hasRuntime && providerDirty_);
    SaveProviderCredentialsButton().IsEnabled(hasRuntime && !selectedProviderId_.empty() && providerCredentialsDirty_);
    SaveProviderAssignmentButton().IsEnabled(
        hasRuntime &&
        selectedAssignmentTargetIndex_ >= 0 &&
        selectedAssignmentTargetIndex_ < static_cast<int>(providerAssignmentTargets_.size()) &&
        providerAssignmentDirty_);
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
