# Provider-Era Removal Map (PHASE-00 Baseline)

This map snapshots the state of the provider-era code removal that ADR-001 §1 declared. PHASE-00 is docs-only; nothing here is being removed in this phase. The map exists so PHASE-01 can act with precision and so future phases can verify regressions against a forbidden-contract list.

Snapshot date: 2026-04-30 (PHASE-00) · updated 2026-05-01 (PHASE-01)
Scope: `master-control-dashboard-main` working tree, post-overlay-install commit `1c5d986`. PHASE-01 closed the residual shell surface.

## Summary

| Category | Status | Evidence |
|---|---|---|
| Runtime provider modules and routes | Removed | Static grep returns 0 matches in `src/MasterControlApp/MasterControlRuntime.cpp` and `src/MasterControlModules/MasterControlModules.cpp` for `Provider*Module`, `ProviderRegistry`, `AutoConnect`, `/api/providers`, `executeClaudeCodeCli`, `executeCodexCli`, `executeOpenAICompatibleChat`. |
| Module manifests | Removed | No JSON manifest under `src/MasterControlModules/Resources/ForsettiManifests/` references any `*ProviderModule`. |
| Tests | Removed | Static grep for the same set returns 0 matches in `tests/MasterControlOrchestrationServerTests.cpp`. |
| Browser UI provider destination | Removed | No `Providers` destination remains as a navigable view; sign-in cards are gone. |
| Shell `ProvidersSectionControl` | **Removed (PHASE-01)** | All references in `src/MasterControlShell/` deleted. Build green; ctest 4/4 pass. See "PHASE-01 resolution" section below. |
| Historical artifacts | Retained intentionally | `CHANGELOG.md`, `VERSION.json` history, and `docs/wiki/Versions.md` document the removal — these stay. |

## Confirmed-removed surface (per ADR-001 §1, evidenced by grep)

The following classes, types, and routes are no longer present in the runtime or modules. Re-introduction is forbidden by ADR-002 §1 and by the global checklist in `handoff/realignment/CHECKLIST.md`.

### Forsetti modules
- `ProviderIntegrationModule`
- `CodexProviderModule`
- `ClaudeCodeProviderModule`
- `XAIProviderModule`

### Provider services
- `IProviderRegistry`, `IProviderCatalogService`, `IProviderCredentialStore`, `IProviderAssignmentService`, `IProviderExecutionCatalogService`, `IProviderExecutionService`
- `ProviderCatalogService`, `ProviderRegistryService`, `ProviderCredentialStore`, `ProviderCliSignInService`, `ProviderAssignmentService`, `ProviderExecutionCatalogService`, `ProviderExecutionService`

### Outbound AI transports
- `executeClaudeCodeCli`
- `executeCodexCli`
- `executeOpenAICompatibleChat`

### HTTP routes (all `/api/providers/*`)
- `GET/POST /api/providers`
- `/api/providers/credentials`
- `/api/providers/auto-connect`
- `/api/providers/signin/{register,start,status,installed}`
- `/api/providers/groups`, `/api/providers/groups/remove`
- `/api/providers/assignments`
- `/api/providers/execute`
- `/api/provider-assignment-targets`

### Data types
- `ProviderConnection`, `ProviderAssignment`, `ProviderExecutionRegistration`, `ProviderExecutionRecord`, `ProviderCapability`, `ProviderCredentialStatus`, `ProviderAssignmentTarget`
- `AutoConnectResult` and the rest of the `AutoConnect*` family

### Browser surfaces
- `Providers` browser destination
- `renderSignInCards` API-key path tied to `/api/providers/auto-connect`

### Forsetti governance action kinds
- `ProviderExecution`
- `ProviderAutonomyEnable`

### Activity event kinds
- `ProviderExecution`
- `AutoConnect`

## PHASE-01 resolution (2026-05-01)

PHASE-01 surgically removed every residual provider reference from the WinUI shell, the Telemetry/CommandLogicUnit section controls, the SetupWizardBuilder, and the broken initializer-list call sites in `MasterControlRuntime.cpp` that the build had stalled on. The shell now compiles end-to-end and the test suite passes.

Files modified in PHASE-01:

| File | What was removed/fixed |
|---|---|
| `src/MasterControlShell/MainWindow.xaml.h` | 5 declarations: `GuidedProviderAssignmentWizardButton_Click`, `GuidedProviderExecutionWizardButton_Click`, `ShowProviderWizardAsync`, `ShowProviderAssignmentWizardAsync`, `ShowProviderExecutionWizardAsync` |
| `src/MasterControlShell/MainWindow.xaml.cpp` | Both `#include`s for missing provider headers; `kProvidersDestination`/`kProvidersView` constants; provider entries in `isInteractiveFormSection`/`isInteractiveDestination`/`destinationLabelFor`/`destinationForViewId`/`metadataForDestination`/`guidedFollowThroughForDestination`/`bootstrapNavigationPointers`/`bootstrapViewInjectionsBySlot`; `ProvidersSectionControl` AttachRuntime/ApplySnapshot/CreateView branches; the two Click handlers; the `connect-model`, `assign-responsibility`, `guided-provider-execution` workflow branches in `StartGuidedWorkflow`; `ProviderCountText` and `snapshot.providerCount` references in `ApplyHeroSnapshot` and the runtime ledger; `kProvidersDestination` redirect in the sub-agent-group wizard completion (now points at `kRuntimeDestination`); `ShowProviderWizardAsync` (308 lines), `ShowProviderAssignmentWizardAsync` (198 lines), `ShowProviderExecutionWizardAsync` (236 lines); `currentSnapshot_.providerAssignmentTargets` loop in `ShowSubAgentGroupWizardAsync` rebound to `currentSnapshot_.endpoints`; comment cleanup |
| `src/MasterControlShell/MainWindow.xaml` | Subtitle/description copy referencing "provider routing"; `GuidedProviderAssignmentWizardButton`; `GuidedProviderExecutionWizardButton`; PROVIDERS metric tile (`ProviderCountText`); RowDefinitions/ColumnDefinitions compressed; "NEXUS provider routing" badge subtitle changed to "orchestration"; comment block referencing "provider execution / auto-connect" updated |
| `src/MasterControlShell/ShellRuntime.h` | Already clean by PHASE-00; no edits |
| `src/MasterControlShell/ShellRuntime.cpp` | `LocalCliSignInSession` struct + `gCliSignInMutex`/`gCliSignInSessions`/`gCliSignInCounter` globals; orphan `generateCliSignInSessionId` function; `providerRow`/`providerConnectionRow`/`providerCapabilityRow`/`providerAssignmentRow`/`providerExecutionRegistrationRow`/`providerExecutionHistoryRow` helpers; `postProviderToAdminApi`; `ShellProvider*` state vectors; `providerFromJson` + all related `provider*FromJson` callers in dashboard loading; `providers`/`providerCapabilities`/`providerCredentialStatuses`/`providerAssignmentTargets`/`providerAssignments`/`providerExecutionRegistrations`/`providerExecutionHistory` state; `providerRows`/`providerCapabilityRows`/`providerAssignmentRows`/`providerExecutionRegistrationRows`/`providerExecutionHistoryRows`; `snapshot.providerCount` setter; default-empty provider row messages; `snapshot.provider*` assignments; `/api/providers/groups[/remove]` URLs replaced with `/api/runtime/subagent-groups[/remove]` for `RemoveSubAgentGroup`; `providerCapabilities_` references in WinHTTP comments updated |
| `src/MasterControlShell/TelemetrySectionControl.xaml` | PROVIDERS metric tile + `ProviderCountText`/`ProviderCountDetailText`; remaining tiles renumbered to 3 cols × 2 rows |
| `src/MasterControlShell/TelemetrySectionControl.xaml.cpp` | `ProviderCountText`/`ProviderCountDetailText` text setters; `Providers:` line in `telemetryRouting` summary; "Provider and Apple traffic budget" copy updated |
| `src/MasterControlShell/CommandLogicUnitSectionControl.xaml` | "Connect AI Model" (`Tag="connect-model"`) and "Assign Responsibility" (`Tag="assign-responsibility"`) CLU quick-action buttons; grid compressed from 3×3 to 2×3 |
| `src/MasterControlShell/CommandLogicUnitSectionControl.xaml.cpp` | "Connected models … Responsibility lanes …" parts of `CluActionSummaryText` |
| `src/MasterControlShell/ShellFormatting.h` | `formatProvidersNarrative` function (orphaned after `snapshot.providerCount` removal) |
| `src/MasterControlShell/SetupWizardBuilder.cpp` | "Connect providers" copy updated; routing changed from `connect-model` workflow to `new-mcp` workflow; provider readiness counting code removed; `Providers` readiness tile removed |
| `src/MasterControlApp/MasterControlRuntime.cpp` | Three `GovernanceEnforcementRequest{...}` initializer lists at `installPackageJson`/`installRepoJson`/`installZipJson` had a spurious `{}` placeholder making the type cast fail. Removed the extra placeholder; struct now matches its `{action, targetId, source, allowUntrustedExecution, [actor]}` layout. |

Validation:

| Command | Result |
|---|---|
| `cmake --preset debug` | configure succeeded (vcpkg restored 3 packages; nlohmann-json 3.12.0 found) |
| `cmake --build --preset debug` | **succeeded** — 0 errors, 1 warning (`SetupWizardBuilder.cpp(133,26): C4100 'snapshot': unreferenced parameter` — pre-existing) |
| `ctest --preset debug --output-on-failure` | **4/4 passed** — `ForsettiCoreTests`, `ForsettiPlatformTests`, `ForsettiArchitectureTests`, `MasterControlOrchestrationServerTests` |
| Static grep across `src/` + `resources/web` | zero matches for any of the forbidden provider patterns |

The shell binary `MasterControlShell.exe` and the bootstrapper/launcher binaries all link successfully. The installer (`MasterControlOrchestrationServer.wxs`) can now find a real `MasterControlShell.exe` to package.

## Historical artifacts (retained — do not modify)

- `CHANGELOG.md` lines 437–460, 537, 560, 572, 582–583, 594–595, 598, 687: version-history entries describing the provider-era removal.
- `VERSION.json` lines 17, 100, 115, 157, 170–171, 185–186, 189, 291: provider-era diary entries.
- `docs/wiki/Versions.md` lines 35–36, 116–119, 162: removed-surfaces summary.
- `plans/PROOF-OF-WORKING/03-command-stream.md`, `08-starter-workflows.md`, `09-sub-agents.md`, `11-lan-client-end-to-end.md`, `17-soak-stability.md`, `plans/API-PROBE-RESULTS.md`, `plans/FEATURE-AUDIT.md`, `plans/PROOF-OF-WORKING.md`, `plans/remediation/01-gut-and-rebuild.md`, `plans/remediation/02-removal-inventory.md`: pre-removal planning evidence.
- `docs/wiki/Architecture-Decisions/ADR-001-lan-client-control-plane.md`: the ADR that locked the removal decision.

## Verification commands (post-PHASE-01)

After PHASE-01 the static greps below MUST return zero matches across the entire `src/` tree, `resources/web`, `tests`, and `include`. Allowed-match scopes shrink to history-only: `CHANGELOG.md`, `VERSION.json`, `docs/wiki/Versions.md`, ADR-001/ADR-002, `docs/implementation/PROVIDER-ERA-REMOVAL-MAP.md` (this file), and `plans/`. See `docs/implementation/FORBIDDEN-CONTRACT-GREP-LIST.md` for the canonical list.

```bash
git grep -nE 'ProviderIntegrationModule|CodexProviderModule|ClaudeCodeProviderModule|XAIProviderModule' -- src/ tests resources/web include
git grep -nE 'IProviderRegistry|IProviderCatalogService|IProviderCredentialStore|IProviderAssignmentService|IProviderExecutionCatalogService|IProviderExecutionService' -- src/ tests resources/web include
git grep -nE 'executeClaudeCodeCli|executeCodexCli|executeOpenAICompatibleChat' -- src/ tests resources/web include
git grep -nE '/api/providers' -- src/ tests resources/web
git grep -nE '/api/provider-assignment-targets' -- src/ tests resources/web
git grep -nE 'AutoConnect[A-Z]' -- src/ tests resources/web include
git grep -nE 'ShellProvider|ProvidersSectionControl|ProviderAssignmentOptions|GuidedProviderAssignmentWizard|GuidedProviderExecutionWizard|kProvidersDestination|kProvidersView|ProviderCountText|ProviderCountDetailText' -- src/ resources/web
git grep -nE '\bconnect-model\b|\bassign-responsibility\b|\bguided-provider-execution\b' -- src/ resources/web
```

All eight greps return empty in the post-PHASE-01 working tree.
