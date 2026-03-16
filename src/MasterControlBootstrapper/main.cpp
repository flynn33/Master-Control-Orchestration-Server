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
#include <string>
#include <vector>

namespace {

constexpr wchar_t kServiceName[] = L"MasterControlProgram";

std::filesystem::path executableDirectory() {
    wchar_t buffer[MAX_PATH]{};
    const auto length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    return std::filesystem::path(std::wstring(buffer, length)).parent_path();
}

std::filesystem::path defaultInstallDirectory() {
    PWSTR programFilesPath = nullptr;
    SHGetKnownFolderPath(FOLDERID_ProgramFiles, KF_FLAG_DEFAULT, nullptr, &programFilesPath);
    std::filesystem::path path(programFilesPath);
    CoTaskMemFree(programFilesPath);
    return path / "Master Control Program";
}

void copyRecursive(const std::filesystem::path& source, const std::filesystem::path& destination) {
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

bool installService(const std::filesystem::path& serviceBinary) {
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

    SERVICE_STATUS_PROCESS status{};
    DWORD bytesNeeded = 0;
    const bool queried = QueryServiceStatusEx(
        service,
        SC_STATUS_PROCESS_INFO,
        reinterpret_cast<LPBYTE>(&status),
        sizeof(status),
        &bytesNeeded) != 0;
    if (!queried || status.dwCurrentState == SERVICE_STOPPED) {
        StartServiceW(service, 0, nullptr);
    }

    CloseServiceHandle(service);
    CloseServiceHandle(scm);
    return true;
}

bool uninstallService() {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (scm == nullptr) {
        return false;
    }

    SC_HANDLE service = OpenServiceW(scm, kServiceName, SERVICE_STOP | DELETE | SERVICE_QUERY_STATUS);
    if (service == nullptr) {
        CloseServiceHandle(scm);
        return false;
    }

    SERVICE_STATUS status{};
    ControlService(service, SERVICE_CONTROL_STOP, &status);
    DeleteService(service);
    CloseServiceHandle(service);
    CloseServiceHandle(scm);
    return true;
}

void showDetectedEnvironment() {
    const auto paths = MasterControl::resolveAppPaths();
    const auto environment = MasterControl::detectLocalEnvironment();
    const auto configuration = MasterControl::buildDefaultConfiguration();
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

void seedConfigurationIfMissing() {
    const auto paths = MasterControl::resolveAppPaths();
    if (std::filesystem::exists(paths.configurationFile)) {
        return;
    }

    std::filesystem::create_directories(paths.configurationFile.parent_path());
    std::ofstream output(paths.configurationFile, std::ios::trunc);
    output << nlohmann::json(MasterControl::buildDefaultConfiguration()).dump(2);
}

} // namespace

int wmain(int argc, wchar_t* argv[]) {
    std::wstring mode = argc > 1 ? argv[1] : L"detect";
    const auto sourceDirectory = executableDirectory();
    const auto installDirectory = argc > 2 ? std::filesystem::path(argv[2]) : defaultInstallDirectory();

    if (mode == L"detect") {
        showDetectedEnvironment();
        return 0;
    }

    if (mode == L"install") {
        std::filesystem::create_directories(installDirectory);
        copyRecursive(sourceDirectory, installDirectory);
        seedConfigurationIfMissing();
        if (!installService(installDirectory / "MasterControlServiceHost.exe")) {
            std::wcerr << L"Failed to install Windows service.\n";
            return 1;
        }
        std::wcout << L"Installed Master Control Program to " << installDirectory.c_str() << L'\n';
        std::wcout << L"Seeded configuration at " << MasterControl::resolveAppPaths().configurationFile.c_str() << L'\n';
        return 0;
    }

    if (mode == L"uninstall") {
        uninstallService();
        std::wcout << L"Uninstalled Master Control Program service.\n";
        return 0;
    }

    std::wcout << L"Usage: MasterControlBootstrapper.exe [detect|install|uninstall] [installDir]\n";
    return 1;
}
