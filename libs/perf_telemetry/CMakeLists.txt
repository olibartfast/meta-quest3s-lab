cmake_minimum_required(VERSION 3.22.1)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(perf_telemetry LANGUAGES CXX)
    include(CTest)
endif()

option(
    QUESTLAB_ENABLE_SANITIZERS
    "Enable AddressSanitizer and UndefinedBehaviorSanitizer for host tests"
    OFF
)

add_library(perf_telemetry STATIC
    src/perf_telemetry.cpp
)

set_target_properties(perf_telemetry PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
target_compile_features(perf_telemetry PUBLIC cxx_std_17)
target_include_directories(perf_telemetry PUBLIC include)
target_compile_options(perf_telemetry PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

if(BUILD_TESTING AND CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    add_subdirectory(tests)
endif()
