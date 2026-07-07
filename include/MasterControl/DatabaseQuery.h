// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.
//
// local-database MCP: read-only query + schema inspection over an
// operator-configured SQLite database. Interface-first: IDatabaseConnectionStore
// resolves the configured database path; IDatabaseQueryExecutor performs the
// read-only work; SqliteReadonlyQueryExecutor is the production concrete.
//
// Read-only safety is enforced three ways, defence in depth:
//   1. The database file is opened with SQLITE_OPEN_READONLY, so the
//      connection physically cannot write.
//   2. Every prepared statement must satisfy sqlite3_stmt_readonly() -- this
//      rejects INSERT/UPDATE/DELETE/DROP/ALTER/CREATE and PRAGMA writes.
//   3. Only a single statement is accepted (a non-empty prepare tail is
//      rejected), and ATTACH/DETACH are rejected by leading keyword.
// A statement timeout and a row limit bound resource use.
//
// The executor is header-only so both the worker executable and the test
// suite can compile it directly; both link the vcpkg sqlite3 port.

#pragma once

#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include <cctype>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace MasterControl {

struct DatabaseQueryResult final {
    bool ok = false;
    std::string errorKind;   // "" | unconfigured | open-failed | prepare-failed |
                             // rejected-mutation | multiple-statements | timeout | runtime-error
    std::string error;       // human-readable detail (empty on success)
    nlohmann::json data = nlohmann::json::object();
    bool truncated = false;  // hit the row limit
};

// Resolves the configured SQLite database file path. Returns nullopt when the
// operator has not configured one.
class IDatabaseConnectionStore {
public:
    virtual ~IDatabaseConnectionStore() = default;
    virtual std::optional<std::string> currentDatabasePath() const = 0;
};

// Read-only query + schema inspection contract.
class IDatabaseQueryExecutor {
public:
    virtual ~IDatabaseQueryExecutor() = default;
    virtual DatabaseQueryResult status() const = 0;
    virtual DatabaseQueryResult listTables() const = 0;
    virtual DatabaseQueryResult describeTable(const std::string& table) const = 0;
    virtual DatabaseQueryResult queryReadonly(const std::string& sql, int rowLimit) const = 0;
};

class SqliteReadonlyQueryExecutor final : public IDatabaseQueryExecutor {
public:
    explicit SqliteReadonlyQueryExecutor(std::shared_ptr<IDatabaseConnectionStore> store,
                                         int defaultRowLimit = 1000,
                                         int statementTimeoutMs = 5000)
        : store_(std::move(store))
        , defaultRowLimit_(defaultRowLimit > 0 ? defaultRowLimit : 1000)
        , timeoutMs_(statementTimeoutMs > 0 ? statementTimeoutMs : 5000) {}

    DatabaseQueryResult status() const override {
        DatabaseQueryResult result;
        const auto path = store_ ? store_->currentDatabasePath() : std::nullopt;
        nlohmann::json info{
            { "provider", "sqlite" },
            { "configured", path.has_value() && !path->empty() },
            { "alphaLimitation",
              "Alpha provider is read-only SQLite. Other providers (ODBC/Postgres/MySQL) are deferred." }
        };
        if (!path.has_value() || path->empty()) {
            info["hint"] = "Configure a SQLite database file path with db.set_connection_string.";
            result.ok = true;  // status itself succeeds; it honestly reports unconfigured.
            result.data = info;
            return result;
        }
        info["path"] = *path;
        sqlite3* db = nullptr;
        const int rc = sqlite3_open_v2(path->c_str(), &db, SQLITE_OPEN_READONLY, nullptr);
        info["readable"] = (rc == SQLITE_OK);
        if (rc != SQLITE_OK) {
            info["openError"] = db ? sqlite3_errmsg(db) : "unable to open database";
        }
        if (db) { sqlite3_close(db); }
        result.ok = true;
        result.data = info;
        return result;
    }

    DatabaseQueryResult listTables() const override {
        return queryReadonly(
            "SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%' ORDER BY name",
            defaultRowLimit_);
    }

    DatabaseQueryResult describeTable(const std::string& table) const override {
        // Use the table-valued pragma function so the table name is a bound
        // parameter (no string interpolation into SQL). This is a read-only
        // SELECT, so it passes the sqlite3_stmt_readonly gate.
        return runSelect(
            "SELECT cid, name, type, \"notnull\", dflt_value, pk FROM pragma_table_info(?1)",
            defaultRowLimit_, table);
    }

    DatabaseQueryResult queryReadonly(const std::string& sql, int rowLimit) const override {
        return runSelect(sql, rowLimit > 0 ? rowLimit : defaultRowLimit_, std::nullopt);
    }

private:
    static DatabaseQueryResult fail(const char* kind, std::string message) {
        DatabaseQueryResult r;
        r.ok = false;
        r.errorKind = kind;
        r.error = std::move(message);
        return r;
    }

    static std::string leadingKeyword(const std::string& sql) {
        std::size_t i = 0;
        while (i < sql.size() && std::isspace(static_cast<unsigned char>(sql[i]))) { ++i; }
        std::string word;
        while (i < sql.size() && std::isalpha(static_cast<unsigned char>(sql[i]))) {
            word.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(sql[i]))));
            ++i;
        }
        return word;
    }

    static bool tailHasStatement(const char* tail) {
        if (tail == nullptr) { return false; }
        for (const char* p = tail; *p != '\0'; ++p) {
            if (!std::isspace(static_cast<unsigned char>(*p)) && *p != ';') { return true; }
        }
        return false;
    }

    struct ProgressDeadline final {
        std::chrono::steady_clock::time_point deadline;
    };

    static int progressCallback(void* payload) {
        const auto* state = static_cast<ProgressDeadline*>(payload);
        return (std::chrono::steady_clock::now() >= state->deadline) ? 1 : 0;
    }

    DatabaseQueryResult runSelect(const std::string& sql, int rowLimit,
                                  const std::optional<std::string>& bindText) const {
        const auto path = store_ ? store_->currentDatabasePath() : std::nullopt;
        if (!path.has_value() || path->empty()) {
            return fail("unconfigured",
                        "No database is configured. Set a SQLite path with db.set_connection_string.");
        }
        // Reject ATTACH/DETACH by leading keyword (defence in depth; the
        // read-only open + stmt_readonly gate below is the primary control).
        const std::string keyword = leadingKeyword(sql);
        if (keyword == "ATTACH" || keyword == "DETACH") {
            return fail("rejected-mutation", "Statement kind is not permitted: " + keyword);
        }

        sqlite3* db = nullptr;
        if (sqlite3_open_v2(path->c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
            const std::string message = db ? sqlite3_errmsg(db) : "unable to open database read-only";
            if (db) { sqlite3_close(db); }
            return fail("open-failed", message);
        }
        sqlite3_busy_timeout(db, timeoutMs_);
        ProgressDeadline deadline{ std::chrono::steady_clock::now()
                                   + std::chrono::milliseconds(timeoutMs_) };
        sqlite3_progress_handler(db, 10000, &SqliteReadonlyQueryExecutor::progressCallback, &deadline);

        sqlite3_stmt* stmt = nullptr;
        const char* tail = nullptr;
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, &tail) != SQLITE_OK) {
            const std::string message = sqlite3_errmsg(db);
            if (stmt) { sqlite3_finalize(stmt); }
            sqlite3_close(db);
            return fail("prepare-failed", message);
        }
        if (tailHasStatement(tail)) {
            sqlite3_finalize(stmt);
            sqlite3_close(db);
            return fail("multiple-statements",
                        "Only a single read-only statement is permitted.");
        }
        if (sqlite3_stmt_readonly(stmt) == 0) {
            sqlite3_finalize(stmt);
            sqlite3_close(db);
            return fail("rejected-mutation",
                        "Only read-only statements are permitted; mutating SQL is rejected.");
        }
        if (bindText.has_value()) {
            sqlite3_bind_text(stmt, 1, bindText->c_str(),
                              static_cast<int>(bindText->size()), SQLITE_TRANSIENT);
        }

        DatabaseQueryResult result;
        nlohmann::json columns = nlohmann::json::array();
        const int columnCount = sqlite3_column_count(stmt);
        for (int c = 0; c < columnCount; ++c) {
            const char* name = sqlite3_column_name(stmt, c);
            columns.push_back(name ? name : "");
        }
        nlohmann::json rows = nlohmann::json::array();
        int stepRc = SQLITE_OK;
        int rowCount = 0;
        while ((stepRc = sqlite3_step(stmt)) == SQLITE_ROW) {
            if (rowCount >= rowLimit) {
                result.truncated = true;
                break;
            }
            nlohmann::json row = nlohmann::json::array();
            for (int c = 0; c < columnCount; ++c) {
                row.push_back(columnValue(stmt, c));
            }
            rows.push_back(std::move(row));
            ++rowCount;
        }
        const bool interrupted = (stepRc == SQLITE_INTERRUPT);
        const bool errored = (stepRc != SQLITE_DONE && stepRc != SQLITE_ROW && !interrupted);
        const std::string stepError = errored ? sqlite3_errmsg(db) : std::string{};
        sqlite3_finalize(stmt);
        sqlite3_close(db);

        if (interrupted) {
            return fail("timeout", "Query exceeded the configured statement timeout.");
        }
        if (errored) {
            return fail("runtime-error", stepError);
        }
        result.ok = true;
        result.data = nlohmann::json{
            { "columns", columns },
            { "rows", rows },
            { "rowCount", rowCount },
        };
        return result;
    }

    static nlohmann::json columnValue(sqlite3_stmt* stmt, int column) {
        switch (sqlite3_column_type(stmt, column)) {
            case SQLITE_INTEGER:
                return nlohmann::json(static_cast<std::int64_t>(sqlite3_column_int64(stmt, column)));
            case SQLITE_FLOAT:
                return nlohmann::json(sqlite3_column_double(stmt, column));
            case SQLITE_TEXT: {
                const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, column));
                return nlohmann::json(text ? std::string(text) : std::string{});
            }
            case SQLITE_BLOB:
                return nlohmann::json{ { "blobBytes", sqlite3_column_bytes(stmt, column) } };
            case SQLITE_NULL:
            default:
                return nlohmann::json(nullptr);
        }
    }

    std::shared_ptr<IDatabaseConnectionStore> store_;
    int defaultRowLimit_;
    int timeoutMs_;
};

} // namespace MasterControl
