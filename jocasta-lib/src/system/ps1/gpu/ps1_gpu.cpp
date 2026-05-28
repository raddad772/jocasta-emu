//
// Created by . on 2/15/25.
//
#include <cassert>

#include "pixel_helpers.h"
#include "../ps1_bus.h"
#include "ps1_gpu.h"

#include "helpers/multisize_memaccess.cpp"

//#define LOG_GP0
//#define DBG_GP0

namespace PS1 {
void core::setup_dotclock()
{
    if (gpu.io.GPUSTAT.hres2) {
        gpu.out_hres = 384; // max 384 pixels
        gpu.dotclock_divider = 7; // 6.9xxxx is correct
    }
    else {
        static constexpr u32 table[4] =  {256, 320, 512, 640};
        static constexpr u32 table_dotclock[4] = { 10, 8, 5, 4};
        gpu.out_hres = table[gpu.io.GPUSTAT.hres1];
        gpu.dotclock_divider = table_dotclock[gpu.io.GPUSTAT.hres1];;
    }
    u32 cycles_per_line = 3413; // NTSC. 3406 PAL

    clock.dot.horizontal_px = cycles_per_line / gpu.dotclock_divider;
    clock.dot.vertical_px = gpu.display_line_end - gpu.display_line_start;
    //printf("\n%dx%d vs. %d:", clock.dot.horizontal_px, clock.dot.vertical_px, gpu.out_hres);

    clock.dot.ratio.cpu_to_gpu = static_cast<float>(clock.timing.gpu.hz) / static_cast<float>(clock.timing.cpu.hz);
    clock.dot.ratio.cpu_to_dotclock = clock.dot.ratio.cpu_to_gpu / gpu.dotclock_divider;
}

void core::dotclock_change()
{
    clock.dot.start.value = dotclock();
    clock.dot.start.time = clock_current();
    setup_dotclock();
}


}

namespace PS1::GPU {

void TEXTURE_SAMPLER::mk_new(u32 page_x_in, u32 page_y_in, u32 clut_addr_in, core *bus)
{
    page_x = (page_x_in & 0x0F) << 6; // * 64
    page_y = (page_y_in & 1) * 256;
    base_addr = (page_y * 2048) + (page_x * 2);
    clut_addr = clut_addr_in;
    VRAM = bus->VRAM;
}

#define R_GPUSTAT 0
#define R_GPUPLAYING 1
#define R_GPUQUIT 2
#define R_GPUGP1 3
#define R_GPUREAD 4
#define R_LASTUSED 23

core::core(PS1::core *parent) : bus(parent)
{
    display_horiz_start = 0x200;
    display_horiz_end = 0xC00;
    display_line_start = 0x10;
    display_line_end = 0x100;
    handle_gp0 = &core::gp0_cmd;
    create_tri_draw_funcs();
}

void core::unready_cmd()
{
    //static u32 e = 0;
    //e++;
    //printf("\nUNREADY CMD %d", e);
    io.GPUSTAT.ready_recv_cmd = 0;
    //printf("\nUready CMD GPUSTAT:%08x", io.GPUSTAT.u);
}

void core::cmd02_quick_rect()
{
    u32 ysize = (CMD[2] >> 16) & 511;
    u32 xsize = (CMD[2]) & 1023;
    u32 BGR = BGR24to15(CMD[0] & 0xFFFFFF) & 0x7FFF;
    u32 start_y = (CMD[1] >> 16) & 511;
    u32 start_x = (CMD[1]) & 0x3F0;
    xsize = (xsize + 15) & 0xFFF0;
    //if (xsize != 0x400)
//        xsize--;
    if ((xsize == 0) || (ysize == 0)) return;
    //printf("\nCMD02 QUCICKRECT, ")

    u32 end_x = start_x + xsize;
    u32 end_y = start_y + ysize;
    //printf("\nX:%d  Y:%d  XS:%d  YS:%d", start_x, start_y, xsize, ysize);

    //if (LOG_DRAW_QUADS) console.log('QUICKRECT! COLOR', hex4(BGR), 'X Y', start_x, start_y, 'SZ X SZ Y', xsize, ysize);
    for (u32 y = start_y; y < end_y; y++) {
        for (u32 x = start_x; x < end_x; x++) {
            //if ((x >= draw_area_left) && (x < draw_area_right) && (y >= draw_area_top) && (y < draw_area_bottom)) {
                u32 ax = x & 1023;
                u32 ay = y & 511;
                u32 addr = (2048*ay)+(ax*2);
                cW16(VRAM, addr & 0xFFFFF, BGR);
                set_cmd_px(y, x);
            //}
        }
    }
}

static inline i32 mksigned11(u32 v)
{
    return sign_extend<11>(v);
}

u16 core::sample_tex_4bit(i32 u, i32 v)
{
    uv_coords(u, v);
    u32 addr = gts.base_addr;
    addr += v*2048; // 2048 bytes per line
    addr += u >> 1; // half of x, since we are 4-bit
    u32 d = gts.VRAM[addr & 0xFFFFF];
    if ((u & 1) == 0) d &= 0x0F;
    else d = (d & 0xF0) >> 4;

    dbg.smp.has = 0x80000000;
    dbg.smp.y = addr >> 11;
    dbg.smp.x = (addr >> 1) % 2048;
    dbg.smp.val = d;
    dbg.smp.depth = 4;
    addr = (gts.clut_addr + (d*2));
    u16 r = cR16(gts.VRAM, addr & 0xFFFFF);
    dbg.smp.clut_sample = r;
    return r;
}

u16 core::sample_tex_8bit(i32 u, i32 v)
{
    dbg.smp.has = 0x80000000;
    uv_coords(u, v);
    u32 addr = (gts.base_addr + (v<<11) + u);
    u32 d = cR8(gts.VRAM, addr);
    dbg.smp.has = 0x80000000;
    dbg.smp.y = addr >> 11;
    dbg.smp.x = (addr >> 1) % 1024;
    dbg.smp.val = d;
    dbg.smp.depth = 8;

    u16 r = cR16(gts.VRAM, (gts.clut_addr + (d*2)) & 0xFFFFF);
    dbg.smp.clut_sample = r;
    return r;
}

void core::uv_coords(i32 &u, i32 &v) {

    u &= 0xFF;
    v &= 0xFF;
    u = (u & ~tx_win_x_mask) | (tx_win_x_mask & tx_win_x_offset);
    v = (v & ~tx_win_y_mask) | (tx_win_y_mask & tx_win_y_offset);
    dbg.smp.uv = (v << 8) | u;
}

u16 core::sample_tex_15bit(i32 u, i32 v)
{
    uv_coords(u, v);
    u32 addr = gts.base_addr + (v<<11) + (u<<1);
    dbg.smp.has = 0x80000000;
    dbg.smp.has = 0x80000000;
    dbg.smp.y = addr >> 11;
    dbg.smp.x = (addr >> 1) % 1024;
    dbg.smp.depth = 15;
    u16 d = cR16(gts.VRAM, addr & 0xFFFFF);
    dbg.smp.val = d;
    dbg.smp.clut_sample = d;

    //return 0xFFFF;
    return d;
}

void core::get_texture_sampler_from_texpage_and_palette(u32 texpage, u32 clut)
{
    u32 clx = (clut & 0x3F) << 4;
    u32 cly = (clut >> 6) & 0x1FF;
    dbg.smp.clut = clut & 0xFFFF;

    u32 clut_addr = (cly * 2048) + (clx * 2);

    u32 tx_x = texpage & 15; // bits 0-3
    u32 tx_y = (texpage >> 4) & 1; // bit 4
    dbg.smp.clut |= (texpage << 16);
    dbg.smp.clutaddr = clut_addr;
    gts.mk_new(tx_x, tx_y, clut_addr, this);
    gts.semi_mode = (texpage >> 5) & 3; // bit 5-6
    u32 tdepth = (texpage >> 7) & 3; // bit 7-8
    switch(tdepth) {
        case 0:
            gts.sample = &core::sample_tex_4bit;
            break;
        case 1:
            gts.sample = &core::sample_tex_8bit;
            break;
        case 2:
            gts.sample = &core::sample_tex_15bit;
            break;
        case 3:
            default:
            NOGOHERE;
    }
}

void core::update_global_texpage(u32 texpage_in) {
    TEXPAGE = texpage_in;
    io.GPUSTAT.texture_page_x_base = (texpage_in & 15) << 3;
    io.GPUSTAT.texture_page_y_base = (texpage_in >> 4) & 1;
    io.GPUSTAT.semi_transparency = (texpage_in >> 5) & 3;
    io.GPUSTAT.texture_page_colors = (texpage_in >> 7) & 3;
}

void core::parse_polygon(u32 cmd) {
#define GBIT(n) ((cmd >> n) & 1)
    parse.parse.goraud = GBIT(28);
    parse.num_verts = GBIT(27) ? 4 : 3;
    parse.parse.textured = GBIT(26) ? 1 : 0;
    parse.parse.semi_transparent = GBIT(25) ? 1 : 0;
    parse.parse.modulated = GBIT(24) ? 0 : 1;
#undef GBIT

    parse.draw.semi_transparent = parse.parse.semi_transparent;
    parse.draw.goraud = parse.parse.goraud;
    parse.draw.textured = parse.parse.textured;
    // IF textured AND goraud, !modulate, goraud=false
    // IF textured AND goraud, modulate, goraud=true
    // IF textured AND !goraud, modulate, goraud=false
    // IF textured AND !goraud, !modulate, goraud=false

    // IF !textured, goraud=goraud
    if (parse.parse.textured) {
        parse.draw.goraud &= parse.parse.modulated;
    }

    i32 a = parse.num_verts + 1; // vertices for each, plus first cmd
    if (parse.parse.goraud) a += parse.num_verts - 1;
    if (parse.parse.textured) a += parse.num_verts;

    parse.cmd_arg_num = a;
    parse.tri_func = tri_draw_funcs[parse.draw.semi_transparent << 3 | parse.draw.goraud << 2 | parse.draw.textured << 1 | io.GPUSTAT.dither];

    current_ins = &core::do_draw_polygon;
}

void core::parse_rectangle(u32 cmd) {
#define GBIT(n) ((cmd >> n) & 1)
    parse.parse.goraud = 0;
    parse.parse.textured = GBIT(26) ? 1 : 0;
    parse.parse.semi_transparent = GBIT(25) ? 1 : 0;
    parse.parse.modulated = GBIT(24) ? 0 : 1;
#undef GBIT

    i32 a = 2;
    u32 ss = (cmd >> 27) & 3;
    parse.draw.variable_size = 0;
    switch (ss) {
        case 0b00:
            // variable size!
            a++;
            parse.draw.variable_size = 1;
            break;
        case 0b01: // 1x1
            parse.draw.width = parse.draw.height = 1;
            break;
        case 0b10: // 8x8
            parse.draw.width = parse.draw.height = 8;
            break;
        case 0b11: // 16x16
            parse.draw.width = parse.draw.height = 16;
            break;
    }
    if (parse.parse.textured) a++;
    parse.cmd_arg_num = a;
    parse.parse.modulated &= parse.parse.textured;
    parse.rect_func = rect_draw_funcs[parse.parse.semi_transparent << 2 | parse.parse.textured << 1 | parse.parse.modulated];

    current_ins = &core::do_draw_rectangle;
}

void core::parse_line(u32 cmd) {
#define GBIT(n) ((cmd >> n) & 1)
    parse.parse.goraud = GBIT(28);
    parse.polyline_verts = GBIT(27);
    parse.parse.semi_transparent = GBIT(25);
#undef GBIT
    if (parse.polyline_verts) {
        parse.num_verts = 1;
        parse.cmd_arg_index = 1;
        parse.cmd_arg_num = 1;
        current_ins = &core::do_draw_line;
    }
    else {
        parse.cmd_arg_num = 3 + parse.parse.goraud;
        current_ins = &core::do_draw_line;
    }
    parse.line_func = line_draw_funcs[parse.parse.semi_transparent << 1 | parse.parse.goraud];
}

void core::do_draw_line() {
    u32 color = CMD[0] & 0xFFFFFF;
    dbg.smp.first_color = color;
    xy_from_cmd(V0, CMD[1]);
    V0.color24_from_cmd(CMD[0]);
    u32 a = 2;
    if (parse.parse.goraud)
        V1.color24_from_cmd(CMD[a++]);
    xy_from_cmd(V1, CMD[a]);

    (this->*parse.line_func)(&V0, &V1, color);
}

void core::do_draw_polygon() {
    // TODO: do it right! lol
    i32 idx = 0;
    u32 color = CMD[0] & 0xFFFFFF;
    dbg.smp.first_color = color;
    RT_POINT2D*Vs[4] = {&V0, &V1, &V2, &V3};
    u32 texpage = 0, clut = 0;
    for (u32 i = 0; i < parse.num_verts; i++) {
        auto *v = Vs[i];
        if (i == 0 || parse.parse.goraud) {
            v->color24_from_cmd(CMD[idx++]);
        }
        xy_from_cmd(*v, CMD[idx++]);
        if (parse.parse.textured) {
            if (i == 0)
                clut = CMD[idx] >> 16;
            if (i == 1)
                texpage = CMD[idx] >> 16;
            v->uv_from_cmd(CMD[idx++]);
        }
    }
    assert(idx==parse.cmd_arg_num);
    if (parse.parse.textured) {
        update_global_texpage(texpage);
        get_texture_sampler_from_texpage_and_palette(texpage, clut);
        if (!parse.parse.modulated) color = 0x808080;
    }
    (this->*parse.tri_func)(&V0, &V1, &V2, color);
    if (parse.num_verts == 4) {
        (this->*parse.tri_func)(&V1, &V2, &V3, color);
    }
    //dbg_break("YO", 0);
    //printf("\nCMD %02x", dbg.CMD);
}


void core::cmd80_vram_copy()
{
    u32 src_y = CMD[1] >> 16;
    u32 src_x = CMD[1] & 0xFFFF;
    u32 dest_y = CMD[2] >> 16;
    u32 dest_x = CMD[2] & 0xFFFF;
    u32 height = CMD[3] >> 16;
    u32 width = CMD[3] & 0xFFFF;
#ifdef LOG_GP0
    printf("\nGP0 cmd80 copy from %d,%d to %d,%d size:%d,%d", src_x, src_y, dest_x,dest_y, width, height);
#endif

    for (u32 row = 0; row < height; row++) {
        for (u32 col = 0; col < width; col++) {
            u32 vram_x = (src_x + col) & 1023;
            u32 vram_y = (src_y + row) & 511;
            u32 vram_idx = (vram_y * 1024 + vram_x) * 2;
            u16 v = cR16(VRAM, vram_idx & 0xFFFFF);

            vram_x = (dest_x + col) & 1023;
            vram_y = (dest_y + row) & 511;
            vram_idx = (vram_y * 1024 + vram_x) * 2;

            if (io.GPUSTAT.preserve_masked_pixels) {
                u16 t = cR16(VRAM, vram_idx & 0xFFFFF);
                if (t & 0x8000) continue;
            }
            cW16(VRAM, vram_idx & 0xFFFFF, v | force_set_mask);
            set_cmd_px(vram_y, vram_x);
        }
    }

    //printf("\nCOPY VRAM %d,%d to %d,%d size:%d,%d");
}

void core::do_draw_rectangle() {
    u32 color = CMD[0] & 0xFFFFFF;
    dbg.smp.first_color = color;
    xy_from_cmd(V0, CMD[1]);
    u32 a = 2;
    i32 clut;
    if (parse.parse.textured) {
        V0.uv_from_cmd(CMD[a]);
        clut = CMD[a] >> 16;
        get_texture_sampler_from_texpage_and_palette(TEXPAGE, clut);

        a++;
    }
    if (parse.draw.variable_size) {
        parse.draw.width = CMD[a] & 0xFFFF;
        parse.draw.height = CMD[a] >> 16;
    }

    V1.x = V0.x + parse.draw.width;
    V1.y = V0.y + parse.draw.height;

    (this->*parse.rect_func)(&V0, &V1, color);
}

void core::load_buffer_reset(u32 x, u32 y, u32 width, u32 height)
{
    load_buffer.x = x;
    load_buffer.y = y;
    load_buffer.width = width;
    load_buffer.height = height;
    load_buffer.line_ptr = (y * 2048) + x;
    load_buffer.img_x = load_buffer.img_y = 0;
}

void core::gp0_VRAM_to_CPU_continue() {
    u32 px = 0;
    for (u32 i = 0; i < 2; i++) {
        px >>= 16;
        u32 y = load_buffer.y+load_buffer.img_y;
        u32 x = load_buffer.x+load_buffer.img_x;
        u32 ay = y & 511;
        u32 ax = x & 1023;
        u32 addr = (2048*ay)+(ax*2);
        u16 v = cR16(VRAM, addr);
        px |= (static_cast<u32>(v) << 16);
        load_buffer.img_x++;
        if ((load_buffer.img_x) >= (load_buffer.width)) {
            load_buffer.img_x = 0;
            load_buffer.img_y++;
        }
    }
    io.GPUREAD = px;
    gp0_transfer_remaining--;
    if (gp0_transfer_remaining <= 0) {
        //printf("\nVRAM TO CPU: 0 TRANSFER REMAINING!");
        current_ins = nullptr;
        handle_gp0 = &core::gp0_cmd;
        VRAM_to_CPU_in_progress = false;
        ready_cmd();
    }
}

void core::gp0_CPU_to_VRAM_continue(u32 cmd)
{
    for (u32 i = 0; i < 2; i++) {
        u32 px = cmd & 0xFFFF;
        cmd >>= 16;
        u32 y = load_buffer.y+load_buffer.img_y;
        u32 x = load_buffer.x+load_buffer.img_x;
        u32 ax = (x & 1023);
        u32 ay = (y & 511);;
        u32 addr = ((2048*ay)+(ax*2)) & 0xFFFFF;
        if (io.GPUSTAT.preserve_masked_pixels) {
            u16 v = cR16(VRAM, addr);
            if (v & 0x8000) continue;
        }
        cW16(VRAM, addr, px | force_set_mask);
        set_cmd_px(ay, ax);

        load_buffer.img_x++;
        if ((load_buffer.img_x) >= (load_buffer.width)) {
            load_buffer.img_x = 0;
            load_buffer.img_y++;
        }
    }
    gp0_transfer_remaining--;
    if (gp0_transfer_remaining <= 0) {
        //printf("\nCPU TO VRAM: 0 TRANSFER REMAINING!");
        current_ins = nullptr;
        handle_gp0 = &core::gp0_cmd;
        ready_cmd();
        //unready_recv_dma();
    }
}

void core::gp0_VRAM_to_CPU_start() {
    unready_cmd();
    ready_vram_to_CPU();
    u32 x = CMD[1] & 1023;
    u32 y = (CMD[1] >> 16) & 511;
    u32 width = (((CMD[2] & 0xFFFF) - 1) & 0x3FF) + 1;
    u32 height = (((CMD[2] >> 16) - 1) & 0x1FF) + 1;
    //printf("\nVRAM TO CPU! %d,%d %dx%d", x, y, width, height);

    // Get imgsize, round it
    u32 imgsize = ((width * height) + 1) & 0xFFFFFFFE;
    //printf("\nVRAM->CPU IMGSIZE:%d  X:%d Y:%d WIDTH:%d HEIGHT:%d", imgsize, x, y, width, height);
    gp0_transfer_remaining = imgsize/2;
    if (gp0_transfer_remaining > 0) {
        VRAM_to_CPU_in_progress = true;
        load_buffer_reset(x, y, width, height);
        gp0_VRAM_to_CPU_continue();
    } else {
        printf("\nBad size image save: 0?");
        current_ins = nullptr;
        ready_cmd();
    }

}

void core::gp0_CPU_to_VRAM_start()
{
    unready_cmd();
    ready_recv_dma();
    // Top-left corner in VRAM
    u32 x = CMD[1] & 1023;
    u32 y = (CMD[1] >> 16) & 511;

    // Resolution
    u32 width = (((CMD[2] & 0xFFFF) - 1) & 0x3FF) + 1;
    u32 height = (((CMD[2] >> 16) - 1) & 0x1FF) + 1;

    // Get imgsize, round it
    u32 imgsize = ((width * height) + 1) & 0xFFFFFFFE;
    //printf("\nCPU to VRAM! %d,%d %dx%d", x, y, width, height);;
    //printf("\nNEW CPU-VRAM TRANSFER x,y:%d,%d width,height:%d,%d  PC:%08x  CMD0:%08x CMD1:%08x CMD2:%08x", x, y, width, height, bus->cpu.regs.PC, CMD[0], CMD[1], CMD[2]);

    gp0_transfer_remaining = imgsize/2;
#ifdef LOG_GP0
    printf("\nTRANSFER IMGSIZE %d X:%d Y:%d WIDTH:%d HEIGHT:%d CMD:%08x", imgsize, x, y, width, height, CMD[0]);
#endif
    if (gp0_transfer_remaining > 0) {
        load_buffer_reset(x, y, width, height);
        handle_gp0 = &core::gp0_CPU_to_VRAM_continue;
    } else {
        printf("\nBad size image load: 0?");
        current_ins = nullptr;
    }
}

void core::gp0_cmd_unhandled()
{

}

void core::new_frame() {
    io.frame ^= 1;
    io.GPUSTAT.interlaced_odd_frame ^= 1;
}

void core::write_gp0(u32 cmd) {
    (this->*handle_gp0)(cmd);
}

void core::gp0_cmd(u32 cmd) {
    if (VRAM_to_CPU_in_progress) {
        printf("\nWARN CMD DURING VRAM TO CPU!?");
    }
    static bool bad = false;
    if (bad) { printf("\nVAL %08x", cmd); }
    if (parse.polyline_verts) {
        // The vertex list is terminated by the bits 12-15 and 28-31 equaling 0x5, or (word & 0xF000F000) == 0x50005000.
        CMD[parse.cmd_arg_index++] = cmd;
        if ((cmd & 0xF000F000) == 0x50005000) {
            parse.polyline_verts = false;
            do_draw_polyline();
            parse.cmd_arg_index = 0;
            current_ins = nullptr;
            return;
        }
        if (parse.cmd_arg_index > 122) {
            printf("\nWARN HUGE POLYLINE!");
        }
    }
    // Check if we have an instruction..
    if (current_ins) {
        CMD[parse.cmd_arg_index++] = cmd;
        assert(parse.cmd_arg_index<128);
        if (parse.cmd_arg_index == parse.cmd_arg_num) {
            // Execute instruction!
            (this->*current_ins)();
            current_ins = nullptr;
            parse.cmd_arg_index = 0;
        }
    } else {
        CMD[0] = cmd;
        dbg.smp.clut = 0;
        dbg.smp.has = 0;
        parse.cmd_arg_index = 1;
        parse.cmd_arg_num = 1;
        parse.polyline_verts = false;
        u32 cmdr = cmd >> 29;
        dbg.CMD = cmd >> 24;
        switch (cmdr) {
            case 0b001: // polygon
                parse_polygon(cmd);
                return;
            case 0b010: // line
                parse_line(cmd);
                return;
            case 0b011: // rectangle
                parse_rectangle(cmd);
                return;
            case 0b100: // VRAM-to-VRAM
                current_ins = &core::cmd80_vram_copy;
                parse.cmd_arg_num = 4;
                return;
            case 0b110: // VRAM-to-CPU
                current_ins = &core::gp0_VRAM_to_CPU_start;
                parse.cmd_arg_num = 3;
                return;
            case 0b101: // CPU-to-VRAM
                current_ins = &core::gp0_CPU_to_VRAM_start;
                parse.cmd_arg_num = 3;
                return;
            default:
                break;
        }
        cmdr = cmd >> 24;
#ifdef LOG_GP0
        printf("\n(GPU) CMD %02x", cmdr);
#endif
        if ((cmdr >= 0x03) && (cmdr < 0x1F)) cmdr = 0;
        dbg.CMD = cmdr;
        switch(cmdr) {
            case 0x00: // NOP
            case 0x01: // Clear cache (not implemented)
            case 0x03:
            case 0xE0:
            case 0xE7:
            case 0xE8:
            case 0xE9:
            case 0xEA:
            case 0xEB:
            case 0xEC:
            case 0xED:
            case 0xEE:
            case 0xEF:
                break;
            case 0x02: // Quick Rectangle
                //console.log('Quick rectangle!');
                current_ins = &core::cmd02_quick_rect;
                parse.cmd_arg_num = 3;
                break;
                //case 0x21: // ??
                //    console.log('NOT IMPLEMENT 0x21');
                //    break;
            case 0x1F: // Set IRQ1
                printf("\nIRQ1 trigger");
                io.GPUSTAT.irq1 = 1;
                bus->set_irq(IRQ_GPU, 1);
                break;
            case 0xE1: // GP0 Draw Mode
#ifdef DBG_GP0
                printf("\nGP0 E1 set draw mode");
#endif
                //printf("\nSET DRAW MODE %08x", cmd);
                io.GPUSTAT.texture_page_x_base = cmd & 15;
                io.GPUSTAT.texture_page_y_base = (cmd >> 4) & 1;
                io.GPUSTAT.semi_transparency = (cmd >> 5) & 3;
                io.GPUSTAT.texture_page_colors = (cmd >> 7) & 3;
                io.GPUSTAT.dither = (cmd >> 9) & 1;
                io.GPUSTAT.drawing_to_display_area = (cmd >> 10) & 1;
                io.GPUSTAT.texture_page_y_base_2 = (cmd >> 1) & 1;
                TEXPAGE = cmd & 0xFFFF;
                rect.texture_x_flip = (cmd >> 12) & 1;
                rect.texture_y_flip = (cmd >> 13) & 1;
                break;
            case 0xE2: // Texture window
#ifdef DBG_GP0
                printf("\nGP0 E2 set draw mode");
#endif
                tx_win_x_mask = (cmd & 0x1F) << 3;
                tx_win_y_mask = ((cmd >> 5) & 0x1F) << 3;
                tx_win_x_offset = ((cmd >> 10) & 0x1F) << 3;
                tx_win_y_offset = ((cmd >> 15) & 0x1F) << 3;
                break;
            case 0xE3: // Set draw area upper-left corner
                draw_area_top = (cmd >> 10) & 0x1FF;
                draw_area_left = cmd & 0x3FF;
#ifdef DBG_GP0
                printf("\nGP0 E3 set draw area UL corner %d, %d", draw_area_top, draw_area_left);
#endif
                break;
            case 0xE4: // Draw area lower-right corner
                draw_area_bottom = ((cmd >> 10) & 0x1FF) + 1;
                draw_area_right = (cmd & 0x3FF) + 1;
#ifdef DBG_GP0
                printf("\nGP0 E4 set draw area LR corner %d, %d", draw_area_right, draw_area_bottom);
#endif
                break;
            case 0xE5: // Drawing offset
                draw_x_offset = mksigned11(cmd & 0x7FF);
                draw_y_offset = mksigned11((cmd >> 11) & 0x7FF);
#ifdef DBG_GP0
                printf("\nGP0 E5 set drawing offset %d, %d", draw_x_offset, draw_y_offset);
#endif
                break;
            case 0xE6: // Set Mask Bit setting
#ifdef DBG_GP0
                printf("\nGP0 E6 set bit mask");
#endif
                io.GPUSTAT.force_set_mask_bit = cmd & 1;
                force_set_mask = (cmd & 1) << 15;
                io.GPUSTAT.preserve_masked_pixels = (cmd >> 1) & 1;
                break;
            default:
                printf("\nUnknown GP0 command %08x", cmd);
                bad = true;
                dbg_break("BAD GP0 CMD", 0);
                break;
        }
    }
}



void core::reset()
{
    ready_all();
    bus->dotclock_change();
}

void core::do_draw_polyline() {
    u32 first_color = CMD[0] & 0xFFFFFF;
    // Go through all the args
    u32 num_points = parse.parse.goraud ? (parse.cmd_arg_index >> 1) : (parse.cmd_arg_index - 1);
    i32 idx = 0;
    V0.color24_from_cmd(CMD[idx++]);
    dbg.smp.first_color = CMD[0] & 0xFFFFFF;
    xy_from_cmd(V0, CMD[idx++]);
    while (idx < parse.cmd_arg_index) {
        if (parse.parse.goraud) V1.color24_from_cmd(CMD[idx++]);
        xy_from_cmd(V1, CMD[idx++]);
        (this->*parse.line_func)(&V0, &V1, first_color);
        V0.copyxycolor(V1);
    }
}

void core::recalc_display_area() {
// Get X1,Y1 - X2, Y2 of display area
    display_area.x1 = display_vram_x_start;
    display_area.y1 = display_vram_y_start;
    u32 full_width = (((display_horiz_end - display_horiz_start) / dotclock_divider) + 2) & ~3;
    u32 full_height = display_line_end - display_line_start;
    display_area.width = full_width;
    display_area.height = full_height;
    display_area.x2 = display_area.x1 + display_area.width;
    display_area.y2 = display_area.y1 + display_area.height;
    if (display_area.x2 > 1023) display_area.x2 = 1023;
    if (display_area.y2 > 511) display_area.y2 = 511;
    if (display_area.x1 > display_area.x2) display_area.x1 = display_area.x2;
    if (display_area.y1 > display_area.y2) display_area.y1 = display_area.y2;
    display_area.width = display_area.x2 - display_area.x1;
    display_area.height = display_area.y2 - display_area.y1;
    display_area.bits24 = io.GPUSTAT.display_area_24bit;

    // OK, now we need to consider interlacing!

    display_area.draw_height = display_area.height * (io.GPUSTAT.interlacing + 1);
    display_area.draw_y2 = display_area.y1 + display_area.draw_height;
    if (display_area.draw_y2 > 511) display_area.draw_y2 = 511;
    display_area.draw_height = display_area.draw_y2 - display_area.y1;

    //printf("\nDISPLAY AREA %d,%d to %d,%d (interlace:%d)", display_area.x1, display_area.y1, display_area.x2, display_area.y2, io.GPUSTAT.interlacing);
}

void core::write_gp1(u32 cmd)
{
    u32 cmdb = cmd >> 24;
    if ((cmdb >= 0x10) && (cmdb <= 0x1F)) cmdb = 0x10;
    switch(cmdb) {
        case 0:
            //printf("\nGP1 soft reset %08x", bus->cpu.gte.flags);
            // Soft reset
            io.GPUSTAT.texture_page_x_base = 0;
            io.GPUSTAT.texture_page_y_base = 0;
            io.GPUSTAT.semi_transparency = 0;
            io.GPUSTAT.texture_page_colors = 0;
            tx_win_x_mask = tx_win_y_mask = 0;
            tx_win_x_offset = tx_win_y_offset = 0;
            io.GPUSTAT.dither = 0;
            io.GPUSTAT.drawing_to_display_area = 0;
            io.GPUSTAT.texture_page_y_base_2 = 0;
            rect.texture_x_flip = rect.texture_y_flip = 0;
            draw_area_bottom = draw_area_right = draw_area_left = draw_area_top = 0;
            draw_x_offset = draw_y_offset = 0;
            io.GPUSTAT.force_set_mask_bit = 0;
            force_set_mask = 0;
            io.GPUSTAT.preserve_masked_pixels = 0;
            io.GPUSTAT.dma_dir = 0;
            io.GPUSTAT.display_disabled = 1;
            display_vram_x_start = display_vram_y_start = 0;
            io.GPUSTAT.hres1 = 0;
            io.GPUSTAT.hres2 = 0;
            io.GPUSTAT.video_mode_PAL = 0;
            io.GPUSTAT.interlacing = 1;
            display_horiz_start = 0x200;
            display_horiz_end = 0xC00;
            display_line_start = 0x10;
            display_line_end = 0x100;
            io.GPUSTAT.display_area_24bit = 0;
            handle_gp0 = &core::gp0_cmd;
            current_ins = nullptr;
            parse.cmd_arg_index = 0;
            //clear_FIFO();
            ready_cmd();
            ready_recv_dma();
            ready_vram_to_CPU();
            recalc_display_area();
            // TODO: remember to flush GPU texture cache
            break;
        case 0x01: // reset CMD FIFO
            //console.log('RESET CMD FIFO NOT IMPLEMENT');
            break;
        case 0x02:
            io.GPUSTAT.irq1 = 0;
            bus->set_irq(IRQ_GPU, 0);
            break;
        case 0x03: // DISPLAY DISABLE
            //TODO: do this
            break;
        case 0x04: // DMA direction
            io.GPUSTAT.dma_dir = cmd & 3;
            break;
        case 0x05: // VRAM start
            //console.log('GP1 VRAM start');
            display_vram_x_start = cmd & 0x3FF;
            display_vram_y_start = (cmd >> 10) & 0x1FF;
            recalc_display_area();
            break;
        case 0x06: // Display horizontal range, in output coordinates
            display_horiz_start = cmd & 0xFFF;
            display_horiz_end = (cmd >> 12) & 0xFFF;
            bus->dotclock_change();
            recalc_display_area();
            break;
        case 0x07: // Display vertical range, in output coordinates
            display_line_start = cmd & 0x3FF;
            display_line_end = (cmd >> 10) & 0x3FF;
            recalc_display_area();
            break;
        case 0x08: {// Display mode
            //console.log('GP1 display mode');
            io.GPUSTAT.hres1 = cmd & 3;
            io.GPUSTAT.hres2 = ((cmd >> 6) & 1);

            io.GPUSTAT.vres = (cmd >> 2) & 1;
            io.GPUSTAT.video_mode_PAL = (cmd >> 3) & 1;
            io.GPUSTAT.display_area_24bit = (cmd >> 4) & 1;
            io.GPUSTAT.interlacing = (cmd >> 5) & 1;
            if ((cmd & 0x80) != 0) {
                printf("\nUnsupported display mode!");
            }
            bus->dotclock_change();
            recalc_display_area();
            break; }
        case 0x10: {
            // Read internal register
            u32 reg = cmd & 0xFFFFFF;
            reg &= 7;
            u32 r = 0;
            switch (reg) {
                case 0x00:
                case 0x01:
                case 0x06:
                case 0x07:
                    break;
                case 0x02:
                    r = (tx_win_x_mask >> 3);
                    r |= (tx_win_y_mask << 2);
                    r |= (tx_win_x_offset << 7);
                    r |= (tx_win_y_offset << 12);
                    io.GPUREAD = r;
                    break;
                case 0x03:
                    r = draw_area_top;
                    r |= (draw_area_left << 10);
                    io.GPUREAD = r;
                    break;
                case 0x04:
                    r = draw_area_bottom;
                    r |= (draw_area_right << 10);
                    io.GPUREAD = r;
                    break;
                case 0x05:
                    r = (draw_x_offset & 0x7FF);
                    r |= ((draw_y_offset & 0x7FF) << 11);
                    io.GPUREAD = r;
                    break;
                default: break;
            }
            break; }
        default:
            printf("\nUnknown GP1 command %08x", cmd);
            break;
    }
}

u32 core::read(u32 addr, u8 sz) {
    if (sz == 1) {
        return read(addr & ~3, 4) >> (8 * (addr & 3)) & 0xFF;
    }
    if (sz == 2) {
        return read(addr & ~3, 4) >> (8 * (addr & 3)) & 0xFFFF;
    }
    switch (addr) {
        case 0x1F801810: // GP0/GPUREAD
            return get_gpuread();
        case 0x1F801814: // GPUSTAT Read GPU Status Register
            return get_gpustat();
        default:
            NOGOHERE;
    }

}

void core::write(u32 addr, u8 sz, u32 val) {
    if (sz < 4) {
        write(addr & ~3, 4, val << (8 * (addr & 3)));
        return;
    }
    switch (addr) {
        case 0x1F801810: // GP0 Send GP0 Commands/Packets (Rendering and VRAM Access)
            write_gp0(val);
            return;
        case 0x1F801814: // GP1
            write_gp1(val);
            return;
        default:
            NOGOHERE;
    }
}

u32 core::get_gpuread()
{
    u32 v = io.GPUREAD;
    if (VRAM_to_CPU_in_progress) gp0_VRAM_to_CPU_continue();
    return v;
}

u32 core::get_gpustat()
{
    //printf("\nDMA_dir:%d", DMA_dir);
    switch(io.GPUSTAT.dma_dir) {
        case 0:
            io.GPUSTAT.dma_data_request_bit = 0;
            break;
        case 1:
            io.GPUSTAT.dma_data_request_bit = 1; // 0=full FIFO, 1 = not full
            break;
        case 2:
            io.GPUSTAT.dma_data_request_bit = io.GPUSTAT.ready_recv_dma;
            break;
        case 3:
            io.GPUSTAT.dma_data_request_bit = io.GPUSTAT.ready_vram_to_cpu;
            break;
        default:
            NOGOHERE;
    }
    return io.GPUSTAT.u;
}

void core::create_tri_draw_funcs() {
    // semi_transparent << 3 | goraud << 2 | textured << 1 | dither
    tri_draw_funcs[0]  = &core::draw_tri<false, false, false, false>; // 0000
    tri_draw_funcs[1]  = &core::draw_tri<false, false, false, true>; // 0001
    tri_draw_funcs[2]  = &core::draw_tri<false, false, true, false>; // 0010
    tri_draw_funcs[3]  = &core::draw_tri<false, false, true, true>; // 0011
    tri_draw_funcs[4]  = &core::draw_tri<false, true, false, false>; // 0100
    tri_draw_funcs[5]  = &core::draw_tri<false, true, false, true>; // 0101
    tri_draw_funcs[6]  = &core::draw_tri<false, true, true, false>; // 0110
    tri_draw_funcs[7]  = &core::draw_tri<false, true, true, true>; // 0111
    tri_draw_funcs[8]  = &core::draw_tri<true, false, false, false>; // 1000
    tri_draw_funcs[9]  = &core::draw_tri<true, false, false, true>; // 1001
    tri_draw_funcs[10] = &core::draw_tri<true, false, true, false>; // 1010
    tri_draw_funcs[11] = &core::draw_tri<true, false, true, true>; // 1011
    tri_draw_funcs[12] = &core::draw_tri<true, true, false, false>; // 1100
    tri_draw_funcs[13] = &core::draw_tri<true, true, false, true>; // 1101
    tri_draw_funcs[14] = &core::draw_tri<true, true, true, false>; // 1110
    tri_draw_funcs[15] = &core::draw_tri<true, true, true, true>; // 1111

    // semi-transparent << 2 | textured << 1 | modulated
    rect_draw_funcs[0] = &core::draw_rect<false, false, false>; // 000
    rect_draw_funcs[1] = &core::draw_rect<false, false, true>; // 001
    rect_draw_funcs[2] = &core::draw_rect<false, true, false>; // 010
    rect_draw_funcs[3] = &core::draw_rect<false, true, true>; // 011
    rect_draw_funcs[4] = &core::draw_rect<true, false, false>; // 100
    rect_draw_funcs[5] = &core::draw_rect<true, false, true>; // 101
    rect_draw_funcs[6] = &core::draw_rect<true, true, false>; // 110
    rect_draw_funcs[7] = &core::draw_rect<true, true, true>; // 111

    // semi_transparent << 1 | goraud
    line_draw_funcs[0] = &core::draw_line<false, false>;
    line_draw_funcs[1] = &core::draw_line<false, true>;
    line_draw_funcs[2] = &core::draw_line<true, false>;
    line_draw_funcs[3] = &core::draw_line<true, true>;
}

}
