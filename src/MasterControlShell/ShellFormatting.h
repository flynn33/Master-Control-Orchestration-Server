// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#pragma once

#include "ShellRuntime.h"

namespace MasterControlShell::Presentation {

inline std::wstring boolLabel(const bool value) {
    return value ? L"Enabled" : L"Disabled";
}

// v0.9.76: Bind-address display helper. Mirrors the v0.9.74 browser
// dashboard's `resolveDisplayUrl` substitution: when the configured
// bindAddress is a wildcard (0.0.0.0 / :: / [::]) or empty, render
// the LAN-routable primary IP instead so every Shell card surfaces an
// address an actual client can connect to. Editor fields (Settings
// surface, Setup Wizard) stay on the raw stored value so maintainer
// edits round-trip; display-only cards route through this helper.
inline std::wstring resolveDisplayBindAddress(const std::wstring& bindAddress,
                                              const std::wstring& lanIp) {
    if (bindAddress.empty()
        || bindAddress == L"0.0.0.0"
        || bindAddress == L"::"
        || bindAddress == L"[::]"
        || bindAddress == L"::0"
        || bindAddress == L"[::0]") {
        return lanIp.empty() ? std::wstring(L"127.0.0.1") : lanIp;
    }
    return bindAddress;
}

// v0.10.12: substitute the LAN-routable primary IP into an MCP gateway
// URL whose host segment is a wildcard. Mirrors the substituteWildcardHostInUrl
// helper in the C++ runtime that powers /api/health/summary's mcpUrl. We
// don't try to URL-parse — the runtime emits a stable shape (scheme://host:port/path),
// so a straight strchr is enough. Empty input returns empty. Non-wildcard
// hosts are returned unchanged.
inline std::wstring substituteWildcardInGatewayUrl(const std::wstring& rawUrl,
                                                   const std::wstring& lanIp) {
    if (rawUrl.empty()) return rawUrl;
    const auto schemeEnd = rawUrl.find(L"://");
    if (schemeEnd == std::wstring::npos) return rawUrl;
    const auto hostStart = schemeEnd + 3;
    auto hostEnd = rawUrl.find_first_of(L":/?", hostStart);
    if (hostEnd == std::wstring::npos) hostEnd = rawUrl.size();
    const auto host = rawUrl.substr(hostStart, hostEnd - hostStart);
    const bool wildcard = host.empty()
        || host == L"0.0.0.0"
        || host == L"::"
        || host == L"[::]"
        || host == L"::0"
        || host == L"[::0]";
    if (!wildcard) return rawUrl;
    const std::wstring replacement = lanIp.empty() ? std::wstring(L"127.0.0.1") : lanIp;
    return rawUrl.substr(0, hostStart) + replacement + rawUrl.substr(hostEnd);
}

inline bool isWildcardBindAddress(const std::wstring& bindAddress) {
    return bindAddress.empty()
        || bindAddress == L"0.0.0.0"
        || bindAddress == L"::"
        || bindAddress == L"[::]"
        || bindAddress == L"::0"
        || bindAddress == L"[::0]";
}

// std::string overload — same canonical wildcard set, for callers that
// hold a UTF-8 narrow string (e.g., ShellRuntime::CaptureSnapshot, where
// bindAddress comes out of JSON parsing as std::string before being
// converted to wstring for the snapshot). Both overloads MUST agree on
// the wildcard set; if either grows a new sentinel, the other must too.
inline bool isWildcardBindAddress(const std::string& bindAddress) {
    return bindAddress.empty()
        || bindAddress == "0.0.0.0"
        || bindAddress == "::"
        || bindAddress == "[::]"
        || bindAddress == "::0"
        || bindAddress == "[::0]";
}

// v0.9.90: relative-time formatter. Parses an ISO-8601 UTC stamp (e.g.
// "2026-05-11T00:57:59Z") into a system_clock time_point, computes now
// - that, and renders the duration in maintainer-friendly buckets:
//
//   <60s        -> "Xs ago"
//   <60m        -> "Xm Ys ago" (or "Xm ago" when seconds == 0)
//   <24h        -> "Xh Ym ago" (or "Xh ago" when minutes == 0)
//   >=24h       -> "Xd Yh ago" (or "Xd ago" when hours == 0)
//
// Returns empty string when the input doesn't parse so callers can
// fall back to the raw UTC stamp. Used by the WinUI Shell Supervisor
// card "Last heartbeat" line + the Telemetry sidebar Supervisor card.
inline std::wstring formatRelativeUtcTime(const std::wstring& isoStampUtc) {
    if (isoStampUtc.empty()) return std::wstring{};
    std::tm tm{};
    std::wistringstream in(isoStampUtc);
    in >> std::get_time(&tm, L"%Y-%m-%dT%H:%M:%SZ");
    if (in.fail()) return std::wstring{};
#if defined(_WIN32)
    const auto stampTime = _mkgmtime(&tm);
#else
    const auto stampTime = timegm(&tm);
#endif
    if (stampTime == static_cast<std::time_t>(-1)) return std::wstring{};
    const auto nowTime = std::chrono::system_clock::to_time_t(
        std::chrono::system_clock::now());
    const auto deltaSeconds = static_cast<int64_t>(nowTime - stampTime);
    if (deltaSeconds < 0) {
        // Timestamp is in the future (clock skew or freshly-stamped
        // value the parser rounded down). Treat as "just now" instead
        // of "-3s ago" which looks wrong to the maintainer.
        return std::wstring(L"just now");
    }
    if (deltaSeconds < 60) {
        return std::to_wstring(deltaSeconds) + L"s ago";
    }
    if (deltaSeconds < 3600) {
        const int64_t minutes = deltaSeconds / 60;
        const int64_t secs = deltaSeconds % 60;
        if (secs == 0) return std::to_wstring(minutes) + L"m ago";
        return std::to_wstring(minutes) + L"m " + std::to_wstring(secs) + L"s ago";
    }
    if (deltaSeconds < 86400) {
        const int64_t hours = deltaSeconds / 3600;
        const int64_t mins = (deltaSeconds % 3600) / 60;
        if (mins == 0) return std::to_wstring(hours) + L"h ago";
        return std::to_wstring(hours) + L"h " + std::to_wstring(mins) + L"m ago";
    }
    const int64_t days = deltaSeconds / 86400;
    const int64_t hours = (deltaSeconds % 86400) / 3600;
    if (hours == 0) return std::to_wstring(days) + L"d ago";
    return std::to_wstring(days) + L"d " + std::to_wstring(hours) + L"h ago";
}

// v0.9.94: future-direction relative-time formatter. Parses an ISO-8601
// UTC stamp expected to be in the future; computes (stamp - now);
// renders as 'in Xs' / 'in Xm Ys' / 'in Xh Ym' / 'in Xd Yh' so callers
// can render expiration phrases like "Expires in 2h 14m". Past stamps
// render as 'expired Xs ago' (using formatRelativeUtcTime under the
// hood) so a clock-skewed or already-elapsed timestamp doesn't display
// as 'in 0s' or a negative duration. Empty / parse-failure inputs
// return empty wstring.
inline std::wstring formatFutureUtcTime(const std::wstring& isoStampUtc) {
    if (isoStampUtc.empty()) return std::wstring{};
    std::tm tm{};
    std::wistringstream in(isoStampUtc);
    in >> std::get_time(&tm, L"%Y-%m-%dT%H:%M:%SZ");
    if (in.fail()) return std::wstring{};
#if defined(_WIN32)
    const auto stampTime = _mkgmtime(&tm);
#else
    const auto stampTime = timegm(&tm);
#endif
    if (stampTime == static_cast<std::time_t>(-1)) return std::wstring{};
    const auto nowTime = std::chrono::system_clock::to_time_t(
        std::chrono::system_clock::now());
    const auto deltaSeconds = static_cast<int64_t>(stampTime - nowTime);
    if (deltaSeconds <= 0) {
        // Past timestamp — surface as 'expired X ago' so the maintainer
        // sees the timestamp is no longer ahead of now.
        const auto since = formatRelativeUtcTime(isoStampUtc);
        return since.empty() ? std::wstring(L"expired") : (L"expired " + since);
    }
    if (deltaSeconds < 60) {
        return L"in " + std::to_wstring(deltaSeconds) + L"s";
    }
    if (deltaSeconds < 3600) {
        const int64_t minutes = deltaSeconds / 60;
        const int64_t secs = deltaSeconds % 60;
        if (secs == 0) return L"in " + std::to_wstring(minutes) + L"m";
        return L"in " + std::to_wstring(minutes) + L"m " + std::to_wstring(secs) + L"s";
    }
    if (deltaSeconds < 86400) {
        const int64_t hours = deltaSeconds / 3600;
        const int64_t mins = (deltaSeconds % 3600) / 60;
        if (mins == 0) return L"in " + std::to_wstring(hours) + L"h";
        return L"in " + std::to_wstring(hours) + L"h " + std::to_wstring(mins) + L"m";
    }
    const int64_t days = deltaSeconds / 86400;
    const int64_t hours = (deltaSeconds % 86400) / 3600;
    if (hours == 0) return L"in " + std::to_wstring(days) + L"d";
    return L"in " + std::to_wstring(days) + L"d " + std::to_wstring(hours) + L"h";
}

// v0.10.12: ISO-UTC -> host-local HH:MM:SS converter for the activity-stream
// "Live Command Stream" log. Pre-v0.10.12 the renderer just substr'd the
// ISO stamp's "HH:MM:SS" segment, which surfaced UTC-time rows alongside the
// title-bar's GetLocalTime clock and produced an apparent offset (5h on a
// CST host) that maintainers read as "the live log is displaying a different
// time from the server time". Returns empty string on parse failure so the
// caller can fall back to the raw stamp segment.
inline std::wstring formatLocalClockFromIsoUtc(const std::wstring& isoStampUtc) {
    if (isoStampUtc.size() < 19 || isoStampUtc[10] != L'T') return std::wstring{};
    std::tm tm{};
    std::wistringstream in(isoStampUtc);
    in >> std::get_time(&tm, L"%Y-%m-%dT%H:%M:%S");
    if (in.fail()) return std::wstring{};
#if defined(_WIN32)
    const auto stampTime = _mkgmtime(&tm);
#else
    const auto stampTime = timegm(&tm);
#endif
    if (stampTime == static_cast<std::time_t>(-1)) return std::wstring{};
    std::tm local{};
#if defined(_WIN32)
    if (localtime_s(&local, &stampTime) != 0) return std::wstring{};
#else
    if (localtime_r(&stampTime, &local) == nullptr) return std::wstring{};
#endif
    wchar_t buffer[16]{};
    swprintf_s(buffer, L"%02d:%02d:%02d",
               local.tm_hour, local.tm_min, local.tm_sec);
    return std::wstring(buffer);
}

inline std::wstring formatPercent(const double value) {
    std::wostringstream stream;
    stream.setf(std::ios::fixed);
    stream.precision(1);
    stream << value << L'%';
    return stream.str();
}

inline std::wstring formatTraffic(const uint64_t txBytesPerSecond, const uint64_t rxBytesPerSecond) {
    std::wostringstream stream;
    stream << L"TX " << txBytesPerSecond << L" B/s  |  RX " << rxBytesPerSecond << L" B/s";
    return stream.str();
}

inline std::wstring formatResourceEnvelope(const ShellSnapshot& snapshot) {
    std::wostringstream stream;
    stream << L"CPU " << snapshot.cpuAllocationPercent
           << L"%  |  RAM " << snapshot.memoryAllocationPercent
           << L"%  |  Bandwidth " << snapshot.bandwidthAllocationPercent
           << L"%  |  Storage " << snapshot.storageAllocationPercent << L'%';
    return stream.str();
}

inline std::wstring pluralize(const size_t count, const wchar_t* singular, const wchar_t* plural) {
    std::wostringstream stream;
    stream << count << L' ' << (count == 1 ? singular : plural);
    return stream.str();
}

inline std::wstring formatRuntimeNarrative(const ShellSnapshot& snapshot) {
    std::wostringstream stream;
    stream << L"The runtime inventory currently tracks "
           << pluralize(snapshot.endpointCount, L"lane", L"lanes")
           << L" across the local host and gateway profile. ";

    if (snapshot.platformGatewayCount != 0 || snapshot.appleRemoteHostCount != 0) {
        stream << L"The Apple production plane currently exposes "
               << pluralize(snapshot.platformGatewayCount, L"gateway lane", L"gateway lanes")
               << L" and "
               << pluralize(snapshot.appleRemoteHostCount, L"remote host", L"remote hosts")
               << L". ";
    }

    if (snapshot.apiHealthy) {
        stream << L"The service is publishing a live inventory snapshot through the local admin API.";
    } else {
        stream << L"The desktop shell is waiting for the service and local admin API to provide a fresh live inventory.";
    }

    return stream.str();
}

inline std::wstring formatImportsNarrative(const ShellSnapshot& snapshot) {
    std::wostringstream stream;
    stream << L"The import ledger currently shows "
           << pluralize(snapshot.installCount, L"record", L"records")
           << L". Use this lane to validate provenance after MSI, EXE, PS1, repo, or zip onboarding events.";
    return stream.str();
}

inline std::wstring formatExportsNarrative(const ShellSnapshot& snapshot) {
    std::wostringstream stream;
    stream << L"Export publishing currently tracks "
           << pluralize(snapshot.exportCount, L"artifact", L"artifacts")
           << L". The browser and agent handoff surface is anchored at "
           << snapshot.dashboardUrl << L'.';
    return stream.str();
}

inline std::wstring formatSecurityNarrative(const ShellSnapshot& snapshot) {
    const auto resolved = resolveDisplayBindAddress(snapshot.bindAddress,
                                                    snapshot.primaryIpAddress);
    std::wostringstream stream;
    stream << L"Security protocols are "
           << (snapshot.securityProtocolsEnabled ? L"enabled" : L"disabled")
           << L", open LAN access is "
           << (snapshot.openLanAccess ? L"enabled" : L"disabled")
           << L", and the service binds on "
           << resolved
           << L':'
           << snapshot.browserPort;
    if (isWildcardBindAddress(snapshot.bindAddress)) {
        stream << L" (configured "
               << (snapshot.bindAddress.empty() ? std::wstring(L"all interfaces") : snapshot.bindAddress)
               << L"; resolved to LAN address)";
    }
    stream << L'.';
    return stream.str();
}

inline void populateListView(const winrt::Microsoft::UI::Xaml::Controls::ListView& listView,
                             const std::vector<std::wstring>& rows) {
    // Diffed update: if the existing items already match `rows` one-for-one,
    // skip the Clear/Append storm entirely. ListView.Items().Clear()+Append
    // on a 30-row list at 1Hz was the single biggest cause of the shell
    // "feeling sluggish" — each mutation drops virtualization state, dirties
    // layout, and schedules another render pass. Short-circuiting when
    // nothing changed makes the timed refresh near-free for static sections.
    const auto items = listView.Items();
    bool identical = (items.Size() == rows.size());
    if (identical) {
        for (uint32_t i = 0; i < items.Size(); ++i) {
            if (const auto existing = items.GetAt(i).try_as<winrt::hstring>();
                existing.has_value()) {
                if (std::wstring(existing->c_str()) != rows[i]) { identical = false; break; }
            } else {
                identical = false; break;
            }
        }
    }
    if (identical) { return; }

    // Prefer in-place updates over Clear+Append when the lengths match
    // (common when one row's text changed but nothing was added/removed).
    if (items.Size() == rows.size()) {
        for (uint32_t i = 0; i < items.Size(); ++i) {
            items.SetAt(i, winrt::box_value(rows[i]));
        }
        return;
    }

    items.Clear();
    for (const auto& row : rows) {
        items.Append(winrt::box_value(row));
    }
}

} // namespace MasterControlShell::Presentation
