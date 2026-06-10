//
// Created by . on 3/28/26.
//

#pragma once

#include "helpers/int.h"
#include "helpers/debugger/debugger.h"
#include "helpers/debugger/debuggerdefs.h"
#include "helpers/physical_io.h"

namespace NEC_D65010G031 {

struct SQWV {
    u8 reload{};
    u8 counter{};
    u8 polarity{};
    bool ext_enable{true};
};

struct core {
    void cycle();
    void reset();
    void cycle_psg();
    i16 sample_psg(bool debug);
    i16 sample_debug_wf(int num);

    u64 master_frame_count{};
    // B800, 32x24, so 32 bytes per line
    // left 2 not displayed
    // 240x192 pixels displayable
    //
    SQWV sq[3];

    bool psg_ext_enable{true};

    struct {
        u16 ROM_tile_addr{};
        u32 all_tiles_from_ROM{};
        u32 border_color{};
        bool IRQ_fired{};
        bool buffer_data{};
        u8 fd_data{};
        u8 fd_control{};
    } io{};

    void *callback_ptr{};
    void (*set_irq)(void *ptr, bool val);
    void (*set_busreq)(void *ptr, bool val);
    u8 (*joy_read)(void *ptr);
    void (*joy_write)(void *ptr, u8 val);
    u8 (*read_mem)(void *ptr, u16 addr);

    u8 *cur_output{};
    u8 *cur_line{};
    u64 *master_clock{};
    cvec_ptr<physical_io_device> display_ptr{};
    u8 read(u8 addr, u8 old, bool has_effect);
    void write(u8 addr, u8 val);
    JSM_DISPLAY *display{};

    struct {
        u16 r{}, g{}, b{};
    } shift{};


    u16 hpos{}, vpos{};
private:
    void new_line();
    void new_frame();
    void irq(bool level);
    void busreq(bool level);

    u16 tile_addr{};
    u16 tilemap_addr{};

    bool irq_level{}, busreq_level{};

    /*DBG_START
    DBG_EVENT_VIEW_START
    DBG_EVENT_VIEW_END
    DBG_END*/

};
}