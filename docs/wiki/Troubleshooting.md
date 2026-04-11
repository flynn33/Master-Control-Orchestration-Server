# Master Control Orchestration Server — Troubleshooting

Symptom-first guide to the most common failure modes. If something here matches 
what you're seeing, follow the diagnosis chain in order.

---

## Shell shows `API OFFLINE` even though the runtime is up

**Cause:** an older build gated `CaptureSnapshot` on the SCM service state, so 
console-mode runs (where the service is technically not registered) showed offline.

**Fix:** upgrade to the current build. The shell now probes `/api/health` and 
`/api/dashboard` directly and trusts the API response over SCM.

---

## Auto-Connect returns HTTP 401

**Cause:** the credential is rejected by the upstream provider.

**Diagnosis:**

1. Re-run with `discoverModels: false` — if it still fails, the probe step is the issue.
2. Check the `steps` array in the response — the failing stage is flagged.
3. Verify the credential field name matches the capability descriptor (`api_key`, `apikey`, `token`, `secret` are all accepted).

---

## MCP server / sub-agent upsert times out for ~10 seconds

**Cause:** older builds called the synchronous `IRuntimeInventoryService::refresh()` 
from the mutating handler, which probed every endpoint sequentially.

**Fix:** the current contract uses `refreshAsync()` so admin calls return immediately 
and the inventory probe runs on a detached background thread, coalesced via an 
`std::atomic_bool` pending flag.

---

## Sub-agent group upsert returns HTTP 400 right after creating its members

**Cause:** the validator was reading the inventory cache, which is updated only after 
`refreshAsync()` finishes — so a fast-following request hit a stale view.

**Fix:** the validator now reads `state_->configuration.activeProfile.seededEndpoints` 
directly under the same mutex as the upsert, eliminating the race.

---

## Setup launcher fails to elevate

**Diagnosis:**

1. Check `~\Desktop\MasterControlOrchestrationServer-install-log-pointer.txt` for the real log path.
2. The launcher uses `ShellExecuteEx` with the `runas` verb. If UAC is suppressed at the policy level, the elevation will fail silently.
3. Fall back to `Install-MasterControlOrchestrationServer.ps1 -Verbose` for a richer log trail.

---

## Tron palette looks washed out

**Cause:** an unstyled control is rendering with the default Fluent brushes.

**Fix:** make sure the control is inside a `RootGrid` with `RequestedTheme="Dark"` 
and that there's an implicit `Style TargetType` entry in `App.xaml` for the 
control's type. Fluent theme brushes (`TextFillColorPrimaryBrush`, etc.) should also 
be remapped — see the existing entries in `App.xaml`.

---

## Build fails with `PlatformToolset=v143 not installed`

**Cause:** the toolchain is **v145** (Visual Studio 2026 / VS18). v143 is not supported.

**Fix:** install Visual Studio 2026 with the C++ workload, or update the platform 
toolset in the affected vcxproj/CMake config.

---

## Browser dashboard renders blank

**Diagnosis:**

1. Open DevTools and check the Console for asset 404s.
2. Confirm `share/MasterControlOrchestrationServer/web/` is staged in the install root.
3. Confirm `/api/health` returns `200 OK` from the same host.

---

## Where the logs live

| Log | Path |
| --- | --- |
| Installer (real log) | `%PUBLIC%\Documents\Master Control Orchestration Server\logs\installer\` |
| Installer (pointer file) | `~\Desktop\MasterControlOrchestrationServer-install-log-pointer.txt` |
| Service host | `%ProgramData%\Master Control Orchestration Server\logs\service\` |
| Shell | `%LOCALAPPDATA%\Master Control Orchestration Server\logs\shell\` |

---

See also: [Operations](Operations) · [Architecture](Architecture) · 
[API Reference](API-Reference)
