#pragma once

#include "helpers/int.h"
#include "helpers/scheduler.h"

namespace TMP95C061 {

struct RTC {
    RTC(scheduler_t *scheduler_in, u64 clocks_per_second_in);
    void reset();
    void schedule_first();

    u8 read(u8 addr);
    void write(u8 addr, u8 val);

    static void on_tick(void *ptr, u64 key, u64 clock, u32 jitter);

    scheduler_t *scheduler{};
    u64 clocks_per_second{};
    u64 sched_id{};
    u32 still_sched{};

    u8 enable{};
    u8 second{}, minute{}, hour{}, weekday{}, day{}, month{}, year{};

private:
    void seed_from_host();
    void tick_second();
    u8 days_in_month() const;
    u8 days_in_february() const;
    void schedule_next();
};

}
