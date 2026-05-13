#!/usr/bin/env python3
# Seed mcos-memory with the current project state.
# Idempotent: re-running replaces existing seed entries by tag-match.

from __future__ import annotations

import json
import sys
import time
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
STATE_FILE = ROOT / ".claude" / "mcp-state" / "mcos-memory.json"


def now():
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


def load():
    if STATE_FILE.exists():
        return json.loads(STATE_FILE.read_text(encoding="utf-8"))
    STATE_FILE.parent.mkdir(parents=True, exist_ok=True)
    return {"schemaVersion": 1, "facts": [], "phaseState": {}, "files": {}}


def save(state):
    tmp = STATE_FILE.with_suffix(".json.tmp")
    tmp.write_text(json.dumps(state, indent=2, ensure_ascii=False), encoding="utf-8")
    tmp.replace(STATE_FILE)


def upsert_fact(state, tag_marker, kind, title, body, tags, source=""):
    """Replace any existing fact carrying tag_marker; otherwise add a new one."""
    state["facts"] = [f for f in state["facts"] if tag_marker not in (f.get("tags") or [])]
    state["facts"].append({
        "id": uuid.uuid4().hex[:12],
        "kind": kind,
        "title": title,
        "body": body,
        "tags": list(set([tag_marker] + tags)),
        "source": source,
        "ts": now(),
    })


def upsert_phase(state, phase, status, commit, report, deferred=None, notes=""):
    state["phaseState"][phase] = {
        "status": status,
        "commit": commit,
        "report": report,
        "deferred": deferred or [],
        "notes": notes,
        "updatedAt": now(),
    }


def upsert_annotation(state, path, note, tags):
    existing = state["files"].setdefault(path, [])
    # Replace prior annotations with this one if same first tag (the "key").
    key = (tags[0] if tags else note[:32])
    existing[:] = [a for a in existing if (a.get("tags") and a["tags"][0] != key) and a.get("note") != note]
    existing.append({"ts": now(), "note": note, "tags": tags})


def main():
    state = load()

    # ---------------- Phase state ----------------
    upsert_phase(state, "PHASE-00", "complete", "d8758ac",
                 "handoff/realignment/PHASE-00-completion-report.md",
                 deferred=[],
                 notes="Repository baseline + ADR-002 + drift inventory + removal map + grep list. Docs only.")
    upsert_phase(state, "PHASE-01", "complete", "a784ffb",
                 "handoff/realignment/PHASE-01-completion-report.md",
                 deferred=["WinUI shell residual cleanup"],
                 notes="Provider-era removal already mostly done (ADR-001); only WinUI shell had residual references. ~1100 lines net deletion.")
    upsert_phase(state, "PHASE-02", "complete", "86695c3",
                 "handoff/realignment/PHASE-02-completion-report.md",
                 deferred=[],
                 notes="IMcpGateway + supervised-mock fallback. Gateway HTTP routes.")
    upsert_phase(state, "PHASE-03", "complete", "6f37cf0",
                 "handoff/realignment/PHASE-03-completion-report.md",
                 deferred=[],
                 notes="DNS-SD service registration + /.well-known/mcos.json + /api/discovery + UDP beacon + AppConfiguration.instanceId.")
    upsert_phase(state, "PHASE-04", "complete", "f2d51bc",
                 "handoff/realignment/PHASE-04-completion-report.md",
                 deferred=["ChatGPT connector-edge companion utility binary"],
                 notes="OnboardingProfile types + /api/onboarding/{clientType} for claude-code/codex/grok/chatgpt/generic-mcp.")
    upsert_phase(state, "PHASE-05", "complete", "aa4087a",
                 "handoff/realignment/PHASE-05-completion-report.md",
                 deferred=[],
                 notes="GovernanceBundleService + bundle distribution routes + bcrypt link + Forsetti compliance script update (6 stale assertions retired).")
    upsert_phase(state, "PHASE-06", "complete", "c8077f0",
                 "handoff/realignment/PHASE-06-completion-report.md",
                 deferred=["Real per-instance telemetry", "Pool persistence across restarts", "End-to-end pool spawn smoke"],
                 notes="WorkerSupervisor + Job Object containment + 7-state lifecycle + pool admin routes. (std::max) parenthesization needed for Windows.h max() macro.")
    upsert_phase(state, "PHASE-07", "complete", "0cb9b48",
                 "handoff/realignment/PHASE-07-completion-report.md",
                 deferred=["Auto-fail-leases-on-failed-instance sweeper", "Multi-threaded scale-out load test"],
                 notes="ILeaseRouter + sticky-session for stateful + least-loaded for stateless + same-type scale-out. (std::numeric_limits<int>::max)() parenthesization.")
    upsert_phase(state, "PHASE-08", "complete", "228e944",
                 "handoff/realignment/PHASE-08-completion-report.md",
                 deferred=["Auto-fail-leases sweeper", "PDH/DXGI host enrichment", "Heartbeat-decay to Stale presence", "End-to-end heartbeat scenario test"],
                 notes="ITelemetryAggregator + ClientHeartbeat with -1.0 unavailable sentinel + 1024-event ring + 4 telemetry routes. (std::min) parenthesization for Windows.h min() macro.")
    upsert_phase(state, "PHASE-09", "complete", "c241440",
                 "handoff/realignment/PHASE-09-completion-report.md",
                 deferred=["Browser-side test harness (formatMetric unit + Playwright)", "WinUI shell realignment", "Pool CRUD UI", "SSE push for telemetry events"],
                 notes="Tron dashboard realigned to gateway-first. 11 destinations. formatMetric() honesty helper. DASHBOARD-ROUTE-MAP.md created.")
    upsert_phase(state, "PHASE-10", "complete", "d98b074",
                 "handoff/realignment/PHASE-10-completion-report.md",
                 deferred=["Bootstrapper INFINITE-wait rewrite (runProcess + runProcessCapture)", "Install-use-uninstall round-trip on clean VM", "README full-badge expansion"],
                 notes="Windows release gate closed. release.yml verifies same-SHA windows-build-test-package run before publishing. VERSION.json bumped 0.5.0 -> 0.6.0.")
    upsert_phase(state, "PHASE-11", "pending", "", "",
                 notes="Native gateway evaluation. Decision phase, not implementation. Pending operator instruction.")

    # ---------------- Phase events ----------------
    for phase, commit in [
        ("PHASE-10", "d98b074"), ("PHASE-09", "c241440"), ("PHASE-08", "228e944"),
        ("PHASE-07", "0cb9b48"), ("PHASE-06", "c8077f0"), ("PHASE-05", "aa4087a"),
        ("PHASE-04", "f2d51bc"), ("PHASE-03", "6f37cf0"), ("PHASE-02", "86695c3"),
        ("PHASE-01", "a784ffb"), ("PHASE-00", "d8758ac"),
    ]:
        upsert_fact(state, f"seed:{phase}-event", "phase-event",
                    f"{phase} complete", f"Phase {phase} landed at commit {commit}.",
                    [phase.lower(), "phase-event", "completed"], commit)

    # ---------------- Deferred work (consolidated) ----------------
    deferred_items = [
        ("Bootstrapper INFINITE-wait rewrite",
         "src/MasterControlBootstrapper/main.cpp:644 (runProcess) and :707 (runProcessCapture) use WaitForSingleObject(..., INFINITE). The right fix is the concurrent-drain pattern at MasterControlRuntime.cpp:1059. FORBIDDEN-CONTRACT 6.4 documents the exemption.",
         ["phase-10", "deferred", "bootstrapper", "process-execution"]),
        ("Auto-fail-leases-on-failed-instance sweeper",
         "Periodic timer that walks IWorkerSupervisor::listPools() + ILeaseRouter::activeLeases() and transitions leases bound to Failed instances to LeaseState::Failed. Pairs with heartbeat-decay-to-Stale presence transitions.",
         ["phase-07", "phase-08", "deferred", "leases", "telemetry"]),
        ("PDH/DXGI host telemetry enrichment",
         "ITelemetryAggregator surface is ready; the actual PDH-driven CPU/disk/network counters and DXGI GPU memory readings are not yet wired. Additive change behind an IHostTelemetryProbe.",
         ["phase-08", "deferred", "telemetry"]),
        ("Browser-side test harness (formatMetric unit + Playwright)",
         "No JS test harness in the repo. PHASE-10 release gate concern. formatMetric is the highest-value unit.",
         ["phase-09", "phase-10", "deferred", "tests", "browser"]),
        ("WinUI shell realignment",
         "src/MasterControlShell/ builds clean (1 pre-existing C4100) but is not wired to any gateway-first panel. Decide whether to delete or rebuild around the new model.",
         ["deferred", "shell", "winui"]),
        ("Install-use-uninstall round-trip on clean VM",
         "Gold-standard installer smoke. Needs Windows Sandbox / fresh-VM runner with admin privileges.",
         ["phase-10", "deferred", "installer", "smoke"]),
        ("Pool CRUD UI in dashboard",
         "Pools panel exposes scale-to-min and drain inline; full create/edit/remove via UI lands when explicitly scoped.",
         ["phase-09", "deferred", "dashboard", "pools"]),
        ("SSE / WebSocket push for telemetry events stream",
         "Currently 5s polling. Optimization, not a regression.",
         ["phase-09", "deferred", "telemetry"]),
        ("README full-badge expansion (or sync script simplification)",
         "Sync-RepositoryVersionBadges.ps1 expects 5 chained badges + a 'Current release' block. README has 1 badge. PHASE-10 hand-patched the visible badge.",
         ["phase-10", "deferred", "docs"]),
    ]
    for title, body, tags in deferred_items:
        upsert_fact(state, f"seed:deferred:{title}", "deferred", title, body, tags)

    # ---------------- Invariants (cite these often) ----------------
    invariants = [
        ("ADR-002 §1 — no provider-era execution",
         "MCOS does not execute ChatGPT/Codex/Claude/Grok directly. Provider names persist only as client profile types.",
         ["adr-002", "invariant", "provider-era"]),
        ("ADR-002 §1 — LAN AI-client surface is auth=none / trust=lan",
         "Trust enforced at the network/firewall layer, not the application layer. Operator surface is logically separate.",
         ["adr-002", "invariant", "auth"]),
        ("ADR-002 §3 — gateway-first topology",
         "AI client -> MCOS advertised MCP Gateway -> NativeHttpSysGatewayAdapter -> MCOS logical pool -> LeaseRouter -> backend instance. Autoscaled clones NOT exposed as separate public tools.",
         ["adr-002", "invariant", "gateway"]),
        ("ADR-002 §8 — sticky-session integrity (no hot-migration)",
         "LeaseRouter step 1 returns active lease verbatim, never re-binding to a different instance. FORBIDDEN-CONTRACT §2.4 enforces.",
         ["adr-002", "invariant", "leases"]),
        ("ADR-002 §9 — honest telemetry (-1.0 = unavailable)",
         "ClientHeartbeat / WorkerTelemetry use -1.0 as the unavailable sentinel. 0.0 is genuine 'idle'. HostTelemetrySnapshot is exempt (PDH-direct). FORBIDDEN-CONTRACT §4.3 + §8.1 enforce.",
         ["adr-002", "invariant", "telemetry", "honesty"]),
        ("ADR-002 §10 — release gate, no workflow_dispatch bypass",
         "windows-build-test-package.yml + release.yml are the gating pair. Neither has workflow_dispatch. FORBIDDEN-CONTRACT §6.2 enforces.",
         ["adr-002", "invariant", "ci", "release"]),
        ("ADR-002 §11 — Forsetti vendoring is sealed",
         "No edits inside Forsetti-Framework-Windows-main/. FORBIDDEN-CONTRACT §5.1 + the Forsetti compliance script enforce.",
         ["adr-002", "invariant", "forsetti", "vendoring"]),
    ]
    for title, body, tags in invariants:
        upsert_fact(state, f"seed:invariant:{title[:40]}", "invariant", title, body, tags)

    # ---------------- File annotations (gotchas) ----------------
    upsert_annotation(state, "src/MasterControlApp/MasterControlRuntime.cpp",
                      "Windows.h max()/min() macro collisions. Use parenthesization: (std::max)(...), (std::min)(...), (std::numeric_limits<int>::max)(). PHASE-06/07/08 all hit this.",
                      ["windows-h", "macro-collision", "gotcha"])
    upsert_annotation(state, "src/MasterControlApp/MasterControlRuntime.cpp",
                      "Lines 914-1110: runHostedExecutable is the canonical 7-step process-execution reference. Concurrent drain threads under RAII scope guards, WaitForSingleObject with 5min timeout, TerminateJobObject on timeout, join, GetExitCodeProcess, cleanup. Reference for any new sync process invocation.",
                      ["reference-impl", "process-execution", "phase-10"])
    upsert_annotation(state, "src/MasterControlBootstrapper/main.cpp",
                      "Lines 644 and 707: WaitForSingleObject(..., INFINITE) without timeout/kill/drain. Pre-existing, tagged with `// PHASE-10 known-issue` source comments. FORBIDDEN-CONTRACT §6.4 documents the exemption. Rewrite is deferred work.",
                      ["known-issue", "process-execution", "phase-10", "deferred"])
    upsert_annotation(state, "tests/MasterControlOrchestrationServerTests.cpp",
                      "Tests must be wired into main() near the bottom of the file. Each phase appended its tests to the ok &= chain, grouped by phase comment. 56 test functions as of PHASE-10.",
                      ["tests", "convention"])
    upsert_annotation(state, "include/MasterControl/MasterControlModels.h",
                      "ManagedEndpointPool's `template_` C++ field maps to `template` JSON key via explicit to_json/from_json. ClientHeartbeat metric fields default to -1.0 (unavailable sentinel). HostTelemetrySnapshot uses 0.0 (genuine idle, PDH-direct).",
                      ["json-mapping", "telemetry-honesty", "convention"])
    upsert_annotation(state, ".github/workflows/windows-build-test-package.yml",
                      "DO NOT add workflow_dispatch. The gate is push/pull_request only. Concurrency cancels racing same-SHA runs. Version-stamp step must precede configure (FORBIDDEN-CONTRACT §6.5 enforces ordering).",
                      ["ci", "release-gate", "phase-10", "warning"])
    upsert_annotation(state, ".github/workflows/release.yml",
                      "DO NOT add workflow_dispatch. Tag-only trigger. Verifies same-SHA windows-build-test-package run via gh api before publishing. The file name is load-bearing — the release workflow's API query depends on it.",
                      ["ci", "release-gate", "phase-10", "warning"])
    upsert_annotation(state, "scripts/check-mastercontrol-forsetti.ps1",
                      "PHASE-05 retired 6 stale assertions about resources/web/app.js's pre-ADR-001 Forsetti bootstrap. Current rules: surface*Host elements present in index.html, no telemetryGrid/endpointTable IDs, no provider-era surfaces, no hardcoded CLU bootstrap.",
                      ["forsetti", "compliance", "phase-05"])
    upsert_annotation(state, "VERSION.json",
                      "Single source of truth for product version. Bump only in PHASE-10 (already done: 0.5.0 -> 0.6.0). Sync-RepositoryVersionBadges.ps1 propagates to vcpkg.json + README badges.",
                      ["versioning", "phase-10"])
    upsert_annotation(state, "resources/web/app.js",
                      "Honesty rule: every render of ClientHeartbeat / WorkerTelemetry metrics must go through formatMetric(). Direct .toFixed(...)+'%' is allowed only on HostTelemetrySnapshot (PDH-direct). FORBIDDEN-CONTRACT §8.1 enforces.",
                      ["dashboard", "telemetry-honesty", "phase-09"])

    # ---------------- Contracts (FORBIDDEN-CONTRACT summary) ----------------
    contracts = [
        ("Group 1 — Provider-era execution forbidden",
         "ADR-001 §1 + ADR-002 §1. Sections 1.1-1.7 in FORBIDDEN-CONTRACT-GREP-LIST.md. Includes provider modules, services, transports, routes, data types, governance kinds.",
         ["contract", "group-1", "provider-era"]),
        ("Group 2 — Gateway abstraction integrity",
         "ADR-002 §2,§3. No autoscaled-clone registration as separate tools; no hot-migration; sticky-session integrity (§2.4); worker process tree containment (§2.1a).",
         ["contract", "group-2", "gateway"]),
        ("Group 3 — Trust model integrity",
         "ADR-002 §1. No app-layer auth on AI-client gateway; no bearer/session token requirements; operator-surface auth must not regress to 'anyone can mutate'.",
         ["contract", "group-3", "trust"]),
        ("Group 4 — Telemetry honesty",
         "ADR-002 §9. No fake telemetry (§4.1); per-client metrics only via heartbeat/sidecar (§4.2); ClientHeartbeat -1.0 sentinel integrity (§4.3); event ring cap stays at 1024 (§4.4); recordHeartbeat is the only client-metric write site (§4.5).",
         ["contract", "group-4", "telemetry"]),
        ("Group 5 — Forsetti vendoring sealed",
         "ADR-002 §11. git diff Forsetti-Framework-Windows-main/ since baseline must be empty.",
         ["contract", "group-5", "forsetti"]),
        ("Group 6 — CI / release gate integrity",
         "ADR-002 §10. No hardcoded VS Enterprise path (§6.1); no workflow_dispatch on gating workflows (§6.2); no path-segment Enterprise in workflows (§6.3); process execution 7-step compliance with documented bootstrapper exemptions (§6.4); version-stamp before configure (§6.5).",
         ["contract", "group-6", "ci", "release"]),
        ("Group 7 — Phase scope integrity",
         "manifest.json. PHASE-00 must not edit code (§7.1); PHASE-XX must not edit files outside its scope (§7.2 — manual review).",
         ["contract", "group-7", "phase-scope"]),
        ("Group 8 — Dashboard honesty",
         "ADR-002 §9, PHASE-09. Dashboard must render -1.0 as 'unavailable' (§8.1); no legacy hardcoded surface IDs (§8.2); no provider-era residue in app.js (§8.3); no removed-route fetches (§8.4).",
         ["contract", "group-8", "dashboard", "telemetry"]),
    ]
    for title, body, tags in contracts:
        upsert_fact(state, f"seed:contract:{title[:30]}", "contract", title, body, tags,
                    "docs/implementation/FORBIDDEN-CONTRACT-GREP-LIST.md")

    save(state)
    print(f"Seeded {len(state['facts'])} facts, {len(state['phaseState'])} phase states, {len(state['files'])} annotated files.")
    print(f"State file: {STATE_FILE}")


if __name__ == "__main__":
    main()
