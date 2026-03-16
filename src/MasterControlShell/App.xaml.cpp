// Master Control Program
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#include "pch.h"

#include "App.xaml.h"
#include "MainWindow.xaml.h"

namespace winrt::MasterControlShell::implementation {

using namespace Microsoft::UI::Xaml;

namespace {

void writeStartupLog(const std::wstring& message) {
    try {
        const auto logPath = std::filesystem::temp_directory_path() / L"MasterControlShell-startup.log";
        std::wofstream stream(logPath, std::ios::app);
        if (!stream.is_open()) {
            return;
        }

        const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        tm localTime{};
        localtime_s(&localTime, &now);

        stream << std::put_time(&localTime, L"%Y-%m-%d %H:%M:%S") << L"  " << message << std::endl;
    } catch (...) {
    }
}

std::wstring describeException() {
    try {
        throw;
    } catch (const winrt::hresult_error& error) {
        std::wostringstream stream;
        stream << L"HRESULT 0x" << std::hex << static_cast<uint32_t>(error.code().value)
               << L": " << error.message().c_str();
        return stream.str();
    } catch (const std::exception& error) {
        return std::wstring(L"std::exception: ") + winrt::to_hstring(error.what()).c_str();
    } catch (...) {
        return L"Unknown startup exception.";
    }
}

void showStartupFailure(const std::wstring& message) {
    writeStartupLog(message);
    MessageBoxW(nullptr, message.c_str(), L"Master Control Program", MB_OK | MB_ICONERROR);
}

} // namespace

App::App() {
    writeStartupLog(L"App ctor: InitializeComponent starting.");
    InitializeComponent();
    writeStartupLog(L"App ctor: InitializeComponent finished.");

    UnhandledException([](auto&&, auto&& arguments) {
        const std::wstring message = L"Unhandled WinUI exception: " + std::wstring(arguments.Message().c_str());
        writeStartupLog(message);
#if defined _DEBUG && !defined DISABLE_XAML_GENERATED_BREAK_ON_UNHANDLED_EXCEPTION
        if (IsDebuggerPresent()) {
            auto errorMessage = arguments.Message();
            __debugbreak();
        }
#endif
        arguments.Handled(true);
    });
}

void App::OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&) {
    try {
        writeStartupLog(L"OnLaunched: creating main window.");
        window_ = winrt::make<MainWindow>();
        writeStartupLog(L"OnLaunched: activating main window.");
        window_.Activate();
        writeStartupLog(L"OnLaunched: main window activated.");
    } catch (...) {
        showStartupFailure(L"Shell launch failed: " + describeException());
    }
}

} // namespace winrt::MasterControlShell::implementation
