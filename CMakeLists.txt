cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if ("${CMAKE_SYSTEM_NAME}" STREQUAL "iOS" OR IOS)
    set(CMAKE_OSX_ARCHITECTURES "arm64")
else()
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
endif()
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

project(macros VERSION 1.0.0)

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

# macros-ffi: the Rust recording/playback engine (macros-core), embedded
# directly instead of talked to over the loopback IPC connection the old
# IpcClient used. Built separately (see macros-ffi/build-windows.sh and
# build-macos.sh in the Macros repo) since this is a different Cargo
# workspace with no submodule relationship to this one — point these at the
# artifacts that script produces.
if (NOT DEFINED MACROS_FFI_LIB)
    message(FATAL_ERROR "MACROS_FFI_LIB must point to the built macros_ffi static library (run macros-ffi/build-windows.sh or build-macos.sh in the Macros repo, then pass e.g. -DMACROS_FFI_LIB=/path/to/macros_ffi.lib)")
endif()
if (NOT DEFINED MACROS_FFI_INCLUDE_DIR)
    message(FATAL_ERROR "MACROS_FFI_INCLUDE_DIR must point to the directory containing the cbindgen-generated macros_ffi.h (macros-ffi/include in the Macros repo)")
endif()

target_include_directories(${PROJECT_NAME} PRIVATE ${MACROS_FFI_INCLUDE_DIR})

if (WIN32)
    # kernel32/ntdll/userenv/ws2_32/dbghelp: exactly what Rust's std +
    # our dependency tree need on *-pc-windows-msvc, per
    # `cargo rustc -- --print native-static-libs` (see build-windows.sh's
    # own output) — not guessed, re-verify that list if macros-ffi's Rust
    # dependencies change.
    target_link_libraries(${PROJECT_NAME}
        ${MACROS_FFI_LIB}
        kernel32 ntdll userenv ws2_32 dbghelp
    )
endif()

if (APPLE AND NOT IOS)
    find_library(CORE_FOUNDATION_LIB CoreFoundation REQUIRED)
    find_library(APPLICATION_SERVICES_LIB ApplicationServices REQUIRED)
    target_link_libraries(${PROJECT_NAME}
        ${MACROS_FFI_LIB}
        ${CORE_FOUNDATION_LIB}
        ${APPLICATION_SERVICES_LIB}
    )
endif()
