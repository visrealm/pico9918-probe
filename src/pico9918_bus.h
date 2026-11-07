// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define HB_MAX_BYTES 256
#define HB_BURST_BYTES 16384
#define HB_TRACE_WORDS 1024
#define HB_MAX_EVENTS 64
// LOAD-BEARING: 200 MHz is chosen so the tick divides 10 ns, which is what makes
// the datasheet's 30, 100 and 200 ns exactly representable. Every other clock
// turns a conformance gate into either a vacuous test or one that rejects correct
// hardware. Do not raise it to the target's own 252 MHz: at equal nominal rates
// the phase dither degenerates to a single position and every measurement picks
// up a fixed bias while looking cleaner than before.
#define HB_SYS_CLOCK_KHZ 200000
#define HB_TICK_NS 5
typedef struct { uint16_t setup, pulse, hold, divider; } hb_timing_t;
typedef struct {
    uint32_t padout, padoe, fdebug, ctrl, addr, pinctrl, execctrl;
    uint8_t csr_func, csw_func;
} hb_diag_t;
/* raw 12-bit counts; rail is the target's 5 V through an external divider, vsys the
   Pico's own supply through the module's. The minimums are since ARM.
   TRAP: the die sensor's voltage falls as it warms, so temp_hot is the LOWEST count
   seen, not the highest. Bus contention lands on this die: the probe drives the socket
   with no buffer in the way, while the target's CD lines sit behind its transceiver. */
typedef struct { uint16_t rail, rail_min, vsys, vsys_min, temp, temp_hot; } hb_rails_t;
typedef struct {
    bool ready;
    uint32_t attempts, last_failed_us, first_success_start_us, first_success_end_us, confirmed_us, reset_released_us;
} hb_boot_result_t;
void hb_init(void);
void hb_lock(void);
void hb_unlock(void);
void hb_request_stop(void);
bool hb_stopping(void);
void hb_off(void);
bool hb_arm(bool switched);
bool hb_active(void);
bool hb_switched(void);
hb_timing_t hb_timing(void);
bool hb_set_timing(hb_timing_t timing);
bool hb_transfer(bool read, unsigned port, uint8_t *bytes, unsigned count);
bool hb_events(const uint32_t *events, uint8_t *samples, unsigned count, unsigned divider, unsigned repeat);
bool hb_pulse(uint32_t bus, unsigned rest, unsigned excursion, unsigned pre, unsigned width,
              unsigned post, unsigned divider, unsigned repeat, uint8_t *sample);
bool hb_stress(bool read, unsigned count, uint32_t seed, unsigned repeat,
               uint32_t *passes, uint32_t *errors, uint32_t *first);
bool hb_powerup(unsigned off_ms, unsigned timeout_ms, unsigned reset_ms, uint32_t seed, hb_boot_result_t *result);
bool hb_power_cycle(unsigned off_ms, unsigned reset_ms, uint32_t *enabled_at_us, uint32_t *reset_released_us);
uint32_t hb_time_us(void);
uint32_t hb_clock_hz(void);
uint32_t hb_swclk_hz(void);
bool hb_reset(unsigned ms);
uint32_t hb_pins(void);
void hb_sample_rails(void);
void hb_rails(hb_rails_t *out);
void hb_diag(hb_diag_t *diag);
bool hb_hold(unsigned pin, bool high);
bool hb_wait_int(bool high, unsigned timeout_ms, uint32_t *elapsed_us);
bool hb_clocks(unsigned ms, uint32_t counts[2], uint32_t *elapsed_us);
bool hb_drive(unsigned ma, bool fast);
bool hb_trace_arm(unsigned count, unsigned divider, unsigned pin);
bool hb_trace_read(unsigned start, unsigned count, uint32_t *data);
void hb_trace_cancel(void);
uint16_t hb_dap_command(const uint8_t *request, uint8_t *response);
