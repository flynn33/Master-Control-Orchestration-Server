// Master Control Program
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#pragma once

#include "ImportsSectionControl.g.h"
#include "pch.h"

#include "ShellRuntime.h"

namespace winrt::MasterControlShell::implementation {

struct ImportsSectionControl : ImportsSectionControlT<ImportsSectionControl> {
    ImportsSectionControl();

    void AttachRuntime(::MasterControlShell::ShellRuntime* runtime,
                       std::function<void()> refreshRequested);
    void ApplySnapshot(const ::MasterControlShell::ShellSnapshot& snapshot);
    void ImportModeSelector_SelectionChanged(Windows::Foundation::IInspectable const&,
                                             Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
    void RunPackageImportButton_Click(Windows::Foundation::IInspectable const&,
                                      Microsoft::UI::Xaml::RoutedEventArgs const&);
    void RunRepoImportButton_Click(Windows::Foundation::IInspectable const&,
                                   Microsoft::UI::Xaml::RoutedEventArgs const&);
    void RunZipImportButton_Click(Windows::Foundation::IInspectable const&,
                                  Microsoft::UI::Xaml::RoutedEventArgs const&);

private:
    enum class ImportMode {
        Package,
        Repository,
        ZipBundle
    };

    void SetImportMode(ImportMode mode);
    void SetStatus(winrt::hstring const& message);
    void UpdateEditorState();
    winrt::Windows::Foundation::IAsyncAction RunPackageImportAsync();
    winrt::Windows::Foundation::IAsyncAction RunRepoImportAsync();
    winrt::Windows::Foundation::IAsyncAction RunZipImportAsync();

    ::MasterControlShell::ShellRuntime* runtime_ = nullptr;
    std::function<void()> refreshRequested_;
    ImportMode importMode_ = ImportMode::Package;
};

} // namespace winrt::MasterControlShell::implementation

namespace winrt::MasterControlShell::factory_implementation {

struct ImportsSectionControl : ImportsSectionControlT<ImportsSectionControl, implementation::ImportsSectionControl> {
};

} // namespace winrt::MasterControlShell::factory_implementation
