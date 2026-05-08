// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#include "pch.h"

#include "OverviewSectionControl.xaml.h"

#if __has_include("OverviewSectionControl.g.cpp")
#include "OverviewSectionControl.g.cpp"
#endif

// v0.8.7: error-export needs Win32 file APIs + KnownFolders for the
// public documents path.
#include <KnownFolders.h>
#include <ShlObj.h>
#include <sstream>

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

    // v0.8.7 status grid + error reporting frame.
    ApplyApisAndServicesCard(snapshot);
    ApplySecurityStanceCard(snapshot);
    ApplyMcpServersCard(snapshot);
    ApplySubAgentsCard(snapshot);
    ApplyErrorReportingCard(snapshot);

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
    out << L"Service: " << serviceStateLabelOverview(snapshot.serviceState) << L'\n'
        << L"Admin API: " << (apiUp ? L"reachable" : L"offline") << L'\n'
        << L"Browser surface: " << (apiUp ? L"published" : L"unpublished") << L'\n'
        << L"Beacon: " << (snapshot.beaconEnabled ? L"on" : L"off")
        << L" : " << snapshot.beaconPort << L'\n'
        << L"Bind: " << (snapshot.bindAddress.empty() ? std::wstring(L"0.0.0.0") : snapshot.bindAddress)
        << L":" << snapshot.browserPort;
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

void OverviewSectionControl::ApplyMcpServersCard(const ::MasterControlShell::ShellSnapshot& snapshot) {
    const size_t total = snapshot.mcpServerRuntimeStats.size();
    int reachable = 0;
    for (const auto& s : snapshot.mcpServerRuntimeStats) {
        if (s.reachable) ++reachable;
    }
    const wchar_t* tone = (total == 0) ? L"neutral"
                       : (reachable == 0) ? L"crit"
                       : (reachable < (int)total) ? L"warn"
                       : L"good";
    paintDot(McpServersStatusDot(), tone);
    std::wostringstream head;
    head << total << L" registered ; " << reachable << L" reachable";
    McpServersHeadline().Text(winrt::hstring(head.str()));
    std::wostringstream body;
    if (total == 0) {
        body << L"No MCP servers registered.";
    } else {
        size_t shown = 0;
        for (const auto& s : snapshot.mcpServerRuntimeStats) {
            if (shown >= 4) break;
            body << (s.reachable ? L"+ " : L"- ")
                 << (s.displayName.empty() ? s.mcpServerId : s.displayName)
                 << L'\n';
            ++shown;
        }
        if (total > shown) {
            body << L"... +" << (total - shown) << L" more in the Runtime tab grid.";
        }
    }
    McpServersText().Text(winrt::hstring(body.str()));
}

void OverviewSectionControl::ApplySubAgentsCard(const ::MasterControlShell::ShellSnapshot& snapshot) {
    const size_t total = snapshot.subAgentRuntimeStats.size();
    int reachable = 0;
    int activeLeases = 0;
    for (const auto& s : snapshot.subAgentRuntimeStats) {
        if (s.reachable) ++reachable;
        activeLeases += s.activeLeaseCount;
    }
    const wchar_t* tone = (total == 0) ? L"neutral"
                       : (reachable == 0) ? L"crit"
                       : (reachable < (int)total) ? L"warn"
                       : L"good";
    paintDot(SubAgentsStatusDot(), tone);
    std::wostringstream head;
    head << total << L" registered ; " << reachable << L" reachable ; "
         << activeLeases << L" active lease" << (activeLeases == 1 ? L"" : L"s");
    SubAgentsHeadline().Text(winrt::hstring(head.str()));
    std::wostringstream body;
    if (total == 0) {
        body << L"No sub-agents registered.";
    } else {
        size_t shown = 0;
        for (const auto& s : snapshot.subAgentRuntimeStats) {
            if (shown >= 4) break;
            body << (s.reachable ? L"+ " : L"- ")
                 << (s.displayName.empty() ? s.subAgentId : s.displayName)
                 << L'\n';
            ++shown;
        }
        if (total > shown) {
            body << L"... +" << (total - shown) << L" more in the Runtime tab grid.";
        }
    }
    SubAgentsText().Text(winrt::hstring(body.str()));
}

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
