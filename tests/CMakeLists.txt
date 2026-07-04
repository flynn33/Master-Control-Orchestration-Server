# Master Control Orchestration Server
# Copyright (c) 2026 James Daley. All Rights Reserved.
# Proprietary and Confidential.

add_executable(MasterControlOrchestrationServerTests
    MasterControlOrchestrationServerTests.cpp
)

target_link_libraries(MasterControlOrchestrationServerTests PRIVATE
    MasterControlApp
    ws2_32
)

target_include_directories(MasterControlOrchestrationServerTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/MasterControlApp
    ${CMAKE_SOURCE_DIR}/include
)

# Repo root for tests that verify cross-file contracts against sources that
# are not compiled into the binary (e.g. the mcos-bridge tool registry in
# .claude-plugin/mcos-control/mcp-servers/mcos-bridge/server.py must only
# call admin routes the backend actually implements).
target_compile_definitions(MasterControlOrchestrationServerTests PRIVATE
    MCOS_REPO_ROOT="${CMAKE_SOURCE_DIR}"
)

# v0.11.0-alpha.3: MasterControlApp now links the vcpkg sqlite3 port,
# which on the x64-windows triplet is a DLL. The test executable runs
# directly via ctest, so sqlite3.dll (and any other transitive runtime
# DLLs) must sit next to it -- otherwise the process fails to load /
# crashes at startup, which on the Windows CI runner manifests as a
# 300s ctest *timeout* (Windows Error Reporting blocks the faulting
# process) rather than a clean failure. $<TARGET_RUNTIME_DLLS> (CMake
# 3.21+) resolves exactly that set; the copy is a no-op when vcpkg's
# applocal step already staged them.
add_custom_command(TARGET MasterControlOrchestrationServerTests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_RUNTIME_DLLS:MasterControlOrchestrationServerTests>
        $<TARGET_FILE_DIR:MasterControlOrchestrationServerTests>
    COMMAND_EXPAND_LISTS)

add_test(
    NAME MasterControlOrchestrationServerTests
    COMMAND $<TARGET_FILE:MasterControlOrchestrationServerTests>
)
