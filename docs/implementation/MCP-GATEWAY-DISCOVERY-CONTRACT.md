# MCP Gateway and Discovery Contract

## Discovery document

Path: `/.well-known/mcos.json`

```json
{
  "product": "MCOS",
  "role": "mcp-gateway-host",
  "version": "0.0.0-phase",
  "instanceId": "mcos-hostname-guid",
  "trust": "lan",
  "auth": "none",
  "gateway": {
    "type": "native",
    "mcpUrl": "http://HOST:PORT/mcp",
    "healthUrl": "http://HOST:PORT/health"
  },
  "onboarding": {
    "generic": "http://HOST:7300/api/onboarding/generic",
    "claudeCode": "http://HOST:7300/api/onboarding/claude-code",
    "codex": "http://HOST:7300/api/onboarding/codex",
    "grok": "http://HOST:7300/api/onboarding/grok"
  },
  "governance": {
    "bundleBaseUrl": "http://HOST:7300/api/governance/bundles",
    "cluProfileUrl": "http://HOST:7300/api/governance/profile"
  },
  "capabilities": [
    "mcp-gateway",
    "the in-process HTTP.sys adapter-adapter",
    "dns-sd",
    "udp-beacon",
    "forsetti-governance",
    "clu"
  ]
}
```

## DNS-SD advertisement

Recommended service records:

| Service type | Purpose |
|---|---|
| `_mcos._tcp.local` | General MCOS service discovery. |
| `_mcos-mcp._tcp.local` | MCP Gateway discovery. |
| `_mcos-onboarding._tcp.local` | Client configuration/onboarding discovery. |

TXT fields:

```text
product=MCOS
role=mcp-gateway
gateway=native
mcp_path=/mcp
config_path=/api/onboarding
governance_path=/api/governance/bundles
protovers=2025-03-26
auth=none
trust=lan
clu=true
forsetti=true
```

## UDP beacon fallback

The UDP beacon should contain the same core fields as `/.well-known/mcos.json`, plus timestamp and server IP/interface metadata.

## Model-specific onboarding

Each profile returns:

- `clientType`
- `displayName`
- `gatewayMcpUrl`
- `transport`
- `authRequired=false`
- `governanceBundleUrl`
- `configSnippets[]`
- `manualInstructions[]`
- `verificationSteps[]`

