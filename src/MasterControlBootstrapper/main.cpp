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
    beaconEnabled)

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
                                         const MasterControl::AppConfiguration& configuration) {
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
    DWORD bytesNeeded = 0;
    if (QueryServiceStatusEx(
            service,
            SC_STATUS_PROCESS_INFO,
            reinterpret_cast<LPBYTE>(&status),
            sizeof(status),
            &bytesNeeded) != 0 &&
        status.dwCurrentState == SERVICE_RUNNING) {
        SERVICE_STATUS serviceStatus{};
        ControlService(service, SERVICE_CONTROL_STOP, &serviceStatus);

        for (int attempt = 0; attempt < 60; ++attempt) {
            Sleep(500);
            if (QueryServiceStatusEx(
                    service,
                    SC_STATUS_PROCESS_INFO,
                    reinterpret_cast<LPBYTE>(&status),
                    sizeof(status),
                    &bytesNeeded) == 0 ||
                status.dwCurrentState == SERVICE_STOPPED) {
                break;
            }
        }
    }

    CloseServiceHandle(service);
    CloseServiceHandle(scm);
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

    SERVICE_DESCRIPTIONW description{};
    description.lpDescription = const_cast<LPWSTR>(L"Forsetti-native control service for MCP servers, sub-agents, telemetry, and browser administration.");
    ChangeServiceConfig2W(service, SERVICE_CONFIG_DESCRIPTION, &description);

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
    DWORD bytesNeeded = 0;
    bool success = QueryServiceStatusEx(
                       service,
                       SC_STATUS_PROCESS_INFO,
                       reinterpret_cast<LPBYTE>(&status),
                       sizeof(status),
                       &bytesNeeded) != 0 &&
        status.dwCurrentState == SERVICE_RUNNING;

    if (!success) {
        success = StartServiceW(service, 0, nullptr) != 0 || GetLastError() == ERROR_SERVICE_ALREADY_RUNNING;
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

bool validateInstalledApplication(const std::filesystem::path& installDirectory, const bool jsonOutput) {
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

    checkFile(installDirectory / "MasterControlServiceHost.exe", L"Service host");
    checkFile(installDirectory / "MasterControlShell.exe", L"Shell host");
    checkFile(installDirectory / "MasterControlBootstrapper.exe", L"Bootstrapper");
    checkDirectory(installDirectory / "share" / "MasterControlProgram" / "ForsettiManifests", L"Forsetti manifest directory");
    checkFile(installDirectory / "share" / "MasterControlProgram" / "ForsettiManifests" / "DashboardUIModule.json", L"Dashboard UI manifest");
    checkDirectory(installDirectory / "share" / "MasterControlProgram" / "web", L"Web asset directory");
    checkFile(installDirectory / "share" / "MasterControlProgram" / "web" / "index.html", L"Browser dashboard asset");

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
    }

    if (jsonOutput) {
        nlohmann::json payload = {
            { "valid", issues.empty() },
            { "installDirectory", installDirectory.string() },
            { "issues", nlohmann::json::array() }
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
        }

        std::cout << payload.dump(2) << '\n';
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
        const nlohmann::json payload = {
            { "detected", true },
            { "hostName", environment.hostName },
            { "operatingSystem", environment.operatingSystem },
            { "preferredBindAddress", environment.preferredBindAddress },
            { "macAddress", environment.macAddress },
            { "configurationPath", paths.configurationFile.string() },
            { "dataDirectory", paths.dataDirectory.string() },
            { "defaultBrowserPort", configuration.browserPort },
            { "defaultBeaconPort", configuration.beaconPort },
            { "seededBladeEndpoints", configuration.activeProfile.seededEndpoints.size() }
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

bool installLike(const std::wstring& mode,
                 const std::filesystem::path& installDirectory,
                 const IntegrationOptions& options) {
    const auto payloadLayout = resolvePayloadLayout();
    std::filesystem::create_directories(installDirectory);

    if (options.manageService) {
        stopServiceIfPresent();
    }

    stagePayload(payloadLayout, installDirectory);
    const auto configuration = ensureConfigurationPresent();
    const auto state = buildInstallationState(installDirectory, configuration);
    if (!writeInstallationState(state, installDirectory)) {
        std::wcerr << L"Failed to write installation state.\n";
        return false;
    }

    if (options.manageFirewall && !configureFirewallRules(state)) {
        std::wcerr << L"Failed to configure firewall rules.\n";
        return false;
    }

    if (options.manageShortcuts && !configureShortcuts(state)) {
        std::wcerr << L"Failed to create shortcuts.\n";
        return false;
    }

    if (options.manageUninstallRegistration && !registerUninstallEntry(state)) {
        std::wcerr << L"Failed to register the uninstall entry.\n";
        return false;
    }

    if (options.manageService) {
        if (!installOrUpdateService(installDirectory / "MasterControlServiceHost.exe")) {
            std::wcerr << L"Failed to install Windows service.\n";
            return false;
        }
        if (!startServiceIfPresent()) {
            std::wcerr << L"Failed to start Windows service.\n";
            return false;
        }
    }

    const wchar_t* action =
        mode == L"repair" ? L"Repaired" :
        mode == L"upgrade" ? L"Upgraded" :
        L"Installed";
    std::wcout << action
               << L" Master Control Program at " << installDirectory.c_str() << L'\n';
    std::wcout << L"Configuration path: " << wideFromUtf8(state.configPath) << L'\n';
    std::wcout << L"Browser URL: " << wideFromUtf8(state.browserUrl) << L'\n';
    return true;
}

bool uninstallApplication(const std::filesystem::path& installDirectory,
                          const IntegrationOptions& options) {
    const auto configuration = ensureConfigurationPresent();
    const auto state = readInstallationState(installDirectory).value_or(buildInstallationState(installDirectory, configuration));

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
        << L"Usage: MasterControlBootstrapper.exe [detect|install|repair|upgrade|validate|uninstall] [installDir] [options]\n"
        << L"Options:\n"
        << L"  --skip-service                Skip Windows service registration/start\n"
        << L"  --skip-firewall               Skip firewall rule creation/removal\n"
        << L"  --skip-shortcuts              Skip Start Menu shortcut creation/removal\n"
        << L"  --skip-uninstall-registration Skip Programs and Features registration/removal\n"
        << L"  --purge-install-dir           Remove the install directory during uninstall\n"
        << L"  --purge-data                  Remove ProgramData configuration and state during uninstall\n"
        << L"  --json                        Emit machine-readable JSON for detect and validate\n";
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
