# 16 - Service Restart (Cold-Boot Surrogate)

**Build:** Master Control Orchestration Server build 48
**Host:** PC-GAMING-R7-58 (Windows 11 Pro, 10.0.26200)
**Date:** 2026-04-19
**Verdict:** RESTART WITH ISSUES — service stop/start cycle works; warm-up and
persistence are mostly clean, but two anomalies documented below.

---

## Method

`sc.exe stop` + `sc.exe start` exercises the same boot-time start path that a
reboot would. Each control command was issued from an elevated PowerShell child
process via `Start-Process -Verb RunAs -Wait -File <script>`.

## Before (`mcos_proof_restart_before.txt`)

- `sc.exe query MasterControlProgram` → `RUNNING`
- `Get-Service` → `Status=Running, StartType=Automatic`
- `GET /api/health` → `HTTP 200 {"status":"ok"}`
- Listener: `0.0.0.0:7300 LISTENING`

## Stop (`mcos_proof_restart_stop.txt`)

- `Stop-Service MasterControlProgram -Force` → `Status=Stopped`
- SCM state-transition elapsed: **1.04 s**
- Result line: `RESULT: STOP_OK`

## Stopped-state probe (`mcos_proof_restart_stopped_probe.txt`)

- `sc.exe query` → `STATE: 1 STOPPED`
- No listener on :7300 (netstat empty)
- `curl /api/health` → exit 7 `Connection refused` after 2.0 s, `HTTP=000`
- No `MasterControlServiceHost` processes

Endpoint is verifiably gone when the service is stopped.

## Start (`mcos_proof_restart_start.txt`)

- `Start-Service MasterControlProgram` → `Status=Running`
- SCM state-transition elapsed: **12.06 s**
- New PID: 16456 (parent `services.exe`)
- Binary: `C:\Program Files\Master Control Orchestration Server\MasterControlServiceHost.exe`
- Result line: `RESULT: START_OK`

## Warm-up (`mcos_proof_restart_warmup.txt`)

First successful `/api/health` 200 with `status:ok` on attempt 1, 20 ms after
the elevated starter returned. True warm-up (process-spawn → listener-ready) is
bounded by the 12.06 s SCM wait — the service reports `Running` only after its
`SERVICE_RUNNING` checkpoint, which in this build is emitted after the HTTP
listener binds. **Effective cold-boot warm-up: ~12 s.**

## Dashboard (`mcos_proof_restart_dashboard.json`)

HTTP 200, 92,494 bytes. All expected top-level keys present:
`providers`, `providerAssignmentTargets`, `providerAssignments`,
`providerCapabilities`, `providerCredentialStatuses`, `providerExecutionHistory`,
`providerExecutionRegistrations`, `endpoints`, `governance`, `governanceServers`,
`telemetry`, `surface`, `security`, `resourceAllocation`, `subAgentGroups`,
`installHistory`, `platformGateways`, `exports`, `appleRemoteHosts`.

## Activity ring (`mcos_proof_restart_activity.json`)

Ring was reset by the restart. 3 events present (all post-restart probes).
- First: `id=1 timestampUtc=2026-04-19T15:42:43.980Z`
- Last:  `id=3 timestampUtc=2026-04-19T15:42:48.221Z`
- `highWaterMarkId=3`

## Providers (`mcos_proof_restart_providers.json`)

10 providers persisted — baseline three (`codex`, `claude-code`, `xai-grok`)
plus seven `xai-grok-20260419-*` test providers from earlier sessions.
DPAPI-sealed credentials survived: `providerCredentialStatuses` in the
dashboard shows `configured:true, "Credentials are present in secure storage"`
for each test provider.

## CLU (`mcos_proof_restart_clu.json`)

HTTP 200. `recentExecutions: []`. **Observation:** CLU execution history does
NOT persist across a service restart in this build; it is in-memory only.

## Anomalies observed

1. **Install directory rewrite during the test window.** Between 10:35 and
   10:39 the `C:\Program Files\Master Control Orchestration Server\` tree was
   re-created (file timestamps 10:39) and the service was transiently
   unregistered. An external installer/bootstrapper ran concurrently.
2. **Transient START_PENDING/STOP_PENDING flapping** during that window,
   consistent with SCM's `FAILURE_ACTIONS = RESTART(5 s)` recovery. Resolved
   once the install tree was fully present on disk.

Given these disturbances, a clean control run was executed afterward and is
what the artifacts above capture. The stop/start path itself is sound: stop in
1 s, start to listener-ready in 12 s, all dashboard surfaces return 200,
DPAPI-sealed credentials persist. CLU execution history is in-memory (known).
