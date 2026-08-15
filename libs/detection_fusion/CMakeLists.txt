cmake_minimum_required(VERSION 3.22.1)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(detection_fusion LANGUAGES CXX)
    include(CTest)
endif()

add_library(detection_fusion STATIC
    src/detection_fusion.cpp
)

if(NOT TARGET xr_math)
    add_subdirectory(
        ${CMAKE_CURRENT_LIST_DIR}/../xr_math
        ${CMAKE_CURRENT_BINARY_DIR}/xr_math
    )
endif()

set_target_properties(detection_fusion PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_features(detection_fusion PUBLIC cxx_std_17)
target_include_directories(detection_fusion PUBLIC include)
target_link_libraries(detection_fusion PUBLIC xr_math)
target_compile_options(detection_fusion PRIVATE -Wall -Wextra -Wpedantic)

if(BUILD_TESTING AND CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    add_subdirectory(tests)
endif()
