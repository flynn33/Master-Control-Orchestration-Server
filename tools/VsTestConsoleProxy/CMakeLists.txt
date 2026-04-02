# Master Control Orchestration Server
# Copyright (c) 2026 James Daley. All Rights Reserved.
# Proprietary and Confidential.

set(MASTERCONTROL_VSTEST_PROXY_DEFAULT_PATH "${MASTERCONTROL_VSTEST_CONSOLE}")
set(MASTERCONTROL_VSTEST_PROXY_OUTPUT_DIR "${CMAKE_BINARY_DIR}/vendor/forsetti/tests")

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/VsTestConsoleProxyConfig.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/VsTestConsoleProxyConfig.h"
    @ONLY
)

add_executable(VsTestConsoleProxy
    main.cpp
)

target_include_directories(VsTestConsoleProxy PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)

set_target_properties(VsTestConsoleProxy PROPERTIES
    OUTPUT_NAME "vstest.console"
    RUNTIME_OUTPUT_DIRECTORY "${MASTERCONTROL_VSTEST_PROXY_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${MASTERCONTROL_VSTEST_PROXY_OUTPUT_DIR}/Debug"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${MASTERCONTROL_VSTEST_PROXY_OUTPUT_DIR}/Release"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${MASTERCONTROL_VSTEST_PROXY_OUTPUT_DIR}/MinSizeRel"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${MASTERCONTROL_VSTEST_PROXY_OUTPUT_DIR}/RelWithDebInfo"
)
