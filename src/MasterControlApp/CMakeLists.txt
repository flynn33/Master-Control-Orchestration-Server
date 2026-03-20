# Master Control Program
# Copyright (c) 2026 James Daley. All Rights Reserved.
# Proprietary and Confidential.

add_library(MasterControlApp STATIC
    MasterControlDefaults.cpp
    MasterControlModels.cpp
    MasterControlRuntime.cpp
)

target_include_directories(MasterControlApp PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${FORSETTI_VENDOR_DIR}/include
)

target_link_libraries(MasterControlApp PUBLIC
    MasterControlModules
    ForsettiCore
    ForsettiPlatform
    nlohmann_json::nlohmann_json
    ws2_32
    iphlpapi
    urlmon
    advapi32
    crypt32
    shlwapi
    shell32
)

target_compile_definitions(MasterControlApp PUBLIC
    MASTERCONTROL_SOURCE_MODULE_MANIFESTS_DIR="${CMAKE_SOURCE_DIR}/src/MasterControlModules/Resources/ForsettiManifests"
    MASTERCONTROL_SOURCE_WEB_RESOURCES_DIR="${CMAKE_SOURCE_DIR}/resources/web"
    MASTERCONTROL_SOURCE_CLU_RESOURCES_DIR="${CMAKE_SOURCE_DIR}/resources/clu"
)
