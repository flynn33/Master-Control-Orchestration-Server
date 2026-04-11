# Changelog

All notable changes to this repository are tracked here by the repository agents.

## [Unreleased]
- Changes pushed to `main` are promoted into the next numbered release automatically.

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
