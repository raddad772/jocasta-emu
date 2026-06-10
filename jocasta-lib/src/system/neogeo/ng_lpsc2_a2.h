#pragma once
#include <memory>
#include <vector>
#include "helpers/cvec.h"
#include "helpers/physical_io.h"
#include "helpers/int.h"
// Sprite Line Generator!
// 384x264 frame

namespace NEOGEO {
struct core;
}

namespace NEOGEO::LPSC2_A2 {

static constexpr u32 PIX_DIV = 4;
struct SPRITE {
    u16 sp_index;
    u16 sp_x;
    u16 y_in_sprite;
    u16 hsize;
    u16 vsize;
    u16 vscale;
    bool wrap;
};


struct core {
    explicit core(NEOGEO::core *parent);
    NEOGEO::core *bus;
    u32 raster_line_counter{}; // upper 9 bits read from this, you add F8 to it

    u32 *cur_output{};
    std::unique_ptr<u32[]> debug_output{};
    cvec_ptr<physical_io_device> display_ptr{};
    JSM_DISPLAY *display{};
    SPRITE spr[381];

    struct {
        u32 shadow{};
    } io{};

    struct {
        u8 frame{};
        u8 counter{};
        u8 reload{};
        bool disable{};
    } auto_animation{};

    u64 frame_start_clocks{};
    u64 frame_num{};

    u32 cycles_per_scanline_fp8{};

    void do_scanline();
    void reset();
    void schedule_first();
    void timer_tick();
    template<bool do_debug> void draw_line(i32 y);
    template<bool do_debug> void new_frame();
    template<bool do_debug> void line_start(u32 line_num);
    template<bool do_debug> void vblank(bool level);
    template<bool do_debug> static void handle_iack(void *ptr);

    template<bool do_debug>void eval_IRQs();

    template<bool do_debug> void draw_sprite(u32 num, u32 *linebuf);
    std::vector<u32> sprs{};

    struct {
        bool reload_on_vblank{}, reload_on_zero{}, reload_on_change{};
        u32 counter{}, reload{};
        bool irq_enable{};
        bool stop{};
    } timer{};

    // If an ack bit is set, that source is re-armed and can raise a new
    // pending IRQ. Pending IRQs are consumed by the 68000 IACK cycle.
    struct {
        bool vblank{}, timer{}, reset{};
    } irq_ack{};
    struct {
        bool vblank{}, timer{}, reset{};
    } irq_pending{};
    template<bool do_debug> void timer_tick();

private:
    template<bool do_debug> void schedule_frame(bool is_first);
};
}
