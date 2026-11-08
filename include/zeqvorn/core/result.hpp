// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 miruku_sora2026

// =============================================
//
// Created At  : 08 / 11 / 2026
//
// Updated At  : 08 / 11 / 2026
//
// Created By  : miruku_sora2026
//
// File Name   : result.hpp
//
// Explanation : A class in `Zeqvorn` that safely represents both
//               the success value and the failure error of
//               a function as a single type.
//
// =============================================

#pragma once

#include <variant>
#include <utility>
#include <concepts>
#include <type_traits>

#include "error.hpp"

namespace zeqvorn
{
    /**
     * @brief Represents the result of an operation.
     *
     * @tparam T Type of the successful value.
     *
     * A Result<T> contains either:
     *    - A successful value of type T.
     *    - An Error describing the failure.
     *
     * Example:
     *
     * Result<int> result = 42;
     *
     * if (result) {
     *     // Operation succeeded
     * }
     */
    template <typename T>
    class [[nodiscard]] Result
    {
        static_assert(!std::is_void_v<T>,
            "Use Result<void> specialization.");

        static_assert(!std::is_reference_v<T>,
            "Result<T> cannot store references.");

        static_assert(!std::is_same_v<
            std::remove_cv_t<T>, Error>,
            "Result<Error> is not supported.");

        static_assert(std::is_object_v<T>,
            "Result<T> requires an object type.");

        static_assert(std::is_destructible_v<T>,
            "Result<T> requires a destructible type.");

    private:
        /**
         * @brief Stores the success value or failure error.
         *
         * Index 0 : T      (Success)
         * Index 1 : Error  (Failure)
         *
         * std::variant stores one of its alternative types
         * in a type-safe manner.
         *
         * In exceptional circumstances, std::variant may
         * become valueless_by_exception.
         */
        std::variant<T, Error> data_;

    public:
        /**
         * @brief Constructs a successful Result by copying.
         *
         * @param value The successful value.
         *
         * This constructor is available only when
         * T supports copy construction.
         */
        // std::in_place_index<0> :
        // Constructs T directly inside the variant.
        Result(const T& value)
            requires std::copy_constructible<T>
            : data_(std::in_place_index<0>, value)
        {
        }

        /**
         * @brief Constructs a successful Result by moving.
         *
         * @param value The successful value.
         *
         * Supports move-only types such as
         * std::unique_ptr and UniqueFd.
         */
        // std::move :
        // Converts an expression into an rvalue expression.
        // The actual move occurs during construction.
        Result(T&& value)
            requires std::move_constructible<T>
            : data_(
                std::in_place_index<0>,
                std::move(value)
            )
        {
        }

        /**
         * @brief Constructs a failed Result.
         *
         * @param error Error describing the failure.
         *
         * Index 1 corresponds to Error.
         */
        Result(Error error)
            : data_(
                std::in_place_index<1>,
                std::move(error)
            )
        {
        }

        /**
         * @brief Checks whether the operation succeeded.
         *
         * @return true if T is active.
         * @return false otherwise.
         *
         * index() returns the currently active
         * alternative index.
         */
        [[nodiscard]]
        bool has_value() const noexcept
        {
            return data_.index() == 0;
        }

        /**
         * @brief Checks whether an Error is stored.
         *
         * @return true if Error is active.
         * @return false otherwise.
         *
         * Note:
         * Both has_value() and has_error() may return
         * false if the variant is valueless_by_exception.
         */
        [[nodiscard]]
        bool has_error() const noexcept
        {
            return data_.index() == 1;
        }

        /**
         * @brief Converts Result into a boolean condition.
         *
         * Allows:
         *
         * if (result)
         * {
         *     // Success
         * }
         */
        // explicit :
        // Prevents unintended implicit conversions.
        [[nodiscard]]
        explicit operator bool() const noexcept
        {
            return has_value();
        }

        /**
         * @brief Returns a mutable reference to the value.
         *
         * @return Reference to the stored T.
         *
         * Throws std::bad_variant_access
         * if the Result does not contain T.
         */
        [[nodiscard]]
        // & : Only callable on lvalue objects.
        T& value() &
        {
            return std::get<0>(data_);
        }

        /**
         * @brief Returns a read-only reference to the value.
         *
         * @return Const reference to the stored T.
         *
         * Throws std::bad_variant_access
         * if the Result does not contain T.
         */
        [[nodiscard]]
        // const & : Supports read-only access.
        const T& value() const &
        {
            return std::get<0>(data_);
        }

        /**
         * @brief Returns an rvalue reference to the value.
         *
         * Useful when transferring ownership from
         * a temporary or movable Result.
         */
        [[nodiscard]]
        // && : Only callable on rvalue objects.
        // T&& : Returns an rvalue reference.
        T&& value() &&
        {
            return std::get<0>(std::move(data_));
        }

        /**
         * @brief Returns a mutable reference to the error.
         *
         * @return Reference to the stored Error.
         *
         * Throws std::bad_variant_access
         * if the Result does not contain Error.
         */
        [[nodiscard]]
        Error& error() &
        {
            return std::get<1>(data_);
        }

        /**
         * @brief Returns a read-only reference to the error.
         *
         * @return Const reference to the stored Error.
         *
         * Throws std::bad_variant_access
         * if the Result does not contain Error.
         */
        [[nodiscard]]
        // const & : Supports read-only error access.
        const Error& error() const &
        {
            return std::get<1>(data_);
        }

        /**
         * @brief Returns an rvalue reference to the error.
         *
         * Allows moving an Error from a temporary
         * or movable Result.
         */
        [[nodiscard]]
        Error&& error() &&
        {
            return std::get<1>(std::move(data_));
        }
    };

    /**
     * @brief Specialization for operations without a return value.
     *
     * Result<void> represents either:
     *     - Successful completion.
     *     - An Error.
     *
     * std::monostate represents successful completion
     * without storing an actual success value.
     */
    template <>
    class [[nodiscard]] Result<void>
    {
    private:
        /**
         * @brief Stores success or failure.
         *
         * Index 0 : std::monostate (Success)
         * Index 1 : Error          (Failure)
         */
        std::variant<std::monostate, Error> data_;

    public:
        /**
         * @brief Constructs a successful Result<void>.
         */
        Result() noexcept
            : data_(std::in_place_index<0>)
        {
        }

        /**
         * @brief Constructs a failed Result<void>.
         *
         * @param error Error describing the failure.
         */
        Result(Error error)
            : data_(
                std::in_place_index<1>,
                std::move(error)
            )
        {
        }

        /**
         * @brief Checks whether the operation succeeded.
         */
        [[nodiscard]]
        bool has_value() const noexcept
        {
            return data_.index() == 0;
        }

        /**
         * @brief Checks whether an Error is stored.
         */
        [[nodiscard]]
        bool has_error() const noexcept
        {
            return data_.index() == 1;
        }

        /**
         * @brief Converts Result<void> into a boolean condition.
         */
        [[nodiscard]]
        explicit operator bool() const noexcept
        {
            return has_value();
        }

        /**
         * @brief Verifies successful completion.
         *
         * Throws std::bad_variant_access
         * when the Result does not contain std::monostate.
         */
        void value() const
        {
            std::get<0>(data_);
        }

        /**
         * @brief Returns a mutable reference to the error.
         *
         * Throws std::bad_variant_access
         * if the Result does not contain Error.
         */
        [[nodiscard]]
        Error& error() &
        {
            return std::get<1>(data_);
        }

        /**
         * @brief Returns a read-only reference to the error.
         *
         * Throws std::bad_variant_access
         * if the Result does not contain Error.
         */
        [[nodiscard]]
        const Error& error() const &
        {
            return std::get<1>(data_);
        }

        /**
         * @brief Returns an rvalue reference to the error.
         *
         * Allows moving an Error from a temporary
         * or movable Result.
         */
        [[nodiscard]]
        Error&& error() &&
        {
            return std::get<1>(std::move(data_));
        }
    };
}
