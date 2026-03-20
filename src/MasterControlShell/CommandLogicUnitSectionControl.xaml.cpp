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

void CommandLogicUnitSectionControl::ApplySnapshot(const ::MasterControlShell::ShellSnapshot& snapshot) {
    CluPostureText().Text(winrt::hstring(snapshot.governancePosture.empty() ? L"Pending" : snapshot.governancePosture));
    CluFindingCountText().Text(winrt::hstring(std::to_wstring(snapshot.governanceFindingCount) + L" findings"));
    CluRoleCountText().Text(winrt::hstring(std::to_wstring(snapshot.governanceRoleCount)));
    CluRuleCountText().Text(winrt::hstring(std::to_wstring(snapshot.governanceRuleCount)));
    CluDocumentCountText().Text(winrt::hstring(std::to_wstring(snapshot.governanceDocumentCount)));
    CluEvaluatedText().Text(winrt::hstring(L"Last evaluated: " + snapshot.governanceLastEvaluatedUtc));
    CluNarrativeText().Text(winrt::hstring(snapshot.governanceNarrative));
    CluDoctrineText().Text(winrt::hstring(snapshot.governanceDoctrine));

    populateListView(CluActionsListView(), snapshot.governanceActionRows);
    populateListView(CluFindingsListView(), snapshot.governanceFindingRows);
    populateListView(CluRolesListView(), snapshot.governanceRoleRows);
    populateListView(CluRulesListView(), snapshot.governanceRuleRows);
    populateListView(CluDocumentsListView(), snapshot.governanceDocumentRows);
}

} // namespace winrt::MasterControlShell::implementation
