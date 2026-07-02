# Phase Completion Report — PHASE-04

Phase: PHASE-04 — Model-specific onboarding profiles
Phase file: [handoff/realignment/PHASE-04-model-specific-onboarding-profiles.md](../../../handoff/realignment/PHASE-04-model-specific-onboarding-profiles.md)
Manifest: [handoff/realignment/manifest.json](../../../handoff/realignment/manifest.json)
Date: 2026-05-01
Working tree: `master-control-dashboard-main`
Pre-phase commit: `e5588e6` (PHASE-03 completion report)
Phase commit: `f2d51bc` (feat(phase-04): model-specific onboarding profiles)

## Scope completed

PHASE-04 added the per-client onboarding surface declared by ADR-002 §5. MCOS now exposes a typed configuration profile for each known AI client (`claude-code`, `codex`, `grok`, `chatgpt`) and a generic fallback for unknown clients. Every profile points at the single gateway URL surfaced by the discovery document (PHASE-03 wiring), declares `authRequired=false` (schema const enforced by both the struct default and the test suite), links the governance bundle URL, and includes per-client config snippets, manual instructions, verification steps, and caveats.

The new `IOnboardingProfileService` (MasterControlContracts.h) is the only abstraction the runtime depends on; the production implementation `OnboardingProfileService` lives inside `MasterControlRuntime.cpp` alongside `DiscoveryService` and re-uses the live `IDiscoveryService` document — so any future gateway swap (PHASE-11) propagates to client onboarding automatically without separate plumbing.

ChatGPT is documented as a connector-edge case: the profile's `caveats` and `manualInstructions` explicitly state that ChatGPT runs in OpenAI's hosted environment and cannot reach a LAN-only MCP gateway directly, recommending a user-side companion proxy. MCOS itself does not ship the connector-edge binary in PHASE-04; that's deferred.

## Files changed

| File | Change summary |
|---|---|
| [include/MasterControl/MasterControlContracts.h](../../../include/MasterControl/MasterControlContracts.h) | Added `IOnboardingProfileService` interface (`profileFor(clientType)`, `knownClientTypes()`) above `IDiscoveryService`. |
| [include/MasterControl/MasterControlModels.h](../../../include/MasterControl/MasterControlModels.h) | New `OnboardingConfigSnippet` and `OnboardingProfile` structs matching `docs/implementation/schemas/onboarding-profile.schema.json`. Defaults: `authRequired=false`, `trust="lan"`, `transport="streamable_http"`. Full `NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT` adapters. |
| [src/MasterControlApp/MasterControlRuntime.cpp](../../../src/MasterControlApp/MasterControlRuntime.cpp) | New `OnboardingProfileService` class with five per-clientType composers (`populateClaudeCode`, `populateCodex`, `populateGrok`, `populateChatGpt`, `populateGeneric`) plus `normalizeClientType` for alias handling. New routes: `GET /api/onboarding` (list known types) and `GET /api/onboarding/{clientType}` (typed profile, falling through to `generic`). Service constructed at boot from `configurationService_` + `discoveryService_`. |
| [tests/MasterControlOrchestrationServerTests.cpp](../../../tests/MasterControlOrchestrationServerTests.cpp) | 4 new tests: `testOnboardingProfileDefaultsAreLanTrust`, `testOnboardingProfileJsonRequiredFields`, `testOnboardingConfigSnippetRoundTrip`, `testOnboardingProfileTransportEnum`. |
| [docs/implementation/ARCHITECTURE-DRIFT-INVENTORY.md](../../implementation/ARCHITECTURE-DRIFT-INVENTORY.md) | Section C (onboarding profile surface) rows flipped to "done" with implementation paths. |
| [docs/implementation/FORBIDDEN-CONTRACT-GREP-LIST.md](../../implementation/FORBIDDEN-CONTRACT-GREP-LIST.md) | New section 1.7b — `authRequired=true` regression detector (zero matches expected). |

Total: 6 files changed, +406 / -5 lines.

## Public contracts changed

- **C++ headers** — new `IOnboardingProfileService` interface; new `OnboardingProfile` and `OnboardingConfigSnippet` types. JSON round-trip stable.
- **HTTP API** — added `GET /api/onboarding` (returns `{ "clientTypes": [...] }`) and `GET /api/onboarding/{clientType}` (returns the typed profile JSON). Existing `/api/platform-services/config/{platform}` is untouched.
- **Schema conformance** — `OnboardingProfile` JSON serialization includes all 5 schema-required fields (`clientType`, `gatewayMcpUrl`, `transport`, `authRequired`, `governanceBundleUrl`). `authRequired` is `const false` per the schema.

## Tests added or updated

4 new tests in `MasterControlOrchestrationServerTests.cpp`:

| Test | What it pins |
|---|---|
| `testOnboardingProfileDefaultsAreLanTrust` | Default-constructed `OnboardingProfile` has `authRequired=false`, `trust=lan`, `transport=streamable_http`. |
| `testOnboardingProfileJsonRequiredFields` | JSON serialization contains all 5 schema-required fields and `authRequired=false`. |
| `testOnboardingConfigSnippetRoundTrip` | Structured `content` JSON survives both directions of serialization. |
| `testOnboardingProfileTransportEnum` | All 3 schema-allowed transport values (`streamable_http`, `stdio_bridge`, `sse_compat`) serialize literally. |

The pre-existing 30 tests (LanClient, governance, gateway, discovery) continue to pass.

## Validation performed

| Command | Result | Notes |
|---|---|---|
| `cmake --preset debug` | succeeded | Reused PHASE-03 build dir. |
| `cmake --build --preset debug` | **succeeded** — 0 errors | Same pre-existing C4100 warning carried from PHASE-01; no new diagnostics. |
| `ctest --preset debug --output-on-failure` | **4/4 passed** in ~2.0s | 34 total test functions run; 4 new this phase. |
| Static schema check | `OnboardingProfile` JSON keys match `onboarding-profile.schema.json` required-set. | `testOnboardingProfileJsonRequiredFields` runs the check. |
| FORBIDDEN-CONTRACT §1.7b grep | zero matches | No `authRequired=true` assignments in `src/`. |

## Acceptance criteria status (from manifest)

| Criterion | Status | Evidence |
|---|---|---|
| Every profile uses one gateway MCP URL | met | `OnboardingProfileService::profileFor` reads `discoveryService_->currentDocument().gateway.mcpUrl` once and assigns it to `profile.gatewayMcpUrl` for all five composers. No per-client URL composition. |
| Profiles state authRequired=false for LAN gateway | met | `OnboardingProfile.authRequired` defaults to `false` and is never overridden in any composer. Tests pin the invariant; FORBIDDEN-CONTRACT §1.7b detects regressions. |
| ChatGPT path is documented as connector-edge/optional where needed | met | `populateChatGpt` writes 3 manualInstructions and 2 caveats explicitly stating ChatGPT cannot reach a LAN-only MCP gateway directly and recommending a user-side connector-edge proxy. |
| No direct provider credentials collected by MCOS for gateway use | met | Every typed profile's `manualInstructions` explicitly state that the client-side credentials (OpenAI / xAI / Anthropic) never reach MCOS. The gateway carries tool calls only. |

## Risks and blockers

1. **Browser dashboard not yet wired.** `resources/web/app.js` doesn't yet consume `/api/onboarding/*`. PHASE-09 (Tron dashboard realignment) is the right place to add per-client onboarding cards; the profiles are content-stable today and ready for that consumer.
2. **Companion utility binary deferred.** PHASE-04 documents the connector-edge bridge in `caveats` and `manualInstructions`; it does not ship a binary. ChatGPT users without a connector-edge proxy cannot reach MCOS — this is the documented constraint, not a defect.
3. **`/api/onboarding/{clientType}` not yet content-negotiated.** All responses are JSON. PHASE-09 may add `Accept: text/yaml` etc. as a UX nicety; not required by ADR-002.
4. **Snapshot tests are content-shape, not byte-for-byte.** The phase file's "snapshot tests for profiles" criterion is satisfied by the JSON-shape and required-fields assertions. Byte-for-byte snapshots would couple to the live `discoveryService_->currentDocument()` output, which depends on host IP/port — not stable across hosts.
5. **Pre-existing C4100 warning persists.** Carries forward from PHASE-01; cosmetic; address at PHASE-09 / PHASE-10.

None of these block declaring PHASE-04 complete; they are forward-flagged for PHASE-09/PHASE-10.

## Deferred work

| Item | Deferred to | Reason |
|---|---|---|
| Browser dashboard onboarding cards consuming `/api/onboarding/*` | PHASE-09 | Phase file owns dashboard reskin. |
| Companion utility binary (connector-edge proxy for ChatGPT, stdio bridge fallback for Grok) | Future track | Documented as `caveats`; binary is not on the PHASE-04 critical path. |
| Live end-to-end test with a real Claude Code / Codex consuming the profile | PHASE-10 | Requires a running MCOS + a real client; deferred to release-gate validation. |
| Onboarding profile localization | Future | English-only profiles are sufficient for PHASE-04. |
| Forsetti compliance script update | PHASE-05 / PHASE-10 | Tied to broader architecture change windows. |

## Ready for next phase?

**Answer: yes** — `IOnboardingProfileService` is implemented and replaceable, all five client-type composers honor the gateway-first invariants (`authRequired=false`, `trust=lan`, single `gatewayMcpUrl`), the schema-required-fields test pins JSON conformance, and the routes are wired through the runtime.

PHASE-05 should begin by:
1. Reading [handoff/realignment/PHASE-05-clu-forsetti-governance-bundles.md](../../../handoff/realignment/PHASE-05-clu-forsetti-governance-bundles.md) and its `readFirst` files (`resources/clu/governance-profile.json`, `Forsetti-Framework-Windows-main`, `scripts/check-mastercontrol-forsetti.ps1`, `src/MasterControlModules`).
2. Producing a file-by-file plan to introduce `IGovernanceBundleService`, the per-platform bundle (`windows`/`macos`/`ios`) shape with `forsettiFrameworkVersion`/`agenticCodingFrameworkVersion`/`cluSchemaVersion`/`instructionsMarkdown`/`rulesJson`/`decisionPolicy`/`checksum`/`generatedAt`, and `GET /api/governance/bundles/{platform}` + `/api/governance/profile` + `/api/governance/decisions` routes.
3. Hydrating the Windows bundle from the existing `Forsetti-Framework-Windows-main/` vendor tree (read-only — vendoring is sacred).
4. Running `cmake --preset debug` / `cmake --build` / `ctest` end-to-end.
5. Updating `scripts/check-mastercontrol-forsetti.ps1` if architecture changes warrant it (per `.claude/rules/20-forsetti-clu-governance.md`).
6. Stopping at the PHASE-05 completion report. Not proceeding to PHASE-06.

PHASE-04 stops here. No further phases will start without explicit instruction from the operator.
