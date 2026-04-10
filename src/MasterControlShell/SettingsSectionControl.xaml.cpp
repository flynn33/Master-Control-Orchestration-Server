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

} // namespace winrt::MasterControlShell::implementation
