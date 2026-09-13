import sys
import unittest
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import pico9918_probe as h


class VDP(h.Probe):
    """Reference host-port behavior, with switchable faults the suite must find."""
    def __init__(self, fault=None):
        self.ram = bytearray(16384)
        self.address_value, self.latch, self.irq = 0, None, False
        self.fault = fault
    def timing(self, values=h.SAFE): pass
    def write(self, port, data):
        for byte in data:
            if port & 1:
                if self.latch is None:
                    self.latch = byte
                else:
                    if byte & 128:
                        if byte & 7 == 1:
                            self.irq = bool(self.latch & 32)
                    else:
                        self.address_value = ((byte & 63) << 8) | self.latch
                    self.latch = None
            else:
                address = self.address_value
                if self.fault == "address": address &= ~256
                self.ram[address] = byte ^ (1 if self.fault == "data" else 0)
                self.address_value = (address+1) & 16383
    def read(self, port, count):
        result = bytearray()
        for _ in range(count):
            if port & 1:
                result.append(128 if self.irq else 0)
                self.irq = self.fault == "irq"
                self.latch = None
            else:
                address = self.address_value
                if self.fault == "address": address &= ~256
                result.append(self.ram[address])
                if self.fault != "prefetch": self.address_value = (address+1) & 16383
        return bytes(result)
    def command(self, *args):
        if args[0] == "RESET":
            self.irq = self.fault == "reset"
            self.latch = None
            return {}
        if args[0] == "WAIT": return {"matched": self.irq == (args[1] == 0)}
        if args[0] == "CLOCKS": return {"cpu_edges": 357955, "grom_edges": 44744, "elapsed_us": 100000}
        raise AssertionError(args)


class Tests(unittest.TestCase):
    @patch.object(h.time, "sleep")
    def test_reference_and_injected_faults(self, _sleep):
        rows = h.functional(VDP(), mode1=True)
        self.assertTrue(all(row.get("pass") for row in rows), rows)
        for fault in ("address", "data", "prefetch", "irq", "reset"):
            rows = h.functional(VDP(fault), mode1=True)
            self.assertTrue(any(row.get("pass") is False for row in rows), fault)


if __name__ == "__main__": unittest.main()
