#include "watchdog.h"

namespace TMP95C061 {

WDT::WDT(scheduler_t *scheduler_in, void *irq_ptr_in, void (*raise_intwd_in)(void *ptr))
{
    scheduler = scheduler_in;
    irq_ptr = irq_ptr_in;
    raise_intwd = raise_intwd_in;
    reset();
}

void WDT::reset()
{
    unschedule();
    WDMOD = 0;
    enabled = false;
}

u64 WDT::period_master() const
{
    const u32 wdtp = (WDMOD >> 5) & 3;
    return (1ull << (16 + 2 * wdtp)) * MASTER_PER_FC;
}

void WDT::unschedule()
{
    if (still_sched) scheduler->delete_if_exist(sched_id);
    still_sched = 0;
}

void WDT::schedule_next()
{
    unschedule();
    if (!enabled || !scheduler) return;
    sched_id = scheduler->only_add_abs(static_cast<i64>((scheduler->current_time() + static_cast<i64>(period_master()))),
                                       0, this, &on_overflow, &still_sched);
}

void WDT::on_overflow(void *ptr, u64 key, u64 clock, u32 jitter)
{
    auto *th = static_cast<WDT *>(ptr);
    if (th->raise_intwd) th->raise_intwd(th->irq_ptr);
    th->schedule_next();
}

u8 WDT::read(u8 addr)
{
    return addr == 0x6e ? WDMOD : 0x00;
}

void WDT::write(u8 addr, u8 val)
{
    if (addr == 0x6e) {
        WDMOD = val;
        enabled = (val >> 7) & 1;
        schedule_next();
        return;
    }
    if (addr == 0x6f) {
        if (val == 0x4e) schedule_next();
        else if (val == 0xb1) { enabled = false; unschedule(); }
        return;
    }
}

}
