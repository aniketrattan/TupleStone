add_library(tuplestone_api STATIC
  db.cpp)

# The compact API implementation depends on the common value/status primitives.
target_link_libraries(tuplestone_api
  PUBLIC tuplestone_common
  PRIVATE tuplestone_options)
