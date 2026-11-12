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
// File Name   : windows_whp.cpp
//
// Explanation : Probes the Windows Hypervisor Platform and
//               preserves Win32 or HRESULT diagnostics.
//
// =============================================

#include "backend.hpp"

#if defined(_WIN64)
#include <windows.h>
#if defined(__MINGW32__)
#include <winhvplatform.h>
#else
#include <WinHvPlatform.h>
#endif

#include <bit>
#endif

namespace zeqvorn::vm
{
#if defined(_WIN64)
    namespace
    {
        /** @brief Releases the DLL reference when the probe returns. */
        struct Module
        {
            HMODULE handle;

            ~Module() noexcept
            {
                if (handle != nullptr)
                {
                    (void)::FreeLibrary(handle);
                }
            }
        };

        ZeqvornVmStatus loader_failure(
            ZeqvornVmProbe& result,
            DWORD error
        ) noexcept
        {
            result.error_domain = ZEQVORN_VM_ERROR_WIN32;
            result.native_error = static_cast<int64_t>(error);

            switch (error)
            {
                case ERROR_MOD_NOT_FOUND:
                case ERROR_DLL_NOT_FOUND:
                case ERROR_PROC_NOT_FOUND:
                    return ZEQVORN_VM_UNAVAILABLE;

                case ERROR_ACCESS_DENIED:
                case ERROR_PRIVILEGE_NOT_HELD:
                    return ZEQVORN_VM_PERMISSION_DENIED;

                default:
                    return ZEQVORN_VM_HOST_ERROR;
            }
        }

        ZeqvornVmStatus capability_failure(
            ZeqvornVmProbe& result,
            HRESULT error
        ) noexcept
        {
            result.error_domain = ZEQVORN_VM_ERROR_HRESULT;
            result.native_error = static_cast<int64_t>(error);

            if (error == E_ACCESSDENIED ||
                error == HRESULT_FROM_WIN32(ERROR_PRIVILEGE_NOT_HELD))
            {
                return ZEQVORN_VM_PERMISSION_DENIED;
            }

            if (error == E_NOTIMPL ||
                error == HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED))
            {
                return ZEQVORN_VM_UNAVAILABLE;
            }

            return ZEQVORN_VM_HOST_ERROR;
        }
    }
#endif

    ZeqvornVmStatus probe_backend(ZeqvornVmProbe& result) noexcept
    {
        result.backend = ZEQVORN_VM_BACKEND_WHP;

#if defined(_WIN64)
        // WHP는 선택 기능이다. 실행 중 시스템 DLL을 찾아 상태를 조회한다.
        const Module platform{
            ::LoadLibraryExW(
                L"WinHvPlatform.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32
            )
        };

        if (platform.handle == nullptr)
        {
            return loader_failure(result, ::GetLastError());
        }

        const FARPROC address = ::GetProcAddress(
            platform.handle, "WHvGetCapability"
        );

        if (address == nullptr)
        {
            return loader_failure(result, ::GetLastError());
        }

        // SDK가 선언한 함수 타입에서 인자와 호출 규약을 가져온다.
        const auto query = std::bit_cast<decltype(&::WHvGetCapability)>(address);
        WHV_CAPABILITY capability{};
        UINT32 written = 0;

        const HRESULT present = query(
            WHvCapabilityCodeHypervisorPresent,
            &capability,
            static_cast<UINT32>(sizeof(capability)),
            &written
        );

        if (FAILED(present))
        {
            return capability_failure(result, present);
        }

        if (written < sizeof(capability.HypervisorPresent))
        {
            return capability_failure(result, E_UNEXPECTED);
        }

        if (capability.HypervisorPresent == FALSE)
        {
            return ZEQVORN_VM_UNAVAILABLE;
        }

#if defined(_M_ARM64) || defined(__aarch64__)
        // ARM64에서는 ARM64 WHP 지원 비트도 확인한다.
        WHV_CAPABILITY_FEATURES features{};
        written = 0;

        const HRESULT features_status = query(
            WHvCapabilityCodeFeatures,
            &features,
            static_cast<UINT32>(sizeof(features)),
            &written
        );

        if (FAILED(features_status))
        {
            return capability_failure(result, features_status);
        }

        if (written < sizeof(features))
        {
            return capability_failure(result, E_UNEXPECTED);
        }

        if (!features.Arm64Support)
        {
            return ZEQVORN_VM_UNAVAILABLE;
        }
#endif

        return ZEQVORN_VM_OK;
#else
        return ZEQVORN_VM_UNAVAILABLE;
#endif
    }
}
