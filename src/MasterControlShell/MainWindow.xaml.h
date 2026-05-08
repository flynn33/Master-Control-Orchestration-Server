// Master Control Orchestration Server
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
    void ShellNavigation_SelectionChanged(
        Windows::Foundation::IInspectable const&,
        Microsoft::UI::Xaml::Controls::NavigationViewSelectionChangedEventArgs const&);
    void RefreshButton_Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void StartServiceButton_Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void StopServiceButton_Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OpenDashboardButton_Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OpenConfigButton_Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OpenDataButton_Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void GuidedMcpWizardButton_Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void GuidedSubAgentWizardButton_Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void GuidedSubAgentGroupWizardButton_Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void GuidedAppleHostWizardButton_Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void GuidedForsettiModuleWizardButton_Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void GuidedImportWizardButton_Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void GuidedSecurityWizardButton_Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void GuidedSettingsWizardButton_Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void GuidedRuntimeMaintenanceWizardButton_Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void SurfaceToolbarButton_Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    // v0.8.1: SizeChanged handler must be public so the auto-generated
    // XAML Connect() can wire it. Keep BuildShellGridBackdrop private
    // since it's only called internally.
    void ShellGridCanvas_SizeChanged(Windows::Foundation::IInspectable const&,
                                     Microsoft::UI::Xaml::SizeChangedEventArgs const&);

private:
    void ConfigureWindow();
    void ConfigureCustomTitleBar();
    void ConfigureTimer();
    void EnsureBootstrapSurface(::MasterControlShell::ShellSnapshot& snapshot);
    void SetCurrentDestination(std::wstring const& destinationId);
    void ApplySnapshot(const ::MasterControlShell::ShellSnapshot& snapshot);
    void ApplyLiveSnapshotFragment(const ::MasterControlShell::ShellSnapshot& snapshot);
    void ApplySurfaceNavigation(const ::MasterControlShell::ShellSnapshot& snapshot);
    void ApplySurfaceToolbar(const ::MasterControlShell::ShellSnapshot& snapshot);
    void ApplySectionMetadata(const ::MasterControlShell::ShellSnapshot& snapshot);
    void ApplyCachedSectionSnapshots(const ::MasterControlShell::ShellSnapshot& snapshot);
    void ApplyHeroSnapshot(const ::MasterControlShell::ShellSnapshot& snapshot);
    // v0.7.8: rebuild the SUB-AGENT GRID footer (cross-tab persistent footer
    // row in MainWindow.xaml) imperatively from ShellSnapshot.subAgentRuntimeStats
    // so each badge shows real per-sub-agent telemetry (utilization bar +
    // active-client IPs) instead of the pre-v0.7.8 hardcoded stub names.
    void ApplySubAgentFooter(const ::MasterControlShell::ShellSnapshot& snapshot);
    // v0.8.1: build the Tron grid backdrop for the shell window. Called
    // on Loaded and on every SizeChanged so the grid always covers the
    // current window size.
    void BuildShellGridBackdrop();
    void ApplyCurrentSectionSnapshot(const ::MasterControlShell::ShellSnapshot& snapshot);
    void ApplyLiveHeartbeat(std::chrono::system_clock::time_point now, bool captured);
    void StartGuidedWorkflow(std::wstring const& workflowId);
    Microsoft::UI::Xaml::FrameworkElement ResolvePrimaryViewForDestination(
        std::wstring const& destinationId,
        const ::MasterControlShell::ShellSnapshot& snapshot);
    Microsoft::UI::Xaml::FrameworkElement CreateViewForViewId(std::wstring const& viewId, bool cacheable);
    Microsoft::UI::Xaml::FrameworkElement CreateUnavailableView(
        winrt::hstring const& title,
        winrt::hstring const& message);
    void UpdateStatusBar(winrt::hstring const& message, Microsoft::UI::Xaml::Controls::InfoBarSeverity severity);

    winrt::Windows::Foundation::IAsyncAction RefreshAsync();
    winrt::Windows::Foundation::IAsyncAction RefreshLiveAsync();
    winrt::Windows::Foundation::IAsyncAction RunServiceActionAsync(bool start);
    winrt::Windows::Foundation::IAsyncAction HandleOpenDashboardAsync();
    winrt::Windows::Foundation::IAsyncAction ShowMcpServerWizardAsync();
    winrt::Windows::Foundation::IAsyncAction ShowSubAgentWizardAsync();
    winrt::Windows::Foundation::IAsyncAction ShowSubAgentGroupWizardAsync();
    winrt::Windows::Foundation::IAsyncAction ShowAppleHostWizardAsync();
    winrt::Windows::Foundation::IAsyncAction ShowForsettiModuleWizardAsync();
    winrt::Windows::Foundation::IAsyncAction ShowImportWizardAsync();
    winrt::Windows::Foundation::IAsyncAction ShowSecurityWizardAsync();
    winrt::Windows::Foundation::IAsyncAction ShowSettingsWizardAsync();
    winrt::Windows::Foundation::IAsyncAction ShowRuntimeMaintenanceWizardAsync();
    winrt::Windows::Foundation::IAsyncAction OpenOverlayRouteAsync(std::wstring routeId);
    winrt::Windows::Foundation::IAsyncAction CompleteGuidedWorkflowAsync(
        winrt::hstring const& message,
        std::wstring const& destinationId);
    winrt::Windows::Foundation::IAsyncAction ShowDialogAsync(winrt::hstring const& title, winrt::hstring const& message);

    HWND windowHandle_ = nullptr;
    bool windowInitialized_ = false;
    ::MasterControlShell::ShellRuntime runtime_{};
    ::MasterControlShell::ShellSnapshot currentSnapshot_{};
    std::wstring currentDestination_ = L"overview";
    bool firstRunWizardDismissed_ = false;
    std::map<std::wstring, Microsoft::UI::Xaml::FrameworkElement> cachedViews_;
    winrt::Windows::Foundation::IAsyncAction PollActivityStreamAsync();

    Microsoft::UI::Dispatching::DispatcherQueueTimer refreshTimer_{ nullptr };
    Microsoft::UI::Dispatching::DispatcherQueueTimer clockTimer_{ nullptr };
    Microsoft::UI::Dispatching::DispatcherQueueTimer activityStreamTimer_{ nullptr };
    std::wstring activityStreamCursor_;
    size_t activityStreamCount_ = 0;
    std::atomic_bool refreshInFlight_{ false };
    std::atomic_bool liveRefreshInFlight_{ false };
    // Monotonically increasing counter — bumped on every live-tick (whether
    // the snapshot capture succeeded or not) so the hero's visible
    // "LIVE #N" indicator changes every second, giving unmistakable
    // feedback that telemetry is updating.
    uint64_t liveSampleCounter_{ 0 };
    // Signature caches so the timed refresh doesn't Clear+Rebuild the
    // navigation or toolbar when nothing actually changed. This is what
    // made the shell feel like the "entire page" was refreshing every
    // cycle even though only the telemetry numbers were supposed to.
    std::wstring lastNavigationSignature_;
    std::wstring lastToolbarSignature_;
};

} // namespace winrt::MasterControlShell::implementation

namespace winrt::MasterControlShell::factory_implementation {

struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow> {
};

} // namespace winrt::MasterControlShell::factory_implementation
