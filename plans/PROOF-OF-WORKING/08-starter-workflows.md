# Proof of Working — Bonus: Starter Workflows

Build 48, http://127.0.0.1:7300/, verified 2026-04-19.

## 1. Template Listing — VERIFIED

```
curl.exe -s -w "HTTP_CODE:%{http_code}" "http://127.0.0.1:7300/api/setup/workflow-templates" -o G:/Claude/mcos_proof_starter_list.json
```

- HTTP 200.
- 3 templates returned:
  - `single-provider-demo` — displayName "Single-Provider Demo", requiresProviders=1, requiresMcp=0, requiresSpecialists=0.
  - `mcp-assisted-demo` — displayName "MCP-Assisted Demo", requiresProviders=1, requiresMcp=1, requiresSpecialists=0.
  - `specialist-team-demo` — displayName "Specialist Team Demo", requiresProviders=2, requiresMcp=0, requiresSpecialists=2.

## 2. single-provider-demo Instantiate — VERIFIED

```
curl.exe -s -w "HTTP_CODE:%{http_code}" -X POST "http://127.0.0.1:7300/api/setup/workflow-templates/single-provider-demo/instantiate" -H "Content-Type: application/json" --data-binary "@G:/Claude/mcos_post_starter_single.json" -o G:/Claude/mcos_proof_starter_single.json
```

- HTTP 200. `succeeded:true`. `workflowId:"single-provider-demo:1-targets"`.
- Message: `"Instantiated starter workflow 'Single-Provider Demo' (1 assignment(s) applied)."`

## 3. mcp-assisted-demo Instantiate — VERIFIED

```
curl.exe -s -w "HTTP_CODE:%{http_code}" -X POST "http://127.0.0.1:7300/api/setup/workflow-templates/mcp-assisted-demo/instantiate" -H "Content-Type: application/json" --data-binary "@G:/Claude/mcos_post_starter_mcp.json" -o G:/Claude/mcos_proof_starter_mcp.json
```

- HTTP 200. `succeeded:true`. `workflowId:"mcp-assisted-demo:1-targets"`.
- Message: `"Instantiated starter workflow 'MCP-Assisted Demo' (1 assignment(s) applied)."`

## 4. specialist-team-demo Without Specialists (Blocked) — VERIFIED

```
curl.exe -s -w "HTTP_CODE:%{http_code}" -X POST "http://127.0.0.1:7300/api/setup/workflow-templates/specialist-team-demo/instantiate" -H "Content-Type: application/json" --data-binary "@G:/Claude/mcos_post_starter_spec_blocked.json" -o G:/Claude/mcos_proof_starter_specialist_blocked.json
```

- HTTP 200. `succeeded:false`. `workflowId:""`.
- Message: `"This starter workflow requires at least 2 specialist(s) assigned. Currently ready: 0."` (exact match).

## 5. Satisfy Prerequisite, Then Retry — VERIFIED

Endpoint discovered via `src/MasterControlApp/MasterControlRuntime.cpp:10717` (`if (request.method == "POST" && request.path == "/api/providers/assignments")`).

- Dashboard: picked `providerId = xai-grok-20260419-075934` (credentialsConfigured=true, kind=xai).
- Initial sub_agent target attempt: `architect` was promoted to `kind:role` by the server (it is a registered role id), so specialistsReadyCount stayed at 1 after first pair. Re-selected a pure sub_agent: `nexus`.

Assignment posts:
```
POST /api/providers/assignments {"targetId":"architect","kind":"sub_agent","providerId":"xai-grok-20260419-075934"} -> 200 "Provider ownership was updated." (server stored as role)
POST /api/providers/assignments {"targetId":"forge","kind":"sub_agent","providerId":"xai-grok-20260419-075934"} -> 200 "Provider ownership was updated." (stored as sub_agent)
POST /api/providers/assignments {"targetId":"nexus","kind":"sub_agent","providerId":"xai-grok-20260419-075934"} -> 200 "Provider ownership was updated." (stored as sub_agent)
```

Readiness check:
```
GET /api/readiness -> HTTP 200, specialistsReadyCount=2, specialistsMissingCount=5
```

Retry:
```
POST /api/setup/workflow-templates/specialist-team-demo/instantiate {}
-> HTTP 200, succeeded:true, workflowId:"specialist-team-demo:4-targets"
Message: "Instantiated starter workflow 'Specialist Team Demo' (4 assignment(s) applied)." (2 roles + 2 specialists)
```

Saved: `G:/Claude/mcos_proof_starter_specialist_ok.json`, `mcos_proof_starter_assign_architect.json`, `mcos_proof_starter_assign_forge.json`, `mcos_proof_starter_assign_nexus.json`.

## 6. Bogus Template — VERIFIED

```
curl.exe -s -w "HTTP_CODE:%{http_code}" -X POST "http://127.0.0.1:7300/api/setup/workflow-templates/bogus-template-xyz/instantiate" -H "Content-Type: application/json" --data-binary "@G:/Claude/mcos_post_starter_bogus.json" -o G:/Claude/mcos_proof_starter_bogus.json
```

- HTTP 404. `succeeded:false`. Message: `"Unknown starter workflow template id."` (exact match, clean).

---

**Verdict: Starter workflow feature FULLY VERIFIED — all 6 bullets pass on build 48.**
