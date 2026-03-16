from __future__ import annotations

import json
import subprocess
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[2]
PROJECT_NAME = "Master Control Program"
REPOSITORY_NAME = "master-control-dashboard"
REPOSITORY_URL = "https://github.com/flynn33/master-control-dashboard"
DEFAULT_VERSION = "0.1.0"
AGENT_COMMIT_PREFIX = "chore(agents):"


def run_git(args: list[str], cwd: Path | None = None, check: bool = True) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["git", *args],
        cwd=str(cwd or ROOT),
        check=check,
        capture_output=True,
        text=True,
        encoding="utf-8",
    )


def git_output(args: list[str], cwd: Path | None = None) -> str:
    return run_git(args, cwd=cwd).stdout.strip()


def ensure_directory(path: Path) -> None:
    path.mkdir(parents=True, exist_ok=True)


def read_json(path: Path, fallback: Any) -> Any:
    if not path.exists():
        return fallback

    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, payload: Any) -> None:
    ensure_directory(path.parent)
    path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


def write_text(path: Path, content: str) -> None:
    ensure_directory(path.parent)
    path.write_text(content.rstrip() + "\n", encoding="utf-8")


def utc_now() -> datetime:
    return datetime.now(timezone.utc)


def utc_date() -> str:
    return utc_now().date().isoformat()


def parse_semver(version: str) -> tuple[int, int, int]:
    major, minor, patch = version.split(".")
    return int(major), int(minor), int(patch)


def format_semver(parts: tuple[int, int, int]) -> str:
    return f"{parts[0]}.{parts[1]}.{parts[2]}"
