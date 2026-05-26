# Minimal CH32V003 + ch32fun + CIB Template

Small CMake firmware template for CH32V003 using:

- `vendor/ch32fun` as a git submodule
- `vendor/compile-time-init-build` as a git submodule
- xPack `riscv-none-elf-gcc`
- [`probe-rs`](https://github.com/probe-rs/probe-rs),
  ch32fun [`minichlink`](https://github.com/cnlohr/ch32fun/tree/master/minichlink),
  [`wlink`](https://github.com/ch32-rs/wlink), or WCH OpenOCD flashing

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

The slightly odd part is `tools/clangd-riscv-none-elf-g++`.  clangd invokes
the configured compiler as a "query driver" to discover system include paths,
but it passes Clang-style target options such as `-target riscv32-none-elf`.
xPack `riscv-none-elf-g++` rejects those options even though clangd itself needs
the equivalent `--target=riscv32-none-elf` for parsing.  The shim handles only
that impedance mismatch:

- finds the real compiler from `CLANGD_RISCV_CXX`, then `build/CMakeCache.txt`,
  then `PATH`
- strips only `-target <value>`, `--target <value>`, and `--target=<value>` from
  the query-driver invocation
- preserves all other compile flags
- adds xPack's `libexec` directory to `DYLD_LIBRARY_PATH` and passes `-B...` so
  GCC can find its `cc1plus` frontend
- rewrites GCC's query-driver banner from `Target: riscv-none-elf` to
  `Target: riscv32-none-elf`, because clangd expects a width-qualified LLVM
  triple in that output

This is for editor analysis only.  The actual firmware build still uses the
compiler path from CMake directly.  The setup is intentionally C++-oriented and
is verified against `src/main.cpp`; vendored C files may still produce clangd
noise.

The build emits:

- `build/ch32v003_cib_blink.elf`
- `build/ch32v003_cib_blink.bin`
- `build/ch32v003_cib_blink.hex`
- `build/ch32v003_cib_blink.map`
- `build/ch32v003_cib_blink.lst`

## CIB Binary SWD Logging

CIB binary logging is opt-in:

```sh
cmake -S . -B build-log -G Ninja \
  -DCH32V003_CIB_BINARY_LOG=ON
cmake --build build-log
```

That links `cib_log_binary`, emits the string catalog under
`build-log/log/`, and sends binary packets through the CH32V003 debug mailbox.
The sink is synchronous and uses the same 7-byte DMDATA frame shape as
ch32fun's SWD `DEBUGPRINTF`; it is not a FIFO.

Because this sink is synchronous, the logging firmware can block when no host is
draining the SWD mailbox. If a logging-enabled image appears stuck, start the
receiver; the `-b -R` command below reboots the chip and drains the log stream.

If you want CMake to use a specific Python for CIB code generation, pass it
without hard-coding a path in the project:

```sh
cmake -S . -B build-log -G Ninja \
  -DCH32V003_CIB_BINARY_LOG=ON \
  -DCIB_PYTHON_EXECUTABLE="$(command -v python3.14)"
```

The vendored `minichlink` has a local raw terminal mode for this stream:

```sh
cmake --build build-log --target minichlink_tool
vendor/ch32fun/minichlink/minichlink -b -R \
  | tools/cib-log-decode.py --json build-log/log/strings.json --input -
```

`-R` keeps stdout as target bytes only. If the `minichlink` on `PATH` does not
support `-R`, use the vendored executable. The firmware does not embed target
timestamps; pass `--host-time` to `tools/cib-log-decode.py` for receiver-side
timestamps.

More detail is in [`docs/cib-binary-swd-logging.md`](docs/cib-binary-swd-logging.md).

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

Project: <https://github.com/probe-rs/probe-rs>

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

Quirk observed on this CH32V003/WCH-LinkE setup: `probe-rs download` can write
and verify the image, but `probe-rs reset --chip CH32V003` left the app
non-running until a WCH-specific reboot/resume command was sent. The current
working explanation is that the generic RISC-V reset/session teardown path
leaves this target's WCH debug block in a bad state. Use `minichlink -b`,
`flash_wlink`, or the OpenOCD `wlink_reset_resume` flow when the program needs
to start immediately after flashing. More detail is in
[`docs/ch32v003-reset-behavior.md`](docs/ch32v003-reset-behavior.md).

### minichlink

Project: <https://github.com/cnlohr/ch32fun/tree/master/minichlink>

```sh
cmake --build --preset default --target flash_minichlink
```

The target prefers a `minichlink` executable found on `PATH`. If none is found,
it builds and uses the vendored ch32fun `minichlink` source:

```sh
vendor/ch32fun/minichlink/minichlink \
  -w build/ch32v003_cib_blink.bin flash -b
```

This follows ch32fun's `cv_flash` rule: write the generated binary to the
`flash` memory section, then reboot out of halt. To choose a specific
`minichlink` executable:

```sh
cmake -S . -B build -G Ninja -DMINICHLINK=/usr/local/bin/minichlink
```

Tested locally with a WCH-LinkE reporting CH32V307 programmer firmware 2.9 and
a detected CH32V003 with 16 KiB flash; the image was written successfully.

### wlink

[`wlink`](https://github.com/ch32-rs/wlink) is the ch32-rs WCH-Link command
line tool. If it is installed on `PATH` or under `$HOME/.cargo/bin`, this target
flashes the generated binary and lets `wlink` reset/run the chip:

```sh
cmake --build --preset default --target flash_wlink
```

The target runs the same shape as:

```sh
/Users/crosstyan/.cargo/bin/wlink --chip CH32V003 \
  flash build/ch32v003_cib_blink.bin
```

Override the executable, chip, or extra flash options if needed:

```sh
cmake -S . -B build -G Ninja \
  -DWLINK=/Users/crosstyan/.cargo/bin/wlink \
  -DWLINK_CHIP=CH32V003 \
  -DWLINK_FLASH_ARGS="--speed medium"
```

Useful direct commands:

```sh
/Users/crosstyan/.cargo/bin/wlink list
/Users/crosstyan/.cargo/bin/wlink --chip CH32V003 reset run
```

Tested locally with `wlink 0.1.2` at `/Users/crosstyan/.cargo/bin/wlink`. It
found the WCH-LinkE in RV mode, connected to WCH-Link firmware 2.9, attached as
CH32V003, flashed 636 bytes to `0x08000000`, and issued its reset step. Its
verbose ESIG/read-protection output looked inconsistent on this probe firmware,
so the command keeps `--chip CH32V003` explicit.

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

The toolchain file does not hard-code an install path. It uses
`TOOLCHAIN_PREFIX` when provided; otherwise it searches `PATH` for
`riscv-none-elf-gcc` and the matching binutils.

Set the prefix in `CMakePresets.json`, `CMakeUserPresets.json`, or on the
configure command line:

```sh
cmake -S . -B build -G Ninja \
  -DTOOLCHAIN_PREFIX=/path/to/xpack-riscv-none-elf-gcc
```

The default preset in this checkout sets `TOOLCHAIN_PREFIX` through
`CMakePresets.json`.
