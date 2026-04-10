#pragma once

#include <ShlObj.h>
#include <Windows.h>

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>

namespace MasterControl::DeploymentLogPaths {

constexpr wchar_t kPersistentLogDirectoryEnv[] = L"MASTERCONTROL_BOOTSTRAPPER_PERSISTENT_LOG_DIR";
constexpr wchar_t kInstallerRunIdEnv[] = L"MASTERCONTROL_INSTALLER_RUN_ID";

struct Paths final {
    std::filesystem::path root;
    std::filesystem::path components;
    std::filesystem::path sessions;
    std::filesystem::path sessionDirectory;
    std::filesystem::path shellLatest;
    std::filesystem::path serviceLatest;
    std::filesystem::path shellSessionLog;
    std::filesystem::path serviceSessionLog;
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

inline std::wstring activeOrGeneratedRunId() {
    if (const auto runId = readEnvironmentVariable(kInstallerRunIdEnv); runId.has_value() && !runId->empty()) {
        return *runId;
    }

    return generateRunId();
}

inline bool hasActiveInstallerRun() {
    const auto runId = readEnvironmentVariable(kInstallerRunIdEnv);
    return runId.has_value() && !runId->empty();
}

inline Paths build(const std::filesystem::path& executableDirectory) {
    std::filesystem::path root;

    if (const auto overrideDirectory = readEnvironmentVariable(kPersistentLogDirectoryEnv);
        overrideDirectory.has_value() && !overrideDirectory->empty()) {
        root = std::filesystem::path(*overrideDirectory);
    } else if (const auto publicDocuments = tryKnownFolder(FOLDERID_PublicDocuments); publicDocuments.has_value()) {
        root = *publicDocuments / "Master Control Orchestration Server" / "logs" / "installer";
    } else if (const auto publicPath = readEnvironmentVariable(L"PUBLIC");
               publicPath.has_value() && !publicPath->empty()) {
        root = std::filesystem::path(*publicPath) / "Documents" / "Master Control Orchestration Server" / "logs" / "installer";
    } else if (const auto localAppDataEnv = readEnvironmentVariable(L"LOCALAPPDATA");
               localAppDataEnv.has_value() && !localAppDataEnv->empty()) {
        root = std::filesystem::path(*localAppDataEnv) / "Master Control Orchestration Server" / "logs" / "installer";
    } else {
        root = executableDirectory / "logs" / "installer";
    }

    const auto components = root / "components";
    const auto sessions = root / "sessions";
    const auto sessionDirectory = sessions / activeOrGeneratedRunId();
    return Paths{
        root,
        components,
        sessions,
        sessionDirectory,
        components / "shell-latest.log",
        components / "service-latest.log",
        sessionDirectory / "shell-startup.log",
        sessionDirectory / "service-host.log"
    };
}

inline bool appendWideTextFile(const std::filesystem::path& filePath, const std::wstring& payload) {
    std::error_code error;
    std::filesystem::create_directories(filePath.parent_path(), error);

    std::wofstream output(filePath, std::ios::binary | std::ios::app);
    if (!output.is_open()) {
        return false;
    }

    output << payload;
    return output.good();
}

inline bool appendComponentLog(const Paths&,
                               const std::filesystem::path& latestLogPath,
                               const std::filesystem::path& sessionLogPath,
                               const std::wstring& payload) {
    bool wroteAnything = appendWideTextFile(latestLogPath, payload);
    if (hasActiveInstallerRun()) {
        wroteAnything |= appendWideTextFile(sessionLogPath, payload);
    }
    return wroteAnything;
}

} // namespace MasterControl::DeploymentLogPaths
