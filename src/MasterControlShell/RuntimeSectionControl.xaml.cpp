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

namespace {

std::wstring trimEditorValue(const std::wstring& value) {
    const auto first = std::find_if_not(value.begin(), value.end(), [](wchar_t character) { return iswspace(character) != 0; });
    if (first == value.end()) {
        return {};
    }

    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](wchar_t character) { return iswspace(character) != 0; }).base();
    return std::wstring(first, last);
}

std::optional<uint16_t> parseEditorPort(const std::wstring& value, const bool allowZero) {
    const auto trimmed = trimEditorValue(value);
    if (trimmed.empty()) {
        return static_cast<uint16_t>(0);
    }

    try {
        const auto parsedPort = std::stoi(trimmed);
        if (parsedPort < 0 || parsedPort > 65535) {
            return std::nullopt;
        }
        if (!allowZero && parsedPort == 0) {
            return std::nullopt;
        }
        return static_cast<uint16_t>(parsedPort);
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace

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
    populateListView(PlatformGatewaysListView(), snapshot.platformGatewayRows);
    populateListView(AppleRemoteHostsListView(), snapshot.appleRemoteHostRows);

    customMcpServers_.clear();
    customSubAgents_.clear();
    for (const auto& endpoint : snapshot.endpoints) {
        if (!endpoint.userDefined) {
            continue;
        }

        if (endpoint.kind == L"mcp_server") {
            customMcpServers_.push_back(endpoint);
        } else if (endpoint.kind == L"sub_agent") {
            customSubAgents_.push_back(endpoint);
        }
    }

    RefreshCustomMcpServerSelector();
    if (!selectedCustomMcpServerId_.empty()) {
        const auto iterator = std::find_if(
            customMcpServers_.begin(),
            customMcpServers_.end(),
            [this](const ::MasterControlShell::ShellRuntimeEndpoint& endpoint) {
                return endpoint.id == selectedCustomMcpServerId_;
            });
        if (iterator != customMcpServers_.end()) {
            PopulateCustomMcpServerEditor(static_cast<size_t>(std::distance(customMcpServers_.begin(), iterator)));
        } else {
            selectedCustomMcpServerId_.clear();
            selectedCustomMcpServerIndex_ = -1;
            ClearCustomMcpServerEditor();
        }
    } else if (!customMcpServers_.empty() && selectedCustomMcpServerIndex_ >= 0) {
        PopulateCustomMcpServerEditor(static_cast<size_t>(selectedCustomMcpServerIndex_));
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
            PopulateCustomSubAgentEditor(static_cast<size_t>(std::distance(customSubAgents_.begin(), iterator)));
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

void RuntimeSectionControl::CustomMcpServerSelector_SelectionChanged(
    Windows::Foundation::IInspectable const&,
    Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&) {
    const auto index = CustomMcpServerSelector().SelectedIndex();
    if (index <= 0) {
        selectedCustomMcpServerIndex_ = -1;
        selectedCustomMcpServerId_.clear();
        ClearCustomMcpServerEditor();
        return;
    }

    const auto resolvedIndex = static_cast<size_t>(index - 1);
    if (resolvedIndex >= customMcpServers_.size()) {
        return;
    }

    PopulateCustomMcpServerEditor(resolvedIndex);
}

void RuntimeSectionControl::CustomMcpServerEditor_TextChanged(
    Windows::Foundation::IInspectable const&,
    Microsoft::UI::Xaml::Controls::TextChangedEventArgs const&) {
    if (suspendDirtyTracking_) {
        return;
    }

    customMcpServerDirty_ = true;
    CustomMcpServerStatusText().Text(L"Custom MCP server changes are ready to save.");
    UpdateEditorState();
}

void RuntimeSectionControl::SaveCustomMcpServerButton_Click(
    Windows::Foundation::IInspectable const&,
    Microsoft::UI::Xaml::RoutedEventArgs const&) {
    auto ignored = SaveCustomMcpServerAsync();
    (void)ignored;
}

void RuntimeSectionControl::NewCustomMcpServerButton_Click(
    Windows::Foundation::IInspectable const&,
    Microsoft::UI::Xaml::RoutedEventArgs const&) {
    selectedCustomMcpServerIndex_ = -1;
    selectedCustomMcpServerId_.clear();
    ClearCustomMcpServerEditor();
    CustomMcpServerStatusText().Text(L"Staging a new shared MCP server lane.");
    UpdateEditorState();
}

void RuntimeSectionControl::RemoveCustomMcpServerButton_Click(
    Windows::Foundation::IInspectable const&,
    Microsoft::UI::Xaml::RoutedEventArgs const&) {
    auto ignored = RemoveCustomMcpServerAsync();
    (void)ignored;
}

void RuntimeSectionControl::PopulateCustomMcpServerEditor(const size_t index) {
    if (index >= customMcpServers_.size()) {
        return;
    }

    const auto& endpoint = customMcpServers_[index];
    suspendDirtyTracking_ = true;
    selectedCustomMcpServerIndex_ = static_cast<int>(index);
    selectedCustomMcpServerId_ = endpoint.id;
    CustomMcpServerIdTextBox().Text(winrt::hstring(endpoint.id));
    CustomMcpServerDisplayNameTextBox().Text(winrt::hstring(endpoint.displayName));
    CustomMcpServerHostTextBox().Text(winrt::hstring(endpoint.host));
    CustomMcpServerPortTextBox().Text(winrt::hstring(endpoint.port == 0 ? std::wstring{} : std::to_wstring(endpoint.port)));
    CustomMcpServerProtocolTextBox().Text(winrt::hstring(endpoint.protocol));
    CustomMcpServerRoutePathTextBox().Text(winrt::hstring(endpoint.routePath));
    CustomMcpServerDescriptionTextBox().Text(winrt::hstring(endpoint.description));
    suspendDirtyTracking_ = false;
    customMcpServerDirty_ = false;
    CustomMcpServerStatusText().Text(L"Custom MCP server loaded from the runtime inventory.");
    UpdateEditorState();
}

void RuntimeSectionControl::ClearCustomMcpServerEditor() {
    suspendDirtyTracking_ = true;
    CustomMcpServerSelector().SelectedIndex(0);
    CustomMcpServerIdTextBox().Text(L"");
    CustomMcpServerDisplayNameTextBox().Text(L"");
    CustomMcpServerHostTextBox().Text(L"");
    CustomMcpServerPortTextBox().Text(L"");
    CustomMcpServerProtocolTextBox().Text(L"http");
    CustomMcpServerRoutePathTextBox().Text(L"/mcp");
    CustomMcpServerDescriptionTextBox().Text(L"");
    suspendDirtyTracking_ = false;
    customMcpServerDirty_ = false;
    UpdateEditorState();
}

void RuntimeSectionControl::RefreshCustomMcpServerSelector() {
    suspendDirtyTracking_ = true;
    CustomMcpServerSelector().Items().Clear();

    Microsoft::UI::Xaml::Controls::ComboBoxItem placeholder;
    placeholder.Content(box_value(L"New custom MCP server"));
    CustomMcpServerSelector().Items().Append(placeholder);

    int selectedIndex = 0;
    int currentIndex = 1;
    for (const auto& endpoint : customMcpServers_) {
        Microsoft::UI::Xaml::Controls::ComboBoxItem item;
        const auto label = endpoint.routePath.empty()
            ? endpoint.displayName
            : (endpoint.displayName + L"  |  " + endpoint.routePath);
        item.Content(box_value(label));
        item.Tag(box_value(endpoint.id));
        CustomMcpServerSelector().Items().Append(item);
        if (!selectedCustomMcpServerId_.empty() && endpoint.id == selectedCustomMcpServerId_) {
            selectedIndex = currentIndex;
        }
        ++currentIndex;
    }

    CustomMcpServerSelector().SelectedIndex(selectedIndex);
    suspendDirtyTracking_ = false;
}

std::optional<::MasterControlShell::ShellRuntimeEndpoint> RuntimeSectionControl::BuildCustomMcpServerFromEditor() {
    const auto id = trimEditorValue(CustomMcpServerIdTextBox().Text().c_str());
    const auto displayName = trimEditorValue(CustomMcpServerDisplayNameTextBox().Text().c_str());
    if (id.empty() || displayName.empty()) {
        return std::nullopt;
    }

    const auto port = parseEditorPort(CustomMcpServerPortTextBox().Text().c_str(), false);
    if (!port.has_value()) {
        return std::nullopt;
    }

    return ::MasterControlShell::ShellRuntimeEndpoint{
        id,
        displayName,
        L"mcp_server",
        trimEditorValue(CustomMcpServerHostTextBox().Text().c_str()),
        *port,
        trimEditorValue(CustomMcpServerProtocolTextBox().Text().c_str()),
        L"unknown",
        trimEditorValue(CustomMcpServerDescriptionTextBox().Text().c_str()),
        trimEditorValue(CustomMcpServerRoutePathTextBox().Text().c_str()),
        L"",
        true
    };
}

winrt::Windows::Foundation::IAsyncAction RuntimeSectionControl::SaveCustomMcpServerAsync() {
    if (runtime_ == nullptr) {
        CustomMcpServerStatusText().Text(L"Custom MCP server editing is unavailable until the shell runtime is attached.");
        co_return;
    }

    const auto endpoint = BuildCustomMcpServerFromEditor();
    if (!endpoint.has_value()) {
        CustomMcpServerStatusText().Text(L"MCP server ID, display name, and a port between 1 and 65535 are required.");
        co_return;
    }

    SaveCustomMcpServerButton().IsEnabled(false);
    winrt::apartment_context uiThread;
    const auto endpointValue = *endpoint;
    co_await winrt::resume_background();
    const auto result = runtime_->UpsertMcpServer(endpointValue);
    co_await uiThread;

    CustomMcpServerStatusText().Text(winrt::hstring(result.message));
    if (result.succeeded) {
        customMcpServerDirty_ = false;
        selectedCustomMcpServerId_ = endpointValue.id;
        if (refreshRequested_) {
            refreshRequested_();
        }
    }
    UpdateEditorState();
}

winrt::Windows::Foundation::IAsyncAction RuntimeSectionControl::RemoveCustomMcpServerAsync() {
    if (runtime_ == nullptr) {
        CustomMcpServerStatusText().Text(L"Custom MCP server editing is unavailable until the shell runtime is attached.");
        co_return;
    }
    if (selectedCustomMcpServerId_.empty()) {
        CustomMcpServerStatusText().Text(L"Select a custom MCP server before removing it.");
        co_return;
    }

    RemoveCustomMcpServerButton().IsEnabled(false);
    winrt::apartment_context uiThread;
    const auto mcpServerId = selectedCustomMcpServerId_;
    co_await winrt::resume_background();
    const auto result = runtime_->RemoveMcpServer(mcpServerId);
    co_await uiThread;

    CustomMcpServerStatusText().Text(winrt::hstring(result.message));
    if (result.succeeded) {
        customMcpServerDirty_ = false;
        selectedCustomMcpServerId_.clear();
        selectedCustomMcpServerIndex_ = -1;
        ClearCustomMcpServerEditor();
        if (refreshRequested_) {
            refreshRequested_();
        }
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
    const auto hasMcpServerSelection = !selectedCustomMcpServerId_.empty();
    SaveCustomMcpServerButton().IsEnabled(runtime_ != nullptr);
    RemoveCustomMcpServerButton().IsEnabled(runtime_ != nullptr && hasMcpServerSelection);

    const auto hasSubAgentSelection = !selectedCustomSubAgentId_.empty();
    SaveCustomSubAgentButton().IsEnabled(runtime_ != nullptr);
    RemoveCustomSubAgentButton().IsEnabled(runtime_ != nullptr && hasSubAgentSelection);
}

std::optional<::MasterControlShell::ShellRuntimeEndpoint> RuntimeSectionControl::BuildCustomSubAgentFromEditor() {
    const auto id = trimEditorValue(CustomSubAgentIdTextBox().Text().c_str());
    const auto displayName = trimEditorValue(CustomSubAgentDisplayNameTextBox().Text().c_str());
    if (id.empty() || displayName.empty()) {
        return std::nullopt;
    }

    const auto port = parseEditorPort(CustomSubAgentPortTextBox().Text().c_str(), true);
    if (!port.has_value()) {
        return std::nullopt;
    }

    return ::MasterControlShell::ShellRuntimeEndpoint{
        id,
        displayName,
        L"sub_agent",
        trimEditorValue(CustomSubAgentHostTextBox().Text().c_str()),
        *port,
        trimEditorValue(CustomSubAgentProtocolTextBox().Text().c_str()),
        L"unknown",
        trimEditorValue(CustomSubAgentDescriptionTextBox().Text().c_str()),
        trimEditorValue(CustomSubAgentRoutePathTextBox().Text().c_str()),
        trimEditorValue(CustomSubAgentSpecializationTextBox().Text().c_str()),
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
