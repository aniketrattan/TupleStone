add_library(tuplestone_index STATIC b_plus_tree.cpp)
target_link_libraries(tuplestone_index PUBLIC tuplestone_common PRIVATE tuplestone_options)
target_include_directories(tuplestone_index PUBLIC "${PROJECT_SOURCE_DIR}/src")
