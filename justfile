# Cross-compilation config: set these in the environment or in a `.env` file
# beside this justfile (see `.env.example`).
set dotenv-load

export SPLAT_DIR := env_var_or_default("SPLAT_DIR", "")
export TOOLCHAIN := env_var_or_default("TOOLCHAIN", "")
export HOST_ARCH := env_var_or_default("HOST_ARCH", "x86_64")
export BLOCKWORK_REPO := env_var_or_default("BLOCKWORK_REPO", "../Blockwork")

build_dir := "build-win"
mod_id := "com.blockworked.blockwork"
blockwork_ffi_lib := BLOCKWORK_REPO / "target/x86_64-pc-windows-msvc/release/blockwork_ffi.lib"
blockwork_ffi_include := BLOCKWORK_REPO / "blockwork-ffi/include"

default: build

# Rebuilds blockwork_ffi.lib (embedded Rust recording/playback engine) - cheap
# no-op if src hasn't changed.
ffi:
    {{BLOCKWORK_REPO}}/blockwork-ffi/build-windows.sh

# Configure the cross-compile build directory
configure: ffi
    @[ -n "{{TOOLCHAIN}}" ] || { echo "Set TOOLCHAIN to a clang-cl CMake toolchain file (see .env.example)" >&2; exit 1; }
    cmake -DCMAKE_TOOLCHAIN_FILE={{TOOLCHAIN}} -DCMAKE_BUILD_TYPE=Release -DBLOCKWORK_FFI_LIB={{blockwork_ffi_lib}} -DBLOCKWORK_FFI_INCLUDE_DIR={{blockwork_ffi_include}} -G Ninja -B {{build_dir}}

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
