# OpenAI Responses

![milestone](https://img.shields.io/badge/milestone-A3.12.0%20Model%20Parity-00f6ff?style=flat-square)
![client](https://img.shields.io/badge/client-OpenAI%20Responses%20API-1cf2c1?style=flat-square)
![endpoint](https://img.shields.io/badge/endpoint-public%20HTTPS%20required-ff8c00?style=flat-square)

The OpenAI Responses API can call MCOS as a **remote MCP** server. This page is
the `openai-responses` entry in the [Client Integrations](Client-Integrations)
catalog (alias `openai`).

Responses is hosted in OpenAI's environment, so it requires a reachable
**public HTTPS** endpoint. A LAN-only MCOS gateway is not reachable from hosted
OpenAI — expose MCOS through an approved public HTTPS deployment or a
[connector edge](ChatGPT-Apps) bridge first.

---

## The remote MCP tool object

A Responses API request attaches MCOS as a `type: "mcp"` tool. The model is
**operator-selected** — MCOS does not hardcode a model.

```json
{
  "model": "<operator-selected-model>",
  "tools": [
    {
      "type": "mcp",
      "server_label": "mcos",
      "server_description": "MCOS governed orchestration gateway",
      "server_url": "https://<public-or-edge-mcos-host>/mcp",
      "require_approval": "always",
      "allowed_tools": ["mcos_read_status", "mcos_list_endpoints"]
    }
  ],
  "input": "Inspect MCOS orchestration health."
}
```

Key fields:

- `server_url` — the reachable public/edge HTTPS endpoint. When the live gateway
  is LAN-only or plain HTTP, the generated artifact emits a
  `https://<public-or-edge-mcos-host>/mcp` placeholder rather than a dead LAN URL.
- `server_label` / `server_description` — how the tool surface identifies itself.
- `allowed_tools` — keep this to the gateway's advertised **read-only** tools.
- `require_approval` — use it for anything mutating. This field is OpenAI-specific;
  it is not part of the [xAI](XAI-Grok) shape.

MCOS does not collect or proxy your OpenAI API key; use `${OPENAI_API_KEY}` in
your own client. Generated artifacts never contain real secrets.

Pull the generated artifact:

```powershell
Invoke-RestMethod 'http://<mcos-host>:7300/api/client-integrations/openai-responses/artifacts' |
  ConvertTo-Json -Depth 6
```

---

## Verification

1. Deploy or bridge MCOS to a reachable public HTTPS endpoint.
2. Issue a Responses API request with the MCP tool object and confirm
   `tools/list` resolves.
3. Confirm destructive tools are excluded from `allowed_tools` or gated by
   `require_approval`.

You can also run the catalog's compatibility check, which flags a LAN-only or
plain-HTTP gateway as blocking for this hosted integration:

```powershell
Invoke-RestMethod 'http://<mcos-host>:7300/api/client-integrations/openai-responses/validate' |
  ConvertTo-Json -Depth 6
```

---

## Transport and safety

- The MCOS gateway itself remains the **POST-only Streamable HTTP** subset with
  **no SSE** (see [Gateway](Gateway)); the public/edge deployment is responsible
  for whatever transport hosted OpenAI negotiates.
- MCOS always applies its confirm guard over mutating and destructive tools,
  in addition to any `require_approval` the client sets.

## Related pages

[Client Integrations](Client-Integrations) |
[ChatGPT Apps](ChatGPT-Apps) |
[xAI and Grok](XAI-Grok) |
[Gateway](Gateway) |
[TLS and HTTPS](TLS-and-HTTPS)
