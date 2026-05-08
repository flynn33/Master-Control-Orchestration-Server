// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#include "MasterControl/MasterControlRuntime.h"
#include "../../include/MasterControl/DeploymentLogPaths.h"

#include <Windows.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <sstream>

namespace {

constexpr wchar_t kCurrentServiceName[] = L"MasterControlProgram";
constexpr wchar_t kAlternateServiceName[] = L"MasterControlOrchestrationServer";
std::unique_ptr<MasterControl::MasterControlApplication> g_application;
SERVICE_STATUS g_serviceStatus{};
SERVICE_STATUS_HANDLE g_statusHandle = nullptr;
const wchar_t* g_registeredServiceName = kCurrentServiceName;

void writeServiceLog(const std::wstring& message) {
    try {
        wchar_t buffer[MAX_PATH]{};
        const auto length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
        const auto executableDirectory = std::filesystem::path(std::wstring(buffer, length)).parent_path();
        const auto paths = MasterControl::DeploymentLogPaths::build(executableDirectory);

        const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        tm localTime{};
        localtime_s(&localTime, &now);

        std::wostringstream line;
        line << std::put_time(&localTime, L"%Y-%m-%d %H:%M:%S") << L"  " << message << std::endl;
        (void)MasterControl::DeploymentLogPaths::appendComponentLog(
            paths,
            paths.serviceLatest,
            paths.serviceSessionLog,
            line.str());
    } catch (...) {
    }
}

void updateServiceStatus(const DWORD currentState, const DWORD win32ExitCode = NO_ERROR) {
    g_serviceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_serviceStatus.dwCurrentState = currentState;
    g_serviceStatus.dwControlsAccepted = (currentState == SERVICE_RUNNING) ? SERVICE_ACCEPT_STOP : 0;
    g_serviceStatus.dwWin32ExitCode = win32ExitCode;
    g_serviceStatus.dwWaitHint = 1000;
    SetServiceStatus(g_statusHandle, &g_serviceStatus);
}

DWORD WINAPI serviceControlHandler(const DWORD control, const DWORD, LPVOID, LPVOID) {
    if (control == SERVICE_CONTROL_STOP && g_application) {
        writeServiceLog(L"Service control handler received stop request.");
        updateServiceStatus(SERVICE_STOP_PENDING);
        g_application->requestStop();
    }
    return NO_ERROR;
}

BOOL WINAPI consoleControlHandler(const DWORD controlType) {
    if ((controlType == CTRL_C_EVENT || controlType == CTRL_BREAK_EVENT || controlType == CTRL_CLOSE_EVENT) && g_application) {
        writeServiceLog(L"Console control handler requested shutdown.");
        g_application->requestStop();
        return TRUE;
    }
    return FALSE;
}

// v0.9.3: process-singleton guard. Pre-v0.9.3 a second MasterControlServiceHost
// invocation -- typically via `--console` while the Windows service was
// already running -- silently failed to bind 7300/8080 but its
// WorkerSupervisor still spawned the baseline-tools worker, doubling
// the child-process count per host and confusing the dashboard.
// CreateMutexW with a Global\ name held for the lifetime of the process
// makes the second instance refuse to start. Service-mode and console-
// mode share the same mutex name so either path keeps the other out.
HANDLE g_singletonMutex = nullptr;

bool acquireSingletonOrFail(const wchar_t* mode) {
    g_singletonMutex = CreateMutexW(
        nullptr, TRUE, L"Global\\MasterControlOrchestrationServerSingleton");
    if (g_singletonMutex == nullptr) {
        writeServiceLog(std::wstring(L"CreateMutexW failed in ") + mode + L" mode.");
        return false;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        std::wstring msg = L"Master Control Orchestration Server is already running. ";
        msg += L"Refusing to start a duplicate instance from ";
        msg += mode;
        msg += L" mode. Stop the existing service before launching another.";
        writeServiceLog(msg);
        std::cerr << "Master Control Orchestration Server is already running. "
                     "Refusing to start a duplicate instance.\n";
        CloseHandle(g_singletonMutex);
        g_singletonMutex = nullptr;
        return false;
    }
    return true;
}

void releaseSingleton() {
    if (g_singletonMutex != nullptr) {
        ReleaseMutex(g_singletonMutex);
        CloseHandle(g_singletonMutex);
        g_singletonMutex = nullptr;
    }
}

int runConsoleMode() {
    writeServiceLog(L"Starting service host in console mode.");
    SetConsoleCtrlHandler(consoleControlHandler, TRUE);

    if (!acquireSingletonOrFail(L"console")) {
        return 2; // distinct exit code for "duplicate instance refused"
    }

    g_application = std::make_unique<MasterControl::MasterControlApplication>();
    if (!g_application->initialize()) {
        writeServiceLog(L"Console mode initialization failed.");
        releaseSingleton();
        throw std::runtime_error("Failed to initialize Master Control Orchestration Server.");
    }

    writeServiceLog(L"Console mode initialized successfully.");
    std::cout << "Master Control Orchestration Server listening at " << g_application->browserUrl() << '\n';
    const int exitCode = g_application->runInteractive();
    writeServiceLog(L"Console mode run loop exited with code " + std::to_wstring(exitCode) + L".");
    g_application->shutdown();
    releaseSingleton();
    writeServiceLog(L"Console mode shutdown completed.");
    return exitCode;
}

void runServiceMain() {
    writeServiceLog(L"Starting service host in Windows service mode.");
    g_statusHandle = RegisterServiceCtrlHandlerExW(g_registeredServiceName, serviceControlHandler, nullptr);
    if (g_statusHandle == nullptr) {
        writeServiceLog(L"RegisterServiceCtrlHandlerExW failed.");
        return;
    }

    updateServiceStatus(SERVICE_START_PENDING);

    if (!acquireSingletonOrFail(L"service")) {
        updateServiceStatus(SERVICE_STOPPED, ERROR_SERVICE_ALREADY_RUNNING);
        return;
    }

    g_application = std::make_unique<MasterControl::MasterControlApplication>();
    if (!g_application->initialize()) {
        writeServiceLog(L"Service mode initialization failed.");
        releaseSingleton();
        updateServiceStatus(SERVICE_STOPPED, ERROR_SERVICE_SPECIFIC_ERROR);
        return;
    }

    writeServiceLog(L"Service mode initialized successfully.");
    updateServiceStatus(SERVICE_RUNNING);
    g_application->runInteractive();
    writeServiceLog(L"Service mode run loop exited.");
    g_application->shutdown();
    releaseSingleton();
    writeServiceLog(L"Service mode shutdown completed.");
    updateServiceStatus(SERVICE_STOPPED);
}

void WINAPI serviceMainCurrent(DWORD, LPWSTR*) {
    g_registeredServiceName = kCurrentServiceName;
    runServiceMain();
}

void WINAPI serviceMainAlternate(DWORD, LPWSTR*) {
    g_registeredServiceName = kAlternateServiceName;
    runServiceMain();
}

} // namespace

int wmain(int argc, wchar_t* argv[]) {
    try {
        if (argc > 1 && wcscmp(argv[1], L"--console") == 0) {
            return runConsoleMode();
        }

        writeServiceLog(L"Attempting to connect to the Windows service control dispatcher.");
        SERVICE_TABLE_ENTRYW dispatchTable[] = {
            { const_cast<LPWSTR>(kCurrentServiceName), serviceMainCurrent },
            { const_cast<LPWSTR>(kAlternateServiceName), serviceMainAlternate },
            { nullptr, nullptr }
        };

        if (StartServiceCtrlDispatcherW(dispatchTable) == 0) {
            if (GetLastError() == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT) {
                writeServiceLog(L"Service control dispatcher unavailable; falling back to console mode.");
                return runConsoleMode();
            }
            writeServiceLog(L"StartServiceCtrlDispatcherW failed.");
            return 1;
        }
    } catch (const std::exception& exception) {
        int required = MultiByteToWideChar(CP_UTF8, 0, exception.what(), -1, nullptr, 0);
        std::wstring message;
        if (required > 1) {
            std::wstring buffer(static_cast<size_t>(required), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, exception.what(), -1, buffer.data(), required);
            buffer.resize(static_cast<size_t>(required - 1));
            message = std::move(buffer);
        }
        writeServiceLog(L"Unhandled service host exception: " + message);
        std::cerr << exception.what() << '\n';
        return 1;
    }

    return 0;
}
