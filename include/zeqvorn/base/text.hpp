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
// File Name   : text.hpp
//
// Explanation : Provides ASCII string checks and
//               unsigned decimal parsing.
//
// =============================================

#pragma once

#include <charconv>
#include <cstdint>
#include <optional>
#include <string_view>
#include <system_error>

namespace zeqvorn
{
    /**
     * @brief Checks for an embedded NUL byte.
     *
     * Use before passing paths or arguments to APIs that accept C strings.
     */
    [[nodiscard]]
    constexpr bool contains_nul(std::string_view text) noexcept
    {
        return text.find('\0') != std::string_view::npos;
    }

    /**
     * @brief Removes leading and trailing ASCII whitespace.
     * @return A view into the input storage.
     */
    [[nodiscard]]
    constexpr std::string_view trim_ascii(std::string_view text) noexcept
    {
        constexpr std::string_view whitespace = " \t\n\r\f\v";
        const auto first = text.find_first_not_of(whitespace);

        if (first == std::string_view::npos)
        {
            return {};
        }

        const auto last = text.find_last_not_of(whitespace);
        return text.substr(first, last - first + 1);
    }

    /**
     * @brief Parses one unsigned base-10 integer after trimming whitespace.
     * @return nullopt for an empty, invalid, or overflowing value.
     *
     * 전체 토큰을 소비해야 성공한다. "42ms", "+42", "4 2"는 거부한다.
     */
    [[nodiscard]]
    inline std::optional<std::uint64_t> parse_u64(std::string_view text) noexcept
    {
        text = trim_ascii(text);

        if (text.empty())
        {
            return std::nullopt;
        }

        std::uint64_t value = 0;
        const auto end = text.data() + text.size();
        const auto parsed = std::from_chars(text.data(), end, value, 10);

        if (parsed.ec != std::errc{} || parsed.ptr != end)
        {
            return std::nullopt;
        }

        return value;
    }
}