//
// Created by . on 5/12/26.
//

#include <cstring>
#include "helpers/setbits.h"

#include "ng_bus.h"


namespace NEOGEO {
NEOB1::NEOB1(core *parent) :
    bus(parent) {
    debug_output = std::make_unique<u32[]>(320 * 224);
    memset(debug_output.get(), 0, 320 * 224 * sizeof(u32));
}

static inline u8 nibble_swap(u8 in) {
    return ((in & 0xF) << 4) | ((in & 0xF0) >> 4);
}

template<bool do_debug>
void NEOB1::draw_line(u32 y, u32 *linebuf) const {
    // We will just be painting over any previous linebuf data!
    /*
    Bit # ...543210
    ...nHCLLL

    n: Tile number (multiple bits)
    H: Half, 0 = right, 1 = left
    C: Column in half, 0 = left, 1 = right
    L: Line number (0~7)
    */
    u32 *debug_linebuf = nullptr;
    if constexpr(do_debug) {
        debug_linebuf = debug_output.get() + (y * 320);
        memset(debug_linebuf, 0, 320 * sizeof(u32));
    }

    u32 col_ptr = 0x7000 + (y >> 3) + 2; // +2 for the two "invisible" top rows
    u32 y_in_tile = y & 7;
    u32 screen_x = 0;
    for (u32 tile_x = 0; tile_x < 40; tile_x++) {
        u16 tile_data = bus->read_VRAM(col_ptr);
        col_ptr += 0x20;
        u8 palette = getbits<12, 15>(tile_data);
        u32 palette_base = bus->io.pal_base_offset + (palette << 4);
        u32 tile_num = getbits<0, 11>(tile_data);
        u32 tile_addr = (tile_num << 5) | y_in_tile;
        u32 pixdata = bus->cart.read_S(tile_addr + 0x10);
        pixdata |= bus->cart.read_S(tile_addr + 0x18) << 8;
        pixdata |= bus->cart.read_S(tile_addr) << 16;
        pixdata |= bus->cart.read_S(tile_addr + 0x8) << 24;

        for (u32 in_tile_x = 0; in_tile_x < 8; in_tile_x++) {
            u32 col = pixdata & 15;
            pixdata >>= 4;
            if (col > 0) {
                u32 out_color = (bus->lpsc.io.shadow << 16) | bus->PRAM[palette_base + col];
                linebuf[screen_x] = out_color;
                if constexpr(do_debug) debug_linebuf[screen_x] = out_color;
            }
            screen_x++;
        }
    }
}

template void NEOB1::draw_line<false>(u32 y, u32 *linebuf) const;
template void NEOB1::draw_line<true>(u32 y, u32 *linebuf) const;

}
