add_executable(perf_telemetry_tests
    perf_telemetry_tests.cpp
)
target_link_libraries(perf_telemetry_tests PRIVATE perf_telemetry)
target_compile_options(perf_telemetry_tests PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Werror
)

if(QUESTLAB_ENABLE_SANITIZERS AND
   CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(perf_telemetry PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(perf_telemetry PRIVATE
        -fsanitize=address,undefined
    )
    target_compile_options(perf_telemetry_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(perf_telemetry_tests PRIVATE
        -fsanitize=address,undefined
    )
endif()

add_test(NAME perf_telemetry_tests COMMAND perf_telemetry_tests)
