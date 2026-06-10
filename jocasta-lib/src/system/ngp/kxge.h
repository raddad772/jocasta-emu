#pragma once
#include "helpers/int.h"
#include "helpers/cvec.h"
#include "helpers/physical_io.h"
#include "helpers/scheduler.h"

namespace TMP95C061 { struct core; }

namespace KXGE {

static constexpr u32 K1GE = 1;
static constexpr u32 K2GE = 2;

static constexpr u32 DISP_WIDTH = 160;
static constexpr u32 DISP_HEIGHT = 152;
static constexpr u32 DISP_LINE_CYCLES = 515;
static constexpr u32 DISP_TOTAL_HEIGHT = 199;
static constexpr u32 FRAME_CYCLES = DISP_LINE_CYCLES * DISP_TOTAL_HEIGHT;
static constexpr u32 HBLANK_LAST_VISIBLE = 150;

struct core {
    explicit core(u32 variant_in) : variant(variant_in) {}

    void draw_line(u32 y);
    u8 chr_dot(u32 ch, u32 row, u32 col) const;
    void reset();
    void init_io_regs();

    void dbg_render_chrmap(u32 plane, u32 *outbuf, u32 out_width);
    void dbg_render_plane_iso(u32 plane, u32 *outbuf, u32 out_width);
    void dbg_render_sprites(u32 *outbuf, u32 out_width);

    void schedule_first();
    void schedule_scanline(u64 line, u64 clock, u32 jitter);
    void new_line();
    void new_frame();
    void issue_hblank();
    void issue_vblank();
    void try_vblank_fire();
    bool vblank_fired_this_frame{};

    u8 read(u32 addr);
    void write(u32 addr, u8 data);

    u32 variant;

    scheduler_t *scheduler{};
    TMP95C061::core *cpu{};

    u16 *cur_output{};
    cvec_ptr<physical_io_device> display_ptr{};
    JSM_DISPLAY *display{};

    u8 CHR_RAM[8 * 1024]{};
    u8 VRAM[4 * 1024]{};
    u8 SPRAM[288]{};
    u16 CRAM[256];

    struct {
        bool hint_enable{};
        bool vint_enable{};
        u8 wba_h{};
        u8 wba_v{};
        u8 wsi_h{};
        u8 wsi_v{};
        u8 ref{};
        bool c_ovr{};
        bool blnk{};
        bool neg{};
        u8 oowc{};
        u8 po_h{}, po_v{};
        bool pf{};
        u8 s1so_h{}, s1so_v{};
        u8 s2so_h{}, s2so_v{};
        u8 spplt[2][4]{};
        u8 sc1plt[2][4]{};
        u8 sc2plt[2][4]{};
        u8 bgon{};
        u8 bgc{};
        u8 ledon{};
        u8 ledfrq{};
        bool mode{};
        u8 bg_dc{};
        u8 ctrl_dc{};
    } io;

    u32 cur_line{};
    u64 line_start_clock{};
    u64 frame_num{};

    static constexpr u16 DBG_TRANSPARENT = 0xFFFF;
    u16 dbg_plane_out[2][DISP_HEIGHT * DISP_WIDTH]{};
    u8 dbg_plane_seen[2][(256 * 256) / 8]{};

private:
    u32 resolve_dot(int kind, u8 pc, u8 cpc, u8 index) const;
    u32 color_to_rgba(u16 color) const;
};
}
