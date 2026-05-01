// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.
//
// PHASE-02 (ADR-002 §2): MCP Gateway adapters. The gateway abstraction
// (`IMcpGateway`) lives in `MasterControl/MasterControlContracts.h`. This
// header declares the production adapter and a test fake that implements
// the same interface without spawning a child process or making network
// calls.
//
// The `McpJungleGatewayAdapter` supervises an external MCPJungle binary
// when one is configured and reachable. When `enabled=false`, the binary
// path is empty, or the binary cannot be located, the adapter operates in
// "configured-but-not-running" mode: state transitions still happen,
// register/deregister calls still succeed against an in-memory registry,
// but no child process is spawned and Probe() reports
// `GatewayHealthStatus::Unknown`. This honors `.claude/rules/00-mcos-realignment.md`'s
// "no live-looking seeded infrastructure" rule — the dashboard sees an
// honest "configured, not running" state rather than a fake "healthy".

#pragma once

#include "MasterControl/MasterControlContracts.h"
#include "MasterControl/MasterControlModels.h"

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace MasterControl {

// Production adapter. Supervises MCPJungle when a binary path is provided.
// Health probe uses WinHTTP against `<listenHost>:<listenPort><healthPath>`.
class McpJungleGatewayAdapter final : public IMcpGateway {
public:
    explicit McpJungleGatewayAdapter(McpGatewayConfiguration configuration);
    ~McpJungleGatewayAdapter() override;

    McpJungleGatewayAdapter(const McpJungleGatewayAdapter&) = delete;
    McpJungleGatewayAdapter& operator=(const McpJungleGatewayAdapter&) = delete;

    GatewayStatus Start() override;
    GatewayStatus Stop() override;
    GatewayStatus CurrentStatus() const override;
    GatewayHealth Probe() override;
    RegistrationResult RegisterHttpServer(const McpServerRegistration& server) override;
    RegistrationResult RegisterStdioServer(const McpServerRegistration& server) override;
    DeregistrationResult DeregisterServer(const std::string& serverName) override;
    std::vector<McpToolDescriptor> ListTools() const override;
    std::string GatewayMcpUrl() const override;
    std::string AdapterType() const override;

    // Test/observability helpers — not part of IMcpGateway.
    McpGatewayConfiguration configuration() const;
    bool isSupervisingChildProcess() const;

private:
    RegistrationResult registerInternal(McpServerRegistration server);
    GatewayHealth probeOverHttp() const;
    void terminateChildProcessTreeIfRunning();

    mutable std::mutex mutex_;
    McpGatewayConfiguration configuration_;
    GatewayStatus status_;
    std::map<std::string, McpServerRegistration> registry_;

#if defined(_WIN32)
    HANDLE jobObject_ = nullptr;
    PROCESS_INFORMATION processInfo_{};
#endif
    std::atomic<bool> childProcessActive_{ false };
};

// In-process fake. State transitions and registry behavior match
// `McpJungleGatewayAdapter`'s contract, but Probe() returns whatever the
// test scripts via `setNextProbe(...)`. Used by
// `MasterControlOrchestrationServerTests` and any future adapter consumer
// that needs to exercise IMcpGateway without a real binary.
class FakeMcpGatewayAdapter final : public IMcpGateway {
public:
    explicit FakeMcpGatewayAdapter(McpGatewayConfiguration configuration);
    ~FakeMcpGatewayAdapter() override = default;

    GatewayStatus Start() override;
    GatewayStatus Stop() override;
    GatewayStatus CurrentStatus() const override;
    GatewayHealth Probe() override;
    RegistrationResult RegisterHttpServer(const McpServerRegistration& server) override;
    RegistrationResult RegisterStdioServer(const McpServerRegistration& server) override;
    DeregistrationResult DeregisterServer(const std::string& serverName) override;
    std::vector<McpToolDescriptor> ListTools() const override;
    std::string GatewayMcpUrl() const override;
    std::string AdapterType() const override;

    // Test scripting hooks.
    void setNextProbe(GatewayHealth probe);
    void setStartShouldFail(bool shouldFail, const std::string& message = "");
    std::size_t startCallCount() const;
    std::size_t stopCallCount() const;
    std::size_t probeCallCount() const;
    std::vector<std::string> registeredServerNames() const;

private:
    mutable std::mutex mutex_;
    McpGatewayConfiguration configuration_;
    GatewayStatus status_;
    std::map<std::string, McpServerRegistration> registry_;
    GatewayHealth nextProbe_;
    bool startShouldFail_ = false;
    std::string startFailureMessage_;
    std::size_t startCalls_ = 0;
    std::size_t stopCalls_ = 0;
    mutable std::size_t probeCalls_ = 0;
};

} // namespace MasterControl
