# Master Control Orchestration Server
# Copyright (c) 2026 James Daley. All Rights Reserved.
# Proprietary and Confidential.

cmake_minimum_required(VERSION 3.28)

# ---------------------------------------------------------------------------
# vcpkg toolchain guard: the CMakePresets reference $env{VCPKG_ROOT}. If the
# env var is empty at configure time, CMake receives the literal path
# "/scripts/buildsystems/vcpkg.cmake" and produces a confusing "cannot find
# toolchain file" error. Detect that specific case early and emit a clear
# message telling the developer to set VCPKG_ROOT before running the preset.
# ---------------------------------------------------------------------------
if(CMAKE_TOOLCHAIN_FILE MATCHES "^/scripts/buildsystems/vcpkg\\.cmake$"
        OR CMAKE_TOOLCHAIN_FILE MATCHES "^\\$\\{?env")
    message(FATAL_ERROR
        "VCPKG_ROOT is not set in the current environment. Set it (e.g. "
        "`$env:VCPKG_ROOT = 'C:\\vcpkg'` on PowerShell) before running "
        "`cmake --preset debug` or `cmake --preset release`.")
endif()

# ---------------------------------------------------------------------------
# Version: single source of truth is VERSION.json
# ---------------------------------------------------------------------------
# v0.9.5: wire VERSION.json into CMake's configure-time depend graph so a
# bump to current_version triggers a re-configure on the next build, which
# in turn regenerates MasterControlVersion.h and propagates the new
# MASTERCONTROL_VERSION literal into every TU that includes it. Pre-v0.9.5
# CMake only watched CMakeLists.txt, so a VERSION.json bump alone left
# MasterControlVersion.h stale and the running binary kept reporting the
# previous version through the MCP initialize handshake until an unrelated
# CMakeLists.txt edit forced a reconfigure.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/VERSION.json")

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION.json" _mc_version_json)
string(JSON _mc_current_version GET "${_mc_version_json}" "current_version")
# Strip any pre-release suffix (e.g., "0.3.0-rc.1" → "0.3.0") for CMake project()
# which only accepts MAJOR.MINOR.PATCH[.TWEAK].
string(REGEX REPLACE "-.*$" "" _mc_base_version "${_mc_current_version}")
# Alpha-stage scheme <Stage><A>.<Feature>.<patch/hotfix>: strip the leading
# stage letter(s) so "A3.11.0" yields the numeric base "3.11.0".
string(REGEX REPLACE "^[A-Za-z]+" "" _mc_base_version "${_mc_base_version}")
string(REPLACE "." ";" _mc_version_parts "${_mc_base_version}")
list(GET _mc_version_parts 0 _mc_ver_major)
list(GET _mc_version_parts 1 _mc_ver_minor)
list(GET _mc_version_parts 2 _mc_ver_patch)
message(STATUS "Master Control version from VERSION.json: ${_mc_current_version} (CMake base: ${_mc_base_version})")

project(MasterControlOrchestrationServer
    VERSION ${_mc_ver_major}.${_mc_ver_minor}.${_mc_ver_patch}
    DESCRIPTION "Master Control Orchestration Server - Forsetti-compliant Windows control plane"
    LANGUAGES CXX RC
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    # /utf-8: treat source + execution character sets as UTF-8. Without it
    # MSVC falls back to the system code page (Windows-1252 on US-English
    # systems), which makes any em-dash, curly quote, or non-ASCII char
    # in a wide-string literal render as mojibake at runtime (observed:
    # "STEP 2 OF 2 \u2014 INSTALLED" was showing as "STEP 2 OF 2 â€" INSTALLED"
    # in the shell's AI Integrations chip because the UTF-8 bytes for \u2014
    # were being read as three separate Win-1252 chars).
    add_compile_options(/W4 /WX /permissive- /EHsc /utf-8)
endif()

# Generate MasterControlVersion.h from VERSION.json at configure time
set(MASTERCONTROL_FULL_VERSION "${_mc_current_version}")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/include/MasterControl/MasterControlVersion.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/include/MasterControl/MasterControlVersion.h"
    @ONLY
)

include(CTest)

find_package(nlohmann_json CONFIG REQUIRED)
# v0.11.0-alpha.3 (PHASE-14 Slice E): SQLite for the persistent
# diagnostics store. vcpkg's sqlite3 port exports the unofficial
# config package.
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_program(MASTERCONTROL_POWERSHELL NAMES pwsh.exe pwsh powershell.exe powershell)
if(WIN32 AND NOT MASTERCONTROL_POWERSHELL)
    message(FATAL_ERROR
        "PowerShell was not found on PATH. Install Windows PowerShell 5.1 or "
        "PowerShell 7+ (pwsh) before configuring. Build, packaging, and "
        "deployment scripts depend on it.")
endif()
find_program(
    MASTERCONTROL_VSTEST_CONSOLE
    NAMES vstest.console.exe
    HINTS
        "C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/TestWindow"
        "C:/Program Files/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/TestWindow"
        "C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/TestWindow"
        "C:/Program Files/Microsoft Visual Studio/18/BuildTools/Common7/IDE/CommonExtensions/Microsoft/TestWindow"
)

set(FORSETTI_VENDOR_DIR "${CMAKE_SOURCE_DIR}/Forsetti-Framework-Windows-main/Forsetti-Framework-Windows-main")

include_directories(
    "${FORSETTI_VENDOR_DIR}/include"
    "${CMAKE_CURRENT_BINARY_DIR}/include"
)

add_subdirectory("${FORSETTI_VENDOR_DIR}" "${CMAKE_BINARY_DIR}/vendor/forsetti")
add_subdirectory(src/MasterControlModules)
add_subdirectory(src/MasterControlApp)
add_subdirectory(src/MasterControlServiceHost)
add_subdirectory(src/MasterControlShell)
add_subdirectory(src/MasterControlBootstrapper)
# v0.9.1: baseline MCP tools worker. Ships in-tree so the gateway's
# tools/list returns real tools (mcos.echo, mcos.now, mcos.host_info,
# mcos.add) from a fresh install.
add_subdirectory(src/MasterControlBaselineToolsWorker)

if(BUILD_TESTING)
    add_subdirectory(tools/VsTestConsoleProxy)
    add_subdirectory(tests)

    if(NOT MASTERCONTROL_VSTEST_CONSOLE)
        message(WARNING "vstest.console.exe was not found at configure time. The local proxy will fall back to runtime discovery.")
    endif()
endif()

if(WIN32 AND MASTERCONTROL_POWERSHELL)
    set(MASTERCONTROL_ACTIVE_PRESET "$<IF:$<CONFIG:Debug>,debug,release>")
    set(MASTERCONTROL_PACKAGE_SCRIPT "${CMAKE_SOURCE_DIR}/scripts/Package-MasterControlOrchestrationServer.ps1")
    set(MASTERCONTROL_IDE_ACCEPTANCE_SCRIPT "${CMAKE_SOURCE_DIR}/scripts/Invoke-MasterControlOrchestrationServerIdeAcceptance.ps1")
    set(MASTERCONTROL_RUNTIME_TARGETS
        MasterControlModules
        MasterControlApp
        MasterControlServiceHost
        MasterControlShell
        MasterControlBootstrapper
        MasterControlOrchestrationServerLauncher
        MasterControlOrchestrationServerSetup
        MasterControlBaselineToolsWorker
    )

    add_custom_target(IDE_PACKAGE
        COMMAND "${MASTERCONTROL_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass -File "${MASTERCONTROL_PACKAGE_SCRIPT}" -Preset "${MASTERCONTROL_ACTIVE_PRESET}" -SkipBuild -SkipTests
        USES_TERMINAL
        VERBATIM
        COMMENT "Package Master Control Orchestration Server for the active IDE configuration"
    )
    add_dependencies(IDE_PACKAGE ${MASTERCONTROL_RUNTIME_TARGETS})
    set_target_properties(IDE_PACKAGE PROPERTIES FOLDER "IDEValidationTargets")

    foreach(MASTERCONTROL_ACCEPTANCE_SCENARIO IN ITEMS mixed managed both)
        string(TOUPPER "${MASTERCONTROL_ACCEPTANCE_SCENARIO}" MASTERCONTROL_ACCEPTANCE_SUFFIX)
        set(MASTERCONTROL_ACCEPTANCE_TARGET "IDE_ACCEPTANCE_${MASTERCONTROL_ACCEPTANCE_SUFFIX}")
        set(MASTERCONTROL_ACCEPTANCE_ARGUMENTS
            -Preset "${MASTERCONTROL_ACTIVE_PRESET}"
            -Scenario "${MASTERCONTROL_ACCEPTANCE_SCENARIO}"
            -SkipBuild
            -SkipTests
            -KeepArtifacts
        )

        if(MASTERCONTROL_ACCEPTANCE_SCENARIO STREQUAL "managed" OR MASTERCONTROL_ACCEPTANCE_SCENARIO STREQUAL "both")
            list(APPEND MASTERCONTROL_ACCEPTANCE_ARGUMENTS -ManagedMutating)
        endif()

        add_custom_target(${MASTERCONTROL_ACCEPTANCE_TARGET}
            COMMAND "${MASTERCONTROL_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass -File "${MASTERCONTROL_IDE_ACCEPTANCE_SCRIPT}" ${MASTERCONTROL_ACCEPTANCE_ARGUMENTS}
            USES_TERMINAL
            VERBATIM
            COMMENT "Run ${MASTERCONTROL_ACCEPTANCE_SCENARIO} deployment acceptance for the active IDE configuration"
        )
        add_dependencies(${MASTERCONTROL_ACCEPTANCE_TARGET} IDE_PACKAGE)
        set_target_properties(${MASTERCONTROL_ACCEPTANCE_TARGET} PROPERTIES FOLDER "IDEValidationTargets")
    endforeach()

    # v0.9.80: DEPLOY_LOCAL_LIVE custom target. Invokes
    # scripts/Deploy-LocalLive.ps1 against the active preset's build
    # tree. The script stops the MasterControlProgram service, kills
    # any running MasterControlShell, copies the freshly-built
    # MasterControlServiceHost.exe + MasterControlShell.exe + the
    # share/MasterControlOrchestrationServer/web/* assets into the
    # spaces-path live install, restarts the service, and probes
    # /api/version + self-tests + supervisor as a smoke check. Pass
    # the WiX MSI installer when you need a clean install; this target
    # is the hot-patch path for development.
    set(MASTERCONTROL_DEPLOY_SCRIPT "${CMAKE_SOURCE_DIR}/scripts/Deploy-LocalLive.ps1")
    add_custom_target(DEPLOY_LOCAL_LIVE
        COMMAND "${MASTERCONTROL_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass -File "${MASTERCONTROL_DEPLOY_SCRIPT}" -BuildDir "${CMAKE_BINARY_DIR}" -RelaunchShell
        USES_TERMINAL
        VERBATIM
        COMMENT "Hot-deploy the active build into the live service path (Admin required)"
    )
    add_dependencies(DEPLOY_LOCAL_LIVE ${MASTERCONTROL_RUNTIME_TARGETS})
    set_target_properties(DEPLOY_LOCAL_LIVE PROPERTIES FOLDER "IDEValidationTargets")
endif()

install(
    DIRECTORY "${CMAKE_SOURCE_DIR}/src/MasterControlModules/Resources/ForsettiManifests"
    DESTINATION "share/MasterControlOrchestrationServer"
)

install(
    DIRECTORY "${CMAKE_SOURCE_DIR}/resources/web"
    DESTINATION "share/MasterControlOrchestrationServer"
)

install(
    DIRECTORY "${CMAKE_SOURCE_DIR}/resources/clu"
    DESTINATION "share/MasterControlOrchestrationServer"
)

# Icon assets — shipped into the staged package so the WiX MSI can reference
# them by fixed relative path, and so the desktop shell's installed layout
# carries the source-of-truth artwork alongside the executables.
install(
    DIRECTORY "${CMAKE_SOURCE_DIR}/resources/icons"
    DESTINATION "share/MasterControlOrchestrationServer"
)

# Claude Code plugin (mcos-control). Bundled with the MSI so installs include
# the operator-side Claude Code tooling. The plugin is NOT auto-registered
# with Claude Code at install time (per-user state from a perMachine MSI is
# fragile). After install, operators register it by running
# Register-McosControlPlugin.ps1 from the install directory, which symlinks
# this directory into %USERPROFILE%\.claude\plugins\mcos-control.
install(
    DIRECTORY "${CMAKE_SOURCE_DIR}/.claude-plugin/mcos-control"
    DESTINATION "share/claude-plugins"
)

install(
    FILES "${CMAKE_SOURCE_DIR}/scripts/Register-McosControlPlugin.ps1"
    DESTINATION "."
)

# v0.11.0-alpha.2 (review-hardened): ship the operator-side TLS
# provisioning + rollback scripts in the MSI so post-install instructions
# like `scripts\Configure-LocalServerCert.ps1` actually find the file under
# `C:\Program Files\Master Control Orchestration Server\scripts\`.
# Pre-hardening only Register-McosControlPlugin.ps1 was packaged; operators
# of a clean alpha.2 install hit "command not found" when following the
# wiki's TLS enable runbook.
install(
    FILES
        "${CMAKE_SOURCE_DIR}/scripts/Configure-LocalServerCert.ps1"
        "${CMAKE_SOURCE_DIR}/scripts/Remove-LocalServerCert.ps1"
        # v0.11.0-alpha.3: cert auto-rotation scheduled-task job (closes
        # the VERSION.json "renewal-cron PS job" deferred item).
        "${CMAKE_SOURCE_DIR}/scripts/Register-CertAutoRotation.ps1"
    DESTINATION "scripts"
)
