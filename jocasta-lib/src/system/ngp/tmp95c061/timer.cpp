#include "timer.h"

namespace TMP95C061 {

TIMER::TIMER(scheduler_t *scheduler_in, void *irq_ptr_in,
                   void (*raise_irq_in)(void *ptr, u32 timer_index))
{
    scheduler = scheduler_in;
    irq_ptr = irq_ptr_in;
    raise_irq = raise_irq_in;
    reset();
}

void TIMER::reset()
{
    for (u32 n = 0; n < 4; n++) {
        deschedule(n);
        t[n] = timer{};
    }
    T01MOD = T23MOD = TFFCR = TRUN = TRDC = 0;
}

u8 TIMER::pair_mode(u32 pair) const { return (pair == 0 ? T01MOD : T23MOD) >> 6 & 3; }
bool TIMER::is_16bit(u32 pair) const { return pair_mode(pair) == 1; }

u32 TIMER::clk_sel_of(u32 n) const
{
    switch (n) {
        case 0: return T01MOD & 3;
        case 1: return (T01MOD >> 2) & 3;
        case 2: return T23MOD & 3;
        default: return (T23MOD >> 2) & 3;
    }
}

bool TIMER::is_external(u32 n) const { return n == 0 && (T01MOD & 3) == 0; }

u32 TIMER::clk_period_master(u32 n) const
{
    u32 sel = clk_sel_of(n);
    switch (n) {
        case 0: return sel == 1 ? PHI_T1 : sel == 2 ? PHI_T4 : sel == 3 ? PHI_T16 : 0;
        case 1: return sel == 1 ? PHI_T1 : sel == 2 ? PHI_T16 : sel == 3 ? PHI_T256 : 0;
        case 2: return sel == 1 ? PHI_T1 : sel == 2 ? PHI_T4 : sel == 3 ? PHI_T16 : 0;
        default:return sel == 1 ? PHI_T1 : sel == 2 ? PHI_T16 : sel == 3 ? PHI_T256 : 0;
    }
}

u32 TIMER::period_ticks(u32 n) const { return t[n].treg ? t[n].treg : 256; }

void TIMER::deschedule(u32 n)
{
    if (t[n].still_sched) scheduler->delete_if_exist(t[n].sched_id);
    t[n].still_sched = 0;
}

void TIMER::reschedule(u32 n)
{
    deschedule(n);
    if (!t[n].run || !scheduler) return;

    const u32 pair = n >> 1;
    u64 ticks, period;

    if (is_16bit(pair)) {
        if (n & 1) return;
        if (is_external(n)) return;
        u32 cp = clk_period_master(n);
        if (!cp) return;
        u32 cmp16 = (static_cast<u32>(t[n | 1].treg) << 8) | t[n].treg;
        ticks = cmp16 ? cmp16 : 65536;
        period = ticks * cp;
    } else {
        if (is_external(n)) return;
        u32 cp = clk_period_master(n);
        if (!cp) return;
        period = static_cast<u64>(period_ticks(n)) * cp;
    }

    t[n].sched_id = scheduler->only_add_abs(static_cast<i64>((scheduler->current_time() + static_cast<i64>(period))),
                                            n, this, &on_match, &t[n].still_sched);
}

void TIMER::on_match(void *ptr, u64 key, u64 clock, u32 jitter)
{
    auto *th = static_cast<TIMER *>(ptr);
    th->on_compare_match(static_cast<u32>(key));
    th->reschedule(static_cast<u32>(key));
}

void TIMER::set_ff(u32 pair, bool level)
{
    bool &ff = t[pair << 1].ff;
    if (ff == level) return;
    ff = level;
    if (ff_out) ff_out(ff_out_ptr, pair, level);
}

void TIMER::toggle_ff(u32 pair)
{
    u32 ie = pair == 0 ? (TFFCR >> 1) & 1 : (TFFCR >> 5) & 1;
    if (ie) set_ff(pair, !t[pair << 1].ff);
}

void TIMER::latch_treg(u32 n)
{
    if (n == 0 && (TRDC & 1)) t[0].treg = t[0].buffer;
    if (n == 2 && (TRDC & 2)) t[2].treg = t[2].buffer;
}

void TIMER::on_compare_match(u32 n)
{
    const u32 pair = n >> 1;
    const u8 mode = pair_mode(pair);

    if (mode == 1 && !(n & 1)) {
        raise_irq(irq_ptr, (pair << 1) | 1);
        toggle_ff(pair);
        latch_treg(pair << 1);
        return;
    }

    raise_irq(irq_ptr, n);
    t[n].counter = 0;
    toggle_ff(pair);
    latch_treg(n);

    const u32 even = pair << 1, odd = even | 1;
    if (mode == 0 && n == even && clk_sel_of(odd) == 0 && t[odd].run) {
        if (++t[odd].counter >= period_ticks(odd)) on_compare_match(odd);
    }
}

void TIMER::recompute_all()
{
    for (u32 n = 0; n < 4; n++) {
        t[n].run = (TRUN >> n) & 1;
        reschedule(n);
    }
}

void TIMER::ext_tick_ti0()
{
    if (!t[0].run || (T01MOD & 3) != 0) return;
    if (++t[0].counter >= period_ticks(0)) on_compare_match(0);
}

u8 TIMER::read(u8 addr)
{
    switch (addr) {
        case 0x20: return TRUN;
        case 0x24: return T01MOD;
        case 0x25: return TFFCR | 0xCC;
        case 0x28: return T23MOD;
        case 0x29: return TRDC;
        default: return 0;
    }
}

void TIMER::write(u8 addr, u8 val)
{
    switch (addr) {
        case 0x20: TRUN = val; recompute_all(); return;
        case 0x22: t[0].buffer = val; if (!(TRDC & 1)) t[0].treg = val; recompute_all(); return;
        case 0x23: t[1].treg = val; recompute_all(); return;
        case 0x24: T01MOD = val; recompute_all(); return;
        case 0x25: {
            TFFCR = val;
            u8 c0 = (val >> 2) & 3;
            if (c0 == 0) set_ff(0, !t[0].ff); else if (c0 == 1) set_ff(0, true); else if (c0 == 2) set_ff(0, false);
            u8 c3 = (val >> 6) & 3;
            if (c3 == 0) set_ff(1, !t[2].ff); else if (c3 == 1) set_ff(1, true); else if (c3 == 2) set_ff(1, false);
            return;
        }
        case 0x26: t[2].buffer = val; if (!(TRDC & 2)) t[2].treg = val; recompute_all(); return;
        case 0x27: t[3].treg = val; recompute_all(); return;
        case 0x28: T23MOD = val; recompute_all(); return;
        case 0x29: TRDC = val; return;
        default: return;
    }
}

}
