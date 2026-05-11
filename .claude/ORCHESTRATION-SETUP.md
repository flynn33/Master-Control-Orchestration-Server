# MCOS Local Orchestration Setup

This file documents the MCP servers and sub-agents configured for working on MCOS from a Claude Code session on this Windows host. It is a setup index, not a contract — change the underlying `.mcp.json` and `.claude/agents/*.md` files when you change the setup.

## MCP servers

Configured in `.mcp.json` at the project root. Claude Code loads them at session start; **restart the session after edits to `.mcp.json` for changes to take effect**.

### Project-scoped MCP servers (`.mcp.json`)

| Name | Source | Purpose |
|---|---|---|
| `memory` | `@modelcontextprotocol/server-memory` (npx) | Generic memory + knowledge graph persistence at `.remember/memory.jsonl` |
| `sequential-thinking` | `@modelcontextprotocol/server-sequential-thinking` (npx) | Step-by-step reasoning scratchpad |
| `filesystem` | `@modelcontextprotocol/server-filesystem` (npx) | Sandboxed filesystem rooted at the project |
| `sqlite` | `mcp-server-sqlite-npx` (npx) | Local SQLite at `.claude/mcp-state/mcos-orchestration.sqlite` |
| `mcos-memory` | Project-local Python | MCOS-specific memory adapter |
| `mcos-contracts` | Project-local Python | MCOS contracts validator |
| `mcos-watcher` | Project-local Python | File-watcher MCP — `watch_start`/`watch_poll`/`watch_list`/`watch_stop`/`watch_reset` over a path with include/exclude globs |
| `mcos-bridge` | Plugin Python | Bridge to running MCOS service (port 7300) |

Verified launch (2026-05-08): sequential-thinking, filesystem, and sqlite all spawn cleanly via npx on this host.
Verified launch (2026-05-10): mcos-watcher initialize + tools/list + watch_start + watch_list all return valid JSON-RPC; MCOS service responds 200 on `:7300/api/health` so mcos-bridge has a live target.

### Capabilities provided by built-in tools / system MCPs (no project config needed)

These items from the requested orchestration list do not need project-level MCP entries because they are already available through Claude Code's built-in tools or the user-global plugin set:

| Requested capability | How it is provided here |
|---|---|
| Filesystem MCP (general) | Built-in `Read`, `Write`, `Edit`, `Glob`, `Grep` tools |
| File Search MCP | Built-in `Grep` (ripgrep) and `Glob` |
| Terminal / Shell MCP | Built-in `Bash` and `PowerShell` tools |
| Code Execution / REPL MCP | Built-in `Bash` (with Python 3.9 + Node 22 available) |
| Local Test Runner MCP | Built-in `Bash` invoking `ctest --preset debug` |
| Local Build Tool MCP | Built-in `Bash` invoking `cmake`, `MSBuild` (VS 2022 installed) |
| Local Git MCP | Built-in `Bash` invoking `git` from `C:\Program Files\Git` |
| Local Linter MCP | Built-in `Bash` invoking project-level lint commands |
| Knowledge Graph MCP | `memory` server above (Anthropic memory MCP IS the knowledge graph) |
| Persistent Context MCP | Auto-memory at `~/.claude/projects/G--Claude-Master-Control-Orchastration-Server/memory/` plus `memory` server |
| Computer Use MCP | System-wide `mcp__computer-use__*` tools |
| Desktop Control MCP | System-wide Desktop Commander (`mcp__Desktop_Commander__*`) |
| Screen Capture / Vision MCP | `mcp__computer-use__screenshot` |
| Keyboard & Mouse Control MCP | `mcp__computer-use__*` (left_click, type, scroll, etc.) |
| Playwright MCP | User-global plugin `playwright` (`mcp__plugin_playwright_playwright__*`) |
| Chrome DevTools MCP | User-global plugin `chrome-devtools-mcp` |
| Local Indexer MCP | User-global plugin `clangd-lsp` for C++ semantic indexing |

### Capabilities not configured

- **Docker Control MCP** — Docker Desktop / Docker Engine is **not installed** on this host (verified 2026-05-08). MCOS hard rule `.claude/rules/10-windows-native-cpp.md` states Docker is a development/testing option only, not the required Windows production path, so this is consistent with project policy. Install Docker Desktop and add an entry to `.mcp.json` if Docker-backed workflows are needed.

## Sub-agents

All sub-agents live in `.claude/agents/`. Each is a markdown file with YAML frontmatter (`name`, `description`, `tools`, `model`) followed by the system prompt.

### MCOS-specific agents (pre-existing)

| Agent | Trigger |
|---|---|
| `mcos-architect` | Phase-aware architecture review against realignment manifest |
| `mcos-code-reviewer` | Diff review against MCOS hard rules + FORBIDDEN-CONTRACT |
| `mcos-contract-auditor` | Public-contract diff audit |
| `mcos-debugger` | MCOS-specific failure triage |
| `mcos-phase-planner` | Phase scope and validation gating |
| `mcos-researcher` | Project-context research |
| `mcp-gateway-reviewer` | MCPJungle / IMcpGateway topology review |
| `windows-native-cpp-reviewer` | Windows API conformance review |
| `forsetti-governance-reviewer` | Forsetti compliance review |
| `qa-release-gate` | Release-phase quality gate |

### Generalist agents (added by this setup)

| Agent | Trigger |
|---|---|
| `architect` | Generic architecture decisions, ADR drafting; pairs with `mcos-architect` |
| `sentinel` | Always-on guard for hard-rule / FORBIDDEN-CONTRACT / silent-failure regressions |
| `planner` | File-by-file implementation plan |
| `reviewer` | Lighter generic diff review (engineering quality, not realignment rules) |
| `debugger` | Root-cause triage, repro production |
| `test-writer` | Adds new tests honoring existing fixture conventions |
| `refactorer` | Cleanup without changing observable behavior |
| `documenter` | Updates README/ADR/wiki/handoff docs to match code |
| `performance-engineer` | Measure-first performance analysis |
| `security-auditor` | Pre-commit security review (LAN trust model aware) |
| `git-manager` | Branch/commit/conflict workflows; honors `.claude/settings.json` deny list |
| `build-resolver` | CMake/MSBuild/vcpkg failure diagnosis |
| `coordinator` | Pipeline planning across multiple specialists |
| `multi-file-specialist` | Sweeping renames / glossary migrations |

### When to call which

Use the `coordinator` agent for tasks that span ≥3 specializations. For single-domain tasks call the specialist directly. For project-rule enforcement always end with `sentinel` and/or `mcos-code-reviewer` before commit.

## Resource pool authorization

Both the MCP-server pool and the sub-agent pool are authorized to consume **up to 90% of host CPU and 90% of physical memory**. Caps are enforced via Windows Job Objects (the project-mandated supervision API per `.claude/rules/10-windows-native-cpp.md`).

### Policy (declared in `.claude/mcp-state/pool-policy.json`)

| Pool | Job Object name | CPU cap | Memory cap | Affinity |
|---|---|---|---|---|
| `mcp-servers` | `Local\MCOS-MCP-ServerPool` | 90% (`HARD_CAP`) | 28,719,673,344 bytes (~26.75 GiB) | full 16-core mask |
| `sub-agents` | `Local\MCOS-SubAgentPool` | 90% (`HARD_CAP`) | 28,719,673,344 bytes (~26.75 GiB) | full 16-core mask |

Caps are independent per-pool, not summed — both pools may saturate to 90% concurrently and the Windows scheduler arbitrates contention. The cumulative authorization matches the operator-stated "up to 90%" intent.

### Components (under `.claude/scripts/`)

| Script | Role |
|---|---|
| `pool-governor.psm1` | PowerShell module wrapping `kernel32!CreateJobObjectW` / `SetInformationJobObject` / `AssignProcessToJobObject` / `QueryInformationJobObject` via P/Invoke. Exports `Initialize-McosResourcePool`, `Connect-McosResourcePool`, `Add-ProcessToMcosResourcePool`, `Get-McosResourcePoolStatus`, `Test-PoolGovernor`. |
| `pool-init.ps1` | Idempotent. Creates both Job Objects with policy limits, spawns one anchor per pool, spawns the sweeper. Wired into `SessionStart` in `.claude/settings.json`. |
| `pool-anchor.ps1` | One per pool. Joins the named job and holds the kernel handle for the session lifetime so the named object remains addressable from sibling processes (without an anchor the kernel reaps the named object the moment all handles close). |
| `pool-sweeper.ps1` | Background loop (3 s interval). Walks Claude Code's process tree and attaches each descendant to the right pool — `claude.exe` → sub-agents, `node.exe` / `py.exe` / `powershell.exe` / `cmd.exe` → mcp-servers. Logs every attach/skip to `.claude/mcp-state/pool-sweeper.log`. |
| `pool-spawn.ps1` | Optional launch-time wrapper for `.mcp.json` entries. Joins the pool before exec'ing the inner command so the child inherits the cap from its first instruction. Currently the sweeper covers this path; use the wrapper when guaranteed-from-start enforcement matters. |
| `pool-status.ps1` | Honest status reader. Reports real Job Object accounting (`ActiveProcesses`, `TotalUserTime`, `TotalKernelTime`, `PeakJobMemoryUsed`, `JobMemoryLimit`) — no fake metrics. |
| `pool-attach-subagents.ps1` | Standalone single-pool attacher (kept for ad-hoc invocation; the sweeper supersedes it for normal operation). |

### Verified behavior (2026-05-10)

- Both Job Objects are visible across processes in session 2 via `Local\` namespace.
- 90% `HARD_CAP` and 26.75 GiB `JobMemoryLimit` are read back correctly by `QueryInformationJobObject`.
- A 4-worker CPU burner launched through `pool-spawn.ps1` produced 11 in-pool processes (wrapper + children + Start-Job descendants), 917 s of accounted CPU, 13 GB peak memory — all real kernel-reported numbers.
- The sweeper attached 3 of 19 Claude Code descendants in its first sweep on the live session. The other 16 returned `Win32 error 5` (`ERROR_ACCESS_DENIED`) because they already sit in another `HARD_CAP` Job Object that Windows won't let us nest under. Those skips are logged honestly — the pool does NOT silently claim to govern processes it cannot attach (per CLAUDE.md "no fake telemetry").

### Operating

```powershell
# Apply policy + start anchors + start sweeper (idempotent; runs at SessionStart):
powershell -NoProfile -ExecutionPolicy Bypass -File .claude/scripts/pool-init.ps1

# Live status (real Job Object accounting):
powershell -NoProfile -ExecutionPolicy Bypass -File .claude/scripts/pool-status.ps1

# Tail sweeper attachments:
Get-Content .claude/mcp-state/pool-sweeper.log -Wait
```

To change the cap, edit `pool-policy.json` and restart the session (or kill the anchors/sweeper PIDs in `.claude/mcp-state/pool-*.pid` and re-run `pool-init.ps1`).

## Reload procedure

After editing `.mcp.json` or `.claude/agents/*.md`:

1. Save changes.
2. Exit the current Claude Code session.
3. Start a fresh session in this directory.
4. Verify the new MCPs appear in the session-start system reminders, and verify agents are listed in `Agent` tool subagent_type completion.

## Validation note

Per CLAUDE.md, every implementation phase ends with the validation chain:

```powershell
cmake --preset debug
cmake --build --preset debug
ctest --preset debug --output-on-failure
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\check-mastercontrol-forsetti.ps1
```

This setup file does not modify MCOS source and does not require running that chain. If a future change here touches scripts that the build relies on, run the chain.
