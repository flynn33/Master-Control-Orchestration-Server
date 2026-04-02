// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#pragma once

#include "ExportsSectionControl.g.h"
#include "pch.h"

#include "ShellRuntime.h"

namespace winrt::MasterControlShell::implementation {

struct ExportsSectionControl : ExportsSectionControlT<ExportsSectionControl> {
    ExportsSectionControl();

    void AttachRuntime(::MasterControlShell::ShellRuntime* runtime);
    void ApplySnapshot(const ::MasterControlShell::ShellSnapshot& snapshot);
    void ExportSelector_SelectionChanged(Windows::Foundation::IInspectable const&,
                                         Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
    void RefreshExportsButton_Click(Windows::Foundation::IInspectable const&,
                                    Microsoft::UI::Xaml::RoutedEventArgs const&);
    void ExportSelectedButton_Click(Windows::Foundation::IInspectable const&,
                                    Microsoft::UI::Xaml::RoutedEventArgs const&);
    void ExportAllButton_Click(Windows::Foundation::IInspectable const&,
                               Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OpenExportFolderButton_Click(Windows::Foundation::IInspectable const&,
                                      Microsoft::UI::Xaml::RoutedEventArgs const&);

private:
    void RefreshSelector();
    void UpdatePreview();
    void UpdateActionState();
    void SetStatus(winrt::hstring const& message);
    winrt::Windows::Foundation::IAsyncAction LoadExportsAsync();
    winrt::Windows::Foundation::IAsyncAction ExportArtifactsAsync(bool selectedOnly);

    ::MasterControlShell::ShellRuntime* runtime_ = nullptr;
    std::vector<::MasterControlShell::ShellExportArtifact> artifacts_;
    std::wstring selectedArtifactId_;
    bool loadInFlight_ = false;
};

} // namespace winrt::MasterControlShell::implementation

namespace winrt::MasterControlShell::factory_implementation {

struct ExportsSectionControl : ExportsSectionControlT<ExportsSectionControl, implementation::ExportsSectionControl> {
};

} // namespace winrt::MasterControlShell::factory_implementation
