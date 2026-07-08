cmake_minimum_required(VERSION 3.16)
project(RakuPP VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra -Wno-unused-parameter)

# Single source of truth for the version — bump `project(... VERSION ...)` above.
add_compile_definitions(RAKUPP_VERSION="${PROJECT_VERSION}")

find_package(Threads REQUIRED)

file(GLOB_RECURSE RAKUPP_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

# The runtime (everything except the CLI entry point) is a static library, so
# it can be linked both into `rakupp` and into standalone executables produced
# by `rakupp --compile`.
set(RAKUPP_MAIN "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")
list(REMOVE_ITEM RAKUPP_SOURCES "${RAKUPP_MAIN}")

add_library(rakupp_rt STATIC ${RAKUPP_SOURCES})
target_include_directories(rakupp_rt PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(rakupp_rt PUBLIC Threads::Threads)

add_executable(rakupp "${RAKUPP_MAIN}")
target_link_libraries(rakupp PRIVATE rakupp_rt)

# Install layout (so `--exe` works after `cmake --install`): the binary in bin/,
# the runtime static library in lib/, and every header in include/rakupp/ — the
# `--exe` compile step needs the headers to build the generated C++. findRuntime()
# in main.cpp looks for exactly this <prefix>/{bin,lib,include/rakupp} layout.
install(TARGETS rakupp RUNTIME DESTINATION bin)
install(TARGETS rakupp_rt ARCHIVE DESTINATION lib)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/src/"
        DESTINATION include/rakupp
        FILES_MATCHING PATTERN "*.h")
