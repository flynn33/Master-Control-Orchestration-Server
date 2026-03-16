// Master Control Program
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#include "pch.h"

#include "MainWindow.xaml.h"

#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include "ExportsSectionControl.xaml.h"
#include "ImportsSectionControl.xaml.h"
#include "microsoft.ui.xaml.window.h"
#include "OverviewSectionControl.xaml.h"
#include "ProvidersSectionControl.xaml.h"
#include "RuntimeSectionControl.xaml.h"
#include "SecuritySectionControl.xaml.h"
#include "SettingsSectionControl.xaml.h"
#include "ShellFormatting.h"
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

void setVisible(const UIElement& element, const bool visible) {
    element.Visibility(visible ? Visibility::Visible : Visibility::Collapsed);
}

struct SectionMetadata final {
    std::wstring eyebrow;
    std::wstring title;
    std::wstring description;
};

SectionMetadata metadataForSection(const ShellSection section) {
    switch (section) {
        case ShellSection::Overview:
            return {
                L"OVERVIEW",
                L"Command Deck",
                L"Move across the desktop control plane without losing access to core service operations and shell health."
            };
        case ShellSection::Telemetry:
            return {
                L"TELEMETRY",
                L"Host Signal Matrix",
                L"Track live CPU, memory, disk, network, and environment discovery details from the local service snapshot."
            };
        case ShellSection::Runtime:
            return {
                L"RUNTIME",
                L"Endpoint Orchestration",
                L"Inspect MCP runtime lanes, gateway-facing inventory, and the current operational map exposed by the service."
            };
        case ShellSection::Providers:
            return {
                L"PROVIDERS",
                L"AI Routing Surface",
                L"Review provider adapters, autonomous control posture, and the current agent service envelope."
            };
        case ShellSection::Imports:
            return {
                L"IMPORTS",
                L"Onboarding Ledger",
                L"Audit installer provenance, trusted-source flows, and the current software onboarding trail."
            };
        case ShellSection::Exports:
            return {
                L"EXPORTS",
                L"Agent Distribution",
                L"Review exported agent artifacts, browser handoff endpoints, and downstream integration material."
            };
        case ShellSection::Security:
            return {
                L"SECURITY",
                L"Protection Envelope",
                L"Inspect bind policy, browser access posture, beacon state, AI autonomy, and operator-sensitive toggles."
            };
        case ShellSection::Settings:
            return {
                L"SETTINGS",
                L"Host Configuration",
                L"Trace configuration files, data paths, and the current resource envelope that shapes the host runtime."
            };
        default:
            return {
                L"OVERVIEW",
                L"Command Deck",
                L"Move across the desktop control plane without losing access to core service operations and shell health."
            };
    }
}

ShellSection sectionFromTag(const winrt::hstring& tag) {
    const std::wstring value = tag.c_str();
    if (value == L"telemetry") {
        return ShellSection::Telemetry;
    }
    if (value == L"runtime") {
        return ShellSection::Runtime;
    }
    if (value == L"providers") {
        return ShellSection::Providers;
    }
    if (value == L"imports") {
        return ShellSection::Imports;
    }
    if (value == L"exports") {
        return ShellSection::Exports;
    }
    if (value == L"security") {
        return ShellSection::Security;
    }
    if (value == L"settings") {
        return ShellSection::Settings;
    }
    return ShellSection::Overview;
}

void syncNavigationItem(const NavigationViewItem& item,
                        const std::vector<::MasterControlShell::ShellNavigationPointer>& pointers,
                        const wchar_t* destinationId,
                        const wchar_t* fallbackLabel) {
    const auto iterator = std::find_if(
        pointers.begin(),
        pointers.end(),
        [destinationId](const auto& pointer) { return pointer.destinationId == destinationId; });

    item.Tag(box_value(hstring(destinationId)));
    item.Content(box_value(hstring(iterator != pointers.end() && !iterator->label.empty() ? iterator->label : fallbackLabel)));
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

        writeShellLog(L"RootGrid_Loaded: AttachInteractiveSections starting.");
        AttachInteractiveSections();
        writeShellLog(L"RootGrid_Loaded: AttachInteractiveSections finished.");

        writeShellLog(L"RootGrid_Loaded: SetCurrentSection starting.");
        ShellNavigation().SelectedItem(OverviewNavItem());
        SetCurrentSection(ShellSection::Overview);
        writeShellLog(L"RootGrid_Loaded: SetCurrentSection finished.");

        writeShellLog(L"RootGrid_Loaded: UpdateStatusBar starting.");
        UpdateStatusBar(L"Loading local service and dashboard state.", InfoBarSeverity::Informational);
        writeShellLog(L"RootGrid_Loaded: UpdateStatusBar finished.");

        writeShellLog(L"RootGrid_Loaded: RefreshAsync starting.");
        RefreshAsync();
        writeShellLog(L"RootGrid_Loaded: RefreshAsync dispatched.");
    } catch (const winrt::hresult_error& error) {
        writeShellLog(L"RootGrid_Loaded caught HRESULT failure: " + std::wstring(error.message().c_str()));
    }
}

void MainWindow::AttachInteractiveSections() {
    const auto weakThis = get_weak();
    const auto refreshRequested = [weakThis]() {
        if (const auto self = weakThis.get()) {
            self->RefreshAsync();
        }
    };

    winrt::get_self<winrt::MasterControlShell::implementation::ProvidersSectionControl>(ProvidersSection())
        ->AttachRuntime(&runtime_, refreshRequested);
    winrt::get_self<winrt::MasterControlShell::implementation::ImportsSectionControl>(ImportsSection())
        ->AttachRuntime(&runtime_, refreshRequested);
    winrt::get_self<winrt::MasterControlShell::implementation::ExportsSectionControl>(ExportsSection())
        ->AttachRuntime(&runtime_);
    winrt::get_self<winrt::MasterControlShell::implementation::SecuritySectionControl>(SecuritySection())
        ->AttachRuntime(&runtime_, refreshRequested);
}

void MainWindow::ShellNavigation_SelectionChanged(
    IInspectable const&,
    NavigationViewSelectionChangedEventArgs const& args) {
    if (const auto selectedItem = args.SelectedItem().try_as<NavigationViewItem>()) {
        SetCurrentSection(sectionFromTag(unbox_value_or<hstring>(selectedItem.Tag(), L"overview")));
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
        window.ExtendsContentIntoTitleBar(true);
        window.SetTitleBar(TitleBarHost());
        writeShellLog(L"Custom title bar enabled.");
    } catch (const winrt::hresult_error& error) {
        writeShellLog(L"Custom title bar unavailable: " + std::wstring(error.message().c_str()));
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

void MainWindow::SetCurrentSection(const ShellSection section) {
    currentSection_ = section;
    const auto metadata = metadataForSection(section);
    CurrentViewEyebrowText().Text(winrt::hstring(metadata.eyebrow));
    CurrentViewTitleText().Text(winrt::hstring(metadata.title));
    CurrentViewDescriptionText().Text(winrt::hstring(metadata.description));

    setVisible(OverviewSection(), section == ShellSection::Overview);
    setVisible(TelemetrySection(), section == ShellSection::Telemetry);
    setVisible(RuntimeSection(), section == ShellSection::Runtime);
    setVisible(ProvidersSection(), section == ShellSection::Providers);
    setVisible(ImportsSection(), section == ShellSection::Imports);
    setVisible(ExportsSection(), section == ShellSection::Exports);
    setVisible(SecuritySection(), section == ShellSection::Security);
    setVisible(SettingsSection(), section == ShellSection::Settings);
}

void MainWindow::ApplySnapshot(const ::MasterControlShell::ShellSnapshot& snapshot) {
    syncNavigationItem(OverviewNavItem(), snapshot.navigationPointers, L"overview", L"Overview");
    syncNavigationItem(TelemetryNavItem(), snapshot.navigationPointers, L"telemetry", L"Telemetry");
    syncNavigationItem(RuntimeNavItem(), snapshot.navigationPointers, L"runtime", L"Runtime");
    syncNavigationItem(ProvidersNavItem(), snapshot.navigationPointers, L"providers", L"Providers");
    syncNavigationItem(ImportsNavItem(), snapshot.navigationPointers, L"imports", L"Imports");
    syncNavigationItem(ExportsNavItem(), snapshot.navigationPointers, L"exports", L"Exports");
    syncNavigationItem(SecurityNavItem(), snapshot.navigationPointers, L"security", L"Security");
    syncNavigationItem(SettingsNavItem(), snapshot.navigationPointers, L"settings", L"Settings");

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

    winrt::get_self<winrt::MasterControlShell::implementation::OverviewSectionControl>(OverviewSection())->ApplySnapshot(snapshot);
    winrt::get_self<winrt::MasterControlShell::implementation::TelemetrySectionControl>(TelemetrySection())->ApplySnapshot(snapshot);
    winrt::get_self<winrt::MasterControlShell::implementation::RuntimeSectionControl>(RuntimeSection())->ApplySnapshot(snapshot);
    winrt::get_self<winrt::MasterControlShell::implementation::ProvidersSectionControl>(ProvidersSection())->ApplySnapshot(snapshot);
    winrt::get_self<winrt::MasterControlShell::implementation::ImportsSectionControl>(ImportsSection())->ApplySnapshot(snapshot);
    winrt::get_self<winrt::MasterControlShell::implementation::ExportsSectionControl>(ExportsSection())->ApplySnapshot(snapshot);
    winrt::get_self<winrt::MasterControlShell::implementation::SecuritySectionControl>(SecuritySection())->ApplySnapshot(snapshot);
    winrt::get_self<winrt::MasterControlShell::implementation::SettingsSectionControl>(SettingsSection())->ApplySnapshot(snapshot);

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

IAsyncAction MainWindow::ShowDialogAsync(winrt::hstring const& title, winrt::hstring const& message) {
    ContentDialog dialog;
    dialog.Title(box_value(title));
    dialog.Content(box_value(message));
    dialog.CloseButtonText(L"Close");
    dialog.XamlRoot(RootGrid().XamlRoot());
    co_await dialog.ShowAsync();
}

} // namespace winrt::MasterControlShell::implementation
