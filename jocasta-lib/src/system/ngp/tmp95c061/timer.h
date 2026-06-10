#pragma once

#include "helpers/int.h"
#include "helpers/scheduler.h"

namespace TMP95C061 {

static constexpr u32 PHI_T1 = 128;
static constexpr u32 PHI_T4 = 512;
static constexpr u32 PHI_T16 = 2048;
static constexpr u32 PHI_T256 = 32768;

struct timer {
    u8 treg{};
    u8 buffer{};
    u8 counter{};
    u8 clk_sel{};
    bool run{};
    bool ff{};
    u64 sched_id{};
    u32 still_sched{};
};

struct TIMER {
    TIMER(scheduler_t *scheduler_in, void *irq_ptr_in,
          void (*raise_irq_in)(void *ptr, u32 timer_index));
    void reset();

    u8 read(u8 addr);
    void write(u8 addr, u8 val);

    void ext_tick_ti0();

    static void on_match(void *ptr, u64 key, u64 clock, u32 jitter);

    scheduler_t *scheduler{};
    void *irq_ptr{};
    void (*raise_irq)(void *ptr, u32 timer_index){};

    void *ff_out_ptr{};
    void (*ff_out)(void *ptr, u32 pair, bool level){};

    timer t[4]{};
    u8 T01MOD{}, T23MOD{}, TFFCR{}, TRUN{}, TRDC{};

private:
    u8 pair_mode(u32 pair) const;
    u32 clk_sel_of(u32 n) const;
    u32 clk_period_master(u32 n) const;
    u32 period_ticks(u32 n) const;
    bool is_external(u32 n) const;
    bool is_16bit(u32 pair) const;
    void latch_treg(u32 n);
    void reschedule(u32 n);
    void deschedule(u32 n);
    void on_compare_match(u32 n);
    void toggle_ff(u32 n);
    void set_ff(u32 pair, bool level);
    void recompute_all();
};

}
