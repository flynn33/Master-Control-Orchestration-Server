// Master Control Program
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#pragma once

#include "pch.h"

namespace MasterControlShell {

enum class ServiceState {
    Missing,
    Stopped,
    StartPending,
    StopPending,
    Running,
    Paused,
    Unknown
};

struct ShellSnapshot final {
    ServiceState serviceState = ServiceState::Missing;
    uint32_t serviceProcessId = 0;
    bool apiHealthy = false;
    bool canStartService = true;
    bool canStopService = false;
    double cpuPercent = 0.0;
    double memoryPercent = 0.0;
    double diskPercent = 0.0;
    uint64_t bytesSentPerSecond = 0;
    uint64_t bytesReceivedPerSecond = 0;
    uint16_t browserPort = 7300;
    bool beaconEnabled = true;
    bool aiAutonomyEnabled = false;
    bool securityProtocolsEnabled = true;
    bool openLanAccess = true;
    int cpuAllocationPercent = 50;
    int memoryAllocationPercent = 50;
    int bandwidthAllocationPercent = 50;
    int storageAllocationPercent = 50;
    std::wstring dashboardUrl;
    std::wstring configPath;
    std::wstring dataDirectory;
    std::wstring environmentName;
    std::wstring hostName;
    std::wstring operatingSystem;
    std::wstring primaryIpAddress;
    std::wstring primaryMacAddress;
    std::wstring bindAddress;
    std::wstring overviewText;
    std::wstring telemetryText;
    std::wstring environmentText;
    std::wstring configurationText;
    std::vector<std::wstring> endpointRows;
    std::vector<std::wstring> providerRows;
    std::vector<std::wstring> installRows;
    std::vector<std::wstring> exportRows;
    std::wstring statusMessage;
};

class ShellRuntime final {
public:
    ShellRuntime() = default;

    [[nodiscard]] ShellSnapshot CaptureSnapshot() const;
    [[nodiscard]] bool StartService(std::wstring& message) const;
    [[nodiscard]] bool StopService(std::wstring& message) const;
    void OpenDashboard(const ShellSnapshot& snapshot) const;
    void OpenConfig() const;
    void OpenDataDirectory() const;

private:
    [[nodiscard]] std::filesystem::path ResolveDataDirectory() const;
    [[nodiscard]] std::filesystem::path ResolveConfigurationFile() const;
};

} // namespace MasterControlShell
