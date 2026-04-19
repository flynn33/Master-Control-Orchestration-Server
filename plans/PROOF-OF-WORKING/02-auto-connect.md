# Feature 2 — Auto-Connect AI providers (build 48)

**Assembly date:** 2026-04-19
**Endpoint:** `POST http://127.0.0.1:7300/api/providers/auto-connect`
**Note:** test providers `xai-grok-20260419-100947` and `xai-grok-20260419-101010`
left registered intentionally for master-assembly cross-reference.

---

## 1. Happy path — VERIFIED

**Command**
```
curl.exe -sS -X POST http://127.0.0.1:7300/api/providers/auto-connect \
  -H "Content-Type: application/json" \
  --data-binary @G:/Claude/mcos_post_happy.json \
  -w '\nHTTP=%{http_code} len=%{size_download}\n' \
  -o G:/Claude/mcos_proof_autoconnect_happy.json
```
**HTTP:** `200`   **Receipt:** `G:/Claude/mcos_proof_autoconnect_happy.json`

```json
{
  "providerId": "xai-grok-20260419-100947",
  "displayName": "Grok",
  "succeeded": true,
  "totalLatencyMs": 6,
  "steps": [
    {"stage":"resolve-capability","succeeded":true},
    {"stage":"derive-shape","succeeded":true},
    {"stage":"validate-credentials","succeeded":true},
    {"stage":"discover-models","succeeded":true},
    {"stage":"register-provider","succeeded":true},
    {"stage":"store-credentials","succeeded":true},
    {"stage":"apply-assignments","succeeded":true}
  ]
}
```
HTTP 200, `succeeded:true`, 7 stages all succeeded, `totalLatencyMs=6 > 0`, `providerId` non-empty.

---

## 2. Assignment fan-out — VERIFIED

Dashboard `providerAssignmentTargets[]` included role entries `planner`, `architect`, `auditor`, `forge`, `nexus`, `recon`, `scribe`, `sentinel`, `watchtower`, `coding-specialists`. Selected `planner` + `architect`.

**Command**
```
curl.exe -sS -X POST http://127.0.0.1:7300/api/providers/auto-connect \
  -H "Content-Type: application/json" \
  --data-binary @G:/Claude/mcos_post_fanout.json \
  -w '\nHTTP=%{http_code} len=%{size_download}\n' \
  -o G:/Claude/mcos_proof_autoconnect_fanout.json
```
**HTTP:** `200`   **Receipt:** `G:/Claude/mcos_proof_autoconnect_fanout.json`

```json
{
  "providerId": "xai-grok-20260419-101010",
  "succeeded": true,
  "assignmentsApplied": ["Planner","Architect"],
  "assignmentsFailed": [],
  "summary": "Connected 'Grok' in 7ms (0 model(s), 2 role(s))"
}
```
Both targets applied, none failed.

---

## 3. DPAPI seal verification — VERIFIED

**Command:** `curl.exe -sS http://127.0.0.1:7300/api/dashboard` → slice `providerCredentialStatuses[]` matching `providerId=xai-grok-20260419-101010`.
**Receipt:** `G:/Claude/mcos_proof_autoconnect_dpapi.json`

```json
{
  "configured": true,
  "configuredFieldIds": ["xai_api_key"],
  "message": "Credentials are present in secure storage.",
  "providerId": "xai-grok-20260419-101010",
  "updatedAtUtc": "2026-04-19T15:10:10Z"
}
```
`configured:true`, DPAPI-sealed field `xai_api_key` present.

---

## 4. Role assignment verification — VERIFIED

**Command:** same `/api/dashboard` fetch → `providerAssignments[]` filtered by `providerId=xai-grok-20260419-101010`.
**Receipt:** `G:/Claude/mcos_proof_autoconnect_assignments.json`

```json
[
  {"kind":"role","providerId":"xai-grok-20260419-101010","targetId":"planner","updatedAtUtc":"2026-04-19T15:10:10Z"},
  {"kind":"role","providerId":"xai-grok-20260419-101010","targetId":"architect","updatedAtUtc":"2026-04-19T15:10:10Z"}
]
```
Both `targetId` entries present and match requested roles.

---

## 5. Negative — unknown kind — VERIFIED

**Command**
```
curl.exe -sS -X POST http://127.0.0.1:7300/api/providers/auto-connect \
  -H "Content-Type: application/json" \
  --data-binary @G:/Claude/mcos_post_bad_kind.json ...
```
**HTTP:** `400`   **Receipt:** `G:/Claude/mcos_proof_autoconnect_bad_kind.json`

```json
{
  "succeeded": false,
  "errorMessage": "Could not parse request: Unknown enum string: anthropic-invalid",
  "steps": [
    {"stage":"parse","succeeded":false,
     "message":"Could not parse request: Unknown enum string: anthropic-invalid"}
  ]
}
```
HTTP 400, structured body, `errorMessage` mentions bad enum string, `steps[0]` parse/failed.

---

## 6. Negative — malformed JSON — VERIFIED

**Command:** same pattern with `@G:/Claude/mcos_post_bad_json.json` (`{not valid json}`).
**HTTP:** `400`   **Receipt:** `G:/Claude/mcos_proof_autoconnect_bad_json.json`

```json
{
  "succeeded": false,
  "errorMessage": "Could not parse request: [json.exception.parse_error.101] parse error at line 1, column 3: syntax error while parsing object key - invalid literal; last read: '{no'; expected string literal",
  "steps": [{"stage":"parse","succeeded":false}]
}
```
HTTP 400 with structured JSON body (not 500/empty).

---

## 7. Negative — unknown assignment target — VERIFIED

**Command:** `@G:/Claude/mcos_post_bad_target.json` with `assignmentTargetIds:["nonexistent-xyz"]`.
**HTTP:** `400`   **Receipt:** `G:/Claude/mcos_proof_autoconnect_bad_target.json`

```json
{
  "providerId": "xai-grok-20260419-101029",
  "succeeded": false,
  "errorMessage": "1 role assignment(s) failed: nonexistent-xyz (unknown target)",
  "assignmentsApplied": [],
  "assignmentsFailed": ["nonexistent-xyz (unknown target)"],
  "steps": [
    {"stage":"apply-assignments","succeeded":false,
     "message":"Applied 0 role assignment(s), 1 failed"}
  ]
}
```
HTTP 400, `succeeded:false`, `errorMessage` populated, `assignmentsFailed` contains the bad id.

---

## Verdict

**All 7 contract bullets VERIFIED**
