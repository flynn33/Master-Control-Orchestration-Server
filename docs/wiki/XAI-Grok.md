# xAI and Grok

![milestone](https://img.shields.io/badge/milestone-A3.12.0%20Model%20Parity-00f6ff?style=flat-square)
![vendor](https://img.shields.io/badge/vendor-xAI-1cf2c1?style=flat-square)
![models](https://img.shields.io/badge/models-grok--4.3%20%2F%20grok--build--0.1-00aacc?style=flat-square)

This page covers the xAI surfaces in the
[Client Integrations](Client-Integrations) catalog: the hosted `xai-responses`
remote MCP integration, and the local `grok-build` / `grok-build-acp` Grok Build
surfaces. For a Grok-Build-focused walkthrough see [Grok Build](Grok-Build).

---

## `xai-responses` — xAI Responses API / Grok API (remote MCP)

The xAI Responses API (and the Grok API) can call MCOS as a **remote MCP**
server. Alias: `xai`.

xAI remote MCP supports **Streaming HTTP and SSE** transports and requires a
reachable **public HTTPS** endpoint. The remote MCP tool object uses
`server_url`, `server_label`, `server_description`, and `allowed_tools`:

```json
{
  "server_url": "https://<public-or-edge-mcos-host>/mcp",
  "server_label": "mcos",
  "server_description": "MCOS governed orchestration gateway",
  "allowed_tools": ["mcos_read_status", "mcos_list_endpoints"]
}
```

Important differences from [OpenAI Responses](OpenAI-Responses):

- The xAI shape **does not** include the OpenAI-only `require_approval` or
  `connector_id` fields. Do not add them.
- Because there is no client-side `require_approval`, **MCOS enforces tool
  safety** through allow-lists, governance policy, edge policy, and confirm
  guards.

The general Grok API model for examples is `grok-4.3` unless the operator
configures another model. MCOS does not collect or proxy your xAI API key; use
`${XAI_API_KEY}` in your own client.

Pull the generated artifact:

```powershell
Invoke-RestMethod 'http://<mcos-host>:7300/api/client-integrations/xai-responses/artifacts' |
  ConvertTo-Json -Depth 6
```

### Verification

1. Expose MCOS at a reachable public HTTPS endpoint that supports Streaming HTTP
   / SSE.
2. Issue an xAI remote MCP request with `server_url` + `server_label` and confirm
   `tools/list` resolves.
3. Confirm tool safety is enforced by MCOS (allow-lists / governance / confirm
   guards).

> The MCOS gateway itself remains the **POST-only Streamable HTTP** subset with
> **no SSE** (see [Gateway](Gateway)). The Streaming HTTP / SSE expectation is on
> the hosted xAI side; use an edge that upgrades the transport, or a client mode
> that accepts the POST-only subset.

---

## `grok-build` — Grok Build CLI / headless

Grok Build is xAI's coding CLI. Its coding model is `grok-build-0.1`. Grok Build
reads project MCP config from `.grok/config.toml`:

```toml
model = "grok-build-0.1"

[mcp_servers.mcos]
url = "http://<mcos-host>:8080/mcp"
startup_timeout_sec = 30
tool_timeout_sec = 6000
```

Transport is the **POST-only Streamable HTTP** subset; LAN HTTP is supported.
Verify with the commands your installed Grok Build version supports:

```text
grok mcp list
grok mcp doctor mcos --json
grok inspect
```

Headless invocation reads `$env:XAI_API_KEY` from the environment — never write
the key into project files:

```powershell
$env:XAI_API_KEY = "<operator-managed-secret>"
grok --no-auto-update -p "Inspect MCOS health through the mcos MCP server." --output-format streaming-json
```

Full details, including `grok mcp add` setup guidance, are on the
[Grok Build](Grok-Build) page.

---

## `grok-build-acp` — Grok Build ACP

Grok Build ACP runs as an external process/client adapter, not embedded MCOS
runtime execution. Start it with `grok agent stdio`; ACP speaks JSON-RPC over
stdin/stdout and is driven behind an MCOS external process adapter. It is not
directly an MCP endpoint. See [Grok Build](Grok-Build).

## Related pages

[Client Integrations](Client-Integrations) |
[Grok Build](Grok-Build) |
[OpenAI Responses](OpenAI-Responses) |
[Gateway](Gateway) |
[CLU Governance](CLU-Governance)
