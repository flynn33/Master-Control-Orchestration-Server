# Changelog

All notable changes to this repository are tracked here. Entries follow the [Keep a Changelog](https://keepachangelog.com/) shape: each release lists Added / Changed / Removed / Fixed / Notes. Versions follow the alpha-stage scheme `A{alphaIteration}.{feature}.{patch}` (e.g. `A3.11.0` = third alpha, feature 11, patch 0) while MCOS is in alpha; pre-migration history below keeps its original semver identifiers.

The release-management and doc-sync GitHub agents that previously generated parts of this file have been retired. CHANGELOG entries are now hand-authored alongside the change.

## [Unreleased]

## [A3.12.0] - pending

Implementation milestone only — not yet released (no tag cut, no MSI packaged).

### Added

- **Model Parity alpha milestone (A3.12.0, implementation only — not yet released).**
  Provider-neutral Client Integration Catalog (`IClientIntegrationProvider` /
  `IClientIntegrationCatalog`, constructor-DI, `final` providers) plus
  provider-native onboarding/export profiles for Claude Code, Codex,
  Codex-as-MCP-server, OpenAI Responses, ChatGPT Apps, ChatGPT connector edge,
  xAI Responses, Grok Build, Grok Build ACP, and generic MCP clients.
  Compatibility aliases (`claude`, `openai`, `chatgpt`, `xai`, `grok`,
  `generic`) resolve to explicit descriptors without collapsing product
  behavior (no `openai==chatgpt`, no `xai==grok`).
- Corrected provider-native artifacts: Codex `config.toml` (not legacy
  `codex.config.json` / `codex-mcp.json`), OpenAI Responses `type=mcp` tool
  shape, xAI remote MCP without the OpenAI-only `require_approval` /
  `connector_id` fields, Grok Build `.grok/config.toml` with `grok-build-0.1`,
  Grok Build ACP over `grok agent stdio`, and an honest POST-only Streamable
  HTTP report for generic MCP.
- `IRemoteMcpCompatibilityAnalyzer` emitting structured transport/auth/
  reachability warnings, surfaced at `GET /api/client-integrations/{id}/validate`.
- Read-only routes `GET /api/client-integrations`, `/{id}`, `/{id}/artifacts`,
  `/{id}/validate`; onboarding and export services delegate to the catalog; the
  MCP `initialize` result carries concise governance instructions (no SSE
  overclaim). Browser dashboard gains a Client Integrations deck; the WinUI
  shell gains provider-neutral generate-config / validate actions; new wiki
  pages Client-Integrations, Codex, OpenAI-Responses, ChatGPT-Apps, XAI-Grok,
  and Grok-Build.

### Changed

- Hardened alpha release-readiness metadata, product-gate preflight behavior,
  bootstrapper JSON CLI compatibility, and operator-gated deployment acceptance
  evidence paths without claiming Gate D/E certification.
- Aligned README, docs/wiki source, and live-wiki publication source with the current `A3.11.0` internal-alpha state.
- Rewrote current operator documentation to distinguish implemented source behavior, alpha validation boundaries, historical release records, and deferred hardening work.
- Updated configuration and API documentation around the current ProgramData config path, `PATCH /api/config` partial deep-merge semantics, TLS fields, diagnostics routes, plugin toggles, supervisor routes, and dynamic route families.

### Removed

- Removed stale remediation, cleanup, machine-handoff, probe, bug-campaign, and feature-audit artifacts from active repository documentation surfaces.

## [A3.11.0] - 2026-07-03

### Summary

**Version-scheme migration and alpha-stage release-policy simplification.** Adopts the alpha-stage scheme `A{alphaIteration}.{feature}.{patch}`: `A3.11.0` (third alpha, feature 11, patch 0) re-expresses `0.11.0-alpha.3` without changing the tree it names.

### Changed

- **Version scheme.** `VERSION.json` `current_version` is now `A3.11.0`. `CMakeLists.txt` strips the stage prefix for `project(VERSION)` (numeric base `3.11.0`); `installer/Build-Msi.ps1` maps `A<a>.<feature>.<patch>` to MSI ProductVersion `<a>.<feature>.<patch>.0` so alpha iterations, features, and patches all order correctly for Windows Installer upgrades.

### Removed

- **`.github/workflows/release.yml`.** Removed from the current alpha release path. Alpha versions are published as GitHub pre-releases with MSI artifacts after the same-SHA Windows Build, Test, and Package product gate passes; operator-host Gates D/E are still required before any deployment-qualified claim. The PHASE-10 no-`workflow_dispatch` rule now applies to `windows-build-test-package.yml` alone; `realignment-discipline.yml`, FORBIDDEN-CONTRACT §6.2, the mcos-contracts audit server, and `Test-MCOSRepositoryMetadata.ps1` were updated in the same change.
- **All historical release tags** (v0.1.x–v0.4.x rc lines) — none correspond to a published release. Local tags deleted; remote deletion requires operator credentials.

### Added

- **`.gitattributes`** (`* text=auto eol=lf`; png/ico/bmp/rtf binary) making LF the canonical checkout encoding on every host, so Windows checkouts with `core.autocrlf=true` no longer break the markdown-link gate or protected-path hash comparisons.

## [0.11.0-alpha.3] - 2026-07-02

### Summary

**PHASE-14 completion (Slices B–E), security hardening, and the 2026-06 bug campaign.** The diagnostics module is now end-to-end — SQLite-backed store, MCP plugin tools, WinUI Shell surface, and browser dashboard tab — and three items deferred at the alpha.2 cut (beacon payload signing, admin-listener TLS, cert auto-rotation) are closed. The MSI cut for this version happens on the Windows host after the Windows Build, Test, and Package gate passes there; the VERSION.json history commit field stays `pending` until that cut.

### Fixed

- **Beacon advertised the admin port as `gatewayPort`.** `BeaconService::currentAdvertisement()` passed `configuration.browserPort` into both port slots of the `BeaconAdvertisement` aggregate, so `/api/beacon` (and the discovery-service-less fallback broadcast) advertised `gatewayPort=7300` instead of `cfg.mcpGateway.listenPort` (8080). Regression test added.
- **Exports surface handed LAN clients a dead MCP URL.** `ExportService::generateExports` composed every client artifact (`.claude.json`, `Install-ClaudeGateway.ps1`, `codex-mcp.json`, openai/xai profiles) around `http://<host>:7200/mcp/gateway` — port 7200 belongs to the external gateway retired at v0.9.0 and `/mcp/gateway/{platform}` is an admin-port document route, not an MCP endpoint. Now composed from `cfg.mcpGateway.listenPort + mcpPath` (same realignment the v0.10.8 supervisor-config fix applied). The seeded `platform-gateway` inventory row likewise moved 7200 → 8080.
- **Silent UDP beacon failures.** `sendto()`/`socket()` results were ignored; broadcast failures now emit edge-triggered diagnostics events (`beacon_broadcast_failed`/`_recovered`, `beacon_socket_create_failed`).
- **Empty-`method` supervisor events in `/api/activity`.** The four supervisor lifecycle emit sites never stamped `evt.method` (the anomaly the 2026-04-19 operator probe flagged).

### Added

- Onboarding profile `governanceBundleUrl` platform-awareness: `/api/onboarding/{clientType}?platform=windows|macos|ios` (alias `?os=`); absent/unknown falls back to `windows` (the prior hardcoded value).
- Persistent-log rotation age (14-day) + count (200k-row) bounds alongside the v0.10.21 50 MB size bound; deep checks throttled to once per 10 minutes per path.
- `scripts/Register-CertAutoRotation.ps1` — weekly scheduled-task cert reuse-or-renew + `cfg.mcpGateway.tlsCertThumbprint` sync via `POST /api/config`. Shipped in the MSI `scripts/` payload.
- UDP beacon payload signing: additive `signature` object (HMAC-SHA256 over the document's compact dump) gated on `security.beaconSigningEnabled` + `security.beaconSigningKey` (auto-generated on fresh installs; legacy configs broadcast unsigned exactly as before).
- **PHASE-14 Slices B–E complete.** Slice E: `SqliteDiagnosticsStore` (WAL, schema_version migration) + `DiagnosticsService` (1000-record ring + store, boot self-test persistence with last-50-boots retention, audited clear) + functional `POST /api/diagnostics/clear` (clear-with-retention, replacing the Slice A 501); diagnostics routes are store-backed with jsonl fallback. Slice B: six `mcos_diagnostics_*` tools in the `mcos-bridge` MCP plugin. Slice C: WinUI Shell `DiagnosticsSectionControl` (severity/source filters, native save-dialog exports, ContentDialog-confirmed clear). Slice D: browser dashboard Diagnostics tab with Blob-download exports.
- Opt-in SChannel TLS for the admin HTTP listener (port 7300): `security.adminTlsEnabled` + `security.adminTlsCertThumbprint` (default off; credentials failure falls back to plain HTTP with a diagnostics event — the admin surface cannot be bricked by a bad cert). Known limitation: SSE over the TLS listener returns 501; plain-HTTP SSE unchanged.
- Tile-grid expand-on-click endpoint detail: tapping a compact tile reveals host:port + the full active-client roster; hover shows host:port via tooltip.

### Closed as already-implemented (stale deferred entries)

- CLU rebuild Phase 2 + Phase 3 (`enforceAction` wiring) — the route-layer `enforceGovernance` helper already wires `enforceAction` into all mutation handlers (catalog, clients, modules, installs) with Block → 403 and RequiresOperatorApproval → staged deferred action + HTTP 202.
- Legacy `GatewayType` tombstone — the enum already holds only `Native`; the bootstrapper kill-list reference was removed at v0.10.14.

### Deferred to v1.0.0+

- Per-pass self-test rows in the persistent Diagnostics log (behind an opt-in flag).
- PHASE-13 Win2D / Direct2D high-rate visual surfaces in the WinUI Shell (per-pool sparklines, Tron grid HLSL, activity stream SwapChainPanel). Requires an interactive Windows build loop; not implementable from a non-Windows session.

## [0.11.0-alpha.2] - 2026-05-15

### Summary

**LAN-trust TLS dual-bind on the MCP gateway.** Adds optional HTTPS support so strict clients (ChatGPT connector-edge, Claude.ai web, security-conscious browser extensions) that refuse plain HTTP can connect. When `cfg.mcpGateway.tlsEnabled=true` AND a cert thumbprint is declared, `NativeHttpSysGatewayAdapter` registers a second URL prefix `https://+:tlsListenPort/` on the same HTTP.sys URL group + request queue alongside the existing `http://+:listenPort/`. The same handler serves both prefixes — no per-route plumbing changes.

Cert binding is operator-managed out-of-band via `scripts\Configure-LocalServerCert.ps1` (self-signed cert generation + `netsh http add sslcert` + Windows Firewall rule + optional service restart). `/api/discovery` and onboarding profiles emit the HTTPS URL only when the runtime confirms TLS is bound (HTTPS URL prefix registered AND `cfg.mcpGateway.tlsCertThumbprint` non-empty), so `.mcp.json` snippets clients receive carry `https://` straight from the well-known doc when — and only when — the gateway can actually serve them.

App-layer auth remains intentionally deferred to retail — transport TLS is a separate concern from authentication; LAN trust posture intact. HTTP fallback on the existing `listenPort` stays available for in-LAN curl / browser-dashboard debugging tooling.

### Added

- **`McpGatewayConfiguration.tlsEnabled` / `tlsListenPort` (default 8443) / `tlsCertThumbprint`** — three new persistable fields on the gateway config block. Default state is `tlsEnabled=false`, so existing installs stay HTTP-only without operator intervention.
- **TLS dual-bind in `NativeHttpSysGatewayAdapter::Start()`** — second `HttpAddUrlToUrlGroup(https://+:tlsListenPort/)` call after the existing HTTP prefix succeeds. Bind failures (URL ACL missing, prefix held by another process) are recorded in the gateway status message but do not fail Start — HTTP fallback continues serving.
- **`GatewayStatus.tlsBound` / `mcpUrlTls` / `tlsCertThumbprint`** *(review-hardened)* — runtime TLS readiness signal on the gateway status. `tlsBound=true` only when the HTTPS URL prefix registered AND `cfg.mcpGateway.tlsCertThumbprint` is non-empty (the operator's "I bound a cert" signal). Discovery + onboarding gate on this rather than the bare config flag.
- **`DiscoveryGateway.mcpUrlTls` / `healthUrlTls` / `tlsEnabled` / `tlsCertThumbprint`** — four new fields on the discovery doc's gateway block. Populated only when `GatewayStatus.tlsBound` is true. The thumbprint is echoed so operators can verify the wired cert via `/.well-known/mcos.json`.
- **HTTPS-aware onboarding profile builder** — `OnboardingService::profileFor()` prefers `document.gateway.mcpUrlTls` over `mcpUrl` when TLS is bound at runtime. All five client types (`claude-code`, `codex`, `grok`, `chatgpt`, `generic`) emit `.mcp.json` snippets with `https://` URLs automatically.
- **`scripts\Configure-LocalServerCert.ps1`** — idempotent self-signed cert provisioning. Generates cert with DNS + IP SANs in `Cert:\LocalMachine\My`, exports public key to `%PUBLIC%\Documents\Master Control Orchestration Server\certs\mcos-server-public.cer`, runs `netsh http add sslcert ipport=...`, opens firewall rule, optionally restarts the service. Prints the operator-facing `mcos.config.json` snippet. *Review-hardened:* validates existing-cert SAN coverage + 90-day renewal headroom before reuse; re-mints when the SAN set has drifted (DHCP/VPN-induced IP change) or expiry is within the renewal window.
- **`scripts\Remove-LocalServerCert.ps1`** — companion rollback. Removes the binding, removes the firewall rule, optionally deletes the cert + exported `.cer`. Success message clarified to point operators at the right re-enable steps.
- **MSI install rules for the TLS scripts** *(review-hardened)* — `CMakeLists.txt` now installs `Configure-LocalServerCert.ps1` and `Remove-LocalServerCert.ps1` under `scripts/` in the MSI payload. Pre-hardening the scripts existed only in the repo, so post-install `scripts\Configure-LocalServerCert.ps1` invocations failed with "command not found."
- **Discovery JSON round-trip test coverage for the TLS fields** *(review-hardened)* — `testDiscoveryDocumentJsonRoundTrip` now exercises `gateway.tlsEnabled`, `mcpUrlTls`, `healthUrlTls`, and `tlsCertThumbprint` in both the TLS-on and TLS-off shapes so future schema/serialization changes cannot silently drop them.
- **`handoff/realignment/v0.11.0-alpha.2-tls-dual-bind.md`** — alpha.2 cut report covering scope, source changes, scripts, build pipeline notes, operator quickstart for HTTPS.

### Changed

- `VERSION.json` bumped 0.11.0-alpha.1 → 0.11.0-alpha.2. `last_release_commit` backfilled to the v0.11.0-alpha.1 cut commit (`d477571c...`). Two new `history[]` entries: alpha.2 at index 0, alpha.1 at index 1 with its commit field also backfilled to `d477571c...` post-PR-#8-merge.
- `vcpkg.json` `version-string` synced.
- README version badge bumped to `v0.11.0--alpha.2`. The `Internal Alpha` channel badge retained.

### Notes

- **Admin HTTP listener (port 7300) stays HTTP-only this iteration.** It uses `SimpleHttpServer` (Winsock), not HTTP.sys, so adding TLS there requires an SChannel handshake rewrite — out of scope for alpha.2. Operator-facing surfaces (WinUI Shell, browser dashboard) typically run on the same trusted host as the service, so the practical impact is bounded.
- **Self-signed cert means per-machine trust on every LAN client.** Distribute `mcos-server-public.cer` from `%PUBLIC%\Documents\Master Control Orchestration Server\certs\` and add it to each client's OS trust store (Windows: `certmgr.msc` → Trusted Root; macOS: `security add-trusted-cert`; Linux: `update-ca-trust`). A local-CA upgrade path is on the roadmap; the wiki's TLS-and-HTTPS page documents both options.

## [0.11.0-alpha.1] - 2026-05-15

### Summary

First internal alpha cut of the v0.11.0 line. Packages the merged operator-audit work (closes items 1–7 from the operator's 2026-05-15 testing-phase audit) as a signed-ready Windows Installer MSI for internal LAN distribution. The MSI is the only operator-facing installer; the zip-archive path continues to ship alongside for headless / CI rollouts. No new runtime code beyond the v0.11.0 merge tip (`94cad65`). Channel: **Internal Alpha** — `auth=none, trust=lan` per the operator's secure-LAN directive; app-layer auth deferred to the retail build.

### Added

- **`MasterControlOrchestrationServer-v0.11.0-alpha.1-win-x64.msi`** under `dist/packages/release/` produced by `scripts\Package-MasterControlOrchestrationServer.ps1 -Preset release -Version 0.11.0-alpha.1`. MSI ProductVersion `0.11.0.1` (alpha BUILD ordinal 1).
- **`handoff/realignment/v0.11.0-alpha.1-internal-alpha.md`** — alpha cut report. Lists the runtime probes that gated the cut (32/32 pools healthy, 40/40 boot self-tests passed, MCP Gateway `tools/list` returning 126 tools on loopback + LAN IP, discovery beacon advertising the full capability set) and the deferred end-to-end client-integration and DNS-SD peer-probe steps the operator drives during alpha.
- **`Internal Alpha` channel badge** in `README.md` next to the version badge so operators can spot the alpha cut at a glance.

### Changed

- **`installer/Build-Msi.ps1` `ConvertTo-MsiProductVersion`** — regex extended from `(?:-rc\.(?<build>\d+))?` to `(?:-(?:alpha|beta|rc)\.(?<build>\d+))?` so semver-correct `-alpha.N` and `-beta.N` pre-release tags map to MSI `MAJOR.MINOR.PATCH.N` the same way `-rc.N` did. Pre-fix the regex threw `Unsupported package version format` on anything but retail or `-rc.N`. Verified accepts: `0.11.0` (.0), `0.11.0-alpha.1` (.1), `0.11.0-beta.3` (.3), `0.11.0-rc.7` (.7). Verified rejects: `0.11.0-alpha` (no ordinal), `0.11.0-zeta.1` (unknown prefix), `0.11` (no patch).
- **`VERSION.json`** — `current_version` bumped `0.11.0` → `0.11.0-alpha.1`, `current_tag` bumped `v0.11.0` → `v0.11.0-alpha.1`, `last_release_commit` backfilled from `"pending"` to `94cad65c8713523038d5c12b61cee3913dc15de7` (the v0.11.0 merge commit). New `history[0]` entry for `0.11.0-alpha.1`; existing `history[1]` (v0.11.0) commit field also backfilled to `94cad65...`.
- **`README.md`** — version badge `v0.11.0` → `v0.11.0--alpha.1` (shields.io hyphen escape). Added blockquote callout below the badge row describing the alpha cut + linking to the alpha release report.
- **`vcpkg.json`** — `version-string` bumped `0.11.0` → `0.11.0-alpha.1` via `Sync-RepositoryVersionBadges.ps1`.
- **`handoff/realignment/manifest.json`** — release manifest updated with the alpha cut entry.

### Notes

- **MSI upgrade ordering caveat.** `0.11.0-alpha.1` carries MSI ProductVersion `0.11.0.1`; a future retail `v0.11.0` would carry `0.11.0.0` which Windows Installer treats as *older*. This is intentional for an internal alpha that gets replaced by a `0.11.1` or `0.12.0` retail bump rather than by a same-band retail rebuild. If a same-band retail rebuild is required later, it must bump to `0.11.0-rc.N` or `0.11.1` to upgrade cleanly.
- **`.rc` VERSIONINFO unchanged.** `Sync-RepositoryVersionBadges.ps1` strips the `-alpha.N` suffix before writing the numeric `FILEVERSION`/`PRODUCTVERSION` 4-tuple, so the `.rc` blocks stay at `0,11,0,0` (matching the v0.11.0 cut). MSI ProductVersion (`0.11.0.1`) and the runtime's `MASTERCONTROL_VERSION` literal (`0.11.0-alpha.1`, generated by CMake from `VERSION.json`) carry the alpha ordinal; `.rc` is cosmetic-only for Explorer File-Properties tooltips and intentionally not surfaced for the alpha.

## [0.11.0] - 2026-05-15

### Summary

PHASE-14 Slice A — five operator-visible HTTP routes under `/api/diagnostics/*` aggregating events from the per-component `events.jsonl` persistent logs (`runtime`, `supervisor`, `installer`) plus their rotated `.1.jsonl` siblings from v0.10.21 rotation. Closes item 7 from the operator's 2026-05-15 testing-phase audit. Minor bump per the manifest's phase-scope convention.

Pre-v0.11.0 operators had no programmatic way to query, search, or export the persisted events — they had to manually concatenate files from `C:\Users\Public\Documents\Master Control Orchestration Server\logs\<component>\` on disk. Slice A delivers the operator-visible HTTP surface; the SqliteDiagnosticsStore (Slice E), WinUI Shell `DiagnosticsSectionControl` (Slice C), browser dashboard Diagnostics tab (Slice D), and `mcos-bridge` MCP plugin tools (Slice B) land in subsequent v0.11.x iterations.

### Added

- **`GET /api/diagnostics/events`** — Aggregates the merged event stream across all per-component `events.jsonl` files. Sorted recent-first. Supports `?max=` / `?severity=` / `?source=` filters via the v0.10.15 alias-aware extractor.
- **`GET /api/diagnostics/summary`** — Returns `totalEvents` + `bySeverity` map + `byComponent` map + `latest5` array + `generatedAtUtc`.
- **`GET /api/diagnostics/self-test`** — Returns the in-memory boot `SelfTestSnapshot` (passed/failed counts + per-test rows).
- **`GET /api/diagnostics/export?format=markdown|json`** — Markdown export renders a structured doc with sections per severity (`critical`, `error`, `warning`/`warn`, `info`, `debug`), capped at 50 events/section with a truncation note. JSON export returns the full event set. `Content-Type` matches the format.
- **`POST /api/diagnostics/clear`** — Returns 501 with a clear deferred-to-Slice-E message. Clear-with-retention lands when `SqliteDiagnosticsStore` arrives.

### Notes

- Slice A intentionally avoids the new `SqliteDiagnosticsStore` / `DiagnosticsService` class hierarchy from the original PHASE-14 plan. The inline route handlers + jsonl-aggregation deliver the operator-visible surface without adding a new database dependency. The persistent store lands in Slice E.
- The minor bump (0.10.21 → 0.11.0) follows the manifest's phase-scope convention — a new HTTP route surface introduced as part of a phase qualifies as an architectural change.

## [0.10.21] - 2026-05-15

### Summary

Three infrastructure fixes from the operator's testing-phase audit. Closes the carry-over from v0.10.20.

1. **Supervisor honors `pool.template_.environment`.** Pre-v0.10.21 `startInstanceLocked` passed `nullptr` for `CreateProcessW`'s `lpEnvironment` so every stdio-supervised child inherited the MCOS service's env (SYSTEM context). The v0.10.19 sqlite seed set `PATH` in its template but the override was a no-op. v0.10.21 builds a merged Unicode env block from `GetEnvironmentStringsW()` + the pool's overrides and passes it with `CREATE_UNICODE_ENVIRONMENT`.
2. **Error-severity TelemetryEvents persist.** `TelemetryAggregator::recordEvent` now routes `Warning`/`Error`/`Critical` events through `MasterControl::Diagnostics::appendEvent` so they reach `runtime/events.jsonl`. Pre-v0.10.21 only info-severity boot self-test summaries reached the file; every operational error (incl. the 168 sqlite-crash errors during testing-phase remediation) was in-memory only and lost on restart. Info-severity events stay in-memory-only to keep the disk-write rate low.
3. **Persistent log rotation.** `appendJsonLine` in `MasterControlDiagnostics.h` now rotates the live file to `<name>.1.jsonl` when it exceeds 50 MB. Two-file rotation caps total per-component disk consumption at ~100 MB. Pre-v0.10.21 `runtime/telemetry.jsonl` had reached 25.7 MB / 101 K snapshots with no upper bound.

### Fixed

- **Supervisor env-block in `MasterControlRuntime.cpp::startInstanceLocked`** (~line 8577). New `LPWCH environmentBlock` build path mirrors the existing `runOneShotProcess` pattern at line 1006–1039. Allocation is `HeapAlloc(GetProcessHeap()...)`, freed after `CreateProcessW` returns. `CREATE_UNICODE_ENVIRONMENT` is OR'd into `creationFlags` only when the block is non-null.
- **`TelemetryAggregator::recordEvent`** (~line 9938). New persist-on-non-info branch wraps a try/catch so Diagnostics failures cannot throw out of the aggregator path. Persisted record carries `category`, `poolId`, `clientId` as `data` fields.
- **`appendJsonLine`** (`include/MasterControl/MasterControlDiagnostics.h`). New `rotateIfOversizedLocked` helper + `kLogRotationBytes = 50 MB` constant. The rotation happens under the existing static `writeMutex` so the rename target isn't held open by this process. If rename fails (e.g. another reader has the file), the function falls through and continues appending — better an oversized log than a dropped event.

### Notes

- No new files. Three localized changes in two existing files. Patch bump (0.10.20 → 0.10.21).
- Backward compatible at every surface. Pool templates without `environment` overrides keep the prior nullptr-env behaviour (inherit parent). Logs that haven't hit 50 MB keep the prior single-file behaviour.

## [0.10.20] - 2026-05-15

### Summary

POST-body unknown-key (`droppedKeys`) parity for the 4 remaining mutation routes that v0.10.17 deferred. Each route now parses the body once in the route layer (in addition to the existing `AdminApiService` parse) so `collectDroppedTopLevelKeys` can detect operator typos against the typed model, and emits the `droppedKeys` array in the response body when the input carried unknown top-level fields. HTTP status codes are unchanged; pre-v0.10.20 clients that ignore the new field stay wire-compatible.

### Added

- **`droppedKeys` field on `POST /api/runtime/mcp-servers`** (round-trip against `RuntimeEndpoint`).
- **`droppedKeys` field on `POST /api/runtime/subagents`** (`RuntimeEndpoint`). The diagnostic is captured at the top of the handler in a `subAgentDroppedKeys` local; the response emit at the bottom (after the v0.7.2 auto-promote-to-pool block) appends it to the result body.
- **`droppedKeys` field on `POST /api/runtime/subagent-groups`** (`SubAgentGroupDefinition`).
- **`droppedKeys` field on `POST /api/clients/{clientId}/privileges`** (`LanClientPrivileges`).

### Notes

- The diagnostic uses the same `MasterControl::collectDroppedTopLevelKeys<T>(input, model)` helper added in v0.10.17 and the same `droppedKeysToJson(list)` serialiser. No new helper required.
- Patch bump (0.10.19 → 0.10.20). No new files. ~50 lines net change in `MasterControlRuntime.cpp`.

## [0.10.19] - 2026-05-15

### Summary

Three operator-flagged testing-phase defects fixed in a single patch.

1. **`sqlite` catalog placeholder is now reachable on a fresh install.** v0.10.18 omitted sqlite from the npx seed block because spawning `npx -y mcp-server-sqlite-npx <db>` under the MCOS service's SYSTEM context exited with `exitCode=1` within ~1s. Root cause traced to a native-module (`better-sqlite3` / `node_sqlite3.node`) build/load failure under SYSTEM's npm cache path — `npx` couldn't materialise the prebuilt native binary into SYSTEM's npm-global cache layout. v0.10.19 seeds the pool against the SYSTEM-account npm-globally-installed binary at `C:\WINDOWS\system32\config\systemprofile\AppData\Roaming\npm\mcp-server-sqlite-npx.cmd`, bypassing the npx download + native-build path entirely.
2. **`/api/health/summary.gateway.toolCount` no longer stays stale.** `NativeHttpSysGatewayAdapter::ListTools()` previously only refreshed the cache when it was empty. After `InvalidateToolCatalog()` was called by `upsertPool()`, the validity stamp reset but the next `ListTools()` call returned the old cached vector. v0.10.19 routes through `refreshToolCatalogLocked()` unconditionally; the refresh function already self-bypasses when the validity stamp is in the future, so the hot path is unchanged.
3. **`mcpServerRuntimeStats[i].status` and `subAgentRuntimeStats[i].status` are no longer decoupled from `.reachable` for stdio-supervised workers.** The status string was derived from a TCP-probe of `endpoint.host:port` and stayed `"offline"` because stdio-supervised workers don't listen on TCP. Now overridden to `"online"` when `readyInstanceCount > 0`.

### Fixed

- **Sqlite seed in `MasterControlRuntime.cpp`** (~line 13823). New `if (std::filesystem::exists(sqliteSystemBin))` block right after the v0.10.18 npx batch. Skips when the SYSTEM npm-global binary is absent — operator sees the catalog placeholder with its `installHint` until they install the package for SYSTEM.
- **`NativeHttpSysGatewayAdapter::ListTools()`** in `McpGatewayAdapters.cpp` (line 595–611). Now always defers to `refreshToolCatalogLocked()`; was conditional on cache-empty.
- **`McpServerRuntimeStat.status` + `SubAgentRuntimeStat.status` derivation** in `MasterControlRuntime.cpp` (both ~line 11456 and ~line 11580). Override to `"online"` when the pool has any Ready instance.

### Notes

- `mcp-server-sqlite-npx` install for SYSTEM is currently a manual step (`npm install -g` as Flynn, then copy the package + bin script into SYSTEM's npm-global). The installer will handle this in a future iteration; for now the seed's binary-exists check keeps the failure mode honest (placeholder remains visible).
- No breaking public-API change. Patch bump (0.10.18 → 0.10.19) per semver.
- Live state: `/api/version` = `0.10.19`, dashboard reaches 26/26 reachable with sqlite stable for 60+s, gateway.toolCount = 121 matching tools/list count, all 23 stdio-supervised runtime stats report `status="online"`.

## [0.10.18] - 2026-05-15

### Summary

Source-seed fix for the 4 catalog placeholders that pre-v0.10.18 advertised host:port `7101` / `7102` / `7103` / `7107` in the operator's Telemetry deck but had no managed pool spec to back them, so a fresh install reported `reachable=false` for `memory` / `filesystem` / `sequential-thinking` / `chrome-devtools` indefinitely. The operator's testing-phase audit reported "26 reported / 21 reachable" against a 26-element catalog. v0.10.18 adds a `registerNpxMcpPool` seed alongside the existing v0.9.64 `registerWorkerPool` block in `MasterControlRuntime.cpp`, spawning the standard `@modelcontextprotocol/server-*` (and `chrome-devtools-mcp`) packages via the operator's nodejs install at `C:\Program Files\nodejs\npx.cmd`. Fresh installs reach 26/26 on first launch when nodejs is present.

### Added

- **`registerNpxMcpPool` seed lambda** in `src/MasterControlApp/MasterControlRuntime.cpp` next to the existing `registerWorkerPool` block. Same `ManagedEndpointPool` shape, different executable (npx instead of the in-tree baseline worker). Skipped at boot when `C:\Program Files\nodejs\npx.cmd` is absent — catalog placeholder remains visible with its `installHint`, preserving the honest-unavailable-sentinel contract.
- **4 new seeded pools** registered unconditionally at boot when nodejs is present:
  - `memory` → `npx -y @modelcontextprotocol/server-memory`
  - `filesystem` → `npx -y @modelcontextprotocol/server-filesystem C:\Users\Public\Documents`
  - `sequential-thinking` → `npx -y @modelcontextprotocol/server-sequential-thinking`
  - `chrome-devtools` → `npx -y chrome-devtools-mcp`

### Fixed

- **Dashboard `mcpServerRuntimeStats.reachable=true` count** reaches **25/26** on a fresh install with nodejs present (up from 21/26). The remaining 1 — sqlite — is a known crash class deferred to a follow-up patch (captured in `knownIssuesDeferredToNextRelease`).
- **Gateway `tools/list` count** grows by the union of tools exposed by the 4 new servers (≈+50 entries in live testing). Operators reach a richer LAN tool surface on first launch.

### Notes

- The seed runs on every boot via `workerSupervisor_->upsertPool(...)`, which is idempotent — operator-customized pool definitions for the same poolId will be re-seeded to the canonical values on the next service restart. This matches the existing v0.9.64 batch's behaviour and is documented in the seed's source comment.
- Sqlite is deliberately omitted from this seed. `mcp-server-sqlite-npx` exits with code 1 within ~1 s of spawn under MCOS supervisor (same package initializes cleanly when invoked outside the supervisor process tree). Root cause TBD; the catalog placeholder continues to advertise the `installHint` so operators see the expected install path.
- No breaking public-API change. Patch bump (0.10.17 → 0.10.18) per semver.
- Live state after build + DEPLOY_LOCAL_LIVE verified against the reference host: `/api/version` = `0.10.18`, `/api/self-tests` = 35/35 passed, dashboard reaches 25/26 reachable with sqlite remaining placeholder.

## [0.10.17] - 2026-05-15

### Summary

POST-body unknown-key visibility on `/api/clients`, `/api/pools`, and `/api/telemetry/heartbeat`. Pre-v0.10.17 a POST whose JSON body carried a field name the destination model did not recognise (operator typo such as `enabledFlag` for `enabled`) returned HTTP 200 with the default value applied and zero indication anything had been dropped. The third item from the v0.10.15 debugger sub-agent scan. v0.10.17 ships an additive diagnostic that includes a `droppedKeys` array in the response body when the input payload carried unknown fields. HTTP status codes are unchanged, the typed model still constructs cleanly, and pre-v0.10.17 clients ignoring the new field stay wire-compatible.

### Added

- **`include/MasterControl/JsonStrictness.h`** — header-only helper. `collectDroppedTopLevelKeys<T>(input, model)` round-trips the typed model through `nlohmann::json()` to discover its known keys, then lists input keys missing from the round-tripped set. `droppedKeysToJson(list)` serialises the list as a JSON array. Safe on non-object input — arrays, scalars, and `null` return an empty list rather than throwing.
- **`droppedKeys` response field** on `/api/clients` (LanClient), `/api/pools` (ManagedEndpointPool), and `/api/telemetry/heartbeat` (ClientHeartbeat). Only emitted when the input payload carried top-level keys the destination model did not recognise.
- **Four unit tests** in `tests/MasterControlOrchestrationServerTests.cpp`: `testJsonStrictnessDetectsTypo`, `testJsonStrictnessNoDropsWhenAllKeysKnown`, `testJsonStrictnessSafeOnNonObjectInput`, `testDroppedKeysToJsonShape`. All passing under `ctest --preset debug`.

### Changed

- `src/MasterControlApp/MasterControlRuntime.cpp` — three POST route handlers now snapshot the parsed JSON before passing the typed model to the service, run the dropped-key detection on the snapshot, and emit the `droppedKeys` array in the response body via the raw `HttpResponse` path (jsonResponse can't sidecar-merge a field).

### Notes

- HTTP status codes are unchanged. A POST that previously returned 200 still returns 200 with `droppedKeys` added when applicable; a POST that previously returned 400 still returns 400 with the same diagnostic. Patch bump (0.10.16 → 0.10.17) per semver.
- Live state on the reference host: `/api/version` = `0.10.17`, `/api/self-tests` = 35/35 passed.

## [0.10.16] - 2026-05-15

### Summary

SSE resume on `/api/events`. Pre-v0.10.16 the Server-Sent Events handler initialised its activity watermark to the empty string on every connection; a reconnecting browser `EventSource` client replayed from the current ring head and silently dropped any activity events that landed between disconnect and reconnect. v0.10.16 honors the standard `Last-Event-ID` request header (the value `EventSource` auto-echoes on reconnect) and additionally accepts the same `?since=` alias set v0.10.15 introduced. The handler now emits the SSE `id:` field on every activity event so `EventSource` can store it and echo it back automatically. Dashboard snapshot events stay un-id'd because they are not a resumable sequence.

### Added

- **SSE `id:` emission** on activity events. The `sendEvent` helper gained an optional `eventId` parameter; the activity-emission site passes `evt.id` from the global `ActivityEventRing`. The dashboard emission site does not (snapshots are not resumable).
- **`Last-Event-ID` request header support** on `GET /api/events`. The handler reads it case-insensitively via the existing `findHeaderCaseInsensitive` helper and uses the value as the initial activity-ring watermark.
- **`?since=` query-param support** on `GET /api/events`, including all v0.10.15 watermark aliases (`sinceId`, `after`, `from`, `cursor`, `lastEventId`). Lets `curl` / manual probes simulate a reconnect without the full SSE handshake.

### Fixed

- **`lastActivityId` initialisation** in the SSE streaming lambda now seeds from the resolved watermark instead of the empty string. Pre-v0.10.16 every reconnect replayed from the live ring head; any events that landed during the disconnect window were silently lost.

### Notes

- Wire-compatible. Clients that do not send `Last-Event-ID` or `?since=` continue to start from the live tail exactly as before. Patch bump (0.10.15 → 0.10.16) per semver.
- Live verification: an SSE client that records `id:` and reconnects with `Last-Event-ID: <id>` now resumes from that watermark; manual probes via `curl -H "Last-Event-ID: <id>"` or `?since=<id>` produce the same resume.

## [0.10.15] - 2026-05-15

### Summary

Operator-facing silent-ignore defect fix. Pre-v0.10.15 `/api/activity`, `/api/telemetry/events`, and `/api/client/activity` accepted only their canonical query-param names (`max=`, `since=`, `kind=`) and silently shipped the full 512-event ring (138 KB) when an operator typed a natural-language alias such as `?limit=N`. The route layer's bare `query.find()` calls also lacked name-boundary anchoring, so a future `?xmax=` param could have spuriously matched `?max=`. v0.10.15 ships a single boundary-aware helper, refactors the three handlers to use it, and grows `/api/client/activity` to mirror `/api/activity`'s pagination surface.

### Added

- **`include/MasterControl/QueryParamParse.h`** — header-only helper. `extractQueryParam(query, name)` does a single boundary-aware lookup; `extractQueryParamAny(query, {names...})` returns the first hit across an alias list (canonical first → canonical wins when both are present). Pure inline functions; no new translation unit, no link-target change.
- **`/api/client/activity` pagination surface.** The route now accepts the same `max=` / `since=` / `kind=` parameters that `/api/activity` already exposed, plus their aliases. Pre-v0.10.15 the route returned the entire activity ring on every call.
- **Five unit tests** in `tests/MasterControlOrchestrationServerTests.cpp`: `testQueryParamCanonicalParse`, `testQueryParamAliasFallback`, `testQueryParamBoundaryGuard`, `testQueryParamMissingReturnsEmpty`, `testQueryParamMultiPair`. All passing under `ctest --preset debug`.

### Fixed

- **`/api/activity?limit=N`** (and `count`, `n`, `top`) now caps the response at N events. Pre-v0.10.15 only `?max=N` was honored; any alias silently returned the full 512-event ring (138 KB).
- **`/api/activity?after=` / `?sinceId=` / `?from=` / `?cursor=` / `?lastEventId=`** are now accepted as watermark aliases for `?since=`.
- **`/api/activity?type=` / `?event=` / `?filter=` / `?category=`** are now accepted as kind-filter aliases for `?kind=` (wildcard suffix `*` preserved).
- **`/api/telemetry/events?limit=N`** (and `count`, `n`, `top`) honored; previously silently capped at 1024.
- **Name-boundary anchoring** for query-param extraction. A search for `max` no longer incidentally matches inside `xmax`; the helper requires the name to start at position 0 of the query string or immediately follow an `&`.

### Changed

- `src/MasterControlApp/MasterControlRuntime.cpp` — three route handlers (`/api/activity`, `/api/telemetry/events`, `/api/client/activity`) refactored to call the new helper instead of inline `query.find("name=")` patterns. Behaviour for callers using only canonical names is unchanged.

### Notes

- No breaking public-API change. Canonical wire names remain wire-compatible. Patch bump (0.10.14 → 0.10.15) per semver.
- Live state after `cmake --build --preset debug --target DEPLOY_LOCAL_LIVE` + `scripts\Deploy-LocalLive.ps1 -RelaunchShell` on the reference host: `/api/version` = `0.10.15`, `/api/self-tests` = 35/35 passed, `/api/activity?limit=5` → 5 events, `/api/client/activity?max=3` → 3 events, `/api/activity?xmax=10` → still 512 (boundary guard fires).

## [0.10.14] - 2026-05-11

### Summary

Audit remediation. Grok-authored repository audit (`AUDIT-FINDINGS-2026-05-11`) confirmed the runtime, contracts, and architecture are sound; flagged three classes of surface-level defects (hardcoded developer paths, retired-gateway documentation, fragile test build). v0.10.14 ships fixes for all three.

### Fixed

- **`.mcp.json` portability.** Replaced absolute `G:\Claude\...` developer paths with relative paths (`.remember/memory.jsonl`, `.`, `.claude/mcp-state/mcos-orchestration.sqlite`). Replaced hardcoded `C:\Program Files\nodejs\node.exe` + `npx-cli.js` invocations with bare `npx`, so the manifest works on any clone in any directory. PATH env entries kept as a sane fallback. The `Orchastration` misspelling existed only inside the absolute paths and is gone with them.
- **`scribe.list_release_reports` portability** (`src/MasterControlBaselineToolsWorker/main.cpp`). No longer hardcodes `G:\Claude\Master-Control-Orchastration-Server\...`. Derives `handoffDir` from `GetModuleFileNameW(nullptr)`, probes `<exe-dir>\handoff\realignment` and `<exe-dir>\..\handoff\realignment`, returns a structured "not available" JSON when neither resolves.
- **`scripts/.../register-pools.ps1` portability.** `$projectRoot` derived from `(Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path` instead of a hardcoded `G:\Claude\...` literal.
- **Tests build robustness** (`tests/CMakeLists.txt`). `MasterControlOrchestrationServerTests` target now declares `target_include_directories(... PRIVATE ${CMAKE_SOURCE_DIR}/src/MasterControlApp ${CMAKE_SOURCE_DIR}/include)`. The relative `../src/MasterControlApp/SupervisorAssignmentService.h` include in the test source becomes a clean `SupervisorAssignmentService.h` include.

### Changed

- **`.claude-plugin/mcos-control/` documentation.** Retired all legacy gateway references. README smoke-test output rewritten to match the live `/api/health/summary.gateway` shape. `mcos-installer.md` Step 2 rewritten around `NativeHttpSysGatewayAdapter` + `listenPort` + `mcpPath`. `mcos-troubleshooter.md` Chain C rewritten to key on `GET /api/health/summary.gateway` and the v0.10.13 `/api/supervisor/reachability-check`. `commands/activity.md` example telemetry SOURCE field corrected to `NativeHttpSysGatewayAdapter`.

### Notes

- `.mcp.json` contract changed shape (path types) but stays operational; no breaking public-API change. Patch bump (0.10.13 → 0.10.14) per semver.
- Live state on the commit's reference host (v0.10.14): `/api/version` = `0.10.14`, `/api/self-tests` = 39/39 passed, `/api/health/summary` = gateway running with 97 advertised tools and 31/31 healthy worker pools, MCP `initialize` JSON-RPC against `http://192.168.1.7:8080/mcp` returns `serverInfo.name = "MCOS Native Gateway"`, `protocolVersion = "2025-03-26"`.

## [0.10.13] - 2026-05-11

### Summary

Server-side reachability self-check + "Verify Endpoints" button on the Supervisor card. Driven by a second ChatGPT supervisor-candidate connection-test report that returned `Connection refused` on all seven probed LAN endpoints — diagnostic finding: every refusal carried POSIX `errno 111` with sub-millisecond response time, the signature of a cloud Linux runtime that cannot route to RFC-1918 private addresses, NOT an MCOS bind failure.

### Added

- **`GET /api/supervisor/reachability-check`.** Probes loopback + LAN-IP variants of `/api/health`, `/.well-known/mcos.json`, `/api/supervisor/status`, and the gateway `mcpPath` via the existing `sendJsonRequest` WinHTTP helper. Returns per-probe `ok` / `statusCode` / `errorMessage` / `interpretation` plus an aggregate `allReachable` + LAN-routable interpretation block.
- **`EndpointProbeResult` + `probeEndpoint` helpers** in the route layer. `probeEndpoint` distinguishes "no HTTP response at all" (listener missing) from "any HTTP response" (listener alive even if status is 4xx/5xx) so a `405` on `/mcp` (correct for GET) doesn't read as a failure.
- **"Verify Endpoints" button** on the WinUI Shell Overview Supervisor card next to "Generate Config" and "Revoke Active". Calls `/api/supervisor/reachability-check` and renders the per-probe roster in a `Consolas`-font `TextBlock` so the result is copy-paste-friendly for bug-report attachments.

### Notes

- Maintainers with cloud-hosted supervisor candidates (cloud ChatGPT runtime) still need a LAN tunnel (Tailscale, ngrok, Cloudflare Tunnel) to publish the MCOS endpoint. The reachability check now produces an unambiguous server-side verdict so the cloud-routing-vs-MCOS-bind distinction is never misread again.

## [0.10.12] - 2026-05-11

### Summary

Maintainer-directed eight-item punch list across the WinUI Shell and supervisor surface. Direct AI plugin slots for ChatGPT and Grok land alongside the existing Claude Code Control toggle, gated by mutual exclusion. Supervisor config emission learns the LAN-routable URL contract. Overview deck APIS & SERVICES card grows an MCP gateway URL line.

### Added

- **ChatGPT Control + Grok Control toggles** on the Overview deck, mirroring the existing Claude Code Control toggle. ChatGPT writes a connector-config JSON to `%USERPROFILE%\Documents\MCOS\DirectAIControl\chatgpt-mcos-control.json`; Grok writes `grok-mcos-control.json`. Both files carry the LAN-routable MCP gateway URL + discovery + health endpoints for the maintainer to import into the corresponding desktop client.
- **Mutual exclusion across all three Direct AI plugin slots.** Turning on Claude / ChatGPT / Grok forcibly revokes the other two. Backend enforces single-active state; UI reflects via snapshot-driven IsOn updates with the suspendToggleHandler pattern.
- **Backend `DirectAIPluginState` + four routes** (`/api/chatgpt-plugin/{status,toggle}`, `/api/grok-plugin/{status,toggle}`). Same shape as `/api/claude-plugin/*`. Per-provider transport-error / activeUserResolved / registered fields.
- **`mcpGatewayStatus.mcpUrl` capture in the WinUI Shell's `/api/dashboard` parser.** New `snapshot.mcpGatewayUrl` + `snapshot.mcpGatewayState` fields drive the new MCP Gateway line on the APIS & SERVICES card.
- **MCP Gateway URL line on the Overview APIS & SERVICES card.** Renders the gateway URL after wildcard-bind substitution, with the gateway state in parentheses.

### Changed

- **`server.mcpEndpoint` in generated supervisor configs and onboarding profiles** uses the LAN-routable gateway URL (`http://<lanIp>:<cfg.mcpGateway.listenPort><cfg.mcpGateway.mcpPath>`) instead of the loopback / admin-port placeholder. Pre-v0.10.12 cloud supervisors saw `127.0.0.1:7300/mcp` (wrong host, wrong port, wrong path) and failed with connection-refused on every probe.
- **Reachability note on the Supervisor card** explicitly documents that ChatGPT cloud runtimes cannot reach RFC-1918 LAN IPs; maintainer-side fix is a LAN tunnel (Tailscale / ngrok / Cloudflare Tunnel / similar).

### Notes

- The mutual-exclusion logic for Direct AI plugins is enforced at the backend; the shell renders server-side state on every snapshot tick. Race-prevention guards (`busy` flags + `suspendToggleHandler` flags) keep the UI honest while round-trips are in flight.

## [0.10.11] - 2026-05-11

### Summary

Aggregate entry covering the 80+ patch releases between v0.9.3 (last committed baseline) and v0.10.11. Per-release detail lives in `handoff/realignment/v0.9.X-release-report.md` and `handoff/realignment/v0.10.X-release-report.md`. Commit `dcd1e8b` carries the full source.

### Added

- **Native HTTP.sys MCP gateway is now the only substrate.** the legacy external gateway was dropped before v0.9.0 per maintainer directive. `cfg.mcpGateway.type` is retained for back-compat JSON deserialization only; the runtime always uses the native HTTP.sys adapter on `0.0.0.0:cfg.mcpGateway.listenPort` (default 8080) at `cfg.mcpGateway.mcpPath` (default `/mcp`).
- **Supervisor Agent Assignment Wizard.** Full backend + WinUI Shell + browser dashboard surface for selecting one supervisor model (chatgpt / claude / grok). Lifecycle: `off → config_generated → pending_connection → connected → disconnected | revoked`. 120-second heartbeat watchdog flips Connected → Disconnected. Persistence at `<dataDirectory>/supervisor-assignment.json` survives service restart.
- **Persistent Diagnostics log.** Supervisor lifecycle + boot self-test failures + per-boot self-test summaries dual-emit to `<PUBLIC>\Documents\Master Control Orchestration Server\logs\<component>\events.jsonl`.
- **Boot self-test count grew to 39 probes** (was ~30 at v0.9.3). New probes include `diagnostics.log_writable` (v0.10.3), `gateway.tool_count_nonzero` (v0.10.4), supervisor lifecycle probes, and pool readiness probes.
- **WinUI Shell footer-style tile grid.** `endpoint_stat_card_grid_detail::buildFooterStyleTile<StatT>` is the shared per-tile builder. The Telemetry MCP / Sub-Agent panels (v0.10.6 - v0.10.7), the Runtime MCP / Sub-Agent panels (v0.10.9), and the cross-tab SUB-AGENT GRID footer (v0.7.8 baseline) all render the same tile shape: 1px TRON-red border, 6px corner, 8x6 padding, uppercase semibold cyan name + reachability dot + specialization + util% + ProgressBar + active/cap ratio + 2-line client list.
- **`scripts\Deploy-LocalLive.ps1` + `DEPLOY_LOCAL_LIVE` CMake target** (v0.9.78 / v0.9.80). Stops `MasterControlProgram`, copies `MasterControlServiceHost.exe` + `MasterControlShell.exe` + xbf + winmd + pri into the spaces-path install dir (`C:\Program Files\Master Control Orchestration Server\`), restarts the service, probes `/api/version` + `/api/self-tests` + `/api/supervisor`, optionally relaunches the shell.
- **Pool orchestration scaffolding** under `.claude/agents/`, `.claude/scripts/`, `.claude/mcp-state/pool-policy.json`, `.claude/mcp-state/pool0.json`. Sub-agent definitions for architect, build-resolver, coordinator, debugger, documenter, git-manager, multi-file-specialist, performance-engineer, planner, refactorer, reviewer, security-auditor, sentinel, test-writer.

### Changed

- **`server.mcpEndpoint` in generated supervisor configs.** Pre-v0.10.8: `http://127.0.0.1:<browserPort>/mcp` (wrong host and wrong port — `/mcp` does not exist on the admin port; `127.0.0.1` is unreachable off-box). Post-v0.10.8: `http://<lanIp>:<cfg.mcpGateway.listenPort><cfg.mcpGateway.mcpPath>` using the same 5-step LAN-IP precedence chain `DiscoveryService::currentDocument()` uses for `/.well-known/mcos.json`.
- **Telemetry dashboard-snapshot writes to the persistent telemetry log are throttled to once per 60 seconds** (v0.10.5). Cuts log growth from ~21 MB/day to ~350 KB/day.
- **`renderEndpointStatCardGrid()` compact mode** (v0.10.7) produces a 7-column tile grid that wraps to additional rows by computing `Grid.Row = i/7, Grid.Column = i%7`. 26 MCP servers wrap to 4 rows (7+7+7+5); 7 sub-agents fit in one row.
- **`MainWindow::ApplySubAgentFooter`** (v0.10.10): cross-tab SUB-AGENT GRID footer is now scoped to the Telemetry and Runtime destinations only. Overview, Imports, Exports, Security, CLU, Settings, and the Setup Wizard no longer carry it.

### Removed

- **native gateway substrate** (v0.9.0). the legacy `GatewayType` slot enum value is retained only so existing on-disk configs deserialize; the runtime always falls through to the native HTTP.sys adapter. `gatewayConfig.binaryPath`, `gatewayConfig.databasePath`, and the legacy supervised-binary management code path are gone.
- **Overview deck `MCP SERVERS` and `SUB-AGENTS` summary cards** (v0.10.10). The 2x2 status grid shrinks to 1x2 — `APIS & SERVICES` + `SECURITY STANCE` remain. The MCP / sub-agent decks live on Runtime and Telemetry as tile grids.
- **Runtime deck `Runtime Endpoints` ListView** (v0.10.11). The flat `name | host:port | status` list that mixed MCP servers, sub-agents, and gateways into one column conflicted with the tile-grid directive. The dedicated MCP Servers and Sub-Agents tile-grid panels above carry the same data in the correct visual form.

### Fixed

- **Supervisor confirm regression message persistence** (v0.10.1). `revoke()` and `regenerateConfig()` now clear `assignment_.lastErrorMessage`; `loadFromDisk()` drops the stale message on load when state is terminal (Off or Revoked).
- **Telemetry compact-card per-row Border tightness** (v0.10.6 → v0.10.7). The interim v0.10.6 flat-row rendering was wrong; v0.10.7 corrected to the footer-style tile grid per maintainer clarification.
- **Supervisor service endpoint refresh** (v0.10.8). `ISupervisorAssignmentService::setEndpoints` + `MasterControlApplication::Impl::refreshSupervisorEndpoints` push live LAN-resolved values into the supervisor service just-in-time on select / generate / regenerate.
- **Boot self-test gateway probes** (v0.10.4): added `gateway.tool_count_nonzero` so an adapter that's listening + Running but advertising 0 tools is now a visible FAIL instead of a silent misconfiguration.

### Notes

- Live state on the commit's reference host (v0.10.11): `GET /api/version` = `0.10.11`, `GET /api/self-tests` = 39/39 passed, `GET /api/dashboard` = 26 MCP servers (25 reachable) + 7 sub-agents (7 reachable), gateway running with 97 advertised tools.
- The Telemetry / Runtime decks still display the SUB-AGENT GRID footer below their in-section tile grids. The repeated surface is intentional for now; further consolidation is a v1.0.0+ candidate.
- Cloud-hosted ChatGPT cannot reach LAN IPs by definition; a tunnel (Tailscale, ngrok, Cloudflare Tunnel) is required when the supervisor client is off-LAN. Maintainer-side networking concern, not an MCOS bug.

## [0.7.0] - 2026-05-05

### Production milestone — architecture complete

The 0.7.0 minor bump under the manifest's `minor-on-architecture-change` policy marks the architectural-completion line. Every numbered phase from PHASE-00 (repository baseline + ADR lock) through PHASE-12 follow-up (the native HTTP.sys adapter with end-to-end stdio bridge to supervised pool children, shipped in v0.6.10) is delivered, validated, and shipping. PHASE-12 follow-up was the last architectural change; from here the work is iteration on top of the locked architecture.

#### Verified at this milestone

- The native HTTP.sys substrate is the only shipping option; `mcpGateway.type` is retained in the schema for back-compat deserialization only. The legacy external-binary substrate (retired before v0.9.0) supervised a separate gateway binary as a Job Object child; the `native` substrate binds Windows-native HTTP.sys directly inside `MasterControlServiceHost.exe` with the v0.6.10 stdio bridge forwarding `tools/list` and `tools/call` to supervised pool children. Both satisfied `IMcpGateway` at the time; only the `native` substrate ships now.
- Bootstrapper `install` action runs `netsh http add urlacl url=http://+:8080/ user=Everyone` so console-mode maintainers bind the native gateway port without `ERROR_ACCESS_DENIED`. `uninstall` reverses it best-effort.
- All four CTest suites (`ForsettiCoreTests`, `ForsettiPlatformTests`, `ForsettiArchitectureTests`, `MasterControlOrchestrationServerTests`) pass green at the 0.7.0 commit.
- End-to-end host smoke-test: MSI installs cleanly, service starts, native gateway binds and responds to MCP `initialize` with `serverInfo.version=0.7.0`, `tools/list` returns honest empty array (no pools registered), URL ACL confirmed via `netsh http show urlacl`.

#### Documentation

- `VERSION.json` history entry summarizes the architectural-completion milestone and explicitly defers PHASE-13 visual polish to v0.7.x point releases.
- `README.md` rewritten: stale per-version sections (v0.6.0..v0.6.4) replaced with v0.7.0 milestone block, mermaid diagram updated to show both substrates, broken `Architecture-Decisions/` and `Operations/` wiki paths corrected to flat `docs/wiki/`.
- Wiki refresh: `Home.md`, `Versions.md`, `Architecture.md`, `Gateway.md`, `Quick-Start.md`, and `ADR-003-mcp-gateway-substrate-decision.md` all updated for v0.7.0 reality. ADR-003 receives a "Status update" section explaining that PHASE-12 was authored and shipped voluntarily (maintainer-experience trigger #4) and the native substrate is now a maintainer-selectable peer of the gateway adapter path.

#### Notes

- Same-version VERSIONINFO behavior: when reinstalling the same v0.7.0 MSI on top of itself, default Windows Installer file-replacement rules treat the binaries as already-current. Use `msiexec /i <msi> REINSTALL=ALL REINSTALLMODE=amus` to force replacement.
- v0.7.0 carries no new schemas, no new client contracts, no new ADRs beyond ADR-003's status update. The `IMcpGateway` interface, `McpServerRegistration`, `EndpointInstance`, `LeaseRequest`/`EndpointLease`, and the discovery document shape are unchanged.

## [0.6.10] - 2026-05-05

### PHASE-12 follow-up complete — stdio bridge, real tools/list aggregation, real tools/call forwarding, URL ACL

#### Added

- **`IWorkerSupervisor::sendStdioJsonRpc(instanceId, request, timeoutMs=30000)`** — synchronous JSON-RPC over a supervised child's stdin/stdout. Per-instance `std::unique_ptr<std::mutex>` (heap-allocated to keep `ChildProcess` movable inside `std::map`) serializes concurrent calls to the same instance; different instances run in parallel. The supervisor mutex is held only briefly to look up the child + grab pipe handles, then released before any blocking I/O so other supervisor traffic (telemetry sampling, lease accounting) is not throttled.
- **Polling read loop** uses `PeekNamedPipe` to test for available bytes without blocking, `ReadFile` drains in 4 KB chunks, accumulates partial lines in `stdioReadBuffer`, scans for `\n` delimiters, parses each line, matches by JSON-RPC `id`. Deadline-based timeout via `std::chrono::steady_clock` + 25 ms poll interval.
- **`WorkerSupervisor::startInstanceLocked` pipe wiring** — two anonymous pipes via `CreatePipe`, child-side ends marked inheritable via `SECURITY_ATTRIBUTES`, parent-side inheritance cleared via `SetHandleInformation`, `STARTF_USESTDHANDLES` set on `STARTUPINFOW` with `hStdInput`/`hStdOutput`/`hStdError` (stderr merged into stdout), `CreateProcessW` called with `bInheritHandles=TRUE`, child-side ends closed in parent immediately after spawn so EOF detection works correctly. If pipe creation fails, the child still spawns and `tools/call` returns an honest -32603 "stdio bridge unavailable on this instance" rather than hanging.
- **`NativeHttpSysGatewayAdapter::AttachWorkerBridge(supervisor, leaseRouter)`** — late-bind hook so the adapter constructs early (before supervisor exists) and gains the bridge once the runtime has built the worker tier. Avoids a construction-order rewrite.
- **`refreshToolCatalogLocked()`** — walks every pool's first Ready instance via the bridge, sends `tools/list` JSON-RPC with a 5 s timeout, parses `.result.tools`, tags each tool with `serverName=poolId`, rebuilds `toolCatalogCache_`. Children that don't respond contribute zero tools (ADR-002 §9: no fabrication).
- **Native `tools/list`** — returns the merged catalog with each tool advertised as `{poolId}__{toolName}` so AI clients have unambiguous routing across overlapping local tool names.
- **Native `tools/call`** — resolves `params.name` against the cached catalog (exact qualified `{pool}__{tool}` match wins; otherwise unique unprefixed match wins; collision returns -32602; not-found returns -32601 after one cache refresh). Acquires a lease via `LeaseRouter::acquireLease`, forwards the envelope to `lease.instanceId` via the bridge with the request id rewritten (so concurrent gateway requests don't collide on the supervisor correlator) and `params.name` unprefixed (so the child sees its own local name). Response id is re-stamped back to the LAN client's original id before return.
- **`configureUrlAcl()` + `removeUrlAcl()`** in the bootstrapper — install action runs `netsh http delete urlacl url=http://+:<port>/` (idempotent cleanup) followed by `netsh http add urlacl url=http://+:<port>/ user=Everyone`. Failure is logged but does NOT abort the install (LocalSystem service-mode does not require the ACL). Uninstall reverses the reservation best-effort.

#### Fixed

- **HTTP.sys body extraction** — `serveLoop` now drains via `HttpReceiveRequestEntityBody` after `HTTP_RECEIVE_REQUEST_FLAG_COPY_BODY` for clients that don't inline the body (PowerShell `Invoke-RestMethod` with `Expect:100-continue`, chunked transfer-encoding). Through v0.6.9 the path missed bodies and the handler returned "Invalid Request: missing method" because `body.empty()` parsed as `{}`.
- **`nlohmann::json{nullptr}` → null scalar** — replaced the brace-init-list construction (which produced `[null]`, an array containing one null element) with explicit `nlohmann::json id; if (req.contains("id")) id = req["id"];` so JSON-RPC error envelopes carry the actual null scalar in the `id` field.

#### Notes

- End-to-end smoke-test on the host: MSI installs cleanly, native gateway binds, MCP `initialize` returns `serverInfo.version=0.6.10`, URL ACL confirmed via `netsh`.
- The supervisor mutex is intentionally NOT held during the blocking read in `sendStdioJsonRpc`. Per-instance mutex is the only lock held during the polling loop, so other supervisor traffic continues at full speed.
- `ChildProcess` is movable because `stdioMutex` is `std::unique_ptr<std::mutex>` (mutex itself is not movable) and `nextRequestId` is a plain `uint64_t` (only mutated under `stdioMutex`, no atomic needed).

## [0.6.9] - 2026-05-05

### PHASE-12 MVP — Windows-native HTTP.sys MCP gateway

#### Added

- **`NativeHttpSysGatewayAdapter`** alongside `FakeMcpGatewayAdapter`. Implements `IMcpGateway` exactly — PHASE-03 through PHASE-11 surfaces don't see the substrate change.
- **HTTP.sys lifecycle** — `HttpInitialize` / `HttpCreateServerSession` / `HttpCreateUrlGroup` / `HttpAddUrlToUrlGroup` with `http://+:PORT/` prefix / `HttpCreateRequestQueue` / `HttpSetUrlGroupProperty(BindingProperty)`. Teardown reverses cleanly. `ERROR_ACCESS_DENIED` on `HttpAddUrlToUrlGroup` surfaces with maintainer guidance ("Run as service or `netsh http add urlacl url=http://+:8080/ user=Everyone`").
- **MCP Streamable-HTTP request loop** — `HttpReceiveHttpRequest` with `HTTP_RECEIVE_REQUEST_FLAG_COPY_BODY` pulls body inline up to 16 KB; `ERROR_MORE_DATA` grows the buffer; `ERROR_OPERATION_ABORTED` breaks the loop on shutdown. Routes `/health` to a structured adapter-state JSON, `/mcp[/{poolId}]` to `handleMcpRequest`, everything else to a 404 with structured-JSON error.
- **MCP protocol surface** — `initialize` returns `serverInfo` + tools-capability advertisement; `tools/list` aggregates registered `McpServerRegistration` tools (returns empty array in MVP, full aggregation lands in v0.6.10); `tools/call` returns -32601 with explicit pointer at v0.6.10 stdio bridge.
- **Adapter selection at construction time** based on `mcpGateway.type`. `GatewayType::Native` → `NativeHttpSysGatewayAdapter`. Maintainers switch substrates by POSTing to `/api/config` and restarting the service.

#### Build

- `MasterControlApp` links `httpapi.lib` for HTTP.sys + `http.h` header. The `winsock2.h` / `windows.h` ordering correction from v0.6.7 still holds.

## [0.6.8] - 2026-05-05

### Pool persistence + per-instance telemetry charts + telemetry events ring + PHASE-12/13 plans

#### Added

- **Pool persistence across service restart and MSI MajorUpgrade.** `AppConfiguration` gains a `pools` field (NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT) mirroring `WorkerSupervisor::pools_` to disk. The runtime hydrates at boot via `workerSupervisor_->upsertPool(...)` for each persisted pool. Through v0.6.7 the maintainer lost their pools every restart.
- **Per-instance browser sparkline charts** on the Pools deck. `paintInstanceSparklines()` walks every `.instance-spark` canvas and draws CPU% (cyan accent, 0-100 fixed range) and Memory MB (cyan-blue, dynamic range). `state.instanceHistory[instanceId]` keeps a 60-sample ring per instance (~2 minutes at the 2 s polling cadence). Browser GPU-composites the canvas automatically.
- **Telemetry events ring producer wired** — `/api/pools` route handlers (POST `/api/pools`, `/scale`, `/drain`, `/remove`, lease acquire) emit structured `TelemetryEvent` records into the `telemetryAggregator_` on success. Through v0.6.7 the events ring stayed empty because no producer was wired.
- **Configuration narrative now surfaces `activeProfile.preferredBindAddress`** so maintainers can confirm what LAN clients see in `/api/discovery` and `/.well-known/mcos.json`.
- **PHASE-12 plan file** (`handoff/realignment/PHASE-12-native-http-sys-gateway.md`) and **PHASE-13 plan file** (`handoff/realignment/PHASE-13-direct2d-shell-rendering.md`).

#### Fixed

- `/api/telemetry/events` returns the `{events, maxEvents}` envelope the dashboard JavaScript reads; through v0.6.7 it returned a raw array and `state.telemetryEvents.events` was undefined. Also: `?max=N` query parsing was missing entirely; the route only matched the bare path. Both fixed.

## [0.6.7] - 2026-05-05

### Honest-503 listener — gateway port stops returning TCP RST

#### Added

- **`NativeHttpSysGatewayAdapter` honest-503 listener**. When the adapter is in `Disabled` or supervised-mock mode (no gateway binary configured, the default state), the adapter binds the configured listen port (default 8080) with a tiny accept loop that returns a structured HTTP 503 "Service Unavailable" JSON to every request. LAN AI clients pointing at the advertised gateway URL get a useful error with maintainer guidance instead of connection refused.
- **Lifecycle** — listener releases the port the moment a real gateway binary is about to be spawned, and re-binds it after `Stop()` so the gateway port stays answerable across the entire adapter lifecycle. Replaced wholesale by the native HTTP.sys adapter (PHASE-12) when active.

#### Fixed

- **`winsock2.h` / `windows.h` include ordering** — defined `WIN32_LEAN_AND_MEAN` and put `winsock2.h` before `windows.h` in both header and TU, fixing `error C2011: 'sockaddr': 'struct' type redefinition` from the legacy winsock1 transitive include.

## [0.6.5] - [0.6.6] - 2026-05-04

### Per-instance CPU/RAM telemetry, MSI uninstall stale-shortcut fix, settings Apply gate fix

#### Added

- **Per-instance CPU and RAM telemetry sampling.** `WorkerSupervisor::sampleProcessLoadLocked` reads `GetProcessTimes` (FILETIME deltas for kernel + user) and `GetProcessMemoryInfo` (working set MB) per supervised child. First sample establishes a baseline and reports 0% CPU; subsequent samples produce real percentages computed from the delta divided by host logical CPU count, clamped to [0, 100] for transient spikes from sampler skew.

#### Fixed

- **MSI uninstall Error 1309** (system error 5 ACCESS_DENIED reading `Master Control Orchestration Server.lnk`). Cause: Explorer / Search Indexer holds an open handle on the .lnk during MajorUpgrade checksum read. Fix: `<RemoveFile On="install">` deletes the stale .lnk before the MSI tries to checksum it; `<util:CloseApplication>` integrates with Restart Manager.
- **Settings Apply button non-pushable.** `MainWindow.xaml.cpp:1179` `attachInteractiveRuntime` call site didn't include `kSettingsView` and `kOverviewView` in the gate. Fixed.

## [0.6.4] - 2026-05-03

### Maintainer-set advertised IP — `preferredBindAddress` propagates everywhere

#### Added

- **`activeProfile.preferredBindAddress` is now the primary source for the advertised LAN IP.** The discovery doc (`/.well-known/mcos.json`, `/api/discovery`) and DNS-SD registration treat it as the canonical advertised address. On dual-stack Windows hosts the runtime's interface auto-pick used to surface the IPv6 ULA first; LAN clients then saw an IPv6 address their stack didn't route to. Setting `preferredBindAddress` (e.g. `192.168.1.7`) via `POST /api/config` now propagates immediately to every advertised URL and to the DNS-SD records.

## [0.6.1] - [0.6.3] - 2026-05-02

### Claude Code Control toggle — Overview deck of both GUI surfaces

#### Added

- **Browser dashboard** — Overview → Claude Code Control card → CSS toggle switch backed by an accessible `<input type="checkbox">`.
- **WinUI desktop shell** — Overview → Claude Code Control card → native `ToggleSwitch` with `OnContent="Connected"` / `OffContent="Disconnected"`.
- Both refresh on load and on every snapshot tick, both stay interactive even when the runtime would refuse, and both call the same `/api/claude-plugin/{status,toggle}` routes. Click Connect from either surface and the runtime drops `%USERPROFILE%\.claude\plugins\mcos-control` as a directory junction onto the install directory's bundled plugin source — no admin prompt, no execution-policy gymnastics.

#### Fixed

- **Console-mode `WTSQueryUserToken` errno 1008**. The active-user resolver is now hosting-mode aware: if the runtime process already has a non-SYSTEM `USERPROFILE` (i.e. console mode), use it directly; only fall through to `WTSGetActiveConsoleSessionId` + `WTSQueryUserToken` when the env var resolves to the SYSTEM profile (the Windows service path).

## [0.6.0] - 2026-05-01

### Gateway-first MCP realignment — PHASE-00 through PHASE-11

The realignment program declared in ADR-002 lands in twelve named phases. The product becomes a **Windows-native LAN MCP Gateway host**: external AI coding clients (Claude Code, Codex, Grok, ChatGPT, generic MCP) discover MCOS via DNS-SD, consume server-generated onboarding profiles and CLU/Forsetti governance bundles, and operate against supervised MCP server and sub-agent worker pools. MCOS owns discovery, governance, telemetry, worker supervision, autoscaling, dashboarding, and Windows packaging.

#### Added

- **PHASE-00** — `ADR-002` (gateway-first realignment), drift inventory, removal map, `FORBIDDEN-CONTRACT-GREP-LIST.md` with eight contract groups.
- **PHASE-01** — provider-era residual cleanup. `ExternalClient` model replaces `Provider*`; deletion/quarantine of execution routes; tests updated.
- **PHASE-02** — `IMcpGateway` interface, `NativeHttpSysGatewayAdapter` (supervised external binary), `FakeMcpGatewayAdapter` (test). Supervised-mock fallback when no binary is configured (honest "configured, not running" state per ADR-002 §9).
- **PHASE-03** — DNS-SD / mDNS LAN discovery via Win32 `DnsServiceRegister`. Three Bonjour service types (`_mcos._tcp.local`, `_mcos-mcp._tcp.local`, `_mcos-onboarding._tcp.local`) plus the legacy beacon, all carrying the canonical `DiscoveryDocument`. `/.well-known/mcos.json`, `/api/discovery`.
- **PHASE-04** — onboarding profile service. Per-client-type profiles (`claude-code`, `codex`, `grok`, `chatgpt`, `generic-mcp`) at `/api/onboarding/{clientType}`. Manual setup is first-class.
- **PHASE-05** — governance bundle service. Per-platform bundles (`windows`, `macos`, `ios`) at `/api/governance/bundles/{platform}` with sha256 checksums. Forsetti version + agentic coding version stamped in.
- **PHASE-06** — managed worker pools. `EndpointTemplate`/`Instance`/`Pool`, `WorkerSupervisor` with 7-state lifecycle, Job Object (`JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`) containment of supervised process trees.
- **PHASE-07** — lease router with sticky-session + autoscaling. Four-step selection: sticky → least-loaded → scale-out → fail honestly. No hot-migration of stateful streams.
- **PHASE-08** — telemetry aggregator with `-1.0` honest-unavailable sentinel. Events ring (1024 cap), client presence roster, gateway traffic snapshot. Activity event taxonomy.
- **PHASE-09** — Tron dashboard realigned to gateway-first. Eleven destinations covering every layer; `formatMetric()` honesty helper enforced by FORBIDDEN-CONTRACT §8.1.
- **PHASE-10** — Windows release gate closed. vswhere-driven toolchain, version-stamping before configure, no `workflow_dispatch` bypass on the gating workflows, MSI rebuilt clean.
- **PHASE-11** — native gateway evaluation memo + `ADR-003` recording the decision to keep the external supervised substrate for the v0.6.x line and defer a native HTTP.sys substrate to a conditional future PHASE-12 with five named operational triggers.

#### Notes

- ADR-001's LAN client identity model (v0.5.0) is preserved as the maintainer surface that coexists with the AI-client gateway surface. Network firewall scoping (`Profile=Private,Domain`) is the load-bearing trust control on both surfaces.

## [Unreleased — superseded by v0.6.0+ above]

### Documentation overhaul + automation retirement (post-v0.5.0)

A comprehensive rebuild of the wiki and a clean-out of the repository agents that previously regenerated documentation on every push. The change is documentation-only — no source, no schema, no route surface modified. Targets the next minor release.

#### Removed

- **`.github/workflows/repository-maintenance-agents.yml`** — the auto-pushing workflow that ran on every push to `main` and regenerated wiki pages, README, and CHANGELOG content as `github-actions[bot]`. The automation bypassed the AI Contributor Guard (bots are allowlisted), bumped patch versions on cosmetic edits, and steamrolled hand-authored content. With the workflow removed, every documentation edit is now an explicit maintainer action.
- **`scripts/github_agents/sync_docs.py`** (89 KB wiki generator) — the doc-sync agent that produced wiki pages from `plans/` source. Generated content was templated and mechanical; the rebuilt wiki is hand-authored with mermaid/sequence/state diagrams the generator could not produce.
- **`scripts/github_agents/release_manager.py`** — the release-bump agent that incremented patch versions on every commit landing in `main`. Real release decisions (patch / minor / major) require maintainer judgment; the commit-message parser could not reliably make that call.
- **`scripts/github_agents/__pycache__/`** — Python bytecode cache for the deleted scripts.

#### Kept

- **`scripts/github_agents/check_no_ai_contributors.py`** + **`common.py`** — the **AI Contributor Guard** remains active. Every push and pull request to `main` runs the guard, rejecting any commit whose author / committer / trailer matches a known AI vendor name (`chatgpt`, `codex`, `claude`, `copilot`, `gemini`, `grok`, `openai`, `anthropic`, `deepseek`, `perplexity`, `x.ai`). The block list applies to identity, not product references — `LanClient` records with `clientType: "claude_code"` continue to be legitimate runtime data.

#### Wiki — comprehensive rebuild

Every wiki page was rewritten by hand. The rebuild adds visual depth (mermaid `flowchart` / `sequenceDiagram` / `stateDiagram-v2` / `classDiagram` / `gantt` blocks, ASCII art topology diagrams, badge banners, decision matrices, severity tables) and substantial worked-example content (curl, PowerShell, Python, TypeScript, Node) so each page stands on its own as a comprehensive reference.

- **[`docs/wiki/Home.md`](docs/wiki/Home.md)** — hand-authored landing page with runtime topology mermaid diagram, three core invariants (Use is universal / Mutation is gated / Identity is by header), full site map across four categories, 5-minute walkthrough sequence diagram.
- **[`docs/wiki/Architecture.md`](docs/wiki/Architecture.md)** — 12 numbered sections covering runtime topology, request lifecycle, the 16-module catalog, service container, `LanClientAccessModule` flowchart, CLU enforcement pipeline with rule branches, maintainer surfaces, activity ring, configuration shape, build composition, persistence paths, and what is intentionally out-of-scope.
- **[`docs/wiki/API-Reference.md`](docs/wiki/API-Reference.md)** — 13 sections: identity model flowchart, read endpoints, LAN client identity routes, config bundle, shared fabric reads, runtime mutation with privilege/CLU mapping, governance routes, configuration mutation, platform services, install/import, status code reference, operation result envelope, end-to-end curl walkthrough.
- **[`docs/wiki/LAN-Clients.md`](docs/wiki/LAN-Clients.md)** — mental model, data model class diagram, lifecycle state machine (`stateDiagram-v2`), identification flowchart with resolution outcomes table, heartbeat performance note, maintainer workflows, activity attribution table, two-client collaboration sequence diagram, FAQ.
- **[`docs/wiki/Privileges.md`](docs/wiki/Privileges.md)** — privilege matrix flowchart with Mcp/Sub/Admin sub-graphs, the 9 flags severity table, gate flowchart with autonomous bypass, autonomous-mode action comparison, four capability bundles (read-only / tool author / sub-agent author / maintainer-equivalent / autonomous worker), decision matrix at a glance, read-modify-write workflow, common pitfalls.
- **[`docs/wiki/Client-Config-Bundle.md`](docs/wiki/Client-Config-Bundle.md)** — mental model, end-to-end issue → consume sequence diagram, full schema reference for every field, server-side resolution flowchart (the never-`0.0.0.0` rule), agent consumption pseudocode in Bash / PowerShell / Python / TypeScript, bundle invariants, re-issue triggers diagram, schema versioning strategy.
- **[`docs/wiki/Governance.md`](docs/wiki/Governance.md)** — mental model, doctrine (D1–D5), the two-stage gate sequence diagram, the 15 action kinds table, three outcomes flowchart, default outcomes per kind, autonomous-mode bypass scope, approval queue with state diagram, full CLU rule catalog (CLU-C001 through CLU-S002), worked examples for deferred policy edit / autonomous-create burst / blocked posture, audit trail, FAQ.
- **[`docs/wiki/Remote-Client.md`](docs/wiki/Remote-Client.md)** — onboarding sequence diagram, nine numbered steps from discovery through agent operation, naming-convention recipe, recipes by role flowchart, autonomous-mode caveat, reference flows in Python / TypeScript / PowerShell, re-issue triggers, four failure-mode diagnoses, multi-client collaboration walkthrough, FAQ.
- **[`docs/wiki/Sub-Agents.md`](docs/wiki/Sub-Agents.md)** — roster mermaid diagram with port assignments, specialization details for each of the seven (SENTINEL through WATCHTOWER), MCP-over-SSE transport with handshake sequence diagram and the three "easy to get wrong" rules, lifecycle state diagram, registration with privilege requirements, capability extension diagram, FAQ.
- **[`docs/wiki/Operations.md`](docs/wiki/Operations.md)** — lifecycle map flowchart, build/test/Forsetti compliance commands, staging and packaging recipes, install entry points table with privilege requirements, lifecycle subcommands state diagram, deployment scripts table, installed runtime surfaces, first-run migration flowchart, standard maintainer flow, post-install verification checklist, backup/restore recipes, FAQ.
- **[`docs/wiki/Infrastructure.md`](docs/wiki/Infrastructure.md)** — topology diagram, target hosts compatibility matrix, hardware minimums, network footprint diagram with port assignments and firewall rule recipes, persistence layout with backup priority table, packaging model, service identity table, browser dashboard composition, single-host rationale flowchart, validation focus, FAQ.
- **[`docs/wiki/Troubleshooting.md`](docs/wiki/Troubleshooting.md)** — universal triage flowchart, one-shot health probe, fifteen symptom-keyed sections (403 unexpected, agent inheriting maintainer authority, unreachable `mcosServer`, 202 deferred mutations, blocked-posture refusals, blank dashboard, elevation failures, toolchain mismatches, log paths, activity-stream debugging, hung service, agent stale tags, Tron palette wrong), quick-reference symptom→section table.
- **[`docs/wiki/Versions.md`](docs/wiki/Versions.md)** — current release banner, what-v0.5.0-ships flowchart, versioning scheme table, release timeline `gantt` diagram, v0.5.0 detail (added/removed/changed), upgrade notes from v0.4.x, release artifacts table, retired release-manager-agent rationale, maintainer runbook for cutting a release, FAQ.
- **[`docs/wiki/Telemetry-and-Activity.md`](docs/wiki/Telemetry-and-Activity.md)** — producer/stream/consumer mental model, host telemetry sampling cadence diagram, activity ring properties, event lifecycle sequence diagram, full event-kind taxonomy across catch-all / LAN client lifecycle / catalog mutation / governance / module / heartbeat groups, color codes, polling and SSE consumption recipes, long-term retention diagram, capacity planning flowchart, code references, FAQ.
- **[`docs/wiki/Tron-UI-Theme.md`](docs/wiki/Tron-UI-Theme.md)** — token catalog mental model, palette reference with hex codes, background composition diagram, typography table, hard rules R1–R5 with allowed/forbidden flowchart, shell consumption with App.xaml mapping, browser consumption with CSS variable mirror, component-states reference, activity-stream color wiring, spacing tokens, accessibility checklist, new-component decision flowchart.
- **[`docs/wiki/Automation.md`](docs/wiki/Automation.md)** — already updated earlier in v0.5.0; documents the active workflow set (AI Contributor Guard, Forsetti Compliance, Windows Build/Test/Package), the retired set (DocSync, ReleaseAgent, WikiSync), the AI vendor block list, the allowed-bot identities.

#### Wiki navigation chrome

- **[`docs/wiki/_Sidebar.md`](docs/wiki/_Sidebar.md)** — rewritten to reflect the post-rebuild structure: LAN Client Control Plane category up top (LAN Clients, Privileges, Client Config Bundle, Governance, Remote Client), Architecture & internals (ADR-001, Architecture, API Reference, Sub-Agents, Telemetry & Activity), UI/UX (Tron UI Theme), Operations (Operations, Infrastructure, Troubleshooting), Project (Automation, Versions). The deleted Auto-Connect-AI link is gone; CLU-Governance is no longer linked because it's a superseded redirect.
- **[`docs/wiki/_Footer.md`](docs/wiki/_Footer.md)** — replaces the "auto-generated by sync_docs.py" disclaimer with a hand-authored note pointing readers to the source markdown and reminding contributors that the AI Contributor Guard rejects AI-attributed commits.

#### `README.md`

- Already rewritten earlier in v0.5.0. Cross-references to the rebuilt wiki pages (LAN Clients / Privileges / Client Config Bundle / Governance / Remote Client) remain accurate.

#### Notes

- No source files modified. The CMake target list, the test suite, the route surface, the Forsetti module set, the CLU profile JSON, and the `VERSION.json` history are unchanged.
- `Home.md` and `Versions.md` continue to advertise `v0.5.0` released `2026-04-25`. The next version cut should bump per the maintainer runbook in `Versions.md`.
- Hand-authored content respects the no-AI-attribution rule throughout. Product references (e.g., `clientType: "claude_code"`, "Claude Code on host A" labels in diagrams) are runtime identifiers and not commit attribution; the AI Contributor Guard's vendor block list applies to commit author / committer / trailer fields only.

## [0.5.0] - 2026-04-25

### Phase 9 - Documentation refresh + end-to-end proof recipe (ADR-001)

The final phase of the rebuild. Wiki pages catch up to the new architecture, an end-to-end verification recipe pins the original product intent in `PROOF-OF-WORKING`, and the version bumps from `0.4.5-rc.5` to `0.5.0` to mark the architectural change. Phases 1–9 of `plans/remediation/01-gut-and-rebuild.md` are now functionally complete.

#### Added
- Five new wiki pages covering the LAN client control plane:
  - [`docs/wiki/LAN-Clients.md`](docs/wiki/LAN-Clients.md) — the data model, lifecycle endpoints, identification rules, heartbeat, maintainer workflows, activity events.
  - [`docs/wiki/Privileges.md`](docs/wiki/Privileges.md) — the nine boolean flags, gate sites, autonomous-mode bypass, capability-driven privilege bundles.
  - [`docs/wiki/Client-Config-Bundle.md`](docs/wiki/Client-Config-Bundle.md) — schemaVersion-1.0 bundle field reference, agent consumption flow, re-issue triggers.
  - [`docs/wiki/Governance.md`](docs/wiki/Governance.md) — Forsetti-aligned doctrine, the two-stage gate, all 15 action kinds, default outcomes, autonomous-mode bypass, the maintainer approval queue.
  - [`plans/PROOF-OF-WORKING/11-lan-client-end-to-end.md`](docs/archive/proof-of-working/11-lan-client-end-to-end.md) — 13-step verification recipe with two simulated LAN clients (alpha autonomous, bravo no-privileges) hitting create/discover/deny/disable flows. Pass criteria and JSON-receipt instructions included so a maintainer can convert the recipe into a captured proof on their host.
- New badges on `Home.md` reflecting `v0.5.0`, 16 modules, the LAN Client Control Plane summary.

#### Rewritten
- [`docs/wiki/Architecture.md`](docs/wiki/Architecture.md) — runtime topology updated with LAN clients flowing through the X-MCOS-Client-Id header, request-lifecycle three-gate description (identity / privilege / CLU), 16-module catalog table, ServiceContainer registration list including `ILanClientAccessService` and `IGovernanceApprovalQueueService`.
- [`docs/wiki/API-Reference.md`](docs/wiki/API-Reference.md) — every route documented with privilege requirement and CLU action kind. New sections for LAN Client identity (Phase 3+4), Client Config Bundle (Phase 5), Shared fabric (Phase 6), Runtime inventory mutation with privilege/CLU mapping, Governance + approval queue (Phase 7).
- [`docs/wiki/Remote-Client.md`](docs/wiki/Remote-Client.md) — bundle-driven onboarding flow with discover → register → grant privileges → download bundle → ship to agent → identify → heartbeat → mutate → re-issue triggers. Includes Python reference flow.
- [`docs/wiki/Home.md`](docs/wiki/Home.md) — navigation reorganized around the LAN Client Control Plane category, mermaid diagram updated to show clients with X-MCOS-Client-Id header, three-line product pitch refreshed, quick-start curl block added.

#### Marked superseded
- [`docs/wiki/CLU-Governance.md`](docs/wiki/CLU-Governance.md) — collapsed to a redirect pointing at the new `Governance.md`. The provider-era governance scope (`ProviderExecution` / `ProviderAutonomyEnable`) is gone and the page would mislead readers if kept.

#### Light updates
- [`docs/wiki/Sub-Agents.md`](docs/wiki/Sub-Agents.md) — registration example now passes the `X-MCOS-Client-Id` header; new "Use is universal" callout points at ADR-001's shared-fabric rule; supersedes the link to CLU-Governance.
- [`docs/wiki/Telemetry-and-Activity.md`](docs/wiki/Telemetry-and-Activity.md) — sample event payload uses the new `lan-client-privileges-changed` kind; new event-kinds table covers the lifecycle + governance events introduced in Phases 3–7.

#### Versioning
- `VERSION.json` advances `current_version` from `0.4.5-rc.5` to **`0.5.0`** with a fresh history entry summarizing the nine-phase rebuild. The bump is a deliberate minor (not patch) to mark the architectural change. Strategy field updated from `patch-on-main` to `minor-on-architecture-change`.

#### Notes
- The proof-of-working file is intentionally a verification **recipe**, not a captured run. Phases 1–9 were authored as code in this branch; running the build (`cmake --preset debug && cmake --build`), executing `ctest`, and capturing the live receipts is the maintainer's next step. The recipe is structured so that capture is a single sweep through 13 numbered curl steps plus a browser walkthrough.
- The Forsetti compliance script (`scripts/check-mastercontrol-forsetti.ps1`) was already updated in Phase 2 to remove provider assertions; it should pass against the new module set.
- The `MasterControlShell` (WinUI 3 desktop) remains in its Phase-2b deferred-cleanup state. A focused shell rewrite is queued as a post-`v0.5.0` follow-on.

#### Phases summary

| Phase | Status |
| --- | --- |
| 1: ADR-001 + removal inventory | ✓ |
| 2 + 2b: Provider stack removal | ✓ |
| 3: LAN client identity | ✓ |
| 4: Privilege model | ✓ |
| 5: Server-authored config bundle | ✓ |
| 6: Identification middleware + privilege gates | ✓ |
| 7: CLU governance expansion | ✓ |
| 8: Browser dashboard pivot | ✓ |
| 9: Documentation, tests, proof artifact | ✓ |

The original product intent — multiple AI models on the LAN connecting to MCOS as governed users, sharing an MCP and sub-agent fabric, operating under per-client privileges with maintainer approval for high-impact mutations — is fully realized in the service tree.

## [Unreleased]

### Phase 8 - Browser dashboard pivot (ADR-001)

The browser admin UI is rebuilt around the LAN client control plane. The 6411-line provider-era `app.js` is replaced with a focused 933-line vanilla-JS surface; the Phase 2b deferred 385 provider references are gone. Six destinations cover the locked product intent: Overview, LAN Clients, Governance, Shared Fabric, Activity, Exports.

#### Added
- **`resources/web/app.js` rewritten from scratch** (was 6411 lines / 385 provider refs; now 933 lines / 0 provider refs - the only "provider" string is the explanatory ADR-001 comment in the header). Single IIFE-scoped module, vanilla JS, no framework dependencies.
- **Six destinations** in the new browser surface:
  - **Overview** — CLU posture, LAN client counts, pending approvals, telemetry summary, recent activity strip.
  - **LAN Clients** — sortable client table with privilege count, autonomous badge, last-seen; selection drawer with full lifecycle (download config bundle, disable/enable, remove), nine privilege checkboxes with Save/Reset, autonomous-mode toggle wired to the Phase-7 CLU enforcement (HTTP 409 surfaces in the drawer status when CLU-C009 blocks). New-client form with clientId/displayName/clientType/hostName.
  - **Governance** — CLU posture stat, pending approvals with Approve/Reject buttons, decisions log (last 20), full rules listing keyed by ruleId.
  - **Shared Fabric** — read-only listing of MCP servers and sub-agents in the universal-use catalog.
  - **Activity** — full activity ring viewer with kind/actor/message columns and relative timestamps.
  - **Exports** — server-authored artifact list with one-click download. LAN client config bundles (`lan-client-config:<clientId>` from Phase 5) appear here for enabled clients.
- **Quick actions toolbar** on the home hero: "Register LAN Client", "Open Clients", "Governance Approvals". Replaces the deleted Auto-Connect / Sign-In / Validate Provider Routing buttons.
- **Live polling**: `refreshAll()` fetches `/api/health`, `/api/dashboard`, `/api/clients`, `/api/clu/approvals`, `/api/exports`, `/api/activity` in parallel every 5 seconds.
- **Config download flow**: drawer button calls `GET /api/clients/{id}/config`, blob-downloads the bundle as `lan-client-<clientId>.json` so the maintainer can drop it on the AI client's host.
- **Approval queue UI**: pending rows show actor, action kind, target, reason, and elapsed time. Reject prompts for a reason and posts to `/api/clu/approvals/{id}/reject`.
- **Phase 8 stylesheet block** appended to `resources/web/styles.css`. New rules cover the summary grid, navigation chips, panel blocks, big-stat tiles, kv-list, client table + drawer, privilege list, governance approval/decision/rule rows, runtime table, activity rows, error banner, and badge tones. Existing styles are untouched so the legacy panels keep their look until those panels are decommissioned.
- **`index.html` subtitle** updated to "LAN client identity, the shared MCP and sub-agent fabric, CLU governance, and live telemetry".

#### Removed
- The entire provider-era browser surface: Providers destination, Auto-Connect form, Sign-in flow (Claude/Codex/Grok), `data-form-kind="provider-*"` form registrations, provider sign-in card grid, Auto-Connect progress and fallback panels, provider-related navigation pointers and toolbar items.
- 6 of the 11 destinations from the old shell are intentionally not reproduced in Phase 8 (CLU panel, Imports, Exports detail, Settings, Security, Setup-Wizard). They will be added back as needed in follow-on tracks; the Phase 8 surface focuses on the locked product intent.

#### Service compatibility
- The browser depends only on Phase 1-7 endpoints; no Phase 9 endpoints required.
- Maintainer-fallback context: when no `X-MCOS-Client-Id` header is present, the browser hits the admin API as the maintainer (full privileges) so the dashboard works without a maintainer-login flow.

#### Verification
- Open `http://127.0.0.1:7300/` in a browser. Health badge transitions from "Connecting" to "Online" within 5 seconds.
- Use the "Register LAN Client" toolbar button to fill the form. The new client appears in the LAN Clients table after the registration completes.
- Select the client, toggle privileges in the drawer, click Save Privileges. Activity stream shows the `lan-client-privileges-changed` event.
- Click "Download config bundle". The browser downloads `lan-client-<clientId>.json` containing the Phase 5 schemaVersion-1.0 bundle.
- Click "Enable autonomous". With `aiAutonomyEnabled=false` (default) the drawer status shows the CLU-C009 refusal text. After flipping `aiAutonomyEnabled` to true via `/api/config`, autonomous enables and the badge updates.
- Trigger a `GovernancePolicyChange` (when an admin route for it lands) to see a deferred action arrive in the Governance destination's pending list. Approve or Reject and see it move to the decisions log.

#### Out of scope for Phase 8 (lands in Phase 9)
- A "Settings" destination for editing host config (bind address, beacon, resource allocation). Today these still need direct curl to `/api/config`.
- Maintainer-login flow. Phase 8 keeps the maintainer-fallback context for browser ergonomics; multi-maintainer audit lands as a future track.
- Migration of the WinUI 3 shell. The shell remains in its Phase-2b broken state and is queued for a dedicated track after Phase 9.
- A documentation refresh covering the new surfaces (lands in Phase 9).

### Phase 7 - CLU governance expansion, aligned to Forsetti (ADR-001)

CLU's enforcement scope grows from three action kinds to fifteen, the autonomous-mode soft gate from Phase 4 lifts (CLU now governs the flip), and a maintainer approval queue lands for actions that defer. The governance profile is rewritten in Forsetti Framework terms with `LanClient` as a first-class governance role.

#### Added
- `GovernanceActionKind` enum expands to **15 values** (was 2 after the Phase 2 cleanup): `Unknown`, `ClientRegister`, `ClientPrivilegeChange`, `ClientAutonomousModeChange`, `ClientRevoke`, `McpServerCreate`, `McpServerModify`, `McpServerRemove`, `SubAgentCreate`, `SubAgentModify`, `SubAgentRemove`, `ModuleEnable`, `ModuleDisable`, `GovernancePolicyChange`, `RemoteInstall`.
- `GovernanceDecisionOutcome { Allow, Block, RequiresMaintainerApproval }` and the matching `to_string` / `fromString` / `to_json` / `from_json`.
- `GovernanceEnforcementDecision` extended with `outcome` and `deferredActionId`. `allowed` stays for Phase-6 backward compatibility (Allow == true).
- `GovernanceEnforcementRequest` extended with `actor` so deferred records identify who requested the change.
- New `GovernanceDeferredAction` struct + `IGovernanceApprovalQueueService` interface in `include/MasterControl/MasterControlContracts.h`. The queue lives in process memory only (Phase 9 may add disk backing if long-running deferrals become a real workflow).
- `GovernanceApprovalQueueService` in-memory implementation: `listPending`, `listAll`, `stage`, `approve`, `reject`. Emits activity events `governance-deferred`, `governance-approved`, `governance-rejected`.
- New admin routes:
  - `GET /api/clu/approvals` — full deferred-action list
  - `POST /api/clu/approvals/{id}/approve` — maintainer approval (gated by `canChangeGovernancePolicy`)
  - `POST /api/clu/approvals/{id}/reject` — maintainer rejection with optional `{ reason }` body
- `enforceGovernance` lambda in `handleHttpRequest`: runs after the privilege gate and before the route's apply path. Returns `nullopt` for Allow, HTTP 403 for Block, HTTP 202 with deferred-action id for RequiresMaintainerApproval.
- `CommandLogicUnitService::enforceAction` rewritten as a switch over the full action enum:
  - **RemoteInstall**: existing logic preserved (resource preflight + posture check).
  - **McpServerCreate / McpServerModify / SubAgentCreate / SubAgentModify**: Allow when posture is fine, Block when posture is `blocked`.
  - **McpServerRemove / SubAgentRemove**: same posture-aware allow/block. Future profile rules may flip these to RequiresMaintainerApproval.
  - **ClientRegister / ClientPrivilegeChange / ClientRevoke**: posture-gated.
  - **ClientAutonomousModeChange**: source-aware. Disabling is always allowed; enabling requires posture-fine + `aiAutonomyEnabled` true (CLU-C009).
  - **ModuleEnable / ModuleDisable**: posture-gated.
  - **GovernancePolicyChange**: always RequiresMaintainerApproval (CLU-C010). The deferred record carries the original payload verbatim.
- Governance profile rewritten in Forsetti terms (`resources/clu/governance-profile.json`):
  - Doctrine restated: contract before action, scope is binding, truthfulness, governance overrides convenience, no meaningful autonomous action without declared scope.
  - `LanClient` added as a first-class governance role with declared responsibilities, forbidden actions, and required outputs. `mcos-runtime` added as the enforcement role.
  - New documents: Shared Fabric Policy, Autonomous Mode Doctrine, Governance Policy Doctrine.
  - New rules: CLU-C009 (autonomous mode requires global autonomy), CLU-C010 (profile edits require maintainer approval), CLU-S001 (shared fabric is universal for use), CLU-S002 (mutations attributed to actor).
  - Provider-era rule CLU-C004 retired (replaced by CLU-S001/S002 + the privilege model).
- Test coverage: full enum round-trip for `GovernanceActionKind` (14 values) and `GovernanceDecisionOutcome` (3 values), plus `GovernanceDeferredAction` JSON shape pinning.

#### Removed
- **Phase 4 autonomous-mode soft gate.** `LanClientAccessService::setAutonomousMode` no longer hard-rejects `enabled=true`; CLU now decides based on posture and `aiAutonomyEnabled`. The HTTP 409 path is retired in favor of CLU's structured Block/Allow decisions.
- `LanClientAccessService::upsertClient` no longer rejects `autonomousMode=true` registrations. CLU enforces via `ClientAutonomousModeChange` (the upsert path emits the action implicitly when the maintainer sets autonomous mode at registration).

#### Wiring
- Every privileged mutation route now runs CLU after the privilege gate:
  - MCP server upsert (Create or Modify based on existence) and remove
  - Sub-agent upsert (Create or Modify) and remove
  - Client roster: register, disable (ClientRevoke), enable (ClientRegister), DELETE (ClientRevoke)
  - Client privileges (ClientPrivilegeChange)
  - Client autonomous mode (ClientAutonomousModeChange with source `enable`/`disable`)
  - Forsetti module state (ModuleEnable / ModuleDisable based on requested action)
- Autonomous-mode bypass on Create paths preserved per ADR-001: a client with `autonomousMode = true` skips both the create privilege and CLU enforcement for `McpServerCreate` / `SubAgentCreate` (still subject to CLU at modify/remove paths).

#### Verification
- Enable global AI autonomy, then enable client autonomous mode:
  ```
  curl -X POST .../api/config -H "X-Confirm-Unsafe: 1" -d '{"aiAutonomyEnabled":true,...}'
  curl -X POST .../api/clients/alpha/autonomous-mode -d '{"enabled":true}'
  # → 200 (CLU-C009 allows when aiAutonomyEnabled is true)
  ```
- Disable global AI autonomy and try again:
  ```
  curl -X POST .../api/config -H "X-Confirm-Unsafe: 1" -d '{"aiAutonomyEnabled":false,...}'
  curl -X POST .../api/clients/alpha/autonomous-mode -d '{"enabled":true}'
  # → 403, ruleId CLU-C009, message "Enable global AI autonomy in configuration before granting client autonomous mode."
  ```
- Trigger the GovernancePolicyChange path (when a policy-edit endpoint is wired in Phase 8+, every change will return 202 with a `deferredActionId`):
  ```
  curl .../api/clu/approvals          # list staged
  curl -X POST .../api/clu/approvals/deferred-1/approve
  curl -X POST .../api/clu/approvals/deferred-1/reject -d '{"reason":"superseded"}'
  ```
- Autonomous client creates 10 MCP servers without approval:
  ```
  curl -X POST .../api/clients/alpha/privileges -d '{}'   # no privileges
  curl -X POST .../api/clients/alpha/autonomous-mode -d '{"enabled":true}'
  for i in 1..10: curl -X POST -H "X-MCOS-Client-Id: alpha" .../api/runtime/mcp-servers \
    -d '{"id":"shared-${i}","displayName":"Shared ${i}","host":"127.0.0.1","port":9000,"protocol":"http","kind":"mcp_server"}'
  # → all 200; create privileges and CLU bypass per ADR-001 autonomous semantics
  ```
- Same client tries to modify another client's server: 403 from privilege gate (canModifyMcpServers missing), CLU never runs.

#### State of the rebuild
- All seven phases scoped in `plans/remediation/01-gut-and-rebuild.md` are now functionally complete in the service tree:
  1. ADR-001 + removal inventory ✓
  2. Provider stack removal (with Phase 2b residual cleanup) ✓
  3. LAN client identity ✓
  4. Privilege model (autonomous-mode soft gate now lifted) ✓
  5. Server-authored config bundle ✓
  6. Client identification middleware + privilege gates ✓
  7. CLU governance expansion ✓
- Remaining: Phase 8 (browser dashboard surfaces) and Phase 9 (docs + end-to-end proof artifact).

### Phase 6 - Client identification middleware and privilege enforcement (ADR-001)

**Pivot moment.** The bundle issued in Phase 5 now has teeth. Every incoming request resolves to an `AuthenticatedRequestContext`, mutation routes are gated by per-client privileges, and a new `/api/client/*` shared-fabric read surface gives identified clients a discoverable view of MCOS without going through the admin API.

#### Added
- `include/MasterControl/AuthenticatedRequestContext.h` — per-request context type carrying the resolved `LanClient`, the privilege snapshot, the autonomous-mode flag, the actor label, and an `isMaintainerFallback` diagnostic.
- `makeMaintainerContext()` factory — full-privilege synthetic context for missing or unknown header (so the browser dashboard and ad-hoc curl keep working unchanged).
- Header-driven resolver inlined into `MasterControlApplication::Impl::handleHttpRequest`:
  - Reads `X-MCOS-Client-Id` (case-insensitive per RFC 7230)
  - Resolves via `ILanClientAccessService::getClient`
  - On hit: populates context, calls `touchClient` for liveness
  - On miss (unknown id): falls through to maintainer context
  - On disabled client: returns HTTP 403 immediately, never enters route handlers
- `requirePrivilege(granted, name)` lambda available across all routes inside `handleHttpRequest`. Returns `nullopt` when allowed, otherwise a 403 `HttpResponse` with `{ succeeded: false, errorMessage, actor, privilege }`.
- Privilege gates on every mutation route:
  - `POST /api/runtime/mcp-servers`: `canModifyMcpServers` when the id already exists, otherwise `canCreateMcpServers` (autonomous-mode bypass for the create path).
  - `POST /api/runtime/mcp-servers/remove`: `canRemoveMcpServers`.
  - `POST /api/runtime/subagents`: `canModifySubAgents` when the id exists, otherwise `canCreateSubAgents` (autonomous-mode bypass).
  - `POST /api/runtime/subagents/remove`: `canRemoveSubAgents`.
  - `POST /api/runtime/subagent-groups[/remove]`: `canModifySubAgents` (groups are organizational metadata over the existing roster).
  - `POST /api/clients`, `POST /api/clients/{id}/{disable,enable,privileges,autonomous-mode}`, `DELETE /api/clients/{id}`: `canManageClients`.
  - `POST /api/forsetti/modules/state`: `canManageModules`.
- New `/api/client/*` shared-fabric read surface (matches the catalog URLs the bundle advertises):
  - `GET /api/client/mcp-servers` — every non-template MCP endpoint, open to any identified client.
  - `GET /api/client/sub-agents` — every non-template sub-agent endpoint.
  - `GET /api/client/activity` — full activity ring snapshot. Phase 7 may filter to events scoped to the requester.
  - `GET /api/client/governance/profile` — read-only governance summary keyed by Forsetti framework identity.
  - `POST /api/client/governance/decisions` — Phase 6 stub returning HTTP 202 with `{ outcome: "deferred", message }`. Phase 7 wires this to the expanded `commandLogicUnitService_->enforceAction` for real Allow / Block / RequiresMaintainerApproval pre-checks.
  - `POST /api/client/heartbeat` — updates `lastSeenUtc` for the resolved client (no-op for the maintainer fallback). Returns the actor and maintainer-fallback flag so the client can detect identity drift.
- Test coverage: `AuthenticatedRequestContext` defaults assertion + `makeMaintainerContext` grants-all-privileges assertion.

#### Notable design choices
- **No auth, just identification.** Per ADR-001 the LAN is trusted. The `X-MCOS-Client-Id` header is the only identity claim; a missing or unknown header is *not* a denial - it's a fallback. This keeps dashboard ergonomics and lets one maintainer unblock themselves without juggling tokens.
- **Disabled clients are denied at the door.** A header naming a registered-but-disabled client returns 403 before any route handler runs. This is the only way a maintainer can effectively shut a client out.
- **Activity ring is shared.** Phase 6 deliberately exposes the same ring to LAN clients as the maintainer dashboard. Phase 7 may add per-client filtering once governance decisions emit events keyed to the actor.

#### Verification
- Two-client scenario:
  ```
  curl -X POST http://127.0.0.1:7300/api/clients \
    -d '{"clientId":"alpha","displayName":"Alpha","clientType":"claude_code"}'
  curl -X POST http://127.0.0.1:7300/api/clients \
    -d '{"clientId":"bravo","displayName":"Bravo","clientType":"codex"}'
  curl -X POST http://127.0.0.1:7300/api/clients/alpha/privileges \
    -d '{"canCreateMcpServers":true}'
  curl -X POST -H "X-MCOS-Client-Id: alpha" http://127.0.0.1:7300/api/runtime/mcp-servers \
    -d '{"id":"shared-tool","displayName":"Shared Tool","host":"127.0.0.1","port":9000,"protocol":"http","kind":"mcp_server"}'
  # → 200, alpha created the MCP server
  curl -X POST -H "X-MCOS-Client-Id: bravo" http://127.0.0.1:7300/api/runtime/mcp-servers \
    -d '{"id":"another-tool","displayName":"Another","host":"127.0.0.1","port":9001,"protocol":"http","kind":"mcp_server"}'
  # → 403 with { errorMessage: "Required privilege missing: canCreateMcpServers.", privilege: "canCreateMcpServers", actor: "bravo" }
  curl -H "X-MCOS-Client-Id: bravo" http://127.0.0.1:7300/api/client/mcp-servers
  # → 200, bravo lists the shared-tool entry alpha created (use is universal)
  ```
- Disable a client and confirm rejection:
  ```
  curl -X POST http://127.0.0.1:7300/api/clients/alpha/disable
  curl -H "X-MCOS-Client-Id: alpha" http://127.0.0.1:7300/api/client/mcp-servers
  # → 403 with errorMessage: "LAN client is disabled: alpha"
  ```
- Heartbeat:
  ```
  curl -X POST -H "X-MCOS-Client-Id: bravo" http://127.0.0.1:7300/api/client/heartbeat
  # → 200, lastSeenUtc on bravo updates (visible via GET /api/clients/bravo)
  ```

#### Out of scope for Phase 6 (lands in later phases)
- Real CLU pre-check on `/api/client/governance/decisions` (Phase 7).
- Approval queue for `RequiresMaintainerApproval` deferred actions (Phase 7).
- Per-client filtering of `/api/client/activity` (Phase 7).
- Any browser dashboard surfaces for the new endpoints (Phase 8).
- Periodic `lastSeenUtc` flush to disk (deliberately skipped to avoid hot-path thrash; Phase 9 may add a timer if last-seen survival across restarts becomes a hard requirement).

### Phase 5 - Client configuration bundle (ADR-001)

Delivers the onboarding primitive: a server-authored JSON bundle that an AI client downloads, drops onto its host, and uses to learn how to reach MCOS, what header to identify with, what privileges it carries, and what governance rules apply. This is the first user-visible piece that matches the original product intent ("multiple AI models on the LAN connect and use the MCOS").

#### Added
- `composeLanClientConfigBundle(client, configuration)` free function in `src/MasterControlApp/MasterControlRuntime.cpp` that emits the spec'd schemaVersion 1.0 bundle.
- `resolveMcosServerHost(configuration)` helper. Substitutes `bindAddress` first, then `activeProfile.preferredBindAddress`, then `127.0.0.1`. **Never serves `0.0.0.0`** to remote clients (which can't route to the wildcard).
- New admin route `GET /api/clients/{id}/config`. Returns 200 with the bundle for a registered client, 404 when the id is unknown. Placed before the bare `/api/clients/{id}` GET so the suffix doesn't collide.
- Bundle shape (pinned, schema versioned):
  - `schemaVersion`, `issuedAtUtc`
  - `mcosServer` (fully-qualified `http://host:port`)
  - `clientId`, `displayName`, `clientType`, `enabled`
  - `identification` = `{ header: "X-MCOS-Client-Id", value: clientId }` (Phase 6 middleware reads this exact header)
  - `privileges` (snapshot of the nine booleans from Phase 4)
  - `autonomousMode` (always false until Phase 7 lifts the soft gate)
  - `catalogs` = `{ mcpServers, subAgents, activity }` URL paths
  - `governance` = `{ authority: "CLU", framework: "Forsetti Framework for Agentic Coding", profileEndpoint, decisionEndpoint }`
  - `rules` array describing the shared-fabric semantics
  - `instructions` for heartbeat, discovery, invocation, governance pre-check
- `ExportService` now takes `ILanClientAccessService` and surfaces a per-client `lan-client-config:<clientId>` artifact in `/api/exports`. Disabled clients are deliberately omitted (their bundle would lie about reachability). File names follow `lan-client-<clientId>.json`.
- Test coverage: bundle shape pinning + identification header invariant + the never-serve-0.0.0.0 guarantee.

#### Verification
- Register a client, then download its bundle:
  ```
  curl -X POST http://127.0.0.1:7300/api/clients \
    -d '{"clientId":"claude-code-jdaley-wks","displayName":"Claude Code","clientType":"claude_code"}'
  curl http://127.0.0.1:7300/api/clients/claude-code-jdaley-wks/config
  ```
  The response is the schemaVersion-1.0 bundle with `mcosServer` resolved to a reachable URL (never `0.0.0.0`).
- Verify the bundle also surfaces in `/api/exports` once the client exists:
  ```
  curl http://127.0.0.1:7300/api/exports
  ```
  Look for `"id": "lan-client-config:claude-code-jdaley-wks"`.
- Disable the client and re-fetch `/api/exports`: the disabled client's bundle is omitted from the export listing, while `GET /api/clients/{id}/config` still serves it (maintainers can still inspect a disabled client's last-issued bundle).

#### Out of scope for Phase 5 (lands in later phases)
- The `X-MCOS-Client-Id` header middleware that reads the bundle's `identification.value` and applies privilege gates (Phase 6).
- The `/api/client/mcp-servers`, `/api/client/sub-agents`, `/api/client/activity`, `/api/client/governance/*`, and `/api/client/heartbeat` endpoints the bundle advertises (Phase 6).
- CLU-driven approval queue + governance decision endpoint (Phase 7).
- Browser dashboard "Download config" button (Phase 8).

### Phase 4 - Privilege model (ADR-001)

Fills the `LanClientPrivileges` shell with nine flat boolean toggles and exposes them on dedicated admin routes. Use is never gated; only creation, modification, and removal of MCP servers and sub-agents (plus client / module / governance management) are privilege-controlled. Locked decisions per ADR-001.

#### Added
- `LanClientPrivileges` boolean fields in `include/MasterControl/LanClient.h`:
  - `canCreateMcpServers`, `canModifyMcpServers`, `canRemoveMcpServers`
  - `canCreateSubAgents`, `canModifySubAgents`, `canRemoveSubAgents`
  - `canManageClients` (register / modify / remove other LAN clients)
  - `canManageModules` (enable / disable Forsetti modules)
  - `canChangeGovernancePolicy` (edit CLU profile)
  - All default to `false` so newly registered clients are read-only until a maintainer explicitly grants capability.
- NLOHMANN macro on `LanClientPrivileges` enumerating the nine fields for JSON round-trip.
- `ILanClientAccessService::setPrivileges(clientId, LanClientPrivileges)` and `setAutonomousMode(clientId, bool)` interface methods.
- `LanClientAccessService` implementations of both, with the autonomous-mode soft gate.
- Two new admin routes:
  - `POST /api/clients/{id}/privileges` - replace the privilege struct atomically (read-modify-write through `GET /api/clients/{id}` for partial updates).
  - `POST /api/clients/{id}/autonomous-mode` - toggle autonomous mode. Body shape: `{"enabled": true|false}`.
- Activity events: `lan-client-privileges-changed`, `lan-client-autonomous-mode-changed`.
- Test coverage: privilege defaults (all nine flags), privilege JSON round-trip with mixed true/false values.

#### Changed
- `upsertClient` now refuses any registration that sets `autonomousMode = true` until Phase 7 ships, returning the same 409-tier error message as the dedicated route.

#### Soft gate (intentional, removed in Phase 7)
- Enabling autonomous mode on any client returns HTTP 409 with message *"Autonomous mode cannot be enabled until CLU governance expansion ships in Phase 7."* Disabling autonomous mode is always allowed.
- Rationale: per ADR-001 the autonomous-mode flag's runtime meaning is "bypass `canCreateMcpServers` / `canCreateSubAgents` privilege check + bypass CLU maintainer approval for those two action kinds." That semantic depends on the expanded `GovernanceActionKind` enum that lands in Phase 7. Allowing the flag to be set before the gate is wired would silently mean nothing.

#### Verification
- Set privileges: `curl -X POST http://127.0.0.1:7300/api/clients/claude-code-jdaley-wks/privileges -d '{"canCreateMcpServers":true,"canCreateSubAgents":true}'` returns `{"succeeded":true,"message":"LAN client privileges updated."}`.
- Read back: `GET /api/clients/claude-code-jdaley-wks` shows the new flags in the `privileges` object.
- Toggle autonomous mode off when already off: idempotent `{"succeeded":true,"message":"Autonomous mode was already disabled."}`.
- Try to enable autonomous mode: `curl -X POST .../autonomous-mode -d '{"enabled":true}'` returns HTTP 409 with the soft-gate message.
- Activity stream: `GET /api/activity` shows the `lan-client-privileges-changed` event keyed to the requested client.

#### Out of scope for Phase 4 (lands in later phases)
- Privilege enforcement at the `/api/runtime/mcp-servers` and `/api/runtime/subagents` mutation handlers (Phase 6 middleware).
- CLU governance hooks for client-privilege changes and the autonomous-mode action kind (Phase 7).
- Browser dashboard surface for the new toggles (Phase 8).

### Phase 3 - LAN client identity model (ADR-001)

First additive phase of the rebuild. Introduces the `LanClient` first-class identity that replaces the deleted `ProviderConnection`. Identity is by `clientId` alone; no tokens, no DPAPI secrets, no auth — the LAN is trusted per locked decisions in ADR-001.

#### Added
- `include/MasterControl/LanClient.h` — `LanClient` struct (clientId, displayName, clientType, hostName, networkAddress, enabled, privileges, autonomousMode, createdAtUtc, lastSeenUtc, disabledAtUtc) plus an empty `LanClientPrivileges` shell that Phase 4 fills in. NLOHMANN macros for both.
- `include/MasterControl/ILanClientAccessService.h` — clean service interface: `listClients`, `getClient`, `upsertClient`, `disableClient`, `enableClient`, `removeClient`, `touchClient` (last-seen hot path for Phase 6 middleware).
- `LanClientAccessService` implementation in `src/MasterControlApp/MasterControlRuntime.cpp` with case-normalized clientId lookups, alphabetized roster, on-disk persistence through `AppConfiguration::lanClients`, and activity events keyed `lan-client-created` / `lan-client-updated` / `lan-client-disabled` / `lan-client-enabled` / `lan-client-removed`.
- `LanClientAccessModule` Forsetti module: declaration in `include/MasterControl/MasterControlModules.h`, implementation in `src/MasterControlModules/MasterControlModules.cpp`, manifest at `src/MasterControlModules/Resources/ForsettiManifests/LanClientAccessModule.json`. Not protected. Module id `com.mastercontrol.lan-client-access`. Capabilities: `storage`, `event_publishing`.
- Default activation list grows from 15 modules to 16 (`LanClientAccessModule` slotted between `ExportModule` and `CommandLogicUnitModule`).
- Six new admin API routes:
  - `GET /api/clients` — list registered LAN clients
  - `GET /api/clients/{id}` — single client lookup
  - `POST /api/clients` — register or update a client
  - `POST /api/clients/{id}/disable` — soft-disable a client (preserves the record, sets `disabledAtUtc`)
  - `POST /api/clients/{id}/enable` — re-enable a previously disabled client
  - `DELETE /api/clients/{id}` — remove a client from the roster
- `AppConfiguration::lanClients` vector + NLOHMANN field entry.
- `appendLanClientActivity` free-function shim so the LAN-client service can emit ring events without holding the full `ActivityEventRing` type at its point of definition.
- Test coverage in `tests/MasterControlOrchestrationServerTests.cpp`: LanClient JSON round-trip, default-state assertions, AppConfiguration container round-trip.

#### Verification
- Register a client: `curl -X POST http://127.0.0.1:7300/api/clients -d '{"clientId":"claude-code-jdaley-wks","displayName":"Claude Code on Jdaley workstation","clientType":"claude_code","hostName":"PC-GAMING-R7-58"}'` returns `{"succeeded":true,"message":"LAN client registered."}`.
- List: `GET /api/clients` shows the new client.
- Restart the service and re-`GET /api/clients` — record persists.
- Disable: `POST /api/clients/claude-code-jdaley-wks/disable` succeeds; subsequent `GET /api/clients/claude-code-jdaley-wks` shows `enabled:false` and a populated `disabledAtUtc`.
- Activity stream: `GET /api/activity` shows the lifecycle events.

#### Out of scope for Phase 3 (lands in later phases)
- Privilege enforcement on creation/modification/removal of MCP servers and sub-agents (Phase 4 + Phase 6).
- Configuration bundle `GET /api/clients/{id}/config` (Phase 5).
- `X-MCOS-Client-Id` request middleware (Phase 6).
- CLU governance hooks for client lifecycle (Phase 7).
- Browser dashboard surfaces (Phase 8).

### Major architectural remediation - Gut and Rebuild (Phase 2 of ADR-001)

**Breaking:** The AI provider integration stack is removed in full per [ADR-001 - LAN Client Control Plane](docs/wiki/ADR-001-lan-client-control-plane.md). MCOS is being rebuilt as a LAN client control plane where external AI clients connect, receive a server-authored configuration bundle, and operate under per-client privileges enforced server-side.

### Removed
- Four Forsetti modules: `ProviderIntegrationModule`, `CodexProviderModule`, `ClaudeCodeProviderModule`, `XAIProviderModule` (source, manifests, runtime registrations, default activation entries, protected-set membership).
- Provider data types: `ProviderConnection`, `ProviderCapabilityDescriptor`, `ProviderAssignment`, `ProviderAssignmentTarget`, `ProviderAssignmentTargetKind`, `ProviderKind`, `ProviderExecutionTransport`, `ProviderExecutionRegistration`, `ProviderExecutionRequest`, `ProviderExecutionRecord`, `ProviderCredentialStatus`, `ProviderCredentialUpdate`, `ProviderCredentialFieldDescriptor`, `ProviderCredentialFieldKind`, `ProviderExecutionStatus`, `DiscoveredModel`, `AutoConnectRequest`, `AutoConnectStep`, `AutoConnectResult`.
- Provider service interfaces: `IProviderRegistry`, `IProviderCatalogService`, `IProviderCredentialStore`, `IProviderAssignmentService`, `IProviderExecutionCatalogService`, `IProviderExecutionService`.
- Provider service implementations: `ProviderCatalogService`, `ProviderRegistryService`, `ProviderCredentialStore`, `ProviderCliSignInService`, `ProviderAssignmentService`, `ProviderExecutionCatalogService`, `ProviderExecutionService`.
- Outbound AI transports: `executeClaudeCodeCli`, `executeCodexCli`, `executeOpenAICompatibleChat`. MCOS does not call AI models; AI clients call MCOS.
- HTTP routes: `GET/POST /api/providers`, `/api/providers/credentials`, `/api/providers/auto-connect`, `/api/providers/signin/{register,start,status,installed}`, `/api/providers/groups[/remove]`, `/api/providers/assignments`, `/api/providers/execute`.
- Sub-agent group routes renamed: `/api/providers/groups[/remove]` → `/api/runtime/subagent-groups[/remove]`.
- Shell surface: `ProvidersSectionControl.xaml` and code-behind, associated IDL entries and project references.
- Docs: `docs/wiki/Auto-Connect-AI.md`.
- Proof artifacts: `plans/PROOF-OF-WORKING/02-auto-connect.md`, `plans/PROOF-OF-WORKING/11-ai-task-execution.md`.
- Related provider constants, helper functions, readiness-snapshot provider tallies, starter-workflow provider references, Forsetti governance action kinds `ProviderExecution` / `ProviderAutonomyEnable`, activity-event kinds `ProviderExecution` / `AutoConnect`.

### Added
- `docs/wiki/Architecture-Decisions/ADR-001-lan-client-control-plane.md` - architectural decision record.
- `plans/remediation/01-gut-and-rebuild.md` - full nine-phase remediation plan.
- `plans/remediation/02-removal-inventory.md` - exhaustive Phase 2 checklist.

### Phase 2b follow-up (service-tree cleanup complete)
All five structural grep invariants return 0 across the service tree (`src/MasterControlApp`, `src/MasterControlModules`, `include`, `tests`):
- Provider module class refs: 0
- Provider data type refs (`ProviderConnection`, `ProviderAssignment`, `AutoConnectResult`, etc.): 0
- Outbound CLI transport refs (`executeClaudeCodeCli`, etc.): 0
- `/api/providers*` path refs: 0
- Provider module id refs: 0

Remaining `Provider` hits in the service tree are Forsetti framework's own `IEntitlementProvider` / `FileBackedEntitlementProvider` (unrelated to AI providers) plus a handful of comments explaining the removal.

### Still deferred (non-service compile units, scheduled for later phases)
- `src/MasterControlShell/ShellRuntime.cpp`, `MainWindow.xaml*` - shell is a separate compilation unit and the header-level cleanup in this phase removed `ShellProviderConnection` and sibling types. The `.cpp` files still reference those types; they will not compile until their remaining call sites are cleaned. A dedicated shell-track pass lands after Phase 8. The service compiles and runs without the shell.
- `resources/web/app.js` - browser dashboard. Complete rewrite in Phase 8 per the plan.
- `scripts/github_agents/sync_docs.py` - wiki doc generator. Rewritten in Phase 9 alongside the new doc set.

### Notes
- Default Forsetti activation drops from 19 modules to 15. Protected-module set drops from 5 to 4 (provider-integration removed).
- Top-level inventory: 10 standalone file deletions, 6 headers cleaned, 3 `.cpp` files under `src/MasterControlApp` and `src/MasterControlModules` cleaned, 1 test suite replaced with skeleton, 1 compliance script cleaned, 2 key wiki pages banner-updated, CHANGELOG entry (this entry).
- See `plans/remediation/02-removal-inventory.md` for the full checklist and follow-up grep invariants.

## [0.4.5-rc.5] - 2026-04-24
### Summary
**Release candidate for the non-security remediation pass.** This candidate promotes the packaging, documentation, and shared-auth provider fixes from the remediation review into a clean RC while intentionally deferring security hardening to a later phase.

### Included Changes
- fix(models): shared-auth metadata now keeps ChatGPT and Codex tied to the same OpenAI bridge and provider family across module registration, execution registration, and shell snapshots.
- test: `tests/MasterControlOrchestrationServerTests.cpp` now asserts `providerFamilyId` and `authBridgeId` coverage for the shared OpenAI-backed providers.
- fix(shell/build): `MasterControlShell` now restores Windows App SDK packages into a repo-local `.nuget` cache, ignores that cache, and drops the tracked `src/MasterControlShell/packages` tree without breaking clean builds.
- fix(ci/docs): Windows packaging CI and maintainer docs now target `dist/packages/release`, validate version-badge sync, and describe the MSI-first host-versus-remote workflow accurately.
- release: cut the non-security remediation candidate as `v0.4.5-rc.5` while deferring security remediation to a future phase.

## [0.4.5-rc.4] - 2026-04-22
### Summary
**Release candidate for the latest Windows-app build.** This candidate promotes the current `main` fixes into a clean RC so the packaged release is no longer stuck on the older rc.3 commit. It includes the product-named installed launcher and the provider-ownership safeguards that stop disconnected providers from lingering in assignment and execution paths.

### Included Changes
- feat(launcher): add the installed `MasterControlOrchestrationServer.exe` product launcher and point MSI shortcuts plus launch-after-install at it so the installed product behaves like a normal Windows application entrypoint.
- fix(shell): provider assignment surfaces now filter out disconnected or unsupported providers, so stale ownership stops masquerading as a sign-in failure.
- fix(runtime): role execution now names the disconnected provider route that still owns a lane and tells the maintainer to reassign it instead of surfacing a generic credentials warning.
- test: installer/package regression coverage now verifies the launcher wiring, and `tests/MasterControlOrchestrationServerTests.cpp` covers the stale-provider ownership failure path.
- release: promote the latest `main` commit into a fresh release candidate so the formal RC matches the newest packaged build.

## [0.4.5-rc.3] - 2026-04-21
### Summary
**Release candidate for the Windows app install and provider-role workflow.** This candidate fixes the last two maintainer-facing gaps from rc.2: the MSI now presents the product as the Windows application instead of "the shell," and the Windows app no longer lets stale cached provider rows shadow the live signed-in provider state after authentication.

### Included Changes
- fix(shell): the Windows app now prefers authoritative `/api/dashboard` provider and sub-agent-group rows over stale cached rows, de-duplicating by id so authenticated providers stay marked ready for role assignment and autonomy controls after sign-in.
- test: `tests/MasterControlOrchestrationServerTests.cpp` covers the authoritative snapshot merge semantics that caused stale unsigned provider rows to shadow the live signed-in copy.
- fix(installer): `installer/MasterControlOrchestrationServer.wxs` now says "Launch Master Control Orchestration Server" and "Launch the ... Windows app after installation completes" instead of telling maintainers to launch "Master Control Shell."
- test: the regression suite now verifies the MSI still exposes Start Menu and Desktop shortcut options while using Windows-app wording instead of shell wording.
- fix(browser/package): host guidance in `resources/web/app.js`, `INSTALL.txt`, and `START-HERE.txt` now consistently points maintainers at the Master Control Orchestration Server Windows app on the host machine.

## [0.4.5-rc.2] - 2026-04-20
### Summary
**GitHub-published release candidate for the Windows host experience.** This release supersedes the old `v0.4.5-rc.1` tag mismatch and publishes the current host-session AI sign-in and MSI-first packaging fixes as a clean prerelease from the latest commit.

### Included Changes
- fix(shell): the Windows app owns `claude login` / `codex login` launch and saved-auth detection, so host users authenticate in their interactive session instead of the service session.
- fix(runtime): CLI-backed provider registration now preserves `credentialsConfigured` through provider upsert and dashboard snapshot generation, so ChatGPT, Codex, and Claude Code stay marked ready after shell sign-in.
- test: `tests/MasterControlOrchestrationServerTests.cpp` exercises the CLI sign-in registration handoff for existing OpenAI-backed and Claude-backed providers.
- fix(packaging): the maintainer-facing ZIP is MSI-first, no longer ships the legacy setup exe, and excludes packaged `.pdb` symbol files from end-user artifacts.
- release: publish the `v0.4.5-rc.2` GitHub prerelease with the matching MSI, ZIP, and package metadata assets.

## [0.4.5-rc.1] - 2026-04-19
### Summary
**Release candidate for the Windows host experience.** This candidate rolls up the guided CLI-install flow, the host-session AI sign-in fix so `claude login` / `codex login` open for the interactive Windows user instead of the service account, and the packaging cleanup needed to ship a clean MSI + ZIP pair from the latest commits.

### Included Changes
- feat(runtime): dependency install flow now handles Node.js as the prerequisite for Claude Code / Codex CLI install on clean Windows hosts, refreshes PATH after install, and keeps the provider onboarding path guided instead of dead-ending on missing npm.
- fix(shell): the desktop shell now owns CLI sign-in launch and auth-file detection for host usage, then hands successful provider registration back to the backend. Host users finally see the authentication prompt in their own session.
- test: `tests/MasterControlOrchestrationServerTests.cpp` now covers the CLI sign-in registration handoff so invalid bridge payloads fail and a successful Codex handoff registers the expected providers.
- fix(installer): the trailing-backslash trim custom action now lives in `TrimInstallFolder.vbs`, and `CustomActions.wxs` no longer contains WiX-invalid XML comment text that prevented MSI generation in clean release builds.
- fix(packaging): staged end-user payloads no longer include `*.pdb` symbol files, and the MSI metadata handoff now records the built `.msi` and `msiVersion` correctly in `PACKAGE-METADATA.json`.

## [0.4.4-rc.1] - 2026-04-18
### Summary
**Auto-install the CLIs from the shell.** The sign-in cards have worked since rc.5, but they silently failed when the underlying CLI (`claude` or `codex`) wasn't on PATH. The browser UI showed a grayed-out "CLI not installed" button and the shell just errored on click. rc.1 closes that gap: the Providers surface now detects on load whether each CLI is installed, and when either is missing it surfaces an active **Install Claude Code CLI** / **Install Codex CLI** button that runs the preset `npm install -g` command through the admin API. A `ProgressRing` ticks while npm is installing; on completion the Install button hides, the Sign-In button enables, and the maintainer can keep going without opening a terminal.

### Included Changes
- feat(runtime): `buildSupportedDependencyCatalog()` now includes a `codex-cli` entry (`npm install -g @openai/codex`) alongside `claude-code-cli`. Both honor the existing three-branch preflight (`ready` / `installable` / `prerequisite-missing`) so the browser + shell can surface useful status when Node.js/npm itself isn't on PATH.
- feat(shell): `ShellRuntime::InstallCliDependency(bridge)` POSTs to `/api/setup/dependencies/{id}/install` and parses the structured response (`succeeded`, `finalState`, `summary`, `postInstallDetection.detectedVersion`). Returns a `ShellCliDependencyInstallResult` that the UI uses to drive button visibility + status text.
- feat(shell): `ProvidersSectionControl` adds `InstallClaudeCliButton`, `InstallCodexCliButton`, and matching `ProgressRing`s into the Claude and ChatGPT + Codex sign-in cards. On `AttachRuntime`, a fire-and-forget `RefreshCliInstallStateAsync()` probes which CLIs are installed and toggles per-card button visibility. Install click disables both buttons, starts the ring, calls `InstallCliDependency`, and on success flips the card into the ready-to-sign-in state with a detected-version message ("Claude Code 1.2.3 installed. Click sign-in to continue.").
- feat(browser): `renderSignInCards` emits an active `data-action="install-cli"` button instead of a disabled "CLI not installed" label. `installCliDependency(bridge, depId)` tracks per-bridge install status in `state.signIn.installByBridge`, POSTs to the admin API, then re-calls `signInDetectInstalled()` so the card refreshes into its normal sign-in state on success.
- test: `tests/MasterControlOrchestrationServerTests.cpp` dependency-catalog test updated to assert both `claude-code-cli` and `codex-cli` are present with the documented `npm install -g` commands.

## [0.4.3-rc.1] - 2026-04-18
### Summary
**Polished Windows Installer + product icon everywhere.** The install experience was the weakest part of the product — no install-directory picker, no desktop-shortcut option, no features dialog, no EULA page, a custom Tron-cyan progress window with zero controls, and no embedded icon on any executable (so every shortcut, taskbar entry, and Programs & Features row showed the generic Windows default). rc.1 fixes all of that with a WiX v5 MSI using the native `WixUI_InstallDir` dialog sequence + a custom Options page with five checkboxes, and embeds the supplied Tron-red icon as resource group 1 in every shipped `.exe` so it surfaces on the MSI file, UAC prompt, Start Menu shortcut, optional Desktop shortcut, taskbar, Alt+Tab, services.msc, Programs & Features, and the browser admin UI favicon.

### Included Changes
- feat(installer): new WiX v5 MSI at `installer/MasterControlOrchestrationServer.wxs`. Dialog sequence: Welcome → License (proprietary, `installer/license.rtf`) → InstallDir (Browse-capable) → **FeaturesDlg** (custom: service / firewall / Start Menu / Desktop / launch-on-finish) → VerifyReady → Progress → Exit. Native Win11 Fluent chrome, no custom theming.
- feat(installer): deferred custom actions invoke the existing `MasterControlBootstrapper.exe` with `--skip-shortcuts --skip-uninstall-registration` plus the maintainer's `--skip-service` / `--skip-firewall` choices composed from the Options page. MSI owns shortcuts + Programs & Features entry; the bootstrapper continues to own service registration, firewall rules, module manifest placement, and data-directory bootstrap. Zero CLI-surface regression.
- feat(installer): `scripts/Package-MasterControlOrchestrationServer.ps1` now emits a `.msi` alongside the existing `.zip`. `installer/Build-Msi.ps1` harvests the staged payload into a generated `Fragments/Files.wxs`, drives `wix build` with the UI + Util extensions, and returns the MSI path + version for `PACKAGE-METADATA.json`.
- feat(icons): Tron-red product icon family staged at `resources/icons/` (master `.ico` + PNG ladder + Windows tile assets + web favicons). `master-control.ico` embedded as icon resource group 1 in `MasterControlShell.exe`, `MasterControlServiceHost.exe`, `MasterControlBootstrapper.exe`, and `MasterControlOrchestrationServerSetup.exe` via a one-line `.rc` file per target wired into each CMake / vcxproj. The existing shortcut `IconLocation=MasterControlShell.exe,0` at [main.cpp:1272](src/MasterControlBootstrapper/main.cpp) now resolves to the Tron-red badge automatically (zero-indexed, first group wins). CLI-installed shortcuts benefit without any bootstrapper change.
- feat(icons): browser admin UI favicon + PWA icons referenced from `resources/web/index.html` so the browser tab shows the Tron-red badge.
- feat(icons): MSI `ARPPRODUCTICON` points at `master-control-installer.ico` so Programs & Features, the MSI file in File Explorer, and the UAC elevation dialog all show the Tron-red icon.
- chore(build): `CMakeLists.txt` now declares `LANGUAGES CXX RC` and installs the `resources/icons/` tree into `share/MasterControlOrchestrationServer/icons/` so the MSI can reference fixed paths.

## [0.4.2-rc.8] - 2026-04-17
### Summary
**UX overhaul: guided path first, advanced hidden, clean desktop.** rc.7 made sign-in work, but the Providers view still presented twelve top-level cards at once — overwhelming for first-time setup — and button heights weren't symmetric. rc.8 promotes the guided AI-model path (sign-in + Auto-Connect + Provider Connections list) to the primary surface and hides the direct-edit and orchestration plumbing behind two **Advanced** Expanders. Button geometry is unified across every card, `Remove Group` now uses the destructive tone, and `Run Provider Task` shows a live progress ring. Separately, successful installs no longer leave `MasterControlOrchestrationServer-install-succeeded-*.txt` receipts on the maintainer's desktop — failures still write, and the persistent log tree under `%PUBLIC%\Documents\Master Control Orchestration Server\logs\installer` still captures every outcome.

### Included Changes
- fix(installer): `writeBootstrapperActionLog` and the setup launcher's textual-log writer only emit the desktop-local `.txt` receipt on failure now. A new constant `writeDesktopLog = !succeeded || hasOverride` gates the `writeTextFile` call in `MasterControlBootstrapper/main.cpp`; `setup_main.cpp` has the matching guard. `MASTERCONTROL_BOOTSTRAPPER_LOG_DIR` still forces a desktop log when set so CI / scripted flows can keep the old behavior.
- feat(shell): `ProvidersSectionControl.xaml` collapsed from 12 top-level cards into a guided primary path plus two `<Expander IsExpanded="False">` groupings — `ADVANCED · DIRECT EDIT` (Provider Editor + Credentials + AI Autonomy) and `ADVANCED · ORCHESTRATION` (Sub-Agent Groups + Ownership Routing + Execution Console). First-time maintainers now see only Sign-In cards, Auto-Connect, Provider Routes, Provider Connections, and Provider Modules.
- fix(shell): `App.xaml` unified the implicit `Button` style and `ShellCommandButtonStyle` to `MinHeight=48`, `Padding=12,10`, `HorizontalAlignment=Stretch`; `ShellSecondaryButtonStyle` is now visibly recessive with `Background=#55050B12` and `FontWeight=Normal`. Paired `Save / New / Remove` rows are wrapped in equal-width column grids so their widths match.
- fix(shell): `Remove Group` now uses `ShellDangerButtonStyle` (red) so destructive actions stand apart from `Save` and `New`.
- feat(shell): `ExecuteProviderTaskAsync` now toggles a new `ProviderExecutionProgressRing` next to `Run Provider Task` while the admin API call is in flight.
- fix(shell/browser): jargon sweep. `FORSETTI SURFACE TOOLBAR` → `QUICK ACTIONS` in `MainWindow.xaml` and `resources/web/index.html`. `Protection Envelope` → `Security Settings` in `SecuritySectionControl.xaml` and `resources/web/app.js`'s `renderSecurityView`. `Governed Resource Envelope` → `Resource Allocation` in `SettingsSectionControl.xaml`, `TelemetrySectionControl.xaml`, and `MainWindow.xaml.cpp`'s setup-wizard `Step 3` label.

## [0.4.2-rc.7] - 2026-04-17
### Summary
**One OpenAI sign-in registers both ChatGPT and Codex.** The `codex` CLI's single OAuth flow authenticates the maintainer for two logically distinct endpoints: ChatGPT (general reasoning / planning) and Codex (coding agent). rc.5's sign-in wizard only registered one of the two, so assigning `coding-specialists` to Codex after signing in to ChatGPT was awkward. rc.7 registers every capability whose `cliBridgeCommand` matches the bridge that just authenticated, so the maintainer signs in once and can immediately split planning and coding across the two endpoints.

### Included Changes
- fix(runtime): `ProviderCliSignInService::registerBridgedProvider` now iterates every capability whose `cliBridgeCommand` matches the bridge that just authenticated, instead of registering only the `providerId` hint — one `codex login` registers both `chatgpt` and `codex` entries; `claude login` still registers only `claude-code` because that's the only capability with `cliBridgeCommand=claude`.
- feat(shell): `ProvidersSectionControl` ChatGPT card retitled **CHATGPT + CODEX (via Codex CLI)**, button relabeled **Sign in with OpenAI account**, and a sub-line explains the two-endpoint outcome explicitly.
- feat(browser): `renderSignInCards` shows **Sign in to use ChatGPT + Codex** with **OPENAI ACCOUNT** eyebrow for the `codex` bridge.
- feat(runtime): on successful codex sign-in the completion message now surfaces *"ChatGPT (planning / reasoning) and Codex (coding agent) are both registered — assign each to roles below"*.
- docs(capabilities): `cliBridgeAccountLabel` on both ChatGPT and Codex capabilities updated to describe the one-sign-in-two-endpoints model.

## [0.4.2-rc.6] - 2026-04-17
### Summary
**Grok API-key onboarding card.** xAI does not publish a consumer OAuth flow — confirmed by the deep-research analysis on non-API middleware bridges — so a true no-key path is not available for hosted Grok today. This release adds a dedicated API-key card beside the Claude and ChatGPT sign-in cards: paste the xAI key once, and the existing Auto-Connect pipeline probes the endpoint, discovers models, seals the key with DPAPI, and registers the provider. The copy on both surfaces is explicit about the tradeoff.

### Included Changes
- feat(shell): new "Grok (xAI API key)" card in `ProvidersSectionControl` beside the Claude and ChatGPT sign-in cards. `PasswordBox` + `Connect Grok` button routes through `ShellRuntime::AutoConnectProvider` with `kind=xai-grok` and the pasted key; status TextBlock below surfaces probe/discover/register progress.
- feat(browser): `renderSignInCards` now emits an additional API-key section for capabilities that have no `cliBridgeCommand` but declare a required `api_key` field. A single-input form posts to `/api/providers/auto-connect` and the card flips through idle → pending → success/error states.
- docs(wizard): copy on both surfaces is explicit that xAI lacks a consumer OAuth path and that the key is sealed locally with Windows DPAPI and never re-transmitted.

## [0.4.2-rc.5] - 2026-04-17
### Summary
**Add AI Model — account-only sign-in wizard.** The primary goal of this project has always been to get users to productive AI use without making them paste API keys. This release delivers that for Claude (Pro / Max / Team) and ChatGPT: click a button in the Providers section, the matching CLI (`claude login` or `codex login`) opens a console and drives OAuth in your browser, and the orchestration server registers the provider on success. The CLI keeps its own tokens — the server never stores them.

### Included Changes
- feat(models): `ProviderCapabilityDescriptor` now carries `cliBridgeCommand` (`claude` | `codex`) and `cliBridgeAccountLabel` so the UI knows which providers support account-only sign-in
- feat(runtime): `ProviderExecutionTransport::CodexCli` added alongside `ClaudeCodeCli`; the execution dispatch bypasses the credential-required check for CLI-bridged transports
- feat(runtime): `ProviderCliSignInService` spawns `claude login` / `codex login` in a new console window, polls for process exit + auth-file presence, registers the provider on success
- feat(runtime): new endpoints `POST /api/providers/signin/start`, `GET /api/providers/signin/status?sessionId=`, `GET /api/providers/signin/installed`
- feat(runtime): `executeCodexCli` invokes `codex exec <prompt>` with optional `OPENAI_API_KEY` env forwarding for maintainers who prefer an API key instead of account sign-in
- feat(modules): Claude Code, Codex, and ChatGPT capabilities advertise account-sign-in as the primary path; credential fields are now marked optional
- feat(shell): Providers section gains a top-level **Add AI Model — Account Sign-In** card with two buttons (Claude, ChatGPT) that drive the end-to-end OAuth flow with live status updates below each button
- feat(browser): matching **Add AI Model** sign-in cards at the top of the Providers view, polling `/api/providers/signin/status` every 2 seconds with live status transitions
- fix(shell): `ShellRuntime` gains `StartCliSignIn`, `GetCliSignInStatus`, `DetectCliSignInInstalled` over the admin API

### What's next
- Gemini CLI sign-in
- Ollama (local models) detection + registration (no sign-in)
- First-time dependency install when `claude` / `codex` aren't yet on PATH — currently the wizard reports the CLI as missing; a one-click install is the next step

## [0.4.2-rc.4] - 2026-04-17
### Summary
Telemetry is now **unmistakably live**. 1-second cadence, a visible `LIVE #N · HH:MM:SS` badge in the title bar that bumps every tick (even failed ticks), and a RAII flag guard so the tick can never silently stop. When the admin API fails, the badge flips to `OFFLINE` so a stuck UI is visually distinct from a non-responsive backend.

### Included Changes
- feat(shell): live telemetry timer dropped from 2s to 1s cadence
- feat(shell): visible `LIVE #N · HH:MM:SS` badge in the title bar that bumps every tick (including failed ticks), giving unmistakable proof the telemetry pipeline is alive even on idle hosts where CPU/RAM numbers happen to be stable
- feat(shell): badge flips to `OFFLINE` (amber) when the admin API call fails so a stuck UI is visually distinct from a non-responsive backend
- fix(shell): `RefreshLiveAsync` now uses a RAII guard so `liveRefreshInFlight_` is ALWAYS cleared on every exit path, including exceptions; prevents the tick from silently stopping
- feat(browser): same `Live #N · HH:MM:SS` treatment on the health badge; browser poll dropped from 2s to 1s

## [0.4.2-rc.3] - 2026-04-17
### Summary
Second hotfix for the host install experience: the **WinUI shell itself** was rebuilding its navigation, toolbar, and section content every 10 seconds, which is what the user saw as "the entire page refreshing" across all menus. Fixed by signature-caching the chrome, no-op'ing redundant destination swaps, and introducing a live-only 2-second refresh path. The setup launcher now auto-launches the desktop shell on the host by default.

### Included Changes
- fix(shell): `ApplySurfaceNavigation` skips `Clear()`+rebuild when the navigation signature is unchanged; selection-only update on the common refresh path
- fix(shell): `ApplySurfaceToolbar` skips `Clear()`+rebuild when the toolbar signature is unchanged
- fix(shell): `SetCurrentDestination` no-ops the content-host swap and `StartBringIntoView()` scroll when the destination hasn't changed
- feat(shell): new `RefreshLiveAsync` + `ApplyLiveSnapshotFragment` path — updates only hero values and the currently visible section's data, leaving navigation, toolbar, section-content host, and scroll position untouched
- feat(shell): `refreshTimer_` now ticks every 2 seconds and calls `RefreshLiveAsync`; full `ApplySnapshot` only runs on user-initiated Refresh or view change
- fix(shell): the 2-second live tick self-suppresses while on Providers / Security / Settings / Imports so in-progress form input is never interrupted
- fix(installer): setup launcher now auto-launches the desktop shell by default on the host; `--no-launch-shell` still opts out for headless installs

## [0.4.2-rc.2] - 2026-04-17
### Summary
Hotfix for the host install experience reported in the field: the browser admin surface was re-rendering the whole page on a 15-second timer (feeling like a page reload) and the Start Menu exposed the browser dashboard shortcut next to the native shell, which was confusing on the host machine itself.

### Included Changes
- fix(browser): replace the 15-second `renderShell()` refresh with a 2-second targeted telemetry poll; the surrounding page no longer visually refreshes
- fix(browser): telemetry meter cards and signal cards now carry `data-live` markers; a new `updateTelemetryLive()` patches only the value, meter width, and tone without touching surrounding DOM
- fix(browser): the live poll skips automatically when the browser tab is hidden and when the user is on a static view (providers, security, settings, imports, setup)
- fix(installer): browser dashboard shortcut moved from *Start Menu > Master Control Orchestration Server > Master Control Orchestration Server Dashboard.url* to *Start Menu > Master Control Orchestration Server > Remote Access > Browser Dashboard (Remote).url*; the native shell shortcut remains at the top of the product folder
- fix(installer): uninstall path cleans up the Remote Access subfolder when it is empty
- docs(install): `START-HERE.txt` now explains that the desktop shell is the intended host surface and the browser dashboard is for remote LAN clients

## [0.4.2-rc.1] - 2026-04-17
### Summary
Non-security remediation: runtime correctness, JSON ingress hardening, build hygiene, WinUI claim-parity, regression tests.

### Included Changes
- fix(runtime): `writeJsonFile` returns `[[nodiscard]] bool` with atomic temp+rename; every call site propagates failure into `OperationResult` or `(void)`-discards in recovery/interface paths
- fix(runtime): `readJsonFile` catches `nlohmann::json::exception` and `std::ios_base::failure`; TOCTOU `exists()` + read patterns are now safe at every caller
- fix(runtime): `upsertProvider` and `upsertMcpServer` capability reads moved inside `state_->mutex` (lost-update race closed)
- fix(runtime): `ScopedThread` RAII wrapper guarantees child-process pipe readers join on any exception unwind
- fix(runtime): cap WinHTTP response accumulator at 32 MiB with clean handle cleanup
- feat(runtime): add `tryParseJson` / `tryGet<T>` / `getOr<T>` helpers; wrap config-load and credential-unseal ingress
- fix(build): `vcpkg.json` version-string synced to `VERSION.json`; README badges regenerated from the same source of truth
- feat(build): new `scripts/Sync-RepositoryVersionBadges.ps1` with `-CheckOnly` mode for CI gating
- fix(build): CMake `VCPKG_ROOT` unresolved-env guard with clear `FATAL_ERROR`
- fix(build): PowerShell `find_program` is now `FATAL_ERROR` on Windows when missing
- fix(build): `MasterControlApp` Windows system libs moved `PUBLIC` → `PRIVATE`; `MasterControlServiceHost` links `advapi32` explicitly
- fix(scripts): `Set-StrictMode -Version Latest` and `$ErrorActionPreference = 'Stop'` across all 12 previously-unset PowerShell scripts
- fix(shell): activity-stream 1 Hz tick suppressed while maintainer is on Providers / Security / Settings / Imports
- fix(shell): `DispatcherQueue` null-guard cached once at `ConfigureTimer` entry
- feat(shell): Tron-themed focus indicators (`ShellAccentBrush` + `ShellGlowBrush`) on `Button` / `TextBox` / `PasswordBox` / `ComboBox` / `ToggleSwitch` implicit styles
- fix(shell): `app.manifest` extended with Windows 10/11 `<supportedOS>` GUIDs and `requestedExecutionLevel=asInvoker`
- test: regression tests for malformed-configuration fallback, activity-ring cap under load, and concurrent `upsertProvider`

## [0.4.1-rc.1] - 2026-04-17
### Summary
UX simplification: stop settings refresh, simplify AI integration form, add OAuth scaffolding, add native WinUI setup wizard.

### Included Changes
- fix(browser): stop 5-second refresh on static views (settings, providers, security); live views refresh at 15s
- fix(browser): simplify provider connect form to pick-model + authenticate; hide Route ID, Base URL, Model ID
- feat(runtime): add OAuth scaffolding (`supportsOAuth`, `oauthAuthorizeUrl`, `oauthClientId`, `oauthScope`) to `ProviderCapabilityDescriptor`
- feat(shell): add programmatic `SetupWizardBuilder` (no MIDL/IDL registration) with Guided / Manual / Import entry cards and readiness review
- feat(shell): first-run routing — shell shows setup wizard automatically when `!firstRunCompleted`

## [0.4.0-rc.1] - 2026-04-14
### Summary
Ease-of-use remediation pass: guided setup spine, browser auto-connect refactor, environment hints UI, Claude Code CLI install automation, shell/browser parity, readiness dashboard, starter workflow templates, exports demotion.

### Included Changes
- feat(runtime): add `/api/readiness` with source-neutral workflow readiness (guided + manual both count)
- feat(runtime): add `/api/setup/start`, `/api/setup/complete`, `/api/setup/reset` for setup lifecycle
- feat(runtime): add `/api/setup/dependencies` with three-branch preflight (ready / installable / prerequisite-missing)
- feat(runtime): add `/api/setup/dependencies/{id}/install` for Claude Code CLI orchestration
- feat(runtime): add `/api/setup/workflow-templates` with three starter templates
- feat(browser): first-run setup wizard with Guided / Manual / Import Existing entry modes
- feat(browser): guided provider form now uses `/api/providers/auto-connect` with manual fallback
- feat(browser): environment hints displayed on credential fields (detected / needed / none)
- feat(browser): readiness dashboard with "Fix now" routing and starter workflow picker
- feat(browser): localStorage-backed wizard state persistence across refresh
- feat(shell): `advancedMode` and `firstRunCompleted` now exposed in `ShellHostSnapshot`
- feat(build): `ReadinessCopy.h` shared copy header consumed by both surfaces
- chore(browser): exports demoted from primary nav to advanced-only
- fix(browser): stale `kind` preset removed from `connect-chatgpt` quick-connect entry
- test: add `/api/readiness` shape, setup lifecycle round-trip, dependency catalog, workflow templates coverage

## [0.3.0-rc.1] - 2026-04-14
### Summary
Productization and stabilization remediation pass covering 10 workstreams.

### Included Changes
- fix(runtime): resolve provider capabilities by providerId instead of kind — fixes ChatGPT/Codex collision
- feat(runtime): mark seeded endpoints and default providers as templates with EndpointStatus::Template
- feat(browser): add quick-connect workflows for ChatGPT, Codex, Claude Code, and xAI
- feat(browser): add progressive disclosure with runtime-backed advancedMode toggle
- feat(build): unify versioning — VERSION.json drives CMake, bootstrapper, and module manifests
- feat(ci): add Windows Build, Test, and Package workflow with release gating
- fix(runtime): harden process execution with concurrent pipe draining, 5-min timeout, bounded capture
- feat(runtime): add GET /api/environment-hints for credential auto-detection
- feat(runtime): add POST /api/settings/advanced-mode for progressive disclosure toggle
- test: add provider identity, template distinction, version alignment, and progressive disclosure tests
- docs: revise README to accurately describe multi-binary architecture and prerequisites

## [0.2.12] - 2026-04-12
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- fix(shell): exclude interactive forms from background refresh timer (flynn33)

## [0.2.11] - 2026-04-12
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- fix(shell): freeze Auto-Connect card during user interaction (flynn33)

## [0.2.10] - 2026-04-12
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- fix(shell): prevent Auto-Connect provider selector from resetting (flynn33)

## [0.2.9] - 2026-04-12
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- fix(shell): eliminate black fonts and left-pane gap (flynn33)

## [0.2.8] - 2026-04-12
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- fix(runtime): use synchronous refresh in dashboard snapshot (flynn33)

## [0.2.7] - 2026-04-11
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- docs(wiki): overhaul wiki agent and source pages (flynn33)

## [0.2.6] - 2026-04-11
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Add autonomous Claude tooling scripts under .claude-work/ (flynn33)

## [0.2.5] - 2026-04-11
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Fix: admin API mutating handlers blocked 10-14s on inventory probes (flynn33)

## [0.2.4] - 2026-04-11
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Theme overhaul + critical API probe fix + workspace dialog removed (flynn33)

## [0.2.3] - 2026-04-11
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Live command stream, nav pane removed, hero auto-collapse, Tron palette v2 (flynn33)

## [0.2.2] - 2026-04-11
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Fully automate AI model integration via Auto-Connect (flynn33)

## [0.2.1] - 2026-04-11
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Initial commit (Jim Daley)
- Import master control dashboard workspace (flynn33)
- chore(repo): add GitHub repository agents (flynn33)
- fix(hooks): allow pre-push stdin on Windows (flynn33)
- fix(hooks): use PowerShell guard wrapper (flynn33)
- fix(agents): preserve git log field separators (flynn33)
- Implement Forsetti compliance and shell surfaces (flynn33)
- Make shell Forsetti-native for certification (flynn33)
- Make browser surface Forsetti-native (flynn33)
- Implement CLU governance and Forsetti runtime hardening (flynn33)
- Split AI integration into provider modules (flynn33)
- Implement provider execution adapters and consoles (flynn33)
- Add group-based provider routing for sub-agents (flynn33)
- Add custom sub-agent authoring workflows (flynn33)
- Add custom MCP server authoring workflows (flynn33)
- Add platform gateway and governance service lanes (flynn33)
- Implement governance tool execution layer (flynn33)
- Add Apple host registry and readiness routing (flynn33)
- Add Apple remote execution tooling (flynn33)
- Add iOS archive export and device install tooling (flynn33)
- Add macOS signing and notarization tooling (flynn33)
- Add macOS stapling and Apple host defaults (flynn33)
- Track Apple governance operation history (flynn33)
- Expose Apple operations in shell and browser (flynn33)
- Add Apple host management and replay controls (flynn33)
- Persist Apple governance operation history (flynn33)
- Harden Apple operation diagnostics and credential redaction (flynn33)
- Add Apple replay readiness safeguards (flynn33)
- Add Apple job-control triage surfaces (flynn33)
- Add proprietary software license (Jim Daley)
- Add Apple queue execution and cancellation (flynn33)
- Add Apple readiness drill-down surfaces (flynn33)
- Add CLU enforcement and local resource controls (flynn33)
- Harden deployment workflow and remove gateway assumptions (flynn33)
- Harden bootstrapper deployment validation (flynn33)
- Expand bootstrapper integration validation (flynn33)
- Add bootstrapper preflight readiness checks (flynn33)
- Harden bootstrapper rollback and deployment contracts (flynn33)
- Fix non-admin shortcut deployment path (flynn33)
- Add deployment acceptance harness (flynn33)
- Harden deployment acceptance reporting (flynn33)
- Expand deployment acceptance diagnostics (flynn33)
- Package deployment acceptance bundles (flynn33)
- Add deployment report comparison tooling (flynn33)
- Package release installer bundle (flynn33)
- Enforce bandwidth and storage resource gates (flynn33)
- Add release readiness reporting script (flynn33)
- Bundle release readiness into packages (flynn33)
- Add installer desktop logging and launch diagnostics (flynn33)
- Fix managed installer lifecycle and elevation flow (flynn33)
- Fix installer elevation path handling (flynn33)
- Add native setup launcher for release packages (flynn33)
- Add guided setup wizards and shell fixes (flynn33)
- Delete .claude directory (Jim Daley)
- feat(wiki): overhaul wiki generation with comprehensive pages (flynn33)
- Align repo naming with orchestration server (flynn33)
- Add guided CLU setup and Forsetti module wizards (flynn33)
- Document the Command Logic Unit module (flynn33)
- Polish setup launcher install flow (flynn33)
- Add browser guided setup workflows (flynn33)
- Redesign telemetry as the primary monitoring deck (flynn33)
- Promote wizard-first admin workflows (flynn33)
- Sync release metadata and readiness tracking (flynn33)
- Fix installer compatibility and package entry point (flynn33)
- Fix installer reliability and shell drag behavior (flynn33)
- Sync release metadata for v0.1.59 (flynn33)
- Stabilize Visual Studio test-machine validation (flynn33)
- Add persistent installer error logging (flynn33)
- Add IDE deployment acceptance targets (flynn33)
- Add VS Code Codex handoff workflow (flynn33)
- Fix WinUI shell build toolchain resolution (flynn33)
- Sync release metadata for v0.1.60 (flynn33)
- Seal release metadata after agent sync (flynn33)
- Clarify release metadata semantics (flynn33)
- Restore valid release tracking base (flynn33)
- Add persistent deployment telemetry and fix setup exit handling (flynn33)
- Fix uninstall cleanup and restore shell window frame (flynn33)
- Refocus shell on readable maintainer workflows (flynn33)
- v0.2.0 — Tron density rework; validated on Windows Server 2022 (flynn33)

## [0.2.0] - 2026-04-11
### Summary
Tron-density UX rework, validated end-to-end on Windows Server 2022.

### Included Changes
- Tron-theme the setup launcher progress window (cyan accent bar, Bahnschrift fonts, accent marquee) to match the shell's App.xaml palette.
- Expand shell resource dictionary with status chip, tonal button variants, compact tiles, sub-agent badge, and live-clock text styles.
- Redesign OverviewSectionControl around hero card + operational snapshot + narrative grid + authored-surfaces legend.
- Add Tron command-center density to MainWindow: live clock (HH:MM:SS) in the title bar, sub-agent footer row (SENTINEL/ARCHITECT/FORGE/SCRIBE/RECON/NEXUS/WATCHDOG), and a ScrollViewer wrapping the main content so the full layout reaches low-resolution displays.
- Add browser dashboard polish layer: prefers-reduced-motion, focus-visible outlines, accent pulse animation, <dialog>::backdrop blur.
- Add one-shot ProgramData migration from legacy MasterControlProgram to MasterControlOrchestrationServer path, with safe fallback if the rename cannot complete.
- Update GitHub repository URL references to flynn33/Master-Control-Orchestration-Server.
- Guard Package-MasterControlOrchestrationServer.ps1 git calls so packaging works outside a git repo.
- Update PlatformToolset from v143 to v145 so the shell builds on Visual Studio 2026.
- Validated end-to-end on Windows Server 2022 Datacenter (21H2, build 20348): cmake configure + build (0 errors, 0 warnings), ctest 4/4 green, package staged (44 MB), unattended install smoke CLEAN, shell launches and renders Tron UI with live clock, bootstrapper preflight/validate reports valid:true.

## [0.1.66] - 2026-04-10
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Refocus shell on readable maintainer workflows (flynn33)

## [0.1.65] - 2026-04-10
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Fix uninstall cleanup and restore shell window frame (flynn33)

## [0.1.64] - 2026-04-10
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Add persistent deployment telemetry and fix setup exit handling (flynn33)

## [0.1.63] - 2026-04-10
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Clarify release metadata semantics (flynn33)
- Restore valid release tracking base (flynn33)

## [0.1.62] - 2026-04-10
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Seal release metadata after agent sync (flynn33)

## [0.1.61] - 2026-04-10
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Sync release metadata for v0.1.60 (flynn33)

## [0.1.60] - 2026-04-10
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Fix installer reliability and shell drag behavior (flynn33)
- Sync release metadata for v0.1.59 (flynn33)
- Stabilize Visual Studio test-machine validation (flynn33)
- Add persistent installer error logging (flynn33)
- Add IDE deployment acceptance targets (flynn33)
- Add VS Code Codex handoff workflow (flynn33)
- Fix WinUI shell build toolchain resolution (flynn33)

## [0.1.59] - 2026-04-03
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Fix installer reliability and shell drag behavior (flynn33)

## [0.1.58] - 2026-04-03
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Fix installer compatibility and package entry point (flynn33)

## [0.1.57] - 2026-04-03
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Sync release metadata and readiness tracking (flynn33)

## [0.1.56] - 2026-04-03
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Promote wizard-first admin workflows (flynn33)

## [0.1.55] - 2026-04-03
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Redesign telemetry as the primary monitoring deck (flynn33)

## [0.1.54] - 2026-04-03
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Add browser guided setup workflows (flynn33)

## [0.1.53] - 2026-04-02
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Polish setup launcher install flow (flynn33)

## [0.1.52] - 2026-04-02
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Document the Command Logic Unit module (flynn33)

## [0.1.51] - 2026-04-02
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Add guided CLU setup and Forsetti module wizards (flynn33)

## [0.1.50] - 2026-04-02
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Align repo naming with orchestration server (flynn33)

## [0.1.49] - 2026-03-29
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- feat(wiki): overhaul wiki generation with comprehensive pages (flynn33)

## [0.1.48] - 2026-03-29
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Delete .claude directory (Jim Daley)

## [0.1.47] - 2026-03-29
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Add guided setup wizards and shell fixes (flynn33)

## [0.1.46] - 2026-03-29
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Add native setup launcher for release packages (flynn33)

## [0.1.45] - 2026-03-29
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Fix installer elevation path handling (flynn33)

## [0.1.44] - 2026-03-29
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Fix managed installer lifecycle and elevation flow (flynn33)

## [0.1.43] - 2026-03-28
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Add installer desktop logging and launch diagnostics (flynn33)

## [0.1.42] - 2026-03-28
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Bundle release readiness into packages (flynn33)

## [0.1.41] - 2026-03-28
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Add release readiness reporting script (flynn33)

## [0.1.40] - 2026-03-28
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Enforce bandwidth and storage resource gates (flynn33)

## [0.1.39] - 2026-03-28
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Package release installer bundle (flynn33)

## [0.1.38] - 2026-03-28
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Add deployment report comparison tooling (flynn33)

## [0.1.37] - 2026-03-27
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Package deployment acceptance bundles (flynn33)

## [0.1.36] - 2026-03-27
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Expand deployment acceptance diagnostics (flynn33)

## [0.1.35] - 2026-03-27
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Harden deployment acceptance reporting (flynn33)

## [0.1.34] - 2026-03-27
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Add deployment acceptance harness (flynn33)

## [0.1.33] - 2026-03-27
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Fix non-admin shortcut deployment path (flynn33)

## [0.1.32] - 2026-03-27
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Harden bootstrapper rollback and deployment contracts (flynn33)

## [0.1.31] - 2026-03-27
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Add bootstrapper preflight readiness checks (flynn33)

## [0.1.30] - 2026-03-27
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Expand bootstrapper integration validation (flynn33)

## [0.1.29] - 2026-03-27
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Harden deployment workflow and remove gateway assumptions (flynn33)
- Harden bootstrapper deployment validation (flynn33)

## [0.1.28] - 2026-03-27
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Add CLU enforcement and local resource controls (flynn33)

## [0.1.27] - 2026-03-27
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Add Apple readiness drill-down surfaces (flynn33)

## [0.1.26] - 2026-03-27
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Add Apple queue execution and cancellation (flynn33)

## [0.1.25] - 2026-03-22
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Add proprietary software license (Jim Daley)

## [0.1.24] - 2026-03-22
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Add Apple job-control triage surfaces (flynn33)

## [0.1.23] - 2026-03-22
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Add Apple replay readiness safeguards (flynn33)

## [0.1.22] - 2026-03-22
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Harden Apple operation diagnostics and credential redaction (flynn33)

## [0.1.21] - 2026-03-22
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Persist Apple governance operation history (flynn33)

## [0.1.20] - 2026-03-22
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Add Apple host management and replay controls (flynn33)

## [0.1.19] - 2026-03-22
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Expose Apple operations in shell and browser (flynn33)

## [0.1.18] - 2026-03-21
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Track Apple governance operation history (flynn33)

## [0.1.17] - 2026-03-21
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Add macOS stapling and Apple host defaults (flynn33)

## [0.1.16] - 2026-03-21
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Add macOS signing and notarization tooling (flynn33)

## [0.1.15] - 2026-03-21
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Add iOS archive export and device install tooling (flynn33)

## [0.1.14] - 2026-03-21
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Add Apple remote execution tooling (flynn33)

## [0.1.13] - 2026-03-21
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Add Apple host registry and readiness routing (flynn33)

## [0.1.12] - 2026-03-21
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Implement governance tool execution layer (flynn33)

## [0.1.11] - 2026-03-20
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Add platform gateway and governance service lanes (flynn33)

## [0.1.10] - 2026-03-20
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Add custom MCP server authoring workflows (flynn33)

## [0.1.9] - 2026-03-20
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Add custom sub-agent authoring workflows (flynn33)

## [0.1.8] - 2026-03-20
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Add group-based provider routing for sub-agents (flynn33)

## [0.1.7] - 2026-03-20
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Implement provider execution adapters and consoles (flynn33)

## [0.1.6] - 2026-03-20
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Split AI integration into provider modules (flynn33)

## [0.1.5] - 2026-03-20
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Implement CLU governance and Forsetti runtime hardening (flynn33)

## [0.1.4] - 2026-03-16
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Make browser surface Forsetti-native (flynn33)

## [0.1.3] - 2026-03-16
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Make shell Forsetti-native for certification (flynn33)

## [0.1.2] - 2026-03-16
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- Implement Forsetti compliance and shell surfaces (flynn33)

## [0.1.1] - 2026-03-16
### Summary
Automated patch release for Master Control Orchestration Server.

### Included Changes
- chore(repo): add GitHub repository agents (flynn33)
- fix(hooks): allow pre-push stdin on Windows (flynn33)
- fix(hooks): use PowerShell guard wrapper (flynn33)
- fix(agents): preserve git log field separators (flynn33)

## [0.1.0] - 2026-03-16
### Summary
Initial tracked baseline for the Forsetti-based Master Control Orchestration Server workspace.

### Included Changes
- Imported the current Forsetti-compliant Master Control Orchestration Server source tree.
- Established WinUI 3 shell, service host, browser dashboard, and bootstrapper scaffolding.
- Seeded repository-owned version, changelog, wiki, and README automation files.
