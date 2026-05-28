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


bool core::prefetch_stop() const {
    // So we need to cover a few cases here...
    // "If ROM data/SRAM/FLASH is accessed in a cycle, where the prefetch unit
    //  is active and finishing a half-word access, then a one-cycle penalty applies."
    if (prefetch.enable) {
        u32 page = gba->cpu.regs.R[15] >> 24;
        if ((page >= 8) && (page < 0xE) && (prefetch.cycles_banked > 0)) {
            // OK so we have cycles-banked that can be up to 8* what it should compare to
            u32 cb = prefetch.cycles_banked % prefetch.duty_cycle;
            if (cb == (prefetch.duty_cycle - 1)) { // ABOUT to finish
                return true;
            }
        }
    }
    return false;
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
        gba->waitstates.current_transaction++;
        if (sz == 4) {
            return ((addr >> 1) & 0xFFFF) | ((((addr >> 1) + 1) & 0xFFFF) << 16);
        }
        return (addr >> 1) & masksz[sz];
    }

    u32 sequential = (access & ARM32P_sequential);
    if ((addr & 0x1FFFF) == 0) sequential = 0; // 128KB blocks are non-sequential
    // First ROM access after the prefetch buffer was disabled must be non-sequential,
    // because the buffer contents are now stale and the pipeline was broken.
    if (th->prefetch.was_disabled) {
        sequential = 0;
        th->prefetch.was_disabled = false;
    }
    // determine cycles of this access
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
        // Just do a normal read
        outcycles = th->prefetch_stop();
        if (sz == 4) outcycles += gba->waitstates.timing32[sequential][page];
        else outcycles += gba->waitstates.timing16[sequential][page];
        gba->waitstates.current_transaction += outcycles;
        if constexpr(sz == 1) return reinterpret_cast<u8 *>(th->ROM.ptr)[addr];
        if constexpr(sz == 2) return reinterpret_cast<u16 *>(th->ROM.ptr)[addr >> 1];
        if constexpr(sz == 4) return reinterpret_cast<u32 *>(th->ROM.ptr)[addr >> 2];
        NOGOHERE;
    }

    // If we got here, prefetch is enabled.
    const i64 tt = static_cast<i64>(gba->clock_current());
    i64 this_cycles = (sz == 4) ? gba->waitstates.timing32[1][page] : gba->waitstates.timing16[1][page];
    // If we are at the next prefetch addr, and it's code...
    if (th->prefetch.last_access != 0xFFFFFFFFFFFFFFFF)
        th->prefetch.cycles_banked += (tt - static_cast<i64>(th->prefetch.last_access));
    if (th->prefetch.cycles_banked > (this_cycles * 8)) { // We can only get ahead 8 times
        th->prefetch.cycles_banked = this_cycles * 8;
    }

    if (addr == th->prefetch.next_addr && (access & ARM32P_code)) {
        // Subtract # of cycles of this access
        th->prefetch.cycles_banked -= this_cycles;
        // if we don't have enough...
        if (th->prefetch.cycles_banked < 0) {
            if constexpr (do_debug) {
                if (::dbg.do_debug) {
                    trace_view *tv = gba->cpu.dbg.tvptr;
                    if (tv) {
                        tv->startline(3);
                        tv->printf(0, "ifetch");
                        tv->printf(1, "%lld", gba->clock.master_cycle_count + gba->waitstates.current_transaction);
                        tv->printf(2, "%08x", addr);
                        tv->printf(4, "partial complete. cycles left: %d", (0 - th->prefetch.cycles_banked));
                        tv->endline();
                    }
                }
            }
            // Add what we have left to the wait
            outcycles += (0 - th->prefetch.cycles_banked);
            // Reset cycles banked to 0
            th->prefetch.cycles_banked = 0;
        } else { // if we DO have enough...
            outcycles++; // transaction only takes 1 cycle!
            //if (prefetch.cycles_banked > (prefetch.duty_cycle * 8)) { // We can only get ahead 8 times
            //    prefetch.cycles_banked = prefetch.duty_cycle * 8;
            //}
        }
    }
    else { // Check for another case: fetch is tried of the currently-in-progress fetch
        // First calculate what the current in-progress fetch is
        u32 current_fetch_addr = th->prefetch.next_addr;
        u32 duty_cycle = gba->waitstates.timing16[1][page];
        if constexpr (sz == 4) duty_cycle *= 2;

        u32 fetch_cycles = th->prefetch.cycles_banked / duty_cycle;
        current_fetch_addr += (fetch_cycles * sz);
        if (addr == current_fetch_addr && (access & ARM32P_code) && (fetch_cycles > 0)) {
            //printf("\nI HIT THE CASE...");
            // TODO: reaosn this out with nonsequential timing
            u32 cycles_left_to_fetch = th->prefetch.cycles_banked % duty_cycle;
            outcycles += cycles_left_to_fetch;
            th->prefetch.cycles_banked = 0;
        }
        else { // Abort the prefetcher
            outcycles += th->prefetch_stop(); // Penalty if we're 1 from end!
            th->prefetch.cycles_banked = 0; // Restart prefetches
            outcycles += (sz == 4) ? gba->waitstates.timing32[sequential][page] : gba->waitstates.timing16[sequential][page];; // Full cost of the read
        }
    }
    th->prefetch.duty_cycle = gba->waitstates.timing16[1][page];
    th->prefetch.next_addr = addr + sz;
    th->prefetch.last_access = tt + outcycles;
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
    gba->waitstates.current_transaction++;
    gba->waitstates.current_transaction += th->prefetch_stop();
    th->prefetch.cycles_banked = 0;
    printf("\nWARNING write cart addr %08x", addr);
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
