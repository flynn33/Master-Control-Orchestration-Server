# Master Control Program Architecture

Master Control Program is composed of four primary surfaces: a Windows Service host, a WinUI 3
desktop shell, a browser-based real-time dashboard, and an MCP aggregator gateway. All
surfaces share the same service APIs and operate on the MASTER-CONTROL server.

## System Overview

```
                          +---------------------+
                          |   Remote Claude     |
                          |   Code Instances    |
                          +--------+------------+
                                   |
                            HTTPS :8443
                                   |
                          +--------v------------+
                          |   Caddy Reverse     |
                          |   Proxy (:8080/8443)|
                          +--+-----+--------+---+
                             |     |        |
              +--------------+  +--+---+  +-+---------+
              |                 |      |  |           |
    +---------v---+   +---------v-+  +-v--v------+  +v-----------+
    |  Dashboard  |   | Aggregator|  | 18 Blade  |  | 7 Sub-     |
    |  SPA :18000 |   | GW :7200  |  | Servers   |  | Agents     |
    +---------+---+   +-----+-----+  | :7101-18  |  | :7201-07   |
              |             |         +-----------+  +------------+
              +------+------+
                     |
              +------v------+
              |  Service    |
              |  Host       |
              +-------------+
```

## Browser Dashboard

Real-time monitoring dashboard for the entire BLADE MCP infrastructure. The dashboard
is a single 81KB HTML page served by a Node.js static server through Caddy.

| Property | Value |
| --- | --- |
| URL | `http://192.168.1.3:8080/dashboard/` |
| Source | `D:\mcp\dashboard\index.html` (81KB single-file SPA) |
| Config | `D:\mcp\dashboard\config.json` (25 backend endpoints) |
| Server | Node.js static server on port 18000 via `serve.ps1` |
| Stack | Pure HTML/CSS/JS, no build step, no dependencies |
| Layout | CSS Grid responsive, designed for 1920x1080 (works down to ~1200px) |

### Dashboard Sections

| Section | Data Source | Update Method |
| --- | --- | --- |
| System Metrics | `/api/metrics`, `/api/metrics/history` | SSE via `/api/metrics/stream` |
| MCP Server Grid | 18 blade server status endpoints | 5-second polling |
| Sub-Agent Grid | `/api/sub-agents` (WATCHTOWER aggregated) | 5-second polling |
| Agent Communication | `/api/agent-comm` | 5-second polling |
| Task Coordination | `/api/coordination` | 5-second polling |
| Event Bus | `/api/event-bus` | 5-second polling |
| Memory Beacon | `/api/memory-beacon` | 5-second polling |

The metrics charts use custom canvas rendering with a 60-point rolling window.
Real-time CPU, RAM, and network data streams over Server-Sent Events while all
other sections poll on a 5-second interval.

## Aggregator Gateway

A single MCP server on port 7200 that proxies every tool from all blade servers
and sub-agents. Remote Claude Code instances need only one connection to access
the full 96+ tool catalog.

| Property | Value |
| --- | --- |
| HTTPS endpoint | `https://192.168.1.3:8443/mcp/gateway` |
| HTTP endpoint | `http://192.168.1.3:8080/mcp/gateway` |
| Health check | `http://192.168.1.3:7200/health` |
| Built-in dashboard | `http://192.168.1.3:7200/dashboard` |
| Memory footprint | ~95MB RAM |

### How It Works

1. On startup, connects to all 25 backends and runs `tools/list` on each.
2. Builds a unified tool registry by merging all discovered tool schemas.
3. Routes incoming tool calls by name to the correct backend server.
4. Maintains persistent sessions with auto-reinit on expiry.
5. Refreshes every 5 minutes to pick up new or recovered backends.
6. Uses a low-level MCP Server class that passes JSON Schema verbatim.

### Key Files

| File | Purpose |
| --- | --- |
| `D:\Sub-Agents\aggregator\index.js` | Main server entry point |
| `D:\Sub-Agents\aggregator\discovery.js` | Backend discovery and SSE parser |
| `D:\Sub-Agents\aggregator\proxy.js` | Tool call routing with retry logic |
| `D:\mcp\config\Caddyfile` | Caddy routes for `/mcp/gateway*` |

## WinUI 3 Desktop Shell

Authored desktop application with Tron-inspired visuals for direct operator
control of the platform.

### Shell Panels

| Panel | Source File | Purpose |
| --- | --- | --- |
| Overview | `OverviewSectionControl.xaml` | System summary and status |
| Telemetry | `TelemetrySectionControl.xaml` | Live metrics and charts |
| Imports | `ImportsSectionControl.xaml` | MSI/EXE/PS1/Git bootstrap import flows |
| Exports | `ExportsSectionControl.xaml` | Configuration and data export |
| Providers | `ProvidersSectionControl.xaml` | Provider integration management |
| Runtime | `RuntimeSectionControl.xaml` | Runtime inventory and control |
| Security | `SecuritySectionControl.xaml` | Security configuration |
| Settings | `SettingsSectionControl.xaml` | Application settings |
| Command Logic | `CommandLogicUnitSectionControl.xaml` | Command execution interface |

## Repository Modules

| Module | Location | Purpose |
| --- | --- | --- |
| **MasterControlServiceHost** | `src/MasterControlServiceHost/` | Windows Service entry point that boots the runtime |
| **MasterControlShell** | `src/MasterControlShell/` | WinUI 3 desktop shell with all section panels |
| **MasterControlBootstrapper** | `src/MasterControlBootstrapper/` | Install, detection, and setup flows |
| **MasterControlApp** | `src/MasterControlApp/` | Shared runtime, defaults, and model implementations |
| **MasterControlModules** | `src/MasterControlModules/` | Forsetti-aligned module discovery and composition |

### Shared Headers (`include/MasterControl/`)

| Header | Purpose |
| --- | --- |
| `MasterControlContracts.h` | Interface contracts and abstract definitions |
| `MasterControlDefaults.h` | Default configuration values |
| `MasterControlModels.h` | Data models and structured types |
| `MasterControlModules.h` | Module discovery and registration |
| `MasterControlRuntime.h` | Runtime lifecycle and service management |

See also: [Infrastructure](Infrastructure) | [Sub-Agents](Sub-Agents) | [API Reference](API-Reference)
