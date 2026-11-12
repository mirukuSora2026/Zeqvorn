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
// File Name   : vm_probe.cpp
//
// Explanation : Exposes the shared host probe through a C ABI.
//
// =============================================

#include "backend.hpp"

extern "C" uint32_t zeqvorn_vm_abi_version(void)
{
    return 1;
}

extern "C" ZeqvornVmStatus zeqvorn_vm_probe(ZeqvornVmProbe* result)
{
    if (result == nullptr) return ZEQVORN_VM_INVALID_ARGUMENT;

    // 이전 호출의 오류 코드가 새 결과에 남지 않도록 초기화한다.
    *result = {};
    return zeqvorn::vm::probe_backend(*result);
}
