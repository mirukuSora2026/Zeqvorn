// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 miruku_sora2026

// =============================================
//
// Created At  : 08 / 11 / 2026
//
// Updated At  : 10 / 11 / 2026
//
// Created By  : miruku_sora2026
//
// File Name   : unique_fd.hpp
//
// Explanation : Provides an RAII wrapper for Linux file
//               descriptors with exclusive ownership.
//
// =============================================

#pragma once

#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <utility>
#include <sys/types.h>

#include "result.hpp"

namespace zeqvorn
{
    /**
     * @brief Manages a Linux file descriptor.
     *
     * UniqueFd provides exclusive ownership of
     * a file descriptor using RAII.
     *
     * Features:
     *     - Automatic resource cleanup.
     *     - Move-only ownership.
     *     - Explicit ownership release.
     *     - Descriptor replacement.
     *
     * The descriptor is closed automatically
     * when the owning object is destroyed.
     */
    class [[nodiscard]] UniqueFd
    {
    private:
        // -1은 유효하지 않은 설명자를 나타낸다.
        int fd_ = -1;

    public:
        /**
         * @brief Constructs an empty UniqueFd.
         */
        UniqueFd() noexcept = default;

        /**
         * @brief Takes ownership of a descriptor.
         *
         * @param fd A valid descriptor or -1.
         *
         * The caller must transfer ownership
         * and must not close the descriptor.
         */
        explicit UniqueFd(int fd) noexcept
            : fd_(fd)
        {
        }

        /**
         * @brief Releases the owned descriptor.
         *
         * The destructor never throws exceptions.
         */
        ~UniqueFd() noexcept
        {
            reset();
        }

        /**
         * @brief Disables copying.
         *
         * Copying ownership could cause
         * multiple objects to close the
         * same descriptor.
         */
        UniqueFd(const UniqueFd&) = delete;
        UniqueFd& operator=(const UniqueFd&) = delete;

        /**
         * @brief Transfers ownership from another object.
         *
         * std::exchange replaces the source
         * descriptor with -1 and returns its
         * previous value.
         */
        UniqueFd(UniqueFd&& other) noexcept
            : fd_(std::exchange(other.fd_, -1))
        {
        }

        /**
         * @brief Replaces the current owned descriptor.
         *
         * Existing ownership is released before
         * ownership is transferred.
         */
        UniqueFd& operator=(UniqueFd&& other) noexcept
        {
            if (this != &other)
            {
                reset();
                fd_ = std::exchange(other.fd_, -1);
            }

            return *this;
        }

        /**
         * @brief Returns the underlying descriptor.
         *
         * @return This descriptor or -1.
         *
         * Ownership isn't transferred.
         */
        [[nodiscard]]
        int get() const noexcept
        {
            return fd_;
        }

        /**
         * @brief Checks whether a descriptor is owned.
         */
        [[nodiscard]]
        bool valid() const noexcept
        {
            return fd_ >= 0;
        }

        /**
         * @brief Allows checking ownership in conditions.
         */
        [[nodiscard]]
        explicit operator bool() const noexcept
        {
            return valid();
        }

        /**
         * @brief Releases ownership without closing.
         *
         * @return The previously owned descriptor.
         *
         * The caller becomes responsible for
         * closing the returned descriptor.
         */
        [[nodiscard]]
        int release() noexcept
        {
            return std::exchange(fd_, -1);
        }

        /**
         * @brief Closes or replaces the current descriptor.
         *
         * @param replacement Replacement descriptor.
         *
         * The previous descriptor is closed
         * unless it is invalid.
         *
         * Passing the current descriptor leaves
         * ownership unchanged and does not close it.
         *
         * On Linux, close() must not be retried
         * after EINTR because the descriptor
         * number may already have been released.
         */
        void reset(int replacement = -1) noexcept
        {
            if (fd_ == replacement) return;

            int old = std::exchange(fd_, replacement);

            if (old >= 0)
            {
                (void)::close(old);
            }
        }

        /**
         * @brief Exchanges ownership with another object.
         */
        void swap(UniqueFd& other) noexcept
        {
            std::swap(fd_, other.fd_);
        }

        /**
         * @brief Opens a file and takes ownership of its FD.
         *
         * @param path File system path.
         * @param flags Linux open flags.
         * @param mode File permissions used when creating.
         *
         * @return Result containing UniqueFd or Error.
         *
         * O_CLOEXEC prevents unintended descriptor
         * inheritance across exec operations.
         *
         * This function doesn't automatically
         * retry interrupted open operations.
         */
        [[nodiscard]]
        static Result<UniqueFd> open(
            const char* path,
            int flags,
            mode_t mode = 0644
        )
        {
            if (!path)
            {
                return Error(
                    ErrorCode::InvalidArgument,
                    "File path must not be null"
                );
            }

            const int fd = ::open(
                path,
                flags | O_CLOEXEC,
                mode
            );

            if (fd < 0)
            {
                // 오류 메시지를 만들기 전에 errno를 저장한다.
                // 문자열 생성이나 다른 함수 호출이 errno를 바꾸더라도
                // 실제 open() 실패 원인을 그대로 전달할 수 있다.
                const int saved_error = errno;

                return Error::from_errno(
                    ErrorCode::IoError,
                    "Failed to open file",
                    saved_error
                );
            }

            return UniqueFd(fd);
        }
    };
}
