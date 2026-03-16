// Master Control Program
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#include "pch.h"

#include "ProvidersSectionControl.xaml.h"

#if __has_include("ProvidersSectionControl.g.cpp")
#include "ProvidersSectionControl.g.cpp"
#endif

#include "ShellFormatting.h"

namespace winrt::MasterControlShell::implementation {

using namespace ::MasterControlShell::Presentation;
using namespace Microsoft::UI::Xaml::Controls;

ProvidersSectionControl::ProvidersSectionControl() {
    InitializeComponent();
}

void ProvidersSectionControl::AttachRuntime(::MasterControlShell::ShellRuntime* runtime,
                                            std::function<void()> refreshRequested) {
    runtime_ = runtime;
    refreshRequested_ = std::move(refreshRequested);
    UpdateEditorState();
}

void ProvidersSectionControl::ApplySnapshot(const ::MasterControlShell::ShellSnapshot& snapshot) {
    providers_ = snapshot.providers;
    aiAutonomyEnabled_ = snapshot.aiAutonomyEnabled;
    ProviderRouteCountText().Text(winrt::hstring(std::to_wstring(snapshot.providerCount)));
    ProviderAutonomyText().Text(snapshot.aiAutonomyEnabled ? L"Autonomy enabled" : L"Human-directed");
    ProvidersNarrativeText().Text(winrt::hstring(formatProvidersNarrative(snapshot)));
    populateListView(ProvidersListView(), snapshot.providerRows);

    RefreshProviderSelector();
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
    PopulateProviderEditor(static_cast<size_t>(index));
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

void ProvidersSectionControl::NewProviderButton_Click(
    Windows::Foundation::IInspectable const&,
    Microsoft::UI::Xaml::RoutedEventArgs const&) {
    selectedProviderIndex_ = -1;
    selectedProviderId_.clear();
    providerDirty_ = true;
    ClearProviderEditor();
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
    ProviderEnabledToggle().IsOn(provider.enabled);
    ProviderAutonomousToggle().IsOn(provider.allowAutonomousControl);

    int selectedKindIndex = 4;
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
    ProviderEnabledToggle().IsOn(true);
    ProviderAutonomousToggle().IsOn(false);
    ProviderKindComboBox().SelectedIndex(4);
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

void ProvidersSectionControl::UpdateEditorState() {
    const bool hasRuntime = runtime_ != nullptr;
    SaveProviderButton().IsEnabled(hasRuntime && providerDirty_);
    SaveAiAutonomyButton().IsEnabled(hasRuntime && autonomyDirty_);
}

void ProvidersSectionControl::SetStatus(winrt::hstring const& message) {
    ProviderEditorStatusText().Text(message);
}

std::optional<::MasterControlShell::ShellProviderConnection> ProvidersSectionControl::BuildProviderFromEditor() {
    const auto id = std::wstring(ProviderIdTextBox().Text().c_str());
    const auto displayName = std::wstring(ProviderDisplayNameTextBox().Text().c_str());
    const auto baseUrl = std::wstring(ProviderBaseUrlTextBox().Text().c_str());
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
        ProviderEnabledToggle().IsOn(),
        ProviderAutonomousToggle().IsOn()
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
