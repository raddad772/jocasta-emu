//
// Created by . on 8/30/24.
//

#include <cstdlib>
#include <cassert>

#include "apple2.h"
#include "apple2_bus.h"
#include "iou.h"
#include "mmu.h"

namespace apple2 {

void core::CPU_cycle()
{
    cpu.cycle();
    if (!cpu.pins.RW) {
        cpu.pins.D = mmu.cpu_bus_read(cpu.pins.Addr, cpu.pins.D, 1);
    }
    else {
        mmu.cpu_bus_write(cpu.pins.Addr, cpu.pins.D);
    }
}

void core::reset()
{
    cpu.reset();
    iou.reset();
    mmu.reset();
    for (auto& s : slots) if (s) s->reset();
    clock.cpu_divisor = 14;
    clock.iou_divisor = 14;
    clock.long_cycle_counter = 0;
    clock.cpu_adder = clock.iou_adder = 0;
}

void core::cycle()
{
    clock.cpu_adder++;
    //clock.iou_adder++;
    if (clock.cpu_adder >= clock.cpu_divisor) {
        clock.cpu_adder = 0;
        clock.long_cycle_counter = (clock.long_cycle_counter + 1) % 65;
        if (clock.long_cycle_counter == 64)
            clock.cpu_divisor = 16;
        else
            clock.cpu_divisor = 14;


        CPU_cycle();
        iou.cycle();
        float s = iou.io.SPKR ? 1.0f : 0.0f;
        iou.audio_ring->push(s, s);
    }
    for (auto& s : slots) if (s) s->cycle();
    clock.master_cycles++;
}

static u32 CPU_read_trace(void *ptr, u32 addr)
{
    auto *th = static_cast<core *>(ptr);
    return th->mmu.cpu_bus_read(addr, 0, false);
}

void core::configure_slot(u32 slot_num, const char* card_name)
{
    if (slot_num >= NUM_SLOTS) return;
    switch (slot::str_to_type(card_name)) {
        case slot::DISK2:
            slots[slot_num] = std::make_unique<slot::disk2>(this, slot_num);
            break;
        case slot::MOCKINGBOARD_B:
            // Not yet implemented — leave slot empty
            slots[slot_num] = nullptr;
            break;
        default:
            slots[slot_num] = nullptr;
            break;
    }
}

core::core(const system_config& cfg) {
    has.load_BIOS = true;
    has.set_audio_ring = true;
    has.max_loaded_files = 0;
    has.max_loaded_folders = 0;
    has.save_state = false;
    described_inputs = false;

    mmu.RAM.allocate(64 * 1024);
    mmu.AUX_RAM.allocate(64 * 1024);

    snprintf(label, sizeof(label), "Apple IIe");

    // setup tracing reads
    jsm_debug_read_trace a;
    a.ptr = this;
    a.read_trace = &CPU_read_trace;
    cpu.setup_tracing(&a, &clock.master_cycles);
    cpu.reset();

    // Install slot cards from config before describe_io() runs
    for (u32 i = 0; i < NUM_SLOTS; i++) {
        if (cfg.slots[i][0])
            configure_slot(i, cfg.slots[i]);
    }
}

}
