# remember to actually set these..
export SPLAT_DIR=/home/treetrain1/Documents/git/splat
export TOOLCHAIN=/home/treetrain1/Documents/git/toolchain/clang-cl-msvc.cmake
export HOST_ARCH=x86_64
export MACROS_REPO=/home/treetrain1/RustroverProjects/Macros

# Rebuilds macros_ffi.lib (embedded Rust recording/playback engine) first —
# cheap no-op if src hasn't changed — then points CMake at it.
SPLAT_DIR=$SPLAT_DIR "$MACROS_REPO/macros-ffi/build-windows.sh"

# you can also use `-G Ninja` here
cmake \
  -DCMAKE_TOOLCHAIN_FILE=$TOOLCHAIN \
  -DCMAKE_BUILD_TYPE=Release \
  -DMACROS_FFI_LIB=$MACROS_REPO/target/x86_64-pc-windows-msvc/release/macros_ffi.lib \
  -DMACROS_FFI_INCLUDE_DIR=$MACROS_REPO/macros-ffi/include \
  -G Ninja \
  -B build-win

cmake --build build-win --config Release
