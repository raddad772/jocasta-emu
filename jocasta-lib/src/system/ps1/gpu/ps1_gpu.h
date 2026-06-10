//
// Created by . on 2/15/25.
//

#pragma once
#include "helpers/cvec.h"
#include "helpers/int.h"
#include "helpers/physical_io.h"
#include "rasterize_tri.h"
#include "helpers/minmax.h"
#include "helpers/scheduler.h"
namespace PS1 {
struct core;
}

namespace PS1::GPU {
struct core;

typedef void (core::*tri_draw_func)(RT_POINT2D *v0, RT_POINT2D *v1, RT_POINT2D *v2, u32 first_color);
typedef void (core::*rect_draw_func)(RT_POINT2D *v0, RT_POINT2D *v1, u32 color);
typedef void (core::*line_draw_func)(RT_POINT2D *v0, RT_POINT2D *v1, u32 first_color);

struct TEXTURE_SAMPLER;
typedef u16 (core::*texture_sample_func)(i32 u, i32 v);

// for textures, you *actually* do goraud or modulate by constant.
    // for tex, goraud+modulate = goraud
    //          goraud+raw = constant_flat (first provided color)
    //          flat+modulate = constant_flat (provided color)
    //          flat+raw = constant_flat (provided color)

    // for flat, goraud+modulate = use goraud
    //           goraud + raw = use goraud
    //           flat+modulate = use the provided color
    //           flat+raw = use the provided color

// so we need the first provided color, as well as the vertices and

enum SHADE_MODE {
    SM_FLAT = 0,
    SM_GORAUD = 1
};



struct TEXTURE_SAMPLER {
    void mk_new(u32 page_x_in, u32 page_y_in, u32 clut_addr_in, core *bus);
    u32 page_x{}, page_y{}, base_addr{}, clut_addr{};
    u8 *VRAM{};
    u32 semi_mode{};
    texture_sample_func sample{};
};


struct VERTEX2 {
    i32 x{}, y{};
    i32 u{}, v{};
    u32 r{}, g{}, b{};
};


struct core {
    explicit core(PS1::core *parent);
    void reset();
    u32 read(u32 addr, u8 sz);
    void write(u32 addr, u8 sz, u32 val);
    void write_gp0(u32 cmd);
    void write_gp1(u32 cmd);
    [[nodiscard]] u32 get_gpuread();
    [[nodiscard]] u32 get_gpustat();

    u32 TEXPAGE{};
    u32 out_hres{}, dotclock_divider{};
    u32 force_set_mask{};
    void cmd_end();

    struct {
        u32 GPUREAD{};
        u32 frame{};
        union {
            struct {
                u32 texture_page_x_base : 4; // 0-3
                u32 texture_page_y_base : 1; // 4
                u32 semi_transparency : 2; // 5-6
                u32 texture_page_colors : 2; // 7-8
                u32 dither : 1; // 9
                u32 drawing_to_display_area : 1; // 10
                u32 force_set_mask_bit : 1; // 11
                u32 preserve_masked_pixels : 1; // 12
                u32 interlace_field : 1; // 13
                u32 screen_flip_x : 1;// 14
                u32 texture_page_y_base_2 : 1; // 15
                // 16 bits...
                u32 hres2: 1; // 16
                u32 hres1: 2; // 17-18
                u32 vres : 1; // 19
                u32 video_mode_PAL : 1; // 20
                u32 display_area_24bit : 1; // 21
                u32 interlacing : 1; // 22
                u32 display_disabled : 1; // 23
                u32 irq1 : 1; // 24
                u32 dma_data_request_bit : 1; // 25
                u32 ready_recv_cmd : 1;
                u32 ready_vram_to_cpu : 1;
                u32 ready_recv_dma : 1;
                u32 dma_dir : 2;
                u32 interlaced_odd_frame : 1;
            };
            u32 u{};
        } GPUSTAT{};
    } io{};
    u8 VRAM[1024 * 1024]{};

    void (core::*current_ins)(){};
    void new_frame();
    u8 mmio_buffer[96]{};
    u32 IRQ_bit{};

    PS1::core *bus{};
    u32 ins_special{};

    struct {
        i32 texture_x_flip{};
        i32 texture_y_flip{};
    } rect{};

    u32 tx_win_x_mask{}, tx_win_y_mask{};
    u32 tx_win_x_offset{}, tx_win_y_offset{};
    i32 draw_area_top{}, draw_area_bottom{};
    i32 draw_area_left{}, draw_area_right{};
    i32 draw_x_offset{}, draw_y_offset{}; // applied to all vertices
    u32 display_vram_x_start{}, display_vram_y_start{};
    u32 display_horiz_start{}, display_horiz_end{};
    u32 display_line_start{}, display_line_end{};

    struct {
        u32 x1{}, y1{};
        u32 x2{}, y2{};
        u32 width{}, height{};
        u32 draw_height{}, draw_y2{};
        bool bits24{};
    } display_area;

    struct COLOR_SAMPLER {
        i32 r_start{}, g_start{}, b_start{};
        i32 r{}, g{}, b{};
        i32 r_end{}, g_end{}, b_end{};
    } color1{}, color2{}, color3{};

    RT_POINT2D V0{}, V1{}, V2{}, V3{}, V4{}, V5{}, T0{}, T1{}, T2{}, T3{}, T4{};

    struct VERTEX3u {
        u32 x{}, y{}, z{};
        i32 r{}, g{}, b{};
        float u{}, v{};
    } s0{}, s1{}, s2{}, s3{};

    VERTEX2 vert[4]{};

    struct {
        u32 x{}, y{}, width{}, height{}, img_x{}, img_y{}, line_ptr{};
    } load_buffer{};

    struct POLYGON {
        u32 shading{}, vertices{}, textured{}, transparent{};
        u32 raw_texture{}, rgb{}, num_cmds{}, clut{};
        u32 tx_page{};
        VERTEX2 vert[4]{};
    } polygon{};

    i32 gp0_transfer_remaining{};
    bool VRAM_to_CPU_in_progress{};

    void (core::*handle_gp0)(u32 cmd);

    u32 *cur_output{};
    cvec_ptr<physical_io_device> display_ptr{};
    JSM_DISPLAY *display{};

    struct {
        u8 CMD{};
        struct {
            u32 clut{};
            u16 uv{};
            u32 has{}, x{}, y{}, val{};
            u16 clut_sample{};
            u8 depth{};
            u32 first_color{};
            u32 clutaddr{};
        } smp{};
        struct {
            u8 cmd[1024 * 512];
            u32 clut[1024 * 512];
            u16 uv[1024 * 512];
            u32 texel[1024 * 512];
            u32 xy_depth[1024 * 512];
            u32 first_color[1024 * 512];
            u16 clut_sample[1024 * 512];
            u32 clutaddr[1024 * 512];
        } buf{};
    } dbg{};

    u32 CMD[128]{};
    struct {
        i32 cmd_arg_index{}, cmd_arg_num{};
        bool polyline_verts{false};

        u32 num_verts{};
        struct {
            u32 goraud{};
            u32 textured{};
            u32 semi_transparent{};
            u32 variable_size{};
            i32 width{}, height{};
            u32 color{};
        } draw{};

        struct {
            u32 goraud{};
            u32 textured{};
            u32 semi_transparent{};
            u32 modulated{};
        } parse{};

        tri_draw_func tri_func{};
        rect_draw_func rect_func{};
        line_draw_func line_func{};
    } parse{};


private:
    void recalc_display_area();
    void unready_recv_dma() { io.GPUSTAT.ready_recv_dma = 0; }
    void unready_vram_to_CPU() { io.GPUSTAT.ready_vram_to_cpu = 0; }
    void unready_all() { unready_cmd(); unready_recv_dma(); unready_vram_to_CPU(); }
    void ready_cmd() {
        io.GPUSTAT.ready_recv_cmd = 1;
        //printf("\nReady CMD GPUSTAT:%08x", io.GPUSTAT.u);
    }
    void ready_recv_dma() { io.GPUSTAT.ready_recv_dma = 1; }
    void ready_vram_to_CPU() { io.GPUSTAT.ready_vram_to_cpu = 1; }
    void ready_all() { ready_cmd(); ready_recv_dma(); ready_vram_to_CPU(); }

    void unready_cmd();
    void gp0_cmd(u32 cmd);

    void cmd02_quick_rect();
    void cmd80_vram_copy();
    void set_cmd_px(i32 y, i32 x);
    void cmdnop() {}
    void load_buffer_reset(u32 x, u32 y, u32 width, u32 height);
    void gp0_CPU_to_VRAM_continue(u32 cmd);
    void gp0_CPU_to_VRAM_start();
    void gp0_VRAM_to_CPU_start();
    void gp0_VRAM_to_CPU_continue();
    void gp0_cmd_unhandled();
    void parse_line(u32 cmd);
    void parse_polygon(u32 cmd);
    void parse_rectangle(u32 cmd);
    void do_draw_line();
    void do_draw_polyline();
    void do_draw_polygon();
    void do_draw_rectangle();

    void bresenham_opaque(RT_POINT2D *v1, RT_POINT2D *v2, u32 color);;
    void bresenham_semi(RT_POINT2D *v1, RT_POINT2D *v2, u32 color);
    void bresenham_shaded_opaque(RT_POINT2D *v1, RT_POINT2D *v2);

    void get_texture_sampler_from_texpage_and_palette(u32 texpage, u32 clut);

    void setpix_split(i32 y, i32 x, u32 r, u32 g, u32 b, u32 tex_mask);
    void semipix_split(i32 y, i32 x, u32 r, u32 g, u32 b, u32 tex_mask, bool force);
    void update_global_texpage(u32 texpage);
    inline u32 ditherP(RT_POINT2D &p)
    {
        i32 y = io.GPUSTAT.interlacing ? p.y >> 1 : p.y;
        //i32 y = p.y;
        return ((y & 3) << 2) | (p.x & 3);
    }


    static inline u32 BGR24to15(u32 c)
    {
        return (((c >> 19) & 0x1F) << 10) |
               (((c >> 11) & 0x1F) << 5) |
               ((c >> 3) & 0x1F);
    }

    inline void xy_from_cmd(RT_POINT2D &v, u32 cmd) {
        v.x = cmd & 0xFFFF;
        v.y = cmd >> 16;
        v.x = sign_extend<11>(v.x);
        v.y = sign_extend<11>(v.y);
        v.x += draw_x_offset;
        v.y += draw_y_offset;
    }

    tri_draw_func tri_draw_funcs[16];
    rect_draw_func rect_draw_funcs[8];
    line_draw_func line_draw_funcs[4];
    void create_tri_draw_funcs();
    u16 sample_tex_4bit(i32 u, i32 v);
    u16 sample_tex_8bit(i32 u, i32 v);
    u16 sample_tex_15bit(i32 u, i32 v);
    void uv_coords(i32 &u, i32 &v);

    TEXTURE_SAMPLER gts{};
#include "drawfuncs.h"
};


}
