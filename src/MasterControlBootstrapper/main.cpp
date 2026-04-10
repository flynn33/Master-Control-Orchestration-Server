// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#include "MasterControl/MasterControlDefaults.h"
#include "InstallerLogSupport.h"

#include <nlohmann/json.hpp>

#include <ShlObj.h>
#include <Windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <array>
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cwctype>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

// Keep the legacy SCM and uninstall identities stable so upgrades from older installs continue to work.
constexpr wchar_t kProductDisplayName[] = L"Master Control Orchestration Server";
constexpr wchar_t kServiceName[] = L"MasterControlProgram";
constexpr wchar_t kUninstallRegistryKey[] = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\MasterControlProgram";
constexpr wchar_t kProgramsFolderName[] = L"Master Control Orchestration Server";
constexpr wchar_t kShellShortcutName[] = L"Master Control Orchestration Server.lnk";
constexpr wchar_t kDashboardShortcutName[] = L"Master Control Orchestration Server Dashboard.url";
constexpr wchar_t kInstallStateFileName[] = L"installation-state.json";
constexpr wchar_t kBrowserRuleName[] = L"Master Control Orchestration Server - Browser Access";
constexpr wchar_t kBeaconRuleName[] = L"Master Control Orchestration Server - Beacon Discovery";
constexpr wchar_t kBootstrapperLogDirectoryEnv[] = L"MASTERCONTROL_BOOTSTRAPPER_LOG_DIR";
constexpr wchar_t kBootstrapperServiceNameEnv[] = L"MASTERCONTROL_BOOTSTRAPPER_SERVICE_NAME";
constexpr wchar_t kBootstrapperUninstallRegistryKeyEnv[] = L"MASTERCONTROL_BOOTSTRAPPER_UNINSTALL_KEY";
constexpr wchar_t kLegacyInstallLeaf[] = L"Master Control Program";

#ifndef MASTERCONTROL_BOOTSTRAPPER_VERSION
#define MASTERCONTROL_BOOTSTRAPPER_VERSION "0.1.0"
#endif

struct IntegrationOptions final {
    bool manageService = true;
    bool manageFirewall = true;
    bool manageShortcuts = true;
    bool manageUninstallRegistration = true;
    bool purgeInstallDirectory = false;
    bool purgeData = false;
    bool jsonOutput = false;
};

struct InstallationState final {
    std::string version;
    std::string installDirectory;
    std::string serviceBinary;
    std::string shellBinary;
    std::string bootstrapperBinary;
    std::string shortcutDirectory;
    std::string shellShortcutPath;
    std::string dashboardShortcutPath;
    std::string browserUrl;
    std::string configPath;
    std::string dataDirectory;
    uint16_t browserPort = 0;
    uint16_t beaconPort = 0;
    bool allowOpenLanAccess = false;
    bool beaconEnabled = false;
    bool serviceManaged = true;
    bool firewallManaged = true;
    bool shortcutsManaged = true;
    bool uninstallRegistrationManaged = true;
};

struct ServiceInstallationStatus final {
    bool registered = false;
    bool autoStart = false;
    bool delayedAutoStart = false;
    bool recoveryConfigured = false;
    bool failureActionsOnNonCrash = false;
    bool sidUnrestricted = false;
    bool running = false;
    std::string state = "not_installed";
    DWORD processId = 0;
    DWORD win32ExitCode = 0;
    DWORD serviceSpecificExitCode = 0;
    DWORD checkpoint = 0;
    DWORD waitHintMilliseconds = 0;
    std::string binaryPath;
};

struct UninstallRegistrationStatus final {
    bool registered = false;
    std::string displayVersion;
    std::string installLocation;
    std::string uninstallCommand;
};

struct ShortcutInstallationStatus final {
    bool shellShortcutPresent = false;
    bool dashboardShortcutPresent = false;
};

struct FirewallRuleStatus final {
    bool browserRulePresent = false;
    bool beaconRulePresent = false;
};

struct ProcessCaptureResult final {
    bool launched = false;
    int exitCode = 1;
    std::string output;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    InstallationState,
    version,
    installDirectory,
    serviceBinary,
    shellBinary,
    bootstrapperBinary,
    shortcutDirectory,
    shellShortcutPath,
    dashboardShortcutPath,
    browserUrl,
    configPath,
    dataDirectory,
    browserPort,
    beaconPort,
    allowOpenLanAccess,
    beaconEnabled,
    serviceManaged,
    firewallManaged,
    shortcutsManaged,
    uninstallRegistrationManaged)

struct PayloadLayout final {
    bool flatPayload = false;
    std::filesystem::path flatRoot;
    std::filesystem::path bootstrapperDirectory;
    std::filesystem::path serviceDirectory;
    std::filesystem::path shellDirectory;
    std::filesystem::path manifestsDirectory;
    std::filesystem::path webDirectory;
};

constexpr wchar_t kCurrentShareLeaf[] = L"MasterControlOrchestrationServer";
constexpr wchar_t kLegacyShareLeaf[] = L"MasterControlProgram";

std::wstring wideFromUtf8(const std::string& input) {
    if (input.empty()) {
        return {};
    }

    const int required = MultiByteToWideChar(
        CP_UTF8,
        0,
        input.c_str(),
        static_cast<int>(input.size()),
        nullptr,
        0);

    std::wstring output(static_cast<size_t>(required), L'\0');
    MultiByteToWideChar(
        CP_UTF8,
        0,
        input.c_str(),
        static_cast<int>(input.size()),
        output.data(),
        required);
    return output;
}

std::string utf8FromWide(const std::wstring& input) {
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

std::filesystem::path executablePath() {
    wchar_t buffer[MAX_PATH]{};
    const auto length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    return std::filesystem::path(std::wstring(buffer, length));
}

std::filesystem::path executableDirectory() {
    return executablePath().parent_path();
}

std::optional<std::wstring> readEnvironmentVariable(const wchar_t* name);

std::wstring configuredServiceName();
std::wstring configuredUninstallRegistryKey();

std::optional<std::wstring> queryRegistryStringFromPath(HKEY root,
                                                        const std::wstring& subKey,
                                                        const wchar_t* valueName);
std::wstring extractExecutablePath(std::wstring commandLine);
bool directoryContainsInstallPayload(const std::filesystem::path& directory);
std::optional<std::filesystem::path> normalizeInstalledDirectoryCandidate(const std::filesystem::path& candidate);
std::optional<std::filesystem::path> detectInstalledDirectoryFromUninstallRegistration();
std::optional<std::filesystem::path> detectInstalledDirectoryFromServiceRegistration();
std::optional<std::filesystem::path> detectLegacyInstalledDirectory();

std::optional<std::filesystem::path> tryKnownFolder(REFKNOWNFOLDERID folderId) {
    PWSTR path = nullptr;
    const HRESULT result = SHGetKnownFolderPath(folderId, KF_FLAG_DEFAULT, nullptr, &path);
    if (FAILED(result) || path == nullptr) {
        if (path != nullptr) {
            CoTaskMemFree(path);
        }
        return std::nullopt;
    }

    std::filesystem::path output(path);
    CoTaskMemFree(path);
    return output;
}

std::optional<std::wstring> queryRegistryStringFromPath(HKEY root,
                                                        const std::wstring& subKey,
                                                        const wchar_t* valueName) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subKey.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS) {
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
    const auto trimLeading = [](std::wstring& value) {
        while (!value.empty() && iswspace(value.front())) {
            value.erase(value.begin());
        }
    };

    trimLeading(commandLine);
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

    if (std::filesystem::exists(directory / "MasterControlBootstrapper.exe", error) ||
        std::filesystem::exists(directory / "MasterControlServiceHost.exe", error) ||
        std::filesystem::exists(directory / "MasterControlShell.exe", error) ||
        std::filesystem::exists(directory / kInstallStateFileName, error)) {
        return true;
    }

    for (const auto* shareLeaf : { kCurrentShareLeaf, kLegacyShareLeaf }) {
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
    const auto installLocation = queryRegistryStringFromPath(
        HKEY_LOCAL_MACHINE,
        configuredUninstallRegistryKey(),
        L"InstallLocation");
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

    const auto serviceName = configuredServiceName();
    SC_HANDLE service = OpenServiceW(scm, serviceName.c_str(), SERVICE_QUERY_CONFIG);
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
        return *programFilesPath / "Master Control Orchestration Server";
    }

    if (const auto programFilesPath = readEnvironmentVariable(L"ProgramW6432"); programFilesPath.has_value() &&
                                                                              !programFilesPath->empty()) {
        return std::filesystem::path(*programFilesPath) / "Master Control Orchestration Server";
    }

    if (const auto programFilesPath = readEnvironmentVariable(L"ProgramFiles"); programFilesPath.has_value() &&
                                                                             !programFilesPath->empty()) {
        return std::filesystem::path(*programFilesPath) / "Master Control Orchestration Server";
    }

    if (const auto systemDrive = readEnvironmentVariable(L"SystemDrive"); systemDrive.has_value() &&
                                                                          !systemDrive->empty()) {
        return std::filesystem::path(*systemDrive) / "Program Files" / "Master Control Orchestration Server";
    }

    return executableDirectory() / "Master Control Orchestration Server";
}

std::filesystem::path knownFolder(REFKNOWNFOLDERID folderId) {
    if (const auto path = tryKnownFolder(folderId); path.has_value()) {
        return *path;
    }

    throw std::runtime_error("Failed to resolve a known folder path.");
}

std::filesystem::path preferredShortcutDirectory() {
    const auto folderId = IsUserAnAdmin() != FALSE ? FOLDERID_CommonPrograms : FOLDERID_Programs;
    return knownFolder(folderId) / kProgramsFolderName;
}

std::wstring escapePowerShellLiteral(std::wstring value) {
    size_t position = 0;
    while ((position = value.find(L'\'', position)) != std::wstring::npos) {
        value.insert(position, L"'");
        position += 2;
    }
    return value;
}

bool samePath(const std::filesystem::path& left, const std::filesystem::path& right) {
    std::error_code error;
    if (!std::filesystem::exists(left, error) || !std::filesystem::exists(right, error)) {
        return false;
    }

    return std::filesystem::equivalent(left, right, error);
}

bool pathIsWithin(const std::filesystem::path& candidate, const std::filesystem::path& root) {
    std::error_code error;
    const auto normalizedCandidate = std::filesystem::weakly_canonical(candidate, error);
    if (error) {
        return false;
    }

    const auto normalizedRoot = std::filesystem::weakly_canonical(root, error);
    if (error) {
        return false;
    }

    auto candidateText = normalizedCandidate.wstring();
    auto rootText = normalizedRoot.wstring();
    if (!rootText.empty() && rootText.back() != L'\\') {
        rootText.push_back(L'\\');
    }

    return candidateText == normalizedRoot.wstring() || candidateText.rfind(rootText, 0) == 0;
}

bool directoryHasEntries(const std::filesystem::path& directory) {
    std::error_code error;
    if (!std::filesystem::exists(directory, error) || !std::filesystem::is_directory(directory, error)) {
        return false;
    }

    return std::filesystem::directory_iterator(directory, error) != std::filesystem::directory_iterator();
}

bool isTransientCopyError(const std::error_code& error) {
    if (!error) {
        return false;
    }

    switch (static_cast<DWORD>(error.value())) {
    case ERROR_SHARING_VIOLATION:
    case ERROR_LOCK_VIOLATION:
    case ERROR_ACCESS_DENIED:
    case ERROR_USER_MAPPED_FILE:
        return true;
    default:
        return false;
    }
}

void copyFileWithRetry(const std::filesystem::path& source, const std::filesystem::path& destination) {
    constexpr auto kRetryDelayMilliseconds = 250UL;
    constexpr auto kMaxAttempts = 24;

    for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
        std::error_code error;
        const bool copied = std::filesystem::copy_file(
            source,
            destination,
            std::filesystem::copy_options::overwrite_existing,
            error);

        if (copied || !error) {
            return;
        }

        if (!isTransientCopyError(error) || attempt + 1 >= kMaxAttempts) {
            throw std::filesystem::filesystem_error("copy_file", source, destination, error);
        }

        Sleep(kRetryDelayMilliseconds);
    }
}

void copyRecursive(const std::filesystem::path& source, const std::filesystem::path& destination) {
    if (samePath(source, destination)) {
        return;
    }

    std::filesystem::create_directories(destination);
    for (const auto& entry : std::filesystem::recursive_directory_iterator(source)) {
        const auto relativePath = std::filesystem::relative(entry.path(), source);
        const auto targetPath = destination / relativePath;
        if (entry.is_directory()) {
            std::filesystem::create_directories(targetPath);
        } else if (entry.is_regular_file()) {
            std::filesystem::create_directories(targetPath.parent_path());
            copyFileWithRetry(entry.path(), targetPath);
        }
    }
}

void removeDirectoryIfExists(const std::filesystem::path& directory) {
    std::error_code error;
    std::filesystem::remove_all(directory, error);
}

std::filesystem::path rollbackSnapshotDirectory(const std::filesystem::path& installDirectory) {
    const auto snapshotName =
        L"." + installDirectory.filename().wstring() +
        L".rollback-" + std::to_wstring(GetCurrentProcessId()) +
        L"-" + std::to_wstring(static_cast<std::uint64_t>(GetTickCount64()));
    return installDirectory.parent_path() / snapshotName;
}

bool captureRollbackSnapshot(const std::filesystem::path& installDirectory,
                             const std::filesystem::path& snapshotDirectory) {
    removeDirectoryIfExists(snapshotDirectory);
    if (!directoryHasEntries(installDirectory)) {
        return false;
    }

    copyRecursive(installDirectory, snapshotDirectory);
    return directoryHasEntries(snapshotDirectory);
}

bool restoreRollbackSnapshot(const std::filesystem::path& installDirectory,
                             const std::filesystem::path& snapshotDirectory) {
    if (!directoryHasEntries(snapshotDirectory)) {
        return false;
    }

    removeDirectoryIfExists(installDirectory);
    std::filesystem::create_directories(installDirectory.parent_path());
    copyRecursive(snapshotDirectory, installDirectory);
    return directoryHasEntries(installDirectory);
}

int runProcess(std::wstring commandLine,
               const std::filesystem::path& workingDirectory = {},
               const DWORD creationFlags = CREATE_NO_WINDOW) {
    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);

    PROCESS_INFORMATION processInformation{};
    if (CreateProcessW(
            nullptr,
            commandLine.data(),
            nullptr,
            nullptr,
            FALSE,
            creationFlags,
            nullptr,
            workingDirectory.empty() ? nullptr : workingDirectory.wstring().c_str(),
            &startupInfo,
            &processInformation) == 0) {
        return static_cast<int>(GetLastError());
    }

    WaitForSingleObject(processInformation.hProcess, INFINITE);

    DWORD exitCode = 1;
    GetExitCodeProcess(processInformation.hProcess, &exitCode);
    CloseHandle(processInformation.hThread);
    CloseHandle(processInformation.hProcess);
    return static_cast<int>(exitCode);
}

ProcessCaptureResult runProcessCapture(std::wstring commandLine,
                                       const std::filesystem::path& workingDirectory = {},
                                       const DWORD creationFlags = CREATE_NO_WINDOW) {
    ProcessCaptureResult result;

    SECURITY_ATTRIBUTES securityAttributes{};
    securityAttributes.nLength = sizeof(securityAttributes);
    securityAttributes.bInheritHandle = TRUE;

    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &securityAttributes, 0)) {
        return result;
    }
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.dwFlags = STARTF_USESTDHANDLES;
    startupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startupInfo.hStdOutput = writePipe;
    startupInfo.hStdError = writePipe;

    PROCESS_INFORMATION processInformation{};
    if (CreateProcessW(
            nullptr,
            commandLine.data(),
            nullptr,
            nullptr,
            TRUE,
            creationFlags,
            nullptr,
            workingDirectory.empty() ? nullptr : workingDirectory.wstring().c_str(),
            &startupInfo,
            &processInformation) == 0) {
        CloseHandle(readPipe);
        CloseHandle(writePipe);
        return result;
    }

    result.launched = true;
    CloseHandle(processInformation.hThread);
    CloseHandle(writePipe);

    WaitForSingleObject(processInformation.hProcess, INFINITE);

    std::array<char, 4096> buffer{};
    DWORD bytesRead = 0;
    while (ReadFile(readPipe, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr) != 0 &&
           bytesRead > 0) {
        result.output.append(buffer.data(), static_cast<size_t>(bytesRead));
    }

    DWORD exitCode = 1;
    GetExitCodeProcess(processInformation.hProcess, &exitCode);
    result.exitCode = static_cast<int>(exitCode);

    CloseHandle(processInformation.hProcess);
    CloseHandle(readPipe);
    return result;
}

std::optional<std::wstring> readEnvironmentVariable(const wchar_t* name) {
    const auto requiredCharacters = GetEnvironmentVariableW(name, nullptr, 0);
    if (requiredCharacters == 0) {
        return std::nullopt;
    }

    std::wstring value(static_cast<size_t>(requiredCharacters), L'\0');
    GetEnvironmentVariableW(name, value.data(), requiredCharacters);
    if (!value.empty() && value.back() == L'\0') {
        value.pop_back();
    }

    if (value.empty()) {
        return std::nullopt;
    }

    return value;
}

std::wstring configuredServiceName() {
    if (const auto overrideName = readEnvironmentVariable(kBootstrapperServiceNameEnv); overrideName.has_value()) {
        return *overrideName;
    }
    return kServiceName;
}

std::wstring configuredUninstallRegistryKey() {
    if (const auto overrideKey = readEnvironmentVariable(kBootstrapperUninstallRegistryKeyEnv); overrideKey.has_value()) {
        return *overrideKey;
    }
    return kUninstallRegistryKey;
}

bool environmentFlagEnabled(const wchar_t* name) {
    auto value = readEnvironmentVariable(name);
    if (!value.has_value()) {
        return false;
    }

    std::transform(
        value->begin(),
        value->end(),
        value->begin(),
        [](const wchar_t character) { return static_cast<wchar_t>(std::towlower(character)); });
    return *value != L"0" &&
        *value != L"false" &&
        *value != L"off" &&
        *value != L"no";
}

bool writeTextFile(const std::filesystem::path& filePath, const std::string& contents) {
    std::filesystem::create_directories(filePath.parent_path());
    std::ofstream output(filePath, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        return false;
    }

    output << contents;
    return output.good();
}

std::string localTimestampForDisplay() {
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

std::string localTimestampForFileName() {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    char buffer[64]{};
    sprintf_s(
        buffer,
        "%04u%02u%02u-%02u%02u%02u-%03u-%lu",
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

std::optional<std::filesystem::path> localUserDesktopDirectory() {
    if (const auto userProfile = readEnvironmentVariable(L"USERPROFILE"); userProfile.has_value() && !userProfile->empty()) {
        const auto candidate = std::filesystem::path(*userProfile) / L"Desktop";
        std::error_code error;
        if (std::filesystem::exists(candidate, error) && std::filesystem::is_directory(candidate, error)) {
            return candidate;
        }
    }

    return std::nullopt;
}

std::filesystem::path bootstrapperLogDirectory() {
    if (const auto overrideDirectory = readEnvironmentVariable(kBootstrapperLogDirectoryEnv); overrideDirectory.has_value()) {
        return std::filesystem::path(*overrideDirectory);
    }

    if (const auto localDesktop = localUserDesktopDirectory(); localDesktop.has_value()) {
        return *localDesktop;
    }

    return executableDirectory();
}

std::string bootstrapperLogMessage(const bool succeeded, const nlohmann::json& payload) {
    if (payload.contains("message") && payload.at("message").is_string()) {
        return payload.at("message").get<std::string>();
    }

    if (payload.contains("error") && payload.at("error").is_string()) {
        return payload.at("error").get<std::string>();
    }

    if (payload.contains("issues") && payload.at("issues").is_array() && !payload.at("issues").empty()) {
        const auto& firstIssue = payload.at("issues").front();
        if (firstIssue.is_string()) {
            return firstIssue.get<std::string>();
        }
    }

    return succeeded ? "Bootstrapper action completed successfully." : "Bootstrapper action failed.";
}

void writeBootstrapperActionLog(const std::wstring& mode,
                                const bool succeeded,
                                const std::filesystem::path& installDirectory,
                                const IntegrationOptions& options,
                                const nlohmann::json& payload) {
    try {
        std::wstring action = mode;
        std::transform(
            action.begin(),
            action.end(),
            action.begin(),
            [](const wchar_t character) {
                if (character == L' ' || character == L'\\' || character == L'/' || character == L':') {
                    return L'-';
                }
                return static_cast<wchar_t>(std::towlower(character));
            });

        const auto logDirectory = bootstrapperLogDirectory();
        const auto logPath = logDirectory /
            (L"MasterControlOrchestrationServer-" + action + L"-" + wideFromUtf8(succeeded ? "succeeded" : "failed") +
             L"-" + wideFromUtf8(localTimestampForFileName()) + L".txt");
        const auto runId = MasterControl::InstallerLogSupport::runId();
        const auto persistentPaths =
            MasterControl::InstallerLogSupport::persistentLogPaths(executableDirectory());

        std::ostringstream output;
        output << "Master Control Orchestration Server Bootstrapper Log\r\n\r\n";
        output << "GeneratedAt: " << localTimestampForDisplay() << "\r\n";
        output << "RunId: " << MasterControl::InstallerLogSupport::utf8FromWide(runId) << "\r\n";
        output << "Action: " << utf8FromWide(mode) << "\r\n";
        output << "Succeeded: " << (succeeded ? "true" : "false") << "\r\n";
        output << "InstallDirectory: " << installDirectory.string() << "\r\n";
        output << "BootstrapperPath: " << utf8FromWide(executablePath().wstring()) << "\r\n";
        output << "PersistentLogRoot: " << MasterControl::InstallerLogSupport::pathToUtf8(persistentPaths.root) << "\r\n";
        output << "PersistentHistoryPath: " << MasterControl::InstallerLogSupport::pathToUtf8(persistentPaths.history)
               << "\r\n";
        output << "PersistentFailurePath: " << MasterControl::InstallerLogSupport::pathToUtf8(persistentPaths.failures)
               << "\r\n";
        output << "LatestPersistentRecordPath: " << MasterControl::InstallerLogSupport::pathToUtf8(persistentPaths.latest)
               << "\r\n";
        output << "LatestPersistentFailurePath: "
               << MasterControl::InstallerLogSupport::pathToUtf8(persistentPaths.latestFailure) << "\r\n";
        output << "ManageService: " << (options.manageService ? "true" : "false") << "\r\n";
        output << "ManageFirewall: " << (options.manageFirewall ? "true" : "false") << "\r\n";
        output << "ManageShortcuts: " << (options.manageShortcuts ? "true" : "false") << "\r\n";
        output << "ManageUninstallRegistration: " << (options.manageUninstallRegistration ? "true" : "false") << "\r\n";
        output << "PurgeInstallDirectory: " << (options.purgeInstallDirectory ? "true" : "false") << "\r\n";
        output << "PurgeData: " << (options.purgeData ? "true" : "false") << "\r\n";
        output << "\r\nDetails\r\n-------\r\n";
        output << payload.dump(2) << "\r\n";

        const auto textLogContents = output.str();
        const bool wroteTextLog = writeTextFile(logPath, textLogContents);
        const bool wroteSessionTextLog =
            MasterControl::InstallerLogSupport::appendTextFile(persistentPaths.bootstrapperSessionLog, textLogContents);
        const auto message = bootstrapperLogMessage(succeeded, payload);
        MasterControl::InstallerLogSupport::persistRecord(
            persistentPaths,
            nlohmann::json{
                { "schema", "mastercontrol.installer-log.v1" },
                { "component", "bootstrapper" },
                { "action", utf8FromWide(mode) },
                { "outcome", succeeded ? "succeeded" : "failed" },
                { "succeeded", succeeded },
                { "message", message },
                { "runId", MasterControl::InstallerLogSupport::utf8FromWide(runId) },
                { "generatedAtLocal", localTimestampForDisplay() },
                { "processId", GetCurrentProcessId() },
                { "bootstrapperVersion", MASTERCONTROL_BOOTSTRAPPER_VERSION },
                { "installDirectory", installDirectory.string() },
                { "bootstrapperPath", utf8FromWide(executablePath().wstring()) },
                { "textLogWritten", wroteTextLog },
                { "sessionTextLogWritten", wroteSessionTextLog },
                { "logPaths",
                  {
                      { "text", MasterControl::InstallerLogSupport::pathToUtf8(logPath) },
                      { "reportDirectory", MasterControl::InstallerLogSupport::pathToUtf8(logDirectory) },
                      { "sessionText", MasterControl::InstallerLogSupport::pathToUtf8(persistentPaths.bootstrapperSessionLog) }
                  } },
                { "persistentPaths", MasterControl::InstallerLogSupport::persistentPathsToJson(persistentPaths) },
                { "options",
                  {
                      { "manageService", options.manageService },
                      { "manageFirewall", options.manageFirewall },
                      { "manageShortcuts", options.manageShortcuts },
                      { "manageUninstallRegistration", options.manageUninstallRegistration },
                      { "purgeInstallDirectory", options.purgeInstallDirectory },
                      { "purgeData", options.purgeData },
                      { "jsonOutput", options.jsonOutput }
                  } },
                { "details", payload }
            });
    } catch (...) {
    }
}

bool shouldWriteBootstrapperActionLog(const std::wstring& mode) {
    return mode == L"install" || mode == L"repair" || mode == L"upgrade" || mode == L"uninstall";
}

int reportBootstrapperStartupFailure(const std::wstring& mode,
                                     const std::filesystem::path& installDirectory,
                                     const IntegrationOptions& options,
                                     const std::string& message) {
    nlohmann::json payload = {
        { "action", utf8FromWide(mode) },
        { "succeeded", false },
        { "bootstrapperVersion", MASTERCONTROL_BOOTSTRAPPER_VERSION },
        { "installDirectory", installDirectory.string() },
        { "stage", "startup" },
        { "message", message }
    };

    if (shouldWriteBootstrapperActionLog(mode)) {
        writeBootstrapperActionLog(mode, false, installDirectory, options, payload);
    }

    if (options.jsonOutput) {
        std::cout << payload.dump(2) << '\n';
    } else {
        std::wcerr << wideFromUtf8(message) << L'\n';
    }

    return 1;
}

std::optional<nlohmann::json> readJsonFile(const std::filesystem::path& filePath) {
    std::ifstream input(filePath, std::ios::binary);
    if (!input.is_open()) {
        return std::nullopt;
    }

    nlohmann::json json;
    input >> json;
    return json;
}

PayloadLayout resolvePayloadLayout() {
    const auto currentDirectory = executableDirectory();
    std::filesystem::path flatShare;
    for (const auto* shareLeaf : { kCurrentShareLeaf, kLegacyShareLeaf }) {
        const auto candidate = currentDirectory / "share" / shareLeaf;
        if (std::filesystem::exists(candidate / "ForsettiManifests") &&
            std::filesystem::exists(candidate / "web")) {
            flatShare = candidate;
            break;
        }
    }
    if (std::filesystem::exists(currentDirectory / "MasterControlServiceHost.exe") &&
        std::filesystem::exists(currentDirectory / "MasterControlShell.exe") &&
        !flatShare.empty()) {
        return PayloadLayout{
            true,
            currentDirectory,
            currentDirectory,
            currentDirectory,
            currentDirectory,
            flatShare / "ForsettiManifests",
            flatShare / "web"
        };
    }

    const auto configurationName = currentDirectory.filename();
    const auto buildRoot = currentDirectory.parent_path().parent_path().parent_path();
    const auto serviceDirectory = buildRoot / "src" / "MasterControlServiceHost" / configurationName;
    const auto shellDirectory = buildRoot / "src" / "MasterControlShell" / configurationName;

    if (std::filesystem::exists(serviceDirectory / "MasterControlServiceHost.exe") &&
        std::filesystem::exists(shellDirectory / "MasterControlShell.exe")) {
        return PayloadLayout{
            false,
            {},
            currentDirectory,
            serviceDirectory,
            shellDirectory,
            std::filesystem::path(MASTERCONTROL_SOURCE_MODULE_MANIFESTS_DIR),
            std::filesystem::path(MASTERCONTROL_SOURCE_WEB_RESOURCES_DIR)
        };
    }

    throw std::runtime_error("Unable to resolve an installable Master Control Orchestration Server payload from the current bootstrapper location.");
}

void stagePayload(const PayloadLayout& layout, const std::filesystem::path& installDirectory) {
    if (layout.flatPayload) {
        copyRecursive(layout.flatRoot, installDirectory);
        return;
    }

    copyRecursive(layout.bootstrapperDirectory, installDirectory);
    copyRecursive(layout.serviceDirectory, installDirectory);
    copyRecursive(layout.shellDirectory, installDirectory);
    copyRecursive(layout.manifestsDirectory, installDirectory / "share" / kCurrentShareLeaf / "ForsettiManifests");
    copyRecursive(layout.webDirectory, installDirectory / "share" / kCurrentShareLeaf / "web");
}

MasterControl::AppConfiguration ensureConfigurationPresent() {
    const auto paths = MasterControl::resolveAppPaths();
    if (std::filesystem::exists(paths.configurationFile)) {
        if (const auto json = readJsonFile(paths.configurationFile); json.has_value()) {
            return json->get<MasterControl::AppConfiguration>();
        }
    }

    const auto configuration = MasterControl::buildDefaultConfiguration();
    std::filesystem::create_directories(paths.configurationFile.parent_path());
    std::ofstream output(paths.configurationFile, std::ios::trunc);
    output << nlohmann::json(configuration).dump(2);
    return configuration;
}

std::string browserHostForConfiguration(const MasterControl::AppConfiguration& configuration) {
    return configuration.bindAddress == "0.0.0.0" ? "127.0.0.1" : configuration.bindAddress;
}

InstallationState buildInstallationState(const std::filesystem::path& installDirectory,
                                         const MasterControl::AppConfiguration& configuration,
                                         const IntegrationOptions& options) {
    const auto paths = MasterControl::resolveAppPaths();
    const auto shortcutDirectory = preferredShortcutDirectory();
    InstallationState state;
    state.version = MASTERCONTROL_BOOTSTRAPPER_VERSION;
    state.installDirectory = installDirectory.string();
    state.serviceBinary = (installDirectory / "MasterControlServiceHost.exe").string();
    state.shellBinary = (installDirectory / "MasterControlShell.exe").string();
    state.bootstrapperBinary = (installDirectory / "MasterControlBootstrapper.exe").string();
    state.shortcutDirectory = shortcutDirectory.string();
    state.shellShortcutPath = (shortcutDirectory / kShellShortcutName).string();
    state.dashboardShortcutPath = (shortcutDirectory / kDashboardShortcutName).string();
    state.browserUrl = "http://" + browserHostForConfiguration(configuration) + ":" + std::to_string(configuration.browserPort) + "/";
    state.configPath = paths.configurationFile.string();
    state.dataDirectory = paths.dataDirectory.string();
    state.browserPort = configuration.browserPort;
    state.beaconPort = configuration.beaconPort;
    state.allowOpenLanAccess = configuration.security.allowOpenLanAccess;
    state.beaconEnabled = configuration.beaconEnabled;
    state.serviceManaged = options.manageService;
    state.firewallManaged = options.manageFirewall;
    state.shortcutsManaged = options.manageShortcuts;
    state.uninstallRegistrationManaged = options.manageUninstallRegistration;
    return state;
}

std::filesystem::path installStatePath(const std::filesystem::path& installDirectory) {
    return installDirectory / kInstallStateFileName;
}

bool writeInstallationState(const InstallationState& state, const std::filesystem::path& installDirectory) {
    return writeTextFile(installStatePath(installDirectory), nlohmann::json(state).dump(2));
}

std::optional<InstallationState> readInstallationState(const std::filesystem::path& installDirectory) {
    if (const auto json = readJsonFile(installStatePath(installDirectory)); json.has_value()) {
        return json->get<InstallationState>();
    }
    return std::nullopt;
}

std::optional<std::wstring> queryRegistryStringValue(HKEY key, const wchar_t* name) {
    DWORD type = 0;
    DWORD bytesNeeded = 0;
    if (RegQueryValueExW(key, name, nullptr, &type, nullptr, &bytesNeeded) != ERROR_SUCCESS ||
        type != REG_SZ ||
        bytesNeeded == 0) {
        return std::nullopt;
    }

    std::wstring value(bytesNeeded / sizeof(wchar_t), L'\0');
    if (RegQueryValueExW(
            key,
            name,
            nullptr,
            &type,
            reinterpret_cast<LPBYTE>(value.data()),
            &bytesNeeded) != ERROR_SUCCESS) {
        return std::nullopt;
    }

    if (!value.empty() && value.back() == L'\0') {
        value.pop_back();
    }
    return value;
}

UninstallRegistrationStatus queryUninstallRegistrationStatus() {
    UninstallRegistrationStatus status;

    HKEY key = nullptr;
    const auto uninstallRegistryKey = configuredUninstallRegistryKey();
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, uninstallRegistryKey.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return status;
    }

    status.registered = true;
    if (const auto displayVersion = queryRegistryStringValue(key, L"DisplayVersion"); displayVersion.has_value()) {
        status.displayVersion = utf8FromWide(*displayVersion);
    }
    if (const auto installLocation = queryRegistryStringValue(key, L"InstallLocation"); installLocation.has_value()) {
        status.installLocation = utf8FromWide(*installLocation);
    }
    if (const auto uninstallCommand = queryRegistryStringValue(key, L"UninstallString"); uninstallCommand.has_value()) {
        status.uninstallCommand = utf8FromWide(*uninstallCommand);
    }

    RegCloseKey(key);
    return status;
}

ShortcutInstallationStatus queryShortcutInstallationStatus(const std::optional<InstallationState>& state) {
    ShortcutInstallationStatus status;
    if (!state.has_value()) {
        return status;
    }

    std::error_code error;
    status.shellShortcutPresent =
        std::filesystem::exists(std::filesystem::path(state->shellShortcutPath), error) &&
        std::filesystem::is_regular_file(std::filesystem::path(state->shellShortcutPath), error);
    error.clear();
    status.dashboardShortcutPresent =
        std::filesystem::exists(std::filesystem::path(state->dashboardShortcutPath), error) &&
        std::filesystem::is_regular_file(std::filesystem::path(state->dashboardShortcutPath), error);
    return status;
}

bool firewallRuleExists(const std::wstring& ruleName) {
    auto result = runProcessCapture(
        L"netsh.exe advfirewall firewall show rule name=\"" + std::wstring(ruleName) + L"\"",
        {},
        CREATE_NO_WINDOW);
    if (!result.launched || result.exitCode != 0) {
        return false;
    }

    auto output = result.output;
    std::transform(
        output.begin(),
        output.end(),
        output.begin(),
        [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return output.find("rule name:") != std::string::npos &&
        output.find("no rules match") == std::string::npos;
}

FirewallRuleStatus queryFirewallRuleStatus() {
    FirewallRuleStatus status;
    status.browserRulePresent = firewallRuleExists(kBrowserRuleName);
    status.beaconRulePresent = firewallRuleExists(kBeaconRuleName);
    return status;
}

bool setRegistryStringValue(HKEY key, const wchar_t* name, const std::wstring& value) {
    const auto bytes = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
    return RegSetValueExW(
               key,
               name,
               0,
               REG_SZ,
               reinterpret_cast<const BYTE*>(value.c_str()),
               bytes) == ERROR_SUCCESS;
}

bool setRegistryDwordValue(HKEY key, const wchar_t* name, const DWORD value) {
    return RegSetValueExW(
               key,
               name,
               0,
               REG_DWORD,
               reinterpret_cast<const BYTE*>(&value),
               sizeof(value)) == ERROR_SUCCESS;
}

bool registerUninstallEntry(const InstallationState& state) {
    HKEY key = nullptr;
    const auto uninstallRegistryKey = configuredUninstallRegistryKey();
    if (RegCreateKeyExW(
            HKEY_LOCAL_MACHINE,
            uninstallRegistryKey.c_str(),
            0,
            nullptr,
            0,
            KEY_WRITE,
            nullptr,
            &key,
            nullptr) != ERROR_SUCCESS) {
        return false;
    }

    const auto uninstallCommand = L"\"" + std::filesystem::path(state.bootstrapperBinary).wstring() +
        L"\" uninstall \"" + std::filesystem::path(state.installDirectory).wstring() + L"\" --purge-install-dir";

    const bool success =
        setRegistryStringValue(key, L"DisplayName", kProductDisplayName) &&
        setRegistryStringValue(key, L"Publisher", L"James Daley") &&
        setRegistryStringValue(key, L"DisplayVersion", wideFromUtf8(state.version)) &&
        setRegistryStringValue(key, L"InstallLocation", std::filesystem::path(state.installDirectory).wstring()) &&
        setRegistryStringValue(key, L"DisplayIcon", std::filesystem::path(state.shellBinary).wstring()) &&
        setRegistryStringValue(key, L"UninstallString", uninstallCommand) &&
        setRegistryDwordValue(key, L"NoModify", 1) &&
        setRegistryDwordValue(key, L"NoRepair", 0);

    RegCloseKey(key);
    return success;
}

bool unregisterUninstallEntry() {
    const auto uninstallRegistryKey = configuredUninstallRegistryKey();
    const auto result = RegDeleteTreeW(HKEY_LOCAL_MACHINE, uninstallRegistryKey.c_str());
    return result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND;
}

bool createShellShortcut(const InstallationState& state) {
    const auto shortcutPath = std::filesystem::path(state.shellShortcutPath);
    std::filesystem::create_directories(shortcutPath.parent_path());

    const auto script = L"$shell = New-Object -ComObject WScript.Shell; " \
        L"$shortcut = $shell.CreateShortcut('" + escapePowerShellLiteral(shortcutPath.wstring()) + L"'); " \
        L"$shortcut.TargetPath = '" + escapePowerShellLiteral(std::filesystem::path(state.shellBinary).wstring()) + L"'; " \
        L"$shortcut.WorkingDirectory = '" + escapePowerShellLiteral(std::filesystem::path(state.installDirectory).wstring()) + L"'; " \
        L"$shortcut.IconLocation = '" + escapePowerShellLiteral(std::filesystem::path(state.shellBinary).wstring()) + L",0'; " \
        L"$shortcut.Description = '" + std::wstring(kProductDisplayName) + L" administrative shell'; " \
        L"$shortcut.Save()";

    const auto command = L"powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \"" + script + L"\"";
    return runProcess(command, {}, CREATE_NO_WINDOW) == 0;
}

bool createDashboardShortcut(const InstallationState& state) {
    const auto shortcutPath = std::filesystem::path(state.dashboardShortcutPath);
    std::filesystem::create_directories(shortcutPath.parent_path());
    const auto contents =
        std::string("[InternetShortcut]\r\nURL=") + state.browserUrl +
        "\r\nIconFile=" + state.shellBinary +
        "\r\nIconIndex=0\r\n";
    return writeTextFile(shortcutPath, contents);
}

bool configureShortcuts(const InstallationState& state) {
    return createShellShortcut(state) && createDashboardShortcut(state);
}

void removeShortcuts(const InstallationState& state) {
    std::error_code error;
    std::filesystem::remove(std::filesystem::path(state.shellShortcutPath), error);
    std::filesystem::remove(std::filesystem::path(state.dashboardShortcutPath), error);
    std::filesystem::remove(std::filesystem::path(state.shortcutDirectory), error);
}

bool runNetshCommand(const std::wstring& arguments) {
    const auto command = L"netsh.exe advfirewall firewall " + arguments;
    return runProcess(command, {}, CREATE_NO_WINDOW) == 0;
}

std::string serviceStateName(const DWORD state) {
    switch (state) {
        case SERVICE_STOPPED:
            return "stopped";
        case SERVICE_START_PENDING:
            return "start_pending";
        case SERVICE_STOP_PENDING:
            return "stop_pending";
        case SERVICE_RUNNING:
            return "running";
        case SERVICE_CONTINUE_PENDING:
            return "continue_pending";
        case SERVICE_PAUSE_PENDING:
            return "pause_pending";
        case SERVICE_PAUSED:
            return "paused";
        default:
            return "unknown";
    }
}

bool queryServiceProcessStatus(SC_HANDLE service, SERVICE_STATUS_PROCESS& status) {
    DWORD bytesNeeded = 0;
    return QueryServiceStatusEx(
               service,
               SC_STATUS_PROCESS_INFO,
               reinterpret_cast<LPBYTE>(&status),
               sizeof(status),
               &bytesNeeded) != 0;
}

bool waitForProcessExit(const DWORD processId, const DWORD timeoutMilliseconds) {
    if (processId == 0) {
        return true;
    }

    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, processId);
    if (process == nullptr) {
        return GetLastError() == ERROR_INVALID_PARAMETER;
    }

    const auto waitResult = WaitForSingleObject(process, timeoutMilliseconds);
    CloseHandle(process);
    return waitResult == WAIT_OBJECT_0;
}

bool waitForServiceState(SC_HANDLE service, const DWORD targetState, const DWORD timeoutMilliseconds) {
    constexpr DWORD kPollMilliseconds = 500;
    DWORD elapsedMilliseconds = 0;

    while (elapsedMilliseconds <= timeoutMilliseconds) {
        SERVICE_STATUS_PROCESS status{};
        if (!queryServiceProcessStatus(service, status)) {
            return false;
        }

        if (status.dwCurrentState == targetState) {
            return true;
        }

        if ((targetState == SERVICE_RUNNING && status.dwCurrentState == SERVICE_STOPPED) ||
            (targetState == SERVICE_STOPPED && status.dwCurrentState == SERVICE_RUNNING && elapsedMilliseconds > 0)) {
            return false;
        }

        Sleep(kPollMilliseconds);
        elapsedMilliseconds += kPollMilliseconds;
    }

    SERVICE_STATUS_PROCESS status{};
    return queryServiceProcessStatus(service, status) && status.dwCurrentState == targetState;
}

bool stopServiceHandle(SC_HANDLE service, const DWORD timeoutMilliseconds = 30000) {
    SERVICE_STATUS_PROCESS status{};
    if (!queryServiceProcessStatus(service, status)) {
        return false;
    }

    const DWORD originalProcessId = status.dwProcessId;
    if (status.dwCurrentState != SERVICE_STOPPED) {
        if (status.dwCurrentState != SERVICE_STOP_PENDING) {
            SERVICE_STATUS serviceStatus{};
            if (ControlService(service, SERVICE_CONTROL_STOP, &serviceStatus) == 0) {
                const auto error = GetLastError();
                if (error != ERROR_SERVICE_NOT_ACTIVE) {
                    return false;
                }
            }
        }

        if (!waitForServiceState(service, SERVICE_STOPPED, timeoutMilliseconds)) {
            return false;
        }
    }

    return waitForProcessExit(originalProcessId, timeoutMilliseconds);
}

bool waitForServiceRemoval(const DWORD timeoutMilliseconds) {
    constexpr DWORD kPollMilliseconds = 500;
    DWORD elapsedMilliseconds = 0;

    while (elapsedMilliseconds <= timeoutMilliseconds) {
        const auto serviceName = configuredServiceName();
        SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
        if (scm == nullptr) {
            return false;
        }

        SC_HANDLE service = OpenServiceW(scm, serviceName.c_str(), SERVICE_QUERY_STATUS);
        if (service == nullptr) {
            const auto error = GetLastError();
            CloseServiceHandle(scm);
            return error == ERROR_SERVICE_DOES_NOT_EXIST;
        }

        CloseServiceHandle(service);
        CloseServiceHandle(scm);

        Sleep(kPollMilliseconds);
        elapsedMilliseconds += kPollMilliseconds;
    }

    return false;
}

void removeFirewallRules() {
    runNetshCommand(L"delete rule name=\"" + std::wstring(kBrowserRuleName) + L"\"");
    runNetshCommand(L"delete rule name=\"" + std::wstring(kBeaconRuleName) + L"\"");
}

bool configureFirewallRules(const InstallationState& state) {
    removeFirewallRules();

    bool success = true;
    if (state.allowOpenLanAccess) {
        success = runNetshCommand(
                      L"add rule name=\"" + std::wstring(kBrowserRuleName) +
                      L"\" dir=in action=allow profile=private protocol=TCP localport=" +
                      std::to_wstring(state.browserPort) +
                      L" program=\"" + std::filesystem::path(state.serviceBinary).wstring() + L"\"") &&
            success;
    }

    if (state.beaconEnabled) {
        success = runNetshCommand(
                      L"add rule name=\"" + std::wstring(kBeaconRuleName) +
                      L"\" dir=in action=allow profile=private protocol=UDP localport=" +
                      std::to_wstring(state.beaconPort) +
                      L" program=\"" + std::filesystem::path(state.serviceBinary).wstring() + L"\"") &&
            success;
    }

    return success;
}

bool stopServiceIfPresent() {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (scm == nullptr) {
        return false;
    }

    const auto serviceName = configuredServiceName();
    SC_HANDLE service = OpenServiceW(scm, serviceName.c_str(), SERVICE_STOP | SERVICE_QUERY_STATUS);
    if (service == nullptr) {
        CloseServiceHandle(scm);
        return true;
    }

    const bool success = stopServiceHandle(service);
    CloseServiceHandle(service);
    CloseServiceHandle(scm);
    return success;
}

ServiceInstallationStatus queryServiceInstallationStatus() {
    ServiceInstallationStatus status;

    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (scm == nullptr) {
        return status;
    }

    const auto serviceName = configuredServiceName();
    SC_HANDLE service = OpenServiceW(scm, serviceName.c_str(), SERVICE_QUERY_CONFIG | SERVICE_QUERY_STATUS);
    if (service == nullptr) {
        CloseServiceHandle(scm);
        return status;
    }

    status.registered = true;

    DWORD bytesNeeded = 0;
    QueryServiceConfigW(service, nullptr, 0, &bytesNeeded);
    if (bytesNeeded != 0) {
        std::vector<std::byte> buffer(bytesNeeded);
        auto* configuration = reinterpret_cast<QUERY_SERVICE_CONFIGW*>(buffer.data());
        if (QueryServiceConfigW(service, configuration, bytesNeeded, &bytesNeeded) != 0) {
            status.autoStart = configuration->dwStartType == SERVICE_AUTO_START;
            if (configuration->lpBinaryPathName != nullptr) {
                status.binaryPath = utf8FromWide(configuration->lpBinaryPathName);
            }
        }
    }

    SERVICE_DELAYED_AUTO_START_INFO delayedAutoStart{};
    bytesNeeded = 0;
    if (QueryServiceConfig2W(
            service,
            SERVICE_CONFIG_DELAYED_AUTO_START_INFO,
            reinterpret_cast<LPBYTE>(&delayedAutoStart),
            sizeof(delayedAutoStart),
            &bytesNeeded) != 0) {
        status.delayedAutoStart = delayedAutoStart.fDelayedAutostart != FALSE;
    }

    DWORD failureActionsBytes = 0;
    QueryServiceConfig2W(service, SERVICE_CONFIG_FAILURE_ACTIONS, nullptr, 0, &failureActionsBytes);
    if (failureActionsBytes != 0) {
        std::vector<std::byte> failureActionsBuffer(failureActionsBytes);
        auto* failureActions = reinterpret_cast<SERVICE_FAILURE_ACTIONSW*>(failureActionsBuffer.data());
        if (QueryServiceConfig2W(
                service,
                SERVICE_CONFIG_FAILURE_ACTIONS,
                reinterpret_cast<LPBYTE>(failureActions),
                failureActionsBytes,
                &failureActionsBytes) != 0) {
            status.recoveryConfigured = failureActions->cActions > 0 &&
                failureActions->lpsaActions != nullptr &&
                failureActions->lpsaActions[0].Type == SC_ACTION_RESTART;
        }
    }

    SERVICE_FAILURE_ACTIONS_FLAG failureActionsFlag{};
    bytesNeeded = 0;
    if (QueryServiceConfig2W(
            service,
            SERVICE_CONFIG_FAILURE_ACTIONS_FLAG,
            reinterpret_cast<LPBYTE>(&failureActionsFlag),
            sizeof(failureActionsFlag),
            &bytesNeeded) != 0) {
        status.failureActionsOnNonCrash = failureActionsFlag.fFailureActionsOnNonCrashFailures != FALSE;
    }

    SERVICE_SID_INFO serviceSidInfo{};
    bytesNeeded = 0;
    if (QueryServiceConfig2W(
            service,
            SERVICE_CONFIG_SERVICE_SID_INFO,
            reinterpret_cast<LPBYTE>(&serviceSidInfo),
            sizeof(serviceSidInfo),
            &bytesNeeded) != 0) {
        status.sidUnrestricted = serviceSidInfo.dwServiceSidType == SERVICE_SID_TYPE_UNRESTRICTED;
    }

    SERVICE_STATUS_PROCESS processStatus{};
    if (queryServiceProcessStatus(service, processStatus)) {
        status.running = processStatus.dwCurrentState == SERVICE_RUNNING;
        status.state = serviceStateName(processStatus.dwCurrentState);
        status.processId = processStatus.dwProcessId;
        status.win32ExitCode = processStatus.dwWin32ExitCode;
        status.serviceSpecificExitCode = processStatus.dwServiceSpecificExitCode;
        status.checkpoint = processStatus.dwCheckPoint;
        status.waitHintMilliseconds = processStatus.dwWaitHint;
    }

    CloseServiceHandle(service);
    CloseServiceHandle(scm);
    return status;
}

bool configureServicePolicies(SC_HANDLE service) {
    SERVICE_DESCRIPTIONW description{};
    description.lpDescription = const_cast<LPWSTR>(
        L"Forsetti-native control service for MCP servers, sub-agents, telemetry, and browser administration.");
    if (ChangeServiceConfig2W(service, SERVICE_CONFIG_DESCRIPTION, &description) == 0) {
        return false;
    }

    SERVICE_DELAYED_AUTO_START_INFO delayedAutoStart{};
    delayedAutoStart.fDelayedAutostart = TRUE;
    if (ChangeServiceConfig2W(service, SERVICE_CONFIG_DELAYED_AUTO_START_INFO, &delayedAutoStart) == 0) {
        return false;
    }

    SC_ACTION recoveryActions[3]{};
    recoveryActions[0].Type = SC_ACTION_RESTART;
    recoveryActions[0].Delay = 5000;
    recoveryActions[1].Type = SC_ACTION_RESTART;
    recoveryActions[1].Delay = 5000;
    recoveryActions[2].Type = SC_ACTION_NONE;
    recoveryActions[2].Delay = 0;

    SERVICE_FAILURE_ACTIONSW failureActions{};
    failureActions.dwResetPeriod = 86400;
    failureActions.cActions = static_cast<DWORD>(std::size(recoveryActions));
    failureActions.lpsaActions = recoveryActions;
    if (ChangeServiceConfig2W(service, SERVICE_CONFIG_FAILURE_ACTIONS, &failureActions) == 0) {
        return false;
    }

    SERVICE_FAILURE_ACTIONS_FLAG failureActionsFlag{};
    failureActionsFlag.fFailureActionsOnNonCrashFailures = TRUE;
    if (ChangeServiceConfig2W(service, SERVICE_CONFIG_FAILURE_ACTIONS_FLAG, &failureActionsFlag) == 0) {
        return false;
    }

    SERVICE_SID_INFO serviceSidInfo{};
    serviceSidInfo.dwServiceSidType = SERVICE_SID_TYPE_UNRESTRICTED;
    if (ChangeServiceConfig2W(service, SERVICE_CONFIG_SERVICE_SID_INFO, &serviceSidInfo) == 0) {
        return false;
    }

    return true;
}

bool installOrUpdateService(const std::filesystem::path& serviceBinary) {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
    if (scm == nullptr) {
        return false;
    }

    const auto serviceName = configuredServiceName();
    const std::wstring binaryPath = L"\"" + serviceBinary.wstring() + L"\"";
    SC_HANDLE service = CreateServiceW(
        scm,
        serviceName.c_str(),
        kProductDisplayName,
        SERVICE_ALL_ACCESS,
        SERVICE_WIN32_OWN_PROCESS,
        SERVICE_AUTO_START,
        SERVICE_ERROR_NORMAL,
        binaryPath.c_str(),
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr);

    if (service == nullptr) {
        service = OpenServiceW(scm, serviceName.c_str(), SERVICE_ALL_ACCESS);
        if (service == nullptr) {
            CloseServiceHandle(scm);
            return false;
        }

        if (ChangeServiceConfigW(
                service,
                SERVICE_NO_CHANGE,
                SERVICE_AUTO_START,
                SERVICE_NO_CHANGE,
                binaryPath.c_str(),
                nullptr,
                nullptr,
                nullptr,
                nullptr,
                nullptr,
                nullptr) == 0) {
            CloseServiceHandle(service);
            CloseServiceHandle(scm);
            return false;
        }
    }

    if (!configureServicePolicies(service)) {
        CloseServiceHandle(service);
        CloseServiceHandle(scm);
        return false;
    }

    CloseServiceHandle(service);
    CloseServiceHandle(scm);
    return true;
}

bool startServiceIfPresent() {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (scm == nullptr) {
        return false;
    }

    const auto serviceName = configuredServiceName();
    SC_HANDLE service = OpenServiceW(scm, serviceName.c_str(), SERVICE_START | SERVICE_QUERY_STATUS);
    if (service == nullptr) {
        CloseServiceHandle(scm);
        return false;
    }

    SERVICE_STATUS_PROCESS status{};
    bool success = queryServiceProcessStatus(service, status) &&
        status.dwCurrentState == SERVICE_RUNNING;

    if (!success) {
        success = StartServiceW(service, 0, nullptr) != 0 || GetLastError() == ERROR_SERVICE_ALREADY_RUNNING;
        if (success) {
            success = waitForServiceState(service, SERVICE_RUNNING, 30000);
        }
    }

    CloseServiceHandle(service);
    CloseServiceHandle(scm);
    return success;
}

bool uninstallService() {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (scm == nullptr) {
        return false;
    }

    const auto serviceName = configuredServiceName();
    SC_HANDLE service = OpenServiceW(scm, serviceName.c_str(), SERVICE_STOP | DELETE | SERVICE_QUERY_STATUS);
    if (service == nullptr) {
        CloseServiceHandle(scm);
        return true;
    }

    bool success = stopServiceHandle(service);
    success = (DeleteService(service) != 0 || GetLastError() == ERROR_SERVICE_MARKED_FOR_DELETE) && success;
    CloseServiceHandle(service);
    CloseServiceHandle(scm);
    return success && waitForServiceRemoval(30000);
}

bool scheduleDeferredInstallRemoval(const std::filesystem::path& installDirectory) {
    const auto scriptPath = std::filesystem::temp_directory_path() / "MasterControlOrchestrationServer-uninstall.cmd";
    const std::string script =
        "@echo off\r\n"
        "ping 127.0.0.1 -n 4 > nul\r\n"
        "rmdir /s /q \"" + installDirectory.string() + "\"\r\n"
        "del /q \"%~f0\"\r\n";

    if (!writeTextFile(scriptPath, script)) {
        return false;
    }

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInformation{};
    std::wstring command = L"cmd.exe /c \"" + scriptPath.wstring() + L"\"";
    if (CreateProcessW(
            nullptr,
            command.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW | DETACHED_PROCESS,
            nullptr,
            nullptr,
            &startupInfo,
            &processInformation) == 0) {
        return false;
    }

    CloseHandle(processInformation.hThread);
    CloseHandle(processInformation.hProcess);
    return true;
}

bool validateInstalledApplication(const std::filesystem::path& installDirectory,
                                  const bool jsonOutput,
                                  const bool emitOutput = true) {
    std::vector<std::wstring> issues;
    auto appendIssue = [&issues](std::wstring issue) {
        issues.push_back(std::move(issue));
    };

    if (!std::filesystem::exists(installDirectory) || !std::filesystem::is_directory(installDirectory)) {
        appendIssue(L"Install directory does not exist.");
    }

    const auto state = readInstallationState(installDirectory);
    if (!state.has_value()) {
        appendIssue(L"Installation state file is missing or unreadable.");
    }

    const auto checkFile = [&appendIssue](const std::filesystem::path& path, const wchar_t* label) {
        if (!std::filesystem::exists(path) || !std::filesystem::is_regular_file(path)) {
            appendIssue(std::wstring(label) + L" is missing: " + path.wstring());
        }
    };

    const auto checkDirectory = [&appendIssue](const std::filesystem::path& path, const wchar_t* label) {
        if (!std::filesystem::exists(path) || !std::filesystem::is_directory(path)) {
            appendIssue(std::wstring(label) + L" is missing: " + path.wstring());
        }
    };
    const auto normalizeServiceBinaryPath = [](std::string value) {
        if (!value.empty() && value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.size() - 2);
        }
        std::error_code error;
        const auto normalized = std::filesystem::weakly_canonical(std::filesystem::path(value), error);
        return error ? std::filesystem::path(value) : normalized;
    };

    checkFile(installDirectory / "MasterControlServiceHost.exe", L"Service host");
    checkFile(installDirectory / "MasterControlShell.exe", L"Shell host");
    checkFile(installDirectory / "MasterControlBootstrapper.exe", L"Bootstrapper");
    std::filesystem::path shareRoot;
    for (const auto* shareLeaf : { kCurrentShareLeaf, kLegacyShareLeaf }) {
        const auto candidate = installDirectory / "share" / shareLeaf;
        if (std::filesystem::exists(candidate / "ForsettiManifests") ||
            std::filesystem::exists(candidate / "web")) {
            shareRoot = candidate;
            break;
        }
    }
    if (shareRoot.empty()) {
        shareRoot = installDirectory / "share" / kCurrentShareLeaf;
    }

    checkDirectory(shareRoot / "ForsettiManifests", L"Forsetti manifest directory");
    checkFile(shareRoot / "ForsettiManifests" / "DashboardUIModule.json", L"Dashboard UI manifest");
    checkDirectory(shareRoot / "web", L"Web asset directory");
    checkFile(shareRoot / "web" / "index.html", L"Browser dashboard asset");

    const auto serviceStatus = queryServiceInstallationStatus();
    const auto uninstallStatus = queryUninstallRegistrationStatus();
    const auto shortcutStatus = queryShortcutInstallationStatus(state);
    const auto firewallStatus = queryFirewallRuleStatus();
    if (state.has_value()) {
        const auto expectedInstallDirectory = std::filesystem::path(state->installDirectory);
        if (!expectedInstallDirectory.empty() && expectedInstallDirectory != installDirectory) {
            appendIssue(
                L"Installation state points to a different install directory: " +
                expectedInstallDirectory.wstring());
        }

        if (state->browserPort == 0) {
            appendIssue(L"Installation state does not publish a valid browser port.");
        }
        if (state->browserUrl.empty()) {
            appendIssue(L"Installation state does not publish a browser URL.");
        }
        if (state->configPath.empty()) {
            appendIssue(L"Installation state does not publish a configuration path.");
        } else {
            checkFile(std::filesystem::path(state->configPath), L"Configuration file");
        }
        if (state->dataDirectory.empty()) {
            appendIssue(L"Installation state does not publish a data directory.");
        } else {
            checkDirectory(std::filesystem::path(state->dataDirectory), L"Data directory");
        }

        if (state->serviceManaged) {
            if (!serviceStatus.registered) {
                appendIssue(L"Windows service integration is expected but the service is not registered.");
            } else {
                if (!serviceStatus.autoStart) {
                    appendIssue(L"Windows service is registered but not configured for automatic startup.");
                }
                if (!serviceStatus.delayedAutoStart) {
                    appendIssue(L"Windows service is registered but delayed auto-start is not enabled.");
                }
                if (!serviceStatus.recoveryConfigured) {
                    appendIssue(L"Windows service is registered but recovery actions are not configured.");
                }
                if (!serviceStatus.failureActionsOnNonCrash) {
                    appendIssue(L"Windows service does not apply recovery actions to non-crash failures.");
                }
                if (!serviceStatus.sidUnrestricted) {
                    appendIssue(L"Windows service SID type is not configured as unrestricted.");
                }
                if (!serviceStatus.running) {
                    appendIssue(
                        L"Windows service is registered but not currently running (state: " +
                        wideFromUtf8(serviceStatus.state) + L").");
                }
                if (!state->serviceBinary.empty() && !serviceStatus.binaryPath.empty()) {
                    const auto expectedBinary = normalizeServiceBinaryPath(state->serviceBinary);
                    const auto registeredBinary = normalizeServiceBinaryPath(serviceStatus.binaryPath);
                    if (expectedBinary != registeredBinary) {
                        appendIssue(
                            L"Windows service is registered to a different binary path: " +
                            registeredBinary.wstring());
                    }
                }
            }
        }

        if (state->shortcutsManaged) {
            if (!shortcutStatus.shellShortcutPresent) {
                appendIssue(L"Shell shortcut integration is expected but the Start Menu shell shortcut is missing.");
            }
            if (!shortcutStatus.dashboardShortcutPresent) {
                appendIssue(L"Browser dashboard shortcut integration is expected but the dashboard shortcut is missing.");
            }
        }

        if (state->uninstallRegistrationManaged) {
            if (!uninstallStatus.registered) {
                appendIssue(L"Programs and Features registration is expected but the uninstall entry is missing.");
            } else if (!state->installDirectory.empty() && !uninstallStatus.installLocation.empty()) {
                const auto expectedUninstallInstallDirectory = normalizeServiceBinaryPath(state->installDirectory);
                const auto registeredInstallDirectory = normalizeServiceBinaryPath(uninstallStatus.installLocation);
                if (expectedUninstallInstallDirectory != registeredInstallDirectory) {
                    appendIssue(
                        L"Uninstall registration points to a different install location: " +
                        registeredInstallDirectory.wstring());
                }
            }
        }

        if (state->firewallManaged) {
            if (state->allowOpenLanAccess && !firewallStatus.browserRulePresent) {
                appendIssue(L"Browser firewall access is expected but the inbound browser rule is missing.");
            }
            if (state->beaconEnabled && !firewallStatus.beaconRulePresent) {
                appendIssue(L"Beacon firewall access is expected but the inbound beacon rule is missing.");
            }
        }
    }

    if (jsonOutput) {
        nlohmann::json payload = {
            { "valid", issues.empty() },
            { "installDirectory", installDirectory.string() },
            { "issues", nlohmann::json::array() },
            { "bootstrapperVersion", MASTERCONTROL_BOOTSTRAPPER_VERSION },
            { "serviceRegistered", serviceStatus.registered },
            { "serviceAutoStart", serviceStatus.autoStart },
            { "serviceDelayedAutoStart", serviceStatus.delayedAutoStart },
            { "serviceRecoveryConfigured", serviceStatus.recoveryConfigured },
            { "serviceFailureActionsOnNonCrash", serviceStatus.failureActionsOnNonCrash },
            { "serviceSidUnrestricted", serviceStatus.sidUnrestricted },
            { "serviceRunning", serviceStatus.running },
            { "serviceState", serviceStatus.state },
            { "serviceProcessId", serviceStatus.processId },
            { "serviceWin32ExitCode", serviceStatus.win32ExitCode },
            { "serviceSpecificExitCode", serviceStatus.serviceSpecificExitCode },
            { "serviceCheckpoint", serviceStatus.checkpoint },
            { "serviceWaitHintMs", serviceStatus.waitHintMilliseconds },
            { "serviceBinaryPath", serviceStatus.binaryPath },
            { "uninstallRegistered", uninstallStatus.registered },
            { "uninstallDisplayVersion", uninstallStatus.displayVersion },
            { "uninstallInstallLocation", uninstallStatus.installLocation },
            { "uninstallCommand", uninstallStatus.uninstallCommand },
            { "shellShortcutPresent", shortcutStatus.shellShortcutPresent },
            { "dashboardShortcutPresent", shortcutStatus.dashboardShortcutPresent },
            { "browserFirewallRulePresent", firewallStatus.browserRulePresent },
            { "beaconFirewallRulePresent", firewallStatus.beaconRulePresent }
        };
        for (const auto& issue : issues) {
            payload["issues"].push_back(utf8FromWide(issue));
        }

        if (state.has_value()) {
            payload["version"] = state->version;
            payload["browserUrl"] = state->browserUrl;
            payload["configPath"] = state->configPath;
            payload["dataDirectory"] = state->dataDirectory;
            payload["browserPort"] = state->browserPort;
            payload["beaconPort"] = state->beaconPort;
            payload["serviceManaged"] = state->serviceManaged;
            payload["firewallManaged"] = state->firewallManaged;
            payload["shortcutsManaged"] = state->shortcutsManaged;
            payload["uninstallRegistrationManaged"] = state->uninstallRegistrationManaged;
        }

        if (emitOutput) {
            std::cout << payload.dump(2) << '\n';
        }
        return issues.empty();
    }

    if (!emitOutput) {
        return issues.empty();
    }

    if (!issues.empty()) {
        std::wcerr << L"Master Control Orchestration Server installation validation failed for "
                   << installDirectory.c_str() << L":\n";
        for (const auto& issue : issues) {
            std::wcerr << L"  - " << issue << L'\n';
        }
        return false;
    }

    std::wcout << L"Validated Master Control Orchestration Server installation at "
               << installDirectory.c_str() << L'\n';
    if (state.has_value()) {
        std::wcout << L"Browser URL: " << wideFromUtf8(state->browserUrl) << L'\n';
        std::wcout << L"Configuration path: " << wideFromUtf8(state->configPath) << L'\n';
        std::wcout << L"Data directory: " << wideFromUtf8(state->dataDirectory) << L'\n';
    }
    return true;
}

void showDetectedEnvironment(const bool jsonOutput) {
    const auto paths = MasterControl::resolveAppPaths();
    const auto environment = MasterControl::detectLocalEnvironment();
    const auto configuration = MasterControl::buildDefaultConfiguration();
    if (jsonOutput) {
        const auto serviceStatus = queryServiceInstallationStatus();
        const auto uninstallStatus = queryUninstallRegistrationStatus();
        const auto firewallStatus = queryFirewallRuleStatus();
        const nlohmann::json payload = {
            { "detected", true },
            { "bootstrapperVersion", MASTERCONTROL_BOOTSTRAPPER_VERSION },
            { "hostName", environment.hostName },
            { "operatingSystem", environment.operatingSystem },
            { "preferredBindAddress", environment.preferredBindAddress },
            { "macAddress", environment.macAddress },
            { "configurationPath", paths.configurationFile.string() },
            { "dataDirectory", paths.dataDirectory.string() },
            { "defaultBrowserPort", configuration.browserPort },
            { "defaultBeaconPort", configuration.beaconPort },
            { "seededPlatformEndpoints", configuration.activeProfile.seededEndpoints.size() },
            { "serviceRegistered", serviceStatus.registered },
            { "serviceAutoStart", serviceStatus.autoStart },
            { "serviceDelayedAutoStart", serviceStatus.delayedAutoStart },
            { "serviceRecoveryConfigured", serviceStatus.recoveryConfigured },
            { "serviceFailureActionsOnNonCrash", serviceStatus.failureActionsOnNonCrash },
            { "serviceSidUnrestricted", serviceStatus.sidUnrestricted },
            { "serviceRunning", serviceStatus.running },
            { "serviceState", serviceStatus.state },
            { "serviceProcessId", serviceStatus.processId },
            { "serviceWin32ExitCode", serviceStatus.win32ExitCode },
            { "serviceSpecificExitCode", serviceStatus.serviceSpecificExitCode },
            { "serviceCheckpoint", serviceStatus.checkpoint },
            { "serviceWaitHintMs", serviceStatus.waitHintMilliseconds },
            { "serviceBinaryPath", serviceStatus.binaryPath },
            { "uninstallRegistered", uninstallStatus.registered },
            { "uninstallDisplayVersion", uninstallStatus.displayVersion },
            { "uninstallInstallLocation", uninstallStatus.installLocation },
            { "browserFirewallRulePresent", firewallStatus.browserRulePresent },
            { "beaconFirewallRulePresent", firewallStatus.beaconRulePresent }
        };
        std::cout << payload.dump(2) << '\n';
        return;
    }

    std::cout << "Detected host name: " << environment.hostName << '\n';
    std::cout << "Detected operating system: " << environment.operatingSystem << '\n';
    std::cout << "Detected primary IP: " << environment.preferredBindAddress << '\n';
    std::cout << "Detected primary MAC: " << (environment.macAddress.empty() ? "n/a" : environment.macAddress) << '\n';
    std::cout << "Configuration path: " << paths.configurationFile.string() << '\n';
    std::cout << "Install data directory: " << paths.dataDirectory.string() << '\n';
    std::cout << "Default browser port: " << configuration.browserPort << '\n';
    std::cout << "Default beacon port: " << configuration.beaconPort << '\n';
    std::cout << "Seeded platform endpoints: " << configuration.activeProfile.seededEndpoints.size() << '\n';
}

bool isProcessElevated() {
    return IsUserAnAdmin() != FALSE;
}

bool probeWritableInstallTarget(const std::filesystem::path& installDirectory) {
    std::error_code error;

    if (std::filesystem::exists(installDirectory, error)) {
        const auto probeFile =
            installDirectory / (L".mastercontrol-write-probe-" + std::to_wstring(GetCurrentProcessId()) + L".tmp");
        if (!writeTextFile(probeFile, "probe")) {
            return false;
        }
        std::filesystem::remove(probeFile, error);
        return true;
    }

    const auto parentDirectory = installDirectory.parent_path();
    if (parentDirectory.empty() ||
        !std::filesystem::exists(parentDirectory, error) ||
        !std::filesystem::is_directory(parentDirectory, error)) {
        return false;
    }

    const auto probeDirectory =
        parentDirectory / (L".mastercontrol-install-probe-" + std::to_wstring(GetCurrentProcessId()));
    if (!std::filesystem::create_directory(probeDirectory, error)) {
        return false;
    }
    std::filesystem::remove(probeDirectory, error);
    return true;
}

bool runPreflight(const std::filesystem::path& installDirectory, const IntegrationOptions& options) {
    std::vector<std::wstring> issues;
    std::vector<std::wstring> warnings;
    const auto appendIssue = [&issues](std::wstring issue) { issues.push_back(std::move(issue)); };
    const auto appendWarning = [&warnings](std::wstring warning) { warnings.push_back(std::move(warning)); };

    const auto elevated = isProcessElevated();
    std::optional<PayloadLayout> payloadLayout;
    try {
        payloadLayout = resolvePayloadLayout();
    } catch (const std::exception& error) {
        appendIssue(L"Bootstrapper payload could not be resolved: " + wideFromUtf8(error.what()));
    }

    if (payloadLayout.has_value()) {
        if (!std::filesystem::exists(payloadLayout->serviceDirectory / "MasterControlServiceHost.exe")) {
            appendIssue(L"Bootstrapper payload is missing MasterControlServiceHost.exe.");
        }
        if (!std::filesystem::exists(payloadLayout->shellDirectory / "MasterControlShell.exe")) {
            appendIssue(L"Bootstrapper payload is missing MasterControlShell.exe.");
        }
        if (!std::filesystem::exists(payloadLayout->manifestsDirectory / "DashboardUIModule.json")) {
            appendIssue(L"Bootstrapper payload is missing the Dashboard UI Forsetti manifest.");
        }
        if (!std::filesystem::exists(payloadLayout->webDirectory / "index.html")) {
            appendIssue(L"Bootstrapper payload is missing browser dashboard assets.");
        }
    }

    if (!probeWritableInstallTarget(installDirectory)) {
        appendIssue(L"Install target is not writable: " + installDirectory.wstring());
    }

    if ((options.manageService || options.manageFirewall || options.manageUninstallRegistration) && !elevated) {
        appendIssue(L"Administrator elevation is required for the selected service, firewall, or uninstall integrations.");
    }

    if (options.manageShortcuts) {
        try {
            const auto shortcutDirectory = preferredShortcutDirectory();
            if (shortcutDirectory.empty()) {
                appendIssue(L"Shortcut folder could not be resolved.");
            } else if (!probeWritableInstallTarget(shortcutDirectory)) {
                appendIssue(L"Shortcut folder is not writable: " + shortcutDirectory.wstring());
            }
        } catch (const std::exception& error) {
            appendIssue(L"Shortcut folder could not be resolved: " + wideFromUtf8(error.what()));
        }
    }

    const auto defaultConfiguration = MasterControl::buildDefaultConfiguration();
    if (options.manageFirewall && !defaultConfiguration.security.allowOpenLanAccess && !defaultConfiguration.beaconEnabled) {
        appendWarning(L"Firewall integration is enabled, but the default configuration does not currently open browser or beacon LAN access.");
    }

    if (options.jsonOutput) {
        nlohmann::json payload = {
            { "ready", issues.empty() },
            { "bootstrapperVersion", MASTERCONTROL_BOOTSTRAPPER_VERSION },
            { "installDirectory", installDirectory.string() },
            { "payloadDetected", payloadLayout.has_value() },
            { "payloadMode", payloadLayout.has_value() ? (payloadLayout->flatPayload ? "flat" : "build-tree") : "missing" },
            { "elevated", elevated },
            { "serviceManaged", options.manageService },
            { "firewallManaged", options.manageFirewall },
            { "shortcutsManaged", options.manageShortcuts },
            { "uninstallRegistrationManaged", options.manageUninstallRegistration },
            { "shortcutDirectory", options.manageShortcuts ? preferredShortcutDirectory().string() : "" },
            { "issues", nlohmann::json::array() },
            { "warnings", nlohmann::json::array() }
        };
        if (payloadLayout.has_value()) {
            payload["servicePayloadPath"] = payloadLayout->serviceDirectory.string();
            payload["shellPayloadPath"] = payloadLayout->shellDirectory.string();
            payload["manifestsPayloadPath"] = payloadLayout->manifestsDirectory.string();
            payload["webPayloadPath"] = payloadLayout->webDirectory.string();
        }
        for (const auto& issue : issues) {
            payload["issues"].push_back(utf8FromWide(issue));
        }
        for (const auto& warning : warnings) {
            payload["warnings"].push_back(utf8FromWide(warning));
        }
        std::cout << payload.dump(2) << '\n';
        return issues.empty();
    }

    if (!issues.empty()) {
        std::wcerr << L"Master Control Orchestration Server preflight failed for "
                   << installDirectory.c_str() << L":\n";
        for (const auto& issue : issues) {
            std::wcerr << L"  - " << issue << L'\n';
        }
        for (const auto& warning : warnings) {
            std::wcerr << L"  * warning: " << warning << L'\n';
        }
        return false;
    }

    std::wcout << L"Preflight ready for " << installDirectory.c_str() << L'\n';
    if (!warnings.empty()) {
        for (const auto& warning : warnings) {
            std::wcout << L"  warning: " << warning << L'\n';
        }
    }
    return true;
}

bool installLike(const std::wstring& mode,
                 const std::filesystem::path& installDirectory,
                 const IntegrationOptions& options) {
    const auto payloadLayout = resolvePayloadLayout();
    const bool hadExistingInstall = directoryHasEntries(installDirectory);
    std::optional<std::filesystem::path> rollbackDirectory;
    if ((mode == L"repair" || mode == L"upgrade") && hadExistingInstall) {
        rollbackDirectory = rollbackSnapshotDirectory(installDirectory);
        if (!captureRollbackSnapshot(installDirectory, *rollbackDirectory)) {
            std::wcerr << L"Failed to capture a rollback snapshot for "
                       << installDirectory.c_str() << L".\n";
            return false;
        }
    }

    std::filesystem::create_directories(installDirectory);

    if (options.manageService) {
        stopServiceIfPresent();
    }

    std::optional<InstallationState> stagedState;
    const auto rollbackOrCleanup = [&]() {
        if (!rollbackDirectory.has_value()) {
            bool cleanupSuccess = true;
            if (options.manageService) {
                cleanupSuccess = uninstallService() && cleanupSuccess;
            }
            if (options.manageFirewall) {
                removeFirewallRules();
            }
            if (stagedState.has_value() && (options.manageShortcuts || stagedState->shortcutsManaged)) {
                removeShortcuts(*stagedState);
            }
            if (options.manageUninstallRegistration) {
                cleanupSuccess = unregisterUninstallEntry() && cleanupSuccess;
            }

            std::error_code error;
            std::filesystem::remove(installStatePath(installDirectory), error);
            removeDirectoryIfExists(installDirectory);
            return cleanupSuccess;
        }

        bool rollbackSuccess = true;
        if (options.manageService) {
            rollbackSuccess = stopServiceIfPresent() && rollbackSuccess;
        }

        rollbackSuccess = restoreRollbackSnapshot(installDirectory, *rollbackDirectory) && rollbackSuccess;

        const auto restoredState = readInstallationState(installDirectory);
        if (!restoredState.has_value()) {
            return false;
        }

        if (options.manageService || restoredState->serviceManaged) {
            if (restoredState->serviceManaged) {
                rollbackSuccess =
                    installOrUpdateService(installDirectory / "MasterControlServiceHost.exe") &&
                    startServiceIfPresent() &&
                    rollbackSuccess;
            } else {
                rollbackSuccess = uninstallService() && rollbackSuccess;
            }
        }

        if (options.manageFirewall || restoredState->firewallManaged) {
            if (restoredState->firewallManaged) {
                rollbackSuccess = configureFirewallRules(*restoredState) && rollbackSuccess;
            } else {
                removeFirewallRules();
            }
        }

        if (options.manageShortcuts || restoredState->shortcutsManaged) {
            removeShortcuts(*restoredState);
            if (restoredState->shortcutsManaged) {
                rollbackSuccess = configureShortcuts(*restoredState) && rollbackSuccess;
            }
        }

        if (options.manageUninstallRegistration || restoredState->uninstallRegistrationManaged) {
            if (restoredState->uninstallRegistrationManaged) {
                rollbackSuccess = registerUninstallEntry(*restoredState) && rollbackSuccess;
            } else {
                rollbackSuccess = unregisterUninstallEntry() && rollbackSuccess;
            }
        }

        return rollbackSuccess;
    };

    const auto failInstall = [&](const std::wstring& message) {
        const bool rollbackAttempted = rollbackDirectory.has_value();
        const bool rollbackRestored = rollbackOrCleanup();
        nlohmann::json payload = {
            { "action", utf8FromWide(mode) },
            { "succeeded", false },
            { "bootstrapperVersion", MASTERCONTROL_BOOTSTRAPPER_VERSION },
            { "installDirectory", installDirectory.string() },
            { "error", utf8FromWide(message) },
            { "rollbackAttempted", rollbackAttempted },
            { "rollbackRestored", rollbackRestored }
        };

        if (options.jsonOutput) {
            const auto currentState = readInstallationState(installDirectory);
            const auto serviceStatus = queryServiceInstallationStatus();
            const auto uninstallStatus = queryUninstallRegistrationStatus();
            const auto shortcutStatus = queryShortcutInstallationStatus(currentState);
            const auto firewallStatus = queryFirewallRuleStatus();
            payload["installStatePresent"] = currentState.has_value();
            payload["serviceRegistered"] = serviceStatus.registered;
            payload["serviceRunning"] = serviceStatus.running;
            payload["serviceState"] = serviceStatus.state;
            payload["uninstallRegistered"] = uninstallStatus.registered;
            payload["shellShortcutPresent"] = shortcutStatus.shellShortcutPresent;
            payload["dashboardShortcutPresent"] = shortcutStatus.dashboardShortcutPresent;
            payload["browserFirewallRulePresent"] = firewallStatus.browserRulePresent;
            payload["beaconFirewallRulePresent"] = firewallStatus.beaconRulePresent;
            if (rollbackDirectory.has_value() && !rollbackRestored) {
                payload["rollbackSnapshotPath"] = rollbackDirectory->string();
            }
            writeBootstrapperActionLog(mode, false, installDirectory, options, payload);
            std::cout << payload.dump(2) << '\n';
        } else {
            if (rollbackDirectory.has_value() && !rollbackRestored) {
                payload["rollbackSnapshotPath"] = rollbackDirectory->string();
            }
            writeBootstrapperActionLog(mode, false, installDirectory, options, payload);
            std::wcerr << message << L'\n';
            if (!rollbackRestored) {
                if (rollbackDirectory.has_value()) {
                    std::wcerr << L"Rollback failed. Backup preserved at "
                               << rollbackDirectory->c_str() << L'\n';
                } else {
                    std::wcerr << L"Failed to clean up the partial installation state.\n";
                }
            } else if (rollbackDirectory.has_value()) {
                std::wcerr << L"Previous installation restored from rollback snapshot.\n";
            }
        }

        if (rollbackDirectory.has_value() && rollbackRestored) {
            removeDirectoryIfExists(*rollbackDirectory);
        }
        return false;
    };

    stagePayload(payloadLayout, installDirectory);
    const auto configuration = ensureConfigurationPresent();
    stagedState = buildInstallationState(installDirectory, configuration, options);
    if (!writeInstallationState(*stagedState, installDirectory)) {
        return failInstall(L"Failed to write installation state.");
    }

    if (environmentFlagEnabled(L"MASTERCONTROL_BOOTSTRAPPER_TEST_FAIL_AFTER_STAGE")) {
        return failInstall(L"Simulated bootstrapper failure after staging payload and installation state.");
    }

    if (options.manageFirewall && !configureFirewallRules(*stagedState)) {
        return failInstall(L"Failed to configure firewall rules.");
    }

    if (options.manageShortcuts && !configureShortcuts(*stagedState)) {
        return failInstall(L"Failed to create shortcuts.");
    }

    if (options.manageUninstallRegistration && !registerUninstallEntry(*stagedState)) {
        return failInstall(L"Failed to register the uninstall entry.");
    }

    if (options.manageService) {
        if (!installOrUpdateService(installDirectory / "MasterControlServiceHost.exe")) {
            return failInstall(L"Failed to install Windows service.");
        }
        if (!startServiceIfPresent()) {
            return failInstall(L"Failed to start Windows service.");
        }
    }

    if (!validateInstalledApplication(installDirectory, false, false)) {
        return failInstall(L"Post-" + mode + L" validation failed.");
    }

    if (rollbackDirectory.has_value()) {
        removeDirectoryIfExists(*rollbackDirectory);
    }

    nlohmann::json payload = {
        { "action", utf8FromWide(mode) },
        { "succeeded", true },
        { "validated", true },
        { "bootstrapperVersion", MASTERCONTROL_BOOTSTRAPPER_VERSION },
        { "installDirectory", installDirectory.string() }
    };

    if (options.jsonOutput) {
        const auto state = *stagedState;
        const auto serviceStatus = queryServiceInstallationStatus();
        const auto uninstallStatus = queryUninstallRegistrationStatus();
        const auto shortcutStatus = queryShortcutInstallationStatus(stagedState);
        const auto firewallStatus = queryFirewallRuleStatus();
        payload["browserUrl"] = state.browserUrl;
        payload["configPath"] = state.configPath;
        payload["dataDirectory"] = state.dataDirectory;
        payload["browserPort"] = state.browserPort;
        payload["beaconPort"] = state.beaconPort;
        payload["serviceManaged"] = state.serviceManaged;
        payload["firewallManaged"] = state.firewallManaged;
        payload["shortcutsManaged"] = state.shortcutsManaged;
        payload["uninstallRegistrationManaged"] = state.uninstallRegistrationManaged;
        payload["serviceRegistered"] = serviceStatus.registered;
        payload["serviceAutoStart"] = serviceStatus.autoStart;
        payload["serviceDelayedAutoStart"] = serviceStatus.delayedAutoStart;
        payload["serviceRecoveryConfigured"] = serviceStatus.recoveryConfigured;
        payload["serviceFailureActionsOnNonCrash"] = serviceStatus.failureActionsOnNonCrash;
        payload["serviceSidUnrestricted"] = serviceStatus.sidUnrestricted;
        payload["serviceRunning"] = serviceStatus.running;
        payload["serviceState"] = serviceStatus.state;
        payload["serviceProcessId"] = serviceStatus.processId;
        payload["serviceBinaryPath"] = serviceStatus.binaryPath;
        payload["uninstallRegistered"] = uninstallStatus.registered;
        payload["shellShortcutPresent"] = shortcutStatus.shellShortcutPresent;
        payload["dashboardShortcutPresent"] = shortcutStatus.dashboardShortcutPresent;
        payload["browserFirewallRulePresent"] = firewallStatus.browserRulePresent;
        payload["beaconFirewallRulePresent"] = firewallStatus.beaconRulePresent;
        writeBootstrapperActionLog(mode, true, installDirectory, options, payload);
        std::cout << payload.dump(2) << '\n';
        return true;
    }

    writeBootstrapperActionLog(mode, true, installDirectory, options, payload);
    const wchar_t* action =
        mode == L"repair" ? L"Repaired" :
        mode == L"upgrade" ? L"Upgraded" :
        L"Installed";
    std::wcout << action
               << L" Master Control Orchestration Server at " << installDirectory.c_str() << L'\n';
    std::wcout << L"Configuration path: " << wideFromUtf8(stagedState->configPath) << L'\n';
    std::wcout << L"Browser URL: " << wideFromUtf8(stagedState->browserUrl) << L'\n';
    return true;
}

bool uninstallApplication(const std::filesystem::path& installDirectory,
                          const IntegrationOptions& options) {
    const auto configuration = ensureConfigurationPresent();
    auto state = readInstallationState(installDirectory);
    if (!state.has_value()) {
        state = buildInstallationState(installDirectory, configuration, options);
    }

    const auto failUninstall = [&](const std::wstring& message) {
        const auto serviceStatus = queryServiceInstallationStatus();
        const auto uninstallStatus = queryUninstallRegistrationStatus();
        const auto shortcutStatus = queryShortcutInstallationStatus(*state);
        const auto firewallStatus = queryFirewallRuleStatus();
        nlohmann::json payload = {
            { "action", "uninstall" },
            { "succeeded", false },
            { "bootstrapperVersion", MASTERCONTROL_BOOTSTRAPPER_VERSION },
            { "installDirectory", installDirectory.string() },
            { "error", utf8FromWide(message) },
            { "purgeInstallDirectory", options.purgeInstallDirectory },
            { "purgeData", options.purgeData },
            { "serviceRegistered", serviceStatus.registered },
            { "serviceRunning", serviceStatus.running },
            { "serviceState", serviceStatus.state },
            { "uninstallRegistered", uninstallStatus.registered },
            { "shellShortcutPresent", shortcutStatus.shellShortcutPresent },
            { "dashboardShortcutPresent", shortcutStatus.dashboardShortcutPresent },
            { "browserFirewallRulePresent", firewallStatus.browserRulePresent },
            { "beaconFirewallRulePresent", firewallStatus.beaconRulePresent }
        };
        writeBootstrapperActionLog(L"uninstall", false, installDirectory, options, payload);

        if (options.jsonOutput) {
            std::cout << payload.dump(2) << '\n';
        } else {
            std::wcerr << message << L'\n';
        }
        return false;
    };

    if (options.manageService && !uninstallService()) {
        return failUninstall(L"Failed to uninstall Windows service.");
    }

    if (options.manageFirewall) {
        removeFirewallRules();
    }

    if (options.manageShortcuts) {
        removeShortcuts(*state);
    }

    if (options.manageUninstallRegistration) {
        unregisterUninstallEntry();
    }

    std::error_code error;
    std::filesystem::remove(installStatePath(installDirectory), error);

    if (options.purgeData) {
        removeDirectoryIfExists(std::filesystem::path(state->dataDirectory));
    }

    if (options.purgeInstallDirectory) {
        const auto currentExecutable = executablePath();
        if (pathIsWithin(currentExecutable, installDirectory)) {
            if (!scheduleDeferredInstallRemoval(installDirectory)) {
                return failUninstall(L"Failed to schedule deferred removal of the installation directory.");
            }
        } else {
            removeDirectoryIfExists(installDirectory);
        }
    }

    const auto serviceStatus = queryServiceInstallationStatus();
    const auto uninstallStatus = queryUninstallRegistrationStatus();
    const auto shortcutStatus = queryShortcutInstallationStatus(*state);
    const auto firewallStatus = queryFirewallRuleStatus();
    std::error_code fileError;
    const auto installStatePresent = std::filesystem::exists(installStatePath(installDirectory), fileError);
    const auto installDirectoryPresent = std::filesystem::exists(installDirectory, fileError);
    const auto dataDirectoryPresent =
        !state->dataDirectory.empty() && std::filesystem::exists(std::filesystem::path(state->dataDirectory), fileError);
    nlohmann::json payload = {
        { "action", "uninstall" },
        { "succeeded", true },
        { "bootstrapperVersion", MASTERCONTROL_BOOTSTRAPPER_VERSION },
        { "installDirectory", installDirectory.string() },
        { "purgeInstallDirectory", options.purgeInstallDirectory },
        { "purgeData", options.purgeData },
        { "installDirectoryPresent", installDirectoryPresent },
        { "dataDirectoryPresent", dataDirectoryPresent },
        { "installStatePresent", installStatePresent },
        { "serviceRegistered", serviceStatus.registered },
        { "serviceRunning", serviceStatus.running },
        { "serviceState", serviceStatus.state },
        { "uninstallRegistered", uninstallStatus.registered },
        { "shellShortcutPresent", shortcutStatus.shellShortcutPresent },
        { "dashboardShortcutPresent", shortcutStatus.dashboardShortcutPresent },
        { "browserFirewallRulePresent", firewallStatus.browserRulePresent },
        { "beaconFirewallRulePresent", firewallStatus.beaconRulePresent }
    };
    writeBootstrapperActionLog(L"uninstall", true, installDirectory, options, payload);

    if (options.jsonOutput) {
        std::cout << payload.dump(2) << '\n';
        return true;
    }

    std::wcout << L"Uninstalled Master Control Orchestration Server integrations.\n";
    if (options.purgeInstallDirectory) {
        std::wcout << L"Install directory removal requested for " << installDirectory.c_str() << L'\n';
    }
    if (options.purgeData) {
        std::wcout << L"ProgramData state removed from " << wideFromUtf8(state->dataDirectory) << L'\n';
    }
    return true;
}

IntegrationOptions parseOptions(int argc, wchar_t* argv[], int startIndex) {
    IntegrationOptions options;
    for (int index = startIndex; index < argc; ++index) {
        const std::wstring_view argument(argv[index]);
        if (argument == L"--run-id") {
            ++index;
        } else if (argument == L"--skip-service") {
            options.manageService = false;
        } else if (argument == L"--skip-firewall") {
            options.manageFirewall = false;
        } else if (argument == L"--skip-shortcuts") {
            options.manageShortcuts = false;
        } else if (argument == L"--skip-uninstall-registration") {
            options.manageUninstallRegistration = false;
        } else if (argument == L"--purge-install-dir") {
            options.purgeInstallDirectory = true;
        } else if (argument == L"--purge-data") {
            options.purgeData = true;
        } else if (argument == L"--json") {
            options.jsonOutput = true;
        }
    }
    return options;
}

std::optional<std::filesystem::path> parseInstallDirectoryArgument(int argc, wchar_t* argv[]) {
    for (int index = 2; index < argc; ++index) {
        const std::wstring_view argument(argv[index]);
        if (argument == L"--run-id") {
            ++index;
            continue;
        }

        if (!argument.empty() && argument.front() != L'-') {
            return std::filesystem::path(argv[index]);
        }
    }
    return std::nullopt;
}

void printUsage() {
    std::wcout
        << L"Usage: MasterControlBootstrapper.exe [detect|preflight|install|repair|upgrade|validate|uninstall] [installDir] [options]\n"
        << L"Options:\n"
        << L"  --skip-service                Skip Windows service registration/start\n"
        << L"  --skip-firewall               Skip firewall rule creation/removal\n"
        << L"  --skip-shortcuts              Skip Start Menu shortcut creation/removal\n"
        << L"  --skip-uninstall-registration Skip Programs and Features registration/removal\n"
        << L"  --purge-install-dir           Remove the install directory during uninstall\n"
        << L"  --purge-data                  Remove ProgramData configuration and state during uninstall\n"
        << L"  --json                        Emit machine-readable JSON for detect, preflight, validate, install, repair, upgrade, and uninstall\n";
}

} // namespace

int wmain(int argc, wchar_t* argv[]) {
    const std::wstring mode = argc > 1 ? argv[1] : L"detect";
    const auto options = parseOptions(argc, argv, 2);
    const auto parsedInstallDirectory = parseInstallDirectoryArgument(argc, argv);
    const auto explicitRunId = MasterControl::InstallerLogSupport::findArgumentValue(argc, argv, L"--run-id");
    MasterControl::InstallerLogSupport::initializeRunId(explicitRunId);

    try {
        const auto installDirectory = parsedInstallDirectory.has_value() ? *parsedInstallDirectory : defaultInstallDirectory();

        if (mode == L"detect") {
            showDetectedEnvironment(options.jsonOutput);
            return 0;
        }

        if (mode == L"preflight") {
            return runPreflight(installDirectory, options) ? 0 : 1;
        }

        if (mode == L"install" || mode == L"repair" || mode == L"upgrade") {
            return installLike(mode, installDirectory, options) ? 0 : 1;
        }

        if (mode == L"validate") {
            return validateInstalledApplication(installDirectory, options.jsonOutput) ? 0 : 1;
        }

        if (mode == L"uninstall") {
            return uninstallApplication(installDirectory, options) ? 0 : 1;
        }

        printUsage();
        return 1;
    } catch (const std::exception& error) {
        const auto installDirectory = parsedInstallDirectory.has_value() ? *parsedInstallDirectory : executableDirectory();
        return reportBootstrapperStartupFailure(mode, installDirectory, options, error.what());
    } catch (...) {
        const auto installDirectory = parsedInstallDirectory.has_value() ? *parsedInstallDirectory : executableDirectory();
        return reportBootstrapperStartupFailure(
            mode,
            installDirectory,
            options,
            "Master Control Bootstrapper encountered an unexpected startup failure.");
    }
}
