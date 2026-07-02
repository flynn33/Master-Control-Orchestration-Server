# WS2 Completion Report - Capability Gating

## Project
Master Control Orchestration Server

## Objective
Make high-risk MCP tools and mutating admin routes require explicit capabilities, with denied calls failing closed and leaving audit evidence.

## Completed Work
- Added shared capability policy strings for process execution, filesystem read/write, desktop control, input synthesis, screen capture, network admin, package install, governance mutation, client management, and supervisor assignment.
- Extended LAN clients with explicit `capabilities` while preserving legacy privilege booleans through compatibility mapping.
- Added route-level capability checks for mutating admin/control endpoints using the remediation package route policy.
- Added MCP gateway filtering so `tools/list` hides tools whose required capabilities are missing.
- Added MCP gateway `tools/call` enforcement with JSON-RPC capability-denied errors and `capability_denied` audit events.
- Added risk/capability metadata to MCP tool descriptors and setup/runtime endpoint JSON.
- Surfaced risk/capability labels in shell runtime summaries and selector labels.
- Added unit coverage for default-deny client capabilities, legacy privilege mapping, tool capability matrix entries, and local bootstrap capabilities.

## Validation
- `.\scripts\Build-MasterControlOrchestrationServer.ps1 -Preset debug` passed, including all 4 CTest tests.
- `git diff --check` passed.
- `Test-MCOSStaticGates.ps1` still reports 9 expected later-workstream failures; WS1/auth static checks remain passing.

## Remaining Risks
- Supervisor assignment token exceptions are still handled by the later WS6 workstream; current route capability mapping uses `supervisor.assign`.
- Process execution, setup completion, and workflow static-gate failures remain for WS3-WS5.
