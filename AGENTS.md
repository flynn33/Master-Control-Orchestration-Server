# AGENTS.md — MCOS Agent Instructions

This repository is governed by `CLAUDE.md` for Claude Code and by this file for agent compatibility.

All coding agents must follow the same realignment rules:

- MCOS is a Windows-native MCP Gateway host, not an embedded AI-provider executor.
- External AI clients connect to MCOS over a trusted LAN with no application-layer authentication.
- Use the phase plan in `handoff/realignment/manifest.json`.
- Do not change code outside the active phase.
- Validate after every phase.
- Preserve Forsetti boundaries.

For full instructions, read `CLAUDE.md` and `handoff/realignment/00-START-HERE.md`.
