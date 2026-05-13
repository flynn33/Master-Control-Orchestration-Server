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

add_test(
    NAME MasterControlOrchestrationServerTests
    COMMAND $<TARGET_FILE:MasterControlOrchestrationServerTests>
)
