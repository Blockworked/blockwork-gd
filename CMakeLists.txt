cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if ("${CMAKE_SYSTEM_NAME}" STREQUAL "iOS" OR IOS)
    set(CMAKE_OSX_ARCHITECTURES "arm64")
elseif (NOT DEFINED CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
endif()
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

project(blockwork VERSION 1.0.0)

# Add all source files inside src (recursively)
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)

# Set up the mod binary
add_library(${PROJECT_NAME} SHARED ${SOURCES})

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK! Please define GEODE_SDK environment variable to point to Geode")
else()
    message(STATUS "Found Geode: $ENV{GEODE_SDK}")
endif()

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

# Set up dependencies, resources, and link Geode.
setup_geode_mod(${PROJECT_NAME})

# blockwork-ffi: the Rust recording/playback engine (blockwork-core), embedded
# directly instead of talked to over the loopback IPC connection the old
# IpcClient used. Built separately (see blockwork-ffi/build-windows.sh and
# build-macos.sh in the Blockwork repo) since this is a different Cargo
# workspace with no submodule relationship to this one — point these at the
# artifacts that script produces.
if (NOT DEFINED BLOCKWORK_FFI_LIB)
    message(FATAL_ERROR "BLOCKWORK_FFI_LIB must point to the built blockwork_ffi static library (run blockwork-ffi/build-windows.sh or build-macos.sh in the Blockwork repo, then pass e.g. -DBLOCKWORK_FFI_LIB=/path/to/blockwork_ffi.lib)")
endif()
if (NOT DEFINED BLOCKWORK_FFI_INCLUDE_DIR)
    message(FATAL_ERROR "BLOCKWORK_FFI_INCLUDE_DIR must point to the directory containing the cbindgen-generated blockwork_ffi.h (blockwork-ffi/include in the Blockwork repo)")
endif()

target_include_directories(${PROJECT_NAME} PRIVATE ${BLOCKWORK_FFI_INCLUDE_DIR})

if (WIN32)
    # kernel32/ntdll/userenv/ws2_32/dbghelp: exactly what Rust's std +
    # our dependency tree need on *-pc-windows-msvc, per
    # `cargo rustc -- --print native-static-libs` (see build-windows.sh's
    # own output) — not guessed, re-verify that list if blockwork-ffi's Rust
    # dependencies change.
    target_link_libraries(${PROJECT_NAME}
        ${BLOCKWORK_FFI_LIB}
        kernel32 ntdll userenv ws2_32 dbghelp bcrypt
    )
endif()

if (APPLE AND NOT IOS)
    find_library(CORE_FOUNDATION_LIB CoreFoundation REQUIRED)
    find_library(APPLICATION_SERVICES_LIB ApplicationServices REQUIRED)
    find_library(SECURITY_LIB Security REQUIRED)
    find_library(IOKIT_LIB IOKit REQUIRED)
    find_library(APPKIT_LIB AppKit REQUIRED)
    find_library(CORE_GRAPHICS_LIB CoreGraphics REQUIRED)
    target_link_libraries(${PROJECT_NAME}
        ${BLOCKWORK_FFI_LIB}
        ${CORE_FOUNDATION_LIB}
        ${APPLICATION_SERVICES_LIB}
        ${SECURITY_LIB}
        ${IOKIT_LIB}
        ${APPKIT_LIB}
        ${CORE_GRAPHICS_LIB}
    )
endif()
