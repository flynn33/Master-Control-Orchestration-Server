// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#include "pch.h"

#include "OverviewSectionControl.xaml.h"

#if __has_include("OverviewSectionControl.g.cpp")
#include "OverviewSectionControl.g.cpp"
#endif

namespace winrt::MasterControlShell::implementation {

OverviewSectionControl::OverviewSectionControl() {
    InitializeComponent();
}

void OverviewSectionControl::AttachRuntime(::MasterControlShell::ShellRuntime* runtime) {
    runtime_ = runtime;
    auto ignored = RefreshClaudePluginAsync();
    (void)ignored;
}

void OverviewSectionControl::ApplySnapshot(const ::MasterControlShell::ShellSnapshot& snapshot) {
    OverviewTextBlock().Text(winrt::hstring(snapshot.overviewText));
    EnvironmentNarrativeText().Text(winrt::hstring(snapshot.environmentText));
    ConfigurationNarrativeText().Text(winrt::hstring(snapshot.configurationText));
    OverviewStatusText().Text(snapshot.apiHealthy
        ? L"ADMIN API ONLINE · SYNCHRONIZED"
        : L"ADMIN API OFFLINE · CACHED STATE");

    // Refresh the Claude Code Control toggle whenever a fresh snapshot
    // lands. Skip if a toggle is currently in flight so we don't clobber
    // the in-progress state.
    if (runtime_ != nullptr && !claudePluginBusy_) {
        auto ignored = RefreshClaudePluginAsync();
        (void)ignored;
    }
}

// ---------------------------------------------------------------------------
// Claude Code Control toggle
//
// Calls /api/claude-plugin/{status,toggle} through ShellRuntime. The toggle
// is the primary action surface: the runtime owns active-user resolution
// and the file ops, the shell just renders state and posts the flip.
//
// ToggleSwitch fires Toggled on every IsOn change including programmatic
// ones, so suspendClaudePluginToggleHandler_ short-circuits the handler
// while RefreshClaudePluginAsync sets IsOn from the API response.
// ---------------------------------------------------------------------------

void OverviewSectionControl::RenderClaudePluginStatus(
    const ::MasterControlShell::ShellClaudePluginStatus& status) {
    std::wstring headline;
    std::wstring detail;
    bool toggleEnabled = (runtime_ != nullptr) && !claudePluginBusy_;
    bool toggleOn = status.registered;

    if (claudePluginBusy_) {
        headline = L"Working…";
    } else if (!status.transportError.empty()) {
        headline = L"Cannot reach the local admin API.";
        detail = status.transportError
            + L"  Make sure the Master Control Orchestration Server is running.";
        toggleEnabled = false;
    } else if (!status.reachable) {
        headline = L"Plugin status surface unavailable.";
        detail = L"The runtime returned an unreadable response. The installed version may be older than 0.6.2.";
        toggleEnabled = false;
    } else if (!status.activeUserResolved) {
        headline = L"No interactive Windows user resolved.";
        detail = status.lastError.empty()
            ? L"Sign in to Windows on this host first."
            : status.lastError;
        toggleEnabled = false;
    } else if (status.registered) {
        headline = L"Connected as " + status.userName + L".";
        detail = L"Junction: " + status.target;
        if (!status.source.empty()) {
            detail += L"  →  " + status.source;
        }
    } else {
        headline = L"Disconnected (" + status.userName + L").";
        detail = L"Toggle on to drop a junction at " + status.target
            + L"  pointing at " + status.source + L".";
        if (!status.lastError.empty()) {
            detail += L"  Last error: " + status.lastError;
        }
    }

    suspendClaudePluginToggleHandler_ = true;
    ClaudePluginToggle().IsEnabled(toggleEnabled);
    ClaudePluginToggle().IsOn(toggleOn);
    suspendClaudePluginToggleHandler_ = false;
    ClaudePluginStatusText().Text(winrt::hstring(headline));
    ClaudePluginDetailText().Text(winrt::hstring(detail));
}

winrt::Windows::Foundation::IAsyncAction OverviewSectionControl::RefreshClaudePluginAsync() {
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
    if (!claudePluginBusy_) {
        RenderClaudePluginStatus(status);
    }
}

winrt::Windows::Foundation::IAsyncAction OverviewSectionControl::ToggleClaudePluginAsync(bool requestedOn) {
    if (runtime_ == nullptr || claudePluginBusy_) {
        co_return;
    }

    claudePluginBusy_ = true;
    {
        ::MasterControlShell::ShellClaudePluginStatus busy;
        busy.reachable = true;
        busy.activeUserResolved = true;
        busy.registered = requestedOn;
        RenderClaudePluginStatus(busy);
    }

    winrt::apartment_context uiThread;
    co_await winrt::resume_background();
    auto status = runtime_->ToggleClaudePlugin();
    co_await uiThread;

    claudePluginBusy_ = false;

    // If the toggle failed and the actual state didn't change, bounce IsOn
    // back to the prior position so the user sees an honest reflection.
    if (status.reachable && status.registered != requestedOn && !status.transportError.empty()) {
        // network error: keep the request visible but flag the failure.
    }
    RenderClaudePluginStatus(status);
}

void OverviewSectionControl::ClaudePluginToggle_Toggled(
    Windows::Foundation::IInspectable const&,
    Microsoft::UI::Xaml::RoutedEventArgs const&) {
    if (suspendClaudePluginToggleHandler_) {
        return;
    }
    if (runtime_ == nullptr) {
        return;
    }
    const bool requestedOn = ClaudePluginToggle().IsOn();
    auto ignored = ToggleClaudePluginAsync(requestedOn);
    (void)ignored;
}

} // namespace winrt::MasterControlShell::implementation
