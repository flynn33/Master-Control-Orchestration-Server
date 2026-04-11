# Master Control Orchestration Server — Remote Client

Remote operator and remote agent onboarding direction. The product is single-host, 
but agents (Codex, Claude Code, custom MCP servers) routinely connect from other 
processes on the same machine — and occasionally over a tunnel from another host.

---

## Onboarding directions

### Codex / Claude Code (local)

1. Launch the orchestration server.
2. Open the desktop shell, navigate to **Providers**.
3. Use **Auto-Connect** with the appropriate credential.
4. Assign roles (`planner`, `coder`, etc.) — CLU routes from there.

### Custom MCP server

1. Stand up the MCP server on a loopback port.
2. POST to `/api/runtime/mcp-servers` with the endpoint definition.
3. The runtime probes the endpoint asynchronously and exposes it through `/api/dashboard`.

### Remote operator over tunnel

The admin API binds to `127.0.0.1:7300`. To reach it from another host, use a trusted 
transport — `ssh -L 7300:127.0.0.1:7300 user@host` is the canonical pattern. The product 
intentionally does not expose the API on an external interface.

---

## Discovery

The Beacon module advertises the runtime on the local network for trusted clients. 
Beacon state is exposed via `/api/beacon`.

---

See also: [Auto-Connect AI](Auto-Connect-AI) · [API Reference](API-Reference) · 
[Architecture](Architecture)
