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

// v0.9.2: resource-allocation slider ValueChanged. Identifies which of the
// four sliders fired by name, mirrors the new value into the partner
// TextBox (formatted as a plain integer), and marks the editor dirty.
// The textbox-edit path also fires here implicitly via SetValue.
void SettingsSectionControl::AllocationSlider_ValueChanged(
    Windows::Foundation::IInspectable const& sender,
    Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const&) {
    if (syncingAllocation_) {
        return;
    }
    auto slider = sender.try_as<Microsoft::UI::Xaml::Controls::Slider>();
    if (slider == nullptr) {
        return;
    }
    const auto name = std::wstring(slider.Name());
    Microsoft::UI::Xaml::Controls::TextBox partner{ nullptr };
    if (name == L"CpuAllocationSlider")           partner = CpuAllocationTextBox();
    else if (name == L"MemoryAllocationSlider")   partner = MemoryAllocationTextBox();
    else if (name == L"BandwidthAllocationSlider") partner = BandwidthAllocationTextBox();
    else if (name == L"StorageAllocationSlider")  partner = StorageAllocationTextBox();
    if (partner == nullptr) {
        return;
    }
    const int rounded = static_cast<int>(slider.Value() + 0.5);
    const auto desired = std::to_wstring(rounded);
    // Avoid clobbering mid-edit if the textbox already shows the same
    // integer value (TextChanged would re-fire and bounce back).
    if (std::wstring(partner.Text().c_str()) == desired) {
        if (!suspendDirtyTracking_) {
            dirty_ = true;
            UpdateSummary();
            UpdateEditorState();
        }
        return;
    }
    syncingAllocation_ = true;
    partner.Text(winrt::hstring(desired));
    syncingAllocation_ = false;

    if (suspendDirtyTracking_) {
        return;
    }
    dirty_ = true;
    UpdateSummary();
    UpdateEditorState();
}

// v0.9.2: paired TextBox edit. Parses the integer (clamped 0..100) and
// pushes it onto the matching Slider so the visual stays in lockstep. If
// the entry is blank or non-numeric we leave the slider alone -- the
// operator may be mid-typing -- but we still mark the editor dirty so
// Apply remains enabled.
void SettingsSectionControl::AllocationTextBox_TextChanged(
    Windows::Foundation::IInspectable const& sender,
    Microsoft::UI::Xaml::Controls::TextChangedEventArgs const&) {
    if (syncingAllocation_) {
        return;
    }
    auto textBox = sender.try_as<Microsoft::UI::Xaml::Controls::TextBox>();
    if (textBox == nullptr) {
        return;
    }
    const auto name = std::wstring(textBox.Name());
    Microsoft::UI::Xaml::Controls::Slider slider{ nullptr };
    if (name == L"CpuAllocationTextBox")           slider = CpuAllocationSlider();
    else if (name == L"MemoryAllocationTextBox")   slider = MemoryAllocationSlider();
    else if (name == L"BandwidthAllocationTextBox") slider = BandwidthAllocationSlider();
    else if (name == L"StorageAllocationTextBox")  slider = StorageAllocationSlider();
    if (slider == nullptr) {
        return;
    }
    const auto parsed = parseInteger(std::wstring(textBox.Text().c_str()), 0, 100);
    if (parsed.has_value()) {
        const double desired = static_cast<double>(*parsed);
        // Skip the slider write if it already matches -- avoids a no-op
        // ValueChanged round-trip.
        if (slider.Value() != desired) {
            syncingAllocation_ = true;
            slider.Value(desired);
            syncingAllocation_ = false;
        }
    }

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
}

void SettingsSectionControl::PopulateEditorFromSnapshot() {
    suspendDirtyTracking_ = true;
    InstanceNameTextBox().Text(winrt::hstring(snapshot_.instanceName.empty() ? L"Master Control Orchestration Server" : snapshot_.instanceName));
    BindAddressTextBox().Text(winrt::hstring(snapshot_.bindAddress.empty() ? L"0.0.0.0" : snapshot_.bindAddress));
    BrowserPortTextBox().Text(winrt::hstring(std::to_wstring(snapshot_.browserPort)));
    BeaconPortTextBox().Text(winrt::hstring(std::to_wstring(snapshot_.beaconPort)));
    BeaconEnabledToggle().IsOn(snapshot_.beaconEnabled);
    // v0.9.2: drive both the Slider and the partner TextBox from the
    // snapshot's percentage value. Set Slider.Value first; the existing
    // ValueChanged handler would normally mirror that into the TextBox,
    // but suspendDirtyTracking_ is in effect throughout this method, so
    // we set the TextBox text explicitly to keep them aligned during
    // initial hydration. Both writes happen under suspendDirtyTracking_
    // so they do not flip dirty_.
    const auto syncAllocation = [&](Microsoft::UI::Xaml::Controls::Slider const& slider,
                                    Microsoft::UI::Xaml::Controls::TextBox const& box,
                                    int percent) {
        // Clamp the persisted value into the slider's range (the schema
        // already enforces 0..100 but a stale config could violate it).
        const int clamped = (percent < 0) ? 0 : (percent > 100 ? 100 : percent);
        slider.Value(static_cast<double>(clamped));
        box.Text(winrt::hstring(std::to_wstring(clamped)));
    };
    syncAllocation(CpuAllocationSlider(), CpuAllocationTextBox(), snapshot_.cpuAllocationPercent);
    syncAllocation(MemoryAllocationSlider(), MemoryAllocationTextBox(), snapshot_.memoryAllocationPercent);
    syncAllocation(BandwidthAllocationSlider(), BandwidthAllocationTextBox(), snapshot_.bandwidthAllocationPercent);
    syncAllocation(StorageAllocationSlider(), StorageAllocationTextBox(), snapshot_.storageAllocationPercent);
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
    // v0.9.2: read percentages from the Slider (the source of truth for
    // these four fields). The paired TextBox is kept in sync via the
    // ValueChanged + TextChanged handlers, but during a mid-edit moment
    // it can briefly lag (or hold an invalid intermediate value the
    // operator is typing); the slider always reflects an in-range integer.
    const auto formatPercent = [](double value) {
        const int rounded = static_cast<int>(value + 0.5);
        return std::to_wstring(rounded);
    };
    summary += L" | CPU ";
    summary += formatPercent(CpuAllocationSlider().Value());
    summary += L"% | RAM ";
    summary += formatPercent(MemoryAllocationSlider().Value());
    summary += L"% | Bandwidth ";
    summary += formatPercent(BandwidthAllocationSlider().Value());
    summary += L"% | Storage ";
    summary += formatPercent(StorageAllocationSlider().Value());
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
    // v0.9.2: read percentages from the Slider (clamped 0..100 by the
    // control's own Minimum/Maximum). Wrap as std::optional<int> for
    // shape compatibility with the existing branches below; the
    // optional is unconditionally engaged because the slider cannot
    // produce an out-of-range value.
    const auto sliderPercent = [](Microsoft::UI::Xaml::Controls::Slider const& slider) -> std::optional<int> {
        return static_cast<int>(slider.Value() + 0.5);
    };
    const auto cpuPercent = sliderPercent(CpuAllocationSlider());
    const auto memoryPercent = sliderPercent(MemoryAllocationSlider());
    const auto bandwidthPercent = sliderPercent(BandwidthAllocationSlider());
    const auto storagePercent = sliderPercent(StorageAllocationSlider());

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

// v0.8.5: shared "extract Button.Tag string" helper used by the two
// settings-tab dispatch handlers below.
namespace {
std::wstring buttonTagAsString(winrt::Windows::Foundation::IInspectable const& sender) {
    auto button = sender.try_as<winrt::Microsoft::UI::Xaml::Controls::Button>();
    if (!button) return {};
    auto tag = button.Tag();
    if (!tag) return {};
    if (auto box = tag.try_as<winrt::Windows::Foundation::IPropertyValue>()) {
        if (box.Type() == winrt::Windows::Foundation::PropertyType::String) {
            return std::wstring(box.GetString().c_str());
        }
    }
    return {};
}
} // namespace

void SettingsSectionControl::HostControlButton_Click(
    Windows::Foundation::IInspectable const& sender,
    Microsoft::UI::Xaml::RoutedEventArgs const&) {
    const std::wstring token = buttonTagAsString(sender);
    if (token.empty()) return;
    if (actionRequested_) {
        actionRequested_(token);
    }
}

void SettingsSectionControl::GuidedWizardButton_Click(
    Windows::Foundation::IInspectable const& sender,
    Microsoft::UI::Xaml::RoutedEventArgs const&) {
    const std::wstring token = buttonTagAsString(sender);
    if (token.empty()) return;
    if (actionRequested_) {
        actionRequested_(token);
    }
}

} // namespace winrt::MasterControlShell::implementation
