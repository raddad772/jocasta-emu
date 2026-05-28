//
// Created by . on 4/21/26.
//

#include "helpers/multisize_memaccess.cpp"
#include "helpers/setbits.h"

#include "ps1_bus.h"
#include "peripheral/ps1_sio.h"

namespace PS1 {
#define DEFAULT_WAITSTATES 0

u32 core::mainbus_read(u32 addr, u8 sz)
{
    clock.waitstates += DEFAULT_WAITSTATES;
    addr &= 0x1FFF'FFFF;
    u32 pg = getbits<21, 28>(addr);
    auto &t = mem.pages[1][sz >> 1][pg];
    return t.read(t.ptr, addr);

}

void core::mainbus_write(u32 addr, u8 sz, u32 val)
{
    clock.waitstates += DEFAULT_WAITSTATES;
    addr &= 0x1FFF'FFFF;
    if (mem.cache_isolated) return;
    auto &t = mem.pages[1][sz >> 1][getbits<21, 28>(addr)];
    t.write(t.ptr, addr, val);
}

void core::mmap_range(u32 range_start, u32 range_end, u32 do_debug, u32 sz, void *ptr, MEM_READ_FUNC rf, MEM_WRITE_FUNC wf) {
    range_start = getbits<21, 28>(range_start);
    range_end = getbits<21, 28>(range_end);
    for (u32 i = range_start; i <= range_end; i++) {
        mem.pages[do_debug][sz >> 1][i].read = rf;
        mem.pages[do_debug][sz >> 1][i].write = wf;
        mem.pages[do_debug][sz >> 1][i].ptr = ptr;
    }
}

void core::mmap_io_range(u32 range_start, u32 range_end, u32 do_debug, u32 sz, void *ptr, MEM_READ_FUNC rf, MEM_WRITE_FUNC wf) {
    range_start = getbits<4, 15>(range_start);
    range_end = getbits<4, 15>(range_end);
    for (u32 i = range_start; i <= range_end; i++) {
        mem.iopages[do_debug][sz >> 1][i].read = rf;
        mem.iopages[do_debug][sz >> 1][i].write = wf;
        mem.iopages[do_debug][sz >> 1][i].ptr = ptr;
    }
}

#define RFUNC(n) template<u8 sz, bool do_debug> static u32 read_##n(void *ptr, u32 addr)
#define WFUNC(n) template<u8 sz, bool do_debug> static void write_##n(void *ptr, u32 addr, u32 val)

RFUNC(bad) {
    printf("\nWARN read from unmapped addr %08x(%d)", addr, sz);
    return 0;
}

WFUNC(bad) {
    printf("\nWARN write to unmapped addr %08x(%d): %08x", addr, sz, val);
}

RFUNC(RAM) {
    if constexpr(sz == 1) return static_cast<u8 *>(ptr)[addr & 0x1F'FFFF];
    if constexpr(sz == 2) return static_cast<u16 *>(ptr)[(addr & 0x1F'FFFF) >> 1];
    if constexpr(sz == 4) return static_cast<u32 *>(ptr)[(addr & 0x1F'FFFF) >> 2];
    NOGOHERE;
}

WFUNC(RAM) {
    if constexpr(sz == 1) static_cast<u8 *>(ptr)[addr & 0x1F'FFFF] = val;
    else if constexpr(sz == 2) static_cast<u16 *>(ptr)[(addr & 0x1F'FFFF) >> 1] = val;
    else if constexpr(sz == 4) static_cast<u32 *>(ptr)[(addr & 0x1F'FFFF) >> 2] = val;
    else {
        NOGOHERE;
    }
}

RFUNC(BIOS) {
    if constexpr(sz == 1) return static_cast<u8 *>(ptr)[addr & 0x7'FFFF];
    if constexpr(sz == 2) return static_cast<u16 *>(ptr)[(addr & 0x7'FFFF) >> 1];
    if constexpr(sz == 4) return static_cast<u32 *>(ptr)[(addr & 0x7'FFFF) >> 2];
    NOGOHERE;
}

WFUNC(BIOS) {
    printf("\nWARN attempt to write BIOS @%08x(%d): %08x", addr, sz, val);
}

RFUNC(io) {
    u32 pg = getbits<4, 15>(addr);
    auto &t = static_cast<MEM *>(ptr)->iopages[do_debug][sz >> 1][pg];
    return t.read(t.ptr, addr);
}

WFUNC(io) {
    u32 pg = getbits<4, 15>(addr);
    auto &t = static_cast<MEM *>(ptr)->iopages[do_debug][sz >> 1][pg];
    t.write(t.ptr, addr, val);
}

RFUNC(misc) {
    auto *th = static_cast<core *>(ptr);
    switch (addr) {
        case 0x00FF1F00: // Invalid addresses?
        case 0x00FF1F04:
        case 0x00FF1F08:
        case 0x00FF1F0C:
        case 0x00FF1F50:
            return 0;
    }
    printf("\nMISSED MISC READ TO %08x(%d)", addr, sz);
    if constexpr (sz == 1) return 0xFF;
    if constexpr (sz == 2) return 0xFFFF;
    if constexpr (sz == 4) return 0xFFFFFFFF;
    NOGOHERE;
}

WFUNC(misc) {
    auto *th = static_cast<core *>(ptr);
    switch (addr) {
        case 0x00FF1F00: // Invalid addresses
        case 0x00FF1F04:
        case 0x00FF1F08:
        case 0x00FF1F0C:
        case 0x00FF1F2C:
        case 0x00FF1F34:
        case 0x00FF1F3C:
        case 0x00FF1F40:
        case 0x00FF1F4C:
        case 0x00FF1F50:
        case 0x00FF1F60:
        case 0x00FF1F64:
        case 0x00FF1F7C:
            return;
    }
    printf("\nMISSED MISC WRITE TO %08x(%d): %08x", addr, sz, val);
}

RFUNC(io_scratchpad) {
    auto *th = static_cast<core *>(ptr);
    if constexpr(sz == 1) return reinterpret_cast<u8 *>(th->mem.scratchpad)[addr & 0x3FF];
    if constexpr(sz == 2) return reinterpret_cast<u16 *>(th->mem.scratchpad)[(addr & 0x3FF) >> 1];
    if constexpr(sz == 4) return reinterpret_cast<u32 *>(th->mem.scratchpad)[(addr & 0x3FF) >> 2];
}

WFUNC(io_scratchpad) {
    auto *th = static_cast<core *>(ptr);
    if (th->mem.cache_isolated) return;
    if constexpr(sz == 1) reinterpret_cast<u8 *>(th->mem.scratchpad)[addr & 0x3FF] = val;
    else if constexpr(sz == 2) reinterpret_cast<u16 *>(th->mem.scratchpad)[(addr & 0x3FF) >> 1] = val;
    else if constexpr(sz == 4) reinterpret_cast<u32 *>(th->mem.scratchpad)[(addr & 0x3FF) >> 2] = val;
    else {
        NOGOHERE;
    }
}

RFUNC(io_timers) {
    auto *th = static_cast<core *>(ptr);
    u32 timer_num = (addr >> 4) & 3;
    switch(addr & 0x1FFFFFCF) {
        case 0x1F801100: // current counter value
            return th->timers[timer_num].read() & 0xFFFF;
        case 0x1F801104: { // timer0...2 mode
            u32 v = th->timers[timer_num].mode.u;
            th->timers[timer_num].mode.reached_target = 0;
            th->timers[timer_num].mode.reached_ffff = 0;
            return v; }
        case 0x1F801108: // timer0...2 target value
            return th->timers[timer_num].target;
    }

    printf("\nUnhandled timers read %08x (%d)", addr, sz);
    return 0;
}

WFUNC(io_timers) {
    auto &t = static_cast<core *>(ptr)->timers[getbits<4, 5>(addr)];
    switch(addr & 0x1FFFFFCF) {
        case 0x1F801100: // current counter value
            t.write(val, sz);
            return;
        case 0x1F801104: // timer0...2 mode
            t.write_mode(val, sz);
            return;
        case 0x1F801108: // timer0...2 target value
            t.write_target(val, sz);
            return;

    }
    printf("\nUnhandled timers write %08x: %08x (%d)", addr, val, sz);
}

RFUNC(io_sio0) {
    return SIO::SIO0::read(ptr, addr, sz);
}

WFUNC(io_sio0) {
    SIO::SIO0::write(ptr, addr, sz, val);
}

RFUNC(io_sio1) {
    auto *th = static_cast<core *>(ptr);
    return th->sio1.read(addr, sz);
}

WFUNC(io_sio1) {
    auto *th = static_cast<core *>(ptr);
    th->sio1.write(addr, sz, val);
}

RFUNC(io_cpuregs) {
    auto *th = static_cast<core *>(ptr);
    return th->cpu.read_reg(addr, sz);
}

WFUNC(io_cpuregs) {
    auto *th = static_cast<core *>(ptr);
    th->cpu.write_reg(addr, sz, val);
}

RFUNC(io_dma) {
    auto *th = static_cast<core *>(ptr);
    return th->dma.read(addr, sz);
}

WFUNC(io_dma) {
    auto *th = static_cast<core *>(ptr);
    th->dma.write(addr, sz, val);
}

RFUNC(io_cdrom) {
    auto *th = static_cast<core *>(ptr);
    return th->cdrom.mainbus_read(addr, sz);
}

WFUNC(io_cdrom) {
    auto *th = static_cast<core *>(ptr);
    th->cdrom.mainbus_write(addr, val, sz);
}

RFUNC(io_gpu) {
    auto *th = static_cast<core *>(ptr);
    return th->gpu.read(addr, sz);
}

WFUNC(io_gpu) {
    auto *th = static_cast<core *>(ptr);
    th->gpu.write(addr, sz, val);
}

RFUNC(io_mdec) {
    auto *th = static_cast<core *>(ptr);
    return th->mdec.mainbus_read(addr, sz);
}

WFUNC(io_mdec) {
    auto *th = static_cast<core *>(ptr);
    th->mdec.mainbus_write(addr, sz, val);
}

RFUNC(io_spu) {
    auto *th = static_cast<core *>(ptr);
    return th->spu.mainbus_read(addr, sz);
}

WFUNC(io_spu) {
    auto *th = static_cast<core *>(ptr);
    th->spu.mainbus_write(addr, sz, val);
}

RFUNC(io_misc) {
    auto *th = static_cast<core *>(ptr);
    switch (addr) {
        case 0x1F80'1014: // SPU_DELAY delay/size
            return th->io.spu_delay;
        case 0x1F80'101C: // Expansion 2 Delay/size
            return 0x00070777;
        case 0x1F80'1060: // memory control 2
            return 0x00000B88;
        case 0x1F00'0084: // PIO
            return 0;
    }
    printf("\nUNHANDLED IO MISC. READ %08x(%d)", addr, sz);
    return 0;
}

WFUNC(io_misc) {
    auto *th = static_cast<core *>(ptr);
    switch (addr) {
        case 0x1F80'1000: // Expansion 1 base addr
        case 0x1F80'1004: // Expansion 2 base addr
        case 0x1F80'1008: // Expansion 1 delay/size
        case 0x1F80'100C: // Expansion 3 delay/size
        case 0x1F80'1010: // BIOS ROM delay/size
        case 0x1F80'1014: // SPU_DELAY delay/size
            th->io.spu_delay = val;
            return;
        case 0x1F80'1018: // CDROM_DELAY delay/size
        case 0x1F80'101C: // Expansion 2 delay/size
        case 0x1F80'1020: // COM_DELAY /size
        case 0x1F80'1060: // RAM SIZE, 2mb mirrored in first 8mb
        case 0x1F80'2041: // F802041h 1 PSX: POST (external 7 segment display, indicate BIOS boot status
            //printf("\nWRITE POST STATUS! %d", val);
            return;
        case 0x1FFE'0130: // Cache control
            return;
    }
}


#undef RFUNC
#undef WFUNC

void core::setup_mmap() {
#define bind(range_start, range_end, ptr, name) mmap_range(range_start, range_end, 0, 1, ptr, &read_##name<1, false>, &write_##name<1, false>); mmap_range(range_start, range_end, 1, 1, ptr, &read_##name<1, true>, &write_##name<1, true>); mmap_range(range_start, range_end, 0, 2, ptr, &read_##name<2, false>, &write_##name<2, false>); mmap_range(range_start, range_end, 1, 2, ptr, &read_##name<2, true>, &write_##name<2, true>); mmap_range(range_start, range_end, 0, 4, ptr, &read_##name<4, false>, &write_##name<4, false>); mmap_range(range_start, range_end, 1, 4, ptr, &read_##name<4, true>, &write_##name<4, true>)
    bind(0x0000'0000, 0xFFFF'FFFF, this, bad);

    bind(0x0000'0000, 0x007F'FFFF, mem.MRAM, RAM);
    bind(0x1F80'0000, 0x1F80'FFFF, &mem, io);
    bind(0x1FC0'0000, 0x1FC7'FFFF, mem.BIOS, BIOS);
    bind(0x00FF'0000, 0x00FF'FFFF, this, misc);

    bind(0x1FFE'0000, 0x1FFE'FFFF, this, io_misc); // mostly just for cache control

#undef bind
#define bind(range_start, range_end, ptr, name) mmap_io_range(range_start, range_end, 0, 1, ptr, &read_##name<1, false>, &write_##name<1, false>); mmap_io_range(range_start, range_end, 1, 1, ptr, &read_##name<1, true>, &write_##name<1, true>); mmap_io_range(range_start, range_end, 0, 2, ptr, &read_##name<2, false>, &write_##name<2, false>); mmap_io_range(range_start, range_end, 1, 2, ptr, &read_##name<2, true>, &write_##name<2, true>); mmap_io_range(range_start, range_end, 0, 4, ptr, &read_##name<4, false>, &write_##name<4, false>); mmap_io_range(range_start, range_end, 1, 4, ptr, &read_##name<4, true>, &write_##name<4, true>)
//    static constexpr u8 esz[5] = { 0, 0, 1, 0, 2};
//#define bindsub(range_start, range_end, ptr, rf, wf) for (u32 do_debug = 0; do_debug < 2; do_debug++) { for (u32 szi = 0; szi < 3; szi++) { mmap_io_range(range_start, range_end, 1, esz[szi], ptr, &rf, &wf); }}
    bind(0x1F80'0000, 0x1F80'FFFF, this, bad);

    bind(0x1F80'0000, 0x1F80'03FF, this, io_scratchpad);
    bind(0x1F80'1000, 0x1F80'102F, this, io_misc);
    bind(0x1F80'1060, 0x1F80'106F, this, io_misc);

    bind(0x1F80'1100, 0x1F80'112F, this, io_timers);
    bind(0x1F80'1040, 0x1F80'104F, &this->sio0, io_sio0);
    bind(0x1F80'1050, 0x1F80'105F, this, io_sio1);
    bind(0x1F80'1070, 0x1F80'107F, this, io_cpuregs);
    bind(0x1F80'1080, 0x1F80'10FF, this, io_dma);
    bind(0x1F80'1800, 0x1F80'180F, this, io_cdrom);
    bind(0x1F80'1810, 0x1F80'181F, this, io_gpu);
    bind(0x1F80'1820, 0x1F80'182F, this, io_mdec);
    bind(0x1F80'1C00, 0x1F80'1FFF, this, io_spu);

    bind(0x1F802040, 0x1F80204F, this, io_misc);
#undef bind
}

}