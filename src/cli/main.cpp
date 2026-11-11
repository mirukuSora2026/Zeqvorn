// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 miruku_sora2026

// =============================================
//
// Created At  : 10 / 11 / 2026
//
// Updated At  : 11 / 11 / 2026
//
// Created By  : miruku_sora2026
//
// File Name   : main.cpp
//
// Explanation : Provides the Zeqvorn command-line interface
//               for launching and collecting child processes.
//
// =============================================

#include <chrono>
#include <iostream>
#include <string_view>
#include <utility>

#include "zeqvorn/process/process_manager.hpp"

/**
 * @brief Runs a program and forwards its captured output and exit status.
 *
 * @param argc Number of command-line arguments, including argv[0].
 * @param argv Arguments supplied by the operating system.
 * @return 0 for --version, 2 for incorrect usage, 1 for an engine error,
 *         or the child's exit code (128 + signal for signal termination).
 *
 * The -- delimiter separates Zeqvorn options from the executable and
 * its arguments. Programs execute directly through execve(); shell
 * expansion occurs only if the user explicitly launches a shell.
 */
int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--version")
    {
        std::cout << "Zeqvorn 0.1.0\n";
        return 0;
    }

    // argc를 먼저 검사하므로 뒤쪽 argv 접근은 항상 범위 안에 있다.
    // -- 뒤의 인자는 옵션처럼 보여도 실행할 프로그램에 그대로 전달한다.
    if (argc < 5 ||
        std::string_view(argv[1]) != "process" ||
        std::string_view(argv[2]) != "run" ||
        std::string_view(argv[3]) != "--")
    {
        std::cerr
            << "Usage: zeqvorn --version\n"
            << "       zeqvorn process run -- "
            << "/absolute/executable [args...]\n";
        return 2;
    }

    zeqvorn::ProcessConfig config;
    config.executable = argv[4];

    for (int i = 5; i < argc; ++i)
    {
        config.arguments.emplace_back(argv[i]);
    }

    auto launched = zeqvorn::ProcessManager::spawn(config);

    if (!launched)
    {
        std::cerr << launched.error().to_string() << '\n';
        return 1;
    }

    // Result에서 핸들을 이동해 자식 회수 책임을 현재 범위로 가져온다.
    zeqvorn::ProcessHandle child = std::move(launched).value();

    // 두 출력 파이프를 함께 비우면서 최대 5초, 합계 1 MiB까지 수집한다.
    // 제한 초과나 수집 오류가 생기면 collect()가 자식을 정리한다.
    auto output = child.collect(std::chrono::seconds(5), 1024 * 1024);

    if (!output)
    {
        std::cerr << output.error().to_string() << '\n';
        return 1;
    }

    std::cout << output.value().stdout_text;
    std::cerr << output.value().stderr_text;

    const auto& status = output.value().status;

    if (status.exited)
    {
        return status.exit_code;
    }

    // 셸에서 사용하는 관례에 맞춰 시그널 종료 상태를 숫자로 전달한다.
    if (status.signaled)
    {
        return 128 + status.signal_number;
    }

    return 1;
}
