// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#include "pch.h"

#include "TelemetrySectionControl.xaml.h"

#if __has_include("TelemetrySectionControl.g.cpp")
#include "TelemetrySectionControl.g.cpp"
#endif

#include "ShellFormatting.h"

namespace winrt::MasterControlShell::implementation {

using namespace ::MasterControlShell::Presentation;

TelemetrySectionControl::TelemetrySectionControl() {
    InitializeComponent();
}

void TelemetrySectionControl::ApplySnapshot(const ::MasterControlShell::ShellSnapshot& snapshot) {
    CpuProgressBar().Value(snapshot.cpuPercent);
    MemoryProgressBar().Value(snapshot.memoryPercent);
    DiskProgressBar().Value(snapshot.diskPercent);
    CpuValueText().Text(winrt::hstring(formatPercent(snapshot.cpuPercent)));
    MemoryValueText().Text(winrt::hstring(formatPercent(snapshot.memoryPercent)));
    DiskValueText().Text(winrt::hstring(formatPercent(snapshot.diskPercent)));
    TrafficValueText().Text(winrt::hstring(formatTraffic(snapshot.bytesSentPerSecond, snapshot.bytesReceivedPerSecond)));
    TelemetrySummaryText().Text(winrt::hstring(snapshot.telemetryText));

    EnvironmentNameText().Text(winrt::hstring(snapshot.environmentName));
    HostNameText().Text(winrt::hstring(snapshot.hostName));
    OperatingSystemText().Text(winrt::hstring(snapshot.operatingSystem));
    PrimaryIpText().Text(winrt::hstring(snapshot.primaryIpAddress));
    PrimaryMacText().Text(winrt::hstring(snapshot.primaryMacAddress));
    EnvironmentNarrativeText().Text(winrt::hstring(snapshot.environmentText));
}

} // namespace winrt::MasterControlShell::implementation
