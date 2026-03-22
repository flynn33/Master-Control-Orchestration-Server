// Master Control Program
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#include "pch.h"

#include "CommandLogicUnitSectionControl.xaml.h"

#if __has_include("CommandLogicUnitSectionControl.g.cpp")
#include "CommandLogicUnitSectionControl.g.cpp"
#endif

#include "ShellFormatting.h"

namespace winrt::MasterControlShell::implementation {

using namespace ::MasterControlShell::Presentation;

CommandLogicUnitSectionControl::CommandLogicUnitSectionControl() {
    InitializeComponent();
}

void CommandLogicUnitSectionControl::AttachRuntime(::MasterControlShell::ShellRuntime* runtime,
                                                   std::function<void()> refreshRequested) {
    runtime_ = runtime;
    refreshRequested_ = std::move(refreshRequested);
    UpdateOperationState();
}

void CommandLogicUnitSectionControl::ApplySnapshot(const ::MasterControlShell::ShellSnapshot& snapshot) {
    CluPostureText().Text(winrt::hstring(snapshot.governancePosture.empty() ? L"Pending" : snapshot.governancePosture));
    CluFindingCountText().Text(winrt::hstring(std::to_wstring(snapshot.governanceFindingCount) + L" findings"));
    CluRoleCountText().Text(winrt::hstring(std::to_wstring(snapshot.governanceRoleCount)));
    CluRuleCountText().Text(winrt::hstring(std::to_wstring(snapshot.governanceRuleCount)));
    CluDocumentCountText().Text(winrt::hstring(std::to_wstring(snapshot.governanceDocumentCount)));
    CluAppleHostCountText().Text(winrt::hstring(std::to_wstring(snapshot.appleRemoteHostCount)));
    CluAppleOperationCountText().Text(winrt::hstring(std::to_wstring(snapshot.appleOperationCount)));
    CluGatewayCountText().Text(winrt::hstring(std::to_wstring(snapshot.platformGatewayCount)));
    CluGovernanceServerCountText().Text(winrt::hstring(std::to_wstring(snapshot.governanceServerCount)));
    CluEvaluatedText().Text(winrt::hstring(L"Last evaluated: " + snapshot.governanceLastEvaluatedUtc));
    CluNarrativeText().Text(winrt::hstring(snapshot.governanceNarrative));
    CluDoctrineText().Text(winrt::hstring(snapshot.governanceDoctrine));

    populateListView(CluActionsListView(), snapshot.governanceActionRows);
    populateListView(CluFindingsListView(), snapshot.governanceFindingRows);
    populateListView(CluAppleHostsListView(), snapshot.appleRemoteHostRows);
    populateListView(CluAppleOperationsListView(), snapshot.appleOperationRows);
    populateListView(CluPlatformGatewaysListView(), snapshot.platformGatewayRows);
    populateListView(CluGovernanceServersListView(), snapshot.governanceServerRows);
    populateListView(CluRolesListView(), snapshot.governanceRoleRows);
    populateListView(CluRulesListView(), snapshot.governanceRuleRows);
    populateListView(CluDocumentsListView(), snapshot.governanceDocumentRows);
    populateListView(CluRecentExecutionsListView(), snapshot.governanceExecutionRows);

    appleOperations_ = snapshot.appleOperations;
    RefreshAppleOperationSelector();
    UpdateOperationState();
}

void CommandLogicUnitSectionControl::AppleOperationSelector_SelectionChanged(
    Windows::Foundation::IInspectable const&,
    Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&) {
    const auto index = AppleOperationSelector().SelectedIndex();
    if (index <= 0) {
        selectedAppleOperationId_.clear();
    } else {
        const auto resolvedIndex = static_cast<size_t>(index - 1);
        if (resolvedIndex < appleOperations_.size()) {
            selectedAppleOperationId_ = appleOperations_[resolvedIndex].operationId;
        }
    }
    UpdateOperationState();
}

void CommandLogicUnitSectionControl::RerunAppleOperationButton_Click(
    Windows::Foundation::IInspectable const&,
    Microsoft::UI::Xaml::RoutedEventArgs const&) {
    auto ignored = RerunAppleOperationAsync();
    (void)ignored;
}

void CommandLogicUnitSectionControl::RefreshAppleOperationSelector() {
    AppleOperationSelector().Items().Clear();

    Microsoft::UI::Xaml::Controls::ComboBoxItem placeholder;
    placeholder.Content(box_value(L"Select an Apple operation"));
    AppleOperationSelector().Items().Append(placeholder);

    int selectedIndex = 0;
    int currentIndex = 1;
    for (const auto& operation : appleOperations_) {
        Microsoft::UI::Xaml::Controls::ComboBoxItem item;
        std::wstring label = operation.displayName.empty() ? operation.toolId : operation.displayName;
        if (!operation.hostDisplayName.empty()) {
            label += L"  |  " + operation.hostDisplayName;
        }
        if (!operation.completedAtUtc.empty()) {
            label += L"  |  " + operation.completedAtUtc;
        }
        item.Content(box_value(label));
        item.Tag(box_value(operation.operationId));
        AppleOperationSelector().Items().Append(item);
        if (!selectedAppleOperationId_.empty() && operation.operationId == selectedAppleOperationId_) {
            selectedIndex = currentIndex;
        }
        ++currentIndex;
    }

    AppleOperationSelector().SelectedIndex(selectedIndex);
}

void CommandLogicUnitSectionControl::UpdateOperationState() {
    const auto hasSelection = !selectedAppleOperationId_.empty();
    RerunAppleOperationButton().IsEnabled(runtime_ != nullptr && hasSelection);
    if (!hasSelection) {
        AppleOperationStatusText().Text(L"Select an Apple operation to replay it through CLU.");
        return;
    }

    const auto iterator = std::find_if(
        appleOperations_.begin(),
        appleOperations_.end(),
        [this](const ::MasterControlShell::ShellAppleOperationRecord& operation) {
            return operation.operationId == selectedAppleOperationId_;
        });
    if (iterator == appleOperations_.end()) {
        AppleOperationStatusText().Text(L"Selected Apple operation is no longer available.");
        return;
    }

    std::wstring status = iterator->summary.empty() ? L"Ready to replay the selected Apple operation." : iterator->summary;
    if (!iterator->routeReason.empty()) {
        status += L" Route: " + iterator->routeReason;
    }
    if (!iterator->selectedDeveloperDirectory.empty()) {
        status += L" Developer dir: " + iterator->selectedDeveloperDirectory + L".";
    }
    if (!iterator->credentialProfileSummary.empty()) {
        status += L" Profile: " + iterator->credentialProfileSummary;
        if (!status.empty() && status.back() != L'.') {
            status += L".";
        }
    }
    if (!iterator->readinessIssues.empty()) {
        status += L" Readiness gaps: ";
        for (size_t index = 0; index < iterator->readinessIssues.size(); ++index) {
            if (index > 0) {
                status += L"; ";
            }
            status += iterator->readinessIssues[index];
        }
        status += L".";
    }
    if (!iterator->redactedRequestOptionKeys.empty()) {
        status += L" Sensitive request options were redacted from stored history, so replay may require host defaults or fresh credentials.";
    }
    if (!iterator->diagnosticSummary.empty()) {
        status += L" Diagnostics: " + iterator->diagnosticSummary;
        if (!status.empty() && status.back() != L'.') {
            status += L".";
        }
    }
    if (!iterator->errorMessage.empty()) {
        status += L" Last error: " + iterator->errorMessage;
    }
    AppleOperationStatusText().Text(winrt::hstring(status));
}

winrt::Windows::Foundation::IAsyncAction CommandLogicUnitSectionControl::RerunAppleOperationAsync() {
    if (runtime_ == nullptr) {
        AppleOperationStatusText().Text(L"Apple operation replay is unavailable until the shell runtime is attached.");
        co_return;
    }
    const auto iterator = std::find_if(
        appleOperations_.begin(),
        appleOperations_.end(),
        [this](const ::MasterControlShell::ShellAppleOperationRecord& operation) {
            return operation.operationId == selectedAppleOperationId_;
        });
    if (iterator == appleOperations_.end()) {
        AppleOperationStatusText().Text(L"Select a recorded Apple operation before replaying it.");
        co_return;
    }

    auto requestOptions = iterator->requestOptions;
    if (!iterator->hostId.empty()) {
        requestOptions.insert_or_assign(L"hostId", iterator->hostId);
    }

    RerunAppleOperationButton().IsEnabled(false);
    AppleOperationStatusText().Text(L"Replaying Apple governance operation through CLU.");
    winrt::apartment_context uiThread;
    const auto platform = iterator->platform;
    const auto toolId = iterator->toolId;
    const auto targetPath = iterator->targetPath;
    co_await winrt::resume_background();
    const auto result = runtime_->ExecuteGovernanceTool(platform, toolId, targetPath, requestOptions);
    co_await uiThread;

    AppleOperationStatusText().Text(winrt::hstring(result.message));
    if (result.succeeded && refreshRequested_) {
        refreshRequested_();
    }
    UpdateOperationState();
}

} // namespace winrt::MasterControlShell::implementation
