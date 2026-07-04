# Versions

![current](https://img.shields.io/badge/current-vA3.11.0-00f6ff?style=flat-square)

This page explains the current alpha-stage version scheme, the active version,
and where historical release records live. `VERSION.json` is the version
authority.

## Current Version

| Field | Value |
|---|---|
| **Version** | `vA3.11.0` |
| Current version | `A3.11.0` |
| Current tag | `vA3.11.0` |
| Released | `2026-07-03` |
| Channel | Internal alpha |
| Previous expression for the same tree | `0.11.0-alpha.3` |
| Release policy | No GitHub Releases during alpha. |

`A3.11.0` re-expresses the same source tree that was previously described as
`0.11.0-alpha.3`. The change is a versioning and release-policy alignment, not
a runtime feature change by itself.

## Alpha-Stage Scheme

While MCOS is in alpha, versions use:

```text
A{alphaIteration}.{feature}.{patch}
```

For `A3.11.0`:

| Segment | Meaning |
|---|---|
| `A` | Alpha-stage channel. |
| `3` | Third alpha iteration. |
| `11` | Feature line. |
| `0` | Patch/hotfix number. |

`installer/Build-Msi.ps1` maps this to a four-part Windows Installer
ProductVersion. `A3.11.0` maps to `3.11.0.0`.

## Release Policy During Alpha

- `VERSION.json` owns `current_version`, `current_tag`, `released_at`, and
  `last_release_commit`.
- `CHANGELOG.md` records human-readable release history and `[Unreleased]`
  documentation or maintenance changes.
- GitHub Releases are deferred until MCOS leaves alpha.
- The Windows Build, Test, and Package workflow is the per-commit health gate.
- Historical semver-alpha entries remain historical and should not be presented
  as the current release.

## Historical Highlights

| Version | Date | Historical theme |
|---|---|---|
| `A3.11.0` | 2026-07-03 | Alpha-stage scheme migration for the `0.11.0-alpha.3` tree. |
| `0.11.0-alpha.3` | 2026-07-02 | PHASE-14 diagnostics slices B-E, security hardening, and 2026-06 bug-campaign fixes. |
| `0.11.0-alpha.2` | 2026-05-15 | Optional gateway TLS dual-bind. |
| `0.11.0-alpha.1` | 2026-05-15 | First internal alpha package for the v0.11.0 line. |
| `0.11.0` | 2026-05-15 | Diagnostics Slice A route surface. |
| `0.9.x` through `0.10.x` | 2026-05 | Gateway, discovery, worker-pool, telemetry, and packaging iterations. |
| `0.7.0` | 2026-05-05 | Gateway-first architecture milestone. |
| `0.6.x` | 2026-05 | Realignment phase delivery and native gateway maturation. |
| `0.5.0` | 2026-04-25 | LAN client control-plane model. |

For full entries, read `VERSION.json`, `CHANGELOG.md`, and retained reports
under `docs/archive/realignment-release-reports/`.

## Maintainer Runbook

Documentation-only changes should normally stay under `[Unreleased]`:

```text
## [Unreleased]
```

Do not change `VERSION.json.current_version` unless the maintainer directs a
version cut. When a version cut is directed:

1. Update `VERSION.json`.
2. Move relevant `[Unreleased]` changelog content under the new version heading.
3. Run existing metadata checks.
4. Build, test, and package through the Windows product gate.
5. Tag only when the version is intentionally cut.

## Common Questions

### Is `vA3.11.0` newer than `v0.11.0-alpha.3`?

It names the same tree in the current alpha-stage scheme. Treat
`vA3.11.0` as the current version string.

### Are older `v0.*` strings wrong?

They are valid in historical release notes, archive reports, and changelog
history. They should not appear as the current release in README, Home,
Release Gate, or live wiki front-door pages.

### Where is the package version transformed for MSI?

`installer/Build-Msi.ps1`, function `ConvertTo-MsiProductVersion`.

## Related Pages

[Release Gate](Release-Gate) |
[Packaging and Gateway Binary](Packaging-and-Gateway-Binary) |
[Operations](Operations) |
[Home](Home)
