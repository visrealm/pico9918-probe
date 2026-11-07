#!/usr/bin/env python3
"""PICO9918 host driver and destructive bus characterization. Python 3.10+."""
import argparse
import json
import random
import statistics
import sys
import time
from pathlib import Path

TICK_NS = 5
# Conservative transfer timing, in ticks: 255 ns setup, 1 us pulse, 1 us hold. The
# nanoseconds are the point, so these track the tick rather than staying put.
SAFE = (51, 200, 200, 1)
# The sequencer's own program length, so the shortest event, and the field width of
# the duration that sits on top of it.
EVENT_FLOOR = 5
EVENT_CEILING = EVENT_FLOOR + 255


class ProbeError(RuntimeError):
    pass


class Probe:
    def __init__(self, serial):
        self.serial, self.sequence = serial, 0

    def command(self, *args, timeout=4):
        self.sequence += 1
        request = (f"{self.sequence} " + " ".join(map(str, args)) + "\n").encode("ascii")
        if len(request) >= 640:
            raise ValueError("request too long")
        self.serial.write(request)
        deadline, line = time.monotonic() + timeout, bytearray()
        while time.monotonic() < deadline:
            c = self.serial.read(1)
            if not c:
                continue
            line.extend(c)
            if len(line) > 1536:
                raise ProbeError("oversized reply")
            if c != b"\n":
                continue
            try:
                reply = json.loads(line)
            except (ValueError, UnicodeError) as exc:
                raise ProbeError("malformed reply; reconnect before retrying") from exc
            line.clear()
            if not isinstance(reply, dict):
                raise ProbeError("reply is not an object")
            if reply.get("id") != self.sequence:
                continue
            if reply.get("ok") is not True:
                raise ProbeError(reply.get("error", "command failed"))
            return reply
        # Never retry a write automatically: it may already have executed.
        raise ProbeError("reply timeout; operation may have executed")

    def timing(self, values=SAFE):
        self.command("TIMING", *values)

    def read(self, port, count):
        result = bytes.fromhex(self.command("READ", port, count)["data"])
        if len(result) != count:
            raise ProbeError("short read")
        return result

    def write(self, port, data):
        self.command("WRITE", port, bytes(data).hex())

    def address(self, address, write=False):
        self.write(1, [address & 255, ((address >> 8) & 63) | (64 if write else 0)])

    def register(self, reg, value):
        self.write(1, [value, 128 | reg])

    def vram_write(self, address, data):
        self.address(address, True)
        for start in range(0, len(data), 256):
            self.write(0, data[start:start+256])

    def vram_read(self, address, count):
        self.address(address)
        return b"".join(self.read(0, min(256, count-i)) for i in range(0, count, 256))

    def events(self, words, divider=1):
        if not 1 <= len(words) <= 64:
            raise ValueError("1..64 events required")
        result = bytes.fromhex(self.command("EVENTS", divider, "".join(f"{w:08x}" for w in words))["data"])
        if len(result) != len(words):
            raise ProbeError("short event result")
        return result


def event(duration, *, data=0, oe=False, csr=1, csw=1, port=0):
    """Output-to-next-output duration in PIO ticks; sample duration-1 after output.

    The sequencer updates pins on an event's first tick and samples on its last,
    so an event cannot be shorter than the five instructions that takes. Longer
    than EVENT_CEILING is several consecutive events with identical pins.

    These are PIO instruction times, not calibrated target-pad measurements.
    Data direction changes one instruction after output value changes.
    """
    if not EVENT_FLOOR <= duration <= EVENT_CEILING or not 0 <= data <= 255 or not 0 <= port <= 3:
        raise ValueError("event duration %d..%d, data 0..255, port 0..3 required"
                         % (EVENT_FLOOR, EVENT_CEILING))
    if not (csr or csw) or (not csr and oe):
        raise ValueError("bus contention")
    pins = data | (csr << 8) | (csw << 9) | ((port & 1) << 10) | ((not (port & 2)) << 11)
    return pins | ((255 if oe else 0) << 12) | ((duration-EVENT_FLOOR) << 20)


def write_wave(value, arrival, hold, pulse=125):
    """Poison -> valid at signed offset from /CSW assertion -> poison after hold.

    Negative arrival tests setup before assertion; positive tests late data.
    Hold is relative to /CSW deassertion. Every segment must be >=EVENT_FLOOR ticks.
    """
    poison = value ^ 255
    words = [event(32, data=poison, oe=True)]
    if arrival < 0:
        words += [event(-arrival, data=value, oe=True), event(pulse, data=value, oe=True, csw=0)]
    elif arrival == 0:
        words += [event(pulse, data=value, oe=True, csw=0)]
    else:
        words += [event(arrival, data=poison, oe=True, csw=0),
                  event(pulse-arrival, data=value, oe=True, csw=0)]
    if hold:
        words += [event(hold, data=value, oe=True)]
    words += [event(32, data=poison, oe=True), event(32)]
    return words


def read_wave(sample, pulse=125):
    """Read sample offset relative to /CSR assertion; pulse independent of sample.

    Late samples may intentionally observe a released/floating bus.
    """
    words = [event(32)]
    if sample <= pulse-1:
        duration = sample+1
        words.append(event(duration, csr=0))
        selected = 1
        remainder = pulse-duration
        if remainder:
            words.append(event(remainder, csr=0))
        words.append(event(32))
    else:
        words.append(event(pulse, csr=0))
        words.append(event(sample-pulse+1))
        selected = 2
    return words, selected


def compare(expected, actual):
    errors = sum(a != b for a, b in zip(expected, actual)) + abs(len(expected)-len(actual))
    first = next((i for i, (a, b) in enumerate(zip(expected, actual)) if a != b), None)
    if first is None and len(expected) != len(actual):
        first = min(len(expected), len(actual))
    return {"errors": errors, "first_error": first,
            "expected": expected[first] if first is not None and first < len(expected) else None,
            "actual": actual[first] if first is not None and first < len(actual) else None}


def pattern(length, seed):
    rng = random.Random(seed)
    return bytes(rng.randrange(256) for _ in range(length))


def functional(probe, model="9918", mode1=False):
    results = []
    def check(name, condition, **details):
        results.append({"test": name, "pass": bool(condition), **details})
    probe.timing()
    probe.command("RESET", 10)
    time.sleep(.1)
    probe.register(1, 0x80)  # 16K, display and IRQ disabled
    patterns = [bytes([v])*16384 for v in (0, 255, 85, 170)]
    patterns += [bytes((i ^ (i >> 8)) & 255 for i in range(16384)), pattern(16384, 9918)]
    for n, expected in enumerate(patterns):
        probe.vram_write(0, expected)
        detail = compare(expected, probe.vram_read(0, len(expected)))
        check(f"full_vram_pattern_{n}", detail["errors"] == 0, **detail)
    walking = bytes([1 << bit for bit in range(8)] + [255 ^ (1 << bit) for bit in range(8)])
    probe.vram_write(0x1234, walking)
    check("walking_data", probe.vram_read(0x1234, len(walking)) == walking)
    # Explicit isolation distinguishes address aliasing from a sequential stream.
    addresses = [0] + [1 << bit for bit in range(14)]
    for n, address in enumerate(addresses):
        probe.vram_write(address, [n+17])
    check("address_lines", all(probe.vram_read(a, 1) == bytes([n+17]) for n, a in enumerate(addresses)))
    expected = pattern(32, 14)
    probe.vram_write(0x3ff8, expected)
    check("wrap_and_prefetch", probe.vram_read(0x3ff8, 32) == expected)
    probe.address(0x3ff8)
    check("individual_read_ahead", b"".join(probe.read(0, 1) for _ in expected) == expected)
    if mode1:
        probe.address(0x3ff8)
        check("MODE1_legacy_data_alias", probe.read(2, 32) == expected)
        probe.write(3, [0xf8, 0x3f])
        check("MODE1_legacy_control_alias", probe.read(0, 32) == expected)
    else:
        results.append({"test": "MODE1", "skip": "use --mode1-alias for current firmware's legacy aliases"})
    probe.read(1, 1)
    probe.register(1, 0xa0)
    check("interrupt_assert", probe.command("WAIT", 0, 100)["matched"])
    status = probe.read(1, 1)[0]
    check("status_vblank", status & 128 != 0, status=status)
    # Disable further frame IRQs before checking clearing; USB can span a frame.
    probe.register(1, 0x80)
    probe.read(1, 1)
    check("interrupt_clear", probe.command("WAIT", 1, 20)["matched"])
    probe.register(1, 0xa0)
    probe.command("WAIT", 0, 100)
    probe.command("RESET", 10)
    time.sleep(.1)
    check("host_reset_clears_irq", probe.command("WAIT", 1, 20)["matched"])
    if model != "skip":
        clocks = probe.command("CLOCKS", 100)
        expected = {"9918": (3579545, 447443.125), "992x": (0, 447443.125),
                    "9118": (3579545, 0), "912x": (0, 3579545)}[model]
        for name, hz in zip(("cpu", "grom"), expected):
            actual = clocks[name+"_edges"] * 1e6 / clocks["elapsed_us"]
            check(name+"_clock", abs(actual-hz) <= max(100, hz*.02), hz=actual, expected_hz=hz)
    return results


def burst_trial(probe, timing, direction, repetitions, length, seed):
    errors, first, tested = 0, None, 0
    for repetition in range(repetitions):
        probe.timing(timing)
        result = probe.command("STRESS", int(direction == "read"), length, (seed+repetition)&0xffffffff)
        errors += result["errors"]
        if first is None and result["errors"]:
            first = {"repetition": repetition, **result}
        tested += length
    period_ns = sum(timing[:3])*timing[3]*TICK_NS
    return {"direction": direction, "timing_ticks": timing, "period_ns": period_ns,
            "burst_bytes_per_second": 1e9/period_ns, "tested_bytes": tested,
            "errors": errors, "first_failure": first, "pass": errors == 0}


def sweep(probe, args):
    rows = []
    try:
        for value in args.values:
            for direction in (["read", "write"] if args.direction == "both" else [args.direction]):
                t = [args.setup, args.pulse, args.hold, args.divider]
                if args.axis == "period":
                    t[1] = value-t[0]-t[2]
                else:
                    t[{"setup": 0, "pulse": 1, "gap": 2}[args.axis]] = value
                if not (5 <= t[0] <= 255 and 6 <= t[1] <= 255 and 6 <= t[2] <= 255):
                    raise ValueError(f"unrepresentable timing {t}")
                row = burst_trial(probe, t, direction, args.repetitions, args.bytes, args.seed)
                row.update(axis=args.axis, value=value)
                rows.append(row)
                print(json.dumps(row), flush=True)
    finally:
        probe.timing()
    fastest = {}
    for direction in ("read", "write"):
        passing = [r for r in rows if r["pass"] and r["direction"] == direction]
        fastest[direction] = max((r["burst_bytes_per_second"] for r in passing), default=None)
    return {"kind": "burst_sweep", "rows": rows, "fastest_observed_passing_bytes_per_second": fastest,
            "note": "Each trial is one uninterrupted DMA burst, up to 16K bytes. USB gaps between trials. Observed pass, not guaranteed limit."}


def edges(probe, args):
    rows = []
    try:
        probe.timing()
        for value in args.values:
            failures, tested, first = 0, 0, None
            for repetition in range(args.repetitions):
                for datum in pattern(16, args.seed+repetition):
                    probe.vram_write(0, [datum if args.axis == "read-sample" else datum ^ 255])
                    probe.address(0, args.axis != "read-sample")
                    if args.axis == "read-sample":
                        words, index = read_wave(value, args.pulse)
                        actual = probe.events(words, args.divider)[index]
                    else:
                        arrival = value if args.axis == "write-arrival" else args.arrival
                        hold = value if args.axis == "write-hold" else args.hold
                        probe.events(write_wave(datum, arrival, hold, args.pulse), args.divider)
                        actual = probe.vram_read(0, 1)[0]
                    tested += 1
                    if actual != datum:
                        failures += 1
                        if first is None:
                            first = {"expected": datum, "actual": actual, "repetition": repetition}
            row = {"axis": args.axis, "offset_ticks": value, "offset_ns": value*args.divider*TICK_NS,
                   "pulse_ticks": args.pulse, "divider": args.divider,
                   "tested_bytes": tested, "errors": failures, "first_failure": first, "pass": failures == 0}
            rows.append(row)
            print(json.dumps(row), flush=True)
    finally:
        probe.timing()
    return {"kind": "edge_sweep", "rows": rows,
            "note": "PIO instruction timing; GPIO pipeline/synchronizer delay and wiring propagation are not calibrated out."}


def powerup(probe, args):
    state = probe.command("STATE")
    if not state.get("armed") or not state.get("switched"):
        raise ProbeError("power-up test requires the fitted power switch and arm switch")
    rows = []
    for cycle in range(args.cycles):
        result = probe.command("BOOT", args.off_ms, args.timeout_ms, args.reset_ms, (args.seed+cycle)&0xffffffff,
                               timeout=(args.off_ms+args.timeout_ms)/1000+4)
        row = {"cycle": cycle+1, "pass": result["ready"], **result}
        row["after_reset_us"] = result["first_success_end_us"]-result["reset_released_us"] if result["ready"] else None
        rows.append(row)
        print(json.dumps(row), flush=True)
    times = [r["first_success_end_us"] for r in rows if r["pass"]]
    reset_times = [r["after_reset_us"] for r in rows if r["pass"]]
    return {"kind": "powerup", "off_ms": args.off_ms, "timeout_ms": args.timeout_ms, "reset_ms": args.reset_ms,
            "rows": rows, "ready_cycles": len(times), "failed_cycles": len(rows)-len(times),
            "first_verified_us": {"min": min(times), "median": statistics.median(times), "max": max(times)} if times else None,
            "after_reset_us": {"min": min(reset_times), "median": statistics.median(reset_times), "max": max(reset_times)} if reset_times else None,
            "note": "From POWER_EN command to verified host write/read; two consecutive distinct 8-byte patterns required. No voltage sensing."}


def capture(probe, args):
    probe.command("TRACE", args.samples, args.divider, args.trigger)
    try:
        if args.read:
            probe.read(args.bus_port, args.read)
        elif args.write:
            probe.write(args.bus_port, bytes.fromhex(args.write))
        deadline = time.monotonic()+3
        while True:
            try:
                result = probe.command("DUMP", 0, min(128, args.samples))
                break
            except ProbeError as exc:
                if "capture incomplete" not in str(exc) or time.monotonic() >= deadline:
                    raise
                time.sleep(.01)
        encoded = result["words"]
        for i in range(128, args.samples, 128):
            encoded += probe.command("DUMP", i, min(128, args.samples-i))["words"]
        words = [int(encoded[i:i+8], 16) for i in range(0, len(encoded), 8)]
        names = [f"D{i}" for i in range(8)] + ["CSR_n", "CSW_n", "MODE", "MODE1", "GP16", "GP17", "INT_n", "CPUCLK", "GROMCLK", "RESET_n"]
        lines = ["$timescale 1ns $end", "$scope module host $end"]
        lines += [f"$var wire 1 {chr(33+i)} {name} $end" for i, name in enumerate(names)]
        lines += ["$upscope $end", "$enddefinitions $end"]
        for sample, word in enumerate(words):
            lines.append(f"#{sample*16*args.divider}")
            lines += [f"{(word>>i)&1}{chr(33+i)}" for i in range(18)]
        Path(args.vcd).write_text("\n".join(lines)+"\n", encoding="ascii")
        return {"vcd": args.vcd, "samples": len(words), "sample_period_ns": 2*TICK_NS*args.divider}
    finally:
        probe.command("CANCEL")


def values(text):
    if ":" in text:
        fields = list(map(int, text.split(":")))
        if len(fields) != 3 or not fields[2]:
            raise argparse.ArgumentTypeError("use start:stop:step (stop inclusive)")
        start, stop, step = fields
        result = list(range(start, stop+(1 if step > 0 else -1), step))
    else:
        result = list(map(int, text.split(",")))
    if not result:
        raise argparse.ArgumentTypeError("empty sweep")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="explicit serial port, e.g. COM8 or /dev/ttyACM0")
    parser.add_argument("--output", help="JSON report file")
    sub = parser.add_subparsers(dest="action", required=True)
    sub.add_parser("ports")
    for name in ("info", "state", "off"):
        sub.add_parser(name)
    arm = sub.add_parser("arm")
    arm.add_argument("power", choices=["manual", "switch"])
    raw = sub.add_parser("raw")
    raw.add_argument("command", nargs="+")
    suite = sub.add_parser("test", help="destructive 16K VRAM, status, IRQ, reset and clock tests")
    suite.add_argument("--model", choices=["9918", "992x", "9118", "912x", "skip"], default="9918")
    suite.add_argument("--mode1-alias", action="store_true")
    boot = sub.add_parser("powerup", help="power-cycle and measure first verified host write/read")
    boot.add_argument("--cycles", type=int, default=10)
    boot.add_argument("--off-ms", type=int, default=1000)
    boot.add_argument("--timeout-ms", type=int, default=2000)
    boot.add_argument("--reset-ms", type=int, default=10)
    boot.add_argument("--seed", type=int, default=9918)
    for name in ("sweep", "edges"):
        p = sub.add_parser(name)
        p.add_argument("axis", choices=["period", "pulse", "setup", "gap"] if name == "sweep" else ["read-sample", "write-arrival", "write-hold"])
        p.add_argument("--values", type=values, required=True, help="comma list or start:stop:step, PIO ticks; stop inclusive")
        p.add_argument("--repetitions", type=int, default=4)
        p.add_argument("--seed", type=int, default=9918)
        p.add_argument("--divider", type=int, default=1)
        p.add_argument("--pulse", type=int, default=SAFE[1])
        p.add_argument("--hold", type=int, default=SAFE[0] if name == "edges" else SAFE[2])
        if name == "sweep":
            p.add_argument("--setup", type=int, default=SAFE[0])
            p.add_argument("--bytes", type=int, default=16384)
            p.add_argument("--direction", choices=["read", "write", "both"], default="both")
        else:
            p.add_argument("--arrival", type=int, default=-SAFE[0])
    cap = sub.add_parser("capture")
    cap.add_argument("--samples", type=int, default=1024)
    cap.add_argument("--divider", type=int, default=1)
    cap.add_argument("--trigger", type=int, choices=[12, 13, 18], default=12)
    cap.add_argument("--bus-port", type=int, choices=range(4), default=0)
    transfer = cap.add_mutually_exclusive_group()
    transfer.add_argument("--read", type=int)
    transfer.add_argument("--write")
    cap.add_argument("--vcd", required=True)
    args = parser.parse_args()
    if args.action == "powerup" and not (1 <= args.cycles <= 10000 and 100 <= args.off_ms <= 10000 and 0 <= args.reset_ms <= 1000 and args.reset_ms < args.timeout_ms <= 10000):
        parser.error("powerup requires cycles 1..10000, off-ms 100..10000, reset-ms 0..1000, reset-ms < timeout-ms <= 10000")
    if hasattr(args, "repetitions") and not 1 <= args.repetitions <= 10000:
        parser.error("repetitions must be 1..10000")
    if hasattr(args, "bytes") and not 1 <= args.bytes <= 16384:
        parser.error("bytes must be 1..16384")
    if args.action == "edges":
        # Reject unrepresentable points before touching target memory.
        for value in args.values:
            if args.axis == "read-sample":
                read_wave(value, args.pulse)
            else:
                write_wave(0x55, value if args.axis == "write-arrival" else args.arrival,
                           value if args.axis == "write-hold" else args.hold, args.pulse)
    import serial
    if args.action == "ports":
        from serial.tools.list_ports import comports
        for p in comports():
            print(p.device, p.description, p.serial_number)
        return 0
    if not args.port:
        parser.error("--port is required")
    with serial.Serial(args.port, 115200, timeout=.1, write_timeout=2) as connection:
        time.sleep(.1)
        connection.reset_input_buffer()
        probe = Probe(connection)
        info = probe.command("INFO")
        if info.get("board") != "pico2-pico9918" or info.get("protocol") != 2:
            raise ProbeError("unexpected probe firmware")
        if args.action in ("test", "sweep", "edges", "capture") and not probe.command("STATE")["armed"]:
            raise ProbeError("arm manual or arm switch first")
        if args.action == "info":
            result = dict(info)
        elif args.action in ("state", "off"):
            result = probe.command(args.action.upper())
        elif args.action == "arm":
            result = probe.command("ARM", args.power.upper())
        elif args.action == "raw":
            result = probe.command(*args.command)
        elif args.action == "test":
            result = {"kind": "functional", "rows": functional(probe, args.model, args.mode1_alias)}
        elif args.action == "sweep":
            result = sweep(probe, args)
        elif args.action == "edges":
            result = edges(probe, args)
        elif args.action == "powerup":
            result = powerup(probe, args)
        else:
            result = capture(probe, args)
        if args.action != "info":
            result["probe"] = info
        rendered = json.dumps(result, indent=2)
        if args.output:
            Path(args.output).write_text(rendered+"\n", encoding="utf-8")
        print(rendered)
        return 1 if any(r.get("pass") is False for r in result.get("rows", [])) else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (ProbeError, ValueError, OSError) as exc:
        print(f"Error: {exc}", file=sys.stderr)
        sys.exit(2)
