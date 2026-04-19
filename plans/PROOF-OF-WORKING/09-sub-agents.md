# 09 - Sub-Agent Roster (Build 48)

Runtime: `http://127.0.0.1:7300/`. Saved artifacts: `G:/Claude/mcos_proof_dashboard_subagents.json`, `G:/Claude/mcos_proof_subagent_endpoint.json`.

---

## 1. Roster via `/api/dashboard` — VERIFIED

```
curl.exe -s http://127.0.0.1:7300/api/dashboard -o G:/Claude/mcos_proof_dashboard_subagents.json
powershell -File G:/Claude/mcos_sub_agents.ps1   # filter endpoints[] where kind == "sub_agent"
```

Observed 7 sub-agent endpoints:

| id | displayName | port | specialization | description |
| --- | --- | --- | --- | --- |
| sentinel | sentinel | 7201 | (empty) | Managed orchestration sub-agent |
| architect | architect | 7202 | (empty) | Managed orchestration sub-agent |
| forge | forge | 7203 | (empty) | Managed orchestration sub-agent |
| scribe | scribe | 7204 | (empty) | Managed orchestration sub-agent |
| recon | recon | 7205 | (empty) | Managed orchestration sub-agent |
| nexus | nexus | 7206 | (empty) | Managed orchestration sub-agent |
| watchtower | watchtower | 7207 | (empty) | Managed orchestration sub-agent |

Count=7, IDs match SENTINEL/ARCHITECT/FORGE/SCRIBE/RECON/NEXUS/WATCHTOWER, ports 7201-7207 match wiki.

## 2. Wiki `docs/wiki/Sub-Agents.md` roles vs runtime — VERIFIED

| Agent | Port | Documented role |
| --- | --- | --- |
| SENTINEL | 7201 | Design validation, import checking, dependency validation, guardrails |
| ARCHITECT | 7202 | Project analysis, design review, dependency checks, pattern suggestions |
| FORGE | 7203 | Build execution, test running, pipeline management, file watching |
| SCRIBE | 7204 | API documentation, code explanation, knowledge search, docs updates |
| RECON | 7205 | Diff review, file analysis, pattern finding, security scans, quality reports |
| NEXUS | 7206 | Workflow orchestration, task management, agent roster, request aggregation |
| WATCHTOWER | 7207 | Health monitoring, agent metrics, alert history, restart capability |

All 7 wiki entries present in runtime; IDs and ports match. Runtime `description` field is generic ("Managed orchestration sub-agent") rather than the detailed wiki role text; `specialization` is empty in the roster payload (matches default unregistered state — agent daemons register via `/api/runtime/subagents` to populate specialization).

## 3. `providerAssignmentTargets[]` where kind == "sub_agent" — VERIFIED

Observed targetIds: `architect, forge, nexus, recon, scribe, sentinel, watchtower` (7). Exact match with the sub-agent endpoint IDs in step 1.

## 4. Specialist group `coding-specialists` — VERIFIED

```
kind = sub_agent_group
targetId = coding-specialists
displayName = Coding Specialists
memberTargetIds = [sentinel, architect, forge, scribe, recon, nexus, watchtower]
```

Non-empty, contains all 7 sub-agents.

## 5. Guided wizard endpoint — VERIFIED

Source: `src/MasterControlApp/MasterControlRuntime.cpp`

- L3171: `SubAgentCatalogService::upsertSubAgent(...)`
- L8890: `upsertSubAgentJson(...)`
- L10701: `if (request.method == "POST" && request.path == "/api/runtime/subagents")`
- L10705: `POST /api/runtime/subagents/remove`

HTTP path: **`POST /api/runtime/subagents`** (upsert) and **`POST /api/runtime/subagents/remove`**.

Probe (GET returns 404 — route is POST-only; POST with `{}` exercises the real handler):

```
curl.exe -X POST -H "Content-Type: application/json" -d "{}" http://127.0.0.1:7300/api/runtime/subagents
-> HTTP 400
{"message":"Sub-agent ID is required.","requiresConfirmation":false,"succeeded":false}
```

Saved to `G:/Claude/mcos_proof_subagent_endpoint.json`. Handler reachable and validating input.

## 6. Custom sub-agent group listing — VERIFIED

Source inspection:

- `SubAgentGroupService::listGroups()` at L3451 exposes `state_->configuration.subAgentGroups`.
- Dashboard snapshot populates `snapshot.subAgentGroups` at L8705 (visible via `/api/dashboard`).
- Upsert route: `POST /api/providers/groups` (L10709) -> `upsertSubAgentGroupJson`.
- Remove route: `POST /api/providers/groups/remove` -> `removeSubAgentGroupJson`.

No dedicated `GET /api/sub-agent-groups` endpoint; custom groups are listed via the dashboard payload's `providerAssignmentTargets[kind=="sub_agent_group"]` and the top-level `subAgentGroups` snapshot. Write operations use `/api/providers/groups[/remove]`.

---

**VERDICT:** All 6 bullets VERIFIED — 7 sub-agents (SENTINEL/ARCHITECT/FORGE/SCRIBE/RECON/NEXUS/WATCHTOWER on 7201-7207) are published as endpoints and provider targets, the `coding-specialists` group contains all seven, and the wizard route `POST /api/runtime/subagents` responds with input validation.
