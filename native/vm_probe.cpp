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

extern "C" ZeqvornVmStatus zeqvorn_vm_run_linux(
    const char* kernel_path,
    const char* initramfs_path,
    ZeqvornVmBootError* error)
{
    if (error == nullptr) return ZEQVORN_VM_INVALID_ARGUMENT;

    // 호출자가 같은 출력 구조체를 재사용해도 이전 진단이 남지 않는다.
    *error = {};

    if (kernel_path == nullptr || kernel_path[0] == '\0' ||
        initramfs_path == nullptr || initramfs_path[0] == '\0')
    {
        return ZEQVORN_VM_INVALID_ARGUMENT;
    }

#if defined(__APPLE__)
    return zeqvorn::vm::run_linux_backend(kernel_path, initramfs_path, *error);
#else
    return ZEQVORN_VM_UNAVAILABLE;
#endif
}

