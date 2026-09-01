add_library(nanosql_api STATIC
  db.cpp)

# Until M12 the api layer is a set of stubs, so `common` is the only real
# dependency. As layers land it gains exec, and nothing above it.
target_link_libraries(nanosql_api
  PUBLIC nanosql_common
  PRIVATE nanosql_options)
