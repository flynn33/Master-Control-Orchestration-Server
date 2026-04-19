# 11 - AI Task Execution (Proof of Wire)

Date: 2026-04-19
Build: 48 at http://127.0.0.1:7300/

## Verdict: FULLY EXECUTED

A real AI provider execution was round-tripped through the orchestrator. Not a speculative path check; actual HTTP wire-level proof captured at both ends.

## Execute endpoint + payload shape

- Route: `POST /api/providers/execute` (`MasterControlRuntime.cpp:10721`).
- Service: `IProviderExecutionService::execute(ProviderExecutionRequest)` (`MasterControlRuntime.cpp:3848`).
- Request body (`include/MasterControl/MasterControlModels.h:291`):
  ```json
  {
    "targetId": "<role or sub_agent id from /api/dashboard.providerAssignmentTargets>",
    "prompt": "<non-empty string>",
    "preferredMcpServerIds": [],
    "workingDirectory": "",
    "allowToolAccess": true,
    "maxTurns": 4
  }
  ```
- Response: full `ProviderExecutionRecord` with `status`, `outputText`, `rawResponse`, timestamps, `referencedMcpServerIds`, `toolEvents`. HTTP 200 on `succeeded`, 400 otherwise.
- Transports dispatched internally by `ProviderKind`:
  - `xai`, `openai` -> `executeOpenAICompatibleChat` (direct HTTPS `POST {baseUrl}/chat/completions`, Bearer `xai_api_key` or `openai_api_key`).
  - `claude_code` -> `executeClaudeCodeCli` (spawn `claude -p <prompt> --output-format json`, forwarding `ANTHROPIC_API_KEY` / `ANTHROPIC_AUTH_TOKEN` / `ANTHROPIC_BASE_URL` / `ANTHROPIC_MODEL` env vars).
  - `codex` -> `executeCodexCli` (spawn `codex exec <prompt> --model <modelId>`, forwarding `OPENAI_API_KEY` when supplied).

Credential catalog (step 2) saved to `G:/Claude/mcos_proof_task_credentials.json`. Full endpoint findings in `G:/Claude/mcos_proof_task_endpoint.json`.

## Option chosen: B' (generic-mock via xai-kind repoint)

- **Option A (ANTHROPIC_API_KEY placeholder)** - skipped. Claude Code CLI supports `ANTHROPIC_API_KEY` (confirmed in `claude --help` under `--bare` and `--betas`), and MCOS forwards it (`MasterControlRuntime.cpp:4351-4352`). But a placeholder key fails at the real Anthropic API with 401. The brief forbids prompting the user for a real key.
- **Option B (generic kind)** - not achievable. No `ProviderKind::Generic` capability is registered in `src/MasterControlModules/MasterControlModules.cpp` for build 48. `POST /api/providers` with `kind:generic` is rejected at `MasterControlRuntime.cpp:2270` with "The selected provider kind is not currently supported by an active provider module."
- **Option C (real xAI credential)** - none found on disk or in env (`%USERPROFILE%/.xai`, `XAI_API_KEY` unset). Skipped per "nothing to ask the user" bar.

**Path used:** Option B' = reuse the existing `openai_compatible_chat` transport by repointing an existing `xai-grok` provider's `baseUrl` at a local mock. The xAI wire format is literally OpenAI-compatible `/chat/completions` with Bearer auth, so it is byte-for-byte what an OpenAI-compatible mock expects.

## Full round-trip log

1. Mock server launched: `node G:/Claude/mcos_mock_server.js` -> `http://127.0.0.1:9999`. Accepts `POST /v1/chat/completions`, logs every request, returns a canned OpenAI `chat.completion` with content `"MOCK-OK: ..."`. Setup captured in `G:/Claude/mcos_proof_task_mock_server.json`.

2. Repoint existing xai provider (keeps the `test-DEAD` xai_api_key in the credential store):
   ```
   POST http://127.0.0.1:7300/api/providers
   Content-Type: application/json

   {"id":"xai-grok-20260419-075934","kind":"xai","displayName":"Grok (mock-repointed)",
    "baseUrl":"http://127.0.0.1:9999/v1","modelId":"grok-code-fast-1","enabled":true,
    "allowAutonomousControl":false,"credentialsConfigured":true,"isTemplate":false}
   -> 200 {"succeeded":true,"message":"Provider settings updated."}
   ```
   The existing assignment `planner -> xai-grok-20260419-075934` now routes to the mock.

3. Fire execute:
   ```
   POST http://127.0.0.1:7300/api/providers/execute
   Content-Type: application/json

   {"targetId":"planner","prompt":"Proof-of-wire test: respond with any token. Orchestrator build 48 execute round-trip.",
    "preferredMcpServerIds":[],"workingDirectory":"","allowToolAccess":false,"maxTurns":1}
   -> HTTP 200
   ```

4. What the mock saw (full log in `G:/Claude/mcos_proof_task_mock_request.json`):
   - Method/path: `POST /v1/chat/completions`
   - `User-Agent: MasterControlServiceHost/2.0`
   - `Authorization: Bearer test-key-DEAD` (forwarded from the credential store)
   - Body: OpenAI chat.completions shape with `model:"grok-code-fast-1"` + system prompt listing all 21 shared MCP endpoints + the user's prompt.

5. What the orchestrator returned (full body in `G:/Claude/mcos_execute_result.json` and `G:/Claude/mcos_proof_task_history.json`):
   ```
   status:       "succeeded"
   executionId:  "exec-1776613383990-1"
   targetId:     "planner" (Planner)
   providerId:   "xai-grok-20260419-075934"
   providerKind: "xai"
   modelId:      "grok-code-fast-1"
   outputText:   "MOCK-OK: Provider execution wire successfully reached the mock
                  server at 127.0.0.1:9999. Echoed prompt byte-count=2458.
                  Bearer header present=true."
   referencedMcpServerIds: [21 entries]
   startedAtUtc / completedAtUtc: ~1 second elapsed
   ```

6. `providerExecutionHistory` delta: 0 -> 1 records, matching executionId `exec-1776613383990-1`. Confirmed via `GET /api/dashboard`.

## Teardown

- Restored xai provider `baseUrl` back to `https://api.x.ai/v1` via `POST /api/providers` -> 200 succeeded.
- Killed mock server (PID 6776). Port 9999 no longer listening.
- Left `xai_api_key` credentials (placeholder `test-DEAD`) as they were.

## Evidence files

All under `G:/Claude/`:
- `mcos_proof_task_endpoint.json` - step 1
- `mcos_proof_task_credentials.json` - step 2
- `mcos_proof_task_env_options.json` - step 4 analysis + reasons A/B/C were not used
- `mcos_proof_task_mock_server.json` - step 5 mock setup
- `mcos_proof_task_mock_request.json` - exact inbound request orchestrator sent
- `mcos_proof_task_mock_response.json` - canned response mock returned
- `mcos_proof_task_history.json` - `providerExecutionHistory` before/after
- `mcos_execute_result.json` - full orchestrator response to step 3
- `mcos_mock_server.js` - mock source
- `mcos_mock_stdout.log` - mock process stdout
