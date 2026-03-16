# Master Control Program
# Copyright (c) 2026 James Daley. All Rights Reserved.
# Proprietary and Confidential.

add_library(MasterControlModules STATIC
    MasterControlModules.cpp
)

target_include_directories(MasterControlModules PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${FORSETTI_VENDOR_DIR}/include
)

# Forsetti modules stay in a Core-only target so they follow the framework's
# intended dependency layering and can be validated independently of the host.
target_link_libraries(MasterControlModules PUBLIC
    ForsettiCore
)
