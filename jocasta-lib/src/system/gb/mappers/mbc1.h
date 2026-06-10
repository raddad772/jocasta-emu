#pragma once

#include "helpers/int.h"
#include "mapper.h"
#include "../gb_clock.h"
#include "../gb_bus.h"

namespace GB::MAPPER {

struct MBC1 {
    core *bus;
    CLOCK *clock;

    u8 *ROM;
    u32 RAM_mask;
    u32 has_RAM;
    CART* cart;

    //
    u32 ROM_bank_lo_offset;// = 0;
    u32 ROM_bank_hi_offset;// = 16384;

    u32 num_ROM_banks;
    u32 num_RAM_banks;
    struct {
        u32 banking_mode;
        u32 BANK1;
        u32 BANK2;
        u32 ext_RAM_enable;
    } regs;
    u32 cartRAM_offset;
};

void MBC1_new(base *parent, CLOCK *clock_in, core *bus_in);
void MBC1_delete(base *parent);
void MBC1_reset(base* parent);
void MBC1_set_cart(base* parent, CART* cart);

u32 MBC1_CPU_read(base* parent, u32 addr, u32 val, u32 has_effect);
void MBC1_CPU_write(base* parent, u32 addr, u32 val);

}