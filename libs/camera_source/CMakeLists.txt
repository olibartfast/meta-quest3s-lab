cmake_minimum_required(VERSION 3.22.1)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(camera_source LANGUAGES CXX)
    include(CTest)
endif()

add_library(camera_source STATIC
    src/camera_source.cpp
    src/fixture_manifest.cpp
    src/latest_frame_queue.cpp
    src/replay_camera_adapter.cpp
    src/yuv_converter.cpp
)

if(NOT TARGET artifact_integrity)
    add_subdirectory(
        ${CMAKE_CURRENT_LIST_DIR}/../artifact_integrity
        ${CMAKE_CURRENT_BINARY_DIR}/artifact_integrity
    )
endif()

if(ANDROID)
    target_sources(camera_source PRIVATE
        src/meta_camera2_adapter.cpp
    )
    target_link_libraries(camera_source PUBLIC android log)
endif()

set_target_properties(camera_source PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

target_compile_features(camera_source PUBLIC cxx_std_17)
target_include_directories(camera_source PUBLIC include)
target_link_libraries(camera_source PUBLIC artifact_integrity)
target_compile_options(camera_source PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

if(BUILD_TESTING AND CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    add_subdirectory(tests)
endif()
