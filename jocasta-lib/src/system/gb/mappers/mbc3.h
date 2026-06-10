//
// Created by Dave on 2/2/2024.
//
#pragma once

#include "helpers/int.h"
#include "mapper.h"
#include "../cart.h"
#include "../gb_clock.h"
#include "../gb_bus.h"
namespace GB::MAPPER {
struct MBC3 {
    core *bus;
    CLOCK *clock;

    u8 *ROM;
    u32 has_RAM;
    CART* cart;

    struct {
        u32 ext_RAM_enable;
        u32 ROM_bank_lo;
        u32 ROM_bank_hi;
        u32 RAM_bank;
        u32 last_RTC_latch_write;
        u32 RTC_latched[5];
        u64 RTC_start;
    } regs;
    u32 ROM_bank_offset_hi;
    u32 RAM_bank_offset;
    u32 num_ROM_banks;
    u32 num_RAM_banks;
};

void MBC3_new(base *parent, CLOCK *clock_in, core *bus_in);
void MBC3_delete(base *parent);
void MBC3_reset(base* parent);
void MBC3_set_cart(base* parent, CART* cart);

u32 MBC3_CPU_read(base* parent, u32 addr, u32 val, u32 has_effect);
void MBC3_CPU_write(base* parent, u32 addr, u32 val);
}
