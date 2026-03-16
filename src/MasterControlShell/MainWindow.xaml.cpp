// Master Control Program
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#include "pch.h"

#include "MainWindow.xaml.h"

#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include "microsoft.ui.xaml.window.h"
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

std::wstring boolLabel(const bool value) {
    return value ? L"Enabled" : L"Disabled";
}

std::wstring formatPercent(const double value) {
    std::wostringstream stream;
    stream.setf(std::ios::fixed);
    stream.precision(1);
    stream << value << L'%';
    return stream.str();
}

std::wstring formatTraffic(const uint64_t txBytesPerSecond, const uint64_t rxBytesPerSecond) {
    std::wostringstream stream;
    stream << L"TX " << txBytesPerSecond << L" B/s  |  RX " << rxBytesPerSecond << L" B/s";
    return stream.str();
}

std::wstring formatResourceEnvelope(const ::MasterControlShell::ShellSnapshot& snapshot) {
    std::wostringstream stream;
    stream << L"CPU " << snapshot.cpuAllocationPercent
           << L"%  |  RAM " << snapshot.memoryAllocationPercent
           << L"%  |  Bandwidth " << snapshot.bandwidthAllocationPercent
           << L"%  |  Storage " << snapshot.storageAllocationPercent << L'%';
    return stream.str();
}

void populateListView(const ListView& listView, const std::vector<std::wstring>& rows) {
    listView.Items().Clear();
    for (const auto& row : rows) {
        listView.Items().Append(box_value(row));
    }
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
        setWindowSize(windowHandle_, 1480, 1024);
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

void MainWindow::ApplySnapshot(const ::MasterControlShell::ShellSnapshot& snapshot) {
    OverviewTextBlock().Text(winrt::hstring(snapshot.overviewText));
    ServiceStateText().Text(winrt::hstring(serviceStateLabel(snapshot.serviceState)));
    ApiStateText().Text(snapshot.apiHealthy ? L"Reachable" : L"Offline");
    EndpointCountText().Text(winrt::hstring(std::to_wstring(snapshot.endpointRows.size())));
    ProviderCountText().Text(winrt::hstring(std::to_wstring(snapshot.providerRows.size())));
    PulseStateText().Text(winrt::hstring(uppercase(serviceStateLabel(snapshot.serviceState))));

    std::wstring pulseCaption = snapshot.apiHealthy ? L"Admin API online" : L"Admin API offline";
    pulseCaption += L"  |  ";
    pulseCaption += std::to_wstring(snapshot.endpointRows.size());
    pulseCaption += L" endpoints";
    PulseCaptionText().Text(winrt::hstring(pulseCaption));

    CpuProgressBar().Value(snapshot.cpuPercent);
    MemoryProgressBar().Value(snapshot.memoryPercent);
    DiskProgressBar().Value(snapshot.diskPercent);
    CpuValueText().Text(winrt::hstring(formatPercent(snapshot.cpuPercent)));
    MemoryValueText().Text(winrt::hstring(formatPercent(snapshot.memoryPercent)));
    DiskValueText().Text(winrt::hstring(formatPercent(snapshot.diskPercent)));
    TrafficValueText().Text(winrt::hstring(formatTraffic(snapshot.bytesSentPerSecond, snapshot.bytesReceivedPerSecond)));
    TelemetrySummaryText().Text(winrt::hstring(snapshot.telemetryText));

    EnvironmentNameText().Text(winrt::hstring(snapshot.environmentName));
    HostNameText().Text(winrt::hstring(snapshot.hostName));
    OperatingSystemText().Text(winrt::hstring(snapshot.operatingSystem));
    PrimaryIpText().Text(winrt::hstring(snapshot.primaryIpAddress));
    PrimaryMacText().Text(winrt::hstring(snapshot.primaryMacAddress));

    BindAddressText().Text(winrt::hstring(snapshot.bindAddress));
    BrowserPortText().Text(winrt::hstring(std::to_wstring(snapshot.browserPort)));
    BeaconEnabledText().Text(winrt::hstring(boolLabel(snapshot.beaconEnabled)));
    AiAutonomyText().Text(winrt::hstring(boolLabel(snapshot.aiAutonomyEnabled)));
    SecurityProtocolsText().Text(winrt::hstring(boolLabel(snapshot.securityProtocolsEnabled)));
    OpenLanAccessText().Text(winrt::hstring(boolLabel(snapshot.openLanAccess)));
    ResourceEnvelopeText().Text(winrt::hstring(formatResourceEnvelope(snapshot)));
    DashboardUrlText().Text(winrt::hstring(snapshot.dashboardUrl));
    ConfigPathText().Text(winrt::hstring(snapshot.configPath));
    DataDirectoryText().Text(winrt::hstring(snapshot.dataDirectory));
    EnvironmentNarrativeText().Text(winrt::hstring(snapshot.environmentText));
    ConfigurationNarrativeText().Text(winrt::hstring(snapshot.configurationText));

    populateListView(EndpointsListView(), snapshot.endpointRows);
    populateListView(ProvidersListView(), snapshot.providerRows);
    populateListView(InstallHistoryListView(), snapshot.installRows);
    populateListView(ExportsListView(), snapshot.exportRows);

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
