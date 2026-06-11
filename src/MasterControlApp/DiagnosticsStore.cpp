// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.
//
// PHASE-14 Slice E: SqliteDiagnosticsStore implementation. WAL-mode
// SQLite keeps the single writer (DiagnosticsService::report under its
// own lock) from blocking readers (HTTP query routes). All statements
// are prepared per-call -- the write rate is operator-event scale
// (tens per minute peak), nowhere near where statement caching would
// matter, and per-call preparation keeps the locking story trivial.

#include "MasterControl/DiagnosticsStore.h"

#include <sqlite3.h>

#include <filesystem>

namespace MasterControl {

namespace {

constexpr int kSchemaVersion = 1;

// Runs DDL/DML with no result rows. Returns the sqlite error string on
// failure, empty on success.
std::string runSimpleSql(sqlite3* db, const char* sql) {
    char* errorMessage = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &errorMessage) != SQLITE_OK) {
        std::string message = errorMessage != nullptr ? errorMessage : "unknown sqlite error";
        sqlite3_free(errorMessage);
        return message;
    }
    return {};
}

std::string columnText(sqlite3_stmt* statement, int column) {
    const auto* text = sqlite3_column_text(statement, column);
    return text != nullptr ? reinterpret_cast<const char*>(text) : "";
}

} // namespace

SqliteDiagnosticsStore::SqliteDiagnosticsStore(std::filesystem::path databasePath)
    : databasePath_(std::move(databasePath)) {
    std::lock_guard<std::mutex> lock(mutex_);
    (void)openAndMigrateLocked();
}

SqliteDiagnosticsStore::~SqliteDiagnosticsStore() {
    std::lock_guard<std::mutex> lock(mutex_);
    closeLocked();
}

bool SqliteDiagnosticsStore::openAndMigrateLocked() {
    if (db_ != nullptr) {
        return true;
    }

    std::error_code ec;
    const auto parent = databasePath_.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
    }

    // sqlite3_open requires UTF-8. path::u8string is the lossless
    // conversion on Windows (the native path is UTF-16).
    const auto u8 = databasePath_.u8string();
    const std::string databasePathUtf8(u8.begin(), u8.end());
    if (sqlite3_open(databasePathUtf8.c_str(), &db_) != SQLITE_OK) {
        lastError_ = db_ != nullptr ? sqlite3_errmsg(db_) : "sqlite3_open failed";
        closeLocked();
        return false;
    }

    // WAL: writers don't block readers; busy timeout absorbs the rare
    // checkpoint collision instead of surfacing SQLITE_BUSY to callers.
    if (auto error = runSimpleSql(db_, "PRAGMA journal_mode=WAL;"); !error.empty()) {
        lastError_ = "PRAGMA journal_mode=WAL failed: " + error;
        closeLocked();
        return false;
    }
    sqlite3_busy_timeout(db_, 2000);

    if (auto error = runSimpleSql(db_,
            "CREATE TABLE IF NOT EXISTS schema_version(version INTEGER NOT NULL);");
        !error.empty()) {
        lastError_ = "schema_version create failed: " + error;
        closeLocked();
        return false;
    }

    int currentVersion = 0;
    {
        sqlite3_stmt* statement = nullptr;
        if (sqlite3_prepare_v2(db_, "SELECT version FROM schema_version LIMIT 1;",
                               -1, &statement, nullptr) == SQLITE_OK) {
            if (sqlite3_step(statement) == SQLITE_ROW) {
                currentVersion = sqlite3_column_int(statement, 0);
            }
        }
        sqlite3_finalize(statement);
    }

    if (currentVersion == 0) {
        // Fresh database: create the v1 schema.
        const char* schema =
            "CREATE TABLE IF NOT EXISTS diagnostics_events("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  captured_at_utc TEXT NOT NULL,"
            "  source TEXT NOT NULL,"
            "  severity INTEGER NOT NULL,"
            "  event_name TEXT NOT NULL,"
            "  message TEXT NOT NULL,"
            "  data_json TEXT NOT NULL,"
            "  session_id TEXT NOT NULL,"
            "  sequence INTEGER NOT NULL);"
            "CREATE INDEX IF NOT EXISTS idx_diag_captured_severity"
            "  ON diagnostics_events(captured_at_utc, severity);"
            "CREATE INDEX IF NOT EXISTS idx_diag_source_captured"
            "  ON diagnostics_events(source, captured_at_utc);"
            "INSERT INTO schema_version(version) VALUES (1);";
        if (auto error = runSimpleSql(db_, schema); !error.empty()) {
            lastError_ = "v1 schema create failed: " + error;
            closeLocked();
            return false;
        }
        currentVersion = 1;
    }

    // Future migrations: `if (currentVersion == 1) { ...ALTER...;
    // UPDATE schema_version SET version=2; currentVersion = 2; }`
    if (currentVersion != kSchemaVersion) {
        lastError_ = "Unsupported diagnostics schema version "
            + std::to_string(currentVersion) + " (this build supports "
            + std::to_string(kSchemaVersion) + ").";
        closeLocked();
        return false;
    }

    lastError_.clear();
    return true;
}

void SqliteDiagnosticsStore::closeLocked() {
    if (db_ != nullptr) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

DiagnosticsStoreResult SqliteDiagnosticsStore::insert(DiagnosticsRecord& record) {
    std::lock_guard<std::mutex> lock(mutex_);
    DiagnosticsStoreResult result;
    if (!openAndMigrateLocked()) {
        result.message = lastError_;
        return result;
    }

    sqlite3_stmt* statement = nullptr;
    const char* sql =
        "INSERT INTO diagnostics_events("
        "captured_at_utc, source, severity, event_name, message,"
        " data_json, session_id, sequence)"
        " VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8);";
    if (sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr) != SQLITE_OK) {
        lastError_ = sqlite3_errmsg(db_);
        result.message = lastError_;
        return result;
    }

    const auto dataJson = record.data.dump();
    sqlite3_bind_text(statement, 1, record.capturedAtUtc.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 2, record.source.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(statement, 3, static_cast<int>(record.severity));
    sqlite3_bind_text(statement, 4, record.eventName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 5, record.message.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 6, dataJson.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 7, record.sessionId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(statement, 8, record.sequence);

    const int stepResult = sqlite3_step(statement);
    sqlite3_finalize(statement);
    if (stepResult != SQLITE_DONE) {
        lastError_ = sqlite3_errmsg(db_);
        result.message = lastError_;
        return result;
    }

    record.id = sqlite3_last_insert_rowid(db_);
    result.ok = true;
    result.affectedRows = 1;
    return result;
}

std::vector<DiagnosticsRecord> SqliteDiagnosticsStore::query(const DiagnosticsQuery& filters) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<DiagnosticsRecord> records;
    if (db_ == nullptr) {
        return records;
    }

    std::string sql =
        "SELECT id, captured_at_utc, source, severity, event_name,"
        " message, data_json, session_id, sequence"
        " FROM diagnostics_events WHERE 1=1";
    if (!filters.severity.empty())  sql += " AND severity = ?";
    if (!filters.source.empty())    sql += " AND source = ?";
    if (!filters.eventName.empty()) sql += " AND event_name = ?";
    if (!filters.sinceUtc.empty())  sql += " AND captured_at_utc >= ?";
    sql += " ORDER BY id DESC";
    if (filters.maxResults > 0)     sql += " LIMIT ?";

    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &statement, nullptr) != SQLITE_OK) {
        lastError_ = sqlite3_errmsg(db_);
        return records;
    }

    int parameter = 1;
    if (!filters.severity.empty()) {
        sqlite3_bind_int(statement, parameter++,
            static_cast<int>(diagnosticsSeverityFromString(filters.severity)));
    }
    if (!filters.source.empty()) {
        sqlite3_bind_text(statement, parameter++, filters.source.c_str(), -1, SQLITE_TRANSIENT);
    }
    if (!filters.eventName.empty()) {
        sqlite3_bind_text(statement, parameter++, filters.eventName.c_str(), -1, SQLITE_TRANSIENT);
    }
    if (!filters.sinceUtc.empty()) {
        sqlite3_bind_text(statement, parameter++, filters.sinceUtc.c_str(), -1, SQLITE_TRANSIENT);
    }
    if (filters.maxResults > 0) {
        sqlite3_bind_int64(statement, parameter++, static_cast<std::int64_t>(filters.maxResults));
    }

    while (sqlite3_step(statement) == SQLITE_ROW) {
        DiagnosticsRecord record;
        record.id            = sqlite3_column_int64(statement, 0);
        record.capturedAtUtc = columnText(statement, 1);
        record.source        = columnText(statement, 2);
        record.severity      = static_cast<DiagnosticsSeverity>(sqlite3_column_int(statement, 3));
        record.eventName     = columnText(statement, 4);
        record.message       = columnText(statement, 5);
        try {
            record.data = nlohmann::json::parse(columnText(statement, 6));
        } catch (const std::exception&) {
            record.data = nlohmann::json::object();
        }
        record.sessionId = columnText(statement, 7);
        record.sequence  = sqlite3_column_int64(statement, 8);
        records.push_back(std::move(record));
    }
    sqlite3_finalize(statement);
    return records;
}

std::int64_t SqliteDiagnosticsStore::count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (db_ == nullptr) {
        return 0;
    }
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM diagnostics_events;",
                           -1, &statement, nullptr) != SQLITE_OK) {
        return 0;
    }
    std::int64_t total = 0;
    if (sqlite3_step(statement) == SQLITE_ROW) {
        total = sqlite3_column_int64(statement, 0);
    }
    sqlite3_finalize(statement);
    return total;
}

DiagnosticsStoreResult SqliteDiagnosticsStore::clear(const std::string& reason,
                                                     const std::string& retainSinceUtc) {
    std::lock_guard<std::mutex> lock(mutex_);
    DiagnosticsStoreResult result;
    if (!openAndMigrateLocked()) {
        result.message = lastError_;
        return result;
    }

    sqlite3_stmt* statement = nullptr;
    if (retainSinceUtc.empty()) {
        if (sqlite3_prepare_v2(db_, "DELETE FROM diagnostics_events;",
                               -1, &statement, nullptr) != SQLITE_OK) {
            lastError_ = sqlite3_errmsg(db_);
            result.message = lastError_;
            return result;
        }
    } else {
        if (sqlite3_prepare_v2(db_,
                "DELETE FROM diagnostics_events WHERE captured_at_utc < ?1;",
                -1, &statement, nullptr) != SQLITE_OK) {
            lastError_ = sqlite3_errmsg(db_);
            result.message = lastError_;
            return result;
        }
        sqlite3_bind_text(statement, 1, retainSinceUtc.c_str(), -1, SQLITE_TRANSIENT);
    }

    const int stepResult = sqlite3_step(statement);
    sqlite3_finalize(statement);
    if (stepResult != SQLITE_DONE) {
        lastError_ = sqlite3_errmsg(db_);
        result.message = lastError_;
        return result;
    }
    result.ok = true;
    result.affectedRows = sqlite3_changes64(db_);
    result.message = reason;
    return result;
}

DiagnosticsStoreResult SqliteDiagnosticsStore::pruneSelfTestSnapshots(std::int64_t keepCount) {
    std::lock_guard<std::mutex> lock(mutex_);
    DiagnosticsStoreResult result;
    if (!openAndMigrateLocked()) {
        result.message = lastError_;
        return result;
    }
    sqlite3_stmt* statement = nullptr;
    const char* sql =
        "DELETE FROM diagnostics_events WHERE source = 'self-test' AND id NOT IN"
        " (SELECT id FROM diagnostics_events WHERE source = 'self-test'"
        "  ORDER BY id DESC LIMIT ?1);";
    if (sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr) != SQLITE_OK) {
        lastError_ = sqlite3_errmsg(db_);
        result.message = lastError_;
        return result;
    }
    sqlite3_bind_int64(statement, 1, keepCount > 0 ? keepCount : 0);
    const int stepResult = sqlite3_step(statement);
    sqlite3_finalize(statement);
    if (stepResult != SQLITE_DONE) {
        lastError_ = sqlite3_errmsg(db_);
        result.message = lastError_;
        return result;
    }
    result.ok = true;
    result.affectedRows = sqlite3_changes64(db_);
    return result;
}

bool SqliteDiagnosticsStore::available() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return db_ != nullptr;
}

std::string SqliteDiagnosticsStore::lastError() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastError_;
}

std::unique_ptr<IDiagnosticsStore> createSqliteDiagnosticsStore(std::filesystem::path databasePath) {
    return std::make_unique<SqliteDiagnosticsStore>(std::move(databasePath));
}

} // namespace MasterControl
