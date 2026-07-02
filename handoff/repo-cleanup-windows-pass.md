# Repo Cleanup — Windows Validation Pass Handoff

**Branch:** `repo-cleanup/metadata-docs-ci` (base `main` @ `9ae2602`)
**Prepared:** 2026-07-02, at the end of the macOS cleanup session
**Audience:** the next engineering session on the Windows build host

The cleanup branch promoted the repository to `0.11.0-alpha.3`, archived historical material under `docs/archive/`, added repository-health gates, and hardened the `.claude/` project configuration. Every static gate passes on the originating host; everything that needs Windows was deferred to this pass. This document is self-contained — the originating session's `artifacts/repo-cleanup/` evidence directory is untracked and will not exist on your checkout.

## Preconditions

- Windows 11 / Server 2022 host with the project toolset (VS2026/v145, CMake, `VCPKG_ROOT` set per `docs/wiki/Quick-Start.md`).
- Both `powershell.exe` (5.1) and `pwsh` (7.x) available.
- Checkout of `repo-cleanup/metadata-docs-ci`.

## Hard rules (unchanged from the original remediation)

1. The five protected attribution-guard paths are **read-only**:
   `.github/workflows/ai-contributor-guard.yml`, `.github/copilot-instructions.md`, `scripts/github_agents/check_no_ai_contributors.py`, `scripts/github_agents/common.py`, `scripts/Invoke-AiContributorGuard.ps1`.
   Their SHA-256 hashes at handoff time (verify before and after your work):

   ```text
   3101a130a96e126bf2619c88e214c5fe7497441c289cfe6443807332cc97c462  .github/workflows/ai-contributor-guard.yml
   b330a79de0b1b3229cf024b46b702f76d70d4c7da7df227d62d098355b1d20f9  .github/copilot-instructions.md
   989972c395b382fbe5a97bf193740cb0162e1e13936f617b00ce6ec1f7c663b6  scripts/github_agents/check_no_ai_contributors.py
   66cfb3167b87dc53a88318877baa58edbd8d7bfd8b829b8b0030cb5b14b4c60a  scripts/github_agents/common.py
   c1b146decc8add301b2b03336179d32c40caeb36e946d90df8eb77f9bb61ce34  scripts/Invoke-AiContributorGuard.ps1
   ```

2. No branch, commit, file, or package names attributing work to an assistant, model, bot, or tool.
3. `release.yml` and `windows-build-test-package.yml` must not gain `workflow_dispatch`.
4. Any C++ change must satisfy the strict-OOP rules in `CLAUDE.md` / `AGENTS.md` and pass `scripts/check-mastercontrol-forsetti.ps1`.

## Task 1 — Enable git hooks (Windows-only step the Mac could not do)

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\Enable-GitHooks.ps1
```

The `.githooks/pre-push` hook invokes `powershell.exe`, so it only works on Windows; the originating session deliberately left `core.hooksPath` unset on the Mac.

## Task 2 — Re-run the static gates on both PowerShell editions

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File scripts\Invoke-MCOSRemediationGates.ps1 -RepoRoot . -SkipBuild
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\Invoke-MCOSRemediationGates.ps1 -RepoRoot . -SkipBuild
```

Expected: all seven chained gates pass on both editions. Known 5.1 caveat (pre-existing pattern, acceptable): if a child gate fails, 5.1's NativeCommandError handling may stop the chain at the first failure instead of aggregating — the exit code is still non-zero. If you see an em-dash-related miss or false pass in the README alpha.1 check under 5.1 only, the gate file lost its encoding — it builds that dash from `[char]0x2014` precisely to avoid this.

## Task 3 — Full build/test validation

```powershell
cmake --preset debug
cmake --build --preset debug
ctest --preset debug --output-on-failure
cmake --preset release
cmake --build build/release --config Release
ctest --test-dir build/release -C Release --output-on-failure --timeout 300
```

The branch contains **no functional C++ changes** (comment wording only, from the residue sweep in commit `03011cb`) — a build or test failure here is either environmental or pre-existing on `main`; bisect against `main` before touching source.

## Task 4 — Package and cut v0.11.0-alpha.3

Only after Task 3 is green:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\Package-MasterControlOrchestrationServer.ps1 -Preset release -SkipBuild
```

Then follow `docs/wiki/Release-Gate.md` (the same-SHA product-gate rule applies; no manual dispatch). After the cut:

1. Backfill `VERSION.json` → `history[0].commit` (currently `"pending"`) with the cut commit hash. Leave `last_release_commit` at `71d8f7f7…` (it points at the *previous* release's cut by convention).
2. Re-run `pwsh -NoProfile -ExecutionPolicy Bypass -File scripts\Test-MCOSRepositoryMetadata.ps1 -RepoRoot .` and `scripts\Sync-RepositoryVersionBadges.ps1 -CheckOnly`.
3. Update `handoff/realignment/v0.11.0-alpha.3-release-report.md` — mark the "Remaining steps" list done.

## Task 5 — Apply the settings permissions change (blocked for agents, one minute by hand)

The originating session could not edit the `permissions` block of `.claude/settings.json` (the Claude Code harness treats agent edits to its own permission rules as self-modification). The hooks block WAS updated and is live. Apply by hand:

**5a. Append to `permissions.deny`:**

```json
"Edit(.github/workflows/ai-contributor-guard.yml)",
"Write(.github/workflows/ai-contributor-guard.yml)",
"Edit(.github/copilot-instructions.md)",
"Write(.github/copilot-instructions.md)",
"Edit(scripts/github_agents/check_no_ai_contributors.py)",
"Write(scripts/github_agents/check_no_ai_contributors.py)",
"Edit(scripts/github_agents/common.py)",
"Write(scripts/github_agents/common.py)",
"Edit(scripts/Invoke-AiContributorGuard.ps1)",
"Write(scripts/Invoke-AiContributorGuard.ps1)"
```

**5b. Replace the single `"PowerShell(*)"` entry in `permissions.allow` with:**

```json
"PowerShell(scripts/Test-MCOS*)",
"PowerShell(scripts/Invoke-MCOSRepositoryHealth*)",
"PowerShell(scripts/Invoke-MCOSRemediationGates*)",
"PowerShell(scripts/Sync-RepositoryVersionBadges*)",
"PowerShell(scripts/check-mastercontrol-forsetti*)",
"PowerShell(scripts/check-mcos-discovery*)",
"PowerShell(Get-*)",
"PowerShell(Select-String*)",
"PowerShell(Test-Path*)",
"PowerShell(Copy-Item*)",
"PowerShell(Move-Item*)"
```

Build/deploy scripts (`Build-*`, `Deploy-LocalLive.ps1`, `Package-*`) intentionally stay prompt-gated. Until 5a is applied, protected-path write-blocking is enforced by `.claude/scripts/pre-edit-scope-gate.ps1` (PreToolUse hook), the metadata gate, and CI.

## Task 6 — Sanity-check the enforcing hooks on Windows

The three hooks in `.claude/settings.json` were tested on the originating host with portable pwsh; give them one pass under real `powershell.exe`:

- `repository-preflight.ps1` → exit 0 on a healthy checkout.
- `pre-edit-scope-gate.ps1` → exit 2 for a payload targeting a protected path; exit 2 for a `src\` path with no `.claude/state/scope-plan.md`; exit 0 for a docs path.
- `session-stop-report-gate.ps1` → exit 2 on a dirty tree without `.claude/state/stop-report.md`; exit 0 on a clean tree.

## Task 7 (optional, separate PR slice) — MasterControlRuntime.cpp extraction

Deferred from the original remediation because it cannot be behavior-verified without this host. If taken on: extract `src/MasterControlApp/MasterControlRuntime.cpp` (~21k lines) by responsibility into `Http/`, `Composition/`, `Services/`, `Supervision/`, `Discovery/`, `Diagnostics/` under `src/MasterControlApp/`, preserving names and behavior; interfaces first; constructor injection; concrete classes `final`; no raw `new`/`delete`; update `src/MasterControlApp/CMakeLists.txt`; keep the `bool testXxx()` test pattern; debug+release build/test and the Forsetti gate must pass; the diff must read as a move, not a rewrite. Do **not** combine it with any other change.

## Required completion report

Branch, commits, gates run per edition with results, build/test/package results, release cut + tag + backfill commit, protected-path hash comparison (before/after against the table above), hook sanity results, settings change applied yes/no, remaining risks. Mark anything unexecuted as DEGRADED or BLOCKED with the exact reason — do not claim unverified gates as passed.
