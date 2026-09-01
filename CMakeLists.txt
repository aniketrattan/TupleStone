cmake_minimum_required(VERSION 3.25)

project(nanosql
  VERSION 0.1.0
  DESCRIPTION "An embedded, ACID, MVCC relational database written from scratch in C++20"
  LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Language and global settings
# ---------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)          # CONTRIBUTING.md: no compiler extensions
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)  # for clang-tidy and editors

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

option(NANOSQL_BUILD_TESTS "Build the unit tests" ON)
option(NANOSQL_BUILD_BENCH "Build the Google Benchmark targets" OFF)
option(NANOSQL_BUILD_FUZZ "Build the libFuzzer targets (requires Clang)" OFF)
option(NANOSQL_PARANOID "Enable the expensive NANOSQL_PARANOID_ASSERT invariant checks" OFF)
option(NANOSQL_WERROR "Treat warnings as errors" ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# ---------------------------------------------------------------------------
# Warning and sanitizer flags, applied through one INTERFACE target
# ---------------------------------------------------------------------------

add_library(nanosql_options INTERFACE)
target_compile_features(nanosql_options INTERFACE cxx_std_20)

if(MSVC)
  target_compile_options(nanosql_options INTERFACE /W4 /permissive- /Zc:preprocessor /utf-8 /EHsc)
  if(NANOSQL_WERROR)
    target_compile_options(nanosql_options INTERFACE /WX)
  endif()
else()
  target_compile_options(nanosql_options INTERFACE
    -Wall -Wextra -Wpedantic
    -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wcast-align
    -Wunused -Woverloaded-virtual -Wconversion -Wsign-conversion
    -Wdouble-promotion -Wformat=2 -Wimplicit-fallthrough)
  if(NANOSQL_WERROR)
    target_compile_options(nanosql_options INTERFACE -Werror)
  endif()
endif()

if(NANOSQL_PARANOID)
  target_compile_definitions(nanosql_options INTERFACE NANOSQL_PARANOID)
endif()

# MinGW's ld (binutils 2.43.1) aborts with a bare "ld returned 116" when linking
# a large C++ program against the shared libstdc++/libgcc DLLs — GoogleTest alone
# is enough to trigger it. Linking those two runtimes statically avoids it, and
# has the side benefit of producing test binaries that run without a DLL search
# path. See docs/design/00-skeleton.md.
if(MINGW)
  target_link_options(nanosql_options INTERFACE -static-libstdc++ -static-libgcc)
endif()


target_compile_definitions(nanosql_options INTERFACE
  NANOSQL_VERSION="${PROJECT_VERSION}")

# NANOSQL_SANITIZER is set by the asan/ubsan/tsan presets. The flags go on the
# INTERFACE target so they reach compile *and* link of every nanosql target.
set(NANOSQL_SANITIZER "" CACHE STRING "Sanitizer to build with: address, undefined, thread, or empty")

if(NANOSQL_SANITIZER)
  if(MSVC)
    if(NOT NANOSQL_SANITIZER STREQUAL "address")
      message(FATAL_ERROR "MSVC supports only NANOSQL_SANITIZER=address")
    endif()
    target_compile_options(nanosql_options INTERFACE /fsanitize=address)
  else()
    # MinGW's GCC ships no sanitizer runtimes at all; failing here with a clear
    # message beats a link error about missing __asan_* symbols.
    if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
      message(FATAL_ERROR
        "The MinGW GCC toolchain provides no sanitizer runtimes. "
        "Use the debug or release preset locally; the sanitizer presets run in CI on Linux. "
        "See docs/design/00-skeleton.md.")
    endif()
    target_compile_options(nanosql_options INTERFACE
      -fsanitize=${NANOSQL_SANITIZER} -fno-omit-frame-pointer -g)
    target_link_options(nanosql_options INTERFACE -fsanitize=${NANOSQL_SANITIZER})
    if(NANOSQL_SANITIZER STREQUAL "undefined")
      # A UBSan report must fail the build, not print and continue.
      target_compile_options(nanosql_options INTERFACE
        -fno-sanitize-recover=all -fsanitize=float-divide-by-zero)
      target_link_options(nanosql_options INTERFACE -fno-sanitize-recover=all)
    endif()
  endif()
endif()

# Threads are used from M2 onward and by the M0 logger tests.
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
# Layer targets
#
# ARCHITECTURE.md §1: dependencies point strictly downward, and the link graph
# is what enforces it. Each src/<layer>/ is one target named nanosql_<layer>,
# and a layer may only link the layer directly beneath it.
# ---------------------------------------------------------------------------

add_subdirectory(src/common)
add_subdirectory(src/api)

add_library(nanosql STATIC)
target_sources(nanosql PRIVATE src/nanosql.cpp)
target_link_libraries(nanosql
  PUBLIC nanosql_common nanosql_api
  PRIVATE nanosql_options)
target_include_directories(nanosql PUBLIC
  "${PROJECT_SOURCE_DIR}/include")
add_library(nanosql::nanosql ALIAS nanosql)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

if(NANOSQL_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

message(STATUS "nanosql ${PROJECT_VERSION}")
message(STATUS "  compiler   : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  build type : ${CMAKE_BUILD_TYPE}")
message(STATUS "  sanitizer  : ${NANOSQL_SANITIZER}")
message(STATUS "  paranoid   : ${NANOSQL_PARANOID}")
message(STATUS "  tests      : ${NANOSQL_BUILD_TESTS}")
