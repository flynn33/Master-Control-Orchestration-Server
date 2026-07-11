# Client Integrations

![milestone](https://img.shields.io/badge/milestone-A3.12.0%20Model%20Parity-00f6ff?style=flat-square)
![integrations](https://img.shields.io/badge/integrations-10-1cf2c1?style=flat-square)
![transport](https://img.shields.io/badge/gateway-POST--only%20Streamable%20HTTP-ff8c00?style=flat-square)

The **Client Integration Catalog** is the provider-neutral registry MCOS uses to
describe how each external AI client or model surface connects to the governed
MCP Gateway. It is the **A3.12.0 Model Parity implementation milestone** — landed
in the current tree, not a released or deployed product feature.

MCOS stays a governed MCP gateway. Each catalog entry emits provider-native
configuration artifacts and reports its transport and auth compatibility
honestly; MCOS never executes a provider SDK inside its core runtime. Generated
artifacts never contain real secrets — only placeholders such as
`${OPENAI_API_KEY}`, `${XAI_API_KEY}`, `<operator-managed bearer token>`, and
`<public-or-edge-mcos-host>`.

The gateway itself is unchanged: it serves the **POST-only Streamable HTTP**
subset of MCP with **no SSE** (see [Gateway](Gateway)). Catalog entries that name
Streaming HTTP / SSE or public HTTPS describe what the *hosted client* expects,
not a new gateway transport.

---

## The ten canonical integrations

The catalog registers ten canonical integrations in a deterministic order. This
is also the order returned by `GET /api/client-integrations`.

### 1. `claude-code` — Claude Code (Anthropic)

- **Artifacts:** `.mcp.json` `mcpServers` entry (Streamable HTTP).
- **Transport:** POST-only Streamable HTTP; LAN HTTP supported.
- **Verification:** restart Claude Code, confirm the `mcos` server appears, run
  a tool listing.
- **Caveats:** Claude Code consumes Streamable HTTP MCP servers natively; no
  companion utility is required. Approval is enforced by the client's own
  confirm UX, and MCOS still applies its confirm guard.

### 2. `codex` — Codex CLI / IDE (OpenAI)

- **Artifacts:** `~/.codex/config.toml` (user) and `.codex/config.toml`
  (project), table `[mcp_servers.mcos]` with `url`, `enabled = true`,
  `startup_timeout_sec`, `tool_timeout_sec`.
- **Transport:** POST-only Streamable HTTP; LAN HTTP supported.
- **Verification:** `codex mcp list`, `codex mcp --help`.
- **Caveats:** Codex MCP configuration is **TOML**. Legacy JSON
  (`codex.config.json` / `codex-mcp.json`) is **not** the current primary
  format. See [Codex](Codex).

### 3. `codex-mcp-server` — Codex as an external MCP server (OpenAI)

- **Artifacts:** `codex-mcp-server-notes.md`.
- **Transport:** external process adapter over stdio JSON-RPC.
- **Verification:** confirm the adapter speaks MCP over stdio and never runs
  inside the MCOS core runtime.
- **Caveats:** Codex execution stays out of the MCOS core runtime; this profile
  is external-process orchestration scaffolding, not an embedded executor.

### 4. `openai-responses` — OpenAI Responses API (remote MCP)

- **Artifacts:** `openai-responses.mcp.json` — a tool object with
  `type: "mcp"`, `server_label`, `server_description`, `server_url`,
  `require_approval`, `allowed_tools`. The model is operator-selected.
- **Transport:** hosted; requires a reachable **public HTTPS** endpoint.
- **Verification:** deploy or bridge MCOS to a public HTTPS endpoint, issue a
  Responses API request with the MCP tool object, confirm `tools/list` resolves.
- **Caveats:** hosted OpenAI cannot reach a LAN-only gateway. Keep
  `allowed_tools` read-only; use `require_approval` for anything mutating. See
  [OpenAI Responses](OpenAI-Responses).

### 5. `chatgpt-apps` — ChatGPT Apps / Connectors (OpenAI)

- **Artifacts:** `chatgpt-apps-connector.md`.
- **Transport:** hosted; requires a public HTTPS `/mcp` endpoint, OAuth 2.1
  authorization-code + PKCE, protected resource metadata, and client identity
  (OpenAI-managed mTLS where implemented).
- **Verification:** confirm the public connector endpoint, OAuth/PKCE, and that
  read-only tools are default while destructive tools stay hidden.
- **Caveats:** hosted ChatGPT **cannot reach a LAN-only HTTP MCOS gateway
  directly**. See [ChatGPT Apps](ChatGPT-Apps).

### 6. `chatgpt-connector-edge` — ChatGPT Connector Edge (OpenAI)

- **Artifacts:** `chatgpt-connector-edge.md`.
- **Transport:** a LAN-to-public edge bridge in front of MCOS.
- **Verification:** confirm the edge forwards only approved MCP tool calls,
  enforces HTTPS + auth, and preserves audit context.
- **Caveats:** the edge must never expose raw LAN admin `/api/*` surfaces; the
  MCOS gateway stays LAN-trusted behind the bridge. See
  [ChatGPT Apps](ChatGPT-Apps).

### 7. `xai-responses` — xAI Responses API / Grok API (remote MCP)

- **Artifacts:** `xai-responses.mcp.json` — `server_url`, `server_label`,
  `server_description`, `allowed_tools`. It **must not** include the OpenAI-only
  `require_approval` or `connector_id` fields.
- **Transport:** hosted; supports Streaming HTTP and SSE; requires public HTTPS.
- **Verification:** expose MCOS at a public HTTPS endpoint that supports
  Streaming HTTP / SSE, issue the xAI remote MCP request, confirm `tools/list`
  resolves.
- **Caveats:** xAI does not use `require_approval`; tool approval is enforced by
  MCOS (allow-lists / governance / confirm guards). General Grok API model:
  `grok-4.3`. See [xAI and Grok](XAI-Grok).

### 8. `grok-build` — Grok Build CLI / headless (xAI)

- **Artifacts:** `.grok/config.toml` with `model = "grok-build-0.1"` and
  `[mcp_servers.mcos]` (`url`, `startup_timeout_sec`, `tool_timeout_sec`); plus a
  headless invocation note.
- **Transport:** POST-only Streamable HTTP; LAN HTTP supported.
- **Verification:** `grok mcp list`, `grok mcp doctor mcos --json`,
  `grok inspect` (only where the installed version supports them).
- **Caveats:** the coding model is `grok-build-0.1`. Headless runs read
  `$env:XAI_API_KEY` from the environment — never write the key into project
  files. See [Grok Build](Grok-Build).

### 9. `grok-build-acp` — Grok Build ACP (xAI)

- **Artifacts:** `grok-build-acp-notes.md`.
- **Transport:** `grok agent stdio`, JSON-RPC over stdin/stdout, external
  process/client adapter.
- **Verification:** `grok agent stdio`; confirm ACP runs over JSON-RPC behind an
  MCOS external process adapter.
- **Caveats:** ACP is not directly an MCP endpoint; execution is not embedded in
  the MCOS core runtime. See [Grok Build](Grok-Build).

### 10. `generic-mcp` — Generic MCP client

- **Artifacts:** `.mcp.json` that honestly reports the current gateway
  transport.
- **Transport:** POST-only Streamable HTTP subset (single JSON responses;
  `GET /mcp` returns `405` with `Allow: POST`); LAN trust; **no SSE**.
- **Verification:** resolve `/.well-known/mcos.json`, connect to the gateway MCP
  URL, list tools.
- **Caveats:** do not expose the gateway port to the public internet. Clients
  that require a full SSE stream should use a companion stdio bridge.

---

## Aliases

Aliases resolve to a canonical integration without collapsing product-specific
behavior:

| Alias | Resolves to |
|---|---|
| `claude` | `claude-code` |
| `openai` | `openai-responses` |
| `chatgpt` | `chatgpt-apps` |
| `xai` | `xai-responses` |
| `grok` | `grok-build` |
| `generic` | `generic-mcp` |

---

## Compatibility matrix

| Integration | LAN HTTP | Public HTTPS | POST-only Streamable HTTP | SSE required | Approval by client | MCOS confirm guard |
|---|---|---|---|---|---|---|
| claude-code | yes | optional | yes | no | client-dependent | yes |
| codex | yes | optional | yes | no | client-dependent | yes |
| openai-responses | no (hosted) | yes | maybe | maybe | yes | yes |
| chatgpt-apps | no | yes | maybe | maybe | via connector/app | yes |
| chatgpt-connector-edge | bridge-dependent | yes | bridge-dependent | bridge-dependent | edge-dependent | yes |
| xai-responses | no (hosted) | yes | if compatible | yes/Streaming HTTP | no OpenAI-style approval | yes |
| grok-build | yes | optional | yes if CLI accepts endpoint | no | client-dependent | yes |
| grok-build-acp | local process | optional | not directly an MCP endpoint | no | adapter-dependent | yes |
| generic-mcp | client-dependent | optional | client-dependent | client-dependent | client-dependent | yes |

`MCOS confirm guard` is always `yes`: whatever a client claims to enforce, MCOS
still gates mutating and destructive tools through allow-lists, governance
policy, and confirm guards.

---

## HTTP routes

The catalog is exposed as read-only runtime routes:

| Method | Route | Returns |
|---|---|---|
| `GET` | `/api/client-integrations` | Descriptor list for all ten integrations |
| `GET` | `/api/client-integrations/{id}` | One integration descriptor |
| `GET` | `/api/client-integrations/{id}/artifacts` | Provider-native config artifacts |
| `GET` | `/api/client-integrations/{id}/validate` | Remote-MCP compatibility result |

`{id}` accepts a canonical id or one of the aliases above. The existing
`GET /api/onboarding/{clientType}` route still works and now delegates to the
catalog, so the [Onboarding](Onboarding) profiles and this catalog stay in sync.

The `/validate` route runs the remote-MCP compatibility analyzer against the
live gateway descriptor. It reports, for example, when a hosted integration
cannot reach a LAN-only endpoint (blocking), when the POST-only Streamable HTTP
subset is in effect (info), and when the client does not enforce per-tool
approval so MCOS must (advisory).

---

## Per-provider pages

- [Codex](Codex) — `config.toml`, `[mcp_servers.mcos]`, verification.
- [OpenAI Responses](OpenAI-Responses) — `type=mcp` remote tool, public/edge endpoint.
- [ChatGPT Apps](ChatGPT-Apps) — public HTTPS `/mcp`, OAuth 2.1, connector edge.
- [xAI and Grok](XAI-Grok) — xAI Responses remote MCP and Grok Build surfaces.
- [Grok Build](Grok-Build) — `.grok/config.toml`, `grok mcp`, headless, ACP.

## Related pages

[Onboarding](Onboarding) |
[Gateway](Gateway) |
[CLU Governance](CLU-Governance) |
[LAN Discovery](LAN-Discovery) |
[API Reference](API-Reference)
