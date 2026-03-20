// Master Control Program
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#include "pch.h"

#include "RuntimeSectionControl.xaml.h"

#if __has_include("RuntimeSectionControl.g.cpp")
#include "RuntimeSectionControl.g.cpp"
#endif

#include "ShellFormatting.h"

namespace winrt::MasterControlShell::implementation {

using namespace ::MasterControlShell::Presentation;

RuntimeSectionControl::RuntimeSectionControl() {
    InitializeComponent();
    UpdateEditorState();
}

void RuntimeSectionControl::AttachRuntime(::MasterControlShell::ShellRuntime* runtime,
                                          std::function<void()> refreshRequested) {
    runtime_ = runtime;
    refreshRequested_ = std::move(refreshRequested);
    UpdateEditorState();
}

void RuntimeSectionControl::ApplySnapshot(const ::MasterControlShell::ShellSnapshot& snapshot) {
    RuntimeCountText().Text(winrt::hstring(std::to_wstring(snapshot.endpointCount)));
    RuntimeNarrativeText().Text(winrt::hstring(formatRuntimeNarrative(snapshot)));
    populateListView(EndpointsListView(), snapshot.endpointRows);

    customSubAgents_.clear();
    for (const auto& endpoint : snapshot.endpoints) {
        if (endpoint.kind == L"sub_agent" && endpoint.userDefined) {
            customSubAgents_.push_back(endpoint);
        }
    }

    RefreshCustomSubAgentSelector();
    if (!selectedCustomSubAgentId_.empty()) {
        const auto iterator = std::find_if(
            customSubAgents_.begin(),
            customSubAgents_.end(),
            [this](const ::MasterControlShell::ShellRuntimeEndpoint& endpoint) {
                return endpoint.id == selectedCustomSubAgentId_;
            });
        if (iterator != customSubAgents_.end()) {
            const auto index = static_cast<size_t>(std::distance(customSubAgents_.begin(), iterator));
            PopulateCustomSubAgentEditor(index);
        } else {
            selectedCustomSubAgentId_.clear();
            selectedCustomSubAgentIndex_ = -1;
            ClearCustomSubAgentEditor();
        }
    } else if (!customSubAgents_.empty() && selectedCustomSubAgentIndex_ >= 0) {
        PopulateCustomSubAgentEditor(static_cast<size_t>(selectedCustomSubAgentIndex_));
    }

    UpdateEditorState();
}

void RuntimeSectionControl::CustomSubAgentSelector_SelectionChanged(
    Windows::Foundation::IInspectable const&,
    Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&) {
    const auto index = CustomSubAgentSelector().SelectedIndex();
    if (index <= 0) {
        selectedCustomSubAgentIndex_ = -1;
        selectedCustomSubAgentId_.clear();
        ClearCustomSubAgentEditor();
        return;
    }

    const auto resolvedIndex = static_cast<size_t>(index - 1);
    if (resolvedIndex >= customSubAgents_.size()) {
        return;
    }

    PopulateCustomSubAgentEditor(resolvedIndex);
}

void RuntimeSectionControl::CustomSubAgentEditor_TextChanged(
    Windows::Foundation::IInspectable const&,
    Microsoft::UI::Xaml::Controls::TextChangedEventArgs const&) {
    if (suspendDirtyTracking_) {
        return;
    }

    customSubAgentDirty_ = true;
    CustomSubAgentStatusText().Text(L"Custom sub-agent changes are ready to save.");
    UpdateEditorState();
}

void RuntimeSectionControl::SaveCustomSubAgentButton_Click(
    Windows::Foundation::IInspectable const&,
    Microsoft::UI::Xaml::RoutedEventArgs const&) {
    auto ignored = SaveCustomSubAgentAsync();
    (void)ignored;
}

void RuntimeSectionControl::NewCustomSubAgentButton_Click(
    Windows::Foundation::IInspectable const&,
    Microsoft::UI::Xaml::RoutedEventArgs const&) {
    selectedCustomSubAgentIndex_ = -1;
    selectedCustomSubAgentId_.clear();
    ClearCustomSubAgentEditor();
    CustomSubAgentStatusText().Text(L"Staging a new custom sub-agent lane.");
    UpdateEditorState();
}

void RuntimeSectionControl::RemoveCustomSubAgentButton_Click(
    Windows::Foundation::IInspectable const&,
    Microsoft::UI::Xaml::RoutedEventArgs const&) {
    auto ignored = RemoveCustomSubAgentAsync();
    (void)ignored;
}

void RuntimeSectionControl::PopulateCustomSubAgentEditor(const size_t index) {
    if (index >= customSubAgents_.size()) {
        return;
    }

    const auto& endpoint = customSubAgents_[index];
    suspendDirtyTracking_ = true;
    selectedCustomSubAgentIndex_ = static_cast<int>(index);
    selectedCustomSubAgentId_ = endpoint.id;
    CustomSubAgentIdTextBox().Text(winrt::hstring(endpoint.id));
    CustomSubAgentDisplayNameTextBox().Text(winrt::hstring(endpoint.displayName));
    CustomSubAgentSpecializationTextBox().Text(winrt::hstring(endpoint.specialization));
    CustomSubAgentHostTextBox().Text(winrt::hstring(endpoint.host));
    CustomSubAgentPortTextBox().Text(winrt::hstring(endpoint.port == 0 ? std::wstring{} : std::to_wstring(endpoint.port)));
    CustomSubAgentProtocolTextBox().Text(winrt::hstring(endpoint.protocol));
    CustomSubAgentRoutePathTextBox().Text(winrt::hstring(endpoint.routePath));
    CustomSubAgentDescriptionTextBox().Text(winrt::hstring(endpoint.description));
    suspendDirtyTracking_ = false;
    customSubAgentDirty_ = false;
    CustomSubAgentStatusText().Text(L"Custom sub-agent loaded from the runtime inventory.");
    UpdateEditorState();
}

void RuntimeSectionControl::ClearCustomSubAgentEditor() {
    suspendDirtyTracking_ = true;
    CustomSubAgentSelector().SelectedIndex(0);
    CustomSubAgentIdTextBox().Text(L"");
    CustomSubAgentDisplayNameTextBox().Text(L"");
    CustomSubAgentSpecializationTextBox().Text(L"");
    CustomSubAgentHostTextBox().Text(L"");
    CustomSubAgentPortTextBox().Text(L"");
    CustomSubAgentProtocolTextBox().Text(L"virtual");
    CustomSubAgentRoutePathTextBox().Text(L"");
    CustomSubAgentDescriptionTextBox().Text(L"");
    suspendDirtyTracking_ = false;
    customSubAgentDirty_ = false;
    UpdateEditorState();
}

void RuntimeSectionControl::RefreshCustomSubAgentSelector() {
    suspendDirtyTracking_ = true;
    CustomSubAgentSelector().Items().Clear();

    Microsoft::UI::Xaml::Controls::ComboBoxItem placeholder;
    placeholder.Content(box_value(L"New custom sub-agent"));
    CustomSubAgentSelector().Items().Append(placeholder);

    int selectedIndex = 0;
    int currentIndex = 1;
    for (const auto& endpoint : customSubAgents_) {
        Microsoft::UI::Xaml::Controls::ComboBoxItem item;
        const auto label = endpoint.specialization.empty()
            ? endpoint.displayName
            : (endpoint.displayName + L"  |  " + endpoint.specialization);
        item.Content(box_value(label));
        item.Tag(box_value(endpoint.id));
        CustomSubAgentSelector().Items().Append(item);
        if (!selectedCustomSubAgentId_.empty() && endpoint.id == selectedCustomSubAgentId_) {
            selectedIndex = currentIndex;
        }
        ++currentIndex;
    }

    CustomSubAgentSelector().SelectedIndex(selectedIndex);
    suspendDirtyTracking_ = false;
}

void RuntimeSectionControl::UpdateEditorState() {
    const auto hasSelection = !selectedCustomSubAgentId_.empty();
    SaveCustomSubAgentButton().IsEnabled(runtime_ != nullptr);
    RemoveCustomSubAgentButton().IsEnabled(runtime_ != nullptr && hasSelection);
}

std::optional<::MasterControlShell::ShellRuntimeEndpoint> RuntimeSectionControl::BuildCustomSubAgentFromEditor() {
    auto trim = [](std::wstring value) {
        const auto first = std::find_if_not(value.begin(), value.end(), [](wchar_t character) { return iswspace(character) != 0; });
        if (first == value.end()) {
            return std::wstring{};
        }
        const auto last = std::find_if_not(value.rbegin(), value.rend(), [](wchar_t character) { return iswspace(character) != 0; }).base();
        return std::wstring(first, last);
    };

    const auto id = trim(CustomSubAgentIdTextBox().Text().c_str());
    const auto displayName = trim(CustomSubAgentDisplayNameTextBox().Text().c_str());
    if (id.empty() || displayName.empty()) {
        return std::nullopt;
    }

    uint16_t port = 0;
    const auto portText = trim(CustomSubAgentPortTextBox().Text().c_str());
    if (!portText.empty()) {
        try {
            const auto parsedPort = std::stoi(portText);
            if (parsedPort < 0 || parsedPort > 65535) {
                return std::nullopt;
            }
            port = static_cast<uint16_t>(parsedPort);
        } catch (...) {
            return std::nullopt;
        }
    }

    return ::MasterControlShell::ShellRuntimeEndpoint{
        id,
        displayName,
        L"sub_agent",
        trim(CustomSubAgentHostTextBox().Text().c_str()),
        port,
        trim(CustomSubAgentProtocolTextBox().Text().c_str()),
        L"unknown",
        trim(CustomSubAgentDescriptionTextBox().Text().c_str()),
        trim(CustomSubAgentRoutePathTextBox().Text().c_str()),
        trim(CustomSubAgentSpecializationTextBox().Text().c_str()),
        true
    };
}

winrt::Windows::Foundation::IAsyncAction RuntimeSectionControl::SaveCustomSubAgentAsync() {
    if (runtime_ == nullptr) {
        CustomSubAgentStatusText().Text(L"Custom sub-agent editing is unavailable until the shell runtime is attached.");
        co_return;
    }

    const auto endpoint = BuildCustomSubAgentFromEditor();
    if (!endpoint.has_value()) {
        CustomSubAgentStatusText().Text(L"Sub-agent ID and display name are required, and the port must be blank or between 0 and 65535.");
        co_return;
    }

    SaveCustomSubAgentButton().IsEnabled(false);
    winrt::apartment_context uiThread;
    const auto endpointValue = *endpoint;
    co_await winrt::resume_background();
    const auto result = runtime_->UpsertSubAgent(endpointValue);
    co_await uiThread;

    CustomSubAgentStatusText().Text(winrt::hstring(result.message));
    if (result.succeeded) {
        customSubAgentDirty_ = false;
        selectedCustomSubAgentId_ = endpointValue.id;
        if (refreshRequested_) {
            refreshRequested_();
        }
    }
    UpdateEditorState();
}

winrt::Windows::Foundation::IAsyncAction RuntimeSectionControl::RemoveCustomSubAgentAsync() {
    if (runtime_ == nullptr) {
        CustomSubAgentStatusText().Text(L"Custom sub-agent editing is unavailable until the shell runtime is attached.");
        co_return;
    }
    if (selectedCustomSubAgentId_.empty()) {
        CustomSubAgentStatusText().Text(L"Select a custom sub-agent before removing it.");
        co_return;
    }

    RemoveCustomSubAgentButton().IsEnabled(false);
    winrt::apartment_context uiThread;
    const auto subAgentId = selectedCustomSubAgentId_;
    co_await winrt::resume_background();
    const auto result = runtime_->RemoveSubAgent(subAgentId);
    co_await uiThread;

    CustomSubAgentStatusText().Text(winrt::hstring(result.message));
    if (result.succeeded) {
        customSubAgentDirty_ = false;
        selectedCustomSubAgentId_.clear();
        selectedCustomSubAgentIndex_ = -1;
        ClearCustomSubAgentEditor();
        if (refreshRequested_) {
            refreshRequested_();
        }
    }
    UpdateEditorState();
}

} // namespace winrt::MasterControlShell::implementation
