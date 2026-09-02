add_library(tuplestone_exec INTERFACE)
target_link_libraries(tuplestone_exec INTERFACE tuplestone_common tuplestone_planner)
target_include_directories(tuplestone_exec INTERFACE "${PROJECT_SOURCE_DIR}/src")
