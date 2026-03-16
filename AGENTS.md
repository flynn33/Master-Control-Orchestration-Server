# AGENTS.md

## Operating Mode
You must operate with a strict MCP Gateway-first workflow.

## Core Rules
- Use the configured MCP Gateway before any non-gateway tool, workflow, direct integration, or external search.
- Prefer sub-agents available through the MCP Gateway whenever they fit the task.
- Before continuing project work, read prior context from the Memory MCP server through the gateway.
- During and after meaningful work, sync updated project state back to Memory MCP.
- Non-gateway execution is allowed only after gateway discovery has been attempted and a concrete fallback reason exists.
- After a fallback step, return to gateway-first behavior immediately.

## Required Sequence
1. Interpret the task and identify needed capabilities.
2. Discover gateway tools, workflows, Memory MCP access, and sub-agents.
3. Read existing Memory MCP context before planning or editing project work when relevant.
4. Choose the best matching gateway sub-agent or tool.
5. Execute through gateway-managed capabilities whenever possible.
6. Validate using gateway-exposed testing, review, linting, or inspection capabilities when available.
7. Sync meaningful updates to Memory MCP.
8. Only then use a documented non-gateway fallback if necessary.
9. Return to the gateway as soon as the fallback condition no longer applies.

## Memory MCP Minimum Sync Content
When syncing project context, include these items when relevant:
- project identifier
- current objective
- active task
- completed work
- design or architecture decisions
- constraints
- blockers or risks
- important files or modules touched
- validation results
- next steps or handoff notes

## Fallback Documentation
If fallback is required, document:
- the discovery attempt
- the expected gateway capability
- why it could not be used
- the fallback resource chosen
- the point at which gateway-first execution resumes

## Execution Note Template
Gateway discovery: <performed/not performed>. Memory context read: <yes/no>. Gateway capability selected: <name or none>. Validation path: <method or none>. Memory sync completed: <yes/no>. Fallback: <yes/no>. Fallback reason: <reason or none>.
