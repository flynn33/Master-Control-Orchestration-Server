# ForsettiPlatform — Windows service implementations
# Copyright (c) 2026 James Daley. All Rights Reserved.

add_library(ForsettiPlatform STATIC
    PlatformServices.cpp
    EntitlementProviders.cpp
    DefaultPlatformServices.cpp
)

target_include_directories(ForsettiPlatform PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(ForsettiPlatform PUBLIC
    ForsettiCore
)

# Windows SDK libraries for platform services
# target_link_libraries(ForsettiPlatform PRIVATE winhttp crypt32 shell32)

# ForsettiPlatform must NOT link to ForsettiHostTemplate or ForsettiModulesExample
