//
// Created by . on 12/4/24.
//

#include <cassert>
#include <cstring>
#include <cstdio>
#include <cstdlib>

#include "helpers/debug.h"
#include "helpers/mem_template_macros.h"
#include "helpers/physical_io.h"
#include "helpers/multisize_memaccess.cpp"

#include "../gba_bus.h"
#include "gba_cart.h"
#include "flash.h"
#include "eeprom.h"

namespace GBA::CART {
static constexpr u32 maskalign[5] = {0, 0xFFFFFFFF, 0xFFFFFFFE, 0, 0xFFFFFFFC};

static constexpr u32 masksz[5] = { 0, 0xFF, 0xFFFF, 0, 0xFFFFFFFF };
MT_IR32(GBA::core, read);
MT_IR32(GBA::core, read_sram);
MT_IW32(GBA::core, write);
MT_IW32(GBA::core, write_sram);


u32 core::prefetch_penalty() const {
    return prefetch.enable && prefetch.active && !prefetch.was_filled && prefetch.countdown == 1 ? 1u : 0u;
}

void core::prefetch_tick(i32 clocks, i32 S) {
    auto &pf = prefetch;
    if (S <= 0 || clocks <= 0 || pf.was_filled) return;
    pf.active = true;
    pf.countdown -= clocks;
    while (pf.countdown <= 0) {
        if (static_cast<i32>(pf.fetch - pf.head) >= 16) { pf.was_filled = true; return; }
        pf.fetch += 2;
        pf.countdown += S;
    }
}

void core::prefetch_sim_advance(u64 now) {
    auto &pf = prefetch;
    i64 clocks = static_cast<i64>(now) - static_cast<i64>(pf.pf_clock);
    pf.pf_clock = now;
    if (clocks <= 0 || !pf.enable || pf.was_filled) return;
    u32 page = gba->cpu.regs.R[15] >> 24;
    if (page < 8 || page >= 0xE) return;
    prefetch_tick(static_cast<i32>(clocks), static_cast<i32>(gba->waitstates.timing16[1][page]));
}

template<u8 sz, bool do_debug, bool peek>
u32 core::read(GBA::core *gba, u32 addr, u8 access) {
    auto *th = &gba->cart;
    if ((th->RAM.is_eeprom) && (addr >= 0x0d000000) && (addr < 0x0e000000)) {
        u32 v = th->read_eeprom<sz, do_debug, peek>(addr, access) & masksz[sz];
        //printf("\nRead EEPROM addr:%08x  sz:%d  val:%02x", addr, sz, v);
        return v;
    }
    u32 page = addr >> 24;
    addr &= 0x01FF'FFFF;

    if (addr >= th->ROM.size) { // OOB read
        if constexpr (!peek) gba->waitstates.current_transaction++;
        if (sz == 4) {
            return ((addr >> 1) & 0xFFFF) | ((((addr >> 1) + 1) & 0xFFFF) << 16);
        }
        return (addr >> 1) & masksz[sz];
    }

    if constexpr (peek) {
        if constexpr(sz == 1) return reinterpret_cast<u8 *>(th->ROM.ptr)[addr];
        if constexpr(sz == 2) return reinterpret_cast<u16 *>(th->ROM.ptr)[addr >> 1];
        if constexpr(sz == 4) return reinterpret_cast<u32 *>(th->ROM.ptr)[addr >> 2];
        NOGOHERE;
    }

    u32 sequential = (access & ARM32P_sequential);
    if ((addr & 0x1FFFF) == 0) sequential = 0; // 128KB blocks are non-sequential
    if (th->prefetch.was_disabled) {
        sequential = 0;
        th->prefetch.was_disabled = false;
    }
    if constexpr (do_debug) {
        if (dbg.do_debug) {
            trace_view *tv = gba->cpu.dbg.tvptr;
            if (tv) {
                tv->startline(3);
                tv->printf(0, "ifetch");
                tv->printf(1, "%lld", gba->clock.master_cycle_count + gba->waitstates.current_transaction);
                tv->printf(2, "%08x", addr);
                tv->printf(4, "READ GAMEPAK. seq:%d code:%d page:%d", sequential, !!(access & ARM32P_code), page);
                tv->endline();
            }
        }
    }
    u32 outcycles = 0;
    if (!th->prefetch.enable) {
        outcycles = th->prefetch_penalty();
        if (sz == 4) outcycles += gba->waitstates.timing32[sequential][page];
        else outcycles += gba->waitstates.timing16[sequential][page];
        gba->waitstates.current_transaction += outcycles;
        if constexpr(sz == 1) return reinterpret_cast<u8 *>(th->ROM.ptr)[addr];
        if constexpr(sz == 2) return reinterpret_cast<u16 *>(th->ROM.ptr)[addr >> 1];
        if constexpr(sz == 4) return reinterpret_cast<u32 *>(th->ROM.ptr)[addr >> 2];
        NOGOHERE;
    }

    const i64 tt = static_cast<i64>(gba->clock_current());
    const u32 full_addr = (page << 24) | addr;
    const i32 S = static_cast<i32>(gba->waitstates.timing16[1][page]);  // sequential half-word
    const i32 N = static_cast<i32>(gba->waitstates.timing16[0][page]);  // non-sequential half-word
    auto &pf = th->prefetch;

    th->prefetch_sim_advance(static_cast<u64>(tt));

    outcycles = 0;
    const bool is_code = (access & ARM32P_code) != 0;
    const u32 halfwords = (sz == 4) ? 2u : 1u;

    if (is_code && full_addr == pf.head) {
        outcycles += 1;
        th->prefetch_tick(1, S);
        for (u32 h = 0; h < halfwords; h++) {
            if (pf.was_filled && pf.head == pf.fetch) { pf.was_filled = false; pf.countdown = N; }
            if (pf.head == pf.fetch) {
                outcycles += static_cast<u32>(pf.countdown);
                th->prefetch_tick(pf.countdown, S);
            }
            pf.head += 2;
        }
    } else {
        outcycles += th->prefetch_penalty();
        if (is_code) {
            pf.head = pf.fetch = full_addr + sz;
            pf.countdown = S;
            pf.was_filled = false;
            pf.active = false;
        } else {
            pf.head = pf.fetch = 0;
            pf.countdown = 0;
            pf.was_filled = true;
            pf.active = false;
        }
        i32 first = sequential ? S : N;
        outcycles += (sz == 4) ? static_cast<u32>(first + S) : static_cast<u32>(first);
    }

    pf.pf_clock = static_cast<u64>(tt + outcycles);
    pf.duty = S;

    gba->waitstates.current_transaction += outcycles;

    if constexpr(sz == 1) return reinterpret_cast<u8 *>(th->ROM.ptr)[addr];
    if constexpr(sz == 2) return reinterpret_cast<u16 *>(th->ROM.ptr)[addr >> 1];
    if constexpr(sz == 4) return reinterpret_cast<u32 *>(th->ROM.ptr)[addr >> 2];
    NOGOHERE;
}

template<u8 sz, bool do_debug, bool peek>
u32 core::read_sram(GBA::core *gba, u32 addr, u8 access)
{
    auto *th = &gba->cart;
    if (th->RAM.is_flash) return th->read_flash<sz, do_debug, peek>(addr, access);

    /*if (addr >= 0x0E010000) {
        return GBA_open_bus(addr, sz);
    }*/

    u32 v = static_cast<u8 *>(th->RAM.store->data)[addr & th->RAM.mask];
    if constexpr (sz == 2) {
        v *= 0x101;
    }
    if constexpr (sz == 4) {
        v *= 0x1010101;
    }
    gba->waitstates.current_transaction += th->prefetch_penalty();
    gba->waitstates.current_transaction += gba->waitstates.sram;
    return v;
}

void core::write_RTC(u32 addr, u8 sz, u8 access, u32 val)
{
    // Ignore byte writes...weirdly?
    if (sz == 1) return;
}

template<u8 sz, bool do_debug>
void core::write(GBA::core *gba, u32 addr, u8 access, u32 val)
{
    auto *th = &gba->cart;
    addr &= maskalign[sz];
    if (th->RTC.present && (addr >= 0x080000C4) && (addr < 0x080000CA)) {
        return th->write_RTC(addr, sz, access, val);
    }
    if (th->RAM.is_eeprom && (addr >= 0x0d000000) && (addr < 0x0e000000)) {
        //printf("\nWrite EEPROM addr:%08x  sz:%d  val:%02x", addr, sz, val);
        return th->write_eeprom(addr, sz, access, val);
    }
    u32 page = addr >> 24;
    auto &pf = th->prefetch;
    u32 outcycles = 0;
    if (pf.enable && page >= 8 && page < 0xE) {
        const i64 tt = static_cast<i64>(gba->clock_current());
        const i32 S = static_cast<i32>(gba->waitstates.timing16[1][page]);
        const i32 N = static_cast<i32>(gba->waitstates.timing16[0][page]);
        th->prefetch_sim_advance(static_cast<u64>(tt));
        outcycles += th->prefetch_penalty();
        pf.head = pf.fetch = 0; pf.countdown = 0; pf.was_filled = true; pf.active = false;
        u32 seq = (access & ARM32P_sequential);
        if ((addr & 0x1FFFF) == 0) seq = 0;
        i32 first = seq ? S : N;
        outcycles += (sz == 4) ? static_cast<u32>(first + S) : static_cast<u32>(first);
        pf.pf_clock = static_cast<u64>(tt + outcycles);
        pf.duty = S;
        gba->waitstates.current_transaction += outcycles;
    } else {
        outcycles = (sz == 4) ? gba->waitstates.timing32[0][page] : gba->waitstates.timing16[0][page];
        gba->waitstates.current_transaction += outcycles;
    }
}

template<u8 sz, bool do_debug>
void core::write_sram(GBA::core *gba, u32 addr, u8 access, u32 val)
{
    auto *th = &gba->cart;
    if (th->RAM.is_flash) return th->write_flash(addr, sz, access, val);

    //printf("\nWRITE SRAM! %08x", addr);
    if constexpr (sz == 2) {
        if (addr & 1) val >>= 8;
    }
    if constexpr (sz == 4) {
        if ((addr & 3) == 1) val >>= 8;
        else if ((addr & 3) == 2) val >>= 16;
        else if ((addr & 3) == 3) val >>= 24;
    }
    val &= 0xFF;
    static_cast<u8 *>(th->RAM.store->data)[addr & th->RAM.mask] = val;
    gba->waitstates.current_transaction += th->prefetch_penalty();
    gba->waitstates.current_transaction += gba->waitstates.sram;
    th->RAM.store->dirty = true;
}

enum save_kinds {
        SK_none,
        SK_EEPROM_Vnnn,
        SK_SRAM_Vnnn,
        SK_FLASH_Vnnn,
        SK_FLASH512_Vnnn,
        SK_FLASH1M_Vnnn
};

static constexpr u32 firstmatch[3] =  {
        0x52504545, // RPEE or EEPR backward
        0x4D415253, // MARS of SRAM backward
        0x53414C46 // SALF or FLAS
};

static bool cmpstr(const BUF *f, const u32 addr, const char *cmp)
{
    const char *ptr1 = static_cast<char *>(f->ptr) + addr;
    const char *ptr2 = cmp;
    while((*ptr2)!=0) {
        if (*ptr2 != *ptr1) {
            return false;
        }
        ptr2++;
        ptr1++;
    }
    return true;
}

static save_kinds search_strings(const BUF *f)
{
    /* First, find a first blush. */
    i64 found_addr = -1;
    auto buf = static_cast<u32 *>(f->ptr);
    for (u32 i = 0; i < f->size; i += 4) {
        const u32 m = *buf;
        buf++;
        if ((m == 0x52504545) || (m == 0x4D415253) || (m == 0x53414C46)) {
            // Do full string match
            if (cmpstr(f, i, "EEPROM_V")) return SK_EEPROM_Vnnn;
            if (cmpstr(f, i, "SRAM_V")) return SK_SRAM_Vnnn;
            if (cmpstr(f, i, "FLASH_V")) return SK_FLASH_Vnnn;
            if (cmpstr(f, i, "FLASH512_V")) return SK_FLASH512_Vnnn;
            if (cmpstr(f, i, "FLASH1M_V")) return SK_FLASH1M_Vnnn;
        }
    }
    return SK_none;
}

void core::detect_RTC(const BUF *mROM)
{
    // offset 00000C4 at least 6 bytes filled with 0
    auto *ptr = static_cast<u8 *>(mROM->ptr) + 0xC4;
    u32 detect = 1;
    for (u32 i = 0; i < 6; i++) {
        if (ptr[i] != 0) detect = 0;
    }
    RTC.present = detect;
    if (detect) printf("\nRTC DETECTED!");
}

bool core::load_ROM_from_RAM(const char* fil, u64 fil_sz, physical_io_device *pio, u32 *SRAM_enable) {
    ROM.allocate(fil_sz);
    memcpy(ROM.ptr, fil, fil_sz);
    if (SRAM_enable) *SRAM_enable = 1;
    RAM.store = &pio->cartridge_port.SRAM;
    RAM.store->fill_value = 0xFF;
    persistent_store *ps = &pio->cartridge_port.SRAM;
    RAM.is_sram = false;
    RAM.is_flash = false;
    RAM.is_eeprom = false;

    save_kinds save_kind = search_strings(&ROM);
    switch(save_kind) {
        case SK_none:
            printf("\nNO SAVE STRING FOUND!");
            RAM.store->requested_size = 128 * 1024;
            RAM.store->persistent = false;
            break;
        case SK_SRAM_Vnnn:
            printf("\nSRAM found!");
            RAM.store->requested_size = 32 * 1024;
            RAM.store->persistent = true;
            RAM.is_sram = true;
            break;
        case SK_FLASH_Vnnn:
            RAM.flash.kind = FK_SST;
            printf("\nFLASH (old) 64k found! Using SST!");
            RAM.is_flash = true;
            RAM.store->requested_size = 64 * 1024;
            RAM.store->persistent = true;
            break;
        case SK_FLASH512_Vnnn:
            RAM.flash.kind = FK_SST;
            printf("\nFLASH (new) 64k found! Using SST!");
            RAM.is_flash = true;
            RAM.store->requested_size = 64 * 1024;
            RAM.store->persistent = true;
            break;
        case SK_FLASH1M_Vnnn:
            RAM.flash.kind = FK_macronix128k;
            printf("\nFLASH 128K found! Using Maronix 128!");
            RAM.is_flash = true;
            RAM.store->requested_size = 128 * 1024;
            RAM.store->persistent = true;
            break;
        case SK_EEPROM_Vnnn:
            printf("\nEEPROM detected!");
            init_eeprom();
            RAM.is_eeprom = true;
            RAM.store->persistent = true;
            RAM.store->requested_size = 8 * 1024;
            break;
    }
    RAM.store->dirty = true;
    RAM.store->ready_to_use = false;

/*
  EEPROM_Vnnn    EEPROM 512 bytes or 8 Kbytes (4Kbit or 64Kbit)
  SRAM_Vnnn      SRAM 32 Kbytes (256Kbit)
  FLASH_Vnnn     FLASH 64 Kbytes (512Kbit) (ID used in older files)
  FLASH512_Vnnn  FLASH 64 Kbytes (512Kbit) (ID used in newer files)
  FLASH1M_Vnnn   FLASH 128 Kbytes (1Mbit) */



    RAM.size = RAM.store->requested_size;
    RAM.mask = RAM.size - 1;
    RAM.persists = RAM.present = 1;

    detect_RTC(&ROM);

    return true;
}
}
