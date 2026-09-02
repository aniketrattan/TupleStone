cmake_minimum_required(VERSION 3.25)

project(tuplestone
  VERSION 0.1.0
  DESCRIPTION "A compact embedded SQL database engine written from scratch in C++20"
  LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Language and global settings
# ---------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)          # CONTRIBUTING.md: no compiler extensions
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)  # for clang-tidy and editors
# The project does not use C++ modules. Disable CMake's optional module scanner so
# C++20 builds remain portable across CMake versions and older GCC toolchains.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

option(TUPLESTONE_BUILD_TESTS "Build the unit tests" ON)
option(TUPLESTONE_BUILD_BENCH "Build the dependency-free API benchmark" OFF)
option(TUPLESTONE_BUILD_FUZZ "Build the libFuzzer targets (requires Clang)" OFF)
option(TUPLESTONE_PARANOID "Enable the expensive TUPLESTONE_PARANOID_ASSERT invariant checks" OFF)
option(TUPLESTONE_WERROR "Treat warnings as errors" ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# ---------------------------------------------------------------------------
# Warning and sanitizer flags, applied through one INTERFACE target
# ---------------------------------------------------------------------------

add_library(tuplestone_options INTERFACE)
target_compile_features(tuplestone_options INTERFACE cxx_std_20)

if(MSVC)
  target_compile_options(tuplestone_options INTERFACE /W4 /permissive- /Zc:preprocessor /utf-8 /EHsc)
  if(TUPLESTONE_WERROR)
    target_compile_options(tuplestone_options INTERFACE /WX)
  endif()
else()
  target_compile_options(tuplestone_options INTERFACE
    -Wall -Wextra -Wpedantic
    -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wcast-align
    -Wunused -Woverloaded-virtual -Wconversion -Wsign-conversion
    -Wdouble-promotion -Wformat=2 -Wimplicit-fallthrough)
  if(TUPLESTONE_WERROR)
    target_compile_options(tuplestone_options INTERFACE -Werror)
  endif()
endif()

if(TUPLESTONE_PARANOID)
  target_compile_definitions(tuplestone_options INTERFACE TUPLESTONE_PARANOID)
endif()

# MinGW's ld (binutils 2.43.1) aborts with a bare "ld returned 116" when linking
# a large C++ program against the shared libstdc++/libgcc DLLs — GoogleTest alone
# is enough to trigger it. Linking those two runtimes statically avoids it, and
# has the side benefit of producing test binaries that run without a DLL search
# path. See docs/design/00-skeleton.md.
if(MINGW)
  target_link_options(tuplestone_options INTERFACE -static-libstdc++ -static-libgcc)
endif()


target_compile_definitions(tuplestone_options INTERFACE
  TUPLESTONE_VERSION="${PROJECT_VERSION}")

# TUPLESTONE_SANITIZER is set by the asan/ubsan/tsan presets. The flags go on the
# INTERFACE target so they reach compile *and* link of every tuplestone target.
set(TUPLESTONE_SANITIZER "" CACHE STRING "Sanitizer to build with: address, undefined, thread, or empty")

if(TUPLESTONE_SANITIZER)
  if(MSVC)
    if(NOT TUPLESTONE_SANITIZER STREQUAL "address")
      message(FATAL_ERROR "MSVC supports only TUPLESTONE_SANITIZER=address")
    endif()
    target_compile_options(tuplestone_options INTERFACE /fsanitize=address)
  else()
    # MinGW's GCC ships no sanitizer runtimes at all; failing here with a clear
    # message beats a link error about missing __asan_* symbols.
    if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
      message(FATAL_ERROR
        "The MinGW GCC toolchain provides no sanitizer runtimes. "
        "Use the debug or release preset locally; the sanitizer presets run in CI on Linux. "
        "See docs/design/00-skeleton.md.")
    endif()
    target_compile_options(tuplestone_options INTERFACE
      -fsanitize=${TUPLESTONE_SANITIZER} -fno-omit-frame-pointer -g)
    target_link_options(tuplestone_options INTERFACE -fsanitize=${TUPLESTONE_SANITIZER})
    if(TUPLESTONE_SANITIZER STREQUAL "undefined")
      # A UBSan report must fail the build, not print and continue.
      target_compile_options(tuplestone_options INTERFACE
        -fno-sanitize-recover=all -fsanitize=float-divide-by-zero)
      target_link_options(tuplestone_options INTERFACE -fno-sanitize-recover=all)
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
# is what enforces it. Each src/<layer>/ is one target named tuplestone_<layer>,
# and a layer may only link the layer directly beneath it.
# ---------------------------------------------------------------------------

add_subdirectory(src/common)
add_subdirectory(src/disk)
add_subdirectory(src/buffer)
add_subdirectory(src/index)
add_subdirectory(src/table)
add_subdirectory(src/wal)
add_subdirectory(src/txn)
add_subdirectory(src/catalog)
add_subdirectory(src/sql)
add_subdirectory(src/planner)
add_subdirectory(src/exec)
add_subdirectory(src/api)
add_subdirectory(src/cli)

add_library(tuplestone STATIC)
target_sources(tuplestone PRIVATE src/tuplestone.cpp)
target_link_libraries(tuplestone
  PUBLIC tuplestone_common tuplestone_api tuplestone_disk tuplestone_buffer tuplestone_index tuplestone_table tuplestone_wal tuplestone_txn tuplestone_catalog tuplestone_sql tuplestone_planner tuplestone_exec
  PRIVATE tuplestone_options)
target_include_directories(tuplestone PUBLIC
  "${PROJECT_SOURCE_DIR}/include")
add_library(tuplestone::tuplestone ALIAS tuplestone)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

if(TUPLESTONE_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

message(STATUS "tuplestone ${PROJECT_VERSION}")
message(STATUS "  compiler   : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  build type : ${CMAKE_BUILD_TYPE}")
message(STATUS "  sanitizer  : ${TUPLESTONE_SANITIZER}")
message(STATUS "  paranoid   : ${TUPLESTONE_PARANOID}")
message(STATUS "  tests      : ${TUPLESTONE_BUILD_TESTS}")
