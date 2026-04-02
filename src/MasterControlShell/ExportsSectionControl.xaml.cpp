// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#include "pch.h"

#include "ExportsSectionControl.xaml.h"

#if __has_include("ExportsSectionControl.g.cpp")
#include "ExportsSectionControl.g.cpp"
#endif

#include "ShellFormatting.h"

namespace winrt::MasterControlShell::implementation {

using namespace ::MasterControlShell::Presentation;
using namespace Microsoft::UI::Xaml::Controls;

ExportsSectionControl::ExportsSectionControl() {
    InitializeComponent();
}

void ExportsSectionControl::AttachRuntime(::MasterControlShell::ShellRuntime* runtime) {
    runtime_ = runtime;
    UpdateActionState();
    if (runtime_ != nullptr) {
        LoadExportsAsync();
    }
}

void ExportsSectionControl::ApplySnapshot(const ::MasterControlShell::ShellSnapshot& snapshot) {
    ExportArtifactCountText().Text(winrt::hstring(std::to_wstring(snapshot.exportCount)));
    DashboardUrlText().Text(winrt::hstring(snapshot.dashboardUrl));
    ExportsNarrativeText().Text(winrt::hstring(formatExportsNarrative(snapshot)));
    populateListView(ExportsListView(), snapshot.exportRows);
    if (runtime_ != nullptr && !loadInFlight_ &&
        (artifacts_.empty() || artifacts_.size() != snapshot.exportCount)) {
        LoadExportsAsync();
    }
    UpdateActionState();
}

void ExportsSectionControl::ExportSelector_SelectionChanged(
    Windows::Foundation::IInspectable const&,
    SelectionChangedEventArgs const&) {
    const auto selectedIndex = ExportSelector().SelectedIndex();
    if (selectedIndex >= 0 && selectedIndex < static_cast<int>(artifacts_.size())) {
        selectedArtifactId_ = artifacts_[static_cast<size_t>(selectedIndex)].id;
    } else {
        selectedArtifactId_.clear();
    }

    UpdatePreview();
    UpdateActionState();
}

void ExportsSectionControl::RefreshExportsButton_Click(
    Windows::Foundation::IInspectable const&,
    Microsoft::UI::Xaml::RoutedEventArgs const&) {
    LoadExportsAsync();
}

void ExportsSectionControl::ExportSelectedButton_Click(
    Windows::Foundation::IInspectable const&,
    Microsoft::UI::Xaml::RoutedEventArgs const&) {
    ExportArtifactsAsync(true);
}

void ExportsSectionControl::ExportAllButton_Click(
    Windows::Foundation::IInspectable const&,
    Microsoft::UI::Xaml::RoutedEventArgs const&) {
    ExportArtifactsAsync(false);
}

void ExportsSectionControl::OpenExportFolderButton_Click(
    Windows::Foundation::IInspectable const&,
    Microsoft::UI::Xaml::RoutedEventArgs const&) {
    if (runtime_ == nullptr) {
        SetStatus(L"Open the export folder after the shell runtime is attached.");
        return;
    }

    runtime_->OpenExportsDirectory();
}

void ExportsSectionControl::RefreshSelector() {
    const auto previousSelection = selectedArtifactId_;
    ExportSelector().Items().Clear();
    for (const auto& artifact : artifacts_) {
        std::wstring label = artifact.fileName.empty() ? artifact.id : artifact.fileName;
        if (!artifact.mediaType.empty()) {
            label += L"  |  ";
            label += artifact.mediaType;
        }
        ExportSelector().Items().Append(winrt::box_value(winrt::hstring(label)));
    }

    selectedArtifactId_.clear();
    if (!previousSelection.empty()) {
        for (size_t index = 0; index < artifacts_.size(); ++index) {
            if (artifacts_[index].id == previousSelection) {
                ExportSelector().SelectedIndex(static_cast<int>(index));
                selectedArtifactId_ = artifacts_[index].id;
                break;
            }
        }
    }

    if (selectedArtifactId_.empty() && !artifacts_.empty()) {
        ExportSelector().SelectedIndex(0);
        selectedArtifactId_ = artifacts_.front().id;
    }
}

void ExportsSectionControl::UpdatePreview() {
    const auto iterator = std::find_if(
        artifacts_.begin(),
        artifacts_.end(),
        [this](const auto& artifact) { return artifact.id == selectedArtifactId_; });
    if (iterator == artifacts_.end()) {
        SelectedExportFileNameText().Text(L"No artifact selected");
        SelectedExportMediaTypeText().Text(L"n/a");
        ExportPreviewTextBox().Text(L"");
        return;
    }

    SelectedExportFileNameText().Text(winrt::hstring(iterator->fileName));
    SelectedExportMediaTypeText().Text(winrt::hstring(iterator->mediaType));
    ExportPreviewTextBox().Text(winrt::hstring(iterator->content));
}

void ExportsSectionControl::UpdateActionState() {
    const bool hasRuntime = runtime_ != nullptr;
    RefreshExportsButton().IsEnabled(hasRuntime && !loadInFlight_);
    ExportAllButton().IsEnabled(hasRuntime && !loadInFlight_ && !artifacts_.empty());
    ExportSelectedButton().IsEnabled(hasRuntime && !loadInFlight_ && !selectedArtifactId_.empty());
    OpenExportFolderButton().IsEnabled(hasRuntime);
}

void ExportsSectionControl::SetStatus(winrt::hstring const& message) {
    ExportStatusText().Text(message);
}

winrt::Windows::Foundation::IAsyncAction ExportsSectionControl::LoadExportsAsync() {
    if (runtime_ == nullptr || loadInFlight_) {
        co_return;
    }

    loadInFlight_ = true;
    SetStatus(L"Loading export artifacts from the local admin API.");
    UpdateActionState();

    winrt::apartment_context uiThread;
    co_await winrt::resume_background();
    const auto result = runtime_->FetchExports();
    co_await uiThread;

    loadInFlight_ = false;
    if (result.succeeded) {
        artifacts_ = result.artifacts;
        RefreshSelector();
        UpdatePreview();
    }

    SetStatus(winrt::hstring(result.message));
    UpdateActionState();
}

winrt::Windows::Foundation::IAsyncAction ExportsSectionControl::ExportArtifactsAsync(const bool selectedOnly) {
    if (runtime_ == nullptr) {
        SetStatus(L"Export actions are unavailable until the shell runtime is attached.");
        co_return;
    }

    std::vector<std::wstring> artifactIds;
    if (selectedOnly) {
        if (selectedArtifactId_.empty()) {
            SetStatus(L"Select an artifact before exporting it.");
            co_return;
        }
        artifactIds.push_back(selectedArtifactId_);
    }

    RefreshExportsButton().IsEnabled(false);
    ExportSelectedButton().IsEnabled(false);
    ExportAllButton().IsEnabled(false);

    winrt::apartment_context uiThread;
    co_await winrt::resume_background();
    const auto result = runtime_->MaterializeExports(artifactIds);
    co_await uiThread;

    SetStatus(winrt::hstring(result.message));
    UpdateActionState();
}

} // namespace winrt::MasterControlShell::implementation
