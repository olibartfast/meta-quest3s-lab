cmake_minimum_required(VERSION 3.22.1)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(detection_projection LANGUAGES CXX)
    include(CTest)
endif()

if(NOT TARGET xr_math)
    add_subdirectory(
        ${CMAKE_CURRENT_LIST_DIR}/../xr_math
        ${CMAKE_CURRENT_BINARY_DIR}/xr_math
    )
endif()
if(NOT TARGET camera_source)
    add_subdirectory(
        ${CMAKE_CURRENT_LIST_DIR}/../camera_source
        ${CMAKE_CURRENT_BINARY_DIR}/camera_source
    )
endif()

add_library(detection_projection STATIC
    src/detection_projection.cpp
)
set_target_properties(detection_projection PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
target_compile_features(detection_projection PUBLIC cxx_std_17)
target_include_directories(detection_projection PUBLIC include)
target_link_libraries(detection_projection PUBLIC camera_source xr_math)
target_compile_options(detection_projection PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

if(BUILD_TESTING AND CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    add_executable(
        detection_projection_tests
        tests/detection_projection_tests.cpp
    )
    target_link_libraries(
        detection_projection_tests PRIVATE detection_projection
    )
    target_compile_options(detection_projection_tests PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
    add_test(
        NAME detection_projection_tests
        COMMAND detection_projection_tests
    )
endif()
