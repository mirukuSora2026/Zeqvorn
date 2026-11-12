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
// File Name   : probe.rs
//
// Explanation : Prints the native backend and probe result.
//
// =============================================

use std::process::ExitCode;

fn main() -> ExitCode {
    let probe = zeqvorn_library::probe();

    println!("ABI: {}", zeqvorn_library::abi_version());
    println!("Backend: {}", probe.backend_name());
    println!("Status: {}", probe.status);

    if probe.available() {
        ExitCode::SUCCESS
    } else {
        eprintln!(
            "Native error domain: {}, code: {}",
            probe.error_domain, probe.native_error
        );
        ExitCode::FAILURE
    }
}
