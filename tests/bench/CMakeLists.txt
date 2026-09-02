# The benchmark is intentionally dependency-free: it uses steady_clock and the
# public API, so a clean clone does not need Google Benchmark to produce useful
# numbers. It is a release-only executable and is not part of ctest.
add_executable(tuplestone_bench main.cpp)
target_link_libraries(tuplestone_bench PRIVATE tuplestone tuplestone_options)
