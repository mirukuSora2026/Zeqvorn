// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 miruku_sora2026

// =============================================
//
// Created At  : 11 / 12 / 2026
//
// Updated At  : 11 / 13 / 2026
//
// Created By  : miruku_sora2026
//
// File Name   : manifest.hpp
//
// Explanation : Defines the manifest fields read by the
//               first version of the ZeqFile parser.
//
// =============================================

#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "zeqvorn/base/result.hpp"

namespace zeqvorn::zf
{
    /** @brief Idnetifies the program and its executable in the Linux guest. */
    struct Program
    {
        std::string id;
        std::string version;
        std::string name;
        std::string description;
        std::string executable;
    };

    /** @brief Describes one local file copied into the package. */
    struct CopyStep
    {
        std::string from;
        std::string to;
        std::string mode;
    };

    /** @brief Contains the fields required by the local-copy package builder. */
    struct Manifest
    {
        Program program;
        std::string platform;
        std::vector<CopyStep> steps;
    };

    /**
     * @brief Parses schema version 1 from a YAML string.
     *
     * Reads program metadata, build.platform and copy steps.
     * The executable path belongs to the Linux guest.
     * The parser performs no file copies.
     */
    Result<Manifest> parse_manifest(std::string_view yaml);

    /** @brief Loads a plain YAML manifest from a local file. */
    Result<Manifest> load_manifest(const std::filesystem::path& path);
}
