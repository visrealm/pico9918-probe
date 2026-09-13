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
