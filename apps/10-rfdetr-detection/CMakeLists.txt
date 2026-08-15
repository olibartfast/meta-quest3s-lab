cmake_minimum_required(VERSION 3.22.1)
project(rfdetr_detection LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT ONNXRUNTIME_ROOT)
    message(FATAL_ERROR
        "Run tools/rfdetr_inference/prepare_onnxruntime_android.sh first"
    )
endif()

find_package(OpenXR REQUIRED CONFIG)
add_subdirectory(
    ../../libs/xr_core
    "${CMAKE_CURRENT_BINARY_DIR}/xr_core"
)
add_subdirectory(
    ../../libs/xr_meta_passthrough
    "${CMAKE_CURRENT_BINARY_DIR}/xr_meta_passthrough"
)
add_subdirectory(
    ../../libs/vulkan_renderer
    "${CMAKE_CURRENT_BINARY_DIR}/vulkan_renderer"
)
add_subdirectory(
    ../../libs/camera_source
    "${CMAKE_CURRENT_BINARY_DIR}/camera_source"
)
add_subdirectory(
    ../../libs/detection_projection
    "${CMAKE_CURRENT_BINARY_DIR}/detection_projection"
)
add_subdirectory(
    ../../libs/detection_fusion
    "${CMAKE_CURRENT_BINARY_DIR}/detection_fusion"
)
add_subdirectory(
    ../../libs/depth_source
    "${CMAKE_CURRENT_BINARY_DIR}/depth_source"
)
set(QUESTLAB_RFDETR_BUILD_TOOL OFF CACHE BOOL "" FORCE)
add_subdirectory(
    ../../libs/object_detector
    "${CMAKE_CURRENT_BINARY_DIR}/object_detector"
)

set(native_app_glue_dir "${ANDROID_NDK}/sources/android/native_app_glue")
add_library(android_native_app_glue STATIC
    "${native_app_glue_dir}/android_native_app_glue.c"
)
target_include_directories(android_native_app_glue PUBLIC "${native_app_glue_dir}")

add_library(rfdetr_detection SHARED
    src/main/cpp/main.cpp
)
target_compile_definitions(rfdetr_detection PRIVATE
    XR_USE_GRAPHICS_API_VULKAN
    XR_USE_PLATFORM_ANDROID
)
if(QUEST_ENABLE_VULKAN_VALIDATION)
    target_compile_definitions(rfdetr_detection PRIVATE
        QUEST_ENABLE_VULKAN_VALIDATION=1
    )
endif()
target_compile_options(rfdetr_detection PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wno-missing-field-initializers
)
target_link_libraries(rfdetr_detection PRIVATE
    camera_source
    detection_projection
    detection_fusion
    depth_source
    object_detector
    vulkan_renderer
    xr_meta_passthrough
    android_native_app_glue
    android
    log
)
target_link_options(rfdetr_detection PRIVATE "-uANativeActivity_onCreate")
