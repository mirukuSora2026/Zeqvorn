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
// File Name   : process_manager_test.cpp
//
// Explanation : Verifies Linux process execution, output
//               capture, lifecycle, and child reaping.
//
// =============================================

#include <chrono>
#include <cerrno>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <string>
#include <sys/wait.h>
#include <type_traits>
#include <utility>

#include "zeqvorn/process/process_manager.hpp"
#include "zeqvorn/process/process_reaper.hpp"

using namespace zeqvorn;
using namespace std::chrono_literals;

static_assert(!std::is_copy_constructible_v<ProcessHandle>);
static_assert(std::is_nothrow_move_constructible_v<ProcessHandle>);
static_assert(!std::is_move_assignable_v<ProcessHandle>);
static_assert(!std::is_copy_constructible_v<UniqueFd>);

namespace
{
    int failures = 0;

    /**
     * @brief Records a failure without skipping subsequent cleanup.
     *
     * @param condition Expected condition.
     * @param label Description printed if the condition is false.
     */
    void check(
        bool condition,
        const char* label
    )
    {
        if (!condition)
        {
            std::cerr
                << "FAIL: " << label << '\n';

            ++failures;
        }
    }

    /**
     * @brief Cleans up a still-owned child when a test takes a failure path.
     *
     * ProcessHandle deliberately performs no automatic process termination.
     * This test guard prevents failed checks from leaving children behind.
     * Moved and reaped handles require no action.
     */
    class ChildCleanup final
    {
    private:
        ProcessHandle& child_;

    public:
        explicit ChildCleanup(ProcessHandle& child) noexcept : child_(child)
        {
        }

        ChildCleanup(const ChildCleanup&) = delete;
        ChildCleanup& operator=(const ChildCleanup&) = delete;

        ~ChildCleanup() noexcept
        {
            if (!child_.valid() || child_.reaped())
                return;

            // An unreaped direct child's PID cannot be reused before wait.
            (void)::kill(child_.pid(), SIGKILL);

            while (::waitpid(child_.pid(), nullptr, 0) < 0 && errno == EINTR)
            {
            }
        }
    };

    /**
     * @brief Counts descriptors through Linux procfs.
     *
     * @return Number of descriptors, including the iterator's temporary FD.
     *
     * Both measurements include the iterator, so their difference detects
     * leaked capture pipes and pidfds after process handles leave scope.
     */
    std::size_t fd_count()
    {
        std::size_t count = 0;

        for (const auto& ignored :
             std::filesystem::directory_iterator(
                 "/proc/self/fd"))
        {
            (void)ignored;
            ++count;
        }

        return count;
    }

    /**
     * @brief Launches and collects a process.
     *
     * @param config Executable, arguments, and capture settings.
     * @param timeout Maximum collection duration.
     * @param output_limit Combined stdout/stderr byte budget.
     * @return Captured output or a launch/collection error.
     */
    Result<CapturedOutput> launch_and_collect(
        const ProcessConfig& config,
        std::chrono::milliseconds timeout = 3s,
        std::size_t output_limit = 1024 * 1024
    )
    {
        auto spawned =
            ProcessManager::spawn(config);

        if (!spawned)
        {
            return spawned.error();
        }

        auto child =
            std::move(spawned).value();

        const ChildCleanup cleanup(child);

        return child.collect(
            timeout,
            output_limit
        );
    }
}

/**
 * @brief Runs process integration checks with trusted Linux executables.
 *
 * @return Zero when all checks succeed; one otherwise.
 *
 * Requires pidfd support and procfs. Tests process management and resource
 * ownership; these checks do not establish security isolation.
 */
int main()
{
    const auto baseline_fds = fd_count();

    // Test 1: stdout and stderr capture.
    {
        auto result = launch_and_collect({
            "/bin/sh",
            {
                "-c",
                "printf abc; printf def >&2; exit 7"
            }
        });

        check(result.has_value(),
              "both streams captured");

        if (result)
        {
            check(
                result.value().stdout_text == "abc",
                "stdout content"
            );

            check(
                result.value().stderr_text == "def",
                "stderr content"
            );

            check(
                result.value().status.exited &&
                result.value().status.exit_code == 7,
                "nonzero exit recorded"
            );
        }
    }

    // Test 2: execve() failure.
    {
        auto result = ProcessManager::spawn({
            "/nonexistent/zeqvorn-program",
            {}
        });

        check(
            !result &&
            result.error().code ==
                ErrorCode::ExecutionFailed,
            "exec failure propagated"
        );
    }

    // Test 3: invalid executable.
    {
        auto result = ProcessManager::spawn({
            "",
            {}
        });

        check(
            !result &&
            result.error().code ==
                ErrorCode::InvalidArgument,
            "invalid configuration rejected"
        );
    }

    // Test 4: embedded NUL in argument.
    {
        auto result = ProcessManager::spawn({
            "/bin/true",
            {
                std::string("x\0y", 3)
            }
        });

        check(
            !result &&
            result.error().code ==
                ErrorCode::InvalidArgument,
            "NUL argument rejected"
        );
    }

    // Test 5: maximum output size.
    {
        auto result = launch_and_collect(
            {
                "/bin/sh",
                {"-c", "printf abc; printf def >&2"}
            },
            3s,
            5
        );

        check(
            !result &&
            result.error().code ==
                ErrorCode::ResourceExhausted,
            "output limit enforced"
        );
    }

    // Test 6: timeout.
    {
        auto result = launch_and_collect(
            {
                "/bin/sleep",
                {"3"}
            },
            25ms
        );

        check(
            !result &&
            result.error().code ==
                ErrorCode::Timeout,
            "timeout enforced"
        );
    }

    // Test 7: pidfd and signal handling.
    // Invalid collection parameters must leave the child available to collect.
    {
        auto launched = ProcessManager::spawn({"/bin/echo", {"retry"}});
        check(launched.has_value(), "spawn timeout-boundary child");

        if (launched)
        {
            auto child = std::move(launched).value();
            const ChildCleanup cleanup(child);
            auto rejected = child.collect(-1ms);

            check(!rejected &&
                  rejected.error().code == ErrorCode::InvalidArgument,
                  "negative timeout rejected");

            // milliseconds::max() must not overflow the clock deadline.
            auto collected = child.collect(std::chrono::milliseconds::max());
            check(collected && collected.value().stdout_text == "retry\n",
                  "maximum timeout accepted after parameter rejection");
        }
    }

    // The exact shared budget succeeds; a zero budget accepts no output.
    {
        auto exact = launch_and_collect(
            {"/bin/sh", {"-c", "printf abc; printf def >&2"}}, 3s, 6
        );

        check(exact && exact.value().stdout_text == "abc" &&
              exact.value().stderr_text == "def",
              "exact combined output limit accepted");

        auto empty = launch_and_collect({"/bin/true", {}}, 3s, 0);
        check(empty.has_value(), "zero output limit accepts empty output");
    }

    // pidfd signaling and blocking wait use a child without capture pipes.
    {
        auto result = ProcessManager::spawn({
            "/bin/sleep",
            {"10"},
            false,
            false
        });

        check(
            result.has_value(),
            "spawn sleep"
        );

        if (result)
        {
            auto process =
                std::move(result).value();

            const ChildCleanup cleanup(process);

            check(
                process.valid() &&
                process.pidfd() >= 0,
                "pidfd acquired"
            );

            check(
                process.state() ==
                    ProcessState::Running,
                "running state"
            );

            auto first = process.try_wait();

            check(
                first && !first.value(),
                "try_wait nonblocking"
            );

            auto signal =
                process.send_signal(SIGTERM);

            check(
                signal.has_value(),
                "pidfd_send_signal works"
            );

            auto status = process.wait();

            check(
                status &&
                status.value().signaled &&
                status.value().signal_number == SIGTERM,
                "signal exit captured"
            );

            check(
                process.state() ==
                    ProcessState::Signaled &&
                process.reaped(),
                "terminal state cached"
            );

            auto again = process.wait();

            check(
                again && again.value().signaled,
                "cached wait idempotent"
            );

            auto invalid_signal =
                process.send_signal(SIGTERM);

            check(
                !invalid_signal &&
                invalid_signal.error().code ==
                    ErrorCode::InvalidState,
                "signal after reaping rejected"
            );
        }
    }

    // Test 8: clean process exit.
    {
        auto result = ProcessManager::spawn({
            "/bin/true",
            {},
            false,
            false
        });

        if (result)
        {
            auto process =
                std::move(result).value();

            const ChildCleanup cleanup(process);

            auto status = process.wait();

            check(
                status &&
                status.value().exited &&
                status.value().exit_code == 0,
                "clean zero exit and wait"
            );

            check(
                process.state() ==
                    ProcessState::Exited,
                "exited state"
            );
        }
        else
        {
            check(false, "spawn true");
        }
    }

    // Test 9: move ownership.
    {
        auto result = ProcessManager::spawn({
            "/bin/echo",
            {"move-test"}
        });

        if (result)
        {
            auto proc =
                std::move(result).value();

            ProcessHandle moved(
                std::move(proc)
            );

            const ChildCleanup cleanup(moved);

            check(
                !proc.valid() && proc.pid() == -1 &&
                    proc.state() == ProcessState::Invalid && moved.valid(),
                "move transfers process handle"
            );

            auto collected = moved.collect();

            check(
                collected &&
                collected.value().stdout_text ==
                    "move-test\n",
                "moved handle collects"
            );
        }
        else
        {
            check(false, "spawn move-test");
        }
    }

    // Each background pipeline exceeds a typical pipe capacity.
    // 두 스트림을 실제로 동시에 채워 순차적인 read()의 교착을 검출한다.
    {
        auto result = launch_and_collect({
            "/bin/sh",
            {
                "-c",
                "head -c 131072 /dev/zero | tr '\\000' x & "
                "head -c 131072 /dev/zero | tr '\\000' y >&2 & wait"
            }
        });

        check(
            result.has_value(),
            "large parallel stdout/stderr"
        );

        if (result)
        {
            check(
                result.value().stdout_text == std::string(131072, 'x'),
                "large stdout content"
            );

            check(
                result.value().stderr_text == std::string(131072, 'y'),
                "large stderr content"
            );
        }
    }

    // Test 11: ProcessReaper.
    {
        ProcessReaper reaper;

        auto a = ProcessManager::spawn({
            "/bin/true", {}, false, false
        });

        auto b = ProcessManager::spawn({
            "/bin/false", {}, false, false
        });

        if (a && b)
        {
            auto aa = std::move(a).value();
            auto bb = std::move(b).value();

            // track() may fail before taking ownership; guards handle that
            // path and become inactive after a successful move to reaper.
            const ChildCleanup cleanup_a(aa);
            const ChildCleanup cleanup_b(bb);

            check(
                reaper.track(std::move(aa)).has_value(),
                "reaper tracks first child"
            );

            check(
                reaper.track(std::move(bb)).has_value(),
                "reaper tracks second child"
            );

            auto ready = reaper.poll_completed();

            check(
                ready.has_value(),
                "reaper nonblocking poll"
            );

            const auto ready_count =
                ready ? ready.value().size() : 0;

            auto rest = reaper.wait_all();

            check(
                rest.has_value() &&
                ready_count + rest.value().size() == 2,
                "reaper reaps both children"
            );

            check(
                reaper.size() == 0,
                "reaper empty after cleanup"
            );
        }
        else
        {
            check(false, "spawn reaper children");

            if (a)
            {
                auto child = std::move(a).value();
                const ChildCleanup cleanup(child);
            }

            if (b)
            {
                auto child = std::move(b).value();
                const ChildCleanup cleanup(child);
            }
        }
    }

    // wait() must reject captured children without consuming their output.
    {
        auto launched = ProcessManager::spawn({"/bin/echo", {"captured"}});
        check(launched.has_value(), "spawn captured-wait child");

        if (launched)
        {
            auto child = std::move(launched).value();
            const ChildCleanup cleanup(child);
            auto rejected = child.wait();

            check(!rejected &&
                  rejected.error().code == ErrorCode::InvalidState,
                  "wait rejects capture pipes");

            auto collected = child.collect();
            check(collected && collected.value().stdout_text == "captured\n",
                  "collect available after rejected wait");
        }
    }

    // Test 12: FD leak detection.
    check(
        fd_count() == baseline_fds,
        "no leaked FDs after tests"
    );

    // A zombie still counts as a child. ECHILD proves direct children were reaped.
    errno = 0;
    check(::waitpid(-1, nullptr, WNOHANG) == -1 && errno == ECHILD,
          "no running or unreaped direct children after tests");

    if (failures)
    {
        std::cerr
            << failures << " test(s) failed\n";

        return 1;
    }

    std::cout
        << "All Zeqvorn process tests passed.\n";

    return 0;
}
