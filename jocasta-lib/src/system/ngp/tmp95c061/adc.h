#pragma once

#include "helpers/int.h"
#include "helpers/scheduler.h"

namespace TMP95C061 {

struct ADC {
    ADC(scheduler_t *scheduler_in, void *irq_ptr_in, void (*raise_intad_in)(void *ptr));
    void reset();

    u8 read(u8 addr);
    void write(u8 addr, u8 val);

    static void on_complete(void *ptr, u64 key, u64 clock, u32 jitter);

    scheduler_t *scheduler{};
    void *irq_ptr{};
    void (*raise_intad)(void *ptr){};

    u16 result[4]{1023, 1023, 1023, 1023};
    u8 channel{}, speed{}, scan{}, repeat{}, busy{}, end{};
    u64 sched_id{};
    u32 still_sched{};

private:
    u64 period_master() const { return speed ? 320 : 160; }
    void schedule_next();
};

}
