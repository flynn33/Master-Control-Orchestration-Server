# ChatGPT Apps

![milestone](https://img.shields.io/badge/milestone-A3.12.0%20Model%20Parity-00f6ff?style=flat-square)
![client](https://img.shields.io/badge/client-ChatGPT%20Apps%20%2F%20Connectors-1cf2c1?style=flat-square)
![endpoint](https://img.shields.io/badge/endpoint-public%20HTTPS%20%2B%20OAuth%202.1-ff8c00?style=flat-square)

ChatGPT Apps and Connectors reach MCOS through an approved public MCP endpoint.
This page covers two catalog entries: `chatgpt-apps` (alias `chatgpt`) and
`chatgpt-connector-edge`. Both live in the
[Client Integrations](Client-Integrations) catalog.

Hosted ChatGPT runs in OpenAI's environment. **It cannot reach a LAN-only HTTP
MCOS gateway directly.** A public HTTPS surface, and in practice a LAN-to-public
edge bridge, is required.

---

## `chatgpt-apps` requirements

A ChatGPT App / connector expects:

- A **public HTTPS `/mcp`** endpoint (the connector/app surface).
- **OAuth 2.1** authorization-code + **PKCE** for user-linked tools.
- **Protected resource metadata** served at the endpoint.
- **Client identity** for ChatGPT connector traffic (OpenAI-managed mTLS where
  implemented).
- **Read-only tools by default.** Destructive operations stay hidden unless
  explicit gateway policy, identity verification, and confirm guards are in
  place.

Do not rely on custom API keys as the primary ChatGPT-facing auth story — the
OAuth 2.1 + PKCE + protected-resource-metadata path is the primary model.

Pull the generated guidance artifact:

```powershell
Invoke-RestMethod 'http://<mcos-host>:7300/api/client-integrations/chatgpt-apps/artifacts' |
  ConvertTo-Json -Depth 6
```

---

## `chatgpt-connector-edge` boundary

Because hosted ChatGPT cannot reach the LAN gateway directly, a **LAN-to-public
edge bridge** sits in front of MCOS. The edge policy is strict:

- Expose **only approved MCP tool calls** — never raw LAN admin `/api/*`
  surfaces.
- Enforce **HTTPS and authentication** at the public boundary.
- **Preserve audit context** (client identity, tool name, timestamp) end to end.
- Keep the MCOS gateway itself **LAN-trusted behind the bridge**.

The edge terminates TLS and auth; the MCOS gateway stays on its trusted LAN
posture. See [TLS and HTTPS](TLS-and-HTTPS) and
[Windows Firewall and LAN Mode](Windows-Firewall-LAN-Mode) for the host-side
network controls.

---

## Verification

1. Publish MCOS behind an approved public HTTPS `/mcp` connector/app endpoint.
2. Confirm OAuth 2.1 authorization-code + PKCE and protected resource metadata
   are served.
3. Confirm read-only tools are default and destructive tools stay hidden without
   explicit policy.

The catalog compatibility check flags a LAN-only gateway as blocking and marks
that an edge bridge is required:

```powershell
Invoke-RestMethod 'http://<mcos-host>:7300/api/client-integrations/chatgpt-apps/validate' |
  ConvertTo-Json -Depth 6
```

---

## Transport and safety

- The MCOS gateway remains the **POST-only Streamable HTTP** subset with **no
  SSE** (see [Gateway](Gateway)); the connector edge is responsible for whatever
  transport hosted ChatGPT negotiates.
- MCOS always applies its confirm guard over mutating and destructive tools.

## Related pages

[Client Integrations](Client-Integrations) |
[OpenAI Responses](OpenAI-Responses) |
[Gateway](Gateway) |
[TLS and HTTPS](TLS-and-HTTPS) |
[CLU Governance](CLU-Governance)
