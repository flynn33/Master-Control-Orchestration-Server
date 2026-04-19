# 17. Soak / Stability (20 min) - Build 48

**Date:** 2026-04-19
**Target:** `http://127.0.0.1:7300/` (MCOS build 48)
**Duration planned:** 1200s (20 min)
**Receipt status:** IN PROGRESS

## Baseline (t=0)

- Initial `Get-Process MasterControlServiceHost`: **PID 12908, WorkingSet 13,840,384 bytes (~13.2 MB)**, ThreadCount field empty (returned null; only `Id` and `WorkingSet64` populated).
- Baseline `/api/health`: first probe **200 OK**, body `{"status":"ok","time":"2026-04-19T15:34:35Z"}`.
- Second baseline health probe (~2 s later): **TIMEOUT** ("The operation has timed out.").
- Raw: `G:/Claude/mcos_proof_soak_baseline.txt`.

## Soak launch

- Script: `G:/Claude/mcos_proof_soak_run.ps1` (parallel triple-GET each 1s, POST instantiate each 30s, CSV sample each 60s).
- Background launcher PID: **7828** (logged to `G:/Claude/mcos_proof_soak_pid.txt`).

## CRITICAL FINDING at t≈+30s

The service process `MasterControlServiceHost` has **disappeared entirely**.

- `Get-Process -Name MasterControlServiceHost` returns NULL.
- `Get-Process -Id 12908` returns NULL (original baseline PID is gone).
- `netstat -ano | findstr 7300` shows **no LISTENING socket** - only dozens of `TIME_WAIT` entries from connections that closed when the listener died, and one `SYN_SENT` from `MasterControlShell` PID 15772 attempting to reconnect.
- `Test-NetConnection 127.0.0.1 -Port 7300` returns **False**.
- First CSV sample row shows `pid=0, ws_mb=0, health_status=ERR, fail_count=0`.
- First failure log entry: `2026-04-19T10:36:02 | POST /instantiate | err=Unable to connect to the remote server`.

## Running processes (MasterControl-related) at t≈+30s

| PID   | Process            |
|-------|--------------------|
| 8724  | MasterControlShell |
| 15772 | MasterControlShell |

No `MasterControlServiceHost`, no listener on 7300. **Service has crashed and has not auto-restarted.**

## Crash-and-restart timeline (first 10 min)

| Time (local) | Event |
|---|---|
| 10:34:35 | Baseline health 200, PID 12908 (13.2 MB). |
| ~10:35 | **Crash #1** — port 7300 loses its LISTENING socket; many TIME_WAIT entries; all POST /instantiate calls fail with "Unable to connect to the remote server". |
| 10:38:15 | **Auto-restart** — new PID 1720 appears. Port 7300 listening again ~30s later. Health returns 200. ~3 min downtime. |
| 10:38:34 – 10:40:53 | Stable-ish window: PID 1720, WS 14.15 MB -> 14.66 MB (+3.6% over 2 min — within noise). Health 200. |
| ~10:41 | **Crash #2** — sample at 10:41:53 shows PID 0 / ERR. CSV fail_count frozen at 7 because POST was already skipped when conn refused. |
| 10:42:xx | **Auto-restart** — transient PID 3356 visible mid-restart (12.5 MB, no StartTime), then final PID 16456. Listener back at ~10:43. |

Notes on counting:
- `fail_count` in CSV understates real GET failures. The soak script used `Start-ThreadJob`, which is unavailable on this host (no ThreadJob module), so parallel GETs never fired. Only the sequential POST /instantiate is reliably counted. During the first outage 5 POSTs failed back-to-back; during recovery 2 POSTs timed out (>15s SLA).
- `POST /instantiate {}` against a healthy service returns **200 OK** with `succeeded:false` ("requires at least 1 connected provider"), so the business logic is fine. The failures above are connectivity/timeout, not business errors.

## Parallel probe (manual, during a healthy interval, 30 iterations x 3 endpoints)

- `/api/activity` — ok=30/30, avg 20ms, max 38ms. Ring buffer responsive.
- `/api/readiness` — ok=30/30, avg 45ms, max 761ms.
- `/api/providers/signin/installed` — ok=29/30, avg 352ms, max 10080ms. **One timeout outlier > 10s.**

Probe completion preceded Crash #2 by ~30 s, so the signin/installed endpoint under load is a plausible trigger (not confirmed).

## Interim verdict

**INCONCLUSIVE** for pure stability — a concurrent installer test poisoned the run.

## Root cause of the observed PID churn (Windows Event Log)

- **10:37:16–10:37:18** `MsiInstaller` (provider 1040/1033/1042/11708): transaction for `G:\Claude\mcos_test_install_v045.msi`, client PID 7912, result `1603` (installation failed / rolled back).
- **10:38:32** `Service Control Manager` 7045: "Master Control Orchestration Server" service re-**installed**.
- **10:39:42** `Service Control Manager` 7045: service **re-installed again**.
- Result: the service was being uninstalled/reinstalled during the soak by a separate installer-verification agent running concurrently on this host, which accounts for the PID churn (12908 → 1720 → 3356 → 16456 → 12336 …) and the connectivity gaps.

This makes the pure-stability verdict **INCONCLUSIVE**: the GETs never established a steady workload (Start-ThreadJob unavailable) and the POSTs sat through rolling re-installs.

## T+5 snapshot (appended from soak CSV)

```
2026-04-19T10:40:53,1720,14.66,200,7
2026-04-19T10:41:53,0,0,ERR,7         <- crash window
2026-04-19T10:42:50,16456,12.93,200,8
2026-04-19T10:43:54,16456,14.86,ERR,8 <- momentary health timeout
2026-04-19T10:44:50,12336,12.72,200,9
```

Latency outliers so far (POST /instantiate > 5 s): 4 calls between 9.2 s and 11.9 s (all returned 200).
