# Cross-compile rakupp for riscv64 Linux with Clang + lld and a riscv64 sysroot:
#
#   cmake -S . -B build-riscv64 -DCMAKE_BUILD_TYPE=Release \
#         -DCMAKE_TOOLCHAIN_FILE=tools/riscv64-toolchain.cmake \
#         -DCMAKE_SYSROOT=/path/to/riscv64-sysroot
#   cmake --build build-riscv64 -j 4
#
# Needs a Clang with the RISC-V backend and lld: Apple's clang has no RISC-V
# target, so on macOS this means `brew install llvm lld`. Any distro clang works
# on Linux. The sysroot is a riscv64 Linux root with the headers and libraries
# of libc6-dev and libstdc++-dev, and the binary needs that glibc or newer on
# the machine that runs it. docs/guide/COMPILERS.md ("Linux on RISC-V, built on
# a Mac") makes one with Docker and says how to run the result.
#
# A cross-compile ships an empty `--cnp` stencil table (CMakeLists.txt says why),
# as the native riscv64 build does too. release.yml's linux-riscv64 job builds
# the release archive with this file on an x86-64 runner; riscv64.yml builds
# natively on riscv64 hardware.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR riscv64)

set(RAKUPP_RISCV64_TRIPLE riscv64-linux-gnu)
set(CMAKE_C_COMPILER_TARGET ${RAKUPP_RISCV64_TRIPLE})
set(CMAKE_CXX_COMPILER_TARGET ${RAKUPP_RISCV64_TRIPLE})

if(NOT CMAKE_CXX_COMPILER)
  find_program(RAKUPP_RISCV64_CLANGXX clang++
               HINTS /opt/homebrew/opt/llvm/bin /usr/local/opt/llvm/bin
               NO_DEFAULT_PATH)
  if(NOT RAKUPP_RISCV64_CLANGXX)
    find_program(RAKUPP_RISCV64_CLANGXX clang++)
  endif()
  set(CMAKE_CXX_COMPILER ${RAKUPP_RISCV64_CLANGXX})
endif()
get_filename_component(RAKUPP_RISCV64_LLVM_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)

# The host's archiver writes a symbol index lld cannot read for ELF objects.
find_program(CMAKE_AR llvm-ar HINTS "${RAKUPP_RISCV64_LLVM_BIN}")
find_program(CMAKE_RANLIB llvm-ranlib HINTS "${RAKUPP_RISCV64_LLVM_BIN}")

# CMAKE_LINKER_TYPE survives a -DCMAKE_EXE_LINKER_FLAGS on the command line (as
# riscv64.yml passes); an older CMake has only the _INIT flags, which that
# replaces.
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.29)
  set(CMAKE_LINKER_TYPE LLD)
else()
  set(CMAKE_EXE_LINKER_FLAGS_INIT "-fuse-ld=lld")
  set(CMAKE_SHARED_LINKER_FLAGS_INIT "-fuse-ld=lld")
  set(CMAKE_MODULE_LINKER_FLAGS_INIT "-fuse-ld=lld")
endif()

if(NOT CMAKE_SYSROOT)
  message(FATAL_ERROR "riscv64 cross-compile: pass -DCMAKE_SYSROOT=<riscv64 Linux "
                      "root>; tools/riscv64-toolchain.cmake says how to make one.")
endif()

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
