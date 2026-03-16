// Master Control Program
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#include "pch.h"

#include "SecuritySectionControl.xaml.h"

#if __has_include("SecuritySectionControl.g.cpp")
#include "SecuritySectionControl.g.cpp"
#endif

#include "ShellFormatting.h"

namespace winrt::MasterControlShell::implementation {

using namespace ::MasterControlShell::Presentation;
using namespace Microsoft::UI::Xaml::Controls;

SecuritySectionControl::SecuritySectionControl() {
    InitializeComponent();
}

void SecuritySectionControl::AttachRuntime(::MasterControlShell::ShellRuntime* runtime,
                                           std::function<void()> refreshRequested) {
    runtime_ = runtime;
    refreshRequested_ = std::move(refreshRequested);
    UpdateEditorState();
}

void SecuritySectionControl::ApplySnapshot(const ::MasterControlShell::ShellSnapshot& snapshot) {
    lastSnapshot_ = snapshot;
    BindAddressText().Text(winrt::hstring(snapshot.bindAddress));
    BrowserPortText().Text(winrt::hstring(std::to_wstring(snapshot.browserPort)));
    BeaconEnabledText().Text(winrt::hstring(boolLabel(snapshot.beaconEnabled)));
    AiAutonomyText().Text(winrt::hstring(boolLabel(snapshot.aiAutonomyEnabled)));
    SecurityProtocolsText().Text(winrt::hstring(boolLabel(snapshot.securityProtocolsEnabled)));
    OpenLanAccessText().Text(winrt::hstring(boolLabel(snapshot.openLanAccess)));
    SecurityNarrativeText().Text(winrt::hstring(formatSecurityNarrative(snapshot)));

    if (!isDirty_) {
        PopulateEditorFromSnapshot(snapshot);
    }
    UpdateEditorState();
}

void SecuritySectionControl::SecurityToggle_Toggled(
    Windows::Foundation::IInspectable const&,
    Microsoft::UI::Xaml::RoutedEventArgs const&) {
    if (!suspendDirtyTracking_) {
        isDirty_ = true;
        UpdateEditorState();
    }
}

void SecuritySectionControl::TrustedHostsTextBox_TextChanged(
    Windows::Foundation::IInspectable const&,
    TextChangedEventArgs const&) {
    if (!suspendDirtyTracking_) {
        isDirty_ = true;
        UpdateEditorState();
    }
}

void SecuritySectionControl::SaveSecurityButton_Click(
    Windows::Foundation::IInspectable const&,
    Microsoft::UI::Xaml::RoutedEventArgs const&) {
    SaveSecurityAsync(false);
}

void SecuritySectionControl::PopulateEditorFromSnapshot(const ::MasterControlShell::ShellSnapshot& snapshot) {
    suspendDirtyTracking_ = true;
    SecurityProtocolsToggle().IsOn(snapshot.securitySettings.securityProtocolsEnabled);
    OpenLanAccessToggle().IsOn(snapshot.securitySettings.allowOpenLanAccess);
    EnableTlsToggle().IsOn(snapshot.securitySettings.enableTls);
    EnableAuthenticationToggle().IsOn(snapshot.securitySettings.enableAuthentication);
    TroubleshootingBypassToggle().IsOn(snapshot.securitySettings.allowTroubleshootingBypass);

    std::wstring hostsText;
    for (size_t index = 0; index < snapshot.securitySettings.trustedRemoteHosts.size(); ++index) {
        if (index > 0) {
            hostsText += L"\r\n";
        }
        hostsText += snapshot.securitySettings.trustedRemoteHosts[index];
    }
    TrustedHostsTextBox().Text(winrt::hstring(hostsText));
    suspendDirtyTracking_ = false;
}

void SecuritySectionControl::UpdateEditorState() {
    SaveSecurityButton().IsEnabled(runtime_ != nullptr && isDirty_);
}

void SecuritySectionControl::SetStatus(winrt::hstring const& message) {
    SecurityEditorStatusText().Text(message);
}

::MasterControlShell::ShellSecuritySettings SecuritySectionControl::BuildSecuritySettings() {
    ::MasterControlShell::ShellSecuritySettings settings;
    settings.securityProtocolsEnabled = SecurityProtocolsToggle().IsOn();
    settings.allowOpenLanAccess = OpenLanAccessToggle().IsOn();
    settings.enableTls = EnableTlsToggle().IsOn();
    settings.enableAuthentication = EnableAuthenticationToggle().IsOn();
    settings.allowTroubleshootingBypass = TroubleshootingBypassToggle().IsOn();

    std::wstringstream stream(std::wstring(TrustedHostsTextBox().Text().c_str()));
    std::wstring line;
    while (std::getline(stream, line)) {
        line.erase(std::remove_if(line.begin(), line.end(), [](const wchar_t character) { return character == L'\r'; }), line.end());
        if (!line.empty()) {
            settings.trustedRemoteHosts.push_back(line);
        }
    }
    return settings;
}

winrt::Windows::Foundation::IAsyncAction SecuritySectionControl::SaveSecurityAsync(const bool confirmUnsafeChanges) {
    if (runtime_ == nullptr) {
        SetStatus(L"Security editing is unavailable until the shell runtime is attached.");
        co_return;
    }

    SaveSecurityButton().IsEnabled(false);
    winrt::apartment_context uiThread;
    const auto settings = BuildSecuritySettings();
    co_await winrt::resume_background();
    const auto result = runtime_->UpdateSecuritySettings(settings, confirmUnsafeChanges);
    co_await uiThread;

    if (result.requiresConfirmation && !confirmUnsafeChanges) {
        SetStatus(winrt::hstring(result.message));
        co_await ShowUnsafeConfirmationAsync();
        co_return;
    }

    SetStatus(winrt::hstring(result.message));
    if (result.succeeded) {
        isDirty_ = false;
        if (refreshRequested_) {
            refreshRequested_();
        }
    }
    UpdateEditorState();
}

winrt::Windows::Foundation::IAsyncAction SecuritySectionControl::ShowUnsafeConfirmationAsync() {
    ContentDialog dialog;
    dialog.Title(winrt::box_value(L"Disable Security Protocols?"));
    dialog.Content(winrt::box_value(L"Disabling security protocols weakens the protection envelope for the Master Control Program. Continue only for controlled troubleshooting."));
    dialog.PrimaryButtonText(L"Disable");
    dialog.CloseButtonText(L"Cancel");
    dialog.DefaultButton(ContentDialogButton::Close);
    dialog.XamlRoot(XamlRoot());

    if (co_await dialog.ShowAsync() == ContentDialogResult::Primary) {
        co_await SaveSecurityAsync(true);
    } else {
        SetStatus(L"Security protocol change was canceled.");
        UpdateEditorState();
    }
}

} // namespace winrt::MasterControlShell::implementation
