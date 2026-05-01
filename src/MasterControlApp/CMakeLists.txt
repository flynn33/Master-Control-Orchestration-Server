# Master Control Orchestration Server
# Copyright (c) 2026 James Daley. All Rights Reserved.
# Proprietary and Confidential.

add_library(MasterControlApp STATIC
    MasterControlDefaults.cpp
    MasterControlModels.cpp
    MasterControlRuntime.cpp
    McpGatewayAdapters.cpp
)

target_include_directories(MasterControlApp PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${FORSETTI_VENDOR_DIR}/include
)

target_link_libraries(MasterControlApp
    PUBLIC
        MasterControlModules
        ForsettiCore
        ForsettiPlatform
        nlohmann_json::nlohmann_json
    PRIVATE
        ws2_32
        iphlpapi
        urlmon
        advapi32
        crypt32
        winhttp
        dnsapi
        shlwapi
        shell32
        userenv
        wtsapi32
        rpcrt4
        bcrypt
)

target_compile_definitions(MasterControlApp PUBLIC
    MASTERCONTROL_SOURCE_MODULE_MANIFESTS_DIR="${CMAKE_SOURCE_DIR}/src/MasterControlModules/Resources/ForsettiManifests"
    MASTERCONTROL_SOURCE_WEB_RESOURCES_DIR="${CMAKE_SOURCE_DIR}/resources/web"
    MASTERCONTROL_SOURCE_CLU_RESOURCES_DIR="${CMAKE_SOURCE_DIR}/resources/clu"
    MASTERCONTROL_SOURCE_FORSETTI_INSTRUCTIONS_FILE="${FORSETTI_VENDOR_DIR}/forsetti-instructions.json"
)

if(MSVC)
    target_compile_options(MasterControlApp PRIVATE /bigobj)
endif()
