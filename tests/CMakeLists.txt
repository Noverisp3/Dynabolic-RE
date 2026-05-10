# Test CMakeLists.txt for Dynabolic-LM

# Simple test executable
add_executable(test_graph test_graph.cpp)
target_link_libraries(test_graph PRIVATE dynabolic_lib Threads::Threads)

# Add test to CTest
add_test(NAME GraphTest COMMAND test_graph)

# Custom test to run basic functionality
add_custom_target(run_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    DEPENDS test_graph
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running tests...")