cmake_minimum_required(VERSION 3.22.1)
project(rfdetr_inference LANGUAGES CXX)

include(CTest)

option(
    QUESTLAB_ENABLE_ONNXRUNTIME
    "Build the pinned ONNX Runtime inference backend"
    OFF
)
option(
    QUESTLAB_RFDETR_BUILD_TOOL
    "Build the host RF-DETR command-line executable"
    ON
)
set(
    ONNXRUNTIME_ROOT
    ""
    CACHE PATH
    "Extracted onnxruntime-linux-x64-1.21.0 directory"
)

if(NOT TARGET artifact_integrity)
    add_subdirectory(
        ${CMAKE_CURRENT_LIST_DIR}/../../libs/artifact_integrity
        ${CMAKE_CURRENT_BINARY_DIR}/artifact_integrity
    )
endif()
if(NOT TARGET camera_source)
    add_subdirectory(
        ${CMAKE_CURRENT_LIST_DIR}/../../libs/camera_source
        ${CMAKE_CURRENT_BINARY_DIR}/camera_source
    )
endif()

add_library(rfdetr_inference_core STATIC
    src/detection_io.cpp
    src/json_value.cpp
    src/model_contract.cpp
    src/onnx_session.cpp
    src/postprocessing.cpp
    src/preprocessing.cpp
    src/timing.cpp
)

target_compile_features(rfdetr_inference_core PUBLIC cxx_std_17)
target_include_directories(rfdetr_inference_core PUBLIC include)
target_include_directories(rfdetr_inference_core PRIVATE src)
target_link_libraries(rfdetr_inference_core PUBLIC
    artifact_integrity
    camera_source
)
target_compile_options(rfdetr_inference_core PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

if(QUESTLAB_ENABLE_ONNXRUNTIME)
    if(NOT ONNXRUNTIME_ROOT)
        message(FATAL_ERROR
            "QUESTLAB_ENABLE_ONNXRUNTIME requires ONNXRUNTIME_ROOT; "
            "run tools/rfdetr_inference/prepare_onnxruntime.sh"
        )
    endif()
    if(ANDROID AND
       EXISTS "${ONNXRUNTIME_ROOT}/headers/onnxruntime_cxx_api.h" AND
       EXISTS "${ONNXRUNTIME_ROOT}/jni/${ANDROID_ABI}/libonnxruntime.so")
        set(ONNXRUNTIME_INCLUDE_DIR "${ONNXRUNTIME_ROOT}/headers")
        set(ONNXRUNTIME_LIBRARY
            "${ONNXRUNTIME_ROOT}/jni/${ANDROID_ABI}/libonnxruntime.so"
        )
    else()
        find_path(
            ONNXRUNTIME_INCLUDE_DIR
            onnxruntime_cxx_api.h
            PATHS
                ${ONNXRUNTIME_ROOT}/include
                ${ONNXRUNTIME_ROOT}/headers
            NO_DEFAULT_PATH
            REQUIRED
        )
        find_library(
            ONNXRUNTIME_LIBRARY
            onnxruntime
            PATHS ${ONNXRUNTIME_ROOT}/lib
            NO_DEFAULT_PATH
            REQUIRED
        )
    endif()
    target_include_directories(
        rfdetr_inference_core PRIVATE ${ONNXRUNTIME_INCLUDE_DIR}
    )
    target_link_libraries(
        rfdetr_inference_core PRIVATE ${ONNXRUNTIME_LIBRARY}
    )
    target_compile_definitions(
        rfdetr_inference_core PRIVATE QUESTLAB_HAS_ONNXRUNTIME=1
    )
    set_target_properties(rfdetr_inference_core PROPERTIES
        BUILD_RPATH ${ONNXRUNTIME_ROOT}/lib
    )
endif()

if(QUESTLAB_RFDETR_BUILD_TOOL)
    add_executable(rfdetr_inference src/main.cpp)
    target_link_libraries(rfdetr_inference PRIVATE rfdetr_inference_core)
    target_compile_options(rfdetr_inference PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
    if(UNIX)
        add_executable(rfdetr_service src/service_main.cpp)
        target_link_libraries(rfdetr_service PRIVATE rfdetr_inference_core)
        target_compile_options(rfdetr_service PRIVATE
            -Wall
            -Wextra
            -Wpedantic
        )
    endif()
endif()

if(BUILD_TESTING)
    add_executable(rfdetr_inference_tests tests/rfdetr_inference_tests.cpp)
    target_link_libraries(
        rfdetr_inference_tests PRIVATE rfdetr_inference_core
    )
    target_compile_definitions(rfdetr_inference_tests PRIVATE
        QUESTLAB_RFDETR_MANIFEST_PATH="${CMAKE_CURRENT_LIST_DIR}/../rfdetr_export/model-manifest.json"
    )
    target_compile_options(rfdetr_inference_tests PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
    add_test(NAME rfdetr_inference_tests COMMAND rfdetr_inference_tests)
endif()
