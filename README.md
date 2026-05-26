# Minimal CH32V003 + ch32fun + CIB Template

Small CMake firmware template for CH32V003 using:

- `vendor/ch32fun` as a git submodule
- `vendor/compile-time-init-build` as a git submodule
- xPack `riscv-none-elf-gcc`
- `probe-rs`, ch32fun `minichlink`, or WCH OpenOCD flashing

The example firmware blinks `PD6` through CIB/nexus flow services. Timing is
driven by a 1 ms SysTick interrupt and a 32-bit millisecond `Instant`, so the
main loop does not sit in a blocking delay.

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
636  0    8   644 284
```

## Flash

All flash targets depend on the firmware target, so a separate build step is
not required.

The board used for the local flash tests was connected through a WCH-LinkE.
`minichlink` reported it as:

```text
Found WCH Link
WCH Programmer is CH32V307 version 2.9
Detected CH32V003
Flash Storage: 16 kB
```

### probe-rs

```sh
cmake --build --preset default --target flash
```

`flash` is an alias for `flash_probe_rs`, which runs:

```sh
probe-rs download --chip CH32V003 --verify build/ch32v003_cib_blink.elf
```

Override probe-rs options if needed:

```sh
cmake -S . -B build -G Ninja \
  -DPROBE_RS_CHIP=CH32V003 \
  -DPROBE_RS_DOWNLOAD_ARGS="--verify --probe VID:PID"
```

Tested locally with `probe-rs 0.31.0`; `flash_probe_rs` completed with
verification in 0.94s.

### minichlink

```sh
cmake --build --preset default --target flash_minichlink
```

The target builds and uses the vendored ch32fun tool:

```sh
vendor/ch32fun/minichlink/minichlink \
  -w build/ch32v003_cib_blink.bin flash -b
```

This follows ch32fun's `cv_flash` rule: write the generated binary to the
`flash` memory section, then reboot out of halt. To use a preinstalled
`minichlink` instead:

```sh
cmake -S . -B build -G Ninja -DMINICHLINK=/usr/local/bin/minichlink
```

Tested locally with a WCH-LinkE reporting CH32V307 programmer firmware 2.9 and
a detected CH32V003 with 16 KiB flash; the image was written successfully.

### OpenOCD

OpenOCD flashing requires a WCH-enabled OpenOCD fork, not stock OpenOCD. The
missing pieces are both the `wlinke` adapter driver and the WCH target script.

Known usable fork: [`cjacker/wch-openocd`](https://github.com/cjacker/wch-openocd).
Build it with WCH-LinkE support, for example:

```sh
./bootstrap
./configure --enable-wlinke --disable-werror --program-prefix=wch-
make
make install
```

That installs a `wch-openocd` binary and WCH scripts such as
`target/wch-riscv.cfg`.

```sh
cmake --build --preset default --target flash_openocd
```

The target prefers `wch-openocd` when it is on `PATH`, otherwise it falls back
to `openocd`. The command shape is:

```sh
wch-openocd -f target/wch-riscv.cfg \
  -c init \
  -c halt \
  -c "program build/ch32v003_cib_blink.elf verify" \
  -c wlink_reset_resume \
  -c exit
```

Stock Homebrew `openocd 0.12.0` was tested here and is not sufficient: it does
not have the `wlinke` adapter driver, and no WCH or CH32 scripts were present
under `/opt/homebrew/share/openocd/scripts`. Copying only `wch-riscv.cfg` into
the project would still fail on that binary.

For a WCH OpenOCD install outside `PATH`, configure explicitly:

```sh
cmake -S . -B build -G Ninja \
  -DOPENOCD=/opt/wch-openocd/bin/wch-openocd \
  -DOPENOCD_CONFIG_FILES=target/wch-riscv.cfg
```

If your package installs a different script path, pass a semicolon-separated
config list and/or an OpenOCD search path:

```sh
cmake -S . -B build -G Ninja \
  -DOPENOCD_CONFIG_FILES="target/wch-riscv.cfg" \
  -DOPENOCD_EXTRA_ARGS="-s /opt/wch-openocd/share/openocd/scripts"
```

The OpenOCD command shape follows the WCH flow documented by
[`cjacker/opensource-toolchain-ch32v`](https://github.com/cjacker/opensource-toolchain-ch32v#with-openocd).

## Toolchain

The default compiler prefix is:

```text
/Users/crosstyan/External/opt/xpack-riscv-none-elf-gcc-15.2.0-1
```

Override it with `-DTOOLCHAIN_PREFIX=/path/to/xpack-riscv-none-elf-gcc`.
