# ForsettiModulesExample — Example modules (links Core ONLY)
# Copyright (c) 2026 James Daley. All Rights Reserved.

add_library(ForsettiModulesExample STATIC
    ExampleModules.cpp
    ExampleModuleRegistry.cpp
)

target_include_directories(ForsettiModulesExample PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(ForsettiModulesExample PUBLIC
    ForsettiCore
)

# ForsettiModulesExample must NOT link to ForsettiPlatform or ForsettiHostTemplate
# This is enforced by the architecture tests and CI guardrails

# Copy manifest JSON files to build output for runtime discovery
file(GLOB MANIFEST_FILES "${CMAKE_CURRENT_SOURCE_DIR}/Resources/ForsettiManifests/*.json")
file(COPY ${MANIFEST_FILES} DESTINATION "${CMAKE_BINARY_DIR}/ForsettiManifests")
