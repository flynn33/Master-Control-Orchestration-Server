# Repo Cleanup — Windows Validation Pass — Completion Report

> **SUPERSEDED IN PART (2026-07-03, same day, operator directive):** §Task 4's "cut v0.11.0-alpha.3 by pushing a tag" instructions no longer apply. The operator directed: no GitHub releases during alpha, `release.yml` removed, all release tags cleared, and version scheme migrated to `<Stage><A>.<Feature>.<patch/hotfix>` (current: `A3.11.0`). See `CHANGELOG.md` §[A3.11.0] and `docs/wiki/Release-Gate.md` for the new policy. Everything else in this report (gates, build/test evidence, hashes, hook sanity) remains accurate for the state it describes.

**Date:** 2026-07-03
**Host:** Windows 11 Pro (10.0.26100), Visual Studio 2022 Community 17.14.34 (v143), CMake 3.31.6-msvc6, Windows PowerShell 5.1.26100.8328, PowerShell 7.5.2 (portable, session-local), VS-bundled vcpkg (`VC\vcpkg`).
**Executed against:** fresh clone of `main` @ `5fc7573e0a3e45c74480d3d4967f201f11ee75fa` at `D:\GitHub\MCOS`.

## Branch and commits

- `repo-cleanup/metadata-docs-ci` was merged to `main` as PR #16 (merge commit `5fc7573`) and the branch deleted from the remote before this pass began. Validation therefore ran on `main`, whose tree contains the branch content. This is the only deviation from the handoff's "checkout of `repo-cleanup/metadata-docs-ci`" precondition.
- **No commits were created by this pass.** The working tree carries exactly one uncommitted file: this report. Git identity (`user.name`/`user.email`) is not configured on this host and GitHub push credentials are not available non-interactively, so nothing could be committed or pushed regardless.

## Environment deviations from the handoff preconditions

| Precondition | Actual | Impact |
|---|---|---|
| VS2026 / v145 toolset | VS2022 / v143 only | `MasterControlShell` (WinUI, pinned to v145 in the vcxproj and in `src/MasterControlShell/CMakeLists.txt:147`) cannot build on this host. All other targets build with v143. |
| `pwsh` 7.x installed | Not installed | Used portable PowerShell 7.5.2 in the session scratchpad; nothing installed system-wide. |
| WiX for MSI authoring | Not installed | Local MSI packaging not possible (moot — see Task 4). |

## Checkout normalization (read before comparing hashes)

The clone was initially smudged to CRLF by the host's global `core.autocrlf=true` (the repo has no `.gitattributes`). Two symptoms: the five protected-path working-tree hashes did not match the handoff table, and `Test-MCOSMarkdownLinks.ps1` failed on 5 code-sample shapes in `docs/archive/proof-of-working/10-ui-automation.md` (its fence-blanking regex anchors on `$`, which does not match `\r\n`). Blob-level verification (`git show HEAD:<file> | sha256sum`) matched the handoff table exactly, proving content integrity; the mismatch was purely line-ending smudge.

Remedy: local `core.autocrlf=false`, tracked files rewritten from the index (LF), stat cache refreshed with `git add --renormalize .` — which staged **zero** changes, confirming byte-identity with HEAD. No content was modified.

## Protected-path hash comparison (hard rule 1)

Working-tree SHA-256 after normalization, re-verified **after all work completed** — all five match the handoff table byte-for-byte:

```text
3101a130a96e126bf2619c88e214c5fe7497441c289cfe6443807332cc97c462  .github/workflows/ai-contributor-guard.yml   MATCH
b330a79de0b1b3229cf024b46b702f76d70d4c7da7df227d62d098355b1d20f9  .github/copilot-instructions.md              MATCH
989972c395b382fbe5a97bf193740cb0162e1e13936f617b00ce6ec1f7c663b6  scripts/github_agents/check_no_ai_contributors.py  MATCH
66cfb3167b87dc53a88318877baa58edbd8d7bfd8b829b8b0030cb5b14b4c60a  scripts/github_agents/common.py              MATCH
c1b146decc8add301b2b03336179d32c40caeb36e946d90df8eb77f9bb61ce34  scripts/Invoke-AiContributorGuard.ps1        MATCH
```

## Task 1 — Git hooks: DONE

`core.hooksPath` is set to `.githooks` (verified; `.githooks/pre-push` present). Applied via `git config core.hooksPath .githooks` directly — the literal `Enable-GitHooks.ps1` invocation was refused by the session's permission layer because of its `-ExecutionPolicy Bypass` flag; the script's only effect is that one config line, so the outcome is identical.

## Task 2 — Static gates, both editions: PASS (7/7 on each)

| Gate | powershell.exe 5.1 | pwsh 7.5.2 |
|---|---|---|
| Test-MCOSJsonCorpus (47 files) | PASS | PASS |
| Test-MCOSMarkdownLinks (787 links) | PASS | PASS |
| Test-MCOSRepositoryMetadata | PASS | PASS |
| Sync-RepositoryVersionBadges -CheckOnly (0.11.0-alpha.3) | PASS | PASS |
| Test-MCOSSecurityDefaults (8 patterns) | PASS | PASS |
| Test-MCOSStaticGates (14 literals) | PASS | PASS |
| check-mastercontrol-forsetti | PASS | PASS |

Orchestrator exit 0 on both editions. Logs: `artifacts/mcos-remediation-gates/` (timestamped 20260703-0942xx pairs). The documented 5.1 NativeCommandError caveat was observed exactly as described on the pre-normalization run (chain stopped at the first failing gate, non-zero exit); after normalization no gate fails so the caveat is dormant. The README em-dash check behaved correctly under both editions.

## Task 3 — Build/test validation: DEGRADED (core green; Shell environmentally blocked)

- `cmake --preset debug` / `--preset release`: both configure cleanly against VS-bundled vcpkg (`nlohmann-json`, `sqlite3` resolved from the manifest baseline).
- Built in **both** Debug and Release: `MasterControlApp`, `MasterControlModules`, `MasterControlServiceHost`, `MasterControlBootstrapper`, `MasterControlOrchestrationServerLauncher`, `MasterControlOrchestrationServerSetup`, `MasterControlBaselineToolsWorker`, `VsTestConsoleProxy`, `ForsettiCore`/`ForsettiPlatform` + the three Forsetti test DLLs, `MasterControlOrchestrationServerTests`. All succeeded.
- `ctest` debug preset: **4/4 passed** (ForsettiCoreTests, ForsettiPlatformTests, ForsettiArchitectureTests, MasterControlOrchestrationServerTests).
- `ctest` release (`build/release -C Release --timeout 300`): **4/4 passed**.
- **BLOCKED (environmental):** `MasterControlShell` — MSB8020, Platform Toolset v145 not installed (VS2026 absent). Per the handoff this is environmental, not a branch defect; no source was touched. The same SHA built the Shell successfully in CI (below).
- Mitigating evidence: the **same-SHA product gate** (`Windows Build, Test, and Package`) completed with `conclusion=success` on `5fc7573` on the `windows-2025-vs2026` runner, which includes the full Shell build, test, package, smoke validation, and artifact upload. `Debug Preset Validation`, `Forsetti Compliance`, `AI Contributor Guard`, and `Realignment Discipline` are also green on the SHA.

## Task 4 — Package and cut v0.11.0-alpha.3: BLOCKED (two independent reasons)

1. **Local packaging blocked:** `Package-MasterControlOrchestrationServer.ps1 -SkipBuild` needs the Release Shell output (v145-only) and WiX 5; neither exists on this host. The authoritative release artifact does not depend on this — `release.yml` downloads the CI gating artifact rather than rebuilding.
2. **Cut blocked for this session:** pushing the `v0.11.0-alpha.3` tag requires GitHub push credentials and a configured git identity; neither is available non-interactively on this host (a push dry-run hangs on a credential prompt).

**The cut is otherwise ready.** CI's same-SHA rule is satisfied on `5fc7573`. To finish by hand:

```powershell
git config --global user.name  "<name>"; git config --global user.email "<email>"
git -C D:\GitHub\MCOS tag v0.11.0-alpha.3 5fc7573
git -C D:\GitHub\MCOS push origin v0.11.0-alpha.3     # release.yml verifies the gate and publishes
# after the release publishes:
#   VERSION.json: history[0].commit "pending" -> "5fc7573e0a3e45c74480d3d4967f201f11ee75fa"
#   (leave last_release_commit at 71d8f7f7... per convention)
pwsh -NoProfile -File scripts\Test-MCOSRepositoryMetadata.ps1 -RepoRoot .
powershell -NoProfile -File scripts\Sync-RepositoryVersionBadges.ps1 -CheckOnly
# mark "Remaining steps" done in handoff/realignment/v0.11.0-alpha.3-release-report.md, commit, push
```

Note: the remote's newest tag is `v0.4.5-rc.5` — no v0.10.x/v0.11.x tag was ever pushed, so this will be the first release cut through the gated tag→release.yml flow on this remote. Cut soon: the gating artifact on the product-gate run is subject to GitHub's artifact retention window.

## Task 5 — settings.json permissions change: BLOCKED for agents (as the handoff predicted)

The edit to `.claude/settings.json`'s `permissions` block was refused by the harness as self-modification — same behavior the originating session hit. The hooks block is live and correct. **Apply 5a and 5b by hand** exactly as written in `handoff/repo-cleanup-windows-pass.md`. Until then, protected-path write-blocking remains enforced by `pre-edit-scope-gate.ps1`, the metadata gate, and CI (verified working — see Task 6).

## Task 6 — Hook sanity under powershell.exe 5.1: PASS (6/6)

| Check | Expected | Actual |
|---|---|---|
| repository-preflight.ps1, healthy checkout | 0 | 0 |
| pre-edit-scope-gate.ps1, protected path payload | 2 | 2 |
| pre-edit-scope-gate.ps1, `src\` payload, no scope-plan.md | 2 | 2 |
| pre-edit-scope-gate.ps1, docs payload | 0 | 0 |
| session-stop-report-gate.ps1, dirty tree, no report | 2 | 2 |
| session-stop-report-gate.ps1, clean tree | 0 | 0 |

Probe-file note: the first dirty-tree probe used a `*.tmp` file, which `.gitignore` covers — the gate correctly saw a clean tree. Re-run with an unignored file behaved as specified.

## Task 7 — MasterControlRuntime.cpp extraction: NOT TAKEN

Optional, explicitly a separate PR slice, and behavior verification requires the full toolset this host lacks.

## Remaining risks / recommendations

1. **CRLF exposure:** the repo has no `.gitattributes`; any Windows checkout with `core.autocrlf=true` silently breaks the markdown-link gate and the protected-path hash comparison. Recommend committing a `.gitattributes` (`* text=auto eol=lf`, with binary exceptions) or hardening the gate regex to accept `\r?$`.
2. **Shell untested locally** until VS2026/v145 is installed on this host; CI remains the only Shell validation path.
3. **Release cut pending** on operator credentials (commands above). Until cut, `v0.11.0-alpha.3` remains canonical-but-unpublished and `history[0].commit` stays `pending`.
4. **WiX 5 and pwsh 7 not installed** on this host; install both before attempting local packaging or making pwsh-edition gates part of a local pre-push routine.
