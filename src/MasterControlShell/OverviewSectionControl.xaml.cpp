// Master Control Program
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
        ? L"Admin API reachable. The desktop shell is synchronized with the local service snapshot."
        : L"Admin API offline. The shell is holding the most recent cached state until the service responds.");
}

} // namespace winrt::MasterControlShell::implementation
