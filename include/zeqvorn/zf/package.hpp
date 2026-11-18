// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 miruku_sora2026

// =============================================
//
// Created At  : 11 / 14 / 2026
//
// Updated At  : 11 / 14 / 2026
//
// Created By  : miruku_sora2026
//
// File Name   : package.hpp
//
// Explanation : Declares the reader for the manifest
//               stored in a ZeqFile package.
//
// =============================================

#pragma once

#include "manifest.hpp"

namespace zeqvorn::zf
{
    /**
     * @brief Loads the YAML manifest from a gzip-compressed ZeqFile package.
     * @param path Host path to the package created by ZeqFile pack.
     * @return The parsed manifest or an error describing the failed check.
     *
     * The archive contains two top-level regular files: a .zf manifest
     * of up to 1 MiB and a .tar.gz payload. The reader checks both entries
     * and passes only the manifest bytes to the YAML parser.
     */
    Result<Manifest> load_package_manifest(const std::filesystem::path& path);
}
