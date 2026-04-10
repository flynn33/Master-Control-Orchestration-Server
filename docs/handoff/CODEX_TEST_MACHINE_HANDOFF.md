# Master Control Orchestration Server Test-Machine Handoff

## Project Identifier
- Product: `Master Control Orchestration Server`
- Repository: `flynn33/master-control-dashboard`
- Remote: `https://github.com/flynn33/master-control-dashboard.git`
- Primary branch: `main`
- Current validation baseline: `fe1bc2b3ef01f28f40b75ba93c226e404fb05762`
- Current local validation workspace: `G:\Codex\MCOS\MCOS-Repo`

## Current Objective
Keep the test-machine handoff aligned with the current repo state: the IDE-driven build, test, package, and full packaged acceptance baseline are complete on this machine, and the remaining work is manual UI validation plus Windows Server 2022 coverage.

## Authoritative Workflow Rule
- Use Visual Studio or Visual Studio Code for build, test, and debug.
- Treat terminal-only runs as supporting diagnostics, not final validation.
- Use the generated solution at `build\release\MasterControlOrchestrationServer.slnx` as the primary validation entry point on this machine.

## Current Handoff Companion Note
Use the refreshed progress note in this folder as the live state document:
- `docs\handoff\CODEX_TEST_MACHINE_PROGRESS_20260405.md`

## VS Code Entry Point
The repository now includes VS Code tasks for the IDE validation targets:
- `.vscode\tasks.json`

Recommended admin VS Code tasks:
- `MCOS: RUN_TESTS`
- `MCOS: IDE_ACCEPTANCE_MIXED`
- `MCOS: IDE_ACCEPTANCE_MANAGED`
- `MCOS: IDE_ACCEPTANCE_BOTH`

## What This Software Is
Master Control Orchestration Server is a Forsetti-compliant Windows orchestration control plane.

It provides:
- a Windows service host
- a WinUI desktop shell
- a browser admin surface
- CLU governance and routing
- multi-model AI provider integration
- MCP server authoring and hosting
- sub-agent and sub-agent group management
- platform governance lanes for Windows, macOS, and iOS
- Apple build, signing, notarization, export, and deployment workflows
- guided setup workflows for most operator tasks

## What It Is Supposed To Do
The product is meant to make operator setup and orchestration easier through guided workflows rather than raw admin editing.

The current build should support:
- installing through an obvious setup executable
- running a background Windows service
- launching a draggable desktop shell window
- serving a local browser dashboard
- exposing CLU, telemetry, runtime, providers, security, and settings surfaces
- guided setup for providers, MCP servers, sub-agents, sub-agent groups, Apple hosts, Forsetti modules, settings, security, imports, and runtime-lane maintenance

## Clone Instructions For The Test Machine
Recommended local clone path:
- `G:\Codex\MCOS\MCOS-Repo`

Suggested commands:

```powershell
git clone https://github.com/flynn33/master-control-dashboard.git G:\Codex\MCOS\MCOS-Repo
cd G:\Codex\MCOS\MCOS-Repo
git checkout main
git pull --ff-only
git status
```

Expected result:
- branch should be `main`
- worktree should be clean immediately after clone

## Current Worktree Expectation
After cloning the remote repository, the worktree should be clean.

If it is not clean:
- stop and record the unexpected files
- include them in the test report

## Current Release / Package Expectations
The package is built from the same repo and should contain:
- `Install Master Control Orchestration Server.exe`
- `MasterControlOrchestrationServerSetup.exe`
- `MasterControlBootstrapper.exe`
- `MasterControlShell.exe`
- `START-HERE.txt`
- `INSTALL.txt`
- `PACKAGE-METADATA.json`

The intended first-run installer entry point is:
- `Install Master Control Orchestration Server.exe`

The PowerShell fallback is:
- `Install-MasterControlOrchestrationServer.ps1`

## Important Compatibility Constraints
- The visible product name is `Master Control Orchestration Server`.
- Some legacy internal identifiers intentionally remain for upgrade compatibility.
- The Windows service name still remains `MasterControlProgram`.
- The uninstall registry identity may still use legacy compatibility identifiers.
- Those legacy names are intentional and should not be treated as defects by themselves.

## Completed Work Relevant To This Handoff
- Added obvious packaged installer entry point and start-here instructions.
- Fixed installer compatibility when a legacy install already exists under `C:\Program Files\Master Control Program`.
- Added installer desktop logs for setup and bootstrapper actions.
- Changed installer logging to prefer the real local desktop path derived from `USERPROFILE`, instead of relying on redirected desktop resolution.
- Added persistent installer JSON and JSONL logging under `C:\Users\Flynn\AppData\Local\Master Control Orchestration Server\logs\installer`.
- Added retry handling for transient payload-copy locks during install staging.
- Improved setup-wrapper correlation with bootstrapper logs.
- Added Visual Studio-aware toolchain discovery so repo scripts work on Visual Studio `18\Community`.
- Added solution-visible package and deployment acceptance targets for IDE-driven validation.
- Reworked telemetry into a denser monitoring deck.
- Expanded wizard-first workflows across shell and browser.
- Made CLU documentation explicit in README/wiki.
- Fixed the desktop shell title bar so the packaged shell configures a custom draggable title surface.

## Important Files And Modules
Installer / packaging:
- `src/MasterControlBootstrapper/main.cpp`
- `src/MasterControlBootstrapper/setup_main.cpp`
- `src/MasterControlBootstrapper/InstallerLogSupport.h`
- `scripts/Package-MasterControlOrchestrationServer.ps1`
- `scripts/Test-MasterControlOrchestrationServerDeployment.ps1`
- `scripts/Invoke-MasterControlOrchestrationServerIdeAcceptance.ps1`
- `scripts/Invoke-MasterControlOrchestrationServerIdeTarget.ps1`
- `.vscode/tasks.json`

Shell drag / shell UX:
- `src/MasterControlShell/MainWindow.xaml`
- `src/MasterControlShell/MainWindow.xaml.cpp`
- `src/MasterControlShell/MainWindow.xaml.h`

Regression coverage:
- `tests/MasterControlOrchestrationServerTests.cpp`

Core product surfaces:
- `src/MasterControlApp/MasterControlRuntime.cpp`
- `src/MasterControlShell/ShellRuntime.cpp`
- `src/MasterControlShell/TelemetrySectionControl.xaml`
- `resources/web/app.js`
- `resources/web/styles.css`

Forsetti manifests:
- `src/MasterControlModules/Resources/ForsettiManifests`

## Local Validation Already Completed
On this test machine, the following already passed:
- Visual Studio release solution build
- `RUN_TESTS` in the generated solution, `4/4` passed
- release packaging
- `IDE_ACCEPTANCE_MIXED` in the generated solution
- `IDE_ACCEPTANCE_MANAGED` in an elevated VS Code session
- `IDE_ACCEPTANCE_BOTH` from an elevated admin shell using the generated solution target

Use these IDE-owned validation targets on the test machine:

```text
build\release\MasterControlOrchestrationServer.slnx
  RUN_TESTS
  IDE_PACKAGE
  IDE_ACCEPTANCE_MIXED
  IDE_ACCEPTANCE_MANAGED
  IDE_ACCEPTANCE_BOTH
```

If you are using VS Code, prefer the repo tasks instead of retyping commands:

```text
Terminal -> Run Task
  MCOS: RUN_TESTS
  MCOS: IDE_ACCEPTANCE_MIXED
  MCOS: IDE_ACCEPTANCE_MANAGED
  MCOS: IDE_ACCEPTANCE_BOTH
```

Supporting commands, when you need them for diagnostics or automation:

```powershell
& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\devenv.com" "G:\Codex\MCOS\MCOS-Repo\build\release\MasterControlOrchestrationServer.slnx" /Build "Release|x64"
& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\devenv.com" "G:\Codex\MCOS\MCOS-Repo\build\release\MasterControlOrchestrationServer.slnx" /Build "Release|x64" /Project RUN_TESTS
& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\devenv.com" "G:\Codex\MCOS\MCOS-Repo\build\release\MasterControlOrchestrationServer.slnx" /Build "Release|x64" /Project IDE_ACCEPTANCE_MIXED
& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\devenv.com" "G:\Codex\MCOS\MCOS-Repo\build\release\MasterControlOrchestrationServer.slnx" /Build "Release|x64" /Project IDE_ACCEPTANCE_MANAGED
& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\devenv.com" "G:\Codex\MCOS\MCOS-Repo\build\release\MasterControlOrchestrationServer.slnx" /Build "Release|x64" /Project IDE_ACCEPTANCE_BOTH
```

Current packaged acceptance artifacts on this machine:
- `dist\packages\release\ide-acceptance-mixed.json`
- `dist\packages\release\ide-acceptance-mixed.md`
- `dist\packages\release\ide-acceptance-managed.json`
- `dist\packages\release\ide-acceptance-managed.md`
- `dist\packages\release\ide-acceptance-both.json`
- `dist\packages\release\ide-acceptance-both.md`

## Test-Machine Tasks
1. Clone the remote repo to a clean local folder.
2. Confirm the worktree is clean.
3. Build `Release|x64` in Visual Studio or VS Code.
4. Run `RUN_TESTS` from the generated solution.
5. Use `IDE_PACKAGE` and `IDE_ACCEPTANCE_MIXED` as the non-admin packaged validation path.
6. Use `IDE_ACCEPTANCE_MANAGED` only from an elevated Visual Studio or VS Code session.
7. In VS Code, use the repo task `MCOS: IDE_ACCEPTANCE_MANAGED` instead of a handwritten command.
8. Use `IDE_ACCEPTANCE_BOTH` to refresh the full packaged acceptance baseline on the current commit when acceptance needs to be re-run.
9. Test the packaged install path through `Install Master Control Orchestration Server.exe`.
10. Confirm persistent installer logs are written under `C:\Users\Flynn\AppData\Local\Master Control Orchestration Server\logs\installer`.
11. Confirm the service installs and validates correctly.
12. Launch the desktop shell and confirm the window can be dragged.
13. Open the browser surface and inspect telemetry, CLU, runtime, providers, security, and settings.
14. Save a comprehensive Markdown report to the desktop.

## Specific Behaviors To Verify
Installer:
- first-run installer entry point is obvious
- install location selection appears
- visible progress appears
- setup does not falsely report a startup failure
- desktop logs are written to the actual local desktop path
- persistent installer logs capture failures across runs

Installed system:
- service registration and running state
- browser responds on the configured local address
- shell launches
- shell window can be dragged by the title region
- Start Menu shortcut launches the shell

Product surfaces:
- telemetry fills the section as a dense monitoring deck
- CLU surface is visible and interactive
- wizard-first setup flows are obvious
- shell and browser guided workflows both function

## Known Risks / Remaining Gaps
- Manual browser/dashboard inspection is still needed.
- Manual packaged shell drag/title-bar recheck is still needed.
- Windows Server 2022 validation is still needed.
- The setup wrapper had a prior false-failure / shell-return inconsistency and should still be watched closely during testing.
- Installer UX is much better than before, but still should be judged critically as a real product installer.

## Required Report Output
Save the final report to the desktop as:
- `MasterControlOrchestrationServer-TestReport-<timestamp>.md`

The report should include:
- machine info
- clone path
- repo head commit
- worktree status
- build result
- test result
- package path tested
- exact installer entry point used
- install result
- desktop log file paths
- service/browser/shell validation
- CLU, telemetry, and wizard workflow findings
- bugs, regressions, and recommendations

## Next-Step Handoff Rule
If the test machine finds a failure:
- include the exact desktop log paths
- include the persistent installer log paths
- include screenshots if the issue is visual
- include whether the failure was in setup launcher, bootstrapper, service, shell, or browser
- do not delete any relevant artifacts before the report is saved
