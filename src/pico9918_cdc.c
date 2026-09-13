// SPDX-License-Identifier: MIT
// Replaces the UART backend, keeping the stock CDC interface and CMSIS-DAP.
#include "pico/stdlib.h"
#include "FreeRTOS.h"
#include "task.h"
#include "tusb.h"
#include "pico9918_bus.h"
#include "pico9918_protocol.h"
#include <stdatomic.h>
#include <string.h>

TaskHandle_t uart_taskhandle;
static char line[HB_LINE_SIZE], reply[HB_REPLY_SIZE];
static size_t used, tx_length, tx_offset;
static bool overflow;
static atomic_uint connection_epoch;

void cdc_uart_init(void) { hb_init(); }

bool cdc_task(void) {
    static unsigned epoch;
    static bool stopped;
    if (hb_stopping() && !stopped) {
        hb_lock(); hb_off(); hb_unlock();
        used=tx_length=tx_offset=0; overflow=false;
        stopped=true;
    } else if (!hb_stopping()) stopped=false;
    if (epoch!=atomic_load(&connection_epoch)) {
        epoch=atomic_load(&connection_epoch);
        used=tx_length=tx_offset=0; overflow=false;
        tud_cdc_write_clear();
    }
    if (!tud_cdc_connected()) return false;
    if (tx_offset<tx_length) {
        tx_offset+=tud_cdc_write(reply+tx_offset,tx_length-tx_offset);
        tud_cdc_write_flush();
        return true;
    }
    while (tud_cdc_available()) {
        char c=(char)tud_cdc_read_char();
        if (c=='\n') {
            line[used]=0;
            if (overflow) strcpy(reply,"{\"id\":0,\"ok\":false,\"error\":\"line too long or invalid character\"}");
            else if (!used) continue;
            else { hb_lock(); hb_command(line,reply,sizeof(reply)-2); hb_unlock(); }
            used=0; overflow=false;
            tx_length=strlen(reply); reply[tx_length++]='\n'; reply[tx_length]=0; tx_offset=0;
            return true;
        }
        if ((c<32 && c!='\r' && c!='\t') || (unsigned char)c>126) overflow=true;
        if (used+1<sizeof(line)) line[used++]=c;
        else overflow=true;
    }
    return false;
}
void cdc_thread(void *unused) {
    (void)unused;
    while (true) { cdc_task(); vTaskDelay(1); }
}
void tud_cdc_line_coding_cb(uint8_t itf, const cdc_line_coding_t *coding) { (void)itf; (void)coding; }
void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts) {
    (void)itf; (void)rts;
    if (!dtr) tud_cdc_read_flush();
    atomic_fetch_add(&connection_epoch,1);
}
void tud_cdc_send_break_cb(uint8_t itf, uint16_t duration) {
    (void)itf;
    if (duration) hb_request_stop(); // emergency release/power-off
}
