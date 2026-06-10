#include "adc.h"

namespace TMP95C061 {

ADC::ADC(scheduler_t *scheduler_in, void *irq_ptr_in, void (*raise_intad_in)(void *ptr))
{
    scheduler = scheduler_in;
    irq_ptr = irq_ptr_in;
    raise_intad = raise_intad_in;
    reset();
}

void ADC::reset()
{
    if (still_sched && scheduler) scheduler->delete_if_exist(sched_id);
    still_sched = 0;
    for (auto &r : result) r = 1023;
    channel = speed = scan = repeat = busy = end = 0;
}

void ADC::schedule_next()
{
    if (!scheduler) return;
    if (still_sched) scheduler->delete_if_exist(sched_id);
    sched_id = scheduler->only_add_abs(static_cast<i64>((scheduler->current_time() + static_cast<i64>(period_master()))),
                                       0, this, &on_complete, &still_sched);
}

void ADC::on_complete(void *ptr, u64 , u64 , u32 )
{
    auto *th = static_cast<ADC *>(ptr);
    th->result[th->channel] = 1023;
    th->end = 1;
    if (th->raise_intad) th->raise_intad(th->irq_ptr);
    if (th->repeat) th->schedule_next();
    else th->busy = 0;
}

u8 ADC::read(u8 addr)
{
    switch (addr) {
        case 0x60: end = 0; return static_cast<u8>(((result[0] & 3) << 6));
        case 0x61: end = 0; return static_cast<u8>((result[0] >> 2));
        case 0x62: end = 0; return static_cast<u8>(((result[1] & 3) << 6));
        case 0x63: end = 0; return static_cast<u8>((result[1] >> 2));
        case 0x64: end = 0; return static_cast<u8>(((result[2] & 3) << 6));
        case 0x65: end = 0; return static_cast<u8>((result[2] >> 2));
        case 0x66: end = 0; return static_cast<u8>(((result[3] & 3) << 6));
        case 0x67: end = 0; return static_cast<u8>((result[3] >> 2));
        case 0x6d:
            return static_cast<u8>((channel | (speed << 3) | (scan << 4) | (repeat << 5) | (busy << 6) | (end << 7)));
        default: return 0;
    }
}

void ADC::write(u8 addr, u8 val)
{
    if (addr != 0x6d) return;
    channel = val & 3;
    u8 do_start = (val >> 2) & 1;
    speed = (val >> 3) & 1;
    scan = (val >> 4) & 1;
    repeat = (val >> 5) & 1;
    if (do_start) {
        busy = 1;
        schedule_next();
    }
}

}
