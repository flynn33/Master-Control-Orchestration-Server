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

constexpr wchar_t kOverviewDestination[] = L"overview";
constexpr wchar_t kTelemetryDestination[] = L"telemetry";
constexpr wchar_t kRuntimeDestination[] = L"runtime";
constexpr wchar_t kProvidersDestination[] = L"providers";
constexpr wchar_t kImportsDestination[] = L"imports";
constexpr wchar_t kExportsDestination[] = L"exports";
constexpr wchar_t kSecurityDestination[] = L"security";
constexpr wchar_t kSettingsDestination[] = L"settings";

constexpr wchar_t kOverviewView[] = L"OverviewSectionView";
constexpr wchar_t kTelemetryView[] = L"TelemetrySectionView";
constexpr wchar_t kRuntimeView[] = L"RuntimeSectionView";
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
        return { L"RUNTIME", title, L"Inspect MCP runtime lanes, gateway-facing inventory, and the current operational map exposed by the service." };
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

    if (viewId == kProvidersView || viewId == kImportsView || viewId == kExportsView || viewId == kSecurityView) {
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
