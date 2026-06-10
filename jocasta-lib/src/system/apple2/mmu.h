//
// Created by . on 8/30/24.
//

#pragma once

#include "helpers/int.h"
#include "helpers/simplebuf.h"

namespace apple2 {
struct core;

struct MMU {
    explicit MMU(core *parent) : bus(parent) {}
    core *bus{};

    simplebuf8 ROM{}, RAM{}, AUX_RAM{};
    u32 RAM_bank{};
    u32 page1_accesses{};
    bool addr_is_aux(u32 addr, bool RDWRT);

    struct APL2SS {           // Off state:
        u32 STORE80{};   // "Page 2 does not bank switch RAM"
        u32 RAMRD{};     // Read from motherboard RAM
        u32 RAMWRT{};    // Write to motherboard RAM
        u32 INTCXROM{};  // Slot response to $C100-CFFF
        u32 ALTZP{};     // Motherboard RAM read/write
        u32 SLOTC3ROM{}; // Motherboard ROM response to $C3XX
        u32 BANK1{};     // off = HiRAM bank 2 response to $$Dxxx
        u32 HRAMRD{};    // $D000-FFFF reads from ROM
        u32 PREWRITE{};  // Reset?
        u32 HRAMWRT{};   // $D000-FFFF writes to hiRAM
        u32 INTC8ROM{};  // Slot response to $C800-CFFF

        u32 WRTCOUNT{};
    } io{};

    void reset();
    void aux_card_RAMwrite(u32 addr, u8 val);
    void cpu_bus_write(u32 addr, u8 val);
    u8 cpu_bus_read(u32 addr, u8 old_val, bool has_effect);
    u8 read_ROM(u32 addr);
    u8 slot_read(u32 addr, u8 old_val);
    u8 aux_card_RAMread(u32 addr);
    u8 read_cxxx(u32 addr, bool is_write, u8 old_val, bool has_effect);
    u8 access_c0xx(u32 addr, bool is_write, u8 old_val, bool has_effect);
};

}
