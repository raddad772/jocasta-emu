//
// Created by . on 4/3/26.
//

#include "helpers/multisize_memaccess.cpp"
#include "helpers/setbits.h"

#include "dc_debugger.h"
#include "dc_bus.h"

#define CHECK_SZ(z) if (sz != 4) printf("\nSIZE %d on " z " @ %08x", sz, addr);

namespace DREAMCAST {

void core::mmap_range(u32 range_start, u32 range_end, u32 do_debug, u32 sz, void *ptr, MEM_READ_FUNC rf, MEM_WRITE_FUNC wf) {
    range_start = getbits<20, 28>(range_start);
    range_end = getbits<20, 28>(range_end);
    for (u32 i = range_start; i <= range_end; i++) {
        mem.pages[do_debug][sz >> 1][i].read = rf;
        mem.pages[do_debug][sz >> 1][i].write = wf;
        mem.pages[do_debug][sz >> 1][i].ptr = ptr;
    }
}

void core::mmap_io_range(u32 range_start, u32 range_end, u32 do_debug, u32 sz, void *ptr, MEM_READ_FUNC rf, MEM_WRITE_FUNC wf) {
    // 0x005F'xx00 is what we care abut
    range_start = getbits<8, 15>(range_start);
    range_end = getbits<8, 15>(range_end);
    for (u32 i = range_start; i <= range_end; i++) {
        mem.iopages[do_debug][sz >> 1][i].read = rf;
        mem.iopages[do_debug][sz >> 1][i].write = wf;
        mem.iopages[do_debug][sz >> 1][i].ptr = ptr;
    }
}

#define RFUNC(n) template<u8 sz, bool do_debug> static u64 read_##n(void *ptr, u32 addr, bool *s)
#define WFUNC(n) template<u8 sz, bool do_debug> static void write_##n(void *ptr, u32 addr, u64 val, bool *s)

RFUNC(bad) {
    *s = false;
    printf("\nUnmapped read to %08x(%d)", addr, sz);
    return 0;
}

WFUNC(bad) {
    *s = false;
    printf("\nUnmapped write to %08x(%d): %08llx", addr, sz, val);
}

RFUNC(bios) {
    auto *th = static_cast<core *>(ptr);
    return cR[sz](th->BIOS.ptr, addr & 0x1FFFFF);
}

WFUNC(bios) {
    printf("\nAttempt to write BIOS %08x(%d): %08llx", addr, sz, val);
}

RFUNC(flash) {
    auto *th = static_cast<core *>(ptr);
    return th->read_flash(addr, sz);
}

RFUNC(io) {
    auto *th = static_cast<core *>(ptr);
    auto &t = th->mem.iopages[do_debug][sz >> 1][getbits<8, 15>(addr)];
    return t.read(t.ptr, addr, s);
}

WFUNC(io) {
    auto *th = static_cast<core *>(ptr);
    auto &t = th->mem.iopages[do_debug][sz >> 1][getbits<8, 15>(addr)];
    t.write(t.ptr, addr, val, s);
}

RFUNC(modem) {
    printf("\nWARN modem read!?!?!");
    //*s = false;
    return 0;
}

WFUNC(modem) {
    printf("\nWARN modem write!?!!?");
    *s = false;
}

RFUNC(aica_reg) {
    auto *th = static_cast<core *>(ptr);
    return th->aica.read_reg<sz, do_debug>(addr, s);
}

WFUNC(aica_reg) {
    auto *th = static_cast<core *>(ptr);
    th->aica.write_reg<sz, do_debug>(addr, val, s);
}

RFUNC(aica_mem) {
    if constexpr(sz == 1) return static_cast<u8 *>(ptr)[addr & 0x1F'FFFF];
    if constexpr(sz == 2) return static_cast<u16 *>(ptr)[(addr & 0x1F'FFFF) >> 1];
    if constexpr(sz == 4) return static_cast<u32 *>(ptr)[(addr & 0x1F'FFFF) >> 2];
    if constexpr(sz == 8) return static_cast<u64 *>(ptr)[(addr & 0x1F'FFFF) >> 3];
    NOGOHERE;
}

WFUNC(aica_mem) {
    if constexpr(sz == 1) static_cast<u8 *>(ptr)[addr & 0x1F'FFFF] = val;
    else if constexpr(sz == 2) static_cast<u16 *>(ptr)[(addr & 0x1F'FFFF) >> 1] = val;
    else if constexpr(sz == 4) static_cast<u32 *>(ptr)[(addr & 0x1F'FFFF) >> 2] = val;
    else if constexpr(sz == 8) static_cast<u64 *>(ptr)[(addr & 0x1F'FFFF) >> 3] = val;
    else {
        NOGOHERE;
    }
}

static u32 pvr_map32(u32 addr)
{
    static constexpr u32 VRAM_MASK = 0x7F'FFFF;
    static constexpr u32 VRAM_BANK_BIT = 0x40'0000;
    // 32-bit PVR path bank swizzle; matches Flycast's pvr_map32 behavior.
    static constexpr u32 static_bits = VRAM_MASK - (VRAM_BANK_BIT * 2 - 1) + 3;
    static constexpr u32 offset_bits = (VRAM_BANK_BIT - 1) & ~3;

    u32 bank = (addr & VRAM_BANK_BIT) / VRAM_BANK_BIT;
    u32 rv = addr & static_bits;
    rv |= (addr & offset_bits) * 2;
    rv |= bank * 4;
    return rv & VRAM_MASK;
}

RFUNC(VRAM) {
    if constexpr(sz == 1) return static_cast<u8 *>(ptr)[addr & 0x7F'FFFF];
    if constexpr(sz == 2) return static_cast<u16 *>(ptr)[(addr & 0x7F'FFFF) >> 1];
    if constexpr(sz == 4) return static_cast<u32 *>(ptr)[(addr & 0x7F'FFFF) >> 2];
    if constexpr(sz == 8) return static_cast<u64 *>(ptr)[(addr & 0x7F'FFFF) >> 3];
}

WFUNC(VRAM) {
    if constexpr(sz == 1) static_cast<u8 *>(ptr)[addr & 0x7F'FFFF] = val;
    else if constexpr(sz == 2) static_cast<u16 *>(ptr)[(addr & 0x7F'FFFF) >> 1] = val;
    else if constexpr(sz == 4) static_cast<u32 *>(ptr)[(addr & 0x7F'FFFF) >> 2] = val;
    else if constexpr(sz == 8) static_cast<u64 *>(ptr)[(addr & 0x7F'FFFF) >> 3] = val;
    else {
        NOGOHERE;
    }
}

RFUNC(VRAM32) {
    auto *vram = static_cast<u8 *>(ptr);
    if constexpr(sz == 1) return vram[pvr_map32(addr)];
    if constexpr(sz == 2) return *reinterpret_cast<u16 *>(vram + (pvr_map32(addr) & ~1));
    if constexpr(sz == 4) return *reinterpret_cast<u32 *>(vram + (pvr_map32(addr) & ~3));
    if constexpr(sz == 8) {
        u64 lo = *reinterpret_cast<u32 *>(vram + (pvr_map32(addr) & ~3));
        u64 hi = *reinterpret_cast<u32 *>(vram + (pvr_map32(addr + 4) & ~3));
        return lo | (hi << 32);
    }
}

WFUNC(VRAM32) {
    auto *vram = static_cast<u8 *>(ptr);
    if constexpr(sz == 1) {
        return;
    }
    else if constexpr(sz == 2) {
        *reinterpret_cast<u16 *>(vram + (pvr_map32(addr) & ~1)) = val;
    }
    else if constexpr(sz == 4) {
        *reinterpret_cast<u32 *>(vram + (pvr_map32(addr) & ~3)) = val;
    }
    else if constexpr(sz == 8) {
        *reinterpret_cast<u32 *>(vram + (pvr_map32(addr) & ~3)) = val & 0xFFFFFFFF;
        *reinterpret_cast<u32 *>(vram + (pvr_map32(addr + 4) & ~3)) = val >> 32;
    }
    else {
        NOGOHERE;
    }
}

RFUNC(PVR_area4) {
    auto *th = static_cast<core *>(ptr);
    u32 area_addr = addr & 0x01FF'FFE0;

    if (area_addr < 0x0100'0000) {
        printf("\nWARN read TA/YUV FIFO!? %08x(%d)", addr, sz);
        return 0;
    }

    bool access32 = ((addr & 0x0200'0000) ? th->io.SB_LMMODE1 : th->io.SB_LMMODE0) == 1;
    if (access32) return read_VRAM32<sz, do_debug>(th->holly.RAM, addr, s);
    return read_VRAM<sz, do_debug>(th->holly.RAM, addr, s);
}

WFUNC(PVR_area4) {
    auto *th = static_cast<core *>(ptr);
    u32 area_addr = addr & 0x01FF'FFE0;

    if (area_addr < 0x0080'0000) {
        th->holly.write_ta_fifo(addr, sz, val, s);
        return;
    }

    if (area_addr < 0x0100'0000) {
        printf("\nWARN YUV FIFO write not implemented %08x(%d): %08llx", addr, sz, val);
        return;
    }

    bool access32 = ((addr & 0x0200'0000) ? th->io.SB_LMMODE1 : th->io.SB_LMMODE0) == 1;
    if (access32) return write_VRAM32<sz, do_debug>(th->holly.RAM, addr, val, s);
    return write_VRAM<sz, do_debug>(th->holly.RAM, addr, val, s);
}

static constexpr u64 szmask[9] = { 0, 0xFF, 0xFFFF, 0, 0xFFFFFFFF, 0, 0, 0, 0xFFFFFFFFFFFFFFFF };

RFUNC(nothing) {
    return 0;//szmask[sz];
}

WFUNC(nothing) {

}

RFUNC(RAM) {
    if constexpr(sz == 1) return static_cast<u8 *>(ptr)[addr & 0xFF'FFFF];
    if constexpr(sz == 2) return static_cast<u16 *>(ptr)[(addr & 0xFF'FFFF) >> 1];
    if constexpr(sz == 4) return static_cast<u32 *>(ptr)[(addr & 0xFF'FFFF) >> 2];
    if constexpr(sz == 8) return static_cast<u64 *>(ptr)[(addr & 0xFF'FFFF) >> 3];
    NOGOHERE;
}

    static const u32 maskalign[9] = { 0, 0xFFFFFFFF, 0xFFFFFFFE, 0, 0xFFFFFFFC, 0, 0, 0, 0xFFFFFFF8 };

WFUNC(RAM) {
    if constexpr(sz == 1) static_cast<u8 *>(ptr)[addr & 0xFF'FFFF] = val;
    else if constexpr(sz == 2) static_cast<u16 *>(ptr)[(addr & 0xFF'FFFF) >> 1] = val;
    else if constexpr(sz == 4) static_cast<u32 *>(ptr)[(addr & 0xFF'FFFF) >> 2] = val;
    else if constexpr(sz == 8) static_cast<u64 *>(ptr)[(addr & 0xFF'FFFF) >> 3] = val;
    else {
        NOGOHERE;
    }
}

RFUNC(io_sys) {
    CHECK_SZ("sysr");
    auto *th = static_cast<core *>(ptr);
    return th->read_io(addr, sz, s);
}

WFUNC(io_sys) {
    CHECK_SZ("sysw");
    auto *th = static_cast<core *>(ptr);
    th->write_io(addr, sz, val, s);
}

RFUNC(io_maple) {
    CHECK_SZ("mapler");
    auto *th = static_cast<core *>(ptr);
    return th->maple.read_io(addr, sz, s);
}

WFUNC(io_maple) {
    CHECK_SZ("maplew");
    auto *th = static_cast<core *>(ptr);
    th->maple.write_io(addr, sz, val, s);
}

RFUNC(io_gdrom) {
    auto *th = static_cast<core *>(ptr);
    return th->gdrom.read_io(addr, sz, s);
}

WFUNC(io_gdrom) {
    auto *th = static_cast<core *>(ptr);
    th->gdrom.write_io(addr, sz, val, s);
}

RFUNC(io_g1) {
    CHECK_SZ("g1r");
    auto *th = static_cast<core *>(ptr);
    return th->read_io_g1(addr, sz, s);
}

WFUNC(io_g1) {
    CHECK_SZ("g1w");
    auto *th = static_cast<core *>(ptr);
    th->write_io_g1(addr, sz, val, s);
}

RFUNC(io_g2) {
    CHECK_SZ("g2r");
    auto *th = static_cast<core *>(ptr);
    return th->read_io_g2(addr, sz, s);
}

WFUNC(io_g2) {
    CHECK_SZ("g2w");
    auto *th = static_cast<core *>(ptr);
    th->write_io_g2(addr, sz, val, s);
}

RFUNC(io_holly) {
    CHECK_SZ("hollyr");
    auto *th = static_cast<core *>(ptr);
    return th->holly.read_io(addr, sz, s);
}

WFUNC(io_holly) {
    CHECK_SZ("hollyw");
    auto *th = static_cast<core *>(ptr);
    th->holly.write_io(addr, sz, val, s);
}

RFUNC(TAFIFO) {
    printf("\nWARN read TA FIFO!? %08x(%d)", addr, sz);
    return 0;
}

WFUNC(TAFIFO) {
    auto *th = static_cast<core *>(ptr);
    th->holly.write_ta_fifo(addr, sz, val, s);
}

void core::setup_mmap() {
#define bind(range_start, range_end, ptr, rf, wf) mmap_range(range_start, range_end, 0, 1, ptr, &rf<1, false>, &wf<1, false>); mmap_range(range_start, range_end, 1, 1, ptr, &rf<1, true>, &wf<1, true>); mmap_range(range_start, range_end, 0, 2, ptr, &rf<2, false>, &wf<2, false>); mmap_range(range_start, range_end, 1, 2, ptr, &rf<2, true>, &wf<2, true>); mmap_range(range_start, range_end, 0, 4, ptr, &rf<4, false>, &wf<4, false>); mmap_range(range_start, range_end, 1, 4, ptr, &rf<4, true>, &wf<4, true>); mmap_range(range_start, range_end, 0, 8, ptr, &rf<8, false>, &wf<8, false>); mmap_range(range_start, range_end, 1, 8, ptr, &rf<8, true>, &wf<8, true>)
    bind(0x0000'0000, 0xFFFF'FFFF, this, read_bad, write_bad);

    bind(0x0000'0000, 0x001F'FFFF, this, read_bios, write_bios);
    bind(0x0020'0000, 0x002F'FFFF, this, DREAMCAST::read_flash, write_bad);
    bind(0x0050'0000, 0x005F'FFFF, this, DREAMCAST::read_io, DREAMCAST::write_io);
    bind(0x0060'0000, 0x006F'FFFF, this, read_modem, write_modem);
    bind(0x0070'0000, 0x007F'FFFF, this, read_aica_reg, write_aica_reg);
    bind(0x0080'0000, 0x009F'FFFF, aica.RAM, read_aica_mem, write_aica_mem);
    bind(0x0400'0000, 0x04FF'FFFF, holly.RAM, read_VRAM, write_VRAM);
    bind(0x0500'0000, 0x05FF'FFFF, holly.RAM, read_VRAM32, write_VRAM32);
    bind(0x0600'0000, 0x06FF'FFFF, holly.RAM, read_VRAM, write_VRAM);
    bind(0x0700'0000, 0x07FF'FFFF, holly.RAM, read_VRAM32, write_VRAM32);
    bind(0x0C00'0000, 0x0DFF'FFFF, RAM, read_RAM, write_RAM);
    bind(0x1000'0000, 0x13FF'FFFF, this, read_PVR_area4, write_PVR_area4);

#define bindio(range_start, range_end, ptr, rf, wf) mmap_io_range(range_start, range_end, 0, 1, ptr, &rf<1, false>, &wf<1, false>); mmap_io_range(range_start, range_end, 1, 1, ptr, &rf<1, true>, &wf<1, true>); mmap_io_range(range_start, range_end, 0, 2, ptr, &rf<2, false>, &wf<2, false>); mmap_io_range(range_start, range_end, 1, 2, ptr, &rf<2, true>, &wf<2, true>); mmap_io_range(range_start, range_end, 0, 4, ptr, &rf<4, false>, &wf<4, false>); mmap_io_range(range_start, range_end, 1, 4, ptr, &rf<4, true>, &wf<4, true>); mmap_io_range(range_start, range_end, 0, 8, ptr, &rf<8, false>, &wf<8, false>); mmap_io_range(range_start, range_end, 1, 8, ptr, &rf<8, true>, &wf<8, true>)

    bindio(0x005F'0000, 0x005F'FFFF, this, read_bad, write_bad);
    bindio(0x005F'6800, 0x005F'69FF, this, read_io_sys, write_io_sys);
    bindio(0x005F'6C00, 0x005F'6CFF, this, read_io_maple, write_io_maple);
    bindio(0x005F'7000, 0x005F'70FF, this, read_io_gdrom, write_io_gdrom);
    bindio(0x005F'7400, 0x005F'74FF, this, DREAMCAST::read_io_g1, DREAMCAST::write_io_g1);
    bindio(0x005F'7800, 0x005F'78FF, this, DREAMCAST::read_io_g2, DREAMCAST::write_io_g2);
    bindio(0x005F'7C00, 0x005F'7CFF, this, DREAMCAST::read_io_holly, DREAMCAST::write_io_holly);
    bindio(0x005F'8000, 0x005F'9FFF, this, read_io_holly, write_io_holly);

}

#undef bind
#undef bindio

}
