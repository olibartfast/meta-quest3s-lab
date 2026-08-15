cmake_minimum_required(VERSION 3.22.1)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(object_detector LANGUAGES CXX)
    include(CTest)
endif()

if(NOT TARGET camera_source)
    add_subdirectory(
        ${CMAKE_CURRENT_LIST_DIR}/../camera_source
        ${CMAKE_CURRENT_BINARY_DIR}/camera_source
    )
endif()
if(NOT TARGET rfdetr_inference_core)
    set(QUESTLAB_RFDETR_BUILD_TOOL OFF CACHE BOOL "" FORCE)
    add_subdirectory(
        ${CMAKE_CURRENT_LIST_DIR}/../../tools/rfdetr_inference
        ${CMAKE_CURRENT_BINARY_DIR}/rfdetr_inference
    )
endif()

find_package(Threads REQUIRED)
add_library(object_detector STATIC
    src/object_detector.cpp
)
set_target_properties(object_detector PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
target_compile_features(object_detector PUBLIC cxx_std_17)
target_include_directories(object_detector PUBLIC include)
target_link_libraries(object_detector
    PUBLIC camera_source
    PRIVATE rfdetr_inference_core Threads::Threads
)
target_compile_options(object_detector PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

if(BUILD_TESTING AND CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    add_executable(object_detector_tests tests/object_detector_tests.cpp)
    target_link_libraries(object_detector_tests PRIVATE object_detector)
    target_compile_options(object_detector_tests PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
    add_test(NAME object_detector_tests COMMAND object_detector_tests)
endif()
