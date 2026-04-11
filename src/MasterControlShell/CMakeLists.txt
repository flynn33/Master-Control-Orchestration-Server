# Master Control Orchestration Server
# Copyright (c) 2026 James Daley. All Rights Reserved.
# Proprietary and Confidential.

set(MASTERCONTROL_SHELL_PROJECT "${CMAKE_CURRENT_SOURCE_DIR}/MasterControlShell.vcxproj")

set(MASTERCONTROL_MSBUILD_HINTS)
if(CMAKE_GENERATOR_INSTANCE)
    # Keep the inner WinUI project build aligned with the active IDE generator.
    list(APPEND MASTERCONTROL_MSBUILD_HINTS
        "${CMAKE_GENERATOR_INSTANCE}/MSBuild/Current/Bin"
    )
endif()

list(APPEND MASTERCONTROL_MSBUILD_HINTS
    "C:/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin"
    "C:/Program Files/Microsoft Visual Studio/18/BuildTools/MSBuild/Current/Bin"
    "C:/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin"
    "C:/Program Files/Microsoft Visual Studio/2022/BuildTools/MSBuild/Current/Bin"
)

# Re-resolve this on each configure so a prior VS installation choice does not linger in the cache.
unset(MASTERCONTROL_MSBUILD CACHE)
find_program(
    MASTERCONTROL_MSBUILD
    NAMES MSBuild.exe
    HINTS ${MASTERCONTROL_MSBUILD_HINTS}
    NO_CACHE
)

if(NOT MASTERCONTROL_MSBUILD)
    message(FATAL_ERROR "MSBuild.exe is required to build the WinUI 3 MasterControlShell project.")
endif()

file(TO_NATIVE_PATH "${CMAKE_CURRENT_BINARY_DIR}" MASTERCONTROL_SHELL_BINARY_DIR_NATIVE)

set(MASTERCONTROL_SHELL_OUTPUT_DIR "${MASTERCONTROL_SHELL_BINARY_DIR_NATIVE}\\$<CONFIG>\\")
set(MASTERCONTROL_SHELL_INTERMEDIATE_DIR "${MASTERCONTROL_SHELL_BINARY_DIR_NATIVE}\\intermediate\\$<CONFIG>\\")

add_custom_target(MasterControlShell ALL
    COMMAND "${MASTERCONTROL_MSBUILD}" "${MASTERCONTROL_SHELL_PROJECT}"
        /restore
        /t:Build
        /p:RestorePackagesConfig=true
        /p:PlatformToolset=v145
        /p:Configuration=$<CONFIG>
        /p:Platform=x64
        /p:OutDir=${MASTERCONTROL_SHELL_OUTPUT_DIR}
        /p:IntDir=${MASTERCONTROL_SHELL_INTERMEDIATE_DIR}
        /m
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    VERBATIM
)

install(
    DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/"
    DESTINATION .
    PATTERN "*.exp" EXCLUDE
    PATTERN "*.lib" EXCLUDE
    PATTERN "*.ilk" EXCLUDE
)
