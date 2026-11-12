// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 miruku_sora2026

// =============================================
//
// Created At  : 11 / 10 / 2026
//
// Updated At  : 11 / 12 / 2026
//
// Created By  : miruku_sora2026
//
// File Name   : build.rs
//
// Explanation : Builds and links the native VM library for Cargo.
//
// =============================================

fn main() {
    for path in ["CMakeLists.txt", "include", "native"] {
        println!("cargo:rerun-if-changed={path}");
    }

    let install = cmake::Config::new(".").build();

    println!(
        "cargo:rustc-link-search=native={}",
        install.join("lib").display()
    );
    println!("cargo:rustc-link-lib=static=zeqvorn_vm");

    let target_os = std::env::var("CARGO_CFG_TARGET_OS").expect("target OS");

    match target_os.as_str() {
        "macos" => {
            println!("cargo:rustc-link-lib=framework=Foundation");
            println!("cargo:rustc-link-lib=framework=Virtualization");
            println!("cargo:rustc-link-lib=dylib=c++");
        }
        "linux" => println!("cargo:rustc-link-lib=dylib=stdc++"),
        "windows" => {}
        _ => panic!("Unsupported target OS: {target_os}"),
    }
}
