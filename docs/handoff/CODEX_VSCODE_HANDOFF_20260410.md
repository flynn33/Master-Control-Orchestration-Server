# Master Control Orchestration Server VS Code Codex Handoff

## Project Identifier
- Product: `Master Control Orchestration Server`
- Repository: `flynn33/master-control-dashboard`
- Remote: `https://github.com/flynn33/master-control-dashboard.git`
- Workspace: `G:\Codex\MCOS\MCOS-Repo`
- Branch: `main`
- Current committed baseline: `fe1bc2b3ef01f28f40b75ba93c226e404fb05762`
- Baseline commit summary: `fe1bc2b Add IDE deployment acceptance targets`
- Handoff date: `2026-04-10`

## Purpose Of This Handoff
This note is for the Codex instance running inside VS Code on this machine. Use it to continue the project from the current workspace state without re-triaging the IDE workflow, packaged validation status, or the known remaining gaps.

## Required Working Rules
- Use the local repository first, per `AGENTS.md`.
- Use Visual Studio or Visual Studio Code for build, test, and debug.
- Treat terminal-only runs as supporting diagnostics, not the final source of truth.
- Use the generated solution at `build\release\MasterControlOrchestrationServer.slnx` as the main validation entry point.
- Managed acceptance only counts when it is launched from an elevated IDE or elevated shell context.

## Current Objective
Continue from the already-validated IDE baseline and finish the remaining real-product checks:
- manual packaged shell validation
- manual browser/dashboard validation
- persistent installer-log usefulness during a real failure or recovery scenario
- Windows Server 2022 validation

## Current Worktree State
The worktree is intentionally dirty right now. As of `2026-04-10`, `git status --short --branch` shows:

```text
## main...origin/main
 M docs/handoff/CODEX_TEST_MACHINE_HANDOFF.md
 D docs/handoff/CODEX_TEST_MACHINE_PROGRESS_20260403.md
?? .vscode/
?? docs/handoff/CODEX_TEST_MACHINE_PROGRESS_20260405.md
?? scripts/Invoke-MasterControlOrchestrationServerIdeTarget.ps1
```

These changes are intentional and should be reviewed before any commit:
- updated handoff state in `docs/handoff/CODEX_TEST_MACHINE_HANDOFF.md`
- removal of stale progress note `docs/handoff/CODEX_TEST_MACHINE_PROGRESS_20260403.md`
- replacement progress note `docs/handoff/CODEX_TEST_MACHINE_PROGRESS_20260405.md`
- VS Code task support in `.vscode/tasks.json`
- VS Code helper script `scripts/Invoke-MasterControlOrchestrationServerIdeTarget.ps1`

## IDE Entry Points
Preferred VS Code tasks:
- `MCOS: Build Release`
- `MCOS: RUN_TESTS`
- `MCOS: IDE_ACCEPTANCE_MIXED`
- `MCOS: IDE_ACCEPTANCE_MANAGED`
- `MCOS: IDE_ACCEPTANCE_BOTH`

These tasks are defined in:
- `.vscode/tasks.json`

They call:
- `scripts/Invoke-MasterControlOrchestrationServerIdeTarget.ps1`

That helper drives:
- `build\release\MasterControlOrchestrationServer.slnx`

## Validation Already Completed
The following authoritative validations already passed on this machine on `2026-04-05`:

Visual Studio solution lane:
- `Release|x64` build passed.
- `RUN_TESTS` passed.
- Test result: `4/4` passed.

Packaged IDE acceptance lane:
- `IDE_ACCEPTANCE_MIXED` passed.
- `IDE_ACCEPTANCE_MANAGED` passed from an elevated VS Code session.
- `IDE_ACCEPTANCE_BOTH` passed from an elevated admin shell against the generated solution target.

Important artifacts:
- `dist\packages\release\ide-acceptance-mixed.json`
- `dist\packages\release\ide-acceptance-mixed.md`
- `dist\packages\release\ide-acceptance-managed.json`
- `dist\packages\release\ide-acceptance-managed.md`
- `dist\packages\release\ide-acceptance-both.json`
- `dist\packages\release\ide-acceptance-both.md`

Packaged release artifacts:
- `dist\packages\release\MasterControlOrchestrationServer-v0.1.59-win-x64`
- `dist\packages\release\MasterControlOrchestrationServer-v0.1.59-win-x64.zip`

## Persistent Diagnostics
Persistent installer log root:
- `C:\Users\Flynn\AppData\Local\Master Control Orchestration Server\logs\installer`

Important files there:
- `installer-history.jsonl`
- `installer-failures.jsonl`
- `installer-latest.json`
- `installer-latest-failure.json`

This logging was added specifically so Codex can compare failures across runs instead of relying only on one-off desktop logs.

## Important Files To Know First
High-value docs and handoff notes:
- `AGENTS.md`
- `docs/handoff/CODEX_TEST_MACHINE_HANDOFF.md`
- `docs/handoff/CODEX_TEST_MACHINE_PROGRESS_20260405.md`

IDE and deployment workflow:
- `.vscode/tasks.json`
- `scripts/Invoke-MasterControlOrchestrationServerIdeTarget.ps1`
- `scripts/Invoke-MasterControlOrchestrationServerIdeAcceptance.ps1`
- `scripts/Test-MasterControlOrchestrationServerDeployment.ps1`
- `scripts/Package-MasterControlOrchestrationServer.ps1`
- `scripts/Resolve-MasterControlToolchain.ps1`

Installer and logging:
- `src/MasterControlBootstrapper/main.cpp`
- `src/MasterControlBootstrapper/setup_main.cpp`
- `src/MasterControlBootstrapper/InstallerLogSupport.h`

Runtime and shell:
- `src/MasterControlApp/MasterControlRuntime.cpp`
- `src/MasterControlShell/MainWindow.xaml`
- `src/MasterControlShell/MainWindow.xaml.cpp`
- `src/MasterControlShell/ShellRuntime.cpp`

Tests:
- `tests/MasterControlOrchestrationServerTests.cpp`

## Known Constraints And Decisions
- This machine uses Visual Studio `18\Community`.
- The repo was adjusted so tool discovery does not assume Visual Studio 2022 paths only.
- `pwsh.exe` is not guaranteed on `PATH`; Windows PowerShell support matters here.
- The Windows service legacy name `MasterControlProgram` is intentional for compatibility.
- Terminal runs can help diagnose issues, but IDE-driven validation is the authoritative path on this machine.

## Remaining Gaps
- Manual browser/dashboard inspection is still outstanding.
- Manual packaged shell drag/title-bar recheck is still outstanding.
- A real failure/recovery exercise should be used to confirm the persistent installer logs are actually helpful in practice.
- Windows Server 2022 validation is still outstanding.

## Suggested Next Steps For VS Code Codex
1. Review the dirty worktree and confirm the handoff/task-support changes are the exact set you want to keep.
2. Launch the packaged shell and manually verify the title bar drag behavior.
3. Open the local browser dashboard and inspect telemetry, CLU, runtime, providers, security, and settings.
4. If you hit any installer or runtime issue, capture the persistent log set under `C:\Users\Flynn\AppData\Local\Master Control Orchestration Server\logs\installer` before changing anything else.
5. When the manual checks are complete, decide whether to commit the current handoff/task-support changes.
6. After local manual validation is satisfactory, run the same acceptance harness on a Windows Server 2022 host.

## Short Resume Point
If you need the shortest possible resume instruction for the next Codex instance:

```text
Open G:\Codex\MCOS\MCOS-Repo in admin VS Code, trust the IDE tasks in .vscode\tasks.json, treat the April 5 2026 mixed/managed/both acceptance reports as the current validated baseline, review the still-uncommitted handoff/task-support files, then continue with manual shell/browser validation and Windows Server 2022 coverage.
```
