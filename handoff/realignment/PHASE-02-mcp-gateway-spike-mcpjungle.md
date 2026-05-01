---
phase: PHASE-02
label: MCP Gateway spike with MCPJungle adapter
objective: Add replaceable MCP Gateway adapter and supervised MCPJungle integration spike.
---


# PHASE-02 — MCP Gateway Spike with MCPJungle Adapter

## Goal

Implement a replaceable MCP Gateway adapter and use MCPJungle as the first gateway substrate.

## Required changes

- Define `IMcpGateway`.
- Define `McpJungleGatewayAdapter`.
- Add gateway configuration model.
- Add supervised process lifecycle for MCPJungle where possible.
- Add fake/mock gateway for tests.
- Add health endpoint/state visible to runtime and dashboard.
- Register one MCOS logical MCP endpoint with gateway adapter.

## Required interface sketch

```cpp
class IMcpGateway {
public:
    virtual ~IMcpGateway() = default;
    virtual GatewayStatus Start() = 0;
    virtual GatewayStatus Stop() = 0;
    virtual GatewayHealth Probe() = 0;
    virtual RegistrationResult RegisterHttpServer(const McpServerRegistration& server) = 0;
    virtual RegistrationResult RegisterStdioServer(const McpServerRegistration& server) = 0;
    virtual DeregistrationResult DeregisterServer(const std::string& serverName) = 0;
    virtual std::vector<McpToolDescriptor> ListTools() = 0;
    virtual std::string GatewayMcpUrl() const = 0;
};
```

## Do not

- Do not make MCPJungle a hard, unreplaceable dependency.
- Do not register autoscaled clones as separate public servers.
- Do not make Docker mandatory for production.

## Exit criteria

- One gateway MCP URL exists.
- Gateway health is visible.
- Test fake can simulate start/stop/probe/register.

## Read first

- `src/MasterControlApp/MasterControlRuntime.cpp`
- `src/MasterControlServiceHost`
- `src/MasterControlModules`
- `CMakeLists.txt`
- `docs/implementation/MCOS-REALIGNMENT-MASTER.md`

## Deliverables

- IMcpGateway interface
- McpJungleGatewayAdapter
- Gateway configuration
- Gateway health API
- First logical MCP endpoint registration path

## Acceptance criteria

- MCOS exposes one gateway MCP URL
- MCPJungle is supervised or mock-supervised
- Adapter can be replaced later
- No worker clone is exposed directly as public gateway server

## Validation

- `Targeted gateway adapter tests`
- `Static health endpoint check`
- `Build/tests where available`

