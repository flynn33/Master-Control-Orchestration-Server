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

target_compile_definitions(MasterControlBootstrapper PRIVATE
    MASTERCONTROL_BOOTSTRAPPER_VERSION="${PROJECT_VERSION}"
)

add_executable(MasterControlSetup WIN32
    setup_main.cpp
)

target_link_libraries(MasterControlSetup PRIVATE
    advapi32
    shell32
    user32
    ole32
)

install(TARGETS MasterControlBootstrapper RUNTIME DESTINATION .)
install(TARGETS MasterControlSetup RUNTIME DESTINATION .)
