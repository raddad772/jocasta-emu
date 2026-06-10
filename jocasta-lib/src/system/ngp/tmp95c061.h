#pragma once

#include "helpers/int.h"
#include "helpers/scheduler.h"
#include "component/cpu/tlcs900h/tlcs900h.h"
#include "tmp95c061/interrupts.h"
#include "tmp95c061/timer.h"
#include "tmp95c061/adc.h"
#include "tmp95c061/rtc.h"
#include "tmp95c061/sio.h"
#include "tmp95c061/chip_select.h"
#include "tmp95c061/watchdog.h"

namespace TMP95C061 {

static constexpr u32 SFR_END = 0x100;

struct core {
    core(scheduler_t *scheduler_in, u64 master_clock_freq, u32 divider);
    void reset();
    void schedule_first(u64 start);
    void setup_tracing(jsm_debug_read_trace *strct, u64 *trace_cycle_ptr, i32 source_id);

    void hblank() { timer.ext_tick_ti0(); }

    int cs_select(u32 addr) const { return cs.select(addr); }

    u8 reg_read(u32 addr, bool has_effect);
    void reg_write(u32 addr, u8 val);

    template<bool peek> u32 bus_read(u8 sz, u32 addr);
    template<bool peek> void bus_write(u8 sz, u32 addr, u32 val);

    scheduler_t *scheduler{};
    u64 clocks_per_second{};
    TLCS900H::core cpu;
    INTC intc;
    TIMER timer;
    ADC adc;
    RTC rtc;
    SIO sio{};
    CHIPSELECT cs{};
    WDT wdt;

    void *ext_ptr{};
    u8 (*ext_read8)(void *, u32){};
    u16 (*ext_read16)(void *, u32){};
    u32 (*ext_read32)(void *, u32){};
    void (*ext_write8)(void *, u32, u8){};
    void (*ext_write16)(void *, u32, u16){};
    void (*ext_write32)(void *, u32, u32){};
    u8 (*ext_peek8)(void *, u32){};
    u16 (*ext_peek16)(void *, u32){};
    u32 (*ext_peek32)(void *, u32){};

    void *io_ptr{};
    u8 (*io_read)(void *ptr, u8 addr, bool has_effect){};
    void (*io_write)(void *ptr, u8 addr, u8 data){};

private:
    static void timer_raise(void *ptr, u32 timer_index);
    static void adc_raise(void *ptr);
    static void wd_raise(void *ptr);
};

}
