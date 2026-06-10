// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.
//
// PHASE-14 Slice E: DiagnosticsService implementation.

#include "MasterControl/DiagnosticsService.h"

#include <algorithm>

namespace MasterControl {

DiagnosticsService::DiagnosticsService(std::unique_ptr<IDiagnosticsStore> store,
                                       std::string sessionId)
    : store_(std::move(store))
    , sessionId_(std::move(sessionId)) {}

DiagnosticsService::ReportResult DiagnosticsService::report(
        DiagnosticsSeverity severity,
        const std::string& source,
        const std::string& eventName,
        const std::string& message,
        nlohmann::json data,
        const std::string& capturedAtUtc) {
    std::lock_guard<std::mutex> lock(mutex_);

    DiagnosticsRecord record;
    record.capturedAtUtc = capturedAtUtc;
    record.source = source.empty() ? std::string("runtime") : source;
    record.severity = severity;
    record.eventName = eventName;
    record.message = message;
    record.data = std::move(data);
    record.sessionId = sessionId_;
    record.sequence = nextSequence_++;

    ReportResult result;
    if (store_) {
        const auto stored = store_->insert(record);
        result.ok = stored.ok;
        result.message = stored.message;
    } else {
        result.message = "No persistent store configured; record held in the in-memory ring only.";
    }

    // The ring accepts the record regardless of store health so the
    // runtime keeps a queryable recent history while the disk is sick.
    ring_.push_back(std::move(record));
    while (ring_.size() > kRingCapacity) {
        ring_.pop_front();
    }
    result.ringAccepted = true;
    return result;
}

DiagnosticsService::ReportResult DiagnosticsService::recordSelfTest(
        const nlohmann::json& snapshotJson,
        const std::string& capturedAtUtc) {
    auto result = report(DiagnosticsSeverity::Info,
                         "self-test",
                         "boot_self_test_snapshot",
                         "Boot self-test snapshot persisted.",
                         snapshotJson,
                         capturedAtUtc);
    if (store_) {
        std::lock_guard<std::mutex> lock(mutex_);
        (void)store_->pruneSelfTestSnapshots(kSelfTestRetention);
    }
    return result;
}

std::vector<DiagnosticsRecord> DiagnosticsService::query(const DiagnosticsQuery& filters) const {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (store_ && store_->available()) {
            return store_->query(filters);
        }
    }

    // Ring fallback: newest-first with in-memory filtering, mirroring
    // the store's ordering contract.
    std::vector<DiagnosticsRecord> results;
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto iterator = ring_.rbegin(); iterator != ring_.rend(); ++iterator) {
        const auto& record = *iterator;
        if (!filters.severity.empty()
            && record.severity != diagnosticsSeverityFromString(filters.severity)) {
            continue;
        }
        if (!filters.source.empty() && record.source != filters.source) {
            continue;
        }
        if (!filters.eventName.empty() && record.eventName != filters.eventName) {
            continue;
        }
        if (!filters.sinceUtc.empty() && record.capturedAtUtc < filters.sinceUtc) {
            continue;
        }
        results.push_back(record);
        if (filters.maxResults > 0 && results.size() >= filters.maxResults) {
            break;
        }
    }
    return results;
}

std::int64_t DiagnosticsService::storeCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return store_ ? store_->count() : 0;
}

bool DiagnosticsService::storeAvailable() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return store_ && store_->available();
}

std::string DiagnosticsService::storeLastError() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return store_ ? store_->lastError() : std::string("No persistent store configured.");
}

DiagnosticsStoreResult DiagnosticsService::clear(const std::string& reason,
                                                 const std::string& retainSinceUtc,
                                                 const std::string& capturedAtUtc) {
    DiagnosticsStoreResult cleared;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!store_) {
            cleared.message = "No persistent store configured; nothing to clear.";
            // The in-memory ring is still cleared so the operator's
            // intent ("wipe the visible history") is honored.
            ring_.clear();
            cleared.ok = true;
            return cleared;
        }
        cleared = store_->clear(reason, retainSinceUtc);
        if (cleared.ok) {
            if (retainSinceUtc.empty()) {
                ring_.clear();
            } else {
                ring_.erase(
                    std::remove_if(ring_.begin(), ring_.end(),
                        [&](const DiagnosticsRecord& record) {
                            return record.capturedAtUtc < retainSinceUtc;
                        }),
                    ring_.end());
            }
        }
    }
    if (cleared.ok) {
        // Audit the clear itself (outside the lock; report re-locks).
        (void)report(DiagnosticsSeverity::Info,
                     "runtime",
                     "diagnostics_cleared",
                     reason.empty()
                         ? std::string("Diagnostics cleared by operator request.")
                         : ("Diagnostics cleared: " + reason),
                     nlohmann::json{
                         { "deletedRows", cleared.affectedRows },
                         { "retainSinceUtc", retainSinceUtc }
                     },
                     capturedAtUtc);
    }
    return cleared;
}

} // namespace MasterControl
