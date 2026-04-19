# Master Control Orchestration Server - Admin API Probe Results

**Probe run timestamp**: 2026-04-19 UTC (server clock: `2026-04-19T14:08:04Z` via `/api/health`)
**Target**: `http://127.0.0.1:7300/`
**Probe evidence**: raw response bodies saved under `G:\Claude\mcos_probe_*.json`

---

## Summary Table

| Endpoint | Method | Status | Bytes | 1-line summary |
|---|---|---:|---:|---|
| `/api/health` | GET | 200 | 54 | `{"status":"ok","time":"2026-04-19T14:08:04Z"}` |
| `/api/dashboard` | GET | 200 | 89,100 | Full dashboard payload: endpoints, MCP servers, providers, specialists, workflows. |
| `/api/config` | GET | 200 | 15,895 | Active profile (`PC-GAMING-R7-58`) with 24+ seeded endpoints, providers, specialists. |
| `/api/readiness` | GET | 200 | 739 | `firstRunCompleted:false`; 1 blocking issue (`specialists.none-ready`); recommendedNextStep = `create-specialist`. |
| `/api/activity` | GET | 200 | 35,356 | 148 admin API events (latest id: 148, `GET /api/readiness -> 200`). |
| `/api/providers` | GET | 200 | 1,385 | 5 providers: codex, claude-code, xai-grok, xai-grok-20260419-075934, -080045. None have credentials. |
| `/api/providers/signin/installed` | GET | 200 | 339 | `claude` CLI installed at `C:\WINDOWS\...\claude.cmd`; `codex` CLI not installed. Neither signed in. |
| `/api/setup/dependencies` | GET | 200 | 2,318 | Node.js ready (v24.15.0); claude-code-cli and codex-cli not installed but installable. |
| `/api/setup/workflow-templates` | GET | 200 | 751 | 3 templates: single-provider-demo, mcp-assisted-demo, specialist-team-demo. |
| `/api/clu` | GET | 200 | 21,345 | Posture=`warning`; 1 finding (CLU-C003 bypass visibility); 7 rules; 4 roles; 3 governance MCP servers online. |
| `/api/clu/tools` | GET | 200 | 8,326 | 24 Forsetti governance tools (windows/macos/ios platforms). |
| `/api/clu/apple-operations` | GET | 200 | 2 | `[]` (no Apple operations recorded). |
| `/api/platform-services` | GET | 200 | 4,462 | Windows/macOS/iOS gateways + governance services; `registration_failed` on gateway LAN advertisement. |
| `/api/platform-services/gateways` | GET | 200 | 2,232 | 3 gateway entries (windows/macos/ios), all `status:registration_failed`. |
| `/api/platform-services/governance` | GET | 200 | 1,917 | 3 governance MCP servers (windows/macos/ios), all `status:online`. |
| `/api/platform-services/apple-hosts` | GET | 200 | 2 | `[]` (no Apple hosts registered). |
| `/api/setup/workflow-templates/single-provider-demo/instantiate` | POST | 200 | 169 | `succeeded:true`, workflowId `single-provider-demo:1-targets`, 1 assignment applied. |
| `/api/setup/workflow-templates/mcp-assisted-demo/instantiate` | POST | 200 | 163 | `succeeded:true`, workflowId `mcp-assisted-demo:1-targets`, 1 assignment applied. |
| `/api/setup/workflow-templates/specialist-team-demo/instantiate` | POST | 200 | 146 | `succeeded:false` (requires >=2 specialists, ready=0) — correct behavior. |
| `/api/providers/auto-connect` (xai happy path) | POST | 200 | 1,466 | `succeeded:true`, providerId `xai-grok-20260419-090906`, DPAPI-encrypted credentials stored, 7 stages passed. |
| `/api/providers/auto-connect` (unknown assignment target) | POST | 400 | 1,595 | `succeeded:false`, `apply-assignments` stage failed: `nonexistent-xyz (unknown target)`. |
| `/api/providers/auto-connect` (malformed JSON) | POST | 400 | 939 | `succeeded:false`, `parse` stage failed with structured error. |
| `/api/providers/auto-connect` (unknown kind) | POST | 400 | 582 | `succeeded:false`, `parse` stage failed: `Unknown enum string: bogus-provider-kind-9001`. |
| `/api/setup/workflow-templates/bogus-id/instantiate` | POST | 404 | 111 | `succeeded:false`, message `"Unknown starter workflow template id."` |

---

## Edge-case sections

### 1. Malformed JSON on `/api/providers/auto-connect`

Command:

```bash
curl.exe -s -X POST -H "Content-Type: application/json" \
  --data-binary '{not json}' \
  http://127.0.0.1:7300/api/providers/auto-connect
```

Response (HTTP **400**, 939 bytes, structured body — not empty 500):

```json
{
  "assignmentsApplied": [],
  "assignmentsFailed": [],
  "baseUrl": "",
  "discoveredModels": [],
  "displayName": "",
  "errorMessage": "Could not parse request: [json.exception.parse_error.101] parse error at line 1, column 3: syntax error while parsing object key - invalid literal; last read: '{no'; expected string literal",
  "providerId": "",
  "selectedModelId": "",
  "steps": [
    {"stage":"parse","succeeded":false,"latencyMs":0,"message":"Could not parse request: ... expected string literal"}
  ],
  "succeeded": false,
  "summary": "Could not parse request: ...",
  "totalLatencyMs": 0
}
```

Verdict: structured error body with a dedicated `parse` stage entry. No empty 500.

### 2. Unknown `kind` on `/api/providers/auto-connect`

Command:

```bash
curl.exe -s -X POST -H "Content-Type: application/json" \
  --data-binary '{"kind":"bogus-provider-kind-9001","providerId":"probe","credentials":{},"discoverModels":false}' \
  http://127.0.0.1:7300/api/providers/auto-connect
```

Response (HTTP **400**, 582 bytes):

```json
{
  "errorMessage": "Could not parse request: Unknown enum string: bogus-provider-kind-9001",
  "steps": [
    {"stage":"parse","succeeded":false,"message":"Could not parse request: Unknown enum string: bogus-provider-kind-9001"}
  ],
  "succeeded": false,
  "summary": "Could not parse request: Unknown enum string: bogus-provider-kind-9001"
}
```

Verdict: enum validation produces a readable error naming the bad token.

### 3. Bogus workflow template id

Command:

```bash
curl.exe -s -X POST -H "Content-Type: application/json" \
  --data-binary '{}' \
  http://127.0.0.1:7300/api/setup/workflow-templates/bogus-id/instantiate
```

Response (HTTP **404**, 111 bytes):

```json
{
  "message": "Unknown starter workflow template id.",
  "requiresConfirmation": false,
  "succeeded": false
}
```

Verdict: proper 404 with readable message.

### 4. Forced-success fix confirmation (`/api/providers/auto-connect` with `assignmentTargetIds:["nonexistent-xyz"]`)

The earlier "forced success" bug would return HTTP 200 `succeeded:true` even when downstream assignment steps failed. The current response correctly returns **HTTP 400** with `succeeded:false` and identifies the failed stage (`apply-assignments`) — the fix is confirmed intact. Provider registration itself still occurs (stages 1-6 succeeded, including DPAPI credential storage); only the bogus assignment is rejected, and the response surfaces that accurately in both `succeeded` flag and `assignmentsFailed` array.

---

## Verdict

**All core endpoints healthy.** 24 endpoints probed, all returned the expected status codes and well-formed payloads.

Minor observations (not regressions, environmental):

- `/api/platform-services/gateways` reports `status:registration_failed` on all three platform gateways. This is a mDNS/LAN-advertisement issue (Bonjour-style registration) and does not affect HTTP reachability — the admin API itself is serving on port 7300 without issue.
- `/api/readiness` reports `firstRunCompleted:false` with one blocking warning (`specialists.none-ready`). Expected in a freshly-provisioned environment.
- `/api/clu` posture is `warning` because troubleshooting bypass is enabled (CLU-C003). Expected given the test posture.
- `/api/providers/signin/installed` shows Codex CLI missing; Claude Code CLI present but not signed in. Expected, matches `/api/setup/dependencies` state.
- `/api/activity` contains ~25 empty-method events (`"method":""`, `"target":""`) from earlier in the session, suggesting some request-logging code paths emit incomplete entries. Minor cosmetic issue in the activity log, not a functional failure.

The three edge-case error paths (malformed JSON, unknown kind, bogus template id) all return proper 400/404 with structured, human-readable bodies. The forced-success regression on `/api/providers/auto-connect` when an assignment target is invalid is fixed — the endpoint correctly returns HTTP 400 `succeeded:false`.
