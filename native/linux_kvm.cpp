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
// File Name   : linux_kvm.cpp
//
// Explanation : Probes access to /dev/kvm and checks
//               the kernel's KVM API version.
//
// =============================================

#include "backend.hpp"

#include <cerrno>
#include <fcntl.h>
#include <linux/kvm.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace zeqvorn::vm
{
    namespace
    {
        /** @brief Maps a saved errno to a portable probe status. */
        ZeqvornVmStatus device_error(int error) noexcept
        {
            switch (error)
            {
                case EACCES:
                case EPERM:
                    return ZEQVORN_VM_PERMISSION_DENIED;

                case ENOENT:
                case ENODEV:
                case ENXIO:
                case ENOSYS:
                case ENOTTY:
                    return ZEQVORN_VM_UNAVAILABLE;

                default:
                    return ZEQVORN_VM_HOST_ERROR;
            }
        }
    }

    ZeqvornVmStatus probe_backend(ZeqvornVmProbe& result) noexcept
    {
        result.backend = ZEQVORN_VM_BACKEND_KVM;

        int fd;

        do
        {
            fd = ::open("/dev/kvm", O_RDWR | O_CLOEXEC);
        }
        while (fd < 0 && errno == EINTR);

        if (fd < 0)
        {
            const int saved = errno;
            result.error_domain = ZEQVORN_VM_ERROR_ERRNO;
            result.native_error = saved;
            return device_error(saved);
        }

        // 장치 수준 ioctl로 KVM API 버전을 조회한다.
        int version;

        do
        {
            version = ::ioctl(fd, KVM_GET_API_VERSION, 0UL);
        }
        while (version < 0 && errno == EINTR);

        // close()가 errno를 덮어쓰기 전에 ioctl 오류를 저장한다.
        const int ioctl_error = version < 0 ? errno : 0;
        const int close_error = ::close(fd) < 0 ? errno : 0;

        if (ioctl_error != 0 || close_error != 0)
        {
            result.error_domain = ZEQVORN_VM_ERROR_ERRNO;
            result.native_error = ioctl_error != 0 ? ioctl_error : close_error;

            return ioctl_error != 0
                ? device_error(ioctl_error)
                : ZEQVORN_VM_HOST_ERROR;
        }

        return version == KVM_API_VERSION
            ? ZEQVORN_VM_OK
            : ZEQVORN_VM_UNAVAILABLE;
    }
}