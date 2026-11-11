cmake_minimum_required(VERSION 3.22)

project(Zeqvorn
    VERSION 0.1.0
    LANGUAGES CXX
)

# pidfd, pipe2(), /proc, and the process tests require Linux.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR
        "Zeqvorn process module requires Linux"
    )
endif()

option(
    ZEQVORN_SANITIZERS
    "Enable AddressSanitizer and UBSan"
    OFF
)

# All targets use standard C++20 rather than compiler-specific language modes.
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(zeqvorn_process STATIC
    src/process/process_manager.cpp
    src/process/process_handle.cpp
    src/process/process_reaper.cpp
)

target_include_directories(zeqvorn_process PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_compile_features(zeqvorn_process PUBLIC
    cxx_std_20
)

# Linux 확장 API의 선언은 엔진 내부에만 필요하다.
# PUBLIC C++20 요구 사항과 include 경로는 CLI 및 테스트에도 전파된다.
target_compile_definitions(zeqvorn_process PRIVATE
    _GNU_SOURCE
)

target_compile_options(zeqvorn_process PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
)

add_executable(zeqvorn
    src/cli/main.cpp
)

target_link_libraries(zeqvorn PRIVATE
    zeqvorn_process
)

# CTest provides BUILD_TESTING, allowing library/CLI-only builds when disabled.
include(CTest)

if(BUILD_TESTING)
    find_package(Threads REQUIRED)

    add_executable(zeqvorn_tests
        tests/process_manager_test.cpp
    )

    target_link_libraries(zeqvorn_tests PRIVATE
        zeqvorn_process
        Threads::Threads
    )

    add_test(
        NAME zeqvorn_process_tests
        COMMAND zeqvorn_tests
    )

    add_executable(zeqvorn_core_tests
        tests/core_test.cpp
    )

    target_link_libraries(zeqvorn_core_tests PRIVATE
        zeqvorn_process
    )

    add_test(
        NAME zeqvorn_core_tests
        COMMAND zeqvorn_core_tests
    )

    set_tests_properties(
        zeqvorn_process_tests
        zeqvorn_core_tests
        PROPERTIES TIMEOUT 20
    )
endif()

# Instrument every executable as well as the static library. The final
# executable must link the sanitizer runtime used by the library's objects.
if(ZEQVORN_SANITIZERS)
    set(zeqvorn_sanitizer_targets zeqvorn_process zeqvorn)

    if(BUILD_TESTING)
        list(APPEND zeqvorn_sanitizer_targets
            zeqvorn_tests
            zeqvorn_core_tests
        )
    endif()

    foreach(tgt IN LISTS zeqvorn_sanitizer_targets)
        target_compile_options(${tgt} PRIVATE
            -fsanitize=address,undefined
            -fno-omit-frame-pointer
        )

        target_link_options(${tgt} PRIVATE
            -fsanitize=address,undefined
        )
    endforeach()
endif()
