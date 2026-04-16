// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.
//
// SetupWizardBuilder: builds the first-run setup wizard UI entirely in C++.
// No MIDL/IDL registration, no XAML compilation — just programmatic WinUI
// controls hosted via the existing SectionContentHost ContentPresenter.
// This approach is deliberately fragility-free: the only dependency is the
// WinUI FrameworkElement API and the app-level styles from App.xaml.

#pragma once

#include "ShellRuntime.h"

#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>

#include <functional>
#include <string>

namespace MasterControlShell {

// Callback for actions the wizard needs from the main window.
struct SetupWizardCallbacks {
    std::function<void(const std::wstring& destinationId)> navigateToDestination;
    std::function<void(const std::wstring& workflowId)> startGuidedWorkflow;
    std::function<void()> refreshData;
};

// Build the setup wizard entry screen (Guided / Manual / Import Existing).
// Returns a FrameworkElement that can be assigned to SectionContentHost.Content().
winrt::Microsoft::UI::Xaml::FrameworkElement BuildSetupWizardEntryView(
    const ShellSnapshot& snapshot,
    const SetupWizardCallbacks& callbacks);

// Build the setup readiness review view.
winrt::Microsoft::UI::Xaml::FrameworkElement BuildSetupReadinessView(
    const ShellSnapshot& snapshot,
    const SetupWizardCallbacks& callbacks);

} // namespace MasterControlShell
