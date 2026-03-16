# Master Control Program
# Copyright (c) 2026 James Daley. All Rights Reserved.
# Proprietary and Confidential.

cmake_minimum_required(VERSION 3.28)

project(MasterControlProgram
    VERSION 0.1.0
    DESCRIPTION "Master Control Program - Forsetti-compliant Windows control plane"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /WX /permissive- /EHsc)
endif()

include(CTest)

find_package(nlohmann_json CONFIG REQUIRED)
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
)

add_subdirectory("${FORSETTI_VENDOR_DIR}" "${CMAKE_BINARY_DIR}/vendor/forsetti")
add_subdirectory(src/MasterControlApp)
add_subdirectory(src/MasterControlServiceHost)
add_subdirectory(src/MasterControlShell)
add_subdirectory(src/MasterControlBootstrapper)

if(BUILD_TESTING)
    add_subdirectory(tools/VsTestConsoleProxy)
    add_subdirectory(tests)

    if(NOT MASTERCONTROL_VSTEST_CONSOLE)
        message(WARNING "vstest.console.exe was not found at configure time. The local proxy will fall back to runtime discovery.")
    endif()
endif()

install(
    DIRECTORY "${CMAKE_SOURCE_DIR}/resources/manifests"
    DESTINATION "share/MasterControlProgram"
)

install(
    DIRECTORY "${CMAKE_SOURCE_DIR}/resources/web"
    DESTINATION "share/MasterControlProgram"
)
