// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#include "InstallerLogSupport.h"

#include <ShObjIdl.h>
#include <ShlObj.h>
#include <Windows.h>
#include <commctrl.h>
#include <shellapi.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <cstddef>
#include <cwctype>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>
#include <algorithm>

namespace {

constexpr wchar_t kBootstrapperName[] = L"MasterControlBootstrapper.exe";
constexpr wchar_t kShellBinaryName[] = L"MasterControlShell.exe";
constexpr wchar_t kLogDirectoryEnv[] = L"MASTERCONTROL_BOOTSTRAPPER_LOG_DIR";
constexpr wchar_t kDefaultInstallLeaf[] = L"Master Control Orchestration Server";
constexpr wchar_t kLegacyInstallLeaf[] = L"Master Control Program";
constexpr wchar_t kServiceName[] = L"MasterControlProgram";
constexpr wchar_t kUninstallRegistryKey[] = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\MasterControlProgram";

struct LauncherOptions final {
    std::filesystem::path installDirectory;
    bool installDirectoryExplicit = false;
    bool quiet = false;
    bool autoLaunchShell = false;
    bool noLaunchShell = false;
    std::vector<std::wstring> bootstrapperArguments;
};

enum class InstallDirectoryPromptResult {
    confirmed,
    canceled,
    failed
};

struct ProgressWindow final {
    HWND handle = nullptr;
    HWND statusLabel = nullptr;
    HWND progressBar = nullptr;
};

struct ShellLaunchResult final {
    bool promptShown = false;
    bool userAccepted = false;
    bool attempted = false;
    bool launched = false;
    DWORD shellExecuteCode = 0;
    std::wstring message = L"Shell launch was not requested.";
};

constexpr wchar_t kProgressWindowClassName[] = L"MasterControlOrchestrationServerSetupProgressWindow";

std::wstring formatWindowsMessage(const DWORD errorCode) {
    LPWSTR buffer = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD length = FormatMessageW(
        flags,
        nullptr,
        errorCode,
        0,
        reinterpret_cast<LPWSTR>(&buffer),
        0,
        nullptr);

    if (length == 0 || buffer == nullptr) {
        return L"Windows error " + std::to_wstring(errorCode);
    }

    std::wstring message(buffer, length);
    LocalFree(buffer);

    while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n' || message.back() == L' ')) {
        message.pop_back();
    }
    return message;
}

std::wstring wideFromUtf8(const std::string& input) {
    if (input.empty()) {
        return {};
    }

    const int required = MultiByteToWideChar(CP_UTF8, 0, input.c_str(), static_cast<int>(input.size()), nullptr, 0);
    if (required <= 0) {
        return {};
    }

    std::wstring output(static_cast<size_t>(required), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, input.c_str(), static_cast<int>(input.size()), output.data(), required);
    return output;
}

std::filesystem::path executablePath() {
    wchar_t buffer[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    return std::filesystem::path(std::wstring(buffer, length));
}

std::filesystem::path executableDirectory() {
    return executablePath().parent_path();
}

std::optional<std::filesystem::path> tryKnownFolder(REFKNOWNFOLDERID folderId) {
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

std::optional<std::wstring> readEnvironmentVariable(const wchar_t* name) {
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

std::optional<std::wstring> queryRegistryStringFromPath(const std::wstring& subKey, const wchar_t* valueName) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, subKey.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return std::nullopt;
    }

    DWORD type = 0;
    DWORD bytesNeeded = 0;
    if (RegQueryValueExW(key, valueName, nullptr, &type, nullptr, &bytesNeeded) != ERROR_SUCCESS ||
        type != REG_SZ ||
        bytesNeeded == 0) {
        RegCloseKey(key);
        return std::nullopt;
    }

    std::wstring value(bytesNeeded / sizeof(wchar_t), L'\0');
    const auto result = RegQueryValueExW(
        key,
        valueName,
        nullptr,
        &type,
        reinterpret_cast<LPBYTE>(value.data()),
        &bytesNeeded);
    RegCloseKey(key);
    if (result != ERROR_SUCCESS) {
        return std::nullopt;
    }

    if (!value.empty() && value.back() == L'\0') {
        value.pop_back();
    }
    return value;
}

std::wstring extractExecutablePath(std::wstring commandLine) {
    while (!commandLine.empty() && iswspace(commandLine.front())) {
        commandLine.erase(commandLine.begin());
    }

    if (commandLine.empty()) {
        return {};
    }

    if (commandLine.front() == L'"') {
        const auto closingQuote = commandLine.find(L'"', 1);
        if (closingQuote != std::wstring::npos) {
            return commandLine.substr(1, closingQuote - 1);
        }
    }

    const auto separator = commandLine.find(L' ');
    return separator == std::wstring::npos ? commandLine : commandLine.substr(0, separator);
}

bool directoryContainsInstallPayload(const std::filesystem::path& directory) {
    std::error_code error;
    if (!std::filesystem::exists(directory, error) || !std::filesystem::is_directory(directory, error)) {
        return false;
    }

    if (std::filesystem::exists(directory / kBootstrapperName, error) ||
        std::filesystem::exists(directory / "MasterControlServiceHost.exe", error) ||
        std::filesystem::exists(directory / kShellBinaryName, error) ||
        std::filesystem::exists(directory / "installation-state.json", error)) {
        return true;
    }

    for (const auto* shareLeaf : { L"MasterControlOrchestrationServer", L"MasterControlProgram" }) {
        if (std::filesystem::exists(directory / "share" / shareLeaf / "ForsettiManifests", error) ||
            std::filesystem::exists(directory / "share" / shareLeaf / "web", error)) {
            return true;
        }
    }

    return false;
}

std::optional<std::filesystem::path> normalizeInstalledDirectoryCandidate(const std::filesystem::path& candidate) {
    std::error_code error;
    const auto normalized = std::filesystem::weakly_canonical(candidate, error);
    const auto resolved = error ? candidate.lexically_normal() : normalized;
    if (!directoryContainsInstallPayload(resolved)) {
        return std::nullopt;
    }

    return resolved;
}

std::optional<std::filesystem::path> detectInstalledDirectoryFromUninstallRegistration() {
    const auto installLocation = queryRegistryStringFromPath(kUninstallRegistryKey, L"InstallLocation");
    if (!installLocation.has_value() || installLocation->empty()) {
        return std::nullopt;
    }

    return normalizeInstalledDirectoryCandidate(std::filesystem::path(*installLocation));
}

std::optional<std::filesystem::path> detectInstalledDirectoryFromServiceRegistration() {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (scm == nullptr) {
        return std::nullopt;
    }

    SC_HANDLE service = OpenServiceW(scm, kServiceName, SERVICE_QUERY_CONFIG);
    if (service == nullptr) {
        CloseServiceHandle(scm);
        return std::nullopt;
    }

    DWORD bytesNeeded = 0;
    QueryServiceConfigW(service, nullptr, 0, &bytesNeeded);
    if (bytesNeeded == 0) {
        CloseServiceHandle(service);
        CloseServiceHandle(scm);
        return std::nullopt;
    }

    std::vector<std::byte> buffer(bytesNeeded);
    auto* configuration = reinterpret_cast<QUERY_SERVICE_CONFIGW*>(buffer.data());
    const auto result = QueryServiceConfigW(service, configuration, bytesNeeded, &bytesNeeded);
    CloseServiceHandle(service);
    CloseServiceHandle(scm);
    if (result == 0 || configuration->lpBinaryPathName == nullptr) {
        return std::nullopt;
    }

    const auto executable = extractExecutablePath(configuration->lpBinaryPathName);
    if (executable.empty()) {
        return std::nullopt;
    }

    return normalizeInstalledDirectoryCandidate(std::filesystem::path(executable).parent_path());
}

std::optional<std::filesystem::path> detectLegacyInstalledDirectory() {
    std::vector<std::filesystem::path> candidates;
    if (const auto folder = tryKnownFolder(FOLDERID_ProgramFiles); folder.has_value()) {
        candidates.push_back(*folder / kLegacyInstallLeaf);
    }
    if (const auto env = readEnvironmentVariable(L"ProgramW6432"); env.has_value() && !env->empty()) {
        candidates.emplace_back(std::filesystem::path(*env) / kLegacyInstallLeaf);
    }
    if (const auto env = readEnvironmentVariable(L"ProgramFiles"); env.has_value() && !env->empty()) {
        candidates.emplace_back(std::filesystem::path(*env) / kLegacyInstallLeaf);
    }
    if (const auto env = readEnvironmentVariable(L"SystemDrive"); env.has_value() && !env->empty()) {
        candidates.emplace_back(std::filesystem::path(*env) / "Program Files" / kLegacyInstallLeaf);
    }

    for (const auto& candidate : candidates) {
        if (const auto normalized = normalizeInstalledDirectoryCandidate(candidate); normalized.has_value()) {
            return normalized;
        }
    }

    return std::nullopt;
}

std::filesystem::path defaultInstallDirectory() {
    if (const auto installedDirectory = detectInstalledDirectoryFromUninstallRegistration(); installedDirectory.has_value()) {
        return *installedDirectory;
    }

    if (const auto installedDirectory = detectInstalledDirectoryFromServiceRegistration(); installedDirectory.has_value()) {
        return *installedDirectory;
    }

    if (const auto legacyDirectory = detectLegacyInstalledDirectory(); legacyDirectory.has_value()) {
        return *legacyDirectory;
    }

    if (const auto programFilesPath = tryKnownFolder(FOLDERID_ProgramFiles); programFilesPath.has_value()) {
        return *programFilesPath / kDefaultInstallLeaf;
    }

    if (const auto programFilesPath = readEnvironmentVariable(L"ProgramW6432"); programFilesPath.has_value() &&
                                                                              !programFilesPath->empty()) {
        return std::filesystem::path(*programFilesPath) / kDefaultInstallLeaf;
    }

    if (const auto programFilesPath = readEnvironmentVariable(L"ProgramFiles"); programFilesPath.has_value() &&
                                                                             !programFilesPath->empty()) {
        return std::filesystem::path(*programFilesPath) / kDefaultInstallLeaf;
    }

    if (const auto systemDrive = readEnvironmentVariable(L"SystemDrive"); systemDrive.has_value() &&
                                                                          !systemDrive->empty()) {
        return std::filesystem::path(*systemDrive) / "Program Files" / kDefaultInstallLeaf;
    }

    return executableDirectory() / kDefaultInstallLeaf;
}

std::optional<std::filesystem::path> localUserDesktopDirectory() {
    const auto userProfile = readEnvironmentVariable(L"USERPROFILE");
    if (!userProfile.has_value() || userProfile->empty()) {
        return std::nullopt;
    }

    const auto candidate = std::filesystem::path(*userProfile) / L"Desktop";
    std::error_code error;
    if (std::filesystem::exists(candidate, error) && std::filesystem::is_directory(candidate, error)) {
        return candidate;
    }

    return std::nullopt;
}

std::vector<std::filesystem::path> desktopDirectories() {
    if (const auto overridePath = readEnvironmentVariable(kLogDirectoryEnv); overridePath.has_value() &&
                                                                          !overridePath->empty()) {
        return { std::filesystem::path(*overridePath) };
    }

    std::vector<std::filesystem::path> directories;
    auto appendUnique = [&directories](const std::filesystem::path& path) {
        if (std::find(directories.begin(), directories.end(), path) == directories.end()) {
            directories.push_back(path);
        }
    };

    if (const auto localDesktop = localUserDesktopDirectory(); localDesktop.has_value()) {
        appendUnique(*localDesktop);
    }

    if (directories.empty()) {
        directories.push_back(executableDirectory());
    }

    return directories;
}

std::filesystem::path desktopDirectory() {
    return desktopDirectories().front();
}

bool isAdministrator() {
    SID_IDENTIFIER_AUTHORITY authority = SECURITY_NT_AUTHORITY;
    PSID administratorsGroup = nullptr;
    BOOL isMember = FALSE;
    if (!AllocateAndInitializeSid(
            &authority,
            2,
            SECURITY_BUILTIN_DOMAIN_RID,
            DOMAIN_ALIAS_RID_ADMINS,
            0,
            0,
            0,
            0,
            0,
            0,
            &administratorsGroup)) {
        return false;
    }

    const BOOL checkResult = CheckTokenMembership(nullptr, administratorsGroup, &isMember);
    FreeSid(administratorsGroup);
    return checkResult != FALSE && isMember != FALSE;
}

bool pathRequiresElevation(const std::filesystem::path& path) {
    std::error_code error;
    const auto candidate = std::filesystem::weakly_canonical(path, error);
    const auto normalizedCandidate = error ? path : candidate;

    std::vector<std::filesystem::path> roots;
    if (const auto folder = tryKnownFolder(FOLDERID_ProgramFiles); folder.has_value()) {
        roots.push_back(*folder);
    }
    if (const auto folder = tryKnownFolder(FOLDERID_ProgramFilesX86); folder.has_value()) {
        roots.push_back(*folder);
    }
    if (const auto env = readEnvironmentVariable(L"ProgramW6432"); env.has_value() && !env->empty()) {
        roots.emplace_back(*env);
    }
    if (const auto env = readEnvironmentVariable(L"ProgramFiles"); env.has_value() && !env->empty()) {
        roots.emplace_back(*env);
    }
    if (const auto env = readEnvironmentVariable(L"ProgramFiles(x86)"); env.has_value() && !env->empty()) {
        roots.emplace_back(*env);
    }

    const auto candidateText = normalizedCandidate.wstring();
    for (const auto& root : roots) {
        const auto rootText = root.wstring();
        if (candidateText.size() < rootText.size()) {
            continue;
        }

        if (_wcsnicmp(candidateText.c_str(), rootText.c_str(), rootText.size()) == 0) {
            if (candidateText.size() == rootText.size() ||
                candidateText[rootText.size()] == L'\\' ||
                rootText.back() == L'\\') {
                return true;
            }
        }
    }

    return false;
}

std::wstring quoteArgument(const std::wstring& argument) {
    if (argument.empty()) {
        return L"\"\"";
    }

    if (argument.find_first_of(L" \t\"") == std::wstring::npos) {
        return argument;
    }

    std::wstring quoted;
    quoted.push_back(L'"');
    size_t backslashCount = 0;
    for (const wchar_t ch : argument) {
        if (ch == L'\\') {
            ++backslashCount;
            continue;
        }

        if (ch == L'"') {
            quoted.append(backslashCount * 2 + 1, L'\\');
            quoted.push_back(L'"');
            backslashCount = 0;
            continue;
        }

        if (backslashCount > 0) {
            quoted.append(backslashCount, L'\\');
            backslashCount = 0;
        }
        quoted.push_back(ch);
    }

    if (backslashCount > 0) {
        quoted.append(backslashCount * 2, L'\\');
    }

    quoted.push_back(L'"');
    return quoted;
}

std::wstring joinArguments(const std::vector<std::wstring>& arguments) {
    std::wstring result;
    bool first = true;
    for (const auto& argument : arguments) {
        if (!first) {
            result.push_back(L' ');
        }
        first = false;
        result += quoteArgument(argument);
    }
    return result;
}

std::wstring currentTimestamp() {
    SYSTEMTIME localTime{};
    GetLocalTime(&localTime);
    wchar_t buffer[64]{};
    swprintf_s(
        buffer,
        L"%04u%02u%02u-%02u%02u%02u-%03u",
        localTime.wYear,
        localTime.wMonth,
        localTime.wDay,
        localTime.wHour,
        localTime.wMinute,
        localTime.wSecond,
        localTime.wMilliseconds);
    return buffer;
}

std::filesystem::path launcherLogPath() {
    const auto timestamp = currentTimestamp();
    return desktopDirectory() / std::filesystem::path(L"MasterControlOrchestrationServer-setup-launcher-" + timestamp + L".txt");
}

std::wstring bootstrapperActionName(const LauncherOptions& options) {
    if (options.bootstrapperArguments.empty()) {
        return L"install";
    }

    return options.bootstrapperArguments.front();
}

std::vector<std::filesystem::path> collectBootstrapperActionLogs(const std::wstring& action) {
    std::vector<std::filesystem::path> logs;
    const std::wstring prefix = L"MasterControlOrchestrationServer-" + action + L"-";

    std::error_code error;
    for (const auto& directory : desktopDirectories()) {
        error.clear();
        if (!std::filesystem::exists(directory, error) || !std::filesystem::is_directory(directory, error)) {
            continue;
        }

        for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
            if (error || !entry.is_regular_file(error)) {
                continue;
            }

            const auto name = entry.path().filename().wstring();
            if (name.rfind(prefix, 0) == 0 && entry.path().extension() == L".txt") {
                logs.push_back(entry.path());
            }
        }
    }

    return logs;
}

bool bootstrapperActionProducesDesktopLog(const std::wstring& action) {
    return action == L"install" || action == L"repair" || action == L"upgrade" || action == L"uninstall";
}

std::optional<std::filesystem::path> findLatestBootstrapperActionLog(
    const std::wstring& action,
    const std::optional<std::filesystem::file_time_type>& notBefore = std::nullopt,
    const std::optional<std::wstring>& runId = std::nullopt) {
    auto newestMatchingPath = [&](const bool requireRunId) -> std::optional<std::filesystem::path> {
        std::optional<std::filesystem::path> newestPath;
        std::filesystem::file_time_type newestWriteTime{};
        bool found = false;
        std::error_code error;

        for (const auto& candidate : collectBootstrapperActionLogs(action)) {
            const auto writeTime = std::filesystem::last_write_time(candidate, error);
            if (error) {
                error.clear();
                continue;
            }

            if (notBefore.has_value() && writeTime < *notBefore) {
                continue;
            }

            if (requireRunId && runId.has_value() && !runId->empty()) {
                const auto contents = MasterControl::InstallerLogSupport::readTextFileUtf8(candidate);
                const auto expectedToken = std::string("RunId: ") +
                    MasterControl::InstallerLogSupport::utf8FromWide(*runId);
                if (!contents.has_value() || contents->find(expectedToken) == std::string::npos) {
                    continue;
                }
            }

            if (!found || writeTime > newestWriteTime) {
                newestWriteTime = writeTime;
                newestPath = candidate;
                found = true;
            }
        }

        return newestPath;
    };

    if (runId.has_value() && !runId->empty()) {
        if (const auto matchedByRunId = newestMatchingPath(true); matchedByRunId.has_value()) {
            return matchedByRunId;
        }
    }

    return newestMatchingPath(false);
}

std::optional<nlohmann::json> waitForBootstrapperRecord(
    const MasterControl::InstallerLogSupport::PersistentLogPaths& persistentPaths,
    const std::wstring& runId,
    const std::chrono::milliseconds timeout = std::chrono::seconds(90)) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    const auto expectedRunId = MasterControl::InstallerLogSupport::utf8FromWide(runId);

    while (std::chrono::steady_clock::now() <= deadline) {
        if (const auto contents = MasterControl::InstallerLogSupport::readTextFileUtf8(persistentPaths.sessionLatest);
            contents.has_value() && !contents->empty()) {
            const auto payload = nlohmann::json::parse(*contents, nullptr, false);
            if (!payload.is_discarded() &&
                payload.value("component", std::string{}) == "bootstrapper" &&
                payload.value("runId", std::string{}) == expectedRunId) {
                return std::optional<nlohmann::json>(payload);
            }
        }

        Sleep(200);
    }

    return std::nullopt;
}

std::vector<std::wstring> bootstrapperLaunchArguments(const LauncherOptions& options) {
    std::vector<std::wstring> arguments = options.bootstrapperArguments;
    if (std::find(arguments.begin(), arguments.end(), std::wstring(L"--run-id")) == arguments.end()) {
        arguments.emplace_back(L"--run-id");
        arguments.emplace_back(MasterControl::InstallerLogSupport::runId());
    }

    return arguments;
}

std::wstring launcherOutcome(const DWORD exitCode, const std::wstring& message) {
    if (message.find(L"canceled") != std::wstring::npos || message.find(L"cancelled") != std::wstring::npos) {
        return L"canceled";
    }

    return exitCode == 0 ? L"succeeded" : L"failed";
}

void persistSetupTrace(const MasterControl::InstallerLogSupport::PersistentLogPaths& persistentPaths,
                       const std::wstring& stage,
                       const nlohmann::json& payload = {}) {
    nlohmann::json record = payload;
    record["schema"] = "mastercontrol.installer-log.v1";
    record["component"] = "setup-launcher-trace";
    record["action"] = "install";
    record["outcome"] = "trace";
    record["succeeded"] = true;
    record["stage"] = MasterControl::InstallerLogSupport::utf8FromWide(stage);
    record["message"] = MasterControl::InstallerLogSupport::utf8FromWide(stage);
    record["runId"] = MasterControl::InstallerLogSupport::utf8FromWide(MasterControl::InstallerLogSupport::runId());
    MasterControl::InstallerLogSupport::appendJsonLine(persistentPaths.sessionHistory, record);
}

bool writeLauncherLog(const std::filesystem::path& logPath,
                      const LauncherOptions& options,
                      const bool administrator,
                      const bool requiresElevation,
                      const bool elevationAttempted,
                      const DWORD bootstrapperExitCode,
                      const int intendedProcessExitCode,
                      const std::wstring& message,
                      const ShellLaunchResult& shellLaunch,
                      const std::optional<std::filesystem::path>& bootstrapperLogPath = std::nullopt) {
    const auto runId = MasterControl::InstallerLogSupport::runId();
    const auto persistentPaths =
        MasterControl::InstallerLogSupport::persistentLogPaths(executableDirectory());
    std::error_code error;
    std::filesystem::create_directories(logPath.parent_path(), error);

    bool wroteTextLog = false;
    std::wostringstream contents;
    {
        contents << L"Master Control Orchestration Server Setup Launcher\r\n\r\n";
        contents << L"GeneratedAt: " << currentTimestamp().c_str() << L"\r\n";
        contents << L"RunId: " << runId << L"\r\n";
        contents << L"BootstrapperPath: " << (executableDirectory() / kBootstrapperName).wstring() << L"\r\n";
        contents << L"InstallDirectory: " << options.installDirectory.wstring() << L"\r\n";
        contents << L"PersistentLogRoot: " << persistentPaths.root.wstring() << L"\r\n";
        contents << L"PersistentHistoryPath: " << persistentPaths.history.wstring() << L"\r\n";
        contents << L"PersistentFailurePath: " << persistentPaths.failures.wstring() << L"\r\n";
        contents << L"LatestPersistentRecordPath: " << persistentPaths.latest.wstring() << L"\r\n";
        contents << L"LatestPersistentFailurePath: " << persistentPaths.latestFailure.wstring() << L"\r\n";
        contents << L"LatestSessionPath: " << persistentPaths.latestSession.wstring() << L"\r\n";
        contents << L"SessionDirectory: " << persistentPaths.sessionDirectory.wstring() << L"\r\n";
        contents << L"SessionHistoryPath: " << persistentPaths.sessionHistory.wstring() << L"\r\n";
        contents << L"SessionLauncherLogPath: " << persistentPaths.launcherSessionLog.wstring() << L"\r\n";
        contents << L"SessionBootstrapperLogPath: " << persistentPaths.bootstrapperSessionLog.wstring() << L"\r\n";
        contents << L"ShellLatestLogPath: " << persistentPaths.shellLatest.wstring() << L"\r\n";
        contents << L"ServiceLatestLogPath: " << persistentPaths.serviceLatest.wstring() << L"\r\n";
        contents << L"IsAdministrator: " << (administrator ? L"true" : L"false") << L"\r\n";
        contents << L"RequiresElevation: " << (requiresElevation ? L"true" : L"false") << L"\r\n";
        contents << L"ElevationAttempted: " << (elevationAttempted ? L"true" : L"false") << L"\r\n";
        contents << L"Quiet: " << (options.quiet ? L"true" : L"false") << L"\r\n";
        contents << L"LaunchedShell: " << (shellLaunch.launched ? L"true" : L"false") << L"\r\n";
        contents << L"ShellLaunchPromptShown: " << (shellLaunch.promptShown ? L"true" : L"false") << L"\r\n";
        contents << L"ShellLaunchAccepted: " << (shellLaunch.userAccepted ? L"true" : L"false") << L"\r\n";
        contents << L"ShellLaunchAttempted: " << (shellLaunch.attempted ? L"true" : L"false") << L"\r\n";
        contents << L"ShellLaunchResultCode: " << shellLaunch.shellExecuteCode << L"\r\n";
        contents << L"BootstrapperExitCode: " << bootstrapperExitCode << L"\r\n";
        contents << L"IntendedProcessExitCode: " << intendedProcessExitCode << L"\r\n";
        contents << L"Arguments: " << joinArguments(options.bootstrapperArguments) << L"\r\n";
        if (bootstrapperLogPath.has_value()) {
            contents << L"BootstrapperLogPath: " << bootstrapperLogPath->wstring() << L"\r\n";
        }
        contents << L"ShellLaunchMessage: " << shellLaunch.message << L"\r\n";
        contents << L"\r\nMessage\r\n-------\r\n";
        contents << message << L"\r\n";
    }

    {
        std::wofstream output(logPath, std::ios::binary);
        if (output.is_open()) {
            output << contents.str();
            wroteTextLog = output.good();
        }
    }

    const bool wroteSessionTextLog =
        MasterControl::InstallerLogSupport::writeWideTextFile(persistentPaths.launcherSessionLog, contents.str());

        MasterControl::InstallerLogSupport::persistRecord(
        persistentPaths,
        nlohmann::json{
            { "schema", "mastercontrol.installer-log.v1" },
            { "component", "setup-launcher" },
            { "action", MasterControl::InstallerLogSupport::utf8FromWide(bootstrapperActionName(options)) },
            { "outcome", MasterControl::InstallerLogSupport::utf8FromWide(launcherOutcome(static_cast<DWORD>(intendedProcessExitCode), message)) },
            { "succeeded", intendedProcessExitCode == 0 },
            { "message", MasterControl::InstallerLogSupport::utf8FromWide(message) },
            { "runId", MasterControl::InstallerLogSupport::utf8FromWide(runId) },
            { "generatedAtLocal", MasterControl::InstallerLogSupport::localTimestampForDisplay() },
            { "processId", GetCurrentProcessId() },
            { "bootstrapperPath", MasterControl::InstallerLogSupport::pathToUtf8(executableDirectory() / kBootstrapperName) },
            { "installDirectory", MasterControl::InstallerLogSupport::pathToUtf8(options.installDirectory) },
            { "administrator", administrator },
            { "requiresElevation", requiresElevation },
            { "elevationAttempted", elevationAttempted },
            { "quiet", options.quiet },
            { "launchedShell", shellLaunch.launched },
            { "exitCode", bootstrapperExitCode },
            { "intendedProcessExitCode", intendedProcessExitCode },
            { "shellLaunch",
              {
                  { "promptShown", shellLaunch.promptShown },
                  { "userAccepted", shellLaunch.userAccepted },
                  { "attempted", shellLaunch.attempted },
                  { "launched", shellLaunch.launched },
                  { "resultCode", shellLaunch.shellExecuteCode },
                  { "message", MasterControl::InstallerLogSupport::utf8FromWide(shellLaunch.message) }
              } },
            { "arguments", MasterControl::InstallerLogSupport::utf8FromWide(joinArguments(options.bootstrapperArguments)) },
            { "textLogWritten", wroteTextLog },
            { "sessionTextLogWritten", wroteSessionTextLog },
            { "logPaths",
              {
                  { "text", MasterControl::InstallerLogSupport::pathToUtf8(logPath) },
                  { "sessionText", MasterControl::InstallerLogSupport::pathToUtf8(persistentPaths.launcherSessionLog) },
                  { "bootstrapperText", bootstrapperLogPath.has_value()
                      ? MasterControl::InstallerLogSupport::pathToUtf8(*bootstrapperLogPath)
                      : std::string{} }
              } },
            { "persistentPaths", MasterControl::InstallerLogSupport::persistentPathsToJson(persistentPaths) }
        });
    return wroteTextLog;
}

std::wstring usageText() {
    return
        L"Usage:\n"
        L"  MasterControlOrchestrationServerSetup.exe [install-directory] [options]\n\n"
        L"Options:\n"
        L"  --install-directory <path>        Override the install directory.\n"
        L"  --skip-service                    Skip Windows service registration.\n"
        L"  --skip-firewall                   Skip firewall rule management.\n"
        L"  --skip-shortcuts                  Skip shortcut creation.\n"
        L"  --skip-uninstall-registration     Skip Programs and Features registration.\n"
        L"  --quiet                           Suppress completion dialogs.\n"
        L"  --launch-shell                    Launch the desktop shell after install.\n"
        L"  --no-launch-shell                 Do not prompt to launch the shell.\n";
}

std::filesystem::path existingDirectoryForPicker(const std::filesystem::path& candidate) {
    std::error_code error;
    auto current = candidate;
    while (!current.empty()) {
        if (std::filesystem::exists(current, error) && std::filesystem::is_directory(current, error)) {
            return current;
        }

        const auto parent = current.parent_path();
        if (parent == current) {
            break;
        }
        current = parent;
    }

    if (const auto folder = tryKnownFolder(FOLDERID_ProgramFiles); folder.has_value()) {
        return *folder;
    }

    return executableDirectory();
}

bool leafMatchesDefaultInstallName(const std::filesystem::path& path) {
    const auto leaf = path.filename().wstring();
    return !_wcsicmp(leaf.c_str(), kDefaultInstallLeaf);
}

InstallDirectoryPromptResult browseForInstallDirectory(std::filesystem::path& installDirectory, std::wstring& errorMessage) {
    IFileOpenDialog* dialog = nullptr;
    const HRESULT createResult = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (FAILED(createResult) || dialog == nullptr) {
        errorMessage = L"Failed to open the folder picker.\n\n" + formatWindowsMessage(HRESULT_CODE(createResult));
        return InstallDirectoryPromptResult::failed;
    }

    auto releaseDialog = [&dialog]() {
        if (dialog != nullptr) {
            dialog->Release();
            dialog = nullptr;
        }
    };

    DWORD dialogOptions = 0;
    dialog->GetOptions(&dialogOptions);
    dialog->SetOptions(dialogOptions | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dialog->SetTitle(L"Choose Install Location");
    dialog->SetOkButtonLabel(L"Install Here");

    const auto initialDirectory = existingDirectoryForPicker(installDirectory.parent_path());
    IShellItem* initialFolder = nullptr;
    if (SUCCEEDED(SHCreateItemFromParsingName(initialDirectory.c_str(), nullptr, IID_PPV_ARGS(&initialFolder))) &&
        initialFolder != nullptr) {
        dialog->SetFolder(initialFolder);
        initialFolder->Release();
    }

    const HRESULT showResult = dialog->Show(nullptr);
    if (showResult == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
        releaseDialog();
        return InstallDirectoryPromptResult::canceled;
    }

    if (FAILED(showResult)) {
        errorMessage = L"Failed to show the folder picker.\n\n" + formatWindowsMessage(HRESULT_CODE(showResult));
        releaseDialog();
        return InstallDirectoryPromptResult::failed;
    }

    IShellItem* resultItem = nullptr;
    const HRESULT resultItemStatus = dialog->GetResult(&resultItem);
    if (FAILED(resultItemStatus) || resultItem == nullptr) {
        errorMessage = L"Failed to read the selected install location.\n\n" + formatWindowsMessage(HRESULT_CODE(resultItemStatus));
        releaseDialog();
        return InstallDirectoryPromptResult::failed;
    }

    PWSTR selectedFolder = nullptr;
    const HRESULT selectedFolderStatus = resultItem->GetDisplayName(SIGDN_FILESYSPATH, &selectedFolder);
    resultItem->Release();
    releaseDialog();

    if (FAILED(selectedFolderStatus) || selectedFolder == nullptr) {
        errorMessage = L"Failed to resolve the selected install location.\n\n" + formatWindowsMessage(HRESULT_CODE(selectedFolderStatus));
        return InstallDirectoryPromptResult::failed;
    }

    const std::filesystem::path selectedPath(selectedFolder);
    CoTaskMemFree(selectedFolder);

    installDirectory = leafMatchesDefaultInstallName(selectedPath)
        ? selectedPath
        : selectedPath / kDefaultInstallLeaf;
    return InstallDirectoryPromptResult::confirmed;
}

InstallDirectoryPromptResult promptForInstallDirectory(LauncherOptions& options, std::wstring& errorMessage) {
    while (true) {
        const std::wstring prompt =
            L"Install Master Control Orchestration Server to:\n\n" + options.installDirectory.wstring() +
            L"\n\nYes = Install here\nNo = Choose a different folder\nCancel = Exit setup";
        const int choice = MessageBoxW(
            nullptr,
            prompt.c_str(),
            L"Master Control Orchestration Server Setup",
            MB_ICONQUESTION | MB_YESNOCANCEL | MB_SETFOREGROUND);

        if (choice == IDYES) {
            return InstallDirectoryPromptResult::confirmed;
        }

        if (choice == IDCANCEL) {
            return InstallDirectoryPromptResult::canceled;
        }

        const auto browseResult = browseForInstallDirectory(options.installDirectory, errorMessage);
        if (browseResult == InstallDirectoryPromptResult::failed) {
            return browseResult;
        }
    }
}

LRESULT CALLBACK progressWindowProc(HWND windowHandle, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CLOSE:
        return 0;
    default:
        return DefWindowProcW(windowHandle, message, wParam, lParam);
    }
}

void centerWindowOnScreen(HWND windowHandle) {
    RECT windowRect{};
    if (!GetWindowRect(windowHandle, &windowRect)) {
        return;
    }

    RECT workArea{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
    const int windowWidth = windowRect.right - windowRect.left;
    const int windowHeight = windowRect.bottom - windowRect.top;
    const int horizontalInset = (((workArea.right - workArea.left) - windowWidth) / 2);
    const int verticalInset = (((workArea.bottom - workArea.top) - windowHeight) / 2);
    const int x = static_cast<int>(workArea.left) + (horizontalInset > 0 ? horizontalInset : 0);
    const int y = static_cast<int>(workArea.top) + (verticalInset > 0 ? verticalInset : 0);
    SetWindowPos(windowHandle, nullptr, x, y, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
}

ProgressWindow createProgressWindow(const LauncherOptions& options) {
    INITCOMMONCONTROLSEX controls{};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_PROGRESS_CLASS;
    InitCommonControlsEx(&controls);

    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = progressWindowProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.hCursor = LoadCursor(nullptr, IDC_WAIT);
    windowClass.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = kProgressWindowClassName;
    RegisterClassW(&windowClass);

    ProgressWindow progressWindow{};
    progressWindow.handle = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        kProgressWindowClassName,
        L"Master Control Orchestration Server Setup",
        WS_CAPTION | WS_POPUP | WS_SYSMENU,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        520,
        190,
        nullptr,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);

    if (progressWindow.handle == nullptr) {
        return progressWindow;
    }

    const HFONT guiFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    const auto header = CreateWindowW(
        L"STATIC",
        L"Installing Master Control Orchestration Server",
        WS_CHILD | WS_VISIBLE,
        20,
        20,
        460,
        24,
        progressWindow.handle,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);

    const std::wstring detailText =
        L"Installing to:\n" + options.installDirectory.wstring() + L"\n\nPlease wait while setup completes.";
    progressWindow.statusLabel = CreateWindowW(
        L"STATIC",
        detailText.c_str(),
        WS_CHILD | WS_VISIBLE,
        20,
        52,
        460,
        60,
        progressWindow.handle,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);

    progressWindow.progressBar = CreateWindowExW(
        0,
        PROGRESS_CLASSW,
        nullptr,
        WS_CHILD | WS_VISIBLE | PBS_MARQUEE,
        20,
        124,
        460,
        24,
        progressWindow.handle,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);

    SendMessageW(header, WM_SETFONT, reinterpret_cast<WPARAM>(guiFont), TRUE);
    SendMessageW(progressWindow.statusLabel, WM_SETFONT, reinterpret_cast<WPARAM>(guiFont), TRUE);
    SendMessageW(progressWindow.progressBar, PBM_SETMARQUEE, TRUE, 50);

    centerWindowOnScreen(progressWindow.handle);
    ShowWindow(progressWindow.handle, SW_SHOWNORMAL);
    UpdateWindow(progressWindow.handle);
    return progressWindow;
}

void destroyProgressWindow(ProgressWindow& progressWindow) {
    if (progressWindow.handle != nullptr) {
        DestroyWindow(progressWindow.handle);
        progressWindow.handle = nullptr;
        progressWindow.statusLabel = nullptr;
        progressWindow.progressBar = nullptr;
    }
}

std::optional<LauncherOptions> parseArguments(int argc, wchar_t* argv[], std::wstring& errorMessage) {
    LauncherOptions options;
    options.installDirectory = defaultInstallDirectory();
    options.bootstrapperArguments = {L"install"};
    bool installDirectorySet = false;

    for (int index = 1; index < argc; ++index) {
        const std::wstring_view argument(argv[index]);
        if (argument == L"--install-directory") {
            if (index + 1 >= argc) {
                errorMessage = L"--install-directory requires a path.";
                return std::nullopt;
            }
            options.installDirectory = std::filesystem::path(argv[++index]);
            installDirectorySet = true;
            options.installDirectoryExplicit = true;
            continue;
        }

        if (argument == L"--quiet") {
            options.quiet = true;
            continue;
        }

        if (argument == L"--launch-shell") {
            options.autoLaunchShell = true;
            continue;
        }

        if (argument == L"--no-launch-shell") {
            options.noLaunchShell = true;
            continue;
        }

        if (argument == L"--skip-service" ||
            argument == L"--skip-firewall" ||
            argument == L"--skip-shortcuts" ||
            argument == L"--skip-uninstall-registration") {
            options.bootstrapperArguments.emplace_back(argument);
            continue;
        }

        if (!argument.empty() && argument[0] == L'-') {
            errorMessage = L"Unknown option: " + std::wstring(argument);
            return std::nullopt;
        }

        if (installDirectorySet) {
            errorMessage = L"Only one install directory may be provided.";
            return std::nullopt;
        }

        options.installDirectory = std::filesystem::path(argument);
        installDirectorySet = true;
        options.installDirectoryExplicit = true;
    }

    options.bootstrapperArguments.emplace_back(options.installDirectory.wstring());
    return options;
}

bool launchProcess(const std::filesystem::path& executable,
                   const std::vector<std::wstring>& arguments,
                   const bool elevate,
                   const LauncherOptions* options,
                   DWORD& exitCode,
                   std::wstring& errorMessage) {
    const auto argumentLine = joinArguments(arguments);
    HANDLE processHandle = nullptr;

    if (elevate) {
        SHELLEXECUTEINFOW executeInfo{};
        executeInfo.cbSize = sizeof(executeInfo);
        executeInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
        executeInfo.lpVerb = L"runas";
        executeInfo.lpFile = executable.c_str();
        executeInfo.lpParameters = argumentLine.c_str();
        executeInfo.lpDirectory = executable.parent_path().c_str();
        executeInfo.nShow = SW_HIDE;

        if (!ShellExecuteExW(&executeInfo)) {
            const DWORD error = GetLastError();
            exitCode = error;
            errorMessage = formatWindowsMessage(error);
            return false;
        }

        processHandle = executeInfo.hProcess;
    } else {
        STARTUPINFOW startupInfo{};
        startupInfo.cb = sizeof(startupInfo);
        startupInfo.dwFlags = STARTF_USESHOWWINDOW;
        startupInfo.wShowWindow = SW_HIDE;

        PROCESS_INFORMATION processInformation{};
        std::wstring commandLine = quoteArgument(executable.wstring());
        if (!argumentLine.empty()) {
            commandLine.push_back(L' ');
            commandLine += argumentLine;
        }

        if (!CreateProcessW(
                executable.c_str(),
                commandLine.data(),
                nullptr,
                nullptr,
                FALSE,
                CREATE_UNICODE_ENVIRONMENT,
                nullptr,
                executable.parent_path().c_str(),
                &startupInfo,
                &processInformation)) {
            const DWORD error = GetLastError();
            exitCode = error;
            errorMessage = formatWindowsMessage(error);
            return false;
        }

        CloseHandle(processInformation.hThread);
        processHandle = processInformation.hProcess;
    }

    ProgressWindow progressWindow{};
    if (options != nullptr && !options->quiet) {
        progressWindow = createProgressWindow(*options);
    }

    auto cleanupProgressWindow = [&progressWindow]() {
        destroyProgressWindow(progressWindow);
    };

    for (;;) {
        const DWORD waitTimeout = progressWindow.handle != nullptr ? 100 : INFINITE;
        const DWORD waitResult = WaitForSingleObject(processHandle, waitTimeout);
        if (waitResult == WAIT_OBJECT_0) {
            break;
        }

        if (waitResult == WAIT_TIMEOUT && progressWindow.handle != nullptr) {
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            continue;
        }

        const DWORD error = (waitResult == WAIT_FAILED) ? GetLastError() : ERROR_GEN_FAILURE;
        exitCode = error;
        errorMessage = formatWindowsMessage(error);
        cleanupProgressWindow();
        CloseHandle(processHandle);
        return false;
    }

    if (!GetExitCodeProcess(processHandle, &exitCode)) {
        const DWORD error = GetLastError();
        errorMessage = formatWindowsMessage(error);
        cleanupProgressWindow();
        CloseHandle(processHandle);
        return false;
    }

    cleanupProgressWindow();
    CloseHandle(processHandle);
    return true;
}

void showMessage(const UINT flags, const std::wstring& title, const std::wstring& text) {
    MessageBoxW(nullptr, text.c_str(), title.c_str(), flags | MB_SETFOREGROUND);
}

int finalizeSetupProcess(const int exitCode, const bool shouldUninitializeCom) {
    if (shouldUninitializeCom) {
        CoUninitialize();
    }
    return exitCode;
}

ShellLaunchResult maybeLaunchShell(const LauncherOptions& options) {
    ShellLaunchResult result;
    if (options.noLaunchShell) {
        result.message = L"Shell launch disabled by setup option.";
        return result;
    }

    bool launchShell = options.autoLaunchShell;
    if (!options.quiet && !launchShell) {
        result.promptShown = true;
        const int promptChoice = MessageBoxW(
            nullptr,
            L"Master Control Orchestration Server installed successfully.\n\nLaunch the desktop shell now?",
            L"Master Control Orchestration Server Setup",
            MB_ICONQUESTION | MB_YESNO | MB_SETFOREGROUND);
        result.userAccepted = (promptChoice == IDYES);
        launchShell = result.userAccepted;
    }

    if (!launchShell) {
        result.message = options.autoLaunchShell
            ? L"Shell launch was requested but not started."
            : L"User declined shell launch after install.";
        return result;
    }

    const auto shellPath = options.installDirectory / kShellBinaryName;
    result.attempted = true;
    const HINSTANCE shellExecuteResult = ShellExecuteW(
        nullptr,
        L"open",
        shellPath.c_str(),
        nullptr,
        options.installDirectory.c_str(),
        SW_SHOWNORMAL);
    result.shellExecuteCode = static_cast<DWORD>(reinterpret_cast<INT_PTR>(shellExecuteResult));
    result.launched = reinterpret_cast<INT_PTR>(shellExecuteResult) > 32;
    result.message = result.launched
        ? L"Desktop shell launched successfully."
        : L"Desktop shell launch failed with ShellExecute code " + std::to_wstring(result.shellExecuteCode) + L".";
    return result;
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    const HRESULT comInitialization = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool shouldUninitializeCom = SUCCEEDED(comInitialization);

    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == nullptr) {
        showMessage(MB_ICONERROR | MB_OK, L"Master Control Orchestration Server Setup", L"Failed to read launcher arguments.");
        return finalizeSetupProcess(1, shouldUninitializeCom);
    }

    MasterControl::InstallerLogSupport::initializeRunId();
    const auto persistentPaths =
        MasterControl::InstallerLogSupport::persistentLogPaths(executableDirectory());
    const std::filesystem::path logPath = launcherLogPath();

    std::wstring parseError;
    auto options = parseArguments(argc, argv, parseError);
    LocalFree(argv);

    if (!options.has_value()) {
        writeLauncherLog(logPath, LauncherOptions{}, isAdministrator(), false, false, 2, 2, parseError + L"\n\n" + usageText(), ShellLaunchResult{});
        showMessage(MB_ICONERROR | MB_OK, L"Master Control Orchestration Server Setup", parseError + L"\n\n" + usageText());
        return finalizeSetupProcess(2, shouldUninitializeCom);
    }

    if (!options->quiet && !options->installDirectoryExplicit) {
        std::wstring installPromptError;
        const auto promptResult = promptForInstallDirectory(*options, installPromptError);
        if (promptResult == InstallDirectoryPromptResult::failed) {
            const std::wstring message = installPromptError + L"\n\nSetup will now exit.";
            writeLauncherLog(logPath, *options, isAdministrator(), false, false, 4, 4, installPromptError, ShellLaunchResult{});
            showMessage(MB_ICONERROR | MB_OK, L"Master Control Orchestration Server Setup", message);
            return finalizeSetupProcess(4, shouldUninitializeCom);
        }

        if (promptResult == InstallDirectoryPromptResult::canceled) {
            writeLauncherLog(logPath, *options, isAdministrator(), false, false, 0, 0, L"Setup canceled before install started.", ShellLaunchResult{});
            return finalizeSetupProcess(0, shouldUninitializeCom);
        }

        options->bootstrapperArguments.back() = options->installDirectory.wstring();
    }

    const auto bootstrapperPath = executableDirectory() / kBootstrapperName;
    if (!std::filesystem::exists(bootstrapperPath)) {
        const std::wstring message = L"Required installer engine was not found:\n" + bootstrapperPath.wstring();
        writeLauncherLog(logPath, *options, isAdministrator(), false, false, 3, 3, message, ShellLaunchResult{});
        showMessage(MB_ICONERROR | MB_OK, L"Master Control Orchestration Server Setup", message);
        return finalizeSetupProcess(3, shouldUninitializeCom);
    }

    const bool administrator = isAdministrator();
    const bool requiresElevation = pathRequiresElevation(options->installDirectory) ||
        std::find(options->bootstrapperArguments.begin(), options->bootstrapperArguments.end(), L"--skip-service") == options->bootstrapperArguments.end() ||
        std::find(options->bootstrapperArguments.begin(), options->bootstrapperArguments.end(), L"--skip-firewall") == options->bootstrapperArguments.end() ||
        std::find(options->bootstrapperArguments.begin(), options->bootstrapperArguments.end(), L"--skip-uninstall-registration") == options->bootstrapperArguments.end();
    const bool elevationAttempted = requiresElevation && !administrator;

    DWORD exitCode = 1;
    std::wstring launchMessage;
    const auto runId = MasterControl::InstallerLogSupport::runId();
    const auto launchArguments = bootstrapperLaunchArguments(*options);
    const auto bootstrapperLogSearchStart = std::filesystem::file_time_type::clock::now() - std::chrono::seconds(2);
    persistSetupTrace(
        persistentPaths,
        L"before-launchProcess",
        nlohmann::json{
            { "bootstrapperPath", MasterControl::InstallerLogSupport::pathToUtf8(bootstrapperPath) },
            { "arguments", MasterControl::InstallerLogSupport::utf8FromWide(joinArguments(launchArguments)) },
            { "requiresElevation", requiresElevation },
            { "elevationAttempted", elevationAttempted }
        });
    if (!launchProcess(bootstrapperPath, launchArguments, elevationAttempted, &(*options), exitCode, launchMessage)) {
        const std::wstring message =
            L"Failed to start the installer engine.\n\n" + launchMessage +
            L"\n\nA launcher log was written to:\n" + logPath.wstring() +
            L"\n\nPersistent installer failure log:\n" + persistentPaths.latestFailure.wstring();
        writeLauncherLog(logPath, *options, administrator, requiresElevation, elevationAttempted, exitCode, static_cast<int>(exitCode == 0 ? 1 : exitCode), launchMessage, ShellLaunchResult{});
        showMessage(MB_ICONERROR | MB_OK, L"Master Control Orchestration Server Setup", message);
        return finalizeSetupProcess(static_cast<int>(exitCode == 0 ? 1 : exitCode), shouldUninitializeCom);
    }

    persistSetupTrace(
        persistentPaths,
        L"after-launchProcess",
        nlohmann::json{
            { "bootstrapperExitCode", exitCode },
            { "launchMessage", MasterControl::InstallerLogSupport::utf8FromWide(launchMessage) }
        });

    const auto actionName = bootstrapperActionName(*options);
    persistSetupTrace(persistentPaths, L"before-waitForBootstrapperRecord");
    const auto bootstrapperRecord = waitForBootstrapperRecord(persistentPaths, runId);
    persistSetupTrace(
        persistentPaths,
        L"after-waitForBootstrapperRecord",
        nlohmann::json{
            { "observedBootstrapperRecord", bootstrapperRecord.has_value() }
        });
    std::optional<std::filesystem::path> bootstrapperLogPath;
    if (std::filesystem::exists(persistentPaths.bootstrapperSessionLog)) {
        bootstrapperLogPath = persistentPaths.bootstrapperSessionLog;
    } else {
        bootstrapperLogPath = findLatestBootstrapperActionLog(actionName, bootstrapperLogSearchStart, runId);
    }

    if (exitCode == 0 && !bootstrapperRecord.has_value()) {
        exitCode = 1;
        launchMessage =
            L"Installer engine did not publish a completed bootstrapper session record for this run. Review the persistent installer logs and retry the install.";
    }

    if (bootstrapperRecord.has_value()) {
        const bool bootstrapperSucceeded = bootstrapperRecord->value("succeeded", exitCode == 0);
        if (!bootstrapperSucceeded) {
            exitCode = static_cast<DWORD>(bootstrapperRecord->value("exitCode", static_cast<int>(exitCode == 0 ? 1 : exitCode)));
            const auto recordMessage = wideFromUtf8(bootstrapperRecord->value("message", std::string{}));
            if (!recordMessage.empty()) {
                launchMessage = recordMessage;
            }
        }
    }

    if (exitCode == 0 && !directoryContainsInstallPayload(options->installDirectory)) {
        exitCode = 1;
        launchMessage =
            L"Bootstrapper reported success, but no installed payload was found at:\n" + options->installDirectory.wstring();
    }

    const ShellLaunchResult shellLaunch = (exitCode == 0) ? maybeLaunchShell(*options) : ShellLaunchResult{};
    const int intendedProcessExitCode = static_cast<int>(exitCode);
    const std::wstring outcomeMessage =
        exitCode == 0
            ? L"Bootstrapper completed successfully."
            : (!launchMessage.empty()
                   ? launchMessage
                   : L"Bootstrapper exited with code " + std::to_wstring(exitCode) + L". Check the desktop logs for details.");
    writeLauncherLog(
        logPath,
        *options,
        administrator,
        requiresElevation,
        elevationAttempted,
        exitCode,
        intendedProcessExitCode,
        outcomeMessage,
        shellLaunch,
        bootstrapperLogPath);

    if (exitCode != 0) {
        std::wstring message =
            L"Master Control Orchestration Server installation failed.\n\nReview the installer logs for details.\n\nLauncher log:\n" +
            logPath.wstring() +
            L"\n\nPersistent installer failure log:\n" + persistentPaths.latestFailure.wstring();
        if (bootstrapperLogPath.has_value()) {
            message += L"\n\nBootstrapper log:\n" + bootstrapperLogPath->wstring();
        }
        showMessage(MB_ICONERROR | MB_OK, L"Master Control Orchestration Server Setup", message);
        return finalizeSetupProcess(static_cast<int>(exitCode), shouldUninitializeCom);
    }

    if (!options->quiet && !shellLaunch.launched) {
        std::wstring successMessage =
            L"Master Control Orchestration Server installed successfully.\n\nA launcher log was written to:\n" + logPath.wstring();
        if (bootstrapperLogPath.has_value()) {
            successMessage += L"\n\nBootstrapper log:\n" + bootstrapperLogPath->wstring();
        }
        showMessage(
            MB_ICONINFORMATION | MB_OK,
            L"Master Control Orchestration Server Setup",
            successMessage);
    }

    return finalizeSetupProcess(0, shouldUninitializeCom);
}
