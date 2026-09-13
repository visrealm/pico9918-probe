import json
import sys
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import pico9918_probe as h


class Serial:
    def __init__(self, response):
        self.response = bytearray(response)
        self.written = b""
    def write(self, data):
        self.written += data
    def read(self, n):
        result = bytes(self.response[:n])
        del self.response[:n]
        return result


class PIOSim:
    """Small independent interpreter for the actual bus PIO source.

    No SDK or hardware: validates instruction timelines, direction and words.
    Does not model DMA bus arbitration or pad/synchronizer propagation.
    """
    def __init__(self, program):
        source = (Path(__file__).resolve().parents[2]/"src/pico9918_bus.pio").read_text()
        body = source.split(".program "+program+"\n")[1].split(".program")[0]
        self.code, self.labels = [], {}
        for line in body.splitlines():
            line = line.split(";")[0].strip()
            if not line or line.startswith("."):
                continue
            if line.endswith(":"):
                self.labels[line[:-1]] = len(self.code)
            else:
                self.code.append(line.replace(",", "").split())

    def run(self, words, samples):
        fifo = iter(words)
        pc = ticks = osr = x = isr = 0
        pins, oe = 0xb00, 0
        log, received = [], []
        while len(received) < samples:
            if ticks > 1000000:
                raise AssertionError("PIO did not finish")
            op, *a = self.code[pc]
            next_pc = (pc+1) % len(self.code)
            if op == "pull":
                osr = next(fifo)
            elif op == "out":
                n = int(a[1]); value = osr & ((1 << n)-1); osr >>= n
                if a[0] == "pins": pins = value
                elif a[0] == "pindirs": oe = value
                elif a[0] == "x": x = value
                else: raise AssertionError(a)
            elif op == "set":
                pins = (pins & ~0x300) | (int(a[1]) << 8)
            elif op == "jmp":
                if x: next_pc = self.labels[a[1]]
                x = (x-1) & 0xffffffff
            elif op == "in": isr = pins & 255
            elif op == "mov": isr = 0
            elif op == "push": received.append(isr)
            else: raise AssertionError(op)
            log.append((ticks, pins, oe, op))
            self.assert_safe(pins, oe)
            pc = next_pc
            ticks += 1
        return log, received

    @staticmethod
    def assert_safe(pins, oe):
        assert pins & 0x300, "both strobes asserted"
        assert pins & 0x100 or not (oe & 0xff), "probe driving target read"

    @staticmethod
    def event_fifo(words):
        fifo = []
        for word in words:
            pins = word & 0xfff
            dirs = ((word >> 12) & 0xff) | 0xf00
            fifo += [pins | (dirs << 12), word >> 20]
        return fifo


class Tests(unittest.TestCase):
    def test_fragmented_reply_and_stale_id(self):
        wire = Serial(b'{"id":99,"ok":true}\n{"id":1,"ok":true,"data":"12ab"}\n')
        self.assertEqual(h.Probe(wire).read(0, 2), b"\x12\xab")
        self.assertEqual(wire.written, b"1 READ 0 2\n")

    def test_invalid_responses(self):
        for reply in (b"garbage\n", b"[]\n", b'{"id":1,"ok":false,"error":"test"}\n', b"x"*1537):
            with self.assertRaises(h.ProbeError):
                h.Probe(Serial(reply)).command("INFO")
        with self.assertRaises(h.ProbeError):
            h.Probe(Serial(b'{"id":1,"ok":true,"data":"12"}\n')).read(0, 2)

    def test_no_automatic_write_retry(self):
        wire = Serial(b"")
        with patch.object(h.time, "monotonic", side_effect=[0, 0, 5]):
            with self.assertRaises(h.ProbeError): h.Probe(wire).write(0, [1])
        self.assertEqual(wire.written.count(b"WRITE"), 1)

    def test_pattern_detects_bit_and_address_faults(self):
        data = h.pattern(16384, 9918)
        self.assertEqual(h.compare(data, data)["errors"], 0)
        for corrupt in (bytes(v ^ 1 for v in data), bytes(data[i & ~256] for i in range(len(data))), data[1:]+data[:1]):
            self.assertGreater(h.compare(data, corrupt)["errors"], 100)

    def test_pio_burst_timings_all_ports(self):
        for read in (False, True):
            for port in range(4):
                for setup, pulse, hold in ((5, 6, 6), (32, 125, 125), (255, 255, 255)):
                    pins = 0x300 | ((port & 1) << 10) | ((not (port & 2)) << 11)
                    sample = min(32, pulse-3)
                    loops = ((setup-4) | ((sample-3) << 8) | ((pulse-sample-3) << 16) |
                             ((hold-6) << 24)) if read else \
                            (setup-5) | ((pulse-3) << 8) | ((hold-6) << 16)
                    words = []
                    for byte in (0x55, 0xaa):
                        words += [(pins << 12) | 0xf00 if read else pins | byte | (0xfff << 12), loops]
                    log, _ = PIOSim("hb_read" if read else "hb_write").run(words, 2)
                    active_bit = 0x100 if read else 0x200
                    falling = [t for (t,p,oe,op), prev in zip(log[1:], log) if not p & active_bit and prev[1] & active_bit]
                    rising = [t for (t,p,oe,op), prev in zip(log[1:], log) if p & active_bit and not prev[1] & active_bit]
                    updates = [t for t,p,oe,op in log if op == "out" and (t == 1 and not read or t == 2 and read)]
                    self.assertEqual(falling[0]-updates[0], setup)
                    self.assertEqual(rising[0]-falling[0], pulse)
                    self.assertEqual(falling[1]-falling[0], setup+pulse+hold)
                    if read:
                        sample_times = [t for t,p,oe,op in log if op == "in"]
                        self.assertEqual(sample_times[0]-falling[0], sample)

    def test_event_timing_and_sampling(self):
        words = [h.event(8), h.event(25, csr=0), h.event(12)]
        log, _ = PIOSim("hb_events").run(PIOSim.event_fifo(words), 3)
        updates = [t for t,p,oe,op in log if op == "out" and (t == 1 or p != log[t-1][1])]
        self.assertEqual(updates, [1, 9, 34])
        self.assertEqual([t for t,p,oe,op in log if op == "in"], [6, 31, 43])

    def test_write_arrival_hold_poison(self):
        for arrival in (-32, 0, 40):
            for hold in (0, 8, 32):
                words = h.write_wave(0xa5, arrival, hold)
                log, _ = PIOSim("hb_events").run(PIOSim.event_fifo(words), len(words))
                start = next(t for t,p,oe,op in log if not p & 0x200)
                end = next(t for t,p,oe,op in log if t > start and p & 0x200)
                valid = next(t for t,p,oe,op in log if p & 255 == 0xa5)
                poison = next(t for t,p,oe,op in log if t > valid and p & 255 == 0x5a)
                self.assertEqual(valid-start, arrival)
                self.assertEqual(end-start, 125)
                self.assertEqual(poison-end, hold)
                self.assertEqual(log[-1][2] & 0xff, 0)

    def test_read_sample_offsets(self):
        for sample in (5, 40, 114, 122, 130, 160):
            words, selected = h.read_wave(sample)
            log, _ = PIOSim("hb_events").run(PIOSim.event_fifo(words), len(words))
            start = next(t for t,p,oe,op in log if not p & 0x100)
            samples = [t for t,p,oe,op in log if op == "in"]
            self.assertEqual(samples[selected]-start, sample)

    def test_unrepresentable_events_rejected(self):
        for make in (lambda: h.event(7), lambda: h.event(4104), lambda: h.event(8, csr=0, oe=True),
                     lambda: h.write_wave(1, 120, 32), lambda: h.read_wave(121)):
            with self.assertRaises(ValueError): make()

    def test_burst_uses_single_firmware_stress_and_detects_failures(self):
        class Fake:
            def __init__(self): self.calls = []
            def timing(self, t=h.SAFE): pass
            def command(self, *args):
                self.calls.append(args)
                return {"errors": 1, "first_error": 7}
        fake = Fake()
        r = h.burst_trial(fake, (5, 6, 6, 1), "read", 3, 16384, 1)
        self.assertEqual(len(fake.calls), 3)
        self.assertTrue(all(c[:3] == ("STRESS", 1, 16384) for c in fake.calls))
        self.assertEqual(r["errors"], 3)
        self.assertFalse(r["pass"])
        self.assertEqual(r["period_ns"], 136)

    def test_values(self):
        self.assertEqual(h.values("5:1:-2"), [5, 3, 1])
        self.assertEqual(h.values("-32,0,32"), [-32, 0, 32])

    def test_powerup_statistics_and_reset_origin(self):
        class Fake:
            def __init__(self): self.cycle = 0
            def command(self, *args, **kwargs):
                if args[0] == "STATE": return {"armed": True, "switched": True}
                assert args[:4] == ("BOOT", 1000, 2000, 10)
                assert kwargs["timeout"] == 7
                self.cycle += 1
                return {"ready": self.cycle != 2, "first_success_end_us": 10000+self.cycle*100,
                        "reset_released_us": 10000}
        args = SimpleNamespace(cycles=3, off_ms=1000, timeout_ms=2000, reset_ms=10, seed=9918)
        with patch("builtins.print"):
            result = h.powerup(Fake(), args)
        self.assertEqual(result["ready_cycles"], 2)
        self.assertEqual(result["failed_cycles"], 1)
        self.assertEqual(result["first_verified_us"]["median"], 10200)
        self.assertEqual(result["after_reset_us"]["median"], 200)

    def test_powerup_refuses_manual_power(self):
        class Fake:
            def command(self, *args):
                assert args == ("STATE",)
                return {"armed": True, "switched": False}
        with self.assertRaises(h.ProbeError): h.powerup(Fake(), None)


if __name__ == "__main__":
    unittest.main()
