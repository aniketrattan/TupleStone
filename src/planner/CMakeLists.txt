add_library(tuplestone_planner STATIC plan.cpp)
target_link_libraries(tuplestone_planner PUBLIC tuplestone_common PRIVATE tuplestone_options)
target_include_directories(tuplestone_planner PUBLIC "${PROJECT_SOURCE_DIR}/src")
