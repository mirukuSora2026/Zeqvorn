# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 miruku_sora2026
#
# =============================================
#
# Created At  : 11 / 11 / 2026
#
# Updated At  : 11 / 12 / 2026
#
# Created By  : miruku_sora2026
#
# File Name   : CMakeLists.txt
#
# Explanation : Builds the ZeqFile compression example.
#
# =============================================

cmake_minimum_required(VERSION 3.22)
project(ZeqFile LANGUAGES CXX)

# Homebrew keeps libarchive in its own prefix on macOS. Search there
# after any prefixes supplied by the caller.
if(APPLE AND NOT CMAKE_CROSSCOMPILING)
    list(APPEND CMAKE_PREFIX_PATH
            "/opt/homebrew/opt/libarchive"
            "/usr/local/opt/libarchive"
    )
endif()

find_package(LibArchive REQUIRED)

add_executable(ZeqFile main.cpp)
set_target_properties(ZeqFile PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
)

target_link_libraries(ZeqFile PRIVATE LibArchive::LibArchive)
