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
// Explanation : Defines process execution configuration
//               and the Linux process management interface
//
// =============================================

#pragma once

#include <string>
#include <vector>

#include "process_handle.hpp"

namespace zeqvorn
{
    /**
     * @brief Configuration for a child process.
     *
     * executable:
     *     Path to the executable file.
     *
     * arguments:
     *     Arguments passed after argv[0].
     *
     * The executable is executed directly
     * without using a shell.
     */
    struct ProcessConfig
    {
        std::string executable;
        std::vector<std::string> arguments;
    };

    /**
     * @brief Creates and manages child processes.
     *
     * Use Linux process APIs to execute
     * external programs.
     *
     * Process creation and execution failures
     * are represented using Result<T>.
     */
    class ProcessManager final
    {
    public:
        /**
         * @brief Creates and executes a child process.
         *
         * @param config Process execution configuration.
         *
         * @return ProcessHandle on successful execve().
         * @return Error if creation or execution fails.
         *
         * A successful result doesn't guarantee
         * that the program will exit successfully.
         *
         * The caller must eventually wait for
         * the returned child process.
         */
        [[nodiscard]]
        static Result<ProcessHandle> spawn(
            const ProcessConfig& config
        );
    };
}