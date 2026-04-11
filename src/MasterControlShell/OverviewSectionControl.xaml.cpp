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

void OverviewSectionControl::ApplySnapshot(const ::MasterControlShell::ShellSnapshot& snapshot) {
    OverviewTextBlock().Text(winrt::hstring(snapshot.overviewText));
    EnvironmentNarrativeText().Text(winrt::hstring(snapshot.environmentText));
    ConfigurationNarrativeText().Text(winrt::hstring(snapshot.configurationText));
    OverviewStatusText().Text(snapshot.apiHealthy
        ? L"ADMIN API ONLINE · SYNCHRONIZED"
        : L"ADMIN API OFFLINE · CACHED STATE");
}

} // namespace winrt::MasterControlShell::implementation
