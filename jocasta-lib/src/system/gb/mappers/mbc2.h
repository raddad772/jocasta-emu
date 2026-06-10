#pragma once

#include "helpers/int.h"
#include "mapper.h"
#include "../gb_clock.h"
#include "../gb_bus.h"

namespace GB::MAPPER {
struct MBC2 {
    core *bus;
    CLOCK *clock;

    u8 *ROM;
    u32 RAM_mask;
    u32 has_RAM;
    CART* cart;

    u32 ROM_bank_hi_offset;// = 16384;

    u32 num_ROM_banks;
    struct {
        u32 ROMB;
        u32 ext_RAM_enable;
    } regs;
};

void MBC2_new(base *parent, CLOCK *clock, core *bus);
void MBC2_delete(base *parent);
void MBC2_reset(base* parent);
void MBC2_set_cart(base* parent, CART* cart);

u32 MBC2_CPU_read(base* parent, u32 addr, u32 val, u32 has_effect);
void MBC2_CPU_write(base* parent, u32 addr, u32 val);

}