# Changelog

All notable changes to this repository are tracked here by the repository agents.

## [Unreleased]
- Changes pushed to `main` are promoted into the next numbered release automatically.

## [0.4.5-rc.1] - 2026-04-19
### Summary
**Release candidate for the Windows host experience.** This candidate rolls up the guided CLI-install flow, the host-session AI sign-in fix so `claude login` / `codex login` open for the interactive Windows user instead of the service account, and the packaging cleanup needed to ship a clean MSI + ZIP pair from the latest commits.

### Included Changes
- feat(runtime): dependency install flow now handles Node.js as the prerequisite for Claude Code / Codex CLI install on clean Windows hosts, refreshes PATH after install, and keeps the provider onboarding path guided instead of dead-ending on missing npm.
- fix(shell): the desktop shell now owns CLI sign-in launch and auth-file detection for host usage, then hands successful provider registration back to the backend. Host users finally see the authentication prompt in their own session.
- test: `tests/MasterControlOrchestrationServerTests.cpp` now covers the CLI sign-in registration handoff so invalid bridge payloads fail and a successful Codex handoff registers the expected providers.
- fix(packaging): staged end-user payloads no longer include `*.pdb` symbol files, and the MSI metadata handoff now records the built `.msi` and `msiVersion` correctly in `PACKAGE-METADATA.json`.

## [0.4.4-rc.1] - 2026-04-18
### Summary
**Auto-install the CLIs from the shell.** The sign-in cards have worked since rc.5, but they silently failed when the underlying CLI (`claude` or `codex`) wasn't on PATH. The browser UI showed a grayed-out "CLI not installed" button and the shell just errored on click. rc.1 closes that gap: the Providers surface now detects on load whether each CLI is installed, and when either is missing it surfaces an active **Install Claude Code CLI** / **Install Codex CLI** button that runs the preset `npm install -g` command through the admin API. A `ProgressRing` ticks while npm is installing; on completion the Install button hides, the Sign-In button enables, and the operator can keep going without opening a terminal.

### Included Changes
- feat(runtime): `buildSupportedDependencyCatalog()` now includes a `codex-cli` entry (`npm install -g @openai/codex`) alongside `claude-code-cli`. Both honor the existing three-branch preflight (`ready` / `installable` / `prerequisite-missing`) so the browser + shell can surface useful status when Node.js/npm itself isn't on PATH.
- feat(shell): `ShellRuntime::InstallCliDependency(bridge)` POSTs to `/api/setup/dependencies/{id}/install` and parses the structured response (`succeeded`, `finalState`, `summary`, `postInstallDetection.detectedVersion`). Returns a `ShellCliDependencyInstallResult` that the UI uses to drive button visibility + status text.
- feat(shell): `ProvidersSectionControl` adds `InstallClaudeCliButton`, `InstallCodexCliButton`, and matching `ProgressRing`s into the Claude and ChatGPT + Codex sign-in cards. On `AttachRuntime`, a fire-and-forget `RefreshCliInstallStateAsync()` probes which CLIs are installed and toggles per-card button visibility. Install click disables both buttons, starts the ring, calls `InstallCliDependency`, and on success flips the card into the ready-to-sign-in state with a detected-version message ("Claude Code 1.2.3 installed. Click sign-in to continue.").
- feat(browser): `renderSignInCards` emits an active `data-action="install-cli"` button instead of a disabled "CLI not installed" label. `installCliDependency(bridge, depId)` tracks per-bridge install status in `state.signIn.installByBridge`, POSTs to the admin API, then re-calls `signInDetectInstalled()` so the card refreshes into its normal sign-in state on success.
- test: `tests/MasterControlOrchestrationServerTests.cpp` dependency-catalog test updated to assert both `claude-code-cli` and `codex-cli` are present with the documented `npm install -g` commands.

## [0.4.3-rc.1] - 2026-04-18
### Summary
**Polished Windows Installer + product icon everywhere.** The install experience was the weakest part of the product — no install-directory picker, no desktop-shortcut option, no features dialog, no EULA page, a custom Tron-cyan progress window with zero controls, and no embedded icon on any executable (so every shortcut, taskbar entry, and Programs & Features row showed the generic Windows default). rc.1 fixes all of that with a WiX v5 MSI using the native `WixUI_InstallDir` dialog sequence + a custom Options page with five checkboxes, and embeds the supplied Tron-red icon as resource group 1 in every shipped `.exe` so it surfaces on the MSI file, UAC prompt, Start Menu shortcut, optional Desktop shortcut, taskbar, Alt+Tab, services.msc, Programs & Features, and the browser admin UI favicon.

### Included Changes
- feat(installer): new WiX v5 MSI at `installer/MasterControlOrchestrationServer.wxs`. Dialog sequence: Welcome → License (proprietary, `installer/license.rtf`) → InstallDir (Browse-capable) → **FeaturesDlg** (custom: service / firewall / Start Menu / Desktop / launch-on-finish) → VerifyReady → Progress → Exit. Native Win11 Fluent chrome, no custom theming.
- feat(installer): deferred custom actions invoke the existing `MasterControlBootstrapper.exe` with `--skip-shortcuts --skip-uninstall-registration` plus the operator's `--skip-service` / `--skip-firewall` choices composed from the Options page. MSI owns shortcuts + Programs & Features entry; the bootstrapper continues to own service registration, firewall rules, module manifest placement, and data-directory bootstrap. Zero CLI-surface regression.
- feat(installer): `scripts/Package-MasterControlOrchestrationServer.ps1` now emits a `.msi` alongside the existing `.zip`. `installer/Build-Msi.ps1` harvests the staged payload into a generated `Fragments/Files.wxs`, drives `wix build` with the UI + Util extensions, and returns the MSI path + version for `PACKAGE-METADATA.json`.
- feat(icons): Tron-red product icon family staged at `resources/icons/` (master `.ico` + PNG ladder + Windows tile assets + web favicons). `master-control.ico` embedded as icon resource group 1 in `MasterControlShell.exe`, `MasterControlServiceHost.exe`, `MasterControlBootstrapper.exe`, and `MasterControlOrchestrationServerSetup.exe` via a one-line `.rc` file per target wired into each CMake / vcxproj. The existing shortcut `IconLocation=MasterControlShell.exe,0` at [main.cpp:1272](master-control-dashboard-main/src/MasterControlBootstrapper/main.cpp:1272) now resolves to the Tron-red badge automatically (zero-indexed, first group wins). CLI-installed shortcuts benefit without any bootstrapper change.
- feat(icons): browser admin UI favicon + PWA icons referenced from `resources/web/index.html` so the browser tab shows the Tron-red badge.
- feat(icons): MSI `ARPPRODUCTICON` points at `master-control-installer.ico` so Programs & Features, the MSI file in File Explorer, and the UAC elevation dialog all show the Tron-red icon.
- chore(build): `CMakeLists.txt` now declares `LANGUAGES CXX RC` and installs the `resources/icons/` tree into `share/MasterControlOrchestrationServer/icons/` so the MSI can reference fixed paths.

## [0.4.2-rc.8] - 2026-04-17
### Summary
**UX overhaul: guided path first, advanced hidden, clean desktop.** rc.7 made sign-in work, but the Providers view still presented twelve top-level cards at once — overwhelming for first-time setup — and button heights weren't symmetric. rc.8 promotes the guided AI-model path (sign-in + Auto-Connect + Provider Connections list) to the primary surface and hides the direct-edit and orchestration plumbing behind two **Advanced** Expanders. Button geometry is unified across every card, `Remove Group` now uses the destructive tone, and `Run Provider Task` shows a live progress ring. Separately, successful installs no longer leave `MasterControlOrchestrationServer-install-succeeded-*.txt` receipts on the operator's desktop — failures still write, and the persistent log tree under `%PUBLIC%\Documents\Master Control Orchestration Server\logs\installer` still captures every outcome.

### Included Changes
- fix(installer): `writeBootstrapperActionLog` and the setup launcher's textual-log writer only emit the desktop-local `.txt` receipt on failure now. A new constant `writeDesktopLog = !succeeded || hasOverride` gates the `writeTextFile` call in `MasterControlBootstrapper/main.cpp`; `setup_main.cpp` has the matching guard. `MASTERCONTROL_BOOTSTRAPPER_LOG_DIR` still forces a desktop log when set so CI / scripted flows can keep the old behavior.
- feat(shell): `ProvidersSectionControl.xaml` collapsed from 12 top-level cards into a guided primary path plus two `<Expander IsExpanded="False">` groupings — `ADVANCED · DIRECT EDIT` (Provider Editor + Credentials + AI Autonomy) and `ADVANCED · ORCHESTRATION` (Sub-Agent Groups + Ownership Routing + Execution Console). First-time operators now see only Sign-In cards, Auto-Connect, Provider Routes, Provider Connections, and Provider Modules.
- fix(shell): `App.xaml` unified the implicit `Button` style and `ShellCommandButtonStyle` to `MinHeight=48`, `Padding=12,10`, `HorizontalAlignment=Stretch`; `ShellSecondaryButtonStyle` is now visibly recessive with `Background=#55050B12` and `FontWeight=Normal`. Paired `Save / New / Remove` rows are wrapped in equal-width column grids so their widths match.
- fix(shell): `Remove Group` now uses `ShellDangerButtonStyle` (red) so destructive actions stand apart from `Save` and `New`.
- feat(shell): `ExecuteProviderTaskAsync` now toggles a new `ProviderExecutionProgressRing` next to `Run Provider Task` while the admin API call is in flight.
- fix(shell/browser): jargon sweep. `FORSETTI SURFACE TOOLBAR` → `QUICK ACTIONS` in `MainWindow.xaml` and `resources/web/index.html`. `Protection Envelope` → `Security Settings` in `SecuritySectionControl.xaml` and `resources/web/app.js`'s `renderSecurityView`. `Governed Resource Envelope` → `Resource Allocation` in `SettingsSectionControl.xaml`, `TelemetrySectionControl.xaml`, and `MainWindow.xaml.cpp`'s setup-wizard `Step 3` label.

## [0.4.2-rc.7] - 2026-04-17
### Summary
**One OpenAI sign-in registers both ChatGPT and Codex.** The `codex` CLI's single OAuth flow authenticates the operator for two logically distinct endpoints: ChatGPT (general reasoning / planning) and Codex (coding agent). rc.5's sign-in wizard only registered one of the two, so assigning `coding-specialists` to Codex after signing in to ChatGPT was awkward. rc.7 registers every capability whose `cliBridgeCommand` matches the bridge that just authenticated, so the operator signs in once and can immediately split planning and coding across the two endpoints.

### Included Changes
- fix(runtime): `ProviderCliSignInService::registerBridgedProvider` now iterates every capability whose `cliBridgeCommand` matches the bridge that just authenticated, instead of registering only the `providerId` hint — one `codex login` registers both `chatgpt` and `codex` entries; `claude login` still registers only `claude-code` because that's the only capability with `cliBridgeCommand=claude`.
- feat(shell): `ProvidersSectionControl` ChatGPT card retitled **CHATGPT + CODEX (via Codex CLI)**, button relabeled **Sign in with OpenAI account**, and a sub-line explains the two-endpoint outcome explicitly.
- feat(browser): `renderSignInCards` shows **Sign in to use ChatGPT + Codex** with **OPENAI ACCOUNT** eyebrow for the `codex` bridge.
- feat(runtime): on successful codex sign-in the completion message now surfaces *"ChatGPT (planning / reasoning) and Codex (coding agent) are both registered — assign each to roles below"*.
- docs(capabilities): `cliBridgeAccountLabel` on both ChatGPT and Codex capabilities updated to describe the one-sign-in-two-endpoints model.

## [0.4.2-rc.6] - 2026-04-17
### Summary
**Grok API-key onboarding card.** xAI does not publish a consumer OAuth flow — confirmed by the deep-research analysis on non-API middleware bridges — so a true no-key path is not available for hosted Grok today. This release adds a dedicated API-key card beside the Claude and ChatGPT sign-in cards: paste the xAI key once, and the existing Auto-Connect pipeline probes the endpoint, discovers models, seals the key with DPAPI, and registers the provider. The copy on both surfaces is explicit about the tradeoff.

### Included Changes
- feat(shell): new "Grok (xAI API key)" card in `ProvidersSectionControl` beside the Claude and ChatGPT sign-in cards. `PasswordBox` + `Connect Grok` button routes through `ShellRuntime::AutoConnectProvider` with `kind=xai-grok` and the pasted key; status TextBlock below surfaces probe/discover/register progress.
- feat(browser): `renderSignInCards` now emits an additional API-key section for capabilities that have no `cliBridgeCommand` but declare a required `api_key` field. A single-input form posts to `/api/providers/auto-connect` and the card flips through idle → pending → success/error states.
- docs(wizard): copy on both surfaces is explicit that xAI lacks a consumer OAuth path and that the key is sealed locally with Windows DPAPI and never re-transmitted.

## [0.4.2-rc.5] - 2026-04-17
### Summary
**Add AI Model — account-only sign-in wizard.** The primary goal of this project has always been to get users to productive AI use without making them paste API keys. This release delivers that for Claude (Pro / Max / Team) and ChatGPT: click a button in the Providers section, the matching CLI (`claude login` or `codex login`) opens a console and drives OAuth in your browser, and the orchestration server registers the provider on success. The CLI keeps its own tokens — the server never stores them.

### Included Changes
- feat(models): `ProviderCapabilityDescriptor` now carries `cliBridgeCommand` (`claude` | `codex`) and `cliBridgeAccountLabel` so the UI knows which providers support account-only sign-in
- feat(runtime): `ProviderExecutionTransport::CodexCli` added alongside `ClaudeCodeCli`; the execution dispatch bypasses the credential-required check for CLI-bridged transports
- feat(runtime): `ProviderCliSignInService` spawns `claude login` / `codex login` in a new console window, polls for process exit + auth-file presence, registers the provider on success
- feat(runtime): new endpoints `POST /api/providers/signin/start`, `GET /api/providers/signin/status?sessionId=`, `GET /api/providers/signin/installed`
- feat(runtime): `executeCodexCli` invokes `codex exec <prompt>` with optional `OPENAI_API_KEY` env forwarding for operators who prefer an API key instead of account sign-in
- feat(modules): Claude Code, Codex, and ChatGPT capabilities advertise account-sign-in as the primary path; credential fields are now marked optional
- feat(shell): Providers section gains a top-level **Add AI Model — Account Sign-In** card with two buttons (Claude, ChatGPT) that drive the end-to-end OAuth flow with live status updates below each button
- feat(browser): matching **Add AI Model** sign-in cards at the top of the Providers view, polling `/api/providers/signin/status` every 2 seconds with live status transitions
- fix(shell): `ShellRuntime` gains `StartCliSignIn`, `GetCliSignInStatus`, `DetectCliSignInInstalled` over the admin API

### What's next
- Gemini CLI sign-in
- Ollama (local models) detection + registration (no sign-in)
- First-time dependency install when `claude` / `codex` aren't yet on PATH — currently the wizard reports the CLI as missing; a one-click install is the next step

## [0.4.2-rc.4] - 2026-04-17
### Summary
Telemetry is now **unmistakably live**. 1-second cadence, a visible `LIVE #N · HH:MM:SS` badge in the title bar that bumps every tick (even failed ticks), and a RAII flag guard so the tick can never silently stop. When the admin API fails, the badge flips to `OFFLINE` so a stuck UI is visually distinct from a non-responsive backend.

### Included Changes
- feat(shell): live telemetry timer dropped from 2s to 1s cadence
- feat(shell): visible `LIVE #N · HH:MM:SS` badge in the title bar that bumps every tick (including failed ticks), giving unmistakable proof the telemetry pipeline is alive even on idle hosts where CPU/RAM numbers happen to be stable
- feat(shell): badge flips to `OFFLINE` (amber) when the admin API call fails so a stuck UI is visually distinct from a non-responsive backend
- fix(shell): `RefreshLiveAsync` now uses a RAII guard so `liveRefreshInFlight_` is ALWAYS cleared on every exit path, including exceptions; prevents the tick from silently stopping
- feat(browser): same `Live #N · HH:MM:SS` treatment on the health badge; browser poll dropped from 2s to 1s

## [0.4.2-rc.3] - 2026-04-17
### Summary
Second hotfix for the host install experience: the **WinUI shell itself** was rebuilding its navigation, toolbar, and section content every 10 seconds, which is what the user saw as "the entire page refreshing" across all menus. Fixed by signature-caching the chrome, no-op'ing redundant destination swaps, and introducing a live-only 2-second refresh path. The setup launcher now auto-launches the desktop shell on the host by default.

### Included Changes
- fix(shell): `ApplySurfaceNavigation` skips `Clear()`+rebuild when the navigation signature is unchanged; selection-only update on the common refresh path
- fix(shell): `ApplySurfaceToolbar` skips `Clear()`+rebuild when the toolbar signature is unchanged
- fix(shell): `SetCurrentDestination` no-ops the content-host swap and `StartBringIntoView()` scroll when the destination hasn't changed
- feat(shell): new `RefreshLiveAsync` + `ApplyLiveSnapshotFragment` path — updates only hero values and the currently visible section's data, leaving navigation, toolbar, section-content host, and scroll position untouched
- feat(shell): `refreshTimer_` now ticks every 2 seconds and calls `RefreshLiveAsync`; full `ApplySnapshot` only runs on user-initiated Refresh or view change
- fix(shell): the 2-second live tick self-suppresses while on Providers / Security / Settings / Imports so in-progress form input is never interrupted
- fix(installer): setup launcher now auto-launches the desktop shell by default on the host; `--no-launch-shell` still opts out for headless installs

## [0.4.2-rc.2] - 2026-04-17
### Summary
Hotfix for the host install experience reported in the field: the browser admin surface was re-rendering the whole page on a 15-second timer (feeling like a page reload) and the Start Menu exposed the browser dashboard shortcut next to the native shell, which was confusing on the host machine itself.

### Included Changes
- fix(browser): replace the 15-second `renderShell()` refresh with a 2-second targeted telemetry poll; the surrounding page no longer visually refreshes
- fix(browser): telemetry meter cards and signal cards now carry `data-live` markers; a new `updateTelemetryLive()` patches only the value, meter width, and tone without touching surrounding DOM
- fix(browser): the live poll skips automatically when the browser tab is hidden and when the user is on a static view (providers, security, settings, imports, setup)
- fix(installer): browser dashboard shortcut moved from *Start Menu > Master Control Orchestration Server > Master Control Orchestration Server Dashboard.url* to *Start Menu > Master Control Orchestration Server > Remote Access > Browser Dashboard (Remote).url*; the native shell shortcut remains at the top of the product folder
- fix(installer): uninstall path cleans up the Remote Access subfolder when it is empty
- docs(install): `START-HERE.txt` now explains that the desktop shell is the intended host surface and the browser dashboard is for remote LAN clients

## [0.4.2-rc.1] - 2026-04-17
### Summary
Non-security remediation: runtime correctness, JSON ingress hardening, build hygiene, WinUI claim-parity, regression tests.

### Included Changes
- fix(runtime): `writeJsonFile` returns `[[nodiscard]] bool` with atomic temp+rename; every call site propagates failure into `OperationResult` or `(void)`-discards in recovery/interface paths
- fix(runtime): `readJsonFile` catches `nlohmann::json::exception` and `std::ios_base::failure`; TOCTOU `exists()` + read patterns are now safe at every caller
- fix(runtime): `upsertProvider` and `upsertMcpServer` capability reads moved inside `state_->mutex` (lost-update race closed)
- fix(runtime): `ScopedThread` RAII wrapper guarantees child-process pipe readers join on any exception unwind
- fix(runtime): cap WinHTTP response accumulator at 32 MiB with clean handle cleanup
- feat(runtime): add `tryParseJson` / `tryGet<T>` / `getOr<T>` helpers; wrap config-load and credential-unseal ingress
- fix(build): `vcpkg.json` version-string synced to `VERSION.json`; README badges regenerated from the same source of truth
- feat(build): new `scripts/Sync-RepositoryVersionBadges.ps1` with `-CheckOnly` mode for CI gating
- fix(build): CMake `VCPKG_ROOT` unresolved-env guard with clear `FATAL_ERROR`
- fix(build): PowerShell `find_program` is now `FATAL_ERROR` on Windows when missing
- fix(build): `MasterControlApp` Windows system libs moved `PUBLIC` → `PRIVATE`; `MasterControlServiceHost` links `advapi32` explicitly
- fix(scripts): `Set-StrictMode -Version Latest` and `$ErrorActionPreference = 'Stop'` across all 12 previously-unset PowerShell scripts
- fix(shell): activity-stream 1 Hz tick suppressed while operator is on Providers / Security / Settings / Imports
- fix(shell): `DispatcherQueue` null-guard cached once at `ConfigureTimer` entry
- feat(shell): Tron-themed focus indicators (`ShellAccentBrush` + `ShellGlowBrush`) on `Button` / `TextBox` / `PasswordBox` / `ComboBox` / `ToggleSwitch` implicit styles
- fix(shell): `app.manifest` extended with Windows 10/11 `<supportedOS>` GUIDs and `requestedExecutionLevel=asInvoker`
- test: regression tests for malformed-configuration fallback, activity-ring cap under load, and concurrent `upsertProvider`

## [0.4.1-rc.1] - 2026-04-17
### Summary
UX simplification: stop settings refresh, simplify AI integration form, add OAuth scaffolding, add native WinUI setup wizard.

### Included Changes
- fix(browser): stop 5-second refresh on static views (settings, providers, security); live views refresh at 15s
- fix(browser): simplify provider connect form to pick-model + authenticate; hide Route ID, Base URL, Model ID
- feat(runtime): add OAuth scaffolding (`supportsOAuth`, `oauthAuthorizeUrl`, `oauthClientId`, `oauthScope`) to `ProviderCapabilityDescriptor`
- feat(shell): add programmatic `SetupWizardBuilder` (no MIDL/IDL registration) with Guided / Manual / Import entry cards and readiness review
- feat(shell): first-run routing — shell shows setup wizard automatically when `!firstRunCompleted`

## [0.4.0-rc.1] - 2026-04-14
### Summary
Ease-of-use remediation pass: guided setup spine, browser auto-connect refactor, environment hints UI, Claude Code CLI install automation, shell/browser parity, readiness dashboard, starter workflow templates, exports demotion.

### Included Changes
- feat(runtime): add `/api/readiness` with source-neutral workflow readiness (guided + manual both count)
- feat(runtime): add `/api/setup/start`, `/api/setup/complete`, `/api/setup/reset` for setup lifecycle
- feat(runtime): add `/api/setup/dependencies` with three-branch preflight (ready / installable / prerequisite-missing)
- feat(runtime): add `/api/setup/dependencies/{id}/install` for Claude Code CLI orchestration
- feat(runtime): add `/api/setup/workflow-templates` with three starter templates
- feat(browser): first-run setup wizard with Guided / Manual / Import Existing entry modes
- feat(browser): guided provider form now uses `/api/providers/auto-connect` with manual fallback
- feat(browser): environment hints displayed on credential fields (detected / needed / none)
- feat(browser): readiness dashboard with "Fix now" routing and starter workflow picker
- feat(browser): localStorage-backed wizard state persistence across refresh
- feat(shell): `advancedMode` and `firstRunCompleted` now exposed in `ShellHostSnapshot`
- feat(build): `ReadinessCopy.h` shared copy header consumed by both surfaces
- chore(browser): exports demoted from primary nav to advanced-only
- fix(browser): stale `kind` preset removed from `connect-chatgpt` quick-connect entry
- test: add `/api/readiness` shape, setup lifecycle round-trip, dependency catalog, workflow templates coverage

## [0.3.0-rc.1] - 2026-04-14
### Summary
Productization and stabilization remediation pass covering 10 workstreams.

### Included Changes
- fix(runtime): resolve provider capabilities by providerId instead of kind — fixes ChatGPT/Codex collision
- feat(runtime): mark seeded endpoints and default providers as templates with EndpointStatus::Template
- feat(browser): add quick-connect workflows for ChatGPT, Codex, Claude Code, and xAI
- feat(browser): add progressive disclosure with runtime-backed advancedMode toggle
- feat(build): unify versioning — VERSION.json drives CMake, bootstrapper, and module manifests
- feat(ci): add Windows Build, Test, and Package workflow with release gating
- fix(runtime): harden process execution with concurrent pipe draining, 5-min timeout, bounded capture
- feat(runtime): add GET /api/environment-hints for credential auto-detection
- feat(runtime): add POST /api/settings/advanced-mode for progressive disclosure toggle
- test: add provider identity, template distinction, version alignment, and progressive disclosure tests
- docs: revise README to accurately describe multi-binary architecture and prerequisites

## [0.2.12] - 2026-04-12
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- fix(shell): exclude interactive forms from background refresh timer (flynn33)

## [0.2.11] - 2026-04-12
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- fix(shell): freeze Auto-Connect card during user interaction (flynn33)

## [0.2.10] - 2026-04-12
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- fix(shell): prevent Auto-Connect provider selector from resetting (flynn33)

## [0.2.9] - 2026-04-12
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- fix(shell): eliminate black fonts and left-pane gap (flynn33)

## [0.2.8] - 2026-04-12
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- fix(runtime): use synchronous refresh in dashboard snapshot (flynn33)

## [0.2.7] - 2026-04-11
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- docs(wiki): overhaul wiki agent and source pages (flynn33)

## [0.2.6] - 2026-04-11
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Add autonomous Claude tooling scripts under .claude-work/ (flynn33)

## [0.2.5] - 2026-04-11
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Fix: admin API mutating handlers blocked 10-14s on inventory probes (flynn33)

## [0.2.4] - 2026-04-11
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Theme overhaul + critical API probe fix + workspace dialog removed (flynn33)

## [0.2.3] - 2026-04-11
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Live command stream, nav pane removed, hero auto-collapse, Tron palette v2 (flynn33)

## [0.2.2] - 2026-04-11
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Fully automate AI model integration via Auto-Connect (flynn33)

## [0.2.1] - 2026-04-11
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Initial commit (Jim Daley)
- Import master control dashboard workspace (flynn33)
- chore(repo): add GitHub repository agents (flynn33)
- fix(hooks): allow pre-push stdin on Windows (flynn33)
- fix(hooks): use PowerShell guard wrapper (flynn33)
- fix(agents): preserve git log field separators (flynn33)
- Implement Forsetti compliance and shell surfaces (flynn33)
- Make shell Forsetti-native for certification (flynn33)
- Make browser surface Forsetti-native (flynn33)
- Implement CLU governance and Forsetti runtime hardening (flynn33)
- Split AI integration into provider modules (flynn33)
- Implement provider execution adapters and consoles (flynn33)
- Add group-based provider routing for sub-agents (flynn33)
- Add custom sub-agent authoring workflows (flynn33)
- Add custom MCP server authoring workflows (flynn33)
- Add platform gateway and governance service lanes (flynn33)
- Implement governance tool execution layer (flynn33)
- Add Apple host registry and readiness routing (flynn33)
- Add Apple remote execution tooling (flynn33)
- Add iOS archive export and device install tooling (flynn33)
- Add macOS signing and notarization tooling (flynn33)
- Add macOS stapling and Apple host defaults (flynn33)
- Track Apple governance operation history (flynn33)
- Expose Apple operations in shell and browser (flynn33)
- Add Apple host management and replay controls (flynn33)
- Persist Apple governance operation history (flynn33)
- Harden Apple operation diagnostics and credential redaction (flynn33)
- Add Apple replay readiness safeguards (flynn33)
- Add Apple job-control triage surfaces (flynn33)
- Add proprietary software license (Jim Daley)
- Add Apple queue execution and cancellation (flynn33)
- Add Apple readiness drill-down surfaces (flynn33)
- Add CLU enforcement and local resource controls (flynn33)
- Harden deployment workflow and remove gateway assumptions (flynn33)
- Harden bootstrapper deployment validation (flynn33)
- Expand bootstrapper integration validation (flynn33)
- Add bootstrapper preflight readiness checks (flynn33)
- Harden bootstrapper rollback and deployment contracts (flynn33)
- Fix non-admin shortcut deployment path (flynn33)
- Add deployment acceptance harness (flynn33)
- Harden deployment acceptance reporting (flynn33)
- Expand deployment acceptance diagnostics (flynn33)
- Package deployment acceptance bundles (flynn33)
- Add deployment report comparison tooling (flynn33)
- Package release installer bundle (flynn33)
- Enforce bandwidth and storage resource gates (flynn33)
- Add release readiness reporting script (flynn33)
- Bundle release readiness into packages (flynn33)
- Add installer desktop logging and launch diagnostics (flynn33)
- Fix managed installer lifecycle and elevation flow (flynn33)
- Fix installer elevation path handling (flynn33)
- Add native setup launcher for release packages (flynn33)
- Add guided setup wizards and shell fixes (flynn33)
- Delete .claude directory (Jim Daley)
- feat(wiki): overhaul wiki generation with comprehensive pages (flynn33)
- Align repo naming with orchestration server (flynn33)
- Add guided CLU setup and Forsetti module wizards (flynn33)
- Document the Command Logic Unit module (flynn33)
- Polish setup launcher install flow (flynn33)
- Add browser guided setup workflows (flynn33)
- Redesign telemetry as the primary monitoring deck (flynn33)
- Promote wizard-first admin workflows (flynn33)
- Sync release metadata and readiness tracking (flynn33)
- Fix installer compatibility and package entry point (flynn33)
- Fix installer reliability and shell drag behavior (flynn33)
- Sync release metadata for v0.1.59 (flynn33)
- Stabilize Visual Studio test-machine validation (flynn33)
- Add persistent installer error logging (flynn33)
- Add IDE deployment acceptance targets (flynn33)
- Add VS Code Codex handoff workflow (flynn33)
- Fix WinUI shell build toolchain resolution (flynn33)
- Sync release metadata for v0.1.60 (flynn33)
- Seal release metadata after agent sync (flynn33)
- Clarify release metadata semantics (flynn33)
- Restore valid release tracking base (flynn33)
- Add persistent deployment telemetry and fix setup exit handling (flynn33)
- Fix uninstall cleanup and restore shell window frame (flynn33)
- Refocus shell on readable operator workflows (flynn33)
- v0.2.0 — Tron density rework; validated on Windows Server 2022 (flynn33)

## [0.2.0] - 2026-04-11
### Summary
Tron-density UX rework, validated end-to-end on Windows Server 2022.

### Included Changes
- Tron-theme the setup launcher progress window (cyan accent bar, Bahnschrift fonts, accent marquee) to match the shell's App.xaml palette.
- Expand shell resource dictionary with status chip, tonal button variants, compact tiles, sub-agent badge, and live-clock text styles.
- Redesign OverviewSectionControl around hero card + operational snapshot + narrative grid + authored-surfaces legend.
- Add Tron command-center density to MainWindow: live clock (HH:MM:SS) in the title bar, sub-agent footer row (SENTINEL/ARCHITECT/FORGE/SCRIBE/RECON/NEXUS/WATCHDOG), and a ScrollViewer wrapping the main content so the full layout reaches low-resolution displays.
- Add browser dashboard polish layer: prefers-reduced-motion, focus-visible outlines, accent pulse animation, <dialog>::backdrop blur.
- Add one-shot ProgramData migration from legacy MasterControlProgram to MasterControlOrchestrationServer path, with safe fallback if the rename cannot complete.
- Update GitHub repository URL references to flynn33/Master-Control-Orchestration-Server.
- Guard Package-MasterControlOrchestrationServer.ps1 git calls so packaging works outside a git repo.
- Update PlatformToolset from v143 to v145 so the shell builds on Visual Studio 2026.
- Validated end-to-end on Windows Server 2022 Datacenter (21H2, build 20348): cmake configure + build (0 errors, 0 warnings), ctest 4/4 green, package staged (44 MB), unattended install smoke CLEAN, shell launches and renders Tron UI with live clock, bootstrapper preflight/validate reports valid:true.

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
