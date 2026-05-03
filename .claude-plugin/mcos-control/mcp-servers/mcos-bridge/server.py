#!/usr/bin/env python3
# Master Control Orchestration Server - mcos-bridge MCP server
# Copyright (c) 2026 James Daley. All Rights Reserved.
# Proprietary and Confidential.
#
# Bridges Claude Code to a running MCOS instance over its admin HTTP API.
# Trust posture: ADR-001 LAN-trusted operator surface (no app-layer auth).
# This bridge talks HTTP to MCOS_BASE_URL (default http://localhost:7300)
# and translates Claude Code MCP tool calls to MCOS admin API requests.
#
# Pure-stdlib Python so the plugin works on any host with python3.9+.

from __future__ import annotations

import json
import os
import platform
import shlex
import subprocess
import sys
import time
import urllib.error
import urllib.request
from typing import Any, Dict, List, Optional, Tuple


# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------
DEFAULT_BASE_URL = os.environ.get("MCOS_BASE_URL", "http://localhost:7300")
DEFAULT_TIMEOUT = float(os.environ.get("MCOS_TIMEOUT", "10.0"))
PLUGIN_VERSION = "1.0.0"
PLUGIN_USER_AGENT = f"mcos-control-plugin/{PLUGIN_VERSION}"


# ---------------------------------------------------------------------------
# HTTP transport
# ---------------------------------------------------------------------------
def _http(method: str, path: str, body: Optional[Dict[str, Any]] = None,
          base_url: Optional[str] = None, timeout: Optional[float] = None) -> Dict[str, Any]:
    """Make an HTTP call to MCOS. Returns a structured result dict.

    Success: {"ok": True, "status": <code>, "body": <parsed JSON or text>}
    Failure: {"ok": False, "error": <message>, "errorCode": <code>, "status": <int|None>}
    """
    base = (base_url or DEFAULT_BASE_URL).rstrip("/")
    url = f"{base}{path}"
    data = None
    headers = {"User-Agent": PLUGIN_USER_AGENT, "Accept": "application/json"}
    if body is not None:
        data = json.dumps(body).encode("utf-8")
        headers["Content-Type"] = "application/json"
    req = urllib.request.Request(url, data=data, method=method, headers=headers)
    try:
        with urllib.request.urlopen(req, timeout=timeout or DEFAULT_TIMEOUT) as resp:
            text = resp.read().decode("utf-8", errors="replace")
            try:
                parsed = json.loads(text) if text else None
            except json.JSONDecodeError:
                parsed = text
            return {"ok": True, "status": resp.status, "body": parsed}
    except urllib.error.HTTPError as e:
        text = e.read().decode("utf-8", errors="replace") if hasattr(e, "read") else ""
        try:
            parsed = json.loads(text) if text else None
        except json.JSONDecodeError:
            parsed = text
        return {
            "ok": False,
            "error": f"HTTP {e.code}: {e.reason}",
            "errorCode": "HTTP_STATUS",
            "status": e.code,
            "body": parsed,
            "url": url,
        }
    except urllib.error.URLError as e:
        return {
            "ok": False,
            "error": f"Network error: {e.reason}",
            "errorCode": "NETWORK",
            "status": None,
            "url": url,
            "hint": (
                f"Cannot reach MCOS at {base}. Check that the service is running "
                "(Get-Service MasterControlOrchestrationServer) and that MCOS_BASE_URL "
                "points at the right host."
            ),
        }
    except Exception as e:
        return {
            "ok": False,
            "error": f"Unexpected error: {e}",
            "errorCode": "UNEXPECTED",
            "status": None,
            "url": url,
        }


def _get(path: str) -> Dict[str, Any]:
    return _http("GET", path)


def _post(path: str, body: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
    return _http("POST", path, body=body)


def _delete(path: str) -> Dict[str, Any]:
    return _http("DELETE", path)


# ---------------------------------------------------------------------------
# Local diagnostic helpers (no HTTP — direct Windows API queries via PowerShell)
# ---------------------------------------------------------------------------
def _powershell(cmd: str, timeout: float = 30.0) -> Dict[str, Any]:
    """Run a PowerShell command and return {stdout, stderr, exitCode}."""
    if platform.system() != "Windows":
        return {"ok": False, "error": "Local diagnostics require Windows", "errorCode": "PLATFORM"}
    try:
        proc = subprocess.run(
            ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", cmd],
            capture_output=True, text=True, timeout=timeout
        )
        return {
            "ok": proc.returncode == 0,
            "exitCode": proc.returncode,
            "stdout": proc.stdout,
            "stderr": proc.stderr,
        }
    except subprocess.TimeoutExpired:
        return {"ok": False, "error": f"timeout after {timeout}s", "errorCode": "TIMEOUT"}
    except Exception as e:
        return {"ok": False, "error": str(e), "errorCode": "UNEXPECTED"}


# ---------------------------------------------------------------------------
# Tool implementations
# ---------------------------------------------------------------------------
def _confirm_required(args: Dict[str, Any], action: str, target: str) -> Optional[Dict[str, Any]]:
    """Return a 'pass confirm:true' refusal if confirm is not set."""
    if not args.get("confirm"):
        return {
            "ok": False,
            "error": (
                f"Refusing to {action} '{target}' without explicit confirmation. "
                "Pass confirm:true to proceed."
            ),
            "errorCode": "CONFIRM_REQUIRED",
            "wouldHaveDone": f"{action} {target}",
        }
    return None


# ---- Read operations ----
def t_health(args):       return _get("/api/health")
def t_dashboard(args):    return _get("/api/dashboard")
def t_gateway_status(args):  return _get("/api/gateway/status")
def t_gateway_health(args):  return _get("/api/gateway/health")
def t_gateway_tools(args):   return _get("/api/gateway/tools")
def t_pools_list(args):   return _get("/api/pools")
def t_pool_get(args):     return _get(f"/api/pools/{args['poolId']}")
def t_pool_leases(args):  return _get(f"/api/pools/{args['poolId']}/leases")
def t_pool_saturation(args): return _get(f"/api/pools/{args['poolId']}/saturation")
def t_telemetry_events(args):
    n = int(args.get("maxEvents") or args.get("max") or 100)
    return _get(f"/api/telemetry/events?max={n}")
def t_telemetry_clients(args):  return _get("/api/telemetry/clients")
def t_telemetry_gateway(args):  return _get("/api/telemetry/gateway")
def t_discovery(args):    return _get("/api/discovery")
def t_onboarding(args):
    ct = args.get("clientType")
    if not ct:
        return _get("/api/onboarding")
    return _get(f"/api/onboarding/{ct}")
def t_governance_bundle(args):
    return _get(f"/api/governance/bundles/{args['platform']}")
def t_governance_approvals(args): return _get("/api/clu/approvals")
def t_clients_list(args): return _get("/api/clients")
def t_config_get(args):   return _get("/api/config")
def t_activity(args):     return _get("/api/activity")
def t_forsetti_modules(args): return _get("/api/forsetti/modules")


# ---- Write — config + telemetry ----
def t_config_update(args):
    fields = args.get("fields") or {}
    if not fields:
        return {"ok": False, "error": "fields is required (object of mcos.json keys to set)",
                "errorCode": "BAD_REQUEST"}
    return _post("/api/config", body=fields)

def t_telemetry_heartbeat(args):
    return _post("/api/telemetry/heartbeat", body=args.get("payload") or {})


# ---- Write — pools + leases ----
def t_pool_upsert(args):
    pool = args.get("pool")
    if not pool or not isinstance(pool, dict):
        return {"ok": False, "error": "pool definition object required",
                "errorCode": "BAD_REQUEST"}
    return _post("/api/pools", body=pool)

def t_pool_scale(args):
    return _post(f"/api/pools/{args['poolId']}/scale")

def t_pool_drain(args):
    refusal = _confirm_required(args, "drain pool", args.get("poolId", ""))
    if refusal: return refusal
    return _post(f"/api/pools/{args['poolId']}/drain")

def t_pool_remove(args):
    refusal = _confirm_required(args, "remove pool", args.get("poolId", ""))
    if refusal: return refusal
    return _post(f"/api/pools/{args['poolId']}/remove")

def t_lease_acquire(args):
    body = {
        "poolId":     args["poolId"],
        "sessionId":  args.get("sessionId", ""),
        "stateful":   bool(args.get("stateful", False)),
        "clientHint": args.get("clientHint", ""),
    }
    return _post(f"/api/pools/{args['poolId']}/leases", body=body)

def t_lease_release(args):
    return _post(f"/api/leases/{args['leaseId']}/release")


# ---- Write — gateway lifecycle ----
def t_gateway_start(args):  return _post("/api/gateway/start")
def t_gateway_stop(args):
    refusal = _confirm_required(args, "stop gateway", "MCP gateway")
    if refusal: return refusal
    return _post("/api/gateway/stop")


# ---- Write — governance ----
def t_governance_approve(args):
    return _post(f"/api/clu/approvals/{args['id']}/approve")

def t_governance_reject(args):
    refusal = _confirm_required(args, "reject governance action", args.get("id", ""))
    if refusal: return refusal
    body = {"reason": args.get("reason", "Rejected by mcos-control-plugin")}
    return _post(f"/api/clu/approvals/{args['id']}/reject", body=body)


# ---- Write — operator surface (ADR-001) ----
def t_client_register(args):
    return _post("/api/clients", body=args.get("client") or {})

def t_client_privileges(args):
    flags = args.get("flags") or {}
    return _post(f"/api/clients/{args['clientId']}/privileges", body=flags)

def t_client_enable(args):
    return _post(f"/api/clients/{args['clientId']}/enable")

def t_client_disable(args):
    refusal = _confirm_required(args, "disable client", args.get("clientId", ""))
    if refusal: return refusal
    return _post(f"/api/clients/{args['clientId']}/disable")


# ---- Write — Forsetti modules ----
def t_forsetti_module_import(args):
    manifest = args.get("manifest")
    if not manifest:
        return {"ok": False, "error": "manifest object required (Forsetti module manifest JSON)",
                "errorCode": "BAD_REQUEST"}
    return _post("/api/forsetti/modules", body=manifest)

def t_forsetti_module_enable(args):
    return _post(f"/api/forsetti/modules/{args['moduleId']}/enable")

def t_forsetti_module_disable(args):
    refusal = _confirm_required(args, "disable Forsetti module", args.get("moduleId", ""))
    if refusal: return refusal
    return _post(f"/api/forsetti/modules/{args['moduleId']}/disable")


# ---- Local diagnostic (Windows-only) ----
def t_service_status(args):
    return _powershell(
        "Get-Service MasterControlOrchestrationServer | "
        "Select-Object Name, Status, StartType, BinaryPathName | ConvertTo-Json"
    )

def t_logs_tail(args):
    count = int(args.get("count", 50))
    pattern = args.get("pattern", "")
    cmd = (
        "$log = \"$env:ProgramData\\Master Control Orchestration Server\\runtime\\events.jsonl\"; "
        "if (Test-Path $log) { "
        f"Get-Content $log -Tail {count}"
        + (f" | Select-String -Pattern '{pattern}'" if pattern else "")
        + " } else { Write-Output 'log file not found' }"
    )
    return _powershell(cmd)

def t_firewall_check(args):
    return _powershell(
        "Get-NetFirewallRule -DisplayName 'MCOS *' -ErrorAction SilentlyContinue | "
        "Select-Object DisplayName, Enabled, Direction, Action, Profile | ConvertTo-Json"
    )

def t_dns_sd_check(args):
    return _powershell(
        "Resolve-DnsName -Name _mcos._tcp.local -Type PTR -LlmnrFallback "
        "-ErrorAction SilentlyContinue | ConvertTo-Json"
    )


# ---------------------------------------------------------------------------
# Tool registry
# ---------------------------------------------------------------------------
TOOLS_REGISTRY: List[Tuple[str, Dict[str, Any], callable]] = [
    # --- Read ---
    ("mcos_health",
     {"description": "Check service health (GET /api/health). Use this first when diagnosing.",
      "inputSchema": {"type": "object", "properties": {}}}, t_health),
    ("mcos_dashboard",
     {"description": "Top-level dashboard snapshot — host telemetry, posture, endpoint counts, etc.",
      "inputSchema": {"type": "object", "properties": {}}}, t_dashboard),
    ("mcos_gateway_status",
     {"description": "MCP gateway adapter state (configured/running/stopped/failed) + advertised URL.",
      "inputSchema": {"type": "object", "properties": {}}}, t_gateway_status),
    ("mcos_gateway_health",
     {"description": "Live gateway health probe (WinHTTP-driven).",
      "inputSchema": {"type": "object", "properties": {}}}, t_gateway_health),
    ("mcos_gateway_tools",
     {"description": "Tools the gateway is currently advertising to clients.",
      "inputSchema": {"type": "object", "properties": {}}}, t_gateway_tools),
    ("mcos_pools_list",
     {"description": "All managed endpoint pools.",
      "inputSchema": {"type": "object", "properties": {}}}, t_pools_list),
    ("mcos_pool_get",
     {"description": "One pool with full instance list.",
      "inputSchema": {"type": "object", "properties": {"poolId": {"type": "string"}}, "required": ["poolId"]}},
     t_pool_get),
    ("mcos_pool_leases",
     {"description": "Active leases on a pool.",
      "inputSchema": {"type": "object", "properties": {"poolId": {"type": "string"}}, "required": ["poolId"]}},
     t_pool_leases),
    ("mcos_pool_saturation",
     {"description": "Saturation snapshot — atSaturation, atMaxInstances, scaleOutTriggered, counts.",
      "inputSchema": {"type": "object", "properties": {"poolId": {"type": "string"}}, "required": ["poolId"]}},
     t_pool_saturation),
    ("mcos_telemetry_events",
     {"description": "Recent telemetry events ring (PHASE-08).",
      "inputSchema": {"type": "object",
                      "properties": {"maxEvents": {"type": "integer", "default": 100}}}},
     t_telemetry_events),
    ("mcos_telemetry_clients",
     {"description": "Heartbeat-driven client presence roster.",
      "inputSchema": {"type": "object", "properties": {}}}, t_telemetry_clients),
    ("mcos_telemetry_gateway",
     {"description": "Gateway traffic snapshot — monotonic counters + live state/health.",
      "inputSchema": {"type": "object", "properties": {}}}, t_telemetry_gateway),
    ("mcos_discovery",
     {"description": "Discovery document — what MCOS advertises on the LAN.",
      "inputSchema": {"type": "object", "properties": {}}}, t_discovery),
    ("mcos_onboarding",
     {"description": "Onboarding profile for a clientType (claude-code, codex, grok, chatgpt, generic-mcp). Omit clientType for index.",
      "inputSchema": {"type": "object",
                      "properties": {"clientType": {"type": "string"}}}}, t_onboarding),
    ("mcos_governance_bundle",
     {"description": "Forsetti/CLU governance bundle for a platform (windows / macos / ios).",
      "inputSchema": {"type": "object",
                      "properties": {"platform": {"type": "string"}}, "required": ["platform"]}},
     t_governance_bundle),
    ("mcos_governance_approvals",
     {"description": "Operator approval queue (deferred CLU actions).",
      "inputSchema": {"type": "object", "properties": {}}}, t_governance_approvals),
    ("mcos_clients_list",
     {"description": "ADR-001 operator-surface LAN clients.",
      "inputSchema": {"type": "object", "properties": {}}}, t_clients_list),
    ("mcos_config_get",
     {"description": "Live mcos.json configuration.",
      "inputSchema": {"type": "object", "properties": {}}}, t_config_get),
    ("mcos_activity",
     {"description": "Legacy admin activity ring (operator-surface).",
      "inputSchema": {"type": "object", "properties": {}}}, t_activity),
    ("mcos_forsetti_modules",
     {"description": "List registered Forsetti modules.",
      "inputSchema": {"type": "object", "properties": {}}}, t_forsetti_modules),

    # --- Write — config ---
    ("mcos_config_update",
     {"description": "Write fields back to mcos.json via POST /api/config. Body: {fields: {...}}",
      "inputSchema": {"type": "object",
                      "properties": {"fields": {"type": "object"}},
                      "required": ["fields"]}},
     t_config_update),
    ("mcos_telemetry_heartbeat",
     {"description": "Send a ClientHeartbeat. Used for testing presence ingest.",
      "inputSchema": {"type": "object",
                      "properties": {"payload": {"type": "object"}},
                      "required": ["payload"]}}, t_telemetry_heartbeat),

    # --- Write — pools ---
    ("mcos_pool_upsert",
     {"description": "Register or update a managed endpoint pool. Body: {pool: ManagedEndpointPool}",
      "inputSchema": {"type": "object", "properties": {"pool": {"type": "object"}},
                      "required": ["pool"]}}, t_pool_upsert),
    ("mcos_pool_scale",
     {"description": "Force the pool to its minInstances.",
      "inputSchema": {"type": "object", "properties": {"poolId": {"type": "string"}},
                      "required": ["poolId"]}}, t_pool_scale),
    ("mcos_pool_drain",
     {"description": "Drain a pool (mark instances Draining; existing sticky leases keep routing). Requires confirm:true.",
      "inputSchema": {"type": "object",
                      "properties": {"poolId": {"type": "string"}, "confirm": {"type": "boolean"}},
                      "required": ["poolId"]}}, t_pool_drain),
    ("mcos_pool_remove",
     {"description": "Remove a pool definition. Requires confirm:true.",
      "inputSchema": {"type": "object",
                      "properties": {"poolId": {"type": "string"}, "confirm": {"type": "boolean"}},
                      "required": ["poolId"]}}, t_pool_remove),
    ("mcos_lease_acquire",
     {"description": "Acquire a lease on a pool (testing).",
      "inputSchema": {"type": "object",
                      "properties": {"poolId": {"type": "string"},
                                     "sessionId": {"type": "string"},
                                     "stateful": {"type": "boolean"},
                                     "clientHint": {"type": "string"}},
                      "required": ["poolId"]}}, t_lease_acquire),
    ("mcos_lease_release",
     {"description": "Release an active lease.",
      "inputSchema": {"type": "object",
                      "properties": {"leaseId": {"type": "string"}},
                      "required": ["leaseId"]}}, t_lease_release),

    # --- Write — gateway ---
    ("mcos_gateway_start",
     {"description": "Start the gateway adapter (it normally starts on boot).",
      "inputSchema": {"type": "object", "properties": {}}}, t_gateway_start),
    ("mcos_gateway_stop",
     {"description": "Stop the gateway adapter. Job Object closure reaps the child tree. Requires confirm:true.",
      "inputSchema": {"type": "object",
                      "properties": {"confirm": {"type": "boolean"}}}}, t_gateway_stop),

    # --- Write — governance ---
    ("mcos_governance_approve",
     {"description": "Approve a deferred governance action.",
      "inputSchema": {"type": "object", "properties": {"id": {"type": "string"}},
                      "required": ["id"]}}, t_governance_approve),
    ("mcos_governance_reject",
     {"description": "Reject a deferred governance action with a reason. Requires confirm:true.",
      "inputSchema": {"type": "object",
                      "properties": {"id": {"type": "string"},
                                     "reason": {"type": "string"},
                                     "confirm": {"type": "boolean"}},
                      "required": ["id"]}}, t_governance_reject),

    # --- Write — operator surface ---
    ("mcos_client_register",
     {"description": "Register a new LanClient (operator surface).",
      "inputSchema": {"type": "object", "properties": {"client": {"type": "object"}},
                      "required": ["client"]}}, t_client_register),
    ("mcos_client_privileges",
     {"description": "Set the nine-flag privilege model for a client.",
      "inputSchema": {"type": "object",
                      "properties": {"clientId": {"type": "string"},
                                     "flags": {"type": "object"}},
                      "required": ["clientId", "flags"]}}, t_client_privileges),
    ("mcos_client_enable",
     {"description": "Enable a LanClient.",
      "inputSchema": {"type": "object", "properties": {"clientId": {"type": "string"}},
                      "required": ["clientId"]}}, t_client_enable),
    ("mcos_client_disable",
     {"description": "Disable a LanClient. Requires confirm:true.",
      "inputSchema": {"type": "object",
                      "properties": {"clientId": {"type": "string"},
                                     "confirm": {"type": "boolean"}},
                      "required": ["clientId"]}}, t_client_disable),

    # --- Write — Forsetti modules ---
    ("mcos_forsetti_module_import",
     {"description": "Import a Forsetti module manifest (registers the module's entry points).",
      "inputSchema": {"type": "object", "properties": {"manifest": {"type": "object"}},
                      "required": ["manifest"]}}, t_forsetti_module_import),
    ("mcos_forsetti_module_enable",
     {"description": "Enable a registered Forsetti module.",
      "inputSchema": {"type": "object", "properties": {"moduleId": {"type": "string"}},
                      "required": ["moduleId"]}}, t_forsetti_module_enable),
    ("mcos_forsetti_module_disable",
     {"description": "Disable a registered Forsetti module. Requires confirm:true.",
      "inputSchema": {"type": "object",
                      "properties": {"moduleId": {"type": "string"},
                                     "confirm": {"type": "boolean"}},
                      "required": ["moduleId"]}}, t_forsetti_module_disable),

    # --- Local diagnostics (Windows-only) ---
    ("mcos_service_status",
     {"description": "Local: Get-Service MasterControlOrchestrationServer status (Windows-only).",
      "inputSchema": {"type": "object", "properties": {}}}, t_service_status),
    ("mcos_logs_tail",
     {"description": "Local: tail %ProgramData%\\Master Control Orchestration Server\\runtime\\events.jsonl. Optional pattern filters via Select-String.",
      "inputSchema": {"type": "object",
                      "properties": {"count": {"type": "integer", "default": 50},
                                     "pattern": {"type": "string"}}}}, t_logs_tail),
    ("mcos_firewall_check",
     {"description": "Local: Get-NetFirewallRule -DisplayName 'MCOS *' (Windows-only).",
      "inputSchema": {"type": "object", "properties": {}}}, t_firewall_check),
    ("mcos_dns_sd_check",
     {"description": "Local: Resolve-DnsName _mcos._tcp.local (Windows-only).",
      "inputSchema": {"type": "object", "properties": {}}}, t_dns_sd_check),
]

TOOLS = [{"name": name, **meta} for name, meta, _ in TOOLS_REGISTRY]
TOOL_HANDLERS = {name: handler for name, _, handler in TOOLS_REGISTRY}


# ---------------------------------------------------------------------------
# JSON-RPC stdio loop (MCP protocol 2024-11-05)
# ---------------------------------------------------------------------------
def write_message(msg: Dict[str, Any]) -> None:
    sys.stdout.write(json.dumps(msg) + "\n")
    sys.stdout.flush()


def call_tool(name: str, args: Dict[str, Any]) -> str:
    handler = TOOL_HANDLERS.get(name)
    if not handler:
        return json.dumps({"ok": False, "error": f"unknown tool: {name}",
                           "errorCode": "UNKNOWN_TOOL"})
    try:
        result = handler(args or {})
    except KeyError as e:
        return json.dumps({"ok": False, "error": f"missing required argument: {e}",
                           "errorCode": "MISSING_ARG"})
    except Exception as e:
        return json.dumps({"ok": False, "error": f"tool raised: {e}",
                           "errorCode": "TOOL_EXCEPTION"})
    return json.dumps(result, indent=2, default=str)


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
                "jsonrpc": "2.0", "id": rid,
                "result": {
                    "protocolVersion": "2024-11-05",
                    "capabilities": {"tools": {"listChanged": False}},
                    "serverInfo": {"name": "mcos-bridge", "version": PLUGIN_VERSION},
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
                write_message({
                    "jsonrpc": "2.0", "id": rid,
                    "result": {"content": [{"type": "text", "text": text}], "isError": False},
                })
            except Exception as e:
                write_message({
                    "jsonrpc": "2.0", "id": rid,
                    "result": {"content": [{"type": "text", "text": f"bridge error: {e}"}],
                               "isError": True},
                })
            continue
        if method == "ping":
            write_message({"jsonrpc": "2.0", "id": rid, "result": {}})
            continue
        if rid is not None:
            write_message({
                "jsonrpc": "2.0", "id": rid,
                "error": {"code": -32601, "message": f"Method not found: {method}"},
            })


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(0)
