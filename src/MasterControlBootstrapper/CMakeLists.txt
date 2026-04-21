# Master Control Orchestration Server
# Copyright (c) 2026 James Daley. All Rights Reserved.
# Proprietary and Confidential.

add_executable(MasterControlBootstrapper
    main.cpp
    MasterControlBootstrapper.rc
)

target_link_libraries(MasterControlBootstrapper PRIVATE
    MasterControlApp
    advapi32
    shell32
)

target_compile_definitions(MasterControlBootstrapper PRIVATE
    MASTERCONTROL_BOOTSTRAPPER_VERSION="${PROJECT_VERSION}"
)

add_executable(MasterControlOrchestrationServerLauncher WIN32
    launcher_main.cpp
    MasterControlBootstrapper.rc
)

set_target_properties(MasterControlOrchestrationServerLauncher PROPERTIES
    OUTPUT_NAME "MasterControlOrchestrationServer"
)

target_link_libraries(MasterControlOrchestrationServerLauncher PRIVATE
    shell32
    user32
)

add_executable(MasterControlOrchestrationServerSetup WIN32
    setup_main.cpp
    MasterControlBootstrapper.rc
)

set_target_properties(MasterControlOrchestrationServerSetup PROPERTIES
    OUTPUT_NAME "MasterControlOrchestrationServerSetup"
)

target_link_libraries(MasterControlOrchestrationServerSetup PRIVATE
    advapi32
    comctl32
    nlohmann_json::nlohmann_json
    shell32
    user32
    ole32
)

install(TARGETS MasterControlBootstrapper RUNTIME DESTINATION .)
install(TARGETS MasterControlOrchestrationServerLauncher RUNTIME DESTINATION .)
