# Receipt 18 — Apple host real connection probe

**Build:** 48 (commit `d187951`) &nbsp;·&nbsp; **Date:** 2026-04-19 &nbsp;·&nbsp; **Host:** Windows 11 Pro, OpenSSH Server NOT installed

## Verdict
**CONNECTION PROBE PATH PROVEN** — diagnostic-rich error observed against an unreachable SSH target. The SSH probe runs, uses `ssh.exe -o BatchMode=yes -o ConnectTimeout=8`, and produces structured readiness issues + a human-readable toolchain message when the peer is unreachable. The probe is wired, fires end-to-end, and produces meaningful output.

One product gap is noted (not broken, just not ideal UX): there is **no dedicated `/apple-hosts/probe` or `/apple-hosts/inspect` endpoint**. The probe is triggered indirectly by `GET /api/platform-services/config/<platform>` via `selectHostForPlatform()` → `inspectHostState()` → `inspectSshHost()`. This works but means clients must know to request a platform config document to force a re-probe.

## Evidence

### 1. Local sshd state (`mcos_proof_apple_sshd_state.txt`)
- `Get-Service sshd` → **NOT installed**
- `Get-Service ssh-agent` → Stopped / Disabled
- `Test-NetConnection 127.0.0.1:22` → `TcpTestSucceeded: False`
- Step 7 (localhost SSH probe with bogus user) skipped — captured in `mcos_proof_apple_probe_local_sshd.json` with `skipped:true` and reason.

### 2. Probe code location (file:line)
Source: `src/MasterControlApp/MasterControlRuntime.cpp`
- `class AppleRemoteHostService : public IAppleRemoteHostService` — **line 5845**
- `inspectHost(const std::string& hostId)` — **line 5899** (locks, finds host, calls `inspectHostState`, persists)
- `inspectHostState(const AppleRemoteHost& host)` — **line 6613** (dispatches by transport)
- `inspectSshHost(AppleRemoteHost host)` — **line 6529** (finds `ssh.exe`, runs `xcode-select -p`, `xcodebuild -version`, `xcrun --sdk macosx/iphoneos --show-sdk-path`, `xcrun simctl/devicectl`, `security find-identity`)
- `executeSshCommand(...)` — **line 6441** (spawns `ssh.exe -o BatchMode=yes -o ConnectTimeout=8 [-p port] [user@]host sh -lc <script>` via `runProcessCapture`)
- `deriveAppleReadinessIssues(...)` — **line 6243** (builds the `readinessIssues[]` strings)

### 3. HTTP routes (no dedicated probe route)
- `GET  /api/platform-services/apple-hosts` — line **10540** (returns cached list, NO re-probe)
- `POST /api/platform-services/apple-hosts` — line **10708** (upsert)
- `POST /api/platform-services/apple-hosts/remove` — line **10712** (delete)
- `GET  /api/platform-services/config/<platform>` — line **10543** → triggers `selectHostForPlatform()` at line **9901** which re-probes ALL enabled hosts for that platform
- `GET  /api/platform-services/governance` — line **10537**

### 4. Register unreachable host (`mcos_proof_apple_register_unreachable.json`)
POST to `/api/platform-services/apple-hosts` with `10.0.0.99:22`, user `probe`, platforms `["macos"]` →
```
{"message":"Apple remote host updated.","requiresConfirmation":false,"succeeded":true}
```

### 5. Probe fired (`mcos_proof_apple_probe_unreachable.json`)
`GET /api/platform-services/config/macos` returned the selected host with live probe state:
- `toolchain.checkedAtUtc: "2026-04-19T15:38:49Z"` (proves probe ran, not cached)
- `toolchain.reachable: false`
- `toolchain.status: "offline"`
- `toolchain.message: "The remote Mac did not respond to xcode-select. The macOS SDK is not available on the selected host."`
- `signing.message: "No usable code-signing identities were detected on the selected Apple host."`
- `readinessIssues: ["Remote host is unreachable.", "Xcode is unavailable.", "macOS SDK route is unavailable."]`
- `transportSummary: "SSH to probe@10.0.0.99:22"`
- `routeable: false`

This is diagnostic-rich, non-empty, non-null. The `checkedAtUtc` stamp confirms a real `ssh.exe` launch was attempted against 10.0.0.99 and it failed cleanly per `BatchMode=yes ConnectTimeout=8`.

### 6. Governance lane reflection (`mcos_proof_apple_governance_state.json`)
The `/api/platform-services/governance` response lists the macOS and iOS lanes with `requiresRemoteToolchain: true`. The MCP server status is `online` (the governance endpoint itself is up) but the config document at step 5 correctly reports `routeable: false` when no ready host is available. That is the correct separation of concerns — lane exists, server online, but no routeable Apple toolchain attached.

### 7. Localhost sshd probe (`mcos_proof_apple_probe_local_sshd.json`)
Skipped — no local sshd. Documented.

### 8. Cleanup
`POST /api/platform-services/apple-hosts/remove` with `{"hostId":"probe-unreachable-01"}` →
```
{"message":"Apple remote host removed.","requiresConfirmation":false,"succeeded":true}
```
Verified: subsequent `GET /api/platform-services/apple-hosts` → `[]`.

## Product gap flagged
The probe is wired and works, but there is no explicit probe endpoint. A dedicated `POST /api/platform-services/apple-hosts/{hostId}/probe` would be more discoverable than requiring clients to pull `/config/<platform>` to force a re-inspect. Not blocking; worth logging as a minor UX item.
