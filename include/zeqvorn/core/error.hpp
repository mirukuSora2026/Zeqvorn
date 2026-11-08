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
// File Name   : error.hpp
//
// Explanation : Defines the custom error handling system
//               used throughout the `Zeqvorn`.
//
// =============================================

#pragma once

#include <string>
#include <system_error>
#include <cerrno>
#include <source_location>
#include <utility>

using namespace std;

namespace zeqvorn
{
    enum class ErrorCode
    {
        InvalidArgument,        // An invalid argument was provided.
        NotFound,               // The requested resource does not exist.
        PermissionDenied,       // The operation was denied due to permissions.
        ProcessCreationFailed,  // Failed to create a new process.
        ExecutionFailed,        // Failed to execute a program.
        ResourceExhausted,      // Insufficient memory, descriptors, or other resources.
        Timeout,                // The operation exceeded its time limit.
        AlreadyExists,          // The requested resource already exists.
        InvalidState,           // The operation is invalid in the current state.
        Unsupported,            // The requested operation is not supported.
        IoError,                // An input/output operation failed.
        SystemError,            // A general operating system error occurred.
        InternalError           // An unexpected internal failure occurred.
    };

    /**
     * @brief Converts an ErrorCode into a readable string.
     *
     * @param code The internal error category.
     * @return A string literal representing the error category.
     */
    // nodiscard : 함수의 반환값(Return value)을 무시하지 못하도록
    //             컴파일러에 경고를 요청하는 키워드이다.
    // constexpr : 해당 변수나 함수가 컴파일 시점(Compile-time)에
    //             상수가 된다는 것을 의미하는 키워드이다.
    [[nodiscard]]
    inline constexpr const char* error_code_name(
        ErrorCode code
    ) noexcept
    {
        switch (code)
        {
            case ErrorCode::InvalidArgument:
                return "InvalidArgument";

            case ErrorCode::NotFound:
                return "NotFound";

            case ErrorCode::PermissionDenied:
                return "PermissionDenied";

            case ErrorCode::ProcessCreationFailed:
                return "ProcessCreationFailed";

            case ErrorCode::ExecutionFailed:
                return "ExecutionFailed";

            case ErrorCode::ResourceExhausted:
                return "ResourceExhausted";

            case ErrorCode::Timeout:
                return "Timeout";

            case ErrorCode::AlreadyExists:
                return "AlreadyExists";

            case ErrorCode::InvalidState:
                return "InvalidState";

            case ErrorCode::Unsupported:
                return "Unsupported";

            case ErrorCode::IoError:
                return "IoError";

            case ErrorCode::SystemError:
                return "SystemError";

            case ErrorCode::InternalError:
                return "InternalError";
            }

        // Fallback for unknown or invalid enumeration values.
        return "Unknown";
    }

    /**
     * @brief Represents a failure that occurred in Zeqvorn.
     *
     * An Error object contains:
     *
     * 1. An internal error category.
     * 2. A human-readable error message.
     * 3. Optional operating system error information.
     * 4. The source location where the error was created.
     *
     * Example:
     *
     * Error error(
     *     ErrorCode::NotFound,
     *     "Executable file not found."
     * );
     */
    struct Error
    {
        /**
         * @param code      : Zeqvorn-specific error category.
         * @param msg       : Human-readable error description.
         * @param sys_error : Optional OS error information.
         * @param location  : File, line, and function information.
         */
        ErrorCode code;
        string msg;
        error_code sys_error{};
        source_location location;

        /**
         * @brief Constructs a new Error object.
         *
         * @param code Internal error category.
         * @param msg  Human-readable description.
         * @param system_error Optional operating system error.
         * @param location Location where the constructor was called.
         */
        // explicit  : 의도치 않은 암시적 변환을 방지한다.
        // std::move : 가능한 경우 메시지 문자열을 복사하는 대신 이동할 수 있도록 한다.
        // std::source_location::current() :
        //            caller의 출처 위치를 자동으로 포착한다.
        explicit Error(
            ErrorCode code,
            string msg,
            error_code system_error = {},
            source_location location =
                source_location::current()
        )
            : code(code),
              msg(std::move(msg)),
              sys_error(system_error),
              location(location) {}

        /**
         * @brief Creates an Error from a specific errno value.
         *
         * @param code Internal Zeqvorn error category.
         * @param msg Description of the failed operation.
         * @param error_no Perviously captured errno value.
         * @param location Source location of the caller.
         * @return An Error containing the specified OS error.
         *
         * This function is useful when errno has already been
         * saved immediately after a failed system call.
         */
        // std::generic_category() : ENOENT, EACCES, ENOMEM과 같은
        //                           표준 POSIX 오류 번호를 해석한다.
        [[nodiscard]]
        static Error from_errno(
            ErrorCode code,
            string msg,
            int error_no,
            source_location location =
                source_location::current()
        )
        {
            return Error(
                code,
                std::move(msg),
                {error_no, generic_category()},
                location
            );
        }

        /**
         * @brief Creates an Error using the current errno value.
         *
         * @param code Internal Zeqvorn error category.
         * @param msg Description of the failed operation.
         * @param location Source location of the caller.
         * @return An Error containing the captured errno.
         *
         * IMPORTANT:
         * Call this function immediately after detecting
         * a failed system call, before executing other
         * operations that might change errno.
         *
         * errno must not be interpreted as an error unless
         * the called API has actually reported failure.
         */
        // errno : POSIX 시스템 호출 래퍼에서 흔히 사용되는 스레드별 오류 표시기
        [[nodiscard]]
        static Error from_current_errno(
            ErrorCode code,
            string msg,
            source_location location =
                source_location::current()
        )
        {
            const int saved_error_no = errno;

            return from_errno(
                code,
                std::move(msg),
                saved_error_no,
                location
            );
        }

        /**
         * @brief Checks whether an OS error is stored.
         *
         * @return true if sys_error contains a nonzero error value.
         * @return false otherwise.
         *
         * This does not indicate whether the Zeqvorn
         * operation succeeded or failed.
         */
        // std::error_code : 저장된 오류 값이 0이 아닐 때 bool로 변환한다.
        [[nodiscard]]
        bool has_system_error() const noexcept
        {
            return static_cast<bool>(sys_error);
        }

        /**
         * @brief Converts the Error into a readable message.
         *
         * @return A formatted string containing the error
         *         category, description, optional OS error,
         *         and source file location.
         *
         * Example output:
         *
         * [NotFound] Failed to open executable
         * (OS Error 2: No such file or directory)
         *   at process_manager.cpp:42
         *
         *   This function may allocate memory and throw
         *   exceptions, so it is not declared noexcept.
         */
        // sys_error.message() : 관련 오류로부터 사람이 읽을 수 있는 설명을 가져온다.
        [[nodiscard]]
        string to_string() const
        {
            string result =
                "[" + string(error_code_name(code)) +
                "] " + msg;

            if (has_system_error())
            {
                result += " (OS Error " +
                    std::to_string(sys_error.value()) +
                        ": " + sys_error.message() + ")";
            }

            result += "\n at ";
            result += location.file_name();
            result += ":" + std::to_string(location.line());

            return result;
        }
    };
}
