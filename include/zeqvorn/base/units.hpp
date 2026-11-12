// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 miruku_sora2026

// =============================================
//
// Created At  : 11 / 11 / 2026
//
// Updated At  : 11 / 12 / 2026
//
// Created By  : miruku_sora2026
//
// File Name   : units.hpp
//
// Explanation : Defines binary byte units and checked
//               conversion from mebibytes to bytes.
//
// =============================================

#pragma once

#include <cstdint>
#include <limits>
#include <optional>

namespace zeqvorn
{
    inline constexpr std::uint64_t KiB = 1024;
    inline constexpr std::uint64_t MiB = 1024 * KiB;
    inline constexpr std::uint64_t GiB = 1024 * MiB;

    /**
     * @brief Converts mebibytes to bytes.
     * @return nullopt if the byte count cannot fit in uint64_t.
     */
    [[nodiscard]]
    constexpr std::optional<std::uint64_t> mib_to_bytes(
        std::uint64_t amount
    ) noexcept
    {
        // 곱셈 전에 범위를 검사해 unsigned 정수 오버플로를 방지한다.
        if (amount > (std::numeric_limits<std::uint64_t>::max)() / MiB)
        {
            return std::nullopt;
        }

        return amount * MiB;
    }
}