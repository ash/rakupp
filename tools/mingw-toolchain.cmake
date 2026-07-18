# Cross-compile rakupp for Windows with MinGW-w64 (e.g. `brew install mingw-w64`
# on macOS, `apt install g++-mingw-w64-x86-64` on Debian):
#
#   cmake -B build-mingw -DCMAKE_TOOLCHAIN_FILE=tools/mingw-toolchain.cmake \
#         -DCMAKE_BUILD_TYPE=Release .
#   cmake --build build-mingw -j 8
#
# Produces build-mingw/rakupp.exe (PE32+) and librakupp_rt.a. CI builds the
# same configuration natively in an MSYS2 MINGW64 shell (release.yml).
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
