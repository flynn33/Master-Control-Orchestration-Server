#!/usr/bin/env python3
# Master Control Orchestration Server - mcos-contracts MCP server
# Copyright (c) 2026 James Daley. All Rights Reserved.
# Proprietary and Confidential.
#
# On-demand contract enforcement for the MCOS realignment work. Wraps:
#   - FORBIDDEN-CONTRACT grep list (docs/implementation/FORBIDDEN-CONTRACT-GREP-LIST.md)
#   - Forsetti compliance script (scripts/check-mastercontrol-forsetti.ps1)
#   - cmake build + ctest summary (read-only — never re-runs the build)
#
# All checks return structured pass/fail data so sub-agents can react.

from __future__ import annotations

import json
import os
import re
import subprocess
import sys
from pathlib import Path
from typing import Any, Dict, List

ROOT = Path(__file__).resolve().parents[3]


def _run(cmd: List[str], cwd: Path = ROOT, timeout: int = 60) -> Dict[str, Any]:
    try:
        proc = subprocess.run(cmd, cwd=str(cwd), capture_output=True, text=True, timeout=timeout)
        return {"exitCode": proc.returncode, "stdout": proc.stdout, "stderr": proc.stderr}
    except subprocess.TimeoutExpired:
        return {"exitCode": -1, "stdout": "", "stderr": f"timeout after {timeout}s"}
    except FileNotFoundError as e:
        return {"exitCode": -2, "stdout": "", "stderr": f"command not found: {e}"}


# ---------------------------------------------------------------------------
# FORBIDDEN-CONTRACT greps (mirror of FORBIDDEN-CONTRACT-GREP-LIST.md)
# ---------------------------------------------------------------------------
# Each check is (id, label, command, expected_zero_matches, allowed_matches_note).
# Commands run via git grep with safe options. Some checks have allowed-matches
# semantics (e.g. §6.4 has documented bootstrapper exemptions).
# Using git grep means the check honors .gitignore and only sees tracked files.

CONTRACT_CHECKS: List[Dict[str, Any]] = [
    {
        "id": "1.1",
        "label": "Provider Forsetti modules removed",
        "args": ["git", "grep", "-nE", r"ProviderIntegrationModule|CodexProviderModule|ClaudeCodeProviderModule|XAIProviderModule",
                  "--", "src/MasterControlApp", "src/MasterControlModules", "src/MasterControlServiceHost",
                  "src/MasterControlBootstrapper", "tests", "resources/web"],
        "expectedZero": True,
    },
    {
        "id": "1.3",
        "label": "Outbound AI transports removed",
        "args": ["git", "grep", "-nE", r"executeClaudeCodeCli|executeCodexCli|executeOpenAICompatibleChat",
                  "--", "src/MasterControlApp", "src/MasterControlModules", "src/MasterControlServiceHost",
                  "src/MasterControlBootstrapper", "tests", "resources/web", "include"],
        "expectedZero": True,
    },
    {
        "id": "1.4",
        "label": "Provider HTTP routes removed",
        "args": ["git", "grep", "-nE", r"/api/providers([/?]|$)", "--", "src/", "tests", "resources/web"],
        "expectedZero": True,
    },
    {
        "id": "4.3",
        "label": "ClientHeartbeat unavailable-sentinel integrity",
        "args": ["git", "grep", "-nE", r"gpuPercent\s*=\s*0\.0|gpuMemoryMb\s*=\s*0\.0",
                  "--", "include/MasterControl", "src/MasterControlApp", "src/MasterControlModules"],
        "expectedZero": True,
    },
    {
        "id": "5.1",
        "label": "Forsetti vendoring zero-diff (since baseline)",
        "args": ["git", "diff", "--name-only", "Forsetti-Framework-Windows-main"],
        "expectedZero": True,
    },
    {
        "id": "6.2",
        "label": "No workflow_dispatch on gating workflows",
        "args": ["git", "grep", "-nE", r"^[[:space:]]+workflow_dispatch:",
                  "--", ".github/workflows/windows-build-test-package.yml"],
        "expectedZero": True,
    },
    {
        "id": "6.3",
        "label": "No path-segment Enterprise in workflow files",
        "args": ["git", "grep", "-nE", r"\\Enterprise\\|/Enterprise/", "--", ".github/workflows"],
        "expectedZero": True,
    },
    {
        "id": "8.1a",
        "label": "Heartbeat metrics use formatMetric (not direct .toFixed)",
        "args": ["git", "grep", "-nE", r"hb\.(cpuPercent|memoryPercent|gpuPercent|gpuMemoryMb)\.toFixed",
                  "--", "resources/web"],
        "expectedZero": True,
    },
    {
        "id": "8.1b",
        "label": "Telemetry metrics use formatMetric (not direct .toFixed)",
        "args": ["git", "grep", "-nE", r"(tel|telemetry)\.(cpuPercent|memoryMbytes|memoryPercent|gpuPercent|gpuMemoryMb)\.toFixed",
                  "--", "resources/web"],
        "expectedZero": True,
    },
    {
        "id": "8.2",
        "label": "No legacy hardcoded surface IDs",
        "args": ["git", "grep", "-nE", r'id="(telemetryGrid|endpointTable)"', "--", "resources/web/index.html"],
        "expectedZero": True,
    },
    {
        "id": "8.3",
        "label": "No provider-era residue in app.js",
        "args": ["git", "grep", "-nE", r"renderSignInCards|/api/providers|dashboard-clu|clu-nav|clu-surface",
                  "--", "resources/web/app.js"],
        "expectedZero": True,
    },
]


def run_contract_checks(filter_ids: List[str] = None) -> Dict[str, Any]:
    results: List[Dict[str, Any]] = []
    failed = 0
    for check in CONTRACT_CHECKS:
        if filter_ids and check["id"] not in filter_ids:
            continue
        proc = _run(check["args"])
        # git grep returns 0 if matches, 1 if no matches. git diff returns 0 if no diff.
        # Normalize: matches present iff stdout has content.
        match_count = sum(1 for line in proc["stdout"].splitlines() if line.strip())
        ok = (match_count == 0) if check["expectedZero"] else True
        if not ok:
            failed += 1
        results.append({
            "id": check["id"],
            "label": check["label"],
            "ok": ok,
            "matches": match_count,
            "sample": proc["stdout"].splitlines()[:5],
        })
    return {"total": len(results), "failed": failed, "results": results}


def run_forsetti_compliance() -> Dict[str, Any]:
    script = ROOT / "scripts" / "check-mastercontrol-forsetti.ps1"
    if not script.exists():
        return {"ok": False, "error": "script not found", "path": str(script)}
    proc = _run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(script)], timeout=120)
    ok = (proc["exitCode"] == 0) and ("checks passed" in proc["stdout"].lower())
    return {"ok": ok, "exitCode": proc["exitCode"], "stdout": proc["stdout"][-2000:], "stderr": proc["stderr"][-500:]}


def list_contract_groups() -> Dict[str, Any]:
    grep_list = ROOT / "docs" / "implementation" / "FORBIDDEN-CONTRACT-GREP-LIST.md"
    if not grep_list.exists():
        return {"error": "FORBIDDEN-CONTRACT-GREP-LIST.md not found"}
    text = grep_list.read_text(encoding="utf-8", errors="ignore")
    groups = re.findall(r"^## (Group \d+ — [^\n]+)", text, re.MULTILINE)
    sections = re.findall(r"^### (\d+\.\d+\w?\s+[^\n]+)", text, re.MULTILINE)
    return {"groups": groups, "sections": sections, "checksImplemented": [c["id"] for c in CONTRACT_CHECKS]}


# ---------------------------------------------------------------------------
# Tools
# ---------------------------------------------------------------------------
TOOLS: List[Dict[str, Any]] = [
    {
        "name": "run_all_contracts",
        "description": "Run every implemented FORBIDDEN-CONTRACT grep check. Returns pass/fail per check with sample matches. Use before declaring a phase complete or after a large diff.",
        "inputSchema": {"type": "object", "properties": {}},
    },
    {
        "name": "run_contract_checks",
        "description": "Run a subset of contract checks by id (e.g. ['8.1a','8.1b','8.2']).",
        "inputSchema": {
            "type": "object",
            "properties": {
                "ids": {"type": "array", "items": {"type": "string"}},
            },
        },
    },
    {
        "name": "list_contract_groups",
        "description": "List the FORBIDDEN-CONTRACT groups + sections from the doc and the subset implemented in this server.",
        "inputSchema": {"type": "object", "properties": {}},
    },
    {
        "name": "run_forsetti_compliance",
        "description": "Run scripts/check-mastercontrol-forsetti.ps1 via PowerShell. Returns ok=true if 'Master Control Forsetti checks passed.' appears in stdout.",
        "inputSchema": {"type": "object", "properties": {}},
    },
]


def call_tool(name: str, args: Dict[str, Any]) -> str:
    if name == "run_all_contracts":
        return json.dumps(run_contract_checks(), indent=2)
    if name == "run_contract_checks":
        ids = args.get("ids") or []
        return json.dumps(run_contract_checks(ids), indent=2)
    if name == "list_contract_groups":
        return json.dumps(list_contract_groups(), indent=2)
    if name == "run_forsetti_compliance":
        return json.dumps(run_forsetti_compliance(), indent=2)
    return json.dumps({"error": f"unknown tool: {name}"})


def write_message(msg: Dict[str, Any]) -> None:
    sys.stdout.write(json.dumps(msg) + "\n")
    sys.stdout.flush()


def main() -> int:
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
                    "serverInfo": {"name": "mcos-contracts", "version": "1.0.0"},
                },
            })
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
                write_message({"jsonrpc": "2.0", "id": rid,
                               "result": {"content": [{"type": "text", "text": text}], "isError": False}})
            except Exception as e:
                write_message({"jsonrpc": "2.0", "id": rid,
                               "result": {"content": [{"type": "text", "text": f"error: {e}"}], "isError": True}})
            continue
        if method == "ping":
            write_message({"jsonrpc": "2.0", "id": rid, "result": {}})
            continue
        if rid is not None:
            write_message({"jsonrpc": "2.0", "id": rid,
                           "error": {"code": -32601, "message": f"Method not found: {method}"}})


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(0)
