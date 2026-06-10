//
// Created by . on 4/16/26.
//

#include <cstdio>

#include "helpers/setbits.h"
#include "tmu.h"
#include "sh4_interpreter.h"

namespace SH4 {

#define SH4_CYCLES_PER_SEC 200000000
#define tmu_underflow 0x0100
#define tmu_UNIE      0x0020
#define TCUSE *master_cycles

u32 TMU_chan::read_TCNT() const {
    if (!underflow_still_sch) return TCNT;

    u64 diff_cycles = (*master_cycles) - cycles_at_start;
    u64 diff_val = diff_cycles >> shift;
    if (diff_val > (static_cast<u64>(TCNT) + 1)) {
        printf("\nT%d WARN. DIFF VAL: %08llx %lld    TCNT: %08llx. SHOULD HAVE UNDERFLOWED! cyc:%lld", num, diff_val, diff_val, static_cast<u64>(TCNT) + 1, *master_cycles);
    }

    return (static_cast<u64>(TCNT) - diff_val) & 0xFFFF'FFFF;
}

void TMU_chan::update_IRQs() {
    cpu->interrupt_pend(static_cast<IRQ_SOURCES>(IRQ_tmu0_tuni0 + num), TCR.UNF && TCR.UNIE); // underflow
}

void TMU_chan::start() {
    if (!(cpu->tmu.TSTR & (1 << num))) {
        // No need to do anything!
        return;
    }
    next_underflow_at = *master_cycles;
    cycles_at_start = *master_cycles;
    schedule_underflow();
}

void TMU_chan::write_TCR(u32 val) {
    // on Dreamcast, minimum divisor is 4.
    stop();
    update_IRQs();

    shift = 2;
    u16 mask = 0b0000'0011'1111'1111;
    if (num != 2) mask ^= 0b10'1100'0000; // bits only exist on ch. 2
    TCR.u = val & mask;
    if (TCR.ICPE) {
        printf("\nWARN ICPE ENABLE!?");
    }
    switch (TCR.TPSC) {
        case 0: shift += 2; break; // 4
        case 1: shift += 4; break;
        case 2: shift += 6; break;
        case 3: shift += 8; break;
        case 4: shift += 10; break;
        default:
            printf("\nWARN INVALID SHIFT MODE FOR TIMER %d: %d", num, TCR.TPSC);
    }
    start();
}

void TMU_chan::stop() {
    //if (num == 2) printf("\nT%d STOP", num);
    if (!underflow_still_sch) return;
    cpu->scheduler->delete_if_exist(sch_id);

    TCNT = read_TCNT();
}

void TMU_chan::write_TSTR(bool new_val) {
    bool cur_val = underflow_still_sch != 0;
    if (cur_val == new_val) return;
    stop();
    if (new_val) start();
}

void TMU_chan::reset() {
    stop();
    TCOR = 0xFFFFFFFF;
    TCR.u = 0;
    TCNT = 0xFFFFFFFF;
}

void TMU::reset() {
    TOCR = TSTR = 0;
    for (auto & t : channels) t.reset();
}

void TMU_chan::write_TCNT(u32 val) {
    stop();
    TCNT = val;
    start();
}

void TMU_chan::underflow() {
    TCR.UNF = 1;
    update_IRQs();
    TCNT = TCOR;
    cycles_at_start = next_underflow_at;
    schedule_underflow();
}

static void sch_chan_underflow(void *ptr, u64 key, u64 clock, u32 jitter) {
    auto *th = static_cast<TMU *>(ptr);;
    th->channels[key].underflow();
}

void TMU_chan::schedule_underflow() {
    next_underflow_at += cycles_until_underflow();
    sch_id = cpu->scheduler->add_or_run_abs(next_underflow_at, num, &cpu->tmu, sch_chan_underflow, &underflow_still_sch);
}


u64 TMU::read(u32 addr, u8 sz, bool *success) {
    switch(addr | 0xF0000000) {
        case 0xFFD80004: // TSTR
            return TSTR;
        case 0xFFD80010: // TCR0
            return channels[0].TCR.u;
        case 0xFFD8001C: // TCR1
            return channels[1].TCR.u;
        case 0xFFD8000C: // TCNT0
            return channels[0].read_TCNT();
        case 0xFFD80018: // TCNT1
            return channels[1].read_TCNT();
        case 0xFFD80024: // TCNT2
            return channels[2].read_TCNT();
        case 0xFFD80028: // TCR2
            return channels[2].TCR.u;
            break;
    }
    *success = false;
    printf("\nTMU: MISS READ %08x", addr);
    return 0;
}

void TMU::write(u32 addr, u8 sz, u64 val, bool *success) {
    switch(addr | 0xF0000000) {
        case 0xFFD80000: // TOCR
            TOCR = val & 0xFF;
            return;
        case 0xFFD80004: // TSTR
            TSTR = val & 0xFF;
            channels[0].write_TSTR(getbit<0>(val));
            channels[1].write_TSTR(getbit<1>(val));
            channels[2].write_TSTR(getbit<2>(val));
            return;
        case 0xFFD80010: // TCR0
            channels[0].write_TCR(val);
            return;
        case 0xFFD8001C: // TCR1
            channels[1].write_TCR(val);
            return;
        case 0xFFD80028: // TCR2
            channels[2].write_TCR(val);
            return;
        case 0xFFD8000C: // TCNT0
            channels[0].write_TCNT(val);
            return;
        case 0xFFD80018: // TCNT1
            channels[1].write_TCNT(val);
            return;
        case 0xFFD80024: // TCNT2
            channels[2].write_TCNT(val);
            return;
        case 0xFFD80008: // TCOR0
            channels[0].TCOR = val;
            return;
        case 0xFFD80014: // TCOR1
            channels[1].TCOR = val;
            return;
        case 0xFFD80020: // TCOR2
            channels[2].TCOR = val;
            return;
    }
    printf("\nSH4: MISSED TIMER WRITE %08x", addr);
    *success = false;
}

}
