## Remote Client Onboarding - Plan & Status

### Objective
Remote coding agents such as Claude Code and Codex should be able to discover Master Control Orchestration Server on the LAN, select the correct platform gateway, and receive a ready-to-apply configuration without manual editing.

### Current Direction
Master Control Orchestration Server now publishes platform-aware gateway lanes and exposes configuration payloads through the built-in runtime rather than an external aggregator stack.

### Onboarding Shape
1. Discover the orchestration server over the LAN.
2. Select the appropriate platform gateway for Windows, macOS, or iOS.
3. Retrieve the client configuration payload from the server.
4. Apply the MCP/gateway entry to the local agent configuration.
5. Verify browser reachability and gateway health from the client machine.

### Current Constraints
- Keep onboarding lightweight for client agents.
- Prefer server-generated configuration over hand-edited files.
- Support both desktop-first and CLI-first client workflows.
- Preserve fallback instructions for clients that cannot consume automatic discovery.

### Follow-Up Work
- add guided client-onboarding export bundles from the shell and browser
- document the exact client configuration format for Claude Code and Codex
- validate onboarding against real Windows, macOS, and iOS-adjacent client setups
