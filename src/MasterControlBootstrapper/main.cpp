// Master Control Program
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#include "MasterControl/MasterControlDefaults.h"

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
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

constexpr wchar_t kServiceName[] = L"MasterControlProgram";
constexpr wchar_t kUninstallRegistryKey[] = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\MasterControlProgram";
constexpr wchar_t kProgramsFolderName[] = L"Master Control Program";
constexpr wchar_t kShellShortcutName[] = L"Master Control Program.lnk";
constexpr wchar_t kDashboardShortcutName[] = L"Master Control Dashboard.url";
constexpr wchar_t kInstallStateFileName[] = L"installation-state.json";
constexpr wchar_t kBrowserRuleName[] = L"Master Control Program - Browser Access";
constexpr wchar_t kBeaconRuleName[] = L"Master Control Program - Beacon Discovery";

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

std::filesystem::path defaultInstallDirectory() {
    PWSTR programFilesPath = nullptr;
    SHGetKnownFolderPath(FOLDERID_ProgramFiles, KF_FLAG_DEFAULT, nullptr, &programFilesPath);
    std::filesystem::path path(programFilesPath);
    CoTaskMemFree(programFilesPath);
    return path / "Master Control Program";
}

std::filesystem::path knownFolder(REFKNOWNFOLDERID folderId) {
    PWSTR path = nullptr;
    const HRESULT result = SHGetKnownFolderPath(folderId, KF_FLAG_DEFAULT, nullptr, &path);
    if (FAILED(result) || path == nullptr) {
        throw std::runtime_error("Failed to resolve a known folder path.");
    }

    std::filesystem::path output(path);
    CoTaskMemFree(path);
    return output;
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
            std::filesystem::copy_file(entry.path(), targetPath, std::filesystem::copy_options::overwrite_existing);
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

bool environmentFlagEnabled(const wchar_t* name) {
    const auto requiredCharacters = GetEnvironmentVariableW(name, nullptr, 0);
    if (requiredCharacters == 0) {
        return false;
    }

    std::wstring value(static_cast<size_t>(requiredCharacters), L'\0');
    GetEnvironmentVariableW(name, value.data(), requiredCharacters);
    if (!value.empty() && value.back() == L'\0') {
        value.pop_back();
    }

    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](const wchar_t character) { return static_cast<wchar_t>(std::towlower(character)); });
    return !value.empty() &&
        value != L"0" &&
        value != L"false" &&
        value != L"off" &&
        value != L"no";
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
    const auto flatShare = currentDirectory / "share" / "MasterControlProgram";
    if (std::filesystem::exists(currentDirectory / "MasterControlServiceHost.exe") &&
        std::filesystem::exists(currentDirectory / "MasterControlShell.exe") &&
        std::filesystem::exists(flatShare / "ForsettiManifests") &&
        std::filesystem::exists(flatShare / "web")) {
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

    throw std::runtime_error("Unable to resolve an installable Master Control Program payload from the current bootstrapper location.");
}

void stagePayload(const PayloadLayout& layout, const std::filesystem::path& installDirectory) {
    if (layout.flatPayload) {
        copyRecursive(layout.flatRoot, installDirectory);
        return;
    }

    copyRecursive(layout.bootstrapperDirectory, installDirectory);
    copyRecursive(layout.serviceDirectory, installDirectory);
    copyRecursive(layout.shellDirectory, installDirectory);
    copyRecursive(layout.manifestsDirectory, installDirectory / "share" / "MasterControlProgram" / "ForsettiManifests");
    copyRecursive(layout.webDirectory, installDirectory / "share" / "MasterControlProgram" / "web");
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
    const auto shortcutDirectory = knownFolder(FOLDERID_CommonPrograms) / kProgramsFolderName;
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
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kUninstallRegistryKey, 0, KEY_READ, &key) != ERROR_SUCCESS) {
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
    if (RegCreateKeyExW(
            HKEY_LOCAL_MACHINE,
            kUninstallRegistryKey,
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
        setRegistryStringValue(key, L"DisplayName", L"Master Control Program") &&
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
    const auto result = RegDeleteTreeW(HKEY_LOCAL_MACHINE, kUninstallRegistryKey);
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
        L"$shortcut.Description = 'Master Control Program administrative shell'; " \
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

    SC_HANDLE service = OpenServiceW(scm, kServiceName, SERVICE_STOP | SERVICE_QUERY_STATUS);
    if (service == nullptr) {
        CloseServiceHandle(scm);
        return true;
    }

    SERVICE_STATUS_PROCESS status{};
    if (queryServiceProcessStatus(service, status) && status.dwCurrentState == SERVICE_RUNNING) {
        SERVICE_STATUS serviceStatus{};
        ControlService(service, SERVICE_CONTROL_STOP, &serviceStatus);
        waitForServiceState(service, SERVICE_STOPPED, 30000);
    }

    CloseServiceHandle(service);
    CloseServiceHandle(scm);
    return true;
}

ServiceInstallationStatus queryServiceInstallationStatus() {
    ServiceInstallationStatus status;

    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (scm == nullptr) {
        return status;
    }

    SC_HANDLE service = OpenServiceW(scm, kServiceName, SERVICE_QUERY_CONFIG | SERVICE_QUERY_STATUS);
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

    const std::wstring binaryPath = L"\"" + serviceBinary.wstring() + L"\"";
    SC_HANDLE service = CreateServiceW(
        scm,
        kServiceName,
        L"Master Control Program",
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
        service = OpenServiceW(scm, kServiceName, SERVICE_ALL_ACCESS);
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

    SC_HANDLE service = OpenServiceW(scm, kServiceName, SERVICE_START | SERVICE_QUERY_STATUS);
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

    SC_HANDLE service = OpenServiceW(scm, kServiceName, SERVICE_STOP | DELETE | SERVICE_QUERY_STATUS);
    if (service == nullptr) {
        CloseServiceHandle(scm);
        return true;
    }

    SERVICE_STATUS status{};
    ControlService(service, SERVICE_CONTROL_STOP, &status);
    const bool success = DeleteService(service) != 0 || GetLastError() == ERROR_SERVICE_MARKED_FOR_DELETE;
    CloseServiceHandle(service);
    CloseServiceHandle(scm);
    return success;
}

bool scheduleDeferredInstallRemoval(const std::filesystem::path& installDirectory) {
    const auto scriptPath = std::filesystem::temp_directory_path() / "MasterControlProgram-uninstall.cmd";
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
    checkDirectory(installDirectory / "share" / "MasterControlProgram" / "ForsettiManifests", L"Forsetti manifest directory");
    checkFile(installDirectory / "share" / "MasterControlProgram" / "ForsettiManifests" / "DashboardUIModule.json", L"Dashboard UI manifest");
    checkDirectory(installDirectory / "share" / "MasterControlProgram" / "web", L"Web asset directory");
    checkFile(installDirectory / "share" / "MasterControlProgram" / "web" / "index.html", L"Browser dashboard asset");

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
        std::wcerr << L"Master Control Program installation validation failed for "
                   << installDirectory.c_str() << L":\n";
        for (const auto& issue : issues) {
            std::wcerr << L"  - " << issue << L'\n';
        }
        return false;
    }

    std::wcout << L"Validated Master Control Program installation at "
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
            { "seededBladeEndpoints", configuration.activeProfile.seededEndpoints.size() },
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
    std::cout << "Seeded BLADE endpoints: " << configuration.activeProfile.seededEndpoints.size() << '\n';
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
            const auto shortcutDirectory = knownFolder(FOLDERID_CommonPrograms) / kProgramsFolderName;
            if (shortcutDirectory.empty()) {
                appendIssue(L"Common Programs shortcut folder could not be resolved.");
            }
        } catch (const std::exception& error) {
            appendIssue(L"Common Programs shortcut folder could not be resolved: " + wideFromUtf8(error.what()));
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
        std::wcerr << L"Master Control Program preflight failed for "
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

        if (options.jsonOutput) {
            const auto currentState = readInstallationState(installDirectory);
            const auto serviceStatus = queryServiceInstallationStatus();
            const auto uninstallStatus = queryUninstallRegistrationStatus();
            const auto shortcutStatus = queryShortcutInstallationStatus(currentState);
            const auto firewallStatus = queryFirewallRuleStatus();
            nlohmann::json payload = {
                { "action", utf8FromWide(mode) },
                { "succeeded", false },
                { "bootstrapperVersion", MASTERCONTROL_BOOTSTRAPPER_VERSION },
                { "installDirectory", installDirectory.string() },
                { "error", utf8FromWide(message) },
                { "rollbackAttempted", rollbackAttempted },
                { "rollbackRestored", rollbackRestored },
                { "installStatePresent", currentState.has_value() },
                { "serviceRegistered", serviceStatus.registered },
                { "serviceRunning", serviceStatus.running },
                { "serviceState", serviceStatus.state },
                { "uninstallRegistered", uninstallStatus.registered },
                { "shellShortcutPresent", shortcutStatus.shellShortcutPresent },
                { "dashboardShortcutPresent", shortcutStatus.dashboardShortcutPresent },
                { "browserFirewallRulePresent", firewallStatus.browserRulePresent },
                { "beaconFirewallRulePresent", firewallStatus.beaconRulePresent }
            };
            if (rollbackDirectory.has_value() && !rollbackRestored) {
                payload["rollbackSnapshotPath"] = rollbackDirectory->string();
            }
            std::cout << payload.dump(2) << '\n';
        } else {
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

    if (options.jsonOutput) {
        const auto state = *stagedState;
        const auto serviceStatus = queryServiceInstallationStatus();
        const auto uninstallStatus = queryUninstallRegistrationStatus();
        const auto shortcutStatus = queryShortcutInstallationStatus(stagedState);
        const auto firewallStatus = queryFirewallRuleStatus();
        nlohmann::json payload = {
            { "action", utf8FromWide(mode) },
            { "succeeded", true },
            { "validated", true },
            { "bootstrapperVersion", MASTERCONTROL_BOOTSTRAPPER_VERSION },
            { "installDirectory", installDirectory.string() },
            { "browserUrl", state.browserUrl },
            { "configPath", state.configPath },
            { "dataDirectory", state.dataDirectory },
            { "browserPort", state.browserPort },
            { "beaconPort", state.beaconPort },
            { "serviceManaged", state.serviceManaged },
            { "firewallManaged", state.firewallManaged },
            { "shortcutsManaged", state.shortcutsManaged },
            { "uninstallRegistrationManaged", state.uninstallRegistrationManaged },
            { "serviceRegistered", serviceStatus.registered },
            { "serviceAutoStart", serviceStatus.autoStart },
            { "serviceDelayedAutoStart", serviceStatus.delayedAutoStart },
            { "serviceRecoveryConfigured", serviceStatus.recoveryConfigured },
            { "serviceFailureActionsOnNonCrash", serviceStatus.failureActionsOnNonCrash },
            { "serviceSidUnrestricted", serviceStatus.sidUnrestricted },
            { "serviceRunning", serviceStatus.running },
            { "serviceState", serviceStatus.state },
            { "serviceProcessId", serviceStatus.processId },
            { "serviceBinaryPath", serviceStatus.binaryPath },
            { "uninstallRegistered", uninstallStatus.registered },
            { "shellShortcutPresent", shortcutStatus.shellShortcutPresent },
            { "dashboardShortcutPresent", shortcutStatus.dashboardShortcutPresent },
            { "browserFirewallRulePresent", firewallStatus.browserRulePresent },
            { "beaconFirewallRulePresent", firewallStatus.beaconRulePresent }
        };
        std::cout << payload.dump(2) << '\n';
        return true;
    }

    const wchar_t* action =
        mode == L"repair" ? L"Repaired" :
        mode == L"upgrade" ? L"Upgraded" :
        L"Installed";
    std::wcout << action
               << L" Master Control Program at " << installDirectory.c_str() << L'\n';
    std::wcout << L"Configuration path: " << wideFromUtf8(stagedState->configPath) << L'\n';
    std::wcout << L"Browser URL: " << wideFromUtf8(stagedState->browserUrl) << L'\n';
    return true;
}

bool uninstallApplication(const std::filesystem::path& installDirectory,
                          const IntegrationOptions& options) {
    const auto configuration = ensureConfigurationPresent();
    const auto state = readInstallationState(installDirectory).value_or(buildInstallationState(
        installDirectory,
        configuration,
        options));

    if (options.manageService && !uninstallService()) {
        std::wcerr << L"Failed to uninstall Windows service.\n";
        return false;
    }

    if (options.manageFirewall) {
        removeFirewallRules();
    }

    if (options.manageShortcuts) {
        removeShortcuts(state);
    }

    if (options.manageUninstallRegistration) {
        unregisterUninstallEntry();
    }

    std::error_code error;
    std::filesystem::remove(installStatePath(installDirectory), error);

    if (options.purgeData) {
        removeDirectoryIfExists(std::filesystem::path(state.dataDirectory));
    }

    if (options.purgeInstallDirectory) {
        const auto currentExecutable = executablePath();
        if (pathIsWithin(currentExecutable, installDirectory)) {
            if (!scheduleDeferredInstallRemoval(installDirectory)) {
                std::wcerr << L"Failed to schedule deferred removal of the installation directory.\n";
                return false;
            }
        } else {
            removeDirectoryIfExists(installDirectory);
        }
    }

    if (options.jsonOutput) {
        const auto serviceStatus = queryServiceInstallationStatus();
        const auto uninstallStatus = queryUninstallRegistrationStatus();
        const auto shortcutStatus = queryShortcutInstallationStatus(state);
        const auto firewallStatus = queryFirewallRuleStatus();
        std::error_code fileError;
        const auto installStatePresent = std::filesystem::exists(installStatePath(installDirectory), fileError);
        const auto installDirectoryPresent = std::filesystem::exists(installDirectory, fileError);
        const auto dataDirectoryPresent =
            !state.dataDirectory.empty() && std::filesystem::exists(std::filesystem::path(state.dataDirectory), fileError);
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
        std::cout << payload.dump(2) << '\n';
        return true;
    }

    std::wcout << L"Uninstalled Master Control Program integrations.\n";
    if (options.purgeInstallDirectory) {
        std::wcout << L"Install directory removal requested for " << installDirectory.c_str() << L'\n';
    }
    if (options.purgeData) {
        std::wcout << L"ProgramData state removed from " << wideFromUtf8(state.dataDirectory) << L'\n';
    }
    return true;
}

IntegrationOptions parseOptions(int argc, wchar_t* argv[], int startIndex) {
    IntegrationOptions options;
    for (int index = startIndex; index < argc; ++index) {
        const std::wstring_view argument(argv[index]);
        if (argument == L"--skip-service") {
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
    const auto installDirectory = parseInstallDirectoryArgument(argc, argv).value_or(defaultInstallDirectory());
    const auto options = parseOptions(argc, argv, 2);

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
}
