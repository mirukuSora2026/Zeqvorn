# ZeqFile

ZeqFile is a C++20 tool that compresses files and directories into `.tar.gz` archives and bundles a YAML manifest and a payload archive into a `.zf` package.

## Build

Build from the ZeqFile directory. The tool requires CMake 3.22 or later, a C++20 compiler, and the libarchive development headers. On macOS, install libarchive with Homebrew.

```sh
brew install libarchive
cmake -S . -B build -DCMAKE_PREFIX_PATH="$(brew --prefix libarchive)"
cmake --build build
```

## Configure the manifest

Use [config.yml](config.yml) as a template. Keep `schema-version` at `1` for this format. Set `program.executable` to the absolute path inside the Linux guest and set `build.platform` to the binary's target platform. In a `copy` step, `from` is relative to the manifest directory and `to` is the guest path.

```yaml
schema-version: 1
program:
  executable: "/usr/bin/zeqvorn-test-program"
build:
  platform: "linux/arm64"
  steps:
    - type: copy
      from: "./dist/zeqvorn-test-program"
      to: "/usr/bin/zeqvorn-test-program"
      mode: "0755"
```

This excerpt shows the fields that identify the payload. Keep the rest of the template and adjust `args`, `exit-codes`, and `environment` for your program. ZeqFile packages the manifest as a file. Prepare the payload yourself according to the YAML before running `pack`.

## Compress and package

With the Linux ARM64 binary at `dist/zeqvorn-test-program`, stage it at the path declared by the manifest. The compression command also accepts a single file.

```sh
mkdir -p demo/payload/usr/bin
cp dist/zeqvorn-test-program demo/payload/usr/bin/
chmod 0755 demo/payload/usr/bin/zeqvorn-test-program
./build/ZeqFile demo/payload demo/payload.tar.gz
cp config.yml manifest.zf
./build/ZeqFile pack manifest.zf demo/payload.tar.gz demo/package.zf
tar -tzf demo/package.zf
```

The package is a gzip-compressed tar archive containing `manifest.zf` and `payload.tar.gz` with their original names and bytes. Choose a new output path when rerunning either command.
