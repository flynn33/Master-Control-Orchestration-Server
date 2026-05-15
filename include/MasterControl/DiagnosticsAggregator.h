// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.
//
// DiagnosticsAggregator.h - testable helpers backing the v0.11.0
// PHASE-14 Slice A /api/diagnostics/* HTTP surface.
//
// Pre-v0.11.0 (per Copilot review on PR #7) the aggregation logic
// lived inline as a lambda inside the route handler in
// MasterControlApp/MasterControlRuntime.cpp. That made the new
// public API surface impossible to cover with the existing custom
// bool-test runner (no in-process HTTP fixture). v0.11.0 factors
// the aggregation + markdown rendering into header-only inline
// helpers so the test suite can exercise them against a synthetic
// logs directory without spinning up an HTTP server.
//
// Behavioural contract (matches what the inline lambda did):
//
//   aggregateDiagnosticsEventsFromRoot(logsRoot, softCap):
//     - Walks <logsRoot>/<component>/events.jsonl for every component
//       directory found at the first level under logsRoot. Each line
//       is parsed as a JSON object; partial-write lines (parse errors)
//       are silently skipped.
//     - If `softCap > 0` and the live-pass already produced
//       (softCap * 4 + 32) events, the rotated-pass is skipped
//       entirely. 4x headroom covers downstream severity-filter
//       truncation without surprises.
//     - Pass 2 (rotated): when needed, walks the same directories
//       again reading events.jsonl.1. Stops early once the cap is
//       reached.
//     - Sorts the merged vector recent-first by `capturedAtUtc`
//       (ISO-8601 strings sort lexicographically).
//     - Returns the merged events. Empty vector when logsRoot does
//       not exist.
//
//   renderDiagnosticsMarkdown(events, capturedAtUtc, version):
//     - Renders the operator-visible Markdown export. Sections per
//       severity in fixed order: critical, error, warning (matches
//       both "warning" and "warn" severity strings), info, debug.
//     - Each section is capped at 50 events with an explicit
//       "_…N more <heading> events truncated…_" note so the operator
//       export can be attached to a support thread without ballooning
//       to MB-class size.

#pragma once

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace MasterControl {

namespace DiagnosticsAggregatorDetail {

inline void readJsonlLinesInto(const std::filesystem::path& path,
                                std::vector<nlohmann::json>& sink) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return;
    }
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) {
            continue;
        }
        try {
            auto parsed = nlohmann::json::parse(line);
            sink.push_back(std::move(parsed));
        } catch (...) {
            // Partial-write tolerance: skip bad line.
        }
    }
}

} // namespace DiagnosticsAggregatorDetail

inline std::vector<nlohmann::json> aggregateDiagnosticsEventsFromRoot(
    const std::filesystem::path& logsRoot,
    std::size_t softCap) {
    std::vector<nlohmann::json> events;
    std::error_code ec;
    if (!std::filesystem::exists(logsRoot, ec)) {
        return events;
    }

    // Headroom multiplier so post-filter truncation upstream still
    // hits the cap. 4x covers ?severity=error filtering against an
    // info-dominated stream without surprises.
    const std::size_t headroomCap = (softCap == 0) ? 0 : (softCap * 4 + 32);
    const auto haveEnough = [&]() {
        return headroomCap != 0 && events.size() >= headroomCap;
    };

    // Pass 1: live events.jsonl across all components.
    for (const auto& componentDir : std::filesystem::directory_iterator(logsRoot, ec)) {
        if (ec) {
            break;
        }
        std::error_code dirEc;
        if (!componentDir.is_directory(dirEc)) {
            continue;
        }
        const auto live = componentDir.path() / "events.jsonl";
        std::error_code existEc;
        if (std::filesystem::exists(live, existEc)) {
            DiagnosticsAggregatorDetail::readJsonlLinesInto(live, events);
        }
    }

    // Pass 2: rotated events.jsonl.1 -- only if the live pass did
    // not already cover the soft cap (with headroom).
    if (!haveEnough()) {
        std::error_code rotEc;
        for (const auto& componentDir : std::filesystem::directory_iterator(logsRoot, rotEc)) {
            if (rotEc) {
                break;
            }
            std::error_code dirEc;
            if (!componentDir.is_directory(dirEc)) {
                continue;
            }
            const auto rotated = componentDir.path() / "events.jsonl.1";
            std::error_code existEc;
            if (std::filesystem::exists(rotated, existEc)) {
                DiagnosticsAggregatorDetail::readJsonlLinesInto(rotated, events);
            }
            if (haveEnough()) {
                break;
            }
        }
    }

    // Sort recent-first by capturedAtUtc.
    std::sort(events.begin(), events.end(),
              [](const nlohmann::json& a, const nlohmann::json& b) {
                  const auto av = a.value("capturedAtUtc", std::string());
                  const auto bv = b.value("capturedAtUtc", std::string());
                  return av > bv;
              });
    return events;
}

inline std::string renderDiagnosticsMarkdown(
    const std::vector<nlohmann::json>& events,
    const std::string& capturedAtUtc,
    const std::string& version) {
    std::ostringstream md;
    md << "# MCOS Diagnostics Snapshot\n\n";
    md << "**Captured:** " << capturedAtUtc << "  \n";
    md << "**Version:** " << version << "  \n";
    md << "**Total events:** " << events.size() << "  \n\n";

    // Section iterator matches BOTH `warning` (TelemetrySeverity) and
    // `warn` (boot self-test row severity strings) under the same
    // "warning" section heading -- without that aliasing the markdown
    // export silently drops every `warn` entry that the JSON / summary
    // path correctly counts.
    struct SeveritySection {
        const char* heading;
        std::vector<std::string> matchValues;
    };
    const std::vector<SeveritySection> severityOrder = {
        { "critical", {"critical"} },
        { "error",    {"error"} },
        { "warning",  {"warning", "warn"} },
        { "info",     {"info"} },
        { "debug",    {"debug"} }
    };
    const auto matchesSection = [](const std::string& sev,
                                   const std::vector<std::string>& matchValues) -> bool {
        for (const auto& m : matchValues) {
            if (sev == m) {
                return true;
            }
        }
        return false;
    };

    for (const auto& section : severityOrder) {
        std::size_t count = 0;
        for (const auto& e : events) {
            if (matchesSection(e.value("severity", std::string()), section.matchValues)) {
                ++count;
            }
        }
        if (count == 0) {
            continue;
        }
        md << "## " << section.heading << " (" << count << ")\n\n";
        std::size_t emitted = 0;
        for (const auto& e : events) {
            if (!matchesSection(e.value("severity", std::string()), section.matchValues)) {
                continue;
            }
            if (emitted >= 50) {
                md << "_…" << (count - emitted) << " more " << section.heading
                   << " events truncated…_\n\n";
                break;
            }
            md << "- **" << e.value("capturedAtUtc", std::string("?")) << "** "
               << "`" << e.value("component", std::string("?")) << "`"
               << " · `" << e.value("event", std::string("?")) << "` — "
               << e.value("message", std::string("(no message)")) << "\n";
            ++emitted;
        }
        md << "\n";
    }
    return md.str();
}

} // namespace MasterControl
