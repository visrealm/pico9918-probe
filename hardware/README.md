# Pico 2 prototype wiring

[Two-page printable wiring diagram](prototype-wiring.pdf) ·
[Bus/SWD drawing](wiring.svg) · [Optional power drawing](power-option.svg) ·
[Wire list](wiring.csv) · [Power connections](power-connections.csv)

This is a fresh prototype using a **Pico 2 (RP2350)** and one PICO9918 or
PICO9918 PRO in a 40-pin ZIF socket. It retains the previous bus/SWD GPIO
assignments, removes the probe bus translators, and omits voltage sensing.
The socket accepts the target's 15.24 mm pin rows. The diagram labels both
Pico physical pins and target DIP/socket pins; these are different numbers.

## Direct wiring and the target's U5

The Pico 2 digital pins used here tolerate 5 V **while its 3.3 V supply is up**;
the ADC-capable GP26-GP28 do not. All bus signals use GP4-GP21. Data pins must
be inputs during target reads. These limits come from the
[RP2350 datasheet, sections 14.8-14.9](https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf)
and [Pico 2 pinout](https://datasheets.raspberrypi.com/pico/pico-2-datasheet.pdf).
This direct wiring is **not for an RP2040 Pico**.

There is one unresolved fitted-part detail: both saved v1.3/v2.0 schematics
and BOMs list **74HC245 at U5**, whereas the conversation mentions HCT.
The target's U6 LVC245 at 3.3 V accepts the probe's control signals. For data
writes, **HCT245 accepts a 3.3 V HIGH; HC245 at roughly 5 V does not provide
the same guaranteed margin**. With HC fitted, this drawing is an experimental
3.3 V host, and data failures must be checked against input voltage before
being attributed to firmware. HCT's guaranteed HIGH threshold is 2 V; HC's
specified thresholds are 3.15 V at 4.5 V supply and 4.2 V at 6 V supply.
[Nexperia HC/HCT datasheet, section 9](https://assets.nexperia.com/documents/data-sheet/74HC_HCT245.pdf).
If direct writes prove marginal, retain the bus wiring concept but add a
proper data translator; the return/control lines need no probe translators.

The target header mapping was read from v1.3/v2.0 sheet 1 and checked against
v2.0 header placement coordinates. **D0 = TI CD7 = socket 17; D7 = TI CD0 =
socket 24. MODE1 = socket 11.** Older probe documents/code comments contain
conflicting data-pin assignments; use this wire list for this prototype.

## Manual power: page 1

One USB connection to the Pico 2 powers both boards. Connect **Pico 2 VBUS,
physical pin 40, through a removable jumper to ZIF pin 33 (H4.1)**, the
target's 5 V power input. Removing this jumper switches the target off for
a manual power cycle. Connect grounds as drawn and leave target USB
unplugged. Do not connect the boards' 3.3 V rails or the Pico VSYS pin.
No extra target 3.3 V wire is needed. Both manual and remote arrangements
use this same Pico 2 USB source; there is no separate target supply.
Allow for the combined USB current; probe firmware should request a
500 mA configuration before connecting target power.

Connect the target video FFC to the existing VGA adapter and monitor.
Connect GP2/GP3 to target DBG1 SWCLK/SWDIO, respectively, with ground on
DBG1 pin 2. The probe Pico 2's own native SWD pads debug the probe itself;
they are not used to drive the target.

## Optional remote power: page 2

The supplied stock list includes BC327-40 and 2N3904. These support the
two-transistor circuit shown, using **220 ohm, 1k, 10k and 100k resistors**.
Use 1/4 W resistors. Transistor terminals are labelled E/B/C; reference
TO-92 numbering is BC327 C-B-E and 2N3904 E-B-C, so their lead order differs.
Check the actual manufacturer's package drawing when identifying legs.

| Connection | Wiring |
| --- | --- |
| USB +5 V | Pico pin 40 to BC327 emitter; also to one end of 10k |
| Target power | BC327 collector to ZIF pin 33 |
| PNP base | BC327 base to other end of 10k and one end of 220 ohm |
| Base sink | Other end of 220 ohm to 2N3904 collector |
| Enable | GP22, physical pin 29, through 1k to 2N3904 base |
| Default off | 100k between 2N3904 base and emitter |
| Ground | 2N3904 emitter to common ground; keep ZIF pin 12 grounded |

**Replace the manual power jumper with this circuit; do not leave a
jumper bypassing the switch.** Target USB stays unplugged. Power comes from
the probe's VBUS through the BC327. GP22 HIGH turns power on; LOW or
undriven turns it off. Firmware should request a 500 mA USB configuration
before enabling it. GP22 replaces the earlier design's control-buffer
enable role; there are no buffers to enable in this version.

Calculated nominal base currents are about 18 mA for the BC327 and 2.5 mA
from the GPIO into the 2N3904. Start with a target load up to 150 mA and
measure loaded socket-pin-33 voltage (aim for at least 4.5 V). This is a
prototype design assumption, not a measured target consumption or a
guaranteed 5.0 V output. The PNP introduces a small load-dependent drop;
verify it with each board/configuration. The circuit has no current limit
or active discharge. Start with one second off for power cycling and check
that the target actually restarts. No relay/flyback diode is involved.

Reference transistor specifications:
[BC327](https://www.onsemi.com/download/data-sheet/pdf/bc327-d.pdf),
[2N3904](https://www.onsemi.com/pdf/datasheet/2n3903-d.pdf).
The IRF520N is omitted: its on-resistance is specified at 10 V gate drive,
so it is not a suitable direct 3.3 V replacement for this switch.
[IRF520N datasheet](https://www.infineon.com/assets/row/public/documents/24/49/infineon-irf520n-datasheet-en.pdf).

## Firmware and first use

The firmware for this wiring is the repository this folder sits in - build it
with [`build-pico9918.ps1`](../build-pico9918.ps1) and see
[`PICO9918.md`](../PICO9918.md) for driving it. The PICO9918's own `test/host`
firmware has a different pin assignment and must not be used unchanged.

1. With the target off, initialise the probe. Keep SWD released and disable
   data-pin pulls. Keep GP22 LOW if the power option is fitted.
2. Set /CSR, /CSW, MODE and MODE1 HIGH; initialise the data bus as inputs.
   Fit the manual power jumper or drive GP22 HIGH for the switch option, wait
   for startup, then use bus/SWD.

   WARNING: /RST is the exception and must not be driven HIGH. It reaches the
   target through a 5 V-powered 74AHC126, and a 3.3 V GPIO does not meet its
   input-high margin. Fit a 4.7-10 kohm pull-up from target 5 V (ZIF pin 33)
   to /RST (ZIF pin 34) and treat GP21 as true open-drain: output LOW to
   assert reset, input with pulls disabled to release it. The resistor then
   produces a valid 5 V HIGH without the probe ever driving 5 V, which is
   also what allows reset to be held low across a power-up.
3. For a read, release GP4-GP11 **before** /CSR goes LOW. At the end, raise
   /CSR and allow target output-disable time before enabling Pico outputs.
   Keep /CSW inactive. For a write, keep /CSR HIGH and set data/MODE before
   pulsing /CSW. MODE1 remains HIGH for ordinary legacy ports.
4. /RST is active LOW host reset; it is distinct from resetting the MCU
   through SWD. CPUCLK/GROMCLK are target outputs: never drive these from
   the probe. The target's SB1/SB2 links must be closed to observe clocks.
5. Before target power-off, stop PIO/SWD activity, release outputs and
   disable pulls. Remove the manual power jumper or drive GP22 LOW.
   Ignore undriven data/clock inputs while idle or target-off.

Keep prototype wires short and include the two ground connections drawn.
Begin with slow bus/SWD operation. GP0, GP1, GP16, GP17 and GP26-GP28 are
unused; GP22 is optional power enable. No sensing circuitry is required.

For later weak-drive experiments, Pico 2 offers drive-strength and slew
settings without extra hardware. They do not set a precise HIGH voltage.
Early RP2350 A2 silicon also has E9 input leakage, so do not interpret
floating or weakly pulled inputs as calibrated voltage measurements.
[RP2350 datasheet, section 9.6 and erratum E9](https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf).

## Verification

`verify_wiring.py` checks the wire list against independent Pico header
assignments and original target header placement, checks SWD and the
optional power connections, and opens both PDF pages with PDFium.
These are static wiring checks only - they read the wire list, not a circuit.
Nothing here simulates the design or runs the firmware; the bench results live
in [`../PICO9918.md`](../PICO9918.md).

Regenerate from the repository root:

```powershell
python hardware/make_wiring.py
python hardware/verify_wiring.py
```
