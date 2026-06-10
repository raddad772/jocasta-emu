//
// Created by . on 6/11/25.
//

#include "atari2600_bus.h"
#include "atari2600_debugger.h"

namespace atari2600 {

static u32 read_trace(void *ptr, u32 addr)
{
    auto *th = static_cast<core *>(ptr);
    u8 v = 0;
    CPU_bus e;
    e.Addr.u = addr;
    if ((e.Addr.a9 && e.Addr.a7) || e.Addr.a7) // RIOT, RIOT RAM
        th->riot.bus_cycle<true>(e.Addr.u, &v, false);
    else if (e.Addr.a12) // cart. a12=1
        th->cart.bus_cycle<true>(e.Addr.u, &v, false);
    return v;
}


core::core() {
    //struct atari2600* this = (atari2600*)malloc(sizeof(atari2600));
    has.load_BIOS = false;
    has.max_loaded_files = 0;
    has.max_loaded_folders = 0;
    has.save_state = false;

    snprintf(label, sizeof(label), "Atari 2600");

    case_switches.reset = false;
    case_switches.select = false;
    case_switches.p0_difficulty = 0;
    case_switches.p1_difficulty = 0;
    case_switches.color = true;

    tia.frames_since_restart = 0;
    tia.timing.vblank_in_lines = 40; // 40 NTSC, 48 PAL
    tia.timing.display_line_start = 40;
    tia.timing.vblank_out_start = 192 + 40; // // 192 NTSC, 22? PAL
    tia.timing.vblank_out_lines = 30; // 30 NTSC, 36 PAL

    described_inputs = 0;

    cycles_left = 0;
    display_enabled = 1;

    jsm_debug_read_trace dt;
    dt.read_trace = &read_trace;
    dt.ptr = static_cast<void *>(this);

    cpu.setup_tracing(&dt, &master_clock);

    reset();

}

void core::reset() {
    cart.reset();
    cpu.reset();
    tia.reset();
    riot.reset();

    this->tia.master_frame = 0;
    this->master_clock = 0;
}

void core::CPU_run_cycle()
{
    if (tia.cpu_RDY) return; // CPU is halted until next scanline

    cpu.cycle();

    cpu_bus.Addr.u = cpu.pins.Addr & 0x1FFF;
    cpu_bus.RW = cpu.pins.RW;
    cpu_bus.D = cpu.pins.D;

    if (cpu_bus.RW) { // Write!
        dbgloglog(A26_CAT_CPU_WRITE, DBGLS_TRACE, "%04x   (write) %02x", cpu_bus.Addr.u, cpu_bus.D);
    }
    if (cpu_bus.Addr.a12) // cart. a12=1
        cart.bus_cycle<false>(cpu_bus.Addr.u, &cpu_bus.D, cpu_bus.RW);
    else if ((cpu_bus.Addr.a9 && cpu_bus.Addr.a7) || cpu_bus.Addr.a7) // RIOT, RIOT RAM
        riot.bus_cycle<false>(cpu_bus.Addr.u, &cpu_bus.D, cpu_bus.RW);
    else if (cpu_bus.Addr.a9 == 0) { // TIA
        tia.bus_cycle(cpu_bus.Addr.u, &cpu_bus.D, cpu_bus.RW);
    }
    else {
        printf("\nMISSED ADDR2 %04x %d %d", cpu_bus.Addr.u, cpu_bus.Addr.a7, cpu_bus.Addr.a9);
    }
    cpu.pins.D = cpu_bus.D;
    if (!cpu_bus.RW) {
        dbgloglog(A26_CAT_CPU_WRITE, DBGLS_TRACE, "%04x   (read)  %02x", cpu_bus.Addr.u, cpu_bus.D);
    }
}
}
