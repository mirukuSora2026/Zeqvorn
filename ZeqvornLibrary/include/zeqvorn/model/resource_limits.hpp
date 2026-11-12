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
// File Name   : resource_limits.hpp
//
// Explanation : Defines CPU, memory, and task limit settings
//               passed to resource controllers.
//
// =============================================

#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

namespace zeqvorn
{
    /**
     * @brief Specifies CPU runtime per scheduling period.
     *
     * quota가 50,000µs이고 period가 100,000µs라면
     * 평균적으로 CPU 하나의 절반에 해당하는 실행 시간을 뜻한다.
     */
    struct CpuLimit
    {
        std::chrono::microseconds quota{};
        std::chrono::microseconds period{100'000};
    };

    /**
     * @brief Contains the optional limits to apply to a workload.
     *
     * An empty optional skips its setting. A numeric zero remains a
     * supplied value. The controller validates backend-specific ranges.
     */
    struct ResourceLimits
    {
        std::optional<CpuLimit> cpu;
        std::optional<std::uint64_t> memory_max_bytes;
        std::optional<std::uint64_t> pids_max;
    };
}