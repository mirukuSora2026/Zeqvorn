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
// Explanation : Manages exclusive child process ownership,
//               captured output, signals, and exit status.
//
// =============================================

#pragma once

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <sys/types.h>

#include "zeqvorn/core/result.hpp"
#include "zeqvorn/core/unique_fd.hpp"

namespace zeqvorn
{
    /**
     * @brief Describes the lifecycle observed by a ProcessHandle.
     *
     * Running means the child has not yet been reaped by this handle.
     * The child may already have terminated before try_wait() observes it.
     */
    enum class ProcessState
    {
        Invalid,  // No child is owned, including after ownership was moved.
        Running,  // The final waitpid() status has not yet been collected.
        Exited,   // The child returned an exit code or called _exit().
        Signaled  // The child terminated because of a signal.
    };

    /**
     * @brief Stores the final termination status of a direct child.
     *
     * Exactly one of exited and signaled is true for a reaped child.
     * exit_code is meaningful only when exited is true; signal_number
     * is meaningful only when signaled is true.
     */
    struct ProcessExitStatus
    {
        bool exited = false;
        int exit_code = -1;

        bool signaled = false;
        int signal_number = 0;
    };

    /**
     * @brief Contains the captured streams and final child status.
     *
     * Each string preserves the bytes read from its own pipe, including
     * embedded NUL bytes. Ordering between stdout and stderr is not stored.
     */
    struct CapturedOutput
    {
        std::string stdout_text;
        std::string stderr_text;
        ProcessExitStatus status;
    };

    /**
     * @brief Exclusively owns the resources used to observe a direct child.
     *
     * A handle owns a pidfd, optional nonblocking capture pipes, and the
     * responsibility to reap its child. Moving transfers all ownership
     * and leaves the source invalid. Copying and move assignment are
     * disabled so that an active child cannot be silently overwritten.
     *
     * IMPORTANT:
     * Destruction closes the owned descriptors, but does not terminate
     * or reap the child. Call collect() or wait(), or transfer the handle
     * to ProcessReaper, before destroying an active handle.
     *
     * Not thread-safe. No other code may reap the same child concurrently.
     */
    class [[nodiscard]] ProcessHandle final
    {
    private:
        // PID는 진단과 waitpid()에 사용하고, 시그널은 pidfd로 전송한다.
        pid_t pid_ = -1;
        UniqueFd pidfd_;
        UniqueFd stdout_read_;
        UniqueFd stderr_read_;

        // waitpid()는 한 번만 상태를 회수할 수 있으므로 종료 결과를 보관한다.
        std::optional<ProcessExitStatus> exit_;

        /**
         * @brief Attempts to kill and reap the child after collection fails.
         *
         * Always closes capture pipes. If pidfd signaling fails, only a
         * nonblocking reap is attempted; the handle may still own a child
         * that requires explicit cleanup by the caller.
         */
        void abort_and_reap() noexcept;

    public:
        /**
         * @brief Constructs an empty handle that owns no child.
         */
        ProcessHandle() noexcept = default;

        /**
         * @brief Takes exclusive ownership of child observation resources.
         *
         * @param pid PID of a direct child of the current process.
         * @param pidfd Owned Linux pidfd referring to the same child.
         * @param stdout_read Optional nonblocking stdout pipe read end.
         * @param stderr_read Optional nonblocking stderr pipe read end.
         *
         * ProcessManager prepares these resources before constructing the
         * handle. The caller must not close or reuse transferred descriptors.
         */
        ProcessHandle(
            pid_t pid,
            UniqueFd pidfd,
            UniqueFd stdout_read,
            UniqueFd stderr_read
        ) noexcept;

        /**
         * @brief Closes descriptors without implicitly killing or reaping.
         */
        ~ProcessHandle() noexcept = default;

        ProcessHandle(const ProcessHandle&) = delete;
        ProcessHandle& operator=(const ProcessHandle&) = delete;

        /**
         * @brief Transfers ownership and resets the source to an empty state.
         */
        ProcessHandle(ProcessHandle&& other) noexcept;

        // 이미 소유한 자식의 회수 책임을 덮어쓰는 연산은 허용하지 않는다.
        ProcessHandle& operator=(ProcessHandle&&) = delete;

        /**
         * @brief Returns the owned child's PID, or -1 for an empty handle.
         */
        [[nodiscard]]
        pid_t pid() const noexcept
        {
            return pid_;
        }

        /**
         * @brief Borrows the pidfd without transferring descriptor ownership.
         *
         * @return The Linux pidfd, or -1 for an empty handle.
         * The caller must not close the returned descriptor.
         */
        [[nodiscard]]
        int pidfd() const noexcept
        {
            return pidfd_.get();
        }

        /**
         * @brief Checks whether the handle owns a child PID and pidfd.
         *
         * A reaped child remains valid so its cached status can be queried.
         */
        [[nodiscard]]
        bool valid() const noexcept
        {
            return pid_ > 0 && pidfd_.valid();
        }

        /**
         * @brief Checks whether this handle has collected the final status.
         */
        [[nodiscard]]
        bool reaped() const noexcept
        {
            return exit_.has_value();
        }

        /**
         * @brief Checks whether either capture pipe still needs draining.
         */
        [[nodiscard]]
        bool capturing_output() const noexcept
        {
            return stdout_read_.valid() || stderr_read_.valid();
        }

        /**
         * @brief Returns the cached lifecycle without making a system call.
         */
        [[nodiscard]]
        ProcessState state() const noexcept;

        /**
         * @brief Attempts to reap the child without waiting for termination.
         *
         * @return An empty optional if still running, the cached final status
         *         if reaped, or an Error if waitpid() failed.
         *
         * Does not drain capture pipes. A captured child still needs collect()
         * even when try_wait() has already obtained its termination status.
         */
        [[nodiscard]]
        Result<std::optional<ProcessExitStatus>> try_wait();

        /**
         * @brief Waits until the direct child terminates and caches its status.
         *
         * @return The final status, or InvalidState if capture pipes are open.
         *
         * Use collect() for captured children: waiting without reading their
         * output could deadlock when the child fills one of its pipes.
         */
        [[nodiscard]]
        Result<ProcessExitStatus> wait();

        /**
         * @brief Sends a signal to the child identified by the owned pidfd.
         *
         * @param signal_number Signal number; zero checks signal permission.
         * @return Success or a system error reported by pidfd_send_signal().
         *
         * Rejects invalid or reaped handles. Does not fall back to kill(pid),
         * which could target a different process after PID reuse.
         */
        [[nodiscard]]
        Result<void> send_signal(int signal_number);

        /**
         * @brief Drains both output streams and reaps the direct child.
         *
         * @param timeout Maximum collection time, measured with a steady clock.
         *                Negative durations are rejected without changing ownership.
         * @param max_output_bytes Combined stdout and stderr byte limit.
         * @return Captured bytes and status, or an Error on collection failure.
         *
         * Uses poll() and bounded nonblocking reads so one busy stream cannot
         * starve the other. A zero byte limit still allows an empty output.
         * On timeout, overflow, or an I/O error, cleanup attempts to kill and
         * reap the direct child and closes both capture pipes. If signaling
         * is denied, the caller remains responsible for an unreaped child.
         * Descendants are not terminated; inherited pipes remain subject to
         * the same collection timeout even after the direct child has exited.
         */
        [[nodiscard]]
        Result<CapturedOutput> collect(
            std::chrono::milliseconds timeout = std::chrono::seconds(5),
            std::size_t max_output_bytes = 1024 * 1024
        );
    };
}

