cmake_minimum_required(VERSION 3.16)

# On macOS, default the build architecture to the HOST's — not to whatever
# architecture `cmake` itself happens to be built for.
#
# CMake infers the target from the architecture of its OWN process, so an
# x86_64 cmake running under Rosetta on an Apple Silicon machine configures an
# x86_64 build from the documented command in README.md. That is not
# hypothetical: on the machine of record the only cmake is
# /usr/local/bin/cmake, an Intel-Homebrew x86_64 binary that precedes
# /opt/homebrew on PATH, so `cmake -S . -B build && cmake --build build`
# produced a TRANSLATED binary at the most-documented path in the project.
#
# It cost three separate gate defects before anyone looked at why: perf-guard
# and run-optbench both measured that binary instead of the native one, and
# doc-examples-diff swept the documentation with it. Translation costs a uniform
# 1.7-2x, so every one of those numbers was wrong in the same direction.
#
# hw.optional.arm64 answers for the HOST even when the asking process is
# translated; `uname -m` reports the caller's own architecture and would repeat
# the bug. An explicit -DCMAKE_OSX_ARCHITECTURES (build-x64, build-universal,
# and CI's release matrix) is left exactly as given.
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
  execute_process(COMMAND /usr/sbin/sysctl -n hw.optional.arm64
                  OUTPUT_VARIABLE _rakupp_host_arm64
                  OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  # Only ACT on a positive answer. If sysctl is missing or silent we do not know
  # the host, and guessing "x86_64" there would force a translated build on the
  # very machines this block exists to protect — a worse failure than the one it
  # fixes, and a silent one. Say nothing and let CMake behave exactly as before.
  if(_rakupp_host_arm64 STREQUAL "1")
    set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Build for the host architecture" FORCE)
    message(STATUS "rakupp: building for the host architecture arm64")
  elseif(_rakupp_host_arm64 STREQUAL "0")
    set(CMAKE_OSX_ARCHITECTURES "x86_64" CACHE STRING "Build for the host architecture" FORCE)
    message(STATUS "rakupp: building for the host architecture x86_64")
  else()
    message(STATUS "rakupp: could not determine the host architecture; leaving "
                   "CMAKE_OSX_ARCHITECTURES to CMake. If this build comes out "
                   "translated, pass -DCMAKE_OSX_ARCHITECTURES explicitly.")
  endif()
endif()

project(RakuPP VERSION 3.25.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

if(MSVC)
  # Static CRT (/MT) by default: `--exe` compiles user programs with /MT and
  # links them against rakupp_rt.lib, so the library must match or every link
  # fails with LNK2038 RuntimeLibrary mismatch (issue #1). The CI release build
  # passes the same value explicitly; -DCMAKE_MSVC_RUNTIME_LIBRARY on the
  # command line still overrides this.
  if(NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  endif()
  # /utf-8 is essential — the sources contain UTF-8 string literals (Raku's ｢｣
  # match quotes, Unicode tables); without it MSVC decodes them as the system
  # codepage and corrupts them. /EHsc = standard C++ exceptions; /Zc:__cplusplus
  # reports the real value; /permissive- for conformance. The /wd disables silence
  # the pervasive intentional 64<->32 narrowing in the numeric tower.
  add_compile_options(/W3 /EHsc /permissive- /Zc:__cplusplus /utf-8
                      /wd4244 /wd4267 /wd4146 /wd4018 /wd4996)
  add_compile_definitions(_USE_MATH_DEFINES _CRT_SECURE_NO_WARNINGS
                          NOMINMAX WIN32_LEAN_AND_MEAN)
else()
  add_compile_options(-Wall -Wextra -Wno-unused-parameter)
endif()

# Single source of truth for the version — bump `project(... VERSION ...)` above.
add_compile_definitions(RAKUPP_VERSION="${PROJECT_VERSION}")

# ---- build identity ---------------------------------------------------------
# `git describe --always --dirty=-modified` + the build date, regenerated per build
# (not at configure time — see cmake/BuildInfo.cmake for why) into a header that
# only src/BuildInfo.cpp includes. copy_if_different keeps an unchanged build
# from touching it, so this costs one recompiled file per commit and nothing at
# all otherwise. Exposed to Raku as $*RAKU.compiler.build / .build-date.
set(RAKUPP_BUILD_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/rakupp_build.h")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
include_directories("${CMAKE_CURRENT_BINARY_DIR}/generated")
# Stamp once at CONFIGURE time as well, so the header exists before anything is
# compiled. Without it the first compile of BuildInfo.cpp sees no header, its
# __has_include is false, and no dependency on the header is ever recorded — so
# the file it later generates would never trigger a recompile and the binary
# would report "unknown" forever.
execute_process(COMMAND "${CMAKE_COMMAND}"
                        -DSRC_DIR=${CMAKE_CURRENT_SOURCE_DIR}
                        -DOUT_FILE=${RAKUPP_BUILD_HEADER}
                        -DVERSION=${PROJECT_VERSION}
                        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/BuildInfo.cmake")
add_custom_target(rakupp_build_info ALL
  BYPRODUCTS "${RAKUPP_BUILD_HEADER}"
  # No inner quotes: VERBATIM passes the argument through as written, so
  # -DOUT_FILE="…" would make the quotes part of the path itself.
  COMMAND "${CMAKE_COMMAND}"
          -DSRC_DIR=${CMAKE_CURRENT_SOURCE_DIR}
          -DOUT_FILE=${RAKUPP_BUILD_HEADER}
          -DVERSION=${PROJECT_VERSION}
          -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/BuildInfo.cmake"
  COMMENT "Stamping build identity (git describe + date)"
  VERBATIM)

find_package(Threads REQUIRED)

file(GLOB_RECURSE RAKUPP_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

# The runtime (everything except the CLI entry point) is a static library, so
# it can be linked both into `rakupp` and into standalone executables produced
# by `rakupp --compile`.
set(RAKUPP_MAIN "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")
list(REMOVE_ITEM RAKUPP_SOURCES "${RAKUPP_MAIN}")
# The REPL belongs to the CLI, not the runtime: keeping it out of rakupp_rt means
# no `--exe` binary carries a line editor it can never reach.
set(RAKUPP_REPL "${CMAKE_CURRENT_SOURCE_DIR}/src/Repl.cpp")
list(REMOVE_ITEM RAKUPP_SOURCES "${RAKUPP_REPL}")
# The JavaScript backend (--target=js) and its embedded runtime are CLI-only for
# the same reason: no `--exe` binary transpiles, and the 200 KB runtime blob
# would otherwise ride along in every one of them.
set(RAKUPP_JS "${CMAKE_CURRENT_SOURCE_DIR}/src/codegen/Js.cpp"
              "${CMAKE_CURRENT_SOURCE_DIR}/src/JsRuntimeSrc.cpp")
list(REMOVE_ITEM RAKUPP_SOURCES ${RAKUPP_JS})

# ---- the SLIM archive split (SLIM-PLAN P2) ----------------------------------
# The four cuttable feature groups live in their own archives, so `--exe` can
# later link a stub in place of any of them (P3). Nothing is cut yet: the CLI
# and every `--exe` binary still link all five, and total shipped bytes only
# grow by the archive headers. The grouping mirrors ucd_seam.h:
#   ucd_names  NAMES + NUMV            (uniname/uniparse/unival)
#   ucd_coll   the DUCET triple        (unicmp/coll/.collate)
#   ucd_props  SCRIPTS, BLOCKS, BIDI   (uniprop by name, <:Script<…>>)
#   parse      Lexer + Parser          (EVAL, require, runtime regexes)
set(RAKUPP_UCD_NAMES_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/unicode_names.cpp")
set(RAKUPP_UCD_COLL_SOURCES  "${CMAKE_CURRENT_SOURCE_DIR}/src/unicode_coll_gen.cpp")
set(RAKUPP_UCD_PROPS_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/unicode_scripts_gen.cpp"
                             "${CMAKE_CURRENT_SOURCE_DIR}/src/unicode_blocks_gen.cpp"
                             "${CMAKE_CURRENT_SOURCE_DIR}/src/unicode_bidi_gen.cpp")
set(RAKUPP_PARSE_SOURCES     "${CMAKE_CURRENT_SOURCE_DIR}/src/Lexer.cpp"
                             "${CMAKE_CURRENT_SOURCE_DIR}/src/Parser.cpp")
# The stubs (P3) are the OTHER half of the seam — throwing doubles linked in
# place of a feature's real archive when --slim cuts it. They must never sit
# in the same link as the real thing (an ODR violation waiting for a
# link-order change), so they come out of the glob before anything snapshots
# it: not in rt, not in librakupp, only in their own archive.
file(GLOB RAKUPP_STUBS_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/stubs/*.cpp")
list(REMOVE_ITEM RAKUPP_SOURCES ${RAKUPP_STUBS_SOURCES})
# The shared library keeps the FULL list: librakupp is the interpreter, the
# interpreter never slims, and in particular it always carries the parser —
# rk_eval/rk_run ARE the parser (SLIM-PLAN, "librakupp pins `eval`").
set(RAKUPP_ALL_SOURCES ${RAKUPP_SOURCES})
list(REMOVE_ITEM RAKUPP_SOURCES ${RAKUPP_UCD_NAMES_SOURCES} ${RAKUPP_UCD_COLL_SOURCES}
                                ${RAKUPP_UCD_PROPS_SOURCES} ${RAKUPP_PARSE_SOURCES})

add_library(rakupp_rt STATIC ${RAKUPP_SOURCES})
# BuildInfo.cpp reads the stamped header, so the stamp has to run first.
add_dependencies(rakupp_rt rakupp_build_info)
# Both spellings of the public headers resolve, because both exist after an
# install: an embedder gets <rakupp/rakupp.h> off include/, while `--exe`
# compiles generated code against the flat include/rakupp/ the install
# collapses everything into, where "rakupp_ext.h" is a plain sibling.
target_include_directories(rakupp_rt PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src"
                                            "${CMAKE_CURRENT_SOURCE_DIR}/include"
                                            "${CMAKE_CURRENT_SOURCE_DIR}/include/rakupp")
target_link_libraries(rakupp_rt PUBLIC Threads::Threads)
# On Windows, RK_API (rakupp_ext.h) expands to dllexport under RAKUPP_BUILDING,
# so the rk_* extension ABI lands in the export table of whatever links this
# library — the rakupp CLI and `--exe` binaries alike, either of which may host
# a dlopen'ed extension. Elsewhere the definition is a no-op.
target_compile_definitions(rakupp_rt PRIVATE RAKUPP_BUILDING)

if(WIN32)
  target_link_libraries(rakupp_rt PUBLIC ws2_32)   # Winsock (sockets, WSAPoll)
  # Target Windows 8+ APIs (GetCurrentThreadStackLimits; WSAPoll needs Vista+).
  # MSVC's SDK headers default new enough, but MinGW's default _WIN32_WINNT can
  # be Windows 7 — which hides the declaration and breaks the build.
  target_compile_definitions(rakupp_rt PUBLIC _WIN32_WINNT=0x0602)
endif()

# The feature archives, plus the stub archive (P3). Each compiles with the
# same include path and flags as the runtime; none needs RAKUPP_BUILDING (no
# RK_API symbol lives in them).
foreach(feat rakupp_ucd_names rakupp_ucd_coll rakupp_ucd_props rakupp_parse rakupp_stubs)
  string(TOUPPER "${feat}_SOURCES" FEAT_VAR)
  add_library(${feat} STATIC ${${FEAT_VAR}})
  target_include_directories(${feat} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
                                             "${CMAKE_CURRENT_SOURCE_DIR}/include"
                                             "${CMAKE_CURRENT_SOURCE_DIR}/include/rakupp")
  if(WIN32)
    target_compile_definitions(${feat} PRIVATE _WIN32_WINNT=0x0602)
  endif()
endforeach()
# rt and parse reference each other (Runtime.cpp drives the Parser; the Parser
# builds AST nodes whose helpers live in rt — nfcNormalize among them).
# Declaring the cycle lets CMake repeat the archives on a single-pass linker
# (GNU ld) instead of leaving an unresolved symbol to the link order gods.
target_link_libraries(rakupp_rt INTERFACE rakupp_parse)
target_link_libraries(rakupp_parse INTERFACE rakupp_rt)
install(TARGETS rakupp_ucd_names rakupp_ucd_coll rakupp_ucd_props rakupp_parse
        rakupp_stubs ARCHIVE DESTINATION lib)

# ---- librakupp — the shared library a host loads (ABI-PLAN A0) --------------
# OFF by default: it recompiles every runtime source (PIC, hidden visibility),
# roughly doubling a clean build, and the plain CLI neither links nor loads it.
# Release and CI builds turn it on; a local edit-compile loop never needs it.
option(RAKUPP_BUILD_SHARED
  "Build librakupp, the shared library embedders and language bindings load" OFF)
# A TEST configuration, not a shipping one: link the CLI against librakupp so
# the full Roast/module surface exercises the runtime running out of the shared
# object. Implies default (not hidden) visibility, because the CLI needs the
# whole C++ surface — the shipped librakupp exports only the rk_* C ABI.
option(RAKUPP_LINK_SHARED
  "Link the rakupp CLI against librakupp instead of the static runtime (test config)" OFF)

if(RAKUPP_BUILD_SHARED OR RAKUPP_LINK_SHARED)
  # The FULL list, not the split one: librakupp is the interpreter, and the
  # interpreter never slims — in particular it always carries the parser
  # (SLIM-PLAN, "librakupp pins `eval`": rk_eval/rk_run ARE the parser).
  add_library(rakupp_shared SHARED ${RAKUPP_ALL_SOURCES})
  add_dependencies(rakupp_shared rakupp_build_info)   # same stamp as rakupp_rt
  target_include_directories(rakupp_shared PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src"
                                                  "${CMAKE_CURRENT_SOURCE_DIR}/include"
                                                  "${CMAKE_CURRENT_SOURCE_DIR}/include/rakupp")
  target_link_libraries(rakupp_shared PUBLIC Threads::Threads)
  target_compile_definitions(rakupp_shared PRIVATE RAKUPP_BUILDING)
  set_target_properties(rakupp_shared PROPERTIES
    OUTPUT_NAME rakupp
    POSITION_INDEPENDENT_CODE ON
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR})
  if(RAKUPP_LINK_SHARED)
    # The test artifact must not wear the shipping name: with the CLI linking
    # it, it exports the whole C++ surface, and a librakupp.so that leaks
    # internals is exactly what tools/embed-smoke.raku exists to reject.
    set_target_properties(rakupp_shared PROPERTIES OUTPUT_NAME rakupp_testrt)
  else()
    # The shipping artifact: everything hidden except the RK_API-marked rk_*
    # surface. One leaked internal symbol is a permanent ABI liability —
    # tools/embed-smoke.raku checks the export table rather than assuming.
    set_target_properties(rakupp_shared PROPERTIES
      CXX_VISIBILITY_PRESET hidden
      VISIBILITY_INLINES_HIDDEN ON)
  endif()
  if(WIN32)
    if(NOT RAKUPP_LINK_SHARED)
      # librakupp.dll, not rakupp.dll: the executable's own import library (see
      # ENABLE_EXPORTS below) is already rakupp.lib, and the DLL's import
      # library must not fight it for the name. PREFIX "" stops MinGW doubling
      # the "lib". (The test artifact keeps its rakupp_testrt name everywhere.)
      set_target_properties(rakupp_shared PROPERTIES OUTPUT_NAME librakupp PREFIX "")
    endif()
    target_link_libraries(rakupp_shared PUBLIC ws2_32)
    target_compile_definitions(rakupp_shared PUBLIC _WIN32_WINNT=0x0602)
  endif()
  # Only the shipping artifact installs; a test-configuration librakupp_testrt
  # must never land in a prefix.
  if(RAKUPP_BUILD_SHARED)
    install(TARGETS rakupp_shared
            LIBRARY DESTINATION lib ARCHIVE DESTINATION lib RUNTIME DESTINATION bin)
  endif()
endif()

add_executable(rakupp "${RAKUPP_MAIN}" "${RAKUPP_REPL}" ${RAKUPP_JS})
if(RAKUPP_LINK_SHARED)
  target_link_libraries(rakupp PRIVATE rakupp_shared)
else()
  target_link_libraries(rakupp PRIVATE rakupp_rt
                        rakupp_ucd_names rakupp_ucd_coll rakupp_ucd_props rakupp_parse)
endif()

# An extension resolves rk_* from the host executable at load time, the way a
# Python C extension resolves Py_*. Mach-O executables export their globals by
# default; ELF ones do not — without the dynamic-list an extension's first rk_*
# call dies with an "undefined symbol" error on Linux and the BSDs. The list
# exports exactly the rk_* glob, where -rdynamic would export every C++ symbol.
if(UNIX AND NOT APPLE)
  target_link_options(rakupp PRIVATE
    "-Wl,--dynamic-list=${CMAKE_CURRENT_SOURCE_DIR}/include/rakupp/rakupp_ext.dynlist")
  set_target_properties(rakupp PROPERTIES
    LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/include/rakupp/rakupp_ext.dynlist")
endif()
# On Windows the RK_API dllexports (via rakupp_rt) put rk_* in rakupp.exe's
# export table; ENABLE_EXPORTS makes CMake treat the generated import library
# (rakupp.lib) as a real artifact, installed below, so an extension has
# something to link against — the piece EXTENSIONS.md promised but the build
# never produced.
if(WIN32)
  set_target_properties(rakupp PROPERTIES ENABLE_EXPORTS ON)
endif()

# Install layout (so `--exe` works after `cmake --install`): the binary in bin/,
# the runtime static library in lib/, and every header in include/rakupp/ — the
# `--exe` compile step needs the headers to build the generated C++. findRuntime()
# in main.cpp looks for exactly this <prefix>/{bin,lib,include/rakupp} layout.
install(TARGETS rakupp RUNTIME DESTINATION bin ARCHIVE DESTINATION lib)
install(TARGETS rakupp_rt ARCHIVE DESTINATION lib)
# Two source directories, ONE destination. The split is public vs internal (see
# include/README.md), and it is a source-tree distinction only: `--exe` compiles
# generated C++ against a single flat -I, so the installed tree stays flat and
# an internal header may still say #include "rakupp_ext.h".
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/rakupp/"
        DESTINATION include/rakupp
        FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp")
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/src/"
        DESTINATION include/rakupp
        FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp")
# the module installer `rakupp install` dispatches to (MODULES-PLAN Part A) —
# shipped beside the binary, never compiled into it
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/tools/install.raku"
        DESTINATION libexec/rakupp)
