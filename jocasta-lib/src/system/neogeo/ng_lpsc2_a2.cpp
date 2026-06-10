//
// Created by . on 5/11/26.
//
#include <cstring>
#include "helpers/setbits.h"

#include "ng_lpsc2_a2.h"
#include "ng_bus.h"

namespace NEOGEO::LPSC2_A2 {
core::core(NEOGEO::core *parent) : bus(parent) {
    sprs.reserve(96);
    debug_output = std::make_unique<u32[]>(320 * 224);
    memset(debug_output.get(), 0, 320 * 224 * sizeof(u32));
    bus->m68k.register_iack_handler(static_cast<void *>(this), &core::handle_iack<false>, &core::handle_iack<true>);
}

void core::do_scanline() {

}

void core::reset() {
    frame_start_clocks = bus->master_clock;
    frame_num = 0;
    // cycles_per_frame = 4*384*264 = 405504 (constant); scanline = 405504/264 = 1536
    // FP8: 1536 * 256 = 393216
    cycles_per_scanline_fp8 = (bus->clock.cycles_per_frame / 264) << 8;
    irq_ack = {true, true, false };
    irq_pending = {false, false, true };
    memset(bus->VRAM, 0xFF, sizeof(bus->VRAM));

    eval_IRQs<false>();
}

template<bool do_debug>
void core::timer_tick() {
    if (timer.counter) {
        timer.counter--;
        if (!timer.counter) {
            if (timer.reload_on_zero) {
                timer.counter = timer.reload;
            }
            if (irq_ack.timer && timer.irq_enable) {
                irq_ack.timer = false;
                irq_pending.timer = true;
                eval_IRQs<do_debug>();
            }
        }
    }
}

template<bool do_debug>
    static void sch_timer_tick(void *ptr, u64 key, u64 clock, u32 jitter) {
    auto *th = static_cast<core *>(ptr);
    th->timer_tick<do_debug>();
    th->bus->scheduler.only_add_abs((clock - jitter) + 4, 0, th, &sch_timer_tick<false>, &sch_timer_tick<true>, nullptr);
}

void core::schedule_first() {
    // Schedule out a 264-line frame...
    if (::dbg.do_debug) schedule_frame<true>(true);
    else schedule_frame<false>(true);

    bus->scheduler.only_add_abs(0, 0, this, &sch_timer_tick<false>, &sch_timer_tick<true>, nullptr);
}

template<bool do_debug>
static void sch_vblank(void *ptr, u64 key, u64 clock, u32 jitter) {
    auto *th = static_cast<core *>(ptr);
    th->vblank<do_debug>(key);
}

template<bool do_debug>
static void sch_line_draw(void *ptr, u64 key, u64 clock, u32 jitter) {
    auto *th = static_cast<core *>(ptr);
    th->draw_line<do_debug>(key);
}



template<bool do_debug>
static void sch_line_start(void *ptr, u64 key, u64 clock, u32 jitter) {
    auto *th = static_cast<core *>(ptr);
    th->line_start<do_debug>(key);
}

template<bool do_debug>
static void sch_new_frame(void *ptr, u64 key, u64 clock, u32 jitter) {
    auto *th = static_cast<core *>(ptr);
    th->new_frame<do_debug>();
}

template<bool do_debug>
void core::new_frame() {
    schedule_frame<do_debug>(false);
    frame_num++;
    frame_start_clocks = bus->master_clock;
    display->active_draw_buffer ^= 1;
    cur_output = static_cast<u32 *>(display->output[display->active_draw_buffer]);
}

constexpr u8 hshrink_table[16][16] = {
    { 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, // 0: 0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0 (15 pixel skipped, 1 remaining)
    { 4, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, // 1: 0,0,0,0,1,0,0,0,1,0,0,0,0,0,0,0 (14 pixels skipped...)
    { 4, 8, 12, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, // 2: 0,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0
    { 2, 4, 8, 12, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, // 3: 0,0,1,0,1,0,0,0,1,0,0,0,1,0,0,0
    { 2, 4, 8, 12, 14, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, // 4: 0,0,1,0,1,0,0,0,1,0,0,0,1,0,1,0
    { 2, 4, 6, 8, 12, 14, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, // 5: 0,0,1,0,1,0,1,0,1,0,0,0,1,0,1,0
    { 2, 4, 6, 8, 10, 12, 14, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, // 6: 0,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0
    { 0, 2, 4, 6, 8, 10, 12, 14, 0, 0, 0, 0, 0, 0, 0, 0 }, // 7: 1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0
    { 0, 2, 4, 6, 8, 9, 10, 12, 14, 0, 0, 0, 0, 0, 0, 0 }, // 8: 1,0,1,0,1,0,1,0,1,1,1,0,1,0,1,0
    { 0, 2, 3, 4, 6, 8, 9, 10, 12, 14, 0, 0, 0, 0, 0, 0 }, // 9: 1,0,1,1,1,0,1,0,1,1,1,0,1,0,1,0
    { 0, 2, 3, 4, 6, 8, 9, 10, 12, 14, 15, 0, 0, 0, 0, 0 }, // A: 1,0,1,1,1,0,1,0,1,1,1,0,1,0,1,1
    { 0, 2, 3, 4, 6, 7, 8, 9, 10, 12, 14, 15, 0, 0, 0, 0 }, // B: 1,0,1,1,1,0,1,1,1,1,1,0,1,0,1,1
    { 0, 2, 3, 4, 6, 7, 8, 9, 10, 12, 13, 14, 15, 0, 0, 0 }, // C: 1,0,1,1,1,0,1,1,1,1,1,0,1,1,1,1
    { 0, 1, 2, 3, 4, 6, 7, 8, 9, 10, 12, 13, 14, 15, 0, 0 }, // D: 1,1,1,1,1,0,1,1,1,1,1,0,1,1,1,1
    { 0, 1, 2, 3, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 0 }, // E: 1,1,1,1,1,0,1,1,1,1,1,1,1,1,1,1 (...1 pixel skipped)
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 } // F: F: 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1 (no pixels skipped, full size)
};


template<bool do_debug>
void core::draw_sprite(u32 num, u32 *linebuf) {
    // Our next step is to calculate, using vsize and wrap, which line of the sprite tiles to actually draw, from 0...vsize
    auto &s = spr[num];
    u16 y_in_sprite = s.y_in_sprite;
    if (y_in_sprite >= s.vsize) {
        if (s.wrap) y_in_sprite %= s.vsize; // Wrap around!
        else y_in_sprite = s.vsize - 1; // Repeat last line of not wrapping
    }
    if (y_in_sprite > 255) y_in_sprite = bus->ROMs.lo[(s.vscale << 8) | ((y_in_sprite & 0xFF) ^ 0xFF)] ^ 0x1FF;
    else y_in_sprite = bus->ROMs.lo[(s.vscale << 8) | (y_in_sprite & 0xFF)];

    u32 addr = s.sp_index << 6;
    u32 tile_y_offset = (y_in_sprite >> 3) & 0x3E;
    u32 tile_number = bus->read_VRAM(addr + tile_y_offset);
    u16 attr = bus->read_VRAM(addr + tile_y_offset + 1);

    bool hflip = getbits<0>(attr);
    bool vflip = getbits<1>(attr);
    u8 auto_a = getbits<2,3>(attr);
    u8 palette = getbits<8,15>(attr);
    setbits<16,19>(tile_number, getbits<4,7>(attr));
    if (vflip) y_in_sprite ^= (s.vsize - 1);

    if (!auto_animation.disable) {
        switch (auto_a) {
            case 0: break;
            case 1: setbits<0,1>(tile_number, getbits<0,1>(auto_animation.frame)); break;
            case 2:
            case 3: setbits<0,2>(tile_number, getbits<0, 2>(auto_animation.frame)); break;
        }
    }

    u32 pram_addr = bus->io.pal_base_offset + (palette << 4);
    u32 tile_addr = ((tile_number << 5) | (y_in_sprite & 15))<< 2;

    // Now, fetch 16 4-bit pixels
    u32 bp0 = (bus->cart.read_C(tile_addr + 0) << 8 | bus->cart.read_C(tile_addr + 64 + 0)) << 0;
    u32 bp1 = (bus->cart.read_C(tile_addr + 2) << 8 | bus->cart.read_C(tile_addr + 64 + 2)) << 1;
    u32 bp2 = (bus->cart.read_C(tile_addr + 1) << 8 | bus->cart.read_C(tile_addr + 64 + 1)) << 2;
    u32 bp3 = (bus->cart.read_C(tile_addr + 3) << 8 | bus->cart.read_C(tile_addr + 64 + 3)) << 3;

    // Now output the pixels...
    for (u32 tx = 0; tx < s.hsize; tx++) {
        u32 screenx = (tx + s.sp_x) & 511;
        if (screenx >= 320) continue;

        u32 shift = hshrink_table[s.hsize-1][tx];
        if (hflip) shift ^= 15;
        u32 color = (bp0 >> shift) & 1;
        color |= (bp1 >> shift) & 2;
        color |= (bp2 >> shift) & 4;
        color |= (bp3 >> shift) & 8;
        if (color) {
            linebuf[screenx] = (io.shadow << 16) | bus->PRAM[pram_addr + color];
        }
    }
}

template<bool do_debug>
void core::draw_line(i32 y) {
    // VRAM $8200-83FF has sprite positions
    sprs.clear();

    u32 *linebuf = cur_output + (y * 320);

    u32 backdrop = io.shadow << 16 | bus->PRAM[bus->io.pal_base_offset | 0xFFF];
    for (u32 i = 0; i < 320; i++) linebuf[i] = backdrop;

    i32 sy = 0;
    u16 vsize = 32 * 16;
    u16 vsize_tiles = 32;
    u16 vscale = 0xFF;
    u16 sx = 0;
    u16 hsize = 16;
    bool swrap = false;

    i32 s_index = -1;
    u32 num_sprites = 0;

    while (num_sprites < 96 && s_index < 380) {
        s_index++;

        u16 scb2 = bus->read_VRAM(0x8000 | s_index);
        u16 scb3 = bus->read_VRAM(0x8200 | s_index);
        bool sticky = getbit<6>(scb3);
        if (sticky) {
            sx = (sx + hsize) & 511;
        }
        else {
            sy = 512 - static_cast<i32>(getbits<7,15>(scb3)) - 16;
            sx = getbits<7, 15>(bus->read_VRAM(0x8400 | s_index)) & 511;
            vsize_tiles = getbits<0,5>(scb3);
            if (vsize_tiles >= 33) {
                swrap = true;
                vsize_tiles = 32;
            }
            else {
                swrap = false;
            }
            vsize = vsize_tiles * 16;
            vscale = getbits<0, 7>(scb2);
        }
        hsize = getbits<8,11>(scb2) + 1;

        if (vsize_tiles == 0) continue;
        if ((sx >= 320) && ((sx + 15) <= 511)) continue;
        u16 line_in_sprite = static_cast<u16>(y - sy) & 0x1FF;
        if (line_in_sprite >= vsize) continue;

        auto &s = spr[num_sprites++];
        s.sp_index = s_index;
        s.sp_x = sx;
        s.y_in_sprite = line_in_sprite;
        s.hsize = hsize;
        s.vsize = vsize;
        s.vscale = vscale;
        s.wrap = swrap;
    }

    if (num_sprites > 0) {
        // Docs say to do this reversed one, but docs are wrong like so many times with NeoGeo
        /*for (i32 i = num_sprites - 1; i >= 0; i--) {
            draw_sprite<do_debug>(i, linebuf);
        }*/
        for (u32 i = 0; i < num_sprites; i++) {
            draw_sprite<do_debug>(i, linebuf);
        }
    }
    if constexpr(do_debug) {
        memcpy(debug_output.get() + (y * 320), linebuf, 320 * sizeof(u32));
        bus->nb1.draw_line<true>(y, linebuf);
    }
    else {
        bus->nb1.draw_line<false>(y, linebuf);
    }

}


template<bool do_debug>
void core::line_start(u32 line_num) {
    raster_line_counter = line_num;
}

template<bool do_debug>
void core::eval_IRQs() {
    u32 level = 0;
    if (irq_pending.vblank) level = 1;
    if (irq_pending.timer) level = 2;
    if (irq_pending.reset) level = 3;
    bus->m68k.set_interrupt_level(level);
}

template<bool do_debug>
void core::handle_iack(void *ptr) {
    auto *th = static_cast<core *>(ptr);
    switch (th->bus->m68k.state.internal_interrupt_level) {
        case 1:
            th->irq_pending.vblank = false;
            break;
        case 2:
            th->irq_pending.timer = false;
            break;
        case 3:
            th->irq_pending.reset = false;
            break;
        default:
            return;
    }
    th->eval_IRQs<do_debug>();
}

template<bool do_debug>
void core::vblank(bool level) {
    if (level) {
        if (irq_ack.vblank) {
            irq_ack.vblank = false;
            irq_pending.vblank = true;
            eval_IRQs<do_debug>();
        }
        if (timer.reload_on_vblank) {
            timer.counter = timer.reload;
        }
        if (auto_animation.counter == 0) {
            auto_animation.counter = auto_animation.reload;
            auto_animation.frame = (auto_animation.frame + 1) & 7;
        }
        else auto_animation.counter--;
    }
}

template<bool do_debug>
void core::schedule_frame(bool is_first) {
    // 264 scanlines.
    // First 8 are vertical pulse
    frame_start_clocks = bus->master_clock;
    raster_line_counter = 0;
    if (is_first) frame_num = 0;
    u64 clk_start = bus->master_clock;
    if (!is_first) vblank<do_debug>(true);
    line_start<do_debug>(0);
    for (u32 line = 1; line < 264; line++) {
        u64 clk = clk_start + ((line * cycles_per_scanline_fp8) >> 8);
        if (line == 8) {
            bus->scheduler.only_add_abs(clk, 0, this, &sch_vblank<false>, &sch_vblank<true>, nullptr);
        }

        if (line > 0) bus->scheduler.only_add_abs_w_tag(clk, line, this, &sch_line_start<false>, &sch_line_start<true>, nullptr, 1);
        if ((line >= 16) && (line < 240)) bus->scheduler.only_add_abs(clk + (56 * PIX_DIV), line - 16, this, &sch_line_draw<false>, &sch_line_draw<true>, nullptr);
    }

    // Now schedule next frame
    bus->scheduler.only_add_abs_w_tag(clk_start + bus->clock.cycles_per_frame, 0, this, &sch_new_frame<false>, &sch_new_frame<true>, nullptr, 2);
}

}
