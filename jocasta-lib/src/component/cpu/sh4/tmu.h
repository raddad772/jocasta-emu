#pragma once

#include "helpers/int.h"

namespace SH4 {
struct core;

struct TMU_chan {
    //explicit TMU_chan() {}
    u32 num{};
    core *cpu{};

    [[nodiscard]] inline u64 cycles_until_underflow() const {
        return (static_cast<u64>(TCNT) + 1) << shift;
    }

    u32 TCOR{}; // Timer Constant Register. When TCNT underflows this is the reload value.
    u32 TCNT{}; // Timer Counter
    union {
        struct {
            u16 TPSC : 3; // Timer prescaler
            u16 CKEG : 2; // Clock Edge
            u16 UNIE : 1; // Interrupt on underflow enable
            u16 ICPE : 2; // Input Capture Interrupt Flag Enable (ch2 only)
            u16 UNF : 1; // Underflow flag, set on timer tick to 0
            u16 ICPF : 1; // Input Capture Has Ocurred flag
        };
        u16 u{};
    } TCR{};
    u32 shift{};
    //u32 old_mode{};
    //u32 base{};
    //u64 base64{};
    //u32 mask{};
    //u64 mask64{};
    u64 *master_cycles{};

    u64 cycles_at_start{}; // Master clock time when it as last started

    u32 sch_id{};
    u32 underflow_still_sch{};
    u64 next_underflow_at{}; // Schedule tiem for next tick when last stopped/started
    void update_IRQs();

    void start();
    void stop();
    void schedule_underflow();

    void underflow();

    void write_TSTR(bool new_val);
    void write_TCNT(u32 val);
    void write_TCR(u32 val);
    void reset();

    u32 read_TCNT() const;
};

struct TMU {
    explicit TMU(core *parent, u64 *cycles_in) : master_cycles(cycles_in), cpu(parent) {
        for (u32 i = 0; i < 3; i++) {
            channels[i].num = i;
            channels[i].cpu = parent;
            channels[i].master_cycles = cycles_in;
        }
    }
    void reset();
    u64 *master_cycles{};
    core *cpu{};
    TMU_chan channels[3];

    u32 pclock_shift{};

    void write(u32 addr, u8 sz, u64 val, bool *success);
    u64 read(u32 addr, u8 sz, bool *success);

    u8 TOCR{}; // Timer output control
    u32 TSTR{}; // Timer start register

};

}