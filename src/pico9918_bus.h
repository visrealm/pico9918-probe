// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define HB_MAX_BYTES 256
#define HB_BURST_BYTES 16384
#define HB_TRACE_WORDS 1024
#define HB_MAX_EVENTS 64
typedef struct { uint16_t setup, pulse, hold, divider; } hb_timing_t;
typedef struct {
    uint32_t padout, padoe, fdebug, ctrl, addr, pinctrl, execctrl;
    uint8_t csr_func, csw_func;
} hb_diag_t;
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
bool hb_events(const uint32_t *events, uint8_t *samples, unsigned count, unsigned divider);
bool hb_stress(bool read, unsigned count, uint32_t seed, uint32_t *errors, uint32_t *first);
bool hb_powerup(unsigned off_ms, unsigned timeout_ms, unsigned reset_ms, uint32_t seed, hb_boot_result_t *result);
bool hb_power_cycle(unsigned off_ms, unsigned reset_ms, uint32_t *enabled_at_us, uint32_t *reset_released_us);
uint32_t hb_time_us(void);
bool hb_reset(unsigned ms);
uint32_t hb_pins(void);
void hb_diag(hb_diag_t *diag);
bool hb_hold(unsigned pin, bool high);
bool hb_wait_int(bool high, unsigned timeout_ms, uint32_t *elapsed_us);
bool hb_clocks(unsigned ms, uint32_t counts[2], uint32_t *elapsed_us);
bool hb_drive(unsigned ma, bool fast);
bool hb_trace_arm(unsigned count, unsigned divider, unsigned pin);
bool hb_trace_read(unsigned start, unsigned count, uint32_t *data);
void hb_trace_cancel(void);
uint16_t hb_dap_command(const uint8_t *request, uint8_t *response);
