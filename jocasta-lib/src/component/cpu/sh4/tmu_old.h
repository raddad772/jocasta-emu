//
// Created by RadDad772 on 3/8/24.
//

#pragma once
#include "helpers/int.h"
#include "helpers/scheduler.h"

// TMUs count down at 1/4 speed from the CPU
namespace SH4 {
struct core;

struct TMU_old;

struct TMU_chan_old {
    explicit TMU_chan_old(TMU_old *parent) : cu(parent) {}
    u32 num;
    TMU_old *cu{};
    u32 TCOR{};
    u32 TCNT{};
    u32 TCR{};
    u32 shift{};
    u32 old_mode{};
    u32 base{};
    u64 base64{};
    u32 mask{};
    u64 mask64{};
};

struct TMU_old {
    explicit TMU_old(core *parent, u64 *cycles_in) : cpu(parent), master_cycles(cycles_in) {}
    u64 *master_cycles{};
    core *cpu;
    u32 TOCR{}; // Timer output control
    u32 TSTR{};
    u32 TCOR[3]{};
    u32 TCNT[3]{};
    u32 TCR[3]{};
    u32 shift[3]{};
    u32 old_mode[3]{};
    u32 base[3]{};
    u64 base64[3]{};
    u32 mask[3]{};
    u64 mask64[3]{};
    [[nodiscard]] i64 read_TCNT64(u32 ch) const;
    [[nodiscard]] u32 read_TCNT(u32 ch, bool is_regread) const;
    void sched_chan_tick(u32 ch);
    void write_TCNT(u32 ch, u32 data);
    void turn_onoff(u32 ch, u32 on);
    void write_TSTR(u32 val);
    void update_counts(u32 ch);
    void reset();
    void write_TCR(u32 ch, u32 val);
    void write(u32 addr, u8 sz, u64 val, bool* success);
    u64 read(u32 addr, u8 sz, bool* success);
};
}