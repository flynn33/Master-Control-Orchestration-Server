# Phase Completion Report — PHASE-00

Phase: PHASE-00 — Repository baseline and ADR lock
Phase file: [handoff/realignment/PHASE-00-repo-baseline-and-adr-lock.md](PHASE-00-repo-baseline-and-adr-lock.md)
Manifest: [handoff/realignment/manifest.json](manifest.json)
Date: 2026-04-30
Working tree: `master-control-dashboard-main`
Pre-phase commit: `1c5d986` (overlay install)
Phase commit: `d8758ac` (PHASE-00 baseline docs)

## Scope completed

PHASE-00 is docs-only. The new architectural direction is now locked in repo documents before any code changes:

- ADR-002 written and accepted, declaring MCOS as a Windows-native LAN MCP Gateway host. ADR-002 builds on ADR-001's provider-removal stance, adds the gateway/discovery/onboarding/governance-bundle/worker-pool/lease-router/autoscale/telemetry/dashboard/Windows-hardening commitments, and explicitly supersedes ADR-001 §3 and §6 for the AI-client surface only.
- ADR-001 annotated with a supersession header pointing at ADR-002; original ADR text untouched.
- Provider-era removal map written, evidencing that the ADR-001 §1 removal already landed in `src/MasterControlApp/MasterControlRuntime.cpp`, `src/MasterControlModules/MasterControlModules.cpp`, `tests/`, and `resources/web/`. Documents residual references in `src/MasterControlShell/` (deferred-cleanup state acknowledged by ADR-001).
- Architecture drift inventory written, mapping every existing surface (beacon, MCP gateway, onboarding, governance, worker pools, telemetry, dashboard, trust model, CI) to its phase target and action (`keep`/`extend`/`subsume`/`replace`/`split`).
- Forbidden-contract grep list written, providing executable greps that PHASE-00…PHASE-11 reviewers can run to detect provider-era regressions, gateway-abstraction breaks, AI-client-surface auth regressions, telemetry honesty regressions, Forsetti vendoring edits, CI bypass risks, and out-of-scope phase edits.
- Wiki sidebar updated with links to ADR-002 and the new realignment artifact set.
- No source, tests, web, installer, or CI files were modified. `VERSION.json` is unchanged at `0.5.0`.

## Files changed

| File | Change summary |
|---|---|
| [docs/wiki/Architecture-Decisions/ADR-002-gateway-first-mcp-realignment.md](../../docs/wiki/Architecture-Decisions/ADR-002-gateway-first-mcp-realignment.md) | New ADR. 12 numbered consequences. Builds on ADR-001; supersedes ADR-001 §3, §6 for AI-client surface. References realignment master, gateway/discovery contract, governance-bundle contract, manifest, and the three new baseline documents. |
| [docs/wiki/Architecture-Decisions/ADR-001-lan-client-control-plane.md](../../docs/wiki/Architecture-Decisions/ADR-001-lan-client-control-plane.md) | Header status changed to "Accepted (in part superseded)". New "Superseded in part by" line links ADR-002 and lists which ADR-001 sections survive vs which are superseded. Body text untouched. |
| [docs/implementation/PROVIDER-ERA-REMOVAL-MAP.md](../../docs/implementation/PROVIDER-ERA-REMOVAL-MAP.md) | New baseline document. Catalogs confirmed-removed surfaces (modules, services, transports, routes, types, browser surfaces, governance/activity kinds), residual shell references (with line numbers), retained historical artifacts. Includes verification grep block. |
| [docs/implementation/ARCHITECTURE-DRIFT-INVENTORY.md](../../docs/implementation/ARCHITECTURE-DRIFT-INVENTORY.md) | New baseline document. Maps current surface → realignment target → resolving phase → action. Sections A–J cover discovery, gateway, onboarding, governance, worker pools, telemetry, dashboard, trust model, CI, native-gateway evaluation. |
| [docs/implementation/FORBIDDEN-CONTRACT-GREP-LIST.md](../../docs/implementation/FORBIDDEN-CONTRACT-GREP-LIST.md) | New baseline document. Seven groups of executable greps with expected zero/positive match expectations and allowed-match scopes. Maintenance protocol included. |
| [docs/wiki/_Sidebar.md](../../docs/wiki/_Sidebar.md) | Added ADR-002 link under Architecture & internals; added new "Realignment (in progress)" section linking the realignment master, the two contracts, and the three baseline documents. |

Total: 4 created, 2 modified. All under `docs/`.

## Public contracts changed

None at the runtime level — no API, schema, header, or build contract was modified in this phase.

ADR-002 and the gateway/discovery and governance-bundle contracts (already in `docs/implementation/` from the overlay) declare new public contracts that PHASE-02 onward will implement (`/.well-known/mcos.json`, `/api/discovery`, `/api/onboarding/{clientType}`, `/api/governance/bundles/{platform}`, `IMcpGateway` C++ interface, `EndpointTemplate/Instance/Pool/Lease`, telemetry schema, etc.). PHASE-00 only documents that those will arrive; it does not implement them.

## Tests added or updated

None. PHASE-00 acceptance criteria explicitly state "No code edits unless needed for docs index."

## Validation performed

| Command | Result | Notes |
|---|---|---|
| `git grep -nE 'ProviderIntegrationModule\|/api/providers\|executeClaudeCodeCli\|executeCodexCli\|executeOpenAICompatibleChat' -- src/MasterControlApp src/MasterControlModules tests` | 0 matches | Confirms ADR-001 §1 removal stuck in runtime/modules/tests. |
| `git grep -nE 'Provider[A-Z]\|AutoConnect[A-Z]' -- src/MasterControlApp src/MasterControlModules tests resources/web include` | 0 matches | Provider-era types and AutoConnect family absent from non-shell scope. |
| `git grep -cE 'Provider' src/MasterControlShell/MainWindow.xaml.cpp src/MasterControlShell/ShellRuntime.cpp` | 83, 22 | Documented residual in PROVIDER-ERA-REMOVAL-MAP.md as deferred-cleanup; not a phase blocker. |
| `git status --short -- 'src/**' 'tests/**' 'resources/web/**' 'installer/**' '.github/workflows/**' VERSION.json` | empty | Confirms PHASE-00 scope discipline: no edits to code, tests, browser UI, installer, CI, or version. |
| Overlay file count: source `find … -type f \| wc -l` vs target | 49 = 49 | Overlay materialized completely under `.claude/`, `handoff/realignment/`, `docs/implementation/`, plus root `CLAUDE.md`/`AGENTS.md`. |
| `head -1 CLAUDE.md` | `# CLAUDE.md — MCOS Realignment Instructions` | Realignment instructions are now the active CLAUDE.md (legacy local-repo-workflow CLAUDE.md replaced). |
| `cmake --preset debug` | **NOT run** | Skipped intentionally per PHASE-00 acceptance criteria ("No code edits unless needed for docs index"). PHASE-01 runs the full build/test pipeline. |
| `ctest --preset debug --output-on-failure` | **NOT run** | Same reason as above. |
| `scripts/check-mastercontrol-forsetti.ps1` | **NOT run** | Same reason; the script is preserved for PHASE-05 (and phase-end runs from PHASE-01 onward). |
| Three commits land cleanly: baseline → overlay install → PHASE-00 docs | yes | `2f802c8` baseline (400 files), `1c5d986` overlay install (49 files added/modified), `d8758ac` PHASE-00 docs (6 files, +587/-2). |

This is **static source inspection plus documentation work** per the rule in `.claude/rules/40-validation-reporting.md` ("Static source inspection must be labeled as static source inspection"). No runtime behavior was tested or claimed; nothing in this report relies on a live build.

## Acceptance criteria status (from manifest)

| Criterion | Status | Evidence |
|---|---|---|
| New ADR states MCOS is not a direct AI executor | met | ADR-002 §1 reaffirms ADR-001 §1's provider-removal stance and §2 reaffirms "MCOS never calls AI models". |
| MCP Gateway and LAN discovery are first-class modules | met | ADR-002 §2 (one MCP Gateway URL, replaceable adapter), §3 (no autoscaled-clone exposure), §4 (DNS-SD service types and TXT fields). Realignment master and gateway/discovery contract are linked from the sidebar. |
| Current provider/client/auth semantics documented before change | met | PROVIDER-ERA-REMOVAL-MAP.md captures provider-era state including residual shell references. ARCHITECTURE-DRIFT-INVENTORY.md sections C, D, E, H document current onboarding, governance, worker, and trust-model semantics and their realignment targets. ADR-002 explicitly identifies which ADR-001 sections survive (§1, §2, §4, §5, §7) and which are superseded (§3, §6 for AI-client surface only). |

## Risks and blockers

1. **Shell residual provider references will block compilation.** `src/MasterControlShell/MainWindow.xaml.cpp` and `ShellRuntime.cpp` reference `ProvidersSectionControl`, `ProviderAssignmentOptions`, and `ShellProvider*` types whose headers/definitions don't exist on disk. The shell's `MasterControlShell.vcxproj` does not list those files, but the `#include`s and type uses in the .cpp files mean the shell does not compile in its current state. ADR-001 acknowledged this as deferred cleanup; PHASE-01 should address it as part of "remove or quarantine direct AI-provider execution paths" before PHASE-02's gateway work, since otherwise build/test validation cannot pass with the shell included.
2. **Beacon platform-gateway field overlap.** The existing `BeaconAdvertisement.platformGateways` (platform-services) is conceptually adjacent to but distinct from the new MCP gateway (`gateway: {type, mcpUrl, healthUrl}`). PHASE-03 will need to disambiguate naming to prevent operator confusion.
3. **Browser admin port vs MCP gateway port** — not yet decided. The schema's default `listenPort=8080` for the gateway implies a separate port from the existing admin `:7300`. PHASE-02 must commit to this and update operations docs.
4. **Forsetti compliance script may need updates** beyond the schedule planned in PHASE-05. The script currently asserts the post-ADR-001 module shape; introducing the gateway/discovery work in PHASE-02–PHASE-03 may surface assertion mismatches earlier than PHASE-05. The risk is that PHASE-02/03's per-phase Forsetti run fails for a benign reason; mitigation is to read the script in PHASE-02's first step.
5. **Companion utility scope undecided.** PHASE-04 deliverables permit either a small companion EXE or pure documentation as the means by which clients that cannot consume DNS-SD apply onboarding. No commitment yet.

None of these block declaring PHASE-00 complete; they are forward-flagged for PHASE-01..PHASE-05.

## Deferred work

| Item | Deferred to | Reason |
|---|---|---|
| Removal of residual `ProvidersSectionControl` / `ShellProvider*` references in `src/MasterControlShell/` | PHASE-01 (per its objective "remove or quarantine direct AI-provider execution paths") | Outside PHASE-00 scope; affects code, not docs. |
| Splitting `BeaconAdvertisement.platformGateways` from the new `gateway` block | PHASE-03 | Beacon work belongs there. |
| Confirming admin port vs gateway port topology | PHASE-02 | Gateway adapter work introduces the new port. |
| Updating `scripts/check-mastercontrol-forsetti.ps1` for new modules/manifests | PHASE-02 first-touch + PHASE-05 | Per `.claude/rules/20-forsetti-clu-governance.md`, update only when architecture changes invalidate assumptions. |
| Companion utility decision | PHASE-04 | Profile-design phase. |
| Real validation runs (`cmake`, `ctest`, Forsetti script) | PHASE-01 onward | PHASE-00 acceptance forbids code edits, so build/test runs are deferred to the first phase that produces code. |

## Ready for next phase?

**Answer: yes** — PHASE-00 acceptance criteria are met, scope discipline held (zero code/tests/web/installer/CI/version edits), and the baseline documents (ADR-002, removal map, drift inventory, forbidden-grep list) give PHASE-01 a precise file-by-file removal target plus the regression detection it needs.

PHASE-01 should begin by:
1. Reading [handoff/realignment/PHASE-01-remove-provider-era-direct-ai-integration.md](PHASE-01-remove-provider-era-direct-ai-integration.md) and its `readFirst` files (especially `src/MasterControlShell/`).
2. Producing a file-by-file plan to remove `ProvidersSectionControl`, `ProviderAssignmentOptions`, `ShellProvider*` types, dead `/api/providers` calls in `ShellRuntime.cpp`, and the navigation/wizard entries in `MainWindow.xaml.cpp` — using `PROVIDER-ERA-REMOVAL-MAP.md` (residual section) as the worklist.
3. Establishing whether the WinUI shell can compile after that removal, or whether `src/MasterControlShell/` should be excluded from the build until a follow-on shell track lands (consistent with ADR-001 §Negative-consequences).
4. Running `cmake --preset debug` / `cmake --build` / `ctest` end-to-end as the first build/test validation since the realignment overlay was applied.
5. Stopping at the PHASE-01 completion report; not proceeding to PHASE-02.

PHASE-00 stops here. No further phases will start without explicit instruction from the operator.
