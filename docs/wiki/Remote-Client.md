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

### Remote operator access

On the host machine, the packaged browser shortcut opens `http://127.0.0.1:7300/`.
Depending on host configuration, the listener may also be reachable on the LAN. Until
transport hardening lands in a future phase, prefer a trusted transport for remote
administration — `ssh -L 7300:127.0.0.1:7300 user@host` is the canonical pattern, and a
trusted reverse proxy is the supported alternative when you need shared remote access.

---

## Discovery

The Beacon module advertises the runtime on the local network for trusted clients. 
Beacon state is exposed via `/api/beacon`.

---

See also: [Auto-Connect AI](Auto-Connect-AI) · [API Reference](API-Reference) · 
[Architecture](Architecture)
