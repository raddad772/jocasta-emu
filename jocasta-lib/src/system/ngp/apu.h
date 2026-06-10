#pragma once

#include "helpers/int.h"
#include "helpers/scheduler.h"
#include "helpers/audio_ring.h"
#include "component/audio/sn76489/sn76489.h"

namespace NGP {

struct APU {
    APU(scheduler_t *scheduler_in, u64 master_clock_freq, u32 psg_divisor_in);
    void reset();
    void schedule_first();

    void set_ring(audio_output_ring *ring) { audio_ring = ring; }
    u64 source_sample_rate() const { return sample_rate; }

    void write_left(u8 v) { psg.write_left(v); }
    void write_right(u8 v) { psg.write_right(v); }
    void write_dac_left(u8 v) { dac_l = v; }
    void write_dac_right(u8 v) { dac_r = v; }
    void set_psg_enable(bool e){ psg_enabled = e; }

    static void on_tick(void *ptr, u64 key, u64 clock, u32 jitter);
    void tick();

    scheduler_t *scheduler{};
    u32 psg_divisor{};
    u64 sample_rate{};
    audio_output_ring *audio_ring{};

    T6W28 psg{};
    u8 dac_l{}, dac_r{};
    bool psg_enabled{};

    u64 sched_id{};
    u32 still_sched{};

private:
    void schedule();
};

}
