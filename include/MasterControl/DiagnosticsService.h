// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.
//
// PHASE-14 Slice E: centralized Diagnostics Service. Owns the
// in-memory ring (most recent kRingCapacity records, always available
// even when the persistent store is not) and the SqliteDiagnosticsStore.
//
// "No fake telemetry": report() returns a structured result. When the
// store is unreachable the ring still accepts the record (the runtime
// keeps working and recent history stays queryable) and the result
// carries ok=false + the store's reason; summary() exposes the same
// condition via storeUnavailable.

#pragma once

#include "MasterControl/DiagnosticsStore.h"

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace MasterControl {

class DiagnosticsService final {
public:
    static constexpr std::size_t kRingCapacity = 1000;
    // Self-test snapshots retained across boots (one record per boot).
    static constexpr std::int64_t kSelfTestRetention = 50;

    // `store` may be null (tests, store-less configurations): the
    // service then runs ring-only. `sessionId` groups this service
    // lifetime's records; pass something boot-unique.
    DiagnosticsService(std::unique_ptr<IDiagnosticsStore> store,
                       std::string sessionId);

    struct ReportResult final {
        bool ok = false;            // persisted to the store
        bool ringAccepted = false;  // always true unless severely broken
        std::string message;
    };

    ReportResult report(DiagnosticsSeverity severity,
                        const std::string& source,
                        const std::string& eventName,
                        const std::string& message,
                        nlohmann::json data,
                        const std::string& capturedAtUtc);

    // Persists one record (source="self-test") carrying the snapshot
    // JSON in data, then prunes to the kSelfTestRetention most recent
    // self-test records.
    ReportResult recordSelfTest(const nlohmann::json& snapshotJson,
                                const std::string& capturedAtUtc);

    // Store-backed when the store is available; falls back to the ring
    // otherwise (filters applied in-memory).
    std::vector<DiagnosticsRecord> query(const DiagnosticsQuery& filters) const;

    std::int64_t storeCount() const;
    bool storeAvailable() const;
    std::string storeLastError() const;

    // Clear-with-retention. Empty retainSinceUtc clears everything.
    // The clear is audited: a fresh info record carrying the reason +
    // deleted count is reported after the deletion.
    DiagnosticsStoreResult clear(const std::string& reason,
                                 const std::string& retainSinceUtc,
                                 const std::string& capturedAtUtc);

    const std::string& sessionId() const { return sessionId_; }

private:
    mutable std::mutex mutex_;
    std::unique_ptr<IDiagnosticsStore> store_;
    std::string sessionId_;
    std::int64_t nextSequence_ = 1;
    std::deque<DiagnosticsRecord> ring_;
};

} // namespace MasterControl
