# Grok Build

![milestone](https://img.shields.io/badge/milestone-A3.12.0%20Model%20Parity-00f6ff?style=flat-square)
![client](https://img.shields.io/badge/client-Grok%20Build%20CLI-1cf2c1?style=flat-square)
![model](https://img.shields.io/badge/model-grok--build--0.1-00aacc?style=flat-square)

Grok Build is xAI's coding CLI / headless surface. This page is the `grok-build`
entry (alias `grok`) in the [Client Integrations](Client-Integrations) catalog,
and also documents the `grok-build-acp` adapter surface. For the hosted xAI
Responses remote MCP integration, see [xAI and Grok](XAI-Grok).

The Grok Build coding model is `grok-build-0.1`.

---

## `.grok/config.toml`

Grok Build reads project MCP config from `.grok/config.toml`:

```toml
model = "grok-build-0.1"

[mcp_servers.mcos]
url = "http://<mcos-host>:8080/mcp"
startup_timeout_sec = 30
tool_timeout_sec = 6000
```

If the active gateway endpoint is HTTPS, the `url` is HTTPS. If auth is required,
use an environment-variable placeholder only — never write a secret into project
files. MCOS does not collect or proxy your xAI credentials.

Pull the generated artifact from the catalog:

```powershell
Invoke-RestMethod 'http://<mcos-host>:7300/api/client-integrations/grok-build/artifacts' |
  ConvertTo-Json -Depth 6
```

The existing `GET /api/onboarding/grok` route still works and now delegates to
this catalog entry.

---

## Setup and verification

Add the MCOS server, either by editing `.grok/config.toml` directly with the
`[mcp_servers.mcos]` table above, or with a Grok Build HTTP MCP add command in
the shape supported by your installed version:

```text
grok mcp add --transport http mcos http://<mcos-host>:8080/mcp
```

Then verify — run only the subcommands your installed Grok Build version
supports; otherwise fall back to the manual steps above:

```text
grok mcp list
grok mcp doctor mcos --json
grok inspect
```

---

## Headless

Headless runs read `$env:XAI_API_KEY` from the environment. Never write the API
key into project files.

```powershell
$env:XAI_API_KEY = "<operator-managed-secret>"
grok --no-auto-update -p "Inspect MCOS health through the mcos MCP server." --output-format streaming-json
```

---

## `grok-build-acp` — ACP over JSON-RPC stdio

Grok Build ACP runs as an external process/client adapter, **not** embedded MCOS
runtime execution. Start it with:

```text
grok agent stdio
```

ACP speaks JSON-RPC over stdin/stdout. Drive it behind an MCOS external process
adapter; never embed Grok Build execution in the MCOS core runtime. ACP is not
directly an MCP endpoint.

---

## Transport and safety

- Grok Build uses the **POST-only Streamable HTTP** subset of MCP — single JSON
  responses, no SSE (see [Gateway](Gateway)) — when the CLI accepts the endpoint.
- LAN HTTP is supported; public HTTPS is optional.
- MCOS always applies its confirm guard over mutating and destructive tools.

## Related pages

[Client Integrations](Client-Integrations) |
[xAI and Grok](XAI-Grok) |
[Onboarding](Onboarding) |
[Gateway](Gateway) |
[CLU Governance](CLU-Governance)
