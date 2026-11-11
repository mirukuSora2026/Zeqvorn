// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 miruku_sora2026

// =============================================
//
// Created At  : 09 / 11 / 2026
//
// Updated At  : 10 / 11 / 2026
//
// Created By  : miruku_sora2026
//
// File Name   : process_reaper.hpp
//
// Explanation : Tracks direct child processes and explicitly
//               collects their termination status.
//
// =============================================

#pragma once

#include <cstddef>
#include <list>
#include <vector>

#include "process_handle.hpp"

namespace zeqvorn
{
    /**
     * @brief Information about a reaped child.
     *
     * Retains the original PID and final exit status after
     * the corresponding ProcessHandle leaves the reaper.
     * This record owns no process or file descriptors.
     */
    struct ReapedChild
    {
        pid_t pid = -1;
        ProcessExitStatus status;
    };

    /**
     * @brief Explicit supervisor for child processes.
     *
     * Only supports processes without capture pipes.
     *
     * No background thread is created.
     *
     * poll_completed() performs non-blocking checks.
     * wait_all() blocks until tracked children exit.
     *
     * The destructor does not reap children.
     * Call wait_all() before destroying a nonempty reaper.
     * Like ProcessHandle, this class is not thread-safe.
     */
    class [[nodiscard]] ProcessReaper final
    {
    private:
        // std::list는 요소를 지울 때 다른 핸들을 이동 대입하지 않는다.
        // ProcessHandle은 소유 중인 자식을 덮어쓰지 않도록 이동 대입이
        // 금지되어 있으므로, 각 핸들의 노드를 독립적으로 관리한다.
        std::list<ProcessHandle> children_;

    public:
        ProcessReaper() noexcept = default;

        // 회수 책임이 복제되거나 이동 대입으로 유실되지 않도록 막는다.
        ProcessReaper(const ProcessReaper&) = delete;
        ProcessReaper& operator=(const ProcessReaper&) = delete;
        ProcessReaper(ProcessReaper&&) noexcept = default;
        ProcessReaper& operator=(ProcessReaper&&) = delete;

        /**
         * @brief Returns the number of children still tracked.
         *
         * A terminated child remains counted until a poll or
         * wait operation removes its handle from the list.
         */
        [[nodiscard]]
        std::size_t size() const noexcept
        {
            return children_.size();
        }

        /**
         * @brief Transfers child management responsibility.
         *
         * @param child Valid, unreaped handle without capture pipes.
         * @return Success after ownership transfer, or an Error.
         *
         * Rejection leaves the caller's handle untouched.
         * Captured children must use collect() so blocked writes
         * cannot deadlock a later wait_all(). Allocation failure
         * may throw before ownership is transferred.
         */
        [[nodiscard]]
        Result<void> track(
            ProcessHandle&& child
        );

        /**
         * @brief Reaps finished children without blocking.
         *
         * @return Records for children reaped during this call.
         *
         * Running children remain tracked. If a wait fails, earlier
         * completed children remain in the list with cached statuses
         * so a subsequent successful call can return their records.
         */
        [[nodiscard]]
        Result<std::vector<ReapedChild>>
        poll_completed();

        /**
         * @brief Waits for and reaps every tracked child.
         *
         * @return Final status for each tracked child.
         *
         * Processes are waited on in insertion order. A child that
         * never exits can block this function indefinitely. On an
         * error, handles remain tracked for an explicit retry.
         */
        [[nodiscard]]
        Result<std::vector<ReapedChild>>
        wait_all();
    };
}
