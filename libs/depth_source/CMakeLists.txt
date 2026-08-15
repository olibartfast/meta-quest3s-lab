cmake_minimum_required(VERSION 3.22.1)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(depth_source LANGUAGES CXX)
    include(CTest)
endif()

add_library(depth_source STATIC
    src/depth_math.cpp
    src/depth_source.cpp
)

if(NOT TARGET xr_math)
    add_subdirectory(
        ${CMAKE_CURRENT_LIST_DIR}/../xr_math
        ${CMAKE_CURRENT_BINARY_DIR}/xr_math
    )
endif()

set_target_properties(depth_source PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
target_compile_features(depth_source PUBLIC cxx_std_17)
target_include_directories(depth_source PUBLIC include)
target_link_libraries(depth_source PUBLIC xr_math)

if(ANDROID)
    # The Meta adapter needs OpenXR and Vulkan, so it is excluded from the
    # host build. The portable depth math and point conversion above stay
    # testable without a headset.
    target_sources(depth_source PRIVATE
        src/meta_environment_depth_adapter.cpp
    )
    if(NOT TARGET xr_core)
        add_subdirectory(
            ${CMAKE_CURRENT_LIST_DIR}/../xr_core
            ${CMAKE_CURRENT_BINARY_DIR}/xr_core
        )
    endif()
    target_link_libraries(depth_source PUBLIC xr_core)
endif()
target_compile_options(depth_source PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

if(BUILD_TESTING AND CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    add_subdirectory(tests)
endif()
