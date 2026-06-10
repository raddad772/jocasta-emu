//
// Created by . on 12/4/24.
//

#include "gba_clock.h"

namespace GBA {
void CLOCK::reset()
{
    master_cycle_count = 0;
    master_frame = 0;
    ppu.x = 0;
    ppu.y = 0;
    ppu.scanline_start = 0;
    ppu.hblank_active = false;
    ppu.vblank_active = false;
}

}
