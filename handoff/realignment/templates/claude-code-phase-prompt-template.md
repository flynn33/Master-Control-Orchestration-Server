# Claude Code Phase Prompt Template

Paste this into Claude Code, replacing the phase ID and file name.

```text
Implement PHASE-XX only.

Read first, in order:
1. CLAUDE.md
2. handoff/realignment/manifest.json
3. handoff/realignment/PHASE-XX-<name>.md
4. All files listed under readFirst for PHASE-XX

Before editing:
- summarize current repo evidence
- identify conflicts with the phase objective
- produce a file-by-file edit plan
- list tests/validation to run

Do not:
- broaden scope beyond PHASE-XX
- change unrelated UI or packaging
- reintroduce direct AI-provider execution
- register autoscaled worker clones directly as public MCP servers
- modify vendored Forsetti framework code

After editing:
- run the strongest available validation
- create/update the phase completion report
- list changed files
- list validation commands/results
- list risks and deferred work
- stop; do not start the next phase
```
