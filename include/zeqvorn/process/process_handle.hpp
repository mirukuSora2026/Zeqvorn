// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 miruku_sora2026

// =============================================
//
// Created At  : 09 / 11 / 2026
//
// Updated At  : 09 / 11 / 2026
//
// Created By  : miruku_sora2026
//
// File Name   : process_handle.hpp
//
// Explanation : Manages a child process handle,
//               its lifecycle, and exit status.
//
// =============================================

#pragma once

#include <cerrno>
#include <csignal>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>

#include "../core/result.hpp"

namespace zeqvorn
{
    /**
     * @brief Represents the termination of a child process.
     */
    struct ProcessExitStatus
    {
        bool exited = false;
        int exit_code = -1;

        bool signaled = false;
        int signal_number = 0;
    };

    /**
     * @brief Owns responsibility for waiting on a child process.
     *
     * ProcessHandle is move-only to prevent multiple
     * objects from independently reaping the same child.
     *
     * IMPORTANT:
     * This destructor doesn't terminate or wait for
     * the child process. The caller must explicitly
     * call wait() or transfer ownership to a manager.
     */
    class [[nodiscard]] ProcessHandle
    {
    private:
        pid_t pid_ = -1;
        bool reaped_ = false;

    public:
        /**
         * @brief Constructs an invalid process handle.
         */
        ProcessHandle() noexcept = default;

        /**
         * @brief Takes responsibility for a child PID.
         *
         * @param pid PID of a direct child process.
         *
         * The PID must refer to a child created and
         * managed by the current process.
         */
        explicit ProcessHandle(pid_t pid) noexcept
            : pid_(pid > 0 ? pid : -1)
        {
        }

        /**
         * @brief Doesn't implicitly terminate the child.
         */
        ~ProcessHandle() noexcept = default;

        ProcessHandle(const ProcessHandle&) = delete;
        ProcessHandle& operator=(const ProcessHandle&) = delete;

        /**
         * @brief Transfers process management responsibility.
         */
        ProcessHandle(ProcessHandle&& other) noexcept
            : pid_(std::exchange(other.pid_, -1)),
              reaped_(std::exchange(other.reaped_, false))
        {
        }

        /**
         * @brief Move assignment.
         *
         * Only an empty or already reaped destination
         * should receive another active child handle.
         */
        ProcessHandle& operator=(ProcessHandle&& other) = delete;

        /**
         * @brief Returns the child PID.
         */
        [[nodiscard]]
        pid_t pid() const noexcept
        {
            return pid_;
        }

        /**
         * @brief Checks whether this handle has a child.
         */
        [[nodiscard]]
        bool valid() const noexcept
        {
            return pid_ > 0 && !reaped_;
        }

        /**
         * @brief Checks whether the child was reaped.
         */
        [[nodiscard]]
        bool reaped() const noexcept
        {
            return reaped_;
        }

        /**
         * @brief Waits for the child to terminate.
         *
         * @return ProcessExitStatus on success.
         * @return Error is waitpid() fails.
         *
         * EINTR is retried because the wait operation
         * may be interrupted by a signal.
         */
        [[nodiscard]]
        Result<ProcessExitStatus> wait()
        {
            if (!valid())
            {
                return Error(
                    ErrorCode::InvalidState,
                    "Process handle is not active"
                );
            }

            int status = 0;
            pid_t result;

            do
            {
                result = ::waitpid(pid_, &status, 0);
            }
            while (result == -1 && errno == EINTR);

            if (result == -1)
            {
                const int saved_errno = errno;

                return Error::from_errno(
                    ErrorCode::SystemError,
                    "Failed to wait for child process",
                    saved_errno
                );
            }

            reaped_ = true;

            ProcessExitStatus exit_status;

            if (WIFEXITED(status))
            {
                exit_status.exited = true;
                exit_status.exit_code =
                    WEXITSTATUS(status);
            }
            else if (WIFSIGNALED(status))
            {
                exit_status.signaled = true;
                exit_status.signal_number =
                    WTERMSIG(status);
            }

            return exit_status;
        }

        /**
         * @brief Sends a signal to the child process.
         *
         * @param signal_number POSIX signal number.
         *
         * This doesn't wait for termination.
         *
         * A PID can be reused after a child has been
         * reaped. Signal operations must only be
         * performed while the handle is active.
         */
        [[nodiscard]]
        Result<void> send_signal(int signal_number)
        {
            if (!valid())
            {
                return Error(
                    ErrorCode::InvalidState,
                    "Process handle is not active"
                );
            }

            if (::kill(pid_, signal_number) == -1)
            {
                const int saved_errno = errno;

                return Error::from_errno(
                    ErrorCode::SystemError,
                    "Failed to signal child process",
                    saved_errno
                );
            }

            return {};
        }
    };
}
