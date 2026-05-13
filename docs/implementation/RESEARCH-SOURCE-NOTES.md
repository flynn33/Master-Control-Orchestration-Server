# Research Source Notes

These are source categories Claude Code should verify again during implementation if exact commands/API syntax matter.

## Claude Code instruction architecture

- `CLAUDE.md` for persistent project instructions.
- `.claude/rules/` for large-project scoped rules.
- `.claude/settings.json` for project settings and hooks.
- `.claude/skills/<name>/SKILL.md` for reusable task procedures.
- `.claude/agents/*.md` for specialized subagents.

## MCP Gateway

- Native HTTP.sys adapter is the current and only shipping gateway substrate (v0.9.0+). MCPJungle was retired in the same release. The substrate-agnostic `IMcpGateway` interface preserved the option to swap later, which made the v0.9.0 retirement a clean delete.
- Microsoft MCP Gateway is useful as a reference pattern for session-aware routing and lifecycle management, but it is Kubernetes-oriented and not applicable to the Windows-native in-process implementation.

## LAN discovery

- DNS-SD/mDNS is the Bonjour-style mechanism.
- Use Windows DNS-SD APIs first where possible.
- Use Bonjour/mDNSResponder only as a deliberate fallback.

## MCP transport

- Use Streamable HTTP as the primary transport.
- Treat old SSE support as compatibility-only.
- Preserve session affinity where the MCP session model requires it.

