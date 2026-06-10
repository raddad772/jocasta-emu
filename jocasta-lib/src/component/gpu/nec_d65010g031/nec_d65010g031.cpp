//
// Created by . on 3/28/26.
//

#include "nec_d65010g031.h"

namespace NEC_D65010G031 {

    /*
 256 pixels.
 1 read red, 1 green, 1 blue for shifters
 then a read of MATRIX if this is a MATRIX row, 64 reads = two rows

 */

void core::new_line() {
    shift.r = shift.g = shift.b = 0;
    hpos = 0;
    vpos++;
    cur_line = cur_output + (256 * vpos);
    tilemap_addr = 0xB800 + (32 * (vpos >> 3));

    if ((vpos >= 195) && (vpos <= 255) && (((vpos - 195) & 3) == 0)) {
        if (vpos == 195) io.IRQ_fired = true;
        irq(true);
    }

    if (vpos >= 262) {
        new_frame();
    }
}

void core::new_frame() {
    master_frame_count++;
    vpos = 0;
    tilemap_addr = 0xB800;
    display->active_draw_buffer ^= 1;
    cur_output = static_cast<u8 *>(display->output[display->active_draw_buffer]);
    cur_line = cur_output;
}

void core::busreq(bool level) {
    if (level != busreq_level) {
        set_busreq(callback_ptr, level);
    }
    busreq_level = level;
}

void core::irq(bool level) {
    if (level != irq_level) {
        set_irq(callback_ptr, level);
    }
    irq_level = level;
}

unsigned char revr8(unsigned char b) {
    b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
    b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
    b = (b & 0xAA) >> 1 | (b & 0x55) << 1;
    return b;
}

void core::cycle() {
    if (vpos < 192) {
        // NOTE to future emulator authors:
        // this is a best-guess of how it *might* work.
        //
        // Tile reads consume the bus for hpos 0-255 (32 tiles × 8 cycles each).
        // Pixel output extends 32 cycles further (hpos 32-287 ... cur_line[0-255])
        // because the shift registers keep draining after the last tile is read.
        if (hpos < 256) {
            busreq(true);
            switch (hpos & 7) {
                case 0: // red read!
                    shift.r |= revr8(read_mem(callback_ptr, tile_addr)) << 8;
                    tile_addr += 8;
                    break;
                case 2: // green read!
                    shift.g |= revr8(read_mem(callback_ptr, tile_addr)) << 6;
                    tile_addr += 8;
                    break;
                case 4: // blue read!
                    shift.b |= revr8(read_mem(callback_ptr, tile_addr)) << 4;
                    tile_addr += 8;
                    break;
                case 6: {
                    // tile read! this would happen differently on real chip?
                    u16 tile_num = read_mem(callback_ptr, tilemap_addr++);
                    // Bytes from 0...7 not used
                    // Bytes from 8...15 red
                    // Bytes from 16...23 green
                    // Bytes from 24...31 blue
                    u16 offset = ((vpos & 7)) + 8; // Y offset
                    if (tile_num < 0xe0 || io.all_tiles_from_ROM) {
                        tile_addr = io.ROM_tile_addr + (tile_num << 5) + offset;
                    }
                    else {
                        tile_num -= 0xe0;
                        tile_addr = 0xBC00 + (tile_num << 5) + offset;
                    }
                    break;
                }
            }
        }
        else {
            busreq(false);
        }

        // Pixel output: hpos 0-23 are border/init cycles (shift reset, written to
        // the right edge of the line buffer and later overwritten by visible content).
        // hpos 24-279 shift out all 256 game pixels to cur_line[0..255].
        if (hpos < 24) {
            //cur_line[hpos + 232] = 0;
            shift.r = shift.g = shift.b = 0;
        }
        else if (hpos < 256) { // 24 border cycles + 248 visible pixels
            u8 v = shift.r & 1;
            v |= (shift.g << 1) & 2;
            v |= (shift.b << 2) & 4;
            cur_line[hpos - 24] = v;
            shift.r >>= 1;
            shift.g >>= 1;
            shift.b >>= 1;
        }
    }
    else {
        busreq(false);
    }
    if (hpos == 190) irq(false);
    hpos++;
    if (hpos >= 380) {
        new_line();
    }
}

void core::reset() {
    hpos = vpos = 0;
    master_frame_count = 0;
    sq[0].reload = sq[1].reload = sq[2].reload = 0x3F;
}

u8 core::read(u8 addr, u8 old, bool has_effect) {
    addr |= 0xF8;
    u8 v;
    switch (addr) {
        case 0xFC:
            v = io.IRQ_fired;
            io.IRQ_fired = false;
            v |= io.fd_data ? 2 : 0;
            return v;
        case 0xFD:
            io.fd_data &= io.fd_control ^ 0xF;
            return joy_read(callback_ptr);
        case 0xFF:
            v = (io.all_tiles_from_ROM << 4);
            v |= (io.ROM_tile_addr >> 8);
            v |= io.border_color;
            return v;
    }
    printf("\nUnimplemented VDP read from %02x", addr);
    return old;
}


void core::cycle_psg() {
    for (auto & s : sq) {
        if (s.reload == 0x3F) continue;
        if (++s.counter >= 0x40) {
            s.polarity ^= 1;
            s.counter = s.reload;
        }
    }
}

i16 core::sample_debug_wf(int num) {
    i32 p = sq[num].polarity ? 0 : 0x7FFF;
    return p;
}

i16 core::sample_psg(bool debug) {
    // 1/2 vol square 0,
    // 1/4 vol square 1,
    // 1/8 vol square 2.
    i32 v = 0; // range: 0...0x7000
    if (debug || psg_ext_enable) {
        if (sq[0].reload != 0x3F && (debug || sq[0].ext_enable)) v += 0x1000 * sq[0].polarity;
        if (sq[1].reload != 0x3F && (debug || sq[1].ext_enable)) v += 0x1000 * sq[1].polarity;
        if (sq[2].reload != 0x3F && (debug || sq[2].ext_enable)) v += 0x1000 * sq[2].polarity;
    }
    return static_cast<i16>(v);
}

void core::write(u8 addr, u8 val) {
    addr |= 0xF8;
    switch (addr) {
        case 0xF8: // square0
        case 0xF9: // square1
        case 0xFA: {// square2
            auto &s = sq[addr - 0xF8];
            s.reload = val & 0x3F;
            s.counter = val & 0x3F;
            //printf("\nCH %d SET TO %02c")
            return; }
        case 0xFD:
            io.fd_control = val & 0xF;
            io.fd_data = 0xF;
            joy_write(callback_ptr, val);
            return;
        case 0xFF:
            io.all_tiles_from_ROM = (val >> 4) & 1;
            io.ROM_tile_addr = (static_cast<u16>(val) << 8) & 0x2000;
            io.border_color = val & 7;
            return;
    }
    static int a = 0;
    if (a < 4) {
        a++;
        printf("\nUnimplemented VDP write to %02x: %02x", addr, val);
    }
    else {
        static int b = 0;
        if (b == 1) {
            printf("\nStopping spam about them but they continue...");
            b = 1;
        }
    }
}

}
