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
// File Name   : boot_linux.rs
//
// Explanation : Runs a Linux kernel and initramfs
//               using the native VM backend.
//
// =============================================

use std::env;
use std::path::Path;
use std::process::ExitCode;

fn main() -> ExitCode {
    // args_os는 파일 경로를 UTF-8 문자열로 강제 변환하지 않는다.
    let mut args = env::args_os().skip(1);
    let (Some(kernel), Some(initramfs), None) = (args.next(), args.next(), args.next()) else {
        eprintln!("Usage: boot_linux <kernel> <initramfs>");
        return ExitCode::FAILURE;
    };

    match zeqvorn_library::run_linux(Path::new(&kernel), Path::new(&initramfs)) {
        Ok(()) => ExitCode::SUCCESS,
        Err(error) => {
            eprintln!("VM boot failed ({}): {}", error.status, error.message);
            ExitCode::FAILURE
        }
    }
}
