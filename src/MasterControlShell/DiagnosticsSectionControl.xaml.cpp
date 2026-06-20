// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#include "pch.h"

#include "DiagnosticsSectionControl.xaml.h"

#if __has_include("DiagnosticsSectionControl.g.cpp")
#include "DiagnosticsSectionControl.g.cpp"
#endif

// v0.11.0-alpha.3 (PHASE-14 Slice C): the export buttons reuse the
// supervisor wizard's native save UX from OverviewSectionControl --
// Win32 IFileSaveDialog (ShObjIdl.h) + ComPtr (wrl/client.h) +
// KnownFolders for the Documents default folder. IFileSaveDialog
// works without WinUI 3 IInitializeWithWindow plumbing, which is why
// the repo standardized on it over Windows.Storage.Pickers.
#include <algorithm>
#include <KnownFolders.h>
#include <ShlObj.h>
#include <ShObjIdl.h>
#include <wrl/client.h>

namespace winrt::MasterControlShell::implementation {

using namespace Microsoft::UI::Xaml::Controls;

namespace {

// Tron palette severity tones. Same colors as OverviewSectionControl's
// statusColor helper (v0.8.7) so diagnostics rows match the Error
// Reporting card and Telemetry status dots: good / warn / crit /
// neutral. Severity ladder maps debug+info -> neutral/good, warning ->
// warn, error+critical -> crit.
winrt::Windows::UI::Color severityColor(const std::wstring& severity) {
    using winrt::Windows::UI::ColorHelper;
    if (severity == L"critical") return ColorHelper::FromArgb(0xFF, 0xff, 0x6a, 0x80);
    if (severity == L"error")    return ColorHelper::FromArgb(0xFF, 0xff, 0x6a, 0x80);
    if (severity == L"warning" || severity == L"warn") {
        return ColorHelper::FromArgb(0xFF, 0xff, 0xc8, 0x57);
    }
    if (severity == L"info")     return ColorHelper::FromArgb(0xFF, 0x1c, 0xf2, 0xc1);
    // debug + anything unrecognized.
    return ColorHelper::FromArgb(0xFF, 0x8c, 0xb7, 0xc4);
}

// v0.11.0-alpha.3 (PHASE-14 Slice C): native save dialog for the
// diagnostics exports. Adapted from showSupervisorSaveDialog in
// OverviewSectionControl.xaml.cpp (v0.9.76) -- Win32 IFileSaveDialog,
// parent left null so the dialog presents itself top-most relative to
// the active foreground window, COM apartment satisfied by WinRT's MTA.
bool showDiagnosticsSaveDialog(const bool asMarkdown,
                               const std::wstring& suggestedFileName,
                               std::wstring& outChosenPath) {
    ::Microsoft::WRL::ComPtr<IFileSaveDialog> dialog;
    HRESULT hr = ::CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_ALL,
                                     IID_PPV_ARGS(&dialog));
    if (FAILED(hr) || !dialog) return false;

    COMDLG_FILTERSPEC markdownFilters[2] = {
        { L"Markdown document", L"*.md" },
        { L"All files",         L"*.*" }
    };
    COMDLG_FILTERSPEC jsonFilters[2] = {
        { L"JSON document", L"*.json" },
        { L"All files",     L"*.*" }
    };
    if (asMarkdown) {
        dialog->SetFileTypes(static_cast<UINT>(std::size(markdownFilters)), markdownFilters);
        dialog->SetDefaultExtension(L"md");
    } else {
        dialog->SetFileTypes(static_cast<UINT>(std::size(jsonFilters)), jsonFilters);
        dialog->SetDefaultExtension(L"json");
    }
    dialog->SetFileTypeIndex(1);
    dialog->SetTitle(L"Save Diagnostics Export");
    dialog->SetFileName(suggestedFileName.c_str());

    // Default to %USERPROFILE%\Documents\MCOS\DiagnosticsExports if it
    // can be created; otherwise the shell parks the dialog wherever it
    // decides (typically Documents). Same fallback contract as the
    // supervisor config save dialog.
    PWSTR documentsPath = nullptr;
    if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &documentsPath))
        && documentsPath != nullptr) {
        std::wstring suggestedFolder = std::wstring(documentsPath) + L"\\MCOS\\DiagnosticsExports";
        ::CoTaskMemFree(documentsPath);
        ::SHCreateDirectoryExW(nullptr, suggestedFolder.c_str(), nullptr);
        ::Microsoft::WRL::ComPtr<IShellItem> defaultFolder;
        if (SUCCEEDED(::SHCreateItemFromParsingName(suggestedFolder.c_str(),
                                                    nullptr,
                                                    IID_PPV_ARGS(&defaultFolder)))
            && defaultFolder) {
            dialog->SetDefaultFolder(defaultFolder.Get());
        }
    }

    hr = dialog->Show(nullptr);
    if (FAILED(hr)) return false;
    ::Microsoft::WRL::ComPtr<IShellItem> result;
    if (FAILED(dialog->GetResult(&result)) || !result) return false;
    PWSTR chosenPath = nullptr;
    if (FAILED(result->GetDisplayName(SIGDN_FILESYSPATH, &chosenPath)) || !chosenPath) {
        return false;
    }
    outChosenPath = chosenPath;
    ::CoTaskMemFree(chosenPath);
    return true;
}

// Write the export bytes verbatim. The /api/diagnostics/export route
// already returns finished UTF-8 (Markdown or JSON, no BOM), so unlike
// writeUtf8FileNoBom in OverviewSectionControl there is no wide ->
// narrow conversion step here -- re-encoding would risk corrupting the
// document the operator attaches to a support thread.
bool writeBytesToFile(const std::wstring& path, const std::string& bytes) {
    if (bytes.empty()) return false;
    HANDLE handle = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const BOOL ok = ::WriteFile(handle, bytes.data(),
                                  static_cast<DWORD>(bytes.size()),
                                  &written, nullptr);
    ::CloseHandle(handle);
    return ok && written == bytes.size();
}

// mcos-diagnostics-<timestamp>.md / .json -- same GetLocalTime +
// swprintf_s safe-filename pattern as ExportErrorsButton_Click in
// OverviewSectionControl (v0.8.7).
std::wstring buildDiagnosticsExportFileName(const bool asMarkdown) {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t fileName[128];
    swprintf_s(fileName, L"mcos-diagnostics-%04u%02u%02uT%02u%02u%02u.%s",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
               asMarkdown ? L"md" : L"json");
    return fileName;
}

} // namespace

DiagnosticsSectionControl::DiagnosticsSectionControl() {
    InitializeComponent();
}

void DiagnosticsSectionControl::AttachRuntime(::MasterControlShell::ShellRuntime* runtime) {
    runtime_ = runtime;
    UpdateActionState();
    if (runtime_ != nullptr) {
        RefreshDiagnosticsAsync();
    }
}

void DiagnosticsSectionControl::ApplySnapshot(const ::MasterControlShell::ShellSnapshot&) {
    // Diagnostics is query-driven rather than snapshot-driven: the
    // central aggregate can hold thousands of records, so re-fetching
    // on every live tick (the cadence ApplySnapshot rides) would
    // hammer the admin API for data the operator has not asked to
    // refresh. The snapshot pass only backfills the first load in case
    // AttachRuntime raced the runtime becoming reachable.
    if (runtime_ != nullptr && !loadInFlight_ && !hasLoadedOnce_) {
        RefreshDiagnosticsAsync();
    }
    UpdateActionState();
}

void DiagnosticsSectionControl::SeverityFilterSelector_SelectionChanged(
    Windows::Foundation::IInspectable const&,
    SelectionChangedEventArgs const&) {
    // Fires during InitializeComponent for the XAML SelectedIndex="0"
    // default; RefreshDiagnosticsAsync early-outs while runtime_ is
    // still null so the startup fire is harmless.
    RefreshDiagnosticsAsync();
}

void DiagnosticsSectionControl::SourceFilterSelector_SelectionChanged(
    Windows::Foundation::IInspectable const&,
    SelectionChangedEventArgs const&) {
    RefreshDiagnosticsAsync();
}

void DiagnosticsSectionControl::RefreshDiagnosticsButton_Click(
    Windows::Foundation::IInspectable const&,
    Microsoft::UI::Xaml::RoutedEventArgs const&) {
    RefreshDiagnosticsAsync();
}

void DiagnosticsSectionControl::ExportMarkdownButton_Click(
    Windows::Foundation::IInspectable const&,
    Microsoft::UI::Xaml::RoutedEventArgs const&) {
    ExportDiagnosticsAsync(true);
}

void DiagnosticsSectionControl::ExportJsonButton_Click(
    Windows::Foundation::IInspectable const&,
    Microsoft::UI::Xaml::RoutedEventArgs const&) {
    ExportDiagnosticsAsync(false);
}

void DiagnosticsSectionControl::ClearDiagnosticsButton_Click(
    Windows::Foundation::IInspectable const&,
    Microsoft::UI::Xaml::RoutedEventArgs const&) {
    ShowClearConfirmationAsync();
}

std::wstring DiagnosticsSectionControl::SelectedSeverityFilter() {
    // Same Tag-carrying ComboBoxItem pattern as the CLU Apple-operation
    // filter selector. An empty Tag means "All severities".
    const auto item = SeverityFilterSelector().SelectedItem().try_as<ComboBoxItem>();
    if (item == nullptr) {
        return {};
    }
    return std::wstring(unbox_value_or<winrt::hstring>(item.Tag(), winrt::hstring()).c_str());
}

std::wstring DiagnosticsSectionControl::SelectedSourceFilter() {
    const auto item = SourceFilterSelector().SelectedItem().try_as<ComboBoxItem>();
    if (item == nullptr) {
        return {};
    }
    return std::wstring(unbox_value_or<winrt::hstring>(item.Tag(), winrt::hstring()).c_str());
}

void DiagnosticsSectionControl::ApplySummary(
    const ::MasterControlShell::ShellDiagnosticsSummary& summary) {
    using winrt::Microsoft::UI::Xaml::Media::SolidColorBrush;
    using winrt::Windows::UI::ColorHelper;

    if (!summary.succeeded) {
        DiagnosticsSummaryText().Text(winrt::hstring(
            std::wstring(L"Diagnostics summary unavailable: ") + summary.message));
        DiagnosticsTotalEventsText().Text(L"--");
        DiagnosticsStoreStatusText().Text(L"Store posture unknown while the admin API is unreachable.");
        DiagnosticsStoreStatusDot().Background(
            SolidColorBrush(ColorHelper::FromArgb(0xFF, 0x8c, 0xb7, 0xc4)));
        return;
    }

    std::wstring headline = std::to_wstring(summary.totalEvents)
        + L" total event" + (summary.totalEvents == 1 ? L"" : L"s")
        + L" in the central aggregate.";
    if (!summary.generatedAtUtc.empty()) {
        headline += L" Summary generated (UTC): " + summary.generatedAtUtc + L".";
    }
    DiagnosticsSummaryText().Text(winrt::hstring(headline));
    DiagnosticsTotalEventsText().Text(winrt::hstring(std::to_wstring(summary.totalEvents)));

    // "No fake state" (PHASE-14 acceptance criterion 8): when the
    // SQLite store could not be opened the route surfaces
    // storeUnavailable with the underlying reason -- render it
    // verbatim instead of pretending the store is healthy.
    if (!summary.storeUnavailable.empty()) {
        DiagnosticsStoreStatusText().Text(winrt::hstring(
            std::wstring(L"Store unavailable: ") + summary.storeUnavailable));
        DiagnosticsStoreStatusDot().Background(
            SolidColorBrush(ColorHelper::FromArgb(0xFF, 0xff, 0x6a, 0x80)));
    } else if (summary.storeBacked) {
        DiagnosticsStoreStatusText().Text(L"SQLite store online -- events persist across service restarts.");
        DiagnosticsStoreStatusDot().Background(
            SolidColorBrush(ColorHelper::FromArgb(0xFF, 0x1c, 0xf2, 0xc1)));
    } else {
        DiagnosticsStoreStatusText().Text(L"Serving from per-component jsonl history; the store has not accumulated rows yet.");
        DiagnosticsStoreStatusDot().Background(
            SolidColorBrush(ColorHelper::FromArgb(0xFF, 0xff, 0xc8, 0x57)));
    }
}

void DiagnosticsSectionControl::PopulateEventRows(
    const std::vector<::MasterControlShell::ShellDiagnosticsEvent>& events) {
    using namespace winrt::Microsoft::UI::Xaml;
    using namespace winrt::Microsoft::UI::Xaml::Media;
    using winrt::Windows::UI::ColorHelper;

    auto stack = DiagnosticsEventsStack();
    stack.Children().Clear();

    if (events.empty()) {
        TextBlock txt;
        txt.Text(L"No diagnostic events match the current filters.");
        txt.FontSize(11);
        txt.Foreground(SolidColorBrush(ColorHelper::FromArgb(0xFF, 0x8c, 0xb7, 0xc4)));
        txt.TextWrapping(TextWrapping::Wrap);
        stack.Children().Append(txt);
        return;
    }

    // Row construction mirrors the Overview Error Reporting card
    // (v0.8.7): bottom hairline border, severity-colored dot + header
    // line, wrapped message body underneath.
    for (const auto& event : events) {
        Border row;
        row.BorderThickness(Thickness{0, 0, 0, 1});
        row.BorderBrush(SolidColorBrush(ColorHelper::FromArgb(0x33, 0xff, 0x3d, 0x2e)));
        row.Padding(Thickness{0, 4, 0, 4});

        StackPanel rowStack;
        rowStack.Spacing(2);

        StackPanel head;
        head.Orientation(Orientation::Horizontal);
        head.Spacing(8);

        Border sevDot;
        sevDot.Width(8); sevDot.Height(8);
        winrt::Microsoft::UI::Xaml::CornerRadius dotCorners;
        dotCorners.TopLeft = 4; dotCorners.TopRight = 4;
        dotCorners.BottomLeft = 4; dotCorners.BottomRight = 4;
        sevDot.CornerRadius(dotCorners);
        sevDot.VerticalAlignment(VerticalAlignment::Center);
        sevDot.Background(SolidColorBrush(severityColor(event.severity)));

        TextBlock headText;
        std::wstring headLabel = event.severity;
        if (!event.component.empty()) {
            headLabel += L" · " + event.component;
        }
        if (!event.eventName.empty()) {
            headLabel += L" · " + event.eventName;
        }
        headText.Text(winrt::hstring(headLabel));
        headText.FontSize(11);
        headText.Foreground(SolidColorBrush(severityColor(event.severity)));

        TextBlock tsText;
        tsText.Text(winrt::hstring(event.capturedAtUtc));
        tsText.FontSize(11);
        tsText.Foreground(SolidColorBrush(ColorHelper::FromArgb(0xFF, 0x8c, 0xb7, 0xc4)));

        head.Children().Append(sevDot);
        head.Children().Append(headText);
        head.Children().Append(tsText);
        rowStack.Children().Append(head);

        if (!event.message.empty()) {
            TextBlock messageText;
            messageText.Text(winrt::hstring(event.message));
            messageText.FontSize(12);
            messageText.TextWrapping(TextWrapping::Wrap);
            rowStack.Children().Append(messageText);
        }

        row.Child(rowStack);
        stack.Children().Append(row);
    }
}

void DiagnosticsSectionControl::UpdateActionState() {
    const bool hasRuntime = runtime_ != nullptr;
    const bool busy = loadInFlight_ || exportInFlight_ || clearInFlight_;
    RefreshDiagnosticsButton().IsEnabled(hasRuntime && !busy);
    ExportMarkdownButton().IsEnabled(hasRuntime && !busy);
    ExportJsonButton().IsEnabled(hasRuntime && !busy);
    ClearDiagnosticsButton().IsEnabled(hasRuntime && !busy);
    SeverityFilterSelector().IsEnabled(!busy);
    SourceFilterSelector().IsEnabled(!busy);
}

void DiagnosticsSectionControl::SetStatus(winrt::hstring const& message) {
    DiagnosticsStatusText().Text(message);
}

winrt::Windows::Foundation::IAsyncAction DiagnosticsSectionControl::RefreshDiagnosticsAsync() {
    if (runtime_ == nullptr || loadInFlight_) {
        co_return;
    }
    auto runtime = runtime_;

    loadInFlight_ = true;
    hasLoadedOnce_ = true;
    SetStatus(L"Querying the local admin API diagnostics aggregate.");
    UpdateActionState();

    // Read the filter slugs on the UI thread before hopping to the
    // background -- XAML accessors are not safe off-thread.
    const auto severity = SelectedSeverityFilter();
    const auto source = SelectedSourceFilter();

    winrt::apartment_context uiThread;
    co_await winrt::resume_background();
    const auto summary = runtime->FetchDiagnosticsSummary();
    const auto events = runtime->FetchDiagnosticsEvents(severity, source, 200);
    co_await uiThread;

    loadInFlight_ = false;
    ApplySummary(summary);
    if (events.succeeded) {
        PopulateEventRows(events.events);
        DiagnosticsEventsHeadlineText().Text(winrt::hstring(
            std::to_wstring(events.events.size()) + L" event"
            + (events.events.size() == 1 ? L"" : L"s")
            + L" shown (newest first, capped at 200)."));
    } else {
        PopulateEventRows({});
        DiagnosticsEventsHeadlineText().Text(L"Diagnostic events unavailable.");
    }
    SetStatus(winrt::hstring(events.message));
    UpdateActionState();
}

winrt::Windows::Foundation::IAsyncAction DiagnosticsSectionControl::ExportDiagnosticsAsync(
    const bool asMarkdown) {
    if (runtime_ == nullptr) {
        SetStatus(L"Diagnostics export is unavailable until the shell runtime is attached.");
        co_return;
    }
    if (exportInFlight_) {
        co_return;
    }
    auto runtime = runtime_;

    exportInFlight_ = true;
    SetStatus(asMarkdown
        ? L"Rendering the Markdown diagnostics export..."
        : L"Rendering the JSON diagnostics export...");
    UpdateActionState();

    winrt::apartment_context uiThread;
    co_await winrt::resume_background();
    const auto fetched = runtime->FetchDiagnosticsExport(
        asMarkdown ? std::wstring(L"markdown") : std::wstring(L"json"));
    co_await uiThread;

    if (!fetched.succeeded) {
        exportInFlight_ = false;
        SetStatus(winrt::hstring(
            std::wstring(L"Export failed: ") + fetched.message));
        UpdateActionState();
        co_return;
    }

    // Save dialog. IFileSaveDialog::Show blocks the UI thread, but a
    // save dialog is intrinsically modal so this matches the operator's
    // expectation (same trade-off as the supervisor config save).
    const auto suggested = buildDiagnosticsExportFileName(asMarkdown);
    std::wstring chosenPath;
    const bool picked = showDiagnosticsSaveDialog(asMarkdown, suggested, chosenPath);
    if (!picked) {
        exportInFlight_ = false;
        SetStatus(L"Export rendered; save cancelled. Click Export again to retry.");
        UpdateActionState();
        co_return;
    }

    if (!writeBytesToFile(chosenPath, fetched.content)) {
        exportInFlight_ = false;
        SetStatus(winrt::hstring(
            std::wstring(L"Error writing diagnostics export to: ") + chosenPath));
        UpdateActionState();
        co_return;
    }

    exportInFlight_ = false;
    SetStatus(winrt::hstring(std::wstring(L"Saved to ") + chosenPath));
    UpdateActionState();
}

winrt::Windows::Foundation::IAsyncAction DiagnosticsSectionControl::ShowClearConfirmationAsync() {
    if (runtime_ == nullptr) {
        SetStatus(L"Diagnostics clear is unavailable until the shell runtime is attached.");
        co_return;
    }
    if (clearInFlight_) {
        co_return;
    }

    // Same confirmation grammar as SecuritySectionControl's unsafe-
    // change dialog: explicit destructive verb on the primary button,
    // Cancel as the safe default.
    ContentDialog dialog;
    dialog.Title(winrt::box_value(L"Clear Diagnostics Store?"));
    dialog.Content(winrt::box_value(L"Clearing removes every record from the central diagnostics store. Per-component jsonl logs under logs\\ are operator-owned and stay untouched, but the queryable history shown here is deleted. Export a snapshot first if you need it for a support thread."));
    dialog.PrimaryButtonText(L"Clear");
    dialog.CloseButtonText(L"Cancel");
    dialog.DefaultButton(ContentDialogButton::Close);
    dialog.XamlRoot(XamlRoot());

    if (co_await dialog.ShowAsync() == ContentDialogResult::Primary) {
        co_await ClearDiagnosticsAsync();
    } else {
        SetStatus(L"Diagnostics clear was canceled.");
        UpdateActionState();
    }
}

winrt::Windows::Foundation::IAsyncAction DiagnosticsSectionControl::ClearDiagnosticsAsync() {
    if (runtime_ == nullptr || clearInFlight_) {
        co_return;
    }
    auto runtime = runtime_;

    clearInFlight_ = true;
    SetStatus(L"Clearing the diagnostics store...");
    UpdateActionState();

    winrt::apartment_context uiThread;
    co_await winrt::resume_background();
    const auto result = runtime->ClearDiagnostics(
        L"Operator clicked Clear on the shell Diagnostics surface.");
    co_await uiThread;

    clearInFlight_ = false;
    if (result.succeeded) {
        SetStatus(winrt::hstring(
            L"Cleared " + std::to_wstring(result.deletedRows)
            + L" record" + (result.deletedRows == 1 ? L"" : L"s")
            + L" from the diagnostics store."));
    } else {
        SetStatus(winrt::hstring(
            std::wstring(L"Clear failed: ") + result.message));
    }
    UpdateActionState();
    // Re-query so the roster + summary reflect the post-clear state.
    RefreshDiagnosticsAsync();
}

} // namespace winrt::MasterControlShell::implementation
