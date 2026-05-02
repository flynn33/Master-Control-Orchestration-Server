#!/usr/bin/env python3
# Master Control Orchestration Server - SessionStart hook
# Copyright (c) 2026 James Daley. All Rights Reserved.
# Proprietary and Confidential.
#
# Prints a one-screen project orientation when a Claude Code session starts.
# Reads VERSION.json + the realignment manifest + mcos-memory state and
# emits a compact summary to stdout. The hook output appears in the session
# context so the assistant sees it on resume.

from __future__ import annotations

import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
STATE_FILE = ROOT / ".claude" / "mcp-state" / "mcos-memory.json"
VERSION_FILE = ROOT / "VERSION.json"
MANIFEST_FILE = ROOT / "handoff" / "realignment" / "manifest.json"


def _git(args):
    try:
        out = subprocess.run(["git"] + args, cwd=str(ROOT), capture_output=True, text=True, timeout=10)
        return out.stdout.strip()
    except Exception:
        return ""


def main():
    print("=" * 72)
    print("MCOS — session orientation")
    print("=" * 72)

    if VERSION_FILE.exists():
        try:
            v = json.loads(VERSION_FILE.read_text(encoding="utf-8"))
            print(f"Version    : {v.get('current_version','?')} ({v.get('released_at','?')})  tag={v.get('current_tag','?')}")
        except Exception:
            pass

    branch = _git(["branch", "--show-current"])
    head = _git(["log", "--oneline", "-1"])
    ahead = _git(["rev-list", "--count", "origin/main..HEAD"]) if _git(["remote"]) else ""
    print(f"Git        : branch={branch or '?'}  head={head or '?'}  ahead-of-origin/main={ahead or '?'}")

    if MANIFEST_FILE.exists():
        try:
            m = json.loads(MANIFEST_FILE.read_text(encoding="utf-8"))
            phases = m.get("phases", [])
            print(f"Manifest   : {len(phases)} phases declared")
        except Exception:
            pass

    if STATE_FILE.exists():
        try:
            s = json.loads(STATE_FILE.read_text(encoding="utf-8"))
            facts = s.get("facts", [])
            ps = s.get("phaseState", {})
            done = sum(1 for v in ps.values() if v.get("status") == "complete")
            inprog = sum(1 for v in ps.values() if v.get("status") == "in-progress")
            print(f"Memory     : {len(facts)} facts  |  phases: {done} complete, {inprog} in-progress")
            # Last 3 phase events
            phase_events = sorted(
                [f for f in facts if f.get("kind") == "phase-event"],
                key=lambda f: f.get("ts", ""),
                reverse=True,
            )[:3]
            if phase_events:
                print("Recent phases:")
                for f in phase_events:
                    print(f"  - {f.get('ts','')}  {f.get('title','')}")
            # Open deferred items
            deferred = [f for f in facts if f.get("kind") == "deferred"]
            if deferred:
                print(f"Deferred   : {len(deferred)} item(s) open")
                for f in deferred[:5]:
                    print(f"  - {f.get('title','')}  [tags: {','.join(f.get('tags', []))}]")
        except Exception as e:
            print(f"Memory     : (state file present but could not parse: {e})")
    else:
        print("Memory     : (none yet — run mcos-memory.summarize)")

    print()
    print("Sub-agents : mcos-architect, mcos-contract-auditor, mcos-debugger,")
    print("             mcos-researcher, mcos-code-reviewer, mcos-phase-planner,")
    print("             plus the inherited forsetti-governance-reviewer / mcp-gateway-reviewer /")
    print("             qa-release-gate / windows-native-cpp-reviewer.")
    print("MCP servers: mcos-memory  -> persistent project memory")
    print("             mcos-contracts -> on-demand contract enforcement")
    print()
    print("To orient: call mcos-memory.recall(query=...) or mcos-memory.summarize() before editing.")
    print("=" * 72)


if __name__ == "__main__":
    try:
        main()
    except Exception as e:
        print(f"[session-start hook error: {e}]")
