//
// Created by . on 12/4/24.
//
#include <cstring>
#include <cassert>

#include "nds_regs.h"
#include "nds_bus.h"
#include "nds_vram.h"
#include "nds_dma.h"
#include "nds_irq.h"
#include "nds_ipc.h"
#include "nds_rtc.h"
#include "nds_spi.h"
#include "nds_clock.h"
#include "nds_timers.h"
#include "nds_debugger.h"
#include "helpers/multisize_memaccess.cpp"

namespace NDS {

static constexpr u32 masksz[5] = { 0, 0xFF, 0xFFFF, 0, 0xFFFFFFFF};
static constexpr u32 maskalign[5] = {0, 0xFFFFFFFF, 0xFFFFFFFE, 0, 0xFFFFFFFC};

template <u8 sz, bool do_debug>
u32 core::busrd7_invalid(u32 addr, u8 access) {
    printf("\nREAD7 UNKNOWN ADDR:%08x sz:%d", addr, sz);
    waitstates.current_transaction++;
    //dbg.var++;
    //if (dbg.var > 15) dbg_break("too many bad reads", clock.master_cycle_count);
    return 0;
}

template <u8 sz, bool do_debug>
u32 core::busrd9_invalid(u32 addr, u8 access) {
    printf("\nREAD9 UNKNOWN ADDR:%08x sz:%d", addr, sz);
    waitstates.current_transaction++;
    //dbg.var++;
    //if (dbg.var > 15) dbg_break("too many bad reads", clock.master_cycle_count);
    return 0;
}

template <u8 sz, bool do_debug>
void core::buswr7_invalid(u32 addr, u8 access, u32 val) {
    printf("\nWRITE7 UNKNOWN ADDR:%08x sz:%d DATA:%08x", addr, sz, val);
    waitstates.current_transaction++;
    ::dbg.var++;
    //if (dbg.var > 15) dbg_break("too many bad writes", clock.master_cycle_count);
}

template <u8 sz, bool do_debug>
void core::buswr9_invalid(u32 addr, u8 access, u32 val) {
    waitstates.current_transaction++;
    static int pokemon_didit = 0;
    if ((addr == 0) && !pokemon_didit) {
        pokemon_didit = 1;
        printf("\nWRITE9 UNKNOWN ADDR:%08x sz:%d DATA:%08x", addr, sz, val);
    }
    if (addr != 0) {
        printf("\nWRITE9 UNKNOWN ADDR:%08x sz:%d DATA:%08x", addr, sz, val);
        ::dbg.var++;
        if (::dbg.var > 15) dbg_break("too many bad writes", clock.master_cycle_count7);
    }
    //dbg_break("unknown addr write9", clock.master_cycle_count7);
}

template<u8 sz, bool do_debug, bool cached>
void core::buswr7_shared(u32 addr, u8 access, u32 val)
{
    if (addr >= 0x03800000) {
        addr &= 0xFFFF;
        if constexpr(cached)
            mem.WRAM_arm7_block_cache.mark_dirty(addr);
        if constexpr(sz == 1) reinterpret_cast<u8 *>(mem.WRAM_arm7)[addr] = val;
        else if constexpr(sz == 2) reinterpret_cast<u16 *>(mem.WRAM_arm7)[addr >> 1] = val;
        else if constexpr(sz == 4) reinterpret_cast<u32 *>(mem.WRAM_arm7)[addr >> 2] = val;
        else NOGOHERE;
        return;
    }
    if (!mem.io.RAM7.disabled) {
        addr &= mem.io.RAM7.mask;
        addr += mem.io.RAM7.base;
        if constexpr(cached)
            mem.WRAM_share_block_cache.mark_dirty(addr);
        if constexpr(sz == 1) reinterpret_cast<u8 *>(mem.WRAM_share)[addr] = val;
        else if constexpr(sz == 2) reinterpret_cast<u16 *>(mem.WRAM_share)[addr >> 1] = val;
        else if constexpr(sz == 4) reinterpret_cast<u32 *>(mem.WRAM_share)[addr >> 2] = val;
        else NOGOHERE;
        return;
    }
    else {
        addr &= 0xFFFF;
        if constexpr(cached)
            mem.WRAM_arm7_block_cache.mark_dirty(addr);
        if constexpr(sz == 1) reinterpret_cast<u8 *>(mem.WRAM_arm7)[addr] = val;
        else if constexpr(sz == 2) reinterpret_cast<u16 *>(mem.WRAM_arm7)[addr >> 1] = val;
        else if constexpr(sz == 4) reinterpret_cast<u32 *>(mem.WRAM_arm7)[addr >> 2] = val;
        else NOGOHERE;
        return;
    }
}

template<u8 sz, bool do_debug>
u32 core::busrd7_shared(u32 addr, u8 access)
{
    if (addr >= 0x03800000) {
        addr &= 0xFFFF;
        if constexpr(sz == 1) return reinterpret_cast<u8 *>(mem.WRAM_arm7)[addr];
        else if constexpr(sz == 2) return reinterpret_cast<u16 *>(mem.WRAM_arm7)[addr >> 1];
        else if constexpr(sz == 4) return reinterpret_cast<u32 *>(mem.WRAM_arm7)[addr >> 2];
        NOGOHERE;
    }
    if (!mem.io.RAM7.disabled) {
        addr &= mem.io.RAM7.mask;
        addr += mem.io.RAM7.base;
        if constexpr(sz == 1) return reinterpret_cast<u8 *>(mem.WRAM_share)[addr];
        else if constexpr(sz == 2) return reinterpret_cast<u16 *>(mem.WRAM_share)[addr >> 1];
        else if constexpr(sz == 4) return reinterpret_cast<u32 *>(mem.WRAM_share)[addr >> 2];
        NOGOHERE;
    }
    else {
        addr &= 0xFFFF;
        if constexpr(sz == 1) return reinterpret_cast<u8 *>(mem.WRAM_arm7)[addr];
        else if constexpr(sz == 2) return reinterpret_cast<u16 *>(mem.WRAM_arm7)[addr >> 1];
        else if constexpr(sz == 4) return reinterpret_cast<u32 *>(mem.WRAM_arm7)[addr >> 2];
        NOGOHERE;
    }
}

template<u8 sz, bool do_debug, bool cached>
void core::buswr7_vram(u32 addr, u8 access, u32 val)
{
    u32 bank = (addr >> 17) & 1;
    if (mem.vram.map.arm7[bank]) {
        u32 cached_addr = addr & 0xF'FFFF;
        addr &= 0x1'FFFF;
        if constexpr(cached)
            mem.vram.block_cache.mark_dirty(cached_addr);
        if constexpr(sz == 1) reinterpret_cast<u8 *>(mem.vram.map.arm7[bank])[addr] = val;
        else if constexpr(sz == 2) reinterpret_cast<u16 *>(mem.vram.map.arm7[bank])[addr >> 1] = val;
        else if constexpr(sz == 4) reinterpret_cast<u32 *>(mem.vram.map.arm7[bank])[addr >> 2] = val;
        else NOGOHERE;
    }
}

template<u8 sz, bool do_debug>
u32 core::busrd7_vram(u32 addr, u8 access)
{
    u32 bank = (addr >> 17) & 1;
    if (mem.vram.map.arm7[bank]) {
        addr &= 0x1'FFFF;
        if constexpr(sz == 1) return reinterpret_cast<u8 *>(mem.vram.map.arm7[bank])[addr];
        if constexpr(sz == 2) return reinterpret_cast<u16 *>(mem.vram.map.arm7[bank])[addr >> 1];
        if constexpr(sz == 4) return reinterpret_cast<u32 *>(mem.vram.map.arm7[bank])[addr >> 2];
        NOGOHERE;
    }

    return busrd7_invalid<sz, do_debug>(addr, access);
}

template<u8 sz, bool do_debug>
void core::buswr7_gba_cart(u32 addr, u8 access, u32 val)
{
}

template<u8 sz, bool do_debug>
u32 core::busrd7_gba_cart(u32 addr, u8 access)
{
    if (!io.rights.gba_slot) return (addr & 0x1FFFF) >> 1;
    return 0;
}

template<u8 sz, bool do_debug>
void core::buswr7_gba_sram(u32 addr, u8 access, u32 val)
{
}

template<u8 sz, bool do_debug>
u32 core::busrd7_gba_sram(u32 addr, u8 access)
{
    if (!io.rights.gba_slot) return masksz[sz];
    return 0;
}

template<u8 sz, bool do_debug>
u32 core::busrd9_main(u32 addr, u8 access)
{
    addr &= 0x3F'FFFF;
    if constexpr (sz == 1) return reinterpret_cast<u8 *>(mem.RAM)[addr];
    if constexpr (sz == 2) return reinterpret_cast<u16 *>(mem.RAM)[addr >> 1];
    if constexpr (sz == 4) return reinterpret_cast<u32 *>(mem.RAM)[addr >> 2];
    NOGOHERE;
}

template<u8 sz, bool do_debug, bool cached>
void core::buswr9_main(u32 addr, u8 access, u32 val)
{
    addr &= 0x3F'FFFF;
    if constexpr(cached)
        mem.RAM_block_cache.mark_dirty(addr);
    if constexpr (sz == 1) reinterpret_cast<u8 *>(mem.RAM)[addr] = val;
    else if constexpr (sz == 2) reinterpret_cast<u16 *>(mem.RAM)[addr >> 1] = val;
    else if constexpr (sz == 4) reinterpret_cast<u32 *>(mem.RAM)[addr >> 2] = val;
    else NOGOHERE;
}

template<u8 sz, bool do_debug>
void core::buswr9_gba_cart(u32 addr, u8 access, u32 val)
{
}

template<u8 sz, bool do_debug>
u32 core::busrd9_gba_cart(u32 addr, u8 access)
{
    if (io.rights.gba_slot) return (addr & 0x1FFFF) >> 1;
    return 0;
}

template<u8 sz, bool do_debug>
void core::buswr9_gba_sram(u32 addr, u8 access, u32 val)
{
}

template<u8 sz, bool do_debug>
u32 core::busrd9_gba_sram(u32 addr, u8 access)
{
    if (io.rights.gba_slot) return masksz[sz];
    return 0;
}

template<u8 sz, bool do_debug, bool cached>
void core::buswr9_shared(u32 addr,  u8 access, u32 val)
{
    if (!mem.io.RAM9.disabled) {
        addr &= mem.io.RAM9.mask;
        addr += mem.io.RAM9.base;
        if constexpr(cached)
            mem.WRAM_share_block_cache.mark_dirty(addr);
        if constexpr (sz == 1) reinterpret_cast<u8 *>(mem.WRAM_share)[addr] = val;
        else if constexpr (sz == 2) reinterpret_cast<u16 *>(mem.WRAM_share)[addr >> 1] = val;
        else if constexpr (sz == 4) reinterpret_cast<u32 *>(mem.WRAM_share)[addr >> 2] = val;
        else NOGOHERE;
    }
}

template<u8 sz, bool do_debug>
u32 core::busrd9_shared(u32 addr, u8 access)
{
    if (mem.io.RAM9.disabled) return 0; // undefined
    addr &= mem.io.RAM9.mask;
    addr += mem.io.RAM9.base;
    if constexpr (sz == 1) return reinterpret_cast<u8 *>(mem.WRAM_share)[addr];
    else if constexpr (sz == 2) return reinterpret_cast<u16 *>(mem.WRAM_share)[addr >> 1];
    else if constexpr (sz == 4) return reinterpret_cast<u32 *>(mem.WRAM_share)[addr >> 2];
}

template<u8 sz, bool do_debug>
void core::buswr9_obj_and_palette(u32 addr, u8 access, u32 val)
{
    if (addr < 0x05000000) return;
    addr &= 0x7FF;
    if (addr < 0x200) return ppu.write_2d_bg_palette(0, addr & 0x1FF, sz, val);
    if (addr < 0x400) {
        return ppu.write_2d_obj_palette(0, addr & 0x1FF, sz, val);
    }
    if (addr < 0x600) return ppu.write_2d_bg_palette(1, addr & 0x1FF, sz, val);

    return ppu.write_2d_obj_palette(1, addr & 0x1FF, sz, val);
}

template<u8 sz, bool do_debug>
u32 core::busrd9_obj_and_palette(u32 addr, u8 access)
{
    if (addr < 0x05000000) return busrd9_invalid<sz, do_debug>(addr, access);
    addr &= 0x7FF;
    if (addr < 0x200)
        return cR[sz](ppu.eng2d[0].mem.bg_palette, addr & 0x1FF);
    if (addr < 0x400)
        return cR[sz](ppu.eng2d[0].mem.obj_palette, addr & 0x1FF);
    if (addr < 0x600)
        return cR[sz](ppu.eng2d[1].mem.bg_palette, addr & 0x1FF);
    return cR[sz](ppu.eng2d[1].mem.obj_palette, addr & 0x1FF);
}

template<u8 sz, bool do_debug, bool cached>
void core::buswr9_vram(u32 addr, u8 access, u32 val)
{
    if constexpr (sz == 1) {
        static int a = 1;
        if (a) {
            printf("\nWarning ignore 8-bit vram write!");
            a = 0;
        }
        return; // 8-bit writes ignored
    }
    //printf("\nVRAM write addr:%08x vaddr:%06x MSTA:%d OFS:%d val:%04x", addr, addr & 0x1FFFF, mem.vram.io.bank[0].mst, mem.vram.io.bank[0].ofs, val);
    u8 *ptr = mem.vram.map.arm9[NDSVRAMSHIFT(addr) & NDSVRAMMASK];
    if (ptr) {
        u32 cached_addr = addr & 0xF'FFFF;
        addr &= 0x3FFF;
        if constexpr(cached)
            mem.vram.block_cache.mark_dirty(cached_addr);
        if constexpr (sz == 2) reinterpret_cast<u16 *>(ptr)[addr >> 1] = val;
        else if constexpr (sz == 4) reinterpret_cast<u32 *>(ptr)[addr >> 2] = val;
        else NOGOHERE;
        return;
    }

    static int a = 2;
    if (a) {
        printf("\nInvalid VRAM9 write unmapped addr:%08x sz:%d val:%08x", addr, sz, val);
        a--;
        if (a == 0) printf("\nMuting invalid VRAM9 write messages...");
    }
}

template<u8 sz, bool do_debug>
u32 core::busrd9_vram(u32 addr, u8 access)
{
    u8 *ptr = mem.vram.map.arm9[NDSVRAMSHIFT(addr) & NDSVRAMMASK];
    if (ptr) {
        addr &= 0x3FFF;
        if constexpr(sz == 1) return ptr[addr];
        if constexpr(sz == 2) return reinterpret_cast<u16 *>(ptr)[addr >> 1];
        if constexpr(sz == 4) return reinterpret_cast<u32 *>(ptr)[addr >> 2];
        NOGOHERE;
    }

    printf("\nInvalid VRAM9 read unmapped addr:%08x sz:%d", addr, sz);
    //dbg_break("Unmapped VRAM9 read", clock.master_cycle_count7);
    return 0;
}

template<u8 sz, bool do_debug>
void core::buswr9_oam(u32 addr, u8 access, u32 val)
{
    addr &= 0x7FF;
    if (addr < 0x400) return cW[sz](ppu.eng2d[0].mem.oam, addr & 0x3FF, val);
    return cW[sz](ppu.eng2d[1].mem.oam, addr & 0x3FF, val);
}

template<u8 sz, bool do_debug>
u32 core::busrd9_oam(u32 addr, u8 access)
{
    addr &= 0x7FF;
    if (addr < 0x400) return cR[sz](ppu.eng2d[0].mem.oam, addr & 0x3FF);
    else return cR[sz](ppu.eng2d[1].mem.oam, addr & 0x3FF);
}

template<u8 sz, bool do_debug>
u32 core::busrd7_bios7(u32 addr, u8 access)
{
    addr &= 0x3FFF;
    if constexpr(sz == 1) return reinterpret_cast<u8 *>(mem.bios7)[addr];
    if constexpr(sz == 2) return reinterpret_cast<u16 *>(mem.bios7)[addr >> 1];
    if constexpr(sz == 4) return reinterpret_cast<u32 *>(mem.bios7)[addr >> 2];
    NOGOHERE;
}

template<u8 sz, bool do_debug>
void core::buswr7_bios7(u32 addr, u8 access, u32 val)
{
}

template<u8 sz, bool do_debug>
u32 core::busrd7_main(u32 addr, u8 access)
{
    addr &= 0x3F'FFFF;
    if constexpr(sz == 1) return reinterpret_cast<u8 *>(mem.RAM)[addr];
    if constexpr(sz == 2) return reinterpret_cast<u16 *>(mem.RAM)[addr >> 1];
    if constexpr(sz == 4) return reinterpret_cast<u32 *>(mem.RAM)[addr >> 2];
    NOGOHERE;
}

template<u8 sz, bool do_debug>
void core::buswr7_main(u32 addr, u8 access, u32 val)
{
    addr &= 0x3F'FFFF;
    if constexpr(sz == 1) reinterpret_cast<u8 *>(mem.RAM)[addr] = val;
    else if constexpr(sz == 2) reinterpret_cast<u16 *>(mem.RAM)[addr >> 1] = val;
    else if constexpr(sz == 4) reinterpret_cast<u32 *>(mem.RAM)[addr >> 2] = val;
    else NOGOHERE;
}

static u32 DMA_CH_NUM(u32 addr)
{
    addr &= 0xFF;
    if (addr < 0xBC) return 0;
    if (addr < 0xC8) return 1;
    if (addr < 0xD4) return 2;
    return 3;
}

template<bool do_debug>
u32 core::busrd7_io8(u32 addr, u8 access)
{
    u32 v;
    switch(addr) {
        case R_AUXSPICNT:
            if (!io.rights.nds_slot_is7) return 0;
            return cart.read_spicnt() & 0xFF;
        case R_AUXSPICNT+1:
            if (!io.rights.nds_slot_is7) return 0;
            return cart.read_spicnt() >> 8;

        case R_RCNT+0: return io.sio_data & 0xFF;
        case R_RCNT+1: return io.sio_data >> 8;
        case 0x04000138:
        case 0x04000139: return 0;
        case R7_SPICNT+0:
            spi.cnt.busy = clock.current7() < spi.busy_until;
            return spi.cnt.u & 0xFF;
        case R7_SPICNT+1:
            return spi.cnt.u >> 8;

        case R_POSTFLG:
            return io.arm7.POSTFLG;
        case R_POSTFLG+1:
            return 0;

        case R7_WIFIWAITCNT:
            return io.powcnt.wifi ? io.powcnt.wifi_waitcnt : 0;
        case R7_WIFIWAITCNT+1:
            return 0;

        case R7_POWCNT2+0:
            v = io.powcnt.speakers;
            v |= io.powcnt.wifi << 1;
            return v;
        case R7_POWCNT2+1:
            return 0;

        case R_KEYINPUT+0: // buttons!!!
            return controller.get_state(0);
        case R_KEYINPUT+1: // buttons!!!
            return controller.get_state(1);
        case R_EXTKEYIN+0:
            return controller.get_state(2);
        case R_EXTKEYIN+1:
            return 0;

        case R_IPCFIFOCNT+0:
            // send fifo from 7 is to_9
            v = io.ipc.to_arm9.is_empty();
            v |= io.ipc.to_arm9.is_full() << 1;
            v |= io.ipc.arm7.irq_on_send_fifo_empty << 2;
            return v;
        case R_IPCFIFOCNT+1:
            v = io.ipc.to_arm7.is_empty();
            v |= io.ipc.to_arm7.is_full() << 1;
            v |= io.ipc.arm7.irq_on_recv_fifo_not_empty << 2;
            v |= io.ipc.arm7.error << 6;
            v |= io.ipc.arm7.fifo_enable << 7;
            return v;


        case R_IPCSYNC+0:
            return io.ipc.arm7sync.dinput;
        case R_IPCSYNC+1:
            v = io.ipc.arm7sync.doutput;
            v |= io.ipc.arm7sync.enable_irq_from_remote << 6;
            return v;
        case R_IPCSYNC+2:
        case R_IPCSYNC+3:
            return 0;

        case R_IME: return io.arm7.IME;
        case R_IME+1: return 0;
        case R_IME+2: return 0;
        case R_IME+3: return 0;
        case R_IE+0: return io.arm7.IE & 0xFF;
        case R_IE+1: return (io.arm7.IE >> 8) & 0xFF;
        case R_IE+2: return (io.arm7.IE >> 16) & 0xFF;
        case R_IE+3: return (io.arm7.IE >> 24) & 0xFF;
        case R_IF+0: return io.arm7.IF & 0xFF;
        case R_IF+1: return (io.arm7.IF >> 8) & 0xFF;
        case R_IF+2: return (io.arm7.IF >> 16) & 0xFF;
        case R_IF+3: return (io.arm7.IF >> 24) & 0xFF;

        case R_DMA0SAD+0: return dma9[0].io.src_addr & 0xFF;
        case R_DMA0SAD+1: return (dma9[0].io.src_addr >> 8) & 0xFF;
        case R_DMA0SAD+2: return (dma9[0].io.src_addr >> 16) & 0xFF;
        case R_DMA0SAD+3: return (dma9[0].io.src_addr >> 24) & 0xFF;
        case R_DMA0DAD+0: return dma9[0].io.dest_addr & 0xFF;
        case R_DMA0DAD+1: return (dma9[0].io.dest_addr >> 8) & 0xFF;
        case R_DMA0DAD+2: return (dma9[0].io.dest_addr >> 16) & 0xFF;
        case R_DMA0DAD+3: return (dma9[0].io.dest_addr >> 24) & 0xFF;

        case R_DMA1SAD+0: return dma9[1].io.src_addr & 0xFF;
        case R_DMA1SAD+1: return (dma9[1].io.src_addr >> 8) & 0xFF;
        case R_DMA1SAD+2: return (dma9[1].io.src_addr >> 16) & 0xFF;
        case R_DMA1SAD+3: return (dma9[1].io.src_addr >> 24) & 0xFF;
        case R_DMA1DAD+0: return dma9[1].io.dest_addr & 0xFF;
        case R_DMA1DAD+1: return (dma9[1].io.dest_addr >> 8) & 0xFF;
        case R_DMA1DAD+2: return (dma9[1].io.dest_addr >> 16) & 0xFF;
        case R_DMA1DAD+3: return (dma9[1].io.dest_addr >> 24) & 0xFF;

        case R_DMA2SAD+0: return dma9[2].io.src_addr & 0xFF;
        case R_DMA2SAD+1: return (dma9[2].io.src_addr >> 8) & 0xFF;
        case R_DMA2SAD+2: return (dma9[2].io.src_addr >> 16) & 0xFF;
        case R_DMA2SAD+3: return (dma9[2].io.src_addr >> 24) & 0xFF;
        case R_DMA2DAD+0: return dma9[2].io.dest_addr & 0xFF;
        case R_DMA2DAD+1: return (dma9[2].io.dest_addr >> 8) & 0xFF;
        case R_DMA2DAD+2: return (dma9[2].io.dest_addr >> 16) & 0xFF;
        case R_DMA2DAD+3: return (dma9[2].io.dest_addr >> 24) & 0xFF;

        case R_DMA3SAD+0: return dma9[3].io.src_addr & 0xFF;
        case R_DMA3SAD+1: return (dma9[3].io.src_addr >> 8) & 0xFF;
        case R_DMA3SAD+2: return (dma9[3].io.src_addr >> 16) & 0xFF;
        case R_DMA3SAD+3: return (dma9[3].io.src_addr >> 24) & 0xFF;
        case R_DMA3DAD+0: return dma9[3].io.dest_addr & 0xFF;
        case R_DMA3DAD+1: return (dma9[3].io.dest_addr >> 8) & 0xFF;
        case R_DMA3DAD+2: return (dma9[3].io.dest_addr >> 16) & 0xFF;
        case R_DMA3DAD+3: return (dma9[3].io.dest_addr >> 24) & 0xFF;

        case R_DMA0CNT_L+0: return dma9[0].io.word_count & 0xFF;
        case R_DMA0CNT_L+1: return (dma9[0].io.word_count >> 8) & 0xFF;
        case R_DMA1CNT_L+0: return dma9[1].io.word_count & 0xFF;
        case R_DMA1CNT_L+1: return (dma9[1].io.word_count >> 8) & 0xFF;
        case R_DMA2CNT_L+0: return dma9[2].io.word_count & 0xFF;
        case R_DMA2CNT_L+1: return (dma9[2].io.word_count >> 8) & 0xFF;
        case R_DMA3CNT_L+0: return dma9[3].io.word_count & 0xFF;
        case R_DMA3CNT_L+1: return (dma9[3].io.word_count >> 8) & 0xFF;

        case R_DMA0CNT_H+0:
        case R_DMA1CNT_H+0:
        case R_DMA2CNT_H+0:
        case R_DMA3CNT_H+0: {
            DMA_ch *ch = &dma9[DMA_CH_NUM(addr)];
            v = ch->io.dest_addr_ctrl << 5;
            v |= (ch->io.src_addr_ctrl & 1) << 7;
            return v; }

        case R_DMA0CNT_H+1:
        case R_DMA1CNT_H+1:
        case R_DMA2CNT_H+1:
        case R_DMA3CNT_H+1: {
            u32 chnum = DMA_CH_NUM(addr);
            DMA_ch *ch = &dma9[chnum];
            v = ch->io.src_addr_ctrl = (ch->io.src_addr_ctrl >> 1) & 1;
            v |= ch->io.repeat << 1;
            v |= ch->io.transfer_size << 2;
            v |= ch->io.start_timing << 4;
            v |= ch->io.irq_on_end << 6;

            v |= ch->io.enable << 7;
            return v;}

        case R_TM0CNT_L+0: return (timer7[0].read() >> 0) & 0xFF;
        case R_TM0CNT_L+1: return (timer7[0].read() >> 8) & 0xFF;
        case R_TM1CNT_L+0: return (timer7[1].read() >> 0) & 0xFF;
        case R_TM1CNT_L+1: return (timer7[1].read() >> 8) & 0xFF;
        case R_TM2CNT_L+0: return (timer7[2].read() >> 0) & 0xFF;
        case R_TM2CNT_L+1: return (timer7[2].read() >> 8) & 0xFF;
        case R_TM3CNT_L+0: return (timer7[3].read() >> 0) & 0xFF;
        case R_TM3CNT_L+1: return (timer7[3].read() >> 8) & 0xFF;

        case R_TM0CNT_H+1: // TIMERCNT upper, not used.
        case R_TM1CNT_H+1:
        case R_TM2CNT_H+1:
        case R_TM3CNT_H+1:
            return 0;

        case R_TM0CNT_H+0:
        case R_TM1CNT_H+0:
        case R_TM2CNT_H+0:
        case R_TM3CNT_H+0: {
            u32 tn = (addr >> 2) & 3;
            v = timer7[tn].divider.io;
            v |= timer7[tn].cascade << 2;
            v |= timer7[tn].irq_on_overflow << 6;
            v |= timer7[tn].enabled() << 7;
            return v;
        }

        case R7_VRAMSTAT:
            v = mem.vram.io.bank[NVC].enable && (mem.vram.io.bank[NVC].mst == 2);
            v |= (mem.vram.io.bank[NVD].enable && (mem.vram.io.bank[NVD].mst == 2)) << 1;
            return v;
        case R7_WRAMSTAT:
            return mem.io.RAM9.val;
        case R7_EXMEMSTAT+0:
            return (io.arm7.EXMEM & 0x7F) | (io.arm9.EXMEM & 0x80);
        case R7_EXMEMSTAT+1:
            return (io.arm9.EXMEM >> 8) | (1 << 5);

        case 0x04004008: // DSi stuff
        case 0x04004009:
        case 0x0400400a:
        case 0x0400400b:
        case 0x04004700:
        case 0x04004701:
            return 0;
    }
    //printf("\nUnhandled BUSRD7IO8 addr:%08x", addr);
    return 0;
}

void core::start_div()
{
    // Set time and needs calculation
    io.div.needs_calc = 1;
    u64 num_clks = 20;
    switch(io.div.mode) {
        case 0:
            num_clks = 18;
        case 1:
        case 2:
            num_clks = 34;
    }
    io.div.busy_until = clock.current9() + num_clks;
}

void core::start_sqrt()
{
    io.sqrt.needs_calc = true;
    io.div.busy_until = clock.current9() + 13;
}

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4146) // unary minus operator applied to unsigned type, result still unsigned
#endif

void core::div_calc()
{
    io.div.needs_calc = false;
    switch (io.div.mode) {
        case 0: {
            i32 num = static_cast<i32>(io.div.numer.data32[0]);
            i32 den = static_cast<i32>(io.div.denom.data32[0]);
            if (den == 0) {
                io.div.result.data32[0] = (num<0) ? 1:-1;
                io.div.result.data32[1] = (num<0) ? -1:0;
                io.div.remainder.u = num;
            }
            else if (num == INT32_MIN && den == -1) {
                io.div.result.u = 0x80000000;
            }
            else {
                io.div.result.u = static_cast<i64>(num / den);
                io.div.remainder.u = static_cast<i64>(num % den);
            }
            break; }

        case 1:
        case 3: {
            i64 num = static_cast<i64>(io.div.numer.u);
            i32 den = static_cast<i32>(io.div.denom.data32[0]);
            if (den == 0) {
                io.div.result.u = (num<0) ? 1:-1;
                io.div.remainder.u = num;
            }
            else if (num == INT64_MIN && den == -1) {
                io.div.result.u = 0x8000000000000000;
                io.div.remainder.u = 0;
            }
            else {
                io.div.result.u = num / den;
                io.div.remainder.u = (i64)(num % den);
            }
            break; }

        case 2: {
            i64 num = static_cast<i64>(io.div.numer.u);
            i64 den = static_cast<i64>(io.div.denom.u);
            if (den == 0) {
                io.div.result.u = (num<0) ? 1:-1;
                io.div.remainder.u = num;
            }
            else if (num == INT64_MIN && den == -1) {
                io.div.result.u = 0x8000000000000000;
                io.div.remainder.u = 0;
            }
            else {
                io.div.result.u = (i64)(num / den);
                io.div.remainder.u = (i64)(num % den);
            }
            break; }
    }

    io.div.by_zero |= io.div.denom.u == 0;
}

#ifdef _MSC_VER
#pragma warning(pop)
#endif

void core::sqrt_calc()
{
    io.sqrt.needs_calc = 0;
    u64 val;
    u32 res = 0;
    u64 rem = 0;
    u32 prod = 0;
    u32 nbits, topshift;

    if (io.sqrt.mode)
    {
        val = io.sqrt.param.u;
        nbits = 32;
        topshift = 62;
    }
    else
    {
        val = static_cast<u64>(io.sqrt.param.data32[0]);
        nbits = 16;
        topshift = 30;
    }

    for (u32 i = 0; i < nbits; i++)
    {
        rem = (rem << 2) + ((val >> topshift) & 0x3);
        val <<= 2;
        res <<= 1;

        prod = (res << 1) + 1;
        if (rem >= prod)
        {
            rem -= prod;
            res++;
        }
    }

    io.sqrt.result.u = res;
}

template<bool do_debug>
void core::buswr7_io8(u32 addr, u8 access, u32 val)
{
    switch(addr) {
        case R_RCNT+0:
            io.sio_data = (io.sio_data & 0xFF00) | val;
            return;
        case R_RCNT+1:
            io.sio_data = (io.sio_data & 0xFF) | (val << 8);
            return;
        case R_ROMCMD+0:
        case R_ROMCMD+1:
        case R_ROMCMD+2:
        case R_ROMCMD+3:
        case R_ROMCMD+4:
        case R_ROMCMD+5:
        case R_ROMCMD+6:
        case R_ROMCMD+7:
            if (!io.rights.nds_slot_is7) return;
            cart.write_cmd(addr - R_ROMCMD, val);
            return;

        case R7_SPICNT+0:
            spi.cnt.u = (spi.cnt.u & 0xFF80) | (val & 0b00000011);
            return;
        case R7_SPICNT+1: {
            if ((val & 0x80) && (!spi.cnt.bus_enable)) {
                // Enabling the bus releases hold on current device
                spi.chipsel = 0;
                SPI_release_hold();
            }
            // Don't change device while chipsel is hi?
            //if (spi.cnt.chipselect_hold)
            if (false)
                val = (val & 0b11111100) | spi.cnt.device;

            spi.cnt.u = (spi.cnt.u & 0xFF) | ((val & 0b11001111) << 8);
            return; }

        case R_POSTFLG:
            io.arm7.POSTFLG |= val & 1;
            return;

        case R7_WIFIWAITCNT+0:
            if (io.powcnt.wifi)
                io.powcnt.wifi_waitcnt = val;
            return;
        case R7_WIFIWAITCNT+1:
            return;
        case R7_HALTCNT+0: {
            u32 v = (val >> 6) & 3;
            switch(v) {
                case 0:
                    return;
                case 1:
                    printf("\nWARNING GBA MODE ATTEMPT");
                    return;
                case 2:
                    if constexpr(do_debug) dbgloglog(NDS_CAT_ARM7_HALT, DBGLS_INFO, "HALT ARM7 cyc:%lld", clock.current7());
                    arm7.halted = true;
                    if (arm7.cached_mode)
                        arm7.set_current_cached_ins_ends_block();
                    return;
                case 3:
                    printf("\nWARNING SLEEP MODE ATTEMPT");
                    return;
            }
            return; }

        case R7_POWCNT2+0:
            io.powcnt.speakers = val & 1;
            io.powcnt.wifi = (val >> 1) & 1;
            return;
        case R7_POWCNT2+1:
            return;

        case R_KEYCNT+0:
            io.arm7.button_irq.buttons = (io.arm7.button_irq.buttons & 0b1100000000) | val;
            return;
        case R_KEYCNT+1: {
            io.arm7.button_irq.buttons = (io.arm7.button_irq.buttons & 0xFF) | ((val & 0b11) << 8);
            u32 old_enable = io.arm7.button_irq.enable;
            io.arm7.button_irq.enable = (val >> 6) & 1;
            if ((old_enable == 0) && io.arm7.button_irq.enable) {
                printf("\nWARNING BUTTON IRQ ENABLED ARM9...");
            }
            io.arm7.button_irq.condition = (val >> 7) & 1;
            return; }

        case R_IPCFIFOCNT+0: {
            u32 old_bits = io.ipc.arm7.irq_on_send_fifo_empty & io.ipc.to_arm9.is_empty();
            io.ipc.arm7.irq_on_send_fifo_empty = (val >> 2) & 1;
            if ((val >> 3) & 1) { // arm7's send fifo is to_arm9
                io.ipc.to_arm9.empty();
            }
            // Edge-sensitive trigger...
            if (io.ipc.arm7.irq_on_send_fifo_empty & !old_bits) {
                update_IF7<do_debug>(IRQ_IPC_SEND_EMPTY);
            }
            return; }
        case R_IPCFIFOCNT+1: {
            u32 old_bits = io.ipc.arm7.irq_on_recv_fifo_not_empty & io.ipc.to_arm7.is_not_empty();
            io.ipc.arm7.irq_on_recv_fifo_not_empty = (val >> 2) & 1;
            if ((val >> 6) & 1) io.ipc.arm7.error = 0;
            io.ipc.arm7.fifo_enable = (val >> 7) & 1;
            u32 new_bits = io.ipc.arm7.irq_on_recv_fifo_not_empty & io.ipc.to_arm7.is_not_empty();
            if (!old_bits && new_bits) {
                update_IF7<do_debug>(IRQ_IPC_RECV_NOT_EMPTY);
            }
            return; }

        case R_IPCSYNC+0:
        case R_IPCSYNC+2:
        case R_IPCSYNC+3:
            return;
        case R_IPCSYNC+1: {
            io.ipc.arm9sync.dinput = io.ipc.arm7sync.doutput = val & 15;

            u32 send_irq = (val >> 5) & 1;
            if (send_irq) printf("\nIPC IRQ REQUEST!");

            if (send_irq && io.ipc.arm9sync.enable_irq_from_remote) {
                printf("\nARM7 SEND IPC SYNC");
                update_IF9<do_debug>(IRQ_IPC_SYNC);
            }
            io.ipc.arm7sync.enable_irq_from_remote = (val >> 6) & 1;
            return; }

        case R_IME: io.arm7.IME = val & 1; eval_irqs_7<true>(); return;
        case R_IME+1: return;
        case R_IME+2: return;
        case R_IME+3: return;
        case R_IF+0: io.arm7.IF &= ~val; eval_irqs_7<true>(); return;
        case R_IF+1: io.arm7.IF &= ~(val << 8); eval_irqs_7<true>(); return;
        case R_IF+2: io.arm7.IF &= ~(val << 16); eval_irqs_7<true>(); return;
        case R_IF+3: io.arm7.IF &= ~(val << 24); eval_irqs_7<true>(); return;

        case R_IE+0:
            io.arm7.IE = (io.arm7.IE & 0xFF00) | (val & 0xFF);
            io.arm7.IE &= ~0x80; // bit 7 doesn't get set on ARM9
            eval_irqs_7<true>();
            return;
        case R_IE+1:
            io.arm7.IE = (io.arm7.IE & 0xFF) | (val << 8);
            eval_irqs_7<true>();
            return;
        case R_IE+2:
            io.arm7.IE = (io.arm7.IE & 0xFF00FFFF) | (val << 16);
            eval_irqs_7<true>();
            return;
        case R_IE+3:
            io.arm7.IE = (io.arm7.IE & 0x00FFFFFF) | (val << 24);
            eval_irqs_7<true>();
            return;

        case R_DMA0SAD+0: dma7[0].io.src_addr = (dma7[0].io.src_addr & 0xFFFFFF00) | (val << 0); return; // DMA source address ch0
        case R_DMA0SAD+1: dma7[0].io.src_addr = (dma7[0].io.src_addr & 0xFFFF00FF) | (val << 8); return; // DMA source address ch0
        case R_DMA0SAD+2: dma7[0].io.src_addr = (dma7[0].io.src_addr & 0xFF00FFFF) | (val << 16); return; // DMA source address ch0
        case R_DMA0SAD+3: dma7[0].io.src_addr = (dma7[0].io.src_addr & 0x00FFFFFF) | ((val & 0x07) << 24); return; // DMA source address ch0
        case R_DMA0DAD+0: dma7[0].io.dest_addr = (dma7[0].io.dest_addr & 0xFFFFFF00) | (val << 0); return; // DMA source address ch0
        case R_DMA0DAD+1: dma7[0].io.dest_addr = (dma7[0].io.dest_addr & 0xFFFF00FF) | (val << 8); return; // DMA source address ch0
        case R_DMA0DAD+2: dma7[0].io.dest_addr = (dma7[0].io.dest_addr & 0xFF00FFFF) | (val << 16); return; // DMA source address ch0
        case R_DMA0DAD+3: dma7[0].io.dest_addr = (dma7[0].io.dest_addr & 0x00FFFFFF) | ((val & 0x07) << 24); return; // DMA source address ch0

        case R_DMA1SAD+0: dma7[1].io.src_addr = (dma7[1].io.src_addr & 0xFFFFFF00) | (val << 0); return; // DMA source address ch0
        case R_DMA1SAD+1: dma7[1].io.src_addr = (dma7[1].io.src_addr & 0xFFFF00FF) | (val << 8); return; // DMA source address ch0
        case R_DMA1SAD+2: dma7[1].io.src_addr = (dma7[1].io.src_addr & 0xFF00FFFF) | (val << 16); return; // DMA source address ch0
        case R_DMA1SAD+3: dma7[1].io.src_addr = (dma7[1].io.src_addr & 0x00FFFFFF) | ((val & 0x0F) << 24); return; // DMA source address ch0
        case R_DMA1DAD+0: dma7[1].io.dest_addr = (dma7[1].io.dest_addr & 0xFFFFFF00) | (val << 0); return; // DMA source address ch0
        case R_DMA1DAD+1: dma7[1].io.dest_addr = (dma7[1].io.dest_addr & 0xFFFF00FF) | (val << 8); return; // DMA source address ch0
        case R_DMA1DAD+2: dma7[1].io.dest_addr = (dma7[1].io.dest_addr & 0xFF00FFFF) | (val << 16); return; // DMA source address ch0
        case R_DMA1DAD+3: dma7[1].io.dest_addr = (dma7[1].io.dest_addr & 0x00FFFFFF) | ((val & 0x07) << 24); return; // DMA source address ch0

        case R_DMA2SAD+0: dma7[2].io.src_addr = (dma7[2].io.src_addr & 0xFFFFFF00) | (val << 0); return; // DMA source address ch0
        case R_DMA2SAD+1: dma7[2].io.src_addr = (dma7[2].io.src_addr & 0xFFFF00FF) | (val << 8); return; // DMA source address ch0
        case R_DMA2SAD+2: dma7[2].io.src_addr = (dma7[2].io.src_addr & 0xFF00FFFF) | (val << 16); return; // DMA source address ch0
        case R_DMA2SAD+3: dma7[2].io.src_addr = (dma7[2].io.src_addr & 0x00FFFFFF) | ((val & 0x0F) << 24); return; // DMA source address ch0
        case R_DMA2DAD+0: dma7[2].io.dest_addr = (dma7[2].io.dest_addr & 0xFFFFFF00) | (val << 0); return; // DMA source address ch0
        case R_DMA2DAD+1: dma7[2].io.dest_addr = (dma7[2].io.dest_addr & 0xFFFF00FF) | (val << 8); return; // DMA source address ch0
        case R_DMA2DAD+2: dma7[2].io.dest_addr = (dma7[2].io.dest_addr & 0xFF00FFFF) | (val << 16); return; // DMA source address ch0
        case R_DMA2DAD+3: dma7[2].io.dest_addr = (dma7[2].io.dest_addr & 0x00FFFFFF) | ((val & 0x07) << 24); return; // DMA source address ch0

        case R_DMA3SAD+0: dma7[3].io.src_addr = (dma7[3].io.src_addr & 0xFFFFFF00) | (val << 0); return; // DMA source address ch0
        case R_DMA3SAD+1: dma7[3].io.src_addr = (dma7[3].io.src_addr & 0xFFFF00FF) | (val << 8); return; // DMA source address ch0
        case R_DMA3SAD+2: dma7[3].io.src_addr = (dma7[3].io.src_addr & 0xFF00FFFF) | (val << 16); return; // DMA source address ch0
        case R_DMA3SAD+3: dma7[3].io.src_addr = (dma7[3].io.src_addr & 0x00FFFFFF) | ((val & 0x0F) << 24); return; // DMA source address ch0
        case R_DMA3DAD+0: dma7[3].io.dest_addr = (dma7[3].io.dest_addr & 0xFFFFFF00) | (val << 0); return; // DMA source address ch0
        case R_DMA3DAD+1: dma7[3].io.dest_addr = (dma7[3].io.dest_addr & 0xFFFF00FF) | (val << 8); return; // DMA source address ch0
        case R_DMA3DAD+2: dma7[3].io.dest_addr = (dma7[3].io.dest_addr & 0xFF00FFFF) | (val << 16); return; // DMA source address ch0
        case R_DMA3DAD+3: dma7[3].io.dest_addr = (dma7[3].io.dest_addr & 0x00FFFFFF) | ((val & 0x0F) << 24); return; // DMA source address ch0

        case R_DMA0CNT_L+0: dma7[0].io.word_count = (dma7[0].io.word_count & 0x3F00) | (val << 0); return;
        case R_DMA0CNT_L+1: dma7[0].io.word_count = (dma7[0].io.word_count & 0xFF) | ((val & 0x3F) << 8); return;
        case R_DMA1CNT_L+0: dma7[1].io.word_count = (dma7[1].io.word_count & 0x3F00) | (val << 0); return;
        case R_DMA1CNT_L+1: dma7[1].io.word_count = (dma7[1].io.word_count & 0xFF) | ((val & 0x3F) << 8); return;
        case R_DMA2CNT_L+0: dma7[2].io.word_count = (dma7[2].io.word_count & 0x3F00) | (val << 0); return;
        case R_DMA2CNT_L+1: dma7[2].io.word_count = (dma7[2].io.word_count & 0xFF) | ((val & 0x3F) << 8); return;
        case R_DMA3CNT_L+0: dma7[3].io.word_count = (dma7[3].io.word_count & 0xFF00) | (val << 0); return;
        case R_DMA3CNT_L+1: dma7[3].io.word_count = (dma7[3].io.word_count & 0xFF) | ((val & 0xFF) << 8); return;

        case R_DMA0CNT_H+0:
        case R_DMA1CNT_H+0:
        case R_DMA2CNT_H+0:
        case R_DMA3CNT_H+0: {
            DMA_ch *ch = &dma7[DMA_CH_NUM(addr)];
            ch->io.dest_addr_ctrl = (val >> 5) & 3;
            ch->io.src_addr_ctrl = (ch->io.src_addr_ctrl & 2) | ((val >> 7) & 1);
            return;}

        case R_DMA0CNT_H+1:
        case R_DMA1CNT_H+1:
        case R_DMA2CNT_H+1:
        case R_DMA3CNT_H+1: {
            u32 chnum = DMA_CH_NUM(addr);
            DMA_ch &ch = dma7[chnum];
            ch.io.src_addr_ctrl = (ch.io.src_addr_ctrl & 1) | ((val & 1) << 1);
            ch.io.repeat = (val >> 1) & 1;
            ch.io.transfer_size = (val >> 2) & 1;
            ch.io.start_timing = (val >> 4) & 3;
            if (ch.io.start_timing >= 2) {
                printf("\nWARN START TIMING %d NOT IMPLEMENT FOR ARM7 DMA!", ch.io.start_timing);
            }
            ch.io.irq_on_end = (val >> 6) & 1;
            u32 old_enable = ch.io.enable;
            ch.io.enable = (val >> 7) & 1;
            if ((ch.io.enable == 1) && (old_enable == 0)) {
                ch.op.first_run = 1;
                if (ch.io.start_timing == 0) {
                    dma7_start<do_debug>(ch, chnum);
                }
            }
            return;}

        case R_TM0CNT_H+1:
        case R_TM1CNT_H+1:
        case R_TM2CNT_H+1:
        case R_TM3CNT_H+1:
            return;

        case R_TM0CNT_H+0:
        case R_TM1CNT_H+0:
        case R_TM2CNT_H+0:
        case R_TM3CNT_H+0: {
            u32 tn = (addr >> 2) & 3;
            timer7[tn].write_cnt<do_debug>(val);
            return; }

        case R_TM0CNT_L+0: timer7[0].reload = (timer7[0].reload & 0xFF00) | val; return;
        case R_TM1CNT_L+0: timer7[1].reload = (timer7[1].reload & 0xFF00) | val; return;
        case R_TM2CNT_L+0: timer7[2].reload = (timer7[2].reload & 0xFF00) | val; return;
        case R_TM3CNT_L+0: timer7[3].reload = (timer7[3].reload & 0xFF00) | val; return;

        case R_TM0CNT_L+1: timer7[0].reload = (timer7[0].reload & 0xFF) | (val << 8); return;
        case R_TM1CNT_L+1: timer7[1].reload = (timer7[1].reload & 0xFF) | (val << 8); return;
        case R_TM2CNT_L+1: timer7[2].reload = (timer7[2].reload & 0xFF) | (val << 8); return;
        case R_TM3CNT_L+1: timer7[3].reload = (timer7[3].reload & 0xFF) | (val << 8); return;

        case R7_BIOSPROT+0:
            io.arm7.BIOSPROT = (io.arm7.BIOSPROT & 0xFF00) | val;
            return;
        case R7_BIOSPROT+1:
            io.arm7.BIOSPROT = (io.arm7.BIOSPROT & 0xFF) | (val << 8);
            return;
        case R7_EXMEMSTAT+0:
            io.arm7.EXMEM = val & 0x7F;
            return;
        case R7_EXMEMSTAT+1:
            return;

    }
    printf("\nUnhandled BUSWR7IO8 addr:%08x val:%08x", addr, val);
}

// --------------
template<bool do_debug>
u32 core::busrd9_io8(u32 addr, u8 access)
{
    u32 v;
    switch(addr) {
        case R_AUXSPICNT:
            if (io.rights.nds_slot_is7) return 0;
            return cart.read_spicnt() & 0xFF;
        case R_AUXSPICNT+1:
            if (io.rights.nds_slot_is7) return 0;
            return cart.read_spicnt() >> 8;
        case R_AUXSPIDATA:
            if (io.rights.nds_slot_is7) return 0;
            return cart.read_spi(0);
        case R_AUXSPIDATA+1:
            if (io.rights.nds_slot_is7) return 0;
            return cart.read_spi(1);
        case R9_POWCNT1+0:
            v = io.powcnt.lcd_enable;
            v |= ppu.eng2d[0].enable << 1;
            v |= re.enable << 2;
            v |= ge.enable << 3;
            return v;
        case R9_POWCNT1+1:
            v = ppu.eng2d[1].enable << 1;
            v |= ppu.io.display_swap << 7;
            return v;

        case R_KEYINPUT+0: // buttons!!!
            return controller.get_state(0);
        case R_KEYINPUT+1: // buttons!!!
            return controller.get_state(1);
        /*case R_EXTKEYIN+0:
            return controller.get_state(2);
        case R_EXTKEYIN+1:
            return 0;*/

        case R_IPCFIFOCNT+0:
            // send fifo from 9 is to_7
            v = io.ipc.to_arm7.is_empty();
            v |= io.ipc.to_arm7.is_full() << 1;
            //printf("\nFIFO7 EMPTY:%d FULL:%d?", v & 1, (v >> 1));
            v |= io.ipc.arm9.irq_on_send_fifo_empty << 2;
            return v;
        case R_IPCFIFOCNT+1:
            v = io.ipc.to_arm9.is_empty();
            v |= io.ipc.to_arm9.is_full() << 1;
            //printf("\nFIFO9 EMPTY:%d FULL:%d", v & 1, (v >> 1));
            v |= io.ipc.arm9.irq_on_recv_fifo_not_empty << 2;
            v |= io.ipc.arm9.error << 6;
            v |= io.ipc.arm9.fifo_enable << 7;
            return v;

        case R_IPCSYNC+0:
            return io.ipc.arm9sync.dinput;
        case R_IPCSYNC+1:
            v = io.ipc.arm9sync.doutput;
            v |= io.ipc.arm9sync.enable_irq_from_remote << 6;
            return v;
        case R_IPCSYNC+2:
        case R_IPCSYNC+3:
            return 0;

        case R9_DIVCNT+0:
            v = io.div.mode;
            return v;
        case R9_DIVCNT+1:
            v = io.div.by_zero << 6;
            v |= (clock.current9() < io.div.busy_until) << 7;
            return v;

        case R9_DIV_NUMER+0:
        case R9_DIV_NUMER+1:
        case R9_DIV_NUMER+2:
        case R9_DIV_NUMER+3:
        case R9_DIV_NUMER+4:
        case R9_DIV_NUMER+5:
        case R9_DIV_NUMER+6:
        case R9_DIV_NUMER+7:
            return io.div.numer.data[addr & 7];

        case R9_DIV_DENOM+0:
        case R9_DIV_DENOM+1:
        case R9_DIV_DENOM+2:
        case R9_DIV_DENOM+3:
        case R9_DIV_DENOM+4:
        case R9_DIV_DENOM+5:
        case R9_DIV_DENOM+6:
        case R9_DIV_DENOM+7:
            return io.div.denom.data[addr & 7];

        case R9_DIV_RESULT+0:
        case R9_DIV_RESULT+1:
        case R9_DIV_RESULT+2:
        case R9_DIV_RESULT+3:
        case R9_DIV_RESULT+4:
        case R9_DIV_RESULT+5:
        case R9_DIV_RESULT+6:
        case R9_DIV_RESULT+7:
            if (io.div.needs_calc) div_calc();
            return io.div.result.data[addr & 7];

        case R9_DIVREM_RESULT+0:
        case R9_DIVREM_RESULT+1:
        case R9_DIVREM_RESULT+2:
        case R9_DIVREM_RESULT+3:
        case R9_DIVREM_RESULT+4:
        case R9_DIVREM_RESULT+5:
        case R9_DIVREM_RESULT+6:
        case R9_DIVREM_RESULT+7:
            if (io.div.needs_calc) div_calc();
            return io.div.remainder.data[addr & 7];

        case R9_SQRTCNT+0:
            return io.sqrt.mode;
        case R9_SQRTCNT+1:
            return 0;

        case R9_SQRT_PARAM+0:
        case R9_SQRT_PARAM+1:
        case R9_SQRT_PARAM+2:
        case R9_SQRT_PARAM+3:
        case R9_SQRT_PARAM+4:
        case R9_SQRT_PARAM+5:
        case R9_SQRT_PARAM+6:
        case R9_SQRT_PARAM+7:
            return io.sqrt.param.data[addr & 7];

        case R9_SQRT_RESULT+0:
        case R9_SQRT_RESULT+1:
        case R9_SQRT_RESULT+2:
        case R9_SQRT_RESULT+3:
            if (io.sqrt.needs_calc) sqrt_calc();
            return io.sqrt.result.data[addr & 3];

        case R_IME: return io.arm9.IME;
        case R_IME+1: return 0;
        case R_IME+2: return 0;
        case R_IME+3: return 0;
        case R_IE+0: return io.arm9.IE & 0xFF;
        case R_IE+1: return (io.arm9.IE >> 8) & 0xFF;
        case R_IE+2: return (io.arm9.IE >> 16) & 0xFF;
        case R_IE+3: return (io.arm9.IE >> 24) & 0xFF;
        case R_IF+0: return io.arm9.IF & 0xFF;
        case R_IF+1: return (io.arm9.IF >> 8) & 0xFF;
        case R_IF+2: return (io.arm9.IF >> 16) & 0xFF;
        case R_IF+3: return (io.arm9.IF >> 24) & 0xFF;

        case R9_WRAMCNT:
            return mem.io.RAM9.val;
        case R9_EXMEMCNT+0:
            return io.arm9.EXMEM & 0xFF;
        case R9_EXMEMCNT+1:
            return (io.arm9.EXMEM >> 8) | (1 << 5);

        case R_DMA0SAD+0: return dma9[0].io.src_addr & 0xFF;
        case R_DMA0SAD+1: return (dma9[0].io.src_addr >> 8) & 0xFF;
        case R_DMA0SAD+2: return (dma9[0].io.src_addr >> 16) & 0xFF;
        case R_DMA0SAD+3: return (dma9[0].io.src_addr >> 24) & 0xFF;
        case R_DMA0DAD+0: return dma9[0].io.dest_addr & 0xFF;
        case R_DMA0DAD+1: return (dma9[0].io.dest_addr >> 8) & 0xFF;
        case R_DMA0DAD+2: return (dma9[0].io.dest_addr >> 16) & 0xFF;
        case R_DMA0DAD+3: return (dma9[0].io.dest_addr >> 24) & 0xFF;

        case R_DMA1SAD+0: return dma9[1].io.src_addr & 0xFF;
        case R_DMA1SAD+1: return (dma9[1].io.src_addr >> 8) & 0xFF;
        case R_DMA1SAD+2: return (dma9[1].io.src_addr >> 16) & 0xFF;
        case R_DMA1SAD+3: return (dma9[1].io.src_addr >> 24) & 0xFF;
        case R_DMA1DAD+0: return dma9[1].io.dest_addr & 0xFF;
        case R_DMA1DAD+1: return (dma9[1].io.dest_addr >> 8) & 0xFF;
        case R_DMA1DAD+2: return (dma9[1].io.dest_addr >> 16) & 0xFF;
        case R_DMA1DAD+3: return (dma9[1].io.dest_addr >> 24) & 0xFF;

        case R_DMA2SAD+0: return dma9[2].io.src_addr & 0xFF;
        case R_DMA2SAD+1: return (dma9[2].io.src_addr >> 8) & 0xFF;
        case R_DMA2SAD+2: return (dma9[2].io.src_addr >> 16) & 0xFF;
        case R_DMA2SAD+3: return (dma9[2].io.src_addr >> 24) & 0xFF;
        case R_DMA2DAD+0: return dma9[2].io.dest_addr & 0xFF;
        case R_DMA2DAD+1: return (dma9[2].io.dest_addr >> 8) & 0xFF;
        case R_DMA2DAD+2: return (dma9[2].io.dest_addr >> 16) & 0xFF;
        case R_DMA2DAD+3: return (dma9[2].io.dest_addr >> 24) & 0xFF;

        case R_DMA3SAD+0: return dma9[3].io.src_addr & 0xFF;
        case R_DMA3SAD+1: return (dma9[3].io.src_addr >> 8) & 0xFF;
        case R_DMA3SAD+2: return (dma9[3].io.src_addr >> 16) & 0xFF;
        case R_DMA3SAD+3: return (dma9[3].io.src_addr >> 24) & 0xFF;
        case R_DMA3DAD+0: return dma9[3].io.dest_addr & 0xFF;
        case R_DMA3DAD+1: return (dma9[3].io.dest_addr >> 8) & 0xFF;
        case R_DMA3DAD+2: return (dma9[3].io.dest_addr >> 16) & 0xFF;
        case R_DMA3DAD+3: return (dma9[3].io.dest_addr >> 24) & 0xFF;

        case R_DMA0CNT_L+0: return dma9[0].io.word_count & 0xFF;
        case R_DMA0CNT_L+1: return (dma9[0].io.word_count >> 8) & 0xFF;
        case R_DMA1CNT_L+0: return dma9[1].io.word_count & 0xFF;
        case R_DMA1CNT_L+1: return (dma9[1].io.word_count >> 8) & 0xFF;
        case R_DMA2CNT_L+0: return dma9[2].io.word_count & 0xFF;
        case R_DMA2CNT_L+1: return (dma9[2].io.word_count >> 8) & 0xFF;
        case R_DMA3CNT_L+0: return dma9[3].io.word_count & 0xFF;
        case R_DMA3CNT_L+1: return (dma9[3].io.word_count >> 8) & 0xFF;

        case R_DMA0CNT_H+0:
        case R_DMA1CNT_H+0:
        case R_DMA2CNT_H+0:
        case R_DMA3CNT_H+0: {
            DMA_ch &ch = dma9[DMA_CH_NUM(addr)];
            v = ch.io.word_count >> 16;
            v |= ch.io.dest_addr_ctrl << 5;
            v |= (ch.io.src_addr_ctrl & 1) << 7;
            return v; }

        case R_DMA0CNT_H+1:
        case R_DMA1CNT_H+1:
        case R_DMA2CNT_H+1:
        case R_DMA3CNT_H+1: {
            u32 chnum = DMA_CH_NUM(addr);
            DMA_ch &ch = dma9[chnum];
            v = ch.io.src_addr_ctrl = (ch.io.src_addr_ctrl >> 1) & 1;
            v |= ch.io.repeat << 1;
            v |= ch.io.transfer_size << 2;
            v |= ch.io.start_timing << 3;
            v |= ch.io.irq_on_end << 6;

            v |= ch.io.enable << 7;
            return v;}

        case R9_DMAFIL+0:
        case R9_DMAFIL+1:
        case R9_DMAFIL+2:
        case R9_DMAFIL+3:
        case R9_DMAFIL+4:
        case R9_DMAFIL+5:
        case R9_DMAFIL+6:
        case R9_DMAFIL+7:
        case R9_DMAFIL+8:
        case R9_DMAFIL+9:
        case R9_DMAFIL+10:
        case R9_DMAFIL+11:
        case R9_DMAFIL+12:
        case R9_DMAFIL+13:
        case R9_DMAFIL+14:
        case R9_DMAFIL+15:
            return io.dma.filldata[addr & 15];

        case R_TM0CNT_L+0: return (timer9[0].read() >> 0) & 0xFF;
        case R_TM0CNT_L+1: return (timer9[0].read() >> 8) & 0xFF;
        case R_TM1CNT_L+0: return (timer9[1].read() >> 0) & 0xFF;
        case R_TM1CNT_L+1: return (timer9[1].read() >> 8) & 0xFF;
        case R_TM2CNT_L+0: return (timer9[2].read() >> 0) & 0xFF;
        case R_TM2CNT_L+1: return (timer9[2].read() >> 8) & 0xFF;
        case R_TM3CNT_L+0: return (timer9[3].read() >> 0) & 0xFF;
        case R_TM3CNT_L+1: return (timer9[3].read() >> 8) & 0xFF;

        case R_TM0CNT_H+1: // TIMERCNT upper, not used.
        case R_TM1CNT_H+1:
        case R_TM2CNT_H+1:
        case R_TM3CNT_H+1:
            return 0;

        case R9_VRAMCNT+0:
        case R9_VRAMCNT+1:
        case R9_VRAMCNT+2:
        case R9_VRAMCNT+3:
        case R9_VRAMCNT+4:
        case R9_VRAMCNT+5:
        case R9_VRAMCNT+6:
        case R9_VRAMCNT+8:
        case R9_VRAMCNT+9: {
            u32 bank_num = addr - R9_VRAMCNT;
            if (bank_num >= 8) bank_num--;

            v = mem.vram.io.bank[bank_num].mst;
            v |= mem.vram.io.bank[bank_num].ofs << 3;
            v |= mem.vram.io.bank[bank_num].enable << 7;
            return v; }

        case R_TM0CNT_H+0:
        case R_TM1CNT_H+0:
        case R_TM2CNT_H+0:
        case R_TM3CNT_H+0: {
            u32 tn = (addr >> 2) & 3;
            v = timer9[tn].divider.io;
            v |= timer9[tn].cascade << 2;
            v |= timer9[tn].irq_on_overflow << 6;
            v |= timer9[tn].enabled() << 7;
            return v;
        }

        case R9_DIVCNT+2:
        case R9_DIVCNT+3:
        case 0x04004000:
            // NDS thing
            return 0;
        case 0x04004008: // new DSi stuff libnds cares about?
        case 0x04004009:
        case 0x0400400A:
        case 0x0400400B:
        case 0x04004010:
        case 0x04004011:
        case 0x04004004:
        case 0x04004005:
            return 0;
    }
    printf("\nUnhandled BUSRD9IO8 addr:%08x", addr);
    return 0;
}

template<bool do_debug>
void core::buswr9_io8(u32 addr, u8 access, u32 val)
{
    switch(addr) {
        case R_ROMCMD+0:
        case R_ROMCMD+1:
        case R_ROMCMD+2:
        case R_ROMCMD+3:
        case R_ROMCMD+4:
        case R_ROMCMD+5:
        case R_ROMCMD+6:
        case R_ROMCMD+7:
            if (io.rights.nds_slot_is7) return;
            cart.write_cmd(addr - R_ROMCMD, val);
            return;

        case R_POSTFLG:
            io.arm9.POSTFLG |= val & 1;
            io.arm9.POSTFLG = (io.arm9.POSTFLG & 1) | (val & 2);
            return;
        case R9_POWCNT1+0:
            io.powcnt.lcd_enable = val & 1;
            ppu.eng2d[0].enable = (val >> 1) & 1;
            re.enable = (val >> 2) & 1;
            ge.enable = (val >> 3) & 1;
            return;
        case R9_POWCNT1+1:
            ppu.eng2d[1].enable = (val >> 1) & 1;
            ppu.io.display_swap = (val >> 7) & 1;
            return;
        case R9_POWCNT1+2:
        case R9_POWCNT1+3:
            return;
        case R_KEYCNT+0:
            io.arm9.button_irq.buttons = (io.arm9.button_irq.buttons & 0b1100000000) | val;
            return;
        case R_KEYCNT+1: {
            io.arm9.button_irq.buttons = (io.arm9.button_irq.buttons & 0xFF) | ((val & 0b11) << 8);
            u32 old_enable = io.arm9.button_irq.enable;
            io.arm9.button_irq.enable = (val >> 6) & 1;
            if ((old_enable == 0) && io.arm9.button_irq.enable) {
                printf("\nWARNING BUTTON IRQ ENABLED ARM9...");
            }
            io.arm9.button_irq.condition = (val >> 7) & 1;
            return; }

        case R_IPCFIFOCNT+0: {
            u32 old_bits = io.ipc.arm9.irq_on_send_fifo_empty & io.ipc.to_arm7.is_empty();
            io.ipc.arm9.irq_on_send_fifo_empty = (val >> 2) & 1;
            if ((val >> 3) & 1) { // arm9's send fifo is to_arm7
                io.ipc.to_arm7.empty();
            }
            // Edge-sensitive trigger...
            if (io.ipc.arm9.irq_on_send_fifo_empty & !old_bits) {
                update_IF9<do_debug>(IRQ_IPC_SEND_EMPTY);
            }
            return; }
        case R_IPCFIFOCNT+1: {
            u32 old_bits = io.ipc.arm9.irq_on_recv_fifo_not_empty & io.ipc.to_arm9.is_not_empty();
            io.ipc.arm9.irq_on_recv_fifo_not_empty = (val >> 2) & 1;
            if ((val >> 6) & 1) io.ipc.arm9.error = 0;
            io.ipc.arm9.fifo_enable = (val >> 7) & 1;
            u32 new_bits = io.ipc.arm9.irq_on_recv_fifo_not_empty & io.ipc.to_arm9.is_not_empty();
            if (!old_bits && new_bits) {
                update_IF9<do_debug>(IRQ_IPC_RECV_NOT_EMPTY);
            }
            return; }

        case R_IPCSYNC+0:
        case R_IPCSYNC+2:
        case R_IPCSYNC+3:
            return;
        case R_IPCSYNC+1: {
            io.ipc.arm7sync.dinput = io.ipc.arm9sync.doutput = val & 15;
            u32 send_irq = (val >> 5) & 1;
            if (send_irq && io.ipc.arm7sync.enable_irq_from_remote) {
                printf("\nARM9 SEND IPC SYNC");
                update_IF7<do_debug>(IRQ_IPC_SYNC);
            }
            io.ipc.arm9sync.enable_irq_from_remote = (val >> 6) & 1;
            return;}

        case R_IME: {
            io.arm9.IME = val & 1;
            eval_irqs_9<true>();
            return;
        }
        case R_IME+1: return;
        case R_IME+2: return;
        case R_IME+3: return;
        case R_IF+0: io.arm9.IF &= ~val; eval_irqs_9<true>(); return;
        case R_IF+1: io.arm9.IF &= ~(val << 8); eval_irqs_9<true>(); return;
        case R_IF+2: io.arm9.IF &= ~(val << 16); eval_irqs_9<true>(); return;
        case R_IF+3: io.arm9.IF &= ~(val << 24); eval_irqs_9<true>(); return;

        case R9_DIVCNT+0:
            io.div.mode = val & 3;
            if (io.div.mode == 3) {
                printf("\nFORBIDDEN DIV MODE");
            }
            start_div();
            return;
        case R9_DIVCNT+1:
            io.div.by_zero = (val >> 6) & 1;
            start_div();
            return;

        case R9_DIV_NUMER+0:
        case R9_DIV_NUMER+1:
        case R9_DIV_NUMER+2:
        case R9_DIV_NUMER+3:
        case R9_DIV_NUMER+4:
        case R9_DIV_NUMER+5:
        case R9_DIV_NUMER+6:
        case R9_DIV_NUMER+7:
            io.div.numer.data[addr & 7] = val;
            start_div();
            return;

        case R9_DIV_DENOM+0:
        case R9_DIV_DENOM+1:
        case R9_DIV_DENOM+2:
        case R9_DIV_DENOM+3:
        case R9_DIV_DENOM+4:
        case R9_DIV_DENOM+5:
        case R9_DIV_DENOM+6:
        case R9_DIV_DENOM+7:
            io.div.denom.data[addr & 7] = val;
            start_div();
            return;

        case R9_SQRTCNT+0:
            io.sqrt.mode = val & 1;
            start_sqrt();
            return;
        case R9_SQRTCNT+1:
            start_sqrt();
            return;

        case R9_SQRT_PARAM+0:
        case R9_SQRT_PARAM+1:
        case R9_SQRT_PARAM+2:
        case R9_SQRT_PARAM+3:
        case R9_SQRT_PARAM+4:
        case R9_SQRT_PARAM+5:
        case R9_SQRT_PARAM+6:
        case R9_SQRT_PARAM+7:
            io.sqrt.param.data[addr & 7] = val;
            start_sqrt();
            return;

        case R_IE+0:
            io.arm9.IE = (io.arm9.IE & 0xFFFFFF00) | (val & 0xFF);
            io.arm9.IE &= ~0x80; // bit 7 doesn't get set on ARM9
            eval_irqs_9<true>();
            return;
        case R_IE+1:
            io.arm9.IE = (io.arm9.IE & 0xFFFF00FF) | (val << 8);
            eval_irqs_9<true>();
            return;
        case R_IE+2:
            io.arm9.IE = (io.arm9.IE & 0xFF00FFFF) | (val << 16);
            eval_irqs_9<true>();
            return;
        case R_IE+3:
            io.arm9.IE = (io.arm9.IE & 0x00FFFFFF) | (val << 24);
            eval_irqs_9<true>();
            return;

        case R_DMA0SAD+0: dma9[0].io.src_addr = (dma9[0].io.src_addr & 0xFFFFFF00) | (val << 0); return;
        case R_DMA0SAD+1: dma9[0].io.src_addr = (dma9[0].io.src_addr & 0xFFFF00FF) | (val << 8); return;
        case R_DMA0SAD+2: dma9[0].io.src_addr = (dma9[0].io.src_addr & 0xFF00FFFF) | (val << 16); return;
        case R_DMA0SAD+3: dma9[0].io.src_addr = (dma9[0].io.src_addr & 0x00FFFFFF) | ((val & 0x0F) << 24); return;
        case R_DMA0DAD+0: dma9[0].io.dest_addr = (dma9[0].io.dest_addr & 0xFFFFFF00) | (val << 0); return;
        case R_DMA0DAD+1: dma9[0].io.dest_addr = (dma9[0].io.dest_addr & 0xFFFF00FF) | (val << 8); return;
        case R_DMA0DAD+2: dma9[0].io.dest_addr = (dma9[0].io.dest_addr & 0xFF00FFFF) | (val << 16); return;
        case R_DMA0DAD+3: dma9[0].io.dest_addr = (dma9[0].io.dest_addr & 0x00FFFFFF) | ((val & 0x0F) << 24); return;

        case R_DMA1SAD+0: dma9[1].io.src_addr = (dma9[1].io.src_addr & 0xFFFFFF00) | (val << 0); return;
        case R_DMA1SAD+1: dma9[1].io.src_addr = (dma9[1].io.src_addr & 0xFFFF00FF) | (val << 8); return;
        case R_DMA1SAD+2: dma9[1].io.src_addr = (dma9[1].io.src_addr & 0xFF00FFFF) | (val << 16); return;
        case R_DMA1SAD+3: dma9[1].io.src_addr = (dma9[1].io.src_addr & 0x00FFFFFF) | ((val & 0x0F) << 24); return;
        case R_DMA1DAD+0: dma9[1].io.dest_addr = (dma9[1].io.dest_addr & 0xFFFFFF00) | (val << 0); return;
        case R_DMA1DAD+1: dma9[1].io.dest_addr = (dma9[1].io.dest_addr & 0xFFFF00FF) | (val << 8); return;
        case R_DMA1DAD+2: dma9[1].io.dest_addr = (dma9[1].io.dest_addr & 0xFF00FFFF) | (val << 16); return;
        case R_DMA1DAD+3: dma9[1].io.dest_addr = (dma9[1].io.dest_addr & 0x00FFFFFF) | ((val & 0x0F) << 24); return;

        case R_DMA2SAD+0: dma9[2].io.src_addr = (dma9[2].io.src_addr & 0xFFFFFF00) | (val << 0); return;
        case R_DMA2SAD+1: dma9[2].io.src_addr = (dma9[2].io.src_addr & 0xFFFF00FF) | (val << 8); return;
        case R_DMA2SAD+2: dma9[2].io.src_addr = (dma9[2].io.src_addr & 0xFF00FFFF) | (val << 16); return;
        case R_DMA2SAD+3: dma9[2].io.src_addr = (dma9[2].io.src_addr & 0x00FFFFFF) | ((val & 0x0F) << 24); return;
        case R_DMA2DAD+0: dma9[2].io.dest_addr = (dma9[2].io.dest_addr & 0xFFFFFF00) | (val << 0); return;
        case R_DMA2DAD+1: dma9[2].io.dest_addr = (dma9[2].io.dest_addr & 0xFFFF00FF) | (val << 8); return;
        case R_DMA2DAD+2: dma9[2].io.dest_addr = (dma9[2].io.dest_addr & 0xFF00FFFF) | (val << 16); return;
        case R_DMA2DAD+3: dma9[2].io.dest_addr = (dma9[2].io.dest_addr & 0x00FFFFFF) | ((val & 0x0F) << 24); return;

        case R_DMA3SAD+0: dma9[3].io.src_addr = (dma9[3].io.src_addr & 0xFFFFFF00) | (val << 0); return;
        case R_DMA3SAD+1: dma9[3].io.src_addr = (dma9[3].io.src_addr & 0xFFFF00FF) | (val << 8); return;
        case R_DMA3SAD+2: dma9[3].io.src_addr = (dma9[3].io.src_addr & 0xFF00FFFF) | (val << 16); return;
        case R_DMA3SAD+3: dma9[3].io.src_addr = (dma9[3].io.src_addr & 0x00FFFFFF) | ((val & 0x0F) << 24); return;
        case R_DMA3DAD+0: dma9[3].io.dest_addr = (dma9[3].io.dest_addr & 0xFFFFFF00) | (val << 0); return;
        case R_DMA3DAD+1: dma9[3].io.dest_addr = (dma9[3].io.dest_addr & 0xFFFF00FF) | (val << 8); return;
        case R_DMA3DAD+2: dma9[3].io.dest_addr = (dma9[3].io.dest_addr & 0xFF00FFFF) | (val << 16); return;
        case R_DMA3DAD+3: dma9[3].io.dest_addr = (dma9[3].io.dest_addr & 0x00FFFFFF) | ((val & 0x0F) << 24); return;

        case R_DMA0CNT_L+0: dma9[0].io.word_count = (dma9[0].io.word_count & 0x1FFF00) | (val << 0); return;
        case R_DMA0CNT_L+1: dma9[0].io.word_count = (dma9[0].io.word_count & 0x1F00FF) | (val << 8); return;
        case R_DMA1CNT_L+0: dma9[1].io.word_count = (dma9[1].io.word_count & 0x1FFF00) | (val << 0); return;
        case R_DMA1CNT_L+1: dma9[1].io.word_count = (dma9[1].io.word_count & 0x1F00FF) | (val << 8); return;
        case R_DMA2CNT_L+0: dma9[2].io.word_count = (dma9[2].io.word_count & 0x1FFF00) | (val << 0); return;
        case R_DMA2CNT_L+1: dma9[2].io.word_count = (dma9[2].io.word_count & 0x1F00FF) | (val << 8); return;
        case R_DMA3CNT_L+0: dma9[3].io.word_count = (dma9[3].io.word_count & 0x1FFF00) | (val << 0); return;
        case R_DMA3CNT_L+1: dma9[3].io.word_count = (dma9[3].io.word_count & 0x1F00FF) | (val << 8); return;

        case R_DMA0CNT_H+0:
        case R_DMA1CNT_H+0:
        case R_DMA2CNT_H+0:
        case R_DMA3CNT_H+0: {
            DMA_ch &ch = dma9[DMA_CH_NUM(addr)];
            ch.io.word_count = (ch.io.word_count & 0xFFFF) | ((val & 31) << 16);
            ch.io.dest_addr_ctrl = (val >> 5) & 3;
            ch.io.src_addr_ctrl = (ch.io.src_addr_ctrl & 2) | ((val >> 7) & 1);
            return;}

        case R_DMA0CNT_H+1:
        case R_DMA1CNT_H+1:
        case R_DMA2CNT_H+1:
        case R_DMA3CNT_H+1: {
            u32 chnum = DMA_CH_NUM(addr);
            DMA_ch &ch = dma9[chnum];
            ch.io.src_addr_ctrl = (ch.io.src_addr_ctrl & 1) | ((val & 1) << 1);
            ch.io.repeat = (val >> 1) & 1;
            ch.io.transfer_size = (val >> 2) & 1;
            ch.io.start_timing = (val >> 3) & 7;
            ch.io.irq_on_end = (val >> 6) & 1;

            u32 old_enable = ch.io.enable;
            ch.io.enable = (val >> 7) & 1;
            if ((ch.io.enable == 1) && (old_enable == 0)) {
                ch.op.first_run = 1;
                if (ch.io.start_timing == 0) {
                    dma9_start<do_debug>(ch, chnum);
                }
            }

            if (ch.io.start_timing == DMA_GE_FIFO) {
                ch.op.first_run = 1;
                if (ge.fifo.len < 128) {
                    dma9_start<do_debug>(ch, chnum);
                }
            }
            else if (ch.io.start_timing > 3) {
                if (ch.io.start_timing != 5) printf("\nwarn DMA9 Start timing:%d", ch.io.start_timing);
            }
            return;}

        case R9_DMAFIL+0:
        case R9_DMAFIL+1:
        case R9_DMAFIL+2:
        case R9_DMAFIL+3:
        case R9_DMAFIL+4:
        case R9_DMAFIL+5:
        case R9_DMAFIL+6:
        case R9_DMAFIL+7:
        case R9_DMAFIL+8:
        case R9_DMAFIL+9:
        case R9_DMAFIL+10:
        case R9_DMAFIL+11:
        case R9_DMAFIL+12:
        case R9_DMAFIL+13:
        case R9_DMAFIL+14:
        case R9_DMAFIL+15:
            io.dma.filldata[addr & 15] = val;
            return;
        case R9_VRAMCNT+0:
        case R9_VRAMCNT+1:
        case R9_VRAMCNT+2:
        case R9_VRAMCNT+3:
        case R9_VRAMCNT+4:
        case R9_VRAMCNT+5:
        case R9_VRAMCNT+6:
        case R9_VRAMCNT+8:
        case R9_VRAMCNT+9: {
            u32 bank_num = addr - R9_VRAMCNT;
            if (bank_num >= 8) bank_num--;


            if ((bank_num < 2) || (bank_num >= 7)) mem.vram.io.bank[bank_num].mst = val & 3;
            else mem.vram.io.bank[bank_num].mst = val & 7;

            mem.vram.io.bank[bank_num].ofs = (val >> 3) & 3;
            mem.vram.io.bank[bank_num].enable = (val >> 7) & 1;

            //printf("\nWRITE VRAM val:%02x CNT:%d MST:%d OFS:%d enable:%d", val, bank_num, mem.vram.io.bank[bank_num].mst, mem.vram.io.bank[bank_num].ofs, mem.vram.io.bank[bank_num].enable);
            VRAM_resetup_banks();
            return; }

        case R_TM0CNT_H+1:
        case R_TM1CNT_H+1:
        case R_TM2CNT_H+1:
        case R_TM3CNT_H+1:
            return;

        case R_TM0CNT_H+0:
        case R_TM1CNT_H+0:
        case R_TM2CNT_H+0:
        case R_TM3CNT_H+0: {
            u32 tn = (addr >> 2) & 3;
            timer9[tn].write_cnt<do_debug>(val);
            return; }

        case R_TM0CNT_L+0: timer9[0].reload = (timer9[0].reload & 0xFF00) | val; return;
        case R_TM1CNT_L+0: timer9[1].reload = (timer9[1].reload & 0xFF00) | val; return;
        case R_TM2CNT_L+0: timer9[2].reload = (timer9[2].reload & 0xFF00) | val; return;
        case R_TM3CNT_L+0: timer9[3].reload = (timer9[3].reload & 0xFF00) | val; return;

        case R_TM0CNT_L+1: timer9[0].reload = (timer9[0].reload & 0xFF) | (val << 8); return;
        case R_TM1CNT_L+1: timer9[1].reload = (timer9[1].reload & 0xFF) | (val << 8); return;
        case R_TM2CNT_L+1: timer9[2].reload = (timer9[2].reload & 0xFF) | (val << 8); return;
        case R_TM3CNT_L+1: timer9[3].reload = (timer9[3].reload & 0xFF) | (val << 8); return;

        case R9_WRAMCNT: {
            mem.io.RAM9.val = val;
            switch (val & 3) {
                case 0: // 0 = 32k/0K, open-bus
                    mem.io.RAM9.base = 0;
                    mem.io.RAM9.mask = 0x7FFF;
                    mem.io.RAM9.disabled = 0;
                    mem.io.RAM7.base = 0;
                    mem.io.RAM7.mask = 0;
                    mem.io.RAM7.disabled = 1;
                    break;
                case 1: // 1 = hi 16K/ lo16K,
                    mem.io.RAM9.base = 0x4000;
                    mem.io.RAM9.mask = 0x3FFF;
                    mem.io.RAM9.disabled = 0;
                    mem.io.RAM7.base = 0;
                    mem.io.RAM7.mask = 0x3FFF;
                    mem.io.RAM7.disabled = 0;
                    break;
                case 2: // 2 = lo 16k/ hi16k,
                    mem.io.RAM9.base = 0;
                    mem.io.RAM9.mask = 0x3FFF;
                    mem.io.RAM9.disabled = 0;
                    mem.io.RAM7.base = 0x4000;
                    mem.io.RAM7.mask = 0x3FFF;
                    mem.io.RAM7.disabled = 0;
                    break;
                case 3: // 3 = 0k / 32k
                    mem.io.RAM9.base = 0;
                    mem.io.RAM9.mask = 0;
                    mem.io.RAM9.disabled = 1;
                    mem.io.RAM7.base = 0;
                    mem.io.RAM7.mask = 0x7FFF;
                    mem.io.RAM7.disabled = 0;
            }
            return; }

        case R9_EXMEMCNT+0:
            io.arm9.EXMEM = (io.arm9.EXMEM & 0xFF00) | val;
            io.rights.gba_slot = ((val >> 7) & 1) ^ 1;
            return;
        case R9_EXMEMCNT+1:
            io.arm9.EXMEM = (io.arm9.EXMEM & 0xFF) | (val << 8);
            io.rights.nds_slot_is7 = ((val >> 3) & 1);
            io.rights.main_memory = ((val >> 7) & 1);
            return;
    }
    printf("\nUnhandled BUSWR9IO8 addr:%08x val:%08x", addr, val);
}

// -----

static u32 busrd9_apu(u32 addr, u8 sz, u8 access){
    static int already_did = 0;
    if (!already_did) {
        already_did = 1;
        printf("\nWARN: APU READ9!");
    }
    return 0;
}


template<u8 sz>
static u32 ins_timing9(void *ptr, u32 addr, u8 access) {
    //auto *th = static_cast<core *>(ptr);
    //return th->ins_timing<sz>(addr, access);
    return 1;
}

template<u8 sz>
static u32 ins_timing7(void *ptr, u32 addr, u8 access) {
    //auto *th = static_cast<core *>(ptr);
    //return th->ins_timing<sz>(addr, access);
    return 1;
}

static u32 read_trace_cpu9(void *ptr, u32 addr, u8 sz) {
    u32 v;
    auto *th = static_cast<core *>(ptr);
    u64 clock = th->clock.master_cycle_count9;
    u64 ws = th->waitstates.current_transaction;
    switch (sz) {
        case 1: v = core::mainbus_read9<1, false, true>(ptr, addr, 0); break;
        case 2: v = core::mainbus_read9<2, false, true>(ptr, addr, 0); break;
        case 4: v = core::mainbus_read9<4, false, true>(ptr, addr, 0); break;
        default: NOGOHERE;
    }
    th->clock.master_cycle_count9 = clock;
    th->waitstates.current_transaction = ws;
    return v;
}

static u32 read_trace_cpu7(void *ptr, u32 addr, u8 sz) {
    u32 v;
    /*auto *th = static_cast<core *>(ptr);
    u64 clock = th->clock.master_cycle_count7;
    u64 ws = th->waitstates.current_transaction;*/
    switch (sz) {
        case 1: v = core::mainbus_read7<1, false, true>(ptr, addr, 0); break;
        case 2: v = core::mainbus_read7<2, false, true>(ptr, addr, 0); break;
        case 4: v = core::mainbus_read7<4, false, true>(ptr, addr, 0); break;
        default: NOGOHERE;
    }
    /*th->waitstates.current_transaction = ws;
    th->clock.master_cycle_count7 = clock;*/
    return v;
}

template<bool do_debug>
static void* get_cached_block7(void *ptr, u32 addr) {
    auto *th = static_cast<core *>(ptr);
    u32 top9 = addr & 0x0F80'0000;
    arm7_cached_block_t *bl;
    switch (top9) {
        case 0x0000'0000:
            // BIOS

            bl = th->mem.bios7_block_cache.peek_block<0>(addr, 0);
            return bl;
        case 0x0200'0000: // Main RAM
            addr &= 0x3F'FFFF;
            return th->mem.RAM_block_cache.get_block<0>(addr, 1);
        case 0x0300'0000: // Shared WRAM. 0-32kb
            // on ARM7, when disabled, this just mirrors normal WRAM
            if (th->mem.io.RAM7.disabled) {
                addr &= 0xFFFF;
                return th->mem.WRAM_arm7_block_cache.get_block<0>(addr, 3);
            }
            // Elsewise....
            addr &= th->mem.io.RAM7.mask;
            addr += th->mem.io.RAM7.base;
            return th->mem.WRAM_share_block_cache.get_block<0>(addr, 2);
        case 0x0380'0000: // ARM7-WRAM
            addr &= 0xFFFF;
            return th->mem.WRAM_arm7_block_cache.get_block<0>(addr, 3);
        case 0x0600'0000: { // VRAM
            u32 bank = (addr >> 17) & 1;
            if (th->mem.vram.map.arm7[bank]) {
                addr &= 0xF'FFFF;
                return th->mem.vram.block_cache.get_block<0>(addr, 4);
            }
            break;
        }
    }
    printf("\nARM7 CACHED BLOCK REQUEST FROM %08x", addr);
    NOGOHERE;
}

template<bool do_debug>
static void* get_cached_block9(void *ptr, u32 addr) {
    auto *th = static_cast<core *>(ptr);
    if (addr >= 0xFFFF'0000) {
        addr &= 0xFFF;
        return th->mem.bios9_block_cache.peek_block<0>(addr, 0);
    }
    u32 top8 = (addr >> 24) & 0xFF;
    switch (top8) {
        case 0x02: // Main RAM
            addr &= 0x3F'FFFF;
            return th->mem.RAM_block_cache.get_block<1>(addr, 1);
        case 0x03: // ARM9 shared RAM
            addr &= th->mem.io.RAM9.mask;
            addr += th->mem.io.RAM9.base;
            return th->mem.WRAM_share_block_cache.get_block<1>(addr, 2);
        case 0x06: { // VRAM
            u8 *ptr = th->mem.vram.map.arm9[NDSVRAMSHIFT(addr) & NDSVRAMMASK];
            if (ptr) {
                addr &= 0xF'FFFF;
                return th->mem.vram.block_cache.get_block<1>(addr, 4);
            }
            break;
        }
    }
    printf("\nGET CACHED BLOCK.9 ADDR %08x", addr);
    NOGOHERE;
}

template<bool do_debug>
void register_cached_block7(void *ptr, void *block_ptr) {
    auto *th = static_cast<core *>(ptr);
    auto *bl = static_cast<arm7_cached_block_t *>(block_ptr);
    switch (bl->mem_region) {
        case 0: // BIOS, ROM
            return;
        case 1: // Main RAM
            th->mem.RAM_block_cache.commit<0>(block_ptr);
            return;
        case 2: // Shared WRAM. 0-32kb
            th->mem.WRAM_share_block_cache.commit<0>(block_ptr);
            return;
        case 3: // ARM7-WRAM
            th->mem.WRAM_arm7_block_cache.commit<0>(block_ptr);
            return;
        case 4: // VRAM
            th->mem.vram.block_cache.commit<0>(block_ptr);
            return;
        default:
            break;
    }
    printf("\nARM7 CACHED BLOCK REGISTER REQUEST TO INVALID MEM REGION %d", bl->mem_region);
    NOGOHERE;

}

template<bool do_debug>
void register_cached_block9(void *ptr, void *block_ptr) {
    auto *th = static_cast<core *>(ptr);
    auto *bl = static_cast<arm9_cached_block_t *>(block_ptr);
    switch (bl->mem_region) {
        case 0: // BIOS
            return;
        case 1: // Main RAM
            th->mem.RAM_block_cache.commit<1>(block_ptr);
            return;
        case 2:
            th->mem.WRAM_share_block_cache.commit<1>(block_ptr);
            return;
        case 4: // VRAM
            th->mem.vram.block_cache.commit<1>(block_ptr);
            return;
    }
    printf("\nREGISTER CACHED BLOCK.9 FAILED REGION %d", bl->mem_region);
    NOGOHERE;

}

template<bool do_debug>
void core::run_block(void *ptr, u64 num_cycles, u64 clock, u32 jitter)
{
    auto *th = static_cast<core *>(ptr);

    th->waitstates.current_transaction = 0;
    th->clock.cycles7 += static_cast<i64>(num_cycles);
    th->clock.cycles9 += static_cast<i64>(num_cycles);

    th->arm7_ins = true;
    if (th->arm7.cached_mode) {
        i64 nc = th->clock.cycles7 - th->clock.master_cycle_count7;
        if (nc > 0) {
            th->arm7.cached_run<do_debug, false, false, true>(nc);
        }
    } else {
        while (static_cast<i64>(th->clock.master_cycle_count7) < th->clock.cycles7) {
            th->arm7.run_instruction<false>();
            th->clock.master_cycle_count7 += th->waitstates.current_transaction;
            th->waitstates.current_transaction = 0;
            if (th->arm7.halted) {
                th->clock.master_cycle_count7 = th->clock.cycles7;
                break;
            }
        }
    }
    th->arm7_ins = false;


    if (th->ge.fifo.pausing_cpu || th->arm9.halted) {
        th->clock.master_cycle_count9 = th->clock.cycles9;
    }
    else {
        th->arm9_ins = true;
        if (th->arm9.cached_mode) {
            i64 mnc = th->clock.cycles9 - th->clock.master_cycle_count9;
            if (mnc > 0) th->arm9.cached_run<do_debug, false, false, true>(mnc);
        } else {
            while(static_cast<i64>(th->clock.master_cycle_count9) < th->clock.cycles9) {
                th->arm9.run_instruction<false>();
                th->clock.master_cycle_count9 += th->waitstates.current_transaction;
                th->waitstates.current_transaction = 0;
            }
        }
        th->arm9_ins = false;
    }

    // TODO: let this be scheduled.
    // TODO Next: yes do it!
    //ARM7TDMI_IRQcheck(&arm7, 0);
    //ARM946ES_IRQcheck(&arm9, 0);
}


core::core() :
    clock{&waitstates.current_transaction},
    scheduler{&clock.master_cycle_count7},
    arm7{&scheduler, &clock.master_cycle_count7, &waitstates.current_transaction},
    arm9{&scheduler, &clock.master_cycle_count9, &waitstates.current_transaction},
    ppu{this},
    ge{this, &scheduler},
    re{this}, apu{this, &scheduler},
    cart{this}
{
    has.load_BIOS = true;
    has.max_loaded_files = 0;
    has.max_loaded_folders = 0;
    has.save_state = true;
    re.ge = &ge;
    ge.re = &re;
    populate_opts();
    map_memory();

    scheduler.max_block_size = 25;

    scheduler.run.func[0] = &run_block<false>;
    scheduler.run.func[1] = &run_block<true>;
    scheduler.run.ptr = this;

    DMA_init();
    //ARM7TDMI_init(&arm7, &clock.master_cycle_count7, &waitstates.current_transaction, nullptr);
    arm7.read_ptr = this;
    arm7.write_ptr = this;
    arm7.read_func8 = &mainbus_read7<1, false, false>;
    arm7.read_func16 = &mainbus_read7<2, false, false>;
    arm7.read_func32 = &mainbus_read7<4, false, false>;
    arm7.write_func8 = &mainbus_write7<1, false>;
    arm7.write_func16 = &mainbus_write7<2, false>;
    arm7.write_func32 = &mainbus_write7<4, false>;
    arm7.read_func8_debug = &mainbus_read7<1, true, true>;
    arm7.read_func16_debug = &mainbus_read7<2, true, true>;
    arm7.read_func32_debug = &mainbus_read7<4, true, true>;
    arm7.write_func8_debug = &mainbus_write7<1, true>;
    arm7.write_func16_debug = &mainbus_write7<2, true>;
    arm7.write_func32_debug = &mainbus_write7<4, true>;
    arm7.cached_max_block_size = 80;
    arm9.cached_max_block_size = 80;

    arm7.cached_block_ptr = this;
    arm7.ins_timing_ptr = this;
    arm7.fetch_ptr = this;
    arm7.fetch_ins_func16 = &mainbus_fetchins7<2, false, false>;
    arm7.fetch_ins_func32 = &mainbus_fetchins7<4, false, false>;
    arm7.fetch_ins_func16_debug = &mainbus_fetchins7<2, true, false>;
    arm7.fetch_ins_func32_debug = &mainbus_fetchins7<4, true, false>;
    arm7.fetch_ins_func16_peek = &mainbus_fetchins7<2, false, true>;
    arm7.fetch_ins_func32_peek = &mainbus_fetchins7<4, false, true>;
    arm7.fetch_ins_func16_peek_debug = &mainbus_fetchins7<2, true, true>;
    arm7.fetch_ins_func32_peek_debug = &mainbus_fetchins7<4, true, true>;

    arm7.get_cached_block = &get_cached_block7<false>;
    arm7.get_cached_block_debug = &get_cached_block7<true>;
    arm7.register_cached_block = &register_cached_block7<false>;
    arm7.register_cached_block_debug = &register_cached_block7<true>;
    arm7.ins_timing16 = &ins_timing7<2>;
    arm7.ins_timing32 = &ins_timing7<4>;

    //ARM946ES_init(&arm9, &clock.master_cycle_count9, &waitstates.current_transaction, nullptr);
    arm9.read_ptr = this;
    arm9.read_func8 = &mainbus_read9<1, false, false>;
    arm9.read_func16 = &mainbus_read9<2, false, false>;
    arm9.read_func32 = &mainbus_read9<4, false, false>;
    arm9.read_func8_debug = &mainbus_read9<1, true, false>;
    arm9.read_func16_debug = &mainbus_read9<2, true, false>;
    arm9.read_func32_debug = &mainbus_read9<4, true, false>;

    arm9.write_ptr = this;
    arm9.write_func8 = &mainbus_write9<1, false>;
    arm9.write_func16 = &mainbus_write9<2, false>;
    arm9.write_func32 = &mainbus_write9<4, false>;
    arm9.write_func8_debug = &mainbus_write9<1, true>;
    arm9.write_func16_debug = &mainbus_write9<2, true>;
    arm9.write_func32_debug = &mainbus_write9<4, true>;

    arm9.cached_block_ptr = this;
    arm9.ins_timing_ptr = this;
    arm9.fetch_ptr = this;
    arm9.fetch_ins_func16 = &mainbus_fetchins9<2, false, false>;
    arm9.fetch_ins_func32 = &mainbus_fetchins9<4, false, false>;
    arm9.fetch_ins_func16_debug = &mainbus_fetchins9<2, true, false>;
    arm9.fetch_ins_func32_debug = &mainbus_fetchins9<4, true, false>;
    arm9.fetch_ins_func16_peek = &mainbus_fetchins9<2, false, true>;
    arm9.fetch_ins_func32_peek = &mainbus_fetchins9<4, false, true>;
    arm9.fetch_ins_func16_peek_debug = &mainbus_fetchins9<2, true, true>;
    arm9.fetch_ins_func32_peek_debug = &mainbus_fetchins9<4, true, true>;
    arm9.get_cached_block = &get_cached_block9<false>;
    arm9.get_cached_block_debug = &get_cached_block9<true>;
    arm9.register_cached_block = &register_cached_block9<false>;
    arm9.register_cached_block_debug = &register_cached_block9<true>;
    arm9.ins_timing16 =  &ins_timing9<2>;
    arm9.ins_timing32 =  &ins_timing9<4>;

    snprintf(label, sizeof(label), "Nintendo DS");
    jsm_debug_read_trace dt7;
    dt7.read_trace_arm = &read_trace_cpu7;
    dt7.ptr = this;
    arm7.setup_tracing(&dt7, &clock.master_cycle_count7, 1);

    jsm_debug_read_trace dt9;
    dt9.read_trace_arm = &read_trace_cpu9;
    dt9.ptr = this;
    arm9.setup_tracing(&dt9, &clock.master_cycle_count9, 2);

    jsm.described_inputs = false;
    jsm.cycles_left = 0;
}



void core::map_memory() {
    // Full bind: sets both read and write function pointers.
#define BND9(page, func) { mem.rw[1].read[0][page] = &core::busrd9_##func<1, false>; mem.rw[1].read[1][page] = &core::busrd9_##func<2, false>; mem.rw[1].read[2][page] = &core::busrd9_##func<4, false>; mem.rw[1].read_debug[0][page] = &core::busrd9_##func<1, true>; mem.rw[1].read_debug[1][page] = &core::busrd9_##func<2, true>; mem.rw[1].read_debug[2][page] = &core::busrd9_##func<4, true>; mem.rw[1].write[0][page] = &core::buswr9_##func<1, false>; mem.rw[1].write[1][page] = &core::buswr9_##func<2, false>; mem.rw[1].write[2][page] = &core::buswr9_##func<4, false>; mem.rw[1].write_debug[0][page] = &core::buswr9_##func<1, true>; mem.rw[1].write_debug[1][page] = &core::buswr9_##func<2, true>; mem.rw[1].write_debug[2][page] = &core::buswr9_##func<4, true>; }
#define BND7(page, func) { mem.rw[0].read[0][page] = &core::busrd7_##func<1, false>; mem.rw[0].read[1][page] = &core::busrd7_##func<2, false>; mem.rw[0].read[2][page] = &core::busrd7_##func<4, false>; mem.rw[0].read_debug[0][page] = &core::busrd7_##func<1, true>; mem.rw[0].read_debug[1][page] = &core::busrd7_##func<2, true>; mem.rw[0].read_debug[2][page] = &core::busrd7_##func<4, true>; mem.rw[0].write[0][page] = &core::buswr7_##func<1, false>; mem.rw[0].write[1][page] = &core::buswr7_##func<2, false>; mem.rw[0].write[2][page] = &core::buswr7_##func<4, false>; mem.rw[0].write_debug[0][page] = &core::buswr7_##func<1, true>; mem.rw[0].write_debug[1][page] = &core::buswr7_##func<2, true>; mem.rw[0].write_debug[2][page] = &core::buswr7_##func<4, true>; }
    // Read-only bind: sets only read function pointers (writes handled by remap_cached_write_funcs).
#define BND9_RD(page, func) { mem.rw[1].read[0][page] = &core::busrd9_##func<1, false>; mem.rw[1].read[1][page] = &core::busrd9_##func<2, false>; mem.rw[1].read[2][page] = &core::busrd9_##func<4, false>; mem.rw[1].read_debug[0][page] = &core::busrd9_##func<1, true>; mem.rw[1].read_debug[1][page] = &core::busrd9_##func<2, true>; mem.rw[1].read_debug[2][page] = &core::busrd9_##func<4, true>; }
#define BND7_RD(page, func) { mem.rw[0].read[0][page] = &core::busrd7_##func<1, false>; mem.rw[0].read[1][page] = &core::busrd7_##func<2, false>; mem.rw[0].read[2][page] = &core::busrd7_##func<4, false>; mem.rw[0].read_debug[0][page] = &core::busrd7_##func<1, true>; mem.rw[0].read_debug[1][page] = &core::busrd7_##func<2, true>; mem.rw[0].read_debug[2][page] = &core::busrd7_##func<4, true>; }

    for (u32 i = 0; i < 16; i++) {
        BND9(i, invalid);
        BND7(i, invalid);
    }
    memset(dbg_info.mgba.str, 0, sizeof(dbg_info.mgba.str));

    // Pages 2 (main RAM), 3 (shared WRAM), and 6 (VRAM): reads only here;
    // writes are installed by remap_cached_write_funcs() below.
    BND9_RD(0x2, main);
    BND9_RD(0x3, shared);
    BND9(0x4, io);
    BND9(0x5, obj_and_palette);
    BND9_RD(0x6, vram);
    BND9(0x7, oam);
    BND9(0x8, gba_cart);
    BND9(0x9, gba_cart);
    BND9(0xA, gba_sram);

    BND7(0x0, bios7);
    BND7(0x2, main);      // ARM7 main writes have no cache invalidation; full bind is fine.
    BND7_RD(0x3, shared); // ARM7 shared writes: cache-sensitive, set by remap below.
    BND7(0x4, io);
    BND7_RD(0x6, vram);
    BND7(0x8, gba_cart);
    BND7(0x9, gba_cart);
    BND7(0xA, gba_sram);
#undef BND7
#undef BND9
#undef BND9_RD
#undef BND7_RD

    // Install write function pointers for cache-sensitive regions.
    // Both CPUs start in cached mode.
    remap_cached_write_funcs(true, true);
}

template<bool do_cached_arm7, bool do_cached_arm9>
void core::remap_cached_write_funcs_t()
{
    static constexpr bool cached = do_cached_arm7 || do_cached_arm9;

    // ARM9 main RAM (page 2) writes
    mem.rw[1].write[0][0x2] = &core::buswr9_main<1, false, cached>;
    mem.rw[1].write[1][0x2] = &core::buswr9_main<2, false, cached>;
    mem.rw[1].write[2][0x2] = &core::buswr9_main<4, false, cached>;
    mem.rw[1].write_debug[0][0x2] = &core::buswr9_main<1, true, cached>;
    mem.rw[1].write_debug[1][0x2] = &core::buswr9_main<2, true, cached>;
    mem.rw[1].write_debug[2][0x2] = &core::buswr9_main<4, true, cached>;

    // ARM9 shared WRAM (page 3) writes
    mem.rw[1].write[0][0x3] = &core::buswr9_shared<1, false, cached>;
    mem.rw[1].write[1][0x3] = &core::buswr9_shared<2, false, cached>;
    mem.rw[1].write[2][0x3] = &core::buswr9_shared<4, false, cached>;
    mem.rw[1].write_debug[0][0x3] = &core::buswr9_shared<1, true, cached>;
    mem.rw[1].write_debug[1][0x3] = &core::buswr9_shared<2, true, cached>;
    mem.rw[1].write_debug[2][0x3] = &core::buswr9_shared<4, true, cached>;

    // ARM9 VRAM (page 6) writes
    mem.rw[1].write[0][0x6] = &core::buswr9_vram<1, false, cached>;
    mem.rw[1].write[1][0x6] = &core::buswr9_vram<2, false, cached>;
    mem.rw[1].write[2][0x6] = &core::buswr9_vram<4, false, cached>;
    mem.rw[1].write_debug[0][0x6] = &core::buswr9_vram<1, true, cached>;
    mem.rw[1].write_debug[1][0x6] = &core::buswr9_vram<2, true, cached>;
    mem.rw[1].write_debug[2][0x6] = &core::buswr9_vram<4, true, cached>;

    // ARM7 shared WRAM (page 3) writes
    mem.rw[0].write[0][0x3] = &core::buswr7_shared<1, false, cached>;
    mem.rw[0].write[1][0x3] = &core::buswr7_shared<2, false, cached>;
    mem.rw[0].write[2][0x3] = &core::buswr7_shared<4, false, cached>;
    mem.rw[0].write_debug[0][0x3] = &core::buswr7_shared<1, true, cached>;
    mem.rw[0].write_debug[1][0x3] = &core::buswr7_shared<2, true, cached>;
    mem.rw[0].write_debug[2][0x3] = &core::buswr7_shared<4, true, cached>;

    // ARM7 VRAM (page 6) writes
    mem.rw[0].write[0][0x6] = &core::buswr7_vram<1, false, cached>;
    mem.rw[0].write[1][0x6] = &core::buswr7_vram<2, false, cached>;
    mem.rw[0].write[2][0x6] = &core::buswr7_vram<4, false, cached>;
    mem.rw[0].write_debug[0][0x6] = &core::buswr7_vram<1, true, cached>;
    mem.rw[0].write_debug[1][0x6] = &core::buswr7_vram<2, true, cached>;
    mem.rw[0].write_debug[2][0x6] = &core::buswr7_vram<4, true, cached>;
}

void core::remap_cached_write_funcs(bool c7, bool c9)
{
    if (c7 && c9)       remap_cached_write_funcs_t<true,  true>();
    else if (c7)        remap_cached_write_funcs_t<true,  false>();
    else if (c9)        remap_cached_write_funcs_t<false, true>();
    else                remap_cached_write_funcs_t<false, false>();
}

void core::arm7_enter_cached_mode()
{
    // Invalidate all block caches that ARM7 can execute from.
    mem.RAM_block_cache.clear_all_blocks();
    mem.WRAM_share_block_cache.clear_all_blocks();
    mem.WRAM_arm7_block_cache.clear_all_blocks();
    mem.bios7_block_cache.clear_all_blocks();
    mem.vram.block_cache.clear_all_blocks();
    // Fill ARM7 pipeline and set cached_mode flag.
    arm7.enter_cached_mode();
    // Remap write function pointers to include ARM7 cache invalidation.
    remap_cached_write_funcs(arm7.cached_mode, arm9.cached_mode);
}

void core::arm7_exit_cached_mode()
{
    arm7.exit_cached_mode();
    // Remap write function pointers to skip ARM7 cache invalidation.
    remap_cached_write_funcs(arm7.cached_mode, arm9.cached_mode);
}

void core::arm9_enter_cached_mode()
{
    // Invalidate all external block caches that ARM9 can execute from.
    // The ITCM block cache (arm9.arm9_block_cache) is cleared by
    // arm9.enter_cached_mode() itself.
    mem.RAM_block_cache.clear_all_blocks();
    mem.WRAM_share_block_cache.clear_all_blocks();
    mem.bios9_block_cache.clear_all_blocks();
    mem.vram.block_cache.clear_all_blocks();
    // Fill ARM9 pipeline, set cached_mode flag, and clear ITCM block cache.
    arm9.enter_cached_mode();
    // Remap write function pointers to include ARM9 cache invalidation.
    remap_cached_write_funcs(arm7.cached_mode, arm9.cached_mode);
}

void core::arm9_exit_cached_mode()
{
    arm9.exit_cached_mode();
    // Remap write function pointers to skip ARM9 cache invalidation.
    remap_cached_write_funcs(arm7.cached_mode, arm9.cached_mode);
}

template<u8 sz, bool do_debug>
u32 core::busrd9_io(u32 addr, u8 access)
{
    u32 v;
    if (((addr >= 0x04000000) && (addr < 0x04000070)) || ((addr >= 0x04001000) && (addr < 0x04001070))) {
        return ppu.read9_io(addr, sz, access);
    }
    if ((addr >= 0x04000320) && (addr < 0x04000700)) {
        return ge.read(addr, sz);
    }
    if ((addr >= 0x04000400) && (addr < 0x04000520)) return busrd9_apu(addr, sz, access);
    switch(addr) {
        case R_ROMCTRL:
            if (io.rights.nds_slot_is7) return 0;
            return cart.read_romctrl();

        case R_ROMDATA+0: // 4100010
        case R_ROMDATA+1: // 4100011
        case R_ROMDATA+2: // 4100012
        case R_ROMDATA+3: // 4100013
            assert(sz==4);
            if (io.rights.nds_slot_is7) return 0;
            return cart.read_rom<do_debug>(addr, sz);

        case R_AUXSPIDATA:
            if (io.rights.nds_slot_is7) return 0;
            return cart.read_spi(0);
        case R_AUXSPIDATA+1:
            if (io.rights.nds_slot_is7) return 0;
            return cart.read_spi(1);

        case R_POSTFLG:
            return io.arm9.POSTFLG;

        case R_IPCFIFORECV+0:
        case R_IPCFIFORECV+1:
        case R_IPCFIFORECV+2:
        case R_IPCFIFORECV+3:
            // arm9 reads from to_arm9
            if (io.ipc.arm9.fifo_enable) {
                if (io.ipc.to_arm9.is_empty()) {
                    io.ipc.arm9.error |= 1;
                    v = io.ipc.to_arm9.peek_last();
                }
                else {
                    u32 old_bits7 = io.ipc.to_arm9.is_empty() & io.ipc.arm7.irq_on_send_fifo_empty;
                    v = io.ipc.to_arm9.pop();
                    u32 new_bits7 = io.ipc.to_arm9.is_empty() & io.ipc.arm7.irq_on_send_fifo_empty;
                    if (!old_bits7 && new_bits7) { // arm7 send is empty
                        update_IF7<do_debug>(IRQ_IPC_SEND_EMPTY);
                    }

                }
            }
            else {
                v = io.ipc.to_arm9.peek_last();
            };
            return v & masksz[sz];
    }
    if constexpr (sz == 1) {
        return busrd9_io8<do_debug>(addr, access);
    }
    else if constexpr (sz == 2) {
        v = busrd9_io8<do_debug>(addr, access);
        v |= busrd9_io8<do_debug>(addr+1, access) << 8;
        return v;
    }
    else if constexpr (sz == 4) {
        v = busrd9_io8<do_debug>(addr, access);
        v |= busrd9_io8<do_debug>(addr+1, access) << 8;
        v |= busrd9_io8<do_debug>(addr+2, access) << 16;
        v |= busrd9_io8<do_debug>(addr+3, access) << 24;
        return v;
    }
    else NOGOHERE;
}

static void buswr9_apu(u32 addr, u8 sz, u8 access, u32 val) {
    static int already_did = 0;
    if (!already_did) {
        already_did = 1;
        printf("\nWARN: APU WRITE9!");
    }
}

template<u8 sz, bool do_debug>
void core::buswr9_io(u32 addr, u8 access, u32 val)
{
    if (((addr >= 0x04000000) && (addr < 0x04000070)) || ((addr >= 0x04001000) && (addr < 0x04001070))) {
        ppu.write9_io(addr, sz, access, val);
        return;
    }
    if ((addr >= 0x04000320) && (addr < 0x04000700)) {
        ge.write<do_debug>(addr, sz, val);
        return;
    }

    if ((addr >= 0x04000400) && (addr < 0x04000520)) return buswr9_apu(addr, sz, access, val);
    switch(addr) {
        case R_AUXSPICNT: {
            if (io.rights.nds_slot_is7) return;
            cart.spi_write_spicnt(val & 0xFF, 0);
            if constexpr (sz >= 2)
                cart.spi_write_spicnt((val >> 8) & 0xFF, 1);
            if constexpr (sz == 4) {
                buswr9_io<2, do_debug>(R_AUXSPIDATA, access, val >> 16);
            }
            return; }
        case R_AUXSPICNT+1:
            if (io.rights.nds_slot_is7) return;
            cart.spi_write_spicnt(val & 0xFF, 1);
            return;

        case R_AUXSPIDATA:
            if (io.rights.nds_slot_is7) return;
            assert(sz!=1);
            cart.spi_transaction(val & 0xFFFF);
            if constexpr (sz == 4) {
                buswr9_io<2, do_debug>(R_ROMCTRL, access, val >> 16);
            }
            return;
        case R_ROMCTRL:
            if (io.rights.nds_slot_is7) return;
            cart.write_romctrl<do_debug>(val);
            return;

        case R_IPCFIFOSEND+0:
        case R_IPCFIFOSEND+1:
        case R_IPCFIFOSEND+2:
        case R_IPCFIFOSEND+3:
            // All writes are only 32 bits here
            if (io.ipc.arm9.fifo_enable) {
                if constexpr (sz == 2) {
                    val &= 0xFFFF;
                    val = (val << 16) | val;
                }
                if constexpr (sz == 1) {
                    val &= 0xFF;
                    val = (val << 24) | (val << 16) | (val << 8) | val;
                }
                // ARM9 writes to_arm7
                u32 old_bits = io.ipc.to_arm7.is_not_empty() & io.ipc.arm7.irq_on_recv_fifo_not_empty;
                if (io.ipc.arm9.fifo_enable) {
                    io.ipc.arm9.error |= io.ipc.to_arm7.push(val);
                    cart.detect_kind(9, val);
                }

                u32 new_bits = io.ipc.to_arm7.is_not_empty() & io.ipc.arm7.irq_on_recv_fifo_not_empty;
                if (!old_bits && new_bits) {
                    // Trigger ARM7 recv not empty
                    update_IF7<do_debug>(IRQ_IPC_RECV_NOT_EMPTY);
                }
            }
            return;
    }
    buswr9_io8<do_debug>(addr, access, val & 0xFF);
    if constexpr(sz >= 2) {
        buswr9_io8<do_debug>(addr+1, access, (val >> 8) & 0xFF);
    }
    if constexpr(sz == 4) {
        buswr9_io8<do_debug>(addr+2, access, (val >> 16) & 0xFF);
        buswr9_io8<do_debug>(addr+3, access, (val >> 24) & 0xFF);
    }
}

template<u8 sz, bool do_debug>
u32 core::busrd7_wifi(u32 addr, u8 access) {
    // 0x04804000 and 0x480C000 are the two 8KB RAM sections, oops!
    // 4804000..4804fff
    if ((addr >= 0x04804000) && (addr < 0x04805000)) return cR[sz](mem.wifi, addr & 0x1FFF);
    switch (addr) {
        case 0x0480803C:
            return 0x200; // wifi power off!
    }
    // TODO: stub W_BB_CNT and pals
    //if (addr < 0x04810000) return cR[sz](mem.wifi, addr & 0x1FFF);
    static int a = 1;
    if (a) {
        a = 0;
        printf("\nWARN read from WIFI!");
    }
    return 0;
}

template<u8 sz, bool do_debug>
void core::buswr7_wifi(u32 addr, u8 access, u32 val)
{
    if ((addr >= 0x04804000) && (addr < 0x04805000)) return cW[sz](mem.wifi, addr & 0x1FFF, val);
    //if (addr < 0x04810000) return cW[sz](mem.wifi, addr & 0x1FFF, val);

    static int a = 1;
    if (a) {
        printf("\nWarning ignore WIFI WRITE(s)....");
        a = 0;
    }
    return;
}

template<u8 sz, bool do_debug>
u32 core::busrd7_io(u32 addr, u8 access)
{
    if (((addr >= 0x04000000) && (addr < 0x04000070)) || ((addr >= 0x04001000) && (addr < 0x04001070))) {
        return ppu.read7_io(addr, sz, access);
    }
    if ((addr >= 0x04000400) && (addr < 0x04000520)) return apu.read(addr, sz, access);
    if (addr >= 0x04800000) return busrd7_wifi<sz, do_debug>(addr, access);
    u32 v;
    switch(addr) {
        case R_ROMCTRL:
            if (!io.rights.nds_slot_is7) return 0;
            return cart.read_romctrl();

        case R_ROMDATA+0:
        case R_ROMDATA+1:
        case R_ROMDATA+2:
        case R_ROMDATA+3:
            assert(sz==4);
            if (!io.rights.nds_slot_is7) return 0;
            return cart.read_rom<do_debug>(addr, sz);

        case R_AUXSPIDATA:
            if (!io.rights.nds_slot_is7) return 0;
            return cart.read_spi(0);
        case R_AUXSPIDATA+1:
            if (!io.rights.nds_slot_is7) return 0;
            return cart.read_spi(1);

        case R7_SPIDATA:
            return SPI_read(sz);

        case R_IPCFIFORECV+0:
        case R_IPCFIFORECV+1:
        case R_IPCFIFORECV+2:
        case R_IPCFIFORECV+3:
            // arm7 reads from to_arm7
            if (io.ipc.arm7.fifo_enable) {
                if (io.ipc.to_arm7.is_empty()) {
                    io.ipc.arm7.error |= 1;
                    v = io.ipc.to_arm7.peek_last();
                }
                else {
                    u32 old_bits = io.ipc.to_arm7.is_empty() & io.ipc.arm9.irq_on_send_fifo_empty;
                    v = io.ipc.to_arm7.pop();
                    u32 new_bits = io.ipc.to_arm7.is_empty() & io.ipc.arm9.irq_on_send_fifo_empty;
                    if (!old_bits && new_bits) { // arm7 send is empty
                        update_IF9<do_debug>(IRQ_IPC_SEND_EMPTY);
                    }
                }
            }
            else {
                v = io.ipc.to_arm7.peek_last();
            };
            return v & masksz[sz];
    }

    v = busrd7_io8<do_debug>(addr, access);
    if constexpr(sz >= 2) v |= busrd7_io8<do_debug>(addr+1, access) << 8;
    if constexpr (sz == 4) {
        v |= busrd7_io8<do_debug>(addr+2, access) << 16;
        v |= busrd7_io8<do_debug>(addr+3, access) << 24;
    }
    return v;
}

template <u8 sz, bool do_debug>
void core::buswr7_io(u32 addr, u8 access, u32 val)
{
    if (((addr >= 0x04000000) && (addr < 0x04000070)) || ((addr >= 0x04001000) && (addr < 0x04001070))) {
        ppu.write7_io(addr, sz, access, val);
        return;
    }
    if ((addr >= 0x04000400) && (addr < 0x04000520)) return apu.write(addr, sz, access, val);
    if (addr >= 0x04800000) return buswr7_wifi<sz, do_debug>(addr, access, val);

    switch(addr) {
        case R7_SPIDATA:
            SPI_write<do_debug>(sz, val);
            return;

        case R_AUXSPICNT: {
            if (!io.rights.nds_slot_is7) return;
            cart.spi_write_spicnt(val & 0xFF, 0);
            if (sz >= 2) {
                cart.spi_write_spicnt((val >> 8) & 0xFF, 1);
            }
            if (sz == 4) {
                buswr7_io<2, do_debug>(R_AUXSPIDATA, access, val >> 16);
            }
            return; }
        case R_AUXSPICNT+1:
            if (!io.rights.nds_slot_is7) return;
            cart.spi_write_spicnt(val & 0xFF, 1);
            return;

        case R_AUXSPIDATA:
            if (!io.rights.nds_slot_is7) return;
            cart.spi_transaction(val & 0xFFFF);
            if (sz == 4) {
                buswr7_io<2, do_debug>(R_ROMCTRL, access, val >> 16);
            }
            return;
        case R_ROMCTRL:
            if (!io.rights.nds_slot_is7) return;
            cart.write_romctrl<do_debug>(val);
            return;

        case R_RTC:
            write_RTC<do_debug>(sz, val & 0xFFFF);
            return;
        case R_IPCFIFOSEND+0:
        case R_IPCFIFOSEND+1:
        case R_IPCFIFOSEND+2:
        case R_IPCFIFOSEND+3:
            // All writes are only 32 bits here
            if (io.ipc.arm7.fifo_enable) {
                if (sz == 2) {
                    val &= 0xFFFF;
                    val = (val << 16) | val;
                }
                if (sz == 1) {
                    val &= 0xFF;
                    val = (val << 24) | (val << 16) | (val << 8) | val;
                }
                // ARM7 writes to_arm9
                u32 old_bits = io.ipc.to_arm9.is_not_empty() & io.ipc.arm9.irq_on_recv_fifo_not_empty;
                if (io.ipc.arm7.fifo_enable) {
                    io.ipc.arm7.error |= io.ipc.to_arm9.push(val);
                    //if (!cart.backup.detect.done) cart.detect_kind(7, val);
                }
                u32 new_bits = io.ipc.to_arm9.is_not_empty() & io.ipc.arm9.irq_on_recv_fifo_not_empty;
                if (!old_bits && new_bits) {
                    // Trigger ARM9 recv not empty
                    update_IF9<do_debug>(IRQ_IPC_RECV_NOT_EMPTY);
                }
            }
            return;
    }

    if (addr >= 0x04800000) return buswr7_wifi<sz, do_debug>(addr, access, val);

    buswr7_io8<do_debug>(addr, access, val & 0xFF);
    if constexpr (sz >= 2) buswr7_io8<do_debug>(addr+1, access, (val >> 8) & 0xFF);
    if constexpr (sz == 4) {
        buswr7_io8<do_debug>(addr+2, access, (val >> 16) & 0xFF);
        buswr7_io8<do_debug>(addr+3, access, (val >> 24) & 0xFF);
    }
}

}
