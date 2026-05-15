// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <Windows.h>
#include <ShlObj.h>

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace MasterControl::Diagnostics {

struct PersistentLogPaths final {
    std::filesystem::path root;
    std::filesystem::path rootLocationFile;
    std::filesystem::path componentDirectory;
    std::filesystem::path locationFile;
    std::filesystem::path eventsFile;
    std::filesystem::path telemetryFile;
};

inline std::string utf8FromWide(const std::wstring& input) {
    if (input.empty()) {
        return {};
    }

    const int required = WideCharToMultiByte(
        CP_UTF8,
        0,
        input.c_str(),
        static_cast<int>(input.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    std::string output(static_cast<size_t>(required), '\0');
    WideCharToMultiByte(
        CP_UTF8,
        0,
        input.c_str(),
        static_cast<int>(input.size()),
        output.data(),
        required,
        nullptr,
        nullptr);
    return output;
}

inline std::string pathToUtf8(const std::filesystem::path& value) {
    return utf8FromWide(value.wstring());
}

inline std::string timestampNowUtc() {
    SYSTEMTIME systemTime{};
    GetSystemTime(&systemTime);

    std::ostringstream stream;
    stream << std::setfill('0')
           << std::setw(4) << systemTime.wYear << '-'
           << std::setw(2) << systemTime.wMonth << '-'
           << std::setw(2) << systemTime.wDay << 'T'
           << std::setw(2) << systemTime.wHour << ':'
           << std::setw(2) << systemTime.wMinute << ':'
           << std::setw(2) << systemTime.wSecond << '.'
           << std::setw(3) << systemTime.wMilliseconds << 'Z';
    return stream.str();
}

inline std::optional<std::filesystem::path> publicDocumentsDirectory() {
    PWSTR path = nullptr;
    const HRESULT result = SHGetKnownFolderPath(FOLDERID_PublicDocuments, KF_FLAG_DEFAULT, nullptr, &path);
    if (FAILED(result) || path == nullptr) {
        return std::nullopt;
    }

    std::filesystem::path output(path);
    CoTaskMemFree(path);
    return output;
}

inline std::optional<std::filesystem::path> localAppDataDirectory() {
    const DWORD required = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (required == 0) {
        return std::nullopt;
    }

    std::wstring buffer(static_cast<size_t>(required - 1), L'\0');
    GetEnvironmentVariableW(L"LOCALAPPDATA", buffer.data(), required);
    return std::filesystem::path(buffer);
}

inline PersistentLogPaths resolvePersistentLogPaths(std::wstring_view componentName) {
    std::filesystem::path root;
    if (const auto publicDocuments = publicDocumentsDirectory(); publicDocuments.has_value()) {
        root = *publicDocuments / "Master Control Orchestration Server" / "logs";
    } else if (const auto localAppData = localAppDataDirectory(); localAppData.has_value()) {
        root = *localAppData / "Master Control Orchestration Server" / "logs";
    } else {
        root = std::filesystem::current_path() / "logs";
    }

    const auto componentDirectory = root / std::filesystem::path(std::wstring(componentName.empty() ? L"runtime" : componentName));
    std::filesystem::create_directories(componentDirectory);

    const PersistentLogPaths paths{
        root,
        root / "LOG-LOCATION.txt",
        componentDirectory,
        componentDirectory / "LOG-LOCATION.txt",
        componentDirectory / "events.jsonl",
        componentDirectory / "telemetry.jsonl"
    };

    std::ofstream rootLocation(paths.rootLocationFile, std::ios::binary | std::ios::trunc);
    if (rootLocation.is_open()) {
        rootLocation << "Master Control Orchestration Server persistent diagnostics\r\n"
                     << "Root: " << pathToUtf8(paths.root) << "\r\n"
                     << "Installer: " << pathToUtf8(paths.root / "installer") << "\r\n"
                     << "Runtime: " << pathToUtf8(paths.root / "runtime") << "\r\n"
                     << "Shell: " << pathToUtf8(paths.root / "shell") << "\r\n";
    }

    std::ofstream location(paths.locationFile, std::ios::binary | std::ios::trunc);
    if (location.is_open()) {
        location << "Master Control Orchestration Server persistent diagnostics\r\n"
                 << "Root: " << pathToUtf8(paths.root) << "\r\n"
                 << "Component: " << pathToUtf8(paths.componentDirectory) << "\r\n"
                 << "Events: " << pathToUtf8(paths.eventsFile) << "\r\n"
                 << "Telemetry: " << pathToUtf8(paths.telemetryFile) << "\r\n";
    }

    return paths;
}

// v0.10.21 / v0.11.0: size-based rotation threshold for the persistent
// log files. runtime/telemetry.jsonl had grown to 25.7 MB / 101 K
// dashboard-snapshot entries at audit time with no upper bound. The
// rotation pattern: when a current file (e.g. events.jsonl) exceeds
// kLogRotationBytes, append ".1" to its full filename so the rotated
// sibling becomes events.jsonl.1 (and analogously telemetry.jsonl.1).
// Two-file rotation keeps the most recent kLogRotationBytes of live
// data + roughly the same of historical rotated data; total disk
// consumption per component caps at ~100 MB. The threshold is
// intentionally generous so an operator running for weeks doesn't
// lose recent forensics; tighten via the constant if the host is
// disk-constrained.
inline constexpr std::uintmax_t kLogRotationBytes = 50ull * 1024ull * 1024ull;

inline void rotateIfOversizedLocked(const std::filesystem::path& filePath) {
    std::error_code ec;
    if (!std::filesystem::exists(filePath, ec)) {
        return;
    }
    const auto fileSize = std::filesystem::file_size(filePath, ec);
    if (ec || fileSize < kLogRotationBytes) {
        return;
    }
    auto rotated = filePath;
    rotated += ".1";
    // v0.11.0 (Copilot review fix): preserve the prior rotated file
    // unless the new rotation succeeds. Pre-v0.11.0 the function
    // removed <name>.1 BEFORE renaming the live file; if the rename
    // then failed (another reader holds the live handle on Windows,
    // disk full, AV scan in flight, etc.) we'd have lost the previous
    // rotated history. Now: rename live -> <name>.tmp first; only
    // if that succeeds do we replace the existing .1 with .tmp. If
    // any step fails, the prior .1 stays intact and the live file
    // either keeps its size (if rename failed before the move) or
    // restarts (if rename moved the bytes out -- in which case the
    // new live file is empty, the .tmp holds the old data, and the
    // operator can recover by renaming .tmp -> .1 manually).
    auto tempRotated = filePath;
    tempRotated += ".rotating";
    // Clean any leftover .rotating from a previous interrupted cycle.
    std::filesystem::remove(tempRotated, ec);
    ec.clear();
    std::filesystem::rename(filePath, tempRotated, ec);
    if (ec) {
        // The live file is still in place at its oversized state and
        // the previous .1 is untouched. Caller will append to the
        // oversized live file -- better an oversized log than a
        // dropped event or a lost rotation history.
        return;
    }
    // The live file is now empty (will be re-created by std::ofstream
    // append in appendJsonLine). Promote .rotating to .1, replacing
    // the prior .1 only at this point.
    ec.clear();
    std::filesystem::remove(rotated, ec);
    ec.clear();
    std::filesystem::rename(tempRotated, rotated, ec);
    // If the second rename fails the rotated data still lives at
    // <name>.rotating on disk; the operator's tail tooling can
    // pick it up there. We do not attempt to undo the first
    // rename because the live file is already empty.
}

inline void appendJsonLine(const std::filesystem::path& filePath, const nlohmann::json& record) {
    static std::mutex writeMutex;
    std::lock_guard<std::mutex> lock(writeMutex);

    std::filesystem::create_directories(filePath.parent_path());

    // v0.10.21: check rotation BEFORE opening the append stream so the
    // rename target isn't held open by this process.
    rotateIfOversizedLocked(filePath);

    std::ofstream output(filePath, std::ios::binary | std::ios::app);
    if (!output.is_open()) {
        return;
    }

    output << record.dump() << '\n';
}

inline void appendEvent(std::wstring_view componentName,
                        std::string_view severity,
                        std::string_view eventName,
                        std::string_view message,
                        nlohmann::json data = nlohmann::json::object()) {
    const auto paths = resolvePersistentLogPaths(componentName);
    appendJsonLine(paths.eventsFile, nlohmann::json{
        { "capturedAtUtc", timestampNowUtc() },
        { "component", utf8FromWide(std::wstring(componentName)) },
        { "severity", std::string(severity) },
        { "event", std::string(eventName) },
        { "message", std::string(message) },
        { "data", std::move(data) }
    });
}

inline void appendTelemetry(std::wstring_view componentName,
                            std::string_view eventName,
                            nlohmann::json data = nlohmann::json::object()) {
    const auto paths = resolvePersistentLogPaths(componentName);
    appendJsonLine(paths.telemetryFile, nlohmann::json{
        { "capturedAtUtc", timestampNowUtc() },
        { "component", utf8FromWide(std::wstring(componentName)) },
        { "event", std::string(eventName) },
        { "data", std::move(data) }
    });
}

} // namespace MasterControl::Diagnostics
