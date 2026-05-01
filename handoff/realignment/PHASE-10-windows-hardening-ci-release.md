---
phase: PHASE-10
label: Windows hardening, CI, packaging, and release gate
objective: Make release validation trustworthy and Windows production behavior explicit.
---


# PHASE-10 — Windows Hardening, CI, Packaging, and Release Gate

## Goal

Make the Windows product releasable and trustworthy.

## Required changes

- Add or fix Windows CI configure/build/test/package gate.
- Prevent release workflow dispatch from bypassing same-SHA successful build.
- Use `vswhere`, setup-msbuild, or preset-driven toolchain discovery. Do not hardcode Visual Studio Enterprise path.
- Version stamping must happen before configure/build/package.
- Package gateway binary/dependency strategy.
- Document Windows Firewall/LAN mode behavior.
- Run bootstrapper smoke validation where possible.

## Process execution correction

For process execution code, use this ordering:

1. Start process.
2. Drain stdout/stderr concurrently.
3. `WaitForSingleObject` with timeout.
4. Kill process tree on timeout if needed.
5. Join drain threads.
6. `GetExitCodeProcess`.
7. Cleanup handles.

## Exit criteria

- CI gate blocks release.
- No manual release bypass.
- Packaging includes documented gateway/dependency behavior.

## Read first

- `.github/workflows`
- `CMakePresets.json`
- `vcpkg.json`
- `VERSION.json`
- `installer`
- `scripts`

## Deliverables

- Windows CI build/test/package gate
- No workflow_dispatch bypass
- Version source of truth
- Packaging docs
- Firewall/LAN mode docs
- Bootstrapper smoke check

## Acceptance criteria

- Windows build/test/package gate blocks release
- Manual dispatch cannot bypass successful same-SHA build
- Toolchain discovery avoids hardcoded VS Enterprise path
- Release version stamped before configure/build/package

## Validation

- `CI workflow static review`
- `Local build/test where available`
- `Package script dry run if possible`

