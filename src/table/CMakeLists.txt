add_library(tuplestone_table STATIC table_heap.cpp)
target_link_libraries(tuplestone_table PUBLIC tuplestone_common PRIVATE tuplestone_options)
target_include_directories(tuplestone_table PUBLIC "${PROJECT_SOURCE_DIR}/src")
