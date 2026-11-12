# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 miruku_sora2026

# =============================================
#
# Created At  : 11 / 10 / 2026
#
# Updated At  : 11 / 12 / 2026
#
# Created By  : miruku_sora2026
#
# File Name   : CMakeLists.txt
#
# Explanation : Registers common C++ headers and builds
#               the native VM library for the target OS.
#
# =============================================

cmake_minimum_required(VERSION 3.22)
project(ZeqvornLibrary VERSION 0.1.0 LANGUAGES CXX)

add_library(zeqvorn_common INTERFACE)
add_library(zeqvorn::common ALIAS zeqvorn_common)
target_include_directories(zeqvorn_common INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_compile_features(zeqvorn_common INTERFACE cxx_std_20)

add_library(zeqvorn_vm STATIC native/vm_probe.cpp)
target_include_directories(zeqvorn_vm PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(zeqvorn_vm PRIVATE cxx_std_20)

set_target_properties(zeqvorn_vm PROPERTIES
        CXX_EXTENSIONS OFF
        POSITION_INDEPENDENT_CODE ON
)

if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    enable_language(OBJCXX)
    target_sources(zeqvorn_vm PRIVATE
            native/macos_vz.mm
            native/macos_vz_boot.mm
    )

    set_source_files_properties(native/macos_vz_boot.mm
        PROPERTIES COMPILE_OPTIONS "-fobjc-arc"
    )

    set_target_properties(zeqvorn_vm PROPERTIES
            OBJCXX_STANDARD 20
            OBJCXX_STANDARD_REQUIRED ON
            OBJCXX_EXTENSIONS OFF
    )

    find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
    find_library(VIRTUALIZATION_FRAMEWORK Virtualization REQUIRED)

    target_link_libraries(zeqvorn_vm PUBLIC
            "${FOUNDATION_FRAMEWORK}"
            "${VIRTUALIZATION_FRAMEWORK}"
    )
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(zeqvorn_vm PRIVATE native/linux_kvm.cpp)
elseif(WIN32)
    target_sources(zeqvorn_vm PRIVATE native/windows_whp.cpp)
    target_compile_definitions(zeqvorn_vm PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
else()
    message(FATAL_ERROR "Zeqvorn VM supports Windows, macOS, and Linux")
endif()

install(TARGETS zeqvorn_vm ARCHIVE DESTINATION lib)
install(DIRECTORY include/ DESTINATION include)