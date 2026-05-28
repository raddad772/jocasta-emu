//
// Created by . on 11/25/25.
//
#include <cstring>
#include <cassert>
#include <cstdio>

#include "vic2.h"
#include "c64_bus.h"

#include "c64_vic2_color.h"

#include "helpers/enums.h"

namespace VIC2 {

core::core(C64::core *parent) : bus(parent) {
    for (u32 i = 0; i < 16; i++) {
        palette[i] = color::pepto::get_abgr(i, 1, 50.0f, 100.0f, 100.0f);
    }
    for (u32 i = 0; i < 8; i++) {
        sprites[i].num = i;
        sprites[i].bit = 1 << i;
    }
}

void core::update_IRQs() {
    io.IF.IRQ = (io.IF.u & io.IE.u & 15) != 0;
    bus->update_IRQ(1, io.IF.IRQ);
}

u8 core::read_IO(u16 addr, u8 old, bool has_effect) {
    addr &= 0x3F;
    switch (addr | 0xD000) {
        case 0xD000:
        case 0xD002:
        case 0xD004:
        case 0xD006:
        case 0xD008:
        case 0xD00A:
        case 0xD00C:
        case 0xD00E: {
            sprite &sp = sprites[addr >> 1];
            return sp.x & 0xFF;
        }
        case 0xD001:
        case 0xD003:
        case 0xD005:
        case 0xD007:
        case 0xD009:
        case 0xD00B:
        case 0xD00D:
        case 0xD00F: {
            sprite &sp = sprites[addr >> 1];
            return sp.y; }
        case 0xD010: return io.D010;
        case 0xD011: return (io.CR1.u & 0x7F) | ((vpos & 0x100) >> 1);
        case 0xD012: return vpos;
        case 0xD013: return io.LPX;
        case 0xD014: return io.LPY;
        case 0xD015: return io.ME;
        case 0xD016: return io.CR2.u | 0xC0;
        case 0xD017: return io.MYE; // sprite expansion
        case 0xD018: return io.D018 | 1;
        case 0xD019: return io.IF.u | 0x70;
        case 0xD01A: return io.IE.u | 0xF0;
        case 0xD01B: return io.MDP;
        case 0xD01C: return io.MMC;
        case 0xD01D: return io.MXE;
        case 0xD01E: {
            u8 r = io.SSx;
            if (has_effect) io.SSx = 0;
            return r; }
        case 0xD01F: {
            u8 r = io.SDx;
            if (has_effect) io.SDx = 0;
            return r; }
        case 0xD020: return io.EC;
        case 0xD021: return io.BC[0];
        case 0xD022: return io.BC[1];
        case 0xD023: return io.BC[2];
        case 0xD024: return io.BC[3];
        case 0xD025: return io.MM[0];
        case 0xD026: return io.MM[1];
        case 0xD027: return io.MC[0];
        case 0xD028: return io.MC[1];
        case 0xD029: return io.MC[2];
        case 0xD02A: return io.MC[3];
        case 0xD02B: return io.MC[4];
        case 0xD02C: return io.MC[5];
        case 0xD02D: return io.MC[6];
        case 0xD02E: return io.MC[7];
    }
    return 0xFF;
}

void core::update_SSx(u8 val) {
    if ((io.SSx == 0) && (val > 0)) {
        io.IF.MMC = 1;
        update_IRQs();
    }
    io.SSx |= val;
}

void core::update_SDx(u8 val) {
    if ((io.SDx == 0) && (val > 0)) {
        io.IF.MBC = 1;
        update_IRQs();
    }
    io.SDx |= val;
}

void core::write_IO(u16 addr, u8 val) {
    addr &= 0x3F;
    switch (addr | 0xD000) {
        case 0xD000:
        case 0xD002:
        case 0xD004:
        case 0xD006:
        case 0xD008:
        case 0xD00A:
        case 0xD00C:
        case 0xD00E: {
            sprite &sp = sprites[addr >> 1];
            sp.x = (sp.x & 0x100) | val;
            return;
        }
        case 0xD001:
        case 0xD003:
        case 0xD005:
        case 0xD007:
        case 0xD009:
        case 0xD00B:
        case 0xD00D:
        case 0xD00F: {
            sprite &sp = sprites[addr >> 1];
            sp.y = val;
            return; }
        case 0xD010:
            sprites[0].x = (sprites[0].x & 0xFF) | ((val & 1) << 8);
            sprites[1].x = (sprites[1].x & 0xFF) | ((val & 2) << 7);
            sprites[2].x = (sprites[2].x & 0xFF) | ((val & 4) << 6);
            sprites[3].x = (sprites[3].x & 0xFF) | ((val & 8) << 5);
            sprites[4].x = (sprites[4].x & 0xFF) | ((val & 0x10) << 4);
            sprites[5].x = (sprites[5].x & 0xFF) | ((val & 0x20) << 3);
            sprites[6].x = (sprites[6].x & 0xFF) | ((val & 0x40) << 2);
            sprites[7].x = (sprites[7].x & 0xFF) | ((val & 0x80) << 1);
            io.D010 = val;
            return;
        case 0xD011: // control register 1
            io.CR1.u = val;
            if (io.CR1.RSEL) {
                timing.frame.first_line = 51;
                timing.frame.last_line = 251;
            }
            else {
                timing.frame.first_line = 55;
                timing.frame.last_line = 247;
            }
            update_raster_compare((io.raster_compare & 0xFF) | ((val & 0x80) << 1));
            update_bad_line();
            update_IRQs();
            return;
        case 0xD012: // raster counter
            // TODO: set comparison line for raster interrupt
            update_raster_compare((io.raster_compare & 0x100) | val);
            update_IRQs();
            return;
        case 0xD013: // LPX
            return;
        case 0xD014: // LPY
            return;
        case 0xD015:
            io.ME = val;
            sprites[0].enabled = (val >> 0) & 1;
            sprites[1].enabled = (val >> 1) & 1;
            sprites[2].enabled = (val >> 2) & 1;
            sprites[3].enabled = (val >> 3) & 1;
            sprites[4].enabled = (val >> 4) & 1;
            sprites[5].enabled = (val >> 5) & 1;
            sprites[6].enabled = (val >> 6) & 1;
            sprites[7].enabled = (val >> 7) & 1;
            return;
        case 0xD016:
            io.CR2.u = val;
            if (io.CR2.CSEL) {
                timing.line.first_col = 24 + (13*8);
                timing.line.last_col = 344 + (13*8);
            }
            else {
                timing.line.first_col = 31 + (13*8);
                timing.line.last_col = 335 + (13*8);
            }
            return;
        case 0xD017:
            io.MYE = val;
            sprites[0].y_expand = (val >> 0) & 1;
            sprites[1].y_expand = (val >> 1) & 1;
            sprites[2].y_expand = (val >> 2) & 1;
            sprites[3].y_expand = (val >> 3) & 1;
            sprites[4].y_expand = (val >> 4) & 1;
            sprites[5].y_expand = (val >> 5) & 1;
            sprites[6].y_expand = (val >> 6) & 1;
            sprites[7].y_expand = (val >> 7) & 1;
            for (auto &sp : sprites) {
                if (!sp.y_expand) sp.y_expand_flipflop = 1;
            }
            return;
        case 0xD018:
            io.D018 = val;
            io.ptr.VM = (val & 0xF0) << 6;
            io.ptr.CB = (val & 0x0E) << 10;
            return;
        case 0xD019:
            io.IF.u &= ~(val & 15);
            update_IRQs();
            return;
        case 0xD01A:
            io.IE.u = val & 15;
            update_IRQs();
            return;
        case 0xD01B:
            io.MDP = val;
            return;
        case 0xD01C:
            io.MMC = val;
            sprites[0].multicolor = (val >> 0) & 1;
            sprites[1].multicolor = (val >> 1) & 1;
            sprites[2].multicolor = (val >> 2) & 1;
            sprites[3].multicolor = (val >> 3) & 1;
            sprites[4].multicolor = (val >> 4) & 1;
            sprites[5].multicolor = (val >> 5) & 1;
            sprites[6].multicolor = (val >> 6) & 1;
            sprites[7].multicolor = (val >> 7) & 1;
            return;
        case 0xD01D:
            io.MXE = val;
            sprites[0].x_expand = (val >> 0) & 1;
            sprites[1].x_expand = (val >> 1) & 1;
            sprites[2].x_expand = (val >> 2) & 1;
            sprites[3].x_expand = (val >> 3) & 1;
            sprites[4].x_expand = (val >> 4) & 1;
            sprites[5].x_expand = (val >> 5) & 1;
            sprites[6].x_expand = (val >> 6) & 1;
            sprites[7].x_expand = (val >> 7) & 1;
            return;
        case 0xD01E:
            return;
        case 0xD01F:
            return;
        case 0xD020: io.EC = val & 15; return;
        case 0xD021: io.BC[0] = val & 15; return;
        case 0xD022: io.BC[1] = val & 15; return;
        case 0xD023: io.BC[2] = val & 15; return;
        case 0xD024: io.BC[3] = val & 15; return;
        case 0xD025: io.MM[0] = val & 15; return;
        case 0xD026: io.MM[1] = val & 15; return;
        case 0xD027: io.MC[0] = val & 15; return;
        case 0xD028: io.MC[1] = val & 15; return;
        case 0xD029: io.MC[2] = val & 15; return;
        case 0xD02A: io.MC[3] = val & 15; return;
        case 0xD02B: io.MC[4] = val & 15; return;
        case 0xD02C: io.MC[5] = val & 15; return;
        case 0xD02D: io.MC[6] = val & 15; return;
        case 0xD02E: io.MC[7] = val & 15; return;
    }

}

void core::write_color_ram(u16 addr, u8 val) {
    addr &= 0x3FF;
    COLOR[addr] = val & 15;
}

u8 core::read_color_ram(u16 addr, u8 old, bool has_effect) {
    addr &= 0x3FF;
    return (open_bus & 0xF0) | COLOR[addr];
}

void core::reset() {
    hpos = 0;
    vpos = -1;
    new_frame();
}

void core::do_g_access() {
    // Address generator has 3 modes
    // BMM=0 character generator
    // BMM=1 bitmap access
    // if ECM is set, address lines 9 and 10 are held low
    u16 addr;
    if (io.CR1.BMM) {
        addr = (io.ptr.CB & 0x2000) | ((vc & 0x3FF) << 3) | rc;
    }
    else {
        addr = rc | ((mtx & 0xFF) << 3) | io.ptr.CB;
    }
    if (io.CR1.ECM) addr &= 0xF9FF;
    //if (vpos == 78) printf("\nG V:%d X:%d addr:%04x mtx:%02x", vpos, hpos, addr, mtx & 0xFF);
    g_access = open_bus = read_mem(read_mem_ptr, addr);
}

void core::mem_access() {
    u32 lcyc = hpos >> 3;
    g_access = 0;
    //assert(vc<1000);
    if ((lcyc >= 16) && (lcyc <= 55) && (state == displaying)) {
        if (vmli < 40) mtx = SCREEN_MTX[vmli];
        do_g_access();
        vmli++;
        vc++;
        if (vc == 1000) vc = 0;
    }
    else {
        g_access = 0;
        // If nothing else, we do an idle access
        read_mem(read_mem_ptr, io.CR1.ECM ? 0x39FF : 0x3FFF);
    }
    if ((lcyc >= 15) && (lcyc <= 54)) {
        // do read of matrtix
        signals.AEC = signals.BA;
        u16 addr = vc | io.ptr.VM;
        //if (vpos == 78) printf ("\n\nC V:%d X:%d addr:%04x VMLI:%d LC:%d", vpos, hpos, addr, vmli, lcyc);
        assert(vmli < 40);
        if (bad_line) {
            open_bus = read_mem(read_mem_ptr, addr);
            SCREEN_MTX[vmli] = open_bus | (COLOR[vc & 0x3FF] << 8);
        }
    }
    //if (vpos == 78) printf("\nV:%d X:%d val:%03x", vpos, hpos, mtx);
    //if (vc == 50) mtx = SCREEN_MTX[vmli] | (10 << 8);
}

void core::reload_shifter() {
    u32 reverse = 0;
    //if (vpos == 78) printf("\nRELOAD SHIFTER %d %02x", hpos, g_access);
    for (u32 i = 0; i < 8; i++) {
        u8 bit = (g_access >> i) & 1;
        reverse |= bit << (7 - i);
    }
    /*if ((vpos == 78) && (hpos == 432)) {
        printf("\nFAKE RELOAD!");
        reverse = 0b11000011;
    }*/
    px_shifter |= reverse << shift_size;
    shift_size += 8;
    //assert(shift_size < 32);
    if (shift_size >= 33) {
        shift_size = 32;
    }
}

u8 core::get_color() {
    u8 out;
    u8 px;
    u8 mode = (io.CR1.ECM << 2) | (io.CR1.BMM << 1) | io.CR2.MCM;
    if (mode == 0) { // standard text
        px = px_shifter & 1;
        bg_collide = px;
        px_shifter >>= 1;
        shift_size = (shift_size > 0) ? shift_size - 1 : 0;
        out = px ? (mtx >> 8) : io.BC[0];
    }
    else if (mode == 1) { // multicolor text
        // How to interpret this depends on bit 11 of color data
        if (mtx & 0x800) { // MC flag=1 2-bit colors
            if (!(hpos & 1)) {
                px = ((px_shifter & 1) << 1) | ((px_shifter >> 1) & 1);
                bg_collide = px >= 2;
                if (px == 3) old_color = (mtx >> 8) & 7;
                else old_color = io.BC[px];
                px_shifter >>= 2;
                shift_size = (shift_size > 1) ? shift_size - 2 : 0;
            }
            out = old_color;
        }
        else { // MC flag=0
            px = px_shifter & 1;
            bg_collide = px;
            out = px ? ((mtx >> 8) & 7) : io.BC[0];
            px_shifter >>= 1;
            shift_size = (shift_size > 0) ? shift_size - 1 : 0;
        }
    }
    else if (mode == 2) { // standard bitmap
        px = px_shifter & 1;
        bg_collide = px;
        out = px ? ((mtx >> 4) & 15) : (mtx & 15);
        px_shifter >>= 1;
        shift_size = (shift_size > 0) ? shift_size - 1 : 0;
    }
    else if (mode == 3) { // multicolor bitmap
        if (!(hpos & 1)) {
            px = ((px_shifter & 1) << 1) | ((px_shifter >> 1) & 1);
            bg_collide = px >= 2;
            switch(px) {
                case 0: old_color = io.BC[0]; break;
                case 1: old_color = (mtx >> 4) & 15; break;
                case 2: old_color = mtx & 15; break;
                case 3: old_color = (mtx >> 8) & 15; break;
            }
            px_shifter >>= 2;
            shift_size = (shift_size > 1) ? shift_size - 2 : 0;
        }
        out = old_color;
    }
    else if (mode == 4) { // ECM text
        px = px_shifter & 1;
        bg_collide = px;
        out = px ? ((mtx >> 8) & 15) : io.BC[(mtx >> 6) & 3];
        px_shifter >>= 1;
        shift_size = (shift_size > 0) ? shift_size - 1 : 0;
    }
    else {
        bg_collide = 0;
        out = 0;
    }
    return out;
}

void core::shift_sprites() {
    for (auto &sp : sprites) {
        if (!sp.displaying || !sp.shifter.bits_left) {
            if (!sp.shifter.bits_left) {
                sprites_shifting &= ~sp.bit;
            }
            sp.shifter.color_latch = 0;
            sp.shifter.pixel_hold = 0;
            continue;
        }
        if (sp.shifter.bits_left > 24) {
            // Countdown til real shift out start
            sp.shifter.bits_left--;
            sp.shifter.color_latch = 0;
            continue;
        }
        if (sp.shifter.pixel_hold) {
            sp.shifter.pixel_hold--;
            continue;
        }
        if (sp.multicolor) {
            sp.shifter.color_latch = (sp.shifter.data >> 30) & 3;
            sp.shifter.bits_left -= 2;
            sp.shifter.data <<= 2;
        }
        else {
            sp.shifter.color_latch = (sp.shifter.data >> 31) & 1;
            sp.shifter.bits_left -= 1;
            sp.shifter.data <<= 1;
        }
        sp.shifter.pixel_hold = (sp.multicolor ? 2 : 1) * (sp.x_expand ? 2 : 1) - 1;
    }
}

void core::output_px() {
    if (sprites_shifting != 0) {
        shift_sprites();
    }
    if (hpos == timing.line.first_col) hborder_on = false;
    if (hpos == timing.line.last_col) hborder_on = true;
    if (hpos>timing.line.pixels_per) {
        printf("\nUH OH NO TOO MANY PIXELS %d", hpos);
    }
    u8 bg_color = get_color();
    if (vborder_on || hborder_on) {
        line_output[hpos++] = io.EC;
    }
    else {
        u8 sprite_color = 0;
        u8 sprite_mask = 0;
        bool sprite_on = false;
        for (auto &sp : sprites) {
            if (!sp.shifter.color_latch) continue;
            u8 c;
            if (sp.multicolor) {
                switch(sp.shifter.color_latch) {
                    case 1: c = io.MM[0]; break;
                    case 2: c = io.MC[sp.num]; break;
                    case 3: c = io.MM[1]; break;
                    default: c = 0; break;
                }
            }
            else {
                c = io.MC[sp.num];
            }
            sprite_mask |= sp.bit;
            if (!sprite_on && (!(io.MDP & sp.bit) || !bg_collide)) {
                sprite_on = true;
                sprite_color = c;
            }
        }
        if (sprite_mask && bg_collide) update_SDx(sprite_mask);
        if (sprite_mask & (sprite_mask - 1)) update_SSx(sprite_mask);
        line_output[hpos] = sprite_on ? sprite_color : bg_color;
        //line_output[hpos] = state == displaying ? 15 : 9;
        hpos++;
    }

}

void core::sprite_ptr_num(u8 num) {
    auto &sp = sprites[num];
    sp.pointer = read_mem(read_mem_ptr, io.ptr.VM | (1016 + num)) << 6;
    // TODO: signals to pause CPU for sprite data reads
    if (sp.dma) {
        u32 addr = sp.pointer | sp.mc;
        sp.mc = (sp.mc + 3) & 0x3F;
        sp.shifter.bits_left = 24;
        sp.shifter.pixel_hold = 0;
        sp.shifter.color_latch = 0;
        sp.shifter.data = (read_mem(read_mem_ptr, addr) << 24) | (read_mem(read_mem_ptr, (addr + 1) & 0x3FFF) << 16) | (read_mem(read_mem_ptr, (addr + 2) & 0x3FFF) << 8);
    }
}

void core::check_sprite_shifts() {
    // Check if any sprites are due to start in the next 8 pixels
    for (auto &sp : sprites) {
        if (sp.enabled && sp.displaying && !(sprites_shifting & sp.bit) && (sp.line_x_counter >= 0)) {
            if (sp.line_x_counter <= 7) {
                sp.shifter.bits_left += sp.line_x_counter;
                sprites_shifting |= sp.bit;
                sp.line_x_counter = -1;
            }
            else
                sp.line_x_counter -= 8;
        }
    }
}

void core::cycle() {

    // either in display state or not
    // regardless of display state, chargen/bitmap reads happen
    // only display state do badlinematrix/color RAM reads happen
    //
    mem_access();

    u32 lcyc = hpos >> 3;
    // (0-based)
    // 57, 59, 61 = sprite 0, 1, 2
    // 0, 2, 4, 6, 8 = sprite 3, 4, 5, 6, 7 pointers
    switch (lcyc) {
        case 0:
        case 2:
        case 4:
        case 6:
        case 8:
            sprite_ptr_num((lcyc >> 1) + 3);
            break;
        case 11:
            if (bad_line) signals.BA = true;
            break;
        case 13:
            vc = vcbase;
            vmli = 0;
            if (bad_line) rc = 0;
            break;
        case 14: sprites_15(); break;
        case 15: sprites_16(); break;
        case 54: sprites_55(); break;
        case 55:
            sprites_56();
            signals.BA = false;
            signals.AEC = false;
            break;
        case 57:
            sprites_58();
        case 59:
        case 61:
            sprite_ptr_num((lcyc - 57) >> 1);
            break;

            case 58:
            if (rc == 7) {
                vcbase = vc;
                if (bad_line) {
                    state = displaying;
                }
                else {
                    state = idle;
                }
            }
            if (state == displaying) {
                rc = (rc + 1) & 7;
            }
            break;
    }

    reload_shifter();

    check_sprite_shifts();

    for (u32 i = 0; i < 8; i++) {
        output_px();
    }
    // Generate border

    /* so we have 4 data...
     * register, RAM, screen matrix, color RAM
    1 cycle = 8 pixel
    1 RAM read per active cycle,
    or with AEC set to 1, we get our normal RAM read + a second to refill screen matrix
    */
    // Of course where IS active area? waht about IRQ etc.?

    // So during active 65 cycles,
    // BA=1 3 cycles before AEC=1
    // BA=0 1 cycle before AEC=0
    if (hpos >= timing.line.pixels_per) new_scanline();
}

static void schedule_scanline(void *ptr, u64 key, u64 clock, u32 jitter) {
    auto *v = static_cast<VIC2::core *>(ptr);
    clock -= jitter;
    v->new_scanline();

    //v->bus->scheduler.only_add_abs_w_tag(clock + v->timing.frame.cycles_per, 0, v, &schedule_scanline, nullptr, 2);
}

void core::new_frame() {
    master_frame++;
    //printf("\nNEW FRAME %lld", master_frame);
    vpos = 0;
    vcbase = 0;
    state = idle;
    bad_line = false;
    io.latch.DEN = 0;
    signals.BA = false;
    signals.AEC = false;
    if (bus->dbg.events.view.vec) {
        debugger_report_frame(bus->dbg.interface);
    }
    display->active_draw_buffer ^= 1;
    cur_output = static_cast<u8 *>(display->output[display->active_draw_buffer]);
    line_output = cur_output;
    memset(cur_output, 0, timing.frame.num_pixels);

    field ^= 1;
    //v->vpos = -1;

    //v->bus->scheduler.only_add_abs_w_tag(cur_clock + v->timing.line.cycles_per, 0, v, &new_frame, nullptr, 2);
}

void core::update_raster_compare(u16 val) {
    io.raster_compare = val;
    do_raster_compare();
}

void core::sprites_55() {
    for (auto & sp : sprites) {
        if (sp.y_expand) sp.y_expand_flipflop ^= 1;
        else sp.y_expand_flipflop = 1;
    }
}

/*
   In the first phases of cycle 55 and 56, the VIC checks for every sprite
   if the corresponding MxE bit in register $d015 is set and the Y
   coordinate of the sprite (odd registers $d001-$d00f) match the lower 8
   bits of RASTER. If this is the case and the DMA for the sprite is still
   off, the DMA is switched on, MCBASE is cleared, and if the MxYE bit is
   set the expansion flip flip is reset.
   */
void core::sprites_56() {
    for (auto & sp: sprites) {
        if (!sp.enabled) continue;
        if (sp.y == (vpos & 0xFF) && !sp.dma) {
            sp.dma = true;
            sp.mcbase = 0;
            if (sp.y_expand) sp.y_expand_flipflop = 0;
            else sp.y_expand_flipflop = 1;
        }
    }
}

/*
    In the first phase of cycle 58, the MC of every sprite is loaded from
       its belonging MCBASE (MCBASE->MC) and it is checked if the DMA for the
       sprite is turned on and the Y coordinate of the sprite matches the lower
       8 bits of RASTER. If this is the case, the display of the sprite is
       turned on. */
void core::sprites_15() {
    for (auto &sp : sprites) {
        if (sp.y_expand_flipflop) {
            sp.mcbase = (sp.mcbase + 2) & 63;
        }
    }
}

void core::sprites_16() {
    for (auto &sp : sprites) {
        if (sp.y_expand_flipflop) {
            sp.mcbase = (sp.mcbase + 1) & 63;
            if (sp.mcbase == 63) {
                sp.dma = false;
                sp.displaying = false;
                sprites_shifting &= ~sp.bit;
            }
        }
    }
}

void core::sprites_58() {
    for (auto &sp : sprites) {
        sp.mc = sp.mcbase;
        if (sp.dma && (sp.y == (vpos & 0xFF))) {
            sp.displaying = true;
            sp.line_x_counter = sp.x;
            sp.shifter.pixel_hold = 0;
            sp.shifter.color_latch = 0;
        }
        sp.x_expand_flipflop = 1;
    }
}

void core::update_vpos(u16 val) {
    vpos = val;
    do_raster_compare();
    line_output = cur_output + (timing.line.pixels_per * vpos);
}

void core::do_raster_compare() {
    if (!io.raster_compare_protect && !io.IF.RST && io.raster_compare == vpos) {
        io.IF.RST = 1;
        io.raster_compare_protect = true;
        update_IRQs();
    }
}

void core::update_bad_line() {
    if ((vpos == 0x30) && io.CR1.DEN) {
        io.latch.DEN = 1;
    }
    bad_line = io.latch.DEN && (vpos >= 0x30) && (vpos <= 0xF7) && ((vpos & 7) == io.CR1.YSCROLL);
    if (bad_line) {
        state = displaying;
        u32 lcyc = hpos >> 3;
        if ((lcyc >= 12) && (lcyc <= 54)) signals.BA = true;
    }
}

void core::new_scanline() {
    //timing.scanline_start = cur_clock;
    hpos = 0;
    px_shifter = 0;
    shift_size = io.CR2.XSCROLL;
    for (auto &sp : sprites) {
        if (sp.displaying) sp.line_x_counter = sp.x;
    }
    io.raster_compare_protect = io.IF.RST;
    update_vpos(vpos+1);
    //printf("\nNEW SCANLINE %d", vpos);
    if ((vpos == timing.frame.first_line) && io.CR1.DEN) vborder_on = false;
    if (vpos == timing.frame.last_line) vborder_on = true;
    if (bus->dbg.events.view.vec) {
        debugger_report_line(bus->dbg.interface, vpos);
    }


    /*
    A Bad Line Condition is given at any arbitrary clock cycle if, at the
    negative edge of ϕ0 at the beginning of the cycle, RASTER >= $30 and RASTER
    <= $f7 and the lower three bits of RASTER are equal to YSCROLL, and if the
    DEN bit was set during an arbitrary cycle of raster line $30.
    */
    update_bad_line();
    if (vpos >= timing.frame.lines_per) new_frame();
}

void core::schedule_first() {
    schedule_scanline(this, 0, 0, 0);
    //bus->scheduler.only_add_abs_w_tag(timing.frame.cycles_per, 0, this, &new_frame, nullptr, 2);
    // TODO: schedule drawing...

}

void core::setup_timing() {
    //printf("\nDISPLAY STANDARD %d", bus->display_standard);
    switch (bus->display_standard) {
        case jsm::display_standards::NTSC: {
            printf("\nNTSC SETUP");
            timing.line.cycles_per = 65;
            timing.line.pixels_per = 520;
            timing.frame.lines_per = 263;
            timing.frame.cycles_per = timing.line.cycles_per * timing.frame.lines_per;
            timing.vblank.first_line = 13;
            timing.vblank.last_line = 40;
            // NTSC crystal 14.31818 MHz / 14 = 1,022,727.3 Hz CPU; / (65 × 263) ... ≈59.826 Hz
            timing.second.frames_per = (14318180.0 / 14.0) / (65.0 * 263.0);
            //  pixels are 412-489-396;
            break;
        }
        case jsm::display_standards::PAL: {
            printf("\nPAL SETUP");
            timing.line.cycles_per = 63;
            timing.line.pixels_per = 504;
            timing.frame.lines_per = 312;
            timing.frame.cycles_per = timing.line.cycles_per * timing.frame.lines_per;
            timing.vblank.first_line = 300;
            timing.vblank.last_line = 15;
            // PAL crystal 17.734472 MHz / 18 = 985,248.4 Hz CPU; / (63 × 312) ... ≈50.124 Hz
            timing.second.frames_per = (17734472.0 / 18.0) / (63.0 * 312.0);
            // pixels are 404-480-380
            break;
        default:
            printf("\nCRAP WRONG SETUP");
            assert(1==2);
            return;
        }
    }
    timing.frame.num_pixels = timing.frame.lines_per * timing.line.pixels_per;
    timing.frame.cycles_per = timing.line.cycles_per * timing.frame.lines_per;
    timing.second.cycles_per = (u32)(timing.frame.cycles_per * timing.second.frames_per);
}

}
