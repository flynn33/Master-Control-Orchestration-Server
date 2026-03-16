// Master Control Program
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#pragma once

#include "MainWindow.g.h"
#include "pch.h"

#include "ShellRuntime.h"

namespace winrt::MasterControlShell::implementation {

struct MainWindow : MainWindowT<MainWindow> {
    MainWindow();

    void RootGrid_Loaded(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void RefreshButton_Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void StartServiceButton_Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void StopServiceButton_Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OpenDashboardButton_Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OpenConfigButton_Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OpenDataButton_Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

private:
    void ConfigureWindow();
    void ConfigureTimer();
    void ApplySnapshot(const ::MasterControlShell::ShellSnapshot& snapshot);
    void UpdateStatusBar(winrt::hstring const& message, Microsoft::UI::Xaml::Controls::InfoBarSeverity severity);

    winrt::Windows::Foundation::IAsyncAction RefreshAsync();
    winrt::Windows::Foundation::IAsyncAction RunServiceActionAsync(bool start);
    winrt::Windows::Foundation::IAsyncAction HandleOpenDashboardAsync();
    winrt::Windows::Foundation::IAsyncAction ShowDialogAsync(winrt::hstring const& title, winrt::hstring const& message);

    HWND windowHandle_ = nullptr;
    bool windowInitialized_ = false;
    ::MasterControlShell::ShellRuntime runtime_{};
    ::MasterControlShell::ShellSnapshot currentSnapshot_{};
    Microsoft::UI::Dispatching::DispatcherQueueTimer refreshTimer_{ nullptr };
    std::atomic_bool refreshInFlight_{ false };
};

} // namespace winrt::MasterControlShell::implementation

namespace winrt::MasterControlShell::factory_implementation {

struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow> {
};

} // namespace winrt::MasterControlShell::factory_implementation
