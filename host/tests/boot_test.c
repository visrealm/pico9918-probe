// Exercise the production readiness algorithm against a simulated powered bus.
#include "pico9918_bus.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint32_t now, enabled, ready_at;
static unsigned transfers, attempts, stop_at;
static bool manual, fail_forever, glitch, off;
static uint8_t ram[8];
static hb_timing_t timing;
uint32_t hb_time_us(void) { return now; }
hb_timing_t hb_timing(void) { return timing; }
bool hb_set_timing(hb_timing_t t) { timing=t;return true; }
void hb_off(void) { off=true; }
bool hb_power_cycle(unsigned off_ms,unsigned reset_ms,uint32_t *start,uint32_t *released) {
    assert(off_ms>=100);if(manual)return false;
    *start=enabled=now;*released=reset_ms*1000u;now+=*released;
    return true;
}
bool hb_transfer(bool read,unsigned port,uint8_t *data,unsigned count) {
    assert(timing.setup==32 && timing.pulse==125 && timing.hold==125 && timing.divider==1);
    now+=50; ++transfers;
    if(stop_at && transfers>=stop_at)return false;
    if(read && port==1) { ++attempts;assert(count==1);data[0]=0; }
    if(port==0) {
        assert(count==8);
        if(!read && now-enabled>=ready_at)memcpy(ram,data,8);
        if(read) {
            memcpy(data,ram,8);
            if(fail_forever || (glitch && attempts==2))data[0]^=255;
        }
    }
    return true;
}
static void init(void) {
    now=100;ready_at=0;transfers=attempts=stop_at=0;
    manual=fail_forever=glitch=off=false;memset(ram,0xff,sizeof(ram));
    timing=(hb_timing_t){5,4,6,2};
}
static void restored(void) { assert(timing.setup==5 && timing.pulse==4 && timing.hold==6 && timing.divider==2); }
int main(void) {
    hb_boot_result_t r;
    init();ready_at=600;
    assert(hb_powerup(1000,20,0,9918,&r));assert(r.ready && r.attempts==4);
    assert(r.last_failed_us==500 && r.first_success_start_us==500 && r.first_success_end_us==750 && r.confirmed_us==1000);restored();
    init();assert(hb_powerup(1000,20,10,9918,&r));assert(r.ready && r.reset_released_us==10000);
    assert(r.first_success_start_us==10000 && r.first_success_end_us==10250);restored();
    init();glitch=true;assert(hb_powerup(1000,20,0,9918,&r));assert(r.ready && r.attempts==4 && r.first_success_start_us==500);restored();
    init();fail_forever=true;assert(hb_powerup(1000,1,0,9918,&r));assert(!r.ready && r.attempts==4 && r.first_success_end_us==0);restored();
    init();stop_at=3;assert(!hb_powerup(1000,20,0,9918,&r));assert(off);restored();
    init();manual=true;assert(!hb_powerup(1000,20,0,9918,&r));assert(transfers==0);restored();
    init();now=UINT32_MAX-100;assert(hb_powerup(1000,20,0,9918,&r));assert(r.ready && r.first_success_end_us==250);restored();
    init();ready_at=950;assert(hb_powerup(1000,1,0,9918,&r));assert(!r.ready); // never enough confirmation before timeout
    init();assert(!hb_powerup(99,20,0,1,&r));assert(!hb_powerup(1000,10,10,1,&r));assert(transfers==0);
    puts("Power-up timing, reset hold, unstable readiness, timeout, abort and timer-wrap tests passed");
}
