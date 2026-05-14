# Forbidden Contract Grep List (PHASE-00 Baseline)

This file is a machine-runnable contract for the realignment. Every grep below MUST return zero matches in the listed scope or the realignment has regressed. PHASE-00 establishes the baseline; subsequent phases extend it (notably PHASE-01 expands the scope to the WinUI shell once `ProvidersSectionControl` is removed, and PHASE-04/PHASE-05 add new patterns once the new endpoints are live).

Snapshot date: 2026-04-30
Working tree: `master-control-dashboard-main`, post-overlay-install commit `1c5d986`.

## How to use this list

- Run each grep block from the repo root.
- Allowed-match files are listed where applicable (history/docs/plans/shell-deferred-cleanup).
- A regression introduced into a forbidden scope is a phase-blocker per `handoff/realignment/CHECKLIST.md`.
- These greps complement (not replace) the C++ build, the test suite, and the Forsetti compliance script.

---

## Group 1 — Provider-era execution (forbidden by ADR-001 §1, ADR-002 §1)

### 1.1 Provider Forsetti modules

```bash
git grep -nE 'ProviderIntegrationModule|CodexProviderModule|ClaudeCodeProviderModule|XAIProviderModule' \
  -- src/MasterControlApp src/MasterControlModules src/MasterControlServiceHost src/MasterControlBootstrapper tests resources/web
```

Expected: zero matches.

Allowed elsewhere: `CHANGELOG.md`, `VERSION.json`, `docs/wiki/Versions.md`, `docs/wiki/Architecture-Decisions/ADR-001-*.md`, `docs/wiki/Architecture-Decisions/ADR-002-*.md`, `docs/implementation/PROVIDER-ERA-REMOVAL-MAP.md`, `plans/`.

### 1.2 Provider service interfaces and implementations

```bash
git grep -nE 'IProviderRegistry|IProviderCatalogService|IProviderCredentialStore|IProviderAssignmentService|IProviderExecutionCatalogService|IProviderExecutionService' \
  -- src/MasterControlApp src/MasterControlModules src/MasterControlServiceHost src/MasterControlBootstrapper tests resources/web include
git grep -nE 'ProviderCatalogService|ProviderRegistryService|ProviderCredentialStore|ProviderCliSignInService|ProviderAssignmentService|ProviderExecutionCatalogService|ProviderExecutionService' \
  -- src/MasterControlApp src/MasterControlModules src/MasterControlServiceHost src/MasterControlBootstrapper tests resources/web include
```

Expected: zero matches.

### 1.3 Outbound AI transports

```bash
git grep -nE 'executeClaudeCodeCli|executeCodexCli|executeOpenAICompatibleChat' \
  -- src/MasterControlApp src/MasterControlModules src/MasterControlServiceHost src/MasterControlBootstrapper tests resources/web include
```

Expected: zero matches. ADR-001 §2: "MCOS never calls out to AI models. The outbound CLI transports are deleted, not repositioned."

### 1.4 Provider HTTP routes

```bash
git grep -nE '/api/providers([/?]|$)' \
  -- src/ tests resources/web
git grep -nE '/api/provider-assignment-targets' \
  -- src/ tests resources/web
```

Expected: zero matches across the entire `src/` tree (PHASE-01 widened the scope to include `src/MasterControlShell/`). Allowed elsewhere only in `CHANGELOG.md`, `VERSION.json`, `docs/wiki/Versions.md`, ADR-001/ADR-002, `docs/implementation/PROVIDER-ERA-REMOVAL-MAP.md`, and `plans/`.

### 1.5 Provider data types and AutoConnect family

```bash
git grep -nE 'ProviderConnection|ProviderAssignment|ProviderExecutionRegistration|ProviderExecutionRecord|ProviderCapability|ProviderCredentialStatus|ProviderAssignmentTarget' \
  -- src/ tests resources/web include
git grep -nE 'AutoConnect[A-Z]' \
  -- src/ tests resources/web include
git grep -nE 'ShellProvider' -- src/ include
```

Expected: zero matches across the entire `src/` tree. PHASE-01 deleted the shell residue (`ShellProvider*` types, `postProviderToAdminApi`, the three `ShowProvider*WizardAsync` methods, `kProvidersDestination`/`kProvidersView`, and the `Providers` browser+shell destination).

### 1.6 Provider-era governance and activity event kinds

```bash
git grep -nE '\bProviderExecution\b|\bProviderAutonomyEnable\b' \
  -- src/MasterControlApp src/MasterControlModules src/MasterControlServiceHost src/MasterControlBootstrapper tests resources/web include
git grep -nE '\bAutoConnect\b' \
  -- src/MasterControlApp src/MasterControlModules src/MasterControlServiceHost src/MasterControlBootstrapper tests resources/web include
```

Expected: zero matches.

### 1.7a Discovery document integrity (PHASE-03, ADR-002 §4)

The discovery document at `/.well-known/mcos.json` and `/api/discovery` MUST always advertise `auth=none` and `trust=lan`. Regression here would either reintroduce app-layer auth on the AI-client surface (ADR-001 §3 supersession violation) or weaken the LAN trust assertion.

```bash
git grep -nE 'document\.auth\s*=\s*"(?!none\b)' -- src/
git grep -nE 'document\.trust\s*=\s*"(?!lan\b)' -- src/
git grep -nE '"auth"\s*:\s*"(bearer|basic|cookie)"' -- src/
```

Expected: zero matches. The discovery document's `auth`/`trust` fields are constants — only `auth=none` and `trust=lan` are valid assignments in the gateway-first model.

### 1.7b Onboarding profile auth invariants (PHASE-04, ADR-002 §1, §5)

The onboarding profile schema declares `authRequired` as a `const false`. Any code path that emits an onboarding profile with `authRequired=true` is a regression of ADR-002 §1's "no app-layer auth on the AI-client surface" rule.

```bash
git grep -nE 'authRequired\s*=\s*true|profile\.authRequired\s*=\s*true' -- src/
git grep -nE '"authRequired"\s*:\s*true' -- src/
```

### 1.7c Forsetti vendoring integrity (PHASE-05, ADR-002 §11)

The vendored Forsetti tree at `Forsetti-Framework-Windows-main/` is read-only per `.claude/rules/20-forsetti-clu-governance.md`. PHASE-05 reads `forsetti-instructions.json` for `forsettiFrameworkVersion` but does not modify it.

```bash
git diff --name-only 2f802c8 HEAD -- 'Forsetti-Framework-Windows-main/**'
```

Expected: empty output. The directory must remain byte-identical to the baseline commit (`2f802c8`).

Expected: zero matches. The `OnboardingProfile.authRequired` default is `false` and no production code path overrides it.

### 1.7 Browser and shell provider-era surfaces

```bash
git grep -nE 'renderSignInCards|providers-surface|kProvidersDestination|kProvidersView|ProvidersSectionView|ProvidersSectionControl|ProviderAssignmentOptions|GuidedProviderAssignmentWizard|GuidedProviderExecutionWizard|ShowProviderWizardAsync|ShowProviderAssignmentWizardAsync|ShowProviderExecutionWizardAsync|ProviderCountText|ProviderCountDetailText' \
  -- src/ resources/web
git grep -nE '\bconnect-model\b|\bassign-responsibility\b|\bguided-provider-execution\b' \
  -- src/ resources/web
```

Expected: zero matches across `src/` and `resources/web`. PHASE-01 removed all of these.

---

## Group 2 — Gateway abstraction integrity (forbidden by ADR-002 §2, §3)

### 2.1 Direct the in-process HTTP.sys adapter coupling outside the adapter

```bash
git grep -nE 'the in-process HTTP.sys adapter|the in-process HTTP.sys adapter|the in-process HTTP.sys adapter' \
  -- src/MasterControlApp src/MasterControlServiceHost src/MasterControlBootstrapper tests resources/web include \
  ':!src/MasterControlApp/McpGatewayAdapters.cpp' \
  ':!include/MasterControl/McpGatewayAdapters.h' \
  ':!src/MasterControlApp/MasterControlDefaults.cpp' \
  ':!src/MasterControlApp/MasterControlModels.cpp' \
  ':!include/MasterControl/MasterControlModels.h' \
  ':!docs/**'
```

Expected: zero matches as of PHASE-02. The substring is allowed inside the adapter (`include/MasterControl/McpGatewayAdapters.h`, `src/MasterControlApp/McpGatewayAdapters.cpp`), the enum string tables (`src/MasterControlApp/MasterControlModels.cpp`), default-config seeding (`MasterControlDefaults.cpp`), and the legacy `GatewayType` slot declaration. Anywhere else is a coupling regression.

### 2.1a Worker process tree containment (PHASE-06, ADR-002 §7)

Every supervised child must be wrapped in a Windows Job Object so the supervisor's destructor can reap the entire tree atomically. `CreateProcessW` calls without an `AssignProcessToJobObject` follow-up risk orphaned process trees.

```bash
git grep -nE 'CreateProcessW' -- src/MasterControlApp src/MasterControlServiceHost src/MasterControlBootstrapper \
  ':!src/MasterControlApp/MasterControlRuntime.cpp' \
  ':!src/MasterControlApp/McpGatewayAdapters.cpp'
```

Expected: zero matches outside the two known supervised-process-tree call sites (`WorkerSupervisor::startInstanceLocked` in `MasterControlRuntime.cpp` and `NativeHttpSysGatewayAdapter::Start` in `McpGatewayAdapters.cpp`). Both already pair `CreateProcessW` with `AssignProcessToJobObject` and `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`.

### 2.2 Autoscaled clones registered as separate public MCP tools

```bash
git grep -nE 'RegisterHttpServer.*\binstance\b|RegisterStdioServer.*\binstance\b|registerServer.*clone' \
  -- src/MasterControlApp src/MasterControlModules tests
```

Expected: zero matches. ADR-002 §3 forbids exposing autoscaled clones as separate public MCP servers. Logical pool endpoints are registered with the gateway exactly once per pool.

### 2.3 Hot-migration of stateful MCP sessions

```bash
git grep -nE 'migrateSession|hotMigrate|migrateLease' \
  -- src/MasterControlApp src/MasterControlModules tests
```

Expected: zero matches unless an explicit backend-specific migration contract is added in a future phase. ADR-002 §8 forbids hot-migrating active stateful streams.

### 2.4 Lease router sticky-session integrity (PHASE-07, ADR-002 §8)

The PHASE-07 LeaseRouter MUST honor sticky-session routing: an active lease bound to a sessionId always returns that lease verbatim on subsequent `acquireLease` calls with the same sessionId. Re-binding an active stateful session to a different instance is hot-migration and forbidden.

```bash
git grep -nE 'stickySessions_\[' src/MasterControlApp/MasterControlRuntime.cpp
```

Expected: matches only inside `LeaseRouter::bindLeaseLocked` (one assignment site for new-lease binding) and inside the cleanup paths in `acquireLease` (stale-entry removal) and `releaseLease`. Any other write site is a hot-migration attempt and a regression.

---

## Group 3 — Trust model integrity (forbidden by ADR-002 §1)

### 3.1 App-layer auth on the AI-client gateway surface

```bash
git grep -nE 'X-MCOS-Client-Id|X-MCOS-Client-ID' \
  -- src/MasterControlApp src/MasterControlModules tests \
  ':!*operator*' ':!*admin*'
```

Expected: matches are allowed in the operator surface code paths but forbidden in any AI-client gateway path. Once the gateway port is live in PHASE-02, the gateway's request handlers MUST NOT read the `X-MCOS-Client-Id` header.

### 3.2 Bearer / session-token requirements on the AI-client surface

```bash
git grep -nE 'Authorization.*Bearer|requireBearer|requireSession|sessionToken' \
  -- src/MasterControlApp src/MasterControlModules tests \
  ':!*operator*' ':!*admin*'
```

Expected: zero matches in any code path serving the gateway URL. ADR-002 §1: `auth=none` on the AI-client surface.

### 3.3 Operator-surface auth must NOT regress into "anyone can mutate"

```bash
git grep -nE 'enforceAction|requirePrivilege|canCreateMcpServers|canCreateSubAgents|canManageClients' \
  -- src/MasterControlApp src/MasterControlModules
```

Expected: matches MUST exist. The privilege-flag/CLU-enforcement surface from ADR-001 §4 stays on the operator surface. (This is a positive grep: zero matches would indicate regression of ADR-001's privilege model.)

---

## Group 4 — Telemetry honesty (forbidden by ADR-002 §9)

### 4.1 Fake telemetry / placeholder values

```bash
git grep -nE 'fakeCpu|placeholderUtilization|simulatedGpu|stubbedClient|TODO[: ]+telemetry|FIXME[: ]+telemetry' \
  -- src/MasterControlApp src/MasterControlModules resources/web
git grep -nE 'utilization\s*=\s*(0\.5|50\.0|"50%")' \
  -- src/MasterControlApp src/MasterControlModules resources/web
```

Expected: zero matches.

### 4.2 Per-client CPU/GPU fabricated without heartbeat

```bash
git grep -nE 'clientCpu|clientGpu|clientDisk' \
  -- src/MasterControlApp src/MasterControlModules resources/web \
  ':!*Heartbeat*' ':!*heartbeat*'
```

Expected: zero matches. PHASE-08: per-client telemetry only via heartbeat/sidecar.

### 4.3 ClientHeartbeat / WorkerTelemetry unavailable-sentinel integrity (PHASE-08)

`ClientHeartbeat` and `WorkerTelemetry` carry **client-supplied** or **worker-side** metrics. ADR-002 §9 forbids confusing "unreported" with "idle": the sentinel for "unavailable" is `-1.0`, not `0.0`.

`gpuPercent` and `gpuMemoryMb` are unique to `ClientHeartbeat` (no other struct uses them), so a `0.0` default on either is unambiguously a regression.

```bash
git grep -nE 'gpuPercent\s*=\s*0\.0|gpuMemoryMb\s*=\s*0\.0' \
  -- include/MasterControl src/MasterControlApp src/MasterControlModules
git grep -nE 'gpuPercent\s*\{\s*0\.0\s*\}|gpuMemoryMb\s*\{\s*0\.0\s*\}' \
  -- include/MasterControl src/MasterControlApp src/MasterControlModules
```

Expected: zero matches.

Note: `cpuPercent` and `memoryPercent` are intentionally **not** part of this grep because `HostTelemetrySnapshot` legitimately uses them with `0.0` defaults — host-side PDH-derived counters measure directly, so `0.0` is genuinely "idle" rather than "unreported". When auditing those fields, verify the enclosing struct manually: `ClientHeartbeat` and `WorkerTelemetry` must keep `-1.0` defaults; `HostTelemetrySnapshot` may keep `0.0`.

### 4.4 Telemetry event ring-buffer cap (PHASE-08)

```bash
git grep -nE 'kMaxEvents_\s*=\s*[0-9]+' \
  -- src/MasterControlApp/MasterControlRuntime.cpp
```

Expected: exactly one match — the `static constexpr std::size_t kMaxEvents_ = 1024;` declaration inside `TelemetryAggregator`. No additional event-buffer caps elsewhere; the ring is bounded so a noisy worker cannot OOM the runtime.

### 4.5 Heartbeat ingest is the only client-metric write site (PHASE-08)

```bash
git grep -nE 'recordHeartbeat\(' \
  -- src/MasterControlApp src/MasterControlModules
```

Expected: matches inside `MasterControlRuntime.cpp` only — at the `POST /api/telemetry/heartbeat` route handler and inside the `TelemetryAggregator` member-function definition. Any other caller is a regression: client CPU/GPU/disk numbers must originate from a client-supplied heartbeat or sidecar, never from synthesizer code paths.

---

## Group 5 — Forsetti vendoring integrity (forbidden by ADR-002 §11)

### 5.1 Edits inside vendored Forsetti

```bash
git diff --name-only "$(git log --reverse --format=%H Forsetti-Framework-Windows-main | head -1)" HEAD \
  -- Forsetti-Framework-Windows-main
```

Expected: zero output. Any modification of `Forsetti-Framework-Windows-main/` between the initial baseline commit and HEAD is a phase-blocker.

---

## Group 6 — CI / release gate integrity (forbidden by ADR-002 §10 acceptance criteria)

### 6.1 Hardcoded VS Enterprise path

```bash
git grep -nE 'Microsoft Visual Studio[/\\][0-9]+[/\\]Enterprise' \
  -- .github/workflows scripts CMakeLists.txt CMakePresets.json
```

Expected: zero matches in workflows/scripts/CMake. PHASE-10 acceptance: toolchain discovery via `vswhere`/preset/`setup-msbuild`.

### 6.2 No `workflow_dispatch` bypass on the product gate or release workflow (PHASE-10)

```bash
git grep -nE '^[[:space:]]+workflow_dispatch:' \
  -- .github/workflows/windows-build-test-package.yml \
     .github/workflows/release.yml
```

Expected: zero matches. ADR-002 §10: "Manual dispatch cannot bypass successful same-SHA build." Adding `workflow_dispatch:` as a YAML trigger to either workflow defeats the same-SHA gate. The grep matches the YAML trigger key specifically (indented, with colon) rather than the bare word, so file-level comments documenting the rule do not false-positive. The advisory workflows (`forsetti-compliance.yml`, `ai-contributor-guard.yml`) MAY have `workflow_dispatch` because they are not release-blocking; this grep is scoped to the two release-blocking workflows specifically.

### 6.3 No hardcoded VS edition path in workflow files (PHASE-10)

```bash
git grep -nE '\\Enterprise\\|/Enterprise/' \
  -- .github/workflows
```

Expected: zero matches. Defense-in-depth above §6.1. The grep matches `\Enterprise\` or `/Enterprise/` as path segments, not the literal word `Enterprise` in comments or warning strings. The product-gate workflow's "Discover and document toolchain" step contains a deliberate `if ($vsInstall -match 'Enterprise')` warning — this is a runtime check, not a hardcoded path, and is allowed.

### 6.4 Process execution 7-step compliance (PHASE-10)

ADR-002 §10 requires synchronous external-process invocations to follow:

> Start → drain stdout/stderr concurrently → WaitForSingleObject with timeout → Kill process tree on timeout if needed → Join drain threads → GetExitCodeProcess → Cleanup handles.

The canonical reference implementation lives at `src/MasterControlApp/MasterControlRuntime.cpp` lines 914-1110 (`runHostedExecutable`). New synchronous-process paths must follow the same shape. This grep flags any naive `WaitForSingleObject(..., INFINITE)` without an accompanying timeout-and-kill path:

```bash
git grep -nE 'WaitForSingleObject\([^,]+,\s*INFINITE' \
  -- src/MasterControlApp src/MasterControlModules src/MasterControlServiceHost src/MasterControlBootstrapper
```

Expected matches:
- `src/MasterControlBootstrapper/main.cpp:637` — `runProcess` (no captured I/O; PHASE-10 known-issue, see comment at the call site).
- `src/MasterControlBootstrapper/main.cpp:690` — `runProcessCapture` (classic single-pipe deadlock risk; PHASE-10 known-issue, see comment at the call site).

These two pre-existing sites are flagged with `// PHASE-10 known-issue:` source comments and are listed in `handoff/realignment/PHASE-10-completion-report.md` deferred work. Any **new** match outside these two lines is a regression.

`WorkerSupervisor` and `NativeHttpSysGatewayAdapter` legitimately wait without timeout for their long-running supervised processes — Job Object containment (`JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`) owns the kill path, and they don't appear in this grep because they don't call `WaitForSingleObject` on supervised children.

### 6.5 Version stamping before configure (PHASE-10)

The product-gate workflow MUST read `VERSION.json` before any configure/build/package step. The "Stamp release version from VERSION.json" step must precede "Configure (Release)":

```bash
awk '/Stamp release version from VERSION.json/{stamp=NR} /Configure \(Release\)/{config=NR} END{ if (stamp == 0 || config == 0 || stamp >= config) print "VIOLATION"; else print "OK"; }' \
  .github/workflows/windows-build-test-package.yml
```

Expected: `OK`. PHASE-10 acceptance: "Release version stamped before configure/build/package."

### 6.2 `workflow_dispatch` release bypass

```bash
git grep -nE 'workflow_dispatch' .github/workflows
```

Expected: matches MAY exist but PHASE-10 acceptance requires that no release workflow can succeed via `workflow_dispatch` if the same-SHA Windows build/test/package gate did not also succeed. This grep is a manual-review trigger, not an automatic block.

### 6.3 Version bump outside PHASE-10

```bash
git diff --name-only $BASE_SHA HEAD -- VERSION.json
```

Expected: VERSION.json MUST NOT change between phase commits except in PHASE-10. Use `BASE_SHA` of the prior phase's completion commit.

---

## Group 7 — Phase scope integrity (forbidden by `handoff/realignment/manifest.json`)

### 7.1 PHASE-00 must not edit code

```bash
git diff --name-only $PHASE_00_BASE HEAD -- 'src/**' 'tests/**' 'resources/web/**' 'installer/**' '.github/workflows/**' VERSION.json
```

Expected: zero output during PHASE-00 commits. `$PHASE_00_BASE` is the overlay-install commit (`1c5d986`).

PHASE-00 acceptance criterion: "No code edits unless needed for docs index."

### 7.2 PHASE-XX must not edit files outside its declared scope

For each PHASE, the completion report lists files changed; reviewers compare against the phase file's `readFirst`/deliverables. No automated grep can replace that, but the phase's completion report is itself a binding artifact (see `40-validation-reporting.md`).

---

## Group 8 — Dashboard honesty (forbidden by ADR-002 §9, PHASE-09)

### 8.1 Dashboard must render `-1.0` self-reported metrics as "unavailable" (PHASE-09)

Self-reported metrics from `ClientHeartbeat` (PHASE-08) and `WorkerTelemetry` (PHASE-06) use `-1.0` as the unavailable sentinel. The dashboard MUST route every such render through the `formatMetric()` helper in `resources/web/app.js`. Direct `.toFixed(...) + '%'` patterns at heartbeat / worker-telemetry sites are forbidden — they would silently turn `-1` into `-1%` or worse, into `0%` after a coercion bug.

```bash
git grep -nE 'hb\.(cpuPercent|memoryPercent|gpuPercent|gpuMemoryMb)\.toFixed' \
  -- resources/web
git grep -nE '(tel|telemetry)\.(cpuPercent|memoryMbytes|memoryPercent|gpuPercent|gpuMemoryMb)\.toFixed' \
  -- resources/web
git grep -nE 'lastHeartbeat\.[a-zA-Z]+\.toFixed' \
  -- resources/web
```

Expected: zero matches.

Allowed (NOT a violation): `t.cpuPercent.toFixed(0) + '%'`, `t.memoryPercent.toFixed(0) + '%'`, `t.diskPercent.toFixed(0) + '%'` where `t` is the legacy `HostTelemetrySnapshot`. Host metrics are PDH-derived (directly measured), so `0%` is genuinely "idle". The `-1.0` sentinel applies only to client-supplied / worker-reported telemetry.

### 8.2 No legacy hardcoded surface IDs in `index.html` (PHASE-09)

```bash
git grep -nE 'id="(telemetryGrid|endpointTable)"' \
  -- resources/web/index.html
```

Expected: zero matches. Forsetti compliance also enforces this; the grep is the second line of defense.

### 8.3 No provider-era residue in `app.js` (PHASE-09 reaffirmation of PHASE-01)

```bash
git grep -nE 'renderSignInCards|/api/providers|dashboard-clu|clu-nav|clu-surface' \
  -- resources/web/app.js
```

Expected: zero matches. ADR-001 §1 + ADR-002 §1 forbid provider-era browser surfaces; PHASE-05 forbade hardcoded CLU bootstrap. The Forsetti compliance script asserts this; this grep mirrors the assertion.

### 8.4 Dashboard must not call removed routes (PHASE-09)

```bash
git grep -nE "fetch\\(['\"]/api/providers" \
  -- resources/web
git grep -nE "loadJson\\(['\"]/api/providers" \
  -- resources/web
```

Expected: zero matches. Provider-era routes are gone; any reintroduction is a regression.

---

## Maintenance

This list is updated at the end of each phase to reflect:

- new forbidden patterns introduced by that phase (e.g., PHASE-01 broadens 1.4 to include `src/MasterControlShell/`),
- new positive patterns required by that phase (e.g., PHASE-02 adds a positive grep for `IMcpGateway` symbols),
- removal of patterns that no longer apply.

The maintainer of this file is the same actor producing the phase completion report. Drift is the QA reviewer's first-line check (`qa-release-gate` agent).
