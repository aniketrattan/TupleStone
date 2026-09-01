add_library(nanosql_common STATIC
  assert.cpp
  crc32c.cpp
  logger.cpp
  status.cpp
  value.cpp)

# `common` is the bottom of the layer map: it links nothing but the standard
# library and the shared option target.
target_link_libraries(nanosql_common
  PUBLIC Threads::Threads
  PRIVATE nanosql_options)

# Two include roots: the public headers (nanosql/...) and src/ so that internal
# headers are included as "common/slice.h" rather than by relative path.
target_include_directories(nanosql_common PUBLIC
  "${PROJECT_SOURCE_DIR}/include"
  "${PROJECT_SOURCE_DIR}/src")
