# Master Control Orchestration Server
# Copyright (c) 2026 James Daley. All Rights Reserved.
# Proprietary and Confidential.

add_executable(MasterControlServiceHost
    main.cpp
)

target_include_directories(MasterControlServiceHost PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(MasterControlServiceHost PRIVATE
    MasterControlApp
    advapi32
)

install(TARGETS MasterControlServiceHost RUNTIME DESTINATION .)
