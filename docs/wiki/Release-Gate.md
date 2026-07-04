# Release Gate

This page documents the current alpha release policy and the existing
repository health gate. It does not add new workflow requirements.

## Alpha Policy

While MCOS is in alpha:

- Alpha versions are published as GitHub pre-releases with the MSI installer.
- `VERSION.json` is the version authority.
- Versions use `A{alphaIteration}.{feature}.{patch}`.
- The Windows Build, Test, and Package workflow is the product health gate for
  each pushed commit and pull request.
- Documentation-only work should use `[Unreleased]` in `CHANGELOG.md` unless a
  maintainer explicitly directs a version cut.

Current alpha:

| Field | Value |
|---|---|
| Version | `A3.11.0` |
| Tag | `vA3.11.0` |
| Released | `2026-07-03` |
| MSI ProductVersion mapping | `3.11.0.0` |

The ProductVersion mapping is implemented in `installer/Build-Msi.ps1`.

## Existing Workflows

| Workflow | Purpose |
|---|---|
| `windows-build-test-package.yml` | Configure, build, test, package, and upload the Windows package artifact. |
| `forsetti-compliance.yml` | Run the existing Forsetti compliance script. |
| `ai-contributor-guard.yml` | Enforce repository attribution rules. |
| `realignment-discipline.yml` | Enforce realignment discipline and release-gate invariants. |

The product gate must not use `workflow_dispatch` as a bypass for same-SHA
validation. Repository checks enforce that invariant.

## Local Equivalent

```powershell
cmake --preset release
cmake --build build\release --config Release
ctest --test-dir build\release -C Release --output-on-failure --timeout 300
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\Package-MasterControlOrchestrationServer.ps1 -Preset release -SkipBuild
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\check-mastercontrol-forsetti.ps1
```

## Toolchain Notes

The repository scripts discover the Visual Studio installation instead of
hardcoding an edition path. The configured project may still require a newer
toolset than a local host has installed; record that as an environment blocker,
not as a documentation claim.

## Failure Modes

| Failure | Likely cause | Action |
|---|---|---|
| Version metadata check fails | README, `vcpkg.json`, or docs drifted from `VERSION.json`. | Run the existing sync/check scripts and commit the correction. |
| Configure fails | Missing CMake, vcpkg, SDK, or Visual Studio components. | Repair the toolchain and rerun configure. |
| Build fails with missing platform toolset | Host lacks the configured MSVC toolset. | Install the required toolset or validate on a host that has it. |
| Test fails | Product regression or environment-specific dependency failure. | Fix the regression or document the exact environment blocker. |
| MSI build fails | WiX v5 or required extensions missing. | Install WiX v5 and rerun the package script. |
| Bootstrapper preflight fails | Staged package is incomplete or install prerequisites are missing. | Inspect the preflight JSON and package inputs. |

## Related Pages

[Packaging and Gateway Binary](Packaging-and-Gateway-Binary) |
[Operations](Operations) |
[Versions](Versions) |
[Troubleshooting](Troubleshooting)
