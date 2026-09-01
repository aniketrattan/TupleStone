include(FetchContent)

# Test-only dependencies (CONTRIBUTING.md: nothing else gets added without an ADR).
# Pinned to exact tags so a CI run a year from now builds what it builds today.
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK ON CACHE BOOL "" FORCE)

FetchContent_Declare(googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.15.2
  GIT_SHALLOW TRUE
  SYSTEM)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

# Warnings-as-errors applies to nanosql's code, not to fetched dependencies.
function(nanosql_add_test name)
  add_executable(${name} ${ARGN})
  target_link_libraries(${name} PRIVATE nanosql nanosql_options GTest::gtest_main)
  gtest_discover_tests(${name}
    DISCOVERY_MODE PRE_TEST
    PROPERTIES TIMEOUT 300)
endfunction()

add_subdirectory(unit)

if(NANOSQL_BUILD_BENCH)
  add_subdirectory(bench)
endif()
