// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 miruku_sora2026

// =============================================
//
// Created At  : 11 / 12 / 2026
//
// Updated At  : 11 / 12 / 2026
//
// Created By  : miruku_sora2026
//
// File Name   : unique_fd.hpp
//
// Explanation : Owns POSIX file descriptors and provides
//               a helper for interrupted system calls.
//
// =============================================

#pragma once

#include <cerrno>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#include <utility>

#include "zeqvorn/base/result.hpp"

namespace zeqvorn
{
    /**
     * @brief Retries an operation that returns -1 and sets EINTR.
     *
     * 재시도 가능한 POSIX 호출에 사용한다. close()와 시간 제한이
     * 걸린 대기 작업은 각 호출의 규칙에 맞춰 따로 처리한다.
     */
    template <typename Operation>
    auto retry_eintr(Operation&& operation) noexcept(noexcept(operation()))
    {
        auto result = operation();

        while (result == -1 && errno == EINTR)
        {
            result = operation();
        }

        return result;
    }

    /**
     * @brief Owns a POSIX descriptor and closes it at scope exit.
     *
     * A descriptor has one owner. Moving transfers that ownership.
     * Descriptor 0 is valid, and -1 represents an empty owner.
     */
    class [[nodiscard]] UniqueFd
    {
    public:
        UniqueFd() noexcept = default;

        /** @brief Adopts a descriptor already owned by the caller. */
        explicit UniqueFd(int fd) noexcept : fd_(fd) {}

        ~UniqueFd() noexcept { reset(); }

        UniqueFd(const UniqueFd&) = delete;
        UniqueFd& operator=(const UniqueFd&) = delete;

        UniqueFd(UniqueFd&& other) noexcept : fd_(other.release()) {}

        UniqueFd& operator=(UniqueFd&& other) noexcept
        {
            if (this != &other)
            {
                reset(other.release());
            }

            return *this;
        }

        /** @brief Borrows the descriptor for a system call. */
        [[nodiscard]] int get() const noexcept { return fd_; }

        [[nodiscard]] bool valid() const noexcept { return fd_ >= 0; }
        [[nodiscard]] explicit operator bool() const noexcept { return valid(); }

        /** @brief Transfers ownership to the caller without closing the FD. */
        [[nodiscard]]
        int release() noexcept
        {
            return std::exchange(fd_, -1);
        }

        /**
         * @brief Closes the current descriptor and adopts a replacement.
         *
         * Passing get() leaves ownership in place. On Linux close()
         * can release the FD even if it returns EINTR, so call it once.
         */
        void reset(int replacement = -1) noexcept
        {
            if (fd_ == replacement)
            {
                return;
            }

            const int old = std::exchange(fd_, replacement);

            if (old >= 0)
            {
                const int saved = errno;
                (void)::close(old);
                errno = saved;
            }
        }

        /** @brief Exchanges ownership without closing either descriptor. */
        void swap(UniqueFd& other) noexcept
        {
            std::swap(fd_, other.fd_);
        }

        /**
         * @brief Opens a file with close-on-exec enabled.
         * @param mode File permissions used when flags request creation.
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
                return Error(ErrorCode::InvalidArgument, "File path must not be null");
            }

            const int fd = retry_eintr([=]() noexcept
            {
                return ::open(path, flags | O_CLOEXEC, mode);
            });

            if (fd < 0)
            {
                const int saved = errno;
                return Error::from_errno(
                    ErrorCode::IoError, "Failed to open file", saved
                );
            }

            return UniqueFd(fd);
        }

    private:
        int fd_ = -1;
    };
}