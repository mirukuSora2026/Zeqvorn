// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 miruku_sora2026

// =============================================
//
// Created At  : 11 / 11 / 2026
//
// Updated At  : 11 / 11 / 2026
//
// Created By  : miruku_sora2026
//
// File Name   : macos_vz_boot.mm
//
// Explanation : Boots an ARM64 Linux guest on macOS
//               and bridges its console to host stdio.
//
// =============================================

#include "backend.hpp"

#import <Virtualization/Virtualization.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <termios.h>
#include <unistd.h>

// VM의 시작 결과와 종료 이벤트는 같은 직렬 큐에서 전달된다.
// 최초 종료 결과만 저장하고 대기 중인 호출 스레드를 깨운다.
@interface ZeqvornLinuxBootSession : NSObject <VZVirtualMachineDelegate>
@property(nonatomic, strong) dispatch_semaphore_t stopped;
@property(nonatomic, strong) NSError* error;
@property(nonatomic) BOOL finished;
- (void)finishWithError:(NSError*)error;
@end

@implementation ZeqvornLinuxBootSession

- (void)finishWithError:(NSError*)error
{
    if (self.finished) return;

    self.finished = YES;
    self.error = error;
    dispatch_semaphore_signal(self.stopped);
}

- (void)guestDidStopVirtualMachine:(VZVirtualMachine*)vm
{
    [self finishWithError:nil];
}

- (void)virtualMachine:(VZVirtualMachine*)vm didStopWithError:(NSError*)error
{
    [self finishWithError:error];
}

@end

namespace
{
    std::mutex console_mutex;

    // 실제 터미널에서만 raw 모드를 사용한다. 함수가 성공이나 오류로
    // 반환할 때 호출자가 쓰던 원래 터미널 설정으로 되돌린다.
    class TerminalMode
    {
    public:
        bool enable() noexcept
        {
            if (!isatty(STDIN_FILENO)) return true;
            if (tcgetattr(STDIN_FILENO, &saved_) != 0) return false;

            auto raw = saved_;
            cfmakeraw(&raw);
            active_ = tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0;
            return active_;
        }

        ~TerminalMode()
        {
            if (active_) tcsetattr(STDIN_FILENO, TCSANOW, &saved_);
        }

    private:
        termios saved_{};
        bool active_{false};
    };

    ZeqvornVmStatus report_error(
        ZeqvornVmBootError& output, const char* stage, const char* detail) noexcept
    {
        std::snprintf(output.message, sizeof(output.message), "%s: %s", stage, detail);
        return ZEQVORN_VM_HOST_ERROR;
    }

    ZeqvornVmStatus report_vm_error(
        ZeqvornVmBootError& output, const char* stage, NSError* error) noexcept
    {
        const char* detail = error.localizedDescription.UTF8String;
        return report_error(output, stage, detail ? detail : "Unknown error");
    }

    NSURL* image_url(const char* path)
    {
        return [NSURL fileURLWithFileSystemRepresentation:path
                                             isDirectory:NO
                                            relativeToURL:nil];
    }
}

namespace zeqvorn::vm
{
    ZeqvornVmStatus run_linux_backend(
        const char* kernel_path,
        const char* initramfs_path,
        ZeqvornVmBootError& output) noexcept
    {
        @autoreleasepool
        {
            if (@available(macOS 11.0, *))
            {
                if (![VZVirtualMachine isSupported]) return ZEQVORN_VM_UNAVAILABLE;

                auto* loader = [[VZLinuxBootLoader alloc] initWithKernelURL:image_url(kernel_path)];
                loader.initialRamdiskURL = image_url(initramfs_path);

                // Debian initramfs의 셸을 PID 1로 실행해 디스크 없이 부팅을 검증한다.
                loader.commandLine = @"console=hvc0 rdinit=/usr/bin/sh";

                auto* console = [[VZVirtioConsoleDeviceSerialPortConfiguration alloc] init];
                console.attachment = [[VZFileHandleSerialPortAttachment alloc]
                    initWithFileHandleForReading:NSFileHandle.fileHandleWithStandardInput
                             fileHandleForWriting:NSFileHandle.fileHandleWithStandardOutput];

                auto* config = [[VZVirtualMachineConfiguration alloc] init];
                config.bootLoader = loader;
                config.CPUCount = 2;
                config.memorySize = 2ULL * 1024 * 1024 * 1024;
                config.serialPorts = @[console];

                NSError* error = nil;
                if (![config validateWithError:&error])
                {
                    return report_vm_error(output, "Invalid VM configuration", error);
                }

                // 두 부팅 호출이 동시에 stdin을 읽거나 터미널 모드를 바꾸지
                // 않도록 게스트가 종료될 때까지 콘솔 사용권을 유지한다.
                std::unique_lock lock(console_mutex, std::try_to_lock);
                if (!lock.owns_lock())
                {
                    return report_error(output, "Cannot open console", "A VM session is already active");
                }

                TerminalMode terminal;
                if (!terminal.enable())
                {
                    return report_error(output, "Cannot configure terminal", std::strerror(errno));
                }

                auto queue = dispatch_queue_create("zeqvorn.vm.linux", DISPATCH_QUEUE_SERIAL);
                auto* session = [[ZeqvornLinuxBootSession alloc] init];
                session.stopped = dispatch_semaphore_create(0);

                // VM의 생성, 시작, 해제는 해당 VM의 큐에서 수행한다.
                // delegate 참조는 weak이므로 session을 종료까지 유지한다.
                __block VZVirtualMachine* vm = nil;
                dispatch_sync(queue, ^{
                    vm = [[VZVirtualMachine alloc] initWithConfiguration:config queue:queue];
                    vm.delegate = session;
                    [vm startWithCompletionHandler:^(NSError* start_error) {
                        if (start_error) [session finishWithError:start_error];
                    }];
                });

                dispatch_semaphore_wait(session.stopped, DISPATCH_TIME_FOREVER);
                dispatch_sync(queue, ^{
                    vm.delegate = nil;
                    vm = nil;
                });

                if (session.error)
                {
                    return report_vm_error(output, "Linux VM failed", session.error);
                }
                return ZEQVORN_VM_OK;
            }
        }

        return ZEQVORN_VM_UNAVAILABLE;
    }
}