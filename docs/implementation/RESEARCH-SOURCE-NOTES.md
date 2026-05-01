# Research Source Notes

These are source categories Claude Code should verify again during implementation if exact commands/API syntax matter.

## Claude Code instruction architecture

- `CLAUDE.md` for persistent project instructions.
- `.claude/rules/` for large-project scoped rules.
- `.claude/settings.json` for project settings and hooks.
- `.claude/skills/<name>/SKILL.md` for reusable task procedures.
- `.claude/agents/*.md` for specialized subagents.

## MCP Gateway

- MCPJungle is the preferred initial gateway substrate.
- MCPJungle provides a single MCP endpoint, supports Streamable HTTP and stdio backend registration, and can run as a host binary or container.
- Microsoft MCP Gateway is also useful as a reference pattern for session-aware routing and lifecycle management, but it is Kubernetes-oriented and not the preferred Windows-native first implementation.

## LAN discovery

- DNS-SD/mDNS is the Bonjour-style mechanism.
- Use Windows DNS-SD APIs first where possible.
- Use Bonjour/mDNSResponder only as a deliberate fallback.

## MCP transport

- Use Streamable HTTP as the primary transport.
- Treat old SSE support as compatibility-only.
- Preserve session affinity where the MCP session model requires it.

