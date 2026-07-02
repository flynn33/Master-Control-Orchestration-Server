# Release Gate

PHASE-10 baseline (ADR-002 §10).

## The rule

> **Releases ship only when CI succeeded on the same SHA. There is no manual bypass.**

This is enforced by two GitHub Actions workflows, both under `.github/workflows/`:

| Workflow | Triggers | What it does |
|---|---|---|
| `windows-build-test-package.yml` | `push` to `main`, any `pull_request` to `main` | Configure → build → test → package → upload artifact. **Same-SHA product gate.** Cancels older same-SHA runs (concurrency). |
| `release.yml` | `push` of a `v*` tag | Verifies the same-SHA product gate succeeded, downloads the gating artifact, publishes the GitHub release. |
| `forsetti-compliance.yml` | every `push` / `pull_request` / `workflow_dispatch` | Vendored Forsetti manifests + dependencies + architecture + Master Control Forsetti compliance script. |
| `ai-contributor-guard.yml` | every `push` / `pull_request` / `workflow_dispatch` | AI authorship guard (`scripts/github_agents/check_no_ai_contributors.py`). |

The Forsetti and AI-attribution workflows are advisory + governance gates. The release-blocking pair is `windows-build-test-package.yml` + `release.yml`.

## Why no `workflow_dispatch` on the product gate or release workflow

If `windows-build-test-package.yml` had `workflow_dispatch`, an operator could re-run a failed build to "make it pass" by retrying flake-prone tests until they happen to pass. The `release.yml` lookup would then find a successful run for the same SHA, even though that success was manufactured. The honest CI gate requires the build to succeed naturally on the merge commit.

If `release.yml` had `workflow_dispatch`, an operator could publish a release without ever passing CI. The same-SHA verification step is bypassable by simply not triggering on tag push.

The current workflows have file-level comments documenting both rules. FORBIDDEN-CONTRACT §6.2 (`docs/implementation/FORBIDDEN-CONTRACT-GREP-LIST.md`) greps for the bypass pattern in both files. A reviewer who tries to add `workflow_dispatch` to either workflow will trip the grep.

## Tag → release flow

1. **Operator merges a PR.** The product gate runs on the merge commit. It must succeed.
2. **Operator bumps `VERSION.json`** (PHASE-10 is the first phase allowed to do this; subsequent feature releases follow the same `current_version` / `current_tag` / `released_at` schema).
3. **Operator runs `scripts/Sync-RepositoryVersionBadges.ps1`** to regenerate `README.md` badges and `vcpkg.json` `version-string`. Commits the result.
4. **Operator pushes a `v<version>` tag** matching `VERSION.json`'s `current_version`.
5. **`release.yml` triggers** on the tag push.
6. **`release.yml` verifies the same-SHA product gate completed with `conclusion=success`.** If not, it errors with "Release refused" and exits non-zero.
7. **`release.yml` downloads the gating artifact** (`mastercontrol-release-package`) — the build is NOT redone, because that would change the SHA-to-artifact relationship.
8. **`release.yml` validates the artifact** (`MasterControlServiceHost.exe`, `MasterControlBootstrapper.exe`, MSI) and publishes a GitHub release with `gh release create`.
9. If a release for the tag already exists (manual edit, race condition), `gh release create` fails. The workflow does not silently overwrite.

## Toolchain hardening

`Resolve-MasterControlToolchain.ps1` and the workflow's "Discover and document toolchain" step both:

- Use `vswhere` for VS install discovery — never a hardcoded edition path.
- Allow Community / BuildTools / Enterprise (the resolver's fallback list does not bias toward any edition; the workflow emits a warning if the resolved install is Enterprise so a regression toward hardcoding can be spotted in CI logs).
- Resolve `cmake.exe`, `ctest.exe`, and `vcpkg.cmake` from the resolved install root.

FORBIDDEN-CONTRACT §6.3 forbids the literal string `Enterprise` in any workflow file (since CI machines are typically Community/BuildTools and any Enterprise-specific path would silently break the gate on the supplied runner pool).

## Version stamping ordering

PHASE-10 acceptance criterion: "Release version stamped before configure/build/package."

The product gate's pipeline order is:

1. Checkout
2. **Stamp release version from VERSION.json** (`MASTERCONTROL_VERSION` env var)
3. Discover toolchain
4. Bootstrap vcpkg
5. Check repository version metadata (badge sync, vcpkg.json sync)
6. Configure
7. Build
8. Test
9. Package
10. Smoke validation
11. Bootstrapper preflight
12. Upload artifact

Steps 1-5 must succeed before step 6. The version is therefore stamped, validated for badge sync, and emitted as `MASTERCONTROL_VERSION` before any build step runs.

## Failure modes and what they mean

| Failure | Likely cause | Action |
|---|---|---|
| `Stamp release version` fails: "VERSION.json missing current_version" | Bad merge or hand-edited VERSION.json | Restore from git history, re-bump cleanly. |
| `Sync-RepositoryVersionBadges.ps1 -CheckOnly` fails | README badges or vcpkg.json drifted from VERSION.json | Run the script without `-CheckOnly` locally and commit the result. |
| `Discover toolchain` warns about Enterprise | CI runner image switched to VS Enterprise | No release-blocking action; reviewers verify no workflow file hardcodes `Enterprise`. |
| Test step fails | Standard test regression | Fix the regression. Do **not** retry to make it pass. |
| Package step fails: "WiX extensions not found" | Runner missing WiX v5 globals | Install via `dotnet tool install --global wix --version 5.0.2` in a runner-bootstrap step. |
| Bootstrapper preflight reports issues | Stage tree is missing a payload file | Re-check `installer/MasterControlOrchestrationServer.wxs` and the `cmake --install` outputs. |
| Release-job: "No Windows product gate run found for SHA" | Tag pushed without a corresponding merge commit on `main` | Don't tag arbitrary commits — tag the head of `main` after CI succeeds. |
| Release-job: "Windows product gate failed for SHA" | The merge commit's CI never succeeded | Fix the build, push a fix commit, let CI succeed, then tag. |

## See also

- [Packaging and Gateway Binary](Packaging-and-Gateway-Binary.md)
- [Windows Firewall and LAN Mode](Windows-Firewall-LAN-Mode.md)
- [ADR-002](ADR-002-gateway-first-mcp-realignment.md) — section 10
- `.github/workflows/windows-build-test-package.yml`
- `.github/workflows/release.yml`
