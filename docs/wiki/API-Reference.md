# Master Control Orchestration Server — API Reference

![base](https://img.shields.io/badge/base-http://127.0.0.1:7300-00f6ff?style=flat-square) ![auth](https://img.shields.io/badge/auth-loopback%20only-0a1018?style=flat-square) ![format](https://img.shields.io/badge/format-JSON-00aacc?style=flat-square)

Every route is served by
[`MasterControlRuntime.cpp`](../../src/MasterControlApp/MasterControlRuntime.cpp)
and bound to the loopback interface. The shell, browser admin UI, and the
feature-test harness all consume the same routes — there is no second API.

---

## Read endpoints

| Method | Route | Returns |
| --- | --- | --- |
| `GET` | `/api/health` | Service health and readiness snapshot |
| `GET` | `/api/dashboard` | Composite payload powering the desktop shell + browser dashboard |
| `GET` | `/api/config` | Current persisted configuration |
| `GET` | `/api/providers` | Provider catalog, credential posture, and assignments |
| `GET` | `/api/exports` | Export inventory + handoff artifacts |
| `GET` | `/api/forsetti/surface` | Forsetti surface model used by both UIs |
| `GET` | `/api/forsetti/modules` | Module catalog metadata |
| `GET` | `/api/install/history` | Install + import execution history |
| `GET` | `/api/beacon` | LAN beacon advertisement payload |
| `GET` | `/api/activity?since={id}` | Activity ring events newer than `id` |

### Activity event shape

```json
{
  "highWaterMarkId": 174,
  "events": [
    {
      "id": 174,
      "kind": "api",
      "timestampUtc": "2026-04-11T17:42:13.812Z",
      "method": "POST",
      "target": "/api/providers/auto-connect",
      "statusCode": 200,
      "latencyMs": 184,
      "summary": "auto-connect openai succeeded"
    }
  ]
}
```

The ring buffer holds the last **512** events with monotonically
increasing IDs. The shell polls every two seconds while focused.

---

## Auto-Connect AI

| Method | Route | Purpose |
| --- | --- | --- |
| `POST` | `/api/providers/auto-connect` | Add an AI provider end-to-end (capability resolution → discovery → DPAPI → assignment) |

**Request body:**

`kind` must be one of the `ProviderKind` strings: `codex`, `claude_code`, `openai`,
`xai`, or `generic` (see `providerKindFromString` in `MasterControlModels.cpp`).
For Grok specifically the shell sends `kind=xai` with `providerId=xai-grok`.

`credentials` is a map of `{ fieldName: value }` — the field names depend on
the capability module (e.g. OpenAI uses `api_key`; xAI uses `xai_api_key`).

`assignmentTargetIds` entries must match active `ProviderAssignmentTarget::targetId`
values. The built-in Role targets are `planner`, `architect`, and `auditor`.
Specialist (SubAgent) target ids are discovered at runtime from
`GET /api/provider-assignment-targets`.

```json
{
  "kind": "openai",
  "providerId": "openai-primary",
  "credentials": { "api_key": "sk-..." },
  "assignmentTargetIds": ["planner"],
  "discoverModels": true
}
```

**Response body** (`AutoConnectResult`):

The pipeline emits one `AutoConnectStep` per stage; each step has fields
`stage` (string), `succeeded` (bool), `message` (string), and `latencyMs`
(int). The stage names used by the current runtime are:
`parse` → `resolve-capability` → `derive-shape` → `validate-credentials` →
`probe` → `discover-models` → `register-provider` → `store-credentials` →
`apply-assignments`.

```json
{
  "succeeded": true,
  "providerId": "openai-primary-20260419-113200",
  "summary": "Connected 'OpenAI (Primary)' in 184ms (4 model(s), 1 role(s))",
  "totalLatencyMs": 184,
  "steps": [
    { "stage": "resolve-capability", "succeeded": true, "message": "Matched 'OpenAI' module (openai)", "latencyMs": 1 },
    { "stage": "derive-shape",       "succeeded": true, "message": "Generated id 'openai-primary-...'",  "latencyMs": 0 },
    { "stage": "validate-credentials","succeeded": true, "message": "All required credential fields supplied", "latencyMs": 0 },
    { "stage": "discover-models",    "succeeded": true, "message": "Discovered 4 models",                "latencyMs": 142 },
    { "stage": "register-provider",  "succeeded": true, "message": "Provider registered in configuration", "latencyMs": 1 },
    { "stage": "store-credentials",  "succeeded": true, "message": "Credentials encrypted with DPAPI and stored", "latencyMs": 2 },
    { "stage": "apply-assignments",  "succeeded": true, "message": "Applied 1 role assignment(s)",       "latencyMs": 2 }
  ],
  "assignmentsApplied": ["Planner"],
  "assignmentsFailed": [],
  "discoveredModels": [
    { "id": "gpt-4o", "displayName": "GPT-4o", "selected": true }
  ]
}
```

On a failed request (e.g. unknown `kind` string), the runtime returns HTTP
400 with a structured body containing the same shape and a `parse` stage
whose `succeeded=false`.

See [Auto-Connect AI](Auto-Connect-AI) for the full pipeline walkthrough.

---

## CLU & governance

| Method | Route | Purpose |
| --- | --- | --- |
| `GET` | `/api/clu` | CLU posture, findings, and current governance state |
| `GET` | `/api/clu/tools` | Published governance tool descriptors |
| `GET` | `/api/clu/apple-operations` | Apple job queue + history |
| `POST` | `/api/clu/execute` | Execute a CLU governance operation |
| `POST` | `/api/clu/apple-operations/cancel` | Cancel a queued Apple operation |

All endpoints are backed by the `CommandLogicUnitModule` Forsetti service module 
(`com.mastercontrol.command-logic-unit`).

---

## Platform services

| Method | Route | Purpose |
| --- | --- | --- |
| `GET` | `/api/platform-services` | Combined gateway + governance + host inventory |
| `GET` | `/api/platform-services/gateways` | Platform gateway summary |
| `GET` | `/api/platform-services/governance` | Platform governance lane summary |
| `GET` | `/api/platform-services/apple-hosts` | Registered Apple remote hosts + readiness |
| `POST` | `/api/platform-services/apple-hosts` | Add or update an Apple host |
| `POST` | `/api/platform-services/apple-hosts/remove` | Remove an Apple host |
| `GET` | `/api/platform-services/config/{platform}` | Platform-specific client configuration |
| `GET` | `/mcp/gateway/{platform}` | Gateway document for `windows` / `macos` / `ios` |
| `GET` | `/mcp/governance/{platform}` | Governance document for the same set |
| `POST` | `/mcp/governance/{platform}` | Execute a platform governance tool call |

---

## Runtime inventory mutation

All mutating routes refresh inventory **asynchronously** via
`IRuntimeInventoryService::refreshAsync()` so admin calls return immediately
instead of blocking on the 35+ endpoint TCP probe loop.

| Method | Route | Purpose |
| --- | --- | --- |
| `POST` | `/api/runtime/mcp-servers` | Create or update a custom MCP server |
| `POST` | `/api/runtime/mcp-servers/remove` | Remove a custom MCP server |
| `POST` | `/api/runtime/subagents` | Create or update a sub-agent |
| `POST` | `/api/runtime/subagents/remove` | Remove a sub-agent |
| `POST` | `/api/providers/groups` | Create or update a provider group |
| `POST` | `/api/providers/groups/remove` | Remove a provider group |
| `POST` | `/api/providers/assignments` | Apply provider routing assignments |
| `POST` | `/api/providers/credentials` | Save provider credential material (DPAPI) |
| `POST` | `/api/providers/execute` | Execute a provider-backed request |

---

## Install & import

| Method | Route | Purpose |
| --- | --- | --- |
| `POST` | `/api/install/package` | Import or deploy a package artifact |
| `POST` | `/api/install/repo` | Import from a Git or bootstrap repository |
| `POST` | `/api/install/zip` | Import from a zip bundle |

---

## Operation result envelope

Every mutating route returns a uniform `OperationResult`:

```json
{
  "succeeded": true,
  "summary": "MCP server feature-test-mcp upserted",
  "errorMessage": "",
  "warnings": []
}
```

On failure, `succeeded` is `false`, `errorMessage` is populated, and the HTTP 
status reflects the failure category (400 / 404 / 409 / 500).

---

See also: [Architecture](Architecture) · [Auto-Connect AI](Auto-Connect-AI) · 
[Telemetry & Activity](Telemetry-and-Activity)
