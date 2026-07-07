# MCOS Software Alpha Readiness — Remediation Report

Internal-alpha, trusted-LAN. Final Windows build/package, MSI install, and all
service/firewall/URL-ACL/TLS mutations remain operator-gated and were NOT run.

## Summary

- Implementation status: implemented and locally validated (Gate A static +
  Gate B build/ctest); host-mutating and second-host gates pending operator run.
- Build/test: `cmake --build --preset debug` (0 errors) and the full ctest
  suite pass after every change.
- Two items were deliberately scoped down (see "Scoped / deferred").

## Product changes implemented

| Task | Change |
|---|---|
| T00 | `AlphaFeatureMatrix.h` + `resources/alpha-feature-matrix.json` — single source of truth for required/optional/deferred/removed feature IDs; the runtime deprecated-endpoint prune consumes it; a test asserts header↔JSON agreement. |
| T01 | Baseline-tools worker fails closed on an unknown non-empty `--specialization` (stderr + nonzero exit before serving JSON-RPC); `baseline-tools` is now an explicit recognized id; `forge.list_specializations` includes `local-database`. |
| T02 | Removed the `docker-control` boot pool (the worker no longer implements that specialization, so the pool served baseline tools under a mislabeled name — a fake-healthy surface). `playwright` was already fully removed. |
| T03 | Pool readiness is gated on a real MCP handshake: `EndpointInstance.healthProbePassed` flips true only after a successful `initialize` handshake; working-alpha readiness counts a required pool ready only when it has a handshake-confirmed instance (a spawned-but-mute worker cannot fake readiness). Lease-router semantics unchanged. |
| T04 | `local-database` implemented fully — read-only SQLite behind `IDatabaseConnectionStore` / `IDatabaseQueryExecutor`; `SqliteReadonlyQueryExecutor` opens `SQLITE_OPEN_READONLY`, enforces `sqlite3_stmt_readonly` + single-statement + ATTACH/DETACH rejection, a row limit and a statement timeout; tools `db.status` / `db.list_tables` / `db.describe_table` / `db.query_readonly`. |
| T05 | Client governance preflight (`POST /api/client/governance/decisions`) is now a real advisory `enforceAction` evaluation (was a constant `{"outcome":"deferred"}` no-op); the actor is forced to the authenticated client id. The false "the original mutation is replayed" comment on the approval queue was corrected to describe actual behavior. |
| T06 | Working-alpha readiness fails closed: every availability signal defaults to its blocking value, and the report carries per-signal provenance; the runtime caller derives `installStateAvailable` from a live filesystem check. |
| T07 | Local acceptance script gains `-Certification` (and `-OutDirectory` alias): in certification mode the runtime `workingAlpha` readiness and bootstrapper bindings become REQUIRED; adds probes for `/api/clients`, `/api/pools`, `/api/diagnostics/runtime-stats`. |
| T08 | Second-host LAN acceptance script gains `-Certification` and `-ClientBundlePath` / `-OutDirectory` aliases (its probes are required by construction). |
| T09 | Diagnostics bundle adds `/api/clients`, `/api/pools`, `/api/diagnostics/runtime-stats`, explicit `workingAlpha` readiness extraction, MCP `ping` + `tools/list`, and a structured `bootstrapper validate --json` collection. |
| T11 | Static stub/fake/dead-code scan: every hit in product code is a proper error return, an honest comment, an exists()-gated optional dependency, or an honest "unavailable" (SSE). No unresolved alpha blockers. |

## Files changed (by area)

- Runtime: `include/MasterControl/{AlphaFeatureMatrix,DatabaseQuery}.h` (new), `include/MasterControl/{MasterControlModels,WorkingAlphaReadiness}.h`, `src/MasterControlApp/MasterControlRuntime.cpp`, `resources/alpha-feature-matrix.json` (new).
- Worker: `src/MasterControlBaselineToolsWorker/main.cpp`, `.../CMakeLists.txt`.
- Validation scripts: `scripts/Test-MasterControlOrchestrationServerWorkingAlpha.ps1`, `scripts/Test-MasterControlLanClientAcceptance.ps1`, `scripts/Get-MasterControlOrchestrationServerDeploymentDiagnostics.ps1`.
- Tests: `tests/MasterControlOrchestrationServerTests.cpp`, `tests/CMakeLists.txt`.
- Report: this file.

## Scoped / deferred (honest status)

- **T05 execute-on-approval auto-replay** — the requires-approval → stage →
  operator-approve path and its audit trail already exist. Automatically
  RE-EXECUTING the staged mutation on approval was deliberately NOT built: it
  requires either a general HTTP-route replay engine or new persisted
  governance-policy state plus a reachable call site, and the approval-queue
  service is not unit-testable without a governance-layer refactor. The
  approval comment now honestly states the operator re-applies the change via
  the normal governed mutation route. No fail-closed condition is violated
  (blocked decisions still prevent mutation; approve/reject remain auditable).
- **T10 `IExecutableResolver`** — deferred. The required alpha executable (the
  in-tree worker) already resolves install-relative, and every hardcoded
  node/npm/sqlite literal is for an OPTIONAL external MCP and is `exists()`-gated
  (honest-degraded when absent). No required default path depends on a
  user/SystemProfile location, so no fail condition is triggered; introducing
  the resolver seam is a recommended follow-up refactor.

## Validation performed

| Command | Result |
|---|---|
| `cmake --build --preset debug --target MasterControlOrchestrationServerTests` | exit 0 |
| ctest suite (worker fail-closed spawn, local-database SQLite guard, readiness fail-closed + provenance, endpoint-instance round-trip, feature-matrix agreement, + full existing suite) | all pass |
| Bootstrapper `validate --json` bindings (Windows, read-only netsh) | required firewall → `failed`, optional URL ACL → `optionalMissing` |
| Acceptance/diagnostics scripts: `[Parser]::ParseFile`, `-Help`, `-Certification` dry-run vs. dead port | parse clean; cert-mode readiness/pools checks required; graceful FAIL + evidence |
| Static stub/fake/dead-code scan | classified; no alpha blockers |

## Validation pending operator authorization

- Final Windows release build/package: `cmake --preset release` / `cmake --build --preset release` / `cpack` (map `windows-release` → `release`).
- MSI install on a clean elevated Windows target: `msiexec /i <MSI> /L*v <log>`.
- Local certification: `pwsh -File scripts/Test-MasterControlOrchestrationServerWorkingAlpha.ps1 -Certification -BaseUrl http://127.0.0.1:<PORT> -OutDirectory <EVIDENCE> -BootstrapperPath <BOOT> -InstallDirectory <DIR>`.
- Client registration: `pwsh -File scripts/Register-MasterControlLanClient.ps1 -ClientId codex-alpha-01 -ClientType codex -OutputPath <BUNDLE.json>`.
- Second-host certification: `pwsh -File scripts/Test-MasterControlLanClientAcceptance.ps1 -Certification -DiscoveryUrl http://<LAN_IP>:<PORT>/.well-known/mcos.json -ClientBundlePath <BUNDLE.json> -OutDirectory <EVIDENCE>`.
- Diagnostics after any failure: `pwsh -File scripts/Get-MasterControlOrchestrationServerDeploymentDiagnostics.ps1 -OutputRoot <DIR> -BaseUrl http://127.0.0.1:<PORT> -BootstrapperPath <BOOT> -InstallDirectory <DIR> -Bundle`.

## Forsetti / OOP compliance confirmation

- Interface-first: `IDatabaseConnectionStore` / `IDatabaseQueryExecutor` (new) with `SqliteReadonlyQueryExecutor final`; existing `I*` interfaces reused. pass
- Constructor DI: `SqliteReadonlyQueryExecutor` takes its connection store by ctor; no hidden globals introduced. pass
- Concrete classes `final`. pass
- Core stays platform-agnostic; SQLite is cross-platform; Windows netsh/SCM stays in bootstrapper/script layers. pass
- Forsetti internals untouched. pass
- No new cyclic dependencies; new headers are self-contained. pass
- Tests cover new behavior (worker fail-closed, SQL read-only guard, readiness, feature matrix, endpoint-instance). pass

## Self-audit

- Scope: only product code / tests / validation scripts / operator docs changed; no repository-presentation, workflow, release, or wiki edits; no attribution language. pass
- Software: unknown specialization fails closed; removed IDs absent from default surfaces; readiness fails closed; certification scripts fail nonzero on required failures; diagnostics carry service/listener/gateway/binding/pool/client/readiness evidence; local-database is real. pass
- Operator gate: no final build/install/mutation run. pass
- Deferred items documented above with honest rationale. pass
