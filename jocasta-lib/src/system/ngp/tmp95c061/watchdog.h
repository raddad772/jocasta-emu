#pragma once

#include "helpers/int.h"
#include "helpers/scheduler.h"

namespace TMP95C061 {

struct WDT {
    WDT(scheduler_t *scheduler_in, void *irq_ptr_in, void (*raise_intwd_in)(void *ptr));
    void reset();

    u8 read(u8 addr);
    void write(u8 addr, u8 val);

    static void on_overflow(void *ptr, u64 key, u64 clock, u32 jitter);

    scheduler_t *scheduler{};
    void *irq_ptr{};
    void (*raise_intwd)(void *ptr){};

    u8 WDMOD{};
    bool enabled{};
    u64 sched_id{};
    u32 still_sched{};

private:
    static constexpr u32 MASTER_PER_FC = 16;
    u64 period_master() const;
    void schedule_next();
    void unschedule();
};

}
