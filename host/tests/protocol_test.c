// Tests the production parser with instrumented hardware boundary stubs.
#include "pico9918_bus.h"
#include "pico9918_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned calls;
static bool switched;
static hb_timing_t timing={32,125,125,1};
bool hb_active(void) { return true; }
bool hb_switched(void) { return switched; }
bool hb_stopping(void) { return false; }
uint32_t hb_pins(void) { return 0; }
void hb_off(void) { ++calls; }
bool hb_arm(bool sw) { (void)sw; ++calls; return true; }
hb_timing_t hb_timing(void) { return timing; }
bool hb_set_timing(hb_timing_t t) { timing=t; ++calls; return true; }
bool hb_drive(unsigned m,bool f) { (void)m;(void)f; ++calls; return true; }
bool hb_reset(unsigned ms) { (void)ms; ++calls; return true; }
void hb_diag(hb_diag_t *d) { memset(d,0,sizeof(*d)); }
void hb_rails(hb_rails_t *r) { memset(r,0,sizeof(*r)); }
bool hb_hold(unsigned pin, bool high) { (void)pin; (void)high; ++calls; return true; }
bool hb_transfer(bool read,unsigned port,uint8_t *data,unsigned count) {
    assert(port<4 && count && count<=256); ++calls;
    if(read) memset(data,0xa5,count);
    return true;
}
bool hb_stress(bool r,unsigned n,uint32_t s,unsigned repeat,uint32_t *passes,uint32_t *errors,uint32_t *first) {
    (void)r;(void)s; assert(n && n<=16384 && repeat); ++calls;
    *passes=repeat-1; *errors=3; *first=123; return true;
}
uint32_t hb_clock_hz(void) { return 200000000; }
uint32_t hb_swclk_hz(void) { return 10000000; }
bool hb_pulse(uint32_t bus,unsigned rest,unsigned excursion,unsigned pre,unsigned width,
              unsigned post,unsigned divider,unsigned repeat,uint8_t *sample) {
    (void)bus; assert(rest && rest<=3 && excursion && excursion<=3);
    assert(pre && width && post && divider && divider<=64 && repeat);
    ++calls; *sample=0x5a; return true;
}
bool hb_powerup(unsigned off,unsigned timeout,unsigned reset,uint32_t seed,hb_boot_result_t *r) {
    (void)seed; assert(off>=100 && off<=10000 && timeout>reset && timeout<=10000 && reset<=1000);
    ++calls; memset(r,0,sizeof(*r));r->ready=true;r->reset_released_us=10000;r->first_success_end_us=12345;return true;
}
bool hb_events(const uint32_t *e,uint8_t *s,unsigned n,unsigned d,unsigned repeat) {
    (void)e; assert(n && n<=64 && d && d<=64 && repeat); ++calls; memset(s,0x55,n); return true;
}
bool hb_clocks(unsigned ms,uint32_t c[2],uint32_t *e) { (void)ms; ++calls; c[0]=3;c[1]=4;*e=1000;return true; }
bool hb_wait_int(bool h,unsigned t,uint32_t *e) { (void)h;(void)t; ++calls;*e=1;return true; }
bool hb_trace_arm(unsigned n,unsigned d,unsigned p) { (void)n;(void)d;(void)p; ++calls;return true; }
void hb_trace_cancel(void) { ++calls; }
bool hb_trace_read(unsigned s,unsigned n,uint32_t *d) {
    (void)s;assert(n && n<=128); ++calls; memset(d,0,n*sizeof(*d));return true;
}
static char response[HB_REPLY_SIZE+16];
static void command(const char *text) {
    char line[HB_LINE_SIZE];
    assert(strlen(text)<sizeof(line)); strcpy(line,text);
    memset(response,0x5a,sizeof(response));
    size_t n=hb_command(line,response,HB_REPLY_SIZE);
    assert(n<HB_REPLY_SIZE && n==strlen(response));
    for(unsigned i=HB_REPLY_SIZE;i<sizeof(response);++i) assert(response[i]==0x5a);
}
int main(void) {
    const char *bad[]={"", "1", "0 OFF", "-1 OFF", "4294967296 OFF", "1 OFF extra", "1 NOPE",
      "1 READ 0 0", "1 READ 4 1", "1 READ 0 257", "1 READ -1 1", "1 READ 0 4294967296",
      "1 WRITE 0 0", "1 WRITE 0 zz", "1 WRITE 0 12xx", "1 WRITE 4 ab", "1 WRITE 0 ab extra",
      "1 TIMING 65536 1 1 1", "1 RESET 0", "1 RESET 1001", "1 WAIT 2 5", "1 WAIT 0 1001",
      "1 STRESS 2 16 1", "1 STRESS 0 16385 1", "1 STRESS 0 0 1", "1 EVENTS 0 00000000",
      "1 EVENTS 1 00000", "1 EVENTS 65 00000000", "1 EVENTS 1 xxxxxxxx", "1 DUMP 0 129",
      "1 CLOCKS 0", "1 CLOCKS 1001", "1 ARM banana", "1 OFF 1 2 3 4 5 6 7 8",
      "1 BOOT 99 2000 10 1", "1 BOOT 10001 2000 10 1", "1 BOOT 1000 10001 10 1",
      "1 BOOT 1000 2000 1001 1", "1 BOOT 1000 10 10 1", "1 BOOT 1000 2000 10 4294967296",
      "1 BOOT 1000 2000 10 1", // last valid syntax is refused with manual power
      "1 EVENTS 1 00000300 0", "1 STRESS 0 16 1 0",
      "1 PULSE 1 3 2 20 1 20 1 0000", "1 PULSE 1 3 2 20 1 20 1 zzzzzzzz",
      "1 PULSE 1 3 2 20 1 20 00000000", "1 PULSE x 3 2 20 1 20 1 00000000"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);++i) {
        calls=0; command(bad[i]); assert(!calls); assert(strstr(response,"\"ok\":false"));
    }
    command("4294967295 INFO"); assert(strstr(response,"4294967295"));
    assert(strstr(response,"\"protocol\":2") && strstr(response,"\"swclk_hz\":10000000"));
    // Repeat is optional on both, so protocol 1's own command lines still parse.
    command("5 EVENTS 1 00000300"); assert(strstr(response,"\"ok\":true"));
    command("6 EVENTS 1 00000300 4"); assert(strstr(response,"\"ok\":true"));
    command("7 STRESS 1 16 1 8"); assert(strstr(response,"\"passes\":7"));
    assert(strstr(response,"\"attempts\":8"));
    command("8 PULSE 1 3 2 20 1 20 100 000ff300"); assert(strstr(response,"\"data\":\"5a\""));
    command("1 READ 3 256"); assert(strstr(response,"\"ok\":true")); assert(strlen(response)>512);
    command("2 DUMP 0 128"); assert(strlen(response)>1024);
    command("3 STRESS 1 16384 4294967295"); assert(strstr(response,"\"errors\":3"));
    switched=true;command("4 BOOT 1000 2000 10 123");assert(strstr(response,"\"first_success_end_us\":12345"));switched=false;
    char text[600]; strcpy(text,"4 WRITE 0 "); memset(text+10,'a',512);text[522]=0;
    calls=0;command(text);assert(calls==1);
    text[522]='a';text[523]='a';text[524]=0;calls=0;command(text);assert(!calls);
    // Deterministic malformed-input corpus with a forbidden command name.
    uint32_t state=1;
    for(unsigned trial=0;trial<5000;++trial) {
        strcpy(text,"5 INVALID ");
        for(unsigned j=10;j<150;++j) { state=state*1664525u+1013904223u;text[j]=33+(state%94); }
        text[150]=0;calls=0;command(text);assert(!calls);
    }
    puts("Parser boundary tests and 5000 malformed commands passed");
}
