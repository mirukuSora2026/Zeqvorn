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
// File Name   : process_reaper.cpp
//
// Explanation : Implements explicit child process tracking
//               and collection of final exit statuses.
//
// =============================================

#include "zeqvorn/process/process_reaper.hpp"

#include <utility>

namespace zeqvorn
{
    /**
     * @brief Validates a child before taking over its handle.
     *
     * Ownership moves only after all checks pass. In particular,
     * rejecting captured children leaves their pipes available
     * to the caller for a subsequent collect().
     */
    Result<void> ProcessReaper::track(ProcessHandle&& child)
    {
        if (!child.valid() || child.reaped())
        {
            return Error(
                ErrorCode::InvalidState,
                "Cannot track invalid/reaped process"
            );
        }

        if (child.capturing_output())
        {
            return Error(
                ErrorCode::InvalidArgument,
                "Captured children must be handled with collect()"
            );
        }

        // 이동 생성은 PID, pidfd, 회수 책임을 함께 이전한다.
        // 호출자에게 남은 핸들은 이후 유효하지 않은 상태가 된다.
        children_.emplace_back(std::move(child));
        return {};
    }

    /**
     * @brief Returns records for all currently terminated children.
     *
     * try_wait() uses WNOHANG and caches completed statuses. List
     * removal is postponed until every check succeeds so a later
     * wait error cannot discard records already collected here.
     */
    Result<std::vector<ReapedChild>> ProcessReaper::poll_completed()
    {
        std::vector<ReapedChild> completed;

        // 자식을 회수하기 전에 결과 공간을 확보한다. 할당 실패 시에도
        // 목록에 있는 핸들과 회수 책임은 그대로 유지된다.
        completed.reserve(children_.size());

        for (auto& child : children_)
        {
            auto status = child.try_wait();

            if (!status)
            {
                return status.error();
            }

            if (status.value())
            {
                completed.push_back({child.pid(), *status.value()});
            }
        }

        // PID와 종료 상태를 결과에 옮긴 뒤에만 핸들을 제거한다.
        // remove_if()는 노드를 지우므로 이동 대입이 필요하지 않다.
        children_.remove_if([](const ProcessHandle& child)
        {
            return child.reaped();
        });

        return completed;
    }

    /**
     * @brief Waits for every tracked child in insertion order.
     *
     * On failure, all handles remain in the list. Processes already
     * waited on retain their cached statuses and can be returned
     * again when the caller retries this operation.
     */
    Result<std::vector<ReapedChild>> ProcessReaper::wait_all()
    {
        std::vector<ReapedChild> completed;
        completed.reserve(children_.size());

        for (auto& child : children_)
        {
            auto status = child.wait();

            if (!status)
            {
                return status.error();
            }

            completed.push_back({child.pid(), status.value()});
        }

        // 모든 상태를 확보한 뒤 목록을 비워 pidfd를 함께 닫는다.
        children_.clear();
        return completed;
    }
}

