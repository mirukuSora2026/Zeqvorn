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
// File Name   : error.hpp
//
// Explanation : Defines portable errors with optional
//               OS diagnostics and source locations.
//
// =============================================

#pragma once

#include <cerrno>
#include <source_location>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace zeqvorn
{
    enum class ErrorCode
    {
        InvalidArgument,
        NotFound,
        PermissionDenied,
        ProcessCreationFailed,
        ExecutionFailed,
        ResourceExhausted,
        Timeout,
        AlreadyExists,
        InvalidState,
        Unsupported,
        IoError,
        SystemError,
        InternalError
    };

    /** @brief Returns a readable name for an error category. */
    [[nodiscard]]
    constexpr const char* error_code_name(ErrorCode code) noexcept
    {
        switch (code)
        {
        case ErrorCode::InvalidArgument: return "InvalidArgument";
        case ErrorCode::NotFound: return "NotFound";
        case ErrorCode::PermissionDenied: return "PermissionDenied";
        case ErrorCode::ProcessCreationFailed: return "ProcessCreationFailed";
        case ErrorCode::ExecutionFailed: return "ExecutionFailed";
        case ErrorCode::ResourceExhausted: return "ResourceExhausted";
        case ErrorCode::Timeout: return "Timeout";
        case ErrorCode::AlreadyExists: return "AlreadyExists";
        case ErrorCode::InvalidState: return "InvalidState";
        case ErrorCode::Unsupported: return "Unsupported";
        case ErrorCode::IoError: return "IoError";
        case ErrorCode::SystemError: return "SystemError";
        case ErrorCode::InternalError: return "InternalError";
        }

        return "Unknown";
    }

    /**
     * @brief Describes a Zeqvorn failure and where it was recorded.
     *
     * code는 호출자가 오류를 분류할 때 사용한다. sys_error는
     * 로그에 표시할 수 있도록 OS 오류 번호와 범주를 보존한다.
     */
    struct Error
    {
        ErrorCode code;
        std::string msg;
        std::error_code sys_error;
        std::source_location location;

        explicit Error(
            ErrorCode category,
            std::string message,
            std::error_code system_error = {},
            std::source_location at = std::source_location::current()
        )
            : code(category),
              msg(std::move(message)),
              sys_error(system_error),
              location(at)
        {
        }

        /** @brief Wraps an errno value saved after a failed call. */
        [[nodiscard]]
        static Error from_errno(
            ErrorCode code,
            std::string_view message,
            int saved_errno,
            std::source_location at = std::source_location::current()
        )
        {
            return Error(
                code,
                std::string(message),
                {saved_errno, std::generic_category()},
                at
            );
        }

        /** @brief Captures errno before copying the diagnostic message. */
        [[nodiscard]]
        static Error from_current_errno(
            ErrorCode code,
            std::string_view message,
            std::source_location at = std::source_location::current()
        )
        {
            const int saved = errno;
            return from_errno(code, message, saved, at);
        }

        [[nodiscard]]
        bool has_system_error() const noexcept
        {
            return static_cast<bool>(sys_error);
        }

        /** @brief Formats a diagnostic for logs or terminal output. */
        [[nodiscard]]
        std::string to_string() const
        {
            std::string text = "[";
            text += error_code_name(code);
            text += "] ";
            text += msg;

            if (has_system_error())
            {
                text += " (OS Error ";
                text += std::to_string(sys_error.value());
                text += ": ";
                text += sys_error.message();
                text += ')';
            }

            text += "\n at ";
            text += location.file_name();
            text += ':';
            text += std::to_string(location.line());

            return text;
        }
    };
}
