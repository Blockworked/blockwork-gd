# remember to actually set these..
export SPLAT_DIR=/home/treetrain1/Documents/git/splat
export TOOLCHAIN=/home/treetrain1/Documents/git/toolchain/clang-cl-msvc.cmake
export HOST_ARCH=x86_64

# you can also use `-G Ninja` here
cmake \
  -DCMAKE_TOOLCHAIN_FILE=$TOOLCHAIN \
  -DCMAKE_BUILD_TYPE=Release \
  -G Ninja \
  -B build-win
  
cmake --build build-win --config Release
