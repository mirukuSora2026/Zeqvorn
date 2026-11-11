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
// File Name   : process_manager.cpp
//
// Explanation : Creates Linux child processes with pidfd handles,
//               output capture, and exec failure reporting.
//
// =============================================

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "zeqvorn/process/process_manager.hpp"

#include <cerrno>
#include <cstddef>
#include <csignal>
#include <fcntl.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include <optional>
#include <string>
#include <utility>
#include <vector>

// environ : execve()에 현재 프로세스의 환경 변수를 전달하기 위한 POSIX 전역 변수이다.
extern "C" char** environ;

namespace zeqvorn
{
    namespace
    {
        /**
         * @brief Owns both ends of a close-on-exec pipe.
         *
         * Each endpoint has independent RAII ownership, so returning an
         * error during setup closes every endpoint already acquired.
         */
        struct Pipe
        {
            UniqueFd read;
            UniqueFd write;
        };

        /**
         * @brief Ensures that a pipe FD is above 2.
         *
         * Prevents descriptor aliasing with
         * stdin, stdout and stderr during dup2().
         *
         * @param fd Owned pipe endpoint, possibly allocated as 0, 1, or 2
         *           when a standard descriptor was previously closed.
         * @return The original endpoint or a close-on-exec duplicate >= 3.
         *
         * The old low-numbered endpoint closes when this function returns.
         * This keeps later child redirection from overwriting a pipe that
         * is still needed for another stream or exec error reporting.
         */
        [[nodiscard]]
        Result<UniqueFd> promote(UniqueFd fd)
        {
            if (fd.get() >= 3)
            {
                return fd;
            }

            const int high = ::fcntl(
                fd.get(),
                F_DUPFD_CLOEXEC,
                3
            );

            if (high < 0)
            {
                const int saved = errno;

                return Error::from_errno(
                    ErrorCode::SystemError,
                    "fcntl(F_DUPFD_CLOEXEC) failed",
                    saved
                );
            }

            return UniqueFd(high);
        }

        /**
         * @brief Creates a pipe whose endpoints are safe for redirection.
         *
         * @return Two owned descriptors >= 3, or a saved setup error.
         *
         * O_CLOEXEC is set atomically by pipe2(). In particular, the exec
         * error pipe's writer must close when execve() succeeds so the
         * parent can observe EOF without waiting for program termination.
         */
        [[nodiscard]]
        Result<Pipe> make_pipe()
        {
            int raw[2] = {-1, -1};

            if (::pipe2(raw, O_CLOEXEC) < 0)
            {
                const int saved = errno;

                return Error::from_errno(
                    ErrorCode::SystemError,
                    "pipe2() failed",
                    saved
                );
            }

            UniqueFd read(raw[0]);
            UniqueFd write(raw[1]);

            auto high_read = promote(
                std::move(read)
            );

            if (!high_read)
            {
                return high_read.error();
            }

            auto high_write = promote(
                std::move(write)
            );

            if (!high_write)
            {
                return high_write.error();
            }

            return Pipe{
                std::move(high_read).value(),
                std::move(high_write).value()
            };
        }

        /**
         * @brief Makes the parent's capture endpoint non-blocking.
         *
         * @param fd Borrowed read endpoint; ownership remains with Pipe.
         * @return Success or the saved fcntl() error.
         *
         * Existing status flags are preserved. Only read endpoints use
         * O_NONBLOCK; the child retains ordinary blocking output writes.
         * collect() can then drain either stream without waiting forever
         * for bytes from the other one.
         */
        [[nodiscard]]
        Result<void> set_nonblocking(int fd)
        {
            const int flags = ::fcntl(
                fd,
                F_GETFL
            );

            if (flags < 0 ||
                ::fcntl(
                    fd,
                    F_SETFL,
                    flags | O_NONBLOCK
                ) < 0)
            {
                const int saved = errno;

                return Error::from_errno(
                    ErrorCode::SystemError,
                    "fcntl(O_NONBLOCK) failed",
                    saved
                );
            }

            return {};
        }

        /**
         * @brief Reaps a specific direct child.
         *
         * Retries when waitpid() is interrupted
         * by EINTR.
         *
         * @param pid Positive PID of a child created by this launcher.
         *
         * Used on a failed launch, where no ProcessHandle will be returned.
         * It never waits for unrelated children and cannot report another
         * error without replacing the original setup or exec failure.
         */
        void wait_child_best_effort(
            pid_t pid
        ) noexcept
        {
            int status = 0;

            while (
                ::waitpid(pid, &status, 0) == -1 &&
                errno == EINTR
            )
            {
            }
        }

        /**
         * @brief Terminates and reaps a newly created child.
         *
         * Assumes the child has not been reaped
         * by another thread or signal handler.
         *
         * @param pid Positive PID returned by fork() in the parent.
         *
         * This helper is limited to the direct child and does not terminate
         * descendants. Blocking reaping is attempted only when SIGKILL was
         * delivered or the child has already disappeared. On other signal
         * failures, a nonblocking reap avoids hanging the failed launch.
         *
         * Cleanup is best effort: a denied signal can leave the child running
         * or unreaped, and a failed spawn returns no ProcessHandle for it.
         * The original errno is preserved for the launch error's caller.
         */
        void kill_and_reap_new_child(
            pid_t pid
        ) noexcept
        {
            const int original_errno = errno;
            int signal_result;
            int signal_error;

            do
            {
                signal_result = ::kill(pid, SIGKILL);
                signal_error = signal_result < 0 ? errno : 0;
            }
            while (signal_result < 0 && signal_error == EINTR);

            if (signal_result == 0 || signal_error == ESRCH)
            {
                wait_child_best_effort(pid);
            }
            else
            {
                // 권한 등의 이유로 종료를 요청하지 못했다면 살아 있는 자식을
                // 무기한 기다리지 않는다. 이미 종료한 경우에만 상태를 회수한다.
                int status = 0;

                while (
                    ::waitpid(pid, &status, WNOHANG) == -1 &&
                    errno == EINTR
                )
                {
                }
            }

            errno = original_errno;
        }

        /**
         * @brief Reports execve() failure to the parent.
         *
         * Only async-signal-safe operations
         * are used in the forked child.
         *
         * @param error_fd Borrowed writer of the private exec error pipe.
         * @param err errno saved at the failing dup2() or execve() call.
         *
         * The pipe protocol is one native int containing errno. Interrupted
         * and partial writes are handled without allocation or logging.
         * _exit() bypasses C++ destructors and inherited stdio buffers;
         * exit() could flush the parent's buffered output a second time.
         */
        [[noreturn]]
        void child_exec_failure(
            int error_fd,
            int err
        ) noexcept
        {
            const auto* bytes =
                reinterpret_cast<const char*>(
                    &err
                );

            std::size_t offset = 0;

            while (offset < sizeof(err))
            {
                const ssize_t n = ::write(
                    error_fd,
                    bytes + offset,
                    sizeof(err) - offset
                );

                if (n > 0)
                {
                    offset +=
                        static_cast<std::size_t>(n);

                    continue;
                }

                if (n < 0 && errno == EINTR)
                {
                    continue;
                }

                break;
            }

            ::_exit(127);
        }

        /**
         * @brief Checks strings for embedded NUL bytes.
         *
         * C++ strings can store NUL bytes, but execve() consumes terminated
         * C strings. Rejecting embedded NUL prevents silent truncation of
         * the executable path or arguments at the system call boundary.
         */
        [[nodiscard]]
        bool has_nul(
            const std::string& text
        ) noexcept
        {
            return text.find('\0') !=
                std::string::npos;
        }
    }

    /**
     * @brief Implements the parent/child launch and ownership handshake.
     *
     * The private error pipe distinguishes exec failure from a program
     * that starts normally and later exits with code 127. Successful exec
     * closes its writer through O_CLOEXEC; a setup failure writes errno
     * before exiting. Output pipes are separate from this protocol.
     */
    Result<ProcessHandle> ProcessManager::spawn(
        const ProcessConfig& config
    )
    {
        // execve()는 C 문자열을 받으므로 빈 경로와 중간 NUL 문자를 먼저 거부한다.
        if (config.executable.empty() ||
            has_nul(config.executable))
        {
            return Error(
                ErrorCode::InvalidArgument,
                "Invalid executable path"
            );
        }

        // 각 인자의 문자열 경계도 검사하여 실제로 전달되는 값이 잘리지 않도록 한다.
        for (const auto& argument : config.arguments)
        {
            if (has_nul(argument))
            {
                return Error(
                    ErrorCode::InvalidArgument,
                    "Process argument contains a NUL byte"
                );
            }
        }

        /**
         * Prepare argv before fork().
         *
         * Argument storage and pointer allocations happen in the parent.
         * Build all strings before taking data() pointers, then append the
         * null sentinel required by execve(). No container is mutated in
         * the child, so these pointers remain valid until exec or _exit.
         */
        std::vector<std::string> arg_storage;

        arg_storage.reserve(
            config.arguments.size() + 1
        );

        arg_storage.push_back(
            config.executable
        );

        for (const auto& arg : config.arguments)
        {
            arg_storage.push_back(arg);
        }

        std::vector<char*> argv;

        argv.reserve(
            arg_storage.size() + 1
        );

        for (auto& arg : arg_storage)
        {
            argv.push_back(arg.data());
        }

        argv.push_back(nullptr);

        // 실행 성공/실패 통지는 stdout 및 stderr와 독립된 파이프를 사용한다.
        auto exec_pipe_result = make_pipe();

        if (!exec_pipe_result)
        {
            return exec_pipe_result.error();
        }

        Pipe exec_pipe =
            std::move(exec_pipe_result).value();

        // stdout의 읽기 끝만 비차단으로 바꾸고, 쓰기 끝은 자식의 일반 출력에 사용한다.
        std::optional<Pipe> out_pipe;

        if (config.capture_stdout)
        {
            auto pipe = make_pipe();

            if (!pipe)
            {
                return pipe.error();
            }

            out_pipe.emplace(
                std::move(pipe).value()
            );

            auto set = set_nonblocking(
                out_pipe->read.get()
            );

            if (!set)
            {
                return set.error();
            }
        }

        // stderr는 별도 파이프로 유지하여 두 스트림을 각각 수집할 수 있도록 한다.
        std::optional<Pipe> err_pipe;

        if (config.capture_stderr)
        {
            auto pipe = make_pipe();

            if (!pipe)
            {
                return pipe.error();
            }

            err_pipe.emplace(
                std::move(pipe).value()
            );

            auto set = set_nonblocking(
                err_pipe->read.get()
            );

            if (!set)
            {
                return set.error();
            }
        }

        // fork() 이후 자식에서 문자열이나 벡터를 조작하지 않도록 포인터를 준비한다.
        const char* const executable =
            arg_storage.front().c_str();

        char* const* const exec_argv =
            argv.data();

        char* const* const exec_env =
            ::environ;

        // Create child process.
        const pid_t pid = ::fork();

        if (pid < 0)
        {
            const int saved = errno;

            return Error::from_errno(
                ErrorCode::ProcessCreationFailed,
                "fork() failed",
                saved
            );
        }

        /**
         * Child process.
         *
         * Avoid allocations, logging and
         * non-async-signal-safe operations.
         */
        if (pid == 0)
        {
            (void)::close(
                exec_pipe.read.get()
            );

            // dup2()가 만든 표준 출력 복제본은 execve() 이후에도 열린 상태로 남는다.
            if (out_pipe)
            {
                (void)::close(
                    out_pipe->read.get()
                );

                if (::dup2(
                    out_pipe->write.get(),
                    STDOUT_FILENO
                ) < 0)
                {
                    child_exec_failure(
                        exec_pipe.write.get(),
                        errno
                    );
                }

                (void)::close(
                    out_pipe->write.get()
                );
            }

            // 사용하지 않는 읽기 끝과 복제 후의 원래 쓰기 끝은 즉시 닫는다.
            if (err_pipe)
            {
                (void)::close(
                    err_pipe->read.get()
                );

                if (::dup2(
                    err_pipe->write.get(),
                    STDERR_FILENO
                ) < 0)
                {
                    child_exec_failure(
                        exec_pipe.write.get(),
                        errno
                    );
                }

                (void)::close(
                    err_pipe->write.get()
                );
            }

            // Replace child image with executable.
            ::execve(
                executable,
                exec_argv,
                exec_env
            );

            // execve() returns only on failure.
            child_exec_failure(
                exec_pipe.write.get(),
                errno
            );
        }

        /**
         * Parent process.
         *
         * Keep only the read ends of stdout and stderr pipes. Closing
         * every parent-side writer is necessary for collect() to see EOF
         * after the child's writers close.
         */
        exec_pipe.write.reset();

        if (out_pipe)
        {
            out_pipe->write.reset();
        }

        if (err_pipe)
        {
            err_pipe->write.reset();
        }

        /**
         * Acquire a pidfd.
         *
         * pidfd provides a stable reference to a process
         * and helps prevent PID reuse issues.
         */

        // 조건부 컴파일 밖에서 선언하여 syscall 번호가 없는 빌드도 문법적으로 유효하게 한다.
        UniqueFd pidfd;

#if defined(SYS_pidfd_open)

        const long raw_pidfd = ::syscall(
            SYS_pidfd_open,
            pid,
            0U
        );

        if (raw_pidfd < 0)
        {
            const int saved = errno;

            kill_and_reap_new_child(pid);

            return Error::from_errno(
                saved == ENOSYS
                    ? ErrorCode::Unsupported
                    : ErrorCode::SystemError,
                "pidfd_open() failed",
                saved
            );
        }

        // Transfer ownership to the RAII wrapper.
        pidfd.reset(
            static_cast<int>(raw_pidfd)
        );

#else

        kill_and_reap_new_child(pid);

        return Error(
            ErrorCode::Unsupported,
            "pidfd_open is not available on this platform"
        );

#endif

        /**
         * Wait for execve() success or failure.
         *
         * O_CLOEXEC closes the child's write
         * descriptor on successful execve().
         *
         * EOF without error bytes means success.
         */
        int exec_errno = 0;

        auto* bytes =
            reinterpret_cast<char*>(
                &exec_errno
            );

        std::size_t received = 0;

        while (received < sizeof(exec_errno))
        {
            const ssize_t n = ::read(
                exec_pipe.read.get(),
                bytes + received,
                sizeof(exec_errno) - received
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

            const int saved = errno;

            kill_and_reap_new_child(pid);

            return Error::from_errno(
                ErrorCode::SystemError,
                "exec handshake read() failed",
                saved
            );
        }

        // Child reported execve() failure.
        if (received == sizeof(exec_errno))
        {
            wait_child_best_effort(pid);

            return Error::from_errno(
                ErrorCode::ExecutionFailed,
                "execve() failed: " +
                    config.executable,
                exec_errno
            );
        }

        // Incomplete protocol response.
        if (received != 0)
        {
            kill_and_reap_new_child(pid);

            return Error(
                ErrorCode::InternalError,
                "Incomplete child exec failure protocol"
            );
        }

        // Transfer captured read descriptors.
        UniqueFd stdout_fd;
        UniqueFd stderr_fd;

        if (out_pipe)
        {
            stdout_fd =
                std::move(out_pipe->read);
        }

        if (err_pipe)
        {
            stderr_fd =
                std::move(err_pipe->read);
        }

        return ProcessHandle(
            pid,
            std::move(pidfd),
            std::move(stdout_fd),
            std::move(stderr_fd)
        );
    }
}

