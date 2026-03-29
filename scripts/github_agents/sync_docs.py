from __future__ import annotations

import argparse
import os
import re
import shutil
import tempfile
from pathlib import Path

from common import (
    AGENT_COMMIT_PREFIX,
    PROJECT_NAME,
    REPOSITORY_NAME,
    REPOSITORY_URL,
    ROOT,
    git_output,
    read_json,
    run_git,
    write_text,
)


VERSION_FILE = ROOT / "VERSION.json"
README_FILE = ROOT / "README.md"
WIKI_DIR = ROOT / "docs" / "wiki"
VERSIONS_DIR = ROOT / "docs" / "versions"
PLANS_DIR = ROOT / "plans"
LIST_PREFIX = re.compile(r"^(?:[-*+]\s+|\d+\.\s+)")


# ---------------------------------------------------------------------------
# State helpers
# ---------------------------------------------------------------------------

def load_state() -> dict:
    return read_json(
        VERSION_FILE,
        {
            "current_version": "0.1.0",
            "current_tag": "v0.1.0",
            "released_at": "",
            "history": [],
        },
    )


def latest_release(state: dict) -> dict:
    history = state.get("history", [])
    if history:
        return history[0]
    return {
        "version": state.get("current_version", "0.1.0"),
        "tag": state.get("current_tag", "v0.1.0"),
        "released_at": state.get("released_at", ""),
        "summary": "Version tracking has been initialized.",
        "entries": [],
    }


# ---------------------------------------------------------------------------
# Content helpers
# ---------------------------------------------------------------------------

def read_excerpt(path: Path, max_lines: int = 12) -> list[str]:
    """Read short excerpt as flat bullet points (used for README summaries)."""
    if not path.exists():
        return []

    lines: list[str] = []
    for line in path.read_text(encoding="utf-8").splitlines():
        stripped = line.strip()
        if not stripped:
            continue
        if stripped.startswith("#") or stripped.startswith("```"):
            continue
        normalized = stripped
        while True:
            updated = LIST_PREFIX.sub("", normalized, count=1).strip()
            if updated == normalized:
                break
            normalized = updated
        lines.append(normalized)
        if len(lines) >= max_lines:
            break
    return lines


def read_full_content(path: Path) -> str:
    """Read full file content, stripping only the first H1 heading line."""
    if not path.exists():
        return ""
    text = path.read_text(encoding="utf-8")
    lines = text.splitlines()
    # Strip the leading H1 and any immediately following blank line
    if lines and lines[0].startswith("# "):
        lines = lines[1:]
        if lines and not lines[0].strip():
            lines = lines[1:]
    return "\n".join(lines)


def read_sections(path: Path) -> dict[str, str]:
    """Parse a markdown file into {heading_text: content_below} sections."""
    if not path.exists():
        return {}
    text = path.read_text(encoding="utf-8")
    sections: dict[str, str] = {}
    current_heading = ""
    current_lines: list[str] = []

    for line in text.splitlines():
        stripped = line.strip()
        if stripped.startswith("## ") or stripped.startswith("### "):
            if current_heading:
                sections[current_heading] = "\n".join(current_lines).strip()
            current_heading = stripped.lstrip("#").strip()
            current_lines = []
        else:
            current_lines.append(line)

    if current_heading:
        sections[current_heading] = "\n".join(current_lines).strip()
    return sections


# ---------------------------------------------------------------------------
# README (kept similar to original — this is the repo landing page)
# ---------------------------------------------------------------------------

def render_readme(state: dict) -> str:
    release = latest_release(state)
    project_overview = read_excerpt(PLANS_DIR / "dashboard" / "project-overview.md", 8)
    technical_details = read_excerpt(PLANS_DIR / "dashboard" / "technical-details.md", 8)
    lines = [
        f"# {REPOSITORY_NAME}",
        "",
        f"{PROJECT_NAME} is a Forsetti-compliant Windows control plane for MCP servers, sub-agents, host telemetry, and browser-based operations.",
        "",
        "## Current Release",
        "",
        f"- Version: `{release['tag']}`",
        f"- Release date: `{release['released_at']}`",
        f"- Summary: {release['summary']}",
        "",
        "## Highlights",
        "",
        "- Windows Service host for configuration, telemetry, imports, provider integration, and LAN beaconing.",
        "- Authored WinUI 3 desktop shell with Tron-inspired visuals and a browser dashboard backed by the same service APIs.",
        "- Import and install paths for MSI, EXE, PS1, Git bootstrap repositories, and manifest-driven zip bundles.",
        "- Forsetti-aligned modules for environment discovery, runtime inventory, configuration, export generation, and gateway control.",
        "",
        "## Repository Layout",
        "",
        "- `src/` - application hosts and shared runtime implementation.",
        "- `include/` - shared contracts, models, and defaults.",
        "- `resources/` - web assets and Forsetti manifests.",
        "- `plans/` - current architecture and infrastructure notes.",
        "- `docs/wiki/` - wiki source pages maintained by repository automation.",
        "- `docs/versions/` - generated release documentation.",
        "",
        "## Build And Validate",
        "",
        "```powershell",
        "cmake --build build\\debug --config Debug",
        "ctest --test-dir build\\debug -C Debug --output-on-failure",
        "cmake --install build\\debug --config Debug --prefix dist\\debug",
        "```",
        "",
        "## GitHub Agents",
        "",
        "| Agent | Responsibility | Trigger |",
        "| --- | --- | --- |",
        "| Changelog Agent | Updates `CHANGELOG.md` after pushes to `main`. | `push`, `workflow_dispatch` |",
        "| Wiki + README Agent | Regenerates `README.md`, wiki source pages, and syncs the GitHub wiki. | `push`, `workflow_dispatch` |",
        "| AI Contributor Guard | Rejects commits that declare AI contributors and can block pushes locally. | `pre-push`, `push`, `pull_request` |",
        "| Version Agent | Tracks semantic versions and creates the next numbered release. | `push`, `workflow_dispatch` |",
        "| Version Documentation Agent | Generates release pages in `docs/versions/` and publishes GitHub releases. | `push`, `workflow_dispatch` |",
        "",
        "## Key Project Notes",
        "",
        "### Dashboard Overview",
    ]
    lines.extend(f"- {item}" for item in project_overview)
    lines.extend(["", "### Technical Snapshot"])
    lines.extend(f"- {item}" for item in technical_details)
    lines.extend(
        [
            "",
            "## Primary Documents",
            "",
            "- [Project Overview](plans/dashboard/project-overview.md)",
            "- [Technical Details](plans/dashboard/technical-details.md)",
            "- [Aggregator Gateway Plan](plans/infrastructure/mcp-aggregator-gateway-deployment.md)",
            "- [Version Index](docs/versions/index.md)",
            "- [Latest Release Notes](docs/versions/latest.md)",
            "",
            f"Repository URL: {REPOSITORY_URL}",
            "",
        ]
    )
    return "\n".join(lines)


# ---------------------------------------------------------------------------
# Wiki: Home
# ---------------------------------------------------------------------------

def render_home(state: dict) -> str:
    release = latest_release(state)
    return "\n".join(
        [
            f"# {PROJECT_NAME} Wiki",
            "",
            f"{PROJECT_NAME} is a Forsetti-compliant Windows control plane that manages MCP servers,",
            "AI sub-agents, host telemetry, and browser-based operations from a single platform.",
            f"The system runs on the MASTER-CONTROL server (`192.168.1.3`) and exposes 96+ tools",
            "through an aggregator gateway that any Claude Code instance on the LAN can reach with",
            "one connection.",
            "",
            "## Current Release",
            "",
            f"| Field | Value |",
            f"| --- | --- |",
            f"| Version | `{release['tag']}` |",
            f"| Released | `{release['released_at']}` |",
            f"| Summary | {release['summary']} |",
            "",
            "## Platform at a Glance",
            "",
            "| Component | Count | Details |",
            "| --- | --- | --- |",
            "| Blade tool servers | 18 | Ports 7101-7118, specialized MCP tool providers |",
            "| AI sub-agents | 7 | Ports 7201-7207, autonomous AI workers |",
            "| Aggregator gateway | 1 | Port 7200, single-endpoint proxy for all tools |",
            "| Exposed tools | 96+ | Accessible via HTTPS at `192.168.1.3:8443` |",
            "| Dashboard sections | 7 | Real-time metrics, server grid, agent grid, comms, coordination, events, beacon |",
            "",
            "## Wiki Pages",
            "",
            "| Page | Description |",
            "| --- | --- |",
            "| [Architecture](Architecture) | System design, dashboard layout, aggregator gateway, and repository module map |",
            "| [Infrastructure](Infrastructure) | Server hardware, port allocation, Caddy proxy, network topology, and storage layout |",
            "| [Sub-Agents](Sub-Agents) | The 7 AI sub-agents, their roles, ports, capabilities, and implementation details |",
            "| [API Reference](API-Reference) | All 25 backend services, API endpoints, metrics system, and health checks |",
            "| [Operations](Operations) | Build, test, install commands, service management, dashboard access, and push guard |",
            "| [Remote Client](Remote-Client) | How to connect a remote Claude Code instance to the BLADE gateway |",
            "| [Automation](Automation) | GitHub agents, CI/CD pipeline, commit conventions, and workflow triggers |",
            "| [Versions](Versions) | Release history, versioning scheme, and release documents |",
            "",
            "## Quick Links",
            "",
            "- Dashboard: `http://192.168.1.3:8080/dashboard/`",
            "- Gateway health: `http://192.168.1.3:7200/health`",
            "- HTTPS gateway: `https://192.168.1.3:8443/mcp/gateway`",
            f"- Repository: {REPOSITORY_URL}",
            "",
        ]
    )


# ---------------------------------------------------------------------------
# Wiki: Architecture
# ---------------------------------------------------------------------------

def render_architecture() -> str:
    return "\n".join(
        [
            f"# {PROJECT_NAME} Architecture",
            "",
            f"{PROJECT_NAME} is composed of four primary surfaces: a Windows Service host, a WinUI 3",
            "desktop shell, a browser-based real-time dashboard, and an MCP aggregator gateway. All",
            "surfaces share the same service APIs and operate on the MASTER-CONTROL server.",
            "",
            "## System Overview",
            "",
            "```",
            "                          +---------------------+",
            "                          |   Remote Claude     |",
            "                          |   Code Instances    |",
            "                          +--------+------------+",
            "                                   |",
            "                            HTTPS :8443",
            "                                   |",
            "                          +--------v------------+",
            "                          |   Caddy Reverse     |",
            "                          |   Proxy (:8080/8443)|",
            "                          +--+-----+--------+---+",
            "                             |     |        |",
            "              +--------------+  +--+---+  +-+---------+",
            "              |                 |      |  |           |",
            "    +---------v---+   +---------v-+  +-v--v------+  +v-----------+",
            "    |  Dashboard  |   | Aggregator|  | 18 Blade  |  | 7 Sub-     |",
            "    |  SPA :18000 |   | GW :7200  |  | Servers   |  | Agents     |",
            "    +---------+---+   +-----+-----+  | :7101-18  |  | :7201-07   |",
            "              |             |         +-----------+  +------------+",
            "              +------+------+",
            "                     |",
            "              +------v------+",
            "              |  Service    |",
            "              |  Host       |",
            "              +-------------+",
            "```",
            "",
            "## Browser Dashboard",
            "",
            "Real-time monitoring dashboard for the entire BLADE MCP infrastructure. The dashboard",
            "is a single 81KB HTML page served by a Node.js static server through Caddy.",
            "",
            "| Property | Value |",
            "| --- | --- |",
            "| URL | `http://192.168.1.3:8080/dashboard/` |",
            "| Source | `D:\\mcp\\dashboard\\index.html` (81KB single-file SPA) |",
            "| Config | `D:\\mcp\\dashboard\\config.json` (25 backend endpoints) |",
            "| Server | Node.js static server on port 18000 via `serve.ps1` |",
            "| Stack | Pure HTML/CSS/JS, no build step, no dependencies |",
            "| Layout | CSS Grid responsive, designed for 1920x1080 (works down to ~1200px) |",
            "",
            "### Dashboard Sections",
            "",
            "| Section | Data Source | Update Method |",
            "| --- | --- | --- |",
            "| System Metrics | `/api/metrics`, `/api/metrics/history` | SSE via `/api/metrics/stream` |",
            "| MCP Server Grid | 18 blade server status endpoints | 5-second polling |",
            "| Sub-Agent Grid | `/api/sub-agents` (WATCHTOWER aggregated) | 5-second polling |",
            "| Agent Communication | `/api/agent-comm` | 5-second polling |",
            "| Task Coordination | `/api/coordination` | 5-second polling |",
            "| Event Bus | `/api/event-bus` | 5-second polling |",
            "| Memory Beacon | `/api/memory-beacon` | 5-second polling |",
            "",
            "The metrics charts use custom canvas rendering with a 60-point rolling window.",
            "Real-time CPU, RAM, and network data streams over Server-Sent Events while all",
            "other sections poll on a 5-second interval.",
            "",
            "## Aggregator Gateway",
            "",
            "A single MCP server on port 7200 that proxies every tool from all blade servers",
            "and sub-agents. Remote Claude Code instances need only one connection to access",
            "the full 96+ tool catalog.",
            "",
            "| Property | Value |",
            "| --- | --- |",
            "| HTTPS endpoint | `https://192.168.1.3:8443/mcp/gateway` |",
            "| HTTP endpoint | `http://192.168.1.3:8080/mcp/gateway` |",
            "| Health check | `http://192.168.1.3:7200/health` |",
            "| Built-in dashboard | `http://192.168.1.3:7200/dashboard` |",
            "| Memory footprint | ~95MB RAM |",
            "",
            "### How It Works",
            "",
            "1. On startup, connects to all 25 backends and runs `tools/list` on each.",
            "2. Builds a unified tool registry by merging all discovered tool schemas.",
            "3. Routes incoming tool calls by name to the correct backend server.",
            "4. Maintains persistent sessions with auto-reinit on expiry.",
            "5. Refreshes every 5 minutes to pick up new or recovered backends.",
            "6. Uses a low-level MCP Server class that passes JSON Schema verbatim.",
            "",
            "### Key Files",
            "",
            "| File | Purpose |",
            "| --- | --- |",
            "| `D:\\Sub-Agents\\aggregator\\index.js` | Main server entry point |",
            "| `D:\\Sub-Agents\\aggregator\\discovery.js` | Backend discovery and SSE parser |",
            "| `D:\\Sub-Agents\\aggregator\\proxy.js` | Tool call routing with retry logic |",
            "| `D:\\mcp\\config\\Caddyfile` | Caddy routes for `/mcp/gateway*` |",
            "",
            "## WinUI 3 Desktop Shell",
            "",
            "Authored desktop application with Tron-inspired visuals for direct operator",
            "control of the platform.",
            "",
            "### Shell Panels",
            "",
            "| Panel | Source File | Purpose |",
            "| --- | --- | --- |",
            "| Overview | `OverviewSectionControl.xaml` | System summary and status |",
            "| Telemetry | `TelemetrySectionControl.xaml` | Live metrics and charts |",
            "| Imports | `ImportsSectionControl.xaml` | MSI/EXE/PS1/Git bootstrap import flows |",
            "| Exports | `ExportsSectionControl.xaml` | Configuration and data export |",
            "| Providers | `ProvidersSectionControl.xaml` | Provider integration management |",
            "| Runtime | `RuntimeSectionControl.xaml` | Runtime inventory and control |",
            "| Security | `SecuritySectionControl.xaml` | Security configuration |",
            "| Settings | `SettingsSectionControl.xaml` | Application settings |",
            "| Command Logic | `CommandLogicUnitSectionControl.xaml` | Command execution interface |",
            "",
            "## Repository Modules",
            "",
            "| Module | Location | Purpose |",
            "| --- | --- | --- |",
            "| **MasterControlServiceHost** | `src/MasterControlServiceHost/` | Windows Service entry point that boots the runtime |",
            "| **MasterControlShell** | `src/MasterControlShell/` | WinUI 3 desktop shell with all section panels |",
            "| **MasterControlBootstrapper** | `src/MasterControlBootstrapper/` | Install, detection, and setup flows |",
            "| **MasterControlApp** | `src/MasterControlApp/` | Shared runtime, defaults, and model implementations |",
            "| **MasterControlModules** | `src/MasterControlModules/` | Forsetti-aligned module discovery and composition |",
            "",
            "### Shared Headers (`include/MasterControl/`)",
            "",
            "| Header | Purpose |",
            "| --- | --- |",
            "| `MasterControlContracts.h` | Interface contracts and abstract definitions |",
            "| `MasterControlDefaults.h` | Default configuration values |",
            "| `MasterControlModels.h` | Data models and structured types |",
            "| `MasterControlModules.h` | Module discovery and registration |",
            "| `MasterControlRuntime.h` | Runtime lifecycle and service management |",
            "",
            "See also: [Infrastructure](Infrastructure) | [Sub-Agents](Sub-Agents) | [API Reference](API-Reference)",
            "",
        ]
    )


# ---------------------------------------------------------------------------
# Wiki: Infrastructure
# ---------------------------------------------------------------------------

def render_infrastructure() -> str:
    return "\n".join(
        [
            f"# {PROJECT_NAME} Infrastructure",
            "",
            "The entire BLADE MCP infrastructure runs on a single physical server with",
            "Caddy as the reverse proxy, 26 backend services, and a self-signed TLS",
            "certificate for remote access.",
            "",
            "## Server Hardware",
            "",
            "| Property | Value |",
            "| --- | --- |",
            "| Hostname | MASTER-CONTROL |",
            "| IP Address | `192.168.1.3` |",
            "| OS | Windows Server 2022 Datacenter |",
            "| CPU | 2x Intel Xeon E5-2640 (12 cores / 24 threads) |",
            "| RAM | 64 GB |",
            "| Storage | 280 GB (C:) system + 10 TB (D:) data |",
            "",
            "## Network Topology",
            "",
            "```",
            "  Remote Clients (LAN)",
            "        |",
            "   +----v-----------------------------------------+",
            "   |  Caddy Reverse Proxy                         |",
            "   |  :8080 (HTTP)  :8443 (HTTPS, self-signed)    |",
            "   +----+---------+---------+----------+----------+",
            "        |         |         |          |",
            "   /dashboard  /mcp/gw  /api/*    /mcp/blade/*",
            "        |         |         |          |",
            "   Node :18000  GW :7200  Metrics  Individual",
            "                  |       :7120-21  blade routes",
            "                  |                 :7101-7118",
            "                  |",
            "            +-----+------+",
            "            |            |",
            "       Blades        Sub-Agents",
            "       :7101-18      :7201-07",
            "```",
            "",
            "## Port Allocation",
            "",
            "### Blade Tool Servers (7101-7118)",
            "",
            "| Port | Server | Purpose |",
            "| --- | --- | --- |",
            "| 7101 | repo-search | Repository code search |",
            "| 7102 | docs-search | Documentation corpus search |",
            "| 7103 | fs-cache | Filesystem cache |",
            "| 7104 | build-cache | Build artifact cache |",
            "| 7105 | symbol-index | Symbol and definition indexing |",
            "| 7106 | session-context | Session context persistence |",
            "| 7107 | response-cache | Response caching |",
            "| 7108 | git-intel | Git history and intelligence |",
            "| 7109 | file-digest | File hashing and digest |",
            "| 7110 | vector-search | Vector embedding search |",
            "| 7111 | dep-graph | Dependency graph analysis |",
            "| 7112 | lint-cache | Lint result caching |",
            "| 7113 | snippet-store | Reusable code snippet storage |",
            "| 7114 | task-queue | Task queue management |",
            "| 7115 | memory | Shared key-value memory (namespaced) |",
            "| 7116 | agent-comm | Agent-to-agent communication |",
            "| 7117 | coordination | Task coordination |",
            "| 7118 | event-bus | Event stream bus |",
            "",
            "### Sub-Agents (7201-7207)",
            "",
            "| Port | Agent | Role |",
            "| --- | --- | --- |",
            "| 7201 | SENTINEL | Validation and guardrails |",
            "| 7202 | ARCHITECT | Analysis and design review |",
            "| 7203 | FORGE | Build execution and pipelines |",
            "| 7204 | SCRIBE | Documentation and knowledge |",
            "| 7205 | RECON | Code review and security |",
            "| 7206 | NEXUS | Workflow orchestration |",
            "| 7207 | WATCHTOWER | Health monitoring and alerts |",
            "",
            "See [Sub-Agents](Sub-Agents) for detailed descriptions.",
            "",
            "### Infrastructure Services",
            "",
            "| Port | Service | Purpose |",
            "| --- | --- | --- |",
            "| 7200 | Aggregator Gateway | Single-endpoint proxy for all 96+ tools |",
            "| 7120 | Client Tracker | Connected client tracking and LAN broadcast |",
            "| 7121 | System Metrics | CPU, RAM, network telemetry |",
            "| 18000 | Dashboard Server | Node.js static server for the browser dashboard |",
            "| 8080 | Caddy HTTP | HTTP reverse proxy |",
            "| 8443 | Caddy HTTPS | HTTPS reverse proxy (self-signed cert) |",
            "",
            "## Caddy Reverse Proxy",
            "",
            "Caddy handles all external routing with two listeners:",
            "",
            "- **Port 8080 (HTTP)**: Routes to individual blade servers, the dashboard,",
            "  metrics, sub-agents, and the aggregator gateway via path-based routing.",
            "- **Port 8443 (HTTPS)**: Self-signed certificate (`CN=master-control`,",
            "  IP SAN `192.168.1.3`, thumbprint `9A2DC81D`). Primarily used for the",
            "  aggregator gateway endpoint at `/mcp/gateway`.",
            "",
            "The Caddyfile lives at `D:\\mcp\\config\\Caddyfile`.",
            "",
            "## Data Storage",
            "",
            "All persistent data is stored under `D:\\mcp\\data\\`:",
            "",
            "| Directory | Content |",
            "| --- | --- |",
            "| `memory/` | Shared key-value memory (namespaced per client) |",
            "| `sessions/` | Session context persistence |",
            "| `repos/` | Mirrored git repositories |",
            "| `docs/` | Documentation corpus |",
            "| `indexes/` | Symbol and vector indexes |",
            "| `snippets/` | Reusable code patterns |",
            "| `build-cache/` | Build artifacts |",
            "| `lint-cache/` | Lint results |",
            "| `response-cache/` | Response cache |",
            "",
            "See also: [Architecture](Architecture) | [Sub-Agents](Sub-Agents) | [Remote Client](Remote-Client)",
            "",
        ]
    )


# ---------------------------------------------------------------------------
# Wiki: Sub-Agents
# ---------------------------------------------------------------------------

def render_sub_agents() -> str:
    return "\n".join(
        [
            f"# {PROJECT_NAME} Sub-Agents",
            "",
            "Seven AI sub-agents run on ports 7201 through 7207, each with a specialized",
            "role in the development and operations lifecycle. All agents share a common",
            "MCP communication layer and are aggregated into the single gateway endpoint.",
            "",
            "## Agent Roster",
            "",
            "| Agent | Port | Role | Key Capabilities |",
            "| --- | --- | --- | --- |",
            "| **SENTINEL** | 7201 | Validation & Guardrails | Design validation, import checking, dependency validation, enforcement rules |",
            "| **ARCHITECT** | 7202 | Analysis & Design | Project analysis, design review, dependency checks, pattern suggestions |",
            "| **FORGE** | 7203 | Build & Execution | Build execution, test running, pipeline management, file watching |",
            "| **SCRIBE** | 7204 | Documentation & Knowledge | API documentation, code explanation, knowledge search, docs updates |",
            "| **RECON** | 7205 | Review & Security | Diff review, file analysis, pattern finding, security scans, quality reports |",
            "| **NEXUS** | 7206 | Orchestration | Workflow orchestration, task management, agent roster, request aggregation |",
            "| **WATCHTOWER** | 7207 | Monitoring & Alerts | Health monitoring, agent metrics, alert history, restart capability |",
            "",
            "## Agent Details",
            "",
            "### SENTINEL (Port 7201)",
            "",
            "The validation and guardrails agent. SENTINEL enforces design constraints,",
            "validates imports against allowed patterns, checks dependency trees for",
            "conflicts, and runs guardrail rules before changes are committed. Acts as",
            "the first line of defense against non-compliant changes.",
            "",
            "### ARCHITECT (Port 7202)",
            "",
            "The analysis and design review agent. ARCHITECT performs project-level",
            "analysis, reviews proposed designs against established patterns, checks",
            "dependency health, and suggests architectural improvements. Consulted",
            "before significant structural changes.",
            "",
            "### FORGE (Port 7203)",
            "",
            "The build execution agent. FORGE manages the full build pipeline: compiling",
            "code, running test suites, managing CI pipelines, and watching for file",
            "changes that trigger rebuild cycles. The primary execution engine for",
            "automated build and test workflows.",
            "",
            "### SCRIBE (Port 7204)",
            "",
            "The documentation and knowledge agent. SCRIBE generates API documentation,",
            "explains code, searches the knowledge base, and keeps docs in sync with",
            "code changes. Responsible for ensuring documentation drift does not occur.",
            "",
            "### RECON (Port 7205)",
            "",
            "The code review and security agent. RECON performs diff reviews, analyzes",
            "file changes for patterns, runs security scans, and generates quality",
            "reports. Provides detailed feedback on code health and identifies potential",
            "vulnerabilities.",
            "",
            "### NEXUS (Port 7206)",
            "",
            "The workflow orchestration agent. NEXUS coordinates multi-step workflows,",
            "manages task queues, maintains the agent roster, and aggregates requests",
            "across agents. Acts as the central dispatcher for complex operations that",
            "span multiple agents.",
            "",
            "### WATCHTOWER (Port 7207)",
            "",
            "The health monitoring agent. WATCHTOWER monitors all agents and blade",
            "servers for health, collects metrics and performance data, maintains alert",
            "history, and can restart failed services. Provides the aggregated data",
            "that powers the dashboard's sub-agent grid via the `/api/sub-agents` endpoint.",
            "",
            "## Implementation Details",
            "",
            "### MCP Communication",
            "",
            "All agents communicate over the MCP protocol with SSE (Server-Sent Events)",
            "response parsing. Critical implementation notes:",
            "",
            "- The MCP SDK returns `text/event-stream`, not raw JSON. Clients must parse SSE frames.",
            "- After the MCP init handshake, agents must send `notifications/initialized`.",
            "- The `Accept` header must include both `application/json` and `text/event-stream`.",
            "- All agents share `blade-client.js` for standardized MCP communication.",
            "",
            "### Key Files",
            "",
            "| File | Purpose |",
            "| --- | --- |",
            "| `D:\\Sub-Agents\\lib\\blade-client.js` | SSE-aware MCP client shared by all agents |",
            "| `D:\\Sub-Agents\\lib\\agent-base.js` | Shared server factory for agent bootstrapping |",
            "| `D:\\Sub-Agents\\agents\\{name}\\index.js` | Individual agent implementation |",
            "| `D:\\Sub-Agents\\Start-SubAgents.ps1` | Start all 7 agents |",
            "| `D:\\Sub-Agents\\Stop-SubAgents.ps1` | Stop all 7 agents |",
            "",
            "### Resource Usage",
            "",
            "| Metric | Value |",
            "| --- | --- |",
            "| Total RAM (7 agents) | ~540 MB |",
            "| Per-agent average | ~77 MB |",
            "| Registration | All registered with blade agent-comm server |",
            "| Aggregation | WATCHTOWER aggregates all data via `/api/sub-agents` |",
            "",
            "See also: [Infrastructure](Infrastructure) | [Architecture](Architecture) | [API Reference](API-Reference)",
            "",
        ]
    )


# ---------------------------------------------------------------------------
# Wiki: API Reference
# ---------------------------------------------------------------------------

def render_api_reference() -> str:
    return "\n".join(
        [
            f"# {PROJECT_NAME} API Reference",
            "",
            "The platform exposes 25 backend services through the Caddy reverse proxy.",
            "All endpoints are accessible at `http://192.168.1.3:8080/` with path-based",
            "routing. The aggregator gateway at port 7200 unifies all MCP tools into a",
            "single connection point.",
            "",
            "## Backend Services",
            "",
            "### Blade Tool Servers (18 services)",
            "",
            "| Port | Service | Description |",
            "| --- | --- | --- |",
            "| 7101 | **repo-search** | Full-text search across mirrored git repositories |",
            "| 7102 | **docs-search** | Documentation corpus search and retrieval |",
            "| 7103 | **fs-cache** | Filesystem caching for frequently accessed files |",
            "| 7104 | **build-cache** | Build artifact caching and retrieval |",
            "| 7105 | **symbol-index** | Code symbol indexing (functions, classes, definitions) |",
            "| 7106 | **session-context** | Session state persistence across tool calls |",
            "| 7107 | **response-cache** | Response caching for expensive operations |",
            "| 7108 | **git-intel** | Git history analysis, blame, and commit intelligence |",
            "| 7109 | **file-digest** | File hashing, checksums, and change detection |",
            "| 7110 | **vector-search** | Vector embedding search for semantic code matching |",
            "| 7111 | **dep-graph** | Dependency graph analysis and visualization |",
            "| 7112 | **lint-cache** | Lint result caching and retrieval |",
            "| 7113 | **snippet-store** | Reusable code snippet storage and search |",
            "| 7114 | **task-queue** | Task queue management and scheduling |",
            "| 7115 | **memory** | Shared key-value memory with namespace isolation |",
            "| 7116 | **agent-comm** | Inter-agent communication and message routing |",
            "| 7117 | **coordination** | Multi-agent task coordination |",
            "| 7118 | **event-bus** | Event streaming and pub/sub bus |",
            "",
            "### Sub-Agents (7 services)",
            "",
            "| Port | Agent | Description |",
            "| --- | --- | --- |",
            "| 7201 | **SENTINEL** | Design validation and enforcement guardrails |",
            "| 7202 | **ARCHITECT** | Project analysis and design review |",
            "| 7203 | **FORGE** | Build execution and test pipeline |",
            "| 7204 | **SCRIBE** | Documentation generation and knowledge search |",
            "| 7205 | **RECON** | Code review, security scanning, and quality reports |",
            "| 7206 | **NEXUS** | Workflow orchestration and request aggregation |",
            "| 7207 | **WATCHTOWER** | Health monitoring, metrics, and alerting |",
            "",
            "See [Sub-Agents](Sub-Agents) for detailed capability descriptions.",
            "",
            "## Dashboard API Endpoints",
            "",
            "The browser dashboard consumes these endpoints through Caddy at `:8080`:",
            "",
            "### Metrics",
            "",
            "| Endpoint | Method | Description |",
            "| --- | --- | --- |",
            "| `/api/metrics` | GET | Current CPU, RAM, network snapshot |",
            "| `/api/metrics/history` | GET | Last 60 data points for charting |",
            "| `/api/metrics/stream` | GET (SSE) | Real-time Server-Sent Events stream for live metrics |",
            "",
            "### Service Status",
            "",
            "| Endpoint | Method | Description |",
            "| --- | --- | --- |",
            "| `/api/clients` | GET | Connected client list from the client tracker |",
            "| `/api/sub-agents` | GET | Aggregated sub-agent status, uptime, and tool counts (via WATCHTOWER) |",
            "",
            "### Communication and Coordination",
            "",
            "| Endpoint | Method | Description |",
            "| --- | --- | --- |",
            "| `/api/agent-comm` | GET | Agent-to-agent message flow and communication status |",
            "| `/api/coordination` | GET | Multi-agent task coordination status |",
            "| `/api/event-bus` | GET | Event stream monitoring and recent events |",
            "| `/api/memory-beacon` | GET | Memory server health and beacon status |",
            "",
            "## Aggregator Gateway",
            "",
            "| Endpoint | Description |",
            "| --- | --- |",
            "| `https://192.168.1.3:8443/mcp/gateway` | HTTPS MCP gateway (self-signed cert) |",
            "| `http://192.168.1.3:8080/mcp/gateway` | HTTP MCP gateway |",
            "| `http://192.168.1.3:7200/health` | Gateway health check |",
            "| `http://192.168.1.3:7200/dashboard` | Gateway built-in dashboard |",
            "",
            "The gateway proxies all tool calls to the correct backend. It currently exposes",
            "96+ tools from 21 of 25 online backends. Four services (response-cache,",
            "coordination, event-bus, agent-comm) do not expose MCP tools but are accessible",
            "through their REST API endpoints above.",
            "",
            "## Metrics System Details",
            "",
            "| Property | Value |",
            "| --- | --- |",
            "| System metrics server | Port 7121 |",
            "| Client tracker | Port 7120 |",
            "| SSE stream | `/api/metrics/stream` (EventSource protocol) |",
            "| Polling interval | 5 seconds for non-SSE endpoints |",
            "| Chart rendering | Custom canvas, 60-point rolling window |",
            "| History depth | 60 data points |",
            "",
            "See also: [Architecture](Architecture) | [Infrastructure](Infrastructure)",
            "",
        ]
    )


# ---------------------------------------------------------------------------
# Wiki: Operations
# ---------------------------------------------------------------------------

def render_operations() -> str:
    return "\n".join(
        [
            f"# {PROJECT_NAME} Operations",
            "",
            "Build, test, deploy, and manage the platform.",
            "",
            "## Build",
            "",
            "The project uses CMake with preset-based configuration. Debug builds are the",
            "standard development target.",
            "",
            "```powershell",
            "# Configure (first time or after CMakeLists.txt changes)",
            "cmake --preset debug",
            "",
            "# Build",
            "cmake --build build\\debug --config Debug",
            "```",
            "",
            "## Test",
            "",
            "Tests run through CTest with verbose output on failure.",
            "",
            "```powershell",
            "ctest --test-dir build\\debug -C Debug --output-on-failure",
            "```",
            "",
            "## Install",
            "",
            "Install produces a distributable layout under `dist\\debug\\`.",
            "",
            "```powershell",
            "cmake --install build\\debug --config Debug --prefix dist\\debug",
            "```",
            "",
            "## Run the Bootstrapper",
            "",
            "The bootstrapper handles environment detection, import flows, and initial setup.",
            "",
            "```powershell",
            "# Detect the current environment",
            "dist\\debug\\MasterControlBootstrapper.exe detect",
            "```",
            "",
            "## Dashboard Access",
            "",
            "| Surface | URL | Notes |",
            "| --- | --- | --- |",
            "| Browser dashboard | `http://192.168.1.3:8080/dashboard/` | Real-time metrics and server status |",
            "| Gateway dashboard | `http://192.168.1.3:7200/dashboard` | Aggregator gateway status |",
            "| Gateway health | `http://192.168.1.3:7200/health` | JSON health check |",
            "| HTTPS gateway | `https://192.168.1.3:8443/mcp/gateway` | Remote MCP connection point |",
            "",
            "## Service Management",
            "",
            "### Sub-Agents",
            "",
            "```powershell",
            "# Start all 7 sub-agents",
            "D:\\Sub-Agents\\Start-SubAgents.ps1",
            "",
            "# Stop all 7 sub-agents",
            "D:\\Sub-Agents\\Stop-SubAgents.ps1",
            "```",
            "",
            "### Dashboard Server",
            "",
            "```powershell",
            "# Start the Node.js static server on port 18000",
            "D:\\mcp\\dashboard\\serve.ps1",
            "```",
            "",
            "## Push Guard",
            "",
            "The repository includes a pre-push hook that rejects commits declaring AI",
            "contributors. This is enforced at two levels:",
            "",
            "1. **Local hook**: Enable with `scripts\\Enable-GitHooks.ps1`. The pre-push",
            "   hook inspects staged commits and blocks any that list AI contributors.",
            "2. **GitHub workflow**: The `AI Contributor Guard` workflow runs on `push` and",
            "   `pull_request` events, mirroring the same rule server-side.",
            "",
            "## Development Workflow",
            "",
            "The standard local development loop:",
            "",
            "1. Make changes in `src/` or `include/`.",
            "2. Build: `cmake --build build\\debug --config Debug`",
            "3. Test: `ctest --test-dir build\\debug -C Debug --output-on-failure`",
            "4. Commit and push to `main`.",
            "5. GitHub agents automatically update CHANGELOG, README, wiki, and version.",
            "",
            "See also: [Automation](Automation) | [Remote Client](Remote-Client)",
            "",
        ]
    )


# ---------------------------------------------------------------------------
# Wiki: Remote Client
# ---------------------------------------------------------------------------

def render_remote_client() -> str:
    return "\n".join(
        [
            f"# {PROJECT_NAME} Remote Client Setup",
            "",
            "How to connect a remote Claude Code Desktop instance to the BLADE MCP Gateway.",
            "",
            "## Overview",
            "",
            "Remote Claude Code instances on the LAN need to reach the aggregator gateway",
            "at `https://192.168.1.3:8443/mcp/gateway`. Because the gateway uses a",
            "self-signed certificate, the client machine needs the certificate installed",
            "and the MCP server entry added to `~/.claude.json`.",
            "",
            "A self-contained PowerShell installer script handles everything in one step.",
            "",
            "## Installation",
            "",
            "Run `Install-BladeGatewayPlugin.ps1` on the remote Windows machine:",
            "",
            "```powershell",
            "# From any of these sources:",
            "# Right-click -> Run with PowerShell",
            "```",
            "",
            "### What the Installer Does",
            "",
            "| Step | Action |",
            "| --- | --- |",
            "| 1 | Checks LAN connectivity to `192.168.1.3:8080` |",
            "| 2 | Downloads and installs the self-signed cert to `CurrentUser\\Root` store |",
            "| 3 | Saves the PEM cert to `~/.blade-mcp/master-control.pem` |",
            "| 4 | Sets `NODE_EXTRA_CA_CERTS` environment variable (for Node.js TLS in Claude Code) |",
            "| 5 | Adds `master-control-gateway` entry to `~/.claude.json` mcpServers |",
            "| 6 | Verifies HTTPS connectivity to `https://192.168.1.3:8443/health` |",
            "",
            "After installation, restart Claude Code Desktop to pick up the new MCP server.",
            "",
            "## Distribution Methods",
            "",
            "| Method | Path |",
            "| --- | --- |",
            "| Desktop shortcut | `C:\\Users\\Master-Control\\Desktop\\Install-BladeGatewayPlugin.ps1` |",
            "| NAS share | `\\\\192.168.1.3\\nas\\Install-BladeGatewayPlugin.ps1` |",
            "| HTTP download | `http://192.168.1.3:8080/Install-BladeGatewayPlugin.ps1` |",
            "| Source | `D:\\mcp\\plugin\\Install-BladeGatewayPlugin.ps1` |",
            "",
            "## Technical Details",
            "",
            "### Certificate Handling",
            "",
            "The installer uses dual certificate trust to cover both PowerShell and Node.js:",
            "",
            "- **OS certificate store** (`CurrentUser\\Root`): Covers PowerShell, .NET, and",
            "  system-level HTTPS calls.",
            "- **NODE_EXTRA_CA_CERTS**: Points Node.js (which Claude Code uses internally)",
            "  to the PEM file at `~/.blade-mcp/master-control.pem`.",
            "",
            "Stale certificate removal is wrapped in `try/catch` because admin-installed",
            "certs may return access-denied errors on removal.",
            "",
            "### Claude Code Configuration",
            "",
            "The installer adds this entry to `~/.claude.json`:",
            "",
            "```json",
            "{",
            '  "mcpServers": {',
            '    "master-control-gateway": {',
            '      "type": "http",',
            '      "url": "https://192.168.1.3:8443/mcp/gateway"',
            "    }",
            "  }",
            "}",
            "```",
            "",
            "This is a direct JSON edit, not a plugin installation. Claude Code Desktop",
            "uses a marketplace system for plugins, not directory scanning. The installer",
            "edits `~/.claude.json` directly.",
            "",
            "### Compatibility",
            "",
            "- PowerShell 5.1 compatible (the default `Run with PowerShell` handler).",
            "- Does not use `-SkipCertificateCheck` (PS 5.1 lacks it). Uses",
            "  `ServicePointManager` fallback for certificate bypass during initial download.",
            "- Works with right-click -> Run with PowerShell on the `.ps1` file.",
            "",
            "## Troubleshooting",
            "",
            "| Issue | Solution |",
            "| --- | --- |",
            "| `Connection refused` on install | Verify MASTER-CONTROL server is running and reachable at `192.168.1.3:8080` |",
            "| Claude Code doesn't see the gateway | Restart Claude Code Desktop after installation |",
            "| Certificate errors after reinstall | Delete `~/.blade-mcp/` and run the installer again |",
            "| UNC path issues in bash | Use the HTTP download method instead of the NAS share |",
            "",
            "### What Did NOT Work (Lessons Learned)",
            "",
            "These approaches were attempted and abandoned:",
            "",
            "1. **Plugin directory** at `~/.claude/plugins/blade-mcp-gateway/` — Claude Code",
            "   Desktop does not scan for arbitrary plugin folders. It uses a git-based",
            "   marketplace system.",
            "2. **installed_plugins.json** — Managed by the marketplace, not user-editable.",
            "3. **Zip file extraction** — Too many manual steps for reliable LAN deployment.",
            "4. **UNC path copying via bash** — Backslashes get eaten by bash interpretation.",
            "",
            "See also: [Infrastructure](Infrastructure) | [Architecture](Architecture)",
            "",
        ]
    )


# ---------------------------------------------------------------------------
# Wiki: Automation
# ---------------------------------------------------------------------------

def render_automation(state: dict) -> str:
    release = latest_release(state)
    return "\n".join(
        [
            f"# {PROJECT_NAME} Automation",
            "",
            "The repository is maintained by a suite of GitHub agents that run automatically",
            "on every push to `main`. These agents handle versioning, changelog updates,",
            "documentation generation, wiki synchronization, and contributor compliance.",
            "",
            "## Automation Pipeline",
            "",
            "When code is pushed to `main`, the following sequence runs:",
            "",
            "```",
            "Push to main",
            "    |",
            "    v",
            "release_manager.py init",
            "    |",
            "    v",
            "release_manager.py bump          (VERSION.json, CHANGELOG.md)",
            "    |",
            "    v",
            "sync_docs.py                     (README.md, docs/wiki/*.md)",
            "    |",
            "    v",
            "git add + commit                 (chore(agents): sync...)",
            "    |",
            "    v",
            "gh release create                (if version bumped)",
            "    |",
            "    v",
            "sync_docs.py --sync-wiki         (push docs/wiki/ to GitHub wiki)",
            "```",
            "",
            "Commits made by the automation pipeline use the `[skip agents]` suffix to",
            "prevent infinite loops.",
            "",
            "## Agent Details",
            "",
            "| Agent | Trigger | Script | What It Does |",
            "| --- | --- | --- | --- |",
            "| **Version Agent** | `push`, `workflow_dispatch` | `release_manager.py bump` | Reads commit history, determines the next semantic version, updates `VERSION.json` |",
            "| **Changelog Agent** | `push`, `workflow_dispatch` | `release_manager.py bump` | Generates changelog entries for new commits, appends to `CHANGELOG.md` |",
            "| **Wiki + README Agent** | `push`, `workflow_dispatch` | `sync_docs.py` | Regenerates `README.md` and all wiki source pages from plan documents |",
            "| **Version Documentation Agent** | `push`, `workflow_dispatch` | `release_manager.py` | Creates release pages in `docs/versions/` and publishes GitHub Releases |",
            "| **AI Contributor Guard** | `push`, `pull_request` | `check_no_ai_contributors.py` | Rejects commits that declare AI contributors in author or co-author fields |",
            "",
            "## Workflows",
            "",
            "| Workflow File | Purpose |",
            "| --- | --- |",
            "| `repository-maintenance-agents.yml` | Main pipeline: version bump, docs sync, release, wiki push |",
            "| `ai-contributor-guard.yml` | Blocks AI-attributed commits on push and PR |",
            "| `forsetti-compliance.yml` | Forsetti governance compliance checks |",
            "",
            "## Commit Conventions",
            "",
            f"- Agent commits are prefixed with `{AGENT_COMMIT_PREFIX}`",
            "- Agent commits include `[skip agents]` to prevent re-triggering.",
            "- The `if: github.actor != 'github-actions[bot]'` guard prevents bot loops.",
            "",
            "## Manual Trigger",
            "",
            "All maintenance agents can be triggered manually via `workflow_dispatch`:",
            "",
            "```bash",
            "gh workflow run repository-maintenance-agents.yml",
            "```",
            "",
            "## Current Baseline",
            "",
            f"| Property | Value |",
            f"| --- | --- |",
            f"| Current tracked release | `{release['tag']}` |",
            f"| Agent commit prefix | `{AGENT_COMMIT_PREFIX}` |",
            "| Wiki source directory | `docs/wiki/` |",
            "| Wiki sync target | `github.com/{repo}.wiki.git` |",
            "",
            "## Scripts",
            "",
            "All automation scripts are in `scripts/github_agents/`:",
            "",
            "| Script | Purpose |",
            "| --- | --- |",
            "| `release_manager.py` | Version bumping, changelog generation, release notes |",
            "| `sync_docs.py` | README, wiki page generation, and GitHub wiki sync |",
            "| `check_no_ai_contributors.py` | AI contributor detection and rejection |",
            "| `common.py` | Shared constants, git helpers, and file utilities |",
            "",
            "See also: [Operations](Operations) | [Versions](Versions)",
            "",
        ]
    )


# ---------------------------------------------------------------------------
# Wiki: Versions
# ---------------------------------------------------------------------------

def render_versions(state: dict) -> str:
    release = latest_release(state)
    history = state.get("history", [])

    lines = [
        f"# {PROJECT_NAME} Versions",
        "",
        "The project uses semantic versioning with automated version bumps on every",
        "push to `main`. The version agent analyzes commit history to determine the",
        "appropriate bump level (patch, minor, or major).",
        "",
        "## Current Release",
        "",
        "| Property | Value |",
        "| --- | --- |",
        f"| Version | `{release['tag']}` |",
        f"| Released | `{release['released_at']}` |",
        f"| Summary | {release['summary']} |",
        "",
        "## Versioning Scheme",
        "",
        "- **Patch** (`0.1.x`): Bug fixes, documentation updates, metadata changes.",
        "- **Minor** (`0.x.0`): New features, new modules, capability additions.",
        "- **Major** (`x.0.0`): Breaking changes requiring consumer adaptation.",
        "",
        "Versions are tracked in `VERSION.json` and tagged as GitHub Releases.",
        "",
        "## Release Documents",
        "",
        "- [Version Index](../versions/index.md) — Full list of all releases.",
        "- [Latest Release Notes](../versions/latest.md) — Notes for the current release.",
        "",
    ]

    if history:
        lines.extend([
            "## Recent Releases",
            "",
            "| Version | Date | Summary |",
            "| --- | --- | --- |",
        ])
        for entry in history[:15]:
            tag = entry.get("tag", "")
            date = entry.get("released_at", "")
            summary = entry.get("summary", "")
            lines.append(f"| `{tag}` | `{date}` | {summary} |")
        lines.append("")

    lines.extend([
        "See also: [Automation](Automation) | [Operations](Operations)",
        "",
    ])

    return "\n".join(lines)


# ---------------------------------------------------------------------------
# Wiki: Sidebar
# ---------------------------------------------------------------------------

def render_sidebar() -> str:
    return "\n".join(
        [
            f"# {PROJECT_NAME}",
            "",
            "**[Home](Home)**",
            "",
            "**Architecture**",
            "- [Architecture](Architecture)",
            "- [Infrastructure](Infrastructure)",
            "- [Sub-Agents](Sub-Agents)",
            "- [API Reference](API-Reference)",
            "",
            "**Usage**",
            "- [Operations](Operations)",
            "- [Remote Client](Remote-Client)",
            "",
            "**Project**",
            "- [Automation](Automation)",
            "- [Versions](Versions)",
            "",
        ]
    )


# ---------------------------------------------------------------------------
# Wiki: Footer
# ---------------------------------------------------------------------------

def render_footer() -> str:
    return "\n".join(
        [
            "---",
            f"This wiki is auto-generated from repository source documents by the [{REPOSITORY_NAME}]({REPOSITORY_URL}) automation pipeline.",
            f"To update wiki content, edit the source files in `plans/` or `scripts/github_agents/sync_docs.py` and push to `main`.",
            "",
        ]
    )


# ---------------------------------------------------------------------------
# Write all docs
# ---------------------------------------------------------------------------

def write_docs() -> None:
    state = load_state()
    write_text(README_FILE, render_readme(state))
    write_text(WIKI_DIR / "Home.md", render_home(state))
    write_text(WIKI_DIR / "Architecture.md", render_architecture())
    write_text(WIKI_DIR / "Infrastructure.md", render_infrastructure())
    write_text(WIKI_DIR / "Sub-Agents.md", render_sub_agents())
    write_text(WIKI_DIR / "API-Reference.md", render_api_reference())
    write_text(WIKI_DIR / "Operations.md", render_operations())
    write_text(WIKI_DIR / "Remote-Client.md", render_remote_client())
    write_text(WIKI_DIR / "Automation.md", render_automation(state))
    write_text(WIKI_DIR / "Versions.md", render_versions(state))
    write_text(WIKI_DIR / "_Sidebar.md", render_sidebar())
    write_text(WIKI_DIR / "_Footer.md", render_footer())


# ---------------------------------------------------------------------------
# Sync to GitHub wiki repository
# ---------------------------------------------------------------------------

def sync_wiki() -> None:
    token = os.environ.get("GITHUB_TOKEN") or os.environ.get("GH_TOKEN")
    repository = os.environ.get("GITHUB_REPOSITORY")
    if not token or not repository:
        raise SystemExit("GITHUB_TOKEN and GITHUB_REPOSITORY are required to sync the wiki.")

    with tempfile.TemporaryDirectory() as temp_dir_raw:
        temp_dir = Path(temp_dir_raw)
        wiki_dir = temp_dir / "wiki"
        remote = f"https://x-access-token:{token}@github.com/{repository}.wiki.git"

        clone = run_git(["clone", remote, str(wiki_dir)], cwd=temp_dir, check=False)
        if clone.returncode != 0:
            wiki_dir.mkdir(parents=True, exist_ok=True)
            run_git(["init"], cwd=wiki_dir)
            run_git(["checkout", "-B", "master"], cwd=wiki_dir)
            run_git(["remote", "add", "origin", remote], cwd=wiki_dir)

        for child in wiki_dir.iterdir():
            if child.name == ".git":
                continue
            if child.is_dir():
                shutil.rmtree(child)
            else:
                child.unlink()

        for source in WIKI_DIR.glob("*.md"):
            shutil.copy2(source, wiki_dir / source.name)

        run_git(["config", "user.name", "github-actions[bot]"], cwd=wiki_dir)
        run_git(
            ["config", "user.email", "41898282+github-actions[bot]@users.noreply.github.com"],
            cwd=wiki_dir,
        )
        run_git(["add", "."], cwd=wiki_dir)
        diff = run_git(["diff", "--cached", "--quiet"], cwd=wiki_dir, check=False)
        if diff.returncode == 0:
            return

        version = load_state().get("current_tag", "v0.1.0")
        run_git(["commit", "-m", f"{AGENT_COMMIT_PREFIX} sync wiki for {version} [skip agents]"], cwd=wiki_dir)
        branch = git_output(["rev-parse", "--abbrev-ref", "HEAD"], cwd=wiki_dir)
        run_git(["push", "origin", f"HEAD:{branch}"], cwd=wiki_dir)


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Synchronize generated repository documentation.")
    parser.add_argument("--sync-wiki", action="store_true", help="Push docs/wiki content to the GitHub wiki repository.")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    write_docs()
    if args.sync_wiki:
        sync_wiki()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
