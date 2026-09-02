add_library(tuplestone_catalog STATIC catalog.cpp)
target_link_libraries(tuplestone_catalog PUBLIC tuplestone_common PRIVATE tuplestone_options)
target_include_directories(tuplestone_catalog PUBLIC "${PROJECT_SOURCE_DIR}/src")
