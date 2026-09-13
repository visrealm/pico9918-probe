// SPDX-License-Identifier: MIT
#include "pico9918_protocol.h"
#include "pico9918_bus.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>
#ifndef PROBE_VERSION
#include "probe/version.h"
#endif

static bool number(const char *s, uint32_t *n) {
    if (!s || !*s) return false;
    for (const char *p=s; *p; ++p) if (*p<'0' || *p>'9') return false;
    errno=0; char *end;
    unsigned long v=strtoul(s,&end,10);
    if (errno || *end || v>UINT32_MAX) return false;
    *n=(uint32_t)v;
    return true;
}
static int hex(char c) {
    if (c>='0' && c<='9') return c-'0';
    if (c>='a' && c<='f') return c-'a'+10;
    if (c>='A' && c<='F') return c-'A'+10;
    return -1;
}
size_t hb_command(char *line, char *reply, size_t cap) {
    char *a[8]; unsigned argc=0;
    char *s=line;
    while (*s) {
        while (*s==' ' || *s=='\t' || *s=='\r') ++s;
        if (!*s) break;
        if (argc==8) break;
        a[argc++]=s;
        while (*s && *s!=' ' && *s!='\t' && *s!='\r') ++s;
        if (*s) *s++=0;
    }
    uint32_t id=0, n[4]={0};
    const char *error="invalid command or arguments";
    if (argc<2 || argc==8 || !number(a[0],&id) || !id) goto fail;
    const char *cmd=a[1];
#define IS(c,k) (argc==(k)+2 && strcmp(cmd,(c))==0)
#define NUM(i) number(a[(i)+2],&n[i])
#define OK(...) do { snprintf(reply,cap,"{\"id\":%" PRIu32 ",\"ok\":true" __VA_ARGS__,id); goto done; } while(0)
    if (IS("INFO",0)) {
        snprintf(reply,cap,"{\"id\":%" PRIu32 ",\"ok\":true,\"protocol\":1,\"firmware\":\"%s\",\"board\":\"pico2-pico9918\",\"clock_hz\":125000000,\"max_bytes\":256,\"trace_words\":1024}",id,PROBE_VERSION);
        goto done;
    }
    if (IS("STATE",0)) {
        snprintf(reply,cap,"{\"id\":%" PRIu32 ",\"ok\":true,\"armed\":%s,\"switched\":%s,\"pins\":%" PRIu32 "}",id,hb_active()?"true":"false",hb_switched()?"true":"false",hb_pins());
        goto done;
    }
    if (IS("DIAG",0)) {
        hb_diag_t d; hb_diag(&d);
        snprintf(reply,cap,"{\"id\":%" PRIu32 ",\"ok\":true,\"pins\":%" PRIu32
            ",\"padout\":%" PRIu32 ",\"padoe\":%" PRIu32 ",\"fdebug\":%" PRIu32
            ",\"ctrl\":%" PRIu32 ",\"addr\":%" PRIu32 ",\"pinctrl\":%" PRIu32
            ",\"execctrl\":%" PRIu32 ",\"csr_func\":%u,\"csw_func\":%u}",
            id,hb_pins(),d.padout,d.padoe,d.fdebug,d.ctrl,d.addr,d.pinctrl,d.execctrl,d.csr_func,d.csw_func);
        goto done;
    }
    if (IS("HOLD",2) && NUM(0) && NUM(1) && n[1]<=1 && hb_hold(n[0],n[1])) OK("}");
    if (IS("OFF",0)) { hb_off(); OK("}"); }
    if (IS("BOOT",4) && NUM(0) && NUM(1) && NUM(2) && NUM(3) && n[0]>=100 && n[0]<=10000 && n[1]>n[2] && n[1]<=10000 && n[2]<=1000) {
        if (!hb_active() || !hb_switched()) { error="BOOT requires ARM SWITCH and the fitted power switch"; goto fail; }
        hb_boot_result_t r;
        if (!hb_powerup(n[0],n[1],n[2],n[3],&r)) goto hardware;
        snprintf(reply,cap,"{\"id\":%" PRIu32 ",\"ok\":true,\"ready\":%s,\"attempts\":%" PRIu32
            ",\"last_failed_us\":%" PRIu32 ",\"first_success_start_us\":%" PRIu32
            ",\"first_success_end_us\":%" PRIu32 ",\"confirmed_us\":%" PRIu32 ",\"reset_released_us\":%" PRIu32 "}",
            id,r.ready?"true":"false",r.attempts,r.last_failed_us,r.first_success_start_us,r.first_success_end_us,r.confirmed_us,r.reset_released_us);
        goto done;
    }
    if (IS("STRESS",3) && NUM(0) && NUM(1) && NUM(2) && n[0]<=1 && n[1]>0 && n[1]<=HB_BURST_BYTES) {
        uint32_t errors,first;
        if (!hb_stress(n[0],n[1],n[2],&errors,&first)) goto hardware;
        snprintf(reply,cap,"{\"id\":%" PRIu32 ",\"ok\":true,\"errors\":%" PRIu32 ",\"first_error\":%" PRIu32 "}",id,errors,first);
        goto done;
    }
    if (IS("ARM",1) && (!strcmp(a[2],"MANUAL") || !strcmp(a[2],"SWITCH"))) {
        if (!hb_arm(!strcmp(a[2],"SWITCH"))) { error="already armed or USB session stopped; use OFF first"; goto fail; }
        OK("}");
    }
    if (IS("TIMING",4) && NUM(0) && NUM(1) && NUM(2) && NUM(3)) {
        for (unsigned i=0;i<4;++i) if (n[i]>65535) goto fail;
        if (!hb_set_timing((hb_timing_t){n[0],n[1],n[2],n[3]})) goto fail;
        OK("}");
    }
    if (IS("TIMING",0)) {
        hb_timing_t t=hb_timing();
        snprintf(reply,cap,"{\"id\":%" PRIu32 ",\"ok\":true,\"setup\":%u,\"pulse\":%u,\"hold\":%u,\"divider\":%u,\"tick_ns\":%u}",id,t.setup,t.pulse,t.hold,t.divider,8*t.divider);
        goto done;
    }
    if (IS("DRIVE",2) && NUM(0) && NUM(1) && n[1]<=1 && hb_drive(n[0],n[1])) OK("}");
    if (IS("RESET",1) && NUM(0) && n[0]>=1 && n[0]<=1000) {
        if (!hb_reset(n[0])) goto hardware;
        OK("}");
    }
    if (IS("READ",2) && NUM(0) && NUM(1) && n[0]<4 && n[1]>0 && n[1]<=HB_MAX_BYTES) {
        uint8_t data[HB_MAX_BYTES]; char encoded[HB_MAX_BYTES*2+1];
        if (!hb_transfer(true,n[0],data,n[1])) goto hardware;
        for (unsigned i=0;i<n[1];++i) snprintf(encoded+2*i,3,"%02x",data[i]);
        snprintf(reply,cap,"{\"id\":%" PRIu32 ",\"ok\":true,\"data\":\"%s\"}",id,encoded);
        goto done;
    }
    if (IS("WRITE",2) && NUM(0) && n[0]<4) {
        size_t length=strlen(a[3]);
        if (!length || (length&1) || length>HB_MAX_BYTES*2) goto fail;
        uint8_t data[HB_MAX_BYTES];
        for (size_t i=0;i<length;i+=2) {
            int h=hex(a[3][i]),l=hex(a[3][i+1]);
            if (h<0 || l<0) goto fail;
            data[i/2]=(h<<4)|l;
        }
        if (!hb_transfer(false,n[0],data,length/2)) goto hardware;
        OK("}");
    }
    if (IS("EVENTS",2) && NUM(0) && n[0]>=1 && n[0]<=64) {
        size_t length=strlen(a[3]);
        if (!length || length%8 || length>HB_MAX_EVENTS*8) goto fail;
        uint32_t events[HB_MAX_EVENTS]; uint8_t samples[HB_MAX_EVENTS];
        char encoded[HB_MAX_EVENTS*2+1];
        for (unsigned i=0;i<length/8;++i) {
            events[i]=0;
            for (unsigned j=0;j<8;++j) {
                int v=hex(a[3][i*8+j]); if(v<0) goto fail;
                events[i]=(events[i]<<4)|v;
            }
        }
        if (!hb_events(events,samples,length/8,n[0])) goto hardware;
        for (unsigned i=0;i<length/8;++i) snprintf(encoded+2*i,3,"%02x",samples[i]);
        snprintf(reply,cap,"{\"id\":%" PRIu32 ",\"ok\":true,\"data\":\"%s\"}",id,encoded);
        goto done;
    }
    if (IS("CLOCKS",1) && NUM(0) && n[0]>=1 && n[0]<=1000) {
        uint32_t counts[2],elapsed;
        if (!hb_clocks(n[0],counts,&elapsed)) goto hardware;
        snprintf(reply,cap,"{\"id\":%" PRIu32 ",\"ok\":true,\"cpu_edges\":%" PRIu32 ",\"grom_edges\":%" PRIu32 ",\"elapsed_us\":%" PRIu32 "}",id,counts[0],counts[1],elapsed);
        goto done;
    }
    if (IS("WAIT",2) && NUM(0) && NUM(1) && n[0]<=1 && n[1]<=1000) {
        if (!hb_active()) goto hardware;
        uint32_t elapsed=0;
        bool matched=hb_wait_int(n[0],n[1],&elapsed);
        if (hb_stopping()) goto hardware;
        snprintf(reply,cap,"{\"id\":%" PRIu32 ",\"ok\":true,\"matched\":%s,\"elapsed_us\":%" PRIu32 "}",id,matched?"true":"false",elapsed);
        goto done;
    }
    if (IS("TRACE",3) && NUM(0) && NUM(1) && NUM(2) && hb_trace_arm(n[0],n[1],n[2])) OK("}");
    if (IS("CANCEL",0)) { hb_trace_cancel(); OK("}"); }
    if (IS("DUMP",2) && NUM(0) && NUM(1) && n[1]>0 && n[1]<=128) {
        uint32_t words[128]; char encoded[1025];
        if (!hb_trace_read(n[0],n[1],words)) { error="capture incomplete or range invalid"; goto fail; }
        for (unsigned i=0;i<n[1];++i) snprintf(encoded+8*i,9,"%08" PRIx32,words[i]);
        snprintf(reply,cap,"{\"id\":%" PRIu32 ",\"ok\":true,\"words\":\"%s\"}",id,encoded);
        goto done;
    }
    goto fail;
hardware:
    error="target not armed, operation aborted, or hardware timeout";
fail:
    snprintf(reply,cap,"{\"id\":%" PRIu32 ",\"ok\":false,\"error\":\"%s\"}",id,error);
done:
    return strlen(reply);
}
