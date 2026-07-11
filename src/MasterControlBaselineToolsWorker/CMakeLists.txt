# Master Control Orchestration Server
# Copyright (c) 2026 James Daley. All Rights Reserved.
# Proprietary and Confidential.

# v0.9.1: minimal native MCP worker that ships in-tree so the gateway's
# tools/list returns real tools from a fresh install. Wire protocol is
# line-delimited JSON-RPC 2.0 over stdin/stdout, matching
# WorkerSupervisor::sendStdioJsonRpc's expectations.

add_executable(MasterControlBaselineToolsWorker
    main.cpp
)

set_target_properties(MasterControlBaselineToolsWorker PROPERTIES
    OUTPUT_NAME "mcos-baseline-tools-worker"
)

target_link_libraries(MasterControlBaselineToolsWorker PRIVATE
    nlohmann_json::nlohmann_json
    unofficial::sqlite3::sqlite3
)

# Source include/ for shared MasterControl headers (e.g. DatabaseQuery.h). The
# generated MasterControlVersion.h comes from the global build-dir include path.
target_include_directories(MasterControlBaselineToolsWorker PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

if(MSVC)
    # Match the parent project's strictness so this worker doesn't slip
    # past the warning-as-error gate. /utf-8 is inherited from the root
    # CMakeLists.txt.
    target_compile_options(MasterControlBaselineToolsWorker PRIVATE /W4 /WX /permissive- /EHsc)
endif()

install(TARGETS MasterControlBaselineToolsWorker RUNTIME DESTINATION .)

# Stage this worker's runtime DLL dependencies (notably the vcpkg sqlite3.dll
# on the x64-windows triplet) into the payload root, mirroring the service host
# and the test target. Redundant with the service host's staging since both
# install to the same payload root, but kept explicit so this worker cannot
# ship without its runtime DLLs if the service host's dependency set changes.
install(FILES $<TARGET_RUNTIME_DLLS:MasterControlBaselineToolsWorker> DESTINATION .)
