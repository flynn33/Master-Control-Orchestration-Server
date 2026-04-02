// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#pragma once

#include "App.xaml.g.h"

namespace winrt::MasterControlShell::implementation {

struct App : AppT<App> {
    App();

    void OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&);

private:
    winrt::Microsoft::UI::Xaml::Window window_{ nullptr };
};

} // namespace winrt::MasterControlShell::implementation
