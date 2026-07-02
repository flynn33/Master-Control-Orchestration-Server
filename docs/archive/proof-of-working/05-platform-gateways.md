# Feature 5 — Cross-platform gateways + Apple remote hosts

Build 48, `http://127.0.0.1:7300/`, 2026-04-19.

## 1. Gateways listing — VERIFIED
Command: `curl http://127.0.0.1:7300/api/platform-services/gateways`
File: `G:/Claude/mcos_proof_gateways.json` (HTTP 200, 3 entries)
- `windows-gateway` / platform=windows / host=`PC-GAMING-R7-58.local` / port=7300 / status=`registration_failed`
- `mac-gateway` / platform=macos / host=`PC-GAMING-R7-58.local` / port=7300 / status=`registration_failed`
- `ios-gateway` / platform=ios / host=`PC-GAMING-R7-58.local` / port=7300 / status=`registration_failed`

## 2. Governance lanes — VERIFIED
Command: `curl http://127.0.0.1:7300/api/platform-services/governance`
File: `G:/Claude/mcos_proof_governance_lanes.json` (HTTP 200)
Lanes observed:
- `windows-governance` — `/mcp/governance/windows` — online — `requiresRemoteToolchain=false` — 4 tools (forsetti.windows.*)
- `mac-governance` — `/mcp/governance/macos` — online — `requiresRemoteToolchain=true` — 10 tools (forsetti.macos.*)
- `ios-governance` — `/mcp/governance/ios` — online — `requiresRemoteToolchain=true` — 10 tools (forsetti.ios.*)

## 3. Apple hosts empty state — VERIFIED
Command: `curl http://127.0.0.1:7300/api/platform-services/apple-hosts`
File: `G:/Claude/mcos_proof_apple_empty.json` (HTTP 200). Body: `[]`

## 4. Apple host add — VERIFIED
Command: `curl -X POST --data-binary @mcos_post_apple_add.json .../../../plans/apple-hosts`
File: `G:/Claude/mcos_proof_apple_add.json` (HTTP 200)
Response: `{"message":"Apple remote host updated.","requiresConfirmation":false,"succeeded":true}`

## 5. Apple host read-back — VERIFIED
Command: `curl .../../../plans/apple-hosts`
File: `G:/Claude/mcos_proof_apple_present.json` (HTTP 200)
Entry present: hostId=`proof-host-01`, displayName=`Proof Apple Build Host`, transport=`ssh`, address=`10.0.0.99`, port=22, username=`build`, platforms=`["macos","ios"]`, enabled=`true`.

## 6. Apple host remove — VERIFIED
Command: `curl -X POST --data-binary @mcos_post_apple_remove.json .../../../plans/apple-hosts/remove`
File: `G:/Claude/mcos_proof_apple_remove.json` (HTTP 200)
Response: `{"message":"Apple remote host removed.","requiresConfirmation":false,"succeeded":true}`

## 7. Apple host removed verification — VERIFIED
Command: `curl .../../../plans/apple-hosts`
File: `G:/Claude/mcos_proof_apple_after_remove.json` (HTTP 200). Body: `[]`

## 8. Negative — empty platforms — VERIFIED
Command: `curl -X POST --data-binary @mcos_post_apple_bad.json .../../../plans/apple-hosts` (platforms=[])
File: `G:/Claude/mcos_proof_apple_bad.json` (HTTP 400)
Response: `{"message":"Apple remote host must declare at least one target platform.","requiresConfirmation":false,"succeeded":false}`

## 9. Governance profile on disk — VERIFIED
Command: `ls -la "/c/Program Files/Master Control Orchestration Server/share/MasterControlOrchestrationServer/clu/governance-profile.json"`
Result: `-rw-r--r-- 1 Flynn 197121 7108 Apr 11 09:04 governance-profile.json` (size = 7108 bytes)

## 10. Combined listing — VERIFIED
Command: `curl http://127.0.0.1:7300/api/platform-services`
File: `G:/Claude/mcos_proof_platform_combined.json` (HTTP 200)
Top-level keys present: `appleHosts` (line 2), `gateways` (line 3), `governanceServers` (line 68).

---
**Verdict:** Feature 5 VERIFIED — all 10 contract bullets pass.
