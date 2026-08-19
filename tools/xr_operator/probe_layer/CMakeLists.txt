cmake_minimum_required(VERSION 3.22.1)

project(openxr_probe_layer LANGUAGES CXX)

find_package(OpenXR REQUIRED CONFIG)

add_library(openxr_probe_layer SHARED src/probe_layer.cpp)

target_compile_features(openxr_probe_layer PRIVATE cxx_std_17)
target_compile_options(openxr_probe_layer
    PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(openxr_probe_layer
    PRIVATE XR_USE_PLATFORM_ANDROID)
target_link_libraries(openxr_probe_layer
    PRIVATE OpenXR::headers log)
