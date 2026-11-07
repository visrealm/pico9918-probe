// SPDX-License-Identifier: MIT
#include "pico9918_bus.h"
#include "probe_config.h"
#include "probe.h"
#include "DAP.h"
#include "semphr.h"
#include "pico/stdlib.h"
#include "hardware/dma.h"
#include "hardware/clocks.h"
#include "hardware/adc.h"
#include "pico9918_bus.pio.h"
#include <stdatomic.h>
#include <string.h>

static SemaphoreHandle_t lock;
static atomic_bool stop_requested;
static bool armed, switched_power;

// Raw counts, not volts: the divider ratio is a property of whatever is wired to the
// pin, so the host calibrates it against a meter and the probe reports what it read.
// The minimum is the measurement that matters - a rail that sags under load is back
// to normal by the time anything asks - so it is tracked continuously and reset by ARM.
static uint16_t rail_now, rail_min = 0xffff, vsys_now, vsys_min = 0xffff;
static uint16_t temp_now, temp_hot = 0xffff;
// 255 ns setup, 1 us pulse, 1 us hold. Expressed in ticks, so it tracks the probe
// clock: these are the nanoseconds that matter, not the counts.
#define HB_SAFE_TIMING {51, 200, 200, 1}
static hb_timing_t timing = HB_SAFE_TIMING;
static uint write_offset, read_offset, counter_offset, trace_offset, events_offset, pulse_offset;
static uint16_t pulse_words[16];
static int tx_dma, rx_dma, trace_dma;
static uint32_t tx_words[HB_BURST_BYTES * 2], rx_words[HB_BURST_BYTES];
static uint8_t stress_expected[HB_BURST_BYTES], stress_actual[HB_BURST_BYTES];
static uint32_t trace_words[HB_TRACE_WORDS];
static unsigned trace_count;
static bool trace_armed;
#define DATA_MASK (0xffu << HB_DATA_BASE)
#define CONTROL_MASK (0xfu << HB_CSR)
#define BUS_MASK (DATA_MASK | CONTROL_MASK | (1u << HB_RESET))

void hb_lock(void) { xSemaphoreTake(lock, portMAX_DELAY); }
void hb_unlock(void) { xSemaphoreGive(lock); }
void hb_request_stop(void) { atomic_store(&stop_requested, true); }
bool hb_stopping(void) { return atomic_load(&stop_requested); }
bool hb_active(void) { return armed && !hb_stopping(); }
bool hb_switched(void) { return switched_power; }
hb_timing_t hb_timing(void) { return timing; }
uint32_t hb_pins(void) { return gpio_get_all(); }

void hb_sample_rails(void) {
    adc_select_input(HB_VSENSE_ADC);
    rail_now = adc_read();
    if (rail_now < rail_min) rail_min = rail_now;

    adc_select_input(HB_VSYS_ADC);
    vsys_now = adc_read();
    if (vsys_now < vsys_min) vsys_min = vsys_now;

    adc_select_input(HB_TEMP_ADC);
    temp_now = adc_read();
    if (temp_now < temp_hot) temp_hot = temp_now;
}

void hb_rails(hb_rails_t *out) {
    out->rail = rail_now;
    out->rail_min = (rail_min == 0xffff) ? rail_now : rail_min;
    out->vsys = vsys_now;
    out->vsys_min = (vsys_min == 0xffff) ? vsys_now : vsys_min;
    out->temp = temp_now;
    out->temp_hot = (temp_hot == 0xffff) ? temp_now : temp_hot;
}

static void release_pin(uint pin);

static void reset_assert(void) {
    gpio_init(HB_RESET);
    gpio_put(HB_RESET,0);
    gpio_set_dir(HB_RESET,GPIO_OUT);
}

static void reset_release(void) {
    gpio_put(HB_RESET,1);
    gpio_set_dir(HB_RESET,GPIO_OUT);
}

void hb_diag(hb_diag_t *d) {
    d->padout=pio1->dbg_padout;
    d->padoe=pio1->dbg_padoe;
    d->fdebug=pio1->fdebug;
    d->ctrl=pio1->ctrl;
    d->addr=pio1->sm[0].addr;
    d->pinctrl=pio1->sm[0].pinctrl;
    d->execctrl=pio1->sm[0].execctrl;
    d->csr_func=(uint8_t)gpio_get_function(HB_CSR);
    d->csw_func=(uint8_t)gpio_get_function(HB_CSW);
}

bool hb_hold(unsigned pin, bool high) {
    if (!hb_active() || (pin != HB_CSR && pin != HB_CSW)) return false;
    pio_sm_set_enabled(pio1,0,false);
    pio_sm_set_enabled(pio2,1,false);
    // Never let the probe drive the data bus during a static strobe test.
    for (uint data=HB_DATA_BASE; data<HB_DATA_BASE+8; ++data) release_pin(data);
    gpio_init(pin);
    gpio_put(pin,high);
    gpio_set_dir(pin,GPIO_OUT);
    return true;
}

static void release_pin(uint pin) {
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_IN);
    gpio_disable_pulls(pin);
}

void hb_init(void) {
    lock = xSemaphoreCreateMutex();
    configASSERT(lock);
    gpio_init(HB_POWER);
    gpio_put(HB_POWER, 0);
    gpio_set_dir(HB_POWER, GPIO_OUT);
    // LOAD-BEARING: PWM mode, not power save. ADC_VREF comes off 3V3, so the regulator's
    // ripple scales every reading, and power save ripples in bursts that a running
    // minimum latches onto in preference to any real sag.
    gpio_init(HB_SMPS_PS);
    gpio_put(HB_SMPS_PS, 1);
    gpio_set_dir(HB_SMPS_PS, GPIO_OUT);

    // TRAP: adc_gpio_init is what clears the pad's reset-state isolation and pull-down
    // on RP2350, so an analogue pin left alone reads zero whatever is on it. That is
    // not an ADC detail: it is why a digital read of GP26 says nothing before this runs.
    adc_init();
    adc_gpio_init(HB_VSENSE);
    adc_gpio_init(HB_VSYS);
    adc_set_temp_sensor_enabled(true);
    for (uint pin = HB_DATA_BASE; pin <= HB_RESET; ++pin) release_pin(pin);
    release_pin(PROBE_PIN_SWCLK);
    release_pin(PROBE_PIN_SWDIO);
    pio_sm_claim(pio1, 0);
    pio_sm_claim(pio2, 0);
    pio_sm_claim(pio2, 1);
    pio_sm_claim(pio2, 2);
    pio_sm_claim(pio2, 3);
    write_offset = pio_add_program(pio1, &hb_write_program);
    read_offset = pio_add_program(pio1, &hb_read_program);
    counter_offset = pio_add_program(pio2, &hb_counter_program);
    trace_offset = pio_add_program(pio2, &hb_trace_program);
    events_offset = pio_add_program(pio2, &hb_events_program);
    pulse_offset = pio_add_program(pio2, &hb_pulse_program);
    // Instruction memory is write-only, so the side-set patching in hb_pulse cannot
    // read back what pio_add_program relocated. Keep the relocated words here: JMP
    // is the only opcode carrying an address, in its low five bits.
    for (unsigned i = 0; i < hb_pulse_program.length; ++i) {
        uint16_t w = hb_pulse_program.instructions[i];
        if ((w & 0xe000u) == 0) w = (uint16_t)((w & ~0x1fu) | ((w + pulse_offset) & 0x1fu));
        pulse_words[i] = w;
    }
    tx_dma = dma_claim_unused_channel(true);
    rx_dma = dma_claim_unused_channel(true);
    trace_dma = dma_claim_unused_channel(true);
}

void hb_trace_cancel(void) {
    pio_sm_set_enabled(pio2, 0, false);
    dma_channel_abort(trace_dma);
    trace_armed = false;
    trace_count = 0;
}

void hb_off(void) {
    // Caller owns lock, so no DAP packet can re-enable SWD halfway through.
    pio_sm_set_enabled(pio1, 0, false);
    pio_sm_set_enabled(pio2, 1, false);
    dma_channel_abort(tx_dma);
    dma_channel_abort(rx_dma);
    hb_trace_cancel();
    probe_deinit();
    DAP_Data.debug_port = 0;
    for (uint pin = HB_DATA_BASE; pin <= HB_RESET; ++pin) release_pin(pin);
    release_pin(PROBE_PIN_SWCLK);
    release_pin(PROBE_PIN_SWDIO);
    gpio_put(HB_POWER, 0);
    armed = false;
    switched_power = false;
}

static void setup_bus_pins(bool reset_high) {
    gpio_put_masked(CONTROL_MASK, CONTROL_MASK);
    gpio_set_dir_out_masked(CONTROL_MASK);
    if (reset_high) reset_release(); else reset_assert();
    pio_sm_set_pins_with_mask(pio1, 0, CONTROL_MASK, DATA_MASK | CONTROL_MASK);
    pio_sm_set_consecutive_pindirs(pio1, 0, HB_DATA_BASE, 8, false);
    pio_sm_set_consecutive_pindirs(pio1, 0, HB_CSR, 4, true);
    for (uint pin = HB_DATA_BASE; pin <= HB_MODE1; ++pin) {
        gpio_disable_pulls(pin);
        pio_gpio_init(pio1, pin);
    }
}

bool hb_arm(bool switched) {
    if (armed) return false; // require explicit OFF to change power arrangement
    atomic_store(&stop_requested, false);
    // WARNING: rail before pins. Driving 3.3 V into the target's HC inputs while its
    // 5 V supply is still off forward-biases their protection diodes and floats the
    // unswitched rail through them, which is the classic CMOS latch-up setup.
    switched_power = switched;
    gpio_put(HB_POWER, switched);
    if (switched) vTaskDelay(pdMS_TO_TICKS(HB_POWER_SETTLE_MS));
    setup_bus_pins(true);
    armed = true;
    vTaskDelay(pdMS_TO_TICKS(100));
    if (hb_stopping()) { hb_off(); return false; }
    rail_min = vsys_min = temp_hot = 0xffff; // extremes over a running target, not its inrush
    return true;
}

bool hb_set_timing(hb_timing_t t) {
    if (t.setup < 5 || t.setup > 255 || t.pulse < 6 || t.pulse > 255 ||
        t.hold < 6 || t.hold > 255 || t.divider < 1 || t.divider > 64) return false;
    timing = t;
    return true;
}

uint32_t hb_time_us(void) { return time_us_32(); }
uint32_t hb_clock_hz(void) { return clock_get_hz(clk_sys); }
uint32_t hb_swclk_hz(void) { return probe_swclk_freq_hz(); }

bool hb_power_cycle(unsigned off_ms, unsigned reset_ms, uint32_t *enabled_at_us, uint32_t *reset_released_us) {
    // Only a previously selected switched-power fixture can run this test.
    if (!hb_active() || !switched_power || off_ms<100 || off_ms>10000 || reset_ms>1000) return false;
    hb_off(); // release SWD and all bus outputs throughout the off interval
    uint32_t off_at=time_us_32();
    while (time_us_32()-off_at<off_ms*1000u) {
        if (hb_stopping()) return false;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    if (hb_stopping()) return false;
    if (reset_ms) {
        reset_assert(); // LOW before power rises
    }
    // The timestamp is the switch command, not the rail: boot latency measured from it
    // carries the switch's own delay, and RESET stays asserted across that window.
    *enabled_at_us=time_us_32();
    gpio_put(HB_POWER,1);
    switched_power=true;
    vTaskDelay(pdMS_TO_TICKS(HB_POWER_SETTLE_MS));
    setup_bus_pins(reset_ms==0);
    armed=true;
    while (time_us_32()-*enabled_at_us<reset_ms*1000u) {
        if (hb_stopping()) { hb_off(); return false; }
        vTaskDelay(1);
    }
    if (hb_stopping()) { hb_off(); return false; }
    reset_release();
    *reset_released_us=reset_ms ? time_us_32()-*enabled_at_us : 0;
    return true;
}

bool hb_transfer(bool read, unsigned port, uint8_t *data, unsigned count) {
    if (!hb_active() || port > 3 || !count || count > HB_BURST_BYTES) return false;
    uint offset = read ? read_offset : write_offset;
    pio_sm_set_enabled(pio1, 0, false);
    // The raw event engine may have owned these pins on PIO2.
    for (uint pin=HB_DATA_BASE; pin<=HB_MODE1; ++pin) pio_gpio_init(pio1,pin);
    pio_sm_config c = read ? hb_read_program_get_default_config(offset) : hb_write_program_get_default_config(offset);
    sm_config_set_out_pins(&c, HB_DATA_BASE, 12);
    sm_config_set_set_pins(&c, HB_CSR, 2);
    sm_config_set_in_pins(&c, HB_DATA_BASE);
    sm_config_set_out_shift(&c, true, false, 32);
    sm_config_set_in_shift(&c, false, false, 32);
    sm_config_set_clkdiv_int_frac(&c, timing.divider, 0);
    pio_sm_init(pio1, 0, offset, &c);
    // RP2350's SM restart clears the PIO pin-write flags, so establish the
    // idle values and directions after pio_sm_init(), immediately before run.
    pio_sm_set_pins_with_mask(pio1, 0, CONTROL_MASK, DATA_MASK | CONTROL_MASK);
    pio_sm_set_consecutive_pindirs(pio1, 0, HB_DATA_BASE, 8, false);
    pio_sm_set_consecutive_pindirs(pio1, 0, HB_CSR, 4, true);
    // Port 0/1 = MODE1 HIGH; extended port 2/3 = MODE1 LOW.
    uint32_t pins = (3u << 8) | ((port & 1u) << 10) | ((!(port & 2u)) << 11);
    unsigned sample = timing.pulse > 35 ? 32 : timing.pulse - 3;
    uint32_t loops = read
        ? (timing.setup - 4) | ((sample - 3) << 8) |
          ((timing.pulse - sample - 3) << 16) | ((timing.hold - 6) << 24)
        : (timing.setup - 5) | ((timing.pulse - 3) << 8) | ((timing.hold - 6) << 16);
    for (unsigned i = 0; i < count; ++i) {
        // RP2350 replaces this SM's pin-direction write set on OUT PINDIRS,
        // so include the four permanently-driven control pins every time.
        tx_words[2*i] = read ? ((pins << 12) | (0xf00u << 0))
                             : (pins | data[i] | (0xfffu << 12));
        tx_words[2*i+1] = loops;
    }
    dma_channel_config tx = dma_channel_get_default_config(tx_dma);
    channel_config_set_transfer_data_size(&tx, DMA_SIZE_32);
    channel_config_set_read_increment(&tx, true);
    channel_config_set_write_increment(&tx, false);
    channel_config_set_dreq(&tx, pio_get_dreq(pio1, 0, true));
    channel_config_set_high_priority(&tx, true);
    dma_channel_config rx = dma_channel_get_default_config(rx_dma);
    channel_config_set_transfer_data_size(&rx, DMA_SIZE_32);
    channel_config_set_read_increment(&rx, false);
    channel_config_set_write_increment(&rx, true);
    channel_config_set_dreq(&rx, pio_get_dreq(pio1, 0, false));
    channel_config_set_high_priority(&rx, true);
    dma_channel_configure(tx_dma, &tx, &pio1->txf[0], tx_words, count*2, false);
    dma_channel_configure(rx_dma, &rx, rx_words, &pio1->rxf[0], count, false);
    dma_start_channel_mask((1u << tx_dma) | (1u << rx_dma));
    pio_sm_set_enabled(pio1, 0, true);
    uint64_t deadline = time_us_64() + 50000 +
        ((uint64_t)count * (timing.setup + timing.pulse + timing.hold) * timing.divider * HB_TICK_NS / 1000);
    while (dma_channel_is_busy(rx_dma)) {
        if (hb_stopping() || time_us_64() > deadline) {
            hb_off();
            return false;
        }
        vTaskDelay(1);
    }
    pio_sm_set_enabled(pio1, 0, false);
    dma_channel_abort(tx_dma);
    if (read) for (unsigned i = 0; i < count; ++i) data[i] = rx_words[i] & 0xff;
    return true;
}

// Hand PIO2 SM1 the bus pins and the idle state both raw engines start from.
static void events_claim_pins(void) {
    pio_sm_set_enabled(pio1,0,false);
    pio_sm_set_enabled(pio2,1,false);
    for (uint pin=HB_DATA_BASE;pin<=HB_MODE1;++pin) pio_gpio_init(pio2,pin);
}

static void events_idle_pins(void) {
    pio_sm_set_pins_with_mask(pio2,1,CONTROL_MASK,DATA_MASK|CONTROL_MASK);
    pio_sm_set_consecutive_pindirs(pio2,1,HB_DATA_BASE,8,false);
    pio_sm_set_consecutive_pindirs(pio2,1,HB_CSR,4,true);
}

// Run SM1 over tx_words and collect rx_count samples. Both engines below share it.
static bool events_run(unsigned tx_count, unsigned rx_count, uint64_t ticks, unsigned divider) {
    dma_channel_config tx=dma_channel_get_default_config(tx_dma);
    channel_config_set_transfer_data_size(&tx,DMA_SIZE_32);
    channel_config_set_read_increment(&tx,true);
    channel_config_set_write_increment(&tx,false);
    channel_config_set_dreq(&tx,pio_get_dreq(pio2,1,true));
    channel_config_set_high_priority(&tx,true);
    dma_channel_config rx=dma_channel_get_default_config(rx_dma);
    channel_config_set_transfer_data_size(&rx,DMA_SIZE_32);
    channel_config_set_read_increment(&rx,false);
    channel_config_set_write_increment(&rx,true);
    channel_config_set_dreq(&rx,pio_get_dreq(pio2,1,false));
    channel_config_set_high_priority(&rx,true);
    dma_channel_configure(tx_dma,&tx,&pio2->txf[1],tx_words,tx_count,false);
    dma_channel_configure(rx_dma,&rx,rx_words,&pio2->rxf[1],rx_count,false);
    dma_start_channel_mask((1u<<tx_dma)|(1u<<rx_dma));
    pio_sm_set_enabled(pio2,1,true);
    uint64_t deadline=time_us_64()+50000+ticks*divider*HB_TICK_NS/1000;
    while (dma_channel_is_busy(rx_dma)) {
        if (hb_stopping() || time_us_64()>deadline) { hb_off(); return false; }
        vTaskDelay(1);
    }
    pio_sm_set_enabled(pio2,1,false);
    dma_channel_abort(tx_dma);
    return true;
}

bool hb_events(const uint32_t *events, uint8_t *samples, unsigned count, unsigned divider, unsigned repeat) {
    if (!hb_active() || !count || count>HB_MAX_EVENTS || !divider || divider>64 ||
        !repeat || (uint64_t)count*repeat>HB_BURST_BYTES) return false;
    uint64_t ticks=0;
    for (unsigned i=0; i<count; ++i) {
        unsigned strobes=(events[i]>>8)&3, oe=(events[i]>>12)&255;
        // Two refusals guard the hardware and are absolute: both strobes asserted
        // means the target drives data into the probe driving data, real contention
        // into a 5 V part; and data driven during a read is the same collision by
        // another route. Data OE therefore changes only with both strobes inactive.
        //
        // MODE is deliberately NOT in that list. Moving it inside an asserted strobe
        // is legal host behaviour - the datasheet guarantees MODE for only 30 ns
        // after /CSW falls and specifies no hold at all for /CSR - so refusing it
        // made the two conformance parameters that test exactly that inexpressible.
        // It is an input to the target either way and collides with nothing.
        if (!strobes || (strobes==2 && oe) || (oe!=0 && oe!=255)) return false;
        if (events[i]>>28) return false;
        if (i && (((events[i]^events[i-1])&0xff000) &&
                  (strobes!=3 || ((events[i-1]>>8)&3)!=3))) return false;
        ticks+=5+((events[i]>>20)&255);
    }
    // A repeat splices the last event back onto the first, so the wrap has to obey
    // the same rules the interior does or a soak can step through an illegal state
    // once per iteration.
    if (((events[0]>>8)&3)!=3 || ((events[count-1]>>8)&3)!=3 ||
        ((events[count-1]>>12)&255)) return false;
    ticks*=repeat;
    events_claim_pins();
    pio_sm_config c=hb_events_program_get_default_config(events_offset);
    sm_config_set_out_pins(&c,HB_DATA_BASE,12);
    sm_config_set_in_pins(&c,HB_DATA_BASE);
    sm_config_set_out_shift(&c,true,true,32);
    sm_config_set_in_shift(&c,false,true,8);
    sm_config_set_clkdiv_int_frac(&c,divider,0);
    pio_sm_init(pio2,1,events_offset,&c);
    events_idle_pins();
    for (unsigned i=0;i<count;++i) {
        uint32_t pins=events[i]&0xfff;
        uint32_t dirs=((events[i]>>12)&0xff)|0xf00;
        tx_words[i]=pins|(dirs<<12)|(((events[i]>>20)&255)<<24);
    }
    for (unsigned r=1;r<repeat;++r) memcpy(tx_words+r*count,tx_words,count*sizeof tx_words[0]);
    if (!events_run(count*repeat,count*repeat,ticks,divider)) return false;
    for (unsigned i=0;i<count;++i) samples[i]=rx_words[i]&255;
    return true;
}

bool hb_pulse(uint32_t bus, unsigned rest, unsigned excursion, unsigned pre, unsigned width,
              unsigned post, unsigned divider, unsigned repeat, uint8_t *sample) {
    unsigned oe=(bus>>12)&255;
    if (!hb_active() || rest>3 || excursion>3 || !divider || divider>64) return false;
    // Same three refusals the event engine makes, against both levels: never both
    // strobes asserted, and never drive the data bus into a target read. Bit 0 of a
    // side-set value is /CSR and bit 1 is /CSW, so a clear bit is an assertion.
    if (!rest || !excursion) return false;
    if ((oe!=0 && oe!=255) || (oe && (!(rest&1) || !(excursion&1)))) return false;
    if (!pre || !width || !post || pre>1024 || width>1024 || post>1024) return false;
    if (bus>>20 || !repeat || (uint64_t)repeat*2>HB_BURST_BYTES) return false;
    events_claim_pins();
    for (unsigned i=0;i<hb_pulse_program.length;++i) {
        unsigned side=(i==hb_pulse_offset_excursion)?excursion:rest;
        pio2->instr_mem[pulse_offset+i]=(pulse_words[i]&~(3u<<11))|(side<<11);
    }
    pio_sm_config c=hb_pulse_program_get_default_config(pulse_offset);
    sm_config_set_out_pins(&c,HB_DATA_BASE,12);
    sm_config_set_in_pins(&c,HB_DATA_BASE);
    sm_config_set_sideset_pins(&c,HB_CSR);
    sm_config_set_out_shift(&c,true,false,32);
    sm_config_set_in_shift(&c,false,false,32);
    sm_config_set_clkdiv_int_frac(&c,divider,0);
    pio_sm_init(pio2,1,pulse_offset,&c);
    events_idle_pins();
    // The side-set drives both strobes every cycle, so the pins word must agree
    // with the resting level rather than race it.
    uint32_t pins=(bus&0xff)|(rest<<8)|(bus&0xc00);
    tx_words[0]=pins|((oe|0xf00u)<<12);
    tx_words[1]=(pre-1)|((width-1)<<10)|((post-1)<<20);
    for (unsigned r=1;r<repeat;++r) { tx_words[2*r]=tx_words[0]; tx_words[2*r+1]=tx_words[1]; }
    uint64_t ticks=(uint64_t)repeat*(9+pre+width+post);
    bool ok=events_run(repeat*2,repeat,ticks,divider);
    // A resting level that asserts a strobe leaves it asserted when the program
    // stalls, so the bus would stay selected after the command returned. Every
    // other engine ends idle because its own validator forces the last event to;
    // this one has to be put back deliberately.
    events_idle_pins();
    if (!ok) return false;
    *sample=rx_words[0]&255;
    return true;
}

static bool stress_once(bool read, unsigned count, uint32_t seed, uint32_t *errors, uint32_t *first) {
    hb_timing_t candidate=timing;
    hb_timing_t safe=HB_SAFE_TIMING;
    uint8_t address[2]={0,64};
    bool ok=false;
    if (!seed) seed=1;
    for (unsigned i=0;i<count;++i) {
        seed^=seed<<13; seed^=seed>>17; seed^=seed<<5;
        stress_expected[i]=(uint8_t)seed;
        stress_actual[i]=read ? stress_expected[i] : (stress_expected[i]^255);
    }
    timing=safe;
    if (!hb_transfer(false,1,address,2) || !hb_transfer(false,0,stress_actual,count)) goto done;
    address[1]=read ? 0 : 64;
    if (!hb_transfer(false,1,address,2)) goto done;
    timing=candidate;
    if (!hb_transfer(read,0,read ? stress_actual : stress_expected,count)) goto done;
    timing=safe;
    if (!read) {
        address[1]=0;
        if (!hb_transfer(false,1,address,2) || !hb_transfer(true,0,stress_actual,count)) goto done;
    }
    *errors=0; *first=UINT32_MAX;
    for (unsigned i=0;i<count;++i) if (stress_expected[i]!=stress_actual[i]) {
        if (!*errors) *first=i;
        ++*errors;
    }
    ok=true;
done:
    timing=candidate;
    return ok;
}

// A boundary is a distribution, not a number: near the limit a period passes
// sometimes, because the probe's tick and the target's are incommensurate and each
// repetition lands on a different phase. Returning the tally rather than the first
// failure is what makes an interpolated 50% point available at all.
bool hb_stress(bool read, unsigned count, uint32_t seed, unsigned repeat,
               uint32_t *passes, uint32_t *errors, uint32_t *first) {
    if (!hb_active() || !count || count>HB_BURST_BYTES || !repeat) return false;
    *passes=0; *errors=0; *first=UINT32_MAX;
    for (unsigned r=0;r<repeat;++r) {
        uint32_t e,f;
        if (!stress_once(read,count,seed+r,&e,&f)) return false;
        if (e) { *errors=e; *first=f; } else ++*passes;
        if (hb_stopping()) return false;
    }
    return true;
}

bool hb_reset(unsigned ms) {
    if (!hb_active() || !ms || ms > 1000) return false;
    // Last transfer has completed and both strobes are inactive.
    reset_assert();
    vTaskDelay(pdMS_TO_TICKS(ms));
    reset_release();
    if (hb_stopping()) { hb_off(); return false; }
    return true;
}

bool hb_wait_int(bool high, unsigned timeout_ms, uint32_t *elapsed) {
    if (!hb_active() || timeout_ms > 1000) return false;
    uint32_t start = time_us_32();
    while (gpio_get(HB_INT) != high) {
        if (hb_stopping() || time_us_32() - start >= timeout_ms * 1000) {
            *elapsed = time_us_32() - start;
            return false;
        }
        vTaskDelay(1);
    }
    *elapsed = time_us_32() - start;
    return true;
}

bool hb_clocks(unsigned ms, uint32_t counts[2], uint32_t *elapsed) {
    if (!hb_active() || !ms || ms > 1000) return false;
    for (uint sm = 2; sm <= 3; ++sm) {
        pio_sm_config c = hb_counter_program_get_default_config(counter_offset);
        sm_config_set_in_pins(&c, sm == 2 ? HB_CPUCLK : HB_GROMCLK);
        pio_sm_init(pio2, sm, counter_offset, &c);
        pio_sm_exec(pio2, sm, pio_encode_mov_not(pio_x, pio_null));
    }
    uint32_t start = time_us_32();
    pio_enable_sm_mask_in_sync(pio2, 12);
    vTaskDelay(pdMS_TO_TICKS(ms));
    pio_set_sm_mask_enabled(pio2, 12, false);
    *elapsed = time_us_32() - start;
    for (uint sm = 2; sm <= 3; ++sm) {
        pio_sm_exec(pio2, sm, pio_encode_mov(pio_isr, pio_x));
        pio_sm_exec(pio2, sm, pio_encode_push(false, false));
        counts[sm-2] = UINT32_MAX - pio_sm_get(pio2, sm);
    }
    return !hb_stopping();
}

bool hb_drive(unsigned ma, bool fast) {
    enum gpio_drive_strength value;
    switch (ma) {
        case 2: value=GPIO_DRIVE_STRENGTH_2MA; break;
        case 4: value=GPIO_DRIVE_STRENGTH_4MA; break;
        case 8: value=GPIO_DRIVE_STRENGTH_8MA; break;
        case 12: value=GPIO_DRIVE_STRENGTH_12MA; break;
        default: return false;
    }
    for (uint pin=HB_DATA_BASE; pin<=HB_MODE1; ++pin) {
        gpio_set_drive_strength(pin,value);
        gpio_set_slew_rate(pin, fast ? GPIO_SLEW_RATE_FAST : GPIO_SLEW_RATE_SLOW);
    }
    return true;
}

bool hb_trace_arm(unsigned count, unsigned divider, unsigned pin) {
    if (!hb_active() || !count || count > HB_TRACE_WORDS || !divider || divider > 65535 ||
        (pin != HB_CSR && pin != HB_CSW && pin != HB_INT)) return false;
    hb_trace_cancel();
    pio2->instr_mem[trace_offset] = pio_encode_wait_gpio(false,pin);
    pio_sm_config c = hb_trace_program_get_default_config(trace_offset);
    sm_config_set_in_pins(&c, HB_DATA_BASE);
    sm_config_set_in_shift(&c, false, false, 32);
    sm_config_set_clkdiv_int_frac(&c,divider,0);
    pio_sm_init(pio2,0,trace_offset,&c);
    dma_channel_config rx=dma_channel_get_default_config(trace_dma);
    channel_config_set_transfer_data_size(&rx,DMA_SIZE_32);
    channel_config_set_read_increment(&rx,false);
    channel_config_set_write_increment(&rx,true);
    channel_config_set_dreq(&rx,pio_get_dreq(pio2,0,false));
    channel_config_set_high_priority(&rx,true);
    dma_channel_configure(trace_dma,&rx,trace_words,&pio2->rxf[0],count,true);
    trace_count=count;
    trace_armed=true;
    pio_sm_set_enabled(pio2,0,true);
    return true;
}

bool hb_trace_read(unsigned start, unsigned count, uint32_t *data) {
    if (!trace_armed || !count || count > 128 || start > trace_count || count > trace_count-start ||
        dma_channel_is_busy(trace_dma)) return false;
    pio_sm_set_enabled(pio2,0,false);
    for (unsigned i=0; i<count; ++i) data[i]=trace_words[start+i] & 0x3ffff;
    return true;
}

uint16_t hb_dap_command(const uint8_t *request, uint8_t *response) {
    hb_lock();
    uint16_t n;
    // Configuration/identity is usable before ARM; no SWD output when off.
    if (!hb_active() && request[0] != ID_DAP_Info && request[0] != ID_DAP_HostStatus &&
        request[0] != ID_DAP_Disconnect && request[0] != ID_DAP_TransferConfigure &&
        request[0] != ID_DAP_SWJ_Clock && request[0] != ID_DAP_SWD_Configure) {
        response[0]=request[0]; response[1]=0xff;
        if (request[0]==ID_DAP_Connect) response[1]=0;
        n=2;
    } else n=DAP_ExecuteCommand(request,response) & 0xffff;
    hb_unlock();
    return n;
}
