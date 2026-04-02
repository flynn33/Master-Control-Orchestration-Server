# Master Control Orchestration Server API Reference

This page documents the current HTTP and MCP-style routes exposed by the shared runtime in `src/MasterControlApp/MasterControlRuntime.cpp`.

## Core Read Endpoints

| Route | Method | Purpose |
| --- | --- | --- |
| `/api/health` | `GET` | Service health and readiness snapshot |
| `/api/dashboard` | `GET` | Browser dashboard payload |
| `/api/config` | `GET` | Current persisted configuration |
| `/api/providers` | `GET` | Provider catalog, credentials posture, and assignments |
| `/api/exports` | `GET` | Export inventory and generated handoff artifacts |
| `/api/forsetti/surface` | `GET` | Current Forsetti surface model for shell/browser rendering |
| `/api/install/history` | `GET` | Install and import execution history |
| `/api/beacon` | `GET` | Beacon state and LAN-facing discovery posture |

## CLU And Governance

| Route | Method | Purpose |
| --- | --- | --- |
| `/api/clu` | `GET` | CLU posture, findings, and current governance state |
| `/api/clu/tools` | `GET` | Published governance tool descriptors |
| `/api/clu/apple-operations` | `GET` | Apple job queue/history snapshot |
| `/api/clu/execute` | `POST` | Execute a CLU governance operation |
| `/api/clu/apple-operations/cancel` | `POST` | Cancel a queued Apple operation |

## Platform Services

| Route | Method | Purpose |
| --- | --- | --- |
| `/api/platform-services` | `GET` | Combined gateway, governance, and host inventory |
| `/api/platform-services/gateways` | `GET` | Platform gateway summary |
| `/api/platform-services/governance` | `GET` | Platform governance lane summary |
| `/api/platform-services/apple-hosts` | `GET` | Registered Apple remote hosts and readiness data |
| `/api/platform-services/apple-hosts` | `POST` | Add or update an Apple host definition |
| `/api/platform-services/apple-hosts/remove` | `POST` | Remove an Apple host definition |
| `/api/platform-services/config/{platform}` | `GET` | Platform-specific client configuration payload |
| `/mcp/gateway/{platform}` | `GET` | Gateway document for `windows`, `macos`, or `ios` |
| `/mcp/governance/{platform}` | `GET` | Governance document for `windows`, `macos`, or `ios` |
| `/mcp/governance/{platform}` | `POST` | Execute a platform governance tool call |

## Runtime Inventory Mutation

| Route | Method | Purpose |
| --- | --- | --- |
| `/api/runtime/mcp-servers` | `POST` | Create or update a custom MCP server definition |
| `/api/runtime/mcp-servers/remove` | `POST` | Remove a custom MCP server definition |
| `/api/runtime/subagents` | `POST` | Create or update a sub-agent definition |
| `/api/runtime/subagents/remove` | `POST` | Remove a sub-agent definition |
| `/api/providers/groups` | `POST` | Create or update a provider group |
| `/api/providers/groups/remove` | `POST` | Remove a provider group |
| `/api/providers/assignments` | `POST` | Apply provider routing assignments |
| `/api/providers/credentials` | `POST` | Save provider credential material |
| `/api/providers/execute` | `POST` | Execute a provider-backed request through the runtime |

## Install And Import Routes

| Route | Method | Purpose |
| --- | --- | --- |
| `/api/install/package` | `POST` | Import or deploy a package artifact |
| `/api/install/repo` | `POST` | Import from a Git/bootstrap repository |
| `/api/install/zip` | `POST` | Import from a zip bundle |

## Notes

- The browser UI and WinUI shell both consume runtime-backed state rather than embedding separate business logic.
- Platform route keys are currently `windows`, `macos`, and `ios`.
- Legacy compatibility identifiers still exist internally for upgrades, but public-facing routes and docs use the Orchestration Server naming.

See also: [Architecture](Architecture) | [Operations](Operations) | [Remote Client](Remote-Client)
