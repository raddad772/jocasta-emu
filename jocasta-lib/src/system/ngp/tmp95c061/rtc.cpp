#include <ctime>
#include "rtc.h"

namespace TMP95C061 {

static u8 to_bcd(int v) { return static_cast<u8>((((v / 10) << 4) | (v % 10))); }

RTC::RTC(scheduler_t *scheduler_in, u64 clocks_per_second_in)
{
    scheduler = scheduler_in;
    clocks_per_second = clocks_per_second_in;
    reset();
}

void RTC::reset()
{
    seed_from_host();
}

void RTC::schedule_first()
{
    schedule_next();
}

void RTC::seed_from_host()
{
    time_t raw = time(nullptr);
    struct tm lt = *localtime(&raw);
    second = to_bcd(lt.tm_sec % 60);
    minute = to_bcd(lt.tm_min);
    hour = to_bcd(lt.tm_hour);
    weekday = static_cast<u8>(lt.tm_wday);
    day = to_bcd(lt.tm_mday);
    month = to_bcd(lt.tm_mon + 1);
    year = to_bcd((lt.tm_year + 1900) % 100);
    enable = 1;
}

void RTC::schedule_next()
{
    if (!scheduler) return;
    if (still_sched) scheduler->delete_if_exist(sched_id);
    sched_id = scheduler->only_add_abs(static_cast<i64>((scheduler->current_time() + static_cast<i64>(clocks_per_second))),
                                       0, this, &on_tick, &still_sched);
}

void RTC::on_tick(void *ptr, u64 key, u64 clock, u32 jitter)
{
    auto *th = static_cast<RTC *>(ptr);
    th->tick_second();
    th->schedule_next();
}

u8 RTC::days_in_february() const { return (year & 3) == 0 ? 0x29 : 0x28; }

u8 RTC::days_in_month() const
{
    switch (month) {
        case 0x02: return days_in_february();
        case 0x04: case 0x06: case 0x09: case 0x11: return 0x30;
        default: return 0x31;
    }
}

void RTC::tick_second()
{
    if (!enable) return;

    second++;
    if ((second & 0x0f) > 0x09) second += 6;
    if (second <= 0x59) return;
    second = 0;

    minute++;
    if ((minute & 0x0f) > 0x09) minute += 6;
    if (minute <= 0x59) return;
    minute = 0;

    hour++;
    if ((hour & 0x0f) > 0x09) hour += 6;
    if (hour <= 0x23) return;
    hour = 0;

    weekday++;
    if (weekday >= 7) weekday = 0;

    day++;
    if ((day & 0x0f) > 0x09) day += 6;
    if (day <= days_in_month()) return;
    day = 1;

    month++;
    if ((month & 0x0f) > 0x09) month += 6;
    if (month <= 0x12) return;
    month = 1;

    year++;
    if ((year & 0x0f) > 0x09) year += 6;
    if (year > 0x99) year = 0;
}

u8 RTC::read(u8 addr)
{
    switch (addr) {
        case 0x90: return enable;
        case 0x91: return year;
        case 0x92: return month;
        case 0x93: return day;
        case 0x94: return hour;
        case 0x95: return minute;
        case 0x96: return second;
        case 0x97: return (weekday & 0x0f) | ((year & 3) << 4);
        default: return 0;
    }
}

void RTC::write(u8 addr, u8 val)
{
    switch (addr) {
        case 0x90: enable = val & 1; return;
        case 0x91: year = val; return;
        case 0x92: month = val; return;
        case 0x93: day = val; return;
        case 0x94: hour = val; return;
        case 0x95: minute = val; return;
        case 0x96: second = val; return;
        case 0x97: weekday = val & 0x0f; return;
        default: return;
    }
}

}
