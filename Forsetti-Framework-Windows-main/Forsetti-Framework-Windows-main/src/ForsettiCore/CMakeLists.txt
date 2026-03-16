# ForsettiCore — Pure C++20, no platform dependencies
# Copyright (c) 2026 James Daley. All Rights Reserved.

add_library(ForsettiCore STATIC
    SemVer.cpp
    ModuleModels.cpp
    UIModels.cpp
    ForsettiLogger.cpp
    ForsettiEventBus.cpp
    ForsettiServiceContainer.cpp
    ForsettiContext.cpp
    ActivationStore.cpp
    CapabilityPolicy.cpp
    EntitlementProviders.cpp
    ManifestLoader.cpp
    CompatibilityChecker.cpp
    ModuleRegistry.cpp
    StaticModuleRegistry.cpp
    UISurfaceManager.cpp
    ModuleManager.cpp
    ForsettiRuntime.cpp
)

target_include_directories(ForsettiCore PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(ForsettiCore PUBLIC
    nlohmann_json::nlohmann_json
)

# ForsettiCore must NOT link to ForsettiPlatform, ForsettiHostTemplate, or ForsettiModulesExample
# This is enforced by the architecture tests and CI guardrails
