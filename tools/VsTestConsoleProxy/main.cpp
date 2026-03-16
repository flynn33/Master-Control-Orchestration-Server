#include "VsTestConsoleProxyConfig.h"

#include <windows.h>
#include <shellapi.h>

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace
{
std::wstring quote_argument(const std::wstring& argument)
{
    if (argument.empty())
    {
        return L"\"\"";
    }

    if (argument.find_first_of(L" \t\n\v\"") == std::wstring::npos)
    {
        return argument;
    }

    std::wstring quoted = L"\"";
    std::size_t backslash_count = 0;

    for (wchar_t character : argument)
    {
        if (character == L'\\')
        {
            ++backslash_count;
            continue;
        }

        if (character == L'"')
        {
            quoted.append(backslash_count * 2 + 1, L'\\');
            quoted.push_back(L'"');
            backslash_count = 0;
            continue;
        }

        if (backslash_count > 0)
        {
            quoted.append(backslash_count, L'\\');
            backslash_count = 0;
        }

        quoted.push_back(character);
    }

    if (backslash_count > 0)
    {
        quoted.append(backslash_count * 2, L'\\');
    }

    quoted.push_back(L'"');
    return quoted;
}

bool file_exists(const std::wstring& path)
{
    return !path.empty() && std::filesystem::exists(path);
}

std::wstring discover_vstest_path()
{
    if (const DWORD env_size = GetEnvironmentVariableW(L"MASTERCONTROL_VSTEST_CONSOLE", nullptr, 0); env_size > 0)
    {
        std::wstring env_value(env_size - 1, L'\0');
        GetEnvironmentVariableW(L"MASTERCONTROL_VSTEST_CONSOLE", env_value.data(), env_size);
        if (file_exists(env_value))
        {
            return env_value;
        }
    }

    if (file_exists(MASTERCONTROL_DEFAULT_VSTEST_PATH))
    {
        return MASTERCONTROL_DEFAULT_VSTEST_PATH;
    }

    const std::vector<std::wstring> candidates = {
        LR"(C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\TestWindow\vstest.console.exe)",
        LR"(C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\TestWindow\vstest.console.exe)",
        LR"(C:\Program Files\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\TestWindow\vstest.console.exe)",
        LR"(C:\Program Files\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\TestWindow\vstest.console.exe)"
    };

    for (const auto& candidate : candidates)
    {
        if (file_exists(candidate))
        {
            return candidate;
        }
    }

    return {};
}
}

int wmain()
{
    std::wstring vstest_path = discover_vstest_path();
    if (vstest_path.empty())
    {
        std::wcerr << L"Unable to locate vstest.console.exe. Set MASTERCONTROL_VSTEST_CONSOLE or install the Visual Studio Test Window components." << std::endl;
        return 1;
    }

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == nullptr)
    {
        std::wcerr << L"Unable to parse command line." << std::endl;
        return 1;
    }

    std::wstring command_line = quote_argument(vstest_path);
    for (int index = 1; index < argc; ++index)
    {
        command_line.push_back(L' ');
        command_line.append(quote_argument(argv[index]));
    }

    LocalFree(argv);

    STARTUPINFOW startup_info{};
    startup_info.cb = sizeof(startup_info);

    PROCESS_INFORMATION process_information{};
    std::wstring mutable_command_line = command_line;

    if (!CreateProcessW(
            vstest_path.c_str(),
            mutable_command_line.data(),
            nullptr,
            nullptr,
            FALSE,
            0,
            nullptr,
            nullptr,
            &startup_info,
            &process_information))
    {
        std::wcerr << L"Failed to launch " << vstest_path << L" (error " << GetLastError() << L")." << std::endl;
        return 1;
    }

    WaitForSingleObject(process_information.hProcess, INFINITE);

    DWORD exit_code = 1;
    GetExitCodeProcess(process_information.hProcess, &exit_code);

    CloseHandle(process_information.hThread);
    CloseHandle(process_information.hProcess);

    return static_cast<int>(exit_code);
}
