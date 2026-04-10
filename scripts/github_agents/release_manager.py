from __future__ import annotations

import argparse
import json
from pathlib import Path

from common import (
    AGENT_COMMIT_PREFIX,
    DEFAULT_VERSION,
    ROOT,
    PROJECT_NAME,
    ensure_directory,
    format_semver,
    git_output,
    parse_semver,
    read_json,
    utc_date,
    write_json,
    write_text,
)


VERSION_FILE = ROOT / "VERSION.json"
CHANGELOG_FILE = ROOT / "CHANGELOG.md"
VERSIONS_DIR = ROOT / "docs" / "versions"

MAJOR_MARKERS = ("semver: major", "#major", "[major]", "release: major")
MINOR_MARKERS = ("semver: minor", "#minor", "[minor]", "release: minor")


def current_head() -> str:
    return git_output(["rev-parse", "HEAD"])


def read_state() -> dict:
    return read_json(
        VERSION_FILE,
        {
            "scheme": "semantic-versioning",
            "strategy": "patch-on-main",
            "current_version": DEFAULT_VERSION,
            "current_tag": f"v{DEFAULT_VERSION}",
            "released_at": utc_date(),
            "last_release_commit": current_head(),
            "history": [],
        },
    )


def commit_records(revision_range: str | None) -> list[dict]:
    format_string = "%H%x1f%s%x1f%an%x1f%ae%x1f%b%x1e"
    args = ["log", "--reverse", f"--format={format_string}"]
    if revision_range:
        args.append(revision_range)

    output = git_output(args)
    records: list[dict] = []
    for chunk in output.split("\x1e"):
        chunk = chunk.rstrip("\r\n")
        if not chunk:
            continue

        sha, subject, author_name, author_email, body = chunk.split("\x1f", 4)
        if subject.startswith(AGENT_COMMIT_PREFIX) or author_name == "github-actions[bot]":
            continue

        records.append(
            {
                "sha": sha,
                "subject": subject.strip(),
                "author_name": author_name.strip(),
                "author_email": author_email.strip(),
                "body": body.strip(),
            }
        )
    return records


def detect_bump(records: list[dict]) -> str:
    level = "patch"
    for record in records:
        text = f"{record['subject']}\n{record['body']}".lower()
        if any(marker in text for marker in MAJOR_MARKERS):
            return "major"
        if any(marker in text for marker in MINOR_MARKERS):
            level = "minor"
    return level


def bump_version(version: str, level: str) -> str:
    major, minor, patch = parse_semver(version)
    if level == "major":
        return format_semver((major + 1, 0, 0))
    if level == "minor":
        return format_semver((major, minor + 1, 0))
    return format_semver((major, minor, patch + 1))


def initial_release_record() -> dict:
    return {
        "version": DEFAULT_VERSION,
        "tag": f"v{DEFAULT_VERSION}",
        "released_at": utc_date(),
        "commit": current_head(),
        "summary": "Initial tracked baseline for the Forsetti-based Master Control Orchestration Server workspace.",
        "entries": [
            "Imported the current Forsetti-compliant Master Control Orchestration Server source tree.",
            "Established WinUI 3 shell, service host, browser dashboard, and bootstrapper scaffolding.",
            "Seeded repository-owned version, changelog, wiki, and README automation files.",
        ],
    }


def render_changelog(state: dict) -> str:
    lines = [
        "# Changelog",
        "",
        "All notable changes to this repository are tracked here by the repository agents.",
        "",
        "## [Unreleased]",
        "- Changes pushed to `main` are promoted into the next numbered release automatically.",
        "",
    ]

    for release in state["history"]:
        lines.extend(
            [
                f"## [{release['version']}] - {release['released_at']}",
                f"### Summary",
                release["summary"],
                "",
                "### Included Changes",
            ]
        )
        lines.extend(f"- {entry}" for entry in release["entries"])
        lines.append("")

    return "\n".join(lines)


def render_release_page(release: dict) -> str:
    lines = [
        f"# Release {release['tag']}",
        "",
        f"- Version: `{release['version']}`",
        f"- Release date: `{release['released_at']}`",
        "",
        "## Summary",
        release["summary"],
        "",
        "## Included Changes",
    ]
    lines.extend(f"- {entry}" for entry in release["entries"])
    lines.append("")
    return "\n".join(lines)


def render_versions_index(state: dict) -> str:
    latest = state["history"][0]
    lines = [
        "# Version Index",
        "",
        f"Current tracked release: `{latest['tag']}`",
        "",
        "| Version | Date | Summary |",
        "| --- | --- | --- |",
    ]
    for release in state["history"]:
        summary = release["summary"].replace("|", "\\|")
        lines.append(
            f"| [{release['tag']}](v{release['version']}.md) | {release['released_at']} | {summary} |"
        )
    lines.append("")
    return "\n".join(lines)


def write_release_artifacts(state: dict) -> None:
    ensure_directory(VERSIONS_DIR)
    write_json(VERSION_FILE, state)
    write_text(CHANGELOG_FILE, render_changelog(state))
    write_text(VERSIONS_DIR / "index.md", render_versions_index(state))
    write_text(VERSIONS_DIR / "latest.md", render_release_page(state["history"][0]))
    for release in state["history"]:
        write_text(VERSIONS_DIR / f"v{release['version']}.md", render_release_page(release))


def init_state() -> dict:
    state = read_state()
    if not state["history"]:
        release = initial_release_record()
        state["current_version"] = release["version"]
        state["current_tag"] = release["tag"]
        state["released_at"] = release["released_at"]
        state["last_release_commit"] = release["commit"]
        state["history"] = [release]

    write_release_artifacts(state)
    return state


def normalize_entry(record: dict) -> str:
    return f"{record['subject']} ({record['author_name']})"


def bump_state(metadata_out: Path | None) -> dict:
    state = init_state()
    base_commit = state.get("last_release_commit")
    records = commit_records(f"{base_commit}..HEAD" if base_commit else None)
    if not records:
        metadata = {
            "version": state["current_version"],
            "tag": state["current_tag"],
            "notes_path": str(VERSIONS_DIR / f"v{state['current_version']}.md"),
            "target_commit": state["history"][0]["commit"],
            "changed": False,
        }
        if metadata_out is not None:
            write_json(metadata_out, metadata)
        return metadata

    level = detect_bump(records)
    next_version = bump_version(state["current_version"], level)
    release = {
        "version": next_version,
        "tag": f"v{next_version}",
        "released_at": utc_date(),
        "commit": current_head(),
        "summary": f"Automated {level} release for {PROJECT_NAME}.",
        "entries": [normalize_entry(record) for record in records],
    }

    state["current_version"] = release["version"]
    state["current_tag"] = release["tag"]
    state["released_at"] = release["released_at"]
    state["last_release_commit"] = release["commit"]
    state["history"] = [release, *state["history"]]

    write_release_artifacts(state)

    metadata = {
        "version": release["version"],
        "tag": release["tag"],
        "notes_path": str(VERSIONS_DIR / f"v{release['version']}.md"),
        "target_commit": release["commit"],
        "changed": True,
    }
    if metadata_out is not None:
        write_json(metadata_out, metadata)
    return metadata


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Manage repository version and changelog artifacts.")
    subparsers = parser.add_subparsers(dest="command", required=True)

    subparsers.add_parser("init", help="Seed version tracking files if they do not already exist.")

    bump_parser = subparsers.add_parser("bump", help="Create the next tracked release from commits since the last release.")
    bump_parser.add_argument("--metadata-out", type=Path, help="Optional JSON file to write release metadata into.")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.command == "init":
        init_state()
        return 0

    if args.command == "bump":
        bump_state(args.metadata_out)
        return 0

    raise ValueError(f"Unsupported command: {args.command}")


if __name__ == "__main__":
    raise SystemExit(main())
