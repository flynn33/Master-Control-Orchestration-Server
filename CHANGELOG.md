# Changelog

All notable changes to this repository are tracked here by the repository agents.

## [Unreleased]
- Changes pushed to `main` are promoted into the next numbered release automatically.
- **Auto-Connect AI Models.** Add a single-call automated pipeline for adding AI providers: the user only picks a provider kind, enters credentials, and checks one or more role assignments. Everything else is automated — capability resolution, route ID generation (`{providerId}-YYYYMMDD-HHMMSS`), display name, base URL, HTTP connectivity probe + model discovery via `GET {baseUrl}/models` (WinHTTP with `sendJsonRequest`), model selection (preferring `capability.recommendedModel`, falling back to first listed or capability default), DPAPI credential encryption, `ProviderConnection` registration, and multi-target role assignment fan-out. Returns a structured `AutoConnectResult` with per-stage step log, discovered models, applied/failed assignments, and total latency. New authentication header builder is module-agnostic: any credential field id matching `api_key`, `apikey`, `api-key`, `token`, `secret`, or `key` is automatically used as a bearer token. Rollback on failure: if credential storage fails after provider registration, the provider record is removed atomically. (`include/MasterControl/MasterControlModels.h`, `include/MasterControl/MasterControlContracts.h`, `include/MasterControl/MasterControlRuntime.h`, `src/MasterControlApp/MasterControlRuntime.cpp`)
- Add `AutoConnectRequest`, `AutoConnectResult`, `AutoConnectStep`, `DiscoveredModel` data models with `NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT` serializers.
- Extend `IProviderRegistry` with `autoConnectProvider(request)` and add `wireAutoConnectDependencies()` post-construction setter for the credential store + assignment service (via `std::weak_ptr` to avoid the circular ownership loop created by `ProviderCredentialStore` taking a `shared_ptr` to the registry).
- Add `POST /api/providers/auto-connect` admin API route, `AdminApiService::autoConnectProviderJson()`, and `MasterControlApplication::autoConnectProviderJson()` public forwarder. CLU governance enforces `ProviderAutonomyEnable` before the pipeline runs if `allowAutonomousControl=true`.
- `ShellRuntime::AutoConnectProvider()` bridges the shell to the new admin route, parsing the full result JSON (including nested steps array, discovered models array, and both assignments lists) into `ShellAutoConnectProviderResult`. New `postAutoConnectProviderToAdminApi` helper builds the JSON payload using the existing `httpRequest` + `JsonObject` plumbing.
- **Shell UI rework** — `ProvidersSectionControl` "Quick Connect" card is replaced with a fully automated "Auto-Connect AI Model" card:
  - Cyan eyebrow `AUTO-CONNECT · AI MODEL` + hero title "Connect an AI Model" + narrative paragraph explaining the automation.
  - Provider dropdown (unchanged).
  - Two dynamic credential password boxes scoped to the capability's first two credential fields.
  - **New multi-select `ListView`** (`AutoConnectRoleSelector`) replacing the old single-responsibility `ComboBox`. Users can check any combination of roles, sub-agent groups, and sub-agents. Each row shows `[ROLE] / [GROUP] / [AGENT]` eyebrow + display name + description.
  - "Auto-Connect" primary button replaces "Connect Provider"; secondary buttons link to the advanced editor wizards.
  - Rich post-connect status line showing provider display name, total latency in ms, discovered model count, selected model id, and applied role names (with any failures called out).
- `ConnectQuickProviderAsync` completely rewritten: now makes **one** call to `runtime_->AutoConnectProvider()` instead of the three sequential calls (`UpsertProvider` + `UpsertProviderCredentials` + `UpsertProviderAssignment`). Selected target ids are fanned out through the `assignmentTargetIds` vector. The resulting `ShellAutoConnectProviderResult` drives the UI status line directly.
- `selectedQuickConnectResponsibilityTargetId_` (single `std::wstring`) replaced with `selectedAutoConnectRoleTargetIds_` (`std::vector<std::wstring>`). The legacy `QuickConnectResponsibilitySelector_SelectionChanged` handler is retained as a no-op for binary compatibility with the XAML-generated header.
- Validated end-to-end on Windows Server 2022 Datacenter: cmake build 0 errors, ctest 4/4 green, live HTTP POST to `/api/providers/auto-connect` with a fake OpenAI API key returned `HTTP 200, succeeded: true, totalLatencyMs: 218` and produced a structured step log showing `resolve-capability ✓`, `derive-shape ✓`, `validate-credentials ✓`, `discover-models ⚠ (fake 401 from api.openai.com, gracefully fell back)`, `register-provider ✓`, `store-credentials ✓`, `apply-assignments ✓ (2 roles)`. Post-call `/api/config` verified the new provider `chatgpt-20260411-124236` with both `planner` and `auditor` role assignments wired to it.

## [0.2.0] - 2026-04-11
### Summary
Tron-density UX rework of the shell, installer, and browser dashboard, validated end-to-end on Windows Server 2022 Datacenter.

### Installer & Deployment
- Tron-theme the setup launcher progress window: dark `#060A10` background with a 2px cyan `#00F6FF` accent bar, Bahnschrift SemiCondensed eyebrow/header/body fonts, accent-colored marquee bar, per-control text coloring via `WM_CTLCOLORSTATIC`, and owner-allocated GDI resources freed in `destroyProgressWindow`. (`src/MasterControlBootstrapper/setup_main.cpp`)
- Update `PlatformToolset` from `v143` to `v145` so the WinUI 3 shell builds on Visual Studio 2026 MSVC 14.50. (`src/MasterControlShell/MasterControlShell.vcxproj`, `src/MasterControlShell/CMakeLists.txt`)
- Guard `Package-MasterControlOrchestrationServer.ps1` git commit lookups so packaging works outside a git repo (records `non-git` commit markers instead of crashing). (`scripts/Package-MasterControlOrchestrationServer.ps1`)
- Validated end-to-end on Windows Server 2022 Datacenter (21H2, build 20348): cmake configure + build with 0 errors and 0 warnings, ctest 4/4 green (ForsettiCoreTests, ForsettiPlatformTests, ForsettiArchitectureTests, MasterControlOrchestrationServerTests), IDE_PACKAGE staged a 44 MB deployable bundle, unattended install smoke CLEAN, bootstrapper `preflight` and `validate` report `ready: true` / `valid: true`, and `MasterControlShell.exe` launches cleanly and renders the full Tron UI.

### Shell UI/UX
- Expand `src/MasterControlShell/App.xaml` with:
  - New brushes `ShellFocusBrush`, `ShellPressFillBrush`, `ShellSkeletonBrush`, `ShellSuccessSoftBrush`, `ShellWarningSoftBrush`, `ShellDangerSoftBrush`.
  - New styles `ShellStatusChipStyle` / `ShellStatusChipTextStyle` for pill status chips.
  - Tonal button variants `ShellSuccessButtonStyle` / `ShellWarningButtonStyle` / `ShellDangerButtonStyle`.
  - Hero metric typography `ShellMetricLabelTextStyle` / `ShellMetricValueTextStyle`.
  - Loading placeholder `ShellSkeletonBlockStyle`.
  - Dense command-center variants `ShellCompactTileStyle`, `ShellSubAgentBadgeStyle`, `ShellSubAgentBadgeTitleStyle`, `ShellSubAgentBadgeSubtitleStyle`.
  - Monospace live-clock text style `ShellLiveClockTextStyle`.
- Redesign `OverviewSectionControl` around a hero card + pill status chip + operational snapshot + two-column narrative grid + authored-surfaces legend. Status chip now shows short uppercase `ADMIN API ONLINE · SYNCHRONIZED` / `ADMIN API OFFLINE · CACHED STATE` to fit the chip pill. (`src/MasterControlShell/OverviewSectionControl.xaml`, `.xaml.cpp`)
- Add a Tron command-center footer row under the section content host: seven pill badges for **SENTINEL**, **ARCHITECT**, **FORGE**, **SCRIBE**, **RECON**, **NEXUS**, **WATCHDOG** — matching the user's reference guidance. (`src/MasterControlShell/MainWindow.xaml`)
- Add a `LIVE HH:MM:SS` indicator to the main title bar alongside the API and service badges, driven by a new one-second `clockTimer_` DispatcherQueueTimer that runs independently of the 10-second refresh timer. (`src/MasterControlShell/MainWindow.xaml`, `.xaml.h`, `.xaml.cpp`)
- Wrap the main NavigationView content in a vertical `ScrollViewer` so the hero card, 4-metric summary row, section content, and sub-agent pill row all stay reachable on low-resolution displays (tested on 1292×715 RDP). (`src/MasterControlShell/MainWindow.xaml`)

### Browser Dashboard
- Append a Tron polish layer to `resources/web/styles.css`:
  - `@keyframes tron-accent-pulse` (2.8s ease-in-out infinite) applied to `[data-tone="info"]` badges and the connecting health badge.
  - `:focus-visible` cyan outlines with matching glow on buttons, links, role=button, and dialogs.
  - `<dialog>::backdrop` radial gradient + `backdrop-filter: blur(4px)` for the overlay and danger dialogs.
  - Full `@media (prefers-reduced-motion: reduce)` support neutralizing animations, transitions, and the button hover lift.

### Runtime
- Add a one-shot ProgramData migration in `resolveAppPaths()`: when only the legacy `C:\ProgramData\MasterControlProgram` directory exists, attempt to rename it to the canonical `C:\ProgramData\MasterControlOrchestrationServer`. If the rename fails (files in use, permissions), transparently fall back to reading from the legacy path so upgrades never break. (`src/MasterControlApp/MasterControlDefaults.cpp`)

### Repository
- Update all in-repo references to the renamed GitHub project `flynn33/Master-Control-Orchestration-Server` in `README.md`, `docs/wiki/Home.md`, `docs/wiki/_Footer.md`, and `scripts/github_agents/common.py`. Historical handoff notes are left untouched.

### Stabilization Sweep
- Ran a Phase A sweep across `src/` and `include/` (excluding third-party `packages/`): zero TODO/FIXME/HACK/XXX/BUG markers in first-party code.
- Re-read `setup_main.cpp` end-to-end and confirmed the elevation path already uses `ShellExecuteExW` + `runas` verb (no Base64 relay) and `maybeLaunchShell` is fully wired end-to-end.
- Decision recorded and documented: the internal SCM service key `MasterControlProgram` is preserved for upgrade compatibility with pre-0.2 installs. All user-visible strings (SCM display name, Programs &amp; Features, shortcuts, firewall rules, window title, data directory post-migration) resolve to "Master Control Orchestration Server".
- Tron-themed the setup launcher progress window: dark `#060A10` background with a 2px cyan `#00F6FF` accent bar, Bahnschrift SemiCondensed eyebrow/header/body fonts, accent-colored marquee progress bar, and per-control text coloring via `WM_CTLCOLORSTATIC`. Kept in sync with `src/MasterControlShell/App.xaml` so the installer feels continuous with the shell it delivers. (`src/MasterControlBootstrapper/setup_main.cpp`)
- Expanded the shell resource dictionary with polish primitives: `ShellStatusChipStyle`/`ShellStatusChipTextStyle` (pill status chips), `ShellSuccessButtonStyle`/`ShellWarningButtonStyle`/`ShellDangerButtonStyle` (tonal command variants), `ShellMetricLabelTextStyle`/`ShellMetricValueTextStyle` (hero tile typography), `ShellSkeletonBlockStyle`/`ShellSkeletonBrush` (loading placeholders), and `ShellFocusBrush`/`ShellPressFillBrush` for interactive state work. (`src/MasterControlShell/App.xaml`)
- Redesigned `OverviewSectionControl` around a hero card, status chip, operational snapshot card, a two-column narrative grid, and an authored-surfaces legend. Status chip text is now short, uppercase, and letter-spaced (`ADMIN API ONLINE · SYNCHRONIZED` / `ADMIN API OFFLINE · CACHED STATE`) to match the chip pill style. (`src/MasterControlShell/OverviewSectionControl.xaml`, `OverviewSectionControl.xaml.cpp`)
- Added a browser polish layer to the dashboard stylesheet: `:focus-visible` cyan outlines with a matching glow, a subtle 2.8s `tron-accent-pulse` on info badges, a radial-gradient backdrop with `backdrop-filter: blur(4px)` behind `<dialog>` modals, and full `prefers-reduced-motion` respect. (`resources/web/styles.css`)
- Ran a Phase A stabilization sweep across `src/` and `include/`: no TODO/FIXME/HACK/XXX/BUG markers found in first-party code. The setup launcher's elevation path already uses `ShellExecuteExW` with the `runas` verb (no Base64 relay in the C++ bootstrapper) and `maybeLaunchShell` is fully wired, so those items were confirmed complete rather than implemented.
- Decision recorded: the internal SCM service key `MasterControlProgram` and its legacy uninstall registry key are intentionally preserved for upgrade compatibility with pre-0.2 installs. User-visible strings everywhere (SCM display name, Programs &amp; Features, shortcuts, firewall rules, window titles) already resolve to "Master Control Orchestration Server", so no rename was performed in this pass.

## [0.1.66] - 2026-04-10
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Refocus shell on readable operator workflows (flynn33)

## [0.1.65] - 2026-04-10
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Fix uninstall cleanup and restore shell window frame (flynn33)

## [0.1.64] - 2026-04-10
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Add persistent deployment telemetry and fix setup exit handling (flynn33)

## [0.1.63] - 2026-04-10
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Clarify release metadata semantics (flynn33)
- Restore valid release tracking base (flynn33)

## [0.1.62] - 2026-04-10
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Seal release metadata after agent sync (flynn33)

## [0.1.61] - 2026-04-10
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Sync release metadata for v0.1.60 (flynn33)

## [0.1.60] - 2026-04-10
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Fix installer reliability and shell drag behavior (flynn33)
- Sync release metadata for v0.1.59 (flynn33)
- Stabilize Visual Studio test-machine validation (flynn33)
- Add persistent installer error logging (flynn33)
- Add IDE deployment acceptance targets (flynn33)
- Add VS Code Codex handoff workflow (flynn33)
- Fix WinUI shell build toolchain resolution (flynn33)

## [0.1.59] - 2026-04-03
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Fix installer reliability and shell drag behavior (flynn33)

## [0.1.58] - 2026-04-03
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Fix installer compatibility and package entry point (flynn33)

## [0.1.57] - 2026-04-03
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Sync release metadata and readiness tracking (flynn33)

## [0.1.56] - 2026-04-03
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Promote wizard-first admin workflows (flynn33)

## [0.1.55] - 2026-04-03
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Redesign telemetry as the primary monitoring deck (flynn33)

## [0.1.54] - 2026-04-03
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Add browser guided setup workflows (flynn33)

## [0.1.53] - 2026-04-02
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Polish setup launcher install flow (flynn33)

## [0.1.52] - 2026-04-02
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Document the Command Logic Unit module (flynn33)

## [0.1.51] - 2026-04-02
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Add guided CLU setup and Forsetti module wizards (flynn33)

## [0.1.50] - 2026-04-02
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Align repo naming with orchestration server (flynn33)

## [0.1.49] - 2026-03-29
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- feat(wiki): overhaul wiki generation with comprehensive pages (flynn33)

## [0.1.48] - 2026-03-29
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Delete .claude directory (Jim Daley)

## [0.1.47] - 2026-03-29
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Add guided setup wizards and shell fixes (flynn33)

## [0.1.46] - 2026-03-29
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Add native setup launcher for release packages (flynn33)

## [0.1.45] - 2026-03-29
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Fix installer elevation path handling (flynn33)

## [0.1.44] - 2026-03-29
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Fix managed installer lifecycle and elevation flow (flynn33)

## [0.1.43] - 2026-03-28
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Add installer desktop logging and launch diagnostics (flynn33)

## [0.1.42] - 2026-03-28
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Bundle release readiness into packages (flynn33)

## [0.1.41] - 2026-03-28
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Add release readiness reporting script (flynn33)

## [0.1.40] - 2026-03-28
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Enforce bandwidth and storage resource gates (flynn33)

## [0.1.39] - 2026-03-28
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Package release installer bundle (flynn33)

## [0.1.38] - 2026-03-28
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Add deployment report comparison tooling (flynn33)

## [0.1.37] - 2026-03-27
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Package deployment acceptance bundles (flynn33)

## [0.1.36] - 2026-03-27
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Expand deployment acceptance diagnostics (flynn33)

## [0.1.35] - 2026-03-27
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Harden deployment acceptance reporting (flynn33)

## [0.1.34] - 2026-03-27
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Add deployment acceptance harness (flynn33)

## [0.1.33] - 2026-03-27
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Fix non-admin shortcut deployment path (flynn33)

## [0.1.32] - 2026-03-27
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Harden bootstrapper rollback and deployment contracts (flynn33)

## [0.1.31] - 2026-03-27
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Add bootstrapper preflight readiness checks (flynn33)

## [0.1.30] - 2026-03-27
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Expand bootstrapper integration validation (flynn33)

## [0.1.29] - 2026-03-27
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Harden deployment workflow and remove gateway assumptions (flynn33)
- Harden bootstrapper deployment validation (flynn33)

## [0.1.28] - 2026-03-27
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Add CLU enforcement and local resource controls (flynn33)

## [0.1.27] - 2026-03-27
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Add Apple readiness drill-down surfaces (flynn33)

## [0.1.26] - 2026-03-27
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Add Apple queue execution and cancellation (flynn33)

## [0.1.25] - 2026-03-22
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Add proprietary software license (Jim Daley)

## [0.1.24] - 2026-03-22
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Add Apple job-control triage surfaces (flynn33)

## [0.1.23] - 2026-03-22
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Add Apple replay readiness safeguards (flynn33)

## [0.1.22] - 2026-03-22
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Harden Apple operation diagnostics and credential redaction (flynn33)

## [0.1.21] - 2026-03-22
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Persist Apple governance operation history (flynn33)

## [0.1.20] - 2026-03-22
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Add Apple host management and replay controls (flynn33)

## [0.1.19] - 2026-03-22
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Expose Apple operations in shell and browser (flynn33)

## [0.1.18] - 2026-03-21
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Track Apple governance operation history (flynn33)

## [0.1.17] - 2026-03-21
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Add macOS stapling and Apple host defaults (flynn33)

## [0.1.16] - 2026-03-21
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Add macOS signing and notarization tooling (flynn33)

## [0.1.15] - 2026-03-21
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Add iOS archive export and device install tooling (flynn33)

## [0.1.14] - 2026-03-21
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Add Apple remote execution tooling (flynn33)

## [0.1.13] - 2026-03-21
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Add Apple host registry and readiness routing (flynn33)

## [0.1.12] - 2026-03-21
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Implement governance tool execution layer (flynn33)

## [0.1.11] - 2026-03-20
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Add platform gateway and governance service lanes (flynn33)

## [0.1.10] - 2026-03-20
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Add custom MCP server authoring workflows (flynn33)

## [0.1.9] - 2026-03-20
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Add custom sub-agent authoring workflows (flynn33)

## [0.1.8] - 2026-03-20
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Add group-based provider routing for sub-agents (flynn33)

## [0.1.7] - 2026-03-20
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Implement provider execution adapters and consoles (flynn33)

## [0.1.6] - 2026-03-20
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Split AI integration into provider modules (flynn33)

## [0.1.5] - 2026-03-20
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Implement CLU governance and Forsetti runtime hardening (flynn33)

## [0.1.4] - 2026-03-16
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Make browser surface Forsetti-native (flynn33)

## [0.1.3] - 2026-03-16
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Make shell Forsetti-native for certification (flynn33)

## [0.1.2] - 2026-03-16
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Implement Forsetti compliance and shell surfaces (flynn33)

## [0.1.1] - 2026-03-16
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- chore(repo): add GitHub repository agents (flynn33)
- fix(hooks): allow pre-push stdin on Windows (flynn33)
- fix(hooks): use PowerShell guard wrapper (flynn33)
- fix(agents): preserve git log field separators (flynn33)

## [0.1.0] - 2026-03-16
### Summary
Initial tracked baseline for the Forsetti-based Master Control Orchestration Server workspace.

### Included Changes
- Imported the current Forsetti-compliant Master Control Orchestration Server source tree.
- Established WinUI 3 shell, service host, browser dashboard, and bootstrapper scaffolding.
- Seeded repository-owned version, changelog, wiki, and README automation files.
