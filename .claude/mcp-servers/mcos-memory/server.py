#!/usr/bin/env python3
# Master Control Orchestration Server - mcos-memory MCP server
# Copyright (c) 2026 James Daley. All Rights Reserved.
# Proprietary and Confidential.
#
# Persistent project memory for the MCOS realignment work. Stores facts
# (notes, file annotations, phase state, deferred work, contracts, decisions)
# in a single JSON file so they survive across sessions and remain greppable
# with stock tools.
#
# This is a hand-rolled stdio JSON-RPC MCP server using only the Python
# standard library so the project does not depend on the `mcp` SDK package
# being installed on every machine. Protocol target: MCP 2024-11-05.

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
STATE_DIR.mkdir(parents=True, exist_ok=True)
STATE_FILE = STATE_DIR / "mcos-memory.json"


def _empty_state() -> Dict[str, Any]:
    return {
        "schemaVersion": 1,
        "facts": [],          # list of {id, kind, tags, title, body, source, ts}
        "phaseState": {},     # phase id -> {status, notes, completedAt, deferred[]}
        "files": {},          # absolute or repo-relative path -> [{ts, note, tags}]
    }


def load_state() -> Dict[str, Any]:
    if not STATE_FILE.exists():
        return _empty_state()
    try:
        return json.loads(STATE_FILE.read_text(encoding="utf-8"))
    except Exception:
        return _empty_state()


def save_state(state: Dict[str, Any]) -> None:
    tmp = STATE_FILE.with_suffix(".json.tmp")
    tmp.write_text(json.dumps(state, indent=2, ensure_ascii=False), encoding="utf-8")
    tmp.replace(STATE_FILE)


# ---------------------------------------------------------------------------
# Tools
# ---------------------------------------------------------------------------
TOOLS: List[Dict[str, Any]] = [
    {
        "name": "remember",
        "description": "Store a project fact (note, decision, contract, deferred work, file annotation, etc.). Returns the fact id. Tags allow later filtering.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "kind": {"type": "string", "description": "Fact category: 'note' | 'decision' | 'contract' | 'deferred' | 'risk' | 'phase-event' | 'file-annotation' | 'invariant' | 'todo'."},
                "title": {"type": "string", "description": "One-line summary."},
                "body": {"type": "string", "description": "Full detail. Markdown OK."},
                "tags": {"type": "array", "items": {"type": "string"}, "description": "Tags for filtering, e.g. ['phase-08','adr-002','telemetry']."},
                "source": {"type": "string", "description": "Where this came from — file path, commit SHA, phase id, URL, etc. Optional."},
            },
            "required": ["kind", "title"],
        },
    },
    {
        "name": "recall",
        "description": "Search the memory by free-text query and/or tags. Returns matching facts ordered most-recent first. Use this at session start to orient yourself before editing anything.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "query": {"type": "string", "description": "Free-text substring searched in title + body + tags + source. Case-insensitive."},
                "tags": {"type": "array", "items": {"type": "string"}, "description": "Required tags. All must match."},
                "kind": {"type": "string", "description": "Filter by fact kind."},
                "limit": {"type": "integer", "description": "Max results (default 25)."},
            },
        },
    },
    {
        "name": "list_facts",
        "description": "List all facts (or by kind). Use sparingly — recall() with a query is usually more useful.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "kind": {"type": "string"},
                "limit": {"type": "integer"},
            },
        },
    },
    {
        "name": "update_fact",
        "description": "Replace or append fields on an existing fact by id.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "id": {"type": "string"},
                "title": {"type": "string"},
                "body": {"type": "string"},
                "tags": {"type": "array", "items": {"type": "string"}},
                "kind": {"type": "string"},
                "source": {"type": "string"},
            },
            "required": ["id"],
        },
    },
    {
        "name": "forget",
        "description": "Delete a fact by id. Irreversible. Use sparingly — generally prefer to mark facts stale via update_fact.",
        "inputSchema": {
            "type": "object",
            "properties": {"id": {"type": "string"}},
            "required": ["id"],
        },
    },
    {
        "name": "set_phase_state",
        "description": "Record the status of a realignment phase (PHASE-00..PHASE-11). Use whenever a phase completes.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "phase": {"type": "string", "description": "e.g. 'PHASE-10'"},
                "status": {"type": "string", "description": "'pending' | 'in-progress' | 'complete' | 'deferred'"},
                "commit": {"type": "string", "description": "phase commit SHA (optional)"},
                "report": {"type": "string", "description": "path to the completion report"},
                "notes": {"type": "string"},
                "deferred": {"type": "array", "items": {"type": "string"}, "description": "Items deferred from this phase."},
            },
            "required": ["phase", "status"],
        },
    },
    {
        "name": "get_phase_state",
        "description": "Return the current status of one phase or all phases. Pass phase='all' or omit to get the full map.",
        "inputSchema": {
            "type": "object",
            "properties": {"phase": {"type": "string"}},
        },
    },
    {
        "name": "annotate_file",
        "description": "Attach a note to a specific file path. Useful for 'gotchas' that future sessions need to know about (e.g. 'Windows.h max() macro collision — use (std::max)(...)').",
        "inputSchema": {
            "type": "object",
            "properties": {
                "path": {"type": "string", "description": "Repo-relative or absolute path."},
                "note": {"type": "string"},
                "tags": {"type": "array", "items": {"type": "string"}},
            },
            "required": ["path", "note"],
        },
    },
    {
        "name": "get_file_annotations",
        "description": "Get all annotations attached to a file path.",
        "inputSchema": {
            "type": "object",
            "properties": {"path": {"type": "string"}},
            "required": ["path"],
        },
    },
    {
        "name": "summarize",
        "description": "Return a one-screen project status summary (counts by kind, phase state map, recent activity). Use this at session start.",
        "inputSchema": {"type": "object", "properties": {}},
    },
]


def _matches(fact: Dict[str, Any], query: Optional[str], tags: Optional[List[str]],
             kind: Optional[str]) -> bool:
    if kind and fact.get("kind") != kind:
        return False
    if tags:
        ftags = set(fact.get("tags", []) or [])
        if not all(t in ftags for t in tags):
            return False
    if query:
        haystack = " ".join([
            str(fact.get("title", "")),
            str(fact.get("body", "")),
            str(fact.get("source", "")),
            " ".join(fact.get("tags", []) or []),
        ]).lower()
        if query.lower() not in haystack:
            return False
    return True


def call_tool(name: str, args: Dict[str, Any]) -> str:
    state = load_state()
    if name == "remember":
        fid = uuid.uuid4().hex[:12]
        fact = {
            "id": fid,
            "kind": args.get("kind", "note"),
            "title": args.get("title", ""),
            "body": args.get("body", ""),
            "tags": args.get("tags", []) or [],
            "source": args.get("source", ""),
            "ts": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        }
        state["facts"].append(fact)
        save_state(state)
        return json.dumps({"id": fid, "stored": fact}, indent=2)

    if name == "recall":
        q = args.get("query")
        tags = args.get("tags")
        kind = args.get("kind")
        limit = int(args.get("limit", 25) or 25)
        matches = [f for f in state["facts"] if _matches(f, q, tags, kind)]
        matches.sort(key=lambda f: f.get("ts", ""), reverse=True)
        return json.dumps({"count": len(matches), "results": matches[:limit]}, indent=2)

    if name == "list_facts":
        kind = args.get("kind")
        limit = int(args.get("limit", 100) or 100)
        out = [f for f in state["facts"] if (kind is None or f.get("kind") == kind)]
        out.sort(key=lambda f: f.get("ts", ""), reverse=True)
        return json.dumps({"count": len(out), "results": out[:limit]}, indent=2)

    if name == "update_fact":
        fid = args["id"]
        for f in state["facts"]:
            if f["id"] == fid:
                for k in ("title", "body", "tags", "kind", "source"):
                    if k in args and args[k] is not None:
                        f[k] = args[k]
                f["ts"] = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
                save_state(state)
                return json.dumps({"updated": f}, indent=2)
        return json.dumps({"error": f"id {fid} not found"})

    if name == "forget":
        fid = args["id"]
        before = len(state["facts"])
        state["facts"] = [f for f in state["facts"] if f["id"] != fid]
        save_state(state)
        return json.dumps({"removed": before - len(state["facts"])})

    if name == "set_phase_state":
        phase = args["phase"]
        existing = state["phaseState"].get(phase, {})
        existing.update({k: v for k, v in args.items() if k != "phase" and v is not None})
        existing["updatedAt"] = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
        state["phaseState"][phase] = existing
        save_state(state)
        return json.dumps({"phase": phase, "state": existing}, indent=2)

    if name == "get_phase_state":
        phase = args.get("phase")
        if phase and phase != "all":
            return json.dumps({phase: state["phaseState"].get(phase)}, indent=2)
        return json.dumps(state["phaseState"], indent=2)

    if name == "annotate_file":
        path = args["path"]
        entry = {
            "ts": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
            "note": args["note"],
            "tags": args.get("tags", []) or [],
        }
        state["files"].setdefault(path, []).append(entry)
        save_state(state)
        return json.dumps({"path": path, "annotation": entry}, indent=2)

    if name == "get_file_annotations":
        return json.dumps({"path": args["path"], "annotations": state["files"].get(args["path"], [])}, indent=2)

    if name == "summarize":
        kind_counts: Dict[str, int] = {}
        for f in state["facts"]:
            kind_counts[f.get("kind", "note")] = kind_counts.get(f.get("kind", "note"), 0) + 1
        recent = sorted(state["facts"], key=lambda f: f.get("ts", ""), reverse=True)[:5]
        return json.dumps({
            "factCount": len(state["facts"]),
            "byKind": kind_counts,
            "phaseState": state["phaseState"],
            "annotatedFiles": list(state["files"].keys())[:20],
            "recent": [{"ts": f.get("ts"), "kind": f.get("kind"), "title": f.get("title")} for f in recent],
            "stateFile": str(STATE_FILE),
        }, indent=2)

    return json.dumps({"error": f"unknown tool: {name}"})


# ---------------------------------------------------------------------------
# JSON-RPC stdio loop (MCP protocol 2024-11-05)
# ---------------------------------------------------------------------------
def write_message(msg: Dict[str, Any]) -> None:
    sys.stdout.write(json.dumps(msg) + "\n")
    sys.stdout.flush()


def main() -> int:
    initialized = False
    while True:
        line = sys.stdin.readline()
        if not line:
            return 0
        line = line.strip()
        if not line:
            continue
        try:
            req = json.loads(line)
        except json.JSONDecodeError:
            continue

        method = req.get("method")
        rid = req.get("id")
        params = req.get("params") or {}

        if method == "initialize":
            write_message({
                "jsonrpc": "2.0",
                "id": rid,
                "result": {
                    "protocolVersion": "2024-11-05",
                    "capabilities": {"tools": {"listChanged": False}},
                    "serverInfo": {"name": "mcos-memory", "version": "1.0.0"},
                },
            })
            initialized = True
            continue

        if method == "notifications/initialized":
            continue

        if method == "tools/list":
            write_message({"jsonrpc": "2.0", "id": rid, "result": {"tools": TOOLS}})
            continue

        if method == "tools/call":
            tname = params.get("name")
            targs = params.get("arguments") or {}
            try:
                text = call_tool(tname, targs)
                write_message({
                    "jsonrpc": "2.0",
                    "id": rid,
                    "result": {"content": [{"type": "text", "text": text}], "isError": False},
                })
            except Exception as e:
                write_message({
                    "jsonrpc": "2.0",
                    "id": rid,
                    "result": {"content": [{"type": "text", "text": f"error: {e}"}], "isError": True},
                })
            continue

        if method == "ping":
            write_message({"jsonrpc": "2.0", "id": rid, "result": {}})
            continue

        if rid is not None:
            write_message({
                "jsonrpc": "2.0",
                "id": rid,
                "error": {"code": -32601, "message": f"Method not found: {method}"},
            })


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(0)
