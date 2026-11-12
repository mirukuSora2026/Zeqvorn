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
// File Name   : backend.hpp
//
// Explanation : Declares the internal host backend entry point.
//
// =============================================

#pragma once

#include "zeqvorn_vm.h"

namespace zeqvorn::vm
{
    /** @brief Populates the probe using the host OS's native API. */
    ZeqvornVmStatus probe_backend(ZeqvornVmProbe& result) noexcept;
}