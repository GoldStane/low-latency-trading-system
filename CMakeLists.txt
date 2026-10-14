cmake_minimum_required(VERSION 3.20)
project(low_latency_trading_system LANGUAGES NONE)

# The initial milestone is offline Python tooling. Enable CXX when runtime
# targets are introduced; do not require a compiler for offline analysis.
include(CTest)
if(BUILD_TESTING)
    find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
    add_test(NAME market_data_analysis
        COMMAND "${Python3_EXECUTABLE}" -m unittest discover -s tests/unit -v)
    set_tests_properties(market_data_analysis PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
endif()
