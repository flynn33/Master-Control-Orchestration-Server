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


def render_readme(state: dict) -> str:
    release = latest_release(state)
    project_overview = read_excerpt(ROOT / "plans" / "dashboard" / "project-overview.md", 8)
    technical_details = read_excerpt(ROOT / "plans" / "dashboard" / "technical-details.md", 8)
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


def render_home(state: dict) -> str:
    release = latest_release(state)
    return "\n".join(
        [
            f"# {PROJECT_NAME} Wiki",
            "",
            f"Welcome to the maintained wiki for `{REPOSITORY_NAME}`.",
            "",
            f"- Current release: `{release['tag']}`",
            f"- Release date: `{release['released_at']}`",
            f"- Summary: {release['summary']}",
            "",
            "## Core Surfaces",
            "",
            "- Windows Service host for orchestration and telemetry.",
            "- WinUI 3 desktop shell for operator control.",
            "- Browser dashboard for remote LAN access.",
            "- Forsetti-aligned modules and manifests for runtime composition.",
            "",
            "## Start Here",
            "",
            "- [Architecture](Architecture.md)",
            "- [Operations](Operations.md)",
            "- [Automation](Automation.md)",
            "- [Versions](Versions.md)",
            "",
        ]
    )


def render_architecture() -> str:
    overview = read_excerpt(ROOT / "plans" / "dashboard" / "project-overview.md", 10)
    gateway = read_excerpt(ROOT / "plans" / "infrastructure" / "mcp-aggregator-gateway-deployment.md", 10)
    lines = [
        f"# {PROJECT_NAME} Architecture",
        "",
        "## Dashboard Program",
    ]
    lines.extend(f"- {item}" for item in overview)
    lines.extend(["", "## Aggregator Gateway"])
    lines.extend(f"- {item}" for item in gateway)
    lines.extend(
        [
            "",
            "## Repository Modules",
            "",
            "- `src/MasterControlServiceHost` boots the service runtime.",
            "- `src/MasterControlShell` provides the WinUI 3 operator shell.",
            "- `src/MasterControlBootstrapper` handles install and detection flows.",
            "- `resources/manifests` holds Forsetti module manifests.",
            "",
        ]
    )
    return "\n".join(lines)


def render_operations() -> str:
    return "\n".join(
        [
            f"# {PROJECT_NAME} Operations",
            "",
            "## Local Commands",
            "",
            "```powershell",
            "cmake --build build\\debug --config Debug",
            "ctest --test-dir build\\debug -C Debug --output-on-failure",
            "cmake --install build\\debug --config Debug --prefix dist\\debug",
            "dist\\debug\\MasterControlBootstrapper.exe detect",
            "```",
            "",
            "## Push Guard",
            "",
            "- Enable the repository hook with `scripts/Enable-GitHooks.ps1`.",
            "- The pre-push hook rejects commits that declare AI contributors.",
            "- The GitHub `AI Contributor Guard` workflow mirrors the same rule for pushes and pull requests.",
            "",
        ]
    )


def render_automation(state: dict) -> str:
    release = latest_release(state)
    return "\n".join(
        [
            f"# {PROJECT_NAME} Automation",
            "",
            "## Managed Agents",
            "",
            "- Changelog Agent",
            "- Wiki + README Agent",
            "- AI Contributor Guard",
            "- Version Agent",
            "- Version Documentation Agent",
            "",
            "## Current Automation Baseline",
            "",
            f"- Current tracked release: `{release['tag']}`",
            f"- Agent commit prefix: `{AGENT_COMMIT_PREFIX}`",
            "- Maintainer workflow runs on pushes to `main` and on manual dispatch.",
            "- Wiki pages are synchronized from `docs/wiki/*.md` to the GitHub wiki repository.",
            "",
        ]
    )


def render_versions(state: dict) -> str:
    release = latest_release(state)
    lines = [
        f"# {PROJECT_NAME} Versions",
        "",
        f"- Current release: `{release['tag']}`",
        f"- Release date: `{release['released_at']}`",
        f"- Summary: {release['summary']}",
        "",
        "## Release Documents",
        "",
        "- [Version Index](../versions/index.md)",
        "- [Latest Release Notes](../versions/latest.md)",
        "",
    ]
    return "\n".join(lines)


def write_docs() -> None:
    state = load_state()
    write_text(README_FILE, render_readme(state))
    write_text(WIKI_DIR / "Home.md", render_home(state))
    write_text(WIKI_DIR / "Architecture.md", render_architecture())
    write_text(WIKI_DIR / "Operations.md", render_operations())
    write_text(WIKI_DIR / "Automation.md", render_automation(state))
    write_text(WIKI_DIR / "Versions.md", render_versions(state))
    write_text(
        WIKI_DIR / "_Sidebar.md",
        "\n".join(
            [
                "# Wiki",
                "",
                "- [Home](Home)",
                "- [Architecture](Architecture)",
                "- [Operations](Operations)",
                "- [Automation](Automation)",
                "- [Versions](Versions)",
                "",
            ]
        ),
    )


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
