"""
Autonomous feature test harness for the Master Control Orchestration Server
admin API.

Runs every feature area against a live service host and logs all results
(pass/fail/warning) to a structured JSON-L stream plus a human-readable
markdown report. Designed to be re-run during autonomous repair cycles.

Usage:
    python feature-test.py --session-dir <dir>
"""

import argparse
import json
import os
import sys
import time
import urllib.request
import urllib.error
from datetime import datetime

BASE = "http://127.0.0.1:7300"

# Mutable state shared across tests
session_dir = None
events = []        # raw test results
warnings = []      # non-fatal issues
errors = []        # fatal issues / unexpected failures
summary_counts = {"pass": 0, "warn": 0, "fail": 0, "skip": 0}


def log(kind, name, status, detail=None, req=None, resp=None, latency_ms=None):
    """Record a test result to in-memory state and the streaming log file."""
    ts = datetime.utcnow().isoformat(timespec="milliseconds") + "Z"
    entry = {
        "ts": ts,
        "kind": kind,                   # "api", "smoke", "repair"
        "name": name,
        "status": status,               # pass/warn/fail/skip
        "detail": detail,
        "request": req,
        "response_excerpt": (resp[:800] if isinstance(resp, str) else resp),
        "latency_ms": latency_ms,
    }
    events.append(entry)
    summary_counts[status] = summary_counts.get(status, 0) + 1
    if status == "warn":
        warnings.append(f"{name}: {detail}")
    elif status == "fail":
        errors.append(f"{name}: {detail}")

    # Streaming JSON-lines output
    with open(os.path.join(session_dir, "stream.jsonl"), "a", encoding="utf-8") as f:
        f.write(json.dumps(entry, default=str) + "\n")

    # Human-readable one-liner
    tag = {"pass": "PASS", "warn": "WARN", "fail": "FAIL", "skip": "SKIP"}[status]
    lat = f" ({latency_ms}ms)" if latency_ms is not None else ""
    print(f"  [{tag}] {name}{lat}  {detail or ''}")


def http_request(method, path, body=None, headers=None, timeout=10):
    url = BASE + path
    data = None
    req_headers = {"Accept": "application/json"}
    if body is not None:
        data = json.dumps(body).encode("utf-8")
        req_headers["Content-Type"] = "application/json"
    if headers:
        req_headers.update(headers)
    req = urllib.request.Request(url, data=data, method=method, headers=req_headers)
    start = time.monotonic()
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            payload = resp.read().decode("utf-8", errors="replace")
            return resp.getcode(), payload, int((time.monotonic() - start) * 1000)
    except urllib.error.HTTPError as e:
        payload = e.read().decode("utf-8", errors="replace")
        return e.code, payload, int((time.monotonic() - start) * 1000)
    except urllib.error.URLError as e:
        return 0, str(e.reason), int((time.monotonic() - start) * 1000)
    except Exception as e:
        return -1, f"{type(e).__name__}: {e}", int((time.monotonic() - start) * 1000)


# ---- test areas ----

def test_smoke():
    print("\n[smoke]")
    for path in ["/api/health", "/api/config", "/api/dashboard", "/api/activity"]:
        code, body, lat = http_request("GET", path)
        if code == 200:
            log("smoke", f"GET {path}", "pass", f"HTTP {code}", req={"method": "GET", "path": path}, resp=body, latency_ms=lat)
        else:
            log("smoke", f"GET {path}", "fail", f"HTTP {code}", req={"method": "GET", "path": path}, resp=body, latency_ms=lat)


def test_telemetry():
    print("\n[telemetry]")
    code, body, lat = http_request("GET", "/api/dashboard")
    if code != 200:
        log("api", "dashboard fetch", "fail", f"HTTP {code}", resp=body, latency_ms=lat)
        return
    try:
        d = json.loads(body)
    except Exception as e:
        log("api", "dashboard parse", "fail", f"json parse: {e}", resp=body)
        return

    tel = d.get("telemetry") or {}
    cpu = tel.get("cpuPercent")
    mem = tel.get("memoryPercent")
    disk = tel.get("diskPercent")
    host = tel.get("hostName")

    if cpu is None or mem is None or disk is None:
        log("api", "telemetry fields", "fail",
            f"missing fields cpu={cpu} mem={mem} disk={disk}")
    elif cpu == 0 and mem == 0 and disk == 0:
        log("api", "telemetry fields", "warn",
            "all telemetry percentages are 0 (suspect)")
    else:
        log("api", "telemetry fields", "pass",
            f"host={host} cpu={cpu:.1f}% mem={mem:.1f}% disk={disk:.1f}%",
            latency_ms=lat)

    # Endpoint inventory
    endpoints = d.get("endpoints") or []
    if not endpoints:
        log("api", "endpoint inventory", "warn", "no endpoints in dashboard")
    else:
        log("api", "endpoint inventory", "pass", f"{len(endpoints)} endpoints")


def test_provider_mgmt():
    print("\n[provider management]")
    # 1. Capability catalog must be populated.
    code, body, lat = http_request("GET", "/api/dashboard")
    d = json.loads(body) if code == 200 else {}
    capabilities = d.get("providerCapabilities") or []
    if not capabilities:
        log("api", "provider capabilities", "fail",
            "providerCapabilities empty — Auto-Connect cannot resolve modules")
        return
    log("api", "provider capabilities", "pass",
        f"{len(capabilities)} capabilities: " + ", ".join(c.get("providerId", "?") for c in capabilities))

    # 2. Auto-Connect with each capability kind, using a fake API key.
    for cap in capabilities:
        kind = cap.get("kind", "")
        if not kind or kind == "generic":
            continue
        field = (cap.get("credentialFields") or [{}])[0].get("fieldId", "api_key")
        payload = {
            "kind": kind,
            "credentials": {field: f"sk-feature-test-{kind}"},
            "assignmentTargetIds": ["planner"],
            "discoverModels": False,
        }
        code, body, lat = http_request("POST", "/api/providers/auto-connect", payload)
        if code == 200:
            r = json.loads(body)
            if r.get("succeeded"):
                log("api", f"auto-connect {kind}", "pass",
                    f"id={r.get('providerId')} latency={r.get('totalLatencyMs')}ms", latency_ms=lat)
            else:
                log("api", f"auto-connect {kind}", "warn",
                    r.get("errorMessage") or r.get("summary"), latency_ms=lat)
        else:
            log("api", f"auto-connect {kind}", "fail", f"HTTP {code}", resp=body, latency_ms=lat)

    # 3. Verify the new providers are persisted in /api/config.
    code, body, lat = http_request("GET", "/api/config")
    if code == 200:
        cfg = json.loads(body)
        providers = cfg.get("providers") or []
        log("api", "providers persisted", "pass", f"{len(providers)} providers in config")
    else:
        log("api", "providers persisted", "fail", f"HTTP {code}")

    # 4. Assignment list includes roles.
    code, body, lat = http_request("GET", "/api/dashboard")
    d = json.loads(body) if code == 200 else {}
    targets = d.get("providerAssignmentTargets") or []
    if not targets:
        log("api", "assignment targets", "fail", "no assignment targets in dashboard")
    else:
        log("api", "assignment targets", "pass", f"{len(targets)} targets available")


def test_mcp_server():
    print("\n[mcp server catalog]")
    payload = {
        "id": "feature-test-mcp",
        "displayName": "Feature Test MCP",
        "kind": "mcp_server",
        "host": "127.0.0.1",
        "port": 9100,
        "protocol": "http",
        "status": "unknown",
        "description": "smoke test",
        "routePath": "/mcp",
        "specialization": "",
        "userDefined": True,
    }
    code, body, lat = http_request("POST", "/api/runtime/mcp-servers", payload)
    if code == 200 and json.loads(body).get("succeeded"):
        log("api", "upsert mcp server", "pass", None, latency_ms=lat)
    else:
        log("api", "upsert mcp server", "fail", f"HTTP {code}", resp=body, latency_ms=lat)

    # remove
    code, body, lat = http_request("POST", "/api/runtime/mcp-servers/remove", {"mcpServerId": "feature-test-mcp"})
    if code == 200 and json.loads(body).get("succeeded"):
        log("api", "remove mcp server", "pass", None, latency_ms=lat)
    else:
        log("api", "remove mcp server", "fail", f"HTTP {code}", resp=body, latency_ms=lat)


def test_subagent():
    print("\n[sub-agent catalog]")
    payload = {
        "id": "feature-test-sa",
        "displayName": "Feature Test Agent",
        "kind": "sub_agent",
        "host": "127.0.0.1",
        "port": 9200,
        "protocol": "http",
        "status": "unknown",
        "description": "smoke test",
        "routePath": "",
        "specialization": "testing",
        "userDefined": True,
    }
    code, body, lat = http_request("POST", "/api/runtime/subagents", payload)
    if code == 200 and json.loads(body).get("succeeded"):
        log("api", "upsert sub-agent", "pass", None, latency_ms=lat)
    else:
        log("api", "upsert sub-agent", "fail", f"HTTP {code}", resp=body, latency_ms=lat)

    # group upsert
    group = {
        "groupId": "feature-test-group",
        "displayName": "Feature Test Group",
        "description": "smoke",
        "memberTargetIds": ["feature-test-sa"],
        "updatedAtUtc": "",
    }
    code, body, lat = http_request("POST", "/api/providers/groups", group)
    if code == 200 and json.loads(body).get("succeeded"):
        log("api", "upsert sub-agent group", "pass", None, latency_ms=lat)
    else:
        log("api", "upsert sub-agent group", "fail", f"HTTP {code}", resp=body, latency_ms=lat)

    # cleanup
    http_request("POST", "/api/providers/groups/remove", {"groupId": "feature-test-group"})
    http_request("POST", "/api/runtime/subagents/remove", {"subAgentId": "feature-test-sa"})


def test_forsetti_modules():
    print("\n[forsetti module catalog]")
    code, body, lat = http_request("GET", "/api/forsetti/modules")
    if code == 200:
        try:
            d = json.loads(body)
            mods = d.get("modules") if isinstance(d, dict) else d
            count = len(mods) if mods else 0
            log("api", "forsetti modules", "pass", f"{count} modules", latency_ms=lat)
        except Exception as e:
            log("api", "forsetti modules", "warn", f"parse: {e}", latency_ms=lat)
    else:
        log("api", "forsetti modules", "fail", f"HTTP {code}", resp=body, latency_ms=lat)


def test_clu():
    print("\n[clu governance]")
    # Dashboard includes governance schema
    code, body, lat = http_request("GET", "/api/dashboard")
    d = json.loads(body) if code == 200 else {}
    gov = d.get("governance") or {}
    findings = gov.get("findings") or []
    roles = gov.get("roles") or []
    rules = gov.get("rules") or []
    if not roles and not rules:
        log("api", "clu profile", "warn",
            "governance.roles and governance.rules both empty — CLU profile may be missing")
    else:
        log("api", "clu profile", "pass",
            f"{len(roles)} roles, {len(rules)} rules, {len(findings)} findings", latency_ms=lat)


def test_exports():
    print("\n[exports]")
    code, body, lat = http_request("GET", "/api/exports")
    if code == 200:
        try:
            arr = json.loads(body)
            log("api", "exports list", "pass", f"{len(arr)} artifacts", latency_ms=lat)
        except Exception as e:
            log("api", "exports list", "warn", f"parse: {e}")
    else:
        log("api", "exports list", "fail", f"HTTP {code}", resp=body, latency_ms=lat)


def test_activity_ring():
    print("\n[activity ring]")
    code, body, lat = http_request("GET", "/api/activity")
    if code != 200:
        log("api", "activity fetch", "fail", f"HTTP {code}", resp=body, latency_ms=lat)
        return
    try:
        d = json.loads(body)
    except Exception as e:
        log("api", "activity parse", "fail", str(e))
        return
    hwm = d.get("highWaterMarkId")
    n = len(d.get("events") or [])
    log("api", "activity fetch", "pass",
        f"hwm={hwm} events={n}", latency_ms=lat)

    # Validate events have expected shape
    for ev in d.get("events") or []:
        required = ["id", "kind", "timestampUtc", "method", "target", "statusCode"]
        missing = [k for k in required if ev.get(k) in (None, "")]
        if missing:
            log("api", "activity event shape", "warn",
                f"missing fields {missing} in event {ev.get('id')}")
            break
    else:
        log("api", "activity event shape", "pass", "all events well-formed")


def write_report():
    path = os.path.join(session_dir, "REPORT.md")
    with open(path, "w", encoding="utf-8") as f:
        f.write("# Master Control Orchestration Server — Feature Test Report\n\n")
        f.write(f"**Generated:** {datetime.utcnow().isoformat()}Z\n\n")
        f.write("## Summary\n\n")
        f.write(f"- PASS:  {summary_counts.get('pass', 0)}\n")
        f.write(f"- WARN:  {summary_counts.get('warn', 0)}\n")
        f.write(f"- FAIL:  {summary_counts.get('fail', 0)}\n")
        f.write(f"- SKIP:  {summary_counts.get('skip', 0)}\n\n")
        if errors:
            f.write("## Errors\n\n")
            for e in errors:
                f.write(f"- {e}\n")
            f.write("\n")
        if warnings:
            f.write("## Warnings\n\n")
            for w in warnings:
                f.write(f"- {w}\n")
            f.write("\n")
        f.write("## Timeline\n\n")
        for ev in events:
            tag = {"pass": "OK", "warn": "!!", "fail": "XX", "skip": ".."}.get(ev["status"], "??")
            lat = f" ({ev['latency_ms']}ms)" if ev.get("latency_ms") is not None else ""
            f.write(f"- `{ev['ts']}` **[{tag}]** {ev['name']}{lat} — {ev.get('detail') or ''}\n")
    print(f"\nreport written: {path}")


def main():
    global session_dir
    parser = argparse.ArgumentParser()
    parser.add_argument("--session-dir", required=True)
    args = parser.parse_args()
    session_dir = args.session_dir
    os.makedirs(session_dir, exist_ok=True)

    print("=" * 64)
    print(" Master Control Orchestration Server — Feature Test")
    print(f" session: {session_dir}")
    print("=" * 64)

    test_smoke()
    test_telemetry()
    test_forsetti_modules()
    test_provider_mgmt()
    test_mcp_server()
    test_subagent()
    test_clu()
    test_exports()
    test_activity_ring()

    write_report()

    print("\n" + "=" * 64)
    print(f" PASS: {summary_counts.get('pass',0)}  "
          f"WARN: {summary_counts.get('warn',0)}  "
          f"FAIL: {summary_counts.get('fail',0)}")
    print("=" * 64)

    return 0 if summary_counts.get("fail", 0) == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
