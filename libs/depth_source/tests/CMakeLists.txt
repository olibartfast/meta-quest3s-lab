add_executable(depth_math_tests depth_math_tests.cpp)
target_link_libraries(depth_math_tests PRIVATE depth_source)
target_compile_options(depth_math_tests PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME depth_math_tests COMMAND depth_math_tests)
