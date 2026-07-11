# Codex

![milestone](https://img.shields.io/badge/milestone-A3.12.0%20Model%20Parity-00f6ff?style=flat-square)
![client](https://img.shields.io/badge/client-Codex%20CLI%20%2F%20IDE-1cf2c1?style=flat-square)
![config](https://img.shields.io/badge/config-config.toml-00aacc?style=flat-square)

Codex (OpenAI Codex CLI and IDE extension) connects to the MCOS MCP Gateway as a
Streamable HTTP MCP server. This page is the `codex` entry in the
[Client Integrations](Client-Integrations) catalog.

Codex MCP configuration is **TOML** (`config.toml`). Legacy JSON
(`codex.config.json` / `codex-mcp.json`, `~/.codex/config.json`) is **not** the
current primary format — do not use it as the primary config.

---

## Config locations

Codex reads MCP servers from `config.toml`. The CLI and IDE share config.

| Scope | Path |
|---|---|
| User | `~/.codex/config.toml` |
| Project | `.codex/config.toml` |

---

## The `[mcp_servers.mcos]` table

Add this table to `~/.codex/config.toml` (user scope) or `.codex/config.toml`
(project scope):

```toml
[mcp_servers.mcos]
url = "http://<mcos-host>:8080/mcp"
enabled = true
startup_timeout_sec = 10
tool_timeout_sec = 60
```

The `url` is templated from the live gateway state (LAN HTTP by default). If auth
is configured for your endpoint, use an environment-variable placeholder rather
than writing a secret into the file — MCOS does not collect or proxy your OpenAI
credentials.

Pull the generated artifact from the catalog:

```powershell
Invoke-RestMethod 'http://<mcos-host>:7300/api/client-integrations/codex/artifacts' |
  ConvertTo-Json -Depth 6
```

The existing `GET /api/onboarding/codex` route still works and now delegates to
the same catalog entry.

---

## Verification

After editing `config.toml`, restart Codex (CLI or IDE) so it re-reads the file,
then confirm the server registered:

```text
codex mcp list
codex mcp --help
```

Then run a Codex session and confirm tool calls land at the MCOS gateway URL.

---

## Transport and safety

- Transport is the **POST-only Streamable HTTP** subset of MCP — single JSON
  responses, no SSE (see [Gateway](Gateway)).
- LAN HTTP is supported; public HTTPS is optional.
- Codex approves tool calls in-session, and MCOS still applies its confirm guard
  over mutating and destructive tools.

## Related pages

[Client Integrations](Client-Integrations) |
[Onboarding](Onboarding) |
[Gateway](Gateway) |
[CLU Governance](CLU-Governance)
