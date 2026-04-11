@echo off
setlocal enableextensions
rem Developer environment wrapper for Claude autonomous build session.
rem Usage: devenv.cmd <command> [args...]
rem Resets PATH to a minimal baseline to avoid the VsDevCmd "input line too long"
rem explosion that occurs when PATH is already near the 8191-char limit, then
rem sources VS2026 Community dev env and sets VCPKG_ROOT.

set "VS_ROOT=C:\Program Files\Microsoft Visual Studio\18\Community"
set "VCPKG_ROOT=%VS_ROOT%\VC\vcpkg"
set "VsDevCmd=%VS_ROOT%\Common7\Tools\VsDevCmd.bat"
set "VsInstaller=C:\Program Files (x86)\Microsoft Visual Studio\Installer"

if not exist "%VsDevCmd%" (
  echo [devenv.cmd] ERROR: VsDevCmd.bat not found at %VsDevCmd%
  exit /b 2
)

rem Reset PATH to a minimal, deterministic baseline.
set "PATH=%SystemRoot%\System32;%SystemRoot%;%SystemRoot%\System32\Wbem;%SystemRoot%\System32\WindowsPowerShell\v1.0;%VsInstaller%;C:\Program Files\Git\cmd"

call "%VsDevCmd%" -arch=x64 -host_arch=x64 -no_logo >nul
if errorlevel 1 (
  echo [devenv.cmd] ERROR: VsDevCmd init failed with code %errorlevel%
  exit /b 3
)

rem Prepend VS-bundled cmake + ninja so we don't need a separate install.
set "PATH=%VS_ROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%VS_ROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"

%*
exit /b %errorlevel%
