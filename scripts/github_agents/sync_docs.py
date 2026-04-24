from __future__ import annotations

import argparse
import json
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
MANIFEST_DIR = ROOT / "src" / "MasterControlModules" / "Resources" / "ForsettiManifests"


# ----------------------------------------------------------------------------
# State / shared helpers
# ----------------------------------------------------------------------------


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


def list_manifest_modules() -> list[dict]:
    """Read every Forsetti manifest in the repository so wiki content is
    grounded in the real module catalog instead of hand-curated lists."""
    if not MANIFEST_DIR.exists():
        return []
    modules: list[dict] = []
    for path in sorted(MANIFEST_DIR.glob("*.json")):
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
        except Exception:
            continue
        version_obj = data.get("moduleVersion") or {}
        if isinstance(version_obj, dict):
            version_str = ".".join(
                str(version_obj.get(k, 0)) for k in ("major", "minor", "patch")
            )
        else:
            version_str = str(version_obj)
        modules.append(
            {
                "file": path.name,
                "id": data.get("moduleID")
                or data.get("moduleId")
                or data.get("id")
                or path.stem,
                "title": data.get("displayName") or data.get("title") or path.stem,
                "type": data.get("moduleType") or "",
                "version": version_str,
                "platforms": ", ".join(data.get("supportedPlatforms") or []),
                "summary": data.get("summary") or data.get("description") or "",
                "category": data.get("category") or "",
            }
        )
    return modules


def encode_badge(text: str) -> str:
    return text.replace(" ", "%20").replace("-", "--").replace("_", "__")


def shield(label: str, value: str, color: str) -> str:
    label_q = encode_badge(label)
    value_q = encode_badge(value)
    return f"![{label}](https://img.shields.io/badge/{label_q}-{value_q}-{color}?style=flat-square)"


# ----------------------------------------------------------------------------
# README
# ----------------------------------------------------------------------------


def render_readme(state: dict) -> str:
    release = latest_release(state)
    modules = list_manifest_modules()

    badges = " ".join(
        [
            shield("version", release["tag"], "00f6ff"),
            shield("released", release.get("released_at", "n/a"), "031018"),
            shield("platform", "Windows 11 / Server 2022", "0a1018"),
            shield("toolchain", "C++20 · WinUI 3 · CMake", "00aacc"),
            shield("license", "Proprietary", "5a00e8"),
        ]
    )

    return "\n".join(
        [
            f"# {PROJECT_NAME}",
            "",
            badges,
            "",
            "> Forsetti-compliant Windows orchestration control plane for MCP services, AI provider routing, ",
            "> CLU governance, sub-agents, platform gateways, telemetry, and browser-based operations — ",
            "> all delivered as a single Tron-themed product.",
            "",
            f"- **Repository:** [`{REPOSITORY_NAME}`]({REPOSITORY_URL})",
            f"- **Current release:** `{release['tag']}` ({release.get('released_at', 'unreleased')})",
            f"- **Forsetti modules:** {len(modules)}",
            "",
            "---",
            "",
            "## Why this project exists",
            "",
            "Modern AI orchestration usually means stitching together a half-dozen CLI tools, ",
            "duct-taping JSON config across them, and praying nothing rotates. This project ",
            "collapses that into one Windows-native control plane: install once, point your ",
            "providers and sub-agents at it, and run everything from a single Tron-styled shell ",
            "or browser dashboard.",
            "",
            "### Highlights",
            "",
            "| | |",
            "| --- | --- |",
            "| **Multi-binary control plane** | Windows service host, WinUI 3 desktop shell, and browser dashboard — all backed by the same in-process runtime |",
            "| **Auto-Connect AI providers** | Enter credentials, pick roles, runtime handles capability resolution, model discovery, DPAPI encryption, and assignment fan-out |",
            "| **Live command stream** | Every admin API request is captured by a 512-event ring buffer with millisecond timestamps, methods, targets, status codes, and latency |",
            "| **CLU governance** | First-class Forsetti service module for posture, rules, role routing, Apple operations, and platform governance execution |",
            "| **Cross-platform gateways** | Windows, macOS, and iOS gateway + governance lanes — Apple lanes route through SSH/companion-service Apple hosts |",
            "| **Repo-owned installer** | MSI-first release bundle plus bootstrapper-backed lifecycle tooling for preflight/install/validate/upgrade/repair/uninstall |",
            "| **Tron aesthetic, end-to-end** | Cyan-on-blue-black palette, Bahnschrift SemiCondensed type, zero corner radii, accent pulse animations, focus-visible outlines, prefers-reduced-motion respected |",
            "",
            "---",
            "",
            "## Repository layout",
            "",
            "```",
            "master-control-dashboard/",
            "├── include/MasterControl/         # public contracts, models, defaults",
            "├── src/",
            "│   ├── MasterControlApp/          # shared runtime (~9k LOC core)",
            "│   ├── MasterControlServiceHost/  # Windows service entry point",
            "│   ├── MasterControlShell/        # WinUI 3 operator shell",
            "│   ├── MasterControlModules/      # Forsetti modules + JSON manifests",
            "│   └── MasterControlBootstrapper/ # lifecycle engine and packaged install helper",
            "├── resources/",
            "│   ├── web/                       # browser dashboard assets",
            "│   └── clu/                       # CLU governance profile",
            "├── scripts/                       # build, package, deploy, agents",
            "├── plans/                         # design + infrastructure notes",
            "├── docs/wiki/                     # wiki source pages (auto-generated)",
            "└── docs/versions/                 # release docs (auto-generated)",
            "```",
            "",
            "---",
            "",
            "## Build, validate, and stage",
            "",
            "```powershell",
            "# Configure and build (Debug)",
            "cmake --preset debug",
            "cmake --build build\\debug --config Debug",
            "",
            "# Run the local test suite",
            "ctest --test-dir build\\debug -C Debug --output-on-failure",
            "",
            "# Forsetti compliance + repo native checks",
            "powershell -NoProfile -ExecutionPolicy Bypass -File scripts\\check-mastercontrol-forsetti.ps1",
            "",
            "# Stage installable payload",
            "cmake --install build\\debug --config Debug --prefix dist\\debug",
            "",
            "# Build a signed release package",
            "powershell -NoProfile -ExecutionPolicy Bypass -File scripts\\Package-MasterControlOrchestrationServer.ps1 -Preset release",
            "```",
            "",
            "See [Operations](docs/wiki/Operations.md) for the full deployment matrix, and ",
            "[Architecture](docs/wiki/Architecture.md) for the runtime composition diagram.",
            "",
            "---",
            "",
            "## Documentation",
            "",
            "| Page | What you get |",
            "| --- | --- |",
            "| [Home](docs/wiki/Home.md) | Project overview, current release, and navigation |",
            "| [Architecture](docs/wiki/Architecture.md) | Runtime composition, Forsetti modules, request flow diagrams |",
            "| [API Reference](docs/wiki/API-Reference.md) | Every admin API route with method, payload, and example responses |",
            "| [Auto-Connect AI](docs/wiki/Auto-Connect-AI.md) | The end-to-end automation pipeline for adding AI providers |",
            "| [CLU Governance](docs/wiki/CLU-Governance.md) | Command Logic Unit module, rules, roles, and platform governance lanes |",
            "| [Telemetry & Activity](docs/wiki/Telemetry-and-Activity.md) | Live telemetry pipeline + the activity ring buffer |",
            "| [Tron UI Theme](docs/wiki/Tron-UI-Theme.md) | Palette, typography, motion language, and component recipes |",
            "| [Sub-Agents](docs/wiki/Sub-Agents.md) | The 7-agent roster, ports, and shared platform gateway client |",
            "| [Operations](docs/wiki/Operations.md) | Build, package, install, upgrade, repair, uninstall flows |",
            "| [Infrastructure](docs/wiki/Infrastructure.md) | Deployment shape, packaging model, and target hosts |",
            "| [Remote Client](docs/wiki/Remote-Client.md) | Onboarding direction for Codex, Claude Code, and gateway discovery |",
            "| [Automation](docs/wiki/Automation.md) | The GitHub agents that maintain the repository |",
            "| [Versions](docs/wiki/Versions.md) | Release history and the versioning scheme |",
            "| [Troubleshooting](docs/wiki/Troubleshooting.md) | Common failure modes and how to diagnose them |",
            "",
            "---",
            "",
            "## Current release",
            "",
            f"**`{release['tag']}` — {release.get('released_at', 'unreleased')}**",
            "",
            f"{release.get('summary', '')}",
            "",
        ]
        + ([f"- {entry}" for entry in release.get("entries", [])] or [])
        + [
            "",
            "---",
            "",
            f"Repository: {REPOSITORY_URL}",
            "",
        ]
    )


# ----------------------------------------------------------------------------
# Wiki: Home
# ----------------------------------------------------------------------------


def render_home(state: dict) -> str:
    release = latest_release(state)
    modules = list_manifest_modules()

    badges = " ".join(
        [
            shield("version", release["tag"], "00f6ff"),
            shield("released", release.get("released_at", "n/a"), "031018"),
            shield("modules", str(len(modules)), "00aacc"),
            shield("platform", "Win11 · Server 2022", "0a1018"),
            shield("theme", "Tron", "00f6ff"),
        ]
    )

    return "\n".join(
        [
            f"# {PROJECT_NAME}",
            "",
            badges,
            "",
            "> A single-binary, Forsetti-compliant Windows control plane for AI orchestration, ",
            "> MCP hosting, sub-agents, platform governance, and telemetry — wrapped in a Tron aesthetic.",
            "",
            "---",
            "",
            "## At a glance",
            "",
            "```mermaid",
            "flowchart LR",
            "    classDef accent fill:#031018,stroke:#00F6FF,color:#E6FCFF;",
            "    classDef faint fill:#0a1018,stroke:#5A00E8FF,color:#8CB7C4;",
            "",
            "    Operator((Operator))",
            "    Shell[WinUI 3 Shell]:::accent",
            "    Browser[Browser Admin UI]:::accent",
            "    Service[[Service Host<br/>127.0.0.1:7300]]:::accent",
            "    Runtime[(Shared Runtime)]:::accent",
            "    Providers[[AI Providers]]:::faint",
            "    SubAgents[[Sub-Agents]]:::faint",
            "    MCP[[MCP Servers]]:::faint",
            "    Apple[[Apple Hosts]]:::faint",
            "    CLU{{CLU Governance}}:::accent",
            "",
            "    Operator --> Shell & Browser",
            "    Shell --> Service",
            "    Browser --> Service",
            "    Service --> Runtime",
            "    Runtime --> Providers & SubAgents & MCP & Apple",
            "    Runtime --> CLU",
            "    CLU -. enforces .-> Providers & MCP & Apple",
            "```",
            "",
            "---",
            "",
            "## Current release",
            "",
            "| Field | Value |",
            "| --- | --- |",
            f"| **Version** | `{release['tag']}` |",
            f"| **Released** | `{release.get('released_at', 'unreleased')}` |",
            f"| **Summary** | {release.get('summary', '')} |",
            f"| **Forsetti modules** | {len(modules)} |",
            f"| **Repository** | [{REPOSITORY_NAME}]({REPOSITORY_URL}) |",
            "",
            "---",
            "",
            "## Navigation",
            "",
            "### Architecture & internals",
            "| Page | Topic |",
            "| --- | --- |",
            "| [Architecture](Architecture) | Runtime composition, modules, request flow |",
            "| [API Reference](API-Reference) | Every HTTP route exposed by the runtime |",
            "| [CLU Governance](CLU-Governance) | Command Logic Unit, rules, role routing |",
            "| [Auto-Connect AI](Auto-Connect-AI) | Provider automation pipeline |",
            "| [Telemetry & Activity](Telemetry-and-Activity) | Live telemetry + activity ring |",
            "| [Sub-Agents](Sub-Agents) | The 7-agent roster |",
            "",
            "### UI & user experience",
            "| Page | Topic |",
            "| --- | --- |",
            "| [Tron UI Theme](Tron-UI-Theme) | Palette, typography, motion |",
            "",
            "### Operations & deployment",
            "| Page | Topic |",
            "| --- | --- |",
            "| [Operations](Operations) | Build, package, install, upgrade, uninstall |",
            "| [Infrastructure](Infrastructure) | Deployment shape and target hosts |",
            "| [Remote Client](Remote-Client) | Codex / Claude Code onboarding |",
            "| [Troubleshooting](Troubleshooting) | Common failures and diagnosis |",
            "",
            "### Project & release",
            "| Page | Topic |",
            "| --- | --- |",
            "| [Automation](Automation) | GitHub agents that maintain this repo |",
            "| [Versions](Versions) | Release history |",
            "",
            "---",
            "",
            "## Three-line product pitch",
            "",
            "1. **Install once.** A single Windows installer drops the service host, the WinUI shell, and the browser admin UI in one place.",
            "2. **Connect everything.** Auto-Connect handles AI providers; custom MCP servers and sub-agents register with one POST.",
            "3. **Run from one console.** Every command is governed by CLU, captured in the activity ring, and visible in the live operator stream.",
            "",
        ]
    )


# ----------------------------------------------------------------------------
# Wiki: Architecture
# ----------------------------------------------------------------------------


def render_architecture() -> str:
    modules = list_manifest_modules()

    module_rows = "\n".join(
        f"| `{m['id']}` | {m['title']} | {m['type'] or '—'} | `{m['version'] or '—'}` | {m['platforms'] or '—'} |"
        for m in modules
    ) or "| _no manifests discovered_ | | | | |"

    return "\n".join(
        [
            f"# {PROJECT_NAME} — Architecture",
            "",
            shield("layer", "C++20 · WinUI 3 · vanilla JS", "00f6ff"),
            "",
            "This page is the canonical map of how the runtime, the operator surfaces, ",
            "and the Forsetti modules fit together. It mirrors the actual repository, ",
            "so when in doubt the source files referenced below are the ground truth.",
            "",
            "---",
            "",
            "## Runtime topology",
            "",
            "```mermaid",
            "flowchart TB",
            "    classDef accent fill:#031018,stroke:#00F6FF,color:#E6FCFF;",
            "    classDef sub fill:#0a1018,stroke:#5A00E8FF,color:#8CB7C4;",
            "",
            "    subgraph Surfaces[Operator Surfaces]",
            "        Shell[WinUI 3 Shell<br/>MasterControlShell.exe]:::accent",
            "        Browser[Browser Admin UI<br/>resources/web]:::accent",
            "    end",
            "",
            "    subgraph Host[Host Process]",
            "        Service[Service Host<br/>MasterControlServiceHost.exe]:::accent",
            "        Runtime[(MasterControlRuntime<br/>shared in-process core)]:::accent",
            "    end",
            "",
            "    subgraph Forsetti[Forsetti Module Catalog]",
            "        CLU{{Command Logic Unit}}:::accent",
            "        ProviderInt[Provider Integration]:::sub",
            "        Inventory[Runtime Inventory]:::sub",
            "        Telemetry[Host Telemetry]:::sub",
            "        Beacon[Beacon Gateway]:::sub",
            "        Win[Windows Gateway/Gov]:::sub",
            "        Mac[macOS Gateway/Gov]:::sub",
            "        IOS[iOS Gateway/Gov]:::sub",
            "        Codex[Codex Provider]:::sub",
            "        Claude[Claude Code Provider]:::sub",
            "        XAI[xAI Provider]:::sub",
            "    end",
            "",
            "    Shell --> Service",
            "    Browser --> Service",
            "    Service --> Runtime",
            "    Runtime --> Forsetti",
            "    Forsetti --> CLU",
            "```",
            "",
            "---",
            "",
            "## Process / binary inventory",
            "",
            "| Binary | Source | Role |",
            "| --- | --- | --- |",
            "| `MasterControlServiceHost.exe` | `src/MasterControlServiceHost/` | Windows service host (also runs as console for development) |",
            "| `MasterControlShell.exe` | `src/MasterControlShell/` | WinUI 3 operator shell, hosts the same runtime in-process |",
            "| `MasterControlBootstrapper.exe` | `src/MasterControlBootstrapper/` | Lifecycle engine (preflight, install, validate, upgrade, repair, uninstall) |",
            "| `MasterControlOrchestrationServerSetup.exe` | `src/MasterControlBootstrapper/setup_main.cpp` | Legacy GUI setup launcher source retained for compatibility work; release bundles are MSI-first |",
            "",
            "---",
            "",
            "## Shared runtime",
            "",
            "The single most important file in the project is",
            "[`src/MasterControlApp/MasterControlRuntime.cpp`](../../src/MasterControlApp/MasterControlRuntime.cpp).",
            "Everything that exposes state through the admin API or accepts a command lives there:",
            "",
            "- HTTP request dispatch + activity ring buffer wrapping",
            "- Forsetti surface model and module loading",
            "- Provider registry, credential store, assignment fabric, execution log",
            "- MCP server / sub-agent inventory + async refresh fabric",
            "- Apple host catalog, command request execution, history",
            "- Configuration persistence and migration",
            "- CLU governance posture, tools, rules, role routing",
            "- Telemetry capture and beacon advertisement",
            "",
            "Contracts are declared in",
            "[`include/MasterControl/MasterControlContracts.h`](../../include/MasterControl/MasterControlContracts.h),",
            "models in",
            "[`include/MasterControl/MasterControlModels.h`](../../include/MasterControl/MasterControlModels.h).",
            "",
            "---",
            "",
            "## Forsetti module catalog",
            "",
            "Every module is a JSON manifest under",
            "`src/MasterControlModules/Resources/ForsettiManifests/` and is",
            "registered into the runtime at startup. The current catalog:",
            "",
            "| Module ID | Display name | Type | Version | Platforms |",
            "| --- | --- | --- | --- | --- |",
            module_rows,
            "",
            "---",
            "",
            "## Request flow — admin API call",
            "",
            "```mermaid",
            "sequenceDiagram",
            "    autonumber",
            "    participant U as Operator",
            "    participant S as Shell / Browser",
            "    participant H as HTTP Listener",
            "    participant R as Runtime Service",
            "    participant A as Activity Ring",
            "    participant F as Forsetti Module",
            "",
            "    U->>S: Click action",
            "    S->>H: HTTP request (127.0.0.1:7300)",
            "    H->>R: dispatch(method, path, body)",
            "    R->>F: invoke service implementation",
            "    F-->>R: result",
            "    R-->>H: response body + status",
            "    H->>A: append ActivityEvent(id, ts, method, target, status, latency)",
            "    H-->>S: response",
            "    S-->>U: UI update",
            "```",
            "",
            "Every response — success or failure — is captured by the activity ring before ",
            "being returned. The shell polls `/api/activity?since={id}` to render the live ",
            "command stream the operator sees in the title bar.",
            "",
            "---",
            "",
            "## Platform governance lanes",
            "",
            "CLU routes governance through one lane per target platform, not per host OS:",
            "",
            "| Lane | Backed by | Notes |",
            "| --- | --- | --- |",
            "| **Windows** | Local Forsetti + architecture validation | Runs in-process on the host |",
            "| **macOS** | Apple host (SSH or companion service) | Routes Xcode/SDK/notarization through the Apple Execution Fabric |",
            "| **iOS** | Apple host (SSH or companion service) | Same fabric as macOS, plus device control / simulator readiness |",
            "",
            "Apple operations supported by the fabric:",
            "`build`, `test`, `archive`, `export`, `install`, `sign`, `notarize`, `staple`, `replay`, plus persisted history.",
            "",
            "---",
            "",
            "## Data on disk",
            "",
            "| Path | Contents |",
            "| --- | --- |",
            "| `%ProgramData%\\Master Control Orchestration Server\\` | Configuration, install history, provider credentials (DPAPI), Apple history, exports |",
            "| `share/MasterControlOrchestrationServer/ForsettiManifests/` | Staged module manifests |",
            "| `share/MasterControlOrchestrationServer/web/` | Browser admin UI assets |",
            "| `share/MasterControlOrchestrationServer/clu/` | CLU governance profile |",
            "",
            "A one-shot migration moves the legacy `MasterControlProgram` ProgramData ",
            "directory to the canonical name on first run; the legacy service name is ",
            "preserved for upgrade compatibility.",
            "",
            "---",
            "",
            "See also: [API Reference](API-Reference) · [CLU Governance](CLU-Governance) · ",
            "[Auto-Connect AI](Auto-Connect-AI) · [Operations](Operations)",
            "",
        ]
    )


# ----------------------------------------------------------------------------
# Wiki: API Reference
# ----------------------------------------------------------------------------


def render_api_reference() -> str:
    return "\n".join(
        [
            f"# {PROJECT_NAME} — API Reference",
            "",
            shield("base", "http://127.0.0.1:7300", "00f6ff")
            + " "
            + shield("auth", "loopback only", "0a1018")
            + " "
            + shield("format", "JSON", "00aacc"),
            "",
            "Every route is served by",
            "[`MasterControlRuntime.cpp`](../../src/MasterControlApp/MasterControlRuntime.cpp)",
            "and bound to the loopback interface. The shell, browser admin UI, and the",
            "feature-test harness all consume the same routes — there is no second API.",
            "",
            "---",
            "",
            "## Read endpoints",
            "",
            "| Method | Route | Returns |",
            "| --- | --- | --- |",
            "| `GET` | `/api/health` | Service health and readiness snapshot |",
            "| `GET` | `/api/dashboard` | Composite payload powering the desktop shell + browser dashboard |",
            "| `GET` | `/api/config` | Current persisted configuration |",
            "| `GET` | `/api/providers` | Provider catalog, credential posture, and assignments |",
            "| `GET` | `/api/exports` | Export inventory + handoff artifacts |",
            "| `GET` | `/api/forsetti/surface` | Forsetti surface model used by both UIs |",
            "| `GET` | `/api/forsetti/modules` | Module catalog metadata |",
            "| `GET` | `/api/install/history` | Install + import execution history |",
            "| `GET` | `/api/beacon` | LAN beacon advertisement payload |",
            "| `GET` | `/api/activity?since={id}` | Activity ring events newer than `id` |",
            "",
            "### Activity event shape",
            "",
            "```json",
            "{",
            '  "highWaterMarkId": 174,',
            '  "events": [',
            "    {",
            '      "id": 174,',
            '      "kind": "api",',
            '      "timestampUtc": "2026-04-11T17:42:13.812Z",',
            '      "method": "POST",',
            '      "target": "/api/providers/auto-connect",',
            '      "statusCode": 200,',
            '      "latencyMs": 184,',
            '      "summary": "auto-connect openai succeeded"',
            "    }",
            "  ]",
            "}",
            "```",
            "",
            "The ring buffer holds the last **512** events with monotonically",
            "increasing IDs. The shell polls every two seconds while focused.",
            "",
            "---",
            "",
            "## Auto-Connect AI",
            "",
            "| Method | Route | Purpose |",
            "| --- | --- | --- |",
            "| `POST` | `/api/providers/auto-connect` | Add an AI provider end-to-end (capability resolution → discovery → DPAPI → assignment) |",
            "",
            "**Request body:**",
            "",
            "```json",
            "{",
            '  "kind": "openai",',
            '  "credentials": { "api_key": "sk-..." },',
            '  "assignmentTargetIds": ["planner", "coder"],',
            '  "discoverModels": true',
            "}",
            "```",
            "",
            "**Response body** (`AutoConnectResult`):",
            "",
            "```json",
            "{",
            '  "succeeded": true,',
            '  "providerId": "openai-7f3a",',
            '  "summary": "Auto-Connect completed in 184 ms",',
            '  "totalLatencyMs": 184,',
            '  "steps": [',
            '    { "name": "Resolve capability", "ok": true, "latencyMs": 1 },',
            '    { "name": "Generate provider id", "ok": true, "latencyMs": 0 },',
            '    { "name": "Probe remote endpoint", "ok": true, "latencyMs": 142 },',
            '    { "name": "Discover models", "ok": true, "latencyMs": 36 },',
            '    { "name": "Persist credentials (DPAPI)", "ok": true, "latencyMs": 2 },',
            '    { "name": "Register provider", "ok": true, "latencyMs": 1 },',
            '    { "name": "Apply role assignments", "ok": true, "latencyMs": 2 }',
            "  ],",
            '  "discoveredModels": [',
            '    { "id": "gpt-4o", "displayName": "GPT-4o", "selected": true }',
            "  ]",
            "}",
            "```",
            "",
            "See [Auto-Connect AI](Auto-Connect-AI) for the full pipeline walkthrough.",
            "",
            "---",
            "",
            "## CLU & governance",
            "",
            "| Method | Route | Purpose |",
            "| --- | --- | --- |",
            "| `GET` | `/api/clu` | CLU posture, findings, and current governance state |",
            "| `GET` | `/api/clu/tools` | Published governance tool descriptors |",
            "| `GET` | `/api/clu/apple-operations` | Apple job queue + history |",
            "| `POST` | `/api/clu/execute` | Execute a CLU governance operation |",
            "| `POST` | `/api/clu/apple-operations/cancel` | Cancel a queued Apple operation |",
            "",
            "All endpoints are backed by the `CommandLogicUnitModule` Forsetti service module ",
            "(`com.mastercontrol.command-logic-unit`).",
            "",
            "---",
            "",
            "## Platform services",
            "",
            "| Method | Route | Purpose |",
            "| --- | --- | --- |",
            "| `GET` | `/api/platform-services` | Combined gateway + governance + host inventory |",
            "| `GET` | `/api/platform-services/gateways` | Platform gateway summary |",
            "| `GET` | `/api/platform-services/governance` | Platform governance lane summary |",
            "| `GET` | `/api/platform-services/apple-hosts` | Registered Apple remote hosts + readiness |",
            "| `POST` | `/api/platform-services/apple-hosts` | Add or update an Apple host |",
            "| `POST` | `/api/platform-services/apple-hosts/remove` | Remove an Apple host |",
            "| `GET` | `/api/platform-services/config/{platform}` | Platform-specific client configuration |",
            "| `GET` | `/mcp/gateway/{platform}` | Gateway document for `windows` / `macos` / `ios` |",
            "| `GET` | `/mcp/governance/{platform}` | Governance document for the same set |",
            "| `POST` | `/mcp/governance/{platform}` | Execute a platform governance tool call |",
            "",
            "---",
            "",
            "## Runtime inventory mutation",
            "",
            "All mutating routes refresh inventory **asynchronously** via",
            "`IRuntimeInventoryService::refreshAsync()` so admin calls return immediately",
            "instead of blocking on the 35+ endpoint TCP probe loop.",
            "",
            "| Method | Route | Purpose |",
            "| --- | --- | --- |",
            "| `POST` | `/api/runtime/mcp-servers` | Create or update a custom MCP server |",
            "| `POST` | `/api/runtime/mcp-servers/remove` | Remove a custom MCP server |",
            "| `POST` | `/api/runtime/subagents` | Create or update a sub-agent |",
            "| `POST` | `/api/runtime/subagents/remove` | Remove a sub-agent |",
            "| `POST` | `/api/providers/groups` | Create or update a provider group |",
            "| `POST` | `/api/providers/groups/remove` | Remove a provider group |",
            "| `POST` | `/api/providers/assignments` | Apply provider routing assignments |",
            "| `POST` | `/api/providers/credentials` | Save provider credential material (DPAPI) |",
            "| `POST` | `/api/providers/execute` | Execute a provider-backed request |",
            "",
            "---",
            "",
            "## Install & import",
            "",
            "| Method | Route | Purpose |",
            "| --- | --- | --- |",
            "| `POST` | `/api/install/package` | Import or deploy a package artifact |",
            "| `POST` | `/api/install/repo` | Import from a Git or bootstrap repository |",
            "| `POST` | `/api/install/zip` | Import from a zip bundle |",
            "",
            "---",
            "",
            "## Operation result envelope",
            "",
            "Every mutating route returns a uniform `OperationResult`:",
            "",
            "```json",
            "{",
            '  "succeeded": true,',
            '  "summary": "MCP server feature-test-mcp upserted",',
            '  "errorMessage": "",',
            '  "warnings": []',
            "}",
            "```",
            "",
            "On failure, `succeeded` is `false`, `errorMessage` is populated, and the HTTP ",
            "status reflects the failure category (400 / 404 / 409 / 500).",
            "",
            "---",
            "",
            "See also: [Architecture](Architecture) · [Auto-Connect AI](Auto-Connect-AI) · ",
            "[Telemetry & Activity](Telemetry-and-Activity)",
            "",
        ]
    )


# ----------------------------------------------------------------------------
# Wiki: Auto-Connect AI
# ----------------------------------------------------------------------------


def render_auto_connect() -> str:
    return "\n".join(
        [
            f"# {PROJECT_NAME} — Auto-Connect AI",
            "",
            shield("status", "production", "00f6ff")
            + " "
            + shield("stages", "7", "00aacc")
            + " "
            + shield("encryption", "DPAPI", "5a00e8"),
            "",
            "**Goal:** the operator types credentials, picks roles, and clicks Connect. ",
            "Every other step — capability lookup, ID generation, model discovery, encryption, ",
            "registration, role fan-out — happens automatically inside the runtime.",
            "",
            "---",
            "",
            "## End-to-end pipeline",
            "",
            "```mermaid",
            "flowchart LR",
            "    classDef accent fill:#031018,stroke:#00F6FF,color:#E6FCFF;",
            "    classDef ok fill:#031018,stroke:#1CF2C1,color:#E6FCFF;",
            "    classDef warn fill:#031018,stroke:#FFC857,color:#E6FCFF;",
            "",
            "    UI[Auto-Connect card]:::accent --> POST[POST /api/providers/auto-connect]:::accent",
            "    POST --> S1[1. Resolve capability]:::ok",
            "    S1 --> S2[2. Generate provider id]:::ok",
            "    S2 --> S3[3. Probe remote endpoint]:::ok",
            "    S3 --> S4[4. Discover models]:::ok",
            "    S4 --> S5[5. Persist credentials DPAPI]:::ok",
            "    S5 --> S6[6. Register provider]:::ok",
            "    S6 --> S7[7. Apply role assignments]:::ok",
            "    S7 --> Result[(AutoConnectResult)]:::accent",
            "```",
            "",
            "Each stage is timed individually and reported back in the response so the UI ",
            "can render a transparent progress log. If any stage fails, the pipeline aborts ",
            "and returns the partial step list with the failing stage flagged.",
            "",
            "---",
            "",
            "## Stage detail",
            "",
            "### 1. Resolve capability",
            "Looks up the matching `ProviderCapabilityDescriptor` for the supplied `kind`. ",
            "Capabilities declare credential field names, the recommended model, and the base URL.",
            "",
            "### 2. Generate provider id",
            "Builds a stable, unique provider ID by combining the kind with a short hash of the credential payload.",
            "",
            "### 3. Probe remote endpoint",
            "Issues a real HTTP request through WinHTTP using the provided credentials. ",
            "401/403 are surfaced as friendly errors; 200 advances the pipeline.",
            "",
            "### 4. Discover models",
            "Calls the provider's models endpoint (e.g. `GET /v1/models` for OpenAI-compatible APIs) ",
            "and prefers `capability.recommendedModel`. The bearer auth helper is module-agnostic — ",
            "any credential field whose ID matches `api_key` / `apikey` / `token` / `secret` is used.",
            "",
            "### 5. Persist credentials (DPAPI)",
            "Encrypts the credential bundle with `CryptProtectData` and writes it to the credential store. ",
            "Only the current Windows user account can decrypt it; the credential never leaves the host in plaintext.",
            "",
            "### 6. Register provider",
            "Inserts the provider into the runtime registry, exposing it through `/api/providers` and `/api/dashboard`.",
            "",
            "### 7. Apply role assignments",
            "If the request includes `assignmentTargetIds`, the provider is bound to those roles via ",
            "`IProviderAssignmentService::upsertAssignment` so CLU can route work to it immediately.",
            "",
            "---",
            "",
            "## Calling it directly",
            "",
            "```bash",
            "curl -X POST http://127.0.0.1:7300/api/providers/auto-connect \\",
            '  -H "Content-Type: application/json" \\',
            "  -d '{",
            '    \"kind\": \"openai\",',
            '    \"credentials\": { \"api_key\": \"sk-...\" },',
            '    \"assignmentTargetIds\": [\"planner\", \"coder\"],',
            '    \"discoverModels\": true',
            "  }'",
            "```",
            "",
            "Successful response:",
            "",
            "```json",
            "{",
            '  "succeeded": true,',
            '  "providerId": "openai-7f3a",',
            '  "summary": "Auto-Connect completed in 184 ms",',
            '  "totalLatencyMs": 184,',
            '  "steps": [ ... ],',
            '  "discoveredModels": [ { "id": "gpt-4o", "selected": true } ]',
            "}",
            "```",
            "",
            "Failure (bad credentials):",
            "",
            "```json",
            "{",
            '  "succeeded": false,',
            '  "providerId": "",',
            '  "summary": "Probe failed: HTTP 401 from https://api.openai.com/v1/models",',
            '  "errorMessage": "HTTP 401 unauthorized",',
            '  "steps": [',
            '    { "name": "Resolve capability", "ok": true },',
            '    { "name": "Generate provider id", "ok": true },',
            '    { "name": "Probe remote endpoint", "ok": false, "errorMessage": "HTTP 401" }',
            "  ]",
            "}",
            "```",
            "",
            "---",
            "",
            "## Where it lives in the source",
            "",
            "| Concern | File |",
            "| --- | --- |",
            "| Models / contracts | `include/MasterControl/MasterControlModels.h` |",
            "| Service interface | `include/MasterControl/MasterControlContracts.h` (`IProviderRegistry::autoConnectProvider`) |",
            "| Implementation | `src/MasterControlApp/MasterControlRuntime.cpp` (`ProviderRegistryService::autoConnectProvider`) |",
            "| HTTP route | `src/MasterControlApp/MasterControlRuntime.cpp` (`/api/providers/auto-connect`) |",
            "| Shell UI | `src/MasterControlShell/ProvidersSectionControl.xaml{,.cpp,.h}` |",
            "",
            "---",
            "",
            "See also: [API Reference](API-Reference) · [CLU Governance](CLU-Governance) · ",
            "[Architecture](Architecture)",
            "",
        ]
    )


# ----------------------------------------------------------------------------
# Wiki: CLU Governance
# ----------------------------------------------------------------------------


def render_clu_governance() -> str:
    return "\n".join(
        [
            f"# {PROJECT_NAME} — CLU Governance",
            "",
            shield("module", "com.mastercontrol.command-logic-unit", "00f6ff"),
            "",
            "**Command Logic Unit** (CLU) is a first-class Forsetti **service module**, not a UI tab. ",
            "It owns governance posture, rule evaluation, role routing, Apple operations, and ",
            "platform-governance execution. The shell and browser admin UI both *read* CLU state ",
            "from the runtime — they never duplicate governance logic locally.",
            "",
            "---",
            "",
            "## What CLU is responsible for",
            "",
            "```mermaid",
            "mindmap",
            "  root((CLU))",
            "    Posture",
            "      Findings",
            "      Health",
            "      Roles",
            "    Rules",
            "      Resource limits",
            "      Action allow/deny",
            "      Provider routing",
            "    Apple operations",
            "      Build / test / archive",
            "      Sign / notarize / staple",
            "      Replay / history",
            "    Platform governance",
            "      Windows lane",
            "      macOS lane",
            "      iOS lane",
            "```",
            "",
            "---",
            "",
            "## Module identity",
            "",
            "| | |",
            "| --- | --- |",
            "| **Module ID** | `com.mastercontrol.command-logic-unit` |",
            "| **Manifest** | `src/MasterControlModules/Resources/ForsettiManifests/CommandLogicUnitModule.json` |",
            "| **Profile** | `resources/clu/governance-profile.json` |",
            "| **Service interface** | `ICommandLogicUnitService` in `include/MasterControl/MasterControlContracts.h` |",
            "| **Implementation** | `CommandLogicUnitService` in `src/MasterControlApp/MasterControlRuntime.cpp` |",
            "",
            "---",
            "",
            "## API surface",
            "",
            "| Method | Route | Purpose |",
            "| --- | --- | --- |",
            "| `GET` | `/api/clu` | Posture + findings + roles + rules |",
            "| `GET` | `/api/clu/tools` | Governance tool descriptors |",
            "| `GET` | `/api/clu/apple-operations` | Apple queue + history |",
            "| `POST` | `/api/clu/execute` | Execute a governance tool |",
            "| `POST` | `/api/clu/apple-operations/cancel` | Cancel a queued Apple operation |",
            "",
            "**Tool execution request:**",
            "",
            "```json",
            "{",
            '  "toolId": "windows.architecture.validate",',
            '  "moduleId": "com.mastercontrol.command-logic-unit",',
            '  "parameters": { "scope": "current" }',
            "}",
            "```",
            "",
            "**Tool execution result:**",
            "",
            "```json",
            "{",
            '  "succeeded": true,',
            '  "toolId": "windows.architecture.validate",',
            '  "summary": "validation passed",',
            '  "findings": [],',
            '  "logs": [ ... ],',
            '  "latencyMs": 142',
            "}",
            "```",
            "",
            "---",
            "",
            "## Role routing",
            "",
            "CLU is the broker for provider responsibility routing. Operators assign roles ",
            "(e.g. `planner`, `coder`, `reviewer`, `recon`, `scribe`) to providers via Auto-Connect ",
            "or directly through `/api/providers/assignments`. CLU then forwards requests for those ",
            "roles to the assigned provider.",
            "",
            "| Role | Typical model | Notes |",
            "| --- | --- | --- |",
            "| `planner` | High-context reasoning model | Long-form planning, architecture decisions |",
            "| `coder` | Code-tuned model | Implementation work |",
            "| `reviewer` | Slower, careful model | Diff review, security scanning |",
            "| `scribe` | Cheaper, fast model | Documentation, summaries |",
            "| `recon` | Search/embedding-friendly model | Codebase exploration |",
            "",
            "---",
            "",
            "## Platform governance lanes",
            "",
            "CLU runs governance per **target platform**, not per host OS:",
            "",
            "| Lane | Backed by | Tooling |",
            "| --- | --- | --- |",
            "| Windows | Local Forsetti + architecture validators | In-process |",
            "| macOS | Apple host (SSH or companion service) | Xcode, notarization, signing |",
            "| iOS | Apple host (same fabric as macOS) | Adds device control + simulator readiness |",
            "",
            "Apple operations supported:",
            "`build`, `test`, `archive`, `export`, `install`, `sign`, `notarize`, `staple`, `replay`, plus persisted history.",
            "",
            "---",
            "",
            "## Why it's a service module",
            "",
            "Forsetti rules forbid duplicate UI shells. CLU is a *service* — it publishes state ",
            "and accepts commands through the runtime. The WinUI shell and browser dashboard are ",
            "the only UI hosts; both read CLU posture from `/api/clu`. This prevents governance ",
            "drift between surfaces.",
            "",
            "---",
            "",
            "See also: [Architecture](Architecture) · [Auto-Connect AI](Auto-Connect-AI) · ",
            "[API Reference](API-Reference) · [Sub-Agents](Sub-Agents)",
            "",
        ]
    )


# ----------------------------------------------------------------------------
# Wiki: Telemetry & Activity
# ----------------------------------------------------------------------------


def render_telemetry() -> str:
    return "\n".join(
        [
            f"# {PROJECT_NAME} — Telemetry & Activity",
            "",
            shield("ring size", "512 events", "00f6ff")
            + " "
            + shield("polling", "2s", "00aacc")
            + " "
            + shield("monotonic ids", "yes", "1cf2c1"),
            "",
            "Two parallel data streams power the operator surfaces:",
            "",
            "1. **Host telemetry** — CPU, memory, disk, hostname, uptime, sampled by the runtime.",
            "2. **Activity ring** — every admin API request, captured at the dispatch layer.",
            "",
            "---",
            "",
            "## Host telemetry",
            "",
            "Telemetry is captured by the `HostTelemetryModule` (Forsetti) and exposed via ",
            "`/api/dashboard` under the `telemetry` field.",
            "",
            "**Sample payload:**",
            "",
            "```json",
            "{",
            '  "telemetry": {',
            '    "hostName": "WEBSERVER",',
            '    "cpuPercent": 12.4,',
            '    "memoryPercent": 38.1,',
            '    "diskPercent": 41.7,',
            '    "uptimeSeconds": 184321',
            "  }",
            "}",
            "```",
            "",
            "Both UIs animate value transitions instead of snapping, and the shell title bar ",
            "shows a live `HH:MM:SS` clock driven by `DispatcherQueueTimer` so the operator can ",
            "tell at a glance whether the surface is alive.",
            "",
            "---",
            "",
            "## Activity ring buffer",
            "",
            "```mermaid",
            "flowchart LR",
            "    classDef accent fill:#031018,stroke:#00F6FF,color:#E6FCFF;",
            "",
            "    Request[Incoming HTTP request]:::accent --> Dispatch[HTTP dispatch wrapper]:::accent",
            "    Dispatch --> Handler[Service handler]:::accent",
            "    Handler --> Response[Response]:::accent",
            "    Response --> Append[append ActivityEvent]:::accent",
            "    Append --> Ring[(512-slot ring)]:::accent",
            "    Ring --> Poll[GET /api/activity?since=N]:::accent",
            "    Poll --> Stream[Live operator stream]:::accent",
            "```",
            "",
            "### Properties",
            "",
            "| Property | Value |",
            "| --- | --- |",
            "| Capacity | 512 events |",
            "| ID assignment | Monotonically increasing, per-process |",
            "| Thread safety | Mutex-guarded append, lock-free read of high water mark |",
            "| Eviction | Oldest event drops when capacity is reached |",
            "| Polling | Shell + browser request `since={lastId}` every two seconds when focused |",
            "",
            "### Event shape",
            "",
            "```json",
            "{",
            '  "id": 174,',
            '  "kind": "api",',
            '  "timestampUtc": "2026-04-11T17:42:13.812Z",',
            '  "method": "POST",',
            '  "target": "/api/providers/auto-connect",',
            '  "statusCode": 200,',
            '  "latencyMs": 184,',
            '  "summary": "auto-connect openai succeeded"',
            "}",
            "```",
            "",
            "### Color codes in the live stream",
            "",
            "| Status class | Stream color |",
            "| --- | --- |",
            "| `2xx` | Cyan accent (`#00F6FF`) |",
            "| `3xx` | Muted cyan |",
            "| `4xx` | Warning amber (`#FFC857`) |",
            "| `5xx` | Danger red (`#FF6A80`) |",
            "",
            "### Code references",
            "",
            "| Concern | File |",
            "| --- | --- |",
            "| Ring buffer | `ActivityEventRing` in `src/MasterControlApp/MasterControlRuntime.cpp` |",
            "| Append wrapper | HTTP dispatch lambda in same file |",
            "| Route | `/api/activity` handler in same file |",
            "| Shell consumer | `MainWindow.xaml.cpp::PollActivityStreamAsync` |",
            "",
            "---",
            "",
            "See also: [Architecture](Architecture) · [API Reference](API-Reference) · ",
            "[Tron UI Theme](Tron-UI-Theme)",
            "",
        ]
    )


# ----------------------------------------------------------------------------
# Wiki: Tron UI Theme
# ----------------------------------------------------------------------------


def render_tron_theme() -> str:
    return "\n".join(
        [
            f"# {PROJECT_NAME} — Tron UI Theme",
            "",
            shield("accent", "#00F6FF", "00f6ff")
            + " "
            + shield("type", "Bahnschrift SemiCondensed", "031018")
            + " "
            + shield("corner radius", "0", "00aacc"),
            "",
            "The product is intentionally Tron-styled: cyan-on-blue-black, hard edges, ",
            "Bahnschrift type, and motion as feedback rather than decoration. The shell and ",
            "the browser admin UI share the same palette so a screenshot of one looks like a ",
            "screenshot of the other.",
            "",
            "---",
            "",
            "## Palette",
            "",
            "| Token | Hex | Use |",
            "| --- | --- | --- |",
            "| `ShellAccentBrush` | `#00F6FF` | Primary accent — borders, active states, CTAs |",
            "| `ShellAccentBrightBrush` | `#7DFFFF` | Hover and pressed accent |",
            "| `ShellHotGlowBrush` | `#2200F6FF` | Diffuse glow under hot elements |",
            "| `TextPrimaryBrush` | `#E6FCFF` | Body and label text |",
            "| `TextMutedBrush` | `#8CB7C4` | Secondary text |",
            "| `TextFaintBrush` | `#6A8B95` | Disabled / placeholder |",
            "| `SuccessBrush` | `#1CF2C1` | OK states |",
            "| `WarningBrush` | `#FFC857` | Caution states, 4xx events |",
            "| `DangerBrush` | `#FF6A80` | Failure, 5xx events |",
            "| `PanelFillBrush` | `#E0060A10` | Section panels |",
            "| `CardFillBrush` | `#E00A1018` | Cards inside panels |",
            "| `CardEdgeBrush` | `#5A00E8FF` | Card outlines |",
            "",
            "**Background gradient:** `#010205 → #031018 → #041C2A → #060A10`, top to bottom. ",
            "Above it sits a 42px × 42px cyan grid at 8% opacity, mask-faded toward the bottom.",
            "",
            "---",
            "",
            "## Typography",
            "",
            "| Style | Font | Size | Tracking |",
            "| --- | --- | --- | --- |",
            "| Hero | Bahnschrift SemiCondensed | 28pt | normal |",
            "| Section header | Bahnschrift SemiCondensed | 17pt | 80% |",
            "| Eyebrow | Bahnschrift SemiCondensed | 12pt | 160% |",
            "| Body | Bahnschrift SemiCondensed | 14pt | normal |",
            "| Data | Consolas | 13pt | normal |",
            "",
            "---",
            "",
            "## Hard rules",
            "",
            "- **No corner radii.** Every `CornerRadius` in the shell and every `border-radius` in CSS is `0`. The product reads as Tron, not Fluent.",
            "- **No oval shapes.** No pill buttons, no circular avatars. If something is round, it gets rebuilt.",
            "- **Cyan border = interactive.** Anything the operator can act on has the accent border. Static text does not.",
            "- **Motion ≠ decoration.** Animations exist to confirm that an action happened or that the surface is alive (live clock, accent pulse, focus outline). Pure decoration is forbidden.",
            "- **Honor `prefers-reduced-motion`.** All motion is opt-out.",
            "",
            "---",
            "",
            "## Shell consumption",
            "",
            "All tokens live in [`src/MasterControlShell/App.xaml`](../../src/MasterControlShell/App.xaml). ",
            "Implicit `Style TargetType` entries override the Fluent defaults for `TextBlock`, ",
            "`Button`, `ToggleSwitch`, `CheckBox`, `RadioButton`, `ComboBoxItem`, and `ListViewItem`, ",
            "and Fluent theme brushes (`TextFillColorPrimaryBrush`, `ControlFillColorDefaultBrush`, ",
            "`AccentFillColorDefaultBrush`, etc.) are remapped to the Tron palette so any ",
            "unstyled control still renders correctly.",
            "",
            "## Browser consumption",
            "",
            "[`resources/web/styles.css`](../../resources/web/styles.css) mirrors the same ",
            "tokens as CSS variables and adds a polish layer: focus-visible outlines, accent ",
            "pulse animation, `<dialog>::backdrop` blur, and a reduced-motion media query that ",
            "disables every animation when the operator prefers it.",
            "",
            "---",
            "",
            "See also: [Architecture](Architecture) · [Telemetry & Activity](Telemetry-and-Activity) · ",
            "[Operations](Operations)",
            "",
        ]
    )


# ----------------------------------------------------------------------------
# Wiki: Sub-Agents
# ----------------------------------------------------------------------------


def render_sub_agents() -> str:
    return "\n".join(
        [
            f"# {PROJECT_NAME} — Sub-Agents",
            "",
            shield("count", "7", "00f6ff")
            + " "
            + shield("ports", "7201-7207", "00aacc")
            + " "
            + shield("transport", "MCP / SSE", "5a00e8"),
            "",
            "The sub-agent fleet is a set of seven specialized Node services that ride on top ",
            "of the orchestration server. Each one owns a single concern and registers with the ",
            "runtime through `/api/runtime/subagents`.",
            "",
            "---",
            "",
            "## Roster",
            "",
            "| Agent | Port | Specialization |",
            "| --- | --- | --- |",
            "| **SENTINEL** | `7201` | Design validation, import checking, dependency validation, guardrails |",
            "| **ARCHITECT** | `7202` | Project analysis, design review, dependency checks, pattern suggestions |",
            "| **FORGE** | `7203` | Build execution, test running, pipeline management, file watching |",
            "| **SCRIBE** | `7204` | API documentation, code explanation, knowledge search, docs updates |",
            "| **RECON** | `7205` | Diff review, file analysis, pattern finding, security scans, quality reports |",
            "| **NEXUS** | `7206` | Workflow orchestration, task management, agent roster, request aggregation |",
            "| **WATCHTOWER** | `7207` | Health monitoring, agent metrics, alert history, restart capability |",
            "",
            "Combined RAM footprint: ~540 MB total (~77 MB per agent).",
            "",
            "---",
            "",
            "## Transport",
            "",
            "The agents communicate over **MCP** with **SSE** response framing. The shared client ",
            "lives at `D:\\Sub-Agents\\lib\\platform-gateway-client.js` and is reused by every agent.",
            "",
            "Critical implementation notes (these are easy to get wrong):",
            "",
            "- The `Accept` header **must** include both `application/json` and `text/event-stream`.",
            "- After the MCP `init` handshake, the client **must** send `notifications/initialized`.",
            "- Responses are streamed as `text/event-stream`, not single JSON bodies — parse SSE frames.",
            "",
            "---",
            "",
            "## Lifecycle",
            "",
            "```powershell",
            "# Start the fleet",
            "powershell -NoProfile -ExecutionPolicy Bypass -File D:\\Sub-Agents\\Start-SubAgents.ps1",
            "",
            "# Stop the fleet",
            "powershell -NoProfile -ExecutionPolicy Bypass -File D:\\Sub-Agents\\Stop-SubAgents.ps1",
            "```",
            "",
            "Each agent registers with the orchestration server's agent-communication endpoint, ",
            "and `WATCHTOWER` aggregates the rest via `/api/sub-agents`.",
            "",
            "---",
            "",
            "## Registration via the runtime",
            "",
            "```bash",
            "curl -X POST http://127.0.0.1:7300/api/runtime/subagents \\",
            '  -H "Content-Type: application/json" \\',
            "  -d '{",
            '    \"id\": \"sentinel\",',
            '    \"displayName\": \"SENTINEL\",',
            '    \"kind\": \"sub_agent\",',
            '    \"host\": \"127.0.0.1\",',
            '    \"port\": 7201,',
            '    \"protocol\": \"http\",',
            '    \"specialization\": \"guardrails\",',
            '    \"userDefined\": false',
            "  }'",
            "```",
            "",
            "---",
            "",
            "See also: [Architecture](Architecture) · [API Reference](API-Reference) · ",
            "[CLU Governance](CLU-Governance)",
            "",
        ]
    )


# ----------------------------------------------------------------------------
# Wiki: Operations
# ----------------------------------------------------------------------------


def render_operations() -> str:
    return "\n".join(
        [
            f"# {PROJECT_NAME} — Operations",
            "",
            shield("toolchain", "VS2026 + MSVC v145", "00f6ff")
            + " "
            + shield("build", "CMake + MSBuild", "00aacc")
            + " "
            + shield("validated", "Win11 + Server 2022", "1cf2c1"),
            "",
            "Build, validate, package, install, upgrade, repair, and uninstall the product ",
            "from the repository-owned tooling. Every operation listed here is also exercised ",
            "by the deployment acceptance harness in `scripts/`.",
            "",
            "---",
            "",
            "## Local build & validation",
            "",
            "```powershell",
            "# Configure",
            "cmake --preset debug",
            "",
            "# Build",
            "cmake --build build\\debug --config Debug",
            "",
            "# Test",
            "ctest --test-dir build\\debug -C Debug --output-on-failure",
            "",
            "# Forsetti compliance check",
            "powershell -NoProfile -ExecutionPolicy Bypass -File scripts\\check-mastercontrol-forsetti.ps1",
            "```",
            "",
            "---",
            "",
            "## Staging & packaging",
            "",
            "```powershell",
            "# Stage installable payload",
            "cmake --install build\\debug --config Debug --prefix dist\\debug",
            "",
            "# Build a release package (MSI-first bundle + zip)",
            "powershell -NoProfile -ExecutionPolicy Bypass -File scripts\\Package-MasterControlOrchestrationServer.ps1 -Preset release",
            "```",
            "",
            "Output is dropped under `dist\\packages\\release\\` and includes the MSI, the ZIP, the",
            "bundle directory, the bootstrapper, the service host, the WinUI shell, the staged",
            "Forsetti manifests, and the browser dashboard assets.",
            "",
            "---",
            "",
            "## Install entry points",
            "",
            "| Entry point | When to use |",
            "| --- | --- |",
            "| **`MasterControlOrchestrationServer-<version>-win-x64.msi`** | Standard interactive Windows install. This is the supported operator-facing installer. |",
            "| **`START-HERE.txt` / `INSTALL.txt`** | Packaged operator guidance for MSI-first and silent-install flows. |",
            "| **`MasterControlBootstrapper.exe`** | Lifecycle engine — exposes `preflight`, `install`, `validate`, `upgrade`, `repair`, `uninstall` subcommands. |",
            "",
            "### Lifecycle subcommands",
            "",
            "```powershell",
            "MasterControlBootstrapper.exe preflight",
            "MasterControlBootstrapper.exe install   --source dist\\packages\\release\\<bundle-name>",
            "MasterControlBootstrapper.exe validate",
            "MasterControlBootstrapper.exe upgrade   --source dist\\packages\\release\\<bundle-name>",
            "MasterControlBootstrapper.exe repair",
            "MasterControlBootstrapper.exe uninstall --purge-data:false",
            "```",
            "",
            "---",
            "",
            "## Deployment scripts",
            "",
            "| Script | Purpose |",
            "| --- | --- |",
            "| `Build-MasterControlOrchestrationServer.ps1` | Configure → build → test → stage local artifacts |",
            "| `Test-MasterControlOrchestrationServerDeployment.ps1` | Acceptance harness for install / validate / upgrade / repair / uninstall |",
            "| `Compare-MasterControlOrchestrationServerDeploymentReports.ps1` | Diff acceptance reports across hosts |",
            "| `Invoke-MasterControlOrchestrationServerDeploymentMatrix.ps1` | Drive labelled deployment-matrix runs |",
            "| `Get-MasterControlOrchestrationServerReleaseReadiness.ps1` | Build a release-readiness markdown report |",
            "",
            "---",
            "",
            "## Installed runtime surfaces",
            "",
            "| Surface | Default location |",
            "| --- | --- |",
            "| Windows service host | `MasterControlServiceHost.exe` (registered as `MasterControlProgram` for upgrade compat) |",
            "| Desktop shell | `MasterControlShell.exe` |",
            "| Browser admin UI | `http://127.0.0.1:7300/` |",
            "| ProgramData | `%ProgramData%\\Master Control Orchestration Server\\` |",
            "",
            "On first run, a one-shot migration moves any legacy ",
            "`%ProgramData%\\MasterControlProgram\\` data to the canonical path with a safe ",
            "fallback if the move cannot complete.",
            "",
            "---",
            "",
            "## Standard operator flow",
            "",
            "```mermaid",
            "flowchart LR",
            "    classDef accent fill:#031018,stroke:#00F6FF,color:#E6FCFF;",
            "    A[1. Build]:::accent --> B[2. Package]:::accent --> C[3. Install]:::accent",
            "    C --> D[4. Verify shell + browser + service]:::accent",
            "    D --> E[5. Run deployment acceptance]:::accent",
            "    E --> F[6. Tag release]:::accent",
            "```",
            "",
            "---",
            "",
            "## Compatibility notes",
            "",
            "- Public product name: **Master Control Orchestration Server**.",
            "- Legacy Windows service name `MasterControlProgram` is preserved for upgrades.",
            "- Legacy uninstall registry key `...\\Uninstall\\MasterControlProgram` is preserved for upgrades.",
            "- Toolchain is **MSVC v145** (Visual Studio 2026 / VS18); v143 is not supported.",
            "",
            "---",
            "",
            "See also: [Infrastructure](Infrastructure) · [Automation](Automation) · ",
            "[Versions](Versions) · [Troubleshooting](Troubleshooting)",
            "",
        ]
    )


# ----------------------------------------------------------------------------
# Wiki: Infrastructure
# ----------------------------------------------------------------------------


def render_infrastructure() -> str:
    return "\n".join(
        [
            f"# {PROJECT_NAME} — Infrastructure",
            "",
            shield("targets", "Win11 / Server 2022", "00f6ff")
            + " "
            + shield("install size", "~44 MB", "00aacc")
            + " "
            + shield("scope", "single host", "031018"),
            "",
            "The product is **single-host** by design. There are no cluster components, no remote ",
            "control plane, no cloud dependencies. One Windows machine runs the service, the shell, ",
            "and the browser admin UI; remote operator access is via the loopback admin API tunneled ",
            "through whatever transport the operator already trusts (RDP, SSH port forward, etc.).",
            "",
            "---",
            "",
            "## Target hosts",
            "",
            "| Host | Status |",
            "| --- | --- |",
            "| Windows 11 (22H2+) | ✅ supported |",
            "| Windows Server 2022 Datacenter (Desktop Experience) | ✅ supported, end-to-end validated |",
            "| Windows Server Core | ❌ unsupported (XAML Islands required) |",
            "| Windows 10 | ⚠ untested; may work with Windows App SDK 1.5 prerequisites |",
            "",
            "---",
            "",
            "## Deployment shape",
            "",
            "```mermaid",
            "flowchart LR",
            "    classDef accent fill:#031018,stroke:#00F6FF,color:#E6FCFF;",
            "",
            "    subgraph Host[Windows host]",
            "        Service[Service host<br/>:7300 loopback]:::accent",
            "        Shell[WinUI shell]:::accent",
            "        Browser[Browser admin UI]:::accent",
            "        Data[(ProgramData)]:::accent",
            "    end",
            "",
            "    Operator((Operator)) --> Shell",
            "    Operator --> Browser",
            "    Shell --> Service",
            "    Browser --> Service",
            "    Service --> Data",
            "```",
            "",
            "---",
            "",
            "## Packaging model",
            "",
            "| Layer | Contents |",
            "| --- | --- |",
            "| Setup launcher | Tron-themed UI, elevation, payload extraction, bootstrapper invocation |",
            "| Bootstrapper | Lifecycle engine: preflight, install, validate, upgrade, repair, uninstall |",
            "| Service host | The orchestration runtime, registered as a Windows service |",
            "| Shell | WinUI 3 desktop UI, runs in the operator session |",
            "| Browser assets | Static HTML/CSS/JS served by the runtime |",
            "| Forsetti manifests | Module catalog under `share/MasterControlOrchestrationServer/ForsettiManifests/` |",
            "| CLU profile | Governance defaults under `share/MasterControlOrchestrationServer/clu/` |",
            "",
            "---",
            "",
            "## Validation focus",
            "",
            "Current external validation gap is automated upgrade-from-legacy on Server Core. ",
            "All other lifecycle flows are exercised by the deployment acceptance harness.",
            "",
            "---",
            "",
            "See also: [Operations](Operations) · [Architecture](Architecture) · ",
            "[Remote Client](Remote-Client)",
            "",
        ]
    )


# ----------------------------------------------------------------------------
# Wiki: Remote Client
# ----------------------------------------------------------------------------


def render_remote_client() -> str:
    return "\n".join(
        [
            f"# {PROJECT_NAME} — Remote Client",
            "",
            "Remote operator and remote agent onboarding direction. The product is single-host, ",
            "but agents (Codex, Claude Code, custom MCP servers) routinely connect from other ",
            "processes on the same machine — and occasionally over a tunnel from another host.",
            "",
            "---",
            "",
            "## Onboarding directions",
            "",
            "### Codex / Claude Code (local)",
            "",
            "1. Launch the orchestration server.",
            "2. Open the desktop shell, navigate to **Providers**.",
            "3. Use **Auto-Connect** with the appropriate credential.",
            "4. Assign roles (`planner`, `coder`, etc.) — CLU routes from there.",
            "",
            "### Custom MCP server",
            "",
            "1. Stand up the MCP server on a loopback port.",
            "2. POST to `/api/runtime/mcp-servers` with the endpoint definition.",
            "3. The runtime probes the endpoint asynchronously and exposes it through `/api/dashboard`.",
            "",
            "### Remote operator over tunnel",
            "",
            "The admin API binds to `127.0.0.1:7300`. To reach it from another host, use a trusted ",
            "transport — `ssh -L 7300:127.0.0.1:7300 user@host` is the canonical pattern. The product ",
            "intentionally does not expose the API on an external interface.",
            "",
            "---",
            "",
            "## Discovery",
            "",
            "The Beacon module advertises the runtime on the local network for trusted clients. ",
            "Beacon state is exposed via `/api/beacon`.",
            "",
            "---",
            "",
            "See also: [Auto-Connect AI](Auto-Connect-AI) · [API Reference](API-Reference) · ",
            "[Architecture](Architecture)",
            "",
        ]
    )


# ----------------------------------------------------------------------------
# Wiki: Automation
# ----------------------------------------------------------------------------


def render_automation(state: dict) -> str:
    release = latest_release(state)
    return "\n".join(
        [
            f"# {PROJECT_NAME} — Automation",
            "",
            shield("agents", "5", "00f6ff")
            + " "
            + shield("trigger", "push to main", "00aacc"),
            "",
            "The repository is maintained by a small fleet of GitHub agents that handle ",
            "versioning, changelog updates, generated docs, wiki synchronization, and ",
            "contributor compliance. The agents live in `scripts/github_agents/`.",
            "",
            "---",
            "",
            "## Pipeline",
            "",
            "```mermaid",
            "flowchart TB",
            "    classDef accent fill:#031018,stroke:#00F6FF,color:#E6FCFF;",
            "",
            "    Push[Push to main]:::accent --> Init[release_manager.py init]:::accent",
            "    Init --> Bump[release_manager.py bump]:::accent",
            "    Bump --> Sync[sync_docs.py]:::accent",
            "    Sync --> Commit[git add + commit]:::accent",
            "    Commit --> Release[gh release create]:::accent",
            "    Release --> Wiki[sync_docs.py --sync-wiki]:::accent",
            "```",
            "",
            "---",
            "",
            "## Agents",
            "",
            "| Agent | Trigger | Script | Responsibility |",
            "| --- | --- | --- | --- |",
            "| **Version Agent** | `push` / `workflow_dispatch` | `release_manager.py bump` | Calculates the next semver and updates `VERSION.json` |",
            "| **Changelog Agent** | `push` / `workflow_dispatch` | `release_manager.py bump` | Rebuilds `CHANGELOG.md` and release notes |",
            "| **Wiki + README Agent** | `push` / `workflow_dispatch` | `sync_docs.py` | Regenerates `README.md`, the wiki source pages, and pushes to the GitHub wiki |",
            "| **Version Documentation Agent** | `push` / `workflow_dispatch` | `release_manager.py` | Updates `docs/versions/` and publishes GitHub releases |",
            "| **AI Contributor Guard** | `push` / `pull_request` | `check_no_ai_contributors.py` | Rejects AI-attributed commits and co-authors |",
            "",
            "---",
            "",
            "## Wiki agent details",
            "",
            "`sync_docs.py` generates the entire wiki on every run from:",
            "",
            "- `VERSION.json` (current release + history)",
            "- Forsetti manifests under `src/MasterControlModules/Resources/ForsettiManifests/`",
            "- Repo plans under `plans/`",
            "- Hard-coded structural content for pages that depend on runtime details (API, Auto-Connect, CLU, telemetry, theme)",
            "",
            "It writes to `docs/wiki/` first. With `--sync-wiki`, it then clones",
            "`github.com/{repo}.wiki.git`, replaces every page, and pushes the result.",
            "Authentication uses `GITHUB_TOKEN` injected by GitHub Actions.",
            "",
            "### Adding a new wiki page",
            "",
            "1. Add a new `render_*()` function in `sync_docs.py`.",
            "2. Wire it into `write_docs()` so the new file lands in `docs/wiki/`.",
            "3. Add a navigation entry in `render_home()` and `render_sidebar()`.",
            "4. Push — the agent regenerates everything on the next CI run.",
            "",
            "---",
            "",
            "## Current baseline",
            "",
            "| Property | Value |",
            "| --- | --- |",
            f"| Current tracked release | `{release['tag']}` |",
            f"| Agent commit prefix | `{AGENT_COMMIT_PREFIX}` |",
            "| Wiki source directory | `docs/wiki/` |",
            "| Wiki sync target | `github.com/{repo}.wiki.git` |",
            "",
            "---",
            "",
            "See also: [Operations](Operations) · [Versions](Versions)",
            "",
        ]
    )


# ----------------------------------------------------------------------------
# Wiki: Versions
# ----------------------------------------------------------------------------


def render_versions(state: dict) -> str:
    release = latest_release(state)
    history = state.get("history", [])

    lines = [
        f"# {PROJECT_NAME} — Versions",
        "",
        shield("scheme", "semver", "00f6ff")
        + " "
        + shield("strategy", "patch on main", "00aacc"),
        "",
        "Semantic versioning, automated bumps on every push to `main`. The version agent ",
        "analyzes commit history to decide between patch / minor / major.",
        "",
        "---",
        "",
        "## Current release",
        "",
        "| Field | Value |",
        "| --- | --- |",
        f"| **Version** | `{release['tag']}` |",
        f"| **Released** | `{release.get('released_at', 'unreleased')}` |",
        f"| **Summary** | {release.get('summary', '')} |",
        "",
    ]

    if release.get("entries"):
        lines.extend(["### Highlights", ""])
        lines.extend(f"- {entry}" for entry in release["entries"])
        lines.append("")

    lines.extend(
        [
            "---",
            "",
            "## Versioning scheme",
            "",
            "| Bump | When | Examples |",
            "| --- | --- | --- |",
            "| **Patch** `0.1.x` | Bug fixes, doc updates, metadata | `0.2.0 → 0.2.1` |",
            "| **Minor** `0.x.0` | New features, new modules, capabilities | `0.2.5 → 0.3.0` |",
            "| **Major** `x.0.0` | Breaking changes | `0.9.0 → 1.0.0` |",
            "",
            "Versions are tracked in `VERSION.json` and tagged as GitHub Releases.",
            "",
            "---",
            "",
            "## Release artifacts",
            "",
            "- [Version Index](../versions/index.md) — full list of releases",
            "- [Latest Release Notes](../versions/latest.md) — notes for the current release",
            "",
        ]
    )

    if history:
        lines.extend(
            [
                "---",
                "",
                "## Recent releases",
                "",
                "| Version | Date | Summary |",
                "| --- | --- | --- |",
            ]
        )
        for entry in history[:20]:
            tag = entry.get("tag", "")
            date = entry.get("released_at", "")
            summary = entry.get("summary", "")
            lines.append(f"| `{tag}` | `{date}` | {summary} |")
        lines.append("")

    lines.extend(["See also: [Automation](Automation) · [Operations](Operations)", ""])
    return "\n".join(lines)


# ----------------------------------------------------------------------------
# Wiki: Troubleshooting
# ----------------------------------------------------------------------------


def render_troubleshooting() -> str:
    return "\n".join(
        [
            f"# {PROJECT_NAME} — Troubleshooting",
            "",
            "Symptom-first guide to the most common failure modes. If something here matches ",
            "what you're seeing, follow the diagnosis chain in order.",
            "",
            "---",
            "",
            "## Shell shows `API OFFLINE` even though the runtime is up",
            "",
            "**Cause:** an older build gated `CaptureSnapshot` on the SCM service state, so ",
            "console-mode runs (where the service is technically not registered) showed offline.",
            "",
            "**Fix:** upgrade to the current build. The shell now probes `/api/health` and ",
            "`/api/dashboard` directly and trusts the API response over SCM.",
            "",
            "---",
            "",
            "## Auto-Connect returns HTTP 401",
            "",
            "**Cause:** the credential is rejected by the upstream provider.",
            "",
            "**Diagnosis:**",
            "",
            "1. Re-run with `discoverModels: false` — if it still fails, the probe step is the issue.",
            "2. Check the `steps` array in the response — the failing stage is flagged.",
            "3. Verify the credential field name matches the capability descriptor (`api_key`, `apikey`, `token`, `secret` are all accepted).",
            "",
            "---",
            "",
            "## MCP server / sub-agent upsert times out for ~10 seconds",
            "",
            "**Cause:** older builds called the synchronous `IRuntimeInventoryService::refresh()` ",
            "from the mutating handler, which probed every endpoint sequentially.",
            "",
            "**Fix:** the current contract uses `refreshAsync()` so admin calls return immediately ",
            "and the inventory probe runs on a detached background thread, coalesced via an ",
            "`std::atomic_bool` pending flag.",
            "",
            "---",
            "",
            "## Sub-agent group upsert returns HTTP 400 right after creating its members",
            "",
            "**Cause:** the validator was reading the inventory cache, which is updated only after ",
            "`refreshAsync()` finishes — so a fast-following request hit a stale view.",
            "",
            "**Fix:** the validator now reads `state_->configuration.activeProfile.seededEndpoints` ",
            "directly under the same mutex as the upsert, eliminating the race.",
            "",
            "---",
            "",
            "## Setup launcher fails to elevate",
            "",
            "**Diagnosis:**",
            "",
            "1. Check `~\\Desktop\\MasterControlOrchestrationServer-install-log-pointer.txt` for the real log path.",
            "2. The launcher uses `ShellExecuteEx` with the `runas` verb. If UAC is suppressed at the policy level, the elevation will fail silently.",
            "3. For packaged bundles, use `INSTALL.txt` or rerun the MSI with `msiexec /i <package>.msi /l*v install.log` for a richer log trail.",
            "",
            "---",
            "",
            "## Tron palette looks washed out",
            "",
            "**Cause:** an unstyled control is rendering with the default Fluent brushes.",
            "",
            "**Fix:** make sure the control is inside a `RootGrid` with `RequestedTheme=\"Dark\"` ",
            "and that there's an implicit `Style TargetType` entry in `App.xaml` for the ",
            "control's type. Fluent theme brushes (`TextFillColorPrimaryBrush`, etc.) should also ",
            "be remapped — see the existing entries in `App.xaml`.",
            "",
            "---",
            "",
            "## Build fails with `PlatformToolset=v143 not installed`",
            "",
            "**Cause:** the toolchain is **v145** (Visual Studio 2026 / VS18). v143 is not supported.",
            "",
            "**Fix:** install Visual Studio 2026 with the C++ workload, or update the platform ",
            "toolset in the affected vcxproj/CMake config.",
            "",
            "---",
            "",
            "## Browser dashboard renders blank",
            "",
            "**Diagnosis:**",
            "",
            "1. Open DevTools and check the Console for asset 404s.",
            "2. Confirm `share/MasterControlOrchestrationServer/web/` is staged in the install root.",
            "3. Confirm `/api/health` returns `200 OK` from the same host.",
            "",
            "---",
            "",
            "## Where the logs live",
            "",
            "| Log | Path |",
            "| --- | --- |",
            "| Installer (real log) | `%PUBLIC%\\Documents\\Master Control Orchestration Server\\logs\\installer\\` |",
            "| Installer (pointer file) | `~\\Desktop\\MasterControlOrchestrationServer-install-log-pointer.txt` |",
            "| Service host | `%ProgramData%\\Master Control Orchestration Server\\logs\\service\\` |",
            "| Shell | `%LOCALAPPDATA%\\Master Control Orchestration Server\\logs\\shell\\` |",
            "",
            "---",
            "",
            "See also: [Operations](Operations) · [Architecture](Architecture) · ",
            "[API Reference](API-Reference)",
            "",
        ]
    )


# ----------------------------------------------------------------------------
# Wiki: Sidebar / Footer
# ----------------------------------------------------------------------------


def render_sidebar() -> str:
    return "\n".join(
        [
            f"### {PROJECT_NAME}",
            "",
            "**[🏠 Home](Home)**",
            "",
            "**Architecture**",
            "- [🏗️ Architecture](Architecture)",
            "- [🌐 API Reference](API-Reference)",
            "- [🤖 Auto-Connect AI](Auto-Connect-AI)",
            "- [⚖️ CLU Governance](CLU-Governance)",
            "- [📡 Telemetry & Activity](Telemetry-and-Activity)",
            "- [🛰️ Sub-Agents](Sub-Agents)",
            "",
            "**UI / UX**",
            "- [🎨 Tron UI Theme](Tron-UI-Theme)",
            "",
            "**Operations**",
            "- [🛠️ Operations](Operations)",
            "- [📦 Infrastructure](Infrastructure)",
            "- [🔌 Remote Client](Remote-Client)",
            "- [🚨 Troubleshooting](Troubleshooting)",
            "",
            "**Project**",
            "- [⚙️ Automation](Automation)",
            "- [🏷️ Versions](Versions)",
            "",
        ]
    )


def render_footer() -> str:
    return "\n".join(
        [
            "---",
            f"_Auto-generated from the [{REPOSITORY_NAME}]({REPOSITORY_URL}) repository by `scripts/github_agents/sync_docs.py`._",
            "_To update wiki content, edit the render functions in that script (or the source files under `plans/`) and push to `main`._",
            "",
        ]
    )


# ----------------------------------------------------------------------------
# Driver
# ----------------------------------------------------------------------------


def write_docs() -> None:
    state = load_state()
    write_text(README_FILE, render_readme(state))
    write_text(WIKI_DIR / "Home.md", render_home(state))
    write_text(WIKI_DIR / "Architecture.md", render_architecture())
    write_text(WIKI_DIR / "API-Reference.md", render_api_reference())
    write_text(WIKI_DIR / "Auto-Connect-AI.md", render_auto_connect())
    write_text(WIKI_DIR / "CLU-Governance.md", render_clu_governance())
    write_text(WIKI_DIR / "Telemetry-and-Activity.md", render_telemetry())
    write_text(WIKI_DIR / "Tron-UI-Theme.md", render_tron_theme())
    write_text(WIKI_DIR / "Sub-Agents.md", render_sub_agents())
    write_text(WIKI_DIR / "Operations.md", render_operations())
    write_text(WIKI_DIR / "Infrastructure.md", render_infrastructure())
    write_text(WIKI_DIR / "Remote-Client.md", render_remote_client())
    write_text(WIKI_DIR / "Automation.md", render_automation(state))
    write_text(WIKI_DIR / "Versions.md", render_versions(state))
    write_text(WIKI_DIR / "Troubleshooting.md", render_troubleshooting())
    write_text(WIKI_DIR / "_Sidebar.md", render_sidebar())
    write_text(WIKI_DIR / "_Footer.md", render_footer())


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
