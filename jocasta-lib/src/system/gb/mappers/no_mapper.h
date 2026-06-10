#pragma once
#include "helpers/int.h"
#include "mapper.h"
#include "../gb_bus.h"
namespace GB::MAPPER {
struct none {
    CLOCK *clock;
    core *bus;

    u8 *ROM;
    u32 ROM_bank_offset;
    u32 RAM_mask;
    u32 has_RAM;
    CART* cart;
};

void none_new(base *parent, CLOCK *clock_in, core *bus_in);
void none_delete(base *parent);
void GBMN_reset(base* parent);
void GBMN_set_cart(base* parent, CART* cart);

u32 GBMN_CPU_read(base* parent, u32 addr, u32 val, u32 has_effect);
void GBMN_CPU_write(base* parent, u32 addr, u32 val);

}