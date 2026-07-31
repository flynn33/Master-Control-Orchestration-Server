# MCOS — parked reference

**Status:** Parked as of 2026-07-31.  
**Last package:** [vA3.12.0](https://github.com/flynn33/Master-Control-Orchestration-Server/releases/tag/vA3.12.0)  
**Policy:** No active feature development. Repo kept for history, patterns, and optional revival of the LAN gateway niche.

## Why parked

MCOS began with a broader “application factory” ambition. What shipped is a
Windows-native **LAN MCP gateway host** (service, pools, discovery, onboarding,
governance distribution, MSI). Daily agent orchestration on the primary
development rig moved to **Forge-Conductor** (stdio MCP) and **Grok Build**.

Those two layers are cheaper to evolve for single-host app production than a
second always-on C++ control plane. Park preserves the investment without
keeping MCOS on the critical path.

## Successor stack (primary rig)

| Role | Tool |
|------|------|
| Interactive engineering partner | Grok Build (TUI / headless) |
| Local stdio orchestration, tools, memory, audit | Forge-Conductor |
| Shared durable host policy | Machine charter (`Agents.md` / operator notes) |

Do **not** reinstall MCOS as a competing orchestration brain on the primary rig
unless a multi-host LAN fabric is an explicit goal.

## Harvest list (worth reusing)

Copy ideas into Forge/Grok when needed; do not re-home the whole service.

1. **Client Integration Catalog** — provider-neutral descriptors, aliases,
   artifact shapes, validate routes → inform Forge `register` / onboarding docs.
2. **Installer / bootstrapper patterns** — WiX harvest, runtime DLL staging
   (`sqlite3.dll` lesson), preflight JSON, service + firewall lifecycle — if
   Forge ever needs a Windows service package.
3. **Worker pool + Job Object supervision** — process containment and lease
   routing patterns for multi-worker hosts.
4. **Discovery document** — `/.well-known/mcos.json`, onboarding URL map,
   governance bundle base URLs — if a LAN product surface returns.
5. **Forsetti / CLU governance framing** — scope-before-action doctrine text
   and bundle distribution concepts (not the full MCOS runtime).
6. **Working-alpha readiness signals** — health summary gate checklist for any
   future packaged host product.

## What not to rebuild here

- Full native HTTP.sys MCP gateway “because it exists”
- Dual orchestration (MCOS + Forge) for the same host models
- “Application factory” as a rewrite of this tree — factory rails belong in
  templates, CI, Forge skills, and Grok workflows

## When to un-park

Only if at least one need is real and written down:

- Multiple LAN machines need one shared MCP fabric
- A Windows service + firewall + discovery **product** is the deliverable
- Field validation requires the MSI path already proven at vA3.12.0

Otherwise leave parked. Prefer Forge + Grok for building applications.

## Local install policy (primary rig)

- **Do not** keep MCOS installed as an automatic service by default.
- Artifacts of record: GitHub release assets only.
- Optional: clone the repo for reading history; no need to run the service.
