// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 miruku_sora2026

// =============================================
//
// Created At  : 11 / 11 / 2026
//
// Updated At  : 11 / 12 / 2026
//
// Created By  : miruku_sora2026
//
// File Name   : lib.rs
//
// Explanation : Wraps the native VM probe for Rust callers.
//
// =============================================

use std::ffi::{CStr, CString, c_char};
use std::path::Path;

#[repr(C)]
#[derive(Default)]
struct RawProbe {
    backend: u32,
    error_domain: u32,
    native_error: i64,
}

/// C ABI의 ZeqvornVmBootError와 같은 레이아웃이다.
#[repr(C)]
struct RawBootError {
    message: [c_char; 512],
}

unsafe extern "C" {
    fn zeqvorn_vm_abi_version() -> u32;
    fn zeqvorn_vm_probe(result: *mut RawProbe) -> i32;
    
    fn zeqvorn_vm_run_linux(
        kernel_path: *const c_char,
        initramfs_path: *const c_char,
        error: *mut RawBootError,
    ) -> i32;
}

/// 가상화 지원 확인 결과. 값의 의미는 zeqvorn_vm.h와 같다.
#[derive(Debug)]
pub struct VmProbe {
    pub status: i32,
    pub backend: u32,
    pub error_domain: u32,
    pub native_error: i64,
}

impl VmProbe {
    pub fn available(&self) -> bool {
        self.status == 0
    }

    pub fn backend_name(&self) -> &'static str {
        match self.backend {
            1 => "KVM",
            2 => "Virtualization.framework",
            3 => "WHP",
            _ => "unknown",
        }
    }
}

/// 네이티브 라이브러리의 C ABI 버전을 반환한다.
pub fn abi_version() -> u32 {
    // SAFETY: 이 함수는 인자와 포인터를 받지 않는다.
    unsafe { zeqvorn_vm_abi_version() }
}

/// 현재 호스트에서 가상화 백엔드를 사용할 수 있는지 확인한다.
pub fn probe() -> VmProbe {
    let mut raw = RawProbe::default();

    // SAFETY: RawProbe는 C 헤더와 동일한 배치다. 함수는 호출 중에만
    // 쓰기 가능한 raw를 사용하며, 반환 뒤 포인터를 보관하지 않는다.
    let status = unsafe { zeqvorn_vm_probe(&mut raw) };

    VmProbe {
        status,
        backend: raw.backend,
        error_domain: raw.error_domain,
        native_error: raw.native_error,
    }
}

/// 네이티브 상태 코드와 화면에 표시할 부팅 오류를 담는다.
#[derive(Debug)]
pub struct VmBootError {
    pub status: i32,
    pub message: String,
}

/// 표준 입출력을 게스트 콘솔로 연결하고 VM 종료될 때까지 기다린다.
/// 프로세스의 표준 입출력을 사용하므로 한 번에 하나의 세션만 실행된다.
pub fn run_linux(kernel: &Path, initramfs: &Path) -> Result<(), VmBootError> {
    let encode_path = |path: &Path| {
        CString::new(path.as_os_str().as_encoded_bytes()).map_err(|_| VmBootError {
            status: 4,
            message: format!("Path contiansa NUL byte: {}", path.display()),
        })
    };
    
    let kernel = encode_path(kernel)?;
    let initramfs = encode_path(initramfs)?;
    let mut error = RawBootError { message: [0; 512] };
    
    // SAFETY: 두 경로와 출력 버퍼는 호출이 끝날 때까지 유효하다.
    // 네이티브 함수는 반환 전에 이 포인터의 사용을 끝낸다.
    let status = unsafe { zeqvorn_vm_run_linux(kernel.as_ptr(), initramfs.as_ptr(), &mut error) };
    if status == 0 {
        return Ok(());
    }
    
    // SAFETY: C ABI는 출력 버퍼를 0으로 초기화하고 진단을 NUL로 끝낸다.
    let detail = unsafe { CStr::from_ptr(error.message.as_ptr()) }.to_string_lossy();
    let message = if detail.is_empty() {
        match status {
            1 => "Linux boot is unavailable on this host.",
            4 => "Both boot image paths are required.",
            _ => "The virtual machine stopped with an error.",
        }
            .to_owned()
    } else {
        detail.into_owned()
    };
    
    Err(VmBootError { status, message} )
}