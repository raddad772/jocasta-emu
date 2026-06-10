//
// Created by . on 6/1/24.
//

#include <cassert>
#include <cstdlib>
#include "genesis_clock.h"

namespace genesis {
CLOCK::CLOCK(jsm::systems in_kind) : kind(in_kind) {
    reset();
    switch(in_kind) {
        {
            case jsm::systems::GENESIS_JAP:
            case jsm::systems::GENESIS_USA:
                ntsc();
                break;
            case jsm::systems::MEGADRIVE_PAL:
                pal();
                break;
            default:
                assert(1==2);
        }}
    timing.frame.cycles_per = timing.scanline.cycles_per * timing.frame.scanlines_per;
    // frames_per is now a double set per-region; derive integer cycles_per from it
    timing.second.cycles_per = (u32)((double)timing.frame.cycles_per * timing.second.frames_per);
}

void CLOCK::reset()
{
    master_cycle_count = 0;
    waitstates = 0;
    master_frame = 0;
    frames_since_reset = 0;
    current_back_buffer = 0;
    current_front_buffer = 1;
    delta = 0;
    mem_break = 0;
    vdp.hcount = vdp.vcount = 0;
    vdp.hblank_active = 0;
    vdp.vblank_active = 0;
    vdp.hblank_fast = 0;
    vdp.field = 0;
    vdp.frame_start = 0;
    psg.next_clock = 0;
    ym2612.next_clock = 0;
}


/*
System master clock rate: 53.693175 MHz (NTSC), 53.203424 MHz (PAL)[1]
Master clock cycles per frame: 896,040 (NTSC), 1,067,040 (PAL)
Master clock cycles per scanline: 3420[2] */
void CLOCK::ntsc()
{
    timing.scanline.cycles_per = 3420;
    timing.frame.scanlines_per = 262;
    // NTSC crystal 53.693175 MHz / (3420 * 262) ... ≈59.922 Hz
    timing.second.frames_per = 53693175.0 / (3420.0 * 262.0);

    vdp.bottom_rendered_line = 223; // or 240
    vdp.bottom_max_rendered_line = 223;
    vdp.vblank_on_line = 224;

    vdp.clock_divisor = 16; // ?
    psg.clock_divisor = 240; // 48 of SMS/GG * 5
    ym2612.clock_divisor = 1008;
}

void CLOCK::pal()
{
    timing.scanline.cycles_per = 3420;
    timing.frame.scanlines_per = 312;
    // PAL crystal 53.203424 MHz / (3420 * 312) ... ≈49.86 Hz
    timing.second.frames_per = 53203424.0 / (3420.0 * 312.0);

    vdp.bottom_rendered_line = 223; // or 240
    vdp.bottom_max_rendered_line = 239;
    vdp.vblank_on_line = 224;


    vdp.clock_divisor = 16; // ?
    psg.clock_divisor = 240; // 48 of SMS/GG * 5
    ym2612.clock_divisor = 1008;
}

}
