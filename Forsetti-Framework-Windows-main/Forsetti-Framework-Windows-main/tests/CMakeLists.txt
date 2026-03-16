# Forsetti Framework Tests — Microsoft CppUnitTest
# Copyright (c) 2026 James Daley. All Rights Reserved.
#
# Tests use Microsoft CppUnitTest (native Visual Studio framework).
# Tests are built as DLLs and discovered by Visual Studio Test Explorer and vstest.console.exe.
# These tests only build on Windows with MSVC.

if(MSVC)
    # Visual Studio CppUnitTest framework paths
    # Detect VCInstallDir from the compiler path if not set in environment
    if(DEFINED ENV{VCInstallDir} AND NOT "$ENV{VCInstallDir}" STREQUAL "")
        set(VC_INSTALL_DIR "$ENV{VCInstallDir}")
    else()
        # Extract VC install dir from CMAKE_CXX_COMPILER (e.g. .../VC/Tools/MSVC/14.xx/bin/Hostx64/x64/cl.exe)
        get_filename_component(_CL_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)        # .../bin/Hostx64/x64
        get_filename_component(_CL_DIR "${_CL_DIR}" DIRECTORY)                    # .../bin/Hostx64
        get_filename_component(_CL_DIR "${_CL_DIR}" DIRECTORY)                    # .../bin
        get_filename_component(_CL_DIR "${_CL_DIR}" DIRECTORY)                    # .../MSVC/14.xx
        get_filename_component(_CL_DIR "${_CL_DIR}" DIRECTORY)                    # .../MSVC
        get_filename_component(_CL_DIR "${_CL_DIR}" DIRECTORY)                    # .../Tools
        get_filename_component(VC_INSTALL_DIR "${_CL_DIR}" DIRECTORY)             # .../VC
        set(VC_INSTALL_DIR "${VC_INSTALL_DIR}/")
    endif()
    set(VS_UNITTEST_INCLUDE "${VC_INSTALL_DIR}Auxiliary/VS/UnitTest/include")
    set(VS_UNITTEST_LIB "${VC_INSTALL_DIR}Auxiliary/VS/UnitTest/lib/x64")

    # --- ForsettiCoreTests ---
    add_library(ForsettiCoreTests SHARED
        ForsettiCoreTests/SemVerTests.cpp
        ForsettiCoreTests/ManifestLoaderTests.cpp
        ForsettiCoreTests/CompatibilityCheckerTests.cpp
        ForsettiCoreTests/ModuleCommunicationTests.cpp
        ForsettiCoreTests/EntitlementProviderTests.cpp
        ForsettiCoreTests/UISurfaceManagerTests.cpp
        ForsettiCoreTests/ModuleLoggingTests.cpp
        ForsettiCoreTests/RuntimeLifecycleTests.cpp
        ForsettiCoreTests/BootstrapTests.cpp
    )
    target_include_directories(ForsettiCoreTests PRIVATE
        ${VS_UNITTEST_INCLUDE}
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/ForsettiCoreTests
    )
    target_link_directories(ForsettiCoreTests PRIVATE ${VS_UNITTEST_LIB})
    target_link_libraries(ForsettiCoreTests PRIVATE
        Microsoft.VisualStudio.TestTools.CppUnitTestFramework
        ForsettiCore
    )

    # Register with ctest via vstest.console
    add_test(
        NAME ForsettiCoreTests
        COMMAND vstest.console.exe "$<TARGET_FILE:ForsettiCoreTests>" /Platform:x64
    )

    # --- ForsettiPlatformTests ---
    add_library(ForsettiPlatformTests SHARED
        ForsettiPlatformTests/PlatformServicesTests.cpp
    )
    target_include_directories(ForsettiPlatformTests PRIVATE
        ${VS_UNITTEST_INCLUDE}
        ${CMAKE_SOURCE_DIR}/include
    )
    target_link_directories(ForsettiPlatformTests PRIVATE ${VS_UNITTEST_LIB})
    target_link_libraries(ForsettiPlatformTests PRIVATE
        Microsoft.VisualStudio.TestTools.CppUnitTestFramework
        ForsettiCore
        ForsettiPlatform
    )

    add_test(
        NAME ForsettiPlatformTests
        COMMAND vstest.console.exe "$<TARGET_FILE:ForsettiPlatformTests>" /Platform:x64
    )

    # --- ForsettiArchitectureTests ---
    add_library(ForsettiArchitectureTests SHARED
        ForsettiArchitectureTests/ArchitectureEnforcementTests.cpp
    )
    target_include_directories(ForsettiArchitectureTests PRIVATE
        ${VS_UNITTEST_INCLUDE}
        ${CMAKE_SOURCE_DIR}/include
    )
    target_link_directories(ForsettiArchitectureTests PRIVATE ${VS_UNITTEST_LIB})
    target_link_libraries(ForsettiArchitectureTests PRIVATE
        Microsoft.VisualStudio.TestTools.CppUnitTestFramework
    )

    add_test(
        NAME ForsettiArchitectureTests
        COMMAND vstest.console.exe "$<TARGET_FILE:ForsettiArchitectureTests>" /Platform:x64
    )
endif()
