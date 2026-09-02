add_library(tuplestone_buffer STATIC buffer_pool_manager.cpp lruk_replacer.cpp)
target_link_libraries(tuplestone_buffer PUBLIC tuplestone_disk PRIVATE tuplestone_options)
target_include_directories(tuplestone_buffer PUBLIC "${PROJECT_SOURCE_DIR}/src")
