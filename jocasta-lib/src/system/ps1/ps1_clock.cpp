//
// Created by . on 3/4/25.
//

#include <cstring>

#include "ps1_bus.h"
#include "ps1_clock.h"

#define PS1_CYCLES_PER_SECOND_NTSC 33868800
#define PS1_CYCLES_PER_SECOND PS1_CYCLES_PER_SECOND_NTSC
#define PS1_FPS_NTSC 60
#define PS1_FPS PS1_FPS_NTSC
#define PS1_CYCLES_PER_FRAME (PS1_CYCLES_PER_SECOND / PS1_FPS)
#define PS1_VBLANK_CYCLE_IN_FRAME ((PS1_CYCLES_PER_FRAME*85)/100)
#define PS1_CYCLES_INSIDE_VBLANK_IN_FRAME (PS1_CYCLES_PER_FRAME - PS1_VBLANK_CYCLE_IN_FRAME)
#define PS1_CYCLES_PER_FRAME_PAL 677376

#define CPU_HZ timing.cpu.hz
#define GPU_HZ timing.gpu.hz
#define FPS timing.fps
#define FRAME_LINES timing.frame.lines

namespace PS1 {

CLOCK::CLOCK(bool is_ntsc_in) {
    init(is_ntsc_in);
}

void CLOCK::finish_timing()
{
    // Keep scheduled frame length and advertised FPS in agreement.
    timing.frame.cycles = static_cast<u64>((static_cast<double>(CPU_HZ) / static_cast<double>(FPS)) + 0.5);
    FPS = static_cast<float>(static_cast<double>(CPU_HZ) / static_cast<double>(timing.frame.cycles));
    timing.scanline.cycles = timing.frame.cycles / FRAME_LINES;
    timing.scanline.hblank.cycles = static_cast<u64>(timing.scanline.hblank.ratio * static_cast<float>(timing.scanline.cycles));
    timing.scanline.hblank.start_on_cycle = timing.scanline.cycles - timing.scanline.hblank.cycles;
    // 262-243
    timing.frame.vblank.cycles = static_cast<u64>(timing.frame.vblank.ratio * static_cast<float>(timing.frame.cycles));
    timing.frame.vblank.start_on_line = (timing.frame.cycles - timing.frame.vblank.cycles) / timing.scanline.cycles;
}


void CLOCK::setup_ntsc()
{
    // 3413
    CPU_HZ = 33868800;
    GPU_HZ = 53693175;
    // NTSC GPU 53.693175 MHz / 3413 pixels/line = 15,734.27 Hz H; / 263 lines ... 59.826 Hz
    FPS = 59.826f;
    FRAME_LINES = 263;
    // In the NTSC television standard, horizontal blanking occupies 10.9 μs out of every 63.6 μs scan line (17.2%).
    timing.scanline.hblank.ratio = 10.9f/63.6f;
    timing.frame.vblank.ratio = 20.0f/263.0f;
    finish_timing();
}

void CLOCK::setup_pal()
{
    CPU_HZ = 33868800;
    GPU_HZ = 53203425;
    // PAL H-freq 15625 Hz / 314 lines ... 49.761 Hz
    FPS = 49.761f;
    FRAME_LINES = 314;
    timing.scanline.hblank.ratio = 12.0f/64.0f;
    timing.frame.vblank.ratio = 70.0f/314.0f;
    finish_timing();
    // In PAL, it occupies 12 μs out of every 64 μs scan line (18.8%).
}

void CLOCK::init(bool is_ntsc_in)
{
    is_ntsc = is_ntsc_in;
    if (is_ntsc) setup_ntsc();
    else setup_pal();
}

void CLOCK::reset()
{
    init(is_ntsc);
}
}
