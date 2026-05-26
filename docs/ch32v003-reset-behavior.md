# CH32V003 reset behavior with probe-rs and minichlink

Project references:

- probe-rs: <https://github.com/probe-rs/probe-rs>
- minichlink: <https://github.com/cnlohr/ch32fun/tree/master/minichlink>

This note records a local investigation into why:

```sh
probe-rs reset --chip CH32V003
```

can leave the test CH32V003 application not blinking, while:

```sh
vendor/ch32fun/minichlink/minichlink -b
```

starts it again.

## Hardware and tools observed

- Target: CH32V003 running the local `PD6` blink firmware.
- Probe: WCH-LinkE reported by minichlink as `CH32V307 version 2.9`.
- `probe-rs --version`: `probe-rs 0.31.0 (git commit: 3de1cae)`.
- probe-rs source inspected from a local checkout of
  <https://github.com/probe-rs/probe-rs>.
- minichlink source inspected from the vendored copy of
  <https://github.com/cnlohr/ch32fun/tree/master/minichlink>.

The local firmware image looked normal during inspection:

- ELF entry point is `0x0`.
- `.init` is at `0x0`.
- `main` is at `0xa0`.
- The startup path sets `mtvec`, clears `.bss`, calls constructors, and enters `main`.
- A halted snapshot after a working boot landed in the blink loop with SysTick interrupt state, so the firmware itself was not the likely cause.

## Observed behavior

The clean reproduction was:

1. Run `probe-rs reset --chip CH32V003`.
2. The command exits successfully with no terminal output.
3. The LED stops blinking.
4. Run `minichlink -b`.
5. The LED starts blinking again.

A narrower resume-only test was also run:

```sh
probe-rs reset --chip CH32V003
minichlink -ke
```

`-ke` skips normal minichlink target setup and sends the resume-style path. It
did not recover the blinking. That suggests the chip is not merely left in a
clean halted state that accepts a plain resume request.

Avoid using `minichlink -m` as a non-invasive state check. Its command handler
prints the register, then calls chip detection, and the detection path changes
debug state. It can halt or otherwise perturb the target.

## What probe-rs does

The `probe-rs reset` CLI attaches, gets the selected core, and calls
`core.reset()`:

- `probe-rs/probe-rs-tools/src/bin/probe-rs/cmd/reset.rs`

For RISC-V, `reset()` is implemented as:

```rust
self.reset_and_halt(Duration::from_secs(1))?;
self.resume_core()?;
```

Relevant probe-rs source:

- `probe-rs/probe-rs/src/architecture/riscv/mod.rs`

The generic RISC-V reset sequence uses `reset_hart_and_halt()`. On this target,
the trace showed:

1. probe-rs tries `hartreset` with `DMCONTROL = 0xa0000001`.
2. Readback shows `hartreset` is not supported.
3. It falls back to `ndmreset` with `DMCONTROL = 0x80000003`.
4. It waits until `DMSTATUS` reports `allhavereset` and `allhalted`.
5. It clears reset/halt request state.
6. It sends `resumereq` with `DMCONTROL = 0x40000001`.

The important detail is what happens after `core.reset()` returns. When the CLI
session exits, `Session::drop` runs:

- `clear_all_hw_breakpoints()`
- `debug_core_stop()` for each core

Relevant probe-rs source:

- `probe-rs/probe-rs/src/session.rs`
- `probe-rs/probe-rs/src/architecture/riscv/communication_interface.rs`

That cleanup can halt and resume the core again, perform abstract register
accesses to update debug state, and finally write:

```text
DMCONTROL = 0x00000000
```

to deactivate the debug module.

The local trace at `/tmp/probe-rs-ch32-reset.log` showed exactly this shape:
the reset span resumed the core, but the later `session_drop` span did more
halt/resume/debug cleanup and ended with `dmcontrol.dmactive = false`.

## What minichlink -b does

In minichlink, `-b` means "Reboot out of Halt". It calls:

```c
MCF.HaltMode(dev, HALT_MODE_REBOOT)
```

Relevant minichlink source:

- `vendor/ch32fun/minichlink/minichlink.c`

The default WCH reboot path writes:

```text
DMCONTROL = 0x80000001  // haltreq + dmactive
DMCONTROL = 0x80000001  // repeated halt request
DMCONTROL = 0x80000003  // haltreq + ndmreset + dmactive
DMCONTROL = 0x40000001  // resumereq + dmactive
```

It does not follow with the same generic probe-rs session teardown or final
`dmactive = 0` write.

This makes `minichlink -b` less like a generic reset command and more like a
WCH-specific "kick the debug/reset path back into user execution" command.

## Working explanation

The most likely explanation is:

- `probe-rs reset` performs a reasonable generic RISC-V reset-and-resume.
- On CH32V003 through WCH-LinkE, the debug transport and target debug block have
  WCH-specific behavior that generic RISC-V reset teardown does not model.
- The probe-rs session cleanup after reset appears to leave the target in a bad
  debug/reset state, or at least a state where the user program does not keep
  running.
- A plain resume request is not enough to recover from that state.
- `minichlink -b` recovers because it sends WCH's stronger reboot-out-of-halt
  sequence and does not immediately tear down the debug module afterward.

This is not evidence that the firmware image or reset vector is wrong. It is
much more consistent with a tool/probe/target-debug-state interaction.

## Related external clues

These references line up with the local observations:

- WCH/OpenOCD flows use `wlink_reset_resume` after programming rather than a
  generic reset:
  <https://github.com/cjacker/opensource-toolchain-ch32v#with-openocd>
- WCH's command-line note for RISC-V downloads shows:
  `openocd.exe -f wch-riscv.cfg -c init -c halt -c wlink_reset_resume -c exit`
  as the soft-reset/run check:
  <https://www.cnblogs.com/wchmcu/p/18595535>
- ch32-rs `wlink` documents CH32V003 as using 1-wire debug on `PD1` and lists
  reset/resume support. Its sample log also includes `resume fails`, which is
  a useful hint that this path is quirky even in WCH-specific tooling:
  <https://docs.rs/crate/wlink/latest/source/docs/CH32V003.md>
- The CH32V003 reference manual distinguishes power reset from system reset.
  Debug `ndmreset`, software reset, NRST, and power cycling should not be
  assumed equivalent on this part:
  <https://www.manualslib.com/manual/2933885/Wch-Ch32v003-Series.html?page=12>
- Albert Skog's CH32V003 Rust note mentions a different case where enabling
  SDI print made upload succeed but the LED did not blink. That is not the same
  issue, but it is another example of CH32V003 SDI/debug side effects looking
  like "the program did not start":
  <https://albertskog.se/ch32v-in-rust/>
- The rusty-probe-firmware issue is probably a different layer: that report is
  about a probe firmware lacking RISC-V interface support, not about CH32V003
  reset semantics:
  <https://github.com/probe-rs/rusty-probe-firmware/issues/28>

## Practical notes

For this repo, the reliable run-after-flash path is still:

```sh
minichlink \
  -w build/ch32v003_cib_blink.bin flash -b
```

or a WCH OpenOCD flow that ends with:

```sh
wlink_reset_resume
```

If using probe-rs for download, a pragmatic workaround is to follow it with:

```sh
minichlink -b
```

A probe-rs-side fix would likely need a CH32V003/WCH-Link-specific reset/run
sequence or a way for this reset command to avoid the generic post-reset debug
module teardown that leaves this target non-running.
