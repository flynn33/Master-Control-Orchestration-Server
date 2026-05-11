#!/usr/bin/env python3
# Master Control Orchestration Server - mcos-watcher MCP server
# Copyright (c) 2026 James Daley. All Rights Reserved.
# Proprietary and Confidential.
#
# File-watcher MCP for the MCOS realignment work. Polls registered paths on
# demand (lazy snapshot, no background thread) and reports add/modify/delete
# events since a caller-supplied baseline timestamp.
#
# Hand-rolled stdio JSON-RPC MCP server using only the Python standard
# library so the project does not depend on the `mcp` SDK package or the
# `watchdog` package being installed on every machine.
# Protocol target: MCP 2024-11-05.

from __future__ import annotations

import json
import os
import sys
import time
import uuid
from pathlib import Path
from typing import Any, Dict, List, Optional


# ---------------------------------------------------------------------------
# Storage
# ---------------------------------------------------------------------------
ROOT = Path(__file__).resolve().parents[3]   # repo root
STATE_DIR = ROOT / ".claude" / "mcp-state"
STATE_FILE = STATE_DIR / "mcos-watcher.json"

# In-memory watch registry. Persisted to STATE_FILE so watches survive
# server restarts. Each watch entry:
#   {
#     "id": "<uuid>",
#     "root": "<absolute path>",
#     "recursive": true,
#     "include_globs": [...],
#     "exclude_globs": [...],
#     "created_at": <epoch>,
#     "last_snapshot": { "<rel path>": {"mtime": float, "size": int} }
#   }
WATCHES: Dict[str, Dict[str, Any]] = {}

# Default exclusions to keep snapshots cheap on this repo. The build dir
# alone has ~10k+ files and the .git dir thrashes constantly during work.
DEFAULT_EXCLUDES = [
    ".git/**",
    "build/**",
    "dist/**",
    "**/__pycache__/**",
    "**/.nuget/**",
    "**/node_modules/**",
    "**/.vs/**",
    ".remember/**",
]


# ---------------------------------------------------------------------------
# Persistence
# ---------------------------------------------------------------------------
def _load() -> None:
    global WATCHES
    if STATE_FILE.exists():
        try:
            WATCHES = json.loads(STATE_FILE.read_text(encoding="utf-8"))
        except (json.JSONDecodeError, OSError):
            WATCHES = {}
    else:
        WATCHES = {}


def _save() -> None:
    STATE_DIR.mkdir(parents=True, exist_ok=True)
    tmp = STATE_FILE.with_suffix(".json.tmp")
    tmp.write_text(json.dumps(WATCHES, indent=2), encoding="utf-8")
    os.replace(tmp, STATE_FILE)


# ---------------------------------------------------------------------------
# Snapshot / diff
# ---------------------------------------------------------------------------
def _matches_any(rel_path: str, patterns: List[str]) -> bool:
    from fnmatch import fnmatch
    norm = rel_path.replace(os.sep, "/")
    for pat in patterns:
        if fnmatch(norm, pat):
            return True
    return False


def _snapshot(watch: Dict[str, Any]) -> Dict[str, Dict[str, Any]]:
    root = Path(watch["root"])
    if not root.exists():
        return {}
    snap: Dict[str, Dict[str, Any]] = {}
    excludes = watch.get("exclude_globs", []) + DEFAULT_EXCLUDES
    includes = watch.get("include_globs", [])
    iterator = root.rglob("*") if watch.get("recursive", True) else root.iterdir()
    for path in iterator:
        if not path.is_file():
            continue
        try:
            rel = str(path.relative_to(root)).replace(os.sep, "/")
        except ValueError:
            continue
        if _matches_any(rel, excludes):
            continue
        if includes and not _matches_any(rel, includes):
            continue
        try:
            st = path.stat()
        except OSError:
            continue
        snap[rel] = {"mtime": st.st_mtime, "size": st.st_size}
    return snap


def _diff(old: Dict[str, Dict[str, Any]], new: Dict[str, Dict[str, Any]]) -> List[Dict[str, Any]]:
    events: List[Dict[str, Any]] = []
    old_keys = set(old)
    new_keys = set(new)
    for added in sorted(new_keys - old_keys):
        events.append({"event": "added", "path": added, "size": new[added]["size"]})
    for removed in sorted(old_keys - new_keys):
        events.append({"event": "removed", "path": removed})
    for common in sorted(old_keys & new_keys):
        if old[common]["mtime"] != new[common]["mtime"] or old[common]["size"] != new[common]["size"]:
            events.append({
                "event": "modified",
                "path": common,
                "size": new[common]["size"],
                "old_mtime": old[common]["mtime"],
                "new_mtime": new[common]["mtime"],
            })
    return events


# ---------------------------------------------------------------------------
# Tool implementations
# ---------------------------------------------------------------------------
def tool_watch_start(args: Dict[str, Any]) -> Dict[str, Any]:
    raw_root = args.get("root")
    if not raw_root:
        return {"error": "root is required"}
    root = Path(raw_root)
    if not root.is_absolute():
        root = (ROOT / root).resolve()
    if not root.exists():
        return {"error": f"root does not exist: {root}"}
    watch_id = str(uuid.uuid4())
    watch = {
        "id": watch_id,
        "root": str(root),
        "recursive": bool(args.get("recursive", True)),
        "include_globs": list(args.get("include_globs", [])),
        "exclude_globs": list(args.get("exclude_globs", [])),
        "created_at": time.time(),
    }
    watch["last_snapshot"] = _snapshot(watch)
    WATCHES[watch_id] = watch
    _save()
    return {
        "id": watch_id,
        "root": watch["root"],
        "tracked_files": len(watch["last_snapshot"]),
    }


def tool_watch_stop(args: Dict[str, Any]) -> Dict[str, Any]:
    watch_id = args.get("id")
    if not watch_id or watch_id not in WATCHES:
        return {"error": "unknown watch id"}
    removed = WATCHES.pop(watch_id)
    _save()
    return {"id": watch_id, "root": removed["root"], "removed": True}


def tool_watch_list(_args: Dict[str, Any]) -> Dict[str, Any]:
    return {
        "watches": [
            {
                "id": w["id"],
                "root": w["root"],
                "recursive": w.get("recursive", True),
                "tracked_files": len(w.get("last_snapshot", {})),
                "created_at": w.get("created_at"),
            }
            for w in WATCHES.values()
        ]
    }


def tool_watch_poll(args: Dict[str, Any]) -> Dict[str, Any]:
    watch_id = args.get("id")
    if not watch_id or watch_id not in WATCHES:
        return {"error": "unknown watch id"}
    watch = WATCHES[watch_id]
    new_snapshot = _snapshot(watch)
    events = _diff(watch.get("last_snapshot", {}), new_snapshot)
    watch["last_snapshot"] = new_snapshot
    watch["last_polled_at"] = time.time()
    _save()
    return {
        "id": watch_id,
        "polled_at": watch["last_polled_at"],
        "tracked_files": len(new_snapshot),
        "events": events,
    }


def tool_watch_reset(args: Dict[str, Any]) -> Dict[str, Any]:
    watch_id = args.get("id")
    if not watch_id or watch_id not in WATCHES:
        return {"error": "unknown watch id"}
    watch = WATCHES[watch_id]
    watch["last_snapshot"] = _snapshot(watch)
    watch["last_polled_at"] = time.time()
    _save()
    return {"id": watch_id, "tracked_files": len(watch["last_snapshot"]), "reset": True}


TOOLS = {
    "watch_start": {
        "fn": tool_watch_start,
        "description": "Register a path to watch. Returns a watch id and the count of files in the initial snapshot. Honors include_globs / exclude_globs (in addition to default excludes for build/, .git/, node_modules/, etc.).",
        "inputSchema": {
            "type": "object",
            "properties": {
                "root": {"type": "string", "description": "Absolute or repo-relative path to watch."},
                "recursive": {"type": "boolean", "default": True},
                "include_globs": {"type": "array", "items": {"type": "string"}},
                "exclude_globs": {"type": "array", "items": {"type": "string"}},
            },
            "required": ["root"],
        },
    },
    "watch_stop": {
        "fn": tool_watch_stop,
        "description": "Stop and remove a watch by id.",
        "inputSchema": {
            "type": "object",
            "properties": {"id": {"type": "string"}},
            "required": ["id"],
        },
    },
    "watch_list": {
        "fn": tool_watch_list,
        "description": "List active watches with their roots and tracked-file counts.",
        "inputSchema": {"type": "object", "properties": {}},
    },
    "watch_poll": {
        "fn": tool_watch_poll,
        "description": "Diff the watch root against the previous snapshot. Returns added/removed/modified events and re-baselines the snapshot.",
        "inputSchema": {
            "type": "object",
            "properties": {"id": {"type": "string"}},
            "required": ["id"],
        },
    },
    "watch_reset": {
        "fn": tool_watch_reset,
        "description": "Re-baseline a watch's snapshot without returning events. Use after consuming a noisy diff to start clean.",
        "inputSchema": {
            "type": "object",
            "properties": {"id": {"type": "string"}},
            "required": ["id"],
        },
    },
}


# ---------------------------------------------------------------------------
# JSON-RPC plumbing (MCP 2024-11-05 stdio transport)
# ---------------------------------------------------------------------------
def _result(req_id: Any, result: Any) -> Dict[str, Any]:
    return {"jsonrpc": "2.0", "id": req_id, "result": result}


def _error(req_id: Any, code: int, message: str) -> Dict[str, Any]:
    return {"jsonrpc": "2.0", "id": req_id, "error": {"code": code, "message": message}}


def handle(request: Dict[str, Any]) -> Optional[Dict[str, Any]]:
    method = request.get("method")
    req_id = request.get("id")
    params = request.get("params") or {}

    if method == "initialize":
        return _result(req_id, {
            "protocolVersion": "2024-11-05",
            "capabilities": {"tools": {}},
            "serverInfo": {"name": "mcos-watcher", "version": "0.1.0"},
        })
    if method == "notifications/initialized":
        return None
    if method == "tools/list":
        return _result(req_id, {
            "tools": [
                {"name": name, "description": meta["description"], "inputSchema": meta["inputSchema"]}
                for name, meta in TOOLS.items()
            ]
        })
    if method == "tools/call":
        name = params.get("name")
        args = params.get("arguments") or {}
        if name not in TOOLS:
            return _error(req_id, -32601, f"unknown tool: {name}")
        try:
            data = TOOLS[name]["fn"](args)
        except Exception as exc:  # surface the real error to the caller
            return _error(req_id, -32000, f"{name} failed: {exc!r}")
        return _result(req_id, {
            "content": [{"type": "text", "text": json.dumps(data, indent=2)}],
            "isError": "error" in data,
        })
    if req_id is None:
        return None
    return _error(req_id, -32601, f"method not found: {method}")


def main() -> None:
    _load()
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            request = json.loads(line)
        except json.JSONDecodeError:
            continue
        response = handle(request)
        if response is not None:
            sys.stdout.write(json.dumps(response) + "\n")
            sys.stdout.flush()


if __name__ == "__main__":
    main()
