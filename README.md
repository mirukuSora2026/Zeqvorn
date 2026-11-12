# ZeqvornLibrary

ZeqvornLibrary provides shared C++ types and a native virtualization interface
for Zeqvorn. Rust callers use the same C ABI as native callers.

## Host support

| Host | Backend | Capability |
| --- | --- | --- |
| macOS | Virtualization.framework | Host probe and Linux initramfs boot. ARM64 boot verified on Apple silicon. |
| Linux | KVM | Host probe via `/dev/kvm`. |
| Windows | Windows Hypervisor Platform | Host capability probe. |

## Build requirements

- Rust and Cargo with Rust 2024 edition support
- CMake 3.22 or newer
- A C++20 compiler and the host platform SDK or development headers

Run the following commands from the repository root.

## Check host virtualization

```sh
cargo run --example probe
```

The probe prints the C ABI version, the selected backend, and a status code.
A status of `0` indicates that the backend is available.

## Boot an ARM64 Linux guest on macOS

Download `linux` and `initrd.gz` from the same
[Debian ARM64 installer build](https://ftp.debian.org/debian/dists/stable/main/installer-arm64/current/images/netboot/debian-installer/arm64/)
and save both files in `~/Downloads/zeqvorn-boot`.

Build and sign the executable with the
[macOS virtualization entitlement](https://developer.apple.com/documentation/virtualization/adding-the-virtualization-entitlement-to-your-project).

```sh
cargo build --example boot_linux &&
codesign --force --sign - \
  --entitlements examples/virtualization.entitlements \
  target/debug/examples/boot_linux &&
./target/debug/examples/boot_linux \
  "$HOME/Downloads/zeqvorn-boot/linux" \
  "$HOME/Downloads/zeqvorn-boot/initrd.gz"
```

The example boots with two vCPUs and 2 GiB of RAM. It connects the guest console
to the host terminal and opens a BusyBox shell from the initramfs.

At the guest prompt, verify the architecture and shut down the VM.

```sh
/usr/bin/uname -sm
/usr/bin/busybox poweroff -f
```

The expected architecture output is `Linux aarch64`. After guest shutdown, the
host executable returns exit code `0`.

## License

Apache-2.0. See [LICENSE](LICENSE).