// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#include "pch.h"

#include "MainWindow.xaml.h"

#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include "CommandLogicUnitSectionControl.xaml.h"
#include "ExportsSectionControl.xaml.h"
#include "ImportsSectionControl.xaml.h"
#include "microsoft.ui.xaml.window.h"
#include "OverviewSectionControl.xaml.h"
#include "ProvidersSectionControl.xaml.h"
#include "RuntimeSectionControl.xaml.h"
#include "SecuritySectionControl.xaml.h"
#include "SettingsSectionControl.xaml.h"
#include "TelemetrySectionControl.xaml.h"

#include <winrt/Microsoft.UI.Composition.SystemBackdrops.h>

namespace winrt::MasterControlShell::implementation {

using namespace Microsoft::UI::Dispatching;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;
using namespace Windows::Foundation;
using namespace Windows::UI;

namespace {

constexpr wchar_t kProductDisplayName[] = L"Master Control Orchestration Server";
constexpr wchar_t kOverviewDestination[] = L"overview";
constexpr wchar_t kTelemetryDestination[] = L"telemetry";
constexpr wchar_t kRuntimeDestination[] = L"runtime";
constexpr wchar_t kCluDestination[] = L"clu";
constexpr wchar_t kProvidersDestination[] = L"providers";
constexpr wchar_t kImportsDestination[] = L"imports";
constexpr wchar_t kExportsDestination[] = L"exports";
constexpr wchar_t kSecurityDestination[] = L"security";
constexpr wchar_t kSettingsDestination[] = L"settings";

constexpr wchar_t kOverviewView[] = L"OverviewSectionView";
constexpr wchar_t kTelemetryView[] = L"TelemetrySectionView";
constexpr wchar_t kRuntimeView[] = L"RuntimeSectionView";
constexpr wchar_t kCluView[] = L"CommandLogicUnitSectionView";
constexpr wchar_t kProvidersView[] = L"ProvidersSectionView";
constexpr wchar_t kImportsView[] = L"ImportsSectionView";
constexpr wchar_t kExportsView[] = L"ExportsSectionView";
constexpr wchar_t kSecurityView[] = L"SecuritySectionView";
constexpr wchar_t kSettingsView[] = L"SettingsSectionView";

void writeShellLog(const std::wstring& message) {
    try {
        const auto logPath = std::filesystem::temp_directory_path() / L"MasterControlShell-startup.log";
        std::wofstream stream(logPath, std::ios::app);
        if (!stream.is_open()) {
            return;
        }

        const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        tm localTime{};
        localtime_s(&localTime, &now);

        stream << std::put_time(&localTime, L"%Y-%m-%d %H:%M:%S") << L"  " << message << std::endl;
    } catch (...) {
    }
}

Color makeColor(const uint8_t alpha, const uint8_t red, const uint8_t green, const uint8_t blue) {
    Color color{};
    color.A = alpha;
    color.R = red;
    color.G = green;
    color.B = blue;
    return color;
}

std::wstring serviceStateLabel(const ::MasterControlShell::ServiceState state) {
    switch (state) {
        case ::MasterControlShell::ServiceState::Missing: return L"Missing";
        case ::MasterControlShell::ServiceState::Stopped: return L"Stopped";
        case ::MasterControlShell::ServiceState::StartPending: return L"Starting";
        case ::MasterControlShell::ServiceState::StopPending: return L"Stopping";
        case ::MasterControlShell::ServiceState::Running: return L"Running";
        case ::MasterControlShell::ServiceState::Paused: return L"Paused";
        default: return L"Unknown";
    }
}

std::wstring uppercase(const std::wstring& input) {
    std::wstring output = input;
    std::transform(output.begin(), output.end(), output.begin(), ::towupper);
    return output;
}

std::wstring trimCopy(const std::wstring& value) {
    auto begin = value.begin();
    while (begin != value.end() && iswspace(*begin) != 0) {
        ++begin;
    }

    auto end = value.end();
    while (end != begin && iswspace(*(end - 1)) != 0) {
        --end;
    }

    return std::wstring(begin, end);
}

void setElementVisibility(const UIElement& element, const bool visible) {
    element.Visibility(visible ? Visibility::Visible : Visibility::Collapsed);
}

struct SectionMetadata final {
    std::wstring eyebrow;
    std::wstring title;
    std::wstring description;
};

std::wstring labelForDestination(const std::wstring& destinationId) {
    if (destinationId == kTelemetryDestination) {
        return L"Telemetry";
    }
    if (destinationId == kRuntimeDestination) {
        return L"Runtime";
    }
    if (destinationId == kCluDestination) {
        return L"Command Logic Unit";
    }
    if (destinationId == kProvidersDestination) {
        return L"Providers";
    }
    if (destinationId == kImportsDestination) {
        return L"Imports";
    }
    if (destinationId == kExportsDestination) {
        return L"Exports";
    }
    if (destinationId == kSecurityDestination) {
        return L"Security";
    }
    if (destinationId == kSettingsDestination) {
        return L"Settings";
    }
    return L"Overview";
}

std::wstring destinationForViewId(const std::wstring& viewId) {
    if (viewId == kTelemetryView) {
        return kTelemetryDestination;
    }
    if (viewId == kRuntimeView) {
        return kRuntimeDestination;
    }
    if (viewId == kCluView) {
        return kCluDestination;
    }
    if (viewId == kProvidersView) {
        return kProvidersDestination;
    }
    if (viewId == kImportsView) {
        return kImportsDestination;
    }
    if (viewId == kExportsView) {
        return kExportsDestination;
    }
    if (viewId == kSecurityView) {
        return kSecurityDestination;
    }
    if (viewId == kSettingsView) {
        return kSettingsDestination;
    }
    return kOverviewDestination;
}

SectionMetadata metadataForDestination(const std::wstring& destinationId, const std::wstring& title) {
    if (destinationId == kTelemetryDestination) {
        return { L"TELEMETRY", title, L"Track live CPU, memory, disk, network, and environment discovery details from the local service snapshot." };
    }
    if (destinationId == kRuntimeDestination) {
        return { L"RUNTIME", title, L"Inspect MCP runtime lanes, platform gateway inventory, Apple remote hosts, and the current operational map exposed by the service." };
    }
    if (destinationId == kCluDestination) {
        return { L"CLU", title, L"Inspect the Command Logic Unit governance profile, Apple production operations, and operator-visible control rules." };
    }
    if (destinationId == kProvidersDestination) {
        return { L"PROVIDERS", title, L"Review provider adapters, autonomous control posture, and the current agent service envelope." };
    }
    if (destinationId == kImportsDestination) {
        return { L"IMPORTS", title, L"Audit installer provenance, trusted-source flows, and the current software onboarding trail." };
    }
    if (destinationId == kExportsDestination) {
        return { L"EXPORTS", title, L"Review exported agent artifacts, browser handoff endpoints, and downstream integration material." };
    }
    if (destinationId == kSecurityDestination) {
        return { L"SECURITY", title, L"Inspect bind policy, browser access posture, beacon state, AI autonomy, and operator-sensitive toggles." };
    }
    if (destinationId == kSettingsDestination) {
        return { L"SETTINGS", title, L"Trace configuration files, data paths, and the current resource envelope that shapes the host runtime." };
    }
    return { L"OVERVIEW", title, L"Forsetti surface navigation is hosting the current command deck instead of a hardcoded shell page map." };
}

std::vector<::MasterControlShell::ShellNavigationPointer> bootstrapNavigationPointers() {
    return {
        { L"overview-nav", L"Overview", kOverviewDestination },
        { L"telemetry-nav", L"Telemetry", kTelemetryDestination },
        { L"runtime-nav", L"Runtime", kRuntimeDestination },
        { L"clu-nav", L"CLU", kCluDestination },
        { L"providers-nav", L"Providers", kProvidersDestination },
        { L"imports-nav", L"Imports", kImportsDestination },
        { L"exports-nav", L"Exports", kExportsDestination },
        { L"security-nav", L"Security", kSecurityDestination },
        { L"settings-nav", L"Settings", kSettingsDestination }
    };
}

std::vector<::MasterControlShell::ShellToolbarItem> bootstrapToolbarItems() {
    return {
        { L"dashboard-home", L"Overview", L"network", ::MasterControlShell::ShellToolbarActionKind::Navigate, kOverviewDestination },
        { L"dashboard-telemetry", L"Telemetry", L"trackers", ::MasterControlShell::ShellToolbarActionKind::Navigate, kTelemetryDestination },
        { L"dashboard-runtime", L"Runtime", L"globe", ::MasterControlShell::ShellToolbarActionKind::Navigate, kRuntimeDestination },
        { L"dashboard-clu", L"CLU", L"shield", ::MasterControlShell::ShellToolbarActionKind::Navigate, kCluDestination },
        { L"dashboard-import", L"Imports", L"arrow.down", ::MasterControlShell::ShellToolbarActionKind::OpenOverlay, L"imports-overlay" },
        { L"dashboard-export", L"Exports", L"share", ::MasterControlShell::ShellToolbarActionKind::OpenOverlay, L"exports-overlay" },
        { L"dashboard-settings", L"Settings", L"gear", ::MasterControlShell::ShellToolbarActionKind::OpenOverlay, L"settings-overlay" }
    };
}

std::vector<::MasterControlShell::ShellOverlayRoute> bootstrapOverlayRoutes() {
    return {
        { L"settings-overlay", L"Settings", ::MasterControlShell::ShellOverlayPresentation::Sheet, true, L"", L"com.mastercontrol.dashboard-ui", kSettingsView },
        { L"imports-overlay", L"Imports", ::MasterControlShell::ShellOverlayPresentation::Sheet, true, L"", L"com.mastercontrol.dashboard-ui", kImportsView },
        { L"exports-overlay", L"Exports", ::MasterControlShell::ShellOverlayPresentation::Sheet, true, L"", L"com.mastercontrol.dashboard-ui", kExportsView },
        { L"security-overlay", L"Security", ::MasterControlShell::ShellOverlayPresentation::Sheet, true, L"", L"com.mastercontrol.dashboard-ui", kSecurityView }
    };
}

std::map<std::wstring, std::vector<::MasterControlShell::ShellViewInjection>> bootstrapViewInjectionsBySlot() {
    return {
        { kOverviewDestination, { { L"overview-surface", kOverviewDestination, kOverviewView, 100 } } },
        { kTelemetryDestination, { { L"telemetry-surface", kTelemetryDestination, kTelemetryView, 100 } } },
        { kRuntimeDestination, { { L"runtime-surface", kRuntimeDestination, kRuntimeView, 100 } } },
        { kCluDestination, { { L"clu-surface", kCluDestination, kCluView, 100 } } },
        { kProvidersDestination, { { L"providers-surface", kProvidersDestination, kProvidersView, 100 } } },
        { kImportsDestination, { { L"imports-surface", kImportsDestination, kImportsView, 100 } } },
        { kExportsDestination, { { L"exports-surface", kExportsDestination, kExportsView, 100 } } },
        { kSecurityDestination, { { L"security-surface", kSecurityDestination, kSecurityView, 100 } } },
        { kSettingsDestination, { { L"settings-surface", kSettingsDestination, kSettingsView, 100 } } }
    };
}

winrt::hstring glyphForSystemImageName(const std::wstring& systemImageName) {
    if (systemImageName == L"network") {
        return L"\uE968";
    }
    if (systemImageName == L"trackers") {
        return L"\uE9D2";
    }
    if (systemImageName == L"globe") {
        return L"\uE909";
    }
    if (systemImageName == L"shield") {
        return L"\uE72E";
    }
    if (systemImageName == L"arrow.down") {
        return L"\uE898";
    }
    if (systemImageName == L"share") {
        return L"\uE72D";
    }
    if (systemImageName == L"gear") {
        return L"\uE713";
    }
    return L"\uE8A5";
}

void setWindowSize(HWND windowHandle, const int width, const int height) {
    const UINT dpi = GetDpiForWindow(windowHandle);
    const float scale = static_cast<float>(dpi) / 96.0f;
    SetWindowPos(
        windowHandle,
        nullptr,
        0,
        0,
        static_cast<int>(width * scale),
        static_cast<int>(height * scale),
        SWP_NOMOVE | SWP_NOZORDER);
}

void centerWindow(HWND windowHandle) {
    RECT windowRect{};
    GetWindowRect(windowHandle, &windowRect);

    MONITORINFO monitorInfo{ sizeof(monitorInfo) };
    GetMonitorInfoW(MonitorFromRect(&windowRect, MONITOR_DEFAULTTONEAREST), &monitorInfo);

    const RECT workArea = monitorInfo.rcWork;
    const int width = windowRect.right - windowRect.left;
    const int height = windowRect.bottom - windowRect.top;

    const int x = workArea.left + ((workArea.right - workArea.left) - width) / 2;
    const int y = workArea.top + ((workArea.bottom - workArea.top) - height) / 2;
    SetWindowPos(windowHandle, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void applyBadge(const Border& border,
                const TextBlock& textBlock,
                const std::wstring& label,
                const Color background,
                const Color foreground,
                const Color edge) {
    border.Background(SolidColorBrush(background));
    border.BorderBrush(SolidColorBrush(edge));
    textBlock.Foreground(SolidColorBrush(foreground));
    textBlock.Text(winrt::hstring(label));
}

void attachInteractiveRuntime(const FrameworkElement& view,
                              const std::wstring& viewId,
                              ::MasterControlShell::ShellRuntime& runtime,
                              std::function<void()> refreshRequested) {
    if (viewId == kRuntimeView) {
        const auto typed = view.as<winrt::MasterControlShell::RuntimeSectionControl>();
        winrt::get_self<winrt::MasterControlShell::implementation::RuntimeSectionControl>(typed)->AttachRuntime(&runtime, std::move(refreshRequested));
        return;
    }
    if (viewId == kCluView) {
        const auto typed = view.as<winrt::MasterControlShell::CommandLogicUnitSectionControl>();
        winrt::get_self<winrt::MasterControlShell::implementation::CommandLogicUnitSectionControl>(typed)->AttachRuntime(&runtime, std::move(refreshRequested));
        return;
    }
    if (viewId == kProvidersView) {
        const auto typed = view.as<winrt::MasterControlShell::ProvidersSectionControl>();
        winrt::get_self<winrt::MasterControlShell::implementation::ProvidersSectionControl>(typed)->AttachRuntime(&runtime, std::move(refreshRequested));
        return;
    }
    if (viewId == kImportsView) {
        const auto typed = view.as<winrt::MasterControlShell::ImportsSectionControl>();
        winrt::get_self<winrt::MasterControlShell::implementation::ImportsSectionControl>(typed)->AttachRuntime(&runtime, std::move(refreshRequested));
        return;
    }
    if (viewId == kExportsView) {
        const auto typed = view.as<winrt::MasterControlShell::ExportsSectionControl>();
        winrt::get_self<winrt::MasterControlShell::implementation::ExportsSectionControl>(typed)->AttachRuntime(&runtime);
        return;
    }
    if (viewId == kSecurityView) {
        const auto typed = view.as<winrt::MasterControlShell::SecuritySectionControl>();
        winrt::get_self<winrt::MasterControlShell::implementation::SecuritySectionControl>(typed)->AttachRuntime(&runtime, std::move(refreshRequested));
    }
}

void applySnapshotToView(const FrameworkElement& view,
                         const std::wstring& viewId,
                         const ::MasterControlShell::ShellSnapshot& snapshot) {
    if (viewId == kOverviewView) {
        const auto typed = view.as<winrt::MasterControlShell::OverviewSectionControl>();
        winrt::get_self<winrt::MasterControlShell::implementation::OverviewSectionControl>(typed)->ApplySnapshot(snapshot);
        return;
    }
    if (viewId == kTelemetryView) {
        const auto typed = view.as<winrt::MasterControlShell::TelemetrySectionControl>();
        winrt::get_self<winrt::MasterControlShell::implementation::TelemetrySectionControl>(typed)->ApplySnapshot(snapshot);
        return;
    }
    if (viewId == kRuntimeView) {
        const auto typed = view.as<winrt::MasterControlShell::RuntimeSectionControl>();
        winrt::get_self<winrt::MasterControlShell::implementation::RuntimeSectionControl>(typed)->ApplySnapshot(snapshot);
        return;
    }
    if (viewId == kCluView) {
        const auto typed = view.as<winrt::MasterControlShell::CommandLogicUnitSectionControl>();
        winrt::get_self<winrt::MasterControlShell::implementation::CommandLogicUnitSectionControl>(typed)->ApplySnapshot(snapshot);
        return;
    }
    if (viewId == kProvidersView) {
        const auto typed = view.as<winrt::MasterControlShell::ProvidersSectionControl>();
        winrt::get_self<winrt::MasterControlShell::implementation::ProvidersSectionControl>(typed)->ApplySnapshot(snapshot);
        return;
    }
    if (viewId == kImportsView) {
        const auto typed = view.as<winrt::MasterControlShell::ImportsSectionControl>();
        winrt::get_self<winrt::MasterControlShell::implementation::ImportsSectionControl>(typed)->ApplySnapshot(snapshot);
        return;
    }
    if (viewId == kExportsView) {
        const auto typed = view.as<winrt::MasterControlShell::ExportsSectionControl>();
        winrt::get_self<winrt::MasterControlShell::implementation::ExportsSectionControl>(typed)->ApplySnapshot(snapshot);
        return;
    }
    if (viewId == kSecurityView) {
        const auto typed = view.as<winrt::MasterControlShell::SecuritySectionControl>();
        winrt::get_self<winrt::MasterControlShell::implementation::SecuritySectionControl>(typed)->ApplySnapshot(snapshot);
        return;
    }
    if (viewId == kSettingsView) {
        const auto typed = view.as<winrt::MasterControlShell::SettingsSectionControl>();
        winrt::get_self<winrt::MasterControlShell::implementation::SettingsSectionControl>(typed)->ApplySnapshot(snapshot);
    }
}

} // namespace

MainWindow::MainWindow() {
    writeShellLog(L"MainWindow ctor: InitializeComponent starting.");
    InitializeComponent();
    writeShellLog(L"MainWindow ctor: InitializeComponent finished.");
}

void MainWindow::RootGrid_Loaded(IInspectable const&, RoutedEventArgs const&) {
    if (windowInitialized_) {
        return;
    }

    windowInitialized_ = true;
    try {
        writeShellLog(L"RootGrid_Loaded: ConfigureWindow starting.");
        ConfigureWindow();
        writeShellLog(L"RootGrid_Loaded: ConfigureWindow finished.");

        writeShellLog(L"RootGrid_Loaded: ConfigureTimer starting.");
        ConfigureTimer();
        writeShellLog(L"RootGrid_Loaded: ConfigureTimer finished.");

        EnsureBootstrapSurface(currentSnapshot_);
        ApplySnapshot(currentSnapshot_);

        writeShellLog(L"RootGrid_Loaded: UpdateStatusBar starting.");
        UpdateStatusBar(L"Loading local service and Forsetti surface state.", InfoBarSeverity::Informational);
        writeShellLog(L"RootGrid_Loaded: UpdateStatusBar finished.");

        writeShellLog(L"RootGrid_Loaded: RefreshAsync starting.");
        RefreshAsync();
        writeShellLog(L"RootGrid_Loaded: RefreshAsync dispatched.");
    } catch (const winrt::hresult_error& error) {
        writeShellLog(L"RootGrid_Loaded caught HRESULT failure: " + std::wstring(error.message().c_str()));
    }
}

void MainWindow::ShellNavigation_SelectionChanged(
    IInspectable const&,
    NavigationViewSelectionChangedEventArgs const& args) {
    if (const auto selectedItem = args.SelectedItem().try_as<NavigationViewItem>()) {
        const auto destination = std::wstring(unbox_value_or<hstring>(selectedItem.Tag(), hstring(kOverviewDestination)).c_str());
        SetCurrentDestination(destination);
    }
}

void MainWindow::RefreshButton_Click(IInspectable const&, RoutedEventArgs const&) {
    RefreshAsync();
}

void MainWindow::StartServiceButton_Click(IInspectable const&, RoutedEventArgs const&) {
    RunServiceActionAsync(true);
}

void MainWindow::StopServiceButton_Click(IInspectable const&, RoutedEventArgs const&) {
    RunServiceActionAsync(false);
}

void MainWindow::OpenDashboardButton_Click(IInspectable const&, RoutedEventArgs const&) {
    HandleOpenDashboardAsync();
}

void MainWindow::OpenConfigButton_Click(IInspectable const&, RoutedEventArgs const&) {
    runtime_.OpenConfig();
}

void MainWindow::OpenDataButton_Click(IInspectable const&, RoutedEventArgs const&) {
    runtime_.OpenDataDirectory();
}

void MainWindow::GuidedProviderWizardButton_Click(IInspectable const&, RoutedEventArgs const&) {
    auto ignored = ShowProviderWizardAsync();
    (void)ignored;
}

void MainWindow::GuidedMcpWizardButton_Click(IInspectable const&, RoutedEventArgs const&) {
    auto ignored = ShowMcpServerWizardAsync();
    (void)ignored;
}

void MainWindow::GuidedSubAgentWizardButton_Click(IInspectable const&, RoutedEventArgs const&) {
    auto ignored = ShowSubAgentWizardAsync();
    (void)ignored;
}

void MainWindow::GuidedAppleHostWizardButton_Click(IInspectable const&, RoutedEventArgs const&) {
    auto ignored = ShowAppleHostWizardAsync();
    (void)ignored;
}

void MainWindow::GuidedProviderAssignmentWizardButton_Click(IInspectable const&, RoutedEventArgs const&) {
    auto ignored = ShowProviderAssignmentWizardAsync();
    (void)ignored;
}

void MainWindow::GuidedImportWizardButton_Click(IInspectable const&, RoutedEventArgs const&) {
    auto ignored = ShowImportWizardAsync();
    (void)ignored;
}

void MainWindow::SurfaceToolbarButton_Click(IInspectable const& sender, RoutedEventArgs const&) {
    const auto button = sender.try_as<Button>();
    if (button == nullptr) {
        return;
    }

    const auto itemId = std::wstring(unbox_value_or<hstring>(button.Tag(), hstring()).c_str());
    const auto iterator = std::find_if(
        currentSnapshot_.toolbarItems.begin(),
        currentSnapshot_.toolbarItems.end(),
        [&itemId](const auto& item) { return item.id == itemId; });

    if (iterator == currentSnapshot_.toolbarItems.end()) {
        UpdateStatusBar(L"Unable to resolve the selected Forsetti toolbar command.", InfoBarSeverity::Warning);
        return;
    }

    switch (iterator->actionKind) {
        case ::MasterControlShell::ShellToolbarActionKind::Navigate:
            SetCurrentDestination(iterator->targetId);
            break;
        case ::MasterControlShell::ShellToolbarActionKind::OpenOverlay:
            OpenOverlayRouteAsync(iterator->targetId);
            break;
        case ::MasterControlShell::ShellToolbarActionKind::PublishEvent:
            UpdateStatusBar(
                winrt::hstring(std::wstring(L"Forsetti event publication is not yet exposed through the local admin API: ") + iterator->targetId),
                InfoBarSeverity::Warning);
            break;
        default:
            UpdateStatusBar(L"Unknown Forsetti toolbar action.", InfoBarSeverity::Warning);
            break;
    }
}

void MainWindow::ConfigureWindow() {
    Window window = *this;
    try {
        if (const auto nativeWindow = window.try_as<::IWindowNative>()) {
            if (FAILED(nativeWindow->get_WindowHandle(&windowHandle_))) {
                windowHandle_ = nullptr;
                writeShellLog(L"IWindowNative returned a null HWND.");
            }
        } else {
            writeShellLog(L"IWindowNative was unavailable during shell startup.");
        }
    } catch (const winrt::hresult_error& error) {
        writeShellLog(L"IWindowNative startup probe failed: " + std::wstring(error.message().c_str()));
        windowHandle_ = nullptr;
    }

    try {
        using namespace Microsoft::UI::Composition::SystemBackdrops;

        MicaBackdrop micaBackdrop;
        micaBackdrop.Kind(MicaKind::BaseAlt);
        window.SystemBackdrop(micaBackdrop);
        writeShellLog(L"Mica backdrop enabled.");
    } catch (const winrt::hresult_error& error) {
        writeShellLog(L"Mica backdrop unavailable: " + std::wstring(error.message().c_str()));
    }

    try {
        window.ExtendsContentIntoTitleBar(false);
        writeShellLog(L"Using the system title bar for reliable shell dragging.");
    } catch (const winrt::hresult_error& error) {
        writeShellLog(L"System title bar configuration failed: " + std::wstring(error.message().c_str()));
    }

    if (windowHandle_ != nullptr) {
        setWindowSize(windowHandle_, 1560, 1024);
        centerWindow(windowHandle_);
    }
}

void MainWindow::ConfigureTimer() {
    try {
        refreshTimer_ = DispatcherQueue().CreateTimer();
        refreshTimer_.Interval(std::chrono::seconds(10));
        const auto weakThis = get_weak();
        refreshTimer_.Tick([weakThis](auto&&, auto&&) {
            if (const auto self = weakThis.get()) {
                self->RefreshAsync();
            }
        });
        refreshTimer_.Start();
    } catch (const winrt::hresult_error& error) {
        writeShellLog(L"Dispatcher timer fallback activated: " + std::wstring(error.message().c_str()));
    }
}

void MainWindow::EnsureBootstrapSurface(::MasterControlShell::ShellSnapshot& snapshot) {
    if (snapshot.navigationPointers.empty()) {
        snapshot.navigationPointers = bootstrapNavigationPointers();
    }
    if (snapshot.toolbarItems.empty()) {
        snapshot.toolbarItems = bootstrapToolbarItems();
    }
    if (snapshot.overlayRoutes.empty()) {
        snapshot.overlayRoutes = bootstrapOverlayRoutes();
    }
    if (snapshot.viewInjectionsBySlot.empty()) {
        snapshot.viewInjectionsBySlot = bootstrapViewInjectionsBySlot();
    }
}

void MainWindow::SetCurrentDestination(const std::wstring& destinationId) {
    currentDestination_ = destinationId.empty() ? std::wstring(kOverviewDestination) : destinationId;
    SectionContentHost().Content(ResolvePrimaryViewForDestination(currentDestination_, currentSnapshot_));
    ApplySectionMetadata(currentSnapshot_);
}

void MainWindow::ApplySurfaceNavigation(const ::MasterControlShell::ShellSnapshot& snapshot) {
    ShellNavigation().MenuItems().Clear();

    NavigationViewItem selectedItem{ nullptr };
    std::wstring firstDestination;
    for (const auto& pointer : snapshot.navigationPointers) {
        NavigationViewItem item;
        item.Content(box_value(hstring(pointer.label.empty() ? labelForDestination(pointer.destinationId) : pointer.label)));
        item.Tag(box_value(hstring(pointer.destinationId)));
        ShellNavigation().MenuItems().Append(item);

        if (firstDestination.empty()) {
            firstDestination = pointer.destinationId;
        }
        if (pointer.destinationId == currentDestination_) {
            selectedItem = item;
        }
    }

    if (selectedItem == nullptr && !firstDestination.empty()) {
        currentDestination_ = firstDestination;
        selectedItem = ShellNavigation().MenuItems().GetAt(0).try_as<NavigationViewItem>();
    }

    if (selectedItem != nullptr) {
        ShellNavigation().SelectedItem(selectedItem);
    }
}

void MainWindow::ApplySurfaceToolbar(const ::MasterControlShell::ShellSnapshot& snapshot) {
    ForsettiToolbarHost().Children().Clear();

    for (const auto& item : snapshot.toolbarItems) {
        Button button;
        button.Style(Application::Current().Resources().Lookup(box_value(L"ShellCommandButtonStyle")).try_as<Style>());
        button.Tag(box_value(hstring(item.id)));
        button.Click({ this, &MainWindow::SurfaceToolbarButton_Click });

        StackPanel content;
        content.Spacing(4);
        content.HorizontalAlignment(HorizontalAlignment::Center);

        FontIcon icon;
        icon.Glyph(glyphForSystemImageName(item.systemImageName));
        icon.FontSize(18);
        icon.HorizontalAlignment(HorizontalAlignment::Center);
        icon.Foreground(Application::Current().Resources().Lookup(box_value(L"ShellAccentBrush")).try_as<Brush>());

        TextBlock label;
        label.Text(hstring(item.title));
        label.Style(Application::Current().Resources().Lookup(box_value(L"ShellLabelTextStyle")).try_as<Style>());
        label.HorizontalAlignment(HorizontalAlignment::Center);

        content.Children().Append(icon);
        content.Children().Append(label);
        button.Content(content);
        ForsettiToolbarHost().Children().Append(button);
    }
}

void MainWindow::ApplySectionMetadata(const ::MasterControlShell::ShellSnapshot& snapshot) {
    auto title = labelForDestination(currentDestination_);
    const auto iterator = std::find_if(
        snapshot.navigationPointers.begin(),
        snapshot.navigationPointers.end(),
        [this](const auto& pointer) { return pointer.destinationId == currentDestination_; });
    if (iterator != snapshot.navigationPointers.end() && !iterator->label.empty()) {
        title = iterator->label;
    }

    const auto metadata = metadataForDestination(currentDestination_, title);
    CurrentViewEyebrowText().Text(winrt::hstring(metadata.eyebrow));
    CurrentViewTitleText().Text(winrt::hstring(metadata.title));
    CurrentViewDescriptionText().Text(winrt::hstring(metadata.description));
}

void MainWindow::ApplyCachedSectionSnapshots(const ::MasterControlShell::ShellSnapshot& snapshot) {
    for (auto& [viewId, view] : cachedViews_) {
        if (view != nullptr) {
            applySnapshotToView(view, viewId, snapshot);
        }
    }
}

FrameworkElement MainWindow::ResolvePrimaryViewForDestination(
    const std::wstring& destinationId,
    const ::MasterControlShell::ShellSnapshot& snapshot) {
    const auto iterator = snapshot.viewInjectionsBySlot.find(destinationId);
    if (iterator == snapshot.viewInjectionsBySlot.end() || iterator->second.empty()) {
        return CreateUnavailableView(
            L"Forsetti View Unavailable",
            L"The selected destination does not currently publish a Forsetti view injection.");
    }

    return CreateViewForViewId(iterator->second.front().viewId, true);
}

FrameworkElement MainWindow::CreateViewForViewId(const std::wstring& viewId, const bool cacheable) {
    if (cacheable) {
        const auto iterator = cachedViews_.find(viewId);
        if (iterator != cachedViews_.end()) {
            return iterator->second;
        }
    }

    FrameworkElement view{ nullptr };
    if (viewId == kOverviewView) {
        view = winrt::MasterControlShell::OverviewSectionControl();
    } else if (viewId == kTelemetryView) {
        view = winrt::MasterControlShell::TelemetrySectionControl();
    } else if (viewId == kRuntimeView) {
        view = winrt::MasterControlShell::RuntimeSectionControl();
    } else if (viewId == kCluView) {
        view = winrt::MasterControlShell::CommandLogicUnitSectionControl();
    } else if (viewId == kProvidersView) {
        view = winrt::MasterControlShell::ProvidersSectionControl();
    } else if (viewId == kImportsView) {
        view = winrt::MasterControlShell::ImportsSectionControl();
    } else if (viewId == kExportsView) {
        view = winrt::MasterControlShell::ExportsSectionControl();
    } else if (viewId == kSecurityView) {
        view = winrt::MasterControlShell::SecuritySectionControl();
    } else if (viewId == kSettingsView) {
        view = winrt::MasterControlShell::SettingsSectionControl();
    }

    if (view == nullptr) {
        return CreateUnavailableView(
            L"Unknown Forsetti View",
            winrt::hstring(std::wstring(L"The shell does not have a renderer registered for ") + viewId));
    }

    if (viewId == kRuntimeView || viewId == kCluView || viewId == kProvidersView || viewId == kImportsView || viewId == kExportsView || viewId == kSecurityView) {
        const auto weakThis = get_weak();
        attachInteractiveRuntime(
            view,
            viewId,
            runtime_,
            [weakThis]() {
                if (const auto self = weakThis.get()) {
                    self->RefreshAsync();
                }
            });
    }

    applySnapshotToView(view, viewId, currentSnapshot_);
    if (cacheable) {
        cachedViews_.insert_or_assign(viewId, view);
    }
    return view;
}

FrameworkElement MainWindow::CreateUnavailableView(
    winrt::hstring const& title,
    winrt::hstring const& message) {
    Border border;
    border.Style(Application::Current().Resources().Lookup(box_value(L"ShellCardStyle")).try_as<Style>());

    StackPanel stack;
    stack.Spacing(14);

    TextBlock titleText;
    titleText.Text(title);
    titleText.Style(Application::Current().Resources().Lookup(box_value(L"ShellSectionTitleTextStyle")).try_as<Style>());

    TextBlock bodyText;
    bodyText.Text(message);
    bodyText.Style(Application::Current().Resources().Lookup(box_value(L"ShellBodyTextStyle")).try_as<Style>());

    stack.Children().Append(titleText);
    stack.Children().Append(bodyText);
    border.Child(stack);
    return border;
}

void MainWindow::ApplySnapshot(const ::MasterControlShell::ShellSnapshot& snapshot) {
    ApplySurfaceNavigation(snapshot);
    ApplySurfaceToolbar(snapshot);
    ApplyCachedSectionSnapshots(snapshot);
    SetCurrentDestination(currentDestination_);

    ServiceStateText().Text(winrt::hstring(serviceStateLabel(snapshot.serviceState)));
    ApiStateText().Text(snapshot.apiHealthy ? L"Reachable" : L"Offline");
    EndpointCountText().Text(winrt::hstring(std::to_wstring(snapshot.endpointCount)));
    ProviderCountText().Text(winrt::hstring(std::to_wstring(snapshot.providerCount)));
    PulseStateText().Text(winrt::hstring(uppercase(serviceStateLabel(snapshot.serviceState))));

    std::wstring pulseCaption = snapshot.apiHealthy ? L"Admin API online" : L"Admin API offline";
    pulseCaption += L"  |  ";
    pulseCaption += std::to_wstring(snapshot.endpointCount);
    pulseCaption += L" endpoints";
    PulseCaptionText().Text(winrt::hstring(pulseCaption));

    StartServiceButton().IsEnabled(snapshot.canStartService);
    StopServiceButton().IsEnabled(snapshot.canStopService);

    const auto serviceLabel = uppercase(serviceStateLabel(snapshot.serviceState));
    switch (snapshot.serviceState) {
        case ::MasterControlShell::ServiceState::Running:
            applyBadge(
                ServiceBadgeBorder(),
                ServiceBadgeText(),
                L"SERVICE " + serviceLabel,
                makeColor(0x33, 0x1C, 0xF2, 0xC1),
                makeColor(0xFF, 0xB8, 0xFF, 0xF0),
                makeColor(0x66, 0x1C, 0xF2, 0xC1));
            break;
        case ::MasterControlShell::ServiceState::StartPending:
        case ::MasterControlShell::ServiceState::StopPending:
            applyBadge(
                ServiceBadgeBorder(),
                ServiceBadgeText(),
                L"SERVICE " + serviceLabel,
                makeColor(0x33, 0xFF, 0xC8, 0x57),
                makeColor(0xFF, 0xFF, 0xE9, 0xC7),
                makeColor(0x66, 0xFF, 0xC8, 0x57));
            break;
        default:
            applyBadge(
                ServiceBadgeBorder(),
                ServiceBadgeText(),
                L"SERVICE " + serviceLabel,
                makeColor(0x33, 0xFF, 0x6A, 0x80),
                makeColor(0xFF, 0xFF, 0xD8, 0xDE),
                makeColor(0x66, 0xFF, 0x6A, 0x80));
            break;
    }

    if (snapshot.apiHealthy) {
        applyBadge(
            ApiBadgeBorder(),
            ApiBadgeText(),
            L"API REACHABLE",
            makeColor(0x33, 0x00, 0xF6, 0xFF),
            makeColor(0xFF, 0xD7, 0xFD, 0xFF),
            makeColor(0x66, 0x00, 0xF6, 0xFF));
    } else {
        applyBadge(
            ApiBadgeBorder(),
            ApiBadgeText(),
            L"API OFFLINE",
            makeColor(0x33, 0xFF, 0xC8, 0x57),
            makeColor(0xFF, 0xFF, 0xED, 0xC0),
            makeColor(0x66, 0xFF, 0xC8, 0x57));
    }

    if (!snapshot.statusMessage.empty()) {
        const auto severity = snapshot.apiHealthy
            ? InfoBarSeverity::Success
            : (snapshot.serviceState == ::MasterControlShell::ServiceState::Running
                ? InfoBarSeverity::Warning
                : InfoBarSeverity::Informational);
        UpdateStatusBar(winrt::hstring(snapshot.statusMessage), severity);
    }
}

void MainWindow::UpdateStatusBar(winrt::hstring const& message, InfoBarSeverity const severity) {
    Color background = makeColor(0x1A, 0x10, 0x30, 0x45);
    Color edge = makeColor(0x44, 0x00, 0xF6, 0xFF);
    Color foreground = makeColor(0xFF, 0xD7, 0xFD, 0xFF);
    std::wstring stateLabel = L"SYSTEM STATUS";

    switch (severity) {
        case InfoBarSeverity::Success:
            background = makeColor(0x22, 0x12, 0x3A, 0x2F);
            edge = makeColor(0x66, 0x1C, 0xF2, 0xC1);
            foreground = makeColor(0xFF, 0xB8, 0xFF, 0xF0);
            stateLabel = L"GRID SYNCHRONIZED";
            break;
        case InfoBarSeverity::Warning:
            background = makeColor(0x24, 0x3E, 0x2C, 0x0A);
            edge = makeColor(0x66, 0xFF, 0xC8, 0x57);
            foreground = makeColor(0xFF, 0xFF, 0xEC, 0xC7);
            stateLabel = L"ATTENTION";
            break;
        case InfoBarSeverity::Error:
            background = makeColor(0x24, 0x3F, 0x12, 0x18);
            edge = makeColor(0x66, 0xFF, 0x6A, 0x80);
            foreground = makeColor(0xFF, 0xFF, 0xD9, 0xDF);
            stateLabel = L"FAULT";
            break;
        default:
            break;
    }

    StatusBannerBorder().Background(SolidColorBrush(background));
    StatusBannerBorder().BorderBrush(SolidColorBrush(edge));
    StatusBannerStateText().Foreground(SolidColorBrush(foreground));
    StatusBannerStateText().Text(winrt::hstring(stateLabel));
    StatusBannerText().Text(message);
}

IAsyncAction MainWindow::RefreshAsync() {
    if (refreshInFlight_.exchange(true)) {
        co_return;
    }

    RefreshButton().IsEnabled(false);
    winrt::apartment_context uiThread;
    co_await winrt::resume_background();

    ::MasterControlShell::ShellSnapshot snapshot;
    try {
        snapshot = runtime_.CaptureSnapshot();
    } catch (...) {
        snapshot.statusMessage = L"Refreshing the WinUI shell failed unexpectedly.";
    }

    EnsureBootstrapSurface(snapshot);

    co_await uiThread;
    currentSnapshot_ = std::move(snapshot);
    ApplySnapshot(currentSnapshot_);
    refreshInFlight_ = false;
    RefreshButton().IsEnabled(true);
}

IAsyncAction MainWindow::RunServiceActionAsync(const bool start) {
    winrt::apartment_context uiThread;
    co_await winrt::resume_background();

    std::wstring message;
    const bool succeeded = start ? runtime_.StartService(message) : runtime_.StopService(message);

    co_await uiThread;
    UpdateStatusBar(winrt::hstring(message), succeeded ? InfoBarSeverity::Success : InfoBarSeverity::Warning);
    RefreshAsync();
}

IAsyncAction MainWindow::HandleOpenDashboardAsync() {
    if (currentSnapshot_.serviceState != ::MasterControlShell::ServiceState::Running) {
        winrt::apartment_context uiThread;
        co_await winrt::resume_background();

        std::wstring message;
        const bool succeeded = runtime_.StartService(message);

        co_await uiThread;
        if (!succeeded) {
            co_await ShowDialogAsync(L"Unable to start service", winrt::hstring(message));
            co_return;
        }

        UpdateStatusBar(winrt::hstring(message), InfoBarSeverity::Success);
        co_await RefreshAsync();
    }

    runtime_.OpenDashboard(currentSnapshot_);
}

IAsyncAction MainWindow::ShowSubAgentWizardAsync() {
    ContentDialog dialog;
    dialog.Title(box_value(L"New Sub-Agent Wizard"));
    dialog.CloseButtonText(L"Close");
    dialog.XamlRoot(RootGrid().XamlRoot());

    ScrollViewer scrollViewer;
    scrollViewer.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
    scrollViewer.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
    scrollViewer.MaxHeight(620);

    StackPanel root;
    root.Spacing(14);
    root.Width(560);

    TextBlock intro;
    intro.Style(Application::Current().Resources().Lookup(box_value(L"ShellBodyTextStyle")).try_as<Style>());
    intro.Text(L"Step 1: identify the specialist lane. Step 2: describe what it is good at. Step 3: choose whether it is a logical lane or a reachable host, then create it.");
    intro.TextWrapping(TextWrapping::WrapWholeWords);
    root.Children().Append(intro);

    const auto addLabel = [&root](std::wstring const& text) {
        TextBlock label;
        label.Style(Application::Current().Resources().Lookup(box_value(L"ShellLabelTextStyle")).try_as<Style>());
        label.Text(winrt::hstring(text));
        root.Children().Append(label);
    };

    TextBox idBox;
    idBox.PlaceholderText(L"swift-specialist");
    root.Children().Append([&]() {
        addLabel(L"Step 1. Sub-Agent ID");
        return idBox;
    }());

    TextBox displayNameBox;
    displayNameBox.PlaceholderText(L"Swift Specialist");
    root.Children().Append([&]() {
        addLabel(L"Display Name");
        return displayNameBox;
    }());

    TextBox specializationBox;
    specializationBox.PlaceholderText(L"Swift, C++, documentation, test automation...");
    root.Children().Append([&]() {
        addLabel(L"Step 2. Specialization");
        return specializationBox;
    }());

    Grid endpointGrid;
    endpointGrid.ColumnSpacing(12);
    endpointGrid.RowSpacing(12);
    endpointGrid.ColumnDefinitions().Append(ColumnDefinition());
    endpointGrid.ColumnDefinitions().Append(ColumnDefinition());
    endpointGrid.ColumnDefinitions().Append(ColumnDefinition());

    TextBox hostBox;
    hostBox.PlaceholderText(L"Optional bind or LAN host");
    TextBox portBox;
    portBox.PlaceholderText(L"0");
    TextBox protocolBox;
    protocolBox.PlaceholderText(L"virtual or http");
    protocolBox.Text(L"virtual");

    StackPanel hostPanel;
    hostPanel.Spacing(6);
    TextBlock hostLabel;
    hostLabel.Style(Application::Current().Resources().Lookup(box_value(L"ShellLabelTextStyle")).try_as<Style>());
    hostLabel.Text(L"Step 3. Host");
    hostPanel.Children().Append(hostLabel);
    hostPanel.Children().Append(hostBox);

    StackPanel portPanel;
    portPanel.Spacing(6);
    TextBlock portLabel;
    portLabel.Style(Application::Current().Resources().Lookup(box_value(L"ShellLabelTextStyle")).try_as<Style>());
    portLabel.Text(L"Port");
    portPanel.Children().Append(portLabel);
    portPanel.Children().Append(portBox);
    Grid::SetColumn(portPanel, 1);

    StackPanel protocolPanel;
    protocolPanel.Spacing(6);
    TextBlock protocolLabel;
    protocolLabel.Style(Application::Current().Resources().Lookup(box_value(L"ShellLabelTextStyle")).try_as<Style>());
    protocolLabel.Text(L"Protocol");
    protocolPanel.Children().Append(protocolLabel);
    protocolPanel.Children().Append(protocolBox);
    Grid::SetColumn(protocolPanel, 2);

    endpointGrid.Children().Append(hostPanel);
    endpointGrid.Children().Append(portPanel);
    endpointGrid.Children().Append(protocolPanel);
    root.Children().Append(endpointGrid);

    TextBox routePathBox;
    routePathBox.PlaceholderText(L"/status or blank for a logical lane");
    root.Children().Append([&]() {
        addLabel(L"Route Path");
        return routePathBox;
    }());

    TextBox descriptionBox;
    descriptionBox.AcceptsReturn(true);
    descriptionBox.Height(88);
    descriptionBox.TextWrapping(TextWrapping::Wrap);
    descriptionBox.PlaceholderText(L"What should this lane handle, and when should the orchestrator use it?");
    root.Children().Append([&]() {
        addLabel(L"Step 4. Notes for operators and agents");
        return descriptionBox;
    }());

    TextBlock statusText;
    statusText.Style(Application::Current().Resources().Lookup(box_value(L"ShellDataTextStyle")).try_as<Style>());
    statusText.Text(L"Create a specialist lane here instead of digging through the runtime editor.");
    statusText.TextWrapping(TextWrapping::WrapWholeWords);
    root.Children().Append(statusText);

    Button createButton;
    createButton.Content(box_value(L"Create Sub-Agent"));
    createButton.Style(Application::Current().Resources().Lookup(box_value(L"ShellCommandButtonStyle")).try_as<Style>());
    root.Children().Append(createButton);

    scrollViewer.Content(root);
    dialog.Content(scrollViewer);

    createButton.Click([this, dialog, createButton, statusText, idBox, displayNameBox, specializationBox, hostBox, portBox, protocolBox, routePathBox, descriptionBox](IInspectable const&, RoutedEventArgs const&) {
        auto ignored = [this, dialog, createButton, statusText, idBox, displayNameBox, specializationBox, hostBox, portBox, protocolBox, routePathBox, descriptionBox]() -> IAsyncAction {
            const auto id = trimCopy(std::wstring(idBox.Text().c_str()));
            const auto displayName = trimCopy(std::wstring(displayNameBox.Text().c_str()));
            const auto specialization = trimCopy(std::wstring(specializationBox.Text().c_str()));
            const auto host = trimCopy(std::wstring(hostBox.Text().c_str()));
            const auto protocol = trimCopy(std::wstring(protocolBox.Text().c_str()));
            const auto routePath = trimCopy(std::wstring(routePathBox.Text().c_str()));
            const auto description = trimCopy(std::wstring(descriptionBox.Text().c_str()));

            uint16_t port = 0;
            const auto portText = trimCopy(std::wstring(portBox.Text().c_str()));
            if (!portText.empty()) {
                try {
                    const auto parsedPort = std::stoul(portText);
                    if (parsedPort > 65535U) {
                        statusText.Text(L"Port must be blank or between 0 and 65535.");
                        co_return;
                    }
                    port = static_cast<uint16_t>(parsedPort);
                } catch (...) {
                    statusText.Text(L"Port must be blank or between 0 and 65535.");
                    co_return;
                }
            }

            if (id.empty() || displayName.empty()) {
                statusText.Text(L"Sub-agent ID and display name are both required.");
                co_return;
            }

            createButton.IsEnabled(false);
            statusText.Text(L"Creating the sub-agent lane through the local admin API.");

            ::MasterControlShell::ShellRuntimeEndpoint endpoint{
                id,
                displayName,
                L"sub_agent",
                host,
                port,
                protocol.empty() ? L"virtual" : protocol,
                L"unknown",
                description,
                routePath,
                specialization,
                true
            };

            winrt::apartment_context uiThread;
            co_await winrt::resume_background();
            const auto result = runtime_.UpsertSubAgent(endpoint);
            co_await uiThread;

            statusText.Text(winrt::hstring(result.message));
            if (!result.succeeded) {
                createButton.IsEnabled(true);
                GuidedWorkflowStatusText().Text(L"Sub-agent wizard needs attention. Review the message inside the wizard and try again.");
                co_return;
            }

            GuidedWorkflowStatusText().Text(winrt::hstring(L"Created sub-agent lane '" + displayName + L"'."));
            SetCurrentDestination(kRuntimeDestination);
            auto refreshIgnored = RefreshAsync();
            (void)refreshIgnored;
            dialog.Hide();
        }();
        (void)ignored;
    });

    co_await dialog.ShowAsync();
}

IAsyncAction MainWindow::ShowMcpServerWizardAsync() {
    ContentDialog dialog;
    dialog.Title(box_value(L"New MCP Server Wizard"));
    dialog.CloseButtonText(L"Close");
    dialog.XamlRoot(RootGrid().XamlRoot());

    ScrollViewer scrollViewer;
    scrollViewer.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
    scrollViewer.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
    scrollViewer.MaxHeight(620);

    StackPanel root;
    root.Spacing(14);
    root.Width(560);

    TextBlock intro;
    intro.Style(Application::Current().Resources().Lookup(box_value(L"ShellBodyTextStyle")).try_as<Style>());
    intro.Text(L"Step 1: name the MCP lane. Step 2: point it at the host and port. Step 3: define the shared route path, then publish it for every provider to reuse.");
    intro.TextWrapping(TextWrapping::WrapWholeWords);
    root.Children().Append(intro);

    const auto addLabel = [&root](std::wstring const& text) {
        TextBlock label;
        label.Style(Application::Current().Resources().Lookup(box_value(L"ShellLabelTextStyle")).try_as<Style>());
        label.Text(winrt::hstring(text));
        root.Children().Append(label);
    };

    TextBox idBox;
    idBox.PlaceholderText(L"swift-tools-mcp");
    root.Children().Append([&]() {
        addLabel(L"Step 1. MCP Server ID");
        return idBox;
    }());

    TextBox displayNameBox;
    displayNameBox.PlaceholderText(L"Swift Tools MCP");
    root.Children().Append([&]() {
        addLabel(L"Display Name");
        return displayNameBox;
    }());

    Grid hostGrid;
    hostGrid.ColumnSpacing(12);
    hostGrid.ColumnDefinitions().Append(ColumnDefinition());
    hostGrid.ColumnDefinitions().Append(ColumnDefinition());
    hostGrid.ColumnDefinitions().Append(ColumnDefinition());

    TextBox hostBox;
    hostBox.PlaceholderText(L"127.0.0.1");
    TextBox portBox;
    portBox.PlaceholderText(L"7302");
    TextBox protocolBox;
    protocolBox.PlaceholderText(L"http");
    protocolBox.Text(L"http");

    StackPanel hostPanel;
    hostPanel.Spacing(6);
    TextBlock hostLabel;
    hostLabel.Style(Application::Current().Resources().Lookup(box_value(L"ShellLabelTextStyle")).try_as<Style>());
    hostLabel.Text(L"Step 2. Host");
    hostPanel.Children().Append(hostLabel);
    hostPanel.Children().Append(hostBox);

    StackPanel portPanel;
    portPanel.Spacing(6);
    TextBlock portLabel;
    portLabel.Style(Application::Current().Resources().Lookup(box_value(L"ShellLabelTextStyle")).try_as<Style>());
    portLabel.Text(L"Port");
    portPanel.Children().Append(portLabel);
    portPanel.Children().Append(portBox);
    Grid::SetColumn(portPanel, 1);

    StackPanel protocolPanel;
    protocolPanel.Spacing(6);
    TextBlock protocolLabel;
    protocolLabel.Style(Application::Current().Resources().Lookup(box_value(L"ShellLabelTextStyle")).try_as<Style>());
    protocolLabel.Text(L"Protocol");
    protocolPanel.Children().Append(protocolLabel);
    protocolPanel.Children().Append(protocolBox);
    Grid::SetColumn(protocolPanel, 2);

    hostGrid.Children().Append(hostPanel);
    hostGrid.Children().Append(portPanel);
    hostGrid.Children().Append(protocolPanel);
    root.Children().Append(hostGrid);

    TextBox routePathBox;
    routePathBox.PlaceholderText(L"/mcp");
    routePathBox.Text(L"/mcp");
    root.Children().Append([&]() {
        addLabel(L"Step 3. Shared Route Path");
        return routePathBox;
    }());

    TextBox descriptionBox;
    descriptionBox.AcceptsReturn(true);
    descriptionBox.Height(88);
    descriptionBox.TextWrapping(TextWrapping::Wrap);
    descriptionBox.PlaceholderText(L"What tools or capabilities does this MCP lane expose to the orchestration server?");
    root.Children().Append([&]() {
        addLabel(L"Notes");
        return descriptionBox;
    }());

    TextBlock statusText;
    statusText.Style(Application::Current().Resources().Lookup(box_value(L"ShellDataTextStyle")).try_as<Style>());
    statusText.Text(L"Publish a shared MCP lane here instead of editing the runtime card by hand.");
    statusText.TextWrapping(TextWrapping::WrapWholeWords);
    root.Children().Append(statusText);

    Button createButton;
    createButton.Content(box_value(L"Create MCP Server"));
    createButton.Style(Application::Current().Resources().Lookup(box_value(L"ShellCommandButtonStyle")).try_as<Style>());
    root.Children().Append(createButton);

    scrollViewer.Content(root);
    dialog.Content(scrollViewer);

    createButton.Click([this, dialog, createButton, statusText, idBox, displayNameBox, hostBox, portBox, protocolBox, routePathBox, descriptionBox](IInspectable const&, RoutedEventArgs const&) {
        auto ignored = [this, dialog, createButton, statusText, idBox, displayNameBox, hostBox, portBox, protocolBox, routePathBox, descriptionBox]() -> IAsyncAction {
            const auto id = trimCopy(std::wstring(idBox.Text().c_str()));
            const auto displayName = trimCopy(std::wstring(displayNameBox.Text().c_str()));
            const auto host = trimCopy(std::wstring(hostBox.Text().c_str()));
            const auto protocol = trimCopy(std::wstring(protocolBox.Text().c_str()));
            const auto routePath = trimCopy(std::wstring(routePathBox.Text().c_str()));
            const auto description = trimCopy(std::wstring(descriptionBox.Text().c_str()));
            const auto portText = trimCopy(std::wstring(portBox.Text().c_str()));

            if (id.empty() || displayName.empty() || portText.empty()) {
                statusText.Text(L"MCP server ID, display name, and port are all required.");
                co_return;
            }

            uint16_t port = 0;
            try {
                const auto parsedPort = std::stoul(portText);
                if (parsedPort == 0U || parsedPort > 65535U) {
                    statusText.Text(L"Port must be between 1 and 65535.");
                    co_return;
                }
                port = static_cast<uint16_t>(parsedPort);
            } catch (...) {
                statusText.Text(L"Port must be between 1 and 65535.");
                co_return;
            }

            createButton.IsEnabled(false);
            statusText.Text(L"Publishing the MCP server lane through the local admin API.");

            ::MasterControlShell::ShellRuntimeEndpoint endpoint{
                id,
                displayName,
                L"mcp_server",
                host.empty() ? L"127.0.0.1" : host,
                port,
                protocol.empty() ? L"http" : protocol,
                L"unknown",
                description,
                routePath.empty() ? L"/mcp" : routePath,
                L"",
                true
            };

            winrt::apartment_context uiThread;
            co_await winrt::resume_background();
            const auto result = runtime_.UpsertMcpServer(endpoint);
            co_await uiThread;

            statusText.Text(winrt::hstring(result.message));
            if (!result.succeeded) {
                createButton.IsEnabled(true);
                GuidedWorkflowStatusText().Text(L"MCP server wizard needs attention. Review the wizard message and try again.");
                co_return;
            }

            GuidedWorkflowStatusText().Text(winrt::hstring(L"Published MCP server lane '" + displayName + L"'."));
            SetCurrentDestination(kRuntimeDestination);
            auto refreshIgnored = RefreshAsync();
            (void)refreshIgnored;
            dialog.Hide();
        }();
        (void)ignored;
    });

    co_await dialog.ShowAsync();
}

IAsyncAction MainWindow::ShowProviderWizardAsync() {
    ContentDialog dialog;
    dialog.Title(box_value(L"New Provider Wizard"));
    dialog.CloseButtonText(L"Close");
    dialog.XamlRoot(RootGrid().XamlRoot());

    ScrollViewer scrollViewer;
    scrollViewer.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
    scrollViewer.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
    scrollViewer.MaxHeight(640);

    StackPanel root;
    root.Spacing(14);
    root.Width(560);

    TextBlock intro;
    intro.Style(Application::Current().Resources().Lookup(box_value(L"ShellBodyTextStyle")).try_as<Style>());
    intro.Text(L"Step 1: pick the provider module. Step 2: confirm the route identity and defaults. Step 3: add credentials and optionally assign the route to an orchestration lane.");
    intro.TextWrapping(TextWrapping::WrapWholeWords);
    root.Children().Append(intro);

    const auto addLabel = [&root](std::wstring const& text) {
        TextBlock label;
        label.Style(Application::Current().Resources().Lookup(box_value(L"ShellLabelTextStyle")).try_as<Style>());
        label.Text(winrt::hstring(text));
        root.Children().Append(label);
    };

    ComboBox capabilitySelector;
    for (const auto& capability : currentSnapshot_.providerCapabilities) {
        ComboBoxItem item;
        item.Content(box_value(winrt::hstring(capability.displayName.empty() ? capability.providerId : capability.displayName)));
        item.Tag(box_value(winrt::hstring(capability.providerId)));
        capabilitySelector.Items().Append(item);
    }
    if (capabilitySelector.Items().Size() > 0) {
        capabilitySelector.SelectedIndex(0);
    }
    root.Children().Append([&]() {
        addLabel(L"Step 1. Provider Module");
        return capabilitySelector;
    }());

    TextBox idBox;
    idBox.PlaceholderText(L"codex");
    root.Children().Append([&]() {
        addLabel(L"Route ID");
        return idBox;
    }());

    TextBox displayNameBox;
    displayNameBox.PlaceholderText(L"Codex");
    root.Children().Append([&]() {
        addLabel(L"Display Name");
        return displayNameBox;
    }());

    TextBox baseUrlBox;
    baseUrlBox.PlaceholderText(L"https://api.openai.com/v1");
    root.Children().Append([&]() {
        addLabel(L"Step 2. Base URL");
        return baseUrlBox;
    }());

    TextBox modelIdBox;
    modelIdBox.PlaceholderText(L"gpt-5.4");
    root.Children().Append([&]() {
        addLabel(L"Recommended Model");
        return modelIdBox;
    }());

    ToggleSwitch enabledToggle;
    enabledToggle.Header(box_value(L"Route enabled"));
    enabledToggle.IsOn(true);
    root.Children().Append(enabledToggle);

    ToggleSwitch autonomousToggle;
    autonomousToggle.Header(box_value(L"Allow autonomous control"));
    autonomousToggle.IsOn(false);
    root.Children().Append(autonomousToggle);

    ComboBox assignmentSelector;
    ComboBoxItem unassignedItem;
    unassignedItem.Content(box_value(L"(Leave unassigned)"));
    assignmentSelector.Items().Append(unassignedItem);
    for (const auto& target : currentSnapshot_.providerAssignmentTargets) {
        ComboBoxItem item;
        std::wstring label = target.displayName.empty() ? target.targetId : target.displayName;
        if (!target.kind.empty()) {
            label += L"  |  " + target.kind;
        }
        item.Content(box_value(winrt::hstring(label)));
        item.Tag(box_value(winrt::hstring(target.targetId)));
        assignmentSelector.Items().Append(item);
    }
    assignmentSelector.SelectedIndex(0);
    root.Children().Append([&]() {
        addLabel(L"Step 3. Assign this route to");
        return assignmentSelector;
    }());

    TextBlock credentialOneLabel;
    credentialOneLabel.Style(Application::Current().Resources().Lookup(box_value(L"ShellLabelTextStyle")).try_as<Style>());
    PasswordBox credentialOneBox;
    credentialOneBox.PlaceholderText(L"Credential value");
    TextBlock credentialOneHint;
    credentialOneHint.Style(Application::Current().Resources().Lookup(box_value(L"ShellDataTextStyle")).try_as<Style>());
    credentialOneHint.TextWrapping(TextWrapping::WrapWholeWords);

    TextBlock credentialTwoLabel;
    credentialTwoLabel.Style(Application::Current().Resources().Lookup(box_value(L"ShellLabelTextStyle")).try_as<Style>());
    PasswordBox credentialTwoBox;
    credentialTwoBox.PlaceholderText(L"Credential value");
    TextBlock credentialTwoHint;
    credentialTwoHint.Style(Application::Current().Resources().Lookup(box_value(L"ShellDataTextStyle")).try_as<Style>());
    credentialTwoHint.TextWrapping(TextWrapping::WrapWholeWords);

    StackPanel credentialPanel;
    credentialPanel.Spacing(8);
    addLabel(L"Credentials");
    credentialPanel.Children().Append(credentialOneLabel);
    credentialPanel.Children().Append(credentialOneBox);
    credentialPanel.Children().Append(credentialOneHint);
    credentialPanel.Children().Append(credentialTwoLabel);
    credentialPanel.Children().Append(credentialTwoBox);
    credentialPanel.Children().Append(credentialTwoHint);
    root.Children().Append(credentialPanel);

    TextBlock statusText;
    statusText.Style(Application::Current().Resources().Lookup(box_value(L"ShellDataTextStyle")).try_as<Style>());
    statusText.Text(L"Start with the provider module, then fill in only the route details you need for a working setup.");
    statusText.TextWrapping(TextWrapping::WrapWholeWords);
    root.Children().Append(statusText);

    Button createButton;
    createButton.Content(box_value(L"Create Provider Route"));
    createButton.Style(Application::Current().Resources().Lookup(box_value(L"ShellCommandButtonStyle")).try_as<Style>());
    root.Children().Append(createButton);

    const auto updateCapabilityForm = [&, this]() {
        const auto index = capabilitySelector.SelectedIndex();
        if (index < 0 || index >= static_cast<int>(currentSnapshot_.providerCapabilities.size())) {
            credentialOneLabel.Text(L"Credential");
            credentialOneHint.Text(L"No provider capability is selected.");
            credentialTwoLabel.Text(L"");
            credentialTwoHint.Text(L"");
            return;
        }

        const auto& capability = currentSnapshot_.providerCapabilities[static_cast<size_t>(index)];
        if (idBox.Text().empty()) {
            idBox.Text(winrt::hstring(capability.providerId));
        }
        if (displayNameBox.Text().empty()) {
            displayNameBox.Text(winrt::hstring(capability.displayName));
        }
        baseUrlBox.Text(winrt::hstring(capability.defaultBaseUrl));
        modelIdBox.Text(winrt::hstring(capability.recommendedModel));
        autonomousToggle.IsOn(capability.supportsAutonomousControl);

        const auto setCredential = [](const TextBlock& label,
                                      const PasswordBox& box,
                                      const TextBlock& hint,
                                      const std::optional<::MasterControlShell::ShellProviderCredentialField>& field) {
            if (!field.has_value()) {
                label.Text(L"");
                box.Visibility(Visibility::Collapsed);
                hint.Text(L"");
                hint.Visibility(Visibility::Collapsed);
                return;
            }

            box.Visibility(Visibility::Visible);
            hint.Visibility(Visibility::Visible);
            label.Text(winrt::hstring(field->label));
            box.PlaceholderText(winrt::hstring(field->placeholder.empty() ? L"Credential value" : field->placeholder));

            std::wstring hintText = field->helpText;
            if (!field->environmentVariableHint.empty()) {
                if (!hintText.empty()) {
                    hintText += L" ";
                }
                hintText += L"Env: " + field->environmentVariableHint;
            }
            hint.Text(winrt::hstring(hintText));
        };

        std::optional<::MasterControlShell::ShellProviderCredentialField> fieldOne;
        std::optional<::MasterControlShell::ShellProviderCredentialField> fieldTwo;
        if (!capability.credentialFields.empty()) {
            fieldOne = capability.credentialFields[0];
        }
        if (capability.credentialFields.size() > 1U) {
            fieldTwo = capability.credentialFields[1];
        }
        setCredential(credentialOneLabel, credentialOneBox, credentialOneHint, fieldOne);
        setCredential(credentialTwoLabel, credentialTwoBox, credentialTwoHint, fieldTwo);
    };

    capabilitySelector.SelectionChanged([&](IInspectable const&, Controls::SelectionChangedEventArgs const&) {
        updateCapabilityForm();
    });
    updateCapabilityForm();

    scrollViewer.Content(root);
    dialog.Content(scrollViewer);

    createButton.Click([this, dialog, createButton, statusText, capabilitySelector, idBox, displayNameBox, baseUrlBox, modelIdBox, enabledToggle, autonomousToggle, assignmentSelector, credentialOneBox, credentialTwoBox](IInspectable const&, RoutedEventArgs const&) {
        auto ignored = [this, dialog, createButton, statusText, capabilitySelector, idBox, displayNameBox, baseUrlBox, modelIdBox, enabledToggle, autonomousToggle, assignmentSelector, credentialOneBox, credentialTwoBox]() -> IAsyncAction {
            const auto capabilityIndex = capabilitySelector.SelectedIndex();
            if (capabilityIndex < 0 || capabilityIndex >= static_cast<int>(currentSnapshot_.providerCapabilities.size())) {
                statusText.Text(L"Select a provider module before creating the route.");
                co_return;
            }

            const auto& capability = currentSnapshot_.providerCapabilities[static_cast<size_t>(capabilityIndex)];
            const auto id = trimCopy(std::wstring(idBox.Text().c_str()));
            const auto displayName = trimCopy(std::wstring(displayNameBox.Text().c_str()));
            const auto baseUrl = trimCopy(std::wstring(baseUrlBox.Text().c_str()));
            const auto modelId = trimCopy(std::wstring(modelIdBox.Text().c_str()));
            if (id.empty() || displayName.empty() || baseUrl.empty()) {
                statusText.Text(L"Provider route ID, display name, and base URL are required.");
                co_return;
            }

            ::MasterControlShell::ShellProviderConnection provider{
                id,
                capability.kind.empty() ? L"generic" : capability.kind,
                displayName,
                baseUrl,
                modelId,
                enabledToggle.IsOn(),
                autonomousToggle.IsOn(),
                false
            };

            std::vector<std::pair<std::wstring, std::wstring>> credentialValues;
            if (!capability.credentialFields.empty()) {
                const auto value = std::wstring(credentialOneBox.Password().c_str());
                if (!value.empty()) {
                    credentialValues.emplace_back(capability.credentialFields[0].fieldId, value);
                }
            }
            if (capability.credentialFields.size() > 1U) {
                const auto value = std::wstring(credentialTwoBox.Password().c_str());
                if (!value.empty()) {
                    credentialValues.emplace_back(capability.credentialFields[1].fieldId, value);
                }
            }

            std::optional<::MasterControlShell::ShellProviderAssignment> assignment;
            const auto assignmentIndex = assignmentSelector.SelectedIndex();
            if (assignmentIndex > 0) {
                const auto targetIndex = static_cast<size_t>(assignmentIndex - 1);
                if (targetIndex < currentSnapshot_.providerAssignmentTargets.size()) {
                    const auto& target = currentSnapshot_.providerAssignmentTargets[targetIndex];
                    assignment = ::MasterControlShell::ShellProviderAssignment{ target.targetId, target.kind, provider.id, L"" };
                }
            }

            createButton.IsEnabled(false);
            statusText.Text(L"Creating the provider route through the local admin API.");

            winrt::apartment_context uiThread;
            co_await winrt::resume_background();
            const auto providerResult = runtime_.UpsertProvider(provider);
            if (!providerResult.succeeded) {
                co_await uiThread;
                statusText.Text(winrt::hstring(providerResult.message));
                createButton.IsEnabled(true);
                GuidedWorkflowStatusText().Text(L"Provider wizard needs attention. Review the wizard message and try again.");
                co_return;
            }

            if (!credentialValues.empty()) {
                const auto credentialsResult = runtime_.UpsertProviderCredentials(provider.id, credentialValues);
                if (!credentialsResult.succeeded) {
                    co_await uiThread;
                    statusText.Text(winrt::hstring(credentialsResult.message));
                    createButton.IsEnabled(true);
                    GuidedWorkflowStatusText().Text(L"Provider route was created, but credential setup still needs attention.");
                    co_return;
                }
            }

            if (assignment.has_value()) {
                const auto assignmentResult = runtime_.UpsertProviderAssignment(*assignment);
                if (!assignmentResult.succeeded) {
                    co_await uiThread;
                    statusText.Text(winrt::hstring(assignmentResult.message));
                    createButton.IsEnabled(true);
                    GuidedWorkflowStatusText().Text(L"Provider route was created, but assignment still needs attention.");
                    co_return;
                }
            }
            co_await uiThread;

            GuidedWorkflowStatusText().Text(winrt::hstring(L"Created provider route '" + displayName + L"'."));
            SetCurrentDestination(kProvidersDestination);
            auto refreshIgnored = RefreshAsync();
            (void)refreshIgnored;
            dialog.Hide();
        }();
        (void)ignored;
    });

    co_await dialog.ShowAsync();
}

IAsyncAction MainWindow::ShowAppleHostWizardAsync() {
    ContentDialog dialog;
    dialog.Title(box_value(L"New Apple Host Wizard"));
    dialog.CloseButtonText(L"Close");
    dialog.XamlRoot(RootGrid().XamlRoot());

    ScrollViewer scrollViewer;
    scrollViewer.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
    scrollViewer.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
    scrollViewer.MaxHeight(680);

    StackPanel root;
    root.Spacing(14);
    root.Width(580);

    TextBlock intro;
    intro.Style(Application::Current().Resources().Lookup(box_value(L"ShellBodyTextStyle")).try_as<Style>());
    intro.Text(L"Step 1: name the Apple host. Step 2: choose SSH or Companion Service. Step 3: publish the host defaults that CLU and the Apple governance lanes will use.");
    intro.TextWrapping(TextWrapping::WrapWholeWords);
    root.Children().Append(intro);

    const auto addLabel = [&root](std::wstring const& text) {
        TextBlock label;
        label.Style(Application::Current().Resources().Lookup(box_value(L"ShellLabelTextStyle")).try_as<Style>());
        label.Text(winrt::hstring(text));
        root.Children().Append(label);
    };

    TextBox idBox;
    idBox.PlaceholderText(L"apple-host-01");
    root.Children().Append([&]() {
        addLabel(L"Step 1. Apple Host ID");
        return idBox;
    }());

    TextBox displayNameBox;
    displayNameBox.PlaceholderText(L"Primary Apple Build Host");
    root.Children().Append([&]() {
        addLabel(L"Display Name");
        return displayNameBox;
    }());

    ComboBox transportSelector;
    ComboBoxItem companionItem;
    companionItem.Content(box_value(L"Companion Service"));
    companionItem.Tag(box_value(L"companion_service"));
    transportSelector.Items().Append(companionItem);
    ComboBoxItem sshItem;
    sshItem.Content(box_value(L"SSH"));
    sshItem.Tag(box_value(L"ssh"));
    transportSelector.Items().Append(sshItem);
    transportSelector.SelectedIndex(0);
    root.Children().Append([&]() {
        addLabel(L"Step 2. Transport");
        return transportSelector;
    }());

    Grid endpointGrid;
    endpointGrid.ColumnSpacing(12);
    endpointGrid.ColumnDefinitions().Append(ColumnDefinition());
    endpointGrid.ColumnDefinitions().Append(ColumnDefinition());
    endpointGrid.ColumnDefinitions().Append(ColumnDefinition());

    TextBox addressBox;
    addressBox.PlaceholderText(L"mac-builder.local");
    TextBox portBox;
    portBox.PlaceholderText(L"8081");
    portBox.Text(L"8081");
    TextBox usernameBox;
    usernameBox.PlaceholderText(L"builder");

    StackPanel addressPanel;
    addressPanel.Spacing(6);
    TextBlock addressLabel;
    addressLabel.Style(Application::Current().Resources().Lookup(box_value(L"ShellLabelTextStyle")).try_as<Style>());
    addressLabel.Text(L"Address or Hostname");
    addressPanel.Children().Append(addressLabel);
    addressPanel.Children().Append(addressBox);

    StackPanel portPanel;
    portPanel.Spacing(6);
    TextBlock portLabel;
    portLabel.Style(Application::Current().Resources().Lookup(box_value(L"ShellLabelTextStyle")).try_as<Style>());
    portLabel.Text(L"Port");
    portPanel.Children().Append(portLabel);
    portPanel.Children().Append(portBox);
    Grid::SetColumn(portPanel, 1);

    StackPanel usernamePanel;
    usernamePanel.Spacing(6);
    TextBlock usernameLabel;
    usernameLabel.Style(Application::Current().Resources().Lookup(box_value(L"ShellLabelTextStyle")).try_as<Style>());
    usernameLabel.Text(L"SSH Username");
    usernamePanel.Children().Append(usernameLabel);
    usernamePanel.Children().Append(usernameBox);
    Grid::SetColumn(usernamePanel, 2);

    endpointGrid.Children().Append(addressPanel);
    endpointGrid.Children().Append(portPanel);
    endpointGrid.Children().Append(usernamePanel);
    root.Children().Append(endpointGrid);

    TextBox serviceBaseUrlBox;
    serviceBaseUrlBox.PlaceholderText(L"http://mac-builder.local:8081");
    root.Children().Append([&]() {
        addLabel(L"Companion Base URL");
        return serviceBaseUrlBox;
    }());

    Grid companionGrid;
    companionGrid.ColumnSpacing(12);
    companionGrid.ColumnDefinitions().Append(ColumnDefinition());
    companionGrid.ColumnDefinitions().Append(ColumnDefinition());

    TextBox healthPathBox;
    healthPathBox.PlaceholderText(L"/healthz");
    healthPathBox.Text(L"/healthz");
    TextBox executePathBox;
    executePathBox.PlaceholderText(L"/execute");
    executePathBox.Text(L"/execute");

    StackPanel healthPanel;
    healthPanel.Spacing(6);
    TextBlock healthLabel;
    healthLabel.Style(Application::Current().Resources().Lookup(box_value(L"ShellLabelTextStyle")).try_as<Style>());
    healthLabel.Text(L"Companion Health Path");
    healthPanel.Children().Append(healthLabel);
    healthPanel.Children().Append(healthPathBox);

    StackPanel executePanel;
    executePanel.Spacing(6);
    TextBlock executeLabel;
    executeLabel.Style(Application::Current().Resources().Lookup(box_value(L"ShellLabelTextStyle")).try_as<Style>());
    executeLabel.Text(L"Companion Execute Path");
    executePanel.Children().Append(executeLabel);
    executePanel.Children().Append(executePathBox);
    Grid::SetColumn(executePanel, 1);

    companionGrid.Children().Append(healthPanel);
    companionGrid.Children().Append(executePanel);
    root.Children().Append(companionGrid);

    TextBox developerDirectoryBox;
    developerDirectoryBox.PlaceholderText(L"/Applications/Xcode.app/Contents/Developer");
    root.Children().Append([&]() {
        addLabel(L"Step 3. Preferred Developer Directory");
        return developerDirectoryBox;
    }());

    Grid defaultsGrid;
    defaultsGrid.ColumnSpacing(12);
    defaultsGrid.ColumnDefinitions().Append(ColumnDefinition());
    defaultsGrid.ColumnDefinitions().Append(ColumnDefinition());

    TextBox signingIdentityBox;
    signingIdentityBox.PlaceholderText(L"Developer ID Application: Example Corp");
    TextBox notaryProfileBox;
    notaryProfileBox.PlaceholderText(L"mastercontrol-notary");

    StackPanel signingPanel;
    signingPanel.Spacing(6);
    TextBlock signingLabel;
    signingLabel.Style(Application::Current().Resources().Lookup(box_value(L"ShellLabelTextStyle")).try_as<Style>());
    signingLabel.Text(L"Default Signing Identity");
    signingPanel.Children().Append(signingLabel);
    signingPanel.Children().Append(signingIdentityBox);

    StackPanel notaryProfilePanel;
    notaryProfilePanel.Spacing(6);
    TextBlock notaryProfileLabel;
    notaryProfileLabel.Style(Application::Current().Resources().Lookup(box_value(L"ShellLabelTextStyle")).try_as<Style>());
    notaryProfileLabel.Text(L"Default Notary Profile");
    notaryProfilePanel.Children().Append(notaryProfileLabel);
    notaryProfilePanel.Children().Append(notaryProfileBox);
    Grid::SetColumn(notaryProfilePanel, 1);

    defaultsGrid.Children().Append(signingPanel);
    defaultsGrid.Children().Append(notaryProfilePanel);
    root.Children().Append(defaultsGrid);

    TextBox teamIdBox;
    teamIdBox.PlaceholderText(L"ABCDE12345");
    root.Children().Append([&]() {
        addLabel(L"Default Notary Team ID");
        return teamIdBox;
    }());

    StackPanel platformPanel;
    platformPanel.Orientation(Orientation::Horizontal);
    platformPanel.Spacing(18);
    CheckBox macPlatformCheckBox;
    macPlatformCheckBox.Content(box_value(L"macOS"));
    macPlatformCheckBox.IsChecked(true);
    CheckBox iosPlatformCheckBox;
    iosPlatformCheckBox.Content(box_value(L"iOS"));
    iosPlatformCheckBox.IsChecked(true);
    CheckBox enabledCheckBox;
    enabledCheckBox.Content(box_value(L"Enabled"));
    enabledCheckBox.IsChecked(true);
    platformPanel.Children().Append(macPlatformCheckBox);
    platformPanel.Children().Append(iosPlatformCheckBox);
    platformPanel.Children().Append(enabledCheckBox);
    root.Children().Append(platformPanel);

    TextBlock statusText;
    statusText.Style(Application::Current().Resources().Lookup(box_value(L"ShellDataTextStyle")).try_as<Style>());
    statusText.Text(L"Register a real Apple execution host here so the macOS and iOS governance lanes become routeable.");
    statusText.TextWrapping(TextWrapping::WrapWholeWords);
    root.Children().Append(statusText);

    Button createButton;
    createButton.Content(box_value(L"Create Apple Host"));
    createButton.Style(Application::Current().Resources().Lookup(box_value(L"ShellCommandButtonStyle")).try_as<Style>());
    root.Children().Append(createButton);

    const auto updateTransportForm = [&, this]() {
        const auto selectedItem = transportSelector.SelectedItem().try_as<ComboBoxItem>();
        const auto tag = selectedItem == nullptr
            ? std::wstring(L"companion_service")
            : std::wstring(unbox_value_or<hstring>(selectedItem.Tag(), hstring(L"companion_service")).c_str());
        const bool companionSelected = tag == L"companion_service";
        setElementVisibility(serviceBaseUrlBox, companionSelected);
        setElementVisibility(companionGrid, companionSelected);
        setElementVisibility(usernamePanel, !companionSelected);
        if (companionSelected) {
            if (portBox.Text().empty()) {
                portBox.Text(L"8081");
            }
            statusText.Text(L"Companion Service is ideal when you want structured readiness and execution endpoints from the remote Mac.");
        } else {
            if (portBox.Text().empty() || portBox.Text() == L"8081") {
                portBox.Text(L"22");
            }
            statusText.Text(L"SSH is ideal when you want direct command execution against a remote Mac host.");
        }
    };

    transportSelector.SelectionChanged([&](IInspectable const&, Controls::SelectionChangedEventArgs const&) {
        updateTransportForm();
    });
    updateTransportForm();

    scrollViewer.Content(root);
    dialog.Content(scrollViewer);

    createButton.Click([this, dialog, createButton, statusText, idBox, displayNameBox, transportSelector, addressBox, portBox, usernameBox, serviceBaseUrlBox, healthPathBox, executePathBox, developerDirectoryBox, signingIdentityBox, notaryProfileBox, teamIdBox, macPlatformCheckBox, iosPlatformCheckBox, enabledCheckBox](IInspectable const&, RoutedEventArgs const&) {
        auto ignored = [this, dialog, createButton, statusText, idBox, displayNameBox, transportSelector, addressBox, portBox, usernameBox, serviceBaseUrlBox, healthPathBox, executePathBox, developerDirectoryBox, signingIdentityBox, notaryProfileBox, teamIdBox, macPlatformCheckBox, iosPlatformCheckBox, enabledCheckBox]() -> IAsyncAction {
            const auto id = trimCopy(std::wstring(idBox.Text().c_str()));
            const auto displayName = trimCopy(std::wstring(displayNameBox.Text().c_str()));
            const auto address = trimCopy(std::wstring(addressBox.Text().c_str()));
            const auto portText = trimCopy(std::wstring(portBox.Text().c_str()));
            const auto username = trimCopy(std::wstring(usernameBox.Text().c_str()));
            const auto serviceBaseUrl = trimCopy(std::wstring(serviceBaseUrlBox.Text().c_str()));
            const auto healthPath = trimCopy(std::wstring(healthPathBox.Text().c_str()));
            const auto executePath = trimCopy(std::wstring(executePathBox.Text().c_str()));
            const auto developerDirectory = trimCopy(std::wstring(developerDirectoryBox.Text().c_str()));
            const auto signingIdentity = trimCopy(std::wstring(signingIdentityBox.Text().c_str()));
            const auto notaryProfile = trimCopy(std::wstring(notaryProfileBox.Text().c_str()));
            const auto teamId = trimCopy(std::wstring(teamIdBox.Text().c_str()));

            if (id.empty() || displayName.empty()) {
                statusText.Text(L"Apple host ID and display name are required.");
                co_return;
            }

            uint16_t port = 0;
            if (!portText.empty()) {
                try {
                    const auto parsedPort = std::stoul(portText);
                    if (parsedPort > 65535U) {
                        statusText.Text(L"Port must be blank or between 0 and 65535.");
                        co_return;
                    }
                    port = static_cast<uint16_t>(parsedPort);
                } catch (...) {
                    statusText.Text(L"Port must be blank or between 0 and 65535.");
                    co_return;
                }
            }

            const auto selectedItem = transportSelector.SelectedItem().try_as<ComboBoxItem>();
            const auto transport = selectedItem == nullptr
                ? std::wstring(L"companion_service")
                : std::wstring(unbox_value_or<hstring>(selectedItem.Tag(), hstring(L"companion_service")).c_str());
            std::vector<std::wstring> platforms;
            const auto macChecked = macPlatformCheckBox.IsChecked();
            const auto iosChecked = iosPlatformCheckBox.IsChecked();
            if (macChecked && macChecked.Value()) {
                platforms.push_back(L"macos");
            }
            if (iosChecked && iosChecked.Value()) {
                platforms.push_back(L"ios");
            }

            createButton.IsEnabled(false);
            statusText.Text(L"Registering the Apple execution host through the local admin API.");

            ::MasterControlShell::ShellAppleRemoteHost host;
            host.hostId = id;
            host.displayName = displayName;
            host.transport = transport;
            host.platforms = std::move(platforms);
            host.address = address.empty() ? L"127.0.0.1" : address;
            host.port = port;
            host.username = username;
            host.serviceBaseUrl = serviceBaseUrl;
            host.companionHealthPath = healthPath.empty() ? L"/healthz" : healthPath;
            host.companionExecutePath = executePath.empty() ? L"/execute" : executePath;
            host.preferredDeveloperDirectory = developerDirectory;
            host.defaultSigningIdentity = signingIdentity;
            host.defaultNotaryKeychainProfile = notaryProfile;
            host.defaultNotaryTeamId = teamId;
            const auto enabled = enabledCheckBox.IsChecked();
            host.enabled = !enabled || enabled.Value();

            winrt::apartment_context uiThread;
            co_await winrt::resume_background();
            const auto result = runtime_.UpsertAppleRemoteHost(host);
            co_await uiThread;

            statusText.Text(winrt::hstring(result.message));
            if (!result.succeeded) {
                createButton.IsEnabled(true);
                GuidedWorkflowStatusText().Text(L"Apple host wizard needs attention. Review the message inside the wizard and try again.");
                co_return;
            }

            GuidedWorkflowStatusText().Text(winrt::hstring(L"Registered Apple host '" + displayName + L"' for CLU and platform governance."));
            SetCurrentDestination(kRuntimeDestination);
            auto refreshIgnored = RefreshAsync();
            (void)refreshIgnored;
            dialog.Hide();
        }();
        (void)ignored;
    });

    co_await dialog.ShowAsync();
}

IAsyncAction MainWindow::ShowProviderAssignmentWizardAsync() {
    ContentDialog dialog;
    dialog.Title(box_value(L"Provider Assignment Wizard"));
    dialog.CloseButtonText(L"Close");
    dialog.XamlRoot(RootGrid().XamlRoot());

    ScrollViewer scrollViewer;
    scrollViewer.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
    scrollViewer.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
    scrollViewer.MaxHeight(560);

    StackPanel root;
    root.Spacing(14);
    root.Width(540);

    TextBlock intro;
    intro.Style(Application::Current().Resources().Lookup(box_value(L"ShellBodyTextStyle")).try_as<Style>());
    intro.Text(L"Step 1: choose the provider route. Step 2: choose the orchestration lane that should own it. Step 3: save the assignment so CLU and provider execution agree on routing.");
    intro.TextWrapping(TextWrapping::WrapWholeWords);
    root.Children().Append(intro);

    const auto addLabel = [&root](std::wstring const& text) {
        TextBlock label;
        label.Style(Application::Current().Resources().Lookup(box_value(L"ShellLabelTextStyle")).try_as<Style>());
        label.Text(winrt::hstring(text));
        root.Children().Append(label);
    };

    ComboBox providerSelector;
    for (const auto& provider : currentSnapshot_.providers) {
        ComboBoxItem item;
        item.Content(box_value(winrt::hstring(provider.displayName.empty() ? provider.id : provider.displayName)));
        item.Tag(box_value(winrt::hstring(provider.id)));
        providerSelector.Items().Append(item);
    }
    if (providerSelector.Items().Size() > 0) {
        providerSelector.SelectedIndex(0);
    }
    root.Children().Append([&]() {
        addLabel(L"Step 1. Provider Route");
        return providerSelector;
    }());

    ComboBox targetSelector;
    for (const auto& target : currentSnapshot_.providerAssignmentTargets) {
        ComboBoxItem item;
        std::wstring label = target.displayName.empty() ? target.targetId : target.displayName;
        if (!target.kind.empty()) {
            label += L"  |  " + target.kind;
        }
        item.Content(box_value(winrt::hstring(label)));
        item.Tag(box_value(winrt::hstring(target.targetId)));
        targetSelector.Items().Append(item);
    }
    if (targetSelector.Items().Size() > 0) {
        targetSelector.SelectedIndex(0);
    }
    root.Children().Append([&]() {
        addLabel(L"Step 2. Orchestration Target");
        return targetSelector;
    }());

    TextBlock summaryText;
    summaryText.Style(Application::Current().Resources().Lookup(box_value(L"ShellDataTextStyle")).try_as<Style>());
    summaryText.TextWrapping(TextWrapping::WrapWholeWords);
    root.Children().Append(summaryText);

    TextBlock statusText;
    statusText.Style(Application::Current().Resources().Lookup(box_value(L"ShellDataTextStyle")).try_as<Style>());
    statusText.Text(L"Use this wizard when you want a role, specialist group, or sub-agent lane to take ownership of a provider.");
    statusText.TextWrapping(TextWrapping::WrapWholeWords);
    root.Children().Append(statusText);

    Button createButton;
    createButton.Content(box_value(L"Save Assignment"));
    createButton.Style(Application::Current().Resources().Lookup(box_value(L"ShellCommandButtonStyle")).try_as<Style>());
    root.Children().Append(createButton);

    const auto updateSummary = [&, this]() {
        if (providerSelector.SelectedIndex() < 0 || targetSelector.SelectedIndex() < 0 ||
            currentSnapshot_.providers.empty() || currentSnapshot_.providerAssignmentTargets.empty()) {
            summaryText.Text(L"Create at least one provider route and one orchestration target before assigning ownership.");
            return;
        }

        const auto& provider = currentSnapshot_.providers[static_cast<size_t>(providerSelector.SelectedIndex())];
        const auto& target = currentSnapshot_.providerAssignmentTargets[static_cast<size_t>(targetSelector.SelectedIndex())];
        std::wstring summary = L"This will assign ";
        summary += provider.displayName.empty() ? provider.id : provider.displayName;
        summary += L" to ";
        summary += target.displayName.empty() ? target.targetId : target.displayName;
        if (!target.kind.empty()) {
            summary += L" (" + target.kind + L")";
        }
        summaryText.Text(winrt::hstring(summary));
    };

    providerSelector.SelectionChanged([&](IInspectable const&, Controls::SelectionChangedEventArgs const&) {
        updateSummary();
    });
    targetSelector.SelectionChanged([&](IInspectable const&, Controls::SelectionChangedEventArgs const&) {
        updateSummary();
    });
    updateSummary();

    scrollViewer.Content(root);
    dialog.Content(scrollViewer);

    createButton.Click([this, dialog, createButton, statusText, providerSelector, targetSelector](IInspectable const&, RoutedEventArgs const&) {
        auto ignored = [this, dialog, createButton, statusText, providerSelector, targetSelector]() -> IAsyncAction {
            if (providerSelector.SelectedIndex() < 0 || providerSelector.SelectedIndex() >= static_cast<int>(currentSnapshot_.providers.size())) {
                statusText.Text(L"Create or select a provider route before saving an assignment.");
                co_return;
            }
            if (targetSelector.SelectedIndex() < 0 || targetSelector.SelectedIndex() >= static_cast<int>(currentSnapshot_.providerAssignmentTargets.size())) {
                statusText.Text(L"Select an orchestration target before saving an assignment.");
                co_return;
            }

            const auto& provider = currentSnapshot_.providers[static_cast<size_t>(providerSelector.SelectedIndex())];
            const auto& target = currentSnapshot_.providerAssignmentTargets[static_cast<size_t>(targetSelector.SelectedIndex())];

            createButton.IsEnabled(false);
            statusText.Text(L"Saving provider ownership through the local admin API.");

            winrt::apartment_context uiThread;
            co_await winrt::resume_background();
            const auto result = runtime_.UpsertProviderAssignment(::MasterControlShell::ShellProviderAssignment{
                target.targetId,
                target.kind,
                provider.id,
                L""
            });
            co_await uiThread;

            statusText.Text(winrt::hstring(result.message));
            if (!result.succeeded) {
                createButton.IsEnabled(true);
                GuidedWorkflowStatusText().Text(L"Provider assignment wizard needs attention. Review the message inside the wizard and try again.");
                co_return;
            }

            GuidedWorkflowStatusText().Text(winrt::hstring(L"Assigned provider '" +
                (provider.displayName.empty() ? provider.id : provider.displayName) +
                L"' to " +
                (target.displayName.empty() ? target.targetId : target.displayName) + L"."));
            SetCurrentDestination(kProvidersDestination);
            auto refreshIgnored = RefreshAsync();
            (void)refreshIgnored;
            dialog.Hide();
        }();
        (void)ignored;
    });

    co_await dialog.ShowAsync();
}

IAsyncAction MainWindow::ShowImportWizardAsync() {
    ContentDialog dialog;
    dialog.Title(box_value(L"Guided Import Wizard"));
    dialog.CloseButtonText(L"Close");
    dialog.XamlRoot(RootGrid().XamlRoot());

    ScrollViewer scrollViewer;
    scrollViewer.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
    scrollViewer.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
    scrollViewer.MaxHeight(680);

    StackPanel root;
    root.Spacing(14);
    root.Width(580);

    TextBlock intro;
    intro.Style(Application::Current().Resources().Lookup(box_value(L"ShellBodyTextStyle")).try_as<Style>());
    intro.Text(L"Step 1: choose what kind of install input you have. Step 2: provide the source and manifest details. Step 3: launch the import and let the orchestration server stage the software for you.");
    intro.TextWrapping(TextWrapping::WrapWholeWords);
    root.Children().Append(intro);

    const auto addLabel = [&root](std::wstring const& text) {
        TextBlock label;
        label.Style(Application::Current().Resources().Lookup(box_value(L"ShellLabelTextStyle")).try_as<Style>());
        label.Text(winrt::hstring(text));
        root.Children().Append(label);
    };

    ComboBox modeSelector;
    ComboBoxItem packageModeItem;
    packageModeItem.Content(box_value(L"Installer Package"));
    packageModeItem.Tag(box_value(L"package"));
    modeSelector.Items().Append(packageModeItem);
    ComboBoxItem repoModeItem;
    repoModeItem.Content(box_value(L"Bootstrap Repository"));
    repoModeItem.Tag(box_value(L"repo"));
    modeSelector.Items().Append(repoModeItem);
    ComboBoxItem zipModeItem;
    zipModeItem.Content(box_value(L"Zip Bundle"));
    zipModeItem.Tag(box_value(L"zip"));
    modeSelector.Items().Append(zipModeItem);
    modeSelector.SelectedIndex(0);
    root.Children().Append([&]() {
        addLabel(L"Step 1. Import Type");
        return modeSelector;
    }());

    StackPanel packagePanel;
    packagePanel.Spacing(10);
    ComboBox packageKindSelector;
    ComboBoxItem exeItem;
    exeItem.Content(box_value(L"EXE"));
    exeItem.Tag(box_value(L"exe"));
    packageKindSelector.Items().Append(exeItem);
    ComboBoxItem msiItem;
    msiItem.Content(box_value(L"MSI"));
    msiItem.Tag(box_value(L"msi"));
    packageKindSelector.Items().Append(msiItem);
    ComboBoxItem psItem;
    psItem.Content(box_value(L"PowerShell"));
    psItem.Tag(box_value(L"powershell"));
    packageKindSelector.Items().Append(psItem);
    packageKindSelector.SelectedIndex(0);
    TextBox packageSourceBox;
    packageSourceBox.PlaceholderText(L"https://example.com/tool.exe or D:\\Installers\\tool.exe");
    TextBox packageArgumentsBox;
    packageArgumentsBox.PlaceholderText(L"/quiet /norestart");
    ToggleSwitch packageTrustToggle;
    packageTrustToggle.Header(box_value(L"Allow untrusted execution"));
    packagePanel.Children().Append([&]() {
        addLabel(L"Step 2. Installer Kind");
        return packageKindSelector;
    }());
    packagePanel.Children().Append([&]() {
        addLabel(L"Package Source or Local Path");
        return packageSourceBox;
    }());
    packagePanel.Children().Append([&]() {
        addLabel(L"Arguments");
        return packageArgumentsBox;
    }());
    packagePanel.Children().Append(packageTrustToggle);
    root.Children().Append(packagePanel);

    StackPanel repoPanel;
    repoPanel.Spacing(10);
    TextBox repoUrlBox;
    repoUrlBox.PlaceholderText(L"https://github.com/org/repo.git or D:\\Repos\\repo-fixture");
    TextBox repoBranchBox;
    repoBranchBox.PlaceholderText(L"main");
    repoBranchBox.Text(L"main");
    TextBox repoManifestBox;
    repoManifestBox.PlaceholderText(L"mcp-bootstrap.json");
    repoManifestBox.Text(L"mcp-bootstrap.json");
    ToggleSwitch repoTrustToggle;
    repoTrustToggle.Header(box_value(L"Allow untrusted execution"));
    repoPanel.Children().Append([&]() {
        addLabel(L"Step 2. Repository URL or Local Path");
        return repoUrlBox;
    }());
    repoPanel.Children().Append([&]() {
        addLabel(L"Branch");
        return repoBranchBox;
    }());
    repoPanel.Children().Append([&]() {
        addLabel(L"Manifest File");
        return repoManifestBox;
    }());
    repoPanel.Children().Append(repoTrustToggle);
    root.Children().Append(repoPanel);

    StackPanel zipPanel;
    zipPanel.Spacing(10);
    TextBox zipSourceBox;
    zipSourceBox.PlaceholderText(L"D:\\Bundles\\tooling.zip");
    TextBox zipManifestBox;
    zipManifestBox.PlaceholderText(L"mcp-bootstrap.json");
    zipManifestBox.Text(L"mcp-bootstrap.json");
    ToggleSwitch zipTrustToggle;
    zipTrustToggle.Header(box_value(L"Allow untrusted execution"));
    zipPanel.Children().Append([&]() {
        addLabel(L"Step 2. Zip Source or Local Path");
        return zipSourceBox;
    }());
    zipPanel.Children().Append([&]() {
        addLabel(L"Manifest File");
        return zipManifestBox;
    }());
    zipPanel.Children().Append(zipTrustToggle);
    root.Children().Append(zipPanel);

    TextBlock statusText;
    statusText.Style(Application::Current().Resources().Lookup(box_value(L"ShellDataTextStyle")).try_as<Style>());
    statusText.Text(L"Guided import keeps the operator in one place instead of switching between import tabs and raw forms.");
    statusText.TextWrapping(TextWrapping::WrapWholeWords);
    root.Children().Append(statusText);

    Button createButton;
    createButton.Content(box_value(L"Run Guided Import"));
    createButton.Style(Application::Current().Resources().Lookup(box_value(L"ShellCommandButtonStyle")).try_as<Style>());
    root.Children().Append(createButton);

    const auto updateImportMode = [&, this]() {
        const auto selectedItem = modeSelector.SelectedItem().try_as<ComboBoxItem>();
        const auto tag = selectedItem == nullptr
            ? std::wstring(L"package")
            : std::wstring(unbox_value_or<hstring>(selectedItem.Tag(), hstring(L"package")).c_str());
        setElementVisibility(packagePanel, tag == L"package");
        setElementVisibility(repoPanel, tag == L"repo");
        setElementVisibility(zipPanel, tag == L"zip");
        if (tag == L"package") {
            statusText.Text(L"Use this when you already have an EXE, MSI, or PowerShell installer.");
        } else if (tag == L"repo") {
            statusText.Text(L"Use this when the software is staged through a repository bootstrap manifest.");
        } else {
            statusText.Text(L"Use this when the software is packaged as a zip bundle with a manifest.");
        }
    };

    modeSelector.SelectionChanged([&](IInspectable const&, Controls::SelectionChangedEventArgs const&) {
        updateImportMode();
    });
    updateImportMode();

    scrollViewer.Content(root);
    dialog.Content(scrollViewer);

    createButton.Click([this, dialog, createButton, statusText, modeSelector, packageKindSelector, packageSourceBox, packageArgumentsBox, packageTrustToggle, repoUrlBox, repoBranchBox, repoManifestBox, repoTrustToggle, zipSourceBox, zipManifestBox, zipTrustToggle](IInspectable const&, RoutedEventArgs const&) {
        auto ignored = [this, dialog, createButton, statusText, modeSelector, packageKindSelector, packageSourceBox, packageArgumentsBox, packageTrustToggle, repoUrlBox, repoBranchBox, repoManifestBox, repoTrustToggle, zipSourceBox, zipManifestBox, zipTrustToggle]() -> IAsyncAction {
            const auto selectedItem = modeSelector.SelectedItem().try_as<ComboBoxItem>();
            const auto mode = selectedItem == nullptr
                ? std::wstring(L"package")
                : std::wstring(unbox_value_or<hstring>(selectedItem.Tag(), hstring(L"package")).c_str());

            createButton.IsEnabled(false);
            statusText.Text(L"Submitting the guided import through the local admin API.");

            winrt::apartment_context uiThread;
            co_await winrt::resume_background();

            ::MasterControlShell::ShellOperationResult result;
            if (mode == L"repo") {
                result = runtime_.InstallRepository(::MasterControlShell::ShellBootstrapRepoSpec{
                    trimCopy(std::wstring(repoUrlBox.Text().c_str())),
                    trimCopy(std::wstring(repoBranchBox.Text().c_str())).empty() ? L"main" : trimCopy(std::wstring(repoBranchBox.Text().c_str())),
                    trimCopy(std::wstring(repoManifestBox.Text().c_str())).empty() ? L"mcp-bootstrap.json" : trimCopy(std::wstring(repoManifestBox.Text().c_str())),
                    repoTrustToggle.IsOn()
                });
            } else if (mode == L"zip") {
                result = runtime_.InstallZipBundle(::MasterControlShell::ShellZipBundleSpec{
                    trimCopy(std::wstring(zipSourceBox.Text().c_str())),
                    trimCopy(std::wstring(zipManifestBox.Text().c_str())).empty() ? L"mcp-bootstrap.json" : trimCopy(std::wstring(zipManifestBox.Text().c_str())),
                    zipTrustToggle.IsOn()
                });
            } else {
                std::wstring packageKind = L"exe";
                if (const auto packageKindItem = packageKindSelector.SelectedItem().try_as<ComboBoxItem>()) {
                    packageKind = unbox_value_or<hstring>(packageKindItem.Tag(), hstring(L"exe")).c_str();
                }
                ::MasterControlShell::ShellInstallerKind kind = ::MasterControlShell::ShellInstallerKind::Exe;
                if (packageKind == L"msi") {
                    kind = ::MasterControlShell::ShellInstallerKind::Msi;
                } else if (packageKind == L"powershell") {
                    kind = ::MasterControlShell::ShellInstallerKind::PowerShell;
                }
                result = runtime_.InstallPackage(::MasterControlShell::ShellInstallerPackageSpec{
                    kind,
                    trimCopy(std::wstring(packageSourceBox.Text().c_str())),
                    trimCopy(std::wstring(packageArgumentsBox.Text().c_str())),
                    packageTrustToggle.IsOn()
                });
            }

            co_await uiThread;
            statusText.Text(winrt::hstring(result.message));
            if (!result.succeeded) {
                createButton.IsEnabled(true);
                GuidedWorkflowStatusText().Text(L"Guided import needs attention. Review the wizard message and try again.");
                co_return;
            }

            GuidedWorkflowStatusText().Text(L"Guided import dispatched successfully.");
            SetCurrentDestination(kImportsDestination);
            auto refreshIgnored = RefreshAsync();
            (void)refreshIgnored;
            dialog.Hide();
        }();
        (void)ignored;
    });

    co_await dialog.ShowAsync();
}

IAsyncAction MainWindow::OpenOverlayRouteAsync(std::wstring routeId) {
    const auto iterator = std::find_if(
        currentSnapshot_.overlayRoutes.begin(),
        currentSnapshot_.overlayRoutes.end(),
        [&routeId](const auto& route) { return route.id == routeId; });

    if (iterator == currentSnapshot_.overlayRoutes.end()) {
        UpdateStatusBar(L"Unable to resolve the requested Forsetti overlay route.", InfoBarSeverity::Warning);
        co_return;
    }

    std::wstring workspaceDestination;
    FrameworkElement content{ nullptr };
    if (iterator->targetsModuleView && !iterator->viewId.empty()) {
        content = CreateViewForViewId(iterator->viewId, false);
        workspaceDestination = destinationForViewId(iterator->viewId);
    } else if (!iterator->destinationId.empty()) {
        workspaceDestination = iterator->destinationId;
        content = ResolvePrimaryViewForDestination(workspaceDestination, currentSnapshot_);
    }

    if (content == nullptr) {
        content = CreateUnavailableView(
            L"Forsetti Overlay Unavailable",
            L"The selected route did not publish a hostable overlay destination.");
    }

    ContentDialog dialog;
    dialog.Title(box_value(hstring(iterator->label.empty() ? routeId : iterator->label)));
    dialog.CloseButtonText(L"Close");
    if (!workspaceDestination.empty()) {
        dialog.PrimaryButtonText(L"Open In Workspace");
    }
    if (iterator->presentation == ::MasterControlShell::ShellOverlayPresentation::FullScreen) {
        dialog.FullSizeDesired(true);
    }

    ScrollViewer scrollViewer;
    scrollViewer.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
    scrollViewer.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
    scrollViewer.Content(content);
    dialog.Content(scrollViewer);
    dialog.XamlRoot(RootGrid().XamlRoot());

    if (co_await dialog.ShowAsync() == ContentDialogResult::Primary && !workspaceDestination.empty()) {
        SetCurrentDestination(workspaceDestination);
    }
}

IAsyncAction MainWindow::ShowDialogAsync(winrt::hstring const& title, winrt::hstring const& message) {
    ContentDialog dialog;
    dialog.Title(box_value(title));
    dialog.Content(box_value(message));
    dialog.CloseButtonText(L"Close");
    dialog.XamlRoot(RootGrid().XamlRoot());
    co_await dialog.ShowAsync();
}

} // namespace winrt::MasterControlShell::implementation
