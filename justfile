# Cross-compilation config (override via env or `just --set`)
export SPLAT_DIR := env_var_or_default("SPLAT_DIR", "/home/treetrain1/Documents/git/splat")
export TOOLCHAIN := env_var_or_default("TOOLCHAIN", "/home/treetrain1/Documents/git/toolchain/clang-cl-msvc.cmake")
export HOST_ARCH := env_var_or_default("HOST_ARCH", "x86_64")
export MACROS_REPO := env_var_or_default("MACROS_REPO", "/home/treetrain1/RustroverProjects/Macros")

build_dir := "build-win"
mod_id := "dev.ethanstokes.macros"
macros_ffi_lib := MACROS_REPO / "target/x86_64-pc-windows-msvc/release/macros_ffi.lib"
macros_ffi_include := MACROS_REPO / "macros-ffi/include"

default: build

# Rebuilds macros_ffi.lib (embedded Rust recording/playback engine) — cheap
# no-op if src hasn't changed.
ffi:
    {{MACROS_REPO}}/macros-ffi/build-windows.sh

# Configure the cross-compile build directory
configure: ffi
    cmake -DCMAKE_TOOLCHAIN_FILE={{TOOLCHAIN}} -DCMAKE_BUILD_TYPE=Release -DMACROS_FFI_LIB={{macros_ffi_lib}} -DMACROS_FFI_INCLUDE_DIR={{macros_ffi_include}} -G Ninja -B {{build_dir}}

# Build the mod, configuring first if the build directory doesn't exist yet
build: ffi
    #!/usr/bin/env bash
    set -euo pipefail
    [ -d "{{build_dir}}" ] || just configure
    cmake --build {{build_dir}} --config Release

# Build and install the .geode package into the current Geode profile
install: build
    geode package install {{build_dir}}/{{mod_id}}.geode

# Build, install, then launch Geometry Dash
run: install
    geode run

# Remove the build directory
clean:
    rm -rf {{build_dir}}
