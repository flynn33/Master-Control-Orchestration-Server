// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.
//
// PHASE-14 Slice E: persistent diagnostics store. The interface is
// pure-virtual so tests can substitute fakes; the shipping
// implementation is SqliteDiagnosticsStore (WAL-mode SQLite file under
// <PUBLIC>\Documents\Master Control Orchestration Server\diagnostics\).
//
// "No fake telemetry" rule: every mutating call returns a structured
// result. When the database is unreachable (locked, disk full,
// unwritable path) the store reports ok=false with the sqlite message;
// it never pretends a write happened.

#pragma once

#include "MasterControl/DiagnosticsTypes.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

struct sqlite3;

namespace MasterControl {

struct DiagnosticsStoreResult final {
    bool ok = false;
    std::string message;
    std::int64_t affectedRows = 0;
};

class IDiagnosticsStore {
public:
    virtual ~IDiagnosticsStore() = default;
    virtual DiagnosticsStoreResult insert(DiagnosticsRecord& record) = 0;
    virtual std::vector<DiagnosticsRecord> query(const DiagnosticsQuery& filters) const = 0;
    virtual std::int64_t count() const = 0;
    // Clear-with-retention: removes records older than `retainSinceUtc`
    // (ISO-8601). An empty bound removes everything. The deletion is
    // recorded as a fresh informational record carrying `reason` so the
    // clear itself is auditable.
    virtual DiagnosticsStoreResult clear(const std::string& reason,
                                         const std::string& retainSinceUtc) = 0;
    // Retention pass for self-test snapshots: keeps the most recent
    // `keepCount` records with source == "self-test", deletes older ones.
    virtual DiagnosticsStoreResult pruneSelfTestSnapshots(std::int64_t keepCount) = 0;
    virtual bool available() const = 0;
    virtual std::string lastError() const = 0;
};

// WAL-mode SQLite implementation. Schema (migrated via the
// schema_version table; current version 1):
//   diagnostics_events(
//     id INTEGER PRIMARY KEY AUTOINCREMENT,
//     captured_at_utc TEXT, source TEXT, severity INTEGER,
//     event_name TEXT, message TEXT, data_json TEXT,
//     session_id TEXT, sequence INTEGER)
//   indexes: (captured_at_utc, severity), (source, captured_at_utc)
class SqliteDiagnosticsStore final : public IDiagnosticsStore {
public:
    explicit SqliteDiagnosticsStore(std::filesystem::path databasePath);
    ~SqliteDiagnosticsStore() override;

    SqliteDiagnosticsStore(const SqliteDiagnosticsStore&) = delete;
    SqliteDiagnosticsStore& operator=(const SqliteDiagnosticsStore&) = delete;

    DiagnosticsStoreResult insert(DiagnosticsRecord& record) override;
    std::vector<DiagnosticsRecord> query(const DiagnosticsQuery& filters) const override;
    std::int64_t count() const override;
    DiagnosticsStoreResult clear(const std::string& reason,
                                 const std::string& retainSinceUtc) override;
    DiagnosticsStoreResult pruneSelfTestSnapshots(std::int64_t keepCount) override;
    bool available() const override;
    std::string lastError() const override;

private:
    bool openAndMigrateLocked();
    void closeLocked();

    mutable std::mutex mutex_;
    std::filesystem::path databasePath_;
    sqlite3* db_ = nullptr;
    std::string lastError_;
};

std::unique_ptr<IDiagnosticsStore> createSqliteDiagnosticsStore(std::filesystem::path databasePath);

} // namespace MasterControl
