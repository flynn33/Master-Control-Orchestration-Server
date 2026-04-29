# 11 — LAN Client End-to-End Verification

Status: **verification recipe** (not a captured run). The Phases 1–9 rebuild was authored as code in this branch; running the build and capturing live receipts is the operator's next step. This file pins the verification flow so the operator can replay it on their host and capture evidence in a sibling `mcos_proof_lan_client_*.json` set.

This receipt covers the originally stated product intent: multiple AI models on the LAN connect to MCOS as governed users, share an MCP and sub-agent fabric, and operate under per-client privileges with operator-approval queue for high-impact mutations.

---

## Build prerequisites

```powershell
# Configure and build (Debug)
cmake --preset debug
cmake --build build\debug --config Debug

# Tests
ctest --test-dir build\debug -C Debug --output-on-failure

# Forsetti compliance
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\check-mastercontrol-forsetti.ps1
```

All three must pass before the runtime checks below are meaningful.

Expected post-Phase-7 module count: **16** active modules. Expected protected-set membership: **4** (configuration, runtime-inventory, command-logic-unit, dashboard-ui).

---

## Runtime verification scenario

### Scenario

Operator on `MCOS-HOST`, two AI clients on remote LAN hosts:
- **alpha** — Claude Code on host A, will be granted `canCreateMcpServers` and autonomous mode after enabling global AI autonomy
- **bravo** — Codex on host B, will be granted **no** privileges initially

Goal: alpha creates and modifies a shared MCP server; bravo discovers it but is denied modify; the operator approves a deferred governance change.

### Step 0 — service health

```bash
curl http://127.0.0.1:7300/api/health
# Expected: {"status":"ok","time":"<rfc3339>"}

curl http://127.0.0.1:7300/api/forsetti/modules | jq '.modules[].moduleId'
# Expected: 16 module ids; com.mastercontrol.lan-client-access present
```

### Step 1 — register both clients

```bash
curl -X POST http://127.0.0.1:7300/api/clients \
  -H "Content-Type: application/json" \
  -d '{"clientId":"alpha","displayName":"Alpha","clientType":"claude_code","hostName":"host-A"}'
# Expected: 200 {"succeeded":true,"message":"LAN client registered."}

curl -X POST http://127.0.0.1:7300/api/clients \
  -H "Content-Type: application/json" \
  -d '{"clientId":"bravo","displayName":"Bravo","clientType":"codex","hostName":"host-B"}'
# Expected: 200 {"succeeded":true,"message":"LAN client registered."}

curl http://127.0.0.1:7300/api/clients | jq 'length'
# Expected: 2
```

Activity ring should record `lan-client-created` × 2.

### Step 2 — grant alpha's privileges

```bash
curl -X POST http://127.0.0.1:7300/api/clients/alpha/privileges \
  -H "Content-Type: application/json" \
  -d '{"canCreateMcpServers":true,"canModifyMcpServers":true,"canCreateSubAgents":true}'
# Expected: 200 {"succeeded":true,"message":"LAN client privileges updated."}

curl http://127.0.0.1:7300/api/clients/alpha | jq '.privileges'
# Expected: canCreateMcpServers true, canModifyMcpServers true, canCreateSubAgents true,
#           everything else false
```

Activity ring: `lan-client-privileges-changed` keyed to `target: alpha`.

### Step 3 — turn on global AI autonomy and enable alpha's autonomous mode

```bash
# Get current config and flip aiAutonomyEnabled to true
curl http://127.0.0.1:7300/api/config | jq '. | .aiAutonomyEnabled = true' > config.json
curl -X POST http://127.0.0.1:7300/api/config \
  -H "Content-Type: application/json" \
  -H "X-Confirm-Unsafe: 1" \
  --data-binary @config.json
# Expected: 200 {"succeeded":true,...}

curl -X POST http://127.0.0.1:7300/api/clients/alpha/autonomous-mode \
  -H "Content-Type: application/json" \
  -d '{"enabled":true}'
# Expected: 200 {"succeeded":true,"message":"Autonomous mode enabled."}
```

Activity ring: `lan-client-autonomous-mode-changed`.

### Step 4 — verify autonomous-mode soft gate while autonomy disabled

(Test in a fresh state — flip `aiAutonomyEnabled` back to false and try to enable autonomous on bravo.)

```bash
curl -X POST http://127.0.0.1:7300/api/clients/bravo/autonomous-mode \
  -H "Content-Type: application/json" \
  -d '{"enabled":true}'
# Expected: HTTP 403, body containing:
#   "ruleId": "CLU-C009"
#   "message": "Enable global AI autonomy in configuration before granting client autonomous mode."
#   "outcome": "block"
```

Re-enable global autonomy before continuing.

### Step 5 — alpha downloads its bundle

```bash
curl http://127.0.0.1:7300/api/clients/alpha/config | jq . > lan-client-alpha.json
cat lan-client-alpha.json | jq '.identification'
# Expected: { "header": "X-MCOS-Client-Id", "value": "alpha" }

cat lan-client-alpha.json | jq '.mcosServer'
# Expected: "http://<resolved-host>:7300"   (NEVER "0.0.0.0")

cat lan-client-alpha.json | jq '.privileges.canCreateMcpServers'
# Expected: true

cat lan-client-alpha.json | jq '.autonomousMode'
# Expected: true
```

### Step 6 — alpha creates an MCP server (autonomous bypass on Create)

```bash
curl -X POST -H "X-MCOS-Client-Id: alpha" \
     http://127.0.0.1:7300/api/runtime/mcp-servers \
  -H "Content-Type: application/json" \
  -d '{"id":"shared-tool","displayName":"Shared Tool","host":"127.0.0.1","port":9000,"protocol":"http","kind":"mcp_server"}'
# Expected: 200 succeeded

curl http://127.0.0.1:7300/api/client/mcp-servers | jq '.[] | select(.id=="shared-tool")'
# Expected: the shared-tool entry
```

### Step 7 — bravo discovers but cannot modify

```bash
curl -H "X-MCOS-Client-Id: bravo" \
     http://127.0.0.1:7300/api/client/mcp-servers | jq '.[] | select(.id=="shared-tool") | .id'
# Expected: "shared-tool"  (use is universal)

curl -X POST -H "X-MCOS-Client-Id: bravo" \
     http://127.0.0.1:7300/api/runtime/mcp-servers \
  -H "Content-Type: application/json" \
  -d '{"id":"shared-tool","displayName":"Hijacked","host":"127.0.0.1","port":9001,"protocol":"http","kind":"mcp_server"}'
# Expected: HTTP 403
#   "errorMessage": "Required privilege missing: canModifyMcpServers."
#   "actor": "bravo"
#   "privilege": "canModifyMcpServers"
```

### Step 8 — disable bravo, confirm the door is closed

```bash
curl -X POST http://127.0.0.1:7300/api/clients/bravo/disable
# Expected: 200

curl -H "X-MCOS-Client-Id: bravo" \
     http://127.0.0.1:7300/api/client/mcp-servers
# Expected: HTTP 403
#   "errorMessage": "LAN client is disabled: bravo"
```

### Step 9 — autonomous client creates 10 MCP servers in a row

```bash
for i in $(seq 1 10); do
  curl -X POST -H "X-MCOS-Client-Id: alpha" \
       http://127.0.0.1:7300/api/runtime/mcp-servers \
    -H "Content-Type: application/json" \
    -d "{\"id\":\"alpha-$i\",\"displayName\":\"Alpha $i\",\"host\":\"127.0.0.1\",\"port\":$((9100+i)),\"protocol\":\"http\",\"kind\":\"mcp_server\"}"
done
# Expected: 10 × 200 succeeded
```

Then strip alpha's `canCreateMcpServers` privilege but keep autonomous mode and re-run:

```bash
curl -X POST http://127.0.0.1:7300/api/clients/alpha/privileges -d '{"canModifyMcpServers":true}'
curl -X POST -H "X-MCOS-Client-Id: alpha" \
     http://127.0.0.1:7300/api/runtime/mcp-servers \
  -d '{"id":"alpha-after-strip","displayName":"After","host":"127.0.0.1","port":9999,"protocol":"http","kind":"mcp_server"}'
# Expected: 200 — autonomous mode bypasses canCreateMcpServers per ADR-001
```

### Step 10 — activity stream shows the actor on every event

```bash
curl 'http://127.0.0.1:7300/api/activity?since=0' | jq '.events[] | select(.kind | startswith("lan-client") or startswith("governance")) | {kind, actor, target, message}'
# Expected: every privileged mutation attributed to either "operator" or "alpha" / "bravo"
```

### Step 11 — exports include alpha's bundle

```bash
curl http://127.0.0.1:7300/api/exports | jq '.[] | select(.id | startswith("lan-client-config:")) | .id'
# Expected: "lan-client-config:alpha"
# (bravo is disabled in step 8 so deliberately omitted from /api/exports;
#  GET /api/clients/bravo/config still serves its bundle for audit)
```

### Step 12 — browser dashboard end-to-end

Open `http://127.0.0.1:7300/` in a browser:

1. Health badge transitions to **Online** within 5 seconds.
2. **Overview** destination shows posture pass, 2 LAN clients, 1 autonomous, 0 pending approvals.
3. **LAN Clients** destination lists alpha and bravo (bravo with disabled badge).
4. Click alpha → drawer shows privileges; click **Download config bundle** → browser saves `lan-client-alpha.json`.
5. **Shared Fabric** destination shows the 11 MCP servers alpha created.
6. **Activity** destination scrolls the lifecycle events from steps 1-11.

### Step 13 — installer build (smoke)

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\Package-MasterControlOrchestrationServer.ps1 -Preset release
```

Should produce a signed MSI in `dist/packages/release/`.

---

## Pass criteria

The receipt is complete when all of the following are observed:

- [ ] Build + ctest + Forsetti compliance pass.
- [ ] 16 modules active in `/api/forsetti/modules`.
- [ ] Two LAN clients register with persisted records that survive service restart.
- [ ] Step 4 returns HTTP 403 with `ruleId: "CLU-C009"`.
- [ ] Step 5 bundle has `mcosServer` reachable (not `0.0.0.0`) and `identification.header == "X-MCOS-Client-Id"`.
- [ ] Step 7's 403 response carries `privilege: "canModifyMcpServers"` and `actor: "bravo"`.
- [ ] Step 8's 403 message reads `LAN client is disabled: bravo`.
- [ ] Step 9 succeeds without explicit `canCreateMcpServers` privilege when autonomous mode is on.
- [ ] Step 11 surfaces `lan-client-config:alpha` and omits the disabled bravo's bundle.
- [ ] Step 12's browser flow lets the operator complete every action without curl.
- [ ] Activity stream attributes every mutation to a real actor (`operator` or a clientId).

When the receipt is captured, append the JSON output and the browser screenshots to `plans/PROOF-OF-WORKING/` and update this file's status badge from "verification recipe" to "✅ VERIFIED" plus the build hash and probe date.
