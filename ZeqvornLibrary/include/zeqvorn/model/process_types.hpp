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
// File Name   : process_types.hpp
//
// Explanation : Defines common process launch options,
//               lifecycle states, and captured output.
//
// =============================================

#pragma once

#include <string>
#include <vector>

namespace zeqvorn
{
    /**
     * @brief Describes a program and the output streams to capture.
     *
     * arguments contains separate tokens after argv[0].
     * The process launcher decides how to resolve executable paths.
     */
    struct ProcessConfig
    {
        std::string executable;
        std::vector<std::string> arguments;

        bool capture_stdout = true;
        bool capture_stderr = true;
    };

    /** @brief Describes the lifecycle observed by a process owner. */
    enum class ProcessState
    {
        Invalid,  // The owner has no process.
        Running,  // The final status has not been collected yet.
        Exited,   // The process returned an exit code.
        Signaled  // The process terminated because of a signal.
    };

    /**
     * @brief Stores the final termination status of a direct child.
     *
     * Each numeric field is meaningful only when its matching flag is set.
     */
    struct ProcessExitStatus
    {
        bool exited = false;
        int exit_code = -1;

        bool signaled = false;
        int signal_number = 0;
    };

    /**
     * @brief Stores separate output streams and the final child status.
     *
     * Each string preserves captured bytes, including embedded NULs.
     * The streams are stored independently.
     */
    struct CapturedOutput
    {
        std::string stdout_text;
        std::string stderr_text;
        ProcessExitStatus status;
    };
}