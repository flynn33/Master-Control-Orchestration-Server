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

namespace {

bool isAttentionAppleOperation(const ::MasterControlShell::ShellAppleOperationRecord& operation) {
    const auto status = operation.status;
    return _wcsicmp(status.c_str(), L"failed") == 0 || _wcsicmp(status.c_str(), L"blocked") == 0;
}

bool isActiveAppleOperation(const ::MasterControlShell::ShellAppleOperationRecord& operation) {
    const auto status = operation.status;
    return _wcsicmp(status.c_str(), L"queued") == 0 || _wcsicmp(status.c_str(), L"running") == 0;
}

std::wstring appleOperationRowText(const ::MasterControlShell::ShellAppleOperationRecord& operation) {
    std::wstring row = L"[";
    row += operation.status.empty() ? L"queued" : operation.status;
    row += L"] ";
    row += operation.displayName.empty() ? operation.toolId : operation.displayName;
    row += L"  |  ";
    row += operation.platform.empty() ? L"unknown" : operation.platform;
    row += L"  |  ";
    row += operation.hostDisplayName.empty() ? (operation.hostId.empty() ? L"unassigned host" : operation.hostId) : operation.hostDisplayName;
    row += L"  |  ";
    row += operation.transport.empty() ? L"unknown" : operation.transport;

    if (!operation.artifactPath.empty()) {
        row += L"  |  artifact=" + operation.artifactPath;
    } else if (!operation.summary.empty()) {
        row += L"  |  " + operation.summary;
    }

    if (!operation.rerunReadinessMessage.empty()) {
        row += L"  |  replay=" + operation.rerunReadinessMessage;
    }

    if (!operation.completedAtUtc.empty()) {
        row += L"  |  " + operation.completedAtUtc;
    } else if (!operation.startedAtUtc.empty()) {
        row += L"  |  " + operation.startedAtUtc;
    } else if (!operation.queuedAtUtc.empty()) {
        row += L"  |  " + operation.queuedAtUtc;
    }

    return row;
}

std::wstring joinValues(const std::vector<std::wstring>& values, const wchar_t* separator = L", ") {
    std::wstring result;
    for (size_t index = 0; index < values.size(); ++index) {
        if (index > 0) {
            result += separator;
        }
        result += values[index];
    }
    return result;
}

} // namespace

CommandLogicUnitSectionControl::CommandLogicUnitSectionControl() {
    InitializeComponent();
    AppleOperationFilterSelector().SelectedIndex(0);
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
    populateListView(CluPlatformGatewaysListView(), snapshot.platformGatewayRows);
    populateListView(CluGovernanceServersListView(), snapshot.governanceServerRows);
    populateListView(CluRolesListView(), snapshot.governanceRoleRows);
    populateListView(CluRulesListView(), snapshot.governanceRuleRows);
    populateListView(CluDocumentsListView(), snapshot.governanceDocumentRows);
    populateListView(CluRecentExecutionsListView(), snapshot.governanceExecutionRows);

    appleOperations_ = snapshot.appleOperations;
    RefreshAppleOperationList();
    RefreshAppleOperationSelector();
    UpdateOperationState();
}

void CommandLogicUnitSectionControl::AppleOperationFilterSelector_SelectionChanged(
    Windows::Foundation::IInspectable const&,
    Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&) {
    const auto index = AppleOperationFilterSelector().SelectedIndex();
    if (index < 0) {
        selectedAppleOperationFilter_ = L"attention";
    } else {
        const auto item = AppleOperationFilterSelector().SelectedItem().try_as<Microsoft::UI::Xaml::Controls::ComboBoxItem>();
        if (item != nullptr) {
            selectedAppleOperationFilter_ = unbox_value_or<winrt::hstring>(item.Tag(), L"attention").c_str();
        }
    }
    RefreshAppleOperationList();
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

void CommandLogicUnitSectionControl::RetryAttentionAppleOperationsButton_Click(
    Windows::Foundation::IInspectable const&,
    Microsoft::UI::Xaml::RoutedEventArgs const&) {
    auto ignored = RetryAttentionAppleOperationsAsync();
    (void)ignored;
}

void CommandLogicUnitSectionControl::RerunAppleOperationButton_Click(
    Windows::Foundation::IInspectable const&,
    Microsoft::UI::Xaml::RoutedEventArgs const&) {
    auto ignored = RerunAppleOperationAsync();
    (void)ignored;
}

std::vector<::MasterControlShell::ShellAppleOperationRecord> CommandLogicUnitSectionControl::FilteredAppleOperations() const {
    std::vector<::MasterControlShell::ShellAppleOperationRecord> filtered;
    filtered.reserve(appleOperations_.size());
    for (const auto& operation : appleOperations_) {
        const auto include = selectedAppleOperationFilter_ == L"all" ||
            (selectedAppleOperationFilter_ == L"attention" && isAttentionAppleOperation(operation)) ||
            (selectedAppleOperationFilter_ == L"active" && isActiveAppleOperation(operation)) ||
            (selectedAppleOperationFilter_ == L"succeeded" && _wcsicmp(operation.status.c_str(), L"succeeded") == 0);
        if (include) {
            filtered.push_back(operation);
        }
    }
    return filtered;
}

void CommandLogicUnitSectionControl::RefreshAppleOperationList() {
    const auto filtered = FilteredAppleOperations();
    std::vector<std::wstring> rows;
    rows.reserve(filtered.size());
    for (const auto& operation : filtered) {
        rows.push_back(appleOperationRowText(operation));
    }
    if (rows.empty()) {
        rows.push_back(L"No Apple operations match the current CLU filter.");
    }
    populateListView(CluAppleOperationsListView(), rows);
}

void CommandLogicUnitSectionControl::RefreshAppleOperationSelector() {
    AppleOperationSelector().Items().Clear();

    Microsoft::UI::Xaml::Controls::ComboBoxItem placeholder;
    placeholder.Content(box_value(L"Select an Apple operation"));
    AppleOperationSelector().Items().Append(placeholder);

    int selectedIndex = 0;
    int currentIndex = 1;
    for (const auto& operation : FilteredAppleOperations()) {
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
    RerunAppleOperationButton().IsEnabled(false);
    size_t queuedCount = 0;
    size_t runningCount = 0;
    size_t succeededCount = 0;
    size_t attentionCount = 0;
    size_t rerunnableAttentionCount = 0;
    for (const auto& operation : appleOperations_) {
        if (_wcsicmp(operation.status.c_str(), L"queued") == 0) {
            ++queuedCount;
        } else if (_wcsicmp(operation.status.c_str(), L"running") == 0) {
            ++runningCount;
        } else if (_wcsicmp(operation.status.c_str(), L"succeeded") == 0) {
            ++succeededCount;
        }
        if (isAttentionAppleOperation(operation)) {
            ++attentionCount;
            if (operation.rerunReady) {
                ++rerunnableAttentionCount;
            }
        }
    }

    std::wstring queueSummary =
        L"Queued " + std::to_wstring(queuedCount) +
        L" | Running " + std::to_wstring(runningCount) +
        L" | Attention " + std::to_wstring(attentionCount) +
        L" | Succeeded " + std::to_wstring(succeededCount);
    if (rerunnableAttentionCount > 0) {
        queueSummary += L" | Rerunnable attention ops " + std::to_wstring(rerunnableAttentionCount);
    }
    AppleOperationQueueText().Text(winrt::hstring(queueSummary));
    RetryAttentionAppleOperationsButton().IsEnabled(runtime_ != nullptr && rerunnableAttentionCount > 0);

    if (!hasSelection) {
        AppleOperationStatusText().Text(L"Select an Apple operation to replay it through CLU.");
        AppleOperationDetailsText().Text(L"Apple operation route and readiness detail will appear here.");
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
        AppleOperationDetailsText().Text(L"Apple operation route and readiness detail will appear here.");
        return;
    }

    RerunAppleOperationButton().IsEnabled(runtime_ != nullptr && iterator->rerunReady);
    std::wstring status = iterator->summary.empty() ? L"Ready to replay the selected Apple operation." : iterator->summary;
    if (!iterator->rerunReadinessMessage.empty()) {
        status += L" Replay: " + iterator->rerunReadinessMessage;
        if (!status.empty() && status.back() != L'.') {
            status += L".";
        }
    }
    if (!iterator->errorMessage.empty()) {
        status += L" Last error: " + iterator->errorMessage;
    }
    AppleOperationStatusText().Text(winrt::hstring(status));

    std::wstring details = L"Route lane: ";
    details += iterator->routeReason.empty() ? L"pending route summary" : iterator->routeReason;
    details += L"\n";

    details += L"Execution host: ";
    details += iterator->hostDisplayName.empty() ? (iterator->hostId.empty() ? L"unassigned host" : iterator->hostId) : iterator->hostDisplayName;
    details += L" via ";
    details += iterator->transport.empty() ? L"unknown transport" : iterator->transport;
    details += L".\n";

    if (!iterator->selectedDeveloperDirectory.empty()) {
        details += L"Developer directory: " + iterator->selectedDeveloperDirectory + L".\n";
    }
    if (!iterator->credentialProfileSummary.empty()) {
        details += L"Credential profile: " + iterator->credentialProfileSummary + L".\n";
    }
    if (!iterator->artifactPath.empty()) {
        details += L"Artifact: " + iterator->artifactPath + L".\n";
    }
    if (!iterator->targetPath.empty()) {
        details += L"Target: " + iterator->targetPath + L".\n";
    }
    if (!iterator->workingDirectory.empty()) {
        details += L"Working directory: " + iterator->workingDirectory + L".\n";
    }
    if (!iterator->queuedAtUtc.empty() || !iterator->startedAtUtc.empty() || !iterator->completedAtUtc.empty()) {
        details += L"Timeline: queued=";
        details += iterator->queuedAtUtc.empty() ? L"n/a" : iterator->queuedAtUtc;
        details += L", started=";
        details += iterator->startedAtUtc.empty() ? L"n/a" : iterator->startedAtUtc;
        details += L", completed=";
        details += iterator->completedAtUtc.empty() ? L"n/a" : iterator->completedAtUtc;
        details += L".\n";
    }
    if (!iterator->requestOptions.empty()) {
        std::vector<std::wstring> visibleOptions;
        visibleOptions.reserve(iterator->requestOptions.size());
        for (const auto& [key, value] : iterator->requestOptions) {
            if (std::find(iterator->redactedRequestOptionKeys.begin(), iterator->redactedRequestOptionKeys.end(), key) != iterator->redactedRequestOptionKeys.end()) {
                continue;
            }
            visibleOptions.push_back(key + L"=" + value);
        }
        if (!visibleOptions.empty()) {
            details += L"Request options: " + joinValues(visibleOptions, L" | ") + L".\n";
        }
    }
    if (!iterator->readinessIssues.empty()) {
        details += L"Readiness gaps: " + joinValues(iterator->readinessIssues, L"; ") + L".\n";
    }
    if (!iterator->redactedRequestOptionKeys.empty()) {
        details += L"Redacted keys: " + joinValues(iterator->redactedRequestOptionKeys) + L". Replay may require host defaults or fresh credentials.\n";
    }
    if (!iterator->diagnosticSummary.empty()) {
        details += L"Diagnostics: " + iterator->diagnosticSummary + L".";
    }
    AppleOperationDetailsText().Text(winrt::hstring(details));
}

winrt::Windows::Foundation::IAsyncAction CommandLogicUnitSectionControl::RetryAttentionAppleOperationsAsync() {
    if (runtime_ == nullptr) {
        AppleOperationStatusText().Text(L"Apple operation replay is unavailable until the shell runtime is attached.");
        co_return;
    }

    std::vector<::MasterControlShell::ShellAppleOperationRecord> candidates;
    for (const auto& operation : appleOperations_) {
        if (isAttentionAppleOperation(operation) && operation.rerunReady) {
            candidates.push_back(operation);
        }
    }

    if (candidates.empty()) {
        AppleOperationStatusText().Text(L"No failed or blocked Apple operations are currently safe to rerun.");
        UpdateOperationState();
        co_return;
    }

    RetryAttentionAppleOperationsButton().IsEnabled(false);
    RerunAppleOperationButton().IsEnabled(false);
    AppleOperationStatusText().Text(L"Retrying attention Apple operations through CLU.");

    size_t succeededCount = 0;
    size_t failedCount = 0;
    std::wstring lastMessage;
    winrt::apartment_context uiThread;
    co_await winrt::resume_background();
    for (const auto& operation : candidates) {
        auto requestOptions = operation.requestOptions;
        if (!operation.hostId.empty()) {
            requestOptions.insert_or_assign(L"hostId", operation.hostId);
        }
        const auto result = runtime_->ExecuteGovernanceTool(operation.platform, operation.toolId, operation.targetPath, requestOptions);
        if (result.succeeded) {
            ++succeededCount;
        } else {
            ++failedCount;
        }
        lastMessage = result.message;
    }
    co_await uiThread;

    std::wstring message =
        L"Retried " + std::to_wstring(candidates.size()) +
        L" Apple operations. Succeeded: " + std::to_wstring(succeededCount) +
        L". Remaining attention: " + std::to_wstring(failedCount) + L".";
    if (!lastMessage.empty()) {
        message += L" Last result: " + lastMessage;
    }
    AppleOperationStatusText().Text(winrt::hstring(message));
    if (refreshRequested_) {
        refreshRequested_();
    }
    UpdateOperationState();
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
    if (!iterator->rerunReady) {
        AppleOperationStatusText().Text(
            iterator->rerunReadinessMessage.empty()
                ? L"This Apple operation is not ready for a safe rerun yet."
                : winrt::hstring(iterator->rerunReadinessMessage));
        UpdateOperationState();
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
