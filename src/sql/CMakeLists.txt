add_library(tuplestone_sql STATIC lexer.cpp parser.cpp binder.cpp)
target_link_libraries(tuplestone_sql PUBLIC tuplestone_common PRIVATE tuplestone_options)
target_include_directories(tuplestone_sql PUBLIC "${PROJECT_SOURCE_DIR}/src")
