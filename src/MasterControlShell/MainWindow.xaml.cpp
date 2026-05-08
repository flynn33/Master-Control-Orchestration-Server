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
#include "../../include/MasterControl/DeploymentLogPaths.h"
#include "microsoft.ui.xaml.window.h"
#include "OverviewSectionControl.xaml.h"
#include "RuntimeSectionControl.xaml.h"
#include "ShellFormatting.h"
#include "SecuritySectionControl.xaml.h"
#include "SetupWizardBuilder.h"
#include "SettingsSectionControl.xaml.h"
#include "TelemetrySectionControl.xaml.h"

#include <winrt/Microsoft.UI.Interop.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Composition.SystemBackdrops.h>

namespace winrt::MasterControlShell::implementation {

using namespace Microsoft::UI::Dispatching;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;
using namespace Windows::Foundation;
using namespace Windows::UI;
using namespace ::MasterControlShell::Presentation;

namespace {

constexpr wchar_t kProductDisplayName[] = L"Master Control Orchestration Server";
constexpr wchar_t kOverviewDestination[] = L"overview";
constexpr wchar_t kTelemetryDestination[] = L"telemetry";
constexpr wchar_t kRuntimeDestination[] = L"runtime";
constexpr wchar_t kCluDestination[] = L"clu";
constexpr wchar_t kImportsDestination[] = L"imports";
constexpr wchar_t kExportsDestination[] = L"exports";
constexpr wchar_t kSecurityDestination[] = L"security";
constexpr wchar_t kSettingsDestination[] = L"settings";
constexpr wchar_t kSetupWizardDestination[] = L"setup-wizard";
constexpr wchar_t kSetupReadinessDestination[] = L"setup-readiness";

constexpr wchar_t kOverviewView[] = L"OverviewSectionView";
constexpr wchar_t kTelemetryView[] = L"TelemetrySectionView";
constexpr wchar_t kRuntimeView[] = L"RuntimeSectionView";
constexpr wchar_t kCluView[] = L"CommandLogicUnitSectionView";
constexpr wchar_t kImportsView[] = L"ImportsSectionView";
constexpr wchar_t kExportsView[] = L"ExportsSectionView";
constexpr wchar_t kSecurityView[] = L"SecuritySectionView";
constexpr wchar_t kSettingsView[] = L"SettingsSectionView";
constexpr wchar_t kSetupWizardView[] = L"SetupWizardView";
constexpr wchar_t kSetupReadinessView[] = L"SetupReadinessView";

static bool isInteractiveFormSection(const std::wstring& viewId) {
    return viewId == kSecurityView
        || viewId == kSettingsView
        || viewId == kImportsView;
}

// v0.7.4: kept for potential future use but no longer called -- live ticks
// now fire on every destination, with edit-state protected by each section's
// own dirty/suspendDirtyTracking flag inside its ApplySnapshot.
[[maybe_unused]] static bool isInteractiveDestination(const std::wstring& destinationId) {
    return destinationId == kSecurityDestination
        || destinationId == kSettingsDestination
        || destinationId == kImportsDestination;
}

void writeShellLog(const std::wstring& message) {
    try {
        wchar_t buffer[MAX_PATH]{};
        const auto length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
        const auto executableDirectory = std::filesystem::path(std::wstring(buffer, length)).parent_path();
        const auto paths = MasterControl::DeploymentLogPaths::build(executableDirectory);

        const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        tm localTime{};
        localtime_s(&localTime, &now);

        std::wostringstream line;
        line << std::put_time(&localTime, L"%Y-%m-%d %H:%M:%S") << L"  " << message << std::endl;
        (void)MasterControl::DeploymentLogPaths::appendComponentLog(
            paths,
            paths.shellLatest,
            paths.shellSessionLog,
            line.str());
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

std::optional<int> parseInteger(const std::wstring& value, const int minimum, const int maximum) {
    const auto trimmed = trimCopy(value);
    if (trimmed.empty()) {
        return std::nullopt;
    }

    try {
        const auto parsed = std::stoi(trimmed);
        if (parsed < minimum || parsed > maximum) {
            return std::nullopt;
        }
        return parsed;
    } catch (...) {
        return std::nullopt;
    }
}

std::wstring joinValues(const std::vector<std::wstring>& values, const wchar_t* separator = L", ") {
    std::wstring result;
    for (size_t index = 0; index < values.size(); ++index) {
        if (index > 0) {
            result += separator;
        }
        result += values[index];
    }
    return result;
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
        return { L"TELEMETRY", title, L"Use the dense monitoring deck to keep live host pressure, governed resource budgets, runtime activity, and environment discovery visible at a glance." };
    }
    if (destinationId == kRuntimeDestination) {
        return { L"RUNTIME", title, L"Inspect MCP runtime lanes, platform gateway inventory, Apple remote hosts, and the current operational map exposed by the service." };
    }
    if (destinationId == kCluDestination) {
        return { L"CLU", title, L"Inspect the Command Logic Unit governance profile, launch guided setup wizards, and manage Apple production operations plus operator-visible control rules." };
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

std::wstring guidedFollowThroughForDestination(const std::wstring& destinationId) {
    if (destinationId == kRuntimeDestination) {
        return L"Next:\n- Review the Runtime surface to confirm the lane or host details.";
    }
    if (destinationId == kCluDestination) {
        return L"Next:\n- Review CLU posture and module state.\n- Confirm the module action matches the current governance plan.";
    }
    if (destinationId == kImportsDestination) {
        return L"Next:\n- Review the Imports surface for staging status and provenance.\n- Continue with deployment or validation once the intake is confirmed.";
    }
    if (destinationId == kSecurityDestination) {
        return L"Next:\n- Review the Security surface to confirm the protection envelope.\n- Verify trusted hosts and authentication posture before broad operator use.";
    }
    if (destinationId == kSettingsDestination) {
        return L"Next:\n- Review Settings to confirm ports, beacon behavior, and resource budgets.\n- Refresh the shell if operators are already connected to the host.";
    }
    return L"Next:\n- Review the updated section to confirm the guided change landed as expected.";
}

std::vector<::MasterControlShell::ShellNavigationPointer> bootstrapNavigationPointers() {
    return {
        { L"overview-nav", L"Overview", kOverviewDestination },
        { L"telemetry-nav", L"Telemetry", kTelemetryDestination },
        { L"runtime-nav", L"Runtime", kRuntimeDestination },
        { L"clu-nav", L"CLU", kCluDestination },
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
        { kImportsDestination, { { L"imports-surface", kImportsDestination, kImportsView, 100 } } },
        { kExportsDestination, { { L"exports-surface", kExportsDestination, kExportsView, 100 } } },
        { kSecurityDestination, { { L"security-surface", kSecurityDestination, kSecurityView, 100 } } },
        { kSettingsDestination, { { L"settings-surface", kSettingsDestination, kSettingsView, 100 } } },
        { kSetupWizardDestination, { { L"setup-wizard-surface", kSetupWizardDestination, kSetupWizardView, 100 } } },
        { kSetupReadinessDestination, { { L"setup-readiness-surface", kSetupReadinessDestination, kSetupReadinessView, 100 } } }
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
                              std::function<void()> refreshRequested,
                              std::function<void(const std::wstring&)> actionRequested) {
    if (viewId == kOverviewView) {
        const auto typed = view.as<winrt::MasterControlShell::OverviewSectionControl>();
        winrt::get_self<winrt::MasterControlShell::implementation::OverviewSectionControl>(typed)->AttachRuntime(&runtime);
        return;
    }
    if (viewId == kRuntimeView) {
        const auto typed = view.as<winrt::MasterControlShell::RuntimeSectionControl>();
        winrt::get_self<winrt::MasterControlShell::implementation::RuntimeSectionControl>(typed)->AttachRuntime(
            &runtime,
            std::move(refreshRequested),
            std::move(actionRequested));
        return;
    }
    if (viewId == kCluView) {
        const auto typed = view.as<winrt::MasterControlShell::CommandLogicUnitSectionControl>();
        winrt::get_self<winrt::MasterControlShell::implementation::CommandLogicUnitSectionControl>(typed)->AttachRuntime(
            &runtime,
            std::move(refreshRequested),
            std::move(actionRequested));
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
        winrt::get_self<winrt::MasterControlShell::implementation::SecuritySectionControl>(typed)->AttachRuntime(
            &runtime,
            std::move(refreshRequested),
            std::move(actionRequested));
        return;
    }
    if (viewId == kSettingsView) {
        const auto typed = view.as<winrt::MasterControlShell::SettingsSectionControl>();
        winrt::get_self<winrt::MasterControlShell::implementation::SettingsSectionControl>(typed)->AttachRuntime(
            &runtime,
            std::move(refreshRequested),
            std::move(actionRequested));
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

        // v0.8.1: paint the Tron grid backdrop now that RootGrid is realized.
        // SizeChanged on the Canvas keeps it sized to the window.
        BuildShellGridBackdrop();
    } catch (const winrt::hresult_error& error) {
        writeShellLog(L"RootGrid_Loaded caught HRESULT failure: " + std::wstring(error.message().c_str()));
    }
}

// v0.8.1: Tron grid backdrop. Replaces the pre-v0.8.1 cyan-stroked
// Ellipses (which the operator called out as "not Tron styling due to
// the curves") with a proper perpendicular grid drawn into the
// ShellGridCanvas. Vertical lines every 80 px, horizontal every 60 px,
// plus an emphasized accent line every 5th tick. Lines are drawn in
// the new Tron CLU red-orange palette via the resource brushes so any
// future palette swap flows through without re-coding here.
void MainWindow::BuildShellGridBackdrop() {
    using namespace winrt::Microsoft::UI::Xaml;
    using namespace winrt::Microsoft::UI::Xaml::Controls;
    using namespace winrt::Microsoft::UI::Xaml::Shapes;
    using namespace winrt::Microsoft::UI::Xaml::Media;

    Canvas canvas = nullptr;
    try { canvas = ShellGridCanvas(); }
    catch (const winrt::hresult_error&) { return; }
    if (!canvas) return;

    const double width  = canvas.ActualWidth() > 0 ? canvas.ActualWidth() : 2400;
    const double height = canvas.ActualHeight() > 0 ? canvas.ActualHeight() : 1600;

    canvas.Children().Clear();

    auto resources = Application::Current().Resources();
    Brush gridlineBrush = nullptr;
    Brush accentBrush = nullptr;
    try {
        gridlineBrush = resources.Lookup(box_value(L"ShellGridlineBrush")).try_as<Brush>();
        accentBrush   = resources.Lookup(box_value(L"ShellAccentSoftBrush")).try_as<Brush>();
    } catch (const winrt::hresult_error&) {}

    const double minorStepX = 80.0;
    const double minorStepY = 60.0;
    const int    accentEvery = 5;

    // Vertical lines.
    int idx = 0;
    for (double x = 0; x <= width + minorStepX; x += minorStepX, ++idx) {
        Line line;
        line.X1(x); line.X2(x);
        line.Y1(0); line.Y2(height);
        line.StrokeThickness(1);
        if (gridlineBrush) line.Stroke(gridlineBrush);
        if (accentBrush && (idx % accentEvery) == 0) {
            line.Stroke(accentBrush);
            line.StrokeThickness(1.4);
        }
        canvas.Children().Append(line);
    }

    // Horizontal lines.
    idx = 0;
    for (double y = 0; y <= height + minorStepY; y += minorStepY, ++idx) {
        Line line;
        line.X1(0); line.X2(width);
        line.Y1(y); line.Y2(y);
        line.StrokeThickness(1);
        if (gridlineBrush) line.Stroke(gridlineBrush);
        if (accentBrush && (idx % accentEvery) == 0) {
            line.Stroke(accentBrush);
            line.StrokeThickness(1.4);
        }
        canvas.Children().Append(line);
    }
}

void MainWindow::ShellGridCanvas_SizeChanged(
    Windows::Foundation::IInspectable const&,
    Microsoft::UI::Xaml::SizeChangedEventArgs const&) {
    BuildShellGridBackdrop();
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

void MainWindow::GuidedMcpWizardButton_Click(IInspectable const&, RoutedEventArgs const&) {
    StartGuidedWorkflow(L"new-mcp");
}

void MainWindow::GuidedSubAgentWizardButton_Click(IInspectable const&, RoutedEventArgs const&) {
    StartGuidedWorkflow(L"new-subagent");
}

void MainWindow::GuidedSubAgentGroupWizardButton_Click(IInspectable const&, RoutedEventArgs const&) {
    StartGuidedWorkflow(L"new-subagent-group");
}

void MainWindow::GuidedAppleHostWizardButton_Click(IInspectable const&, RoutedEventArgs const&) {
    StartGuidedWorkflow(L"new-apple-host");
}

void MainWindow::GuidedForsettiModuleWizardButton_Click(IInspectable const&, RoutedEventArgs const&) {
    StartGuidedWorkflow(L"manage-forsetti-modules");
}

void MainWindow::GuidedImportWizardButton_Click(IInspectable const&, RoutedEventArgs const&) {
    StartGuidedWorkflow(L"guided-import");
}

void MainWindow::GuidedSecurityWizardButton_Click(IInspectable const&, RoutedEventArgs const&) {
    StartGuidedWorkflow(L"guided-security");
}

void MainWindow::GuidedSettingsWizardButton_Click(IInspectable const&, RoutedEventArgs const&) {
    StartGuidedWorkflow(L"guided-settings");
}

void MainWindow::GuidedRuntimeMaintenanceWizardButton_Click(IInspectable const&, RoutedEventArgs const&) {
    StartGuidedWorkflow(L"guided-runtime-maintenance");
}

void MainWindow::StartGuidedWorkflow(std::wstring const& workflowId) {
    const auto navigateToDestination = [this](const std::wstring& destinationId) {
        SetCurrentDestination(destinationId);
        for (uint32_t index = 0; index < ShellNavigation().MenuItems().Size(); ++index) {
            if (const auto item = ShellNavigation().MenuItems().GetAt(index).try_as<NavigationViewItem>()) {
                const auto taggedDestination = std::wstring(winrt::unbox_value_or<hstring>(item.Tag(), hstring()).c_str());
                if (taggedDestination == destinationId) {
                    ShellNavigation().SelectedItem(item);
                    break;
                }
            }
        }
    };

    if (workflowId == L"new-mcp") {
        navigateToDestination(kRuntimeDestination);
        GuidedWorkflowStatusText().Text(L"New MCP Server now opens on the Runtime surface so you can create and review runtime lanes without a cramped modal dialog.");
        UpdateStatusBar(L"Use the Runtime surface to create or maintain MCP runtime lanes.", InfoBarSeverity::Informational);
        return;
    } else if (workflowId == L"new-subagent") {
        navigateToDestination(kRuntimeDestination);
        GuidedWorkflowStatusText().Text(L"New Sub-Agent now opens on the Runtime surface so the main window stays readable while you configure the lane.");
        UpdateStatusBar(L"Use the Runtime surface to create new sub-agent lanes directly.", InfoBarSeverity::Informational);
        return;
    } else if (workflowId == L"new-subagent-group") {
        navigateToDestination(kRuntimeDestination);
        GuidedWorkflowStatusText().Text(L"New Sub-Agent Group now opens on the Runtime surface so you can review the sub-agent catalog directly.");
        UpdateStatusBar(L"Use the Runtime surface to review sub-agent groups.", InfoBarSeverity::Informational);
        return;
    } else if (workflowId == L"new-apple-host") {
        navigateToDestination(kRuntimeDestination);
        GuidedWorkflowStatusText().Text(L"New Apple Host now opens on the Runtime surface so you can add and inspect remote build hosts without a cramped modal dialog.");
        UpdateStatusBar(L"Use the Runtime surface to add or update Apple hosts.", InfoBarSeverity::Informational);
        return;
    } else if (workflowId == L"manage-forsetti-modules") {
        navigateToDestination(kCluDestination);
        GuidedWorkflowStatusText().Text(L"Manage Forsetti Modules now opens the CLU surface directly so governance and module state stay visible while you work.");
        UpdateStatusBar(L"Use the CLU surface to inspect and manage Forsetti modules.", InfoBarSeverity::Informational);
        return;
    } else if (workflowId == L"guided-import") {
        navigateToDestination(kImportsDestination);
        GuidedWorkflowStatusText().Text(L"Guided Import now opens on the Imports surface so you can review source material and onboarding status in the full app window.");
        UpdateStatusBar(L"Use the Imports surface for guided software onboarding.", InfoBarSeverity::Informational);
        return;
    } else if (workflowId == L"guided-security") {
        navigateToDestination(kSecurityDestination);
        GuidedWorkflowStatusText().Text(L"Security Hardening now opens directly on the Security surface so the Windows app can stay full-size while you work.");
        UpdateStatusBar(L"Use the Security surface to apply security settings directly.", InfoBarSeverity::Informational);
        return;
    } else if (workflowId == L"guided-settings") {
        navigateToDestination(kSettingsDestination);
        GuidedWorkflowStatusText().Text(L"Host Settings now opens directly on the Settings surface so the main window can be resized while you work.");
        UpdateStatusBar(L"Use the Settings surface to edit and apply host settings directly.", InfoBarSeverity::Informational);
        return;
    } else if (workflowId == L"guided-runtime-maintenance") {
        navigateToDestination(kRuntimeDestination);
        GuidedWorkflowStatusText().Text(L"Manage Runtime Lanes now opens on the Runtime surface so you can edit and remove lanes without a cramped modal dialog.");
        UpdateStatusBar(L"Use the Runtime surface to manage runtime lanes directly.", InfoBarSeverity::Informational);
        return;
    } else {
        UpdateStatusBar(winrt::hstring(std::wstring(L"Unknown guided workflow request: ") + workflowId), InfoBarSeverity::Warning);
        return;
    }

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

    ConfigureCustomTitleBar();

    if (windowHandle_ != nullptr) {
    setWindowSize(windowHandle_, 1680, 980);
    centerWindow(windowHandle_);
}
}

void MainWindow::ConfigureCustomTitleBar() {
    Window window = *this;

    try {
        window.ExtendsContentIntoTitleBar(false);
        TitleBarLeftInsetColumn().Width(GridLengthHelper::FromPixels(0));
        TitleBarRightInsetColumn().Width(GridLengthHelper::FromPixels(0));
        writeShellLog(L"Disabled custom title bar and restored the standard system frame.");
    } catch (const winrt::hresult_error& error) {
        writeShellLog(L"Standard title bar restoration failed: " + std::wstring(error.message().c_str()));
        return;
    }

    if (windowHandle_ == nullptr) {
        return;
    }

    try {
        const auto windowId = Microsoft::UI::GetWindowIdFromWindow(windowHandle_);
        const auto appWindow = Microsoft::UI::Windowing::AppWindow::GetFromWindowId(windowId);
        if (appWindow.Presenter().Kind() != Microsoft::UI::Windowing::AppWindowPresenterKind::Overlapped) {
            appWindow.SetPresenter(Microsoft::UI::Windowing::AppWindowPresenterKind::Overlapped);
        }

        if (const auto presenter = appWindow.Presenter().try_as<Microsoft::UI::Windowing::OverlappedPresenter>()) {
            presenter.SetBorderAndTitleBar(true, true);
            presenter.IsMinimizable(true);
            presenter.IsMaximizable(true);
            presenter.IsResizable(true);
            writeShellLog(L"Configured overlapped presenter with standard title bar and resize border.");
        }
    } catch (const winrt::hresult_error& error) {
        writeShellLog(L"Window presenter configuration failed: " + std::wstring(error.message().c_str()));
    }

    const auto currentStyle = GetWindowLongPtrW(windowHandle_, GWL_STYLE);
    if (currentStyle != 0) {
        const auto requiredStyle = (currentStyle | WS_OVERLAPPEDWINDOW | WS_CAPTION | WS_THICKFRAME | WS_SYSMENU |
                                    WS_MINIMIZEBOX | WS_MAXIMIZEBOX) &
                                   ~static_cast<LONG_PTR>(WS_POPUP);
        if (requiredStyle != currentStyle) {
            SetWindowLongPtrW(windowHandle_, GWL_STYLE, requiredStyle);
        }

        const auto currentExStyle = GetWindowLongPtrW(windowHandle_, GWL_EXSTYLE);
        if ((currentExStyle & WS_EX_APPWINDOW) == 0) {
            SetWindowLongPtrW(windowHandle_, GWL_EXSTYLE, currentExStyle | WS_EX_APPWINDOW);
        }

        SetWindowPos(
            windowHandle_,
            nullptr,
            0,
            0,
            0,
            0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        writeShellLog(L"Forced Win32 overlapped window styles so the shell exposes drag and resize handles.");
    } else {
        writeShellLog(L"Unable to inspect the native shell window style for drag and resize validation.");
    }
}

void MainWindow::ConfigureTimer() {
    const auto dispatcher = DispatcherQueue();
    if (dispatcher == nullptr) {
        writeShellLog(L"DispatcherQueue unavailable; background timers skipped.");
        return;
    }

    try {
        // Live telemetry cadence. The tick calls RefreshLiveAsync, which
        // updates hero values + the current section's data only. Navigation,
        // toolbar, section content host, and scroll position are deliberately
        // left alone -- a full ApplySnapshot (including nav/toolbar rebuilds)
        // only runs on operator-driven Refresh or section navigation.
        refreshTimer_ = dispatcher.CreateTimer();
        // v0.7.4: dropped from 3s to 1s after measuring /api/dashboard at
        // 13-25ms on this build (was previously 2s+ on busy machines).
        // 1Hz updates feel real-time without overlapping requests.
        refreshTimer_.Interval(std::chrono::seconds(1));
        const auto weakThis = get_weak();
        refreshTimer_.Tick([weakThis](auto&&, auto&&) {
            if (const auto self = weakThis.get()) {
                // v0.7.4: tick fires on every destination, including the
                // interactive form sections (Settings/Security/Imports).
                // Each section's ApplySnapshot already protects in-progress
                // edit state via its own dirty/suspendDirtyTracking flag --
                // narrative panels (Resource Envelope, Configuration,
                // Bind address) refresh; textbox/toggle values stay
                // untouched while the operator is editing. Pre-v0.7.4
                // this gate suppressed BOTH narrative refresh AND textbox
                // repopulation, so operators on the Settings view saw
                // stale Resource Envelope / Bind address narratives.
                self->RefreshLiveAsync();
            }
        });
        refreshTimer_.Start();
        writeShellLog(L"Live telemetry timer started (1-second cadence; ticks every section, dirty-flag protects edit state).");
    } catch (const winrt::hresult_error& error) {
        writeShellLog(L"Dispatcher timer fallback activated: " + std::wstring(error.message().c_str()));
    }

    // Activity stream poll timer: one-second cadence against /api/activity.
    // Each tick pulls events strictly newer than activityStreamCursor_ and
    // appends them to ActivityStreamListView so the operator sees every
    // incoming command/request in real time.
    //
    // Extension of the v0.2.12 fix: the 1Hz tick is also suppressed while the
    // operator is editing Security/Settings/Imports, because those views
    // share the UI thread with our collection mutations. We do still update
    // the cursor on navigation-away so the operator does not see a large
    // backlog of pre-edit events when they switch back.
    try {
        activityStreamTimer_ = dispatcher.CreateTimer();
        // 2-second cadence: /api/activity can take 2+ seconds to return on
        // a busy machine (100KB+ payload when the activity log has grown),
        // and 1Hz polling caused overlapping requests to queue. The
        // title-bar clock is the operator's "live heartbeat" cue so the
        // activity list doesn't need sub-2-second updates.
        activityStreamTimer_.Interval(std::chrono::seconds(2));
        const auto weakThis = get_weak();
        activityStreamTimer_.Tick([weakThis](auto&&, auto&&) {
            if (const auto self = weakThis.get()) {
                // v0.7.4: tick on every destination. The activity-stream
                // surface is read-only (TextBlocks inside a ListView with
                // no operator inputs), so polling it on Settings/Security/
                // Imports cannot disturb edit state. Pre-v0.7.4 the gate
                // froze the activity log whenever the operator opened
                // Settings to change anything.
                self->PollActivityStreamAsync();
            }
        });
        activityStreamTimer_.Start();
    } catch (const winrt::hresult_error& error) {
        writeShellLog(L"Activity stream timer fallback activated: " + std::wstring(error.message().c_str()));
    }

    // Live-clock timer: one-second tick that updates the Tron-style LIVE · HH:MM:SS
    // indicator in the title bar. Kept separate from the 10-second refresh timer so
    // the clock stays smooth without dragging RefreshAsync with it.
    try {
        clockTimer_ = dispatcher.CreateTimer();
        clockTimer_.Interval(std::chrono::seconds(1));
        const auto weakThis = get_weak();
        auto updateClock = [weakThis]() {
            if (const auto self = weakThis.get()) {
                SYSTEMTIME now{};
                GetLocalTime(&now);
                wchar_t buffer[16]{};
                swprintf_s(buffer, L"%02u:%02u:%02u", now.wHour, now.wMinute, now.wSecond);
                try {
                    self->LiveClockText().Text(winrt::hstring(buffer));
                } catch (const winrt::hresult_error&) {
                    // Title bar not yet realized; ignore and retry next tick.
                }
            }
        };
        clockTimer_.Tick([updateClock](auto&&, auto&&) { updateClock(); });
        updateClock();
        clockTimer_.Start();
    } catch (const winrt::hresult_error& error) {
        writeShellLog(L"Live clock timer fallback activated: " + std::wstring(error.message().c_str()));
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
    // Per-slot merge (not all-or-nothing replace). The server's
    // composeDashboardSurface populates every slot, but if the
    // Forsetti module activation order ever means DashboardUIModule
    // publishes the surface before every view-injection descriptor
    // has registered (observed: Imports/Exports/Security/Settings/CLU
    // sometimes missing), the previous all-or-nothing guard
    // `if (map.empty())` would skip the bootstrap fallback entirely
    // because SOME slots had arrived. Result: clicking those nav
    // tabs resolved to CreateUnavailableView and the SectionContentHost
    // never swapped. Iterate the bootstrap map instead and insert
    // each slot only if it's still missing so nav clicks always
    // resolve to a real view.
    const auto bootstrap = bootstrapViewInjectionsBySlot();
    for (const auto& [slot, injections] : bootstrap) {
        if (snapshot.viewInjectionsBySlot.find(slot) == snapshot.viewInjectionsBySlot.end()) {
            snapshot.viewInjectionsBySlot[slot] = injections;
        }
    }
}

void MainWindow::SetCurrentDestination(const std::wstring& destinationId) {
    const std::wstring normalized = destinationId.empty() ? std::wstring(kOverviewDestination) : destinationId;
    const bool destinationChanged = (normalized != currentDestination_);
    currentDestination_ = normalized;

    // v0.7.9: HOST CONTROLS and GUIDED SETUP WIZARDS panels in the hero
    // header are hidden when the operator is on the Telemetry tab. The
    // Telemetry tab is purely for live telemetry; setup/control affordances
    // belong in Overview / Runtime / Settings / Imports / Security where
    // they are contextually useful. The panels are restored on every
    // non-Telemetry destination so navigating away from Telemetry brings
    // them straight back.
    {
        using winrt::Microsoft::UI::Xaml::Visibility;
        const Visibility heroSetupVisibility = (normalized == kTelemetryDestination)
            ? Visibility::Collapsed
            : Visibility::Visible;
        try {
            HostControlsPanel().Visibility(heroSetupVisibility);
        } catch (const winrt::hresult_error&) {}
        try {
            GuidedSetupWizardsPanel().Visibility(heroSetupVisibility);
        } catch (const winrt::hresult_error&) {}
    }

    // Only swap SectionContentHost.Content and scroll into view when the
    // destination actually changed. The timed refresh path used to call
    // this on every tick — which visibly flashed the section content and
    // auto-scrolled the page, giving the feel of "the entire page is
    // refreshing" that the user reported. Text/value updates still happen
    // via ApplyLiveSnapshotFragment without going through here.
    if (destinationChanged) {
        const auto view = ResolvePrimaryViewForDestination(currentDestination_, currentSnapshot_);
        SectionContentHost().Content(view);
        ApplySectionMetadata(currentSnapshot_);

        // Interactive form sections are excluded from the timed refresh so
        // they must receive data when the user navigates to them.
        const auto slotIter = currentSnapshot_.viewInjectionsBySlot.find(currentDestination_);
        if (slotIter != currentSnapshot_.viewInjectionsBySlot.end() && !slotIter->second.empty()) {
            const auto& viewId = slotIter->second.front().viewId;
            if (isInteractiveFormSection(viewId) && view != nullptr) {
                applySnapshotToView(view, viewId, currentSnapshot_);
            }
        }

        // Bring the section content into view so guided wizard buttons
        // actually reveal the destination surface instead of silently
        // updating a ContentPresenter that's below the fold. On Overview,
        // skip the scroll so the hero stays pinned at the top as the
        // command deck.
        if (currentDestination_ != kOverviewDestination) {
            try {
                const auto host = SectionContentHost();
                if (host != nullptr) {
                    host.StartBringIntoView();
                }
            } catch (const winrt::hresult_error&) {
                // ContentHost may not be realized during early bootstrap;
                // subsequent SetCurrentDestination calls will retry.
            }
        }
    } else {
        // Same destination — just refresh the header chrome labels in case
        // the snapshot's view titles changed. No content swap, no scroll.
        ApplySectionMetadata(currentSnapshot_);
    }
}

void MainWindow::ApplySurfaceNavigation(const ::MasterControlShell::ShellSnapshot& snapshot) {
    // Signature-cache: only Clear+rebuild the NavigationView MenuItems when
    // the snapshot's nav pointers actually changed. The refresh timer runs
    // every 10 seconds and re-invoking Clear/Append on the XAML collection
    // caused a very visible flicker across the whole shell — which is what
    // the user was observing as "the entire page is refreshing".
    std::wstring signature;
    signature.reserve(snapshot.navigationPointers.size() * 64);
    for (const auto& pointer : snapshot.navigationPointers) {
        signature += pointer.destinationId;
        signature += L'\x1f';
        signature += pointer.label;
        signature += L'\x1e';
    }

    const bool structureChanged = (signature != lastNavigationSignature_);
    if (structureChanged) {
        lastNavigationSignature_ = signature;
        ShellNavigation().MenuItems().Clear();

        for (const auto& pointer : snapshot.navigationPointers) {
            NavigationViewItem item;
            item.Content(box_value(hstring(pointer.label.empty() ? labelForDestination(pointer.destinationId) : pointer.label)));
            item.Tag(box_value(hstring(pointer.destinationId)));
            ShellNavigation().MenuItems().Append(item);
        }
    }

    // Selection management runs every pass (cheap, only flips the current
    // item) so navigation state stays correct even when we skipped the
    // structural rebuild above.
    NavigationViewItem selectedItem{ nullptr };
    std::wstring firstDestination;
    const auto menu = ShellNavigation().MenuItems();
    for (uint32_t index = 0; index < menu.Size(); ++index) {
        if (auto item = menu.GetAt(index).try_as<NavigationViewItem>()) {
            const auto tagBox = item.Tag().try_as<hstring>();
            if (!tagBox.has_value()) {
                continue;
            }
            const std::wstring destinationId = tagBox->c_str();
            if (firstDestination.empty()) {
                firstDestination = destinationId;
            }
            if (destinationId == currentDestination_) {
                selectedItem = item;
            }
        }
    }

    if (selectedItem == nullptr && !firstDestination.empty()) {
        currentDestination_ = firstDestination;
        if (menu.Size() > 0) {
            selectedItem = menu.GetAt(0).try_as<NavigationViewItem>();
        }
    }

    if (selectedItem != nullptr && ShellNavigation().SelectedItem() != selectedItem) {
        ShellNavigation().SelectedItem(selectedItem);
    }
}

void MainWindow::ApplySurfaceToolbar(const ::MasterControlShell::ShellSnapshot& snapshot) {
    // Same signature-cache strategy as the nav: skip Children().Clear() +
    // rebuild when the snapshot's toolbar items are unchanged. This is
    // called from every RefreshAsync pass, so a bare rebuild caused the
    // entire toolbar row to flash every 10 seconds.
    std::wstring signature;
    signature.reserve(snapshot.toolbarItems.size() * 64);
    for (const auto& item : snapshot.toolbarItems) {
        signature += item.id;
        signature += L'\x1f';
        signature += item.title;
        signature += L'\x1f';
        signature += item.systemImageName;
        signature += L'\x1e';
    }

    if (signature == lastToolbarSignature_) {
        return;
    }
    lastToolbarSignature_ = signature;

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
        if (view != nullptr && !isInteractiveFormSection(viewId)) {
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
    } else if (viewId == kImportsView) {
        view = winrt::MasterControlShell::ImportsSectionControl();
    } else if (viewId == kExportsView) {
        view = winrt::MasterControlShell::ExportsSectionControl();
    } else if (viewId == kSecurityView) {
        view = winrt::MasterControlShell::SecuritySectionControl();
    } else if (viewId == kSettingsView) {
        view = winrt::MasterControlShell::SettingsSectionControl();
    } else if (viewId == kSetupWizardView || viewId == kSetupReadinessView) {
        // Programmatic wizard — no MIDL/IDL registration, no XAML compilation.
        // Always rebuilt (never cached) so it shows fresh snapshot data.
        const auto weakThis = get_weak();
        ::MasterControlShell::SetupWizardCallbacks wizardCallbacks{
            [weakThis](const std::wstring& dest) {
                if (const auto self = weakThis.get()) {
                    self->SetCurrentDestination(dest);
                }
            },
            [weakThis](const std::wstring& workflowId) {
                if (const auto self = weakThis.get()) {
                    self->StartGuidedWorkflow(workflowId);
                }
            },
            [weakThis]() {
                if (const auto self = weakThis.get()) {
                    self->RefreshAsync();
                }
            }
        };
        // Return directly — skip caching and interactive-runtime attachment.
        if (viewId == kSetupWizardView) {
            return ::MasterControlShell::BuildSetupWizardEntryView(currentSnapshot_, wizardCallbacks);
        } else {
            return ::MasterControlShell::BuildSetupReadinessView(currentSnapshot_, wizardCallbacks);
        }
    }

    if (view == nullptr) {
        return CreateUnavailableView(
            L"Unknown Forsetti View",
            winrt::hstring(std::wstring(L"The shell does not have a renderer registered for ") + viewId));
    }

    // Every view that exposes interactive controls (writes config, fires
    // workflows, drives the supervisor) must have its runtime attached
    // here. Without this branch, AttachRuntime never runs for the view,
    // its runtime_ stays nullptr, and IsEnabled-gated controls (Apply
    // Host Settings, the Claude Code Control toggle, etc.) sit greyed
    // out forever. v0.6.3 shipped this list missing kSettingsView and
    // kOverviewView -- both fixed in v0.6.4.
    if (viewId == kRuntimeView
        || viewId == kCluView
        || viewId == kImportsView
        || viewId == kExportsView
        || viewId == kSecurityView
        || viewId == kSettingsView
        || viewId == kOverviewView) {
        const auto weakThis = get_weak();
        attachInteractiveRuntime(
            view,
            viewId,
            runtime_,
            [weakThis]() {
                if (const auto self = weakThis.get()) {
                    self->RefreshAsync();
                }
            },
            [weakThis](const std::wstring& workflowId) {
                if (const auto self = weakThis.get()) {
                    self->StartGuidedWorkflow(workflowId);
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
    // Mirror the snapshot into currentSnapshot_ so SetCurrentDestination
    // below sees the freshly-populated viewInjectionsBySlot map. Without
    // this, the first-paint flip below would call SetCurrentDestination,
    // which in turn calls ResolvePrimaryViewForDestination against a stale
    // currentSnapshot_ and falls back to CreateUnavailableView — whose
    // style lookup throws if Application.Resources isn't fully loaded yet.
    currentSnapshot_ = snapshot;

    ApplySurfaceNavigation(snapshot);
    ApplySurfaceToolbar(snapshot);
    ApplyCachedSectionSnapshots(snapshot);

    // Fallback first-run routing: if setup hasn't been completed yet and
    // the user hasn't navigated away from the default, route to the setup
    // wizard automatically.
    if (!snapshot.firstRunCompleted
        && currentDestination_ == kOverviewDestination
        && !firstRunWizardDismissed_) {
        currentDestination_ = kSetupWizardDestination;
    }
    SetCurrentDestination(currentDestination_);

    ApplyHeroSnapshot(snapshot);
    ApplySubAgentFooter(snapshot);
}

// Live fragment: ONLY what should update on the 2-second tick. No nav,
// no toolbar, no section-view swap, no scroll, no re-apply across dormant
// cached section views. Just the hero + current section's values.
void MainWindow::ApplyLiveSnapshotFragment(const ::MasterControlShell::ShellSnapshot& snapshot) {
    ApplyHeroSnapshot(snapshot);
    // v0.7.8: the SUB-AGENT GRID footer row lives in MainWindow.xaml outside
    // SectionContentHost, so it persists across all section navigations.
    // Refresh it on every live tick so utilization bars + client IPs stay
    // current at the 1Hz cadence.
    ApplySubAgentFooter(snapshot);
    ApplyCurrentSectionSnapshot(snapshot);
}

// Update the visible LIVE indicator in the title bar badge. Bumps a sample
// counter and a short timestamp on every tick (even failed ones) so the
// operator can tell at a glance whether the telemetry pipeline is alive.
// Green label = fresh snapshot captured. Amber label = tick fired but the
// admin API did not respond.
void MainWindow::ApplyLiveHeartbeat(std::chrono::system_clock::time_point /*now*/, bool captured) {
    try {
        SYSTEMTIME localNow{};
        GetLocalTime(&localNow);
        wchar_t clockBuffer[16]{};
        swprintf_s(clockBuffer, L"%02u:%02u:%02u", localNow.wHour, localNow.wMinute, localNow.wSecond);
        LiveClockText().Text(winrt::hstring(clockBuffer));

        std::wstring sampleText = L"#" + std::to_wstring(liveSampleCounter_);
        LiveTelemetrySampleText().Text(winrt::hstring(sampleText));

        // Flip the label to OFFLINE (amber) when the admin API fails so
        // there is an immediate visual distinction between "stuck UI" and
        // "service not reachable".
        LiveTelemetryLabelText().Text(winrt::hstring(captured ? L"LIVE" : L"OFFLINE"));
    } catch (const winrt::hresult_error&) {
        // Title bar elements may not be realized during very early bootstrap;
        // the next tick will retry.
    }
}

// Apply the snapshot to just the currently visible section view. This is
// safe during the live tick because section controls update their
// TextBlocks / ProgressBars in place.
void MainWindow::ApplyCurrentSectionSnapshot(const ::MasterControlShell::ShellSnapshot& snapshot) {
    // v0.8.0: when one or more telemetry tiles are detached to desktop
    // windows, we want them to keep updating regardless of which section
    // the operator is currently viewing. Always feed the cached telemetry
    // view its snapshot in addition to the active section. The cached
    // view's ApplySnapshot updates the same x:Name'd inner controls
    // whether they are parented to the main shell grid or to a detached
    // window's content tree.
    {
        auto telemetryCached = cachedViews_.find(L"telemetry");
        if (telemetryCached != cachedViews_.end() && telemetryCached->second != nullptr
            && currentDestination_ != L"telemetry") {
            applySnapshotToView(telemetryCached->second, L"telemetry", snapshot);
        }
    }
    const auto slotIter = snapshot.viewInjectionsBySlot.find(currentDestination_);
    if (slotIter == snapshot.viewInjectionsBySlot.end() || slotIter->second.empty()) {
        return;
    }
    const auto& viewId = slotIter->second.front().viewId;
    // v0.7.4: forward the live snapshot to every section, including the
    // interactive form sections (Settings/Security/Imports). Each section's
    // own ApplySnapshot is already responsible for protecting in-progress
    // edit state via its dirty/suspendDirtyTracking flag --
    // SettingsSectionControl::ApplySnapshot only calls
    // PopulateEditorFromSnapshot when !dirty_, SecuritySectionControl
    // gates the same way. Pre-v0.7.4 the early-return here meant that the
    // narrative panels (Resource Envelope, Configuration narrative, Bind
    // address read-out) inside Settings stayed static while the operator
    // was on the Settings view, even though the runtime had fresh data.
    const auto cached = cachedViews_.find(viewId);
    if (cached == cachedViews_.end() || cached->second == nullptr) {
        return;
    }
    applySnapshotToView(cached->second, viewId, snapshot);
}

// Hero + badges + status bar. Broken out of ApplySnapshot so the live
// fragment can update exactly the same values without pulling in the
// heavy surface-rebuild cost.
void MainWindow::ApplyHeroSnapshot(const ::MasterControlShell::ShellSnapshot& snapshot) {
    ServiceStateText().Text(winrt::hstring(serviceStateLabel(snapshot.serviceState)));
    ApiStateText().Text(snapshot.apiHealthy ? L"Reachable" : L"Offline");
    EndpointCountText().Text(winrt::hstring(std::to_wstring(snapshot.endpointCount)));
    HeroCpuProgressBar().Value(snapshot.cpuPercent);
    HeroMemoryProgressBar().Value(snapshot.memoryPercent);
    HeroDiskProgressBar().Value(snapshot.diskPercent);
    HeroCpuValueText().Text(winrt::hstring(formatPercent(snapshot.cpuPercent)));
    HeroMemoryValueText().Text(winrt::hstring(formatPercent(snapshot.memoryPercent)));
    HeroDiskValueText().Text(winrt::hstring(formatPercent(snapshot.diskPercent)));
    HeroTrafficValueText().Text(winrt::hstring(formatTraffic(snapshot.bytesSentPerSecond, snapshot.bytesReceivedPerSecond)));

    std::wstring heroTrafficDetail = snapshot.primaryIpAddress.empty() ? L"Awaiting telemetry." : L"Primary route: " + snapshot.primaryIpAddress;
    if (!snapshot.telemetryCapturedAtUtc.empty()) {
        heroTrafficDetail += L"  |  " + snapshot.telemetryCapturedAtUtc;
    }
    HeroTrafficDetailText().Text(winrt::hstring(heroTrafficDetail));
    HeroTelemetrySummaryText().Text(winrt::hstring(
        snapshot.telemetryText.empty()
            ? L"Waiting for a live orchestration snapshot."
            : snapshot.telemetryText));

    std::wstring operationsHeadline = L"SERVICE " + uppercase(serviceStateLabel(snapshot.serviceState));
    if (snapshot.apiHealthy) {
        operationsHeadline += L"  |  API ONLINE";
    } else {
        operationsHeadline += L"  |  API OFFLINE";
    }
    HeroOperationsHeadlineText().Text(winrt::hstring(operationsHeadline));

    std::wstring operationsBody = !snapshot.statusMessage.empty()
        ? snapshot.statusMessage
        : (snapshot.telemetryText.empty()
            ? L"Waiting for a live orchestration snapshot."
            : snapshot.telemetryText);
    if (!snapshot.governanceFindingRows.empty()) {
        operationsBody += L"\n\nTop finding: " + snapshot.governanceFindingRows.front();
    } else if (!snapshot.installRows.empty()) {
        operationsBody += L"\n\nLatest deployment event: " + snapshot.installRows.front();
    }
    HeroOperationsBodyText().Text(winrt::hstring(operationsBody));

    std::wstring identity = snapshot.hostName.empty() ? L"Host pending" : snapshot.hostName;
    if (!snapshot.operatingSystem.empty()) {
        identity += L"\n" + snapshot.operatingSystem;
    }
    if (!snapshot.primaryIpAddress.empty()) {
        identity += L"\n" + snapshot.primaryIpAddress;
    }
    if (!snapshot.primaryMacAddress.empty()) {
        identity += L"\n" + snapshot.primaryMacAddress;
    }
    HeroIdentityText().Text(winrt::hstring(identity));

    std::wstring governance = snapshot.governancePosture.empty()
        ? L"Awaiting CLU and governance posture."
        : snapshot.governancePosture;
    if (!snapshot.governanceDoctrine.empty()) {
        governance += L"\n" + snapshot.governanceDoctrine;
    }
    if (!snapshot.governanceLastEvaluatedUtc.empty()) {
        governance += L"\nLast evaluated: " + snapshot.governanceLastEvaluatedUtc;
    }
    HeroGovernanceText().Text(winrt::hstring(governance));

    std::wostringstream ledger;
    ledger << L"Routes " << snapshot.endpointCount
           << L"  |  Gateways " << snapshot.platformGatewayCount
           << L"\nApple hosts " << snapshot.appleRemoteHostCount
           << L"  |  Findings " << snapshot.governanceFindingCount;
    HeroRuntimeLedgerText().Text(winrt::hstring(ledger.str()));

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

// v0.7.8: rebuild the SUB-AGENT GRID footer row from
// ShellSnapshot.subAgentRuntimeStats. Each badge previously was a static
// XAML <Border> with a literal name + tag-line; the operator's repeated
// complaint was that the badges had no individual telemetry, no
// utilization indicator, and no IP addresses of clients using the
// sub-agent. v0.7.8 builds each badge imperatively from the live runtime
// stats so every refresh tick (1 Hz) updates the visible bar fill, the
// reachability dot, the active-client IP list (truncated to 2 lines per
// badge for footer compactness), and the active/capacity ratio.
void MainWindow::ApplySubAgentFooter(const ::MasterControlShell::ShellSnapshot& snapshot) {
    using namespace winrt::Microsoft::UI::Xaml;
    using namespace winrt::Microsoft::UI::Xaml::Controls;
    using namespace winrt::Microsoft::UI::Xaml::Media;
    using winrt::Windows::UI::Color;
    using winrt::Windows::UI::ColorHelper;

    auto grid = SubAgentFooterGrid();
    grid.Children().Clear();
    grid.ColumnDefinitions().Clear();

    if (snapshot.subAgentRuntimeStats.empty()) {
        try {
            SubAgentFooterHeadline().Text(winrt::hstring(L"SUB-AGENT GRID  -  no sub-agents registered"));
        } catch (const winrt::hresult_error&) {}
        return;
    }

    int reachableCount = 0;
    int totalActiveLeases = 0;
    for (const auto& s : snapshot.subAgentRuntimeStats) {
        if (s.reachable) ++reachableCount;
        totalActiveLeases += s.activeLeaseCount;
    }
    try {
        std::wstring headline = L"SUB-AGENT GRID  -  "
            + std::to_wstring(snapshot.subAgentRuntimeStats.size())
            + L" registered, "
            + std::to_wstring(reachableCount)
            + L" reachable, "
            + std::to_wstring(totalActiveLeases)
            + L" active lease"
            + (totalActiveLeases == 1 ? L"" : L"s");
        SubAgentFooterHeadline().Text(winrt::hstring(headline));
    } catch (const winrt::hresult_error&) {}

    auto fromHex = [](uint8_t r, uint8_t g, uint8_t b, uint8_t a = 0xFF) {
        return ColorHelper::FromArgb(a, r, g, b);
    };
    const auto goodColor    = fromHex(0x1c, 0xf2, 0xc1);
    const auto warnColor    = fromHex(0xff, 0xc8, 0x57);
    const auto critColor    = fromHex(0xff, 0x6a, 0x80);
    const auto neutralColor = fromHex(0x8c, 0xb7, 0xc4);
    // v0.8.1: chrome shifted from cyan (0x00,0xf6,0xff) to Tron CLU
    // red-orange (0xff,0x3d,0x2e). Status semantic colors below stay
    // unchanged.
    const auto cardEdgeBrush       = SolidColorBrush(ColorHelper::FromArgb(0x55, 0xff, 0x3d, 0x2e));
    const auto cardBackgroundBrush = SolidColorBrush(ColorHelper::FromArgb(0x18, 0xff, 0x3d, 0x2e));
    const auto barTrackBrush       = SolidColorBrush(ColorHelper::FromArgb(0x40, 0x8c, 0xb7, 0xc4));

    Thickness cardBorder;
    cardBorder.Left = 1.0; cardBorder.Top = 1.0; cardBorder.Right = 1.0; cardBorder.Bottom = 1.0;
    Thickness cardPadding;
    cardPadding.Left = 8.0; cardPadding.Top = 6.0; cardPadding.Right = 8.0; cardPadding.Bottom = 6.0;
    winrt::Microsoft::UI::Xaml::CornerRadius cardCorners;
    cardCorners.TopLeft = 6.0; cardCorners.TopRight = 6.0; cardCorners.BottomRight = 6.0; cardCorners.BottomLeft = 6.0;

    int columnIndex = 0;
    for (const auto& stat : snapshot.subAgentRuntimeStats) {
        ColumnDefinition col;
        col.Width(GridLengthHelper::FromValueAndType(1.0, GridUnitType::Star));
        grid.ColumnDefinitions().Append(col);

        Border card;
        card.Background(cardBackgroundBrush);
        card.BorderBrush(cardEdgeBrush);
        card.BorderThickness(cardBorder);
        card.CornerRadius(cardCorners);
        card.Padding(cardPadding);
        Grid::SetColumn(card, columnIndex);

        StackPanel inner;
        inner.Spacing(4);

        // Title row: name (uppercase) + reachability dot.
        StackPanel titleRow;
        titleRow.Orientation(Orientation::Horizontal);
        titleRow.Spacing(6);

        TextBlock nameText;
        std::wstring upperName = stat.displayName.empty() ? stat.subAgentId : stat.displayName;
        std::transform(upperName.begin(), upperName.end(), upperName.begin(), ::towupper);
        nameText.Text(winrt::hstring(upperName));
        nameText.FontSize(11);
        nameText.FontWeight(winrt::Microsoft::UI::Text::FontWeights::SemiBold());
        nameText.Foreground(SolidColorBrush(ColorHelper::FromArgb(0xFF, 0xb8, 0xff, 0xf0)));

        Border dot;
        dot.Width(8); dot.Height(8);
        winrt::Microsoft::UI::Xaml::CornerRadius dotCorners;
        dotCorners.TopLeft = 4.0; dotCorners.TopRight = 4.0;
        dotCorners.BottomRight = 4.0; dotCorners.BottomLeft = 4.0;
        dot.CornerRadius(dotCorners);
        dot.VerticalAlignment(VerticalAlignment::Center);
        const bool hasEndpoint = !stat.endpointHostPort.empty();
        const auto dotColor = hasEndpoint
            ? (stat.reachable ? goodColor : critColor)
            : neutralColor;
        dot.Background(SolidColorBrush(dotColor));

        titleRow.Children().Append(nameText);
        titleRow.Children().Append(dot);
        inner.Children().Append(titleRow);

        // Specialization sub-text (kept for visual continuity with the
        // pre-v0.7.8 stub badges).
        if (!stat.specialization.empty()) {
            TextBlock specText;
            specText.Text(winrt::hstring(stat.specialization));
            specText.FontSize(10);
            specText.Foreground(SolidColorBrush(neutralColor));
            inner.Children().Append(specText);
        }

        // Utilization bar + percent + active/capacity ratio.
        const double util = stat.utilizationPercent;
        const auto barTone = (util >= 95.0) ? critColor
                            : (util >= 75.0) ? warnColor
                            : goodColor;

        StackPanel utilRow;
        utilRow.Orientation(Orientation::Horizontal);
        utilRow.Spacing(4);

        TextBlock utilLabel;
        wchar_t pctBuf[16]{};
        swprintf_s(pctBuf, L"%.0f%%", util);
        utilLabel.Text(pctBuf);
        utilLabel.FontSize(10);
        utilLabel.Foreground(SolidColorBrush(barTone));
        utilLabel.MinWidth(28);
        utilRow.Children().Append(utilLabel);

        ProgressBar bar;
        bar.Minimum(0);
        bar.Maximum(100);
        bar.Value(util < 0 ? 0.0 : (util > 100.0 ? 100.0 : util));
        bar.Foreground(SolidColorBrush(barTone));
        bar.Background(barTrackBrush);
        bar.Height(4);
        bar.HorizontalAlignment(HorizontalAlignment::Stretch);
        bar.MinWidth(60);
        utilRow.Children().Append(bar);

        TextBlock ratioText;
        wchar_t ratioBuf[32]{};
        swprintf_s(ratioBuf, L"%d/%d",
                   stat.activeLeaseCount,
                   stat.leaseCapacity);
        ratioText.Text(ratioBuf);
        ratioText.FontSize(10);
        ratioText.Foreground(SolidColorBrush(neutralColor));
        utilRow.Children().Append(ratioText);

        inner.Children().Append(utilRow);

        // Active client IPs. Footer is compact, so cap at 2 lines and
        // append "+N more" when oversized. Empty list shows "no clients"
        // in muted color so the operator can tell at a glance which
        // sub-agents are idle vs in use.
        TextBlock clientsText;
        clientsText.FontSize(10);
        clientsText.TextWrapping(TextWrapping::Wrap);
        clientsText.Foreground(SolidColorBrush(neutralColor));
        if (stat.activeClients.empty()) {
            clientsText.Text(L"no clients");
        } else {
            std::wstring acc;
            const size_t shown = std::min<size_t>(2, stat.activeClients.size());
            for (size_t i = 0; i < shown; ++i) {
                if (i > 0) acc += L'\n';
                const auto& holder = stat.activeClients[i];
                acc += holder.ipAddress.empty() ? std::wstring(L"unknown") : holder.ipAddress;
                if (!holder.clientType.empty()) {
                    acc += L" (";
                    acc += holder.clientType;
                    acc += L")";
                }
            }
            if (stat.activeClients.size() > shown) {
                acc += L"\n+";
                acc += std::to_wstring(stat.activeClients.size() - shown);
                acc += L" more";
            }
            clientsText.Text(winrt::hstring(acc));
            clientsText.Foreground(SolidColorBrush(ColorHelper::FromArgb(0xFF, 0xb8, 0xff, 0xf0)));
        }
        inner.Children().Append(clientsText);

        card.Child(inner);
        grid.Children().Append(card);
        ++columnIndex;
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

// Timer-initiated refresh that only updates the "live" fields — telemetry
// values, counts, badges, and the current section's values. Crucially, it
// does NOT rebuild the navigation, rebuild the toolbar, swap the section
// view, or scroll into view. That's the path that was making the shell
// feel like the whole page was refreshing every 10 seconds.
IAsyncAction MainWindow::RefreshLiveAsync() {
    // Skip if the user is in the middle of an explicit full refresh or
    // another live pass is still running. Overlapping passes are what
    // caused mid-animation redraws in the previous builds.
    if (refreshInFlight_.load()) {
        co_return;
    }
    if (liveRefreshInFlight_.exchange(true)) {
        co_return;
    }

    // RAII guard so liveRefreshInFlight_ is ALWAYS cleared on scope exit,
    // no matter how the coroutine unwinds. A stuck-true flag would silently
    // disable live telemetry until the next explicit Refresh, which is the
    // kind of regression that looks like "telemetry is frozen" from the
    // operator's side.
    struct LiveFlagGuard {
        std::atomic_bool* flag;
        ~LiveFlagGuard() { if (flag) { flag->store(false); } }
    };
    LiveFlagGuard guard{ &liveRefreshInFlight_ };

    winrt::apartment_context uiThread;
    co_await winrt::resume_background();

    ::MasterControlShell::ShellSnapshot snapshot;
    bool captured = false;
    try {
        snapshot = runtime_.CaptureSnapshot();
        captured = true;
    } catch (...) {
        captured = false;
    }

    EnsureBootstrapSurface(snapshot);

    co_await uiThread;
    if (!captured) {
        // Even on capture failure, bump the live tick counter so the
        // operator can tell the timer itself is alive. The visible "Live"
        // indicator in the hero distinguishes heartbeat from fresh data.
        ++liveSampleCounter_;
        ApplyLiveHeartbeat(std::chrono::system_clock::now(), false);
        co_return;
    }
    currentSnapshot_ = std::move(snapshot);
    ++liveSampleCounter_;
    ApplyLiveSnapshotFragment(currentSnapshot_);
    ApplyLiveHeartbeat(std::chrono::system_clock::now(), true);
}

IAsyncAction MainWindow::PollActivityStreamAsync() {
    // Fetch new events since the last cursor, then marshal back to the UI
    // thread to append them to the list view. Uses the most-recent-first
    // ordering: each new event is inserted at index 0 and the list is
    // capped at 120 rows so long-running sessions don't drift into
    // unbounded memory.
    winrt::apartment_context uiThread;
    const auto cursor = activityStreamCursor_;
    co_await winrt::resume_background();

    const auto result = runtime_.FetchActivityEvents(cursor);

    co_await uiThread;

    if (!result.succeeded) {
        try {
            ActivityStreamStatusText().Text(winrt::hstring(
                result.errorMessage.empty() ? L"activity stream offline" : result.errorMessage));
        } catch (const winrt::hresult_error&) {}
        co_return;
    }

    activityStreamCursor_ = result.highWaterMarkId;

    if (result.events.empty()) {
        try {
            ActivityStreamStatusText().Text(winrt::hstring(L"idle"));
        } catch (const winrt::hresult_error&) {}
        co_return;
    }

    try {
        auto listView = ActivityStreamListView();
        for (const auto& event : result.events) {
            // Compose a compact single-line representation:
            //   HH:MM:SS  <kind>  <method> <target> -> <status>  <latency>ms
            std::wstring timestamp = event.timestampUtc;
            // Trim to HH:MM:SS if the timestamp is an ISO string
            if (timestamp.size() >= 19 && timestamp[10] == L'T') {
                timestamp = timestamp.substr(11, 8);
            }

            std::wostringstream line;
            line << timestamp
                 << L"  " << event.kind
                 << L"  " << event.method
                 << L" " << event.target
                 << L"  -> " << event.statusCode
                 << L"  " << event.latencyMs << L"ms";
            if (!event.message.empty() && event.message.size() < 160) {
                line << L"  | " << event.message;
            }

            ListViewItem row;
            TextBlock text;
            text.Text(winrt::hstring(line.str()));
            text.Style(Application::Current().Resources().Lookup(box_value(L"ShellDataTextStyle")).try_as<Style>());
            // Colour-code by status class for quick scanning
            if (event.statusCode >= 500) {
                text.Foreground(Application::Current().Resources().Lookup(box_value(L"ShellDangerBrush")).try_as<Brush>());
            } else if (event.statusCode >= 400) {
                text.Foreground(Application::Current().Resources().Lookup(box_value(L"ShellWarningBrush")).try_as<Brush>());
            } else if (event.statusCode >= 200) {
                text.Foreground(Application::Current().Resources().Lookup(box_value(L"ShellSuccessBrush")).try_as<Brush>());
            }
            row.Content(text);
            listView.Items().InsertAt(0, row);
            ++activityStreamCount_;
        }

        // Cap at 120 rows so long soak sessions stay bounded.
        while (listView.Items().Size() > 120) {
            listView.Items().RemoveAt(listView.Items().Size() - 1);
        }

        ActivityStreamCounterText().Text(
            winrt::hstring(std::to_wstring(activityStreamCount_) + L" events"));
        ActivityStreamStatusText().Text(
            winrt::hstring(L"+" + std::to_wstring(result.events.size()) + L" new"));
    } catch (const winrt::hresult_error&) {
        // List view not yet realized or collection mutation conflict; the
        // next tick will try again.
    }
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
            const auto completionMessage = GuidedWorkflowStatusText().Text();
            dialog.Hide();
            co_await CompleteGuidedWorkflowAsync(completionMessage, kRuntimeDestination);
        }();
        (void)ignored;
    });

    co_await dialog.ShowAsync();
}

IAsyncAction MainWindow::ShowSubAgentGroupWizardAsync() {
    ContentDialog dialog;
    dialog.Title(box_value(L"New Sub-Agent Group Wizard"));
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
    intro.Text(L"Step 1: name the specialist group. Step 2: explain what this group is responsible for. Step 3: select the sub-agent members that should work together when CLU routes multi-model execution.");
    intro.TextWrapping(TextWrapping::WrapWholeWords);
    root.Children().Append(intro);

    const auto addLabel = [&root](std::wstring const& text) {
        TextBlock label;
        label.Style(Application::Current().Resources().Lookup(box_value(L"ShellLabelTextStyle")).try_as<Style>());
        label.Text(winrt::hstring(text));
        root.Children().Append(label);
    };

    TextBox idBox;
    idBox.PlaceholderText(L"coding-squad");
    root.Children().Append([&]() {
        addLabel(L"Step 1. Group ID");
        return idBox;
    }());

    TextBox displayNameBox;
    displayNameBox.PlaceholderText(L"Coding Squad");
    root.Children().Append([&]() {
        addLabel(L"Display Name");
        return displayNameBox;
    }());

    TextBox descriptionBox;
    descriptionBox.AcceptsReturn(true);
    descriptionBox.Height(96);
    descriptionBox.TextWrapping(TextWrapping::Wrap);
    descriptionBox.PlaceholderText(L"Describe the work this group owns, such as coding, review, test automation, or release packaging.");
    root.Children().Append([&]() {
        addLabel(L"Step 2. Responsibility Summary");
        return descriptionBox;
    }());

    ListBox memberSelector;
    memberSelector.SelectionMode(Microsoft::UI::Xaml::Controls::SelectionMode::Multiple);
    memberSelector.MaxHeight(220);
    memberSelector.BorderThickness(Thickness{ 1.0, 1.0, 1.0, 1.0 });
    memberSelector.BorderBrush(Application::Current().Resources().Lookup(box_value(L"ShellTileEdgeBrush")).try_as<Brush>());
    memberSelector.Background(Application::Current().Resources().Lookup(box_value(L"ShellPanelDeepBrush")).try_as<Brush>());

    size_t eligibleMemberCount = 0;
    for (const auto& endpoint : currentSnapshot_.endpoints) {
        const auto normalizedKind = uppercase(endpoint.kind);
        if (normalizedKind.find(L"SUB") == std::wstring::npos) {
            continue;
        }

        Microsoft::UI::Xaml::Controls::ListBoxItem item;
        std::wstring label = endpoint.displayName.empty() ? endpoint.id : endpoint.displayName;
        if (!endpoint.description.empty()) {
            label += L"  |  " + endpoint.description;
        } else if (!endpoint.kind.empty()) {
            label += L"  |  " + endpoint.kind;
        }
        item.Content(box_value(winrt::hstring(label)));
        item.Tag(box_value(winrt::hstring(endpoint.id)));
        memberSelector.Items().Append(item);
        ++eligibleMemberCount;
    }

    root.Children().Append([&]() {
        addLabel(L"Step 3. Sub-Agent Members");
        return memberSelector;
    }());

    TextBlock memberHintText;
    memberHintText.Style(Application::Current().Resources().Lookup(box_value(L"ShellDataTextStyle")).try_as<Style>());
    memberHintText.TextWrapping(TextWrapping::WrapWholeWords);
    memberHintText.Text(
        eligibleMemberCount == 0
            ? L"Create sub-agents first, then come back here to group them for shared responsibility routing."
            : L"Select the sub-agents that belong in this group. You can save the group first and refine membership later.");
    root.Children().Append(memberHintText);

    TextBlock statusText;
    statusText.Style(Application::Current().Resources().Lookup(box_value(L"ShellDataTextStyle")).try_as<Style>());
    statusText.Text(L"Use groups to make provider responsibilities obvious, such as routing planning to one model and coding to a mixed specialist squad.");
    statusText.TextWrapping(TextWrapping::WrapWholeWords);
    root.Children().Append(statusText);

    Button createButton;
    createButton.Content(box_value(L"Create Sub-Agent Group"));
    createButton.Style(Application::Current().Resources().Lookup(box_value(L"ShellCommandButtonStyle")).try_as<Style>());
    root.Children().Append(createButton);

    scrollViewer.Content(root);
    dialog.Content(scrollViewer);

    createButton.Click([this, dialog, createButton, statusText, idBox, displayNameBox, descriptionBox, memberSelector](IInspectable const&, RoutedEventArgs const&) {
        auto ignored = [this, dialog, createButton, statusText, idBox, displayNameBox, descriptionBox, memberSelector]() -> IAsyncAction {
            const auto id = trimCopy(std::wstring(idBox.Text().c_str()));
            const auto displayName = trimCopy(std::wstring(displayNameBox.Text().c_str()));
            const auto description = trimCopy(std::wstring(descriptionBox.Text().c_str()));
            if (id.empty() || displayName.empty()) {
                statusText.Text(L"Group ID and display name are required.");
                co_return;
            }

            std::vector<std::wstring> members;
            members.reserve(memberSelector.SelectedItems().Size());
            for (const auto& selected : memberSelector.SelectedItems()) {
                const auto item = selected.try_as<Microsoft::UI::Xaml::Controls::ListBoxItem>();
                if (item == nullptr) {
                    continue;
                }
                const auto targetId = trimCopy(std::wstring(winrt::unbox_value_or<hstring>(item.Tag(), hstring()).c_str()));
                if (!targetId.empty()) {
                    members.push_back(targetId);
                }
            }

            createButton.IsEnabled(false);
            statusText.Text(L"Saving the sub-agent group through the local admin API.");

            ::MasterControlShell::ShellSubAgentGroupDefinition group;
            group.groupId = id;
            group.displayName = displayName;
            group.description = description;
            group.memberTargetIds = std::move(members);

            winrt::apartment_context uiThread;
            co_await winrt::resume_background();
            const auto result = runtime_.UpsertSubAgentGroup(group);
            co_await uiThread;

            statusText.Text(winrt::hstring(result.message));
            if (!result.succeeded) {
                createButton.IsEnabled(true);
                GuidedWorkflowStatusText().Text(L"Sub-agent group wizard needs attention. Review the wizard message and try again.");
                co_return;
            }

            GuidedWorkflowStatusText().Text(winrt::hstring(L"Created sub-agent group '" + displayName + L"' for CLU routing and model responsibility mapping."));
            const auto completionMessage = GuidedWorkflowStatusText().Text();
            dialog.Hide();
            co_await CompleteGuidedWorkflowAsync(completionMessage, kRuntimeDestination);
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
            const auto completionMessage = GuidedWorkflowStatusText().Text();
            dialog.Hide();
            co_await CompleteGuidedWorkflowAsync(completionMessage, kRuntimeDestination);
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
            const auto completionMessage = GuidedWorkflowStatusText().Text();
            dialog.Hide();
            co_await CompleteGuidedWorkflowAsync(completionMessage, kRuntimeDestination);
        }();
        (void)ignored;
    });

    co_await dialog.ShowAsync();
}

IAsyncAction MainWindow::ShowForsettiModuleWizardAsync() {
    winrt::apartment_context uiThread;
    co_await winrt::resume_background();
    const auto catalog = runtime_.FetchForsettiModules();
    co_await uiThread;

    if (!catalog.succeeded) {
        co_await ShowDialogAsync(L"Forsetti Module Wizard", winrt::hstring(catalog.message.empty()
            ? L"Unable to read the current Forsetti module catalog."
            : catalog.message));
        co_return;
    }

    ContentDialog dialog;
    dialog.Title(box_value(L"Manage Forsetti Modules"));
    dialog.CloseButtonText(L"Close");
    dialog.XamlRoot(RootGrid().XamlRoot());

    ScrollViewer scrollViewer;
    scrollViewer.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
    scrollViewer.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
    scrollViewer.MaxHeight(660);

    StackPanel root;
    root.Spacing(14);
    root.Width(580);

    TextBlock intro;
    intro.Style(Application::Current().Resources().Lookup(box_value(L"ShellBodyTextStyle")).try_as<Style>());
    intro.Text(L"Step 1: choose the Forsetti module you want to manage. Step 2: review its status, capabilities, and supported platforms. Step 3: install, enable, update, or disable it through the local module runtime.");
    intro.TextWrapping(TextWrapping::WrapWholeWords);
    root.Children().Append(intro);

    const auto addLabel = [&root](std::wstring const& text) {
        TextBlock label;
        label.Style(Application::Current().Resources().Lookup(box_value(L"ShellLabelTextStyle")).try_as<Style>());
        label.Text(winrt::hstring(text));
        root.Children().Append(label);
    };

    ComboBox moduleSelector;
    for (const auto& module : catalog.modules) {
        ComboBoxItem item;
        std::wstring label = module.displayName.empty() ? module.moduleId : module.displayName;
        label += L"  |  ";
        label += module.statusSummary.empty() ? (module.active ? L"active" : L"inactive") : module.statusSummary;
        item.Content(box_value(winrt::hstring(label)));
        item.Tag(box_value(winrt::hstring(module.moduleId)));
        moduleSelector.Items().Append(item);
    }
    if (moduleSelector.Items().Size() > 0) {
        moduleSelector.SelectedIndex(0);
    }
    root.Children().Append([&]() {
        addLabel(L"Step 1. Forsetti Module");
        return moduleSelector;
    }());

    ComboBox actionSelector;
    const auto appendAction = [&actionSelector](const wchar_t* label, const wchar_t* tag) {
        ComboBoxItem item;
        item.Content(box_value(label));
        item.Tag(box_value(tag));
        actionSelector.Items().Append(item);
    };
    appendAction(L"Install or Enable", L"install");
    appendAction(L"Update or Reload", L"update");
    appendAction(L"Disable or Remove", L"disable");
    actionSelector.SelectedIndex(0);
    root.Children().Append([&]() {
        addLabel(L"Step 3. Module Action");
        return actionSelector;
    }());

    Border detailCard;
    detailCard.Background(Application::Current().Resources().Lookup(box_value(L"ShellPanelDeepBrush")).try_as<Brush>());
    detailCard.BorderBrush(Application::Current().Resources().Lookup(box_value(L"ShellTileEdgeBrush")).try_as<Brush>());
    detailCard.BorderThickness(Thickness{ 1.0, 1.0, 1.0, 1.0 });
    detailCard.CornerRadius(CornerRadius{ 16.0, 16.0, 16.0, 16.0 });
    detailCard.Padding(Thickness{ 16.0, 14.0, 16.0, 14.0 });

    TextBlock detailText;
    detailText.Style(Application::Current().Resources().Lookup(box_value(L"ShellDataTextStyle")).try_as<Style>());
    detailText.TextWrapping(TextWrapping::WrapWholeWords);
    detailCard.Child(detailText);
    root.Children().Append([&]() {
        addLabel(L"Step 2. Module Status");
        return detailCard;
    }());

    TextBlock statusText;
    statusText.Style(Application::Current().Resources().Lookup(box_value(L"ShellDataTextStyle")).try_as<Style>());
    statusText.Text(L"Use this wizard when you want to install add-on Forsetti modules, refresh updated modules, or temporarily disable non-core modules.");
    statusText.TextWrapping(TextWrapping::WrapWholeWords);
    root.Children().Append(statusText);

    Button createButton;
    createButton.Content(box_value(L"Apply Module Action"));
    createButton.Style(Application::Current().Resources().Lookup(box_value(L"ShellCommandButtonStyle")).try_as<Style>());
    createButton.IsEnabled(!catalog.modules.empty());
    root.Children().Append(createButton);

    const auto updateModuleDetails = [&, this]() {
        const auto index = moduleSelector.SelectedIndex();
        if (index < 0 || index >= static_cast<int>(catalog.modules.size())) {
            detailText.Text(L"No Forsetti module is selected.");
            createButton.IsEnabled(false);
            return;
        }

        const auto& module = catalog.modules[static_cast<size_t>(index)];
        std::wstring details = module.displayName.empty() ? module.moduleId : module.displayName;
        if (!module.version.empty()) {
            details += L"\nVersion: " + module.version;
        }
        if (!module.moduleType.empty()) {
            details += L"\nType: " + module.moduleType;
        }
        if (!module.entryPoint.empty()) {
            details += L"\nEntry point: " + module.entryPoint;
        }
        details += L"\nStatus: ";
        details += module.statusSummary.empty() ? (module.active ? L"active" : L"inactive") : module.statusSummary;
        details += L"\nUnlocked: ";
        details += module.unlocked ? L"yes" : L"no";
        details += L"\nProtected: ";
        details += module.protectedModule ? L"yes" : L"no";
        if (!module.supportedPlatforms.empty()) {
            details += L"\nPlatforms: " + joinValues(module.supportedPlatforms);
        }
        if (!module.capabilitiesRequested.empty()) {
            details += L"\nCapabilities: " + joinValues(module.capabilitiesRequested);
        }
        if (!module.recommendedAction.empty()) {
            details += L"\nRecommended action: " + module.recommendedAction;
        }
        detailText.Text(winrt::hstring(details));

        const auto recommended = uppercase(module.recommendedAction);
        if (recommended.find(L"UPDATE") != std::wstring::npos || recommended.find(L"RELOAD") != std::wstring::npos) {
            actionSelector.SelectedIndex(1);
        } else if (recommended.find(L"DISABLE") != std::wstring::npos || recommended.find(L"REMOVE") != std::wstring::npos) {
            actionSelector.SelectedIndex(2);
        } else {
            actionSelector.SelectedIndex(0);
        }

        createButton.IsEnabled(!module.protectedModule || actionSelector.SelectedIndex() != 2);
    };

    moduleSelector.SelectionChanged([&](IInspectable const&, Controls::SelectionChangedEventArgs const&) {
        updateModuleDetails();
    });
    actionSelector.SelectionChanged([&](IInspectable const&, Controls::SelectionChangedEventArgs const&) {
        const auto moduleIndex = moduleSelector.SelectedIndex();
        if (moduleIndex < 0 || moduleIndex >= static_cast<int>(catalog.modules.size())) {
            createButton.IsEnabled(false);
            return;
        }
        const auto& module = catalog.modules[static_cast<size_t>(moduleIndex)];
        createButton.IsEnabled(!module.protectedModule || actionSelector.SelectedIndex() != 2);
    });
    updateModuleDetails();

    scrollViewer.Content(root);
    dialog.Content(scrollViewer);

    createButton.Click([this, dialog, createButton, statusText, moduleSelector, actionSelector, catalog](IInspectable const&, RoutedEventArgs const&) {
        auto ignored = [this, dialog, createButton, statusText, moduleSelector, actionSelector, catalog]() -> IAsyncAction {
            const auto moduleIndex = moduleSelector.SelectedIndex();
            if (moduleIndex < 0 || moduleIndex >= static_cast<int>(catalog.modules.size())) {
                statusText.Text(L"Select a Forsetti module before applying a module action.");
                co_return;
            }

            const auto actionItem = actionSelector.SelectedItem().try_as<ComboBoxItem>();
            const auto action = trimCopy(std::wstring(actionItem == nullptr
                ? L""
                : winrt::unbox_value_or<hstring>(actionItem.Tag(), hstring()).c_str()));
            if (action.empty()) {
                statusText.Text(L"Choose a Forsetti module action before continuing.");
                co_return;
            }

            const auto& module = catalog.modules[static_cast<size_t>(moduleIndex)];
            createButton.IsEnabled(false);
            statusText.Text(L"Applying the Forsetti module action through the local admin API.");

            winrt::apartment_context uiThread;
            co_await winrt::resume_background();
            const auto result = runtime_.ManageForsettiModule(module.moduleId, action);
            co_await uiThread;

            statusText.Text(winrt::hstring(result.message));
            if (!result.succeeded) {
                createButton.IsEnabled(true);
                GuidedWorkflowStatusText().Text(L"Forsetti module wizard needs attention. Review the module message and try again.");
                co_return;
            }

            GuidedWorkflowStatusText().Text(winrt::hstring(L"Applied '" + action + L"' to Forsetti module '" +
                (module.displayName.empty() ? module.moduleId : module.displayName) + L"'."));
            const auto completionMessage = GuidedWorkflowStatusText().Text();
            dialog.Hide();
            co_await CompleteGuidedWorkflowAsync(completionMessage, kCluDestination);
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
            const auto completionMessage = GuidedWorkflowStatusText().Text();
            dialog.Hide();
            co_await CompleteGuidedWorkflowAsync(completionMessage, kImportsDestination);
        }();
        (void)ignored;
    });

    co_await dialog.ShowAsync();
}

IAsyncAction MainWindow::ShowSecurityWizardAsync() {
    ContentDialog dialog;
    dialog.Title(box_value(L"Guided Security Hardening"));
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
    intro.Text(L"Step 1: choose the security posture that best matches this host. Step 2: review access, authentication, and troubleshooting switches. Step 3: publish the protection envelope through the local admin API.");
    intro.TextWrapping(TextWrapping::WrapWholeWords);
    root.Children().Append(intro);

    const auto addLabel = [&root](std::wstring const& text) {
        TextBlock label;
        label.Style(Application::Current().Resources().Lookup(box_value(L"ShellLabelTextStyle")).try_as<Style>());
        label.Text(winrt::hstring(text));
        root.Children().Append(label);
    };

    ComboBox postureSelector;
    const auto appendPosture = [&postureSelector](const wchar_t* label, const wchar_t* tag) {
        ComboBoxItem item;
        item.Content(box_value(label));
        item.Tag(box_value(tag));
        postureSelector.Items().Append(item);
    };
    appendPosture(L"Balanced Default", L"balanced");
    appendPosture(L"Restricted Operations", L"restricted");
    appendPosture(L"Controlled Troubleshooting", L"troubleshooting");
    postureSelector.SelectedIndex(0);
    root.Children().Append([&]() {
        addLabel(L"Step 1. Security Posture");
        return postureSelector;
    }());

    ToggleSwitch securityProtocolsToggle;
    securityProtocolsToggle.Header(box_value(L"Security protocols enabled"));
    securityProtocolsToggle.IsOn(currentSnapshot_.securitySettings.securityProtocolsEnabled);
    ToggleSwitch tlsToggle;
    tlsToggle.Header(box_value(L"Enable TLS"));
    tlsToggle.IsOn(currentSnapshot_.securitySettings.enableTls);
    ToggleSwitch authToggle;
    authToggle.Header(box_value(L"Require authentication"));
    authToggle.IsOn(currentSnapshot_.securitySettings.enableAuthentication);
    ToggleSwitch bypassToggle;
    bypassToggle.Header(box_value(L"Allow troubleshooting bypass"));
    bypassToggle.IsOn(currentSnapshot_.securitySettings.allowTroubleshootingBypass);
    ToggleSwitch openLanToggle;
    openLanToggle.Header(box_value(L"Allow open LAN access"));
    openLanToggle.IsOn(currentSnapshot_.securitySettings.allowOpenLanAccess);

    TextBox trustedHostsBox;
    trustedHostsBox.AcceptsReturn(true);
    trustedHostsBox.Height(120);
    trustedHostsBox.PlaceholderText(L"one host per line");
    trustedHostsBox.Text(winrt::hstring(joinValues(currentSnapshot_.securitySettings.trustedRemoteHosts, L"\r\n")));

    root.Children().Append([&]() {
        addLabel(L"Step 2. Protection Controls");
        StackPanel panel;
        panel.Spacing(10);
        panel.Children().Append(securityProtocolsToggle);
        panel.Children().Append(tlsToggle);
        panel.Children().Append(authToggle);
        panel.Children().Append(bypassToggle);
        panel.Children().Append(openLanToggle);
        return panel;
    }());

    root.Children().Append([&]() {
        addLabel(L"Trusted Remote Hosts");
        return trustedHostsBox;
    }());

    TextBlock summaryText;
    summaryText.Style(Application::Current().Resources().Lookup(box_value(L"ShellDataTextStyle")).try_as<Style>());
    summaryText.TextWrapping(TextWrapping::WrapWholeWords);
    root.Children().Append(summaryText);

    TextBlock statusText;
    statusText.Style(Application::Current().Resources().Lookup(box_value(L"ShellDataTextStyle")).try_as<Style>());
    statusText.Text(L"Choose the posture that matches normal operations, then override only the switches you explicitly need.");
    statusText.TextWrapping(TextWrapping::WrapWholeWords);
    root.Children().Append(statusText);

    Button applyButton;
    applyButton.Content(box_value(L"Apply Security Hardening"));
    applyButton.Style(Application::Current().Resources().Lookup(box_value(L"ShellCommandButtonStyle")).try_as<Style>());
    root.Children().Append(applyButton);

    const auto applyProfile = [&, this](std::wstring const& profileId) {
        if (profileId == L"restricted") {
            securityProtocolsToggle.IsOn(true);
            tlsToggle.IsOn(true);
            authToggle.IsOn(true);
            bypassToggle.IsOn(false);
            openLanToggle.IsOn(false);
        } else if (profileId == L"troubleshooting") {
            securityProtocolsToggle.IsOn(true);
            tlsToggle.IsOn(false);
            authToggle.IsOn(false);
            bypassToggle.IsOn(true);
            openLanToggle.IsOn(false);
        } else {
            securityProtocolsToggle.IsOn(true);
            tlsToggle.IsOn(currentSnapshot_.securitySettings.enableTls);
            authToggle.IsOn(currentSnapshot_.securitySettings.enableAuthentication);
            bypassToggle.IsOn(currentSnapshot_.securitySettings.allowTroubleshootingBypass);
            openLanToggle.IsOn(currentSnapshot_.securitySettings.allowOpenLanAccess);
        }
    };

    const auto updateSummary = [&, this]() {
        const auto selectedItem = postureSelector.SelectedItem().try_as<ComboBoxItem>();
        const auto postureValue = selectedItem == nullptr
            ? hstring(L"balanced")
            : unbox_value_or<hstring>(selectedItem.Tag(), hstring(L"balanced"));
        const auto posture = std::wstring(postureValue.c_str());
        std::vector<std::wstring> summaryParts;
        summaryParts.push_back(L"Posture: " + posture);
        summaryParts.push_back(std::wstring(L"Protocols ") + (securityProtocolsToggle.IsOn() ? L"enabled" : L"disabled"));
        summaryParts.push_back(std::wstring(L"TLS ") + (tlsToggle.IsOn() ? L"on" : L"off"));
        summaryParts.push_back(std::wstring(L"Authentication ") + (authToggle.IsOn() ? L"required" : L"optional"));
        summaryParts.push_back(std::wstring(L"Open LAN ") + (openLanToggle.IsOn() ? L"allowed" : L"restricted"));
        if (bypassToggle.IsOn()) {
            summaryParts.push_back(L"Troubleshooting bypass available");
        }
        const auto trustedHosts = trimCopy(std::wstring(trustedHostsBox.Text().c_str()));
        if (!trustedHosts.empty()) {
            summaryParts.push_back(L"Trusted hosts curated");
        }
        summaryText.Text(winrt::hstring(joinValues(summaryParts, L"  |  ")));
    };

    postureSelector.SelectionChanged([&](IInspectable const&, Controls::SelectionChangedEventArgs const&) {
        const auto selectedItem = postureSelector.SelectedItem().try_as<ComboBoxItem>();
        const auto postureValue = selectedItem == nullptr
            ? hstring(L"balanced")
            : unbox_value_or<hstring>(selectedItem.Tag(), hstring(L"balanced"));
        const auto posture = std::wstring(postureValue.c_str());
        applyProfile(posture);
        updateSummary();
    });
    auto trackedToggle = [&](ToggleSwitch const& toggle) {
        toggle.Toggled([&](IInspectable const&, RoutedEventArgs const&) {
            updateSummary();
        });
    };
    trackedToggle(securityProtocolsToggle);
    trackedToggle(tlsToggle);
    trackedToggle(authToggle);
    trackedToggle(bypassToggle);
    trackedToggle(openLanToggle);
    trustedHostsBox.TextChanged([&](IInspectable const&, Controls::TextChangedEventArgs const&) {
        updateSummary();
    });
    updateSummary();

    scrollViewer.Content(root);
    dialog.Content(scrollViewer);

    applyButton.Click([this, dialog, applyButton, statusText, securityProtocolsToggle, tlsToggle, authToggle, bypassToggle, openLanToggle, trustedHostsBox](IInspectable const&, RoutedEventArgs const&) {
        auto ignored = [this, dialog, applyButton, statusText, securityProtocolsToggle, tlsToggle, authToggle, bypassToggle, openLanToggle, trustedHostsBox]() -> IAsyncAction {
            ::MasterControlShell::ShellSecuritySettings settings;
            settings.securityProtocolsEnabled = securityProtocolsToggle.IsOn();
            settings.enableTls = tlsToggle.IsOn();
            settings.enableAuthentication = authToggle.IsOn();
            settings.allowTroubleshootingBypass = bypassToggle.IsOn();
            settings.allowOpenLanAccess = openLanToggle.IsOn();

            std::wstringstream trustedHostsStream(std::wstring(trustedHostsBox.Text().c_str()));
            std::wstring line;
            while (std::getline(trustedHostsStream, line)) {
                std::wstringstream commaStream(line);
                std::wstring host;
                while (std::getline(commaStream, host, L',')) {
                    const auto trimmed = trimCopy(host);
                    if (!trimmed.empty()) {
                        settings.trustedRemoteHosts.push_back(trimmed);
                    }
                }
            }

            applyButton.IsEnabled(false);
            statusText.Text(L"Applying the guided security posture through the local admin API.");

            winrt::apartment_context uiThread;
            co_await winrt::resume_background();
            auto result = runtime_.UpdateSecuritySettings(settings, false);
            co_await uiThread;

            if (result.requiresConfirmation && !result.succeeded) {
                ContentDialog confirmation;
                confirmation.Title(box_value(L"Disable Security Protocols?"));
                confirmation.Content(box_value(L"Disabling security protocols weakens the protection envelope for the Master Control Orchestration Server. Continue only for a controlled troubleshooting window."));
                confirmation.PrimaryButtonText(L"Disable");
                confirmation.CloseButtonText(L"Cancel");
                confirmation.DefaultButton(ContentDialogButton::Close);
                confirmation.XamlRoot(RootGrid().XamlRoot());

                if (co_await confirmation.ShowAsync() == ContentDialogResult::Primary) {
                    statusText.Text(L"Applying the confirmed security exception.");
                    co_await winrt::resume_background();
                    result = runtime_.UpdateSecuritySettings(settings, true);
                    co_await uiThread;
                } else {
                    statusText.Text(L"Guided security hardening was cancelled before unsafe changes were applied.");
                    applyButton.IsEnabled(true);
                    GuidedWorkflowStatusText().Text(L"Security hardening wizard cancelled the unsafe change request.");
                    co_return;
                }
            }

            statusText.Text(winrt::hstring(result.message));
            if (!result.succeeded) {
                applyButton.IsEnabled(true);
                GuidedWorkflowStatusText().Text(L"Security hardening wizard needs attention. Review the wizard message and try again.");
                co_return;
            }

            GuidedWorkflowStatusText().Text(L"Applied guided security hardening to the orchestration server.");
            const auto completionMessage = GuidedWorkflowStatusText().Text();
            dialog.Hide();
            co_await CompleteGuidedWorkflowAsync(completionMessage, kSecurityDestination);
        }();
        (void)ignored;
    });

    co_await dialog.ShowAsync();
}

IAsyncAction MainWindow::ShowSettingsWizardAsync() {
    ContentDialog dialog;
    dialog.Title(box_value(L"Guided Host Settings"));
    dialog.CloseButtonText(L"Close");
    dialog.XamlRoot(RootGrid().XamlRoot());

    ScrollViewer scrollViewer;
    scrollViewer.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
    scrollViewer.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
    scrollViewer.MaxHeight(700);

    StackPanel root;
    root.Spacing(14);
    root.Width(580);

    TextBlock intro;
    intro.Style(Application::Current().Resources().Lookup(box_value(L"ShellBodyTextStyle")).try_as<Style>());
    intro.Text(L"Step 1: confirm the host identity and bind endpoints. Step 2: decide whether the LAN beacon should stay active. Step 3: shape the governed CPU, memory, bandwidth, and storage envelope for managed launches.");
    intro.TextWrapping(TextWrapping::WrapWholeWords);
    root.Children().Append(intro);

    const auto addLabel = [&root](std::wstring const& text) {
        TextBlock label;
        label.Style(Application::Current().Resources().Lookup(box_value(L"ShellLabelTextStyle")).try_as<Style>());
        label.Text(winrt::hstring(text));
        root.Children().Append(label);
    };

    TextBox instanceNameBox;
    instanceNameBox.PlaceholderText(L"Master Control Orchestration Server");
    instanceNameBox.Text(winrt::hstring(currentSnapshot_.instanceName.empty() ? std::wstring(kProductDisplayName) : currentSnapshot_.instanceName));
    root.Children().Append([&]() {
        addLabel(L"Step 1. Instance Name");
        return instanceNameBox;
    }());

    TextBox bindAddressBox;
    bindAddressBox.PlaceholderText(L"0.0.0.0");
    bindAddressBox.Text(winrt::hstring(currentSnapshot_.bindAddress.empty() ? L"0.0.0.0" : currentSnapshot_.bindAddress));
    root.Children().Append([&]() {
        addLabel(L"Bind Address");
        return bindAddressBox;
    }());

    Grid portGrid;
    portGrid.ColumnSpacing(12);
    portGrid.ColumnDefinitions().Append(ColumnDefinition());
    portGrid.ColumnDefinitions().Append(ColumnDefinition());

    StackPanel browserPortPanel;
    browserPortPanel.Spacing(6);
    TextBlock browserPortLabel;
    browserPortLabel.Style(Application::Current().Resources().Lookup(box_value(L"ShellLabelTextStyle")).try_as<Style>());
    browserPortLabel.Text(L"Browser Port");
    TextBox browserPortBox;
    browserPortBox.Text(winrt::hstring(std::to_wstring(currentSnapshot_.browserPort)));
    browserPortPanel.Children().Append(browserPortLabel);
    browserPortPanel.Children().Append(browserPortBox);

    StackPanel beaconPortPanel;
    beaconPortPanel.Spacing(6);
    TextBlock beaconPortLabel;
    beaconPortLabel.Style(Application::Current().Resources().Lookup(box_value(L"ShellLabelTextStyle")).try_as<Style>());
    beaconPortLabel.Text(L"Beacon Port");
    TextBox beaconPortBox;
    beaconPortBox.Text(winrt::hstring(std::to_wstring(currentSnapshot_.beaconPort)));
    beaconPortPanel.Children().Append(beaconPortLabel);
    beaconPortPanel.Children().Append(beaconPortBox);

    Grid::SetColumn(browserPortPanel, 0);
    Grid::SetColumn(beaconPortPanel, 1);
    portGrid.Children().Append(browserPortPanel);
    portGrid.Children().Append(beaconPortPanel);
    root.Children().Append(portGrid);

    ToggleSwitch beaconEnabledToggle;
    beaconEnabledToggle.Header(box_value(L"Broadcast LAN beacon and gateway metadata"));
    beaconEnabledToggle.IsOn(currentSnapshot_.beaconEnabled);
    root.Children().Append(beaconEnabledToggle);

    Grid resourceGrid;
    resourceGrid.ColumnSpacing(12);
    resourceGrid.RowSpacing(12);
    resourceGrid.ColumnDefinitions().Append(ColumnDefinition());
    resourceGrid.ColumnDefinitions().Append(ColumnDefinition());
    resourceGrid.RowDefinitions().Append(RowDefinition());
    resourceGrid.RowDefinitions().Append(RowDefinition());

    const auto addResourceEditor = [&resourceGrid, this](const int row,
                                                         const int column,
                                                         std::wstring const& labelText,
                                                         std::wstring const& valueText) {
        StackPanel panel;
        panel.Spacing(6);
        TextBlock label;
        label.Style(Application::Current().Resources().Lookup(box_value(L"ShellLabelTextStyle")).try_as<Style>());
        label.Text(winrt::hstring(labelText));
        TextBox valueBox;
        valueBox.Text(winrt::hstring(valueText));
        panel.Children().Append(label);
        panel.Children().Append(valueBox);
        Grid::SetRow(panel, row);
        Grid::SetColumn(panel, column);
        resourceGrid.Children().Append(panel);
        return valueBox;
    };

    addLabel(L"Step 3. Resource Allocation");
    TextBox cpuPercentBox = addResourceEditor(0, 0, L"CPU Allocation %", std::to_wstring(currentSnapshot_.cpuAllocationPercent));
    TextBox memoryPercentBox = addResourceEditor(0, 1, L"Memory Allocation %", std::to_wstring(currentSnapshot_.memoryAllocationPercent));
    TextBox bandwidthPercentBox = addResourceEditor(1, 0, L"Bandwidth Allocation %", std::to_wstring(currentSnapshot_.bandwidthAllocationPercent));
    TextBox storagePercentBox = addResourceEditor(1, 1, L"Storage Allocation %", std::to_wstring(currentSnapshot_.storageAllocationPercent));
    root.Children().Append(resourceGrid);

    TextBlock summaryText;
    summaryText.Style(Application::Current().Resources().Lookup(box_value(L"ShellDataTextStyle")).try_as<Style>());
    summaryText.TextWrapping(TextWrapping::WrapWholeWords);
    root.Children().Append(summaryText);

    TextBlock statusText;
    statusText.Style(Application::Current().Resources().Lookup(box_value(L"ShellDataTextStyle")).try_as<Style>());
    statusText.Text(L"Use this wizard when you want to adjust bind ports, beacon behavior, or the governed resource budget without hand-editing configuration.");
    statusText.TextWrapping(TextWrapping::WrapWholeWords);
    root.Children().Append(statusText);

    Button applyButton;
    applyButton.Content(box_value(L"Apply Host Settings"));
    applyButton.Style(Application::Current().Resources().Lookup(box_value(L"ShellCommandButtonStyle")).try_as<Style>());
    root.Children().Append(applyButton);

    const auto updateSummary = [&, this]() {
        std::wstring summary = L"Host '";
        summary += trimCopy(std::wstring(instanceNameBox.Text().c_str())).empty()
            ? std::wstring(kProductDisplayName)
            : trimCopy(std::wstring(instanceNameBox.Text().c_str()));
        summary += L"' on ";
        summary += trimCopy(std::wstring(bindAddressBox.Text().c_str())).empty()
            ? L"0.0.0.0"
            : trimCopy(std::wstring(bindAddressBox.Text().c_str()));
        summary += L" | Browser ";
        summary += trimCopy(std::wstring(browserPortBox.Text().c_str())).empty()
            ? std::to_wstring(currentSnapshot_.browserPort)
            : trimCopy(std::wstring(browserPortBox.Text().c_str()));
        summary += L" | Beacon ";
        summary += beaconEnabledToggle.IsOn() ? L"enabled" : L"disabled";
        summary += L" | CPU ";
        summary += trimCopy(std::wstring(cpuPercentBox.Text().c_str()));
        summary += L"% | RAM ";
        summary += trimCopy(std::wstring(memoryPercentBox.Text().c_str()));
        summary += L"% | Bandwidth ";
        summary += trimCopy(std::wstring(bandwidthPercentBox.Text().c_str()));
        summary += L"% | Storage ";
        summary += trimCopy(std::wstring(storagePercentBox.Text().c_str()));
        summary += L"%";
        summaryText.Text(winrt::hstring(summary));
    };

    instanceNameBox.TextChanged([&](IInspectable const&, Controls::TextChangedEventArgs const&) { updateSummary(); });
    bindAddressBox.TextChanged([&](IInspectable const&, Controls::TextChangedEventArgs const&) { updateSummary(); });
    browserPortBox.TextChanged([&](IInspectable const&, Controls::TextChangedEventArgs const&) { updateSummary(); });
    beaconPortBox.TextChanged([&](IInspectable const&, Controls::TextChangedEventArgs const&) { updateSummary(); });
    cpuPercentBox.TextChanged([&](IInspectable const&, Controls::TextChangedEventArgs const&) { updateSummary(); });
    memoryPercentBox.TextChanged([&](IInspectable const&, Controls::TextChangedEventArgs const&) { updateSummary(); });
    bandwidthPercentBox.TextChanged([&](IInspectable const&, Controls::TextChangedEventArgs const&) { updateSummary(); });
    storagePercentBox.TextChanged([&](IInspectable const&, Controls::TextChangedEventArgs const&) { updateSummary(); });
    beaconEnabledToggle.Toggled([&](IInspectable const&, RoutedEventArgs const&) { updateSummary(); });
    updateSummary();

    scrollViewer.Content(root);
    dialog.Content(scrollViewer);

    applyButton.Click([this, dialog, applyButton, statusText, instanceNameBox, bindAddressBox, browserPortBox, beaconPortBox, beaconEnabledToggle, cpuPercentBox, memoryPercentBox, bandwidthPercentBox, storagePercentBox](IInspectable const&, RoutedEventArgs const&) {
        auto ignored = [this, dialog, applyButton, statusText, instanceNameBox, bindAddressBox, browserPortBox, beaconPortBox, beaconEnabledToggle, cpuPercentBox, memoryPercentBox, bandwidthPercentBox, storagePercentBox]() -> IAsyncAction {
            const auto browserPort = parseInteger(std::wstring(browserPortBox.Text().c_str()), 1, 65535);
            const auto beaconPort = parseInteger(std::wstring(beaconPortBox.Text().c_str()), 1, 65535);
            const auto cpuPercent = parseInteger(std::wstring(cpuPercentBox.Text().c_str()), 0, 100);
            const auto memoryPercent = parseInteger(std::wstring(memoryPercentBox.Text().c_str()), 0, 100);
            const auto bandwidthPercent = parseInteger(std::wstring(bandwidthPercentBox.Text().c_str()), 0, 100);
            const auto storagePercent = parseInteger(std::wstring(storagePercentBox.Text().c_str()), 0, 100);

            if (trimCopy(std::wstring(instanceNameBox.Text().c_str())).empty()) {
                statusText.Text(L"Instance name is required.");
                co_return;
            }
            if (trimCopy(std::wstring(bindAddressBox.Text().c_str())).empty()) {
                statusText.Text(L"Bind address is required.");
                co_return;
            }
            if (!browserPort.has_value() || !beaconPort.has_value()) {
                statusText.Text(L"Browser and beacon ports must be between 1 and 65535.");
                co_return;
            }
            if (!cpuPercent.has_value() || !memoryPercent.has_value() || !bandwidthPercent.has_value() || !storagePercent.has_value()) {
                statusText.Text(L"Resource allocation values must stay between 0 and 100.");
                co_return;
            }

            applyButton.IsEnabled(false);
            statusText.Text(L"Applying the guided host settings through the local admin API.");

            winrt::apartment_context uiThread;
            co_await winrt::resume_background();
            const auto result = runtime_.UpdateHostSettings(::MasterControlShell::ShellHostSettings{
                trimCopy(std::wstring(instanceNameBox.Text().c_str())),
                trimCopy(std::wstring(bindAddressBox.Text().c_str())),
                static_cast<uint16_t>(*browserPort),
                static_cast<uint16_t>(*beaconPort),
                beaconEnabledToggle.IsOn(),
                *cpuPercent,
                *memoryPercent,
                *bandwidthPercent,
                *storagePercent
            });
            co_await uiThread;

            statusText.Text(winrt::hstring(result.message));
            if (!result.succeeded) {
                applyButton.IsEnabled(true);
                GuidedWorkflowStatusText().Text(L"Host settings wizard needs attention. Review the wizard message and try again.");
                co_return;
            }

            GuidedWorkflowStatusText().Text(L"Applied guided host settings and resource envelope updates.");
            const auto completionMessage = GuidedWorkflowStatusText().Text();
            dialog.Hide();
            co_await CompleteGuidedWorkflowAsync(completionMessage, kSettingsDestination);
        }();
        (void)ignored;
    });

    co_await dialog.ShowAsync();
}

IAsyncAction MainWindow::ShowRuntimeMaintenanceWizardAsync() {
    ContentDialog dialog;
    dialog.Title(box_value(L"Manage Runtime Lanes"));
    dialog.CloseButtonText(L"Close");
    dialog.XamlRoot(RootGrid().XamlRoot());

    std::vector<::MasterControlShell::ShellRuntimeEndpoint> customMcpServers;
    std::vector<::MasterControlShell::ShellRuntimeEndpoint> customSubAgents;
    customMcpServers.reserve(currentSnapshot_.endpoints.size());
    customSubAgents.reserve(currentSnapshot_.endpoints.size());
    for (const auto& endpoint : currentSnapshot_.endpoints) {
        if (!endpoint.userDefined) {
            continue;
        }

        if (endpoint.kind == L"mcp_server") {
            customMcpServers.push_back(endpoint);
        } else if (endpoint.kind == L"sub_agent") {
            customSubAgents.push_back(endpoint);
        }
    }
    const auto appleHosts = currentSnapshot_.appleRemoteHosts;

    ScrollViewer scrollViewer;
    scrollViewer.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
    scrollViewer.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
    scrollViewer.MaxHeight(720);

    StackPanel root;
    root.Spacing(14);
    root.Width(600);

    TextBlock intro;
    intro.Style(Application::Current().Resources().Lookup(box_value(L"ShellBodyTextStyle")).try_as<Style>());
    intro.Text(L"Step 1: choose the runtime lane type you need to maintain. Step 2: load a published lane. Step 3: update or remove it without dropping into the raw runtime editors.");
    intro.TextWrapping(TextWrapping::WrapWholeWords);
    root.Children().Append(intro);

    const auto addLabel = [&root](std::wstring const& text) {
        TextBlock label;
        label.Style(Application::Current().Resources().Lookup(box_value(L"ShellLabelTextStyle")).try_as<Style>());
        label.Text(winrt::hstring(text));
        root.Children().Append(label);
    };

    ComboBox kindSelector;
    kindSelector.PlaceholderText(L"Choose a runtime lane type");
    {
        ComboBoxItem mcpItem;
        mcpItem.Content(box_value(L"Shared MCP Server"));
        mcpItem.Tag(box_value(L"mcp"));
        kindSelector.Items().Append(mcpItem);

        ComboBoxItem subAgentItem;
        subAgentItem.Content(box_value(L"Custom Sub-Agent"));
        subAgentItem.Tag(box_value(L"subagent"));
        kindSelector.Items().Append(subAgentItem);

        ComboBoxItem appleItem;
        appleItem.Content(box_value(L"Apple Remote Host"));
        appleItem.Tag(box_value(L"apple"));
        kindSelector.Items().Append(appleItem);
    }
    kindSelector.SelectedIndex(0);
    root.Children().Append([&]() {
        addLabel(L"Step 1. Runtime Lane Type");
        return kindSelector;
    }());

    ComboBox recordSelector;
    recordSelector.PlaceholderText(L"Choose a published lane");
    root.Children().Append([&]() {
        addLabel(L"Step 2. Published Lane");
        return recordSelector;
    }());

    StackPanel mcpPanel;
    mcpPanel.Spacing(10);

    TextBox mcpIdBox;
    mcpIdBox.PlaceholderText(L"swift-tools-mcp");
    TextBox mcpDisplayNameBox;
    mcpDisplayNameBox.PlaceholderText(L"Swift Tools MCP");
    TextBox mcpHostBox;
    mcpHostBox.PlaceholderText(L"127.0.0.1");
    TextBox mcpPortBox;
    mcpPortBox.PlaceholderText(L"7302");
    TextBox mcpProtocolBox;
    mcpProtocolBox.PlaceholderText(L"http");
    TextBox mcpRoutePathBox;
    mcpRoutePathBox.PlaceholderText(L"/mcp");
    TextBox mcpDescriptionBox;
    mcpDescriptionBox.AcceptsReturn(true);
    mcpDescriptionBox.Height(88);
    mcpDescriptionBox.TextWrapping(TextWrapping::Wrap);
    mcpDescriptionBox.PlaceholderText(L"What tools or capabilities does this MCP lane expose?");

    TextBlock mcpIdLabel;
    mcpIdLabel.Style(Application::Current().Resources().Lookup(box_value(L"ShellLabelTextStyle")).try_as<Style>());
    mcpIdLabel.Text(L"MCP Server ID");
    mcpPanel.Children().Append(mcpIdLabel);
    mcpPanel.Children().Append(mcpIdBox);

    TextBlock mcpDisplayLabel;
    mcpDisplayLabel.Style(Application::Current().Resources().Lookup(box_value(L"ShellLabelTextStyle")).try_as<Style>());
    mcpDisplayLabel.Text(L"Display Name");
    mcpPanel.Children().Append(mcpDisplayLabel);
    mcpPanel.Children().Append(mcpDisplayNameBox);

    Grid mcpHostGrid;
    mcpHostGrid.ColumnSpacing(12);
    mcpHostGrid.ColumnDefinitions().Append(ColumnDefinition());
    mcpHostGrid.ColumnDefinitions().Append(ColumnDefinition());
    mcpHostGrid.ColumnDefinitions().Append(ColumnDefinition());

    StackPanel mcpHostPanel;
    mcpHostPanel.Spacing(6);
    TextBlock mcpHostLabel;
    mcpHostLabel.Style(Application::Current().Resources().Lookup(box_value(L"ShellLabelTextStyle")).try_as<Style>());
    mcpHostLabel.Text(L"Host");
    mcpHostPanel.Children().Append(mcpHostLabel);
    mcpHostPanel.Children().Append(mcpHostBox);

    StackPanel mcpPortPanel;
    mcpPortPanel.Spacing(6);
    TextBlock mcpPortLabel;
    mcpPortLabel.Style(Application::Current().Resources().Lookup(box_value(L"ShellLabelTextStyle")).try_as<Style>());
    mcpPortLabel.Text(L"Port");
    mcpPortPanel.Children().Append(mcpPortLabel);
    mcpPortPanel.Children().Append(mcpPortBox);
    Grid::SetColumn(mcpPortPanel, 1);

    StackPanel mcpProtocolPanel;
    mcpProtocolPanel.Spacing(6);
    TextBlock mcpProtocolLabel;
    mcpProtocolLabel.Style(Application::Current().Resources().Lookup(box_value(L"ShellLabelTextStyle")).try_as<Style>());
    mcpProtocolLabel.Text(L"Protocol");
    mcpProtocolPanel.Children().Append(mcpProtocolLabel);
    mcpProtocolPanel.Children().Append(mcpProtocolBox);
    Grid::SetColumn(mcpProtocolPanel, 2);

    mcpHostGrid.Children().Append(mcpHostPanel);
    mcpHostGrid.Children().Append(mcpPortPanel);
    mcpHostGrid.Children().Append(mcpProtocolPanel);
    mcpPanel.Children().Append(mcpHostGrid);

    TextBlock mcpRouteLabel;
    mcpRouteLabel.Style(Application::Current().Resources().Lookup(box_value(L"ShellLabelTextStyle")).try_as<Style>());
    mcpRouteLabel.Text(L"Shared Route Path");
    mcpPanel.Children().Append(mcpRouteLabel);
    mcpPanel.Children().Append(mcpRoutePathBox);

    TextBlock mcpDescriptionLabel;
    mcpDescriptionLabel.Style(Application::Current().Resources().Lookup(box_value(L"ShellLabelTextStyle")).try_as<Style>());
    mcpDescriptionLabel.Text(L"Notes");
    mcpPanel.Children().Append(mcpDescriptionLabel);
    mcpPanel.Children().Append(mcpDescriptionBox);

    StackPanel subAgentPanel;
    subAgentPanel.Spacing(10);

    TextBox subAgentIdBox;
    subAgentIdBox.PlaceholderText(L"swift-specialist");
    TextBox subAgentDisplayNameBox;
    subAgentDisplayNameBox.PlaceholderText(L"Swift Specialist");
    TextBox subAgentSpecializationBox;
    subAgentSpecializationBox.PlaceholderText(L"Swift, C++, documentation, test automation...");
    TextBox subAgentHostBox;
    subAgentHostBox.PlaceholderText(L"Optional host");
    TextBox subAgentPortBox;
    subAgentPortBox.PlaceholderText(L"0");
    TextBox subAgentProtocolBox;
    subAgentProtocolBox.PlaceholderText(L"virtual");
    TextBox subAgentRoutePathBox;
    subAgentRoutePathBox.PlaceholderText(L"/status");
    TextBox subAgentDescriptionBox;
    subAgentDescriptionBox.AcceptsReturn(true);
    subAgentDescriptionBox.Height(88);
    subAgentDescriptionBox.TextWrapping(TextWrapping::Wrap);
    subAgentDescriptionBox.PlaceholderText(L"Optional notes about this specialist lane.");

    TextBlock subAgentIdLabel;
    subAgentIdLabel.Style(Application::Current().Resources().Lookup(box_value(L"ShellLabelTextStyle")).try_as<Style>());
    subAgentIdLabel.Text(L"Sub-Agent ID");
    subAgentPanel.Children().Append(subAgentIdLabel);
    subAgentPanel.Children().Append(subAgentIdBox);

    TextBlock subAgentDisplayLabel;
    subAgentDisplayLabel.Style(Application::Current().Resources().Lookup(box_value(L"ShellLabelTextStyle")).try_as<Style>());
    subAgentDisplayLabel.Text(L"Display Name");
    subAgentPanel.Children().Append(subAgentDisplayLabel);
    subAgentPanel.Children().Append(subAgentDisplayNameBox);

    TextBlock subAgentSpecializationLabel;
    subAgentSpecializationLabel.Style(Application::Current().Resources().Lookup(box_value(L"ShellLabelTextStyle")).try_as<Style>());
    subAgentSpecializationLabel.Text(L"Specialization");
    subAgentPanel.Children().Append(subAgentSpecializationLabel);
    subAgentPanel.Children().Append(subAgentSpecializationBox);

    Grid subAgentHostGrid;
    subAgentHostGrid.ColumnSpacing(12);
    subAgentHostGrid.ColumnDefinitions().Append(ColumnDefinition());
    subAgentHostGrid.ColumnDefinitions().Append(ColumnDefinition());
    subAgentHostGrid.ColumnDefinitions().Append(ColumnDefinition());

    StackPanel subAgentHostPanel;
    subAgentHostPanel.Spacing(6);
    TextBlock subAgentHostLabel;
    subAgentHostLabel.Style(Application::Current().Resources().Lookup(box_value(L"ShellLabelTextStyle")).try_as<Style>());
    subAgentHostLabel.Text(L"Host");
    subAgentHostPanel.Children().Append(subAgentHostLabel);
    subAgentHostPanel.Children().Append(subAgentHostBox);

    StackPanel subAgentPortPanel;
    subAgentPortPanel.Spacing(6);
    TextBlock subAgentPortLabel;
    subAgentPortLabel.Style(Application::Current().Resources().Lookup(box_value(L"ShellLabelTextStyle")).try_as<Style>());
    subAgentPortLabel.Text(L"Port");
    subAgentPortPanel.Children().Append(subAgentPortLabel);
    subAgentPortPanel.Children().Append(subAgentPortBox);
    Grid::SetColumn(subAgentPortPanel, 1);

    StackPanel subAgentProtocolPanel;
    subAgentProtocolPanel.Spacing(6);
    TextBlock subAgentProtocolLabel;
    subAgentProtocolLabel.Style(Application::Current().Resources().Lookup(box_value(L"ShellLabelTextStyle")).try_as<Style>());
    subAgentProtocolLabel.Text(L"Protocol");
    subAgentProtocolPanel.Children().Append(subAgentProtocolLabel);
    subAgentProtocolPanel.Children().Append(subAgentProtocolBox);
    Grid::SetColumn(subAgentProtocolPanel, 2);

    subAgentHostGrid.Children().Append(subAgentHostPanel);
    subAgentHostGrid.Children().Append(subAgentPortPanel);
    subAgentHostGrid.Children().Append(subAgentProtocolPanel);
    subAgentPanel.Children().Append(subAgentHostGrid);

    TextBlock subAgentRouteLabel;
    subAgentRouteLabel.Style(Application::Current().Resources().Lookup(box_value(L"ShellLabelTextStyle")).try_as<Style>());
    subAgentRouteLabel.Text(L"Route Path");
    subAgentPanel.Children().Append(subAgentRouteLabel);
    subAgentPanel.Children().Append(subAgentRoutePathBox);

    TextBlock subAgentDescriptionLabel;
    subAgentDescriptionLabel.Style(Application::Current().Resources().Lookup(box_value(L"ShellLabelTextStyle")).try_as<Style>());
    subAgentDescriptionLabel.Text(L"Notes");
    subAgentPanel.Children().Append(subAgentDescriptionLabel);
    subAgentPanel.Children().Append(subAgentDescriptionBox);

    StackPanel applePanel;
    applePanel.Spacing(10);

    TextBox appleHostIdBox;
    appleHostIdBox.PlaceholderText(L"apple-host-01");
    TextBox appleDisplayNameBox;
    appleDisplayNameBox.PlaceholderText(L"Primary Apple Build Host");
    TextBox appleAddressBox;
    appleAddressBox.PlaceholderText(L"mac-builder.local");
    TextBox applePortBox;
    applePortBox.PlaceholderText(L"8081");
    TextBox appleUsernameBox;
    appleUsernameBox.PlaceholderText(L"builder");
    TextBox appleServiceBaseUrlBox;
    appleServiceBaseUrlBox.PlaceholderText(L"http://mac-builder.local:8081");
    TextBox appleHealthPathBox;
    appleHealthPathBox.PlaceholderText(L"/healthz");
    TextBox appleExecutePathBox;
    appleExecutePathBox.PlaceholderText(L"/execute");
    TextBox appleDeveloperDirectoryBox;
    appleDeveloperDirectoryBox.PlaceholderText(L"/Applications/Xcode.app/Contents/Developer");
    TextBox appleSigningIdentityBox;
    appleSigningIdentityBox.PlaceholderText(L"Developer ID Application: Example Corp");
    TextBox appleNotaryProfileBox;
    appleNotaryProfileBox.PlaceholderText(L"mastercontrol-notary");
    TextBox appleTeamIdBox;
    appleTeamIdBox.PlaceholderText(L"ABCDE12345");

    TextBlock appleIdLabel;
    appleIdLabel.Style(Application::Current().Resources().Lookup(box_value(L"ShellLabelTextStyle")).try_as<Style>());
    appleIdLabel.Text(L"Apple Host ID");
    applePanel.Children().Append(appleIdLabel);
    applePanel.Children().Append(appleHostIdBox);

    TextBlock appleDisplayLabel;
    appleDisplayLabel.Style(Application::Current().Resources().Lookup(box_value(L"ShellLabelTextStyle")).try_as<Style>());
    appleDisplayLabel.Text(L"Display Name");
    applePanel.Children().Append(appleDisplayLabel);
    applePanel.Children().Append(appleDisplayNameBox);

    ComboBox appleTransportSelector;
    ComboBoxItem appleCompanionItem;
    appleCompanionItem.Content(box_value(L"Companion Service"));
    appleCompanionItem.Tag(box_value(L"companion_service"));
    appleTransportSelector.Items().Append(appleCompanionItem);
    ComboBoxItem appleSshItem;
    appleSshItem.Content(box_value(L"SSH"));
    appleSshItem.Tag(box_value(L"ssh"));
    appleTransportSelector.Items().Append(appleSshItem);
    appleTransportSelector.SelectedIndex(0);
    TextBlock appleTransportLabel;
    appleTransportLabel.Style(Application::Current().Resources().Lookup(box_value(L"ShellLabelTextStyle")).try_as<Style>());
    appleTransportLabel.Text(L"Transport");
    applePanel.Children().Append(appleTransportLabel);
    applePanel.Children().Append(appleTransportSelector);

    Grid appleEndpointGrid;
    appleEndpointGrid.ColumnSpacing(12);
    appleEndpointGrid.ColumnDefinitions().Append(ColumnDefinition());
    appleEndpointGrid.ColumnDefinitions().Append(ColumnDefinition());
    appleEndpointGrid.ColumnDefinitions().Append(ColumnDefinition());

    StackPanel appleAddressPanel;
    appleAddressPanel.Spacing(6);
    TextBlock appleAddressLabel;
    appleAddressLabel.Style(Application::Current().Resources().Lookup(box_value(L"ShellLabelTextStyle")).try_as<Style>());
    appleAddressLabel.Text(L"Address or Hostname");
    appleAddressPanel.Children().Append(appleAddressLabel);
    appleAddressPanel.Children().Append(appleAddressBox);

    StackPanel applePortPanel;
    applePortPanel.Spacing(6);
    TextBlock applePortLabel;
    applePortLabel.Style(Application::Current().Resources().Lookup(box_value(L"ShellLabelTextStyle")).try_as<Style>());
    applePortLabel.Text(L"Port");
    applePortPanel.Children().Append(applePortLabel);
    applePortPanel.Children().Append(applePortBox);
    Grid::SetColumn(applePortPanel, 1);

    StackPanel appleUsernamePanel;
    appleUsernamePanel.Spacing(6);
    TextBlock appleUsernameLabel;
    appleUsernameLabel.Style(Application::Current().Resources().Lookup(box_value(L"ShellLabelTextStyle")).try_as<Style>());
    appleUsernameLabel.Text(L"SSH Username");
    appleUsernamePanel.Children().Append(appleUsernameLabel);
    appleUsernamePanel.Children().Append(appleUsernameBox);
    Grid::SetColumn(appleUsernamePanel, 2);

    appleEndpointGrid.Children().Append(appleAddressPanel);
    appleEndpointGrid.Children().Append(applePortPanel);
    appleEndpointGrid.Children().Append(appleUsernamePanel);
    applePanel.Children().Append(appleEndpointGrid);

    StackPanel appleServiceBaseUrlPanel;
    appleServiceBaseUrlPanel.Spacing(6);
    TextBlock appleServiceBaseUrlLabel;
    appleServiceBaseUrlLabel.Style(Application::Current().Resources().Lookup(box_value(L"ShellLabelTextStyle")).try_as<Style>());
    appleServiceBaseUrlLabel.Text(L"Companion Base URL");
    appleServiceBaseUrlPanel.Children().Append(appleServiceBaseUrlLabel);
    appleServiceBaseUrlPanel.Children().Append(appleServiceBaseUrlBox);
    applePanel.Children().Append(appleServiceBaseUrlPanel);

    Grid appleCompanionGrid;
    appleCompanionGrid.ColumnSpacing(12);
    appleCompanionGrid.ColumnDefinitions().Append(ColumnDefinition());
    appleCompanionGrid.ColumnDefinitions().Append(ColumnDefinition());

    StackPanel appleHealthPanel;
    appleHealthPanel.Spacing(6);
    TextBlock appleHealthLabel;
    appleHealthLabel.Style(Application::Current().Resources().Lookup(box_value(L"ShellLabelTextStyle")).try_as<Style>());
    appleHealthLabel.Text(L"Companion Health Path");
    appleHealthPanel.Children().Append(appleHealthLabel);
    appleHealthPanel.Children().Append(appleHealthPathBox);

    StackPanel appleExecutePanel;
    appleExecutePanel.Spacing(6);
    TextBlock appleExecuteLabel;
    appleExecuteLabel.Style(Application::Current().Resources().Lookup(box_value(L"ShellLabelTextStyle")).try_as<Style>());
    appleExecuteLabel.Text(L"Companion Execute Path");
    appleExecutePanel.Children().Append(appleExecuteLabel);
    appleExecutePanel.Children().Append(appleExecutePathBox);
    Grid::SetColumn(appleExecutePanel, 1);

    appleCompanionGrid.Children().Append(appleHealthPanel);
    appleCompanionGrid.Children().Append(appleExecutePanel);
    applePanel.Children().Append(appleCompanionGrid);

    TextBlock appleDeveloperLabel;
    appleDeveloperLabel.Style(Application::Current().Resources().Lookup(box_value(L"ShellLabelTextStyle")).try_as<Style>());
    appleDeveloperLabel.Text(L"Preferred Developer Directory");
    applePanel.Children().Append(appleDeveloperLabel);
    applePanel.Children().Append(appleDeveloperDirectoryBox);

    Grid appleDefaultsGrid;
    appleDefaultsGrid.ColumnSpacing(12);
    appleDefaultsGrid.ColumnDefinitions().Append(ColumnDefinition());
    appleDefaultsGrid.ColumnDefinitions().Append(ColumnDefinition());

    StackPanel appleSigningPanel;
    appleSigningPanel.Spacing(6);
    TextBlock appleSigningLabel;
    appleSigningLabel.Style(Application::Current().Resources().Lookup(box_value(L"ShellLabelTextStyle")).try_as<Style>());
    appleSigningLabel.Text(L"Default Signing Identity");
    appleSigningPanel.Children().Append(appleSigningLabel);
    appleSigningPanel.Children().Append(appleSigningIdentityBox);

    StackPanel appleNotaryProfilePanel;
    appleNotaryProfilePanel.Spacing(6);
    TextBlock appleNotaryProfileLabel;
    appleNotaryProfileLabel.Style(Application::Current().Resources().Lookup(box_value(L"ShellLabelTextStyle")).try_as<Style>());
    appleNotaryProfileLabel.Text(L"Default Notary Profile");
    appleNotaryProfilePanel.Children().Append(appleNotaryProfileLabel);
    appleNotaryProfilePanel.Children().Append(appleNotaryProfileBox);
    Grid::SetColumn(appleNotaryProfilePanel, 1);

    appleDefaultsGrid.Children().Append(appleSigningPanel);
    appleDefaultsGrid.Children().Append(appleNotaryProfilePanel);
    applePanel.Children().Append(appleDefaultsGrid);

    TextBlock appleTeamIdLabel;
    appleTeamIdLabel.Style(Application::Current().Resources().Lookup(box_value(L"ShellLabelTextStyle")).try_as<Style>());
    appleTeamIdLabel.Text(L"Default Notary Team ID");
    applePanel.Children().Append(appleTeamIdLabel);
    applePanel.Children().Append(appleTeamIdBox);

    StackPanel applePlatformPanel;
    applePlatformPanel.Orientation(Orientation::Horizontal);
    applePlatformPanel.Spacing(18);
    CheckBox appleMacPlatformCheckBox;
    appleMacPlatformCheckBox.Content(box_value(L"macOS"));
    CheckBox appleIosPlatformCheckBox;
    appleIosPlatformCheckBox.Content(box_value(L"iOS"));
    CheckBox appleEnabledCheckBox;
    appleEnabledCheckBox.Content(box_value(L"Enabled"));
    applePlatformPanel.Children().Append(appleMacPlatformCheckBox);
    applePlatformPanel.Children().Append(appleIosPlatformCheckBox);
    applePlatformPanel.Children().Append(appleEnabledCheckBox);
    applePanel.Children().Append(applePlatformPanel);

    root.Children().Append(mcpPanel);
    root.Children().Append(subAgentPanel);
    root.Children().Append(applePanel);

    TextBlock summaryText;
    summaryText.Style(Application::Current().Resources().Lookup(box_value(L"ShellDataTextStyle")).try_as<Style>());
    summaryText.TextWrapping(TextWrapping::WrapWholeWords);
    root.Children().Append(summaryText);

    TextBlock statusText;
    statusText.Style(Application::Current().Resources().Lookup(box_value(L"ShellDataTextStyle")).try_as<Style>());
    statusText.TextWrapping(TextWrapping::WrapWholeWords);
    root.Children().Append(statusText);

    StackPanel buttonRow;
    buttonRow.Orientation(Orientation::Horizontal);
    buttonRow.Spacing(12);

    Button saveButton;
    saveButton.Content(box_value(L"Save Runtime Lane"));
    saveButton.Style(Application::Current().Resources().Lookup(box_value(L"ShellCommandButtonStyle")).try_as<Style>());
    saveButton.IsEnabled(false);

    Button removeButton;
    removeButton.Content(box_value(L"Remove Runtime Lane"));
    removeButton.Style(Application::Current().Resources().Lookup(box_value(L"ShellCommandButtonStyle")).try_as<Style>());
    removeButton.IsEnabled(false);

    buttonRow.Children().Append(saveButton);
    buttonRow.Children().Append(removeButton);
    root.Children().Append(buttonRow);

    scrollViewer.Content(root);
    dialog.Content(scrollViewer);

    const auto currentKind = [kindSelector]() -> std::wstring {
        const auto item = kindSelector.SelectedItem().try_as<ComboBoxItem>();
        return item == nullptr
            ? std::wstring(L"mcp")
            : std::wstring(unbox_value_or<hstring>(item.Tag(), hstring(L"mcp")).c_str());
    };

    const auto updateAppleTransportForm = [&, this]() {
        const auto selectedItem = appleTransportSelector.SelectedItem().try_as<ComboBoxItem>();
        const auto transport = selectedItem == nullptr
            ? std::wstring(L"companion_service")
            : std::wstring(unbox_value_or<hstring>(selectedItem.Tag(), hstring(L"companion_service")).c_str());
        const bool companionSelected = transport == L"companion_service";
        setElementVisibility(appleServiceBaseUrlPanel, companionSelected);
        setElementVisibility(appleCompanionGrid, companionSelected);
        setElementVisibility(appleUsernamePanel, !companionSelected);
    };

    const auto clearMcpFields = [&]() {
        mcpIdBox.Text(L"");
        mcpDisplayNameBox.Text(L"");
        mcpHostBox.Text(L"");
        mcpPortBox.Text(L"");
        mcpProtocolBox.Text(L"http");
        mcpRoutePathBox.Text(L"/mcp");
        mcpDescriptionBox.Text(L"");
    };

    const auto clearSubAgentFields = [&]() {
        subAgentIdBox.Text(L"");
        subAgentDisplayNameBox.Text(L"");
        subAgentSpecializationBox.Text(L"");
        subAgentHostBox.Text(L"");
        subAgentPortBox.Text(L"");
        subAgentProtocolBox.Text(L"virtual");
        subAgentRoutePathBox.Text(L"");
        subAgentDescriptionBox.Text(L"");
    };

    const auto clearAppleFields = [&]() {
        appleHostIdBox.Text(L"");
        appleDisplayNameBox.Text(L"");
        appleTransportSelector.SelectedIndex(0);
        appleAddressBox.Text(L"");
        applePortBox.Text(L"8081");
        appleUsernameBox.Text(L"");
        appleServiceBaseUrlBox.Text(L"");
        appleHealthPathBox.Text(L"/healthz");
        appleExecutePathBox.Text(L"/execute");
        appleDeveloperDirectoryBox.Text(L"");
        appleSigningIdentityBox.Text(L"");
        appleNotaryProfileBox.Text(L"");
        appleTeamIdBox.Text(L"");
        appleMacPlatformCheckBox.IsChecked(true);
        appleIosPlatformCheckBox.IsChecked(true);
        appleEnabledCheckBox.IsChecked(true);
    };

    const auto refreshRecordSelector = [&]() {
        recordSelector.Items().Clear();
        const auto kind = currentKind();
        if (kind == L"mcp") {
            for (const auto& endpoint : customMcpServers) {
                ComboBoxItem item;
                const auto label = endpoint.routePath.empty()
                    ? endpoint.displayName
                    : (endpoint.displayName + L"  |  " + endpoint.routePath);
                item.Content(box_value(winrt::hstring(label)));
                item.Tag(box_value(winrt::hstring(endpoint.id)));
                recordSelector.Items().Append(item);
            }
        } else if (kind == L"subagent") {
            for (const auto& endpoint : customSubAgents) {
                ComboBoxItem item;
                const auto label = endpoint.specialization.empty()
                    ? endpoint.displayName
                    : (endpoint.displayName + L"  |  " + endpoint.specialization);
                item.Content(box_value(winrt::hstring(label)));
                item.Tag(box_value(winrt::hstring(endpoint.id)));
                recordSelector.Items().Append(item);
            }
        } else {
            for (const auto& host : appleHosts) {
                ComboBoxItem item;
                const auto label = host.transport.empty()
                    ? host.displayName
                    : (host.displayName + L"  |  " + host.transport);
                item.Content(box_value(winrt::hstring(label)));
                item.Tag(box_value(winrt::hstring(host.hostId)));
                recordSelector.Items().Append(item);
            }
        }

        const bool hasItems = recordSelector.Items().Size() > 0;
        recordSelector.IsEnabled(hasItems);
        recordSelector.SelectedIndex(hasItems ? 0 : -1);
    };

    const auto updateSummary = [&, this]() {
        const auto kind = currentKind();
        std::wstring summary;
        if (kind == L"mcp") {
            summary = L"Shared MCP | ";
            summary += trimCopy(std::wstring(mcpDisplayNameBox.Text().c_str())).empty()
                ? L"no lane selected"
                : trimCopy(std::wstring(mcpDisplayNameBox.Text().c_str()));
            summary += L" | ";
            summary += trimCopy(std::wstring(mcpHostBox.Text().c_str())).empty()
                ? L"host pending"
                : trimCopy(std::wstring(mcpHostBox.Text().c_str()));
            summary += L":";
            summary += trimCopy(std::wstring(mcpPortBox.Text().c_str())).empty()
                ? L"port pending"
                : trimCopy(std::wstring(mcpPortBox.Text().c_str()));
            if (!trimCopy(std::wstring(mcpRoutePathBox.Text().c_str())).empty()) {
                summary += L" | ";
                summary += trimCopy(std::wstring(mcpRoutePathBox.Text().c_str()));
            }
        } else if (kind == L"subagent") {
            summary = L"Sub-Agent | ";
            summary += trimCopy(std::wstring(subAgentDisplayNameBox.Text().c_str())).empty()
                ? L"no lane selected"
                : trimCopy(std::wstring(subAgentDisplayNameBox.Text().c_str()));
            if (!trimCopy(std::wstring(subAgentSpecializationBox.Text().c_str())).empty()) {
                summary += L" | ";
                summary += trimCopy(std::wstring(subAgentSpecializationBox.Text().c_str()));
            }
            if (!trimCopy(std::wstring(subAgentProtocolBox.Text().c_str())).empty()) {
                summary += L" | ";
                summary += trimCopy(std::wstring(subAgentProtocolBox.Text().c_str()));
            }
        } else {
            summary = L"Apple Host | ";
            summary += trimCopy(std::wstring(appleDisplayNameBox.Text().c_str())).empty()
                ? L"no host selected"
                : trimCopy(std::wstring(appleDisplayNameBox.Text().c_str()));
            summary += L" | ";
            summary += trimCopy(std::wstring(appleAddressBox.Text().c_str())).empty()
                ? L"address pending"
                : trimCopy(std::wstring(appleAddressBox.Text().c_str()));
            const auto selectedItem = appleTransportSelector.SelectedItem().try_as<ComboBoxItem>();
            const auto transport = selectedItem == nullptr
                ? std::wstring(L"companion_service")
                : std::wstring(unbox_value_or<hstring>(selectedItem.Tag(), hstring(L"companion_service")).c_str());
            summary += L" | ";
            summary += transport == L"ssh" ? L"SSH" : L"Companion Service";
            summary += L" | platforms ";
            std::vector<std::wstring> platforms;
            const auto macChecked = appleMacPlatformCheckBox.IsChecked();
            if (macChecked && macChecked.Value()) {
                platforms.push_back(L"macOS");
            }
            const auto iosChecked = appleIosPlatformCheckBox.IsChecked();
            if (iosChecked && iosChecked.Value()) {
                platforms.push_back(L"iOS");
            }
            summary += platforms.empty() ? L"none" : joinValues(platforms, L", ");
        }
        summaryText.Text(winrt::hstring(summary));
    };

    const auto populateCurrentSelection = [&]() {
        clearMcpFields();
        clearSubAgentFields();
        clearAppleFields();

        const auto kind = currentKind();
        const auto index = recordSelector.SelectedIndex();
        const bool hasSelection = index >= 0;

        if (kind == L"mcp" && hasSelection && index < static_cast<int>(customMcpServers.size())) {
            const auto& endpoint = customMcpServers[static_cast<size_t>(index)];
            mcpIdBox.Text(winrt::hstring(endpoint.id));
            mcpDisplayNameBox.Text(winrt::hstring(endpoint.displayName));
            mcpHostBox.Text(winrt::hstring(endpoint.host));
            mcpPortBox.Text(winrt::hstring(endpoint.port == 0 ? std::wstring{} : std::to_wstring(endpoint.port)));
            mcpProtocolBox.Text(winrt::hstring(endpoint.protocol.empty() ? L"http" : endpoint.protocol));
            mcpRoutePathBox.Text(winrt::hstring(endpoint.routePath.empty() ? L"/mcp" : endpoint.routePath));
            mcpDescriptionBox.Text(winrt::hstring(endpoint.description));
            statusText.Text(L"Loaded the selected shared MCP lane. Update the published details here or remove it cleanly.");
        } else if (kind == L"subagent" && hasSelection && index < static_cast<int>(customSubAgents.size())) {
            const auto& endpoint = customSubAgents[static_cast<size_t>(index)];
            subAgentIdBox.Text(winrt::hstring(endpoint.id));
            subAgentDisplayNameBox.Text(winrt::hstring(endpoint.displayName));
            subAgentSpecializationBox.Text(winrt::hstring(endpoint.specialization));
            subAgentHostBox.Text(winrt::hstring(endpoint.host));
            subAgentPortBox.Text(winrt::hstring(endpoint.port == 0 ? std::wstring{} : std::to_wstring(endpoint.port)));
            subAgentProtocolBox.Text(winrt::hstring(endpoint.protocol.empty() ? L"virtual" : endpoint.protocol));
            subAgentRoutePathBox.Text(winrt::hstring(endpoint.routePath));
            subAgentDescriptionBox.Text(winrt::hstring(endpoint.description));
            statusText.Text(L"Loaded the selected custom sub-agent. Update its specialist details here or remove it cleanly.");
        } else if (kind == L"apple" && hasSelection && index < static_cast<int>(appleHosts.size())) {
            const auto& host = appleHosts[static_cast<size_t>(index)];
            appleHostIdBox.Text(winrt::hstring(host.hostId));
            appleDisplayNameBox.Text(winrt::hstring(host.displayName));
            appleTransportSelector.SelectedIndex(host.transport == L"ssh" ? 1 : 0);
            appleAddressBox.Text(winrt::hstring(host.address));
            applePortBox.Text(winrt::hstring(host.port == 0 ? std::wstring{} : std::to_wstring(host.port)));
            appleUsernameBox.Text(winrt::hstring(host.username));
            appleServiceBaseUrlBox.Text(winrt::hstring(host.serviceBaseUrl));
            appleHealthPathBox.Text(winrt::hstring(host.companionHealthPath.empty() ? L"/healthz" : host.companionHealthPath));
            appleExecutePathBox.Text(winrt::hstring(host.companionExecutePath.empty() ? L"/execute" : host.companionExecutePath));
            appleDeveloperDirectoryBox.Text(winrt::hstring(host.preferredDeveloperDirectory));
            appleSigningIdentityBox.Text(winrt::hstring(host.defaultSigningIdentity));
            appleNotaryProfileBox.Text(winrt::hstring(host.defaultNotaryKeychainProfile));
            appleTeamIdBox.Text(winrt::hstring(host.defaultNotaryTeamId));
            appleMacPlatformCheckBox.IsChecked(std::find(host.platforms.begin(), host.platforms.end(), L"macos") != host.platforms.end());
            appleIosPlatformCheckBox.IsChecked(std::find(host.platforms.begin(), host.platforms.end(), L"ios") != host.platforms.end());
            appleEnabledCheckBox.IsChecked(host.enabled);
            statusText.Text(L"Loaded the selected Apple host. Update transport, defaults, or platform coverage here or remove it cleanly.");
        } else if (kind == L"mcp") {
            statusText.Text(L"No custom MCP server lanes are published yet. Use the New MCP Server wizard first.");
        } else if (kind == L"subagent") {
            statusText.Text(L"No custom sub-agent lanes are published yet. Use the New Sub-Agent wizard first.");
        } else {
            statusText.Text(L"No Apple remote hosts are registered yet. Use the New Apple Host wizard first.");
        }

        setElementVisibility(mcpPanel, kind == L"mcp");
        setElementVisibility(subAgentPanel, kind == L"subagent");
        setElementVisibility(applePanel, kind == L"apple");
        updateAppleTransportForm();
        saveButton.IsEnabled(hasSelection);
        removeButton.IsEnabled(hasSelection);
        updateSummary();
    };

    kindSelector.SelectionChanged([&](IInspectable const&, Controls::SelectionChangedEventArgs const&) {
        refreshRecordSelector();
        populateCurrentSelection();
    });
    recordSelector.SelectionChanged([&](IInspectable const&, Controls::SelectionChangedEventArgs const&) { populateCurrentSelection(); });
    appleTransportSelector.SelectionChanged([&](IInspectable const&, Controls::SelectionChangedEventArgs const&) {
        updateAppleTransportForm();
        updateSummary();
    });

    const auto attachSummaryTextChanged = [&updateSummary](TextBox const& textBox) {
        textBox.TextChanged([&](IInspectable const&, Controls::TextChangedEventArgs const&) { updateSummary(); });
    };
    attachSummaryTextChanged(mcpIdBox);
    attachSummaryTextChanged(mcpDisplayNameBox);
    attachSummaryTextChanged(mcpHostBox);
    attachSummaryTextChanged(mcpPortBox);
    attachSummaryTextChanged(mcpProtocolBox);
    attachSummaryTextChanged(mcpRoutePathBox);
    attachSummaryTextChanged(mcpDescriptionBox);
    attachSummaryTextChanged(subAgentIdBox);
    attachSummaryTextChanged(subAgentDisplayNameBox);
    attachSummaryTextChanged(subAgentSpecializationBox);
    attachSummaryTextChanged(subAgentHostBox);
    attachSummaryTextChanged(subAgentPortBox);
    attachSummaryTextChanged(subAgentProtocolBox);
    attachSummaryTextChanged(subAgentRoutePathBox);
    attachSummaryTextChanged(subAgentDescriptionBox);
    attachSummaryTextChanged(appleHostIdBox);
    attachSummaryTextChanged(appleDisplayNameBox);
    attachSummaryTextChanged(appleAddressBox);
    attachSummaryTextChanged(applePortBox);
    attachSummaryTextChanged(appleUsernameBox);
    attachSummaryTextChanged(appleServiceBaseUrlBox);
    attachSummaryTextChanged(appleHealthPathBox);
    attachSummaryTextChanged(appleExecutePathBox);
    attachSummaryTextChanged(appleDeveloperDirectoryBox);
    attachSummaryTextChanged(appleSigningIdentityBox);
    attachSummaryTextChanged(appleNotaryProfileBox);
    attachSummaryTextChanged(appleTeamIdBox);

    appleMacPlatformCheckBox.Click([&](IInspectable const&, RoutedEventArgs const&) { updateSummary(); });
    appleIosPlatformCheckBox.Click([&](IInspectable const&, RoutedEventArgs const&) { updateSummary(); });
    appleEnabledCheckBox.Click([&](IInspectable const&, RoutedEventArgs const&) { updateSummary(); });

    saveButton.Click([this, dialog, saveButton, statusText, kindSelector, recordSelector, mcpIdBox, mcpDisplayNameBox, mcpHostBox, mcpPortBox, mcpProtocolBox, mcpRoutePathBox, mcpDescriptionBox, subAgentIdBox, subAgentDisplayNameBox, subAgentSpecializationBox, subAgentHostBox, subAgentPortBox, subAgentProtocolBox, subAgentRoutePathBox, subAgentDescriptionBox, appleHostIdBox, appleDisplayNameBox, appleTransportSelector, appleAddressBox, applePortBox, appleUsernameBox, appleServiceBaseUrlBox, appleHealthPathBox, appleExecutePathBox, appleDeveloperDirectoryBox, appleSigningIdentityBox, appleNotaryProfileBox, appleTeamIdBox, appleMacPlatformCheckBox, appleIosPlatformCheckBox, appleEnabledCheckBox](IInspectable const&, RoutedEventArgs const&) {
        auto ignored = [this, dialog, saveButton, statusText, kindSelector, recordSelector, mcpIdBox, mcpDisplayNameBox, mcpHostBox, mcpPortBox, mcpProtocolBox, mcpRoutePathBox, mcpDescriptionBox, subAgentIdBox, subAgentDisplayNameBox, subAgentSpecializationBox, subAgentHostBox, subAgentPortBox, subAgentProtocolBox, subAgentRoutePathBox, subAgentDescriptionBox, appleHostIdBox, appleDisplayNameBox, appleTransportSelector, appleAddressBox, applePortBox, appleUsernameBox, appleServiceBaseUrlBox, appleHealthPathBox, appleExecutePathBox, appleDeveloperDirectoryBox, appleSigningIdentityBox, appleNotaryProfileBox, appleTeamIdBox, appleMacPlatformCheckBox, appleIosPlatformCheckBox, appleEnabledCheckBox]() -> IAsyncAction {
            if (recordSelector.SelectedIndex() < 0) {
                statusText.Text(L"Choose a published runtime lane before saving changes.");
                co_return;
            }

            const auto selectedKindItem = kindSelector.SelectedItem().try_as<ComboBoxItem>();
            const auto kind = selectedKindItem == nullptr
                ? std::wstring(L"mcp")
                : std::wstring(unbox_value_or<hstring>(selectedKindItem.Tag(), hstring(L"mcp")).c_str());

            winrt::apartment_context uiThread;
            saveButton.IsEnabled(false);

            if (kind == L"mcp") {
                const auto id = trimCopy(std::wstring(mcpIdBox.Text().c_str()));
                const auto displayName = trimCopy(std::wstring(mcpDisplayNameBox.Text().c_str()));
                const auto portText = trimCopy(std::wstring(mcpPortBox.Text().c_str()));
                if (id.empty() || displayName.empty() || portText.empty()) {
                    statusText.Text(L"MCP server ID, display name, and a port between 1 and 65535 are required.");
                    saveButton.IsEnabled(true);
                    co_return;
                }

                uint16_t port = 0;
                try {
                    const auto parsedPort = std::stoul(portText);
                    if (parsedPort == 0U || parsedPort > 65535U) {
                        statusText.Text(L"MCP server ID, display name, and a port between 1 and 65535 are required.");
                        saveButton.IsEnabled(true);
                        co_return;
                    }
                    port = static_cast<uint16_t>(parsedPort);
                } catch (...) {
                    statusText.Text(L"MCP server ID, display name, and a port between 1 and 65535 are required.");
                    saveButton.IsEnabled(true);
                    co_return;
                }

                statusText.Text(L"Saving the shared MCP lane through the local admin API.");
                ::MasterControlShell::ShellRuntimeEndpoint endpoint{
                    id,
                    displayName,
                    L"mcp_server",
                    trimCopy(std::wstring(mcpHostBox.Text().c_str())),
                    port,
                    trimCopy(std::wstring(mcpProtocolBox.Text().c_str())),
                    L"unknown",
                    trimCopy(std::wstring(mcpDescriptionBox.Text().c_str())),
                    trimCopy(std::wstring(mcpRoutePathBox.Text().c_str())),
                    L"",
                    true
                };

                co_await winrt::resume_background();
                const auto result = runtime_.UpsertMcpServer(endpoint);
                co_await uiThread;
                statusText.Text(winrt::hstring(result.message));
                if (!result.succeeded) {
                    saveButton.IsEnabled(true);
                    GuidedWorkflowStatusText().Text(L"Runtime maintenance needs attention. Review the wizard message and try again.");
                    co_return;
                }
                GuidedWorkflowStatusText().Text(winrt::hstring(L"Updated shared MCP lane '" + displayName + L"'."));
            } else if (kind == L"subagent") {
                const auto id = trimCopy(std::wstring(subAgentIdBox.Text().c_str()));
                const auto displayName = trimCopy(std::wstring(subAgentDisplayNameBox.Text().c_str()));
                if (id.empty() || displayName.empty()) {
                    statusText.Text(L"Sub-agent ID and display name are required, and the port must be blank or between 0 and 65535.");
                    saveButton.IsEnabled(true);
                    co_return;
                }

                uint16_t port = 0;
                const auto portText = trimCopy(std::wstring(subAgentPortBox.Text().c_str()));
                if (!portText.empty()) {
                    try {
                        const auto parsedPort = std::stoul(portText);
                        if (parsedPort > 65535U) {
                            statusText.Text(L"Sub-agent ID and display name are required, and the port must be blank or between 0 and 65535.");
                            saveButton.IsEnabled(true);
                            co_return;
                        }
                        port = static_cast<uint16_t>(parsedPort);
                    } catch (...) {
                        statusText.Text(L"Sub-agent ID and display name are required, and the port must be blank or between 0 and 65535.");
                        saveButton.IsEnabled(true);
                        co_return;
                    }
                }

                statusText.Text(L"Saving the custom sub-agent through the local admin API.");
                ::MasterControlShell::ShellRuntimeEndpoint endpoint{
                    id,
                    displayName,
                    L"sub_agent",
                    trimCopy(std::wstring(subAgentHostBox.Text().c_str())),
                    port,
                    trimCopy(std::wstring(subAgentProtocolBox.Text().c_str())),
                    L"unknown",
                    trimCopy(std::wstring(subAgentDescriptionBox.Text().c_str())),
                    trimCopy(std::wstring(subAgentRoutePathBox.Text().c_str())),
                    trimCopy(std::wstring(subAgentSpecializationBox.Text().c_str())),
                    true
                };

                co_await winrt::resume_background();
                const auto result = runtime_.UpsertSubAgent(endpoint);
                co_await uiThread;
                statusText.Text(winrt::hstring(result.message));
                if (!result.succeeded) {
                    saveButton.IsEnabled(true);
                    GuidedWorkflowStatusText().Text(L"Runtime maintenance needs attention. Review the wizard message and try again.");
                    co_return;
                }
                GuidedWorkflowStatusText().Text(winrt::hstring(L"Updated custom sub-agent lane '" + displayName + L"'."));
            } else {
                const auto hostId = trimCopy(std::wstring(appleHostIdBox.Text().c_str()));
                const auto displayName = trimCopy(std::wstring(appleDisplayNameBox.Text().c_str()));
                if (hostId.empty() || displayName.empty()) {
                    statusText.Text(L"Apple host ID, display name, transport, and at least one platform are required. Ports must be blank or between 0 and 65535.");
                    saveButton.IsEnabled(true);
                    co_return;
                }

                uint16_t port = 0;
                const auto portText = trimCopy(std::wstring(applePortBox.Text().c_str()));
                if (!portText.empty()) {
                    try {
                        const auto parsedPort = std::stoul(portText);
                        if (parsedPort > 65535U) {
                            statusText.Text(L"Apple host ID, display name, transport, and at least one platform are required. Ports must be blank or between 0 and 65535.");
                            saveButton.IsEnabled(true);
                            co_return;
                        }
                        port = static_cast<uint16_t>(parsedPort);
                    } catch (...) {
                        statusText.Text(L"Apple host ID, display name, transport, and at least one platform are required. Ports must be blank or between 0 and 65535.");
                        saveButton.IsEnabled(true);
                        co_return;
                    }
                }

                std::vector<std::wstring> platforms;
                const auto macChecked = appleMacPlatformCheckBox.IsChecked();
                if (macChecked && macChecked.Value()) {
                    platforms.push_back(L"macos");
                }
                const auto iosChecked = appleIosPlatformCheckBox.IsChecked();
                if (iosChecked && iosChecked.Value()) {
                    platforms.push_back(L"ios");
                }
                if (platforms.empty()) {
                    statusText.Text(L"Apple host ID, display name, transport, and at least one platform are required. Ports must be blank or between 0 and 65535.");
                    saveButton.IsEnabled(true);
                    co_return;
                }

                const auto selectedTransportItem = appleTransportSelector.SelectedItem().try_as<ComboBoxItem>();
                const auto transport = selectedTransportItem == nullptr
                    ? std::wstring(L"companion_service")
                    : std::wstring(unbox_value_or<hstring>(selectedTransportItem.Tag(), hstring(L"companion_service")).c_str());
                const auto enabledChecked = appleEnabledCheckBox.IsChecked();

                statusText.Text(L"Saving the Apple host through the local admin API.");
                ::MasterControlShell::ShellAppleRemoteHost host{
                    hostId,
                    displayName,
                    transport,
                    platforms,
                    trimCopy(std::wstring(appleAddressBox.Text().c_str())),
                    port,
                    trimCopy(std::wstring(appleUsernameBox.Text().c_str())),
                    trimCopy(std::wstring(appleServiceBaseUrlBox.Text().c_str())),
                    trimCopy(std::wstring(appleHealthPathBox.Text().c_str())),
                    trimCopy(std::wstring(appleExecutePathBox.Text().c_str())),
                    trimCopy(std::wstring(appleDeveloperDirectoryBox.Text().c_str())),
                    trimCopy(std::wstring(appleSigningIdentityBox.Text().c_str())),
                    trimCopy(std::wstring(appleNotaryProfileBox.Text().c_str())),
                    trimCopy(std::wstring(appleTeamIdBox.Text().c_str())),
                    enabledChecked && enabledChecked.Value()
                };

                co_await winrt::resume_background();
                const auto result = runtime_.UpsertAppleRemoteHost(host);
                co_await uiThread;
                statusText.Text(winrt::hstring(result.message));
                if (!result.succeeded) {
                    saveButton.IsEnabled(true);
                    GuidedWorkflowStatusText().Text(L"Runtime maintenance needs attention. Review the wizard message and try again.");
                    co_return;
                }
                GuidedWorkflowStatusText().Text(winrt::hstring(L"Updated Apple host '" + displayName + L"'."));
            }

            const auto completionMessage = GuidedWorkflowStatusText().Text();
            dialog.Hide();
            co_await CompleteGuidedWorkflowAsync(completionMessage, kRuntimeDestination);
        }();
        (void)ignored;
    });

    removeButton.Click([this, dialog, removeButton, saveButton, statusText, kindSelector, recordSelector, customMcpServers, customSubAgents, appleHosts](IInspectable const&, RoutedEventArgs const&) {
        auto ignored = [this, dialog, removeButton, saveButton, statusText, kindSelector, recordSelector, customMcpServers, customSubAgents, appleHosts]() -> IAsyncAction {
            if (recordSelector.SelectedIndex() < 0) {
                statusText.Text(L"Choose a published runtime lane before removing it.");
                co_return;
            }

            const auto selectedKindItem = kindSelector.SelectedItem().try_as<ComboBoxItem>();
            const auto kind = selectedKindItem == nullptr
                ? std::wstring(L"mcp")
                : std::wstring(unbox_value_or<hstring>(selectedKindItem.Tag(), hstring(L"mcp")).c_str());
            const auto index = static_cast<size_t>(recordSelector.SelectedIndex());

            removeButton.IsEnabled(false);
            saveButton.IsEnabled(false);
            winrt::apartment_context uiThread;

            if (kind == L"mcp") {
                statusText.Text(L"Removing the shared MCP lane through the local admin API.");
                co_await winrt::resume_background();
                const auto result = runtime_.RemoveMcpServer(customMcpServers[index].id);
                co_await uiThread;
                statusText.Text(winrt::hstring(result.message));
                if (!result.succeeded) {
                    removeButton.IsEnabled(true);
                    saveButton.IsEnabled(true);
                    GuidedWorkflowStatusText().Text(L"Runtime maintenance needs attention. Review the wizard message and try again.");
                    co_return;
                }
                GuidedWorkflowStatusText().Text(winrt::hstring(L"Removed shared MCP lane '" + customMcpServers[index].displayName + L"'."));
            } else if (kind == L"subagent") {
                statusText.Text(L"Removing the custom sub-agent through the local admin API.");
                co_await winrt::resume_background();
                const auto result = runtime_.RemoveSubAgent(customSubAgents[index].id);
                co_await uiThread;
                statusText.Text(winrt::hstring(result.message));
                if (!result.succeeded) {
                    removeButton.IsEnabled(true);
                    saveButton.IsEnabled(true);
                    GuidedWorkflowStatusText().Text(L"Runtime maintenance needs attention. Review the wizard message and try again.");
                    co_return;
                }
                GuidedWorkflowStatusText().Text(winrt::hstring(L"Removed custom sub-agent lane '" + customSubAgents[index].displayName + L"'."));
            } else {
                statusText.Text(L"Removing the Apple host through the local admin API.");
                co_await winrt::resume_background();
                const auto result = runtime_.RemoveAppleRemoteHost(appleHosts[index].hostId);
                co_await uiThread;
                statusText.Text(winrt::hstring(result.message));
                if (!result.succeeded) {
                    removeButton.IsEnabled(true);
                    saveButton.IsEnabled(true);
                    GuidedWorkflowStatusText().Text(L"Runtime maintenance needs attention. Review the wizard message and try again.");
                    co_return;
                }
                GuidedWorkflowStatusText().Text(winrt::hstring(L"Removed Apple host '" + appleHosts[index].displayName + L"'."));
            }

            const auto completionMessage = GuidedWorkflowStatusText().Text();
            dialog.Hide();
            co_await CompleteGuidedWorkflowAsync(completionMessage, kRuntimeDestination);
        }();
        (void)ignored;
    });

    refreshRecordSelector();
    populateCurrentSelection();

    co_await dialog.ShowAsync();
}

IAsyncAction MainWindow::OpenOverlayRouteAsync(std::wstring routeId) {
    // Operator feedback: dialog-based overlays with an "Open In Workspace"
    // button were confusing — users wanted to edit settings / imports /
    // exports in place, not through a modal route-then-navigate dance.
    // This handler now navigates directly to the destination and renders
    // its section control inside SectionContentHost, same as a toolbar click.
    const auto iterator = std::find_if(
        currentSnapshot_.overlayRoutes.begin(),
        currentSnapshot_.overlayRoutes.end(),
        [&routeId](const auto& route) { return route.id == routeId; });

    if (iterator == currentSnapshot_.overlayRoutes.end()) {
        UpdateStatusBar(L"Unable to resolve the requested Forsetti overlay route.", InfoBarSeverity::Warning);
        co_return;
    }

    std::wstring destinationId;
    if (iterator->targetsModuleView && !iterator->viewId.empty()) {
        destinationId = destinationForViewId(iterator->viewId);
    } else if (!iterator->destinationId.empty()) {
        destinationId = iterator->destinationId;
    }

    if (destinationId.empty()) {
        UpdateStatusBar(
            L"The selected route did not publish an in-place destination.",
            InfoBarSeverity::Warning);
        co_return;
    }

    SetCurrentDestination(destinationId);
    UpdateStatusBar(
        winrt::hstring(std::wstring(L"Navigated to ")
            + (iterator->label.empty() ? routeId : iterator->label)
            + L"."),
        InfoBarSeverity::Informational);
    co_return;
}

IAsyncAction MainWindow::CompleteGuidedWorkflowAsync(winrt::hstring const& message, std::wstring const& destinationId) {
    UpdateStatusBar(message, InfoBarSeverity::Success);
    SetCurrentDestination(destinationId);
    co_await RefreshAsync();

    std::wstring detail = std::wstring(message.c_str());
    const auto followThrough = guidedFollowThroughForDestination(destinationId);
    if (!followThrough.empty()) {
        detail += L"\n\n";
        detail += followThrough;
    }

    co_await ShowDialogAsync(L"Guided Workflow Complete", winrt::hstring(detail));
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
