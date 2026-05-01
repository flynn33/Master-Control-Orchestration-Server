// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#include "pch.h"

#include "TelemetrySectionControl.xaml.h"

#if __has_include("TelemetrySectionControl.g.cpp")
#include "TelemetrySectionControl.g.cpp"
#endif

#include "ShellFormatting.h"

namespace winrt::MasterControlShell::implementation {

using namespace ::MasterControlShell::Presentation;

namespace {

std::wstring serviceStateLabel(const ::MasterControlShell::ServiceState state) {
    switch (state) {
        case ::MasterControlShell::ServiceState::Missing: return L"Missing";
        case ::MasterControlShell::ServiceState::Stopped: return L"Stopped";
        case ::MasterControlShell::ServiceState::StartPending: return L"Starting";
        case ::MasterControlShell::ServiceState::StopPending: return L"Stopping";
        case ::MasterControlShell::ServiceState::Running: return L"Running";
        case ::MasterControlShell::ServiceState::Paused: return L"Paused";
        default: return L"Unknown";
    }
}

std::wstring formatRate(const uint64_t bytesPerSecond) {
    std::wostringstream stream;
    stream.setf(std::ios::fixed);
    stream.precision(1);

    if (bytesPerSecond >= 1024ULL * 1024ULL * 1024ULL) {
        stream << (static_cast<double>(bytesPerSecond) / (1024.0 * 1024.0 * 1024.0)) << L" GB/s";
    } else if (bytesPerSecond >= 1024ULL * 1024ULL) {
        stream << (static_cast<double>(bytesPerSecond) / (1024.0 * 1024.0)) << L" MB/s";
    } else if (bytesPerSecond >= 1024ULL) {
        stream << (static_cast<double>(bytesPerSecond) / 1024.0) << L" KB/s";
    } else {
        stream.precision(0);
        stream << bytesPerSecond << L" B/s";
    }

    return stream.str();
}

std::wstring formatCountValue(const size_t value) {
    std::wostringstream stream;
    stream << value;
    return stream.str();
}

} // namespace

TelemetrySectionControl::TelemetrySectionControl() {
    InitializeComponent();
}

void TelemetrySectionControl::ApplySnapshot(const ::MasterControlShell::ShellSnapshot& snapshot) {
    CpuProgressBar().Value(snapshot.cpuPercent);
    MemoryProgressBar().Value(snapshot.memoryPercent);
    DiskProgressBar().Value(snapshot.diskPercent);
    CpuValueText().Text(winrt::hstring(formatPercent(snapshot.cpuPercent)));
    MemoryValueText().Text(winrt::hstring(formatPercent(snapshot.memoryPercent)));
    DiskValueText().Text(winrt::hstring(formatPercent(snapshot.diskPercent)));
    TxValueText().Text(winrt::hstring(formatRate(snapshot.bytesSentPerSecond)));
    RxValueText().Text(winrt::hstring(formatRate(snapshot.bytesReceivedPerSecond)));
    TrafficValueText().Text(winrt::hstring(formatTraffic(snapshot.bytesSentPerSecond, snapshot.bytesReceivedPerSecond)));

    CpuDetailText().Text(winrt::hstring(snapshot.hostName.empty() ? L"Live host utilization." : L"Host " + snapshot.hostName + L" live utilization."));
    MemoryDetailText().Text(winrt::hstring(snapshot.operatingSystem.empty() ? L"Resident memory pressure." : snapshot.operatingSystem + L" resident memory pressure."));
    DiskDetailText().Text(winrt::hstring(snapshot.dataDirectory.empty() ? L"Payload and runtime storage pressure." : L"Data root: " + snapshot.dataDirectory));
    TxDetailText().Text(winrt::hstring(snapshot.primaryIpAddress.empty() ? L"Outbound network traffic." : L"Primary route: " + snapshot.primaryIpAddress));
    RxDetailText().Text(winrt::hstring(snapshot.primaryMacAddress.empty() ? L"Inbound network traffic." : L"MAC: " + snapshot.primaryMacAddress));
    TrafficDetailText().Text(winrt::hstring(snapshot.telemetryCapturedAtUtc.empty() ? L"Live traffic summary." : L"Captured: " + snapshot.telemetryCapturedAtUtc));

    CpuAllocationProgressBar().Value(snapshot.cpuAllocationPercent);
    MemoryAllocationProgressBar().Value(snapshot.memoryAllocationPercent);
    BandwidthAllocationProgressBar().Value(snapshot.bandwidthAllocationPercent);
    StorageAllocationProgressBar().Value(snapshot.storageAllocationPercent);
    CpuAllocationValueText().Text(winrt::hstring(std::to_wstring(snapshot.cpuAllocationPercent) + L"%"));
    MemoryAllocationValueText().Text(winrt::hstring(std::to_wstring(snapshot.memoryAllocationPercent) + L"%"));
    BandwidthAllocationValueText().Text(winrt::hstring(std::to_wstring(snapshot.bandwidthAllocationPercent) + L"%"));
    StorageAllocationValueText().Text(winrt::hstring(std::to_wstring(snapshot.storageAllocationPercent) + L"%"));
    CpuAllocationDetailText().Text(winrt::hstring(snapshot.cpuAllocationPercent <= 0 ? L"Governed launches blocked." : L"Planner and worker CPU budget."));
    MemoryAllocationDetailText().Text(winrt::hstring(snapshot.memoryAllocationPercent <= 0 ? L"Governed launches blocked." : L"Managed memory ceiling."));
    BandwidthAllocationDetailText().Text(winrt::hstring(snapshot.bandwidthAllocationPercent <= 0 ? L"Network lanes blocked." : L"Network lane traffic budget."));
    StorageAllocationDetailText().Text(winrt::hstring(snapshot.storageAllocationPercent <= 0 ? L"Artifact lanes blocked." : L"Exports and staging budget."));

    EndpointCountText().Text(winrt::hstring(formatCountValue(snapshot.endpointCount)));
    GovernanceFindingCountText().Text(winrt::hstring(formatCountValue(snapshot.governanceFindingCount)));
    AppleOperationCountText().Text(winrt::hstring(formatCountValue(snapshot.appleOperationCount)));
    PlatformGatewayCountText().Text(winrt::hstring(formatCountValue(snapshot.platformGatewayCount)));
    GovernanceServerCountText().Text(winrt::hstring(formatCountValue(snapshot.governanceServerCount)));
    EndpointCountDetailText().Text(winrt::hstring(pluralize(snapshot.endpointCount, L"published lane", L"published lanes")));
    GovernanceFindingDetailText().Text(winrt::hstring(pluralize(snapshot.governanceFindingCount, L"live finding", L"live findings")));
    AppleOperationDetailText().Text(winrt::hstring(pluralize(snapshot.appleOperationCount, L"tracked Apple job", L"tracked Apple jobs")));
    PlatformGatewayDetailText().Text(winrt::hstring(pluralize(snapshot.platformGatewayCount, L"gateway lane", L"gateway lanes")));
    GovernanceServerDetailText().Text(winrt::hstring(pluralize(snapshot.governanceServerCount, L"governance server", L"governance servers")));

    const std::wstring telemetryHeadline = snapshot.hostName.empty()
        ? L"Telemetry Grid"
        : L"Telemetry Grid - " + snapshot.hostName;
    TelemetryHeadlineText().Text(winrt::hstring(telemetryHeadline));
    TelemetrySummaryText().Text(winrt::hstring(snapshot.telemetryText.empty() ? L"Live telemetry is waiting for the local orchestration service." : snapshot.telemetryText));
    TelemetryCaptureText().Text(winrt::hstring(snapshot.telemetryCapturedAtUtc.empty()
        ? L"Capture timestamp pending."
        : L"Last capture UTC: " + snapshot.telemetryCapturedAtUtc));

    std::wostringstream hostIdentity;
    hostIdentity << L"Host: " << (snapshot.hostName.empty() ? L"pending" : snapshot.hostName) << L'\n'
                 << L"Operating system: " << (snapshot.operatingSystem.empty() ? L"pending" : snapshot.operatingSystem) << L'\n'
                 << L"Primary IP: " << (snapshot.primaryIpAddress.empty() ? L"pending" : snapshot.primaryIpAddress) << L'\n'
                 << L"Primary MAC: " << (snapshot.primaryMacAddress.empty() ? L"pending" : snapshot.primaryMacAddress);
    HostIdentityText().Text(winrt::hstring(hostIdentity.str()));

    std::wostringstream environmentProfile;
    environmentProfile << L"Environment: " << (snapshot.environmentName.empty() ? L"pending" : snapshot.environmentName) << L'\n'
                       << L"Dashboard URL: " << (snapshot.dashboardUrl.empty() ? L"pending" : snapshot.dashboardUrl) << L'\n'
                       << L"Bind address: " << (snapshot.bindAddress.empty() ? L"pending" : snapshot.bindAddress) << L':'
                       << snapshot.browserPort;
    EnvironmentProfileText().Text(winrt::hstring(environmentProfile.str()));

    std::wostringstream serviceSummary;
    serviceSummary << L"Service: " << serviceStateLabel(snapshot.serviceState) << L'\n'
                   << L"Admin API: " << (snapshot.apiHealthy ? L"reachable" : L"offline") << L'\n'
                   << L"Beacon: " << boolLabel(snapshot.beaconEnabled) << L'\n'
                   << L"AI autonomy: " << boolLabel(snapshot.aiAutonomyEnabled) << L'\n'
                   << L"Security protocols: " << boolLabel(snapshot.securityProtocolsEnabled) << L'\n'
                   << L"Open LAN access: " << boolLabel(snapshot.openLanAccess);
    TelemetryServiceSummaryText().Text(winrt::hstring(serviceSummary.str()));

    std::wostringstream pathsSummary;
    pathsSummary << L"Config: " << (snapshot.configPath.empty() ? L"pending" : snapshot.configPath) << L'\n'
                 << L"Data root: " << (snapshot.dataDirectory.empty() ? L"pending" : snapshot.dataDirectory) << L'\n'
                 << L"Browser port: " << snapshot.browserPort << L'\n'
                 << L"Envelope: " << formatResourceEnvelope(snapshot);
    TelemetryPathsText().Text(winrt::hstring(pathsSummary.str()));

    std::wostringstream controlPlanePosture;
    controlPlanePosture << L"Service state: " << serviceStateLabel(snapshot.serviceState) << L'\n'
                        << L"Admin API healthy: " << (snapshot.apiHealthy ? L"Yes" : L"No") << L'\n'
                        << L"Governance posture: " << (snapshot.governancePosture.empty() ? L"pending" : snapshot.governancePosture) << L'\n'
                        << L"Governance findings: " << snapshot.governanceFindingCount << L'\n'
                        << L"Apple hosts / ops: " << snapshot.appleRemoteHostCount << L" / " << snapshot.appleOperationCount;
    ControlPlanePostureText().Text(winrt::hstring(controlPlanePosture.str()));

    std::wostringstream telemetryRouting;
    telemetryRouting << L"Runtime lanes: " << snapshot.endpointCount << L'\n'
                     << L"Governance executions: " << snapshot.governanceExecutionCount << L'\n'
                     << L"Gateway lanes: " << snapshot.platformGatewayCount << L'\n'
                     << L"Governance servers: " << snapshot.governanceServerCount << L'\n'
                     << L"Dashboard route: " << (snapshot.dashboardUrl.empty() ? L"pending" : snapshot.dashboardUrl);
    TelemetryRoutingText().Text(winrt::hstring(telemetryRouting.str()));

    TelemetryNarrativeText().Text(winrt::hstring(snapshot.telemetryText.empty() ? L"Telemetry narrative will populate here." : snapshot.telemetryText));
    EnvironmentNarrativeText().Text(winrt::hstring(snapshot.environmentText));
}

} // namespace winrt::MasterControlShell::implementation
