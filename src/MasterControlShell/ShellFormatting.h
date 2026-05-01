// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#pragma once

#include "ShellRuntime.h"

namespace MasterControlShell::Presentation {

inline std::wstring boolLabel(const bool value) {
    return value ? L"Enabled" : L"Disabled";
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
    std::wostringstream stream;
    stream << L"Security protocols are "
           << (snapshot.securityProtocolsEnabled ? L"enabled" : L"disabled")
           << L", open LAN access is "
           << (snapshot.openLanAccess ? L"enabled" : L"disabled")
           << L", and the service binds on "
           << snapshot.bindAddress
           << L':'
           << snapshot.browserPort
           << L'.';
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
