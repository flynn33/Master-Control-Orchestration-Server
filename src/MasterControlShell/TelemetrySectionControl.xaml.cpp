// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#include "pch.h"

#include "TelemetrySectionControl.xaml.h"

#if __has_include("TelemetrySectionControl.g.cpp")
#include "TelemetrySectionControl.g.cpp"
#endif

#include "ShellFormatting.h"
// v0.9.76: shared imperative card-grid renderer used by RuntimeSection,
// OverviewSection, and (now) TelemetrySection. WinUI is the priority
// surface; mirroring the browser's v0.9.70 Telemetry deck means the
// remote-desktop operator never has to bounce to Runtime to see live
// per-endpoint state.
#include "EndpointStatCardGrid.h"

// v0.8.0: layout persistence + detach windows
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <winrt/Windows.Storage.Streams.h>
#include <KnownFolders.h>
#include <ShlObj.h>
#include <fstream>
#include <sstream>

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
    // v0.8.0: deferred layout load -- the XAML element accessors aren't
    // safe to dereference until the visual tree is fully realized. Hook
    // Loaded once.
    this->Loaded([weak = get_weak()](auto&&, auto&&) {
        if (auto self = weak.get()) {
            self->LoadAndApplyLayout();
            self->RebuildCustomizeFlyoutCheckboxes();
        }
    });
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

    // v0.9.76: route bind-address render through resolveDisplayBindAddress
    // so wildcard binds surface the LAN-routable primary IP. Raw configured
    // value is shown on the trailing line so the operator sees both the
    // listen value and the resolved client-facing value at once.
    const auto resolvedTelemetryBind = ::MasterControlShell::Presentation::resolveDisplayBindAddress(
        snapshot.bindAddress, snapshot.primaryIpAddress);
    std::wostringstream environmentProfile;
    environmentProfile << L"Environment: " << (snapshot.environmentName.empty() ? L"pending" : snapshot.environmentName) << L'\n'
                       << L"Dashboard URL: " << (snapshot.dashboardUrl.empty() ? L"pending" : snapshot.dashboardUrl) << L'\n'
                       << L"Bind address: " << (resolvedTelemetryBind.empty() ? std::wstring(L"pending") : resolvedTelemetryBind) << L':'
                       << snapshot.browserPort;
    if (::MasterControlShell::Presentation::isWildcardBindAddress(snapshot.bindAddress)) {
        environmentProfile << L"\nListening on: "
                           << (snapshot.bindAddress.empty() ? std::wstring(L"0.0.0.0") : snapshot.bindAddress);
    }
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

    // v0.9.76: rebuild the MCP server + Sub-agent card grids on every
    // snapshot tick. Visual shape exactly matches RuntimeSection /
    // Overview surface cards via the shared template.
    PopulateMcpServerCards(snapshot);
    PopulateSubAgentCards(snapshot);

    // v0.9.79: compact Supervisor status card in the Telemetry sidebar.
    // Mirrors the Overview surface card without the toggle group; the
    // Telemetry deck is monitoring-only, so the operator pivots back
    // to Overview when they want to change the supervisor selection.
    {
        using winrt::Microsoft::UI::Xaml::Media::SolidColorBrush;
        using winrt::Windows::UI::ColorHelper;
        const auto& sup = snapshot.supervisorStatus;
        const std::wstring state = sup.state.empty() ? std::wstring(L"off") : sup.state;
        const std::wstring provider = sup.activeProviderId;
        auto fromHex = [](uint8_t r, uint8_t g, uint8_t b) {
            return ColorHelper::FromArgb(0xFF, r, g, b);
        };
        winrt::Windows::UI::Color tone = fromHex(0x8c, 0xb7, 0xc4); // neutral
        std::wstring headline = L"No supervisor assigned.";
        std::wstring detail = L"Pick a supervisor model on the Overview deck.";
        if (state == L"connected") {
            tone = fromHex(0x1c, 0xf2, 0xc1);
            headline = L"Active: " + (sup.providerDisplayName.empty() ? provider : sup.providerDisplayName);
            // v0.9.90: relative-time formatting on the heartbeat (see
            // OverviewSectionControl::ApplySupervisorCard).
            std::wstring heartbeatPhrase;
            if (sup.lastHeartbeatUtc.empty()) {
                heartbeatPhrase = std::wstring(L".");
            } else {
                const auto relative = ::MasterControlShell::Presentation::formatRelativeUtcTime(sup.lastHeartbeatUtc);
                heartbeatPhrase = relative.empty()
                    ? (L" | Last heartbeat (UTC): " + sup.lastHeartbeatUtc + L".")
                    : (L" | Last heartbeat: " + relative + L".");
            }
            detail = L"Status: connected" + heartbeatPhrase;
        } else if (state == L"pending_connection" || state == L"config_generated") {
            tone = fromHex(0xff, 0xaf, 0x3a);
            headline = (sup.providerDisplayName.empty() ? provider : sup.providerDisplayName)
                + L" pending connection";
            detail = sup.expiresAtUtc.empty()
                ? std::wstring(L"Config issued; awaiting client connect.")
                : (L"Config expires (UTC): " + sup.expiresAtUtc);
        } else if (state == L"disconnected") {
            tone = fromHex(0xff, 0xaf, 0x3a);
            headline = (sup.providerDisplayName.empty() ? provider : sup.providerDisplayName)
                + L" disconnected";
            detail = L"Heartbeat timeout. Remote client may have stopped.";
        } else if (state == L"revoked") {
            tone = fromHex(0xff, 0xaf, 0x3a);
            headline = (sup.providerDisplayName.empty() ? provider : sup.providerDisplayName)
                + L" revoked";
            detail = L"Re-select a provider on the Overview deck to reassign.";
        } else if (state == L"error") {
            tone = fromHex(0xff, 0x3a, 0x5a);
            headline = (sup.providerDisplayName.empty() ? provider : sup.providerDisplayName)
                + L" error";
            detail = sup.lastErrorMessage.empty()
                ? std::wstring(L"See /api/supervisor/status for detail.")
                : sup.lastErrorMessage;
        }
        TelemetrySupervisorStatusDot().Background(SolidColorBrush(tone));
        TelemetrySupervisorHeadline().Text(winrt::hstring(headline));
        TelemetrySupervisorDetail().Text(winrt::hstring(detail));
    }
}

// v0.9.76: thin adapters that route the per-kind XAML element names +
// headlines through the shared renderer in EndpointStatCardGrid.h. The
// Telemetry surface uses compact density (compact=true) so MCP-server +
// sub-agent decks render at matching size and stack closer together than
// the wider Runtime / Overview cards. Operator directive: "MCP Servers
// on the telemetry page must have smaller cards. They must be close in
// size to the current Sub-Agent Cards." Compact mode shrinks padding,
// header/util fonts, progress-bar height, and hides the empty-state
// "No active clients" line so the two surfaces are visually identical.
void TelemetrySectionControl::PopulateMcpServerCards(const ::MasterControlShell::ShellSnapshot& snapshot) {
    renderEndpointStatCardGrid(
        TelemetryMcpServersCardStack(),
        TelemetryMcpServersHeadlineText(),
        snapshot.mcpServerRuntimeStats,
        L"MCP server",
        L"No MCP servers registered yet. Use POST /api/runtime/mcp-servers to publish one.",
        L"No managed pool. POST /api/pools with matching id to enable autoscale.",
        true);
}

void TelemetrySectionControl::PopulateSubAgentCards(const ::MasterControlShell::ShellSnapshot& snapshot) {
    renderEndpointStatCardGrid(
        TelemetrySubAgentsCardStack(),
        TelemetrySubAgentsHeadlineText(),
        snapshot.subAgentRuntimeStats,
        L"sub-agent",
        L"No sub-agents registered yet. Use the New Sub-Agent action in Runtime to publish one.",
        L"No managed pool. POST /api/pools with matching id to enable autoscale.",
        true);
}

// =====================================================================
// v0.8.0: Telemetry layout customization (select / drag-reorder / detach)
// =====================================================================

namespace {

// Stable list of every tile name and its default home section + grid
// position. Single source of truth for "where does this tile live by
// default" -- consulted by BuildDefaultLayout, ResolveSectionGridForTile,
// and the Customize flyout label.
struct TileMetadata {
    const wchar_t* name;       // x:Name on the tile Border
    const wchar_t* displayName;// label shown in the Customize flyout
    const wchar_t* sectionId;  // "host-pressure" | "resource-allocation" | "operational-activity"
    int defaultRow;
    int defaultColumn;
};

const std::vector<TileMetadata>& tileCatalog() {
    static const std::vector<TileMetadata> catalog = {
        // Live Host Pressure (3 columns x 2 rows)
        { L"CpuTile",                L"CPU Load",            L"host-pressure", 0, 0 },
        { L"MemoryTile",             L"Memory Pressure",     L"host-pressure", 0, 1 },
        { L"DiskTile",               L"Disk Occupancy",      L"host-pressure", 0, 2 },
        { L"TxTile",                 L"TX Throughput",       L"host-pressure", 1, 0 },
        { L"RxTile",                 L"RX Throughput",       L"host-pressure", 1, 1 },
        { L"TrafficTile",            L"Live Traffic",        L"host-pressure", 1, 2 },
        // Resource Allocation (2 columns x 2 rows)
        { L"CpuBudgetTile",          L"CPU Budget",          L"resource-allocation", 0, 0 },
        { L"RamBudgetTile",          L"RAM Budget",          L"resource-allocation", 0, 1 },
        { L"BandwidthBudgetTile",    L"Bandwidth Budget",    L"resource-allocation", 1, 0 },
        { L"StorageBudgetTile",      L"Storage Budget",      L"resource-allocation", 1, 1 },
        // Operational Activity (3 columns x 2 rows; 5 tiles, 1 cell empty by default)
        { L"RuntimeLanesTile",       L"Runtime Lanes",       L"operational-activity", 0, 0 },
        { L"GovernanceFindingsTile", L"Governance Findings", L"operational-activity", 0, 1 },
        { L"AppleOperationsTile",    L"Apple Operations",    L"operational-activity", 0, 2 },
        { L"PlatformGatewaysTile",   L"Platform Gateways",   L"operational-activity", 1, 0 },
        { L"GovernanceServersTile",  L"Governance Servers",  L"operational-activity", 1, 1 },
    };
    return catalog;
}

const TileMetadata* findTileMetadata(const std::wstring& tileName) {
    for (const auto& meta : tileCatalog()) {
        if (tileName == meta.name) return &meta;
    }
    return nullptr;
}

// Tag-to-string helper. WinUI 3 Tag is IInspectable; the XAML carries
// string Tag values for our tile/button identification.
std::wstring tagAsString(winrt::Windows::Foundation::IInspectable const& tag) {
    if (!tag) return L"";
    if (auto box = tag.try_as<winrt::Windows::Foundation::IPropertyValue>()) {
        if (box.Type() == winrt::Windows::Foundation::PropertyType::String) {
            return std::wstring(box.GetString().c_str());
        }
    }
    return L"";
}

// Locate %ProgramData%\MasterControlOrchestrationServer\config\.
std::wstring programDataConfigDir() {
    PWSTR pPath = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramData, 0, nullptr, &pPath))) {
        std::wstring root(pPath);
        CoTaskMemFree(pPath);
        std::wstring dir = root + L"\\MasterControlOrchestrationServer\\config";
        CreateDirectoryW(dir.c_str(), nullptr);
        return dir;
    }
    return L"";
}

// JSON serialization is intentionally minimal -- we control both the
// reader and the writer, so we don't need a full parser. Layout is small
// (15 tile entries) so even O(n^2) scans are fine.

std::wstring escapeJsonString(const std::wstring& raw) {
    std::wstring out;
    out.reserve(raw.size() + 4);
    for (wchar_t c : raw) {
        switch (c) {
            case L'\\': out += L"\\\\"; break;
            case L'"':  out += L"\\\""; break;
            case L'\n': out += L"\\n"; break;
            case L'\r': out += L"\\r"; break;
            case L'\t': out += L"\\t"; break;
            default:    out += c; break;
        }
    }
    return out;
}

// Wide-to-UTF8 for fstream output.
std::string wideToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int needed = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string out(needed, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), out.data(), needed, nullptr, nullptr);
    return out;
}

std::wstring utf8ToWide(const std::string& u) {
    if (u.empty()) return {};
    int needed = MultiByteToWideChar(CP_UTF8, 0, u.c_str(), (int)u.size(), nullptr, 0);
    std::wstring out(needed, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, u.c_str(), (int)u.size(), out.data(), needed);
    return out;
}

// Tiny single-purpose JSON parser for the layout document. Takes the
// utf-8 file text, returns a map keyed by tile name. Tolerates missing
// fields by falling back to defaults.
std::map<std::wstring, TelemetryTileLayoutState> parseLayoutJson(const std::string& utf8Text) {
    std::map<std::wstring, TelemetryTileLayoutState> result;
    if (utf8Text.empty()) return result;

    auto find = [&](size_t from, const std::string& needle) -> size_t {
        return utf8Text.find(needle, from);
    };
    auto extractString = [&](size_t openQuote) -> std::pair<std::string, size_t> {
        std::string s;
        size_t i = openQuote + 1;
        while (i < utf8Text.size()) {
            char c = utf8Text[i];
            if (c == '\\' && i + 1 < utf8Text.size()) {
                char n = utf8Text[i + 1];
                if (n == 'n') s += '\n';
                else if (n == 'r') s += '\r';
                else if (n == 't') s += '\t';
                else s += n;
                i += 2;
            } else if (c == '"') {
                return {s, i};
            } else {
                s += c;
                ++i;
            }
        }
        return {s, i};
    };
    auto extractInt = [&](size_t startPos, int fallback) -> std::pair<int, size_t> {
        size_t i = startPos;
        while (i < utf8Text.size() && (utf8Text[i] == ' ' || utf8Text[i] == ':')) ++i;
        bool neg = false;
        if (i < utf8Text.size() && utf8Text[i] == '-') { neg = true; ++i; }
        int v = 0;
        bool any = false;
        while (i < utf8Text.size() && utf8Text[i] >= '0' && utf8Text[i] <= '9') {
            v = v * 10 + (utf8Text[i] - '0');
            ++i;
            any = true;
        }
        return {any ? (neg ? -v : v) : fallback, i};
    };
    auto extractBool = [&](size_t startPos, bool fallback) -> std::pair<bool, size_t> {
        size_t i = startPos;
        while (i < utf8Text.size() && (utf8Text[i] == ' ' || utf8Text[i] == ':')) ++i;
        if (i + 4 <= utf8Text.size() && utf8Text.substr(i, 4) == "true") return {true, i + 4};
        if (i + 5 <= utf8Text.size() && utf8Text.substr(i, 5) == "false") return {false, i + 5};
        return {fallback, i};
    };

    // Find each "TileName": { ... } object inside the top-level "tiles": { ... }.
    size_t tilesPos = find(0, "\"tiles\"");
    if (tilesPos == std::string::npos) return result;
    size_t cursor = utf8Text.find('{', tilesPos);
    if (cursor == std::string::npos) return result;
    ++cursor;
    int depth = 1;
    while (cursor < utf8Text.size() && depth > 0) {
        // Skip whitespace + commas.
        while (cursor < utf8Text.size() &&
               (utf8Text[cursor] == ' ' || utf8Text[cursor] == ',' ||
                utf8Text[cursor] == '\n' || utf8Text[cursor] == '\r' || utf8Text[cursor] == '\t')) {
            ++cursor;
        }
        if (cursor >= utf8Text.size()) break;
        if (utf8Text[cursor] == '}') { --depth; ++cursor; break; }
        if (utf8Text[cursor] != '"') { ++cursor; continue; }
        auto [tileNameUtf8, afterName] = extractString(cursor);
        cursor = afterName + 1;
        // Skip ':' and any whitespace.
        while (cursor < utf8Text.size() && (utf8Text[cursor] == ':' || utf8Text[cursor] == ' ')) ++cursor;
        if (cursor >= utf8Text.size() || utf8Text[cursor] != '{') continue;
        ++cursor;
        TelemetryTileLayoutState state{};
        // Walk fields until matching close brace.
        int innerDepth = 1;
        while (cursor < utf8Text.size() && innerDepth > 0) {
            while (cursor < utf8Text.size() &&
                   (utf8Text[cursor] == ' ' || utf8Text[cursor] == ',' ||
                    utf8Text[cursor] == '\n' || utf8Text[cursor] == '\r' || utf8Text[cursor] == '\t')) {
                ++cursor;
            }
            if (cursor >= utf8Text.size()) break;
            if (utf8Text[cursor] == '}') { --innerDepth; ++cursor; break; }
            if (utf8Text[cursor] != '"') { ++cursor; continue; }
            auto [fieldUtf8, afterField] = extractString(cursor);
            cursor = afterField + 1;
            while (cursor < utf8Text.size() && (utf8Text[cursor] == ':' || utf8Text[cursor] == ' ')) ++cursor;
            if (cursor >= utf8Text.size()) break;
            if (fieldUtf8 == "visible") {
                auto [b, after] = extractBool(cursor, true);
                state.visible = b;
                cursor = after;
            } else if (fieldUtf8 == "detached") {
                auto [b, after] = extractBool(cursor, false);
                state.detached = b;
                cursor = after;
            } else if (fieldUtf8 == "row") {
                auto [v, after] = extractInt(cursor, 0);
                state.row = v;
                cursor = after;
            } else if (fieldUtf8 == "column") {
                auto [v, after] = extractInt(cursor, 0);
                state.column = v;
                cursor = after;
            } else if (fieldUtf8 == "detachedX") {
                auto [v, after] = extractInt(cursor, 80);
                state.detachedX = v;
                cursor = after;
            } else if (fieldUtf8 == "detachedY") {
                auto [v, after] = extractInt(cursor, 80);
                state.detachedY = v;
                cursor = after;
            } else if (fieldUtf8 == "detachedWidth") {
                auto [v, after] = extractInt(cursor, 360);
                state.detachedWidth = v;
                cursor = after;
            } else if (fieldUtf8 == "detachedHeight") {
                auto [v, after] = extractInt(cursor, 320);
                state.detachedHeight = v;
                cursor = after;
            } else if (fieldUtf8 == "sectionId") {
                if (cursor < utf8Text.size() && utf8Text[cursor] == '"') {
                    auto [s, after] = extractString(cursor);
                    state.sectionId = utf8ToWide(s);
                    cursor = after + 1;
                }
            } else {
                // Unknown field -- skip until next comma or '}'.
                while (cursor < utf8Text.size() && utf8Text[cursor] != ',' && utf8Text[cursor] != '}') ++cursor;
            }
        }
        result[utf8ToWide(tileNameUtf8)] = state;
    }
    return result;
}

std::string serializeLayoutJson(const std::map<std::wstring, TelemetryTileLayoutState>& layout) {
    std::wostringstream out;
    out << L"{\n  \"tiles\": {\n";
    bool first = true;
    for (const auto& [name, state] : layout) {
        if (!first) out << L",\n";
        first = false;
        out << L"    \"" << escapeJsonString(name) << L"\": {"
            << L"\"visible\":" << (state.visible ? L"true" : L"false")
            << L",\"detached\":" << (state.detached ? L"true" : L"false")
            << L",\"row\":" << state.row
            << L",\"column\":" << state.column
            << L",\"sectionId\":\"" << escapeJsonString(state.sectionId) << L"\""
            << L",\"detachedX\":" << state.detachedX
            << L",\"detachedY\":" << state.detachedY
            << L",\"detachedWidth\":" << state.detachedWidth
            << L",\"detachedHeight\":" << state.detachedHeight
            << L"}";
    }
    out << L"\n  }\n}\n";
    return wideToUtf8(out.str());
}

} // namespace

std::map<std::wstring, TelemetryTileLayoutState> TelemetrySectionControl::BuildDefaultLayout() {
    std::map<std::wstring, TelemetryTileLayoutState> result;
    for (const auto& meta : tileCatalog()) {
        TelemetryTileLayoutState state{};
        state.visible = true;
        state.detached = false;
        state.row = meta.defaultRow;
        state.column = meta.defaultColumn;
        state.sectionId = meta.sectionId;
        state.detachedX = 80;
        state.detachedY = 80;
        state.detachedWidth = 360;
        state.detachedHeight = 320;
        result[meta.name] = state;
    }
    return result;
}

std::wstring TelemetrySectionControl::LayoutFilePath() {
    std::wstring dir = programDataConfigDir();
    if (dir.empty()) return L"";
    return dir + L"\\telemetry-layout.json";
}

void TelemetrySectionControl::PersistLayout() {
    if (!layoutLoaded_) return;
    // Snapshot current Grid.Row/Grid.Column for every still-attached tile
    // so a drag-reorder shows up in the file even if we never set the
    // value directly in tileLayout_.
    using namespace winrt::Microsoft::UI::Xaml;
    using namespace winrt::Microsoft::UI::Xaml::Controls;
    for (const auto& meta : tileCatalog()) {
        auto& state = tileLayout_[meta.name];
        state.sectionId = meta.sectionId;
        if (state.detached) continue;
        auto tile = ResolveTileByName(meta.name);
        if (!tile) continue;
        try {
            state.row    = (int)Grid::GetRow(tile);
            state.column = (int)Grid::GetColumn(tile);
        } catch (const winrt::hresult_error&) {}
        state.visible = (tile.Visibility() == Visibility::Visible);
    }
    auto path = LayoutFilePath();
    if (path.empty()) return;
    std::string body = serializeLayoutJson(tileLayout_);
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f.write(body.data(), (std::streamsize)body.size());
}

void TelemetrySectionControl::LoadAndApplyLayout() {
    using namespace winrt::Microsoft::UI::Xaml;
    using namespace winrt::Microsoft::UI::Xaml::Controls;

    auto path = LayoutFilePath();
    std::map<std::wstring, TelemetryTileLayoutState> loaded;
    if (!path.empty()) {
        std::ifstream f(path, std::ios::binary);
        if (f.good()) {
            std::ostringstream buf;
            buf << f.rdbuf();
            loaded = parseLayoutJson(buf.str());
        }
    }
    if (loaded.empty()) {
        loaded = BuildDefaultLayout();
    }
    // Fill in missing entries with defaults (handles older layout files
    // saved before a tile was added to the catalog).
    auto defaults = BuildDefaultLayout();
    for (const auto& [name, state] : defaults) {
        if (!loaded.count(name)) loaded[name] = state;
    }
    tileLayout_ = std::move(loaded);
    layoutLoaded_ = true;

    // v0.8.1: cross-section moves persisted in v0.8.1+ files carry a
    // sectionId that may not match the catalog default. On Loaded we
    // physically reparent any tile whose persisted sectionId differs
    // from the grid that currently owns it, then apply Grid.Row/Column
    // and Visibility. Detach pass runs last so still-attached tiles have
    // their grid positions correct before we pop them.
    auto resolveSectionGridById = [this](const std::wstring& sectionId) -> Grid {
        if (sectionId == L"host-pressure")        return HostPressureGrid();
        if (sectionId == L"resource-allocation")  return ResourceAllocationGrid();
        if (sectionId == L"operational-activity") return OperationalActivityGrid();
        return nullptr;
    };
    for (const auto& meta : tileCatalog()) {
        auto& state = tileLayout_[meta.name];
        auto tile = ResolveTileByName(meta.name);
        if (!tile) continue;
        try {
            // Reparent if needed.
            std::wstring intendedSection = state.sectionId.empty()
                ? std::wstring(meta.sectionId) : state.sectionId;
            auto intendedGrid = resolveSectionGridById(intendedSection);
            if (intendedGrid) {
                auto currentParent = tile.Parent().try_as<Grid>();
                if (currentParent && currentParent != intendedGrid) {
                    auto currentChildren = currentParent.Children();
                    uint32_t idx = 0;
                    if (currentChildren.IndexOf(tile, idx)) {
                        currentChildren.RemoveAt(idx);
                    }
                    intendedGrid.Children().Append(tile);
                }
            }
            Grid::SetRow(tile, state.row);
            Grid::SetColumn(tile, state.column);
            tile.Visibility(state.visible ? Visibility::Visible : Visibility::Collapsed);
        } catch (const winrt::hresult_error&) {}
    }
    for (const auto& meta : tileCatalog()) {
        auto& state = tileLayout_[meta.name];
        if (state.detached && !detachedWindows_.count(meta.name)) {
            DetachTileToDesktop(meta.name);
        }
    }
}

void TelemetrySectionControl::ResetLayoutToDefaults() {
    // Reattach any detached windows first so the tile Borders return to
    // their grid section parents before we apply default positions.
    std::vector<std::wstring> detachedNames;
    for (const auto& [name, _] : detachedWindows_) detachedNames.push_back(name);
    for (const auto& name : detachedNames) ReattachTile(name);
    tileLayout_ = BuildDefaultLayout();
    LoadAndApplyLayout();
    PersistLayout();
    RebuildCustomizeFlyoutCheckboxes();
}

void TelemetrySectionControl::ResetLayoutButton_Click(
    Windows::Foundation::IInspectable const&,
    Microsoft::UI::Xaml::RoutedEventArgs const&) {
    ResetLayoutToDefaults();
}

winrt::Microsoft::UI::Xaml::Controls::Border
TelemetrySectionControl::ResolveTileByName(const std::wstring& tileName) {
    using namespace winrt::Microsoft::UI::Xaml::Controls;
    if (tileName == L"CpuTile")                return CpuTile();
    if (tileName == L"MemoryTile")             return MemoryTile();
    if (tileName == L"DiskTile")               return DiskTile();
    if (tileName == L"TxTile")                 return TxTile();
    if (tileName == L"RxTile")                 return RxTile();
    if (tileName == L"TrafficTile")            return TrafficTile();
    if (tileName == L"CpuBudgetTile")          return CpuBudgetTile();
    if (tileName == L"RamBudgetTile")          return RamBudgetTile();
    if (tileName == L"BandwidthBudgetTile")    return BandwidthBudgetTile();
    if (tileName == L"StorageBudgetTile")      return StorageBudgetTile();
    if (tileName == L"RuntimeLanesTile")       return RuntimeLanesTile();
    if (tileName == L"GovernanceFindingsTile") return GovernanceFindingsTile();
    if (tileName == L"AppleOperationsTile")    return AppleOperationsTile();
    if (tileName == L"PlatformGatewaysTile")   return PlatformGatewaysTile();
    if (tileName == L"GovernanceServersTile")  return GovernanceServersTile();
    return nullptr;
}

winrt::Microsoft::UI::Xaml::Controls::Grid
TelemetrySectionControl::ResolveSectionGridForTile(const std::wstring& tileName) {
    // v0.8.1: prefer the live layout state's sectionId so cross-section
    // moves resolve to the new grid. Falls back to the static catalog
    // metadata when no layout entry exists yet (very first load before
    // PersistLayout has run).
    std::wstring sectionId;
    if (auto it = tileLayout_.find(tileName); it != tileLayout_.end() && !it->second.sectionId.empty()) {
        sectionId = it->second.sectionId;
    } else if (auto* meta = findTileMetadata(tileName); meta) {
        sectionId = meta->sectionId;
    }
    if (sectionId == L"host-pressure")        return HostPressureGrid();
    if (sectionId == L"resource-allocation")  return ResourceAllocationGrid();
    if (sectionId == L"operational-activity") return OperationalActivityGrid();
    return nullptr;
}

std::wstring TelemetrySectionControl::ResolveSectionIdForTile(const std::wstring& tileName) {
    // v0.8.1: same precedence as ResolveSectionGridForTile -- live layout
    // state first, catalog default second.
    if (auto it = tileLayout_.find(tileName); it != tileLayout_.end() && !it->second.sectionId.empty()) {
        return it->second.sectionId;
    }
    auto* meta = findTileMetadata(tileName);
    return meta ? std::wstring(meta->sectionId) : L"";
}

// =====================================================================
// Drag and drop reorder
// =====================================================================

void TelemetrySectionControl::Tile_DragStarting(
    winrt::Windows::Foundation::IInspectable const& sender,
    winrt::Microsoft::UI::Xaml::DragStartingEventArgs const& e) {
    using namespace winrt::Windows::ApplicationModel::DataTransfer;
    auto border = sender.try_as<winrt::Microsoft::UI::Xaml::Controls::Border>();
    if (!border) return;
    std::wstring tileName = tagAsString(border.Tag());
    if (tileName.empty()) return;
    e.Data().SetText(winrt::hstring(tileName));
    e.Data().RequestedOperation(DataPackageOperation::Move);
}

void TelemetrySectionControl::Tile_DragOver(
    winrt::Windows::Foundation::IInspectable const&,
    winrt::Microsoft::UI::Xaml::DragEventArgs const& e) {
    using namespace winrt::Windows::ApplicationModel::DataTransfer;
    e.AcceptedOperation(DataPackageOperation::Move);
}

void TelemetrySectionControl::Tile_Drop(
    winrt::Windows::Foundation::IInspectable const& sender,
    winrt::Microsoft::UI::Xaml::DragEventArgs const& e) {
    using namespace winrt::Microsoft::UI::Xaml;
    using namespace winrt::Microsoft::UI::Xaml::Controls;

    auto target = sender.try_as<Border>();
    if (!target) return;
    std::wstring targetName = tagAsString(target.Tag());

    // The DataPackage carries the source tile's name as text.
    auto async = e.DataView().GetTextAsync();
    auto sourceName = std::wstring(async.get().c_str());
    if (sourceName.empty() || sourceName == targetName) return;

    auto source = ResolveTileByName(sourceName);
    if (!source) return;

    // v0.8.1: cross-section drag is now supported. When source and target
    // live in different section grids, reparent both: source goes to the
    // target's grid (taking the target's row/column), and target goes to
    // the source's old grid (taking the source's old row/column). Within
    // the same grid the reparent reduces to a no-op and the swap collapses
    // to an in-place attached-property exchange.
    auto sourceGrid = ResolveSectionGridForTile(sourceName);
    auto targetGrid = ResolveSectionGridForTile(targetName);
    if (!sourceGrid || !targetGrid) return;

    int sourceRow = (int)Grid::GetRow(source);
    int sourceCol = (int)Grid::GetColumn(source);
    int targetRow = (int)Grid::GetRow(target);
    int targetCol = (int)Grid::GetColumn(target);

    if (sourceGrid != targetGrid) {
        // Cross-section: physically reparent both tiles.
        auto sourceChildren = sourceGrid.Children();
        auto targetChildren = targetGrid.Children();
        uint32_t sourceIdx = 0;
        if (sourceChildren.IndexOf(source, sourceIdx)) {
            sourceChildren.RemoveAt(sourceIdx);
        }
        uint32_t targetIdx = 0;
        if (targetChildren.IndexOf(target, targetIdx)) {
            targetChildren.RemoveAt(targetIdx);
        }
        // Source -> target's section, takes target's row/col.
        Grid::SetRow(source,    targetRow);
        Grid::SetColumn(source, targetCol);
        targetChildren.Append(source);
        // Target -> source's section, takes source's old row/col.
        Grid::SetRow(target,    sourceRow);
        Grid::SetColumn(target, sourceCol);
        sourceChildren.Append(target);

        // Update the persisted sectionId for both tiles so the next
        // Loaded() applies the cross-section move correctly.
        auto& sourceState = tileLayout_[sourceName];
        auto& targetState = tileLayout_[targetName];
        std::swap(sourceState.sectionId, targetState.sectionId);
    } else {
        // Same-section: swap attached props in place.
        Grid::SetRow(source,    targetRow);
        Grid::SetColumn(source, targetCol);
        Grid::SetRow(target,    sourceRow);
        Grid::SetColumn(target, sourceCol);
    }

    PersistLayout();
}

// =====================================================================
// Hide / Detach / Customize
// =====================================================================

void TelemetrySectionControl::HideButton_Click(
    winrt::Windows::Foundation::IInspectable const& sender,
    winrt::Microsoft::UI::Xaml::RoutedEventArgs const&) {
    auto button = sender.try_as<winrt::Microsoft::UI::Xaml::Controls::Button>();
    if (!button) return;
    std::wstring tileName = tagAsString(button.Tag());
    if (tileName.empty()) return;
    ApplyTileVisibility(tileName, /*visible=*/false);
    if (auto& state = tileLayout_[tileName]; true) {
        state.visible = false;
    }
    PersistLayout();
    RebuildCustomizeFlyoutCheckboxes();
}

void TelemetrySectionControl::ApplyTileVisibility(const std::wstring& tileName, bool visible) {
    using namespace winrt::Microsoft::UI::Xaml;
    auto tile = ResolveTileByName(tileName);
    if (tile) {
        tile.Visibility(visible ? Visibility::Visible : Visibility::Collapsed);
    }
}

void TelemetrySectionControl::DetachButton_Click(
    winrt::Windows::Foundation::IInspectable const& sender,
    winrt::Microsoft::UI::Xaml::RoutedEventArgs const&) {
    auto button = sender.try_as<winrt::Microsoft::UI::Xaml::Controls::Button>();
    if (!button) return;
    std::wstring tileName = tagAsString(button.Tag());
    if (tileName.empty()) return;
    DetachTileToDesktop(tileName);
}

void TelemetrySectionControl::DetachTileToDesktop(const std::wstring& tileName) {
    using namespace winrt::Microsoft::UI::Xaml;
    using namespace winrt::Microsoft::UI::Xaml::Controls;

    if (detachedWindows_.count(tileName)) return; // already detached
    auto tile = ResolveTileByName(tileName);
    if (!tile) return;
    auto sectionGrid = ResolveSectionGridForTile(tileName);
    if (!sectionGrid) return;

    // Capture the tile's current position so re-attach can restore it.
    auto& state = tileLayout_[tileName];
    state.row    = (int)Grid::GetRow(tile);
    state.column = (int)Grid::GetColumn(tile);
    state.detached = true;

    // Remove from current parent grid.
    auto children = sectionGrid.Children();
    uint32_t childIdx = 0;
    if (children.IndexOf(tile, childIdx)) {
        children.RemoveAt(childIdx);
    }

    // Build a new top-level Window. Layout: a small header with a
    // Re-attach button, then the tile itself filling the rest. The
    // window's content runs on the same UI dispatcher as the main shell,
    // so existing x:Name accessors keep working from ApplySnapshot.
    Window detachedWindow;
    auto title = std::wstring(L"MCOS Telemetry - ");
    if (auto* meta = findTileMetadata(tileName)) {
        title += meta->displayName;
    } else {
        title += tileName;
    }
    detachedWindow.Title(winrt::hstring(title));

    Grid root;
    root.RowDefinitions().Append(RowDefinition{});
    root.RowDefinitions().Append(RowDefinition{});
    root.RowDefinitions().GetAt(0).Height(GridLengthHelper::Auto());
    root.RowDefinitions().GetAt(1).Height(GridLengthHelper::FromValueAndType(1.0, GridUnitType::Star));
    root.Padding(Thickness{12, 12, 12, 12});

    StackPanel header;
    header.Orientation(Orientation::Horizontal);
    header.Spacing(10);
    header.Margin(Thickness{0, 0, 0, 8});
    TextBlock label;
    label.Text(winrt::hstring(L"Detached telemetry tile"));
    label.VerticalAlignment(VerticalAlignment::Center);
    Button reattachButton;
    reattachButton.Content(winrt::box_value(winrt::hstring(L"Re-attach to main")));
    auto weak = get_weak();
    std::wstring captured = tileName;
    reattachButton.Click([weak, captured](auto&&, auto&&) {
        if (auto self = weak.get()) self->ReattachTile(captured);
    });
    header.Children().Append(label);
    header.Children().Append(reattachButton);

    Grid::SetRow(header, 0);
    root.Children().Append(header);
    Grid::SetRow(tile, 1);
    tile.Margin(Thickness{0});
    root.Children().Append(tile);

    detachedWindow.Content(root);
    // When the operator closes the detached window, treat it as reattach
    // so the tile returns to its section.
    detachedWindow.Closed([weak, captured](auto&&, auto&&) {
        if (auto self = weak.get()) self->ReattachTile(captured);
    });
    detachedWindow.Activate();
    detachedWindows_[tileName] = detachedWindow;

    PersistLayout();
}

void TelemetrySectionControl::ReattachTile(const std::wstring& tileName) {
    using namespace winrt::Microsoft::UI::Xaml;
    using namespace winrt::Microsoft::UI::Xaml::Controls;

    auto it = detachedWindows_.find(tileName);
    if (it == detachedWindows_.end()) return;
    auto window = it->second;
    detachedWindows_.erase(it);

    auto tile = ResolveTileByName(tileName);
    auto sectionGrid = ResolveSectionGridForTile(tileName);
    if (tile && sectionGrid) {
        // Detach from the window content first.
        auto windowContent = window.Content().try_as<Grid>();
        if (windowContent) {
            uint32_t idx = 0;
            if (windowContent.Children().IndexOf(tile, idx)) {
                windowContent.Children().RemoveAt(idx);
            }
        }
        // Restore prior grid position from layout state.
        auto& state = tileLayout_[tileName];
        state.detached = false;
        try {
            Grid::SetRow(tile, state.row);
            Grid::SetColumn(tile, state.column);
        } catch (const winrt::hresult_error&) {}
        sectionGrid.Children().Append(tile);
    }

    // Closing the window may already be in progress (if Closed fired this
    // path). Try to close once more -- harmless if already closed.
    try { window.Close(); } catch (const winrt::hresult_error&) {}

    PersistLayout();
    RebuildCustomizeFlyoutCheckboxes();
}

// =====================================================================
// Customize flyout
// =====================================================================

void TelemetrySectionControl::RebuildCustomizeFlyoutCheckboxes() {
    using namespace winrt::Microsoft::UI::Xaml;
    using namespace winrt::Microsoft::UI::Xaml::Controls;

    auto stack = CustomizeTileCheckboxStack();
    if (!stack) return;
    suspendCheckboxHandler_ = true;
    stack.Children().Clear();
    for (const auto& meta : tileCatalog()) {
        const auto& state = tileLayout_[meta.name];
        CheckBox cb;
        cb.IsChecked(winrt::Windows::Foundation::IReference<bool>(state.visible));
        std::wstring label = std::wstring(meta.displayName);
        if (state.detached) label += L"  (detached)";
        cb.Content(winrt::box_value(winrt::hstring(label)));
        std::wstring captured = meta.name;
        auto weak = get_weak();
        cb.Checked([weak, captured](auto&&, auto&&) {
            if (auto self = weak.get()) self->OnTileVisibleCheckboxChanged(captured, true);
        });
        cb.Unchecked([weak, captured](auto&&, auto&&) {
            if (auto self = weak.get()) self->OnTileVisibleCheckboxChanged(captured, false);
        });
        stack.Children().Append(cb);
    }
    suspendCheckboxHandler_ = false;
}

void TelemetrySectionControl::OnTileVisibleCheckboxChanged(
    const std::wstring& tileName, bool visible) {
    if (suspendCheckboxHandler_) return;
    auto& state = tileLayout_[tileName];
    state.visible = visible;
    // If a hidden tile is being shown again while detached, leave the
    // detached window alone -- it's still showing the live tile.
    if (!state.detached) {
        ApplyTileVisibility(tileName, visible);
    }
    PersistLayout();
}

} // namespace winrt::MasterControlShell::implementation
