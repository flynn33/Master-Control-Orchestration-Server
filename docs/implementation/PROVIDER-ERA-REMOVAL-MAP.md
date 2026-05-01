# Provider-Era Removal Map (PHASE-00 Baseline)

This map snapshots the state of the provider-era code removal that ADR-001 §1 declared. PHASE-00 is docs-only; nothing here is being removed in this phase. The map exists so PHASE-01 can act with precision and so future phases can verify regressions against a forbidden-contract list.

Snapshot date: 2026-04-30
Scope: `master-control-dashboard-main` working tree, post-overlay-install commit `1c5d986`.

## Summary

| Category | Status | Evidence |
|---|---|---|
| Runtime provider modules and routes | Removed | Static grep returns 0 matches in `src/MasterControlApp/MasterControlRuntime.cpp` and `src/MasterControlModules/MasterControlModules.cpp` for `Provider*Module`, `ProviderRegistry`, `AutoConnect`, `/api/providers`, `executeClaudeCodeCli`, `executeCodexCli`, `executeOpenAICompatibleChat`. |
| Module manifests | Removed | No JSON manifest under `src/MasterControlModules/Resources/ForsettiManifests/` references any `*ProviderModule`. |
| Tests | Removed | Static grep for the same set returns 0 matches in `tests/MasterControlOrchestrationServerTests.cpp`. |
| Browser UI provider destination | Removed | No `Providers` destination remains as a navigable view; sign-in cards are gone. |
| Shell `ProvidersSectionControl` | **Residual / deferred-cleanup** | `src/MasterControlShell/MainWindow.xaml.cpp` and `src/MasterControlShell/ShellRuntime.cpp` still reference `ProvidersSectionControl`, `ShellProviderConnection`, and `/api/providers`. |
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

## Residual / deferred-cleanup surface (NOT for removal in PHASE-00)

These references survive in the WinUI shell. ADR-001 acknowledged the shell would be in a deferred-cleanup state. PHASE-01 may remove them as part of "remove or quarantine direct AI-provider execution paths"; PHASE-09 reskins the shell/dashboard. PHASE-00 only catalogs them.

### `src/MasterControlShell/MainWindow.xaml.cpp`
- Line 19: `#include "ProviderAssignmentOptions.h"` — header file does not exist on disk
- Line 20: `#include "ProvidersSectionControl.xaml.h"` — header file does not exist on disk
- Lines 49, 61, 70, 77, 194, 222–223, 250–251, 272–273, 296, 331, 428–430, 486–488, 1187–1188, 1232: `kProvidersDestination`, `kProvidersView`, `ProvidersSectionControl` navigation/binding
- Line 251: status-bar copy referencing "ChatGPT, Codex, Claude Code, and Grok" assignment
- Lines 599, 619: `GuidedProviderAssignmentWizardButton_Click`, `GuidedProviderExecutionWizardButton_Click`
- Lines 642–644, 657–659, 667–669, 692–694: guided-workflow status text routing operators to the providers surface
- Lines 887, 998, 1300: residual comments referencing the providers surface

### `src/MasterControlShell/ShellRuntime.cpp`
- Line 1093: `providerConnectionRow(const ShellProviderConnection&)` — `ShellProviderConnection` type is undefined in the current source tree
- Lines 1107, 1117–1119, 1127, 1138, 1151: row formatters for capability/assignment/execution rows
- Lines 1487–1504: `postProviderToAdminApi` posts to `L"/api/providers"`, which the runtime no longer serves
- Lines 1665–1672, 1825–1835: shell-side state vectors of `ShellProvider*` types
- Lines 2391, 2405: `L"/api/providers/groups"`, `L"/api/providers/groups/remove"` calls

### Project wiring
- `src/MasterControlShell/MasterControlShell.vcxproj` and `src/MasterControlShell/Project.idl` do **not** reference any `Provider*` files. The `#include`s in `MainWindow.xaml.cpp` for files that do not exist on disk imply the shell does not compile in its current state, consistent with ADR-001's "deferred-cleanup" framing.

## Historical artifacts (retained — do not modify)

- `CHANGELOG.md` lines 437–460, 537, 560, 572, 582–583, 594–595, 598, 687: version-history entries describing the provider-era removal.
- `VERSION.json` lines 17, 100, 115, 157, 170–171, 185–186, 189, 291: provider-era diary entries.
- `docs/wiki/Versions.md` lines 35–36, 116–119, 162: removed-surfaces summary.
- `plans/PROOF-OF-WORKING/03-command-stream.md`, `08-starter-workflows.md`, `09-sub-agents.md`, `11-lan-client-end-to-end.md`, `17-soak-stability.md`, `plans/API-PROBE-RESULTS.md`, `plans/FEATURE-AUDIT.md`, `plans/PROOF-OF-WORKING.md`, `plans/remediation/01-gut-and-rebuild.md`, `plans/remediation/02-removal-inventory.md`: pre-removal planning evidence.
- `docs/wiki/Architecture-Decisions/ADR-001-lan-client-control-plane.md`: the ADR that locked the removal decision.

## Verification commands

The static greps below MUST return zero matches in `src/MasterControlApp/`, `src/MasterControlModules/`, and `tests/`. They MAY return matches in `src/MasterControlShell/`, `CHANGELOG.md`, `VERSION.json`, `docs/wiki/Versions.md`, `docs/wiki/Architecture-Decisions/ADR-001-*.md`, and `plans/`. See `docs/implementation/FORBIDDEN-CONTRACT-GREP-LIST.md` for the canonical list.

```bash
# Allowed: runtime + modules + tests must be clean
git grep -nE 'ProviderIntegrationModule|CodexProviderModule|ClaudeCodeProviderModule|XAIProviderModule' -- src/MasterControlApp src/MasterControlModules tests
git grep -nE 'IProviderRegistry|IProviderCatalogService|IProviderCredentialStore|IProviderAssignmentService|IProviderExecutionCatalogService|IProviderExecutionService' -- src/MasterControlApp src/MasterControlModules tests
git grep -nE 'executeClaudeCodeCli|executeCodexCli|executeOpenAICompatibleChat' -- src/MasterControlApp src/MasterControlModules tests
git grep -nE '/api/providers' -- src/MasterControlApp src/MasterControlModules tests
git grep -nE '/api/provider-assignment-targets' -- src/MasterControlApp src/MasterControlModules tests
git grep -nE 'AutoConnect[A-Z]' -- src/MasterControlApp src/MasterControlModules tests
```

PHASE-01 will extend this list to the shell once `ProvidersSectionControl` is removed.
