add_library(tuplestone_common STATIC
  assert.cpp
  crc32c.cpp
  logger.cpp
  status.cpp
  value.cpp)

# `common` is the bottom of the layer map: it links nothing but the standard
# library and the shared option target.
target_link_libraries(tuplestone_common
  PUBLIC Threads::Threads
  PRIVATE tuplestone_options)

# Two include roots: the public headers (tuplestone/...) and src/ so that internal
# headers are included as "common/slice.h" rather than by relative path.
target_include_directories(tuplestone_common PUBLIC
  "${PROJECT_SOURCE_DIR}/include"
  "${PROJECT_SOURCE_DIR}/src")
