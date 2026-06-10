//
// Created by Dave on 2/6/2024.
//

#pragma once

#include "helpers/int.h"
#include "../../nes_clock.h"

namespace NES {

enum a12_r {
    A12_NOTHING,
    A12_RISE,
    A12_FALL
};

struct CLOCK;
struct a12_watcher {
    explicit a12_watcher(CLOCK *clock) : clock(clock)
    { delay = clock->timing.ppu_divisor * 3; }

    i64 cycles_down{};
    i64 last_cycle{};
    u32 delay{};

    CLOCK* clock{};
    a12_watcher() {};
    a12_r update(u32 addr);
};

} // namespace NES
