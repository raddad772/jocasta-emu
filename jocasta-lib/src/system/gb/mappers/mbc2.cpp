//
// Created by Dave on 2/2/2024.
//
//
// Created by Dave on 1/27/2024.
//

#include <cstdlib>
#include <cassert>
#include "stdio.h"
#include <cstring>

#include "helpers/serialize/serialize.h"

#include "mbc2.h"

namespace GB::MAPPER {
#define SELF struct MBC2* self = (MBC2*)parent->ptr

static void MBC2_update_banks(MBC2 *self);

static void serialize(base *parent, serialized_state &state)
{
    SELF;
#define S(x) Sadd(state, &(self-> x), sizeof(self-> x))
    S(ROM_bank_hi_offset);
    S(regs.ROMB);
    S(regs.ext_RAM_enable);
#undef S
}

static void deserialize(base *parent, serialized_state &state)
{
    SELF;
#define L(x) Sload(state, &(self-> x), sizeof(self-> x))
    L(ROM_bank_hi_offset);
    L(regs.ROMB);
    L(regs.ext_RAM_enable);
#undef L
}


void MBC2_new(base *parent, CLOCK *clock, core *bus)
{
    auto *self = static_cast<MBC2 *>(malloc(sizeof(MBC2)));
    parent->ptr = static_cast<void *>(self);

    self->ROM = nullptr;
    self->bus = bus;
    self->clock = clock;
    self->RAM_mask = 0;
    self->cart = nullptr;

    parent->CPU_read = &MBC2_CPU_read;
    parent->CPU_write = &MBC2_CPU_write;
    parent->reset = &MBC2_reset;
    parent->set_cart = &MBC2_set_cart;
    parent->serialize = &serialize;
    parent->deserialize = &deserialize;

    self->ROM_bank_hi_offset = 16384;

    self->num_ROM_banks = 0;
    self->regs.ROMB = 1;
    self->regs.ext_RAM_enable = 0;
}

void MBC2_delete(base *parent)
{
    if (parent->ptr == nullptr) return;
    SELF;

    if (self->ROM != nullptr) {
        free(self->ROM);
        self->ROM = nullptr;
    }

    free(parent->ptr);
}

void MBC2_reset(base* parent)
{
    SELF;
    self->ROM_bank_hi_offset = 16384;

    self->num_ROM_banks = 0;
    self->regs.ROMB = 1;
    self->regs.ext_RAM_enable = 0;
    MBC2_update_banks(self);
}

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4724) // warning C4724: potential mod by 0
#endif

static void MBC2_update_banks(MBC2 *self)
{
    self->ROM_bank_hi_offset = (self->regs.ROMB % self->num_ROM_banks) * 16384;
}

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

u32 MBC2_CPU_read(base* parent, u32 addr, u32 val, u32 has_effect)
{
    SELF;
    if (addr < 0x4000) // ROM lo bank
        return self->ROM[addr];
    if (addr < 0x8000) // ROM hi bank
        return self->ROM[(addr & 0x3FFF) + self->ROM_bank_hi_offset];
    if ((addr >= 0xA000) && (addr < 0xC000)) {
        if (!self->regs.ext_RAM_enable)
            return 0xFF;
        return (static_cast<u8 *>(self->cart->SRAM->data))[addr & 0x1FF] & 0x0F;
    }
    assert(1!=0);
    return 0xFF;
}

void MBC2_CPU_write(base* parent, u32 addr, u32 val)
{
    SELF;
    if (addr < 0x4000) {
        switch (addr & 0x100) {
            case 0x0000: // RAM write enable
                self->regs.ext_RAM_enable = +((val & 0x0F) == 0x0A);
                return;
            case 0x0100: // ROM bank number
                val &= 0x0F;
                if (val == 0) val = 1;
                self->regs.ROMB = val;
                MBC2_update_banks(self);
                return;
        }
    }
    else if ((addr >= 0xA000) && (addr < 0xC000)) { // cart RAM
        if (self->regs.ext_RAM_enable) {
            (static_cast<u8 *>(self->cart->SRAM->data))[addr & 0x1FF] = val & 0x0F;
            self->cart->SRAM->dirty = true;
        }
        return;
    }
}

void MBC2_set_cart(base* parent, CART* cart)
{
    SELF;
    self->cart = cart;

    if (self->ROM != nullptr) free(self->ROM);
    self->ROM = static_cast<u8 *>(malloc(cart->header.ROM_size));
    memcpy(self->ROM, cart->ROM, cart->header.ROM_size);

    self->RAM_mask = 0x1FF;

    self->num_ROM_banks = cart->header.ROM_size / 16384;
    printf("\nNUMBER OF ROM BANKS %d", self->num_ROM_banks);
}
}