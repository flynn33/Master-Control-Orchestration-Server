# API Reference

This page documents the current admin and operator HTTP route surface. It is
cross-checked against `include/MasterControl/AdminRouteRegistry.h`,
`include/MasterControl/AdminRouteAuthorization.h`,
`include/MasterControl/CapabilityAuthorization.h`,
`src/MasterControlApp/MasterControlRuntime.cpp`, and the local bridge server.

Base URL for the admin surface:

```text
http://<mcos-host>:7300
```

The MCP gateway itself listens on `mcpGateway.listenPort` and
`mcpGateway.mcpPath`:

```text
http://<mcos-host>:8080/mcp
```

## Identity And Capability Model

Requests may include:

```text
X-MCOS-Client-Id: <client id>
```

If the header identifies a registered enabled LAN client, mutating routes are
authorized against that client's capabilities. If no client header is present,
local operator fallback behavior applies as implemented by the runtime.
Disabled clients are rejected.

Mutating methods are:

```text
POST, PUT, PATCH, DELETE
```

The current route capability policy is:

| Capability | Routes |
|---|---|
| `governance.modify` | `/api/config`, `/api/governance/decisions`, `/api/client/governance/decisions`, `/api/clu/execute`, `/api/clu/apple-operations/cancel`, `/api/diagnostics/clear`, `/api/platform-services/apple-hosts`, `/api/platform-services/apple-hosts/remove`, `/mcp/governance/*` |
| `network.admin` | `/api/gateway/start`, `/api/gateway/stop`, `/api/gateway/restart` |
| `clients.manage` | `/api/clients`, `/api/clients/*` |
| `setup.local_or_admin` | setup state mutations, workflow-template instantiate routes, `/api/settings/advanced-mode`, `/api/self-tests/run` |
| `install.package` | setup dependency install routes, package/repo/zip import routes, `/api/forsetti/modules/state`, plugin toggle routes |
| `process.exec` | pool, lease, runtime catalog, workflow, MCP server, and sub-agent mutation routes |
| `supervisor.assign` | supervisor assignment/config/connect/heartbeat mutation routes |

`X-Confirm-Unsafe: 1` is required for unsafe configuration writes and for bridge
tools that explicitly mark destructive operations as confirmation-gated.

## Method Handling

Known routes return `405 Method Not Allowed` plus an `Allow` header when the
path exists but the method is unsupported. `HEAD` is allowed when `GET` is
allowed. `OPTIONS` is universally handled by the server preflight path.

## Exact Routes

| Method | Path | Mutates | Capability | Notes |
|---|---|---:|---|---|
| `GET` | `/api/health` | No | None | Basic health. |
| `GET` | `/api/version` | No | None | Runtime version. |
| `GET` | `/api/health/summary` | No | None | Aggregated health. |
| `GET` | `/api/host/telemetry` | No | None | Host telemetry. |
| `GET` | `/api/readiness` | No | None | Setup readiness snapshot. |
| `GET` | `/api/activity/health` | No | None | Activity health. |
| `GET` | `/.well-known/mcos.json` | No | None | Discovery document. |
| `GET` | `/api/discovery` | No | None | Discovery document. |
| `GET` | `/api/onboarding` | No | None | Onboarding index. |
| `GET` | `/api/beacon` | No | None | Beacon payload. |
| `GET` | `/api/environment-hints` | No | None | Environment hints. |
| `GET` | `/api/governance/profile` | No | None | Governance profile. |
| `GET` | `/api/governance/bundles` | No | None | Bundle index. |
| `GET` | `/api/governance/decisions` | No | None | Decision state. |
| `POST` | `/api/governance/decisions` | Yes | `governance.modify` | Governance decision submit. |
| `GET` | `/api/dashboard` | No | None | Dashboard snapshot. |
| `GET` | `/api/config` | No | None | Current configuration. |
| `POST` | `/api/config` | Yes | `governance.modify` | Full-document replacement; unsafe changes require `X-Confirm-Unsafe: 1`. |
| `PATCH` | `/api/config` | Yes | `governance.modify` | Partial deep merge; unsafe changes require `X-Confirm-Unsafe: 1`. |
| `GET` | `/api/exports` | No | None | Export inventory. |
| `GET` | `/api/workflows` | No | None | Workflow list. |
| `POST` | `/api/workflows` | Yes | `process.exec` | Workflow create/update. |
| `GET` | `/api/clu` | No | None | CLU snapshot. |
| `GET` | `/api/clu/tools` | No | None | CLU tools. |
| `GET` | `/api/clu/apple-operations` | No | None | Apple operation state. |
| `POST` | `/api/clu/apple-operations/cancel` | Yes | `governance.modify` | Cancel queued operation. |
| `GET` | `/api/clu/approvals` | No | None | Approval queue. |
| `POST` | `/api/clu/execute` | Yes | `governance.modify` | Execute governance tool. |
| `GET` | `/api/forsetti/surface` | No | None | Forsetti UI surface. |
| `GET` | `/api/forsetti/modules` | No | None | Module catalog. |
| `POST` | `/api/forsetti/modules/state` | Yes | `install.package` | Enable, disable, or remove a module. |
| `GET` | `/api/pools` | No | None | Managed pool list. |
| `POST` | `/api/pools` | Yes | `process.exec` | Upsert a pool. |
| `GET` | `/api/telemetry/clients` | No | None | Client telemetry. |
| `GET` | `/api/telemetry/gateway` | No | None | Gateway telemetry. |
| `POST` | `/api/telemetry/heartbeat` | Yes | None | Client heartbeat. |
| `GET` | `/api/gateway/status` | No | None | Gateway status. |
| `GET` | `/api/gateway/health` | No | None | Gateway health. |
| `GET` | `/api/gateway/tools` | No | None | Aggregated tool catalog. |
| `POST` | `/api/gateway/start` | Yes | `network.admin` | Start gateway. |
| `POST` | `/api/gateway/stop` | Yes | `network.admin` | Stop gateway. |
| `POST` | `/api/gateway/restart` | Yes | `network.admin` | Restart gateway. |
| `GET` | `/api/client/mcp-servers` | No | None | Client catalog. |
| `GET` | `/api/client/sub-agents` | No | None | Client sub-agent catalog. |
| `GET` | `/api/client/activity` | No | None | Client activity. |
| `GET` | `/api/client/governance/profile` | No | None | Client governance profile. |
| `POST` | `/api/client/governance/decisions` | Yes | `governance.modify` | Client decision submit. |
| `POST` | `/api/client/heartbeat` | Yes | None | Client heartbeat. |
| `GET` | `/api/clients` | No | None | LAN client roster. |
| `POST` | `/api/clients` | Yes | `clients.manage` | Register/update a client. |
| `GET` | `/api/setup/state` | No | None | Setup state. |
| `POST` | `/api/setup/start` | Yes | `setup.local_or_admin` | Start setup. |
| `POST` | `/api/setup/complete` | Yes | `setup.local_or_admin` | Complete setup. |
| `POST` | `/api/setup/dismiss` | Yes | `setup.local_or_admin` | Dismiss setup. |
| `POST` | `/api/setup/reset` | Yes | `setup.local_or_admin` | Reset setup. |
| `GET` | `/api/setup/dependencies` | No | None | Dependency state. |
| `GET` | `/api/setup/workflow-templates` | No | None | Workflow templates. |
| `GET` | `/api/install/history` | No | None | Install/import history. |
| `POST` | `/api/install/package` | Yes | `install.package` | Install/import package. |
| `POST` | `/api/install/repo` | Yes | `install.package` | Import repository. |
| `POST` | `/api/install/zip` | Yes | `install.package` | Import zip. |
| `POST` | `/api/settings/advanced-mode` | Yes | `setup.local_or_admin` | Toggle advanced mode. |
| `POST` | `/api/runtime/mcp-servers` | Yes | `process.exec` | Runtime MCP catalog mutation. |
| `POST` | `/api/runtime/mcp-servers/remove` | Yes | `process.exec` | Runtime MCP catalog removal. |
| `POST` | `/api/runtime/subagents` | Yes | `process.exec` | Runtime sub-agent catalog mutation. |
| `POST` | `/api/runtime/subagents/remove` | Yes | `process.exec` | Runtime sub-agent removal. |
| `POST` | `/api/runtime/subagent-groups` | Yes | `process.exec` | Runtime group mutation. |
| `POST` | `/api/runtime/subagent-groups/remove` | Yes | `process.exec` | Runtime group removal. |
| `GET` | `/api/platform-services` | No | None | Platform service summary. |
| `GET` | `/api/platform-services/gateways` | No | None | Platform gateways. |
| `GET` | `/api/platform-services/governance` | No | None | Platform governance. |
| `GET` | `/api/platform-services/apple-hosts` | No | None | Apple hosts. |
| `POST` | `/api/platform-services/apple-hosts` | Yes | `governance.modify` | Add/update Apple host. |
| `POST` | `/api/platform-services/apple-hosts/remove` | Yes | `governance.modify` | Remove Apple host. |
| `GET` | `/api/claude-plugin/status` | No | None | Plugin status. |
| `POST` | `/api/claude-plugin/toggle` | Yes | `install.package` | Toggle plugin. |
| `GET` | `/api/chatgpt-plugin/status` | No | None | Plugin status. |
| `POST` | `/api/chatgpt-plugin/toggle` | Yes | `install.package` | Toggle plugin. |
| `GET` | `/api/grok-plugin/status` | No | None | Plugin status. |
| `POST` | `/api/grok-plugin/toggle` | Yes | `install.package` | Toggle plugin. |
| `GET` | `/api/diagnostics/runtime-stats` | No | None | Runtime stats. |
| `GET` | `/api/diagnostics/events` | No | None | Diagnostics query. |
| `GET` | `/api/diagnostics/summary` | No | None | Diagnostics summary. |
| `GET` | `/api/diagnostics/self-test` | No | None | Self-test diagnostics. |
| `GET` | `/api/diagnostics/export` | No | None | Markdown or JSON export. |
| `POST` | `/api/diagnostics/clear` | Yes | `governance.modify` | Destructive clear; bridge requires confirmation. |
| `GET` | `/api/self-tests` | No | None | Boot self-test snapshot. |
| `POST` | `/api/self-tests/run` | Yes | `setup.local_or_admin` | Re-run self-tests. |
| `GET` | `/api/events` | No | None | Admin SSE activity stream. |
| `GET` | `/api/governance/agentic-edition/manifest` | No | None | Vendored governance manifest. |
| `GET` | `/api/supervisor/assignment` | No | None | Supervisor assignment. |
| `POST` | `/api/supervisor/assignment/select` | Yes | `supervisor.assign` | Select supervisor. |
| `POST` | `/api/supervisor/assignment/revoke` | Yes | `supervisor.assign` | Revoke supervisor. |
| `POST` | `/api/supervisor/config/generate` | Yes | `supervisor.assign` | Generate config. |
| `POST` | `/api/supervisor/connect/confirm` | Yes | `supervisor.assign` | Confirm connection. |
| `POST` | `/api/supervisor/heartbeat` | Yes | `supervisor.assign` | Supervisor heartbeat. |
| `GET` | `/api/supervisor/status` | No | None | Supervisor status. |
| `GET` | `/api/supervisor/reachability-check` | No | None | Reachability self-check. |

## Pattern Routes

| Method | Pattern | Mutates | Capability | Notes |
|---|---|---:|---|---|
| `POST` | `/api/setup/workflow-templates/{templateId}/instantiate` | Yes | `setup.local_or_admin` | Instantiate setup workflow template. |
| `GET` | `/api/clients/{clientId}` | No | None | Single client lookup. |
| `DELETE` | `/api/clients/{clientId}` | Yes | `clients.manage` | Remove client. |
| `GET` | `/api/clients/{clientId}/config` | No | None | Client config bundle. |
| `POST` | `/api/clients/{clientId}/disable` | Yes | `clients.manage` | Disable client. |
| `POST` | `/api/clients/{clientId}/enable` | Yes | `clients.manage` | Enable client. |
| `POST` | `/api/clients/{clientId}/privileges` | Yes | `clients.manage` | Replace privileges. |
| `POST` | `/api/clients/{clientId}/autonomous-mode` | Yes | `clients.manage` | Toggle autonomous mode. |
| `POST` | `/api/clu/approvals/{approvalId}/approve` | Yes | `governance.modify` | Approve deferred action; bridge requires confirmation. |
| `POST` | `/api/clu/approvals/{approvalId}/reject` | Yes | `governance.modify` | Reject deferred action; bridge requires confirmation. |

## Prefix Routes

| Method | Prefix | Mutates | Capability | Notes |
|---|---|---:|---|---|
| `GET` | `/api/onboarding/` | No | None | Per-client onboarding profile. |
| `GET` | `/api/governance/bundles/` | No | None | Platform governance bundle. |
| `POST` | `/api/setup/dependencies/` | Yes | `install.package` | Dependency install action. |
| `GET` | `/api/platform-services/config/` | No | None | Platform-specific config. |
| `POST` | `/api/leases/` | Yes | `process.exec` | Lease operations such as release. |
| `GET` | `/api/pools/` | No | None | Pool detail/status routes. |
| `POST` | `/api/pools/` | Yes | `process.exec` | Pool control routes. |
| `GET` | `/api/telemetry/events` | No | None | Telemetry event query. |
| `GET` | `/api/activity` | No | None | Activity query. |
| `GET` | `/api/workflows/` | No | None | Workflow detail. |
| `POST` | `/api/workflows/` | Yes | `process.exec` | Workflow mutation. |
| `DELETE` | `/api/workflows/` | Yes | `process.exec` | Workflow deletion. |
| `GET` | `/mcp/gateway/` | No | None | Platform gateway document. |
| `GET` | `/mcp/governance/` | No | None | Platform governance document. |
| `POST` | `/mcp/governance/` | Yes | `governance.modify` | Platform governance tool call. |
| `GET` | `/api/governance/agentic-edition/document/` | No | None | Vendored governance document by path. |

## Key Request Bodies

### `PATCH /api/config`

```json
{
  "mcpGateway": {
    "enabled": true,
    "listenHost": "0.0.0.0"
  }
}
```

Deep-merge object fields. Use `X-Confirm-Unsafe: 1` for unsafe changes.

### `POST /api/config`

Send a complete `AppConfiguration` document. Partial top-level documents are
rejected so omitted fields are not reset to defaults.

### `POST /api/forsetti/modules/state`

```json
{
  "moduleId": "com.mastercontrol.example",
  "action": "enable"
}
```

Valid actions are `enable`, `disable`, and `remove`.

### `POST /api/diagnostics/clear`

```json
{
  "confirm": true,
  "reason": "operator maintenance window",
  "retainSinceUtc": "2026-07-04T00:00:00Z"
}
```

The bridge requires explicit confirmation for this destructive operation.

## Status Codes

| Code | Meaning |
|---|---|
| `200` | Read or mutation succeeded. |
| `202` | Action accepted or deferred for approval. |
| `204` | Notification or empty success where applicable. |
| `400` | Invalid JSON, bad content length, or validation error. |
| `403` | Disabled client, missing capability, or blocked governance action. |
| `404` | Unknown route or resource. |
| `405` | Known route with unsupported method; `Allow` header is included. |
| `413` | Request body too large where enforced. |
| `500` | Runtime exception or operation failure. |
| `503` | Gateway queue saturation or unavailable backend where applicable. |

## Related Pages

[Configuration](Configuration) |
[Gateway](Gateway) |
[Worker Pools](Worker-Pools) |
[LAN Clients](LAN-Clients) |
[Privileges](Privileges) |
[Governance](Governance) |
[Troubleshooting](Troubleshooting)
