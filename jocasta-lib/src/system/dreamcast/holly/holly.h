#pragma once

#include "helpers/int.h"
#include "helpers/scheduler.h"

#include "../dreamcast.h"

namespace DREAMCAST {
    struct core;
}

namespace DREAMCAST::HOLLY {


enum interrupthi {
    holly_nrm = 0,
    holly_ext = 0x100,
    holly_err = 0x200
};

enum interruptmasks {
    hirq_render_done_video = holly_nrm | 0,
    hirq_render_done_isp = holly_nrm | 1,
    hirq_render_done_tsp = holly_nrm | 2,
    hirq_vblank_in = holly_nrm | 3,
    hirq_vblank_out = holly_nrm | 4,
    hirq_hblank_in = holly_nrm | 5,

    hirq_YUV_DMA = holly_nrm | 6,
    hirq_opaque_list = holly_nrm | 7,
    hirq_opaque_modifier_list = holly_nrm | 8,
    hirq_translucent_list = holly_nrm | 9,

    hirq_translucent_modifier_list = holly_nrm | 10,
    hirq_maple_dma = holly_nrm | 12,
    hirq_maple_vblank_over = holly_nrm | 13,

    hirq_SPU_DMA = holly_nrm | 15,			//bit 15 = End of DMA interrupt : AICA-DMA
    hirq_EXT_DMA1 = holly_nrm | 16,		//bit 16 = End of DMA interrupt : Ext-DMA1(External 1)
    hirq_EXT_DMA2 = holly_nrm | 17,		//bit 17 = End of DMA interrupt : Ext-DMA2(External 2)
    hirq_DEV_DMA = holly_nrm | 18,			//bit 18 = End of DMA interrupt : Dev-DMA(Development tool DMA)
    hirq_ch2_dma = holly_nrm | 19,
    hirq_sort_dma = holly_nrm | 20,
    hirq_punchthru = holly_nrm | 21,

    hirq_gdrom_cmd = holly_ext | 0,
    hirq_aica = holly_ext | 1,

    hirq_PRIM_NOMEM = holly_err | 0x02,	//bit 2 = TA : ISP/TSP Parameter Overflow
    hirq_MATR_NOMEM = holly_err | 0x03,	//bit 3 = TA : Object List Pointer Overflow
    hirq_ILLEGAL_PARAM = holly_err | 0x04 //bit 4 = TA : Illegal Parameter


};

static constexpr char HIRQ_NRM_NAMES[22][50] = {
    "00 Video Done", "01 ISP Done", "02 TSP Done", "03 VBlank In", "04 VBlank Out",
    "05 HBlank In", "06 YUV DMA complete", "07 Opaque List Complete", "08 Opaque Modifier List Complete",
    "09 Translucent List Complete", "10 Translucent Modifier List Complete", "11 UNKNOWN",
    "12 Maple", "13 Maple VBlank Over", "14 UKN", "15 SPU DMA Complete",
    "16 Ext-DMA1 Complete", "17 Ext-DMA2 Complete", "18 Dev DMA complete", "19 TA FIFO/ch2 DMA complete",
    "20 Sort DMA Complete", "21 Punch-Through List Complete"
};

static constexpr char HIRQ_EXT_NAMES[22][50] = {
    "00 GDROM Cmd", "01 AICA", "02 Unknown", "03 Unknown",
    "04 Unknown", "05 Unknown", "06 Unknown", "07 Unknown",
    "08 Unknown", "09 Unknown", "10 Unknown", "11 Unknown",
    "12 Unknown", "13 Unknown", "14 Unknown", "15 Unknown",
    "16 Unknown", "17 Unknown", "18 Unknown", "19 Unknown",
    "20 Unknown", "21 Unknown"
};

static constexpr char HIRQ_ERR_NAMES[22][50] = {
    "00 Unknown", "01 Unknown", "02 PRIM_NOMEM", "03 MATR_NOMEM",
    "04 ILLEGAL_PARAM", "05 Unknown", "06 Unknown", "07 Unknown",
    "08 Unknown", "09 Unknown", "10 Unknown", "11 Unknown",
    "12 Unknown", "13 Unknown", "14 Unknown", "15 Unknown",
    "16 Unknown", "17 Unknown", "18 Unknown", "19 Unknown",
    "20 Unknown", "21 Unknown"
};


enum PCW_listtype {
    HPL_opaque = 0,
    HPL_opaque_mv = 1,
    HPL_translucent = 2,
    HPL_translucent_mv = 3,
    HPL_punchthru = 4,
    HPL_none = 10
};

enum PCW_paratype {
    ctrl_end_of_list = 0,
    ctrl_user_tile_clip = 1,
    ctrl_object_list_set = 2,
    ctrl_reserved = 3,
    global_polygon_or_modifier_volume = 4,
    global_sprite = 5,
    global_reserved = 6,
    vertex_parameter = 7
};

union PCW {
    struct {
        u32 uv_is_16bit: 1; // 0
        u32 gouraud: 1; // 1
        u32 offset: 1; // 2
        u32 texture: 1;// 3
        u32 col_type: 2; // 4-5
        u32 volume: 1; // 6
        u32 shadow: 1; // 7
        u32 : 8; // 8-15

        u32 user_clip: 2; // 16-17
        u32 strip_len: 2; // 18-19
        u32 : 3; // 20-22
        u32 group_en: 1; // 23

        u32 list_type: 3; // 24-26
        u32 : 1;// 27
        u32 end_of_strip: 1; // 28
        u32 para_type: 3; // 29-31
    };
    u32 u{};
};

union TCW_palette {
    struct {
        u32 tex_addr : 21;
        u32 pal_select : 6;
        u32 pixel_format : 3;
        u32 vq_compressed : 1;
        u32 mipmapped : 1;
    };
    u32 u{};
};

union TCW_normal {
    struct {
        u32 tex_addr : 21;
        u32 : 4;
        u32 stride_select : 1;
        u32 scan_order : 1;
        u32 pixel_format : 3;
        u32 vq_compressed : 1;
        u32 mipmapped : 1;
    };
    u32 u{};
};

union TCW {
    TCW_palette palette;
    TCW_normal normal;
};

union TSP_WORD {
    struct {
        u32 tex_u_size : 3; // 1 << (3 + this)
        u32 tex_v_size : 3;
        u32 tex_shading_instruction : 2;
        u32 mipmap_d_adjust : 4;
        u32 super_sample_texture : 1;
        u32 filter_mode : 2;
        u32 clamp_uv : 2;
        u32 flip_uv : 2;
        u32 ignore_tex_alpha : 1;
        u32 use_alpha : 1;
        u32 color_clamp : 1;
        u32 fog_ctrl : 2;
        u32 DST_select : 1;
        u32 SRC_select : 1;
        u32 DST_alpha_instr : 3;
        u32 SRC_alpha_instr : 3;
    };
    u32 u{};
};

union ISP_TSP_WORD {
    struct {
        u32 : 20;
        u32 dcalc_ctrl : 1;
        u32 cache_bypass : 1;
        u32 uv_16bit : 1;
        u32 gouraud : 1;
        u32 offset : 1;
        u32 texture : 1;
        u32 z_write_disable : 1;
        u32 culling_mode : 2;
        u32 depth_compare_mode : 3;
    };
    u32 u{};
};

struct VERTEX {
    float x{}, y{}, z{};
    float u{}, v{};
    u32 r{}, g{}, b{}, a{};
    struct {
        u32 r{}, g{}, b{}, a{};
    } offset{};

    void copy(const VERTEX* other) {
        x = other->x;
        y = other->y;
        z = other->z;
        u = other->u;
        v = other->v;
        r = other->r;
        g = other->g;
        b = other->b;
        a = other->a;
        offset.r = other->offset.r;
        offset.g = other->offset.g;
        offset.b = other->offset.b;
        offset.a = other->offset.a;
    }
};

struct POLY {
    void copy(const POLY& other) {
        pcw = other.pcw;
        isp_tsp_word = other.isp_tsp_word;
        tsp_word = other.tsp_word;
        tcw = other.tcw;
    }
    void clear() {
        num_vtx = 0;
        face.r = face.g = face.b = face.a = 0;
    }
    struct {
        u32 r{}, g{}, b{}, a{};
    } face{};
    u32 num_vtx{};
    VERTEX vtx[4]{};
    PCW pcw{};
    ISP_TSP_WORD isp_tsp_word{};
    TSP_WORD tsp_word{};
    TCW tcw{};
    bool sprite{};
};

struct core;
typedef void (core::*setpix_func)(i32 x, i32 y, u32 r, u32 g, u32 b);

struct core {
    explicit core(DREAMCAST::core *parent);
    ~core();
    DREAMCAST::core *bus;

    void copy_fb();

    u32 in_vblank{};
    u64 frame_start_cycle{};

    u32 *cur_output{};
    cvec_ptr<physical_io_device> display_ptr{};
    JSM_DISPLAY *display{};

    void reset();
    void start_render();
    void vblank_in();
    void vblank_out();
    void new_frame();
    void recalc_interrupts();
    void process_global_pcw(PCW cmd);
    u64 read_io(u32 addr, u8 sz, bool *success);
    void write_io(u32 addr, u8 sz, u64 val, bool *success);
    void write_ta_fifo(u32 addr, u8 sz, u64 val, bool *success);

    void soft_reset();
    void TA_cmd();
    void parse_polygon();
    void parse_vertex(PCW cmd);
    void parse_sprite();
    void parse_mv();
    void TA_list_init();
    void TA_FIFO_load(u32 src_addr, u32 tx_len, void* src);
    void TA_FIFO_DMA(u32 src_addr, u32 tx_len, void *src, u32 src_len);

    struct {
        u64 cycles_per_line{};
        u64 cycles_per_frame{};
        struct {
            u64 vblank_in_start{}, vblank_out_start{};
        } interrupt{};
    } timing{};

    u64 master_frame{};

    u8 *RAM{};
    void eval_interrupt(interruptmasks irq_num, bool is_true);
    void lower_interrupt(interruptmasks irq_num);
    void raise_interrupt(interruptmasks irq_num, i64 delay);
    void recalc_frame_timing();
    u64 schedule_frame(scheduler_callback vblankfunc, u64 cur_clock);
    u32 get_polygon_typenum(u8 wrd);
    u32 get_vertex_typenum(u8 wrd);
    u32 get_sprite_typenum(u8 wrd);
    u32 poly_size(u32 poly_kind);
    u32 vertex_size(u32 vertex_kind);
    u32 sprite_vertex_size(u32 vertex_kind);

    struct {
        u32 list_type{}; // 0-4, or 5 if Sprite

        u32 tri_type{};

        u32 cmd_buffer_index{};
        u8 cmd_buffer[64];


        bool first_gp{};
        PCW global_params{};

        u32 poly_kind{};
        u32 vertex_kind{};
        POLY cur_poly{};
        std::vector<POLY> poly_lists[6]; // last is sprite
        std::vector<VERTEX> vtx_list;
        bool end_of_strip{};
        struct {
            u32 a, r, g, b{};
        } intensity{};
    } ta{};
    void ingest_tri(VERTEX *v0, VERTEX *v1, VERTEX *v2);
    void ingest_quad(VERTEX *v0, VERTEX *v1, VERTEX *v2, VERTEX *v3);
    void end_parse();
    void setup_setpix();
    setpix_func setpix;
    void setpix_rgb565(i32 x, i32 y, u32 r, u32 g, u32 b);



private:
    u32 get_SPG_line();
    u32 get_frame_cycle();
    void update_poly_vertex_type();

public:
    union {
        struct {
            u32 fb_packmode : 3; // 0 = 555 KRGB. 1=565 RGB. 2=4444ARGB. 3=1555 ARGB. 4=888RGB/24bit. 5=0888KRGB. 6=8888ARGB. 7=bad
            u32 fb_dither : 1; // 0 = discard lower bits. 1 = dither
            u32 _res : 4;
            u32 fb_kval : 8; // value to use for K
            u32 fb_alpha_threshold : 8;
        };
        u32 u{};
    } FB_W_CTRL{};

    union {
        struct {
            u32 fb_enable : 1;
            u32 fb_line_double : 1;
            u32 fb_depth : 2; // 0 = 0555. 1 = 565. 2 = 888. 3 = 0888
            u32 fb_concat : 3;
            u32 _ : 1;
            u32 fb_chroma_threshold : 8;
            u32 fb_stripsize : 6;
            u32 fb_strip_buf_en : 1;
            u32 vclk_div : 1;
        };
        u32 u{};
    } FB_R_CTRL{};

    union {
        struct {
            u32 _ : 2;
            u32 field : 21;
        };
        u32 u{};
    } FB_R_SOF1{};

    union {
        struct {
            u32 _ : 2;
            u32 field : 21;
        };
        u32 u{};
    } FB_R_SOF2{};

    union {
        struct {
            u32 _ : 2;
            u32 field : 22;
        };
        u32 u{};
    } FB_W_SOF1{};

    union {
        struct {
            u32 _ : 2;
            u32 field : 22;
        };
        u32 u{};
    } FB_W_SOF2{};

    union {
        struct {
            u32 min : 11;
            u32 _ : 5;
            u32 max : 11;
        };
        u32 u{};
    } FB_X_CLIP{};

    float x_min{}, x_max{};
    float y_min{}, y_max{};

    union {
        struct {
            u32 min : 10;
            u32 _ : 6;
            u32 max : 10;
        };
        u32 u{};
    } FB_Y_CLIP{};

    union {
        struct {
            u32 scale : 8;
            u32 intensity_shadow_enable : 1;
        };
        u32 u{};
    } FPU_SHAD_SCALE{};

    union {
        u32 u{};
        float f;
    } FPU_CULL_VAL{};
    union {
        u32 u{};
        float f;
    } ISP_BACKGND_D{};

    union {
        u32 u{};
        float f;
    } FPU_PERP_VAL{};

    union {
        struct {
            u32 ptr_first_burst_sz : 4;
            u32 ptr_burst_sz : 4;
            u32 isp_param_burst_trigger_threshold : 6;
            u32 tsp_param_burst_trigger_threshold : 6;
            u32 R : 1;
            u32 region_header_type : 1;
        };
        u32 u{};
    } FPU_PARAM_CFG{};

    union {
        struct {
            u32 fb_x_size : 10;
            u32 fb_y_size : 10;
            u32 fb_modulus : 10;
        };
        u32 u{};
    } FB_R_SIZE{};

    union {
        struct {
            u32 tag_offset : 3;
            u32 tag_addr : 21;
            u32 skip : 3;
            u32 shadow : 1;
            u32 cache_bypass: 1;
        };
        u32 u{};
    } ISP_BACKGND_T{};

    union {
        struct {
            u32 fpu_pixel_sampling_pos : 1;
            u32 tsp_pixel_sampling_pos : 1;
            u32 tsp_texel_sampling_pos : 1;
        };
        u32 u{0b111};
    } HALF_OFFSET{};

    u32 SPAN_SORT_CFG{};  // 0x005F8030
    union {  // VO_BORDER_COL
        struct {
            u32 blue : 8;
            u32 green : 8;
            u32 red : 8;
            u32 chroma : 1;
        };
        u32 u{};
    } VO_BORDER_COL;  // 0x005F8040
    u32 REGION_BASE{};
    u32 PARAM_BASE{};
    union {  // FB_W_LINESTRIDE
        struct {
            u32 line_stride : 9;
        };
        u32 u{};
    } FB_W_LINESTRIDE;  // 0x005F804C
    union {  // ISP_FEED_CFG
        struct {
            u32 pre_sort_mode : 1;
            u32 : 2;
            u32 discard_mode : 1;
            u32 punch_through_chunk_size : 10;
            u32 cache_size_for_translucency : 10;
        };
        u32 u{};
    } ISP_FEED_CFG;  // 0x005F8098
    u32 SDRAM_REFRESH{};  // 0x005F80A0
    u32 SDRAM_CFG{};  // 0x005F80A8
    u32 FOG_COL_RAM{};  // 0x005F80B0
    union {  // FOG_COL_VERT
        struct {
            u32 blue : 8;
            u32 green : 8;
            u32 red : 8;
        };
        u32 u{};
    } FOG_COL_VERT;  // 0x005F80B4
    union {  // FOG_DENSITY
        struct {
            u32 exponent : 8;
            u32 mantissa : 8;
        };
        u32 u{};
    } FOG_DENSITY;  // 0x005F80B8
    u32 FOG_CLAMP_MAX{};  // 0x005F80BC
    u32 FOG_CLAMP_MIN{};  // 0x005F80C0
    union {  // SPG_HBLANK_INT
        struct {
            u32 line_comp_val : 10;
            u32 : 2;
            u32 hblank_int_mode : 2;
            u32 : 2;
            u32 hblank_in_interrupt : 10;
        };
        u32 u{};
    } SPG_HBLANK_INT;  // 0x005F80C8
    union {  // SPG_VBLANK_INT
        struct {
            u32 vblank_in_line : 10;
            u32 : 6;
            u32 vblank_out_line : 10;
        };
        u32 u{};
    } SPG_VBLANK_INT;  // 0x005F80CC
    union {  // SPG_CONTROL
        struct {
            u32 mhsync_pol : 1;
            u32 mvsync_pol : 1;
            u32 mcsync_pol : 1;
            u32 spg_lock : 1;
            u32 interlace : 1;
            u32 force_field2 : 1;
            u32 NTSC : 1;
            u32 PAL : 1;
            u32 sync_direction : 1;
            u32 csync_on_h : 1;
        };
        u32 u{};
    } SPG_CONTROL;  // 0x005F80D0
    union {  // SPG_HBLANK
        struct {
            u32 hbstart : 10;
            u32 : 6;
            u32 hbend : 10;
        };
        u32 u{};
    } SPG_HBLANK;  // 0x005F80D4
    union {  // SPG_LOAD
        struct {
            u32 hcount : 10;
            u32 : 6;
            u32 vcount : 10;
        };
        u32 u{};
    } SPG_LOAD;  // 0x005F80D8
    union {  // SPG_VBLANK
        struct {
            u32 vbstart : 10;
            u32 : 6;
            u32 vbend : 10;
        };
        u32 u{};
    } SPG_VBLANK;  // 0x005F80DC
    union {  // SPG_WIDTH
        struct {
            u32 hswidth : 7;
            u32 : 1;
            u32 vswidth : 4;
            u32 bpwidth : 10;
            u32 eqwidth : 10;
        };
        u32 u{};
    } SPG_WIDTH;  // 0x005F80E0
    union {  // TEXT_CONTROL
        struct {
            u32 stride : 5;
            u32 : 3;
            u32 bank_bit : 5;
            u32 : 3;
            u32 index_endian_reg : 1;
            u32 cb_endian_reg : 1;
        };
        u32 u{};
    } TEXT_CONTROL;  // 0x005F80E4
    union {  // VO_CONTROL
        struct {
            u32 hsync_pol : 1;
            u32 vsync_pol : 1;
            u32 blank_pol : 1;
            u32 blank_video : 1;
            u32 field_mode : 4;
            u32 pixel_double : 1;
            u32 : 7;
            u32 pclk_delay : 6;
        };
        u32 u{};
    } VO_CONTROL;  // 0x005F80E8
    u32 VO_STARTX{};  // 0x005F80EC
    union {  // VO_STARTY
        struct {
            u32 vertical_start_on_field1 : 10;
            u32 : 6;
            u32 vertical_start_on_field2 : 10;
        };
        u32 u{};
    } VO_STARTY;  // 0x005F80F0
    union {  // SCALER_CTL
        struct {
            u32 vertical_scale_factor : 16;
            u32 horizontal_scroll_enable : 1;
            u32 interlace : 1;
            u32 field_select : 1;
        };
        u32 u{};
    } SCALER_CTL;  // 0x005F80F4
    union {  // FB_BURSTCTRL
        struct {
            u32 vid_burst : 6;
            u32 : 2;
            u32 vid_lat : 7;
            u32 wr_burst : 4;
        };
        u32 u{};
    } FB_BURSTCTRL;  // 0x005F8110
    union {  // Y_COEFF
        struct {
            u32 coeff_0_2 : 8;
            u32 coeff_1 : 8;
        };
        u32 u{};
    } Y_COEFF;  // 0x005F8118
    union {  // TA_OL_BASE
        struct {
            u32 : 5;
            u32 base_addr : 19;
        };
        u32 u{};
    } TA_OL_BASE;  // 0x005F8124
    u32 TA_ISP_BASE{};  // 0x005F8128
    union {  // TA_OL_LIMIT
        struct {
            u32 : 5;
            u32 limit_addr : 19;
        };
        u32 u{};
    } TA_OL_LIMIT;  // 0x005F812C
    union {  // TA_ISP_LIMIT
        struct {
            u32 : 2;
            u32 limit_addr : 22;
        };
        u32 u{};
    } TA_ISP_LIMIT;  // 0x005F8130
    u32 TA_NEXT_OPB{};  // 0x005F8134
    u32 TA_ITP_CURRENT{};  // 0x005F8138
    union {  // TA_GLOB_TILE_CLIP
        struct {
            u32 tile_x_num : 6;
            u32 : 10;
            u32 tile_y_num : 4;
        };
        u32 u{};
    } TA_GLOB_TILE_CLIP;  // 0x005F813C
    union {  // TA_ALLOC_CTRL
        struct {
            u32 O_OPB : 2;
            u32 : 2;
            u32 OM_OPB : 2;
            u32 : 2;
            u32 T_OPB : 2;
            u32 : 6;
            u32 PT_OPB : 2;
            u32 : 2;
            u32 OPB_MODE : 1;
        };
        u32 u{};
    } TA_ALLOC_CTRL;  // 0x005F8140
    u32 TA_LIST_CONT{};  // 0x005F8160
    u32 TA_NEXT_OPB_INIT{};  // 0x005F8164
    u32 FOG_TABLE[128]{};
    u32 SB_PDSTAP{};  // 0x005F7C00
    u32 SB_PDSTAR{};  // 0x005F7C04
    u32 SB_PDSLEN{};  // 0x005F7C08
    u32 SB_PDDIR{};  // 0x005F7C0C
    u32 SB_PDTSEL{};  // 0x005F7C10
    u32 SB_PDEN{};  // 0x005F7C14
    u32 SB_PDST{};  // 0x005F7C18
    union {  // SB_PDAPRO
        struct {
            u32 bottom_address : 7;
            u32 : 1;
            u32 top_address : 7;
        };
        u32 u{};
    } SB_PDAPRO;  // 0x005F7C80
    void pvr_dma_init(u64 val);
    void pvr_dma_irq_trigger();
    void update_dma_trigger(u64 val);
    void draw_line(VERTEX *v0, VERTEX *v1);
    void draw_tri(VERTEX *v0, VERTEX *v1, VERTEX *v2);
    u32 TA_cmd_len(PCW cmd);
};
}