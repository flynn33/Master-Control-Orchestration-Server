// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#include "pch.h"

#include "ImportsSectionControl.xaml.h"

#if __has_include("ImportsSectionControl.g.cpp")
#include "ImportsSectionControl.g.cpp"
#endif

#include "ShellFormatting.h"

namespace winrt::MasterControlShell::implementation {

using namespace ::MasterControlShell::Presentation;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

namespace {

void setVisible(const UIElement& element, const bool visible) {
    element.Visibility(visible ? Visibility::Visible : Visibility::Collapsed);
}

::MasterControlShell::ShellInstallerKind packageKindFromTag(const winrt::hstring& tag) {
    const std::wstring value = tag.c_str();
    if (value == L"msi") {
        return ::MasterControlShell::ShellInstallerKind::Msi;
    }
    if (value == L"powershell") {
        return ::MasterControlShell::ShellInstallerKind::PowerShell;
    }
    return ::MasterControlShell::ShellInstallerKind::Exe;
}

} // namespace

ImportsSectionControl::ImportsSectionControl() {
    InitializeComponent();
    ImportModeSelector().SelectedIndex(0);
    PackageKindComboBox().SelectedIndex(1);
    RepoBranchTextBox().Text(L"main");
    RepoManifestTextBox().Text(L"mcp-bootstrap.json");
    ZipManifestTextBox().Text(L"mcp-bootstrap.json");
}

void ImportsSectionControl::AttachRuntime(::MasterControlShell::ShellRuntime* runtime,
                                          std::function<void()> refreshRequested) {
    runtime_ = runtime;
    refreshRequested_ = std::move(refreshRequested);
    UpdateEditorState();
}

void ImportsSectionControl::ApplySnapshot(const ::MasterControlShell::ShellSnapshot& snapshot) {
    ImportRecordCountText().Text(winrt::hstring(std::to_wstring(snapshot.installCount)));
    ImportTrustText().Text(snapshot.apiHealthy ? L"Managed provenance ledger online" : L"Showing cached provenance state");
    ImportsNarrativeText().Text(winrt::hstring(formatImportsNarrative(snapshot)));
    populateListView(InstallHistoryListView(), snapshot.installRows);
}

void ImportsSectionControl::ImportModeSelector_SelectionChanged(
    Windows::Foundation::IInspectable const&,
    SelectionChangedEventArgs const&) {
    if (const auto selectedItem = ImportModeSelector().SelectedItem().try_as<ComboBoxItem>()) {
        const std::wstring tag = unbox_value_or<winrt::hstring>(selectedItem.Tag(), winrt::hstring(L"package")).c_str();
        if (tag == L"repo") {
            SetImportMode(ImportMode::Repository);
        } else if (tag == L"zip") {
            SetImportMode(ImportMode::ZipBundle);
        } else {
            SetImportMode(ImportMode::Package);
        }
    }
}

void ImportsSectionControl::RunPackageImportButton_Click(
    Windows::Foundation::IInspectable const&,
    RoutedEventArgs const&) {
    RunPackageImportAsync();
}

void ImportsSectionControl::RunRepoImportButton_Click(
    Windows::Foundation::IInspectable const&,
    RoutedEventArgs const&) {
    RunRepoImportAsync();
}

void ImportsSectionControl::RunZipImportButton_Click(
    Windows::Foundation::IInspectable const&,
    RoutedEventArgs const&) {
    RunZipImportAsync();
}

void ImportsSectionControl::SetImportMode(const ImportMode mode) {
    importMode_ = mode;
    setVisible(PackageEditorPanel(), mode == ImportMode::Package);
    setVisible(RepoEditorPanel(), mode == ImportMode::Repository);
    setVisible(ZipEditorPanel(), mode == ImportMode::ZipBundle);
    UpdateEditorState();
}

void ImportsSectionControl::SetStatus(winrt::hstring const& message) {
    ImportStatusText().Text(message);
}

void ImportsSectionControl::UpdateEditorState() {
    const bool hasRuntime = runtime_ != nullptr;
    RunPackageImportButton().IsEnabled(hasRuntime);
    RunRepoImportButton().IsEnabled(hasRuntime);
    RunZipImportButton().IsEnabled(hasRuntime);
}

winrt::Windows::Foundation::IAsyncAction ImportsSectionControl::RunPackageImportAsync() {
    if (runtime_ == nullptr) {
        SetStatus(L"Package imports are unavailable until the shell runtime is attached.");
        co_return;
    }

    const auto source = std::wstring(PackageSourceTextBox().Text().c_str());
    if (source.empty()) {
        SetStatus(L"Enter a package source or local path before running the import.");
        co_return;
    }

    ::MasterControlShell::ShellInstallerPackageSpec spec;
    spec.source = source;
    spec.arguments = std::wstring(PackageArgumentsTextBox().Text().c_str());
    spec.allowUntrustedExecution = PackageTrustToggle().IsOn();
    if (const auto selectedItem = PackageKindComboBox().SelectedItem().try_as<ComboBoxItem>()) {
        spec.kind = packageKindFromTag(unbox_value_or<winrt::hstring>(selectedItem.Tag(), winrt::hstring(L"exe")));
    }

    RunPackageImportButton().IsEnabled(false);
    winrt::apartment_context uiThread;
    co_await winrt::resume_background();
    const auto result = runtime_->InstallPackage(spec);
    co_await uiThread;

    SetStatus(winrt::hstring(result.message));
    if (result.succeeded && refreshRequested_) {
        refreshRequested_();
    }
    UpdateEditorState();
}

winrt::Windows::Foundation::IAsyncAction ImportsSectionControl::RunRepoImportAsync() {
    if (runtime_ == nullptr) {
        SetStatus(L"Repository imports are unavailable until the shell runtime is attached.");
        co_return;
    }

    const auto repositoryUrl = std::wstring(RepoUrlTextBox().Text().c_str());
    if (repositoryUrl.empty()) {
        SetStatus(L"Enter a repository URL or local path before running the import.");
        co_return;
    }

    ::MasterControlShell::ShellBootstrapRepoSpec spec;
    spec.repositoryUrl = repositoryUrl;
    spec.branch = std::wstring(RepoBranchTextBox().Text().c_str());
    spec.manifestFile = std::wstring(RepoManifestTextBox().Text().c_str());
    spec.allowUntrustedExecution = RepoTrustToggle().IsOn();
    if (spec.branch.empty()) {
        spec.branch = L"main";
    }
    if (spec.manifestFile.empty()) {
        spec.manifestFile = L"mcp-bootstrap.json";
    }

    RunRepoImportButton().IsEnabled(false);
    winrt::apartment_context uiThread;
    co_await winrt::resume_background();
    const auto result = runtime_->InstallRepository(spec);
    co_await uiThread;

    SetStatus(winrt::hstring(result.message));
    if (result.succeeded && refreshRequested_) {
        refreshRequested_();
    }
    UpdateEditorState();
}

winrt::Windows::Foundation::IAsyncAction ImportsSectionControl::RunZipImportAsync() {
    if (runtime_ == nullptr) {
        SetStatus(L"Zip imports are unavailable until the shell runtime is attached.");
        co_return;
    }

    const auto source = std::wstring(ZipSourceTextBox().Text().c_str());
    if (source.empty()) {
        SetStatus(L"Enter a zip source or local path before running the import.");
        co_return;
    }

    ::MasterControlShell::ShellZipBundleSpec spec;
    spec.source = source;
    spec.manifestFile = std::wstring(ZipManifestTextBox().Text().c_str());
    spec.allowUntrustedExecution = ZipTrustToggle().IsOn();
    if (spec.manifestFile.empty()) {
        spec.manifestFile = L"mcp-bootstrap.json";
    }

    RunZipImportButton().IsEnabled(false);
    winrt::apartment_context uiThread;
    co_await winrt::resume_background();
    const auto result = runtime_->InstallZipBundle(spec);
    co_await uiThread;

    SetStatus(winrt::hstring(result.message));
    if (result.succeeded && refreshRequested_) {
        refreshRequested_();
    }
    UpdateEditorState();
}

} // namespace winrt::MasterControlShell::implementation
