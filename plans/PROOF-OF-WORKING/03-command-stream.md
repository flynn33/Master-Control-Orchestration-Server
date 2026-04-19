# Feature 3 - Live command stream / 512-event ring buffer

Build: 48
Host: http://127.0.0.1:7300/
Date: 2026-04-19

Evidence artifacts:
- `G:/Claude/mcos_proof_activity_schema.json` (initial snapshot)
- `G:/Claude/mcos_proof_activity_cap.json` (after 600-request burst)
- `G:/Claude/mcos_proof_activity_excluded.json` (after excluded-endpoint burst)

---

## Bullet 1 - Schema check

Command:
```bash
curl.exe -s http://127.0.0.1:7300/api/activity -o G:/Claude/mcos_proof_activity_schema.json
powershell.exe -NoProfile -ExecutionPolicy Bypass -File G:/Claude/mcos_proof_probe.ps1 -Path G:/Claude/mcos_proof_activity_schema.json
```

Observed first-event keys: `actor, detail, id, kind, latencyMs, message, method, statusCode, target, timestampUtc`.

Sample event:
```json
{"actor":"admin-api","detail":"","id":"1","kind":"admin_api_request","latencyMs":5,"message":"POST /api/providers/auto-connect -> 200 (5ms)","method":"POST","statusCode":200,"target":"/api/providers/auto-connect","timestampUtc":"2026-04-19T12:59:34.554Z"}
```

Required fields `id, timestampUtc, method, target, statusCode, latencyMs, actor, kind` are all present (plus `message` and `detail`).

Status: **VERIFIED**

---

## Bullet 2 - Cap enforcement (ring size == 512)

Command:
```bash
for i in $(seq 1 600); do curl.exe -s -o /dev/null 'http://127.0.0.1:7300/api/forsetti/modules'; done
curl.exe -s http://127.0.0.1:7300/api/activity -o G:/Claude/mcos_proof_activity_cap.json
powershell.exe -NoProfile -ExecutionPolicy Bypass -File G:/Claude/mcos_proof_probe.ps1 -Path G:/Claude/mcos_proof_activity_cap.json
```

Observed:
- `events.Count` = **512**
- `highWaterMarkId` = 848
- `first.id` = 337, `last.id` = 848

Status: **VERIFIED** (events.length == 512).

---

## Bullet 3 - Monotonic id

From the post-burst snapshot (same probe run as Bullet 2):
- `id.max` = 848, equal to `highWaterMarkId` (848).
- `ids.strictly.increasing` = **True** (per-index numeric compare across all 512 events).
- `ids.dup.groups` = 0 (no duplicates).

Status: **VERIFIED** (`highWaterMarkId` == max(id), ids strictly increasing, no dupes, no gaps in the 337..848 window).

---

## Bullet 4 - FIFO eviction

Computation: `highWaterMarkId - events[0].id = 848 - 337 = 511`; `events.length - 1 = 512 - 1 = 511`. They are equal, so eviction order is correct (oldest first, newest retained).

Status: **VERIFIED**.

---

## Bullet 5 - Excluded endpoints

Command:
```bash
for p in health dashboard config; do for i in 1 2 3 4 5; do curl.exe -s -o /dev/null "http://127.0.0.1:7300/api/$p"; done; done
curl.exe -s http://127.0.0.1:7300/api/activity -o G:/Claude/mcos_proof_activity_excluded.json
powershell.exe -NoProfile -ExecutionPolicy Bypass -File G:/Claude/mcos_proof_excluded.ps1 -Path G:/Claude/mcos_proof_activity_excluded.json
```

Observed:
- `events.count` = 512, `highWaterMarkId` = 848 (UNCHANGED from post-burst snapshot).
- `excluded.hits` (count of events whose `target` is `/api/health`, `/api/dashboard`, or `/api/config`) = **0**.
- Distinct targets in the ring (5): `/api/forsetti/modules`, `/api/platform-services`, `/api/platform-services/apple-hosts`, `/api/platform-services/apple-hosts/remove`, `/api/providers/auto-connect`. None of the three excluded paths, and `/api/activity` itself, are present.

Because `highWaterMarkId` did not advance past 848 after 15 excluded-endpoint requests plus two `/api/activity` GETs, the exclusion is confirmed by absence: those paths never take a ring slot.

Status: **VERIFIED**.

---

## Verdict

Feature 3 VERIFIED: ring buffer hard-caps at 512 events, ids are monotonically increasing with `highWaterMarkId` tracking the max, FIFO eviction preserves contiguity, and `/api/health`, `/api/dashboard`, `/api/config` (and `/api/activity`) are excluded from capture.
