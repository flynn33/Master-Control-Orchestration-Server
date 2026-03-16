# Master Control Program
# Copyright (c) 2026 James Daley. All Rights Reserved.
# Proprietary and Confidential.

add_executable(MasterControlBootstrapper
    main.cpp
)

target_link_libraries(MasterControlBootstrapper PRIVATE
    MasterControlApp
    advapi32
    shell32
)

install(TARGETS MasterControlBootstrapper RUNTIME DESTINATION .)
