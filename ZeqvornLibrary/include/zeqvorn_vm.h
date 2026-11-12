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
// File Name   : zeqvorn_vm.h
//
// Explanation : Defines the C ABI for querying host
//               virtualization support.
//
// =============================================

#ifndef ZEQVORN_VM_H
#define ZEQVORN_VM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t ZeqvornVmStatus;

enum
{
    ZEQVORN_VM_OK = 0,
    ZEQVORN_VM_UNAVAILABLE = 1,
    ZEQVORN_VM_PERMISSION_DENIED = 2,
    ZEQVORN_VM_HOST_ERROR = 3,
    ZEQVORN_VM_INVALID_ARGUMENT = 4
};

enum
{
    ZEQVORN_VM_BACKEND_KVM = 1,
    ZEQVORN_VM_BACKEND_VZ = 2,
    ZEQVORN_VM_BACKEND_WHP = 3
};

enum
{
    ZEQVORN_VM_ERROR_NONE = 0,
    ZEQVORN_VM_ERROR_ERRNO = 1,
    ZEQVORN_VM_ERROR_WIN32 = 2,
    ZEQVORN_VM_ERROR_HRESULT = 3
};

/**
 * @brief Contains the selected host backend and any native error.
 *
 * error_domain tells the caller how to interpret native_error.
 * A domain of zero means no native error code was recorded.
 */
typedef struct ZeqvornVmProbe
{
    uint32_t backend;
    uint32_t error_domain;
    int64_t native_error;
} ZeqvornVmProbe;

/** @brief Returns the version of this C ABI. */
uint32_t zeqvorn_vm_abi_version(void);

/**
 * @brief Checks the current host's virtualization backend.
 * @param result Writable output structure owned by the caller.
 * @return A ZeqvornVmStatus. A null output is invalid.
 *
 * The function initializes result on every call with a non-null output.
 * This query checks the host without creating a virtual machine.
 */
ZeqvornVmStatus zeqvorn_vm_probe(ZeqvornVmProbe* result);

#ifdef __cplusplus
}
#endif

#endif