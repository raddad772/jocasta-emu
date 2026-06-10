#include "apu.h"

namespace NGP {

static inline float i16_to_float(i16 v)
{
    return ((static_cast<float>(static_cast<i32>(v) + 32768) / 65535.0f) * 2.0f) - 1.0f;
}

APU::APU(scheduler_t *scheduler_in, u64 master_clock_freq, u32 psg_divisor_in)
    : scheduler(scheduler_in),
      psg_divisor(psg_divisor_in),
      sample_rate(master_clock_freq / psg_divisor_in)
{
    reset();
}

void APU::reset()
{
    psg.reset();
    dac_l = dac_r = 0;
    psg_enabled = false;
}

void APU::schedule_first()
{
    if (!scheduler) return;
    schedule();
}

void APU::schedule() {
    if (still_sched) scheduler->delete_if_exist(sched_id);
    sched_id = scheduler->only_add_abs(static_cast<i64>((scheduler->current_time() + static_cast<i64>(psg_divisor))), 0, this, &on_tick, &still_sched);

}

void APU::on_tick(void *ptr, u64 key, u64 clock, u32 jitter)
{
    auto *th = static_cast<APU *>(ptr);
    th->tick();
    th->schedule();
}

void APU::tick()
{
    psg.cycle();

    float l, r;
    if (psg_enabled) {
        i16 il, ir;
        psg.mix_stereo(il, ir, false);
        l = i16_to_float(il);
        r = i16_to_float(ir);
    } else {
        l = static_cast<float>(dac_l) / 255.0f;
        r = static_cast<float>(dac_r) / 255.0f;
    }

    audio_ring->push(l, r);
}

}
