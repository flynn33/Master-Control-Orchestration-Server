## Gut and Rebuild - LAN Client Access, Shared Fabric, CLU Governance

### Context

The product owner has declared the current AI integration layer out of scope and wrong in direction. The stated product intent is:

1. Multiple AI coding models (Claude Code, Codex, Grok, and any future model) run on their own machines on the LAN and connect to MCOS as external clients.
2. MCOS issues each model a configuration file that describes how to connect, authenticate, and operate.
3. Privileges are enforced by MCOS, not negotiated by the client. Each client's privilege set (up to and including autonomous mode) is authored in a settings surface on the server.
4. All MCP servers and all sub-agents registered with MCOS are usable by every authenticated LAN client. Creation, modification, and removal of those resources are gated by per-client privileges.
5. CLU is the governance enforcer. The governance principles come from the Forsetti Framework and Forsetti Framework for Agentic Coding; CLU's job is to apply them.
6. The current `provider-integration` stack (umbrella module plus the three vendor modules for Codex, Claude Code, and xAI) is the wrong abstraction. It is to be removed, not demoted or hidden behind a feature flag.

This plan executes that intent as a clean subtraction of the provider stack followed by an additive build of the LAN-client model. It is more aggressive than the earlier strangler-fig draft in `00-lan-control-plane-remediation.md`, which is now marked superseded.

### What is removed

The entire AI integration stack, as currently defined in the source tree, is deleted outright:

- `ProviderIntegrationModule`, `CodexProviderModule`, `ClaudeCodeProviderModule`, `XAIProviderModule` (module classes, manifests, registrations).
- Provider data types: `ProviderConnection`, `ProviderCapabilityDescriptor`, `ProviderAssignment`, `ProviderAssignmentTarget`, `ProviderAssignmentTargetKind`, `ProviderKind`, `ProviderExecutionTransport`, `ProviderExecutionRegistration`, `ProviderExecutionRequest`, `ProviderExecutionRecord`, `AutoConnectStep`, `AutoConnectResult`.
- Provider runtime services: provider registry, provider credential store, provider auto-connect pipeline, provider assignment service, provider execution service, provider execution history.
- All provider HTTP routes: `GET/POST /api/providers`, `/api/providers/auto-connect`, `/api/providers/credentials`, `/api/providers/assignments`, `/api/providers/groups`, `/api/providers/execute`, `/api/provider-assignment-targets`.
- Provider CLI transports: `executeClaudeCodeCli`, `executeCodexCli`, `executeOpenAICompatibleChat`. MCOS does not make outbound AI calls in the new design. Models call MCOS, not the other way around.
- Provider browser surface (`Providers` destination in `resources/web/app.js`).
- Provider shell surface (`src/MasterControlShell/ProvidersSectionControl.xaml` and code-behind). Scheduled for deletion in Phase 2; optionally deferred to a follow-on shell track only if the owner wants the shell updated in lockstep.
- Provider documentation: `docs/wiki/Auto-Connect-AI.md`, provider sections of `docs/wiki/Architecture.md`, `docs/wiki/API-Reference.md`, `docs/wiki/Remote-Client.md`, `docs/wiki/CLU-Governance.md`.
- Provider-oriented proof files: `plans/PROOF-OF-WORKING/02-auto-connect.md`, `11-ai-task-execution.md` (kept in git history; removed from `plans/`).

### What is kept

- Forsetti framework and module manager. Nothing in `Forsetti-Framework-Windows-main/` changes.
- CLU module, governance profile structure, enforcement plumbing. The action enum and the profile rules are expanded, not replaced.
- Configuration service and persisted `AppConfiguration`.
- Runtime inventory service, MCP server catalog, sub-agent catalog, sub-agent group service.
- Beacon and LAN discovery service.
- Export registry and artifact generation machinery. Reused for the new client configuration bundle.
- Activity ring buffer and telemetry pipeline.
- Platform gateways for Windows, macOS, iOS. They are not in the AI integration layer and remain as-is.
- Browser admin UI shell, surface navigation, and styling. Existing destinations for Runtime, Telemetry, Install, Platform Services stay; the Providers destination is replaced with new LAN Client surfaces.

### Guiding principles

- Subtraction first, then addition. Phase 2 removes the provider stack before any new code lands. The product runs in a deliberately reduced state between Phase 2 and Phase 6, during which time AI models cannot connect. This is acceptable because no production LAN clients exist today; the current AI path is the one we are discarding.
- Every new module lands behind a clean interface (`ILanClientAccessService`, `IPrivilegeService`, `IAuthenticatedRequestContextResolver`) to preserve modularity and testability.
- The shared fabric rule is absolute: every authenticated client can use every MCP server and every sub-agent. Use is never privilege-gated. Creation, modification, and removal are privilege-gated.
- Privileges are flat booleans, not a profile system. There are no "reader" or "builder" profiles; each client carries its own set of toggles. Operators can copy settings between clients through the UI.
- CLU is the gate for every privileged mutation. Enforcement is central, not per-route.
- Existing wiki, test, and doc files that reference the removed stack are updated or deleted in the same phase that removes the underlying code. No stale documentation ships.

### Known constraints on my execution

- I can write C++20, CMake, JSON manifests, vanilla JS, and wiki docs. I cannot run the Windows build on this machine. Each phase ends with an explicit compile-readiness checkpoint for you to verify before we move on.
- Shell XAML updates (`ProvidersSectionControl.xaml`, replacement `LanClientsSectionControl.xaml`) are in Phase 2 and Phase 8 respectively. If you want the shell changes deferred to a separate track, flag it in open decisions and I will remove the XAML work from those phases.
- Each phase is sized to fit in a single focused work session. If a phase grows mid-flight I will split it explicitly rather than rush.

---

## Phase 1 - Architecture decision record and module inventory

**Goal.** Lock in the direction before any code moves. Capture the Forsetti-governed LAN client control plane as the sole architectural target.

**Code changes.**
- New file `docs/wiki/ADR-001-lan-client-control-plane.md` stating the decision, the Forsetti governance alignment, and the explicit rejection of embedded provider runtimes.
- Update `docs/wiki/Architecture.md` and `docs/wiki/Home.md` to point at the ADR and drop the provider references.
- New file `plans/remediation/02-removal-inventory.md` listing every file, symbol, route, manifest, test, and doc scheduled for deletion in Phase 2. This is the subtraction checklist. I will generate it by a clean pass over the source tree.

**Verification.**
- The ADR and the removal inventory are reviewed and approved before Phase 2 runs. No runtime verification at this stage.

**What you can do after this phase.** Nothing new operationally. This phase exists so the subtraction is deliberate, not exploratory.

---

## Phase 2 - Remove the AI integration stack

**Goal.** Delete the provider stack end to end. The product builds, runs, and passes tests without any AI integration code. No LAN client support exists yet either; the product is intentionally reduced.

**Code changes.**
- Delete module classes: `ProviderIntegrationModule`, `CodexProviderModule`, `ClaudeCodeProviderModule`, `XAIProviderModule` from `src/MasterControlModules/MasterControlModules.cpp` and `include/MasterControl/MasterControlModules.h`.
- Delete their manifests under `src/MasterControlModules/Resources/ForsettiManifests/`.
- Delete the provider data type declarations from `include/MasterControl/MasterControlModels.h` (and the matching `to_json` / `from_json` definitions in its `.cpp`).
- Delete the provider services from the runtime: provider registry, credential store, auto-connect, assignment service, execution service, execution history. Runtime container wiring at `src/MasterControlApp/MasterControlRuntime.cpp:10242-10255` loses those lines.
- Remove provider routes from the HTTP handler table (around `MasterControlRuntime.cpp:11409-11415` and surrounding blocks).
- Remove `com.mastercontrol.provider-integration` and the three vendor module ids from both the protected set at `src/MasterControlApp/MasterControlRuntime.cpp:1691-1699` and the default activation list at `src/MasterControlApp/MasterControlRuntime.cpp:10292-10313`. The default activation list drops from 19 modules to 15.
- Delete the `Providers` destination in `resources/web/app.js` including its forms, render functions, and navigation entries. Keep the navigation skeleton and landing hero; the freed slot stays empty until Phase 8.
- Delete `src/MasterControlShell/ProvidersSectionControl.xaml`, its code-behind, and remove it from `MasterControlShell.csproj` and any view-locator registration. (If the owner wants to defer shell changes, see open decision 1.)
- Delete or rewrite tests in `tests/MasterControlOrchestrationServerTests.cpp` that exercise provider flows. All tests must pass on a clean build.
- Delete provider wiki pages and update surrounding docs: remove provider subsections from `Architecture.md`, `API-Reference.md`, `Remote-Client.md`, `CLU-Governance.md`. Delete `Auto-Connect-AI.md`.
- Remove `plans/PROOF-OF-WORKING/02-auto-connect.md` and `11-ai-task-execution.md`. Update `plans/PROOF-OF-WORKING.md` and the README in that folder so the link set matches.
- Update `docs/wiki/Sub-Agents.md` to drop provider-centric framing; the roster stays but the invocation language is removed until Phase 8.
- `CHANGELOG.md` gets a new major-remediation entry.

**Data model consequences.**
- `AppConfiguration` loses `providers`, `providerAssignments`, `providerCredentials`, `providerExecutionHistory`. Persisted configs from older installs need a migration read path that strips unknown fields gracefully. `nlohmann::json` with default-tolerant parsing handles this as long as the `from_json` visitor skips missing keys; add an explicit migrator function in the configuration service to clear removed fields and log the migration to the activity ring.

**Verification.**
- Clean build from CMake Debug and Release presets succeeds.
- `ctest` passes.
- `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\check-mastercontrol-forsetti.ps1` passes.
- Service starts. `/api/health` returns `{status:"ok"}`. `/api/forsetti/modules` lists 15 modules, all active, none marked `protectedModule: true` that were not in the kept set.
- `GET /api/providers` returns 404. `POST /api/providers/execute` returns 404.
- Browser dashboard loads without the Providers destination.
- Existing installs receive a warning banner on the dashboard: "AI integration has been removed in this version. LAN client support is being added in subsequent phases."

**What you can do after this phase.** Nothing AI-related. The product is deliberately in a trough until Phase 6 lands. This is the contract of the rebuild.

---

## Phase 3 - LAN client identity

**Goal.** Introduce the new first-class identity. Persist clients, mint bearer tokens, track liveness. Still no enforcement, still no connection path for real models.

**Code changes.**
- New header `include/MasterControl/LanClient.h`:
  ```cpp
  struct LanClient {
      std::string clientId;              // operator-authored, slug form
      std::string displayName;
      std::string clientType;            // free-form, e.g. "claude_code", "codex", "grok"
      std::string hostName;              // informational
      std::string networkAddress;        // informational, last observed
      bool enabled = true;
      LanClientPrivileges privileges{};  // defined in Phase 4
      bool autonomousMode = false;       // defined in Phase 4
      std::string createdAtUtc;
      std::string lastSeenUtc;
      std::string disabledAtUtc;
  };

  struct LanClientToken {
      std::string tokenId;
      std::string clientId;
      std::string tokenHash;             // salted SHA-256
      std::string issuedAtUtc;
      std::string expiresAtUtc;          // optional; empty means non-expiring
      bool revoked = false;
      std::string revokedAtUtc;
  };
  ```
- New interface `include/MasterControl/ILanClientAccessService.h`:
  - `listClients()`
  - `getClient(clientId)`
  - `upsertClient(LanClient)`
  - `disableClient(clientId)`, `enableClient(clientId)`, `removeClient(clientId)`
  - `issueToken(clientId)` returns one-shot plaintext token plus stored `LanClientToken`
  - `rotateToken(clientId)` revokes existing and issues new
  - `validateToken(plaintext)` returns resolved `LanClient` when valid
- Implementation in `src/MasterControlApp/LanClientAccessService.cpp`. Token storage uses the existing DPAPI pattern from the deleted credential store; adapt the helper functions into `src/MasterControlApp/DpapiSecrets.cpp` (new shared utility) so they are not specific to providers.
- New Forsetti module `LanClientAccessModule` in `src/MasterControlModules/MasterControlModules.cpp` with manifest at `src/MasterControlModules/Resources/ForsettiManifests/LanClientAccessModule.json`. Not protected. Added to the default activation list (now 16 modules).
- New admin API routes in `MasterControlRuntime.cpp`:
  - `GET /api/clients` (list)
  - `GET /api/clients/{id}` (single, includes never-returned token fields)
  - `POST /api/clients` (create or update metadata; cannot set privileges yet until Phase 4)
  - `POST /api/clients/{id}/disable`
  - `POST /api/clients/{id}/enable`
  - `POST /api/clients/{id}/rotate-token` (returns one-shot plaintext)
  - `DELETE /api/clients/{id}` (refuses if client has active sessions in future phases; for now always succeeds)
- `AppConfiguration` gains `std::vector<LanClient> lanClients`. The token store persists separately under DPAPI to keep `GET /api/config` clean.
- Activity ring emits new events for client-created, client-disabled, client-enabled, client-removed, token-rotated.

**Verification.**
- Register a client: `curl -X POST localhost:7300/api/clients -d '{"clientId":"claude-code-jdaley-wks","displayName":"Claude Code on Jdaley workstation","clientType":"claude_code","hostName":"PC-GAMING-R7-58"}'` returns succeeded.
- Issue a token: `POST /api/clients/claude-code-jdaley-wks/rotate-token` returns a one-shot bearer string.
- `GET /api/clients` lists the client without tokens.
- Restart service. Client and token hashes persist.
- Unit test: `validateToken` resolves the correct client for a live token, rejects revoked and expired tokens.

**What you can do after this phase.** Register clients and mint tokens from curl or a temporary browser form. The token has no runtime effect yet.

---

## Phase 4 - Privilege model

**Goal.** Define the flat privilege struct. Seed defaults. Expose the settings surface at the API level.

**Code changes.**
- New struct in `include/MasterControl/LanClient.h`:
  ```cpp
  struct LanClientPrivileges {
      bool canCreateMcpServers = false;
      bool canModifyMcpServers = false;
      bool canRemoveMcpServers = false;
      bool canCreateSubAgents = false;
      bool canModifySubAgents = false;
      bool canRemoveSubAgents = false;
      bool canManageClients = false;        // register and modify other LAN clients
      bool canManageModules = false;        // enable and disable Forsetti modules
      bool canChangeGovernancePolicy = false; // edit CLU governance profile
  };
  ```
- `LanClient` gains `privileges` (default all-false, meaning a newly registered client can use MCP servers and sub-agents but cannot modify anything) and `autonomousMode` (default false).
- Use is deliberately not a privilege. Any authenticated client can use every MCP server and every sub-agent. That is the shared fabric rule, encoded by absence of a toggle rather than by a toggle that is always true.
- New routes under the same `ILanClientAccessService`:
  - `POST /api/clients/{id}/privileges` (operator sets the privilege struct)
  - `POST /api/clients/{id}/autonomous-mode` (operator toggles autonomous mode)
- Validation: setting `autonomousMode = true` is only allowed when CLU has been extended with the `AutonomousModeEnable` action kind and the global `AppConfiguration::aiAutonomyEnabled` flag is true. Until Phase 7 lands, attempting to set it returns a 409 with message "Autonomous mode cannot be enabled until CLU governance expansion ships."
- Activity events: client-privileges-changed, client-autonomous-mode-changed.

**Verification.**
- `POST /api/clients/claude-code-jdaley-wks/privileges -d '{"canCreateMcpServers":true,"canCreateSubAgents":true}'` returns succeeded; `GET /api/clients/claude-code-jdaley-wks` shows the new values.
- Attempting `autonomousMode = true` returns 409 with the documented message.
- Unit test: privilege changes persist across restart.

**What you can do after this phase.** Model which clients can do what. Still no enforcement wired up, so the privileges are advisory metadata.

---

## Phase 5 - Client configuration bundle

**Goal.** Produce a server-authored file that a LAN client drops onto its host to know how to talk to MCOS. This is the "configuration file with instructions" the product owner specified.

**Shape of the bundle.** Emitted by `GET /api/clients/{id}/config`. Includes:
```json
{
  "schemaVersion": "1.0",
  "issuedAtUtc": "2026-04-24T18:00:00Z",
  "mcosServer": "http://192.168.1.10:7300",
  "clientId": "claude-code-jdaley-wks",
  "displayName": "Claude Code on Jdaley workstation",
  "auth": {
    "mode": "bearer_token",
    "token": "<one-shot plaintext from paired rotate-token call>",
    "headerName": "Authorization",
    "headerValue": "Bearer <token>"
  },
  "privileges": {
    "canCreateMcpServers": true,
    "canModifyMcpServers": false,
    "canRemoveMcpServers": false,
    "canCreateSubAgents": true,
    "canModifySubAgents": false,
    "canRemoveSubAgents": false,
    "canManageClients": false,
    "canManageModules": false,
    "canChangeGovernancePolicy": false
  },
  "autonomousMode": false,
  "catalogs": {
    "mcpServers": "/api/client/mcp-servers",
    "subAgents": "/api/client/sub-agents",
    "activity": "/api/client/activity"
  },
  "governance": {
    "authority": "CLU",
    "framework": "Forsetti Framework for Agentic Coding",
    "profileEndpoint": "/api/client/governance/profile",
    "decisionEndpoint": "/api/client/governance/decisions"
  },
  "rules": [
    "All MCP servers registered with MCOS are available for use by every authenticated LAN client.",
    "All sub-agents registered with MCOS are available for use by every authenticated LAN client.",
    "Creation, modification, and removal of MCP servers and sub-agents are governed by the privileges listed above.",
    "Autonomous actions require autonomousMode=true on this client and are subject to CLU governance.",
    "Every privileged action is recorded in the MCOS activity stream and evaluated by CLU."
  ],
  "instructions": {
    "heartbeat": "POST /api/client/heartbeat at least every 60 seconds to remain in the live roster.",
    "discovery": "Use /api/client/mcp-servers and /api/client/sub-agents to discover the current shared fabric.",
    "invocation": "MCP servers and sub-agents are addressed directly using the endpoint metadata in each catalog entry.",
    "governance": "Before any privileged mutation, GET the governance profile and pre-check with POST to decisionEndpoint if needed."
  }
}
```

**Code changes.**
- New endpoint `GET /api/clients/{id}/config`. Under the hood it rotates the token, composes the bundle, emits the bundle as the response body, and stages the token record in the DPAPI store. The plaintext token exists only for the duration of the response.
- Address resolution: when `AppConfiguration::bindAddress == "0.0.0.0"` the `mcosServer` field uses `hostName` (preferred) or `preferredBindAddress`. Never serve `0.0.0.0` to the client.
- New export artifact kind `lan-client-config` registered with the export machinery at `src/MasterControlApp/MasterControlRuntime.cpp:5570-5671` so the bundle is discoverable from `/api/exports` and the Export tab.
- Activity event: client-config-issued.

**Verification.**
- Register a client, set privileges, hit `GET /api/clients/{id}/config`. Save the response. The file is a usable JSON with a fresh token and matching privileges.
- Rotate token via a separate call: the previously issued bundle's token is now invalid (test once Phase 6 lands).
- Inspect `/api/exports` for the new artifact kind.

**What you can do after this phase.** Emit a usable config file for a Claude Code instance on the LAN. The client cannot yet actually call into MCOS, but the bundle is ready.

---

## Phase 6 - Request authentication and shared-fabric enforcement

**Goal.** Make the bundle real. Every API request is authenticated by bearer token. Privileged mutations are enforced. Shared-fabric reads are open to every authenticated client. This is the phase that restores end-to-end functionality for LAN clients.

This phase is the largest in the plan. If mid-flight the work exceeds one session, I will split it into 6a (authentication middleware and request context) and 6b (privilege gates on mutations and client-facing read routes).

**Code changes.**
- New interface `IAuthenticatedRequestContextResolver`. Implementation parses `Authorization: Bearer <token>`, calls `ILanClientAccessService::validateToken`, populates a request-scoped `AuthenticatedRequestContext{client, privileges, autonomousMode}`.
- Add a lightweight middleware layer around `SimpleHttpServer` (or the local routing shim) that runs the resolver before dispatching to handlers. Handlers access the context through a per-request accessor.
- New config flag `AppConfiguration::requireAuthenticatedClient` (default `false` during Phase 6; flipped to `true` at the end of Phase 8 after UI is proven; final default of `true` committed in Phase 9).
- When `requireAuthenticatedClient = false`, missing tokens yield a synthetic context with full operator privileges so the dashboard keeps working. When `true`, missing tokens return 401.
- Mutation routes enforce the matching privilege flag:
  - `POST /api/runtime/mcp-servers` requires `canCreateMcpServers` (or `canModifyMcpServers` when the target already exists).
  - `POST /api/runtime/mcp-servers/remove` requires `canRemoveMcpServers`.
  - `POST /api/runtime/subagents` requires `canCreateSubAgents` (or `canModifySubAgents`).
  - `POST /api/runtime/subagents/remove` requires `canRemoveSubAgents`.
  - `POST /api/clients*` requires `canManageClients`.
  - `POST /api/clu/execute`, governance-profile edits require `canChangeGovernancePolicy`.
  - Module activation routes require `canManageModules`.
- Shared-fabric reads for clients:
  - `GET /api/client/mcp-servers` returns every registered MCP server (not gated).
  - `GET /api/client/sub-agents` returns every registered sub-agent.
  - `GET /api/client/activity` returns events scoped to the requesting client plus shared-fabric events.
  - `GET /api/client/governance/profile` returns the CLU profile summary.
  - `POST /api/client/governance/decisions` is the pre-check endpoint for proposed actions.
  - `POST /api/client/heartbeat` updates `lastSeenUtc`.
- Dashboard routes stay as-is but start honoring the middleware so the browser UI can introduce a future operator login.

**Verification.**
- With a valid token and `canCreateMcpServers = true`, `POST /api/runtime/mcp-servers` for a new server succeeds.
- Same token with `canCreateMcpServers = false`: 403 with explicit `errorMessage` naming the missing privilege.
- Any authenticated token reads the full catalog via `GET /api/client/mcp-servers` (shared fabric).
- Flip `requireAuthenticatedClient = true`, restart: anonymous requests return 401; authenticated ones keep working.
- Integration test: register two clients, issue configs, verify that client A's MCP-server creation is visible to client B through the shared fabric.

**What you can do after this phase.** Real LAN-client connectivity. Drop a config bundle on a Claude Code host, point the tool at MCOS, and it can list the shared fabric and create resources according to its privilege set.

---

## Phase 7 - CLU governance expansion, aligned to Forsetti

**Goal.** Move CLU from three action kinds to full coverage of every privileged mutation. Rewrite the governance profile so the rules are expressed in Forsetti terms as applied to LAN clients and shared resources.

If the profile rewrite grows beyond one session, split into 7a (enum and enforcement wiring) and 7b (profile content rewrite, deferred-approval queue, surface).

**Code changes.**
- Replace `GovernanceActionKind` at `include/MasterControl/MasterControlModels.h:112-117` with a superset:
  - `Unknown`
  - `ClientRegister`, `ClientPrivilegeChange`, `ClientAutonomousModeChange`, `ClientRevoke`
  - `McpServerCreate`, `McpServerModify`, `McpServerRemove`
  - `SubAgentCreate`, `SubAgentModify`, `SubAgentRemove`
  - `ModuleEnable`, `ModuleDisable`
  - `GovernancePolicyChange`
  - `RemoteInstall` (kept, already in use elsewhere)
- Enforcement decision enum `GovernanceDecisionOutcome{Allow, Block, RequiresOperatorApproval}` at `include/MasterControl/MasterControlModels.h` alongside the existing decision types.
- Every mutation handler calls `commandLogicUnitService_->enforceAction(...)` and honors the outcome. `RequiresOperatorApproval` returns HTTP 202 with a deferred action id.
- New deferred-decision queue:
  - `struct GovernanceDeferredAction{id, action, requesterClientId, payloadSnapshot, createdAtUtc, status, decidedAtUtc, operatorId}`.
  - Service `IGovernanceApprovalQueueService` with list, approve, reject.
  - Routes: `GET /api/clu/approvals`, `POST /api/clu/approvals/{id}/approve`, `POST /api/clu/approvals/{id}/reject`. The latter two require operator privileges (`canManageClients` is the proxy; add `canApproveGovernance` in a minor Phase 7 model bump if needed).
- Rewrite `resources/clu/governance-profile.json`:
  - Doctrine section aligned to Forsetti: contract before action, scope is binding, truthfulness, governance overrides convenience, no meaningful autonomous action without declared scope.
  - Rule section rewritten around LAN clients: shared-fabric invariants, privilege honesty, autonomous-mode declaration required, governance-policy changes auditable, module lifecycle traceable.
  - Roles stay; add `LanClient` as a first-class role with a defined scope.
- Autonomous mode semantics: a client with `autonomousMode = true` and the matching privilege can take actions immediately; CLU logs and may retroactively flag. A client with `autonomousMode = false` but the matching privilege gets its action queued as `RequiresOperatorApproval` when CLU deems it high-impact (removals, privilege escalations, module state changes). Other actions go straight through.
- Every CLU action emits an activity event keyed on the new action kind.

**Verification.**
- With a `canCreateMcpServers` client and autonomousMode true, create MCP: immediate success, CLU allow event.
- Same client and privilege, autonomousMode false, attempt a removal of another client's MCP: 202 + deferred id. Operator approves. Action applies.
- Profile file round-trips through `/api/clu` without schema errors.
- Unit test: every new action kind round-trips through `to_string`, `from_string`, and JSON.

**What you can do after this phase.** CLU genuinely governs. Autonomous mode is real; operator approval is real; every privileged mutation is logged and evaluated.

---

## Phase 8 - Settings surface in the browser

**Goal.** Give the operator a UI for all of the above. This is the "settings menu that controls privileges (up to and including Autonomous mode) for each model" that the product owner described.

**Code changes.**
- New browser destination `LAN Clients` in `resources/web/app.js`:
  - Table of clients with status, privileges summary, autonomous-mode badge, last-seen.
  - Register-client form.
  - Per-client drawer with privilege toggles, autonomous-mode toggle, token rotation button, config download button, disable/enable/remove actions.
- New browser destination `Governance` in `resources/web/app.js`:
  - Pending approvals list with approve/reject buttons.
  - Recent decisions log with filters.
  - Governance profile summary view (read-only surface; edits land in a later phase if requested).
- Existing destinations (Runtime, Telemetry, MCP Servers, Sub-Agents, Platform Services) remain. The emptied Providers slot from Phase 2 is removed from navigation.
- Quick actions toolbar on the home hero: "Register LAN Client" becomes the primary call to action.
- Activity stream panel shows the new event kinds.

**Verification.**
- From a clean install: register a client, toggle privileges, rotate token, download config, drop the config on a Claude Code host, confirm the client appears in the LAN Clients table with a recent `lastSeenUtc`.
- From the Governance destination: create a deferred approval, approve it, confirm the action applies.
- `requireAuthenticatedClient` can now be flipped true without breaking the UI (the dashboard continues to run anonymously from localhost; a future operator-login phase can add real auth for the dashboard).

**What you can do after this phase.** Operate the entire new model from the browser. The product is back to being functional and now matches the original intent.

---

## Phase 9 - Documentation, tests, and end-to-end smoke verification

**Goal.** Land the updated documentation set, a complete test pass, and a proof-of-working artifact that demonstrates two LAN clients on two machines collaborating through MCOS.

**Code changes.**
- New wiki pages:
  - `docs/wiki/LAN-Clients.md`
  - `docs/wiki/Privileges.md`
  - `docs/wiki/Client-Config-Bundle.md`
  - `docs/wiki/Governance.md` (replaces the provider-framed `CLU-Governance.md`)
  - `docs/wiki/Remote-Client.md` rewritten for the new bundle-based onboarding.
- Update `docs/wiki/Home.md`, `Architecture.md`, `API-Reference.md`, `Operations.md`, `Troubleshooting.md`, `Infrastructure.md`, `Versions.md`, `Automation.md` to reflect the new model.
- New proof artifact `plans/PROOF-OF-WORKING/11-lan-client-end-to-end.md` demonstrating: Client A (claude-code) creates MCP server X, Client B (codex) discovers and uses MCP server X, Client B attempts to delete X without `canRemoveMcpServers` and receives 403, the operator-audit CLU log contains the full trace.
- Full `ctest` pass. Forsetti compliance script pass.
- Default `AppConfiguration::requireAuthenticatedClient = true` on fresh installs. Upgraded installs keep their current value unless the operator flips it.
- `VERSION.json` bumped to a new major-remediation version.
- `CHANGELOG.md` finalized.

**Verification.**
- Clean install, register two clients, run the proof scenario end to end, capture the trace in the proof file.
- All wiki links lint.
- Installer builds and validates: `scripts\Package-MasterControlOrchestrationServer.ps1 -Preset release`.

**What you can do after this phase.** The redesigned MCOS is production-ready for the scenario the product owner described. Any AI model on the LAN can be onboarded through a configuration file, governed by privileges, and held to the Forsetti-aligned CLU posture.

---

## Cross-cutting concerns

- **Authentication.** Bearer tokens over HTTP. Suitable for a trusted LAN. If you want mTLS or HTTPS up front, flag it in open decisions and I will insert Phase 6.5 with certificate lifecycle, `httpsys` binding, and client-side pinning instructions.
- **Autonomous mode precedence.** A client with `autonomousMode = true` still cannot exceed its declared privileges. Autonomous mode only governs whether high-impact actions queue for operator approval. The global `aiAutonomyEnabled` flag at `AppConfiguration` is a kill switch that overrides every client's autonomous flag when off.
- **Shell UI.** Phase 2 deletes `ProvidersSectionControl.xaml`. Phase 8 does browser only. The shell ends up without a client surface until a follow-on track adds `LanClientsSectionControl.xaml`. If this is unacceptable, flag it and I will add Phase 8.5 to cover the shell.
- **Bind address.** The current default `0.0.0.0` is correct for a LAN deployment. Config bundle generation refuses to serialize `0.0.0.0` and substitutes a reachable address.
- **Migration from old configs.** Phase 2 strips removed fields cleanly. There is no provider-to-client auto-migration; the old providers were not genuine identities and there is no matching record to carry forward. Operators register their models as LAN clients fresh in Phase 3 onwards.
- **Observability.** Every new privileged action and every CLU decision emits an activity-ring event. The existing 512-event buffer stays; we do not extend it in this plan.

---

## Open decisions

I have defaulted each of these; flip them at review time if you disagree.

1. **Shell UI scope.** Phase 2 deletes the Providers XAML. Default: no immediate replacement in the shell; browser is the operator surface until a later shell track. Alternative: insert Phase 8.5 that adds a matching shell section.
2. **Authentication strength.** Default: bearer tokens over HTTP for a trusted LAN. Alternative: mTLS or HTTPS from day one as Phase 6.5.
3. **Autonomous mode action scope.** Default: autonomous mode only skips the operator-approval queue for the client; it never grants privileges the client does not hold. Alternative: tie autonomous mode to an auto-escalation rule where CLU can widen scope temporarily for Forsetti-defined reasons.
4. **Governance profile owner.** Default: operators with `canChangeGovernancePolicy` edit the profile. Alternative: profile is immutable at runtime and only changes via a repo-tracked file reload.
5. **Anonymous admin during transition.** Default: `requireAuthenticatedClient = false` during Phases 3 through 7 so the browser dashboard keeps working from localhost without an operator login story. Alternative: require token auth from Phase 6 onward and add a minimal operator-login flow in Phase 8.
6. **Deletion of activity log.** Default: keep the existing 512-event ring buffer as-is. Alternative: persist events to disk so the governance audit trail survives service restarts.

---

## Dependency graph

```
Phase 1 (ADR)
  -> Phase 2 (remove provider stack)
       -> Phase 3 (LAN client identity)
            -> Phase 4 (privileges)
                 -> Phase 5 (config bundle)
                      -> Phase 6 (auth + enforcement)    <-- pivot moment
                           -> Phase 7 (CLU expansion)
                                -> Phase 8 (browser UI)
                                     -> Phase 9 (docs + proof + defaults)
```

Phase 2 is the only destructive phase. Phases 3 through 9 are purely additive inside the new skeleton.

---

## Recommended first cut

If you approve the plan, start with **Phase 1 + Phase 2** in a single session. Phase 1 produces the ADR and removal inventory for you to review; Phase 2 executes the removal. That puts the system in the deliberate trough and makes the rest of the work sequential addition.

If you would rather see Phase 1 alone before committing to the deletion, I will stop after Phase 1 for review.
