#pragma once

#include "helpers/int.h"
#include "helpers/simplebuf.h"
#include "helpers/buf.h"
#include "helpers/physical_io.h"

namespace NEOGEO {
struct core;
// P = program
// V = "voice" ROM
// S = "fix layer" ROM
// M = Z80 8bit ROM and music data
// C = sprite GFX ROM

struct CART {
    explicit CART(core *parent);
    core *bus;

    simplebuf8 C{}; // sprite gfx
    simplebuf8 V[2]{}; // voice
    simplebuf8 S{}; // fix layer
    simplebuf8 M{}; // "Music" Z80
    simplebuf8 P[2]{}; // "Program"

    u32 P_bank{};
    void reset();

    bool load(multi_file_set &mfs, physical_io_device &which_pio);
    void write_P(u32 addr, u8 val);
    void write_M(u32 addr, u8 val);
    [[nodiscard]] u8 read_M(u32 addr) const;
    template<u8 num>[[nodiscard]] u16 read_P(u32 addr) const;
    [[nodiscard]] u8 read_C(u32 addr) const;
    [[nodiscard]] u8 read_S(u32 addr) const;
    [[nodiscard]] u8 read_V(u32 addr) const;
    [[nodiscard]] u32 V_size() const;

};
}
