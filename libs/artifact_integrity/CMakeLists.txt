cmake_minimum_required(VERSION 3.22.1)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(artifact_integrity LANGUAGES CXX)
endif()

add_library(artifact_integrity STATIC
    src/sha256.cpp
)

set_target_properties(artifact_integrity PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

target_compile_features(artifact_integrity PUBLIC cxx_std_17)
target_include_directories(artifact_integrity PUBLIC include)
target_compile_options(artifact_integrity PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)
