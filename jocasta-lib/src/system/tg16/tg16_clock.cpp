//
// Created by . on 6/18/25.
//

#include <cstdio>
#include <cstring>
#include "tg16_clock.h"
#include "component/gpu/huc6260/huc6260.h"


namespace TG16 {
/*
The (NTSC) PC Engine is clocked with a master clock equal to six times the NTSC color burst (315/88 MHz), or approximately 21.47727 MHz.
This is divided into the following clocks:
CPU "high" clock speed = master clock / 3 (approx. 7.15909 MHz),
CPU "low" clock speed = master clock / 12 (approx. 1.78978 MHz),
Timer speed = master clock / 3072 (approx. 6.991 KHz),
VCE pixel clocks:
"10MHz" = master clock / 2,
"7MHz" = master clock / 3,
"5MHz" = master clock / 4.
 */

CLOCK::CLOCK()
{
    reset();
}

void CLOCK::reset()
{
    timing.frame.lines = 262;

    u64 per_line = HUC6260::CYCLE_PER_LINE; // = 1364
    u64 per_frame = per_line * timing.frame.lines;

    timing.scanline.cycles = per_line;
    timing.frame.cycles = per_frame;
    // TG16 NTSC crystal = 6 × (315/88 MHz) = 21,477,272 Hz.
    // Do NOT derive this as per_frame * 60 (≈21,442,080) — that's 0.16% low
    // and causes the HuC6280 timer to fire at the wrong cadence.
    timing.second.cycles = 21477272;
    timing.second.frames = (u32)(timing.second.cycles / per_frame); // ≈60

    next.cpu = 3;
    next.vce = 4;
    next.timer = (u32)(timing.second.cycles / 6992); // ≈3072 master cycles per timer tick
}

}