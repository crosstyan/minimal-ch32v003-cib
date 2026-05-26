# Minimal CH32V003 + ch32fun + CIB Template

Small CMake firmware template for CH32V003 using:

- `vendor/ch32fun` as a git submodule
- `vendor/compile-time-init-build` as a git submodule
- xPack `riscv-none-elf-gcc`
- `probe-rs` flashing

The example firmware blinks `PD6` through CIB/nexus flow services.

## C++ Constructors

This template defines `CPLUSPLUS=1` for `ch32fun.c`, which enables ch32fun's
startup call to `__libc_init_array()` before `main()`. That is the path needed
for normal C++ static object constructors and vtable-bearing global instances.

ch32fun also has `FUNCONF_SUPPORT_CONSTRUCTORS`; that calls constructor
functions from `SystemInit()` via `CallConstructors()`. It is useful for C
projects or deliberate manual constructor timing, but this template leaves it
off because `SystemInit()` is called from the CIB boot flow and enabling both
paths would run `.init_array` twice.

## Checkout

```sh
git submodule update --init --recursive
```

To move both template dependencies to their upstream default-branch HEADs:

```sh
git submodule update --remote --merge vendor/ch32fun vendor/compile-time-init-build
```

## Build

```sh
cmake --preset default
cmake --build --preset default
```

The default preset uses `RelWithDebInfo`: optimized firmware with debug info in
the ELF.

## clangd

VS Code is configured to run clangd with:

```text
--compile-commands-dir=${workspaceFolder}/build
--query-driver=**/*
```

`.clangd` keeps clangd on the RISC-V target, removes GCC-only warning flags that
clang cannot parse, and suppresses the embedded `extern "C" main` diagnostic.

The build emits:

- `build/ch32v003_cib_blink.elf`
- `build/ch32v003_cib_blink.bin`
- `build/ch32v003_cib_blink.hex`
- `build/ch32v003_cib_blink.map`
- `build/ch32v003_cib_blink.lst`

## Footprint Rules

CH32V003 has 16 KiB flash and 2 KiB SRAM. The post-build verifier fails if:

- flash payload exceeds 16,384 bytes
- RAM use exceeds 2,048 bytes
- heap allocation symbols are linked
- software floating-point helpers are linked
- newlib/libstdc++/libm/libnosys runtime archives are linked
- heavy C++ runtime symbols such as `std::basic_string`, `std::vector`, or
  `std::locale` are linked

ch32fun's own tiny `printf`/`putchar` path is allowed. The verifier is intended
to catch hosted C/C++ runtime pull-in, not ch32fun's local stubs.

Current example size:

```text
text data bss dec hex
468  0    4   472 1d8
```

## Flash

```sh
cmake --build --preset default --target flash
```

The `flash` target runs:

```sh
probe-rs download --chip CH32V003 --verify build/ch32v003_cib_blink.elf
```

Override probe-rs options if needed:

```sh
cmake -S . -B build -G Ninja \
  -DPROBE_RS_CHIP=CH32V003 \
  -DPROBE_RS_DOWNLOAD_ARGS="--verify --probe VID:PID"
```

## Toolchain

The default compiler prefix is:

```text
/Users/crosstyan/External/opt/xpack-riscv-none-elf-gcc-15.2.0-1
```

Override it with `-DTOOLCHAIN_PREFIX=/path/to/xpack-riscv-none-elf-gcc`.

On macOS, the toolchain file still invokes the xPack compiler driver from this
prefix, but wraps it with copied `cc1`/`cc1plus` frontends from the build tree.
This avoids frontend launch stalls when the xPack directory lives on an external
APFS volume.
