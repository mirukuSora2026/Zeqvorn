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
// File Name   : macos_vz.mm
//
// Explanation : Queries Virtualization.framework support
//               for the macOS virtual machine backend.
//
// =============================================

#include "backend.hpp"

#import <Virtualization/Virtualization.h>

namespace zeqvorn::vm
{
    ZeqvornVmStatus probe_backend(ZeqvornVmProbe& result) noexcept
    {
        result.backend = ZEQVORN_VM_BACKEND_VZ;

        @autoreleasepool
        {
            if (@available(macOS 11.0, *))
            {
                // 호스트의 지원 여부를 조회한다. 실행 파일 권한과 VM 구성은
                // 실제 VM 생성 단계에서 별도로 검사한다.
                return [VZVirtualMachine isSupported]
                    ? ZEQVORN_VM_OK
                    : ZEQVORN_VM_UNAVAILABLE;
            }
        }

        return ZEQVORN_VM_UNAVAILABLE;
    }
}