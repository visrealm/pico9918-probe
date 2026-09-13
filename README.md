# PICO9918 debug probe

A CMSIS-DAP debug probe that is also a deterministic TMS9918A host bus driver.

This is a fork of the official [Raspberry Pi debugprobe](https://github.com/raspberrypi/debugprobe).
It keeps the CMSIS-DAP interface exactly as upstream left it, so OpenOCD, gdb and
picotool all work normally. What it adds is the other half of the problem: it
replaces the UART on the USB CDC interface with a command channel that drives the
target's **host bus** the way a real 1980s computer would.

The [PICO9918](https://github.com/visrealm/pico9918) is a drop-in replacement for
the TI TMS9918A video display processor, built on an RP2040 or RP2350. It plugs
into a TI-99/4A, ColecoVision, MSX, NABU, Tatung Einstein and others. Those hosts
talk to it over a 40-pin bus with two active-low strobes, and until now the only
way to test that bus was to plug the board into a real computer and look at the
screen.

## Why

SWD tells you what the firmware's memory looks like. It cannot tell you what the
host bus did to get it there. That gap is not theoretical - the first thing this
probe found was a genuine defect:

The target's PIO read program debounced the *falling* edge of `/CSR` but accepted
a single sample as release. A brief HIGH excursion while `/CSR` was still asserted
therefore re-armed the state machine and started a second read inside one
externally clean strobe. Eight intended reads advanced the target's address
pointer to 12; sixty-four sometimes reached 0x51. Slowing the bus eightfold did
not remove them, because the extra reads were happening *inside* the pulse.

That matches a long-standing intermittent field report from a Tatung Einstein user
running GFXII text rendering. It had never been reproducible on the bench before.

## What it does

- CMSIS-DAP v2 probe, unchanged from upstream - stable at 10 MHz SWD on flying wires
- drives and reads the PICO9918 host bus as a host, up to 16 KiB per transfer
- varies bus timing to find setup, pulse, hold, sampling and repetition limits
- measures CPUCLK, GROMCLK and `/INT`, and controls the host reset input
- optionally switches the target's 5 V supply and times power-up readiness
- captures bus activity and exports VCD for a waveform viewer
- runs automated functional, stress, edge and power-up suites from a PC

## Hardware

Everything needed to build one is in [`hardware/`](hardware/): a printable wiring
guide, the wire list as CSV, editable SVG sources, and a static verifier that
checks 20 bus, SWD and ground connections plus the optional power nets.

You need a **Raspberry Pi Pico 2** and a 40-pin ZIF socket with 15.24 mm row
spacing. It must be a Pico 2 - the wiring relies on the RP2350's 5 V-tolerant
digital GPIOs and there are no level shifters on the board. Do not use an RP2040
Pico.

See [`PICO9918.md`](PICO9918.md) for the full pinout and the command protocol.

## Build

```bash
git submodule update --init --recursive
./build-pico9918.ps1          # Windows; configures with PICO9918_HOST_BUS=ON
```

Output is `build-debugprobe-pico9918/debugprobe_pico9918.uf2`. Hold BOOTSEL,
replug, and copy it to the `RP2350` drive.

Building with `PICO9918_HOST_BUS=OFF` gives you a stock debugprobe with its
normal UART behaviour, so one checkout covers both uses.

## Quick start

```bash
pip install -r host/requirements.txt
python host/pico9918_probe.py ports
python host/pico9918_probe.py --port COM10 arm switch
python host/pico9918_probe.py --port COM10 --output functional.json test --model 9918
python host/pico9918_probe.py --port COM10 off
```

`arm manual` instead of `arm switch` if the target is powered through the VBUS
jumper rather than the transistor switch.

## Two things that will waste your afternoon

Both of these fail silently and look exactly like a wiring fault. They are not.

**SWD is gated on ARM.** Before the fixture is armed, `hb_dap_command` passes only
`DAP_Info`, `HostStatus`, `Disconnect`, `TransferConfigure`, `SWJ_Clock` and
`SWD_Configure`. Everything else gets a canned failure, and `DAP_Connect`
specifically reports a failed connect - so OpenOCD tells you the target is dead
when the probe simply is not armed yet. Arm over CDC first, then run OpenOCD.

**CDC is gated on DTR.** The command task drops everything unless
`tud_cdc_connected()`, which is false until the host asserts DTR. pyserial does
this on open, so `pico9918_probe.py` never sees it - but .NET's `SerialPort` and a
raw `CreateFile` do not, and the port just goes quiet.

## Staying current with upstream

`master` tracks [raspberrypi/debugprobe](https://github.com/raspberrypi/debugprobe)
unmodified. All PICO9918 work sits on `pico9918`, which is the default branch, so:

```bash
git fetch upstream
git merge upstream/master
```

The fork point is `3fff5b2` (Debugprobe v2.3.1 plus the USB reset interface).

## Licence

Upstream files keep their original licences and copyright. The PICO9918 additions
(`src/pico9918_*`, `include/board_pico9918_config.h`, `host/`, `hardware/`) are
MIT, consistent with the rest of the PICO9918 project.
