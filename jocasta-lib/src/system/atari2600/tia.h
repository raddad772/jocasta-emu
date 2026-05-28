#pragma once

#include "helpers/physical_io.h"
#include "helpers/int.h"
#include "helpers/cvec.h"
#include "helpers/debugger/debugger.h"
#include "helpers/debugger/debuggerdefs.h"

namespace atari2600 {
#define TIA_WQ_MAX 20

struct TIA {
    // For output to display
    u8 *cur_output{};
    u8 missed_vblank{};
    cvec_ptr<physical_io_device> display_ptr{};
    JSM_DISPLAY *display{};

    void reset();
    void bus_cycle(u16 addr, u8 *data, bool rw);
    void run_cycle();

    u8 cpu_RDY{};

    u32 hcounter{};
    u32 pcounter{};
    u32 vcounter{};
    u32 CLK{};
    u32 master_frame{};
    u32 frames_since_restart{};
    u32 clock_div{};

    u32 hblank{}, x{}, hsync{};

    struct {
        u32 vblank_in_lines{};
        u32 display_line_start{};
        u32 vblank_out_start{};
        u32 vblank_out_lines{};
    } timing{};

    struct atari_TIA_col {
        u32 m0_p0{}, m0_p1{}, m1_p0{}, m1_p1{};
        u32 p0_pf{}, p0_ball{}, p1_pf{}, p1_ball{};
        u32 m0_pf{}, m0_ball{}, m1_pf{}, m1_ball{};
        u32 ball_pf{}, p0_p1{}, m0_m1{};
    } col{};

    struct {
        u32 vsync{};
        u32 vblank{};
        u32 inpt_0_3_ctrl{};
        u32 inpt_4_5_ctrl{};

        u32 pf{};
        u32 pf_pixel{};

        u8 COLUP0{};
        u8 COLUP1{};
        u8 COLUPF{};
        u8 COLUBK{};
        union {
            struct {
                u8 mirror : 1;
                u8 score_mode: 1;
                u8 priority: 1;
                u8 : 1;
                u8 ball_size: 2;
            };
            u8 u{};
        } CTRLPF{};

        u32 ENABL_cache{}; // Vertical delay cache for ENABL

        u32 VDELBL{};

        u32 hmoved{}; // was hmove triggered?

        u32 INPT[6]{};
    } io{};

    struct M_t {
        u32 hm{};
        u32 size{};
        i32 start_counter{};
        i32 starting{}, counter{};
        i32 width_counter{};
        i32 output{};
        i32 pixel_counter{};
        u32 enable{};
        u32 locked_to_player{};
    } m[2]{};
    struct BALL_t {
        i32 counter{};
        u32 output{}, enable[2]{}, delay{}, size{}, hm{};
    } ball{};

    struct P_t {
        u32 copy{}, enable{};
        i32 start_counter{}, starting{}, counter{}, pixel_counter{}, width_counter{};
        u32 GRP[2]{}, delay{};
        u32 hm{};

        u32 count{}, phase{};
        u32 scan_counter{}, scan_counting{};
        u32 reflect{};
        u32 output{}; // 0 or 1
        u32 size{};
        i32 scan_divisor{}, scan_duty{};
    } P[2]{};

    struct WQ_item {
        u32 active{};
        u32 address{};
        u32 data{};
        u32 delay{};
    } write_queue[TIA_WQ_MAX]{};

    DBG_START
    DBG_EVENT_VIEW
    DBG_END

private:
    void flip_buffer();
    void WQ_add(u32 num, u32 val, u32 delay);
    u32 m_width(u32 num);
    void m_start(u32 num);
    void m_step(u32 num, u32 clocks);
    u32 p_width(u32 num);
    void p_start(u32 num, u32 copy);
    void p_step(u32 num, u32 clocks);
    void ball_step(u32 clocks);
    void WQ_finish(WQ_item* item);
    void WQ_cycle();
    void vsync(u32 val);
    void new_frame();
    void update_RESMPn(u32 num);
    void update_RESMP();
    u8 read(u16 addr);
    void write(u16 addr, u8 *data);
    u32 get_player_pixel(u32 msx, u32 player_num);
    u32 get_ball_pixel(u32 msx);
    u32 get_missile_pixel(u32 msx, u32 missile_num);
    void new_scanline();
};


}
