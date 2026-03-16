# Master Control Program
# Copyright (c) 2026 James Daley. All Rights Reserved.
# Proprietary and Confidential.

add_executable(MasterControlProgramTests
    MasterControlProgramTests.cpp
)

target_link_libraries(MasterControlProgramTests PRIVATE
    MasterControlApp
)

add_test(
    NAME MasterControlProgramTests
    COMMAND $<TARGET_FILE:MasterControlProgramTests>
)
