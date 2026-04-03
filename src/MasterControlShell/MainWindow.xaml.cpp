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
        return { L"TELEMETRY", title, L"Use the dense monitoring deck to keep live host pressure, governed resource budgets, runtime activity, and environment discovery visible at a glance." };
    }
    if (destinationId == kRuntimeDestination) {
        return { L"RUNTIME", title, L"Inspect MCP runtime lanes, platform gateway inventory, Apple remote hosts, and the current operational map exposed by the service." };
    }
    if (destinationId == kCluDestination) {
        return { L"CLU", title, L"Inspect the Command Logic Unit governance profile, launch guided setup wizards, and manage Apple production operations plus operator-visible control rules." };
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

std::wstring guidedFollowThroughForDestination(const std::wstring& destinationId) {
    if (destinationId == kRuntimeDestination) {
        return L"Next:\n- Review the Runtime surface to confirm the lane or host details.\n- If this lane should own orchestration work, use Assign Responsibility next.";
    }
    if (destinationId == kProvidersDestination) {
        return L"Next:\n- Review the Providers surface to confirm routing or ownership.\n- Run Validate Provider Routing next if you want an operator-safe execution check.";
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
                              std::function<void()> refreshRequested,
                              std::function<void(const std::wstring&)> actionRequested) {
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
    if (viewId == kProvidersView) {
        const auto typed = view.as<winrt::MasterControlShell::ProvidersSectionControl>();
        winrt::get_self<winrt::MasterControlShell::implementation::ProvidersSectionControl>(typed)->AttachRuntime(
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
        winrt::get_self<winrt::MasterControlShell::implementation::SettingsSectionControl>(typed)->AttachActions(std::move(actionRequested));
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
    StartGuidedWorkflow(L"connect-model");
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

void MainWindow::GuidedProviderAssignmentWizardButton_Click(IInspectable const&, RoutedEventArgs const&) {
    StartGuidedWorkflow(L"assign-responsibility");
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

void MainWindow::GuidedProviderExecutionWizardButton_Click(IInspectable const&, RoutedEventArgs const&) {
    StartGuidedWorkflow(L"guided-provider-execution");
}

void MainWindow::GuidedRuntimeMaintenanceWizardButton_Click(IInspectable const&, RoutedEventArgs const&) {
    StartGuidedWorkflow(L"guided-runtime-maintenance");
}

void MainWindow::StartGuidedWorkflow(std::wstring const& workflowId) {
    IAsyncAction action{ nullptr };
    if (workflowId == L"connect-model") {
        action = ShowProviderWizardAsync();
    } else if (workflowId == L"new-mcp") {
        action = ShowMcpServerWizardAsync();
    } else if (workflowId == L"new-subagent") {
        action = ShowSubAgentWizardAsync();
    } else if (workflowId == L"new-subagent-group") {
        action = ShowSubAgentGroupWizardAsync();
    } else if (workflowId == L"new-apple-host") {
        action = ShowAppleHostWizardAsync();
    } else if (workflowId == L"assign-responsibility") {
        action = ShowProviderAssignmentWizardAsync();
    } else if (workflowId == L"manage-forsetti-modules") {
        action = ShowForsettiModuleWizardAsync();
    } else if (workflowId == L"guided-import") {
        action = ShowImportWizardAsync();
    } else if (workflowId == L"guided-security") {
        action = ShowSecurityWizardAsync();
    } else if (workflowId == L"guided-settings") {
        action = ShowSettingsWizardAsync();
    } else if (workflowId == L"guided-provider-execution") {
        action = ShowProviderExecutionWizardAsync();
    } else if (workflowId == L"guided-runtime-maintenance") {
        action = ShowRuntimeMaintenanceWizardAsync();
    } else {
        UpdateStatusBar(winrt::hstring(std::wstring(L"Unknown guided workflow request: ") + workflowId), InfoBarSeverity::Warning);
        return;
    }

    (void)action;
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
        setWindowSize(windowHandle_, 1560, 1024);
        centerWindow(windowHandle_);
    }
}

void MainWindow::ConfigureCustomTitleBar() {
    Window window = *this;

    try {
        window.ExtendsContentIntoTitleBar(true);
        window.SetTitleBar(TitleBarHost());
        writeShellLog(L"Configured TitleBarHost as the custom draggable title bar.");
    } catch (const winrt::hresult_error& error) {
        writeShellLog(L"Custom title bar configuration failed: " + std::wstring(error.message().c_str()));
        return;
    }

    TitleBarLeftInsetColumn().Width(GridLengthHelper::FromPixels(14));
    TitleBarRightInsetColumn().Width(GridLengthHelper::FromPixels(156));
    writeShellLog(L"Reserved caption-button space for the custom title bar drag surface.");
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
    for (const auto& target : currentSnapshot_.providerAssignmentTargets) {
        const auto normalizedKind = uppercase(target.kind);
        if (normalizedKind.find(L"SUB") == std::wstring::npos) {
            continue;
        }

        Microsoft::UI::Xaml::Controls::ListBoxItem item;
        std::wstring label = target.displayName.empty() ? target.targetId : target.displayName;
        if (!target.description.empty()) {
            label += L"  |  " + target.description;
        } else if (!target.kind.empty()) {
            label += L"  |  " + target.kind;
        }
        item.Content(box_value(winrt::hstring(label)));
        item.Tag(box_value(winrt::hstring(target.targetId)));
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
            co_await CompleteGuidedWorkflowAsync(completionMessage, kProvidersDestination);
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

IAsyncAction MainWindow::ShowProviderWizardAsync() {
    ContentDialog dialog;
    dialog.Title(box_value(L"Connect AI Model Wizard"));
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
    intro.Text(L"Step 1: choose the AI model connector. Step 2: confirm the route identity and defaults. Step 3: add credentials and optionally assign that model to a planning, coding, or specialist responsibility lane.");
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
        addLabel(L"Step 1. AI Model Connector");
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
        addLabel(L"Step 3. Assign this model to");
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
    statusText.Text(L"Connect one or more model routes here, then let CLU and provider assignments split planning, coding, review, and specialist work between them.");
    statusText.TextWrapping(TextWrapping::WrapWholeWords);
    root.Children().Append(statusText);

    Button createButton;
    createButton.Content(box_value(L"Connect AI Model"));
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
            statusText.Text(L"Connecting the AI model route through the local admin API.");

            winrt::apartment_context uiThread;
            co_await winrt::resume_background();
            const auto providerResult = runtime_.UpsertProvider(provider);
            if (!providerResult.succeeded) {
                co_await uiThread;
                statusText.Text(winrt::hstring(providerResult.message));
                createButton.IsEnabled(true);
                GuidedWorkflowStatusText().Text(L"AI model connection wizard needs attention. Review the wizard message and try again.");
                co_return;
            }

            if (!credentialValues.empty()) {
                const auto credentialsResult = runtime_.UpsertProviderCredentials(provider.id, credentialValues);
                if (!credentialsResult.succeeded) {
                    co_await uiThread;
                    statusText.Text(winrt::hstring(credentialsResult.message));
                    createButton.IsEnabled(true);
                    GuidedWorkflowStatusText().Text(L"AI model route was connected, but credential setup still needs attention.");
                    co_return;
                }
            }

            if (assignment.has_value()) {
                const auto assignmentResult = runtime_.UpsertProviderAssignment(*assignment);
                if (!assignmentResult.succeeded) {
                    co_await uiThread;
                    statusText.Text(winrt::hstring(assignmentResult.message));
                    createButton.IsEnabled(true);
                    GuidedWorkflowStatusText().Text(L"AI model route was connected, but responsibility assignment still needs attention.");
                    co_return;
                }
            }
            co_await uiThread;

            GuidedWorkflowStatusText().Text(winrt::hstring(L"Connected AI model route '" + displayName + L"'."));
            const auto completionMessage = GuidedWorkflowStatusText().Text();
            dialog.Hide();
            co_await CompleteGuidedWorkflowAsync(completionMessage, kProvidersDestination);
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

IAsyncAction MainWindow::ShowProviderAssignmentWizardAsync() {
    ContentDialog dialog;
    dialog.Title(box_value(L"Assign Responsibility Wizard"));
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
    intro.Text(L"Step 1: choose the connected AI model. Step 2: choose the planning, coding, review, or specialist lane that should own it. Step 3: save the responsibility mapping so CLU and provider execution agree on routing.");
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
        addLabel(L"Step 1. Connected AI Model");
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
        addLabel(L"Step 2. Responsibility Lane");
        return targetSelector;
    }());

    TextBlock summaryText;
    summaryText.Style(Application::Current().Resources().Lookup(box_value(L"ShellDataTextStyle")).try_as<Style>());
    summaryText.TextWrapping(TextWrapping::WrapWholeWords);
    root.Children().Append(summaryText);

    TextBlock statusText;
    statusText.Style(Application::Current().Resources().Lookup(box_value(L"ShellDataTextStyle")).try_as<Style>());
    statusText.Text(L"Use this wizard when you want ChatGPT on planning, Claude Code on coding, or any other model-to-responsibility split across CLU lanes.");
    statusText.TextWrapping(TextWrapping::WrapWholeWords);
    root.Children().Append(statusText);

    Button createButton;
    createButton.Content(box_value(L"Assign Responsibility"));
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
        std::wstring summary = L"This will assign AI model ";
        summary += provider.displayName.empty() ? provider.id : provider.displayName;
        summary += L" to responsibility lane ";
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
            statusText.Text(L"Saving model responsibility through the local admin API.");

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
                GuidedWorkflowStatusText().Text(L"Responsibility assignment wizard needs attention. Review the message inside the wizard and try again.");
                co_return;
            }

            GuidedWorkflowStatusText().Text(winrt::hstring(L"Assigned AI model '" +
                (provider.displayName.empty() ? provider.id : provider.displayName) +
                L"' to " +
                (target.displayName.empty() ? target.targetId : target.displayName) + L"."));
            const auto completionMessage = GuidedWorkflowStatusText().Text();
            dialog.Hide();
            co_await CompleteGuidedWorkflowAsync(completionMessage, kProvidersDestination);
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

    addLabel(L"Step 3. Governed Resource Envelope");
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

IAsyncAction MainWindow::ShowProviderExecutionWizardAsync() {
    ContentDialog dialog;
    dialog.Title(box_value(L"Guided Provider Routing Validation"));
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
    intro.Text(L"Step 1: choose the orchestration lane you want to validate. Step 2: choose the kind of routing check you want the assigned provider to perform. Step 3: dispatch the guided validation through the local admin API and review the result in provider execution history.");
    intro.TextWrapping(TextWrapping::WrapWholeWords);
    root.Children().Append(intro);

    const auto addLabel = [&root](std::wstring const& text) {
        TextBlock label;
        label.Style(Application::Current().Resources().Lookup(box_value(L"ShellLabelTextStyle")).try_as<Style>());
        label.Text(winrt::hstring(text));
        root.Children().Append(label);
    };

    ComboBox targetSelector;
    targetSelector.PlaceholderText(L"Choose a role or sub-agent lane");
    for (const auto& target : currentSnapshot_.providerAssignmentTargets) {
        ComboBoxItem item;
        std::wstring label = target.displayName.empty() ? target.targetId : target.displayName;
        if (!target.kind.empty()) {
            label += L"  |  ";
            label += target.kind;
        }
        item.Content(box_value(winrt::hstring(label)));
        targetSelector.Items().Append(item);
    }
    if (targetSelector.Items().Size() > 0) {
        targetSelector.SelectedIndex(0);
    }
    root.Children().Append([&]() {
        addLabel(L"Step 1. Validation Target");
        return targetSelector;
    }());

    const std::vector<std::pair<std::wstring, std::wstring>> promptTemplates{
        {L"Planning Check", L"Create a concise plan for this orchestration lane and explain the first action you would take."},
        {L"Coding Check", L"Describe how you would implement the next coding task for this lane and note any MCP tools you would need."},
        {L"Review Check", L"Review the current state of this lane, identify the highest-risk issue, and recommend the next operator action."},
        {L"Specialist Coordination", L"Summarize how this lane would coordinate with related specialists and what inputs it needs before execution."},
        {L"Custom Prompt", L""}
    };

    ComboBox templateSelector;
    templateSelector.PlaceholderText(L"Choose a validation pattern");
    for (const auto& templateEntry : promptTemplates) {
        ComboBoxItem item;
        item.Content(box_value(winrt::hstring(templateEntry.first)));
        templateSelector.Items().Append(item);
    }
    if (templateSelector.Items().Size() > 0) {
        templateSelector.SelectedIndex(0);
    }
    root.Children().Append([&]() {
        addLabel(L"Step 2. Validation Pattern");
        return templateSelector;
    }());

    Grid optionGrid;
    optionGrid.ColumnSpacing(12);
    optionGrid.ColumnDefinitions().Append(ColumnDefinition());
    optionGrid.ColumnDefinitions().Append(ColumnDefinition());

    ToggleSwitch toolAccessToggle;
    toolAccessToggle.Header(box_value(L"Allow shared MCP access"));
    toolAccessToggle.IsOn(true);

    StackPanel maxTurnsPanel;
    maxTurnsPanel.Spacing(6);
    TextBlock maxTurnsLabel;
    maxTurnsLabel.Style(Application::Current().Resources().Lookup(box_value(L"ShellLabelTextStyle")).try_as<Style>());
    maxTurnsLabel.Text(L"Max Turns");
    TextBox maxTurnsBox;
    maxTurnsBox.Text(L"4");
    maxTurnsPanel.Children().Append(maxTurnsLabel);
    maxTurnsPanel.Children().Append(maxTurnsBox);

    Grid::SetColumn(toolAccessToggle, 0);
    Grid::SetColumn(maxTurnsPanel, 1);
    optionGrid.Children().Append(toolAccessToggle);
    optionGrid.Children().Append(maxTurnsPanel);
    root.Children().Append(optionGrid);

    TextBox promptBox;
    promptBox.AcceptsReturn(true);
    promptBox.Height(140);
    promptBox.TextWrapping(TextWrapping::WrapWholeWords);
    promptBox.PlaceholderText(L"Ask the assigned provider to work within its orchestration lane.");
    promptBox.Text(winrt::hstring(promptTemplates.front().second));
    root.Children().Append([&]() {
        addLabel(L"Step 3. Prompt");
        return promptBox;
    }());

    TextBlock summaryText;
    summaryText.Style(Application::Current().Resources().Lookup(box_value(L"ShellDataTextStyle")).try_as<Style>());
    summaryText.TextWrapping(TextWrapping::WrapWholeWords);
    root.Children().Append(summaryText);

    TextBlock statusText;
    statusText.Style(Application::Current().Resources().Lookup(box_value(L"ShellDataTextStyle")).try_as<Style>());
    statusText.TextWrapping(TextWrapping::WrapWholeWords);
    statusText.Text(currentSnapshot_.providerAssignmentTargets.empty()
        ? L"No provider ownership lanes are published yet. Connect a model and assign responsibility before using this wizard."
        : L"Use this wizard when you want to validate routing, ownership, credentials, and MCP access without manually filling the provider execution console.");
    root.Children().Append(statusText);

    Button runButton;
    runButton.Content(box_value(L"Run Guided Validation"));
    runButton.Style(Application::Current().Resources().Lookup(box_value(L"ShellCommandButtonStyle")).try_as<Style>());
    runButton.IsEnabled(!currentSnapshot_.providerAssignmentTargets.empty());
    root.Children().Append(runButton);

    const auto updatePromptFromTemplate = [templateSelector, promptBox, promptTemplates]() {
        const auto index = templateSelector.SelectedIndex();
        if (index < 0 || index >= static_cast<int>(promptTemplates.size())) {
            return;
        }
        const auto& templatePrompt = promptTemplates[static_cast<size_t>(index)].second;
        if (!templatePrompt.empty()) {
            promptBox.Text(winrt::hstring(templatePrompt));
        }
    };

    const auto updateSummary = [&, this]() {
        std::wstring targetLabel = L"No target selected";
        if (const auto index = targetSelector.SelectedIndex();
            index >= 0 && index < static_cast<int>(currentSnapshot_.providerAssignmentTargets.size())) {
            const auto& target = currentSnapshot_.providerAssignmentTargets[static_cast<size_t>(index)];
            targetLabel = target.displayName.empty() ? target.targetId : target.displayName;
            if (!target.kind.empty()) {
                targetLabel += L"  |  ";
                targetLabel += target.kind;
            }
        }

        std::wstring templateLabel = L"Custom Prompt";
        if (const auto index = templateSelector.SelectedIndex();
            index >= 0 && index < static_cast<int>(promptTemplates.size())) {
            templateLabel = promptTemplates[static_cast<size_t>(index)].first;
        }

        std::wstring summary = L"Validate ";
        summary += targetLabel;
        summary += L" using ";
        summary += templateLabel;
        summary += L" | Shared MCP ";
        summary += toolAccessToggle.IsOn() ? L"enabled" : L"disabled";
        summary += L" | Max turns ";
        summary += trimCopy(std::wstring(maxTurnsBox.Text().c_str())).empty()
            ? L"4"
            : trimCopy(std::wstring(maxTurnsBox.Text().c_str()));
        summaryText.Text(winrt::hstring(summary));
    };

    templateSelector.SelectionChanged([&](IInspectable const&, Controls::SelectionChangedEventArgs const&) {
        updatePromptFromTemplate();
        updateSummary();
    });
    targetSelector.SelectionChanged([&](IInspectable const&, Controls::SelectionChangedEventArgs const&) { updateSummary(); });
    toolAccessToggle.Toggled([&](IInspectable const&, RoutedEventArgs const&) { updateSummary(); });
    maxTurnsBox.TextChanged([&](IInspectable const&, Controls::TextChangedEventArgs const&) { updateSummary(); });
    promptBox.TextChanged([&](IInspectable const&, Controls::TextChangedEventArgs const&) { updateSummary(); });
    updateSummary();

    scrollViewer.Content(root);
    dialog.Content(scrollViewer);

    runButton.Click([this, dialog, runButton, statusText, targetSelector, toolAccessToggle, maxTurnsBox, promptBox](IInspectable const&, RoutedEventArgs const&) {
        auto ignored = [this, dialog, runButton, statusText, targetSelector, toolAccessToggle, maxTurnsBox, promptBox]() -> IAsyncAction {
            if (targetSelector.SelectedIndex() < 0 ||
                targetSelector.SelectedIndex() >= static_cast<int>(currentSnapshot_.providerAssignmentTargets.size())) {
                statusText.Text(L"Choose a role or sub-agent lane before running guided validation.");
                co_return;
            }

            const auto maxTurns = parseInteger(std::wstring(maxTurnsBox.Text().c_str()), 1, 12);
            if (!maxTurns.has_value()) {
                statusText.Text(L"Max turns must be between 1 and 12.");
                co_return;
            }

            const auto prompt = trimCopy(std::wstring(promptBox.Text().c_str()));
            if (prompt.empty()) {
                statusText.Text(L"Enter a prompt before running guided validation.");
                co_return;
            }

            const auto& target = currentSnapshot_.providerAssignmentTargets[static_cast<size_t>(targetSelector.SelectedIndex())];
            runButton.IsEnabled(false);
            statusText.Text(L"Dispatching guided provider validation through the local admin API.");

            winrt::apartment_context uiThread;
            co_await winrt::resume_background();
            const auto record = runtime_.ExecuteProviderTask(::MasterControlShell::ShellProviderExecutionRequest{
                target.targetId,
                prompt,
                toolAccessToggle.IsOn(),
                *maxTurns
            });
            co_await uiThread;

            const auto output = !record.outputText.empty() ? record.outputText : record.rawResponse;
            if (!record.errorMessage.empty() || record.status == L"failed") {
                statusText.Text(winrt::hstring(
                    record.errorMessage.empty() ? L"Provider validation failed. Review execution history for details." : record.errorMessage));
                runButton.IsEnabled(true);
                GuidedWorkflowStatusText().Text(L"Provider routing validation needs attention. Review the wizard message and try again.");
                co_return;
            }

            statusText.Text(winrt::hstring(output.empty() ? L"Provider routing validation completed." : output));
            GuidedWorkflowStatusText().Text(L"Completed guided provider routing validation and refreshed provider history.");
            const auto completionMessage = GuidedWorkflowStatusText().Text();
            dialog.Hide();
            co_await CompleteGuidedWorkflowAsync(completionMessage, kProvidersDestination);
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
