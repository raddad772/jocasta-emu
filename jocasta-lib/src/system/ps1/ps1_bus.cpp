//
// Created by . on 2/11/25.
//

#include <cstdio>

#include "helpers/multisize_memaccess.cpp"
#include "helpers/setbits.h"

#include "ps1_bus.h"
#include "ps1_dma.h"
#include "ps1_debugger.h"
#include "peripheral/ps1_sio.h"
#include "peripheral/ps1_digital_pad.h"
#include <cassert>

#define deKSEG(addr) ((addr) & 0x1FFFFFFF)

namespace PS1 {
static u32 read_trace_cpu(void *ptr, u32 addr, u8 sz)
{
    auto *th = static_cast<core *>(ptr);
    return th->mainbus_read(addr, sz);
}

static u32 mainbus_peekins(void *ptr, u32 addr)
{
    auto *th = static_cast<core *>(ptr);
    return th->mainbus_read(addr, 4);
}

template<bool do_debug>
static u32 cpu_fetch(void *ptr, u32 addr)
{
    return static_cast<core *>(ptr)->mainbus_read<do_debug>(addr, 4);
}

template<bool do_debug>
static void run_block(void *bound_ptr, u64 num, u64 current_clock, u32 jitter)
{
    auto *th = static_cast<core *>(bound_ptr);
    th->cycles_left += static_cast<i64>(num);
    th->cpu.check_IRQ<do_debug>();
    th->cpu.cycle<do_debug>(num);
}

template<bool do_debug>
static u32 cpu_read(void *ptr, u32 addr, u8 sz)
{
    return static_cast<core *>(ptr)->mainbus_read<do_debug>(addr, sz);
}

template<bool do_debug>
static void cpu_write(void *ptr, u32 addr, u8 sz, u32 val)
{
    static_cast<core *>(ptr)->mainbus_write<do_debug>(addr, sz, val);
}

template<bool do_debug>
static u32 cpu_read24_unaligned(void *ptr, u32 addr)
{
    auto *th = static_cast<core *>(ptr);
    if (addr & 1)
        return (th->mainbus_read<do_debug>(addr, 1) & 0xFF) | ((th->mainbus_read<do_debug>(addr + 1, 2) & 0xFFFF) << 8);
    return (th->mainbus_read<do_debug>(addr, 2) & 0xFFFF) | ((th->mainbus_read<do_debug>(addr + 2, 1) & 0xFF) << 16);
}

template<bool do_debug>
static void cpu_write24_unaligned(void *ptr, u32 addr, u32 val)
{
    auto *th = static_cast<core *>(ptr);
    if (addr & 1) {
        th->mainbus_write<do_debug>(addr, 1, val);
        th->mainbus_write<do_debug>(addr + 1, 2, val >> 8);
    } else {
        th->mainbus_write<do_debug>(addr, 2, val);
        th->mainbus_write<do_debug>(addr + 2, 1, val >> 16);
    }
}

static void update_SR(void *ptr, R3000::core *cpucore, u32 val)
{
    auto *th = static_cast<PS1::core *>(ptr);
    th->mem.cache_isolated = (val & 0x10000) == 0x10000;
    //printf("\nNew SR: %04x", core->regs.COP0[12] & 0xFFFF);
}

static void set_cdrom_irq_level(void *ptr, u32 lvl) {
    auto *th = static_cast<PS1::core *>(ptr);
    th->set_irq(IRQ_CDROM, lvl);
}
// TODO: add in audio to scheduler, as well as audio sampling and output
    // TODO: finish up SPU
core::core() :
    IRQ_multiplexer(15),
    clock(true),
    scheduler(&clock.master_cycle_count),
    cpu(&clock.master_cycle_count, &clock.waitstates, &scheduler, &IRQ_multiplexer),
    sio0(this),
    cdrom(this),
    mdec(this),
    gpu(this),
    spu(this),
    dma(this),
    io{ false, SIO::digital_gamepad(this), SIO::memcard(this) }
    {
    setup_mmap();
    for (u32 i = 0; i < 3; i++) {
        timers[i].num = i;
        timers[i].bus = this;
    }
    dma.control = 0x7654321;
    for (u32 i = 0; i < 7; i++) {
        dma.channels[i].num = i;
        dma.channels[i].step = D_increment;
        dma.channels[i].sync = D_manual;
        dma.channels[i].direction = D_to_ram;
    }
    for (u32 i = 0; i <24; i++) {
        spu.voices[i].reset(this, i);
    }
    has.load_BIOS = true;
    has.max_loaded_files = 0;
    has.max_loaded_folders = 0;
    has.save_state = false;
    has.set_audio_ring = true;

    scheduler.max_block_size = 2;

    scheduler.run.func[0] = &run_block<false>;
    scheduler.run.func[1] = &run_block<true>;
    scheduler.run.ptr = this;

    setup_IRQs();

    cpu.read_ptr = this;
    cpu.write_ptr = this;
    cpu.peek_ins = &mainbus_peekins;
    cpu.peek_ins_ptr = this;
    cpu.read = &cpu_read<false>;
    cpu.read_debug = &cpu_read<true>;
    cpu.write = &cpu_write<false>;
    cpu.write_debug = &cpu_write<true>;
    cpu.read24_unaligned = &cpu_read24_unaligned<false>;
    cpu.read24_unaligned_debug = &cpu_read24_unaligned<true>;
    cpu.write24_unaligned = &cpu_write24_unaligned<false>;
    cpu.write24_unaligned_debug = &cpu_write24_unaligned<true>;
    cpu.fetch_ins_ptr = this;
    cpu.fetch_ins = &cpu_fetch<false>;
    cpu.fetch_ins_debug = &cpu_fetch<true>;
    cpu.update_sr_ptr = this;
    cpu.update_sr = &update_SR;

    snprintf(label, sizeof(label), "PlayStation");
    jsm_debug_read_trace dt;
    dt.read_trace_arm = &read_trace_cpu;
    dt.ptr = this;
    cpu.setup_tracing(dt, &clock.master_cycle_count, 1);

    jsm.described_inputs = false;
    jsm.cycles_left = 0;

    cdrom.set_irq_lvl = &set_cdrom_irq_level;
    cdrom.set_irq_ptr = this;
    IRQ_multiplexer.clock = &clock.master_cycle_count;
}

static constexpr u32 alignmask[5] = { 0, 0xFFFFFFFF, 0xFFFFFFFE, 0, 0xFFFFFFFC };

void core::set_irq(IRQ from, u32 level)
{
    u32 old_if = IRQ_multiplexer.IF;

    IRQ_multiplexer.set_level(from, level);
    if (old_if != IRQ_multiplexer.IF) {
        dbgloglog(PS1D_BUS_IRQS, DBGLS_INFO, "IRQ %d (%s) set to %d", from, IRQnames[from], level);
    }
    cpu.update_I_STAT();
}

u64 core::clock_current() const
{
    return clock.master_cycle_count + clock.waitstates;
}
}
