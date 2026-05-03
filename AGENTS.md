# AGENTS.md — MCOS Agent Instructions

This repository is governed by `CLAUDE.md` for Claude Code and by this file for agent compatibility.

All coding agents must follow the same realignment rules:

- MCOS is a Windows-native MCP Gateway host, not an embedded AI-provider executor.
- External AI clients connect to MCOS over a trusted LAN with no application-layer authentication.
- Use the phase plan in `handoff/realignment/manifest.json`.
- Do not change code outside the active phase.
- Validate after every phase.
- Preserve Forsetti boundaries.
- Do not add AI attribution of any kind. Do not identify work as produced, assisted, reviewed, generated, co-authored, or authored by an AI system, model, bot, assistant, coding agent, vendor, or tool in commits, pull requests, issues, review comments, changelogs, release notes, documentation, source comments, generated files, or metadata under agent control unless Flynn explicitly asks for that exact attribution.
- Before finalizing any repository-authored output, remove automated-assistance signatures, model names, assistant names, vendor names, bot names, and AI-related co-author trailers.

For full instructions, read `CLAUDE.md` and `handoff/realignment/00-START-HERE.md`.
