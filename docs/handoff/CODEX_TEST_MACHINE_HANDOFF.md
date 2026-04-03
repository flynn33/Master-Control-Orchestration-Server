# Master Control Orchestration Server Test-Machine Handoff

## Project Identifier
- Product: `Master Control Orchestration Server`
- Repository: `flynn33/master-control-dashboard`
- Remote: `https://github.com/flynn33/master-control-dashboard.git`
- Primary branch: `main`

## Current Objective
Move active validation and analysis onto the test machine, using a local clone of the repository and the current release package, then produce a detailed install and runtime report.

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
- `D:\Repos\Master-Control-Dashboard`

Suggested commands:

```powershell
git clone https://github.com/flynn33/master-control-dashboard.git D:\Repos\Master-Control-Dashboard
cd D:\Repos\Master-Control-Dashboard
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
- Added retry handling for transient payload-copy locks during install staging.
- Improved setup-wrapper correlation with bootstrapper logs.
- Reworked telemetry into a denser monitoring deck.
- Expanded wizard-first workflows across shell and browser.
- Made CLU documentation explicit in README/wiki.
- Fixed the desktop shell title bar so the packaged shell configures a custom draggable title surface.

## Important Files And Modules
Installer / packaging:
- `src/MasterControlBootstrapper/main.cpp`
- `src/MasterControlBootstrapper/setup_main.cpp`
- `scripts/Package-MasterControlOrchestrationServer.ps1`
- `scripts/Test-MasterControlOrchestrationServerDeployment.ps1`

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
On the source machine, the following passed before handoff:
- Visual Studio 2022 release solution build
- `ctest` in `Release`, `4/4` passed
- release packaging
- packaged mixed deployment acceptance
- packaged shell launch with startup log confirming custom draggable title-bar wiring

Key local validation expectations you can re-run on the test machine:

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\devenv.com" "D:\Repos\Master-Control-Dashboard\build\release\MasterControlOrchestrationServer.sln" /Build "Release|x64"
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe" --test-dir "D:\Repos\Master-Control-Dashboard\build\release" -C Release --output-on-failure
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "D:\Repos\Master-Control-Dashboard\scripts\Package-MasterControlOrchestrationServer.ps1" -Preset release
```

## Test-Machine Tasks
1. Clone the remote repo to a clean local folder.
2. Confirm the worktree is clean.
3. Build in Visual Studio 2022 `Release|x64`.
4. Run `ctest` in `Release`.
5. Rebuild the release package locally if needed.
6. Test the packaged install path through `Install Master Control Orchestration Server.exe`.
7. Confirm logs are written to the real desktop, not a redirected OneDrive desktop.
8. Confirm the service installs and validates correctly.
9. Launch the desktop shell and confirm the window can be dragged.
10. Open the browser surface and inspect telemetry, CLU, runtime, providers, security, and settings.
11. Save a comprehensive Markdown report to the desktop.

## Specific Behaviors To Verify
Installer:
- first-run installer entry point is obvious
- install location selection appears
- visible progress appears
- setup does not falsely report a startup failure
- desktop logs are written to the actual local desktop path

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
- Windows Server 2022 validation is still needed.
- The setup wrapper had a prior false-failure / shell-return inconsistency and should be watched closely during testing.
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
- include screenshots if the issue is visual
- include whether the failure was in setup launcher, bootstrapper, service, shell, or browser
- do not delete any relevant artifacts before the report is saved
