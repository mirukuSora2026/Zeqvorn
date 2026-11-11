// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 miruku_sora2026

// =============================================
//
// Created At  : 08 / 11 / 2026
//
// Updated At  : 11 / 11 / 2026
//
// Created By  : miruku_sora2026
//
// File Name   : core_test.cpp
//
// Explanation : Verifies core error handling, result values,
//               and exclusive file descriptor ownership.
//
// =============================================

#include <cerrno>
#include <fcntl.h>
#include <iostream>
#include <memory>
#include <string>
#include <type_traits>
#include <unistd.h>
#include <utility>
#include <variant>

#include "zeqvorn/core/error.hpp"
#include "zeqvorn/core/result.hpp"
#include "zeqvorn/core/unique_fd.hpp"

using namespace zeqvorn;

// FD 소유권은 복사할 수 없으며 예외 없이 이동할 수 있어야 한다.
static_assert(!std::is_copy_constructible_v<UniqueFd>);
static_assert(!std::is_copy_assignable_v<UniqueFd>);
static_assert(std::is_nothrow_move_constructible_v<UniqueFd>);
static_assert(std::is_nothrow_move_assignable_v<UniqueFd>);
static_assert(!std::is_copy_constructible_v<Result<UniqueFd>>);

/**
 * @brief Verifies error propagation, result access, and FD ownership.
 *
 * @return Zero when all checks pass, or a distinct failure code.
 *
 * Descriptors remain under RAII ownership unless release() is explicitly
 * being tested. Returning after a failure therefore still closes them.
 */
int main()
{
    // Test Result<int>.
    Result<int> success = 42;

    if (!success || success.value() != 42)
        return 1;

    success.value() = 100;

    if (success.value() != 100)
        return 2;

    // Test failure Result.
    Result<int> failed =
        Error(ErrorCode::NotFound, "missing");

    if (!failed.has_error() ||
        failed.error().code != ErrorCode::NotFound)
        return 3;

    // Test Result<void>.
    Result<void> nothing;

    if (!nothing)
        return 4;

    Result<void> empty_fail(
        Error(ErrorCode::Timeout, "late")
    );

    if (empty_fail ||
        empty_fail.error().code != ErrorCode::Timeout)
        return 5;

    // Test move-only values.
    Result<std::unique_ptr<int>> owned =
        std::make_unique<int>(5);

    auto transferred =
        std::move(owned).value();

    if (!transferred || *transferred != 5)
        return 6;

    // Test errno handling.
    Error e = Error::from_errno(
        ErrorCode::IoError,
        "read",
        ENOENT
    );

    if (!e.has_system_error() ||
        e.sys_error.value() != ENOENT)
        return 7;

    if (e.to_string().find("IoError") ==
        std::string::npos)
        return 8;

    // Test UniqueFd.
    auto maybe_fd =
        UniqueFd::open("/dev/null", O_RDONLY);

    if (!maybe_fd)
        return 9;

    UniqueFd fd =
        std::move(maybe_fd).value();

    if (!fd ||
        ::fcntl(fd.get(), F_GETFD) == -1)
        return 10;

    // FD_CLOEXEC prevents this file from leaking through execve().
    if ((::fcntl(fd.get(), F_GETFD) & FD_CLOEXEC) == 0)
        return 14;

    // Moving leaves no duplicate owner; reset(get()) must preserve the FD.
    UniqueFd moved(std::move(fd));

    if (fd.valid() || !moved.valid())
        return 15;

    moved.reset(moved.get());

    if (::fcntl(moved.get(), F_GETFD) == -1)
        return 16;

    // release() 후에는 호출자가 반환된 FD를 직접 회수해야 한다.
    const int raw = moved.release();

    if (moved.valid() || raw < 0)
        return 11;

    if (::close(raw) != 0)
        return 12;

    // Test open failure.
    auto nonexistent = UniqueFd::open(
        "/this/does/not/exist",
        O_RDONLY
    );

    if (nonexistent ||
        nonexistent.error().sys_error.value() != ENOENT)
        return 13;

    auto null_path = UniqueFd::open(nullptr, O_RDONLY);

    if (null_path ||
        null_path.error().code != ErrorCode::InvalidArgument)
        return 17;

    // Replacing ownership closes the destination's previous descriptor.
    {
        auto first = UniqueFd::open("/dev/null", O_RDONLY);
        auto second = UniqueFd::open("/dev/null", O_RDONLY);

        if (!first || !second)
            return 18;

        auto destination = std::move(first).value();
        auto source = std::move(second).value();
        const int previous = destination.get();
        const int replacement = source.get();

        destination = std::move(source);

        if (source.valid() || destination.get() != replacement)
            return 19;

        errno = 0;

        if (::fcntl(previous, F_GETFD) != -1 || errno != EBADF)
            return 20;
    }

    // Reading the inactive variant alternative must fail explicitly.
    bool wrong_access_rejected = false;

    try
    {
        (void)failed.value();
    }
    catch (const std::bad_variant_access&)
    {
        wrong_access_rejected = true;
    }

    if (!wrong_access_rejected)
        return 21;

    errno = EACCES;
    const auto current = Error::from_current_errno(
        ErrorCode::PermissionDenied, "open"
    );

    if (current.sys_error.value() != EACCES || current.location.line() == 0)
        return 22;

    std::cout
        << "All Zeqvorn core tests passed.\n";

    return 0;
}
