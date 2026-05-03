// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#include "pch.h"

#include "SettingsSectionControl.xaml.h"

#if __has_include("SettingsSectionControl.g.cpp")
#include "SettingsSectionControl.g.cpp"
#endif

#include "ShellFormatting.h"

#include <optional>
#include <winrt/Windows.ApplicationModel.DataTransfer.h>

namespace winrt::MasterControlShell::implementation {

using namespace ::MasterControlShell::Presentation;

namespace {

std::wstring trimCopy(const std::wstring& value) {
    auto begin = value.begin();
    while (begin != value.end() && iswspace(*begin) != 0) {
        ++begin;
    }

    auto end = value.end();
    while (end != begin && iswspace(*(end - 1)) != 0) {
        --end;
    }

    return std::wstring(begin, end);
}

std::optional<int> parseInteger(const std::wstring& value, const int minimum, const int maximum) {
    const auto trimmed = trimCopy(value);
    if (trimmed.empty()) {
        return std::nullopt;
    }

    try {
        const auto parsed = std::stoi(trimmed);
        if (parsed < minimum || parsed > maximum) {
            return std::nullopt;
        }
        return parsed;
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace

SettingsSectionControl::SettingsSectionControl() {
    InitializeComponent();
}

void SettingsSectionControl::AttachRuntime(::MasterControlShell::ShellRuntime* runtime,
                                           std::function<void()> refreshRequested,
                                           std::function<void(const std::wstring&)> actionRequested) {
    runtime_ = runtime;
    refreshRequested_ = std::move(refreshRequested);
    actionRequested_ = std::move(actionRequested);
    UpdateEditorState();
    auto ignored = RefreshClaudePluginAsync();
    (void)ignored;
}

void SettingsSectionControl::SettingsEditor_TextChanged(
    Windows::Foundation::IInspectable const&,
    Microsoft::UI::Xaml::Controls::TextChangedEventArgs const&) {
    if (suspendDirtyTracking_) {
        return;
    }

    dirty_ = true;
    UpdateSummary();
    UpdateEditorState();
}

void SettingsSectionControl::SettingsToggle_Toggled(
    Windows::Foundation::IInspectable const&,
    Microsoft::UI::Xaml::RoutedEventArgs const&) {
    if (suspendDirtyTracking_) {
        return;
    }

    dirty_ = true;
    UpdateSummary();
    UpdateEditorState();
}

void SettingsSectionControl::ApplySettingsButton_Click(
    Windows::Foundation::IInspectable const&,
    Microsoft::UI::Xaml::RoutedEventArgs const&) {
    auto ignored = ApplySettingsAsync();
    (void)ignored;
}

void SettingsSectionControl::ApplySnapshot(const ::MasterControlShell::ShellSnapshot& snapshot) {
    snapshot_ = snapshot;
    ResourceEnvelopeText().Text(winrt::hstring(formatResourceEnvelope(snapshot)));
    ConfigPathText().Text(winrt::hstring(snapshot.configPath));
    DataDirectoryText().Text(winrt::hstring(snapshot.dataDirectory));
    ConfigurationNarrativeText().Text(winrt::hstring(snapshot.configurationText));

    if (!dirty_) {
        PopulateEditorFromSnapshot();
    } else {
        UpdateSummary();
        UpdateEditorState();
    }

    UpdateFirewallRuleSnippets();

    // Refresh the Claude Code Control card whenever a fresh snapshot lands.
    // Skip if a toggle is currently in flight so we don't clobber the UI.
    if (runtime_ != nullptr && !claudePluginBusy_) {
        auto ignored = RefreshClaudePluginAsync();
        (void)ignored;
    }
}

void SettingsSectionControl::PopulateEditorFromSnapshot() {
    suspendDirtyTracking_ = true;
    InstanceNameTextBox().Text(winrt::hstring(snapshot_.instanceName.empty() ? L"Master Control Orchestration Server" : snapshot_.instanceName));
    BindAddressTextBox().Text(winrt::hstring(snapshot_.bindAddress.empty() ? L"0.0.0.0" : snapshot_.bindAddress));
    BrowserPortTextBox().Text(winrt::hstring(std::to_wstring(snapshot_.browserPort)));
    BeaconPortTextBox().Text(winrt::hstring(std::to_wstring(snapshot_.beaconPort)));
    BeaconEnabledToggle().IsOn(snapshot_.beaconEnabled);
    CpuAllocationTextBox().Text(winrt::hstring(std::to_wstring(snapshot_.cpuAllocationPercent)));
    MemoryAllocationTextBox().Text(winrt::hstring(std::to_wstring(snapshot_.memoryAllocationPercent)));
    BandwidthAllocationTextBox().Text(winrt::hstring(std::to_wstring(snapshot_.bandwidthAllocationPercent)));
    StorageAllocationTextBox().Text(winrt::hstring(std::to_wstring(snapshot_.storageAllocationPercent)));
    SettingsStatusText().Text(L"Edit any field on this surface and click Apply Host Settings. The shell sends the update directly through the local admin API.");
    suspendDirtyTracking_ = false;
    UpdateSummary();
    UpdateEditorState();
}

void SettingsSectionControl::UpdateSummary() {
    std::wstring summary = L"Host '";
    const auto instanceName = trimCopy(std::wstring(InstanceNameTextBox().Text().c_str()));
    summary += instanceName.empty() ? L"Master Control Orchestration Server" : instanceName;
    summary += L"' on ";
    const auto bindAddress = trimCopy(std::wstring(BindAddressTextBox().Text().c_str()));
    summary += bindAddress.empty() ? L"0.0.0.0" : bindAddress;
    summary += L" | Browser ";
    summary += trimCopy(std::wstring(BrowserPortTextBox().Text().c_str()));
    summary += L" | Beacon ";
    summary += BeaconEnabledToggle().IsOn() ? L"enabled" : L"disabled";
    summary += L" | CPU ";
    summary += trimCopy(std::wstring(CpuAllocationTextBox().Text().c_str()));
    summary += L"% | RAM ";
    summary += trimCopy(std::wstring(MemoryAllocationTextBox().Text().c_str()));
    summary += L"% | Bandwidth ";
    summary += trimCopy(std::wstring(BandwidthAllocationTextBox().Text().c_str()));
    summary += L"% | Storage ";
    summary += trimCopy(std::wstring(StorageAllocationTextBox().Text().c_str()));
    summary += L"%";
    SettingsSummaryText().Text(winrt::hstring(summary));
}

void SettingsSectionControl::UpdateEditorState() {
    ApplySettingsButton().IsEnabled(runtime_ != nullptr);
}

winrt::Windows::Foundation::IAsyncAction SettingsSectionControl::ApplySettingsAsync() {
    if (runtime_ == nullptr) {
        SettingsStatusText().Text(L"Host settings are unavailable until the shell runtime is attached.");
        co_return;
    }

    const auto browserPort = parseInteger(std::wstring(BrowserPortTextBox().Text().c_str()), 1, 65535);
    const auto beaconPort = parseInteger(std::wstring(BeaconPortTextBox().Text().c_str()), 1, 65535);
    const auto cpuPercent = parseInteger(std::wstring(CpuAllocationTextBox().Text().c_str()), 0, 100);
    const auto memoryPercent = parseInteger(std::wstring(MemoryAllocationTextBox().Text().c_str()), 0, 100);
    const auto bandwidthPercent = parseInteger(std::wstring(BandwidthAllocationTextBox().Text().c_str()), 0, 100);
    const auto storagePercent = parseInteger(std::wstring(StorageAllocationTextBox().Text().c_str()), 0, 100);

    const auto instanceName = trimCopy(std::wstring(InstanceNameTextBox().Text().c_str()));
    const auto bindAddress = trimCopy(std::wstring(BindAddressTextBox().Text().c_str()));
    if (instanceName.empty()) {
        SettingsStatusText().Text(L"Instance name is required.");
        co_return;
    }
    if (bindAddress.empty()) {
        SettingsStatusText().Text(L"Bind address is required.");
        co_return;
    }
    if (!browserPort.has_value() || !beaconPort.has_value()) {
        SettingsStatusText().Text(L"Browser and beacon ports must be between 1 and 65535.");
        co_return;
    }
    if (!cpuPercent.has_value() || !memoryPercent.has_value() || !bandwidthPercent.has_value() || !storagePercent.has_value()) {
        SettingsStatusText().Text(L"Resource allocation values must stay between 0 and 100.");
        co_return;
    }

    ApplySettingsButton().IsEnabled(false);
    SettingsStatusText().Text(L"Applying host settings through the local admin API.");
    winrt::apartment_context uiThread;
    co_await winrt::resume_background();
    const auto result = runtime_->UpdateHostSettings(::MasterControlShell::ShellHostSettings{
        instanceName,
        bindAddress,
        static_cast<uint16_t>(*browserPort),
        static_cast<uint16_t>(*beaconPort),
        BeaconEnabledToggle().IsOn(),
        *cpuPercent,
        *memoryPercent,
        *bandwidthPercent,
        *storagePercent
    });
    co_await uiThread;

    SettingsStatusText().Text(winrt::hstring(result.message));
    if (result.succeeded) {
        dirty_ = false;
        if (refreshRequested_) {
            refreshRequested_();
        }
    }
    UpdateEditorState();
}

// ---------------------------------------------------------------------------
// LAN advertising and Windows Firewall guidance
//
// Builds the four New-NetFirewallRule snippets from the live snapshot and
// shows them in read-only TextBoxes for copy. Operators apply them from an
// elevated PowerShell. MCOS does not invoke them - admin elevation is the
// operator's call. See docs/wiki/Operations/Windows-Firewall-LAN-Mode.md.
// ---------------------------------------------------------------------------

namespace {

// Default ports when the snapshot has not yet populated. Match
// MasterControlDefaults.cpp's buildDefaultConfiguration() values.
constexpr uint16_t kDefaultBrowserPort = 7300;
constexpr uint16_t kDefaultBeaconPort = 7301;
constexpr uint16_t kDefaultGatewayPort = 8080;
constexpr uint16_t kMDnsPort = 5353;

std::wstring formatPortOrDefault(uint16_t port, uint16_t fallback) {
    return std::to_wstring(port == 0 ? fallback : port);
}

std::wstring buildFirewallRule(const std::wstring& displayName,
                               const std::wstring& protocol,
                               const std::wstring& port,
                               const std::wstring& serviceHostExePath) {
    std::wstring rule;
    rule += L"New-NetFirewallRule `\r\n";
    rule += L"  -DisplayName \"" + displayName + L"\" `\r\n";
    rule += L"  -Direction Inbound `\r\n";
    rule += L"  -Action Allow `\r\n";
    rule += L"  -Protocol " + protocol + L" `\r\n";
    rule += L"  -LocalPort " + port + L" `\r\n";
    rule += L"  -Profile Private,Domain `\r\n";
    rule += L"  -Program \"" + serviceHostExePath + L"\"";
    return rule;
}

} // namespace

void SettingsSectionControl::UpdateFirewallRuleSnippets() {
    const auto browserPort = formatPortOrDefault(snapshot_.browserPort, kDefaultBrowserPort);
    const auto beaconPort = formatPortOrDefault(snapshot_.beaconPort, kDefaultBeaconPort);
    // Gateway port is owned by AppConfiguration::mcpGateway::listenPort, which
    // the WinUI shell snapshot does not currently mirror. We surface the
    // configured-default 8080 with a note in the summary so the operator
    // knows to substitute their actual port if mcos.json overrides it.
    const auto gatewayPort = formatPortOrDefault(0, kDefaultGatewayPort);

    const std::wstring serviceHostPath = L"C:\\Program Files\\Master Control Orchestration Server\\MasterControlServiceHost.exe";

    FirewallAdvertisingSummaryText().Text(winrt::hstring(
        L"Advertising state. DNS-SD service types: _mcos._tcp.local on TCP " + browserPort
        + L", _mcos-mcp._tcp.local on TCP " + gatewayPort
        + L", _mcos-onboarding._tcp.local on TCP " + browserPort
        + L". UDP beacon broadcasts on UDP " + beaconPort
        + L". DNS-SD itself uses UDP " + std::to_wstring(kMDnsPort)
        + L". The gateway port shown here is the buildDefaultConfiguration value (8080); if mcos.json sets mcpGateway.listenPort to something else, edit the snippet to match before running it."));

    FirewallGatewayRuleTextBox().Text(winrt::hstring(buildFirewallRule(
        L"MCOS - MCP Gateway (LAN)", L"TCP", gatewayPort, serviceHostPath)));
    FirewallOperatorRuleTextBox().Text(winrt::hstring(buildFirewallRule(
        L"MCOS - Operator Surface (LAN)", L"TCP", browserPort, serviceHostPath)));
    FirewallMDnsRuleTextBox().Text(winrt::hstring(buildFirewallRule(
        L"MCOS - DNS-SD/mDNS (LAN)", L"UDP", std::to_wstring(kMDnsPort), serviceHostPath)));
    FirewallBeaconRuleTextBox().Text(winrt::hstring(buildFirewallRule(
        L"MCOS - Discovery Beacon (LAN)", L"UDP", beaconPort, serviceHostPath)));
}

void SettingsSectionControl::CopyTextToClipboard(const std::wstring& text, const std::wstring& successMessage) {
    using namespace winrt::Windows::ApplicationModel::DataTransfer;
    DataPackage package;
    package.SetText(winrt::hstring(text));
    Clipboard::SetContent(package);
    FirewallCopyStatusText().Text(winrt::hstring(successMessage));
}

void SettingsSectionControl::CopyFirewallGatewayRuleButton_Click(
    Windows::Foundation::IInspectable const&,
    Microsoft::UI::Xaml::RoutedEventArgs const&) {
    CopyTextToClipboard(std::wstring(FirewallGatewayRuleTextBox().Text().c_str()),
                        L"Copied: MCP Gateway firewall rule. Run from an elevated PowerShell.");
}

void SettingsSectionControl::CopyFirewallOperatorRuleButton_Click(
    Windows::Foundation::IInspectable const&,
    Microsoft::UI::Xaml::RoutedEventArgs const&) {
    CopyTextToClipboard(std::wstring(FirewallOperatorRuleTextBox().Text().c_str()),
                        L"Copied: Operator surface firewall rule. Run from an elevated PowerShell.");
}

void SettingsSectionControl::CopyFirewallMDnsRuleButton_Click(
    Windows::Foundation::IInspectable const&,
    Microsoft::UI::Xaml::RoutedEventArgs const&) {
    CopyTextToClipboard(std::wstring(FirewallMDnsRuleTextBox().Text().c_str()),
                        L"Copied: DNS-SD/mDNS firewall rule. Run from an elevated PowerShell.");
}

void SettingsSectionControl::CopyFirewallBeaconRuleButton_Click(
    Windows::Foundation::IInspectable const&,
    Microsoft::UI::Xaml::RoutedEventArgs const&) {
    CopyTextToClipboard(std::wstring(FirewallBeaconRuleTextBox().Text().c_str()),
                        L"Copied: Discovery beacon firewall rule. Run from an elevated PowerShell.");
}

// ---------------------------------------------------------------------------
// Claude Code Control card
//
// Reads /api/claude-plugin/status on attach and after every toggle. The
// runtime owns active-user resolution and the junction file ops; the shell
// just renders state and posts the toggle from the UI thread.
// ---------------------------------------------------------------------------

void SettingsSectionControl::RenderClaudePluginStatus(
    const ::MasterControlShell::ShellClaudePluginStatus& status) {
    std::wstring headline;
    std::wstring detail;
    std::wstring buttonContent = L"Connect Claude Code";
    bool buttonEnabled = (runtime_ != nullptr) && !claudePluginBusy_;

    if (claudePluginBusy_) {
        headline = L"Working...";
    } else if (!status.transportError.empty()) {
        headline = L"Cannot reach the local admin API";
        detail = status.transportError + L"  Make sure the Master Control Orchestration Server service is running.";
        buttonEnabled = false;
    } else if (!status.reachable) {
        headline = L"Plugin status surface unavailable";
        detail = L"The runtime returned an unreadable response. The installed version may be older than 0.6.1.";
        buttonEnabled = false;
    } else if (!status.activeUserResolved) {
        headline = L"No active interactive user";
        detail = status.lastError.empty()
            ? L"Sign in to Windows on this host first, then click Refresh."
            : status.lastError;
        buttonEnabled = false;
    } else if (status.registered) {
        headline = L"Claude Code Control: connected as " + status.userName;
        detail = L"Junction at " + status.target;
        if (!status.source.empty()) {
            detail += L"  ->  " + status.source;
        }
        buttonContent = L"Disconnect Claude Code";
    } else {
        headline = L"Claude Code Control: disconnected (" + status.userName + L")";
        detail = L"Click Connect to drop a junction at " + status.target
            + L"  pointing at " + status.source + L".";
        if (!status.lastError.empty()) {
            detail += L"  Last error: " + status.lastError;
        }
    }

    ClaudePluginStatusText().Text(winrt::hstring(headline));
    ClaudePluginDetailText().Text(winrt::hstring(detail));
    ClaudePluginToggleButton().Content(winrt::box_value(winrt::hstring(buttonContent)));
    ClaudePluginToggleButton().IsEnabled(buttonEnabled);
}

winrt::Windows::Foundation::IAsyncAction SettingsSectionControl::RefreshClaudePluginAsync() {
    if (runtime_ == nullptr) {
        ::MasterControlShell::ShellClaudePluginStatus s;
        s.transportError = L"Shell runtime is not attached yet.";
        RenderClaudePluginStatus(s);
        co_return;
    }

    winrt::apartment_context uiThread;
    co_await winrt::resume_background();
    const auto status = runtime_->FetchClaudePluginStatus();
    co_await uiThread;
    RenderClaudePluginStatus(status);
}

winrt::Windows::Foundation::IAsyncAction SettingsSectionControl::ToggleClaudePluginAsync() {
    if (runtime_ == nullptr || claudePluginBusy_) {
        co_return;
    }

    claudePluginBusy_ = true;
    {
        ::MasterControlShell::ShellClaudePluginStatus busy;
        busy.reachable = true;
        busy.activeUserResolved = true;
        // Render the "Working..." state without changing other fields.
        RenderClaudePluginStatus(busy);
    }

    winrt::apartment_context uiThread;
    co_await winrt::resume_background();
    const auto status = runtime_->ToggleClaudePlugin();
    co_await uiThread;

    claudePluginBusy_ = false;
    RenderClaudePluginStatus(status);
}

void SettingsSectionControl::ClaudePluginToggleButton_Click(
    Windows::Foundation::IInspectable const&,
    Microsoft::UI::Xaml::RoutedEventArgs const&) {
    auto ignored = ToggleClaudePluginAsync();
    (void)ignored;
}

} // namespace winrt::MasterControlShell::implementation
