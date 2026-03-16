// Master Control Program
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#include "pch.h"

#include "SettingsSectionControl.xaml.h"

#if __has_include("SettingsSectionControl.g.cpp")
#include "SettingsSectionControl.g.cpp"
#endif

#include "ShellFormatting.h"

namespace winrt::MasterControlShell::implementation {

using namespace ::MasterControlShell::Presentation;

SettingsSectionControl::SettingsSectionControl() {
    InitializeComponent();
}

void SettingsSectionControl::ApplySnapshot(const ::MasterControlShell::ShellSnapshot& snapshot) {
    ResourceEnvelopeText().Text(winrt::hstring(formatResourceEnvelope(snapshot)));
    ConfigPathText().Text(winrt::hstring(snapshot.configPath));
    DataDirectoryText().Text(winrt::hstring(snapshot.dataDirectory));
    ConfigurationNarrativeText().Text(winrt::hstring(snapshot.configurationText));
}

} // namespace winrt::MasterControlShell::implementation
