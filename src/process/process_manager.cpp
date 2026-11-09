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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "../../include/zeqvorn/process/process_manager.hpp"
#include "../../include/zeqvorn/core/unique_fd.hpp"

#include <cerrno>
#include <complex>
#include <csignal>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <string>
#include <vector>

extern "C" {
    extern char** environ;
}

namespace zeqvorn
{
    namespace
    {
        /**
         * @brief Waits until a child is reaped.
         *
         * Retries waitpid() when interrupted
         * by a signal.
         */
        void reap_child(pid_t pid) noexcept
        {
            int status = 0;

            while (::waitpid(pid, &status, 0) == -1 &&
                   errno == EINTR)
            {
            }
        }

        /**
         * @brief Terminates and reaps a child.
         *
         * Used when the parent cannot reliably
         * complete the startup handshake.
         *
         * Assumes exclusive wait responsibility
         * for this child process.
         */
        void terminate_and_reap(pid_t pid) noexcept
        {
            (void)::kill(pid, SIGKILL);
            reap_child(pid);
        }

        /**
         * @brief Checks for embedded NUL bytes.
         *
         * execve() expects NUL-terminated
         * C strings for its arguments.
         */
        bool contains_nul(
            const std::string& value
        ) noexcept
        {
            return value.find('\0') !=
                   std::string::npos;
        }
    }

    /**
     * @brief Creates  new child process.
     *
     * Execution sequence.
     *
     * 1. Validate the configuration.
     * 2. Prepare executable arguments.
     * 3. Create an error-reporting pipe.
     * 4. Fork the current process.
     * 5. Execute the requested program.
     * 6. Verify execve() success or failure.
     * 7. Return ProcessHandle or Error.
     */
    Result<ProcessHandle> ProcessManager::spawn(
        const ProcessConfig& config
    )
    {
        // Validate the executable path.
        if (config.executable.empty() ||
            contains_nul(config.executable))
        {
            return Error(
                ErrorCode::InvalidArgument,
                "Executable path is empty or contains a NUL byte"
            );
        }

        // Validate every argument.
        for (const auto& argument : config.arguments)
        {
            if (contains_nul(argument))
            {
                return Error(
                    ErrorCode::InvalidArgument,
                    "An argument contains a NUL byte"
                );
            }
        }

        /**
         * Prepare argv before fork().
         *
         * argv[0] contains the program name.
         * The array must end with nullptr.
         */
        std::vector<std::string> argument_storage;

        argument_storage.reserve(
            config.arguments.size() + 1
        );

        argument_storage.push_back(
            config.executable
        );

        for (const auto& argument : config.arguments)
        {
            argument_storage.push_back(argument);
        }

        std::vector<char*> argv;

        argv.reserve(
            argument_storage.size() + 1
        );

        for (auto& argument : argument_storage)
        {
            argv.push_back(argument.data());
        }

        argv.push_back(nullptr);

        // Save pointers before creating the child.
        const char* const executable =
            argument_storage.front().c_str();

        char* const* const exec_argv = argv.data();

        // Inherit the parent's environment.
        char* const* const exec_env = ::environ;

        /**
         * Create a pipe used only for reporting
         * execve() failures to the parent.
         *
         * 0_CL0EXEC closes the descriptors
         * automatically after successful exec.
         */
        int pipe_fds[2] = {-1, -1};

        if (::pipe2(pipe_fds, O_CLOEXEC) == -1)
        {
            const int saved_errno = errno;

            return Error::from_errno(
                ErrorCode::SystemError,
                "pipe2() failed",
                saved_errno
            );
        }

        UniqueFd read_end(pipe_fds[0]);
        UniqueFd write_end(pipe_fds[1]);

        const int child_read_fd = read_end.get();
        const int child_write_fd = write_end.get();

        /**
         * Create a child process
         *
         * fork() returns:
         *     -1 : failure
         *      0 : child process
         *      1 : parent process (child PID)
         */
        const pid_t pid = ::fork();

        if (pid == -1)
        {
            const int saved_errno = errno;

            return Error::from_errno(
                ErrorCode::ProcessCreationFailed,
                "fork() failed",
                saved_errno
            );
        }

        /**
         * Child process.
         *
         * After fork(), avoid operations that
         * are not async-signal-safe before exec
         */
        if (pid == 0)
        {
            (void)::close(child_read_fd);

            ::execve(
                executable,
                exec_argv,
                exec_env
            );

            // This code executes only if execve fails.
            const int exec_errno = errno;

            const char* bytes =
                reinterpret_cast<const char*>(
                    &exec_errno
                );

            std::size_t written = 0;

            // Report the error to the parent.
            while (written < sizeof(exec_errno))
            {
                const ssize_t n = ::write(
                    child_write_fd,
                    bytes + written,
                    sizeof(exec_errno) - written
                );

                if (n > 0)
                {
                    written += static_cast<std::size_t>(n);
                    continue;
                }

                if (n == -1 && errno == EINTR)
                {
                    continue;
                }

                break;
            }

            ::exit(127);
        }

        /** Parent process.
         *
         * Close the parent's write end.
         * The child is now responsible for
         * writing an error if execve fails.
         */
        write_end.reset();

        int child_errno = 0;
        std::size_t received = 0;

        /**
         * Read the child's execution result.
         *
         * EOF without data:
         *     execve() succeeded.
         *
         * Complete errno value:
         *     execve() failed.
         */
        while (received < sizeof(child_errno))
        {
            auto* bytes =
                reinterpret_cast<char*>(
                    &child_errno
                );

            const ssize_t n = ::read(
                read_end.get(),
                bytes + received,
                sizeof(child_errno) - received
            );

            if (n > 0)
            {
                received +=
                    static_cast<std::size_t>(n);

                continue;
            }

            if (n == 0)
            {
                break;
            }

            if (errno == EINTR)
            {
                continue;
            }

            const int saved_errno = errno;

            read_end.reset();
            terminate_and_reap(pid);

            return Error::from_errno(
                ErrorCode::SystemError,
                "Failed to read child exec status",
                saved_errno
            );
        }

        read_end.reset();

        /**
         * No error bytes were received.
         *
         * The executable was loaded successfully.
         * Its eventual exit status is still unknown.
         */
        if (received == 0)
        {
            return ProcessHandle(pid);
        }

        // Execution failed: reap the child.
        reap_child(pid);

        if (received != sizeof(child_errno))
        {
            return Error(
                ErrorCode::InternalError,
                "Incomplete execve error report"
            );
        }

        return Error::from_errno(
            ErrorCode::ExecutionFailed,
            "execve() failed for " +
                config.executable,
            child_errno
        );
    }
}
