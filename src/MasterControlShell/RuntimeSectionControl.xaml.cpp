// Master Control Program
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#include "pch.h"

#include "RuntimeSectionControl.xaml.h"

#if __has_include("RuntimeSectionControl.g.cpp")
#include "RuntimeSectionControl.g.cpp"
#endif

#include "ShellFormatting.h"

namespace winrt::MasterControlShell::implementation {

using namespace ::MasterControlShell::Presentation;

RuntimeSectionControl::RuntimeSectionControl() {
    InitializeComponent();
}

void RuntimeSectionControl::ApplySnapshot(const ::MasterControlShell::ShellSnapshot& snapshot) {
    RuntimeCountText().Text(winrt::hstring(std::to_wstring(snapshot.endpointCount)));
    RuntimeNarrativeText().Text(winrt::hstring(formatRuntimeNarrative(snapshot)));
    populateListView(EndpointsListView(), snapshot.endpointRows);
}

} // namespace winrt::MasterControlShell::implementation
