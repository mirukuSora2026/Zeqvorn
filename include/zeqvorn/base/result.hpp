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
// File Name   : result.hpp
//
// Explanation : Represents success values and errors,
//               including operations with no result value.
//
// =============================================

#pragma once

#include <concepts>
#include <type_traits>
#include <utility>
#include <variant>

#include "error.hpp"

namespace zeqvorn
{
    /**
     * @brief Stores a successful T or a failure Error.
     *
     * Supports move-only values. Check the active alternative before
     * calling value() or error().
     */
    template <typename T>
    class [[nodiscard]] Result
    {
        static_assert(std::is_object_v<T> && !std::is_array_v<T>);
        static_assert(!std::is_same_v<std::remove_cv_t<T>, Error>);

    private:
        std::variant<T, Error> data_;

    public:
        Result(const T& value) requires std::copy_constructible<T>
            : data_(std::in_place_index<0>, value)
        {
        }

        Result(T&& value) requires std::move_constructible<T>
            : data_(std::in_place_index<0>, std::move(value))
        {
        }

        Result(Error error)
            : data_(std::in_place_index<1>, std::move(error))
        {
        }

        [[nodiscard]] bool has_value() const noexcept { return data_.index() == 0; }
        [[nodiscard]] bool has_error() const noexcept { return data_.index() == 1; }
        [[nodiscard]] explicit operator bool() const noexcept { return has_value(); }

        // 다른 대안을 읽으면 std::get이 std::bad_variant_access를 던진다.
        [[nodiscard]] T& value() & { return std::get<0>(data_); }
        [[nodiscard]] const T& value() const & { return std::get<0>(data_); }
        [[nodiscard]] T&& value() && { return std::get<0>(std::move(data_)); }

        [[nodiscard]] Error& error() & { return std::get<1>(data_); }
        [[nodiscard]] const Error& error() const & { return std::get<1>(data_); }
        [[nodiscard]] Error&& error() && { return std::get<1>(std::move(data_)); }
    };

    /**
     * @brief Represents success without a payload, or a failure.
     *
     * Default construction selects success. A function that finishes
     * successfully can return {}.
     */
    template <>
    class [[nodiscard]] Result<void>
    {
    private:
        std::variant<std::monostate, Error> data_;

    public:
        Result() noexcept = default;

        Result(Error error)
            : data_(std::in_place_index<1>, std::move(error))
        {
        }

        [[nodiscard]] bool has_value() const noexcept { return data_.index() == 0; }
        [[nodiscard]] bool has_error() const noexcept { return data_.index() == 1; }
        [[nodiscard]] explicit operator bool() const noexcept { return has_value(); }

        void value() const
        {
            (void)std::get<0>(data_);
        }

        [[nodiscard]] Error& error() & { return std::get<1>(data_); }
        [[nodiscard]] const Error& error() const & { return std::get<1>(data_); }
        [[nodiscard]] Error&& error() && { return std::get<1>(std::move(data_)); }
    };
}