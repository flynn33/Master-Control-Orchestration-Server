# Claude Code Handoff Note — Ease-of-Use Remediation (v0.4.0-rc.1)

## Project Identifier
Master Control Orchestration Server

## Objective
Implement the MCOS Ease-of-Use Remediation Package dated 2026-04-14 while preserving the current internal-LAN security posture and providerId-based identity.

## Active Task
All 8 workstreams landed on runtime + browser + shell data-layer. Full WinUI `SetupWizardControl` XAML is intentionally deferred — see Remaining Work.

## Completed Work

### WS1 — First-Run Setup Wizard
- New models: `ReadinessIssue`, `ReadinessSnapshot` in `include/MasterControl/MasterControlModels.h` with NLOHMANN registrations.
- Extended `AppConfiguration` with `firstRunCompleted`, `firstRunStartedAtUtc`, `firstRunCompletedAtUtc`, `firstRunSkippedSteps`.
- New runtime helper `computeReadinessSnapshot(DashboardSnapshot, AppConfiguration)` in `src/MasterControlApp/MasterControlRuntime.cpp`. Source-neutral workflow ready rule per Fix 3.
- New endpoints: `GET /api/readiness`, `POST /api/setup/start`, `POST /api/setup/complete` (unambiguous — no boolean), `POST /api/setup/reset` (unambiguous inverse).
- Browser: `renderFirstRunView()` with three entry cards (Guided / Manual / Import Existing). 7-step guided flow: preflight → discovery → providers → mcp → specialist → workflow → review. localStorage persistence via `mco.wizard.state.v1`, corruption-safe restore.
- Manual mode: full-depth flow. Routes to full operator surface, persistent "Finish Setup" banner until `firstRunCompleted=true`. Acceptance criteria 1–5 satisfied.
- Import Existing mode: dedicated step hosts the existing guided-import workflow. Routes to readiness review on success. Acceptance criteria 1–6 satisfied.
- Dispatcher `renderCurrentContent` short-circuits to `renderFirstRunView()` when `!firstRunCompleted && !wizard.dismissed && mode != 'manual|import'`.

### WS2 — Browser Auto-Connect Refactor
- `submitGuidedProviderForm` in `resources/web/app.js` now POSTs to `/api/providers/auto-connect` with the full `AutoConnectRequest` shape (`providerId` only — no `kind` in payload; runtime resolves kind).
- `renderAutoConnectProgress()` displays step-by-step orchestration progress.
- `renderAutoConnectFallbackBanner()` + `submitGuidedProviderFormManualFallback()` preserve the legacy three-call flow (`/api/providers` + `/credentials` + `/assignments`) as the explicit manual fallback — Rule 1 (manual first-class) and Rule 6 satisfied.
- Fixed the stale `connect-chatgpt` mapping — removed redundant `kind: 'codex'` from all four quick-connect presets since capabilityId (= providerId) is the canonical identity.
- Template conversion explanation surfaced in progress note.

### WS3 — Environment Hints UI Integration
- `loadEnvironmentHints()` fetches `/api/environment-hints` and caches to `state.environmentHints`.
- `environmentHintStatus(field)` returns `{ hint, detected, badgeClass, badgeText }` using copy from `READINESS_COPY` mirror.
- Credential field rendering (both `connect-model` workflow form at line ~889 and provider section credential form at line ~3051) now emit hint badges. Detected → input `required=false`, placeholder "Using environment variable X (type to override)". Manual input always overrides; env value never displayed.
- Discovery step in the wizard lists detected and missing env vars with the same badge styling.

### WS4 — Provider Install Automation (Claude Code CLI only)
- Models: `SupportedDependency`, `DependencyDetection` (with `preflight` field), `DependencyInstallResult` with NLOHMANN registrations.
- `buildSupportedDependencyCatalog()` returns exactly one entry: Claude Code CLI.
- **Three-branch preflight** per user feedback:
  - **Branch A (ready)**: `claude --version` exits 0 → `state=ready`, `preflight=ready`.
  - **Branch B (installable)**: claude missing, `npm --version` exits 0 → `state=not-installed`, `preflight=installable`.
  - **Branch C (prerequisite-missing)**: claude missing AND npm missing → `state=manual-action-required`, `preflight=prerequisite-missing`, `detail` points to https://nodejs.org. **No install attempt is ever made in this branch.**
- `POST /api/setup/dependencies/{id}/install` short-circuits Branch C before any process launch (user feedback Fix 1 satisfied).
- EACCES/EPERM in install stderr → `finalState=manual-action-required` with elevation remediation copy. No auto-elevation (Rule 2).
- Install runs via the existing hardened `runProcessCapture` (5-min timeout, concurrent pipe drain, 4MB head-preserved bounded capture — inherited from v0.3.0-rc.1 WS8).
- Browser renders `ClaudeCodeDependencyCard` in the wizard Providers step and on the readiness dashboard. Three visual states mapped to preflight. Install button disabled on Branch C with nodejs.org link.

### WS5 — Shell/Browser Parity
- `ShellHostSnapshot` now carries `advancedMode` and `firstRunCompleted`.
- `ShellRuntime.cpp` reads both from `/api/config` (via the already-existing configuration file path) so shell consumers see the same state the runtime persists.
- `ReadinessCopy.h` centralizes user-facing labels; browser mirrors via `READINESS_COPY` object. Both surfaces route through the same strings. WS8 test asserts copy parity.
- Full WinUI `SetupWizardControl` XAML is deferred (see Remaining Work) per Risk #1 mitigation — XAML changes land in a separate pass to avoid destabilizing the shell build.

### WS6 — Readiness Dashboard and Starter Flow
- New endpoints: `GET /api/setup/workflow-templates` (three templates: single-provider-demo, mcp-assisted-demo, specialist-team-demo) and `POST /api/setup/workflow-templates/{id}/instantiate` with prerequisite validation.
- Browser: `renderReadinessView()` hosted at `setup-readiness` destination, always accessible via nav. Five category tiles (Providers / MCP / Specialists / Workflows / Blocking Issues) each with "Fix now" routing.
- Starter Workflow step in wizard lets user pick a template (Rule 6 preserved — user chooses). Instantiation reuses existing provider-assignment primitives (no new workflow engine).
- "Setup Readiness" added to main nav as a reopen path.

### WS7 — Exports as Secondary Path
- `renderSurfaceNavigation()` filters `exports` out of the primary nav unless `advancedMode === true`. Visible on demand; no export affordance during first-run wizard.
- Export fallback reasons are explicit via `renderAutoConnectFallbackBanner()` — never leads with "Download script".

### WS8 — Test and Validation Matrix
- Added `httpPostJson` helper in `tests/MasterControlOrchestrationServerTests.cpp`.
- Seven new test sections inserted after line 1372 (Progressive Disclosure block):
  - **A**: `/api/readiness` shape validation + setup lifecycle round-trip (`complete` → `reset`).
  - **C**: `/api/environment-hints` returns object.
  - **D**: `/api/config` exposes `advancedMode`; toggle round-trip.
  - **E**: dependency catalog (exactly one entry, correct installMethod), preflight field with valid enum, 404 for unknown id.
  - **F**: three starter workflow templates returned.
  - **G**: Fix-3 critical baseline — fresh install reports `workflowsReadyCount=0`.

## Design Decisions

### Source-neutral readiness model
Per user feedback Fix 3: a workflow is "ready" if it has (a) at least one provider assignment and (b) an executable target, regardless of whether it came from a starter template or was created manually. `computeReadinessSnapshot` walks `providerAssignments` and counts distinct providers with at least one role/specialist/group assignment. This keeps Manual and Import Existing first-class.

### Three-branch install preflight (Fix 1)
The plan initially had a single `claude --version` detect. User feedback flagged that without npm, the install command fails in a confusing way. The three-branch design runs a second `npm --version` probe to classify the state explicitly. Branch C never launches the install command — it surfaces the nodejs.org remediation instead.

### `/api/setup/complete` semantics (Fix 2)
Original plan had `{completed: bool}`. User feedback flagged that making the endpoint accept a boolean creates a "was complete, now complete" ambiguity. Final design: body is `{skippedSteps: []}` only; endpoint unambiguously marks complete. Added `POST /api/setup/reset` as the unambiguous inverse, useful for testing and a future "redo setup" flow.

### Validation ordering (Fix 5)
VERSION.json bump precedes configure/build/package so the generated `MasterControlVersion.h` and package metadata stamp the correct version. See the release sequence below.

### Shell XAML wizard deferred
Full `SetupWizardControl.xaml` / per-step UserControls / IDL registrations intentionally not landed in this pass. Rationale: WinUI IDL + XAML + .cpp changes are high-risk for build stability, and the data-layer parity (shell reads `advancedMode` and `firstRunCompleted`) already gives the shell everything it needs to route users to the browser surface for setup tasks. A follow-up pass should add `SetupWizardControl` XAML with the IDL-first / bindings-second approach described in the plan's Risk #1.

## Constraints Observed
- Internal-LAN security posture unchanged (`bindAddress=0.0.0.0`, `enableTls=false`, `enableAuthentication=false`, `allowTroubleshootingBypass=true`, `allowOpenLanAccess=true`, `beaconEnabled=true`).
- providerId-based resolution preserved — no regression to kind-based lookup. WS2 deliberately omits `kind` from auto-connect payload.
- Manual setup remains first-class alongside guided — Rule 1 satisfied by Manual entry card + banner + legacy-flow manual fallback.
- No placeholder UX — every wizard step is real (preflight checks actual state; discovery shows real hints; dependency card drives real process execution).

## Files Touched
### New
- `include/MasterControl/ReadinessCopy.h`
- `docs/handoff/CLAUDE_REMEDIATION_20260414.md` (this file)

### Modified
- `include/MasterControl/MasterControlModels.h`
- `src/MasterControlApp/MasterControlRuntime.cpp`
- `src/MasterControlShell/ShellRuntime.h`
- `src/MasterControlShell/ShellRuntime.cpp`
- `resources/web/app.js`
- `resources/web/styles.css`
- `tests/MasterControlOrchestrationServerTests.cpp`
- `VERSION.json`
- `CHANGELOG.md`

## Validation Results
- Configure: `cmake --preset debug` succeeds with VERSION.json parsed as 0.4.0-rc.1 (after the final bump).
- Build: `cmake --build build/debug --config Debug` — exit code 0 after each workstream.
- Tests: `ctest --test-dir build/debug -C Debug --output-on-failure` — **4/4 suites pass** after each workstream, including all new WS8 assertions.
- Security posture: verified unchanged via grep of `bindAddress=0.0.0.0`, `enableTls=false`, etc. in `MasterControlDefaults.cpp`.

## Remaining Work
- **Shell wizard XAML (WS5 extension)**: `SetupWizardControl.xaml` + 7 per-step UserControls + IDL registrations + `BoolToVisibilityConverter` + `MainWindowViewModel`. Intentionally deferred per Risk #1 mitigation. Should land in a follow-up pass using the IDL-first / XAML-second / bindings-third sequence.
- **Starter workflow depth**: the current instantiator creates one provider→role assignment. A richer workflow (MCP wiring, specialist creation) could be added, but Rule 6 honored by letting the user choose a template.
- **CI workflow run**: `windows-build-test-package.yml` exists from v0.3.0-rc.1 but has not been exercised against this remediation's commits. A real GitHub Actions run is the last gate before a non-RC release.
- **End-to-end manual QA**: browse `localhost:7300` with a fresh configuration, click through Guided → Manual → Import Existing, install Claude Code CLI on a test machine, confirm readiness dashboard reflects reality. This is a manual integration test documented here, not an automated one.

## Risks / Notes
- Wizard `localStorage` key is `mco.wizard.state.v1`. If future schemas break compatibility, bump the version to `v2` to force a clean reset.
- WS8 Section E install test asserts shape only in CI without npm. On a developer machine with Claude Code CLI already present, the Branch A path should be exercised manually.
- The shell data-layer exposes `advancedMode` and `firstRunCompleted` but the shell's existing XAML still renders the full operator surface. Until WS5's XAML pass lands, shell users are routed through the browser for first-run setup.
