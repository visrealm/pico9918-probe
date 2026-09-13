// SPDX-License-Identifier: MIT
// Host-bus readiness test, separated from GPIO code for fault-injection tests.
#include "pico9918_bus.h"
#include <string.h>

bool hb_powerup(unsigned off_ms, unsigned timeout_ms, unsigned reset_ms, uint32_t seed, hb_boot_result_t *result) {
    if (off_ms<100 || off_ms>10000 || timeout_ms<=reset_ms || timeout_ms>10000 || reset_ms>1000) return false;
    memset(result,0,sizeof(*result));
    uint32_t start;
    if (!hb_power_cycle(off_ms,reset_ms,&start,&result->reset_released_us)) return false;
    hb_timing_t previous=hb_timing();
    hb_set_timing((hb_timing_t){32,125,125,1});
    bool ok=true;
    unsigned consecutive=0;
    if (!seed) seed=1;
    while (hb_time_us()-start<timeout_ms*1000u) {
        uint8_t expected[8], actual[8], status;
        uint8_t write_address[2]={0,64}, read_address[2]={0,0};
        for (unsigned i=0;i<sizeof(expected);++i) {
            seed^=seed<<13; seed^=seed>>17; seed^=seed<<5;
            expected[i]=(uint8_t)seed;
        }
        uint32_t attempt_start=hb_time_us()-start;
        // Clear any partial control latch from an attempt that straddled boot.
        // Different data on every attempt avoids accepting stale VRAM contents.
        if (!hb_transfer(true,1,&status,1) ||
            !hb_transfer(false,1,write_address,2) ||
            !hb_transfer(false,0,expected,sizeof(expected)) ||
            !hb_transfer(false,1,read_address,2) ||
            !hb_transfer(true,0,actual,sizeof(actual))) {
            hb_off(); ok=false; break;
        }
        uint32_t attempt_end=hb_time_us()-start;
        ++result->attempts;
        if (memcmp(expected,actual,sizeof(expected))) {
            result->last_failed_us=attempt_end;
            result->first_success_start_us=result->first_success_end_us=0;
            consecutive=0;
        } else {
            if (!consecutive) {
                result->first_success_start_us=attempt_start;
                result->first_success_end_us=attempt_end;
            }
            if (++consecutive==2 && attempt_end<=timeout_ms*1000u) {
                result->ready=true;
                result->confirmed_us=attempt_end;
                break;
            }
        }
    }
    hb_set_timing(previous);
    return ok; // ready=false is a measured timeout, not a protocol failure
}
