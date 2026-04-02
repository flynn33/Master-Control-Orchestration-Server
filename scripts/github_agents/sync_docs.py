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


def read_excerpt(path: Path, max_lines: int = 12) -> list[str]:
    if not path.exists():
        return []

    lines: list[str] = []
    for line in path.read_text(encoding="utf-8").splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith("#") or stripped.startswith("```"):
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
    if not path.exists():
        return ""
    text = path.read_text(encoding="utf-8")
    lines = text.splitlines()
    if lines and lines[0].lstrip().startswith("#"):
        lines = lines[1:]
        if lines and not lines[0].strip():
            lines = lines[1:]
    return "\n".join(lines).strip()


def render_plan_backed_page(
    title: str,
    source_paths: list[Path],
    intro: str | None = None,
    see_also: list[str] | None = None,
) -> str:
    lines = [f"# {PROJECT_NAME} {title}", ""]
    if intro:
        lines.extend([intro, ""])

    for path in source_paths:
        content = read_full_content(path)
        if not content:
            continue
        lines.extend([content, ""])

    if see_also:
        lines.extend([f"See also: {' | '.join(see_also)}", ""])

    return "\n".join(lines)


def render_readme(state: dict) -> str:
    release = latest_release(state)
    project_overview = read_excerpt(PLANS_DIR / "dashboard" / "project-overview.md", 8)
    technical_details = read_excerpt(PLANS_DIR / "dashboard" / "technical-details.md", 8)

    lines = [
        f"# {PROJECT_NAME}",
        "",
        f"{PROJECT_NAME} is a Forsetti-compliant Windows orchestration control plane for guided setup, governance, MCP services, sub-agents, AI provider routing, telemetry, and browser-based operations.",
        "",
        f"- Repository slug: `{REPOSITORY_NAME}`",
        f"- Repository URL: {REPOSITORY_URL}",
        "",
        "## Current Release",
        "",
        f"- Version: `{release['tag']}`",
        f"- Release date: `{release['released_at']}`",
        f"- Summary: {release['summary']}",
        "",
        "## Highlights",
        "",
        "- Windows Service host, WinUI 3 operator shell, and browser admin surface backed by the same local runtime.",
        "- Guided setup workflows for providers, MCP servers, sub-agents, Apple hosts, imports, and assignment flows.",
        "- Command Logic Unit (CLU) Forsetti module for governance, responsibility routing, and platform-governance execution.",
        "- CLU governance, platform gateway lanes, platform governance lanes, and Apple remote-host execution support.",
        "- Release packaging, setup launcher, bootstrapper validation, and deployment acceptance tooling in-repo.",
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
        "### Product Overview",
    ]
    lines.extend(f"- {item}" for item in project_overview)
    lines.extend(["", "### Technical Snapshot"])
    lines.extend(f"- {item}" for item in technical_details)
    lines.extend(
        [
            "",
            "### Command Logic Unit (CLU)",
            "- CLU is a first-class Forsetti service module, not just a navigation label in the shell.",
            "- Manifest: `src/MasterControlModules/Resources/ForsettiManifests/CommandLogicUnitModule.json`",
            "- Module ID: `com.mastercontrol.command-logic-unit`",
            "- Responsibilities: governance posture, rule evaluation, provider responsibility routing, Apple operations, and platform governance execution.",
            "",
            "## Primary Documents",
            "",
            "- [Project Overview](plans/dashboard/project-overview.md)",
            "- [Technical Details](plans/dashboard/technical-details.md)",
            "- [Remote Client Onboarding](plans/dashboard/remote-client-onboarding.md)",
            "- [Deployment Overview](plans/infrastructure/deployment-overview.md)",
            "- [Platform Gateway And Governance](plans/infrastructure/platform-gateway-and-governance.md)",
            "- [Version Index](docs/versions/index.md)",
            "- [Latest Release Notes](docs/versions/latest.md)",
            "",
            f"Repository URL: {REPOSITORY_URL}",
            "",
        ]
    )
    return "\n".join(lines)


def render_home(state: dict) -> str:
    release = latest_release(state)
    return "\n".join(
        [
            f"# {PROJECT_NAME} Wiki",
            "",
            f"{PROJECT_NAME} is a Forsetti-compliant Windows orchestration server for guided setup, provider routing, CLU governance, platform gateways, platform governance lanes, telemetry, imports, exports, and browser-based operations.",
            "",
            "## Current Release",
            "",
            "| Field | Value |",
            "| --- | --- |",
            f"| Version | `{release['tag']}` |",
            f"| Released | `{release['released_at']}` |",
            f"| Summary | {release['summary']} |",
            "",
            "## Platform at a Glance",
            "",
            "| Component | Count | Details |",
            "| --- | --- | --- |",
            "| Operator surfaces | 2 | WinUI 3 desktop shell and browser admin UI backed by the same runtime |",
            "| Platform lanes | 6 | Windows, macOS, and iOS gateway plus governance lanes |",
            "| AI providers | 3 | Codex, Claude Code, and xAI routing in the current build |",
            "| Install flow | 2 | Native setup launcher plus diagnostic PowerShell fallback |",
            "| Governance | 1 core module | Command Logic Unit (CLU) plus platform governance execution and resource enforcement |",
            "",
            "## Command Logic Unit",
            "",
            "- CLU is a Forsetti service module with manifest ID `com.mastercontrol.command-logic-unit`.",
            "- It coordinates governance posture, rule evaluation, model-to-responsibility routing, Apple operations, and platform governance execution.",
            "- The shell and browser surfaces read CLU state from the runtime instead of duplicating governance logic locally.",
            "",
            "## Wiki Pages",
            "",
            "| Page | Description |",
            "| --- | --- |",
            "| [Architecture](Architecture) | Product composition, runtime structure, and platform governance model |",
            "| [Infrastructure](Infrastructure) | Deployment shape, packaging model, and target-host validation focus |",
            "| [Sub-Agents](Sub-Agents) | Current seven-agent roster, responsibilities, and shared client details |",
            "| [API Reference](API-Reference) | Current browser, CLU, platform-service, and governance routes from the runtime |",
            "| [Operations](Operations) | Build, validate, package, install, and deployment-acceptance workflows |",
            "| [Remote Client](Remote-Client) | Current onboarding direction for Codex, Claude Code, and platform gateway discovery |",
            "| [Automation](Automation) | GitHub agents, CI/CD pipeline, commit conventions, and workflow triggers |",
            "| [Versions](Versions) | Release history, versioning scheme, and release documents |",
            "",
            "## Quick Links",
            "",
            "- [README](../README.md)",
            "- [Project Overview](../plans/dashboard/project-overview.md)",
            "- [Technical Details](../plans/dashboard/technical-details.md)",
            "- [Version Index](../docs/versions/index.md)",
            f"- Repository: {REPOSITORY_URL}",
            "",
        ]
    )


def render_architecture() -> str:
    return render_plan_backed_page(
        "Architecture",
        [
            PLANS_DIR / "dashboard" / "project-overview.md",
            PLANS_DIR / "dashboard" / "technical-details.md",
            PLANS_DIR / "infrastructure" / "platform-gateway-and-governance.md",
        ],
        intro="This page reflects the current repository-backed architecture rather than the retired external aggregator design.",
        see_also=["[Infrastructure](Infrastructure)", "[API Reference](API-Reference)", "[Operations](Operations)"],
    )


def render_infrastructure() -> str:
    return render_plan_backed_page(
        "Infrastructure",
        [PLANS_DIR / "infrastructure" / "deployment-overview.md"],
        intro="This page tracks the deployment model and installer shape that currently ship from this repository.",
        see_also=["[Architecture](Architecture)", "[Operations](Operations)", "[Remote Client](Remote-Client)"],
    )


def render_sub_agents() -> str:
    return render_plan_backed_page(
        "Sub-Agents",
        [PLANS_DIR / "infrastructure" / "sub-agent-system-plan.md"],
        intro="This page reflects the current seven-agent architecture and shared platform gateway client model.",
        see_also=["[Architecture](Architecture)", "[API Reference](API-Reference)"],
    )


def render_api_reference() -> str:
    return "\n".join(
        [
            f"# {PROJECT_NAME} API Reference",
            "",
            "This page documents the current HTTP and MCP-style routes exposed by the shared runtime in `src/MasterControlApp/MasterControlRuntime.cpp`.",
            "",
            "## Core Read Endpoints",
            "",
            "| Route | Method | Purpose |",
            "| --- | --- | --- |",
            "| `/api/health` | `GET` | Service health and readiness snapshot |",
            "| `/api/dashboard` | `GET` | Browser dashboard payload |",
            "| `/api/config` | `GET` | Current persisted configuration |",
            "| `/api/providers` | `GET` | Provider catalog, credentials posture, and assignments |",
            "| `/api/exports` | `GET` | Export inventory and generated handoff artifacts |",
            "| `/api/forsetti/surface` | `GET` | Current Forsetti surface model for shell/browser rendering |",
            "| `/api/install/history` | `GET` | Install and import execution history |",
            "| `/api/beacon` | `GET` | Beacon state and LAN-facing discovery posture |",
            "",
            "## CLU And Governance",
            "",
            "| Route | Method | Purpose |",
            "| --- | --- | --- |",
            "| `/api/clu` | `GET` | CLU posture, findings, and current governance state |",
            "| `/api/clu/tools` | `GET` | Published governance tool descriptors |",
            "| `/api/clu/apple-operations` | `GET` | Apple job queue/history snapshot |",
            "| `/api/clu/execute` | `POST` | Execute a CLU governance operation |",
            "| `/api/clu/apple-operations/cancel` | `POST` | Cancel a queued Apple operation |",
            "",
            "The CLU endpoints are backed by the `CommandLogicUnitModule` Forsetti service module (`com.mastercontrol.command-logic-unit`), which owns governance posture and responsibility routing inside the runtime.",
            "",
            "## Platform Services",
            "",
            "| Route | Method | Purpose |",
            "| --- | --- | --- |",
            "| `/api/platform-services` | `GET` | Combined gateway, governance, and host inventory |",
            "| `/api/platform-services/gateways` | `GET` | Platform gateway summary |",
            "| `/api/platform-services/governance` | `GET` | Platform governance lane summary |",
            "| `/api/platform-services/apple-hosts` | `GET` | Registered Apple remote hosts and readiness data |",
            "| `/api/platform-services/apple-hosts` | `POST` | Add or update an Apple host definition |",
            "| `/api/platform-services/apple-hosts/remove` | `POST` | Remove an Apple host definition |",
            "| `/api/platform-services/config/{platform}` | `GET` | Platform-specific client configuration payload |",
            "| `/mcp/gateway/{platform}` | `GET` | Gateway document for `windows`, `macos`, or `ios` |",
            "| `/mcp/governance/{platform}` | `GET` | Governance document for `windows`, `macos`, or `ios` |",
            "| `/mcp/governance/{platform}` | `POST` | Execute a platform governance tool call |",
            "",
            "## Runtime Inventory Mutation",
            "",
            "| Route | Method | Purpose |",
            "| --- | --- | --- |",
            "| `/api/runtime/mcp-servers` | `POST` | Create or update a custom MCP server definition |",
            "| `/api/runtime/mcp-servers/remove` | `POST` | Remove a custom MCP server definition |",
            "| `/api/runtime/subagents` | `POST` | Create or update a sub-agent definition |",
            "| `/api/runtime/subagents/remove` | `POST` | Remove a sub-agent definition |",
            "| `/api/providers/groups` | `POST` | Create or update a provider group |",
            "| `/api/providers/groups/remove` | `POST` | Remove a provider group |",
            "| `/api/providers/assignments` | `POST` | Apply provider routing assignments |",
            "| `/api/providers/credentials` | `POST` | Save provider credential material |",
            "| `/api/providers/execute` | `POST` | Execute a provider-backed request through the runtime |",
            "",
            "## Install And Import Routes",
            "",
            "| Route | Method | Purpose |",
            "| --- | --- | --- |",
            "| `/api/install/package` | `POST` | Import or deploy a package artifact |",
            "| `/api/install/repo` | `POST` | Import from a Git/bootstrap repository |",
            "| `/api/install/zip` | `POST` | Import from a zip bundle |",
            "",
            "## Notes",
            "",
            "- The browser UI and WinUI shell both consume runtime-backed state rather than embedding separate business logic.",
            "- Platform route keys are currently `windows`, `macos`, and `ios`.",
            "- Legacy compatibility identifiers still exist internally for upgrades, but public-facing routes and docs use the Orchestration Server naming.",
            "",
            "See also: [Architecture](Architecture) | [Operations](Operations) | [Remote Client](Remote-Client)",
            "",
        ]
    )


def render_operations() -> str:
    return "\n".join(
        [
            f"# {PROJECT_NAME} Operations",
            "",
            "Build, validate, package, install, and support the current product from the repository-owned tooling.",
            "",
            "## Local Build And Validation",
            "",
            "```powershell",
            "cmake --preset debug",
            "cmake --build build\\debug --config Debug",
            "ctest --test-dir build\\debug -C Debug --output-on-failure",
            "powershell -NoProfile -ExecutionPolicy Bypass -File scripts\\check-mastercontrol-forsetti.ps1",
            "```",
            "",
            "## Staging And Packaging",
            "",
            "```powershell",
            "cmake --install build\\debug --config Debug --prefix dist\\debug",
            "powershell -NoProfile -ExecutionPolicy Bypass -File scripts\\Package-MasterControlOrchestrationServer.ps1 -Preset release",
            "```",
            "",
            "## Preferred Install Entry Points",
            "",
            "| Entry point | Role |",
            "| --- | --- |",
            "| `MasterControlOrchestrationServerSetup.exe` | Standard interactive Windows installer entry point |",
            "| `Install-MasterControlOrchestrationServer.ps1` | Diagnostic fallback with desktop logging |",
            "| `MasterControlBootstrapper.exe` | Core lifecycle engine for `preflight`, `install`, `validate`, `upgrade`, `repair`, and `uninstall` |",
            "",
            "## Deployment Validation Scripts",
            "",
            "| Script | Purpose |",
            "| --- | --- |",
            "| `scripts/Build-MasterControlOrchestrationServer.ps1` | Configure, build, test, and stage local artifacts |",
            "| `scripts/Test-MasterControlOrchestrationServerDeployment.ps1` | End-to-end deployment acceptance harness |",
            "| `scripts/Compare-MasterControlOrchestrationServerDeploymentReports.ps1` | Compare acceptance reports across hosts |",
            "| `scripts/Invoke-MasterControlOrchestrationServerDeploymentMatrix.ps1` | Drive labeled deployment-matrix runs |",
            "| `scripts/Get-MasterControlOrchestrationServerReleaseReadiness.ps1` | Build release-readiness markdown |",
            "",
            "## Installed Runtime Surfaces",
            "",
            "| Surface | Typical path |",
            "| --- | --- |",
            "| Windows service host | `MasterControlServiceHost.exe` |",
            "| Desktop shell | `MasterControlShell.exe` |",
            "| Browser admin UI | `http://127.0.0.1:7300/` after local install |",
            "",
            "## Compatibility Notes",
            "",
            "- The public product name is `Master Control Orchestration Server`.",
            "- The legacy Windows service name remains `MasterControlProgram` for upgrade compatibility.",
            "- The legacy uninstall registry key also remains `...\\Uninstall\\MasterControlProgram` for upgrade compatibility.",
            "",
            "## Standard Operator Flow",
            "",
            "1. Build and validate locally.",
            "2. Package a release artifact.",
            "3. Install via `MasterControlOrchestrationServerSetup.exe`.",
            "4. Verify service, browser UI, and desktop shell launch behavior.",
            "5. Run deployment acceptance and review readiness artifacts.",
            "",
            "See also: [API Reference](API-Reference) | [Automation](Automation) | [Versions](Versions)",
            "",
        ]
    )


def render_remote_client() -> str:
    return render_plan_backed_page(
        "Remote Client",
        [PLANS_DIR / "dashboard" / "remote-client-onboarding.md"],
        intro="This page tracks the current remote-agent onboarding direction rather than the retired Blade gateway plugin workflow.",
        see_also=["[Architecture](Architecture)", "[API Reference](API-Reference)"],
    )


def render_automation(state: dict) -> str:
    release = latest_release(state)
    return "\n".join(
        [
            f"# {PROJECT_NAME} Automation",
            "",
            "The repository is maintained by GitHub agents that handle versioning, changelog updates, generated docs, wiki synchronization, and contributor compliance.",
            "",
            "## Automation Pipeline",
            "",
            "```",
            "Push to main",
            "    |",
            "    v",
            "release_manager.py init",
            "    |",
            "    v",
            "release_manager.py bump",
            "    |",
            "    v",
            "sync_docs.py",
            "    |",
            "    v",
            "git add + commit",
            "    |",
            "    v",
            "gh release create",
            "    |",
            "    v",
            "sync_docs.py --sync-wiki",
            "```",
            "",
            "## Agent Details",
            "",
            "| Agent | Trigger | Script | What It Does |",
            "| --- | --- | --- | --- |",
            "| **Version Agent** | `push`, `workflow_dispatch` | `release_manager.py bump` | Calculates the next semantic version and updates `VERSION.json` |",
            "| **Changelog Agent** | `push`, `workflow_dispatch` | `release_manager.py bump` | Rebuilds `CHANGELOG.md` and release notes |",
            "| **Wiki + README Agent** | `push`, `workflow_dispatch` | `sync_docs.py` | Regenerates `README.md` and the wiki source pages |",
            "| **Version Documentation Agent** | `push`, `workflow_dispatch` | `release_manager.py` | Updates `docs/versions/` and publishes GitHub releases |",
            "| **AI Contributor Guard** | `push`, `pull_request` | `check_no_ai_contributors.py` | Rejects AI-attributed commits and co-authors |",
            "",
            "## Current Baseline",
            "",
            "| Property | Value |",
            "| --- | --- |",
            f"| Current tracked release | `{release['tag']}` |",
            f"| Agent commit prefix | `{AGENT_COMMIT_PREFIX}` |",
            "| Wiki source directory | `docs/wiki/` |",
            "| Wiki sync target | `github.com/{repo}.wiki.git` |",
            "",
            "## Scripts",
            "",
            "| Script | Purpose |",
            "| --- | --- |",
            "| `release_manager.py` | Version bumping, changelog generation, and release pages |",
            "| `sync_docs.py` | README generation, wiki generation, and GitHub wiki sync |",
            "| `check_no_ai_contributors.py` | Contributor compliance enforcement |",
            "| `common.py` | Shared constants, git helpers, and file utilities |",
            "",
            "See also: [Operations](Operations) | [Versions](Versions)",
            "",
        ]
    )


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
        lines.extend(
            [
                "## Recent Releases",
                "",
                "| Version | Date | Summary |",
                "| --- | --- | --- |",
            ]
        )
        for entry in history[:15]:
            tag = entry.get("tag", "")
            date = entry.get("released_at", "")
            summary = entry.get("summary", "")
            lines.append(f"| `{tag}` | `{date}` | {summary} |")
        lines.append("")

    lines.extend(["See also: [Automation](Automation) | [Operations](Operations)", ""])
    return "\n".join(lines)


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


def render_footer() -> str:
    return "\n".join(
        [
            "---",
            f"This wiki is auto-generated from repository source documents by the [{REPOSITORY_NAME}]({REPOSITORY_URL}) automation pipeline.",
            "To update wiki content, edit the source files in `plans/` or `scripts/github_agents/sync_docs.py` and push to `main`.",
            "",
        ]
    )


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
