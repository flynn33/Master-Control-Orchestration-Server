// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#include "pch.h"
#include "SetupWizardBuilder.h"

#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;

namespace MasterControlShell {

namespace {

// -----------------------------------------------------------------------
// Style helpers — look up app-level styles by key.
// Wrapped in try/catch so a missing style doesn't crash the wizard.
// -----------------------------------------------------------------------

Style tryStyle(const wchar_t* key) {
    try {
        return Application::Current().Resources().Lookup(box_value(key)).try_as<Style>();
    } catch (...) {
        return nullptr;
    }
}

TextBlock makeEyebrow(const std::wstring& text) {
    TextBlock block;
    block.Text(winrt::hstring(text));
    block.Style(tryStyle(L"ShellEyebrowTextStyle"));
    return block;
}

TextBlock makeTitle(const std::wstring& text) {
    TextBlock block;
    block.Text(winrt::hstring(text));
    block.Style(tryStyle(L"ShellSectionTitleTextStyle"));
    return block;
}

TextBlock makeBody(const std::wstring& text) {
    TextBlock block;
    block.Text(winrt::hstring(text));
    block.Style(tryStyle(L"ShellBodyTextStyle"));
    block.TextWrapping(TextWrapping::WrapWholeWords);
    return block;
}

Button makeButton(const std::wstring& label, const wchar_t* styleKey = L"ShellCommandButtonStyle") {
    Button button;
    button.Content(box_value(winrt::hstring(label)));
    button.Style(tryStyle(styleKey));
    return button;
}

Border makeCard() {
    Border border;
    border.Style(tryStyle(L"ShellCardStyle"));
    return border;
}

// Build a clickable entry card for the wizard's three-mode selection.
FrameworkElement buildEntryCard(
    const std::wstring& eyebrow,
    const std::wstring& title,
    const std::wstring& description,
    const std::function<void()>& onClick) {

    StackPanel stack;
    stack.Spacing(8);
    stack.Children().Append(makeEyebrow(eyebrow));
    stack.Children().Append(makeTitle(title));
    stack.Children().Append(makeBody(description));

    Button card = makeButton(L"");
    card.Padding(ThicknessHelper::FromLengths(16, 14, 16, 14));
    card.HorizontalAlignment(HorizontalAlignment::Stretch);
    card.HorizontalContentAlignment(HorizontalAlignment::Stretch);
    card.Content(stack);
    if (onClick) {
        card.Click([onClick](auto&&, auto&&) { onClick(); });
    }
    return card;
}

// Build a readiness tile.
FrameworkElement buildReadinessTile(
    const std::wstring& label,
    int ready,
    int missing,
    const std::wstring& fixAction,
    const std::function<void(const std::wstring&)>& onFix) {

    const bool allReady = (ready > 0 && missing == 0);
    const bool noneReady = (ready == 0);

    StackPanel stack;
    stack.Spacing(6);
    stack.Children().Append(makeEyebrow(label));

    TextBlock count;
    count.Text(winrt::hstring(std::to_wstring(ready) + L" / " + std::to_wstring(ready + missing)));
    count.Style(tryStyle(L"ShellSectionTitleTextStyle"));
    stack.Children().Append(count);

    std::wstring stateLabel = allReady ? L"Ready" : (noneReady ? L"Missing" : L"Needs Attention");
    stack.Children().Append(makeBody(stateLabel));

    if (!allReady && !fixAction.empty() && onFix) {
        Button fix = makeButton(L"Fix now");
        fix.Click([onFix, fixAction](auto&&, auto&&) { onFix(fixAction); });
        stack.Children().Append(fix);
    }

    Border tile = makeCard();
    tile.Child(stack);
    return tile;
}

} // namespace

// -----------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------

FrameworkElement BuildSetupWizardEntryView(
    const ShellSnapshot& snapshot,
    const SetupWizardCallbacks& callbacks) {

    StackPanel root;
    root.Spacing(16);

    // Welcome header
    root.Children().Append(makeEyebrow(L"WELCOME"));
    root.Children().Append(makeTitle(L"Start Here"));
    root.Children().Append(makeBody(
        L"Choose how you want to set up Master Control. All three paths lead to "
        L"the same outcome \u2014 a configured, ready orchestration instance."));

    // Three entry cards in a vertical stack (simpler than a grid, always works)
    root.Children().Append(buildEntryCard(
        L"GUIDED",
        L"Guided Setup",
        L"Step-by-step assistant. Connect providers, add MCP servers, "
        L"create specialists, pick a starter workflow, and review readiness.",
        [callbacks]() {
            // Route to the providers section with the guided connect workflow.
            if (callbacks.startGuidedWorkflow) {
                callbacks.startGuidedWorkflow(L"connect-model");
            }
        }));

    root.Children().Append(buildEntryCard(
        L"MANUAL",
        L"Manual Setup",
        L"Go straight to the full operator surface and configure each section "
        L"yourself. Open Setup Readiness when done to review.",
        [callbacks]() {
            if (callbacks.navigateToDestination) {
                callbacks.navigateToDestination(L"overview");
            }
        }));

    root.Children().Append(buildEntryCard(
        L"IMPORT",
        L"Import Existing Configuration",
        L"Restore from an existing package, repository, or zip bundle. "
        L"We validate it, surface any gaps, and route you to fix them.",
        [callbacks]() {
            if (callbacks.startGuidedWorkflow) {
                callbacks.startGuidedWorkflow(L"guided-import");
            }
        }));

    // Wrap in a card border
    Border outer = makeCard();
    outer.Child(root);
    return outer;
}

FrameworkElement BuildSetupReadinessView(
    const ShellSnapshot& snapshot,
    const SetupWizardCallbacks& callbacks) {

    StackPanel root;
    root.Spacing(16);

    const bool firstRunCompleted = snapshot.firstRunCompleted;
    root.Children().Append(makeEyebrow(L"SETUP READINESS"));
    root.Children().Append(makeTitle(
        firstRunCompleted ? L"Setup Complete" : L"Review and Complete Setup"));

    // Count readiness from the snapshot data. Shell types don't carry
    // isTemplate, so we use credentialsConfigured+enabled as the ready
    // signal for providers. MCP readiness uses the online status.
    int providersReady = 0, providersMissing = 0;
    for (const auto& provider : snapshot.providers) {
        if (provider.credentialsConfigured && provider.enabled) {
            ++providersReady;
        } else {
            ++providersMissing;
        }
    }

    int mcpReady = 0, mcpMissing = 0;
    for (const auto& endpoint : snapshot.endpoints) {
        const auto& kindStr = endpoint.kind;
        if (kindStr == L"mcp_server" || kindStr == L"MCP_Server" || kindStr == L"MCPServer") {
            if (endpoint.status == L"online" || endpoint.status == L"Online") { ++mcpReady; }
            else { ++mcpMissing; }
        }
    }

    auto fixHandler = [callbacks](const std::wstring& destination) {
        if (callbacks.navigateToDestination) {
            callbacks.navigateToDestination(destination);
        }
    };

    // Readiness tiles
    StackPanel tiles;
    tiles.Spacing(8);
    tiles.Children().Append(buildReadinessTile(L"Providers", providersReady, providersMissing, L"providers", fixHandler));
    tiles.Children().Append(buildReadinessTile(L"MCP Servers", mcpReady, mcpMissing, L"runtime", fixHandler));
    root.Children().Append(tiles);

    // Complete / Reset buttons
    StackPanel buttonRow;
    buttonRow.Orientation(Orientation::Horizontal);
    buttonRow.Spacing(8);

    if (!firstRunCompleted) {
        Button completeBtn = makeButton(L"Mark Setup Complete");
        completeBtn.Click([callbacks](auto&&, auto&&) {
            if (callbacks.refreshData) {
                // The actual POST to /api/setup/complete is done via the ShellRuntime.
                // For now, trigger a refresh which will show the updated state.
                callbacks.refreshData();
            }
        });
        buttonRow.Children().Append(completeBtn);
    }

    root.Children().Append(buttonRow);

    Border outer = makeCard();
    outer.Child(root);
    return outer;
}

} // namespace MasterControlShell
