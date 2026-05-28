//
// Created by . on 12/4/24.
//

#pragma once

#include "helpers/int.h"

namespace GBA {
struct CLOCK {
    void reset();
    u64 master_cycle_count{};
    u64 master_frame{};

    struct {
        i32 x{}, y{};
        u64 scanline_start{};
        bool hblank_active{}, vblank_active{};
    } ppu{};
};

}
