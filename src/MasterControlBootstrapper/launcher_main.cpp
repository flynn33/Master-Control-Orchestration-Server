// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#include <Windows.h>

#include <filesystem>
#include <string>

namespace {

constexpr wchar_t kShellBinaryName[] = L"MasterControlShell.exe";

std::wstring quoteWindowsArgument(const std::wstring& argument) {
    std::wstring quoted;
    quoted.push_back(L'"');
    size_t backslashCount = 0;
    for (const wchar_t character : argument) {
        if (character == L'\\') {
            ++backslashCount;
            continue;
        }
        if (character == L'"') {
            quoted.append(backslashCount * 2 + 1, L'\\');
            quoted.push_back(L'"');
            backslashCount = 0;
            continue;
        }
        if (backslashCount > 0) {
            quoted.append(backslashCount, L'\\');
            backslashCount = 0;
        }
        quoted.push_back(character);
    }
    if (backslashCount > 0) {
        quoted.append(backslashCount * 2, L'\\');
    }
    quoted.push_back(L'"');
    return quoted;
}

std::wstring currentExecutablePath() {
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            return {};
        }
        if (length < buffer.size() - 1) {
            buffer.resize(length);
            return buffer;
        }
        buffer.resize(buffer.size() * 2);
    }
}

int showLaunchError(const std::wstring& message) {
    MessageBoxW(
        nullptr,
        message.c_str(),
        L"Master Control Orchestration Server",
        MB_ICONERROR | MB_OK | MB_SETFOREGROUND);
    return 1;
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    const auto launcherPathText = currentExecutablePath();
    if (launcherPathText.empty()) {
        return showLaunchError(L"Windows could not resolve the installed application path.");
    }

    const std::filesystem::path launcherPath(launcherPathText);
    const auto installDirectory = launcherPath.parent_path();
    const auto shellPath = installDirectory / kShellBinaryName;
    if (!std::filesystem::exists(shellPath)) {
        return showLaunchError(
            L"MasterControlShell.exe was not found beside the installed launcher.\n\n"
            L"Repair or reinstall Master Control Orchestration Server.");
    }

    std::wstring commandLine = quoteWindowsArgument(shellPath.wstring());
    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo{};
    const std::wstring workingDirectory = installDirectory.wstring();
    if (!CreateProcessW(
            shellPath.c_str(),
            commandLine.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_UNICODE_ENVIRONMENT,
            nullptr,
            workingDirectory.empty() ? nullptr : workingDirectory.c_str(),
            &startupInfo,
            &processInfo)) {
        const DWORD lastError = GetLastError();
        return showLaunchError(
            L"Windows could not start the installed application (error " +
            std::to_wstring(lastError) + L").");
    }

    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    return 0;
}
