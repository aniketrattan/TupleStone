add_library(tuplestone_txn STATIC transaction_manager.cpp)
target_link_libraries(tuplestone_txn PUBLIC tuplestone_common PRIVATE tuplestone_options)
target_include_directories(tuplestone_txn PUBLIC "${PROJECT_SOURCE_DIR}/src")
