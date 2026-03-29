// Master Control Program
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#include <ShlObj.h>
#include <Windows.h>
#include <shellapi.h>

#include <filesystem>
#include <fstream>
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
constexpr wchar_t kDefaultInstallLeaf[] = L"Master Control Program";

struct LauncherOptions final {
    std::filesystem::path installDirectory;
    bool quiet = false;
    bool autoLaunchShell = false;
    bool noLaunchShell = false;
    std::vector<std::wstring> bootstrapperArguments;
};

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

std::filesystem::path defaultInstallDirectory() {
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

std::filesystem::path desktopDirectory() {
    if (const auto overridePath = readEnvironmentVariable(kLogDirectoryEnv); overridePath.has_value() &&
                                                                          !overridePath->empty()) {
        return std::filesystem::path(*overridePath);
    }

    if (const auto folder = tryKnownFolder(FOLDERID_Desktop); folder.has_value()) {
        return *folder;
    }

    return executableDirectory();
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
    return desktopDirectory() / std::filesystem::path(L"MasterControlProgram-setup-launcher-" + timestamp + L".txt");
}

bool writeLauncherLog(const std::filesystem::path& logPath,
                      const LauncherOptions& options,
                      const bool administrator,
                      const bool requiresElevation,
                      const bool elevationAttempted,
                      const DWORD exitCode,
                      const std::wstring& message,
                      const bool launchedShell) {
    std::error_code error;
    std::filesystem::create_directories(logPath.parent_path(), error);

    std::wofstream output(logPath, std::ios::binary);
    if (!output.is_open()) {
        return false;
    }

    output << L"Master Control Program Setup Launcher\r\n\r\n";
    output << L"GeneratedAt: " << currentTimestamp().c_str() << L"\r\n";
    output << L"BootstrapperPath: " << (executableDirectory() / kBootstrapperName).wstring() << L"\r\n";
    output << L"InstallDirectory: " << options.installDirectory.wstring() << L"\r\n";
    output << L"IsAdministrator: " << (administrator ? L"true" : L"false") << L"\r\n";
    output << L"RequiresElevation: " << (requiresElevation ? L"true" : L"false") << L"\r\n";
    output << L"ElevationAttempted: " << (elevationAttempted ? L"true" : L"false") << L"\r\n";
    output << L"Quiet: " << (options.quiet ? L"true" : L"false") << L"\r\n";
    output << L"LaunchedShell: " << (launchedShell ? L"true" : L"false") << L"\r\n";
    output << L"ExitCode: " << exitCode << L"\r\n";
    output << L"Arguments: " << joinArguments(options.bootstrapperArguments) << L"\r\n";
    output << L"\r\nMessage\r\n-------\r\n";
    output << message << L"\r\n";
    return true;
}

std::wstring usageText() {
    return
        L"Usage:\n"
        L"  MasterControlSetup.exe [install-directory] [options]\n\n"
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
    }

    options.bootstrapperArguments.emplace_back(options.installDirectory.wstring());
    return options;
}

bool launchProcess(const std::filesystem::path& executable,
                   const std::vector<std::wstring>& arguments,
                   const bool elevate,
                   DWORD& exitCode,
                   std::wstring& errorMessage) {
    SHELLEXECUTEINFOW executeInfo{};
    executeInfo.cbSize = sizeof(executeInfo);
    executeInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
    executeInfo.lpVerb = elevate ? L"runas" : L"open";
    executeInfo.lpFile = executable.c_str();

    const auto argumentLine = joinArguments(arguments);
    executeInfo.lpParameters = argumentLine.c_str();
    executeInfo.lpDirectory = executable.parent_path().c_str();
    executeInfo.nShow = SW_HIDE;

    if (!ShellExecuteExW(&executeInfo)) {
        const DWORD error = GetLastError();
        exitCode = error;
        errorMessage = formatWindowsMessage(error);
        return false;
    }

    const auto waitResult = WaitForSingleObject(executeInfo.hProcess, INFINITE);
    if (waitResult != WAIT_OBJECT_0) {
        const DWORD error = GetLastError();
        exitCode = error;
        errorMessage = formatWindowsMessage(error);
        CloseHandle(executeInfo.hProcess);
        return false;
    }

    if (!GetExitCodeProcess(executeInfo.hProcess, &exitCode)) {
        const DWORD error = GetLastError();
        errorMessage = formatWindowsMessage(error);
        CloseHandle(executeInfo.hProcess);
        return false;
    }

    CloseHandle(executeInfo.hProcess);
    return true;
}

void showMessage(const UINT flags, const std::wstring& title, const std::wstring& text) {
    MessageBoxW(nullptr, text.c_str(), title.c_str(), flags | MB_SETFOREGROUND);
}

bool maybeLaunchShell(const LauncherOptions& options) {
    if (options.noLaunchShell) {
        return false;
    }

    bool launchShell = options.autoLaunchShell;
    if (!options.quiet && !launchShell) {
        const int result = MessageBoxW(
            nullptr,
            L"Master Control Program installed successfully.\n\nLaunch the desktop shell now?",
            L"Master Control Program Setup",
            MB_ICONQUESTION | MB_YESNO | MB_SETFOREGROUND);
        launchShell = (result == IDYES);
    }

    if (!launchShell) {
        return false;
    }

    const auto shellPath = options.installDirectory / kShellBinaryName;
    const HINSTANCE result = ShellExecuteW(
        nullptr,
        L"open",
        shellPath.c_str(),
        nullptr,
        options.installDirectory.c_str(),
        SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(result) > 32;
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == nullptr) {
        showMessage(MB_ICONERROR | MB_OK, L"Master Control Program Setup", L"Failed to read launcher arguments.");
        return 1;
    }

    const std::filesystem::path logPath = launcherLogPath();

    std::wstring parseError;
    const auto options = parseArguments(argc, argv, parseError);
    LocalFree(argv);

    if (!options.has_value()) {
        writeLauncherLog(logPath, LauncherOptions{}, isAdministrator(), false, false, 2, parseError + L"\n\n" + usageText(), false);
        showMessage(MB_ICONERROR | MB_OK, L"Master Control Program Setup", parseError + L"\n\n" + usageText());
        return 2;
    }

    const auto bootstrapperPath = executableDirectory() / kBootstrapperName;
    if (!std::filesystem::exists(bootstrapperPath)) {
        const std::wstring message = L"Required installer engine was not found:\n" + bootstrapperPath.wstring();
        writeLauncherLog(logPath, *options, isAdministrator(), false, false, 3, message, false);
        showMessage(MB_ICONERROR | MB_OK, L"Master Control Program Setup", message);
        return 3;
    }

    const bool administrator = isAdministrator();
    const bool requiresElevation = pathRequiresElevation(options->installDirectory) ||
        std::find(options->bootstrapperArguments.begin(), options->bootstrapperArguments.end(), L"--skip-service") == options->bootstrapperArguments.end() ||
        std::find(options->bootstrapperArguments.begin(), options->bootstrapperArguments.end(), L"--skip-firewall") == options->bootstrapperArguments.end() ||
        std::find(options->bootstrapperArguments.begin(), options->bootstrapperArguments.end(), L"--skip-uninstall-registration") == options->bootstrapperArguments.end();
    const bool elevationAttempted = requiresElevation && !administrator;

    DWORD exitCode = 1;
    std::wstring launchMessage;
    if (!launchProcess(bootstrapperPath, options->bootstrapperArguments, elevationAttempted, exitCode, launchMessage)) {
        const std::wstring message =
            L"Failed to start the installer engine.\n\n" + launchMessage + L"\n\nA launcher log was written to:\n" + logPath.wstring();
        writeLauncherLog(logPath, *options, administrator, requiresElevation, elevationAttempted, exitCode, launchMessage, false);
        showMessage(MB_ICONERROR | MB_OK, L"Master Control Program Setup", message);
        return static_cast<int>(exitCode == 0 ? 1 : exitCode);
    }

    const bool launchedShell = (exitCode == 0) ? maybeLaunchShell(*options) : false;
    const std::wstring outcomeMessage =
        exitCode == 0
            ? L"Bootstrapper completed successfully."
            : L"Bootstrapper exited with code " + std::to_wstring(exitCode) + L". Check the desktop logs for details.";
    writeLauncherLog(logPath, *options, administrator, requiresElevation, elevationAttempted, exitCode, outcomeMessage, launchedShell);

    if (exitCode != 0) {
        const std::wstring message =
            L"Master Control Program installation failed.\n\nReview the desktop logs for details.\n\nLauncher log:\n" +
            logPath.wstring();
        showMessage(MB_ICONERROR | MB_OK, L"Master Control Program Setup", message);
        return static_cast<int>(exitCode);
    }

    if (!options->quiet && !launchedShell) {
        showMessage(
            MB_ICONINFORMATION | MB_OK,
            L"Master Control Program Setup",
            L"Master Control Program installed successfully.\n\nA launcher log was written to:\n" + logPath.wstring());
    }

    return 0;
}
