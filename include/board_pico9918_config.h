// SPDX-License-Identifier: MIT
#pragma once
#define PROBE_IO_RAW
#define PROBE_SM 0
#define PROBE_PIN_OFFSET 2
#define PROBE_PIN_SWCLK 2
#define PROBE_PIN_SWDIO 3
// No nRESET wire to the target MCU. GP21 is a separate host-bus reset.
#define PROBE_USB_CONNECTED_LED 25
#define PROBE_PRODUCT_STRING "PICO9918 Probe (CMSIS-DAP)"
#define HB_DATA_BASE 4
#define HB_CSR 12
#define HB_CSW 13
#define HB_MODE 14
#define HB_MODE1 15
#define HB_INT 18
#define HB_CPUCLK 19
#define HB_GROMCLK 20
#define HB_RESET 21
#define HB_POWER 22

// Rail monitoring. HB_VSENSE reads the target's 5 V through an external 2:1 divider
// fitted to socket pin 33; HB_VSYS is the Pico's own VSYS/3, already divided on the
// module. One is downstream of the power switch and the other upstream, so the pair
// says whether a sag is the USB supply or the switch and its contacts.
#define HB_VSENSE 26
#define HB_VSENSE_ADC 0
#define HB_VSYS 29
#define HB_VSYS_ADC 3
#define HB_TEMP_ADC 4
#define HB_SMPS_PS 23

// Worst case for the power switch to bring the target's rail up. Driving a bus pin
// before it elapses puts 3.3 V into an unpowered target's inputs, which forward biases
// their protection diodes. A FET needs none of this; a mechanical relay needs tens of ms.
#define HB_POWER_SETTLE_MS 50
