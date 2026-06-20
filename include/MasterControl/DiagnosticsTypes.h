// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.
//
// PHASE-14 Slice E: public types for the centralized Diagnostics
// Service + SqliteDiagnosticsStore. Kept platform-neutral (no Windows
// headers) so tests and tooling can include it cheaply.

#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

namespace MasterControl {

// Severity ladder. Stored as the integer value in SQLite so range
// queries (>= Warning) stay index-friendly; serialized as the lowercase
// slug on every JSON surface. "warn" is accepted as an alias of
// "warning" on parse for parity with the jsonl history and the
// renderDiagnosticsMarkdown alias handling.
enum class DiagnosticsSeverity : int {
    Debug = 0,
    Info = 1,
    Warning = 2,
    Error = 3,
    Critical = 4
};

inline std::string to_string(DiagnosticsSeverity value) {
    switch (value) {
        case DiagnosticsSeverity::Debug:    return "debug";
        case DiagnosticsSeverity::Info:     return "info";
        case DiagnosticsSeverity::Warning:  return "warning";
        case DiagnosticsSeverity::Error:    return "error";
        case DiagnosticsSeverity::Critical: return "critical";
    }
    return "info";
}

inline DiagnosticsSeverity diagnosticsSeverityFromString(const std::string& value) {
    if (value == "debug")    return DiagnosticsSeverity::Debug;
    if (value == "warning" || value == "warn") return DiagnosticsSeverity::Warning;
    if (value == "error")    return DiagnosticsSeverity::Error;
    if (value == "critical") return DiagnosticsSeverity::Critical;
    return DiagnosticsSeverity::Info;
}

// One diagnostics record. `id` is the store row id (0 for records that
// have not been persisted, e.g. ring-only records while the store is
// unavailable). `sessionId` groups records by service lifetime so
// operators can filter to "this boot" / "previous boot".
struct DiagnosticsRecord final {
    std::int64_t id = 0;
    std::string capturedAtUtc;
    std::string source;          // "runtime" | "supervisor" | "installer" | "self-test" | ...
    DiagnosticsSeverity severity = DiagnosticsSeverity::Info;
    std::string eventName;
    std::string message;
    nlohmann::json data = nlohmann::json::object();
    std::string sessionId;
    std::int64_t sequence = 0;

    // Legacy jsonl-compatible shape: the Slice A HTTP routes (and their
    // consumers) speak the appendEvent jsonl schema -- component /
    // severity / event / message / data / capturedAtUtc. Store-backed
    // rows serialize to the identical shape so the route contract is
    // unchanged, with the store-only fields added additively.
    nlohmann::json toLegacyJson() const {
        return nlohmann::json{
            { "capturedAtUtc", capturedAtUtc },
            { "component", source },
            { "severity", to_string(severity) },
            { "event", eventName },
            { "message", message },
            { "data", data },
            { "sessionId", sessionId },
            { "sequence", sequence }
        };
    }
};

// Query filters. Empty string / zero members mean "no filter".
struct DiagnosticsQuery final {
    std::string severity;        // exact slug ("warn" alias accepted)
    std::string source;
    std::string eventName;
    std::string sinceUtc;        // ISO-8601 lower bound (inclusive)
    std::size_t maxResults = 0;  // 0 = unbounded
};

} // namespace MasterControl
