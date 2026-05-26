# CIB binary logging over CH32V003 SWD

This repo has an opt-in CIB binary logging path:

```sh
cmake -S . -B build-log -G Ninja \
  -DCH32V003_CIB_BINARY_LOG=ON
cmake --build build-log
```

When enabled, CMake links `cib_log_binary`, scans the firmware application
library for CIB catalog symbols, and writes catalog collateral under:

```text
build-log/log/strings.cpp
build-log/log/strings.json
build-log/log/strings.xml
```

The generated `strings.cpp` is linked back into the firmware so the numeric log
IDs used on target match `strings.json`.

## Build integration notes

There are a few non-obvious pieces in the build:

- the firmware logic lives in a `firmware_app` static library so
  `gen_str_catalog` can scan its undefined `catalog<>` and `module<>` symbols
  before the final ELF link
- the root CMake file asks CMake to prefer the newest discovered CPython and
  finds `Python3::Interpreter` when logging is enabled, so CIB's generator runs
  with Python 3.10+ instead of macOS `/usr/bin/env python3`
- set `CIB_PYTHON_EXECUTABLE` when you want a specific interpreter; it is
  forwarded to CMake's `Python3_EXECUTABLE` before CIB is configured:

```sh
cmake -S . -B build-log -G Ninja \
  -DCH32V003_CIB_BINARY_LOG=ON \
  -DCIB_PYTHON_EXECUTABLE="$(command -v python3.14)"
```

- `firmware_log_catalog` links the CH32V003 compile flags too, because the
  generated `strings.cpp` object must use the same `rv32ec/ilp32e` ABI as the
  firmware
- `cib_log_config.h` undefines ch32fun's `INFO` peripheral macro before CIB log
  macros expand, otherwise `logging::level::INFO` is preprocessed into the
  hardware register macro
- `cib_log_config.h` also injects a CIB concurrency policy that saves `mstatus`,
  disables interrupts while the binary destination writes a packet, then
  restores `mstatus`

## Target sink

The sink is `firmware_log::swd_binary_sink`. It writes raw CIB binary packets to
the WCH debug mailbox registers used by ch32fun's `DEBUGPRINTF` path:

- `DMDATA0` carries the status byte and up to 3 payload bytes.
- `DMDATA1` carries the remaining 0 to 4 payload bytes.
- bit 7 of the low byte in `DMDATA0` means target output is pending.
- the low nibble stores `payload_size + 4`.
- the maximum payload per mailbox frame is 7 bytes.

This is not a hardware FIFO. It is a single pending target-to-host frame. The
firmware waits for the host to acknowledge the current frame before publishing
the next one, so the logging path is synchronous and can block if nothing is
draining the mailbox.

Practical consequence: a logging-enabled firmware may appear dead if it reaches
a log call while no raw terminal receiver is running. Start the receiver with
`minichlink -b -R | tools/cib-log-decode.py ...`; `-b` reboots/resumes the chip
and `-R` drains the mailbox. The default non-logging firmware does not have this
blocking behavior.

This frame layout is a ch32fun convention built on top of QingKe debug data
registers:

- `docs/pdf/QingKeV2_Processor_Manual.PDF`, Chapter 6, lists debug module
  register `data0` at `0x04` and `data1` at `0x05`; the same chapter describes
  the debug module as following the RISC-V External Debug 0.13.2 model.
- `docs/pdf/CH32V003RM.PDF` points core/debug details to the QingKeV2 manual
  and describes the CH32V003 reset default as SWD/SDI enabled.
- `vendor/ch32fun/ch32fun/ch32v003hw.h` maps `DMDATA0` to `0xe00000f4` and
  `DMDATA1` to `0xe00000f8`.
- `vendor/ch32fun/ch32fun/ch32fun.c` documents the status byte above `_write`,
  chunks writes to 7 bytes, stores bytes 4-7 in `DMDATA1`, then publishes the
  frame by writing `DMDATA0`. `SetupDebugPrintf()` initializes the mailbox
  sentinel.
- `vendor/ch32fun/minichlink/minichlink.c` `DefaultPollTerminal()` is the host
  side: read `DMDATA0`, read `DMDATA1` when the frame is longer than 3 bytes,
  copy out the payload, then acknowledge by writing `DMDATA0`.

The local sink mirrors that transport shape but writes CIB binary packets
instead of formatted text and waits indefinitely rather than using ch32fun's
printf timeout path.

The enabled sample app currently emits one no-argument log after boot:

```cpp
CIB_INFO("boot");
```

A no-argument CIB log is a 4-byte `Short32` packet, so it fits in one mailbox
frame and normally does not block at startup. Logs with runtime arguments use
larger catalog packets and may need multiple mailbox frames.

## Host receiver

The vendored `minichlink` has a local `-R` mode for raw terminal capture. It is
like `-T` for the target reset/resume part, but it does not print a terminal
banner, does not enter line editing mode, does not forward stdin, and writes
target bytes only to stdout. Tool status output goes to stderr.

Build the vendored tool when needed:

```sh
cmake --build build-log --target minichlink_tool
```

Then decode the byte stream with the matching catalog:

```sh
vendor/ch32fun/minichlink/minichlink -b -R \
  | tools/cib-log-decode.py --json build-log/log/strings.json --input -
```

If a `minichlink` on `PATH` is from upstream and does not know `-R`, use the
vendored executable above.

`tools/cib-log-decode.py` can also read a captured binary file:

```sh
tools/cib-log-decode.py \
  --json build-log/log/strings.json \
  --input capture.bin
```

The script carries
[PEP 723 inline metadata](https://peps.python.org/pep-0723/), so a runner such
as `uv` can execute it directly as a single-file script:

```sh
uv run tools/cib-log-decode.py \
  --json build-log/log/strings.json \
  --input capture.bin
```

Add `--host-time` if receiver-side timestamps are useful. The firmware does not
embed target timestamps.

## Packet format

CIB's binary encoder emits a small MIPI SyS-T-style subset:

- `Short32`, type nibble `1`: one 32-bit little-endian word. Bits `[3:0]` are
  the type and bits `[31:4]` are the string ID. This is used for logs with no
  runtime arguments.
- `Catalog32`, type nibble `3`: one 32-bit little-endian header, followed by a
  32-bit string ID, followed by packed runtime arguments. The header includes
  severity, unit, module ID, and subtype. Current CIB uses subtype `1`
  (`Id32_Pack32`) for these catalog messages.

The decoder uses CIB's Python `mipi_messages.py` parser from
`vendor/compile-time-init-build/tools` and the generated `strings.json` catalog.

## Timestamp choice

The current firmware sink does not add target timestamps. For synchronous SWD
logging, the receiver can timestamp packets as it decodes them. That avoids
spending target cycles and bytes on a timestamp field and keeps the packet
format exactly what CIB emits.
