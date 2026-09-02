add_library(tuplestone_disk STATIC
  disk_manager.cpp)
target_link_libraries(tuplestone_disk PUBLIC tuplestone_common PRIVATE tuplestone_options)
target_include_directories(tuplestone_disk PUBLIC "${PROJECT_SOURCE_DIR}/src")
