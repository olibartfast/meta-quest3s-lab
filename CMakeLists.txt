cmake_minimum_required(VERSION 3.22)
project(meta_quest3s_lab)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Android-specific settings
if(ANDROID)
    set(CMAKE_ANDROID_ARCH_ABI arm64-v8a)
    set(CMAKE_ANDROID_NDK $ENV{ANDROID_NDK_ROOT})
    set(CMAKE_SYSTEM_NAME Android)
    set(CMAKE_SYSTEM_VERSION 29) # Android 10 minimum
    set(CMAKE_ANDROID_STL_TYPE c++_shared)
endif()

# OpenXR SDK
include(FetchContent)
FetchContent_Declare(
    openxr
    GIT_REPOSITORY https://github.com/KhronosGroup/OpenXR-SDK.git
    GIT_TAG release-1.0.33
)
FetchContent_MakeAvailable(openxr)

# Meta XR SDK (OpenXR extensions)
FetchContent_Declare(
    meta_xr_sdk
    GIT_REPOSITORY https://github.com/meta-quest/Meta-OpenXR-SDK.git
    GIT_TAG main
)
FetchContent_MakeAvailable(meta_xr_sdk)

# GLM for math
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 0.9.9.8
)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
FetchContent_MakeAvailable(glm)

# Source files
file(GLOB_RECURSE SOURCES 
    "src/*.cpp"
    "src/core/*.cpp"
    "src/xr/*.cpp"
    "src/rendering/*.cpp"
    "src/utils/*.cpp"
)

# Add native app glue as static library (following Meta's approach)
if(ANDROID)
    add_library(app_glue STATIC ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c)
endif()

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${openxr_SOURCE_DIR}/include
    ${meta_xr_sdk_SOURCE_DIR}/OpenXR/Include
    ${glm_SOURCE_DIR}
    ${ANDROID_NDK}/sources/android/native_app_glue
)

# Create shared library for Android
add_library(${PROJECT_NAME} SHARED ${SOURCES})

target_link_libraries(${PROJECT_NAME}
    android
    app_glue
    log
    EGL
    GLESv3
    openxr_loader
)

# Compiler flags
target_compile_options(${PROJECT_NAME} PRIVATE
    -Wall
    -Wextra
    -O3
    -ffast-math
    -march=armv8-a+simd
)

# Link-time optimization
set_target_properties(${PROJECT_NAME} PROPERTIES
    INTERPROCEDURAL_OPTIMIZATION TRUE
)