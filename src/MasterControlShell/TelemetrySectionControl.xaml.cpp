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

// v0.7.9: Tron-themed status colors used to paint the per-tile readiness
// dots that flank every Telemetry-tab gauge label. The four states map
// onto the snapshot value semantics, not raw thresholds, so the dot is
// always honest about what the operator should read into it:
//   good    = data is flowing and within healthy bounds
//   warn    = saturating or approaching limit / non-zero attention items
//   crit    = saturated / blocked / hard fault state
//   neutral = no data yet (admin API offline or counter idle)
constexpr uint8_t kStatusGoodA = 0xFF, kStatusGoodR = 0x1c, kStatusGoodG = 0xf2, kStatusGoodB = 0xc1;
constexpr uint8_t kStatusWarnA = 0xFF, kStatusWarnR = 0xff, kStatusWarnG = 0xc8, kStatusWarnB = 0x57;
constexpr uint8_t kStatusCritA = 0xFF, kStatusCritR = 0xff, kStatusCritG = 0x6a, kStatusCritB = 0x80;
constexpr uint8_t kStatusNeutA = 0xFF, kStatusNeutR = 0x8c, kStatusNeutG = 0xb7, kStatusNeutB = 0xc4;

enum class StatusTone { Good, Warn, Crit, Neutral };

void paintDot(winrt::Microsoft::UI::Xaml::Controls::Border const& dot, StatusTone tone) {
    using winrt::Windows::UI::Color;
    using winrt::Windows::UI::ColorHelper;
    using winrt::Microsoft::UI::Xaml::Media::SolidColorBrush;
    Color color;
    switch (tone) {
        case StatusTone::Good:    color = ColorHelper::FromArgb(kStatusGoodA, kStatusGoodR, kStatusGoodG, kStatusGoodB); break;
        case StatusTone::Warn:    color = ColorHelper::FromArgb(kStatusWarnA, kStatusWarnR, kStatusWarnG, kStatusWarnB); break;
        case StatusTone::Crit:    color = ColorHelper::FromArgb(kStatusCritA, kStatusCritR, kStatusCritG, kStatusCritB); break;
        case StatusTone::Neutral:
        default:                  color = ColorHelper::FromArgb(kStatusNeutA, kStatusNeutR, kStatusNeutG, kStatusNeutB); break;
    }
    dot.Background(SolidColorBrush(color));
}

// Pressure tiles (CPU / Memory / Disk): green <75%, amber 75-89%, red >=90%.
// When the admin API is offline the dot stays neutral because the underlying
// telemetry source is not producing data.
StatusTone pressureTone(double percent, bool apiHealthy) {
    if (!apiHealthy) return StatusTone::Neutral;
    if (percent < 0)  return StatusTone::Neutral;
    if (percent >= 90) return StatusTone::Crit;
    if (percent >= 75) return StatusTone::Warn;
    return StatusTone::Good;
}

// Throughput tiles (TX / RX / Live Traffic): green when bytes/sec > 0
// (link is moving data), neutral when idle. No upper threshold -- a busy
// link is not unhealthy.
StatusTone throughputTone(uint64_t bps, bool apiHealthy) {
    if (!apiHealthy) return StatusTone::Neutral;
    return bps > 0 ? StatusTone::Good : StatusTone::Neutral;
}

// Allocation budgets (CPU / RAM / Bandwidth / Storage): red at 0%
// (governed launches blocked), amber 1-29% (constrained), green >=30%.
StatusTone budgetTone(int percent, bool apiHealthy) {
    if (!apiHealthy) return StatusTone::Neutral;
    if (percent <= 0)  return StatusTone::Crit;
    if (percent < 30)  return StatusTone::Warn;
    return StatusTone::Good;
}

// Population counters (lanes / gateways / servers / Apple ops): green when
// the surface has produced at least one item, neutral when empty.
StatusTone populationTone(size_t count, bool apiHealthy) {
    if (!apiHealthy) return StatusTone::Neutral;
    return count > 0 ? StatusTone::Good : StatusTone::Neutral;
}

// Governance findings: zero findings is the healthy state; any finding is
// operator attention, not a hard fault.
StatusTone findingsTone(size_t count, bool apiHealthy) {
    if (!apiHealthy) return StatusTone::Neutral;
    return count == 0 ? StatusTone::Good : StatusTone::Warn;
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

    // v0.7.9: status dots flanking each gauge label. Each dot reflects the
    // honesty of the underlying value, not just whether the tile is
    // populated -- e.g. CPU dot is red at saturation, allocation dots are
    // red when the budget is zeroed (governed lanes blocked), traffic dots
    // are neutral when the link is idle. apiHealthy gates everything: if
    // the local admin API is unreachable we cannot tell what the underlying
    // values actually mean, so all dots fall back to neutral.
    paintDot(CpuStatusDot(),                pressureTone(snapshot.cpuPercent,    snapshot.apiHealthy));
    paintDot(MemoryStatusDot(),             pressureTone(snapshot.memoryPercent, snapshot.apiHealthy));
    paintDot(DiskStatusDot(),               pressureTone(snapshot.diskPercent,   snapshot.apiHealthy));
    paintDot(TxStatusDot(),                 throughputTone(snapshot.bytesSentPerSecond,     snapshot.apiHealthy));
    paintDot(RxStatusDot(),                 throughputTone(snapshot.bytesReceivedPerSecond, snapshot.apiHealthy));
    paintDot(TrafficStatusDot(),            throughputTone(snapshot.bytesSentPerSecond + snapshot.bytesReceivedPerSecond, snapshot.apiHealthy));
    paintDot(CpuAllocationStatusDot(),       budgetTone(snapshot.cpuAllocationPercent,       snapshot.apiHealthy));
    paintDot(MemoryAllocationStatusDot(),    budgetTone(snapshot.memoryAllocationPercent,    snapshot.apiHealthy));
    paintDot(BandwidthAllocationStatusDot(), budgetTone(snapshot.bandwidthAllocationPercent, snapshot.apiHealthy));
    paintDot(StorageAllocationStatusDot(),   budgetTone(snapshot.storageAllocationPercent,   snapshot.apiHealthy));
    paintDot(EndpointCountStatusDot(),       populationTone(snapshot.endpointCount,          snapshot.apiHealthy));
    paintDot(GovernanceFindingStatusDot(),   findingsTone(snapshot.governanceFindingCount,   snapshot.apiHealthy));
    paintDot(AppleOperationStatusDot(),      populationTone(snapshot.appleOperationCount,    snapshot.apiHealthy));
    paintDot(PlatformGatewayStatusDot(),     populationTone(snapshot.platformGatewayCount,   snapshot.apiHealthy));
    paintDot(GovernanceServerStatusDot(),    populationTone(snapshot.governanceServerCount,  snapshot.apiHealthy));
}

} // namespace winrt::MasterControlShell::implementation
