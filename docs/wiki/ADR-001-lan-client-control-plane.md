## ADR-001 - MCOS is a LAN Client Control Plane

- Status: Accepted (in part superseded — see [ADR-002](ADR-002-gateway-first-mcp-realignment.md))
- Date: 2026-04-24
- Deciders: Product owner, engineering
- Supersedes: the embedded-provider direction in `docs/wiki/Auto-Connect-AI.md` and the provider-centric framing in `docs/wiki/Architecture.md`
- Superseded in part by: [ADR-002 - MCOS is a Windows-Native LAN MCP Gateway Host (Gateway-First Realignment)](ADR-002-gateway-first-mcp-realignment.md). ADR-002 retains the no-provider-execution stance (§1, §2), the per-client privilege flags (§4), autonomous mode (§5), and CLU centrality (§7). It supersedes §3 (the `X-MCOS-Client-Id` model on the AI-client surface) and §6 (the catalog read surface as the AI-client tool path) — those become the operator surface only; AI clients reach tools through one MCOS-advertised MCP Gateway URL.
- Related: `plans/dashboard/remote-client-onboarding.md`, `docs/archive/remediation/02-removal-inventory.md`, `docs/wiki/CLU-Governance.md`

### Context

The Master Control Orchestration Server was originally intended as a shared orchestration control plane on a trusted LAN. External AI coding clients (Claude Code, Codex, Grok, and any future model) were to run on their own machines, connect to MCOS as governed users of a shared resource fabric, and operate under per-client privileges enforced server-side. The governance authority is the Command Logic Unit module, and the governance principles come from the Forsetti Framework and the Forsetti Framework for Agentic Coding.

The shipped implementation drifted into the opposite shape. `ProviderIntegrationModule` and three vendor modules (`CodexProviderModule`, `ClaudeCodeProviderModule`, `XAIProviderModule`) embed AI systems as internal `Provider*` runtime objects. An Auto-Connect pipeline collects credentials, stores them under DPAPI, discovers models, and registers the provider as a first-class MCOS entity. Role assignments bind providers to built-in lanes (`planner`, `architect`, `auditor`). `POST /api/providers/execute` is the sole path for AI work, invoking vendor CLIs (`claude.exe`, `codex.exe`) or OpenAI-compatible chat endpoints from inside MCOS. This is architecturally inverted relative to the intent.

The parallel thread toward the original intent exists in the repo but is only partially implemented: `plans/dashboard/remote-client-onboarding.md` describes LAN discovery, server-generated client configuration, and local application on the client side. Platform config endpoints (`/api/platform-services/config/{platform}`) and gateway export artifacts demonstrate that server-authored client configuration is already a known pattern. The remediation converges on that pattern and removes the provider-centric one.

### Decision

MCOS is a LAN client control plane. AI systems connect to MCOS as external clients over a trusted LAN. The server issues each client a configuration file containing connection details, a client identifier, a privilege snapshot, catalog URLs, governance pointers, and operating rules. Every authenticated client may use every registered MCP server and sub-agent. Creation, modification, and removal of those resources are gated by per-client privilege flags. CLU enforces Forsetti-framework governance on every privileged mutation.

Specific consequences locked by this decision:

1. The provider stack is removed in full: four module classes, their manifests and registrations, all `Provider*` and `AutoConnect*` data types, the provider registry and credential store, the Auto-Connect pipeline, the assignment service, the execution service and its CLI transports, every `/api/providers/*` and `/api/provider-assignment-targets` route, the browser `Providers` destination, and the shell `ProvidersSectionControl` UI.
2. MCOS never calls out to AI models. The outbound CLI transports (`executeClaudeCodeCli`, `executeCodexCli`, `executeOpenAICompatibleChat`) are deleted, not repositioned.
3. No authentication is required to reach the admin API. The LAN is trusted. Client identity for privilege resolution is a bare `X-MCOS-Client-Id` header read by a lightweight middleware. Missing or unknown headers yield an operator-equivalent context so the browser dashboard continues to function.
4. Privileges are flat booleans on a `LanClient` record, not a profile system. The set is: `canCreateMcpServers`, `canModifyMcpServers`, `canRemoveMcpServers`, `canCreateSubAgents`, `canModifySubAgents`, `canRemoveSubAgents`, `canManageClients`, `canManageModules`, `canChangeGovernancePolicy`. Plus a separate `autonomousMode` flag.
5. Autonomous mode has a narrow, explicit meaning. When `autonomousMode = true` on a client, that client may create MCP servers and sub-agents without limit, bypassing both the matching create privilege and any CLU operator-approval gate for those two action kinds. All other privileged actions continue to honor privileges and CLU.
6. The shared fabric rule is absolute. `GET /api/client/mcp-servers` and `GET /api/client/sub-agents` return the full catalog to any identified client. Use is never privilege-gated.
7. CLU's governance profile is rewritten in Forsetti terms as applied to LAN clients and shared resources. The `GovernanceActionKind` enum expands from three values to cover every privileged mutation.

### Consequences

Positive. The product matches its stated intent. Multiple AI clients can collaborate on a shared fabric with per-client privilege control. Governance is central rather than decorative. The server's surface area shrinks: no credential store, no auto-connect pipeline, no vendor-specific transports, no protected provider module.

Negative. Existing installs that relied on the provider flow must re-register their models as LAN clients. Operators lose the "Sign in with Claude / OpenAI" shortcut in the shell and browser. The shell `ProvidersSectionControl` is deleted without an immediate replacement; the browser becomes the sole operator surface until a follow-on shell track lands. There is no operator-login story for the browser itself during the rebuild; the trusted-LAN assumption is load-bearing.

Neutral. Sub-agents remain catalogued but their backing process fleet (`D:\Sub-Agents\`) is still out-of-repo. Making sub-agents invocable as specialist modes is orthogonal to this ADR and can land on a separate track.

### References

- Remediation plan: `docs/archive/remediation/01-gut-and-rebuild.md` (extended draft), `C:\Users\Flynn\.claude\plans\g-claude-master-control-orchastration-s-giggly-hartmanis.md` (authoritative, concise).
- Removal inventory: `docs/archive/remediation/02-removal-inventory.md`.
- Deep research report: `G:\Claude\deep-research-report.md`.
- Proof that current provider execute loop works end to end: `docs/archive/proof-of-working/11-ai-task-execution.md` (to be deleted in Phase 2; retained in git history).
- Parallel architectural thread that pointed at this decision all along: `plans/dashboard/remote-client-onboarding.md`.
