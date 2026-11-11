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
// File Name   : process_manager.hpp
//
// Explanation : Defines process execution configuration
//               and the Linux process management interface.
//
// =============================================

#pragma once

#include <string>
#include <vector>

#include "process_handle.hpp"

namespace zeqvorn
{
    /**
     * @brief Describes a program and the output streams to capture.
     *
     * executable:
     *     Path passed directly to execve(). Absolute paths are
     *     recommended; relative paths use the parent's working
     *     directory. PATH lookup is not performed.
     *
     * arguments:
     *     Arguments passed after argv[0], which is set to executable.
     *     Spaces and shell metacharacters remain literal characters.
     *
     * capture_stdout / capture_stderr:
     *     Creates a separate pipe for each enabled stream. Disabled
     *     streams inherit the parent's corresponding descriptor.
     *
     * The parent environment and working directory are inherited.
     * No shell expansion, resource limits, or privilege isolation
     * are applied. Paths and arguments must not contain NUL bytes.
     */
    struct ProcessConfig
    {
        std::string executable;                  ///< Program image to execute.
        std::vector<std::string> arguments;      ///< Arguments after argv[0].

        bool capture_stdout = true;              ///< Capture standard output.
        bool capture_stderr = true;              ///< Capture standard error.
    };

    /**
     * @brief Creates Linux child processes.
     *
     * Requires a Linux kernel with pidfd support and default SIGCHLD
     * handling. A signal handler or another thread must not reap these
     * children; their ProcessHandle owns that responsibility.
     *
     * Designed for a single-threaded launcher.
     *
     * This is not a security sandbox.
     */
    class ProcessManager final
    {
    public:
        /**
         * @brief Creates a direct child and checks its exec handshake.
         *
         * @param config Executable path, arguments, and capture options.
         * @return ProcessHandle after the exec error pipe closes without
         *         reporting a setup or execve() error.
         * @return InvalidArgument for an empty path or embedded NUL byte.
         * @return ExecutionFailed with saved errno when the child reports
         *         redirection or execve() failure.
         * @return Unsupported if pidfd_open is unavailable; other setup
         *         failures retain their operating system error information.
         *
         * Pipe creation and argument allocation occur before fork(). The
         * child performs only descriptor operations, execve(), and _exit().
         * The parent closes unused pipe ends and reaps the child if setup
         * fails after fork(). A successful result transfers descriptor and
         * reaping ownership to the caller.
         *
         * Call collect() when capturing output, or wait() when capture is
         * disabled. Destroying a running handle does not reap the child.
         * This call can block during the exec handshake; collect()'s
         * timeout starts after spawn() has returned.
         */
        [[nodiscard]]
        static Result<ProcessHandle> spawn(
            const ProcessConfig& config
        );
    };
}
