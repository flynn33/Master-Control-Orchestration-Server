// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#include "MasterControl/MasterControlRuntime.h"

#include <Windows.h>

#include <iostream>
#include <memory>
#include <stdexcept>

namespace {

constexpr wchar_t kCurrentServiceName[] = L"MasterControlProgram";
constexpr wchar_t kAlternateServiceName[] = L"MasterControlOrchestrationServer";
std::unique_ptr<MasterControl::MasterControlApplication> g_application;
SERVICE_STATUS g_serviceStatus{};
SERVICE_STATUS_HANDLE g_statusHandle = nullptr;
const wchar_t* g_registeredServiceName = kCurrentServiceName;

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
        updateServiceStatus(SERVICE_STOP_PENDING);
        g_application->requestStop();
    }
    return NO_ERROR;
}

BOOL WINAPI consoleControlHandler(const DWORD controlType) {
    if ((controlType == CTRL_C_EVENT || controlType == CTRL_BREAK_EVENT || controlType == CTRL_CLOSE_EVENT) && g_application) {
        g_application->requestStop();
        return TRUE;
    }
    return FALSE;
}

int runConsoleMode() {
    SetConsoleCtrlHandler(consoleControlHandler, TRUE);

    g_application = std::make_unique<MasterControl::MasterControlApplication>();
    if (!g_application->initialize()) {
        throw std::runtime_error("Failed to initialize Master Control Orchestration Server.");
    }

    std::cout << "Master Control Orchestration Server listening at " << g_application->browserUrl() << '\n';
    const int exitCode = g_application->runInteractive();
    g_application->shutdown();
    return exitCode;
}

void runServiceMain() {
    g_statusHandle = RegisterServiceCtrlHandlerExW(g_registeredServiceName, serviceControlHandler, nullptr);
    if (g_statusHandle == nullptr) {
        return;
    }

    updateServiceStatus(SERVICE_START_PENDING);

    g_application = std::make_unique<MasterControl::MasterControlApplication>();
    if (!g_application->initialize()) {
        updateServiceStatus(SERVICE_STOPPED, ERROR_SERVICE_SPECIFIC_ERROR);
        return;
    }

    updateServiceStatus(SERVICE_RUNNING);
    g_application->runInteractive();
    g_application->shutdown();
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

        SERVICE_TABLE_ENTRYW dispatchTable[] = {
            { const_cast<LPWSTR>(kCurrentServiceName), serviceMainCurrent },
            { const_cast<LPWSTR>(kAlternateServiceName), serviceMainAlternate },
            { nullptr, nullptr }
        };

        if (StartServiceCtrlDispatcherW(dispatchTable) == 0) {
            if (GetLastError() == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT) {
                return runConsoleMode();
            }
            return 1;
        }
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }

    return 0;
}
