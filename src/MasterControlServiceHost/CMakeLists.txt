# Master Control Orchestration Server
# Copyright (c) 2026 James Daley. All Rights Reserved.
# Proprietary and Confidential.

add_executable(MasterControlServiceHost
    main.cpp
    MasterControlServiceHost.rc
)

target_include_directories(MasterControlServiceHost PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(MasterControlServiceHost PRIVATE
    MasterControlApp
    advapi32
)

install(TARGETS MasterControlServiceHost RUNTIME DESTINATION .)

# Stage the service host's runtime DLL dependencies into the payload root.
# MasterControlApp links the vcpkg sqlite3 port, which on the x64-windows
# triplet is a DLL (sqlite3.dll). Without this, the MSI payload omits
# sqlite3.dll and the installed service fails to load with 0xC0000135
# (STATUS_DLL_NOT_FOUND) -> the SCM start times out -> the bootstrapper's
# install custom action returns non-zero -> the MSI aborts with 1603 and rolls
# back. $<TARGET_RUNTIME_DLLS> (CMake 3.21+) resolves exactly the non-system
# runtime DLLs; this mirrors the test target's TARGET_RUNTIME_DLLS staging in
# tests/CMakeLists.txt so the installed layout matches the tested layout.
install(FILES $<TARGET_RUNTIME_DLLS:MasterControlServiceHost> DESTINATION .)
