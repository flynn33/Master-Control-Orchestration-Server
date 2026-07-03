# Release Gate

PHASE-10 baseline (ADR-002 §10), alpha-stage form (A3.11.0).

## The rule

> **While MCOS is in alpha, no GitHub releases are cut. The same-SHA product gate is the per-commit health check, and it has no manual bypass.**

The tag-triggered release workflow (`release.yml`) was removed at `A3.11.0`: nothing had ever been published through it, all historical release tags were cleared, and GitHub Releases are deferred until MCOS leaves alpha. What remains is enforced by the workflows under `.github/workflows/`:

| Workflow | Triggers | What it does |
|---|---|---|
| `windows-build-test-package.yml` | `push` to `main`, any `pull_request` to `main` | Configure → build → test → package → upload artifact. **Same-SHA product gate.** Cancels older same-SHA runs (concurrency). |
| `forsetti-compliance.yml` | every `push` / `pull_request` / `workflow_dispatch` | Vendored Forsetti manifests + dependencies + architecture + Master Control Forsetti compliance script. |
| `ai-contributor-guard.yml` | every `push` / `pull_request` / `workflow_dispatch` | AI authorship guard (`scripts/github_agents/check_no_ai_contributors.py`). |
| `realignment-discipline.yml` | every `push` / `pull_request` | Realignment hard rules, including "the product gate carries no `workflow_dispatch`". |

## Why no `workflow_dispatch` on the product gate

If `windows-build-test-package.yml` had `workflow_dispatch`, an operator could re-run a failed build to "make it pass" by retrying flake-prone tests until they happen to pass. The honest CI signal requires the build to succeed naturally on the pushed commit. FORBIDDEN-CONTRACT §6.2 (`docs/implementation/FORBIDDEN-CONTRACT-GREP-LIST.md`) greps for the bypass pattern; `realignment-discipline.yml` enforces it in CI.

## Versioning during alpha

Versions follow `<Stage><A>.<Feature>.<patch/hotfix>` — e.g. `A3.11.0` = third alpha, feature 11, patch 0. `VERSION.json` is the single source of truth; `scripts/Sync-RepositoryVersionBadges.ps1` keeps README badges, `vcpkg.json`, and the doc surfaces in sync. The MSI ProductVersion maps from `A<a>.<feature>.<patch>` to `<a>.<feature>.<patch>.0` (see `installer/Build-Msi.ps1`).

## What "shipping" means during alpha

Internal alpha builds are produced two ways:

1. **CI artifact** — every green product-gate run uploads `mastercontrol-release-package` (zip + MSI). Pull the artifact from the run for the commit you want.
2. **Local packaging** — `scripts/Package-MasterControlOrchestrationServer.ps1 -Preset release` on a host with the full toolset (VS2026/v145 for the WinUI Shell, WiX 5 for MSI authoring).

Neither path publishes anything; distribution is operator-managed on the LAN.

## Toolchain hardening (unchanged)

`Resolve-MasterControlToolchain.ps1` and the workflow's "Discover and document toolchain" step both:

- Use `vswhere` for VS install discovery — never a hardcoded edition path.
- Allow Community / BuildTools / Enterprise (the resolver's fallback list does not bias toward any edition; the workflow emits a warning if the resolved install is Enterprise so a regression toward hardcoding can be spotted in CI logs).
- Resolve `cmake.exe`, `ctest.exe`, and `vcpkg.cmake` from the resolved install root.

FORBIDDEN-CONTRACT §6.3 forbids hardcoded `Enterprise` path segments in any workflow file.

## Version stamping ordering (unchanged)

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

Steps 1-5 must succeed before step 6.

## Failure modes and what they mean

| Failure | Likely cause | Action |
|---|---|---|
| `Stamp release version` fails: "VERSION.json missing current_version" | Bad merge or hand-edited VERSION.json | Restore from git history, re-bump cleanly. |
| `Sync-RepositoryVersionBadges.ps1 -CheckOnly` fails | README badges or vcpkg.json drifted from VERSION.json | Run the script without `-CheckOnly` locally and commit the result. |
| `Discover toolchain` warns about Enterprise | CI runner image switched to VS Enterprise | Not gate-blocking; reviewers verify no workflow file hardcodes `Enterprise`. |
| Test step fails | Standard test regression | Fix the regression. Do **not** retry to make it pass. |
| Package step fails: "WiX extensions not found" | Runner missing WiX v5 globals | Install via `dotnet tool install --global wix --version 5.0.2` in a runner-bootstrap step. |
| Bootstrapper preflight reports issues | Stage tree is missing a payload file | Re-check `installer/MasterControlOrchestrationServer.wxs` and the `cmake --install` outputs. |

## When MCOS leaves alpha

Reintroducing published releases is a deliberate decision for the beta/retail transition: restore a tag-triggered release workflow with the same-SHA verification shape (see `docs/archive/realignment-release-reports/PHASE-10-completion-report.md` for the retired design), and re-point FORBIDDEN-CONTRACT §6.2 plus `realignment-discipline.yml` at the restored file.

## See also

- [Packaging and Gateway Binary](Packaging-and-Gateway-Binary.md)
- [Windows Firewall and LAN Mode](Windows-Firewall-LAN-Mode.md)
- [ADR-002](ADR-002-gateway-first-mcp-realignment.md) — section 10
- `.github/workflows/windows-build-test-package.yml`
