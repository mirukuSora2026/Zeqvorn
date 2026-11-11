// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 miruku_sora2026

// =============================================
//
// Created At  : 10 / 11 / 2026
//
// Updated At  : 11 / 11 / 2026
//
// Created By  : miruku_sora2026
//
// File Name   : process_handle.cpp
//
// Explanation : Implements child status collection,
//               output capture, and pidfd signal handling.
//
// =============================================

#include "zeqvorn/process/process_handle.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <new>
#include <poll.h>
#include <stdexcept>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>

namespace zeqvorn
{
    namespace
    {
        /**
         * @brief Converts a terminal waitpid() status into public exit fields.
         *
         * @param status Status word returned for an exited or signaled child.
         * @return Exit code or terminating signal, with its validity flag.
         *
         * WEXITSTATUS and WTERMSIG are used only after checking their
         * corresponding status predicates; interpreting the wrong field
         * would produce a meaningless result.
         */
        ProcessExitStatus decode_exit(
            int status
        ) noexcept
        {
            ProcessExitStatus value;

            if (WIFEXITED(status))
            {
                value.exited = true;
                value.exit_code =
                    WEXITSTATUS(status);
            }
            else if (WIFSIGNALED(status))
            {
                value.signaled = true;
                value.signal_number =
                    WTERMSIG(status);
            }

            return value;
        }
    }

    ProcessHandle::ProcessHandle(
        pid_t pid,
        UniqueFd pidfd,
        UniqueFd stdout_read,
        UniqueFd stderr_read
    ) noexcept
        : pid_(pid),
          pidfd_(std::move(pidfd)),
          stdout_read_(std::move(stdout_read)),
          stderr_read_(std::move(stderr_read))
    {
    }

    ProcessHandle::ProcessHandle(ProcessHandle&& other) noexcept
        : pid_(std::exchange(other.pid_, -1)),
          pidfd_(std::move(other.pidfd_)),
          stdout_read_(std::move(other.stdout_read_)),
          stderr_read_(std::move(other.stderr_read_)),
          exit_(std::exchange(other.exit_, std::nullopt))
    {
        // 기본 이동은 정수 PID와 optional의 상태를 비우지 않는다.
        // 소유권과 관찰 상태를 함께 이전해 이동된 객체를 완전히 무효화한다.
    }

    /**
     * @brief Returns the cached lifecycle state.
     */
    ProcessState ProcessHandle::state() const noexcept
    {
        if (!valid())
        {
            return ProcessState::Invalid;
        }

        if (!exit_)
        {
            return ProcessState::Running;
        }

        if (exit_->exited)
        {
            return ProcessState::Exited;
        }

        return ProcessState::Signaled;
    }

    /**
     * @brief Checks child termination without blocking.
     *
     * Uses waitpid(WNOHANG), retrying only interrupted calls. A successful
     * terminal status is cached because the same child cannot be reaped twice.
     */
    Result<std::optional<ProcessExitStatus>>
    ProcessHandle::try_wait()
    {
        if (!valid())
        {
            return Error(
                ErrorCode::InvalidState,
                "Invalid process handle"
            );
        }

        if (exit_)
        {
            return exit_;
        }

        int raw_status = 0;
        pid_t rc;

        do
        {
            rc = ::waitpid(
                pid_,
                &raw_status,
                WNOHANG
            );
        }
        while (rc == -1 && errno == EINTR);

        if (rc < 0)
        {
            const int saved = errno;

            return Error::from_errno(
                ErrorCode::SystemError,
                "waitpid(WNOHANG) failed",
                saved
            );
        }

        if (rc == 0)
        {
            return std::optional<ProcessExitStatus>{};
        }

        exit_ = decode_exit(raw_status);

        return exit_;
    }

    /**
     * @brief Blocks until the child is reaped.
     *
     * Captured children must use collect().
     */
    Result<ProcessExitStatus> ProcessHandle::wait()
    {
        if (!valid())
        {
            return Error(
                ErrorCode::InvalidState,
                "Invalid process handle"
            );
        }

        if (stdout_read_.valid() ||
            stderr_read_.valid())
        {
            return Error(
                ErrorCode::InvalidState,
                "Cannot wait with open capture pipes; call collect()"
            );
        }

        if (exit_)
        {
            return *exit_;
        }

        int raw_status = 0;
        pid_t rc;

        do
        {
            rc = ::waitpid(
                pid_,
                &raw_status,
                0
            );
        }
        while (rc == -1 && errno == EINTR);

        if (rc < 0)
        {
            const int saved = errno;

            return Error::from_errno(
                ErrorCode::SystemError,
                "waitpid() failed",
                saved
            );
        }

        exit_ = decode_exit(raw_status);

        return *exit_;
    }

    /**
     * @brief Sends a signal using pidfd_send_signal.
     *
     * Never falls back to kill(pid).
     */
    Result<void> ProcessHandle::send_signal(
        int signal_number
    )
    {
        if (!valid() || exit_)
        {
            return Error(
                ErrorCode::InvalidState,
                "Process is invalid or already reaped"
            );
        }

#if defined(SYS_pidfd_send_signal)
        long rc;

        do
        {
            rc = ::syscall(
                SYS_pidfd_send_signal,
                pidfd_.get(),
                signal_number,
                nullptr,
                0U
            );
        }
        while (rc < 0 && errno == EINTR);

        if (rc < 0)
        {
            const int saved = errno;

            return Error::from_errno(
                ErrorCode::SystemError,
                "pidfd_send_signal() failed",
                saved
            );
        }

        return {};
#else
        (void)signal_number;

        return Error(
            ErrorCode::Unsupported,
            "pidfd_send_signal is unavailable"
        );
#endif
    }

    /**
     * @brief Aborts and reaps the direct child.
     *
     * Used when collection fails or times out. A failed signal must not
     * turn a collection timeout into an unbounded wait for a living child.
     * In that case only waitpid(WNOHANG) is attempted, leaving explicit
     * cleanup responsibility with the caller if the child remains alive.
     */
    void ProcessHandle::abort_and_reap() noexcept
    {
        if (!exit_ && valid())
        {
            bool can_wait = false;

#if defined(SYS_pidfd_send_signal)
            long signal_result;

            do
            {
                signal_result = ::syscall(
                    SYS_pidfd_send_signal,
                    pidfd_.get(),
                    SIGKILL,
                    nullptr,
                    0U
                );
            }
            while (signal_result < 0 && errno == EINTR);

            // ESRCH는 pidfd가 가리키는 자식이 이미 종료되었음을 의미한다.
            // 그 외 실패에서는 살아 있는 자식을 무기한 기다리지 않는다.
            can_wait = signal_result == 0 || errno == ESRCH;
#endif

            int status = 0;
            pid_t rc;

            do
            {
                rc = ::waitpid(
                    pid_,
                    &status,
                    can_wait ? 0 : WNOHANG
                );
            }
            while (rc == -1 && errno == EINTR);

            if (rc == pid_)
            {
                exit_ = decode_exit(status);
            }
        }

        stdout_read_.reset();
        stderr_read_.reset();
    }

    /**
     * @brief Collects stdout/stderr and exit status.
     *
     * Uses non-blocking reads and poll().
     *
     * Both output streams are handled
     * independently but within one event loop.
     *
     * On error, timeout, or output overflow, cleanup attempts to terminate
     * and reap the child. If the pidfd signal fails, capture pipes are
     * still closed and an unreaped child remains the caller's responsibility.
     */
    Result<CapturedOutput> ProcessHandle::collect(
        std::chrono::milliseconds timeout,
        std::size_t max_output_bytes
    )
    {
        if (!valid())
        {
            return Error(
                ErrorCode::InvalidState,
                "Invalid process handle"
            );
        }

        if (timeout.count() < 0)
        {
            return Error(
                ErrorCode::InvalidArgument,
                "Timeout cannot be negative"
            );
        }

        CapturedOutput result;

        const auto started = std::chrono::steady_clock::now();

        // 큰 timeout을 시각에 더하면 clock::duration에서 오버플로할 수 있다.
        // 밀리초로 경과 시간을 비교하면 milliseconds::max()도 표현 가능하다.
        const auto elapsed = [&started]()
        {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started
            );
        };

        std::size_t total = 0;

        for (;;)
        {
            /**
             * Check whether the direct child
             * has already terminated.
             */
            auto status = try_wait();

            if (!status)
            {
                auto failure = status.error();
                abort_and_reap();
                return failure;
            }

            // All output is drained and the child is reaped.
            if (exit_ &&
                !stdout_read_.valid() &&
                !stderr_read_.valid())
            {
                result.status = *exit_;
                return result;
            }

            /**
             * Build pollfd array.
             *
             * Maximum:
             *   stdout pipe
             *   stderr pipe
             *   pidfd
             */
            pollfd pending[3]{};
            nfds_t count = 0;

            const int out_idx =
                stdout_read_.valid()
                    ? static_cast<int>(count)
                    : -1;

            if (out_idx >= 0)
            {
                pending[count++] = {
                    stdout_read_.get(),
                    POLLIN,
                    0
                };
            }

            const int err_idx =
                stderr_read_.valid()
                    ? static_cast<int>(count)
                    : -1;

            if (err_idx >= 0)
            {
                pending[count++] = {
                    stderr_read_.get(),
                    POLLIN,
                    0
                };
            }

            if (!exit_)
            {
                pending[count++] = {
                    pidfd_.get(),
                    POLLIN,
                    0
                };
            }

            /**
             * Calculate remaining timeout.
             *
             * A 100ms maximum interval allows
             * periodic state checks.
             */
            const auto spent = elapsed();
            const auto remaining = timeout > spent
                ? timeout - spent
                : std::chrono::milliseconds::zero();

            const int ms =
                static_cast<int>(
                    std::min<std::int64_t>(
                        remaining.count(),
                        100
                    )
                );

            int rc = ::poll(
                pending,
                count,
                ms
            );

            if (rc < 0 && errno == EINTR)
            {
                // 시그널이 계속 들어와도 아래의 제한 시간 검사를 수행한다.
                // 준비 여부가 확인되지 않은 descriptor는 이번 회차에 읽지 않는다.
                rc = 0;
            }

            if (rc < 0)
            {
                const int saved = errno;

                auto failure =
                    Error::from_errno(
                        ErrorCode::SystemError,
                        "poll() failed",
                        saved
                    );

                abort_and_reap();

                return failure;
            }

            for (nfds_t index = 0; index < count; ++index)
            {
                if (rc > 0 && (pending[index].revents & POLLNVAL) != 0)
                {
                    auto failure = Error::from_errno(
                        ErrorCode::IoError,
                        "Invalid descriptor while collecting process output",
                        EBADF
                    );

                    abort_and_reap();
                    return failure;
                }
            }

            /**
             * @brief Reads ready data from one pipe.
             *
             * Reads are non-blocking.
             * A bounded read budget prevents one
             * stream from starving the other.
             */
            const auto drain =
                [&](
                    UniqueFd& fd,
                    std::string& output,
                    int index
            ) -> Result<void>
            {
                if (rc == 0 || index < 0 ||
                    !(pending[index].revents &
                        (POLLIN | POLLHUP | POLLERR)))
                {
                    return {};
                }

                char buffer[8192];

                std::size_t budget =
                    64 * 1024;

                while (budget > 0)
                {
                    const ssize_t n = ::read(
                        fd.get(),
                        buffer,
                        std::min<std::size_t>(
                            sizeof(buffer),
                            budget
                        )
                    );

                    if (n > 0)
                    {
                        const auto size =
                            static_cast<std::size_t>(n);

                        if (size > max_output_bytes - total)
                        {
                            return Error(
                                ErrorCode::ResourceExhausted,
                                "Captured output size limit exceeded"
                            );
                        }

                        try
                        {
                            output.append(
                                buffer,
                                size
                            );
                        }
                        catch (const std::bad_alloc&)
                        {
                            return Error(
                                ErrorCode::ResourceExhausted,
                                "Out of memory while capturing output"
                            );
                        }
                        catch (const std::length_error&)
                        {
                            return Error(
                                ErrorCode::ResourceExhausted,
                                "Captured output exceeds the string capacity"
                            );
                        }

                        total += size;
                        budget -= size;

                        continue;
                    }

                    // EOF: close this capture pipe.
                    if (n == 0)
                    {
                        fd.reset();
                        return {};
                    }

                    if (errno == EINTR)
                    {
                        // 시그널로 중단되면 event loop에서 다시 poll()한다.
                        // 한 스트림의 재시도가 제한 시간 검사를 막지 않게 한다.
                        return {};
                    }

                    if (errno == EAGAIN ||
                        errno == EWOULDBLOCK)
                    {
                        return {};
                    }

                    const int saved = errno;

                    return Error::from_errno(
                        ErrorCode::IoError,
                        "Captured pipe read() failed",
                        saved
                    );
                }

                return {};
            };

            // Drain stdout.
            auto out = drain(
                stdout_read_,
                result.stdout_text,
                out_idx
            );

            if (!out)
            {
                auto failure = out.error();
                abort_and_reap();
                return failure;
            }

            // Drain stderr.
            auto err = drain(
                stderr_read_,
                result.stderr_text,
                err_idx
            );

            if (!err)
            {
                auto failure = err.error();
                abort_and_reap();
                return failure;
            }

            /**
             * Check timeout after consuming
             * any ready output.
             */
            if (elapsed() >= timeout)
            {
                auto latest = try_wait();

                if (!latest)
                {
                    auto failure = latest.error();
                    abort_and_reap();
                    return failure;
                }

                if (exit_ &&
                    !stdout_read_.valid() &&
                    !stderr_read_.valid())
                {
                    result.status = *exit_;
                    return result;
                }

                abort_and_reap();

                return Error(
                    ErrorCode::Timeout,
                    "Process collection timed out"
                );
            }
        }
    }
}
