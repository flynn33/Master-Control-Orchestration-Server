// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#pragma once

#include "DiagnosticsSectionControl.g.h"
#include "pch.h"

#include "ShellRuntime.h"

namespace winrt::MasterControlShell::implementation {

// v0.11.0-alpha.3 (PHASE-14 Slice C): operator surface for the
// centralized Diagnostics Service. Queries the Slice A/E HTTP routes
// through ShellRuntime fetch helpers, renders the severity-filtered
// event roster, exports Markdown / JSON snapshots through the native
// Win32 save dialog (same IFileSaveDialog pattern as the supervisor
// wizard in OverviewSectionControl), and clears the persistent store
// behind a ContentDialog confirmation.
struct DiagnosticsSectionControl : DiagnosticsSectionControlT<DiagnosticsSectionControl> {
    DiagnosticsSectionControl();

    void AttachRuntime(::MasterControlShell::ShellRuntime* runtime);
    void ApplySnapshot(const ::MasterControlShell::ShellSnapshot& snapshot);
    void SeverityFilterSelector_SelectionChanged(Windows::Foundation::IInspectable const&,
                                                 Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
    void SourceFilterSelector_SelectionChanged(Windows::Foundation::IInspectable const&,
                                               Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
    void RefreshDiagnosticsButton_Click(Windows::Foundation::IInspectable const&,
                                        Microsoft::UI::Xaml::RoutedEventArgs const&);
    void ExportMarkdownButton_Click(Windows::Foundation::IInspectable const&,
                                    Microsoft::UI::Xaml::RoutedEventArgs const&);
    void ExportJsonButton_Click(Windows::Foundation::IInspectable const&,
                                Microsoft::UI::Xaml::RoutedEventArgs const&);
    void ClearDiagnosticsButton_Click(Windows::Foundation::IInspectable const&,
                                      Microsoft::UI::Xaml::RoutedEventArgs const&);

private:
    [[nodiscard]] std::wstring SelectedSeverityFilter();
    [[nodiscard]] std::wstring SelectedSourceFilter();
    void ApplySummary(const ::MasterControlShell::ShellDiagnosticsSummary& summary);
    void PopulateEventRows(const std::vector<::MasterControlShell::ShellDiagnosticsEvent>& events);
    void UpdateActionState();
    void SetStatus(winrt::hstring const& message);
    winrt::Windows::Foundation::IAsyncAction RefreshDiagnosticsAsync();
    winrt::Windows::Foundation::IAsyncAction ExportDiagnosticsAsync(bool asMarkdown);
    winrt::Windows::Foundation::IAsyncAction ShowClearConfirmationAsync();
    winrt::Windows::Foundation::IAsyncAction ClearDiagnosticsAsync();

    ::MasterControlShell::ShellRuntime* runtime_ = nullptr;
    bool loadInFlight_ = false;
    bool exportInFlight_ = false;
    bool clearInFlight_ = false;
    bool hasLoadedOnce_ = false;
};

} // namespace winrt::MasterControlShell::implementation

namespace winrt::MasterControlShell::factory_implementation {

struct DiagnosticsSectionControl : DiagnosticsSectionControlT<DiagnosticsSectionControl, implementation::DiagnosticsSectionControl> {
};

} // namespace winrt::MasterControlShell::factory_implementation
