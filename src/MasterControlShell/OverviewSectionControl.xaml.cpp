// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#include "pch.h"

#include "OverviewSectionControl.xaml.h"
#include "ShellFormatting.h"
// v0.11.0-alpha.2 follow-up: MASTERCONTROL_VERSION literal for the
// hero-eyebrow version badge. GeneratedVersion.h is CMake-generated
// next to MasterControlShell.vcxproj from VERSION.json at configure
// time; the Shell-local include avoids needing AdditionalIncludeDirectories
// to point at the runtime-side ${CMAKE_BINARY_DIR}/include tree.
#include "GeneratedVersion.h"

#if __has_include("OverviewSectionControl.g.cpp")
#include "OverviewSectionControl.g.cpp"
#endif

// v0.8.7: error-export needs Win32 file APIs + KnownFolders for the
// public documents path.
// v0.9.76: Supervisor wizard adds IFileSaveDialog (ShObjIdl_core.h via
// ShlObj.h), ComPtr (wrl/client.h), and KnownFolders for the
// "Documents/MCOS/SupervisorConfigs" default folder.
#include <algorithm>
#include <KnownFolders.h>
#include <ShlObj.h>
#include <ShObjIdl.h>
#include <wrl/client.h>
#include <sstream>

namespace winrt::MasterControlShell::implementation {

OverviewSectionControl::OverviewSectionControl() {
    InitializeComponent();
    // v0.11.0-alpha.2 follow-up: surface the running build version in the
    // hero eyebrow. Format mirrors the /api/version response so operators
    // can confirm the Shell binary matches the service it is talking to
    // without bouncing to Settings or curling the admin port.
    try {
        const std::wstring versionBadge = std::wstring(L"v") + L"" MASTERCONTROL_VERSION;
        OverviewVersionText().Text(winrt::hstring(versionBadge));
    } catch (...) {
        // Generated XAML accessors throw if the visual tree is not yet
        // realized. The element default-empty Text in XAML is the safe
        // fallback -- losing the version badge is strictly cosmetic and
        // must not crash the Overview ctor.
    }
}

void OverviewSectionControl::AttachRuntime(::MasterControlShell::ShellRuntime* runtime) {
    runtime_ = runtime;
    // v0.10.12: refresh all three Direct AI Plugin Connection slots
    // (claude + chatgpt + grok) on attach so the operator sees the
    // current mutually-exclusive state immediately instead of after
    // the first snapshot tick.
    auto ignored = RefreshAllPluginSlotsAsync();
    (void)ignored;
}

void OverviewSectionControl::ApplySnapshot(const ::MasterControlShell::ShellSnapshot& snapshot) {
    OverviewTextBlock().Text(winrt::hstring(snapshot.overviewText));
    EnvironmentNarrativeText().Text(winrt::hstring(snapshot.environmentText));
    ConfigurationNarrativeText().Text(winrt::hstring(snapshot.configurationText));
    OverviewStatusText().Text(snapshot.apiHealthy
        ? L"ADMIN API ONLINE · SYNCHRONIZED"
        : L"ADMIN API OFFLINE · CACHED STATE");

    // v0.8.7 status grid + error reporting frame.
    // v0.10.10: MCP SERVERS and SUB-AGENTS summary cards removed from the
    // Overview deck per operator directive. Those decks live on Runtime
    // and Telemetry as footer-style tile grids; the Overview status grid
    // now carries only APIS & SERVICES + SECURITY STANCE.
    ApplyApisAndServicesCard(snapshot);
    ApplySecurityStanceCard(snapshot);
    // v0.9.75: visible self-tests precede the error frame so the
    // operator sees the affirmative pass/fail roster before
    // scrolling into the failure list.
    ApplySelfTestCard(snapshot);
    // v0.9.76: Supervisor Agent card lives between Self-Tests and Error
    // Reporting. The card mirrors /api/supervisor/status into the
    // ShellSnapshot.supervisorStatus fields on every tick, and routes
    // the operator's selection through ShellRuntime helpers that POST
    // /api/supervisor/* and surface the result back through this
    // section.
    ApplySupervisorCard(snapshot);
    ApplyErrorReportingCard(snapshot);
    // Cache bind-host + port so async handlers (FileSavePicker dialog
    // running off the UI thread) don't have to re-resolve them while
    // the operator is in the dialog. Refreshes every snapshot tick.
    supervisorBindHost_ = snapshot.bindAddress;
    supervisorBindPort_ = snapshot.browserPort;

    // Refresh the Claude Code Control toggle whenever a fresh snapshot
    // lands. Skip if a toggle is currently in flight so we don't clobber
    // the in-progress state.
    if (runtime_ != nullptr && !claudePluginBusy_) {
        auto ignored = RefreshClaudePluginAsync();
        (void)ignored;
    }
    // v0.10.12: same snapshot-tick refresh for the ChatGPT + Grok
    // Direct AI Plugin Connection toggles. Skipped when the
    // corresponding slot has a toggle in flight so we don't clobber
    // the request's busy headline before the response lands.
    if (runtime_ != nullptr && !chatGptPluginBusy_) {
        auto ignored = RefreshDirectAIPluginAsync(L"chatgpt");
        (void)ignored;
    }
    if (runtime_ != nullptr && !grokPluginBusy_) {
        auto ignored = RefreshDirectAIPluginAsync(L"grok");
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

// ---------------------------------------------------------------------------
// v0.10.12: ChatGPT / Grok Direct AI Plugin Connection toggle plumbing.
//
// Each provider has its own ToggleSwitch + status TextBlock + detail
// TextBlock. The Toggled handler kicks off ToggleDirectAIPluginAsync,
// which posts /api/<providerId>-plugin/toggle and -- because the
// backend revokes the other two slots when one is turned on -- always
// fires RefreshAllPluginSlotsAsync afterwards so every card reflects
// the new mutually-exclusive state within one round-trip.
//
// suspend*ToggleHandler_ guards programmatic IsOn updates (from the
// snapshot/refresh path) from re-entering the toggle handler.
// ---------------------------------------------------------------------------

void OverviewSectionControl::RenderDirectAIPluginStatus(
    const std::wstring& providerId,
    const ::MasterControlShell::ShellDirectAIPluginStatus& status) {
    std::wstring headline;
    std::wstring detail;
    bool toggleEnabled = (runtime_ != nullptr);
    bool toggleOn = status.registered;
    bool busy = (providerId == L"chatgpt") ? chatGptPluginBusy_ : grokPluginBusy_;

    if (busy) {
        headline = L"Working…";
    } else if (!status.transportError.empty()) {
        headline = L"Cannot reach the local admin API.";
        detail = status.transportError
            + L"  Make sure the Master Control Orchestration Server is running.";
        toggleEnabled = false;
    } else if (!status.reachable) {
        headline = L"Plugin status surface unavailable.";
        detail = L"The runtime returned an unreadable response. The installed version may be older than 0.10.12.";
        toggleEnabled = false;
    } else if (!status.activeUserResolved) {
        headline = L"No interactive Windows user resolved.";
        detail = status.lastError.empty()
            ? L"Sign in to Windows on this host first."
            : status.lastError;
        toggleEnabled = false;
    } else if (status.registered) {
        headline = L"Connected as " + status.userName + L".";
        detail = L"Connector config: " + status.target;
    } else {
        headline = L"Disconnected (" + status.userName + L").";
        detail = L"Toggle on to drop the connector config at " + status.target + L".";
        if (!status.lastError.empty()) {
            detail += L"  Last error: " + status.lastError;
        }
    }

    if (providerId == L"chatgpt") {
        suspendChatGptPluginToggleHandler_ = true;
        ChatGptPluginToggle().IsEnabled(toggleEnabled);
        ChatGptPluginToggle().IsOn(toggleOn);
        suspendChatGptPluginToggleHandler_ = false;
        ChatGptPluginStatusText().Text(winrt::hstring(headline));
        ChatGptPluginDetailText().Text(winrt::hstring(detail));
    } else if (providerId == L"grok") {
        suspendGrokPluginToggleHandler_ = true;
        GrokPluginToggle().IsEnabled(toggleEnabled);
        GrokPluginToggle().IsOn(toggleOn);
        suspendGrokPluginToggleHandler_ = false;
        GrokPluginStatusText().Text(winrt::hstring(headline));
        GrokPluginDetailText().Text(winrt::hstring(detail));
    }
}

winrt::Windows::Foundation::IAsyncAction
OverviewSectionControl::RefreshDirectAIPluginAsync(std::wstring providerId) {
    if (runtime_ == nullptr) {
        ::MasterControlShell::ShellDirectAIPluginStatus s;
        s.providerId = providerId;
        s.transportError = L"Shell runtime is not attached yet.";
        RenderDirectAIPluginStatus(providerId, s);
        co_return;
    }
    winrt::apartment_context uiThread;
    co_await winrt::resume_background();
    const auto status = runtime_->FetchDirectAIPluginStatus(providerId);
    co_await uiThread;
    // If a toggle landed since we kicked off the fetch, leave the
    // busy headline up; the toggle path will rerender on completion.
    const bool busy = (providerId == L"chatgpt") ? chatGptPluginBusy_ : grokPluginBusy_;
    if (!busy) {
        RenderDirectAIPluginStatus(providerId, status);
    }
}

winrt::Windows::Foundation::IAsyncAction
OverviewSectionControl::RefreshAllPluginSlotsAsync() {
    // The backend's mutual-exclusion enforcement means a successful
    // toggle on any of the three slots can flip the other two off.
    // Pull all three fresh so the UI never drifts from server-side
    // state. Claude refresh runs first because its render path also
    // hits Win32 plugin-junction APIs that take a few ms.
    co_await RefreshClaudePluginAsync();
    co_await RefreshDirectAIPluginAsync(L"chatgpt");
    co_await RefreshDirectAIPluginAsync(L"grok");
}

winrt::Windows::Foundation::IAsyncAction
OverviewSectionControl::ToggleDirectAIPluginAsync(std::wstring providerId, bool requestedOn) {
    if (runtime_ == nullptr) co_return;
    const bool chatgpt = (providerId == L"chatgpt");
    bool& busy = chatgpt ? chatGptPluginBusy_ : grokPluginBusy_;
    if (busy) co_return;
    busy = true;
    {
        ::MasterControlShell::ShellDirectAIPluginStatus pending;
        pending.providerId = providerId;
        pending.reachable = true;
        pending.activeUserResolved = true;
        pending.registered = requestedOn;
        RenderDirectAIPluginStatus(providerId, pending);
    }

    winrt::apartment_context uiThread;
    co_await winrt::resume_background();
    auto status = runtime_->ToggleDirectAIPlugin(providerId);
    co_await uiThread;
    busy = false;
    RenderDirectAIPluginStatus(providerId, status);
    // Mutual exclusion: refresh siblings so their toggles reflect the
    // backend's revoke side-effect.
    co_await RefreshClaudePluginAsync();
    co_await RefreshDirectAIPluginAsync(chatgpt ? std::wstring(L"grok") : std::wstring(L"chatgpt"));
}

void OverviewSectionControl::ChatGptPluginToggle_Toggled(
    Windows::Foundation::IInspectable const&,
    Microsoft::UI::Xaml::RoutedEventArgs const&) {
    if (suspendChatGptPluginToggleHandler_) return;
    if (runtime_ == nullptr) return;
    const bool requestedOn = ChatGptPluginToggle().IsOn();
    auto ignored = ToggleDirectAIPluginAsync(L"chatgpt", requestedOn);
    (void)ignored;
}

void OverviewSectionControl::GrokPluginToggle_Toggled(
    Windows::Foundation::IInspectable const&,
    Microsoft::UI::Xaml::RoutedEventArgs const&) {
    if (suspendGrokPluginToggleHandler_) return;
    if (runtime_ == nullptr) return;
    const bool requestedOn = GrokPluginToggle().IsOn();
    auto ignored = ToggleDirectAIPluginAsync(L"grok", requestedOn);
    (void)ignored;
}

// =====================================================================
// v0.8.7: Overview status grid + Error Reporting frame
// =====================================================================

namespace {

// Tron palette helpers shared by the four status cards. Same tones used
// by the Telemetry tab status dots (v0.7.9).
winrt::Windows::UI::Color statusColor(const wchar_t* tone) {
    using winrt::Windows::UI::ColorHelper;
    if (std::wstring(tone) == L"good")    return ColorHelper::FromArgb(0xFF, 0x1c, 0xf2, 0xc1);
    if (std::wstring(tone) == L"warn")    return ColorHelper::FromArgb(0xFF, 0xff, 0xc8, 0x57);
    if (std::wstring(tone) == L"crit")    return ColorHelper::FromArgb(0xFF, 0xff, 0x6a, 0x80);
    return ColorHelper::FromArgb(0xFF, 0x8c, 0xb7, 0xc4);
}

void paintDot(winrt::Microsoft::UI::Xaml::Controls::Border const& dot, const wchar_t* tone) {
    using winrt::Microsoft::UI::Xaml::Media::SolidColorBrush;
    dot.Background(SolidColorBrush(statusColor(tone)));
}

std::wstring serviceStateLabelOverview(::MasterControlShell::ServiceState s) {
    switch (s) {
        case ::MasterControlShell::ServiceState::Running:       return L"running";
        case ::MasterControlShell::ServiceState::Stopped:       return L"stopped";
        case ::MasterControlShell::ServiceState::StartPending:  return L"starting";
        case ::MasterControlShell::ServiceState::StopPending:   return L"stopping";
        case ::MasterControlShell::ServiceState::Paused:        return L"paused";
        case ::MasterControlShell::ServiceState::Missing:       return L"missing";
        default:                                                return L"unknown";
    }
}

std::wstring escapeJsonW(const std::wstring& raw) {
    std::wstring out;
    out.reserve(raw.size() + 4);
    for (wchar_t c : raw) {
        switch (c) {
            case L'\\': out += L"\\\\"; break;
            case L'"':  out += L"\\\""; break;
            case L'\n': out += L"\\n"; break;
            case L'\r': out += L"\\r"; break;
            case L'\t': out += L"\\t"; break;
            default:
                if (c < 0x20) {
                    wchar_t esc[8];
                    swprintf_s(esc, L"\\u%04x", static_cast<unsigned>(c));
                    out += esc;
                } else {
                    out += c;
                }
                break;
        }
    }
    return out;
}

std::string wideToUtf8Local(const std::wstring& w) {
    if (w.empty()) return {};
    int needed = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string out(needed, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), out.data(), needed, nullptr, nullptr);
    return out;
}

std::wstring publicDocsErrorDir() {
    PWSTR pPath = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_PublicDocuments, 0, nullptr, &pPath))) {
        std::wstring root(pPath);
        CoTaskMemFree(pPath);
        std::wstring dir = root + L"\\Master Control Orchestration Server";
        CreateDirectoryW(dir.c_str(), nullptr);
        return dir;
    }
    return L"";
}

} // namespace

void OverviewSectionControl::ApplyApisAndServicesCard(const ::MasterControlShell::ShellSnapshot& snapshot) {
    using namespace winrt::Microsoft::UI::Xaml;
    const bool serviceUp = (snapshot.serviceState == ::MasterControlShell::ServiceState::Running);
    const bool apiUp     = snapshot.apiHealthy;
    const wchar_t* tone = (serviceUp && apiUp) ? L"good"
                        : (apiUp || serviceUp) ? L"warn"
                                               : L"crit";
    paintDot(ApiServicesStatusDot(), tone);
    std::wostringstream out;
    // v0.9.76: route the bind-address render through resolveDisplayBindAddress
    // so wildcard binds (0.0.0.0 / ::) surface the LAN-routable primary IP.
    // v0.10.12: refresh the operator-facing label so it reads as a result of
    // auto-detection rather than a confusing "configured 0.0.0.0" parenthetical.
    // The card now spells out four operationally relevant lines so the
    // operator can confirm at a glance that MCOS bound to its host IP and
    // is publishing a reachable MCP gateway URL.
    const auto resolvedBind = ::MasterControlShell::Presentation::resolveDisplayBindAddress(
        snapshot.bindAddress, snapshot.primaryIpAddress);
    out << L"Service: " << serviceStateLabelOverview(snapshot.serviceState) << L'\n'
        << L"Admin API: " << (apiUp ? L"reachable" : L"offline") << L'\n'
        << L"Browser surface: " << (apiUp ? L"published" : L"unpublished") << L'\n'
        << L"Beacon: " << (snapshot.beaconEnabled ? L"on" : L"off")
        << L" : " << snapshot.beaconPort << L'\n'
        << L"Bind: " << resolvedBind
        << L":" << snapshot.browserPort;
    if (::MasterControlShell::Presentation::isWildcardBindAddress(snapshot.bindAddress)) {
        out << L"  (auto-detected; binding all interfaces)";
    }
    // v0.10.12: explicit MCP gateway URL line. Pre-v0.10.12 the operator had
    // to cross-reference the discovery doc or supervisor config to confirm
    // the gateway was reachable on the LAN IP. Now the URL shows directly
    // beneath the bind line on the Overview card.
    if (!snapshot.mcpGatewayUrl.empty()) {
        const auto gatewayUrl = ::MasterControlShell::Presentation::substituteWildcardInGatewayUrl(
            snapshot.mcpGatewayUrl, snapshot.primaryIpAddress);
        out << L"\nMCP Gateway: " << gatewayUrl;
        if (!snapshot.mcpGatewayState.empty()) {
            out << L"  (" << snapshot.mcpGatewayState << L")";
        }
    }
    ApiServicesText().Text(winrt::hstring(out.str()));
}

void OverviewSectionControl::ApplySecurityStanceCard(const ::MasterControlShell::ShellSnapshot& snapshot) {
    const wchar_t* tone = L"good";
    if (!snapshot.securityProtocolsEnabled) tone = L"crit";
    else if (snapshot.aiAutonomyEnabled || snapshot.openLanAccess) tone = L"warn";
    if (snapshot.governanceFindingCount > 0 && tone == L"good") tone = L"warn";
    paintDot(SecurityStanceStatusDot(), tone);

    std::wostringstream out;
    out << L"Security protocols: " << (snapshot.securityProtocolsEnabled ? L"enabled" : L"disabled") << L'\n'
        << L"AI autonomy: " << (snapshot.aiAutonomyEnabled ? L"enabled" : L"disabled") << L'\n'
        << L"Open LAN access: " << (snapshot.openLanAccess ? L"yes" : L"no") << L'\n'
        << L"Governance posture: " << (snapshot.governancePosture.empty() ? std::wstring(L"pending") : snapshot.governancePosture) << L'\n'
        << L"Live findings: " << snapshot.governanceFindingCount;
    SecurityStanceText().Text(winrt::hstring(out.str()));
}

// v0.10.10: ApplyMcpServersCard + ApplySubAgentsCard removed alongside
// the XAML elements they populated (McpServersStatusDot, McpServersHeadline,
// McpServersText, SubAgentsStatusDot, SubAgentsHeadline, SubAgentsText).
// Per operator directive the Overview deck no longer carries an MCP /
// Sub-Agent summary card -- those decks live on Runtime and Telemetry
// as footer-style tile grids.

void OverviewSectionControl::ApplyErrorReportingCard(const ::MasterControlShell::ShellSnapshot& snapshot) {
    using namespace winrt::Microsoft::UI::Xaml;
    using namespace winrt::Microsoft::UI::Xaml::Controls;
    using namespace winrt::Microsoft::UI::Xaml::Media;
    using winrt::Windows::UI::ColorHelper;

    lastErrorEvents_ = snapshot.errorEvents;
    const size_t errorCount = lastErrorEvents_.size();

    const wchar_t* tone = errorCount == 0 ? L"good"
                       : errorCount < 5 ? L"warn"
                       : L"crit";
    paintDot(ErrorReportingStatusDot(), tone);

    std::wostringstream head;
    if (errorCount == 0) {
        head << L"No errors recorded";
    } else {
        head << errorCount << L" error" << (errorCount == 1 ? L"" : L"s") << L" recorded";
    }
    ErrorReportingHeadline().Text(winrt::hstring(head.str()));

    auto stack = ErrorReportingList();
    stack.Children().Clear();

    if (errorCount == 0) {
        TextBlock noErrors;
        noErrors.Text(L"Activity stream is clean. The export button writes an empty list when no errors are present.");
        noErrors.FontSize(11);
        noErrors.Foreground(SolidColorBrush(ColorHelper::FromArgb(0xFF, 0x8c, 0xb7, 0xc4)));
        noErrors.TextWrapping(TextWrapping::Wrap);
        stack.Children().Append(noErrors);
        return;
    }

    for (const auto& err : lastErrorEvents_) {
        Border row;
        row.BorderThickness(Thickness{0, 0, 0, 1});
        row.BorderBrush(SolidColorBrush(ColorHelper::FromArgb(0x33, 0xff, 0x3d, 0x2e)));
        row.Padding(Thickness{0, 4, 0, 4});

        StackPanel rowStack;
        rowStack.Spacing(2);

        StackPanel head;
        head.Orientation(Orientation::Horizontal);
        head.Spacing(8);

        const wchar_t* sevTone = (err.severity == L"critical") ? L"crit"
                              : (err.severity == L"error")    ? L"crit"
                              : L"warn";
        Border sevDot;
        sevDot.Width(8); sevDot.Height(8);
        winrt::Microsoft::UI::Xaml::CornerRadius dotCorners;
        dotCorners.TopLeft = 4; dotCorners.TopRight = 4;
        dotCorners.BottomLeft = 4; dotCorners.BottomRight = 4;
        sevDot.CornerRadius(dotCorners);
        sevDot.VerticalAlignment(VerticalAlignment::Center);
        sevDot.Background(SolidColorBrush(statusColor(sevTone)));

        TextBlock kindText;
        const std::wstring kindLabel = err.severity + L" · " + (err.kind.empty() ? std::wstring(L"event") : err.kind);
        kindText.Text(winrt::hstring(kindLabel));
        kindText.FontSize(11);
        kindText.Foreground(SolidColorBrush(statusColor(sevTone)));

        TextBlock tsText;
        tsText.Text(winrt::hstring(err.timestampUtc));
        tsText.FontSize(11);
        tsText.Foreground(SolidColorBrush(ColorHelper::FromArgb(0xFF, 0x8c, 0xb7, 0xc4)));

        head.Children().Append(sevDot);
        head.Children().Append(kindText);
        head.Children().Append(tsText);

        TextBlock messageText;
        std::wstring message = err.message.empty()
            ? (L"HTTP " + std::to_wstring(err.statusCode))
            : err.message;
        if (err.statusCode > 0 && !err.message.empty()) {
            message = L"HTTP " + std::to_wstring(err.statusCode) + L" · " + message;
        }
        messageText.Text(winrt::hstring(message));
        messageText.FontSize(12);
        messageText.TextWrapping(TextWrapping::Wrap);

        rowStack.Children().Append(head);
        rowStack.Children().Append(messageText);

        if (!err.source.empty()) {
            TextBlock sourceText;
            sourceText.Text(winrt::hstring(err.source));
            sourceText.FontSize(10);
            sourceText.Foreground(SolidColorBrush(ColorHelper::FromArgb(0xFF, 0x8c, 0xb7, 0xc4)));
            sourceText.TextWrapping(TextWrapping::Wrap);
            rowStack.Children().Append(sourceText);
        }

        row.Child(rowStack);
        stack.Children().Append(row);
    }
}

// v0.9.75: visible self-test card. Renders ShellSnapshot.selfTests as
// a per-probe pass/fail roster with a status dot + headline + scrollable
// list. Tone is good when failedCount==0 and the sweep has completed,
// warn when pending, crit when failedCount>0. Each row shows a small
// pass/fail mark, the probe name, the duration, and the message.
void OverviewSectionControl::ApplySelfTestCard(const ::MasterControlShell::ShellSnapshot& snapshot) {
    using namespace winrt::Microsoft::UI::Xaml;
    using namespace winrt::Microsoft::UI::Xaml::Controls;
    using namespace winrt::Microsoft::UI::Xaml::Media;
    using winrt::Windows::UI::ColorHelper;

    const auto& s = snapshot.selfTests;

    const wchar_t* tone = L"info";
    std::wstring headline;
    if (!s.fetchError.empty()) {
        tone = L"crit";
        headline = L"Self-tests unavailable: " + s.fetchError;
    } else if (!s.available || s.pending) {
        tone = L"warn";
        headline = L"Self-tests pending — first sweep runs ~3s after service start.";
    } else if (s.failedCount > 0) {
        tone = L"crit";
        headline = std::to_wstring(s.failedCount) + L" of "
                 + std::to_wstring(s.totalCount) + L" probe"
                 + (s.totalCount == 1 ? L"" : L"s") + L" FAILED.";
    } else {
        tone = L"good";
        headline = L"All " + std::to_wstring(s.passedCount) + L" probe"
                 + (s.passedCount == 1 ? L"" : L"s") + L" passed.";
    }
    paintDot(SelfTestStatusDot(), tone);
    SelfTestHeadline().Text(winrt::hstring(headline));

    std::wostringstream sub;
    if (!s.startedAtUtc.empty()) {
        sub << L"Last sweep: " << s.startedAtUtc;
        if (!s.finishedAtUtc.empty() && s.finishedAtUtc != s.startedAtUtc) {
            sub << L" → " << s.finishedAtUtc;
        }
    } else {
        sub << L"Probes: admin port + gateway state + every supervised pool + activity ring + telemetry sampler + worker exe presence.";
    }
    SelfTestSubline().Text(winrt::hstring(sub.str()));

    auto stack = SelfTestList();
    stack.Children().Clear();

    if (!s.fetchError.empty()) {
        TextBlock txt;
        txt.Text(winrt::hstring(L"Cannot reach /api/self-tests on the local admin port."));
        txt.FontSize(11);
        txt.Foreground(SolidColorBrush(ColorHelper::FromArgb(0xFF, 0xff, 0x8c, 0x4c)));
        txt.TextWrapping(TextWrapping::Wrap);
        stack.Children().Append(txt);
        return;
    }
    if (!s.available || s.results.empty()) {
        TextBlock txt;
        txt.Text(s.pending
                 ? winrt::hstring(L"Sweep has not finished yet. Re-poll in a few seconds.")
                 : winrt::hstring(L"No probe results yet."));
        txt.FontSize(11);
        txt.Foreground(SolidColorBrush(ColorHelper::FromArgb(0xFF, 0x8c, 0xb7, 0xc4)));
        txt.TextWrapping(TextWrapping::Wrap);
        stack.Children().Append(txt);
        return;
    }

    // Group counter so failures float up. Sort: failures first by name,
    // then passes by name.
    auto results = s.results;
    std::sort(results.begin(), results.end(),
              [](const auto& a, const auto& b) {
                  if (a.ok != b.ok) return !a.ok && b.ok;
                  return a.name < b.name;
              });

    for (const auto& r : results) {
        Border row;
        row.BorderThickness(Thickness{0, 0, 0, 1});
        row.BorderBrush(SolidColorBrush(ColorHelper::FromArgb(
            0x33,
            r.ok ? 0x1c : 0xff,
            r.ok ? 0xf2 : 0x3a,
            r.ok ? 0xc1 : 0x5a)));
        row.Padding(Thickness{0, 4, 0, 4});

        StackPanel rowStack;
        rowStack.Spacing(2);

        StackPanel head;
        head.Orientation(Orientation::Horizontal);
        head.Spacing(8);

        Border dot;
        dot.Width(8); dot.Height(8);
        winrt::Microsoft::UI::Xaml::CornerRadius dotCorners;
        dotCorners.TopLeft = 4; dotCorners.TopRight = 4;
        dotCorners.BottomLeft = 4; dotCorners.BottomRight = 4;
        dot.CornerRadius(dotCorners);
        dot.VerticalAlignment(VerticalAlignment::Center);
        dot.Background(SolidColorBrush(statusColor(r.ok ? L"good" : L"crit")));

        TextBlock nameText;
        std::wstring nameLabel = (r.ok ? std::wstring(L"PASS") : std::wstring(L"FAIL"))
                               + L" · " + r.name;
        nameText.Text(winrt::hstring(nameLabel));
        nameText.FontSize(11);
        nameText.Foreground(SolidColorBrush(statusColor(r.ok ? L"good" : L"crit")));

        TextBlock durText;
        if (r.durationMs > 0) {
            durText.Text(winrt::hstring(std::to_wstring(r.durationMs) + L" ms"));
        }
        durText.FontSize(11);
        durText.Foreground(SolidColorBrush(ColorHelper::FromArgb(0xFF, 0x8c, 0xb7, 0xc4)));

        head.Children().Append(dot);
        head.Children().Append(nameText);
        head.Children().Append(durText);

        TextBlock messageText;
        messageText.Text(winrt::hstring(r.message));
        messageText.FontSize(11);
        messageText.TextWrapping(TextWrapping::Wrap);
        messageText.Foreground(SolidColorBrush(ColorHelper::FromArgb(
            0xFF, 0xC8, 0xD8, 0xDD)));

        rowStack.Children().Append(head);
        rowStack.Children().Append(messageText);

        row.Child(rowStack);
        stack.Children().Append(row);
    }
}

winrt::Windows::Foundation::IAsyncAction OverviewSectionControl::ReRunSelfTestsAsync() {
    if (!runtime_) co_return;
    auto runtime = runtime_;
    // Pattern matches RefreshClaudePluginAsync above: capture the
    // apartment_context before going to background so we can return
    // to the UI thread without depending on resume_foreground (which
    // isn't in this build's winrt headers).
    winrt::apartment_context uiThread;
    co_await winrt::resume_background();
    auto fresh = runtime->RunSelfTestsNow();
    co_await uiThread;
    // Rebuild the card from the freshly-returned snapshot so the
    // operator sees the result immediately rather than waiting for
    // the next live tick.
    ::MasterControlShell::ShellSnapshot synthetic{};
    synthetic.selfTests = std::move(fresh);
    ApplySelfTestCard(synthetic);
}

// ============================================================
// v0.9.76: Supervisor Agent Assignment Wizard handlers.
// ============================================================
//
// The operator picks exactly one supervisor model (ChatGPT / Claude /
// Grok) via a single-selection ToggleSwitch group. The Generate Config
// & Save button posts /api/supervisor/config/generate, which returns
// the freshly-issued JSON config. We then open IFileSaveDialog so the
// operator chooses where the config file lives -- it is then ferried
// to the supervisor client machine. The Revoke button revokes the
// active assignment without re-issuing.
//
// Toggle group is "single-selection radio": turning one provider on
// turns the other two off (suspendSupervisorToggleHandler_ guards the
// recursive Toggled event during the programmatic deselect).
//
// Server-side state is the source of truth -- ApplySupervisorCard
// reflects the live /api/supervisor/status field on every snapshot
// tick. Toggling on disk writes nothing; only Generate Config makes a
// new assignment. Toggling without generating just changes the local
// "intent" but still calls /api/supervisor/config/generate when the
// button is clicked.

namespace {

bool showSupervisorSaveDialog(const std::wstring& suggestedFileName,
                               std::wstring& outChosenPath) {
    // Win32 IFileSaveDialog is the canonical native save UX on Windows
    // and works without WinUI 3 IInitializeWithWindow plumbing. Parent
    // is left null; the dialog presents itself top-most relative to
    // the active foreground window. The COM apartment must be
    // initialized -- WinRT's MTA satisfies that.
    ::Microsoft::WRL::ComPtr<IFileSaveDialog> dialog;
    HRESULT hr = ::CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_ALL,
                                     IID_PPV_ARGS(&dialog));
    if (FAILED(hr) || !dialog) return false;

    COMDLG_FILTERSPEC filters[2] = {
        { L"JSON config", L"*.json" },
        { L"All files",   L"*.*" }
    };
    dialog->SetFileTypes(static_cast<UINT>(std::size(filters)), filters);
    dialog->SetFileTypeIndex(1);
    dialog->SetDefaultExtension(L"json");
    dialog->SetTitle(L"Save Supervisor Configuration");
    dialog->SetFileName(suggestedFileName.c_str());

    // Default to %USERPROFILE%\Documents\MCOS\SupervisorConfigs if it
    // exists, otherwise ShellExecute will park the dialog wherever the
    // shell decides (typically Documents).
    PWSTR documentsPath = nullptr;
    if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &documentsPath))
        && documentsPath != nullptr) {
        std::wstring suggestedFolder = std::wstring(documentsPath) + L"\\MCOS\\SupervisorConfigs";
        ::CoTaskMemFree(documentsPath);
        ::SHCreateDirectoryExW(nullptr, suggestedFolder.c_str(), nullptr);
        ::Microsoft::WRL::ComPtr<IShellItem> defaultFolder;
        if (SUCCEEDED(::SHCreateItemFromParsingName(suggestedFolder.c_str(),
                                                    nullptr,
                                                    IID_PPV_ARGS(&defaultFolder)))
            && defaultFolder) {
            dialog->SetDefaultFolder(defaultFolder.Get());
        }
    }

    hr = dialog->Show(nullptr);
    if (FAILED(hr)) return false;
    ::Microsoft::WRL::ComPtr<IShellItem> result;
    if (FAILED(dialog->GetResult(&result)) || !result) return false;
    PWSTR chosenPath = nullptr;
    if (FAILED(result->GetDisplayName(SIGDN_FILESYSPATH, &chosenPath)) || !chosenPath) {
        return false;
    }
    outChosenPath = chosenPath;
    ::CoTaskMemFree(chosenPath);
    return true;
}

bool writeUtf8FileNoBom(const std::wstring& path, const std::wstring& wideContent) {
    // Convert to UTF-8 without a BOM. JSON files are widely consumed
    // and a BOM would trip strict parsers (jq, nlohmann, etc.).
    if (wideContent.empty()) return false;
    const int bytes = ::WideCharToMultiByte(CP_UTF8, 0,
                                              wideContent.c_str(),
                                              static_cast<int>(wideContent.size()),
                                              nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) return false;
    std::string narrow(static_cast<size_t>(bytes), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0,
                          wideContent.c_str(),
                          static_cast<int>(wideContent.size()),
                          narrow.data(), bytes, nullptr, nullptr);
    HANDLE handle = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const BOOL ok = ::WriteFile(handle, narrow.data(),
                                  static_cast<DWORD>(narrow.size()),
                                  &written, nullptr);
    ::CloseHandle(handle);
    return ok && written == narrow.size();
}

const wchar_t* providerDisplayLabel(const std::wstring& providerId) {
    if (providerId == L"chatgpt") return L"ChatGPT";
    if (providerId == L"claude")  return L"Claude";
    if (providerId == L"grok")    return L"Grok";
    return L"";
}

} // namespace

void OverviewSectionControl::ApplySupervisorCard(
        const ::MasterControlShell::ShellSnapshot& snapshot) {
    using winrt::Microsoft::UI::Xaml::Media::SolidColorBrush;
    using winrt::Windows::UI::ColorHelper;

    const auto& sup = snapshot.supervisorStatus;
    const std::wstring state    = sup.state.empty() ? std::wstring(L"off") : sup.state;
    const std::wstring provider = sup.activeProviderId;
    const wchar_t* dotTone = L"neutral";
    std::wstring headline;
    std::wstring statusLine;
    if (state == L"connected") {
        dotTone = L"good";
        headline = L"Active supervisor: " + std::wstring(providerDisplayLabel(provider));
        // v0.9.90: relative-time formatting on the heartbeat. Falls
        // back to the raw UTC stamp when the parser doesn't recognize
        // the format. Empty heartbeat (just-connected, no tick yet)
        // is rendered as "(none yet)" instead of an awkward "0s ago".
        std::wstring heartbeatPhrase;
        if (sup.lastHeartbeatUtc.empty()) {
            heartbeatPhrase = L" | Last heartbeat: (none yet)";
        } else {
            const auto relative = ::MasterControlShell::Presentation::formatRelativeUtcTime(sup.lastHeartbeatUtc);
            heartbeatPhrase = relative.empty()
                ? (L" | Last heartbeat (UTC): " + sup.lastHeartbeatUtc)
                : (L" | Last heartbeat: " + relative);
        }
        statusLine = L"Mode: " + (sup.mode.empty() ? std::wstring(L"autonomous_supervisor") : sup.mode)
                   + L" | Status: connected" + heartbeatPhrase;
    } else if (state == L"pending_connection" || state == L"config_generated") {
        dotTone = L"warn";
        headline = std::wstring(providerDisplayLabel(provider))
            + L" supervisor pending connection";
        statusLine = L"Status: " + state
                   + (sup.expiresAtUtc.empty()
                        ? std::wstring(L"")
                        : (L" | Expires (UTC): " + sup.expiresAtUtc))
                   + L". Move the saved config to the LAN client and import it.";
    } else if (state == L"error") {
        dotTone = L"crit";
        headline = std::wstring(providerDisplayLabel(provider))
            + L" supervisor error";
        statusLine = sup.lastErrorMessage.empty()
            ? std::wstring(L"Status: error.")
            : (L"Error: " + sup.lastErrorMessage);
    } else if (state == L"revoked") {
        dotTone = L"warn";
        headline = std::wstring(providerDisplayLabel(provider))
            + L" supervisor revoked";
        statusLine = L"Status: revoked. Select a provider and Generate Config to assign a new supervisor.";
    } else if (state == L"disconnected") {
        dotTone = L"warn";
        headline = std::wstring(providerDisplayLabel(provider))
            + L" supervisor disconnected";
        statusLine = L"Status: disconnected. The remote client has not heartbeated recently.";
    } else {
        // off / unknown
        headline = L"No supervisor assigned.";
        statusLine = L"Status: off. Pick exactly one provider above and click Generate Config & Save.";
    }
    SupervisorHeadlineText().Text(winrt::hstring(headline));
    SupervisorStatusText().Text(winrt::hstring(statusLine));

    // v0.9.79: assignment / config / expiry / heartbeat detail line.
    // Operator-facing debug info: visible when there's an active
    // assignment, hidden when state=off so the unconfigured card
    // doesn't show empty placeholder fields.
    // v0.9.94: expiresAtUtc now renders via formatFutureUtcTime
    // ('Expires: in 2h 14m') alongside the raw UTC stamp; same shape
    // as the v0.9.90 heartbeat reformat but in the future direction.
    if (!sup.assignmentId.empty()) {
        std::wostringstream detail;
        detail << L"Assignment: " << sup.assignmentId;
        if (!sup.configId.empty()) {
            detail << L"\nConfig: " << sup.configId;
        }
        // v0.9.96: "Issued: X ago" so the operator can see assignment
        // age at a glance. Useful for deciding whether to rotate a long-
        // running config. issuedAtUtc lives on the snapshot via /api/
        // supervisor/status -> ShellSnapshot.supervisorStatus.issuedAtUtc.
        if (!sup.issuedAtUtc.empty()) {
            const auto issuedRelative = ::MasterControlShell::Presentation::formatRelativeUtcTime(sup.issuedAtUtc);
            if (issuedRelative.empty()) {
                detail << L"\nIssued (UTC): " << sup.issuedAtUtc;
            } else {
                detail << L"\nIssued: " << issuedRelative;
            }
        }
        if (!sup.expiresAtUtc.empty()) {
            const auto expiryRelative = ::MasterControlShell::Presentation::formatFutureUtcTime(sup.expiresAtUtc);
            if (expiryRelative.empty()) {
                detail << L"\nExpires (UTC): " << sup.expiresAtUtc;
            } else {
                detail << L"\nExpires: " << expiryRelative
                       << L" (" << sup.expiresAtUtc << L")";
            }
        }
        if (!sup.lastHeartbeatUtc.empty()) {
            const auto heartbeatRelative = ::MasterControlShell::Presentation::formatRelativeUtcTime(sup.lastHeartbeatUtc);
            if (heartbeatRelative.empty()) {
                detail << L"\nLast heartbeat (UTC): " << sup.lastHeartbeatUtc;
            } else {
                detail << L"\nLast heartbeat: " << heartbeatRelative;
            }
        } else if (state == L"connected") {
            detail << L"\nLast heartbeat: (none yet)";
        }
        if (!sup.clientId.empty()) {
            detail << L"\nClient: " << sup.clientId;
        }
        SupervisorDetailText().Text(winrt::hstring(detail.str()));
        SupervisorDetailText().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
    } else {
        SupervisorDetailText().Text(L"");
        SupervisorDetailText().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
    }

    // Status dot.
    auto fromHex = [](uint8_t r, uint8_t g, uint8_t b) {
        return ColorHelper::FromArgb(0xFF, r, g, b);
    };
    winrt::Windows::UI::Color color =
          (std::wstring(dotTone) == L"good")    ? fromHex(0x1c, 0xf2, 0xc1)
        : (std::wstring(dotTone) == L"warn")    ? fromHex(0xff, 0xaf, 0x3a)
        : (std::wstring(dotTone) == L"crit")    ? fromHex(0xff, 0x3a, 0x5a)
        :                                         fromHex(0x8c, 0xb7, 0xc4);
    SupervisorStatusDot().Background(SolidColorBrush(color));

    // Sync the toggle group to server-side state so the radio reflects
    // the active assignment. suspendSupervisorToggleHandler_ blocks
    // the toggle handlers from re-firing during the programmatic set.
    suspendSupervisorToggleHandler_ = true;
    SupervisorChatGptToggle().IsOn(provider == L"chatgpt");
    SupervisorClaudeToggle().IsOn(provider == L"claude");
    SupervisorGrokToggle().IsOn(provider == L"grok");
    suspendSupervisorToggleHandler_ = false;

    // Generate is enabled when any provider is selected (or none --
    // letting the operator switch). Revoke is enabled iff there's a
    // server-side active assignment.
    const bool anySelected = SupervisorChatGptToggle().IsOn()
        || SupervisorClaudeToggle().IsOn()
        || SupervisorGrokToggle().IsOn();
    SupervisorGenerateButton().IsEnabled(anySelected && !supervisorBusy_);
    const bool active = sup.active
        || state == L"pending_connection"
        || state == L"config_generated"
        || state == L"connected"
        || state == L"disconnected";
    SupervisorRevokeButton().IsEnabled(active && !supervisorBusy_);
    lastSupervisorSelection_ = provider;
}

void OverviewSectionControl::SetSupervisorSelection(const std::wstring& providerId) {
    suspendSupervisorToggleHandler_ = true;
    SupervisorChatGptToggle().IsOn(providerId == L"chatgpt");
    SupervisorClaudeToggle().IsOn(providerId == L"claude");
    SupervisorGrokToggle().IsOn(providerId == L"grok");
    suspendSupervisorToggleHandler_ = false;
    SupervisorGenerateButton().IsEnabled(!providerId.empty() && !supervisorBusy_);
}

std::wstring OverviewSectionControl::CurrentSupervisorSelection() {
    if (SupervisorChatGptToggle().IsOn()) return L"chatgpt";
    if (SupervisorClaudeToggle().IsOn())  return L"claude";
    if (SupervisorGrokToggle().IsOn())    return L"grok";
    return std::wstring{};
}

void OverviewSectionControl::SupervisorChatGptToggle_Toggled(
        Windows::Foundation::IInspectable const&,
        Microsoft::UI::Xaml::RoutedEventArgs const&) {
    if (suspendSupervisorToggleHandler_) return;
    const bool on = SupervisorChatGptToggle().IsOn();
    if (on) {
        SetSupervisorSelection(L"chatgpt");
    } else if (CurrentSupervisorSelection().empty()) {
        SupervisorGenerateButton().IsEnabled(false);
    }
}

void OverviewSectionControl::SupervisorClaudeToggle_Toggled(
        Windows::Foundation::IInspectable const&,
        Microsoft::UI::Xaml::RoutedEventArgs const&) {
    if (suspendSupervisorToggleHandler_) return;
    const bool on = SupervisorClaudeToggle().IsOn();
    if (on) {
        SetSupervisorSelection(L"claude");
    } else if (CurrentSupervisorSelection().empty()) {
        SupervisorGenerateButton().IsEnabled(false);
    }
}

void OverviewSectionControl::SupervisorGrokToggle_Toggled(
        Windows::Foundation::IInspectable const&,
        Microsoft::UI::Xaml::RoutedEventArgs const&) {
    if (suspendSupervisorToggleHandler_) return;
    const bool on = SupervisorGrokToggle().IsOn();
    if (on) {
        SetSupervisorSelection(L"grok");
    } else if (CurrentSupervisorSelection().empty()) {
        SupervisorGenerateButton().IsEnabled(false);
    }
}

void OverviewSectionControl::SupervisorGenerateButton_Click(
        Windows::Foundation::IInspectable const&,
        Microsoft::UI::Xaml::RoutedEventArgs const&) {
    const auto provider = CurrentSupervisorSelection();
    if (provider.empty() || supervisorBusy_) return;
    auto ignored = GenerateSupervisorConfigAsync(provider);
    (void)ignored;
}

void OverviewSectionControl::SupervisorRevokeButton_Click(
        Windows::Foundation::IInspectable const&,
        Microsoft::UI::Xaml::RoutedEventArgs const&) {
    if (supervisorBusy_) return;
    auto ignored = RevokeSupervisorAsync();
    (void)ignored;
}

void OverviewSectionControl::SupervisorVerifyEndpointsButton_Click(
        Windows::Foundation::IInspectable const&,
        Microsoft::UI::Xaml::RoutedEventArgs const&) {
    // v0.10.13: probe MCOS's own URLs from inside the runtime and
    // render the per-probe roster. Doesn't gate on supervisorBusy_
    // because the reachability check is read-only and orthogonal to
    // the assignment lifecycle.
    auto ignored = VerifySupervisorEndpointsAsync();
    (void)ignored;
}

winrt::Windows::Foundation::IAsyncAction
OverviewSectionControl::VerifySupervisorEndpointsAsync() {
    if (!runtime_) co_return;
    auto runtime = runtime_;
    SupervisorVerifyEndpointsButton().IsEnabled(false);
    SupervisorReachabilityText().Visibility(
        winrt::Microsoft::UI::Xaml::Visibility::Visible);
    SupervisorReachabilityText().Text(
        L"Probing server-side reachability of every URL the supervisor wizard would issue...");

    winrt::apartment_context uiThread;
    co_await winrt::resume_background();
    const auto check = runtime->CheckSupervisorReachability();
    co_await uiThread;

    SupervisorVerifyEndpointsButton().IsEnabled(true);
    if (!check.ok) {
        SupervisorReachabilityText().Text(winrt::hstring(
            std::wstring(L"Reachability check failed: ")
            + (check.transportError.empty()
                ? std::wstring(L"unknown transport error.")
                : check.transportError)));
        co_return;
    }
    SupervisorReachabilityText().Text(winrt::hstring(check.bodyText));
}

winrt::Windows::Foundation::IAsyncAction
OverviewSectionControl::GenerateSupervisorConfigAsync(std::wstring providerId) {
    if (!runtime_) co_return;
    auto runtime = runtime_;

    // Mark busy so toggles + buttons go inert while the round-trip is
    // outstanding. Re-enable on every exit path through a small RAII.
    supervisorBusy_ = true;
    SupervisorGenerateButton().IsEnabled(false);
    SupervisorRevokeButton().IsEnabled(false);
    SupervisorStatusText().Text(L"Generating supervisor configuration...");

    winrt::apartment_context uiThread;
    co_await winrt::resume_background();
    auto issue = runtime->GenerateSupervisorConfig(providerId);
    co_await uiThread;

    if (!issue.ok) {
        SupervisorStatusText().Text(winrt::hstring(
            std::wstring(L"Error: ")
            + (issue.errorMessage.empty()
                ? std::wstring(L"unknown failure from /api/supervisor/config/generate.")
                : issue.errorMessage)));
        supervisorBusy_ = false;
        SupervisorGenerateButton().IsEnabled(true);
        co_return;
    }

    // Save dialog. The IFileSaveDialog Show() call blocks the UI
    // thread, but a save dialog is intrinsically modal so this matches
    // the operator's expectation.
    std::wstring chosenPath;
    const std::wstring suggested = issue.fileName.empty()
        ? (std::wstring(L"mcos-supervisor-") + providerId + L".config.json")
        : issue.fileName;
    const bool picked = showSupervisorSaveDialog(suggested, chosenPath);
    if (!picked) {
        SupervisorStatusText().Text(
            L"Configuration generated; save cancelled. Click Generate Config & Save to retry.");
        supervisorBusy_ = false;
        // Keep the assignment in pending state on the server side; the
        // operator can re-pick the same provider and regenerate.
        SupervisorGenerateButton().IsEnabled(true);
        SupervisorRevokeButton().IsEnabled(true);
        co_return;
    }

    if (!writeUtf8FileNoBom(chosenPath, issue.configJson)) {
        SupervisorStatusText().Text(winrt::hstring(
            std::wstring(L"Error writing configuration to: ") + chosenPath));
        supervisorBusy_ = false;
        SupervisorGenerateButton().IsEnabled(true);
        SupervisorRevokeButton().IsEnabled(true);
        co_return;
    }

    SupervisorStatusText().Text(winrt::hstring(
        std::wstring(L"Configuration saved to: ") + chosenPath
        + L"\nMove this file to the LAN machine running "
        + providerDisplayLabel(providerId)
        + L" and import it into that client. Status: pending connection."));
    supervisorBusy_ = false;
    // Keep generate enabled so the operator can re-issue if they
    // changed their mind; revoke too so they can drop it.
    SupervisorGenerateButton().IsEnabled(true);
    SupervisorRevokeButton().IsEnabled(true);
}

winrt::Windows::Foundation::IAsyncAction
OverviewSectionControl::RevokeSupervisorAsync() {
    if (!runtime_) co_return;
    auto runtime = runtime_;
    supervisorBusy_ = true;
    SupervisorGenerateButton().IsEnabled(false);
    SupervisorRevokeButton().IsEnabled(false);
    SupervisorStatusText().Text(L"Revoking supervisor assignment...");

    winrt::apartment_context uiThread;
    co_await winrt::resume_background();
    std::wstring err;
    const bool ok = runtime->RevokeSupervisor(L"Operator clicked Revoke Active.", err);
    co_await uiThread;

    supervisorBusy_ = false;
    if (!ok) {
        SupervisorStatusText().Text(winrt::hstring(
            std::wstring(L"Error revoking: ")
            + (err.empty() ? std::wstring(L"unknown failure.") : err)));
        SupervisorGenerateButton().IsEnabled(true);
        SupervisorRevokeButton().IsEnabled(true);
        co_return;
    }
    // Clear local UI state -- next snapshot tick will refresh from
    // /api/supervisor/status with state=off.
    suspendSupervisorToggleHandler_ = true;
    SupervisorChatGptToggle().IsOn(false);
    SupervisorClaudeToggle().IsOn(false);
    SupervisorGrokToggle().IsOn(false);
    suspendSupervisorToggleHandler_ = false;
    SupervisorStatusText().Text(L"Supervisor revoked. Pick a provider and Generate Config to reassign.");
    SupervisorGenerateButton().IsEnabled(false);
    SupervisorRevokeButton().IsEnabled(false);
}

void OverviewSectionControl::ReRunSelfTestsButton_Click(
    Windows::Foundation::IInspectable const&,
    Microsoft::UI::Xaml::RoutedEventArgs const&) {
    auto ignored = ReRunSelfTestsAsync();
    (void)ignored;
}

void OverviewSectionControl::ExportErrorsButton_Click(
    Windows::Foundation::IInspectable const&,
    Microsoft::UI::Xaml::RoutedEventArgs const&) {
    // Build an ISO-8601-ish timestamp local to disk -- safe filename.
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t fileName[128];
    swprintf_s(fileName, L"error-export-%04u%02u%02uT%02u%02u%02u.json",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    std::wstring dir = publicDocsErrorDir();
    if (dir.empty()) {
        ErrorExportStatusText().Text(L"Could not resolve %PUBLIC%\\Documents path; export skipped.");
        return;
    }
    const std::wstring fullPath = dir + L"\\" + fileName;

    // Build the JSON document. Schema:
    //   {
    //     "exportedAtUtc": "...",
    //     "schema": "mcos.error-export.v1",
    //     "count": N,
    //     "events": [ { id, timestampUtc, kind, severity, source, statusCode, message, detail }, ... ]
    //   }
    std::wostringstream json;
    SYSTEMTIME utc{};
    GetSystemTime(&utc);
    wchar_t utcBuf[64];
    swprintf_s(utcBuf, L"%04u-%02u-%02uT%02u:%02u:%02uZ",
               utc.wYear, utc.wMonth, utc.wDay, utc.wHour, utc.wMinute, utc.wSecond);
    json << L"{\n"
         << L"  \"schema\": \"mcos.error-export.v1\",\n"
         << L"  \"exportedAtUtc\": \"" << utcBuf << L"\",\n"
         << L"  \"count\": " << lastErrorEvents_.size() << L",\n"
         << L"  \"events\": [";
    bool first = true;
    for (const auto& err : lastErrorEvents_) {
        if (!first) json << L",";
        first = false;
        json << L"\n    {"
             << L"\"id\":\"" << escapeJsonW(err.id) << L"\","
             << L"\"timestampUtc\":\"" << escapeJsonW(err.timestampUtc) << L"\","
             << L"\"kind\":\"" << escapeJsonW(err.kind) << L"\","
             << L"\"severity\":\"" << escapeJsonW(err.severity) << L"\","
             << L"\"source\":\"" << escapeJsonW(err.source) << L"\","
             << L"\"statusCode\":" << err.statusCode << L","
             << L"\"message\":\"" << escapeJsonW(err.message) << L"\","
             << L"\"detail\":\"" << escapeJsonW(err.detail) << L"\""
             << L"}";
    }
    json << L"\n  ]\n}\n";

    const std::string utf8 = wideToUtf8Local(json.str());
    HANDLE h = CreateFileW(fullPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                           nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        ErrorExportStatusText().Text(winrt::hstring(L"Failed to open " + fullPath + L" for write."));
        return;
    }
    DWORD written = 0;
    WriteFile(h, utf8.data(), (DWORD)utf8.size(), &written, nullptr);
    CloseHandle(h);

    std::wstring statusMessage = L"Exported " + std::to_wstring(lastErrorEvents_.size())
        + L" error" + (lastErrorEvents_.size() == 1 ? L"" : L"s")
        + L" to " + fullPath;
    ErrorExportStatusText().Text(winrt::hstring(statusMessage));
}

} // namespace winrt::MasterControlShell::implementation
