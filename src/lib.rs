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

#[repr(C)]
#[derive(Default)]
struct RawProbe {
    backend: u32,
    error_domain: u32,
    native_error: i64,
}

unsafe extern "C" {
    fn zeqvorn_vm_abi_version() -> u32;
    fn zeqvorn_vm_probe(result: *mut RawProbe) -> i32;
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
