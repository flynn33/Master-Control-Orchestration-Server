# MCOS Remediation Completion Report

Instructions package: `mcos_project_remediation_handoff_2026-07-03`.

## Summary

- Remediation date: 2026-07-03
- Executor: engineering session on the Windows build host
- Source branch/worktree: `remediation/runtime-correctness` (base `main` @ `b06c9e2` via `versioning/alpha-scheme-migration`), worktree `D:\GitHub\MCOS`
- Build host: Windows 11 Pro
- Windows version: 10.0.26100
- Visual Studio/MSBuild version: VS2022 Community 17.14.34 (MSBuild 17.14.40, toolset v143). Note: the WinUI Shell (`MasterControlShell`) pins toolset v145 (VS2026) and cannot build on this host; no remediation task touches Shell code, and everything remediated here lives in targets that build and test green under v143.
- WiX version: not installed on this host (see MCOS-016 verification note)
- vcpkg root: `C:\Program Files\Microsoft Visual Studio\2022\Community\VC\vcpkg`

## Scope confirmation

```text
No new CI checks added: yes (none)
No new workflow files added: yes (none; .github/workflows untouched)
No new gateway implementation added: yes (NativeHttpSysGatewayAdapter refactored in place; no new IMcpGateway implementer, no new port/surface)
No attribution statements added: yes (none in code, comments, docs, filenames, branch name, or this report)
Attribution guard assets untouched: yes (all five paths byte-identical to HEAD; verified by compliance audit)
```

## Changed files

| File | Purpose | Related task IDs |
|---|---|---|
| `.claude-plugin/mcos-control/mcp-servers/mcos-bridge/server.py` | `_patch` helper; `mcos_config_update` -> PATCH /api/config; forsetti enable/disable -> state route; import tool removed | MCOS-001, MCOS-002 |
| `include/MasterControl/JsonMerge.h` (new) | Deep merge + `mergeConfigurationPatch` + partial-document detection (single source of truth for route + tests) | MCOS-001 |
| `include/MasterControl/MasterControlContracts.h` | `IAdminApiService::applyConfigurationPatchJson`; `createWorkerSupervisor` / `createLeaseRouter` factories | MCOS-001, MCOS-006, MCOS-010 |
| `include/MasterControl/MasterControlRuntime.h` | `applyConfigurationPatchJson` public shim | MCOS-001 |
| `include/MasterControl/McpGatewayAdapters.h` | Job/queue/worker members; session param; `currentToolCatalog`/`collectToolCatalog`; atomic bridge id; `closeHttpSysHandlesLocked`; `BuildGatewayInternalErrorBody` | MCOS-004, 005, 006, 009, 013, 014 |
| `src/MasterControlApp/McpGatewayAdapters.cpp` | Receive/execute split + bounded worker pool + inline /health + 503 backpressure; multi-phase Stop (no join under mutex); catalog child RPC outside mutex; shared registration validation; JSON-safe error envelopes; Mcp-Session-Id capture; sticky/stateless lease lifecycle with scope-guard release; 405 with Allow: POST | MCOS-004, 005, 006, 008, 009, 013, 014 |
| `src/MasterControlApp/MasterControlRuntime.cpp` | POST /api/config partial-body rejection; PATCH /api/config route + telemetry + droppedKeys; `resolveWorkerExecutable` (SearchPathW) wired into `startInstanceLocked`; join-only supervisor teardown + `shuttingDown_` spawn gate; LeaseRouter idle-timeout sweep + Released GC; admin listener worker pool + queue 503 + body cap; case-insensitive Content-Length via shared helper + 400/413; route registry moved to header; onboarding transport caveats | MCOS-001, 002, 006, 007, 008, 010, 011, 012, 015 |
| `include/MasterControl/AdminRouteRegistry.h` (new) | Typed method-allow registry incl. previously-missing diagnostics + chatgpt/grok plugin routes | MCOS-015 |
| `include/MasterControl/HttpHeaderParse.h` (new) | Case-insensitive header lookup + validated Content-Length parse | MCOS-012 |
| `installer/MasterControlOrchestrationServer.wxs` | No-op plugin checkbox + `INSTALL_CLAUDE_PLUGIN` property removed; LaunchCheck re-flowed; explanatory comment | MCOS-003 |
| `scripts/Package-MasterControlOrchestrationServer.ps1` | MSI failure fatal by default; `-AllowZipOnly`/`-ZipOnlyReason` opt-in; `zipOnlyExplicit`/`zipOnlyReason` metadata | MCOS-016 |
| `tests/CMakeLists.txt` | `MCOS_REPO_ROOT` compile definition for cross-file contract tests | tests |
| `tests/MasterControlOrchestrationServerTests.cpp` | 25 new bool-style tests (135 total, all registered in `main()`) | all |
| `docs/wiki/Worker-Pools.md` | Stale mermaid diagram corrected to `MasterControlModels.h`; healthProbe rows prefixed `template.`; PATH-resolution rules documented | MCOS-007, MCOS-017 |
| `docs/wiki/Gateway.md` | "Transport profile (alpha): POST-only Streamable HTTP" section; session header contract; backpressure | MCOS-006, MCOS-008 |
| `docs/wiki/Onboarding.md` | Transport compatibility note on the profile table | MCOS-008 |

## Task status

| Task | Status | Evidence | Tests |
|---|---|---|---|
| MCOS-001 | Fixed | `JsonMerge.h::mergeConfigurationPatch`; `AdminApiService::applyConfigurationJson` partial rejection + `applyConfigurationPatchJson`; PATCH route in `handleHttpRequest`; bridge `_patch("/api/config")`; live smoke: partial POST -> 400 with guidance, PATCH preserved security/mcpGateway/activeProfile sections byte-for-byte | testConfigPatchPreservesUnrelatedSections, testConfigPatchRejectsNonObjectPayload, testConfigPostRejectsPartialTopLevelBody, testBridgeConfigUpdateUsesPatchRoute + live smoke |
| MCOS-002 | Fixed | Bridge enable/disable POST `/api/forsetti/modules/state` with `{moduleId, action}`; import tool + handler removed from `TOOLS_REGISTRY` | testForsettiModuleBridgeUsesStateRoute, testBrokenForsettiImportToolIsNotAdvertised, testBridgeRoutesMatchBackendContract |
| MCOS-003 | Fixed (preferred path: removal) | `INSTALL_CLAUDE_PLUGIN` property + `ClaudePluginCheck` control deleted from the wxs; zero references remain repo-wide; docs already routed operators to the dashboard toggle / `Register-McosControlPlugin.ps1` (`docs/wiki/Claude-Code-Plugin.md` §"Why the MSI doesn't register the plugin") | compliance audit (wxs inspection); grep zero-reference check |
| MCOS-004 | Fixed | `GatewayRequestJob` + bounded `jobQueue_` (depth 64) + 4 workers; `/health` answered inline on the receive thread; saturation -> structured 503 | Structural: testToolCatalogRefresh... proves mutex-free execution paths; live HTTP.sys load test environment-blocked (see risks) |
| MCOS-005 | Fixed | `Stop()` is three-phase: state+snapshot under mutex, queue shutdown + all joins with NO lock held, handle close + finalize under mutex; `teardownHttpSysLocked` replaced by `closeHttpSysHandlesLocked` (no joins) | Lifecycle covered by suite construction/destruction; live stop-under-load stress environment-blocked (see risks) |
| MCOS-006 | Fixed | `Mcp-Session-Id` (+ `X-MCOS-Session-Id` fallback) read in serveLoop; `LeaseRequest.sessionId/stateful` populated; stateful leases sticky (router reuses lease), stateless release via scope guard; idle-timeout sweep + Released GC in LeaseRouter; contract documented in Gateway.md | testGatewayStickySessionRoutesSameSessionToSameInstance, testGatewayDifferentSessionsCanDistributeAcrossInstances, testGatewayStatelessCallsReleaseLeaseAfterCall, testGatewayStatefulSessionLeaseExpiresOrReleases |
| MCOS-007 | Fixed | `resolveWorkerExecutable` (absolute / relative-with-separator / bare via SearchPathW + .exe/.cmd/.bat probing); resolved path used for CreateProcessW; diagnostic names the executable; docs updated | testWorkerSupervisorResolvesPathExecutable (real .cmd spawned via PATH), testWorkerSupervisorRejectsMissingBareExecutable, testWorkerSupervisorAcceptsAbsoluteExecutablePath |
| MCOS-008 | Fixed (POST-only alpha outcome) | 405 on non-POST /mcp now carries `Allow: POST` + valid JSON; Gateway.md transport-profile section; Onboarding.md compat note; runtime onboarding caveats state POST-only/no-SSE; runtime/doc/test alignment pinned | testMcpTransportContractMatchesDocs |
| MCOS-009 | Fixed | `currentToolCatalog` snapshots under lock, `collectToolCatalog` does child RPC with no gateway lock, publish under lock; `bridgeRequestIdCounter_` atomic | testToolCatalogRefreshDoesNotHoldGatewayMutexDuringChildRpc (blocked child RPC while CurrentStatus() returns promptly) |
| MCOS-010 | Fixed | Destructor is join-only (no detach path exists); order: `shuttingDown_` -> wake watchdog -> `shutdownAll()` -> unconditional join; spawn gates in respawn pass, `ensureMinInstances`, `scaleUpOnce`; ownership comments added | testWorkerSupervisorDestructorJoinsWatchdogWithoutDetach, testWorkerSupervisorShutdownDoesNotRespawnDuringTeardown |
| MCOS-011 | Fixed | Admin listener: 8-worker bounded pool + depth-64 socket queue + structured 503; SSE already detaches so it cannot occupy the pool; 10 MB request-size bound (413); socket ownership fixed (OPTIONS leak + legacy double-close removed) | Live smoke exercised the pooled listener end-to-end; queue-saturation simulation not automatable hermetically (see risks) |
| MCOS-012 | Fixed | `HttpHeaderParse.h` used by plain + TLS readers; lowercase accepted, invalid -> 400 | testAdminParserAcceptsLowercaseContentLength, testAdminParserRejectsInvalidContentLength + live smoke (raw-socket lowercase + `banana` -> 400) |
| MCOS-013 | Fixed | `validateServerRegistration` shared by fake + native adapters (both Register methods); native returns structured failure without inserting | testNativeGatewayRegistrationRejectsEmptyName; existing testFakeGatewayRegistrationRejectsEmptyName still passes |
| MCOS-014 | Fixed | `BuildGatewayInternalErrorBody` (nlohmann + error_handler_t::replace) used in `processGatewayJob` catch paths; id:null preserved | testGatewayExceptionResponseEscapesJsonMessage |
| MCOS-015 | Fixed | `AdminRouteRegistry.h` typed registry (diagnostics + claude/chatgpt/grok plugin routes added) used by method-allow handling; live smoke: POST /api/diagnostics/events -> 405, `Allow: GET, HEAD, OPTIONS` | testSupportedMethodsIncludesDiagnosticsRoutes, testSupportedMethodsIncludesPluginRoutes, testWrongMethodOnKnownRouteReturns405 + live smoke |
| MCOS-016 | Fixed | MSI failure/absence now throws unless `-AllowZipOnly`; explicit path writes `zipOnlyExplicit` + `zipOnlyReason` metadata and warns; stale "not fatal" comment replaced | PSParser: 0 syntax errors; full end-to-end run environment-blocked on this host (see risks) |
| MCOS-017 | Fixed | Mermaid diagram now matches `MasterControlModels.h` field-for-field; healthProbe rows carry `template.` prefix; probe transports corrected to `http`/`stdio_handshake`/`none` | Manual doc review against the header (recon-verified field list) |

## Commands run

```powershell
# Debug: build all non-Shell targets + full suite
cmake --preset debug
cmake --build build/debug --config Debug --target MasterControlOrchestrationServerTests MasterControlServiceHost MasterControlBootstrapper MasterControlOrchestrationServerLauncher MasterControlOrchestrationServerSetup MasterControlBaselineToolsWorker VsTestConsoleProxy ForsettiCoreTests ForsettiPlatformTests ForsettiArchitectureTests -- /m
ctest --preset debug --output-on-failure                       # 100% (4/4 executables; 135 bool tests in the main suite)

# Release
cmake --preset release
cmake --build build/release --config Release --target <same target list> -- /m
ctest --test-dir build\release -C Release --output-on-failure --timeout 300   # 100%

# Static gates (both editions), after all changes
powershell -NoProfile -File scripts\Invoke-MCOSRemediationGates.ps1 -RepoRoot . -SkipBuild   # exit 0, 7/7
pwsh       -NoProfile -File scripts\Invoke-MCOSRemediationGates.ps1 -RepoRoot . -SkipBuild   # exit 0, 7/7 (portable pwsh 7.5.2)

# Bridge + package script static validation
py -3 -m py_compile .claude-plugin\mcos-control\mcp-servers\mcos-bridge\server.py            # exit 0
# PSParser tokenize of scripts\Package-MasterControlOrchestrationServer.ps1                  # 0 errors

# Live runtime smoke (console mode, port 7300, unprivileged)
build\debug\src\MasterControlServiceHost\Debug\MasterControlServiceHost.exe --console
# + scripted probe: 15/15 PASS (detail below)
```

## Test results

| Test/check | Result | Notes |
|---|---|---|
| cmake --preset debug / release | PASS | vcpkg manifest resolves (nlohmann-json, sqlite3) |
| cmake --build (debug + release) | PASS | all remediated targets; zero warnings introduced under /W4 /WX |
| ctest (debug + release) | PASS | 135 bool tests in main suite incl. 25 new; Forsetti suites green |
| Static gates x2 editions | PASS | 7/7 both, post-change |
| package script default MSI behavior | DEGRADED | Fail-fast verified by code path + PSParser; full run needs the WinUI Shell (v145) + WiX 5, neither on this host |
| gateway slow-call health check (live HTTP.sys) | DEGRADED | Needs URL ACL for http://+:8080 (netsh, admin) absent on this host; concurrency design covered hermetically (worker pool + inline /health + catalog-lock test) |
| gateway shutdown stress (live) | DEGRADED | Same environment constraint; lock-order fix verified structurally (no join under mutex; three-phase Stop) |
| PATH executable resolution | PASS | Automated: real `.cmd` spawned via PATH under supervision (job-object kill verified by teardown test) |
| Live admin smoke (console host on 7300) | PASS 15/15 | health 200; partial POST 400 + guidance; PATCH 200 with all unrelated sections preserved; wrong-verb diagnostics 405 + `Allow: GET, HEAD, OPTIONS`; lowercase `content-length` parsed; `Content-Length: banana` -> 400 |

## Forsetti/OOP compliance statement

```text
Interface-first contracts preserved or improved: yes — IAdminApiService gained applyConfigurationPatchJson; IWorkerSupervisor/ILeaseRouter now constructible via interface-returning factories (MasterControlContracts.h).
Constructor dependency injection preserved for new collaborators: yes — LeaseRouter(idleTimeout) injected; gateway worker pool owned by the adapter; no service locators.
Concrete production classes final unless documented otherwise: yes — GatewayRequestJob, ContentLengthParse, ConfigurationPatchOutcome, StatelessLeaseReleaseGuard all final; existing final classes unchanged.
No hidden globals added: yes — all new state is class members or function locals.
No raw new/delete added: yes — std::thread/std::deque/std::shared_ptr/RAII throughout (verified over the diff).
No cyclic dependencies introduced: yes — new headers depend only on std + nlohmann (+ models for tests).
Windows-specific code remains in platform/runtime adapter areas: yes — SearchPathW resolver in MasterControlRuntime.cpp beside the existing findCommandOnPath; HTTP.sys work inside NativeHttpSysGatewayAdapter.
Forsetti types remain opaque: yes — no Forsetti-Framework file touched, no Forsetti type dissected.
No new gateway implementation added: yes — NativeHttpSysGatewayAdapter refactored in place; FakeMcpGatewayAdapter unchanged in shape.
No new CI checks or workflows added: yes — .github/workflows untouched.
```

## Post-review hardening

An adversarial multi-lens review (concurrency/lifecycle, logic correctness, Windows-native API usage, constraint compliance) ran over the full diff after the initial green verification. The compliance lens reported zero violations. Ten real defects surfaced across the other lenses and were fixed, then the entire verification chain (debug+release build, both ctest suites, gates on both editions, live console smoke) was re-run green:

1. **Gateway Start/Stop interleave (critical, introduced by the multi-phase Stop):** a Start() racing Stop()'s unlocked join phase could move-assign a joinable `std::thread` (std::terminate) or wedge Stop forever. Fixed with a dedicated `lifecycleMutex_` serializing Start()/Stop() end-to-end; state reads (`CurrentStatus`/`Probe`, i.e. `/health`) still use only `mutex_`, preserving the deadlock fix.
2. **Sticky lease pinned to a dead instance:** the sticky path now validates the bound instance is still Ready/Busy in the pool; a dead binding releases the lease and the session transparently rebinds to the respawned instance. Test: `testGatewayStickySessionRebindsAfterInstanceLoss`.
3. **Stateful lease kept on bridge failure:** the gateway now releases a stateful lease when `sendStdioJsonRpc` fails, so the next call rebinds instead of retrying a dead instance forever.
4. **Detached SSE threads outliving stop():** SSE stream threads are now tracked in a registry; `stop()` shuts their sockets, joins them, then closes — no thread can touch freed server/runtime state during service stop.
5. **Per-instance stdio mutex use-after-free:** `ChildProcess::stdioMutex` is now `std::shared_ptr<std::mutex>` and `sendStdioJsonRpc` holds shared ownership, so watchdog reap / terminate / write-failure cleanup can never destroy a mutex mid-hold.
6. **PATCH read-merge-write atomicity:** a `configurationWriteMutex_` serializes `applyConfigurationJson`/`applyConfigurationPatchJson`, closing the lost-update window opened by the concurrent admin worker pool.
7. **Extensionless-shim resolution:** the worker resolver probes `.exe`/`.cmd`/`.bat` BEFORE the bare name, so `npx` resolves to `npx.cmd` instead of the POSIX shim nodejs ships beside it. Test: `testWorkerSupervisorPrefersCmdOverExtensionlessShim`.
8. **Batch-script launch quoting:** `.cmd`/`.bat` workers launch explicitly as `cmd.exe /d /s /c ""script" args"` with `lpApplicationName` pinned, surviving spaced install paths plus quoted args (the implicit relaunch corrupted such command lines). Covered by the same test (spaced directory + quoted arg); the cmd `%VAR%` expansion caveat is documented in Worker-Pools.md.
9. **HTTP.sys ERROR_MORE_DATA orphaning:** oversized requests are now re-received by their pinned `RequestId` instead of `HTTP_NULL_ID`, so >16 KB-header clients get served rather than hanging to connection timeout.
10. **CORS/doc drift:** `Access-Control-Allow-Methods` now includes PATCH/DELETE (browser dashboards can call the new PATCH route) and `X-Confirm-Unsafe` joined `Allow-Headers`; the plugin command/README/wiki no longer reference the removed `mcos_forsetti_module_import` tool (`forsetti-import.md` rewritten around manifest deployment + the state route).

One finding was deliberately deferred (documented below in risks): the receive thread still drains request entity bodies synchronously, so a slow-trickle client on the trusted LAN can stall intake; fixing it properly means moving the drain onto the worker jobs.

## Remaining risks

| Risk | Severity | Follow-up required |
|---|---|---|
| Live HTTP.sys integration checks (health-during-slow-call, stop-under-load stress) not executed on this host | Medium | Run on a host with the `netsh http add urlacl url=http://+:8080/` ACL (or elevated) or on the CI product-gate runner; the hermetic suite covers the underlying lock/queue behavior |
| Packaging fail-fast not executed end-to-end (host lacks v145 Shell build + WiX 5) | Low | One `Package-MasterControlOrchestrationServer.ps1 -Preset release` run on a full-toolset host: expect throw at MSI step without `-AllowZipOnly`, success + `zipOnlyExplicit` metadata with it |
| Two concurrent catalog refreshes may both fan out child tools/list on a cold cache (last writer wins) | Low | Accepted trade-off documented in `currentToolCatalog`; serialize with a refresh-in-flight flag only if child RPC volume ever matters |
| Stateful session leases pin an instance slot for up to the 15-minute idle window | Low | Operator-visible via /api/pools/{id}/leases; tune `createLeaseRouter` idle timeout if pools with maxActiveLeasesPerInstance=1 see contention |
| MasterControlShell (v145) still unbuildable locally | Pre-existing | Unrelated to this remediation; CI covers it |
| Receive thread drains request entity bodies synchronously; a slow-trickle LAN client can stall gateway intake (incl. /health) for the drain duration | Low (trusted-LAN posture) | Move the body drain into the worker jobs (store RequestId + MORE_ENTITY_BODY state in GatewayRequestJob) and/or cap accepted Content-Length with HttpCancelHttpRequest on slow drains |
| cmd.exe %VAR% expansion applies inside batch-worker args (operator-owned config; documented in Worker-Pools.md) | Low | Escape or reject cmd metacharacters in template.args if pool templates ever become non-operator input |

## Operator notes

- **Config patch route:** partial updates now go to `PATCH /api/config` (deep merge; unrelated sections preserved; `droppedKeys` reported for unknown top-level fields). `POST /api/config` remains full-replacement and rejects partial bodies with HTTP 400 pointing at PATCH. The mcos-bridge `mcos_config_update` tool already uses PATCH.
- **Plugin registration:** the MSI no longer shows a plugin checkbox (it never worked). Register through the dashboard plugin toggle or `scripts\Register-McosControlPlugin.ps1`.
- **MCP transport:** POST-only Streamable HTTP subset. `GET /mcp` = 405 + `Allow: POST` (spec-permitted). `Mcp-Session-Id` gives sticky per-session routing; sessions expire after 15 idle minutes or via `POST /api/leases/{leaseId}/release`.
- **Zip-only packaging:** requires `-AllowZipOnly` (optionally `-ZipOnlyReason`); the metadata JSON records `zipOnlyExplicit: true` + the reason. A default run fails when the MSI cannot be built.
- **Worker executables:** `template.executable` accepts absolute paths, relative paths with separators, or bare PATH commands (`npx.cmd`, `node`) with `.exe`/`.cmd`/`.bat` probing; resolution failures name the executable in the instance statusMessage.
