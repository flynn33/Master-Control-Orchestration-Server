---
name: mcp-gateway-spike
description: Implement or review the MCP Gateway/MCPJungle phase.
disable-model-invocation: true
---

# MCP Gateway Spike Skill

Goal: add MCP Gateway behavior without hardwiring MCOS forever to one external gateway.

Procedure:

1. Define `IMcpGateway` interface.
2. Define `McpJungleGatewayAdapter` as the first implementation.
3. Add gateway process supervision using Windows-native process APIs.
4. Add health probe for MCPJungle.
5. Add configuration structure for gateway binary path, port, DB location, and mode.
6. Add registration path for MCOS logical endpoints.
7. Do not register autoscaled worker clones directly to MCPJungle.
8. Add tests for adapter state transitions using fakes/mocks where runtime MCPJungle cannot run.

Acceptance:

- Gateway can be configured.
- Gateway process can be started/stopped or represented by a fake adapter in tests.
- Health status is visible through API/dashboard state.
- Onboarding profile exposes one gateway MCP URL.
