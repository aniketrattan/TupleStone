add_library(tuplestone_wal STATIC log_manager.cpp)
target_link_libraries(tuplestone_wal PUBLIC tuplestone_common PRIVATE tuplestone_options)
target_include_directories(tuplestone_wal PUBLIC "${PROJECT_SOURCE_DIR}/src")
