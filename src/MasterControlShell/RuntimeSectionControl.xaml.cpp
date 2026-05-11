// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#include "pch.h"

#include "RuntimeSectionControl.xaml.h"

#if __has_include("RuntimeSectionControl.g.cpp")
#include "RuntimeSectionControl.g.cpp"
#endif

#include "ShellFormatting.h"
// v0.9.76: card-grid renderer extracted to a shared header so the
// TelemetrySectionControl can reuse the exact same imperative builder
// without duplicating ~250 lines of WinRT code. Single source of truth.
#include "EndpointStatCardGrid.h"

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

std::wstring boolLabel(const bool value, const wchar_t* whenTrue = L"yes", const wchar_t* whenFalse = L"no") {
    return value ? whenTrue : whenFalse;
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

RuntimeSectionControl::RuntimeSectionControl() {
    InitializeComponent();
    UpdateEditorState();
}

void RuntimeSectionControl::AttachRuntime(::MasterControlShell::ShellRuntime* runtime,
                                          std::function<void()> refreshRequested,
                                          std::function<void(const std::wstring&)> actionRequested) {
    runtime_ = runtime;
    refreshRequested_ = std::move(refreshRequested);
    actionRequested_ = std::move(actionRequested);
    UpdateEditorState();
}

void RuntimeSectionControl::GuidedRuntimeActionButton_Click(
    Windows::Foundation::IInspectable const& sender,
    Microsoft::UI::Xaml::RoutedEventArgs const&) {
    const auto button = sender.try_as<Microsoft::UI::Xaml::Controls::Button>();
    if (button == nullptr) {
        return;
    }
    if (!actionRequested_) {
        RuntimeNarrativeText().Text(L"Guided runtime workflows are not attached to the shell yet.");
        return;
    }

    const auto workflowId = std::wstring(winrt::unbox_value_or<winrt::hstring>(button.Tag(), winrt::hstring()).c_str());
    if (workflowId.empty()) {
        RuntimeNarrativeText().Text(L"Runtime could not resolve the requested guided workflow.");
        return;
    }

    actionRequested_(workflowId);
}

void RuntimeSectionControl::ApplySnapshot(const ::MasterControlShell::ShellSnapshot& snapshot) {
    RuntimeCountText().Text(winrt::hstring(std::to_wstring(snapshot.endpointCount)));
    RuntimeNarrativeText().Text(winrt::hstring(formatRuntimeNarrative(snapshot)));
    // v0.10.11: EndpointsListView removed -- the dedicated MCP Servers
    // and Sub-Agents tile-grid panels (compact=true, same renderer as
    // Telemetry) now carry that data in the correct visual form. The
    // gateway + Apple-host lists remain because they are separate
    // entity types not represented in those tile grids.
    populateListView(PlatformGatewaysListView(), snapshot.platformGatewayRows);
    populateListView(AppleRemoteHostsListView(), snapshot.appleRemoteHostRows);

    // v0.7.6: rebuild the Sub-Agents card grid imperatively from the
    // live runtime stats. Each card carries: name + specialization,
    // utilization bar (always-visible 0-100), reachability dot,
    // endpoint host:port, pool/no-pool note, active-client list.
    PopulateSubAgentCards(snapshot);
    // v0.8.3: same render path, sourced from mcpServerRuntimeStats.
    PopulateMcpServerCards(snapshot);

    customMcpServers_.clear();
    customSubAgents_.clear();
    appleRemoteHosts_ = snapshot.appleRemoteHosts;
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

    RefreshAppleHostSelector();
    if (!selectedAppleHostId_.empty()) {
        const auto iterator = std::find_if(
            appleRemoteHosts_.begin(),
            appleRemoteHosts_.end(),
            [this](const ::MasterControlShell::ShellAppleRemoteHost& host) {
                return host.hostId == selectedAppleHostId_;
            });
        if (iterator != appleRemoteHosts_.end()) {
            PopulateAppleHostEditor(static_cast<size_t>(std::distance(appleRemoteHosts_.begin(), iterator)));
        } else {
            selectedAppleHostId_.clear();
            selectedAppleHostIndex_ = -1;
            ClearAppleHostEditor();
        }
    } else if (!appleRemoteHosts_.empty() && selectedAppleHostIndex_ >= 0) {
        PopulateAppleHostEditor(static_cast<size_t>(selectedAppleHostIndex_));
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

void RuntimeSectionControl::AppleHostSelector_SelectionChanged(
    Windows::Foundation::IInspectable const&,
    Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&) {
    if (suspendDirtyTracking_) {
        return;
    }

    const auto index = AppleHostSelector().SelectedIndex();
    if (index <= 0) {
        selectedAppleHostIndex_ = -1;
        selectedAppleHostId_.clear();
        ClearAppleHostEditor();
        return;
    }

    const auto resolvedIndex = static_cast<size_t>(index - 1);
    if (resolvedIndex >= appleRemoteHosts_.size()) {
        return;
    }

    PopulateAppleHostEditor(resolvedIndex);
}

void RuntimeSectionControl::AppleHostEditor_TextChanged(
    Windows::Foundation::IInspectable const&,
    Microsoft::UI::Xaml::Controls::TextChangedEventArgs const&) {
    if (suspendDirtyTracking_) {
        return;
    }

    appleHostDirty_ = true;
    AppleHostStatusText().Text(L"Apple remote host changes are ready to save.");
    UpdateEditorState();
}

void RuntimeSectionControl::AppleHostTransportComboBox_SelectionChanged(
    Windows::Foundation::IInspectable const&,
    Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&) {
    if (suspendDirtyTracking_) {
        return;
    }

    appleHostDirty_ = true;
    AppleHostStatusText().Text(L"Apple remote host changes are ready to save.");
    UpdateEditorState();
}

void RuntimeSectionControl::AppleHostEnabledCheckBox_Click(
    Windows::Foundation::IInspectable const&,
    Microsoft::UI::Xaml::RoutedEventArgs const&) {
    if (suspendDirtyTracking_) {
        return;
    }

    appleHostDirty_ = true;
    AppleHostStatusText().Text(L"Apple remote host changes are ready to save.");
    UpdateEditorState();
}

void RuntimeSectionControl::SaveAppleHostButton_Click(
    Windows::Foundation::IInspectable const&,
    Microsoft::UI::Xaml::RoutedEventArgs const&) {
    auto ignored = SaveAppleHostAsync();
    (void)ignored;
}

void RuntimeSectionControl::NewAppleHostButton_Click(
    Windows::Foundation::IInspectable const&,
    Microsoft::UI::Xaml::RoutedEventArgs const&) {
    selectedAppleHostIndex_ = -1;
    selectedAppleHostId_.clear();
    ClearAppleHostEditor();
    AppleHostStatusText().Text(L"Staging a new Apple remote host.");
    UpdateEditorState();
}

void RuntimeSectionControl::RemoveAppleHostButton_Click(
    Windows::Foundation::IInspectable const&,
    Microsoft::UI::Xaml::RoutedEventArgs const&) {
    auto ignored = RemoveAppleHostAsync();
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

void RuntimeSectionControl::PopulateAppleHostEditor(const size_t index) {
    if (index >= appleRemoteHosts_.size()) {
        return;
    }

    const auto& host = appleRemoteHosts_[index];
    suspendDirtyTracking_ = true;
    selectedAppleHostIndex_ = static_cast<int>(index);
    selectedAppleHostId_ = host.hostId;
    AppleHostIdTextBox().Text(winrt::hstring(host.hostId));
    AppleHostDisplayNameTextBox().Text(winrt::hstring(host.displayName));
    AppleHostAddressTextBox().Text(winrt::hstring(host.address));
    AppleHostPortTextBox().Text(winrt::hstring(host.port == 0 ? std::wstring{} : std::to_wstring(host.port)));
    AppleHostUsernameTextBox().Text(winrt::hstring(host.username));
    AppleHostServiceBaseUrlTextBox().Text(winrt::hstring(host.serviceBaseUrl));
    AppleHostHealthPathTextBox().Text(winrt::hstring(host.companionHealthPath.empty() ? L"/healthz" : host.companionHealthPath));
    AppleHostExecutePathTextBox().Text(winrt::hstring(host.companionExecutePath.empty() ? L"/execute" : host.companionExecutePath));
    AppleHostDeveloperDirectoryTextBox().Text(winrt::hstring(host.preferredDeveloperDirectory));
    AppleHostSigningIdentityTextBox().Text(winrt::hstring(host.defaultSigningIdentity));
    AppleHostNotaryProfileTextBox().Text(winrt::hstring(host.defaultNotaryKeychainProfile));
    AppleHostTeamIdTextBox().Text(winrt::hstring(host.defaultNotaryTeamId));
    AppleHostMacPlatformCheckBox().IsChecked(
        std::find(host.platforms.begin(), host.platforms.end(), L"macos") != host.platforms.end());
    AppleHostIosPlatformCheckBox().IsChecked(
        std::find(host.platforms.begin(), host.platforms.end(), L"ios") != host.platforms.end());
    AppleHostEnabledCheckBox().IsChecked(host.enabled);

    int transportIndex = 0;
    if (host.transport == L"ssh") {
        transportIndex = 1;
    }
    AppleHostTransportComboBox().SelectedIndex(transportIndex);
    suspendDirtyTracking_ = false;
    appleHostDirty_ = false;

    std::wstring status = L"Apple remote host loaded from the CLU registry.";
    if (!host.transportSummary.empty()) {
        status += L" " + host.transportSummary + L".";
    }
    if (!host.toolchainStatus.empty() || !host.signingStatus.empty()) {
        status += L" Toolchain ";
        status += host.toolchainStatus.empty() ? L"unknown" : host.toolchainStatus;
        status += L", signing ";
        status += host.signingStatus.empty() ? L"unknown" : host.signingStatus;
        status += L".";
    }
    if (!host.credentialProfileSummary.empty()) {
        status += L" " + host.credentialProfileSummary;
        if (!status.empty() && status.back() != L'.') {
            status += L".";
        }
    }
    if (!host.readinessIssues.empty()) {
        status += L" Readiness gaps: ";
        for (size_t issueIndex = 0; issueIndex < host.readinessIssues.size(); ++issueIndex) {
            if (issueIndex > 0) {
                status += L"; ";
            }
            status += host.readinessIssues[issueIndex];
        }
    }
    AppleHostStatusText().Text(winrt::hstring(status));

    std::wstring details = L"Route: ";
    details += host.transport.empty() ? L"unknown transport" : host.transport;
    details += L" via ";
    if (!host.serviceBaseUrl.empty()) {
        details += host.serviceBaseUrl;
    } else {
        details += host.address.empty() ? L"unconfigured host" : host.address;
        if (host.port > 0) {
            details += L":" + std::to_wstring(host.port);
        }
    }
    if (!host.username.empty()) {
        details += L" as " + host.username;
    }
    details += L".\n";

    details += L"Toolchain: reachable=" + boolLabel(host.reachable);
    details += L", Xcode installed=" + boolLabel(host.xcodeInstalled);
    if (!host.xcodeVersion.empty()) {
        details += L", version=" + host.xcodeVersion;
    }
    if (!host.developerDirectory.empty()) {
        details += L", developer dir=" + host.developerDirectory;
    }
    details += L".\n";

    details += L"SDK lanes: macOS=" + boolLabel(host.macosSdkAvailable);
    details += L", iOS=" + boolLabel(host.iosSdkAvailable);
    details += L", simulator control=" + boolLabel(host.simulatorControlAvailable);
    details += L", device control=" + boolLabel(host.deviceControlAvailable);
    details += L".\n";

    if (!host.simulatorRuntimes.empty()) {
        details += L"Simulator runtimes: " + joinValues(host.simulatorRuntimes) + L".\n";
    } else {
        details += L"Simulator runtimes: none published.\n";
    }

    details += L"Signing: ready=" + boolLabel(host.signingReady);
    details += L", development=" + boolLabel(host.developmentSigningReady);
    details += L", distribution=" + boolLabel(host.distributionSigningReady);
    details += L".\n";

    if (!host.availableTeams.empty()) {
        details += L"Apple teams: " + joinValues(host.availableTeams) + L".\n";
    }
    if (!host.defaultSigningIdentity.empty() || !host.defaultNotaryKeychainProfile.empty() || !host.defaultNotaryTeamId.empty()) {
        details += L"Defaults: ";
        if (!host.defaultSigningIdentity.empty()) {
            details += L"signing identity=" + host.defaultSigningIdentity + L"; ";
        }
        if (!host.defaultNotaryKeychainProfile.empty()) {
            details += L"notary profile=" + host.defaultNotaryKeychainProfile + L"; ";
        }
        if (!host.defaultNotaryTeamId.empty()) {
            details += L"team ID=" + host.defaultNotaryTeamId + L"; ";
        }
        if (details.size() >= 2 && details.substr(details.size() - 2) == L"; ") {
            details.erase(details.size() - 2);
        }
        details += L".\n";
    }
    if (!host.toolchainMessage.empty()) {
        details += L"Toolchain note: " + host.toolchainMessage + L".\n";
    }
    if (!host.signingMessage.empty()) {
        details += L"Signing note: " + host.signingMessage + L".\n";
    }
    if (!host.toolchainCheckedAtUtc.empty()) {
        details += L"Last checked: " + host.toolchainCheckedAtUtc + L".";
    }
    AppleHostReadinessDetailsText().Text(winrt::hstring(details));
    UpdateEditorState();
}

void RuntimeSectionControl::ClearAppleHostEditor() {
    suspendDirtyTracking_ = true;
    AppleHostSelector().SelectedIndex(0);
    AppleHostIdTextBox().Text(L"");
    AppleHostDisplayNameTextBox().Text(L"");
    AppleHostTransportComboBox().SelectedIndex(0);
    AppleHostAddressTextBox().Text(L"");
    AppleHostPortTextBox().Text(L"");
    AppleHostUsernameTextBox().Text(L"");
    AppleHostServiceBaseUrlTextBox().Text(L"");
    AppleHostHealthPathTextBox().Text(L"/healthz");
    AppleHostExecutePathTextBox().Text(L"/execute");
    AppleHostDeveloperDirectoryTextBox().Text(L"");
    AppleHostSigningIdentityTextBox().Text(L"");
    AppleHostNotaryProfileTextBox().Text(L"");
    AppleHostTeamIdTextBox().Text(L"");
    AppleHostMacPlatformCheckBox().IsChecked(true);
    AppleHostIosPlatformCheckBox().IsChecked(true);
    AppleHostEnabledCheckBox().IsChecked(true);
    suspendDirtyTracking_ = false;
    appleHostDirty_ = false;
    AppleHostReadinessDetailsText().Text(L"Apple host readiness details will appear here.");
    UpdateEditorState();
}

void RuntimeSectionControl::RefreshAppleHostSelector() {
    suspendDirtyTracking_ = true;
    AppleHostSelector().Items().Clear();

    Microsoft::UI::Xaml::Controls::ComboBoxItem placeholder;
    placeholder.Content(box_value(L"New Apple remote host"));
    AppleHostSelector().Items().Append(placeholder);

    int selectedIndex = 0;
    int currentIndex = 1;
    for (const auto& host : appleRemoteHosts_) {
        Microsoft::UI::Xaml::Controls::ComboBoxItem item;
        std::wstring label = host.displayName.empty() ? host.hostId : host.displayName;
        if (!host.transport.empty()) {
            label += L"  |  " + host.transport;
        }
        item.Content(box_value(label));
        item.Tag(box_value(host.hostId));
        AppleHostSelector().Items().Append(item);
        if (!selectedAppleHostId_.empty() && host.hostId == selectedAppleHostId_) {
            selectedIndex = currentIndex;
        }
        ++currentIndex;
    }

    AppleHostSelector().SelectedIndex(selectedIndex);
    suspendDirtyTracking_ = false;
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

    const auto hasAppleHostSelection = !selectedAppleHostId_.empty();
    SaveAppleHostButton().IsEnabled(runtime_ != nullptr);
    RemoveAppleHostButton().IsEnabled(runtime_ != nullptr && hasAppleHostSelection);
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

std::optional<::MasterControlShell::ShellAppleRemoteHost> RuntimeSectionControl::BuildAppleHostFromEditor() {
    const auto hostId = trimEditorValue(AppleHostIdTextBox().Text().c_str());
    const auto displayName = trimEditorValue(AppleHostDisplayNameTextBox().Text().c_str());
    if (hostId.empty() || displayName.empty()) {
        return std::nullopt;
    }

    const auto transportIndex = AppleHostTransportComboBox().SelectedIndex();
    const std::wstring transport = transportIndex == 1 ? L"ssh" : L"companion_service";
    const auto port = parseEditorPort(AppleHostPortTextBox().Text().c_str(), true);
    if (!port.has_value()) {
        return std::nullopt;
    }

    std::vector<std::wstring> platforms;
    const auto macPlatformChecked = AppleHostMacPlatformCheckBox().IsChecked();
    if (macPlatformChecked && macPlatformChecked.Value()) {
        platforms.push_back(L"macos");
    }
    const auto iosPlatformChecked = AppleHostIosPlatformCheckBox().IsChecked();
    if (iosPlatformChecked && iosPlatformChecked.Value()) {
        platforms.push_back(L"ios");
    }
    if (platforms.empty()) {
        return std::nullopt;
    }

    const auto enabledChecked = AppleHostEnabledCheckBox().IsChecked();

    return ::MasterControlShell::ShellAppleRemoteHost{
        hostId,
        displayName,
        transport,
        platforms,
        trimEditorValue(AppleHostAddressTextBox().Text().c_str()),
        *port,
        trimEditorValue(AppleHostUsernameTextBox().Text().c_str()),
        trimEditorValue(AppleHostServiceBaseUrlTextBox().Text().c_str()),
        trimEditorValue(AppleHostHealthPathTextBox().Text().c_str()),
        trimEditorValue(AppleHostExecutePathTextBox().Text().c_str()),
        trimEditorValue(AppleHostDeveloperDirectoryTextBox().Text().c_str()),
        trimEditorValue(AppleHostSigningIdentityTextBox().Text().c_str()),
        trimEditorValue(AppleHostNotaryProfileTextBox().Text().c_str()),
        trimEditorValue(AppleHostTeamIdTextBox().Text().c_str()),
        enabledChecked && enabledChecked.Value()
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

winrt::Windows::Foundation::IAsyncAction RuntimeSectionControl::SaveAppleHostAsync() {
    if (runtime_ == nullptr) {
        AppleHostStatusText().Text(L"Apple remote host editing is unavailable until the shell runtime is attached.");
        co_return;
    }

    const auto host = BuildAppleHostFromEditor();
    if (!host.has_value()) {
        AppleHostStatusText().Text(L"Apple host ID, display name, transport, and at least one platform are required. Ports must be blank or between 0 and 65535.");
        co_return;
    }

    SaveAppleHostButton().IsEnabled(false);
    winrt::apartment_context uiThread;
    const auto hostValue = *host;
    co_await winrt::resume_background();
    const auto result = runtime_->UpsertAppleRemoteHost(hostValue);
    co_await uiThread;

    AppleHostStatusText().Text(winrt::hstring(result.message));
    if (result.succeeded) {
        appleHostDirty_ = false;
        selectedAppleHostId_ = hostValue.hostId;
        if (refreshRequested_) {
            refreshRequested_();
        }
    }
    UpdateEditorState();
}

winrt::Windows::Foundation::IAsyncAction RuntimeSectionControl::RemoveAppleHostAsync() {
    if (runtime_ == nullptr) {
        AppleHostStatusText().Text(L"Apple remote host editing is unavailable until the shell runtime is attached.");
        co_return;
    }
    if (selectedAppleHostId_.empty()) {
        AppleHostStatusText().Text(L"Select an Apple remote host before removing it.");
        co_return;
    }

    RemoveAppleHostButton().IsEnabled(false);
    winrt::apartment_context uiThread;
    const auto hostId = selectedAppleHostId_;
    co_await winrt::resume_background();
    const auto result = runtime_->RemoveAppleRemoteHost(hostId);
    co_await uiThread;

    AppleHostStatusText().Text(winrt::hstring(result.message));
    if (result.succeeded) {
        appleHostDirty_ = false;
        selectedAppleHostId_.clear();
        selectedAppleHostIndex_ = -1;
        ClearAppleHostEditor();
        if (refreshRequested_) {
            refreshRequested_();
        }
    }
    UpdateEditorState();
}

// v0.9.76: the templated card-grid renderer + endpointStatId overloads
// were extracted to EndpointStatCardGrid.h so TelemetrySectionControl
// can reuse them. The two thin adapters below keep their RuntimeSection
// names and just route through the shared template.
//
// v0.10.9: operator directive -- "sub-agents and MCP servers displayed on
// the Run Time view, need to be the same style as in the Telemetry view."
// Both calls now pass compact=true so the Runtime deck uses the same
// footer-style tile grid (7-column wide, wraps vertically) as the
// Telemetry deck and the cross-tab SUB-AGENT GRID footer. The wide
// stacked-card path remains available for any future surface that wants
// it but is no longer used by Runtime.
void RuntimeSectionControl::PopulateSubAgentCards(const ::MasterControlShell::ShellSnapshot& snapshot) {
    renderEndpointStatCardGrid(
        SubAgentsCardStack(),
        SubAgentsHeadlineText(),
        snapshot.subAgentRuntimeStats,
        L"sub-agent",
        L"No sub-agents registered yet. Use the New Sub-Agent action above to publish one.",
        L"No managed pool. POST /api/pools with matching id to enable autoscale.",
        /*compact=*/true);
}

void RuntimeSectionControl::PopulateMcpServerCards(const ::MasterControlShell::ShellSnapshot& snapshot) {
    renderEndpointStatCardGrid(
        McpServersCardStack(),
        McpServersHeadlineText(),
        snapshot.mcpServerRuntimeStats,
        L"MCP server",
        L"No MCP servers registered yet. Use POST /api/runtime/mcp-servers to publish one.",
        L"No managed pool. POST /api/pools with matching id to enable autoscale.",
        /*compact=*/true);
}

} // namespace winrt::MasterControlShell::implementation
