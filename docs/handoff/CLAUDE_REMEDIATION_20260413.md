# Claude Code Handoff Note

## Project Identifier
Master Control Orchestration Server

## Current Objective
Implement the approved remediation package dated 2026-04-13 while preserving the current internal-LAN security posture.

## Active Task
All 10 workstreams completed in this pass.

## Completed Work

### WS1: Fix Provider Identity Resolution
- Added `findCapabilityByProviderId` and `findExecutionRegistrationByProviderId` template helpers in MasterControlRuntime.cpp with three-tier resolution: exact providerId match, prefix match for auto-connect-generated IDs, kind-based fallback.
- Fixed 4 kind-based lookup sites in MasterControlRuntime.cpp (upsertProvider, autoConnectProvider, saveProviderCredentials, executeProviderTask).
- Fixed shell capability lookup in ProvidersSectionControl.xaml.cpp (findCapabilityForProvider).
- Fixed browser capability lookup in app.js (selectedProviderCapability → resolveProviderCapability).
- Added `providerId` field to AutoConnectRequest struct and JSON serialization.
- Added `providerId` to ShellAutoConnectProviderRequest and JSON payload builder.
- Removed fragile `if (provider.id == L"chatgpt")` combo box hack, replaced with capability-driven display name matching.

### WS2: Separate Templates from Configured Infrastructure
- Added `EndpointStatus::Template` to the enum with to/from-string serialization.
- Added `isTemplate` field to both `RuntimeEndpoint` and `ProviderConnection` with JSON serialization.
- All seeded endpoints from `makeEndpoint()` now have `status = Template` and `isTemplate = true`.
- All default providers from `buildDefaultProviders()` now have `isTemplate = true`.
- `upsertProvider` clears `isTemplate = false` when a user saves a provider.
- Browser UI shows "TEMPLATE" badges on template endpoints/providers, muted row styling, and separate active/template counts.

### WS3: Make Onboarding Outcome-Driven
- Added 4 quick-connect workflows: connect-chatgpt, connect-codex, connect-claude-code, connect-xai with pre-set providerId and kind.
- Restructured overview landing: outcome-driven "Get Started / Connect a Provider" hero, existing workflows moved to "More Setup Options" collapsible.
- Quick-connect workflows route through the existing connect-model form with pre-set capability.
- Added `GET /api/environment-hints` API endpoint returning environment variable detection for credential auto-fill.
- Added `friendlyProviderError()` for user-language error messages in guided workflow submissions.

### WS4: Add Progressive Disclosure
- Added `advancedMode` field to `AppConfiguration` (defaults to `false`).
- Added `POST /api/settings/advanced-mode` API endpoint for runtime-backed toggle.
- Browser reads `advancedMode` from config (not localStorage) — both shell and browser share the same state.
- Advanced sections gated with `advancedOnly()` helper: sub-agent groups, assignment matrix, autonomy gate, execution console, provider/execution modules detail, ownership map, execution history.
- Toggle checkbox added to settings view.

### WS5: Unify Versioning
- Created `include/MasterControl/MasterControlVersion.h.in` template.
- CMakeLists.txt now reads VERSION.json at configure time, parses major.minor.patch, sets project version.
- `configure_file()` generates `MasterControlVersion.h` with `MASTERCONTROL_VERSION`, `_MAJOR`, `_MINOR`, `_PATCH` macros.
- Bootstrapper now includes the generated header instead of hardcoded `"0.1.0"`.
- Module manifests now use `SemVer{ MASTERCONTROL_VERSION_MAJOR, ... }` instead of hardcoded `0.1.0`.
- All version sources now derive from VERSION.json (currently 0.2.11).

### WS6: Add Real Windows CI Build/Test/Package Gates
- Created `.github/workflows/windows-build-test-package.yml` with: checkout, vswhere-based toolchain discovery, vcpkg install, CMake configure/build/test, package, smoke validation (binary presence check), bootstrapper preflight, artifact upload.
- Modified `.github/workflows/repository-maintenance-agents.yml`: trigger changed from push to `workflow_run` (Windows Build must complete first), added mandatory Windows product gate verification step that checks the check-runs API for the exact HEAD SHA before allowing release creation. `workflow_dispatch` is preserved but the gate still fires.

### WS7: Reduce Packaging and Repo Hygiene Debt
- Audited repo: `.gitignore` is comprehensive (covers build/, dist/, IDE files, .claude-work/ subdirs).
- Packaging script (`Package-MasterControlOrchestrationServer.ps1`) already handles missing git gracefully (try/catch with "non-git" fallback).
- No accidental clutter identified in tracked files. Forsetti vendor is intentional (475K).
- No removals needed; hygiene is already adequate.

### WS8: Harden External Process Execution
- Replaced sequential pipe reads with concurrent `std::thread` readers for stdout and stderr.
- Added bounded capture (4MB per stream, head-preserved, pipe continues draining after limit).
- Replaced `WaitForSingleObject(INFINITE)` with 5-minute configurable timeout.
- On timeout: `TerminateJobObject` for process tree kill (when job object exists), `TerminateProcess` fallback with documented orphan limitation.
- Corrected execution sequence: wait-before-join (process exit/kill breaks pipes, then threads unblock).
- Added `FormatMessageW`-based error messages for `CreateProcessW` failures.

### WS9: Improve Tests
- Added 4 clearly marked test sections with header comments.
- Provider Identity: ChatGPT/Codex coexistence, distinct providerIds, distinct display names, distinct execution registrations.
- First-Run Templates: `buildDefaultSeededEndpoints()` returns Template status, `buildDefaultProviders()` returns isTemplate=true, snapshot reflects template providers.
- Version Alignment: `MASTERCONTROL_VERSION` macro is defined and contains dot-separated version.
- Progressive Disclosure: Default configuration has `advancedMode=false`.

### WS10: Documentation Remediation
- Revised README "Why this project exists" section to accurately describe the multi-binary architecture and target audience.
- Added architecture subsection describing the four binaries.
- Removed "install once" language, added honest prerequisite description.
- Changed "Single-binary" to "Multi-binary" in highlights table.

## Design Decisions

### providerId vs providerKind
- `providerId` is now the primary resolution key for concrete provider operations.
- `ProviderKind` remains as a family/transport concept. Kind-based fallback preserved for backward compatibility.
- Auto-connect-generated IDs (format: `{providerId}-YYYYMMDD-HHMMSS`) are handled via prefix matching.

### First-run template vs configured distinction
- All 27 seeded endpoints and 3 default providers remain but are marked as templates.
- Templates render with muted styling and "TEMPLATE" badge.
- `upsertProvider` clears `isTemplate` on save, transitioning template to active.

### Guided/basic vs advanced/operator UX split
- `advancedMode` is stored in `AppConfiguration`, persisted to disk, served via API.
- Both shell and browser consume the same runtime-backed flag.
- Default is `false` (basic mode) for new installs.

### Version source of truth
- `VERSION.json` is the single source. CMake reads it at configure time and generates `MasterControlVersion.h`.
- All consumers (bootstrapper, modules, runtime, packaging scripts) derive from this chain.

### CI/release gating
- Windows build workflow must pass for the exact HEAD SHA before releases are created.
- `workflow_dispatch` preserved for manual re-runs but the gate check still fires.

### Process execution refactor
- 5-minute default timeout (not 2 minutes) to accommodate AI CLI tools and installers.
- Head-preserved truncation at 4MB with continued pipe draining.
- Wait-before-join ordering to prevent deadlock on hung children.

## Constraints
- Internal-LAN security posture preserved: `0.0.0.0` bind, TLS disabled, auth disabled, beacon enabled, troubleshooting bypass allowed, open LAN access allowed.
- No framework replacements or WinUI stack changes.
- No whole-product rewrite or architecture redesign.

## Files Touched
- `include/MasterControl/MasterControlModels.h`
- `include/MasterControl/MasterControlVersion.h.in` (new)
- `src/MasterControlApp/MasterControlRuntime.cpp`
- `src/MasterControlApp/MasterControlDefaults.cpp`
- `src/MasterControlApp/MasterControlModels.cpp`
- `src/MasterControlModules/MasterControlModules.cpp`
- `src/MasterControlShell/ProvidersSectionControl.xaml.cpp`
- `src/MasterControlShell/ShellRuntime.h`
- `src/MasterControlShell/ShellRuntime.cpp`
- `src/MasterControlBootstrapper/main.cpp`
- `resources/web/app.js`
- `resources/web/styles.css`
- `CMakeLists.txt`
- `.github/workflows/windows-build-test-package.yml` (new)
- `.github/workflows/repository-maintenance-agents.yml`
- `tests/MasterControlOrchestrationServerTests.cpp`
- `README.md`
- `docs/handoff/CLAUDE_REMEDIATION_20260413.md` (this file)

## Validation Results
- **Configure**: `cmake --preset debug` succeeds, VERSION.json parsed as 0.2.11.
- **Build**: `cmake --build build/debug --config Debug` completes with 0 errors.
- **Tests**: `ctest --test-dir build/debug -C Debug` passes 4/4 suites (including all new remediation test assertions).
- **Security posture**: All defaults verified unchanged via grep.
- **Version alignment**: Generated `MasterControlVersion.h` contains `MASTERCONTROL_VERSION "0.2.11"`.

## Remaining Work
- VERSION.json should be bumped to 0.3.0 (or appropriate) for the remediation release.
- CHANGELOG.md entry for the remediation.
- GitHub Actions workflow should be tested on a real push to main.
- Shell XAML sections for progressive disclosure (WS4) need `Visibility::Collapsed` gating applied in the WinUI shell code — currently implemented in browser surface only.
- Environment hints API pre-fill should be wired into the browser guided workflow form UI for credential auto-detection display.

## Risks / Notes
- The process execution timeout (5 minutes) may need tuning for specific workloads.
- The `TerminateProcess` fallback (no job object) does NOT kill child processes — documented in code.
- The CI workflow assumes `windows-latest` runner has vcpkg and CMake presets compatible with the project. May need adjustment for VS version differences.
- Progressive disclosure in the WinUI shell is specified but not yet implemented in XAML (browser surface is complete).
