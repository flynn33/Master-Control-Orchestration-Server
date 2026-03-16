// Master Control Program
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

    if (snapshot.apiHealthy) {
        stream << L"The service is publishing a live inventory snapshot through the local admin API.";
    } else {
        stream << L"The desktop shell is waiting for the service and local admin API to provide a fresh live inventory.";
    }

    return stream.str();
}

inline std::wstring formatProvidersNarrative(const ShellSnapshot& snapshot) {
    std::wostringstream stream;
    stream << L"Provider routing currently exposes "
           << pluralize(snapshot.providerCount, L"connection", L"connections")
           << L". AI autonomy is "
           << (snapshot.aiAutonomyEnabled ? L"enabled" : L"disabled")
           << L", so operator intent remains "
           << (snapshot.aiAutonomyEnabled ? L"shared with automation." : L"anchored to explicit human control.");
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
    listView.Items().Clear();
    for (const auto& row : rows) {
        listView.Items().Append(winrt::box_value(row));
    }
}

} // namespace MasterControlShell::Presentation
