#pragma once

#include <nlohmann/json.hpp>

#include <ShlObj.h>
#include <Windows.h>

#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>

namespace MasterControl::InstallerLogSupport {

constexpr wchar_t kPersistentLogDirectoryEnv[] = L"MASTERCONTROL_BOOTSTRAPPER_PERSISTENT_LOG_DIR";
constexpr wchar_t kInstallerRunIdEnv[] = L"MASTERCONTROL_INSTALLER_RUN_ID";

struct PersistentLogPaths final {
    std::filesystem::path root;
    std::filesystem::path history;
    std::filesystem::path failures;
    std::filesystem::path latest;
    std::filesystem::path latestFailure;
};

inline std::optional<std::wstring> readEnvironmentVariable(const wchar_t* name) {
    const DWORD required = GetEnvironmentVariableW(name, nullptr, 0);
    if (required == 0) {
        return std::nullopt;
    }

    std::wstring value(required, L'\0');
    const DWORD length = GetEnvironmentVariableW(name, value.data(), required);
    if (length == 0) {
        return std::nullopt;
    }

    value.resize(length);
    return value;
}

inline std::optional<std::filesystem::path> tryKnownFolder(REFKNOWNFOLDERID folderId) {
    PWSTR folderPath = nullptr;
    const HRESULT result = SHGetKnownFolderPath(folderId, KF_FLAG_DEFAULT, nullptr, &folderPath);
    if (FAILED(result) || folderPath == nullptr) {
        if (folderPath != nullptr) {
            CoTaskMemFree(folderPath);
        }
        return std::nullopt;
    }

    std::filesystem::path output(folderPath);
    CoTaskMemFree(folderPath);
    return output;
}

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
    if (required <= 0) {
        return {};
    }

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

inline std::string pathToUtf8(const std::filesystem::path& path) {
    return utf8FromWide(path.wstring());
}

inline std::optional<std::string> readTextFileUtf8(const std::filesystem::path& filePath) {
    std::ifstream input(filePath, std::ios::binary);
    if (!input.is_open()) {
        return std::nullopt;
    }

    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}

inline std::string localTimestampForDisplay() {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    char buffer[64]{};
    sprintf_s(
        buffer,
        "%04u-%02u-%02u %02u:%02u:%02u.%03u",
        now.wYear,
        now.wMonth,
        now.wDay,
        now.wHour,
        now.wMinute,
        now.wSecond,
        now.wMilliseconds);
    return buffer;
}

inline std::string utcTimestampForJson() {
    SYSTEMTIME now{};
    GetSystemTime(&now);
    char buffer[64]{};
    sprintf_s(
        buffer,
        "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
        now.wYear,
        now.wMonth,
        now.wDay,
        now.wHour,
        now.wMinute,
        now.wSecond,
        now.wMilliseconds);
    return buffer;
}

inline std::wstring generateRunId() {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t buffer[80]{};
    swprintf_s(
        buffer,
        L"%04u%02u%02u-%02u%02u%02u-%03u-%lu",
        now.wYear,
        now.wMonth,
        now.wDay,
        now.wHour,
        now.wMinute,
        now.wSecond,
        now.wMilliseconds,
        GetCurrentProcessId());
    return buffer;
}

inline std::wstring initializeRunId(const std::optional<std::wstring>& explicitRunId = std::nullopt) {
    static bool initialized = false;
    static std::wstring cachedRunId;

    if (!initialized) {
        if (explicitRunId.has_value() && !explicitRunId->empty()) {
            cachedRunId = *explicitRunId;
        } else if (const auto existingRunId = readEnvironmentVariable(kInstallerRunIdEnv);
                   existingRunId.has_value() && !existingRunId->empty()) {
            cachedRunId = *existingRunId;
        } else {
            cachedRunId = generateRunId();
        }

        SetEnvironmentVariableW(kInstallerRunIdEnv, cachedRunId.c_str());
        initialized = true;
    }

    return cachedRunId;
}

inline std::wstring runId() {
    return initializeRunId();
}

inline std::optional<std::wstring> findArgumentValue(int argc,
                                                     wchar_t* argv[],
                                                     const std::wstring_view argumentName) {
    for (int index = 1; index + 1 < argc; ++index) {
        if (std::wstring_view(argv[index]) == argumentName) {
            return std::wstring(argv[index + 1]);
        }
    }

    return std::nullopt;
}

inline PersistentLogPaths persistentLogPaths(const std::filesystem::path& executableDirectory) {
    std::filesystem::path root;

    if (const auto overrideDirectory = readEnvironmentVariable(kPersistentLogDirectoryEnv);
        overrideDirectory.has_value() && !overrideDirectory->empty()) {
        root = std::filesystem::path(*overrideDirectory);
    } else if (const auto localAppData = tryKnownFolder(FOLDERID_LocalAppData); localAppData.has_value()) {
        root = *localAppData / "Master Control Orchestration Server" / "logs" / "installer";
    } else if (const auto localAppDataEnv = readEnvironmentVariable(L"LOCALAPPDATA");
               localAppDataEnv.has_value() && !localAppDataEnv->empty()) {
        root = std::filesystem::path(*localAppDataEnv) / "Master Control Orchestration Server" / "logs" / "installer";
    } else {
        root = executableDirectory / "logs" / "installer";
    }

    return PersistentLogPaths{
        root,
        root / "installer-history.jsonl",
        root / "installer-failures.jsonl",
        root / "installer-latest.json",
        root / "installer-latest-failure.json"
    };
}

inline nlohmann::json persistentPathsToJson(const PersistentLogPaths& paths) {
    return nlohmann::json{
        { "root", pathToUtf8(paths.root) },
        { "history", pathToUtf8(paths.history) },
        { "failures", pathToUtf8(paths.failures) },
        { "latest", pathToUtf8(paths.latest) },
        { "latestFailure", pathToUtf8(paths.latestFailure) }
    };
}

inline bool appendJsonLine(const std::filesystem::path& filePath, const nlohmann::json& payload) {
    std::error_code error;
    std::filesystem::create_directories(filePath.parent_path(), error);

    std::ofstream output(filePath, std::ios::binary | std::ios::app);
    if (!output.is_open()) {
        return false;
    }

    output << payload.dump() << '\n';
    return output.good();
}

inline bool writeJsonFile(const std::filesystem::path& filePath, const nlohmann::json& payload) {
    std::error_code error;
    std::filesystem::create_directories(filePath.parent_path(), error);

    std::ofstream output(filePath, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        return false;
    }

    output << payload.dump(2) << '\n';
    return output.good();
}

inline bool persistRecord(const PersistentLogPaths& paths, const nlohmann::json& payload) {
    nlohmann::json record = payload;
    if (!record.contains("generatedAtUtc")) {
        record["generatedAtUtc"] = utcTimestampForJson();
    }

    if (!record.contains("runId")) {
        record["runId"] = utf8FromWide(runId());
    }

    if (!record.contains("persistentPaths")) {
        record["persistentPaths"] = persistentPathsToJson(paths);
    }

    bool wroteAnything = false;
    wroteAnything |= appendJsonLine(paths.history, record);
    wroteAnything |= writeJsonFile(paths.latest, record);

    if (!record.value("succeeded", false)) {
        wroteAnything |= appendJsonLine(paths.failures, record);
        wroteAnything |= writeJsonFile(paths.latestFailure, record);
    }

    return wroteAnything;
}

} // namespace MasterControl::InstallerLogSupport
