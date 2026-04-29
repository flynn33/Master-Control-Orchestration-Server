## Phase 2 Removal Inventory

Authoritative checklist of every file, symbol, route, manifest, test, and doc to delete or edit in Phase 2 of the remediation. Line numbers are anchors at the time of this inventory (2026-04-24); they may drift once earlier deletions shift the files. Grep by symbol name is the reliable confirmation in every case.

All references to `Provider*`, `AutoConnect*`, and the four vendor module classes must be zero after Phase 2. The grep commands at the end of this document verify that invariant.

### 1. Headers - files to edit

#### `include/MasterControl/MasterControlModels.h`

Delete the following declarations. Ranges are approximate; delete the full body of each enum and struct including braces.

| Kind | Symbol | Anchor line |
| --- | --- | --- |
| enum class | `ProviderKind` | 36 |
| enum class | `ProviderCredentialFieldKind` | 44 |
| enum class | `ProviderAssignmentTargetKind` | 51 |
| enum class | `ProviderExecutionStatus` | 57 |
| enum class | `ProviderExecutionTransport` | 64 |
| struct | `ProviderConnection` | 175 |
| struct | `ProviderCredentialFieldDescriptor` | 187 |
| struct | `ProviderCapabilityDescriptor` | 199 |
| struct | `ProviderCredentialStatus` | 238 |
| struct | `ProviderCredentialUpdate` | 246 |
| struct | `ProviderAssignmentTarget` | 271 |
| struct | `ProviderAssignment` | 279 |
| struct | `ProviderExecutionRegistration` | 287 |
| struct | `ProviderExecutionRequest` | 299 |
| struct | `ProviderExecutionRecord` | 308 |
| struct | `AutoConnectRequest` | 338 |
| struct | `AutoConnectStep` | 362 |
| struct | `AutoConnectResult` | 369 |

Remove the `AppConfiguration` vector fields at lines 860-867:
- `providers`
- `providerCapabilities`
- `providerCredentialStatuses`
- `providerAssignmentTargets` (note: there is also a snapshot variant; confirm which one persists)
- `providerAssignments`
- `providerExecutionRegistrations`
- `providerExecutionHistory`

Remove the `DashboardSnapshot` fields at lines 894-896 that carry `providers` and `providerAssignments`.

Remove the free functions in the vicinity of lines 905-970:
- `to_string(ProviderAssignmentTargetKind)` and siblings for every deleted enum
- `providerCredentialFieldKindFromString`
- `providerAssignmentTargetKindFromString`
- `to_json(nlohmann::json&, ProviderAssignmentTargetKind)` and siblings for every deleted enum
- Any `NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE*` entries at lines 1113, 1121, 1631, 1634, 1636, 1665 that name the deleted structs.

#### `include/MasterControl/MasterControlContracts.h`

Delete these interface declarations (lines are anchors):

- `IProviderRegistry` (line 74): `listProviders`, `upsertProvider`, `autoConnectProvider`, destructor
- `IProviderCatalogService` (line 93): `listCapabilities`, `upsertCapability`
- `IProviderCredentialStore` (line 101): `listStatuses`, `upsertCredentials`
- `IProviderAssignmentService` (line 133): `listTargets`, `listAssignments`, `upsertAssignment`
- `IProviderExecutionCatalogService` (line 141): `listRegistrations`, `upsertRegistration`
- `IProviderExecutionService` (line 149): `history`, `execute`

Delete these methods from the admin-runtime JSON interface (lines 242-254):

- `upsertProviderJson`
- `autoConnectProviderJson`
- `upsertProviderCredentialsJson`
- `upsertProviderAssignmentJson`
- `executeProviderTaskJson`

Also delete the corresponding comment fragment at line 27 referencing `upsertProvider` in the noun list.

#### `include/MasterControl/MasterControlModules.h`

Delete the four module class declarations:

- `ProviderIntegrationModule` (line 56)
- `CodexProviderModule` (line 64)
- `ClaudeCodeProviderModule` (line 72)
- `XAIProviderModule` (line 80)

#### `include/MasterControl/MasterControlRuntime.h`

Delete the following public methods (lines 33-45):

- `upsertProviderJson`
- `autoConnectProviderJson`
- `upsertProviderCredentialsJson`
- `upsertProviderAssignmentJson`
- `executeProviderTaskJson`

#### `include/MasterControl/MasterControlDefaults.h`

Delete at line 39:

- `std::vector<ProviderConnection> buildDefaultProviders();`

---

### 2. Source - files to edit

#### `src/MasterControlApp/MasterControlModels.cpp`

Delete every `to_string`, `from_string`, `to_json`, `from_json`, and `NLOHMANN_DEFINE_TYPE` implementation for the enums and structs listed in the MasterControlModels.h section above. Grep confirmation: `grep -nE "Provider|AutoConnect" src/MasterControlApp/MasterControlModels.cpp` should return zero matches after the edit.

#### `src/MasterControlApp/MasterControlDefaults.cpp`

- Delete `buildDefaultProviders()` at line 392 and its entire body.
- Delete the call site at line 416: `configuration.providers = buildDefaultProviders();`
- Delete any default `providerAssignments` or related seeded fields nearby.

#### `src/MasterControlApp/MasterControlRuntime.cpp`

This file is the largest surgical target. Delete in the following order to keep the file compilable between edits:

Route handlers (by path, approximate anchor lines):
- `GET /api/providers` at line 10622
- `POST /api/providers` at line 11269
- `POST /api/providers/credentials` at line 11273
- `POST /api/providers/auto-connect` at line 11277
- `POST /api/providers/signin/register` at line 11281
- `POST /api/providers/signin/start` at line 11309
- `GET /api/providers/signin/status` at line 11333
- `GET /api/providers/signin/installed` at line 11371
- `POST /api/providers/groups` at line 11401
- `POST /api/providers/groups/remove` at line 11405
- `POST /api/providers/assignments` at line 11409
- `POST /api/providers/execute` at line 11413
- `GET /api/provider-assignment-targets` (confirm exact line with grep)

Protected module set at line 1691-1699:
- Remove `"com.mastercontrol.provider-integration"` from `protectedForsettiModuleIds()`.

Default activation list at line 10292-10313:
- Remove `"com.mastercontrol.provider-integration"`, `"com.mastercontrol.provider-codex"`, `"com.mastercontrol.provider-claude-code"`, `"com.mastercontrol.provider-xai"`.
- Update the `std::array<const char*, 19>` size literal to `15`.

Runtime container wiring at line 10242-10255:
- Delete registrations for provider registry, credential store, auto-connect service, assignment service, execution catalog service, execution service.

Service class implementations (anchor search via grep `class ProviderAssignmentService`, `class ProviderExecutionService`, `class ProviderRegistry`, `class ProviderCredentialStore`, `class AutoConnectService`):
- Delete entire class bodies and any free helpers they depend on.

Provider execution transports (delete entire function bodies):
- `executeClaudeCodeCli` (line 4877)
- `executeCodexCli` (line 5019 area, confirm)
- `executeOpenAICompatibleChat` (grep for definition)
- `buildExecutionSystemPrompt` (line 4635) and any helpers only called by the three transports above

Provider-related activity event emissions - remove every one where the event kind is provider-scoped. Grep `appendEvent` for provider/auto-connect strings.

Dashboard snapshot assembly at line 9362 area - remove `snapshot.providerAssignmentTargets = ...` and the other provider-related fields being populated into the snapshot.

Provider diagnostic writes at lines 2431-2489 (provider/credential context building) - remove.

After all deletions, `grep -nE "Provider|AutoConnect|provider-codex|provider-claude-code|provider-xai|provider-integration|/api/providers|/api/provider-" src/MasterControlApp/MasterControlRuntime.cpp` must return zero matches.

#### `src/MasterControlModules/MasterControlModules.cpp`

Delete the four module class implementations:
- `ProviderIntegrationModule::descriptor/manifest/start/stop` at lines 1555-1577
- `CodexProviderModule::descriptor/manifest/start/stop` at lines 1579-1608
- `ClaudeCodeProviderModule::descriptor/manifest/start/stop` at lines 1611-1637
- `XAIProviderModule::descriptor/manifest/start/stop` at lines 1639-1665

Delete the four module registrations at lines 1970-1982:
- `registry.registerModule("ProviderIntegrationModule", ...)`
- `registry.registerModule("CodexProviderModule", ...)`
- `registry.registerModule("ClaudeCodeProviderModule", ...)`
- `registry.registerModule("XAIProviderModule", ...)`

Delete any `#include "MasterControl/MasterControlModels.h"` fragments if they become orphaned, or prune to the remaining used types.

Delete provider capability seeding at line 178 and vicinity (the manifest-capability registration).

Also inspect lines 662-899 for any provider-specific helpers (`makeOpenAICapability`, `makeClaudeCodeCapability`, `makeCodexCapability`, `makeXAICapability`, etc.) and delete them.

After this file is done: `grep -nE "Provider|AutoConnect" src/MasterControlModules/MasterControlModules.cpp` should return zero matches.

---

### 3. Shell - files to delete

- `src/MasterControlShell/ProvidersSectionControl.xaml`
- `src/MasterControlShell/ProvidersSectionControl.xaml.h`
- `src/MasterControlShell/ProvidersSectionControl.xaml.cpp`

### 4. Shell - files to edit

#### `src/MasterControlShell/MasterControlShell.vcxproj`

Remove `<ClCompile>` and `<ClInclude>` and `<Page>` entries for `ProvidersSectionControl.xaml*`.

#### `src/MasterControlShell/Project.idl`

Remove the `ProvidersSectionControl` runtimeclass and any related namespace entries.

#### `src/MasterControlShell/MainWindow.xaml`

Remove the `<local:ProvidersSectionControl .../>` node and any `TabViewItem` / `NavigationViewItem` that references the providers surface.

#### `src/MasterControlShell/MainWindow.xaml.h`

Remove any header include for `ProvidersSectionControl.xaml.h` and any member variable referring to the providers control.

#### `src/MasterControlShell/MainWindow.xaml.cpp`

Remove any initialization, navigation, or binding code for the providers section.

#### `src/MasterControlShell/ShellRuntime.cpp`, `ShellRuntime.h`

Grep for `Provider`, `AutoConnect`, `provider`. Remove every member, method, and invocation that carries provider state into the shell. Typical hits: `ProvidersViewModel`, `LoadProviders()`, `ProviderList` binding sources. After edits, the shell runtime should no longer reference any provider type.

---

### 5. Module manifests - files to delete

- `src/MasterControlModules/Resources/ForsettiManifests/ProviderIntegrationModule.json`
- `src/MasterControlModules/Resources/ForsettiManifests/CodexProviderModule.json`
- `src/MasterControlModules/Resources/ForsettiManifests/ClaudeCodeProviderModule.json`
- `src/MasterControlModules/Resources/ForsettiManifests/XAIProviderModule.json`

Also scan `src/MasterControlModules/Resources/ForsettiManifests/*.json` for cross-references to the four deleted module ids (dependencies, feature flags). Update as needed.

---

### 6. Web UI - files to edit

#### `resources/web/app.js`

Delete:
- The `Providers` navigation destination and its route handler.
- Every render function scoped to providers: `renderProviders`, `renderProviderRow`, `renderProviderExecutionHistory`, `renderProviderAssignments`, `renderProviderCapabilities`, and similar.
- Auto-connect form: `provider-auto-connect`, `provider-execution`, `provider-assignment` form kinds.
- Sign-in flow UI: anything keyed on `providers/signin/*`.
- The `subAgentGroupsMarkup` block that references provider-backed routing (strip provider-specific framing while keeping the sub-agent group rendering).
- The `providers` entry from `executionTargetOptions`, `providerCapabilities`, and any `data-form-kind="provider-*"` form registrations.

Delete related CSS rules in `resources/web/styles.css` that target provider-specific selectors. Grep `grep -nE "provider|Provider|auto-connect" resources/web/styles.css` and prune matches.

---

### 7. Tests - files to edit

#### `tests/MasterControlOrchestrationServerTests.cpp`

- Delete every test case that asserts provider registry, credential store, assignment, execution, or auto-connect behavior.
- Delete assertions that `provider-integration` is protected.
- Delete `providerFamilyId` and `authBridgeId` assertions (per v0.4.5-rc.5 entry in CHANGELOG).
- Replace any fixture bootstrap that seeded providers with an empty fixture.
- Add minimal skeletal tests that assert the 15-module default set activates without provider modules. (Phase 3 and beyond will add tests for the LAN client stack.)

---

### 8. Scripts - files to edit

#### `scripts/check-mastercontrol-forsetti.ps1`

Grep for `Provider`, `Codex`, `ClaudeCode`, `XAI`, `provider-integration`. Remove assertion lines that expect any of those entities. Keep the overall compliance guardrails intact.

#### `scripts/github_agents/sync_docs.py`

Remove any entries that export or link provider wiki pages. Ensure the script still works after `Auto-Connect-AI.md` is gone.

---

### 9. Documentation - files to delete

- `docs/wiki/Auto-Connect-AI.md`

### 10. Documentation - files to edit

#### `docs/wiki/Architecture.md`

Strip the provider sections (lines 28-47 and 84-110 from the research report). Replace with a placeholder referencing ADR-001 and noting that LAN Client docs will land in Phase 9.

#### `docs/wiki/API-Reference.md`

Delete the entire "Auto-Connect AI" section and the "Provider" subsections of Read endpoints and Runtime inventory mutation. Add a short replacement pointer to ADR-001.

#### `docs/wiki/CLU-Governance.md`

Strip provider-specific framing (lines 84-98 per the research report). Leave the doctrine and the remaining governance structure; Phase 7 will do a full rewrite.

#### `docs/wiki/Remote-Client.md`

Rewrite around the new LAN client model. For Phase 2, a short placeholder is sufficient: "Remote client onboarding is being rebuilt per ADR-001. See `plans/remediation/01-gut-and-rebuild.md` for the in-progress design."

#### `docs/wiki/Telemetry-and-Activity.md`

Remove any provider-execution event examples and replace with a generic admin-API example.

#### `docs/wiki/Sub-Agents.md`

Remove provider framing from the invocation examples. Keep the roster and the transport notes.

#### `docs/wiki/Home.md`

Update the documentation navigation table to drop the Auto-Connect-AI row. Link ADR-001.

#### `docs/wiki/Automation.md`, `docs/wiki/Operations.md`, `docs/wiki/Troubleshooting.md`, `docs/wiki/Infrastructure.md`, `docs/wiki/Versions.md`

Grep each for `provider`, `Provider`, `auto-connect`. Remove or rewrite every hit. Provider-flow guidance moves to the LAN Client docs in Phase 9.

---

### 11. Plans and proof artifacts - files to delete

- `plans/PROOF-OF-WORKING/02-auto-connect.md`
- `plans/PROOF-OF-WORKING/11-ai-task-execution.md`

### 12. Plans and proof artifacts - files to edit

#### `plans/PROOF-OF-WORKING.md` and `plans/PROOF-OF-WORKING/README.md`

Remove links to 02 and 11. Add a forward-pointer to the upcoming `11-lan-client-end-to-end.md` artifact (Phase 9 deliverable).

#### `plans/PROOF-OF-WORKING/03-command-stream.md`, `08-starter-workflows.md`, `09-sub-agents.md`, `16-service-restart.md`, `17-soak-stability.md`

Grep each for `Provider`, `auto-connect`, `provider`. Rewrite or strip the paragraphs that still refer to the removed stack.

#### `plans/API-PROBE-RESULTS.md`, `plans/FEATURE-AUDIT.md`

Append a "Phase 2 remediation" note at the top explaining that the provider surface no longer exists and the remainder of the document describes the pre-remediation state. Do not edit the historical body content.

#### `plans/dashboard/project-overview.md`, `plans/dashboard/technical-details.md`

Remove the "provider routing for Codex, Claude Code, and xAI" bullets. Add a pointer to ADR-001.

---

### 13. Top-level files - to edit

#### `CHANGELOG.md`

Add a new entry at the top:

```
## Unreleased - Gut and Rebuild (Phase 2)

### Removed
- Full AI provider integration stack: four Forsetti modules (ProviderIntegrationModule, CodexProviderModule, ClaudeCodeProviderModule, XAIProviderModule), their manifests, provider data types (ProviderConnection, ProviderCapabilityDescriptor, ProviderAssignment, ProviderExecutionRegistration, ProviderExecutionRequest, ProviderExecutionRecord, AutoConnectRequest/Step/Result, related enums), provider runtime services, all /api/providers/* and /api/provider-assignment-targets routes, outbound AI CLI transports (Claude Code, Codex, OpenAI-compatible), browser Providers destination, shell ProvidersSectionControl, provider docs, and the corresponding test coverage. See ADR-001 for the decision rationale and plans/remediation/02-removal-inventory.md for the inventory.
```

#### `VERSION.json`

Do not bump version in Phase 2. Version bump is deferred to Phase 9 when the replacement is in place.

#### `README.md`

Update the "Highlights" table to remove "Auto-Connect AI providers". The "Build, validate, stage" section is unchanged. Add a pointer to ADR-001 near the top.

---

### 14. Post-deletion verification

After every file in this inventory has been edited or deleted, run these checks:

```bash
# No provider symbols remain in source
grep -rnE "ProviderIntegrationModule|CodexProviderModule|ClaudeCodeProviderModule|XAIProviderModule" src include resources tests scripts

grep -rnE "ProviderConnection|ProviderCapabilityDescriptor|ProviderAssignment|ProviderAssignmentTarget|ProviderKind|ProviderExecutionTransport|ProviderExecutionRegistration|ProviderExecutionRequest|ProviderExecutionRecord|AutoConnectStep|AutoConnectResult|AutoConnectRequest" src include resources tests scripts

grep -rnE "executeClaudeCodeCli|executeCodexCli|executeOpenAICompatibleChat|buildExecutionSystemPrompt" src include

grep -rnE "/api/providers|/api/provider-assignment" src include resources tests scripts

# No provider module ids remain
grep -rnE "provider-integration|provider-codex|provider-claude-code|provider-xai" src include resources tests scripts
```

All five grep runs must return zero matches. If any do, the referenced file was missed; fix and re-run.

Build verification:

```powershell
cmake --preset debug
cmake --build build\debug --config Debug
ctest --test-dir build\debug -C Debug --output-on-failure
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\check-mastercontrol-forsetti.ps1
```

All four must pass. If the Forsetti compliance script still expects provider modules, update the script assertions rather than restoring the modules.

Runtime verification:

1. Start the service.
2. `curl http://127.0.0.1:7300/api/health` returns `{status: "ok"}`.
3. `curl http://127.0.0.1:7300/api/forsetti/modules` lists exactly 15 active modules. No module has `moduleId` containing `provider-`.
4. `curl http://127.0.0.1:7300/api/providers` returns 404.
5. `curl http://127.0.0.1:7300/api/providers/execute -X POST` returns 404.
6. Browser dashboard at `http://localhost:8000` loads without a Providers navigation entry.

When all checks pass, Phase 2 is complete.

---

### Risk notes

- The provider-execution proof at `plans/PROOF-OF-WORKING/11-ai-task-execution.md` documents that the current execute loop works for xAI/OpenAI transports. Deleting this file erases the visible evidence; git history remains. Worth double-checking the deletion with the product owner before running.
- `docs/wiki/Auto-Connect-AI.md` has outbound links from several other wiki pages and from `docs/wiki/Home.md`. A link audit after deletion is part of the verification pass.
- The shell `ProvidersSectionControl.xaml` changes may require an IDL compilation step. On Windows, deleting the `.idl` entry is sufficient; on first rebuild the generated `.g.h` files regenerate without the deleted runtimeclass.
- `AppConfiguration` currently persists provider fields. Existing installs' configs will contain unknown keys after upgrade; the configuration migrator added in Phase 2 strips them silently and logs one activity event. Verify the migrator's behavior explicitly by running the upgraded binary against a pre-remediation config backup.
