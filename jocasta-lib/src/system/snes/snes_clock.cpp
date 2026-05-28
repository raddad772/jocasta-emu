//
// Created by . on 4/19/25.
//
#include <cstring>

#include "snes_clock.h"
namespace SNES {
void CLOCK::fill_timing_ntsc()
{
    timing.master_hz = 21477270;
    timing.apu_master_hz = 24576000;
    apu.ratio = 24576000.0 / 21477270.0;

    timing.line.master_cycles = 1364;
    timing.frame.master_cycles = timing.line.master_cycles * 262;
    // Use the actual crystal frequency, not frame_cycles * 60 (which is off by ~35 kHz)
    timing.second.master_cycles = timing.master_hz;  // 21,477,270 Hz

    apu.sample.stride    = static_cast<long double>(timing.second.master_cycles) / 32000.0;
    apu.cycle.stride     = static_cast<long double>(timing.second.master_cycles) / 1024000.0;
    apu.env.stride       = static_cast<long double>(timing.second.master_cycles) / 32000.0;
    apu.sample.pitch_ratio = static_cast<long double>(timing.second.master_cycles) / 131072.0;

    timing.line.hdma_setup_position = rev == 1 ? 12 + 8 : 12;
    timing.line.dram_refresh = rev == 1 ? 530 : 538;
    timing.line.hdma_position = 1104;
    timing.line.hblank_start = 277 * 4;
    timing.line.hblank_stop = 21 * 4;

    cpu.divider = 6;
}

CLOCK::CLOCK()
{

    fill_timing_ntsc();
}

void CLOCK::reset()
{
    master_cycle_count = 0;
    master_frame = 0;
    cpu.has = 0;
    ppu.has = 0;
    ppu.y = 0;
    ppu.field = 0;
    ppu.vblank_active = 0;
    ppu.hblank_active = 0;
    ppu.scanline_start = 0;
    apu.has = 0;
    apu.sample.next = 0;
    apu.cycle.next = 0;
}
}
