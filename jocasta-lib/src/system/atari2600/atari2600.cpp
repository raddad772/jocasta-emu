//
// Created by Dave on 4/14/24.
//

#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <cstring>

#include "atari2600_bus.h"
#include "atari2600.h"
#include "cart.h"


jsm_system *atari2600_new() {
    return new atari2600::core();
}

void atari2600_delete(jsm_system *sys)
{
}

namespace atari2600 {

static void setup_crt(JSM_DISPLAY *d)
{
    d->kind = jsm::CRT;
    d->enabled = true;

    // NTSC crystal 3.579545 MHz, 228 colour clocks × 262 lines = ≈59.923 Hz
    d->fps = 3579545.0 / (228.0 * 262.0);

    d->pixelometry.cols.left_hblank = 1;
    d->pixelometry.cols.right_hblank = 85;
    d->pixelometry.cols.visible = 160;
    d->pixelometry.cols.max_visible = 160;
    d->pixelometry.offset.x = 1;

    d->pixelometry.rows.top_vblank = 1;
    d->pixelometry.rows.visible = 192;
    d->pixelometry.rows.max_visible = 192;
    d->pixelometry.rows.bottom_vblank = 21;
    d->pixelometry.offset.y = 1;

    d->geometry.physical_aspect_ratio.width = 4;
    d->geometry.physical_aspect_ratio.height = 3;

    d->pixelometry.overscan.left = d->pixelometry.overscan.right = d->pixelometry.overscan.top = d->pixelometry.overscan.bottom = 8;
}

static void atari2600IO_load_cart(jsm_system *sys, multi_file_set &mfs, physical_io_device &which_pio) {

    auto *th = dynamic_cast<core *>(sys);
    th->cart.load(mfs, which_pio);
    sys->reset();
}

static void atari2600IO_unload_cart(jsm_system *sys)
{

}

void core::describe_io()
{
    
    if (described_inputs) return;
    described_inputs = true;

    IOs.reserve(15);

    // controllers
    controller1.setup(0, "Player 1", true, &IOs.emplace_back());
    controller2.setup(1, "Player 2", false, &IOs.emplace_back());

    // Chassis buttons
    auto *chassis = &IOs.emplace_back();
    chassis->init(HID_CHASSIS, true, true, true, true);;
    HID_digital_button* b;
    b = &chassis->chassis.digital_buttons.emplace_back();
    snprintf(b->name, sizeof(b->name), "Power");
    b->state = 1;
    b->common_id = DBCID_ch_power;

    b = &chassis->chassis.digital_buttons.emplace_back();
    b->common_id = DBCID_ch_reset;
    snprintf(b->name, sizeof(b->name), "Reset");
    b->state = 0;

    b = &chassis->chassis.digital_buttons.emplace_back();
    b->common_id = DBCID_ch_diff_left;
    b->kind = DBK_SWITCH;
    snprintf(b->name, sizeof(b->name), "Left Difficulty");
    b->state = 0;

    b = &chassis->chassis.digital_buttons.emplace_back();
    b->common_id = DBCID_ch_diff_right;
    b->kind = DBK_SWITCH;
    snprintf(b->name, sizeof(b->name), "Right Difficulty");
    b->state = 0;

    b = &chassis->chassis.digital_buttons.emplace_back();
    b->common_id = DBCID_ch_game_select;
    snprintf(b->name, sizeof(b->name), "Game Select");
    b->state = 0;

    // cartridge port
    physical_io_device *d = &IOs.emplace_back();
    d->init(HID_CART_PORT, true, true, true, false);
    d->cartridge_port.load_cart = &atari2600IO_load_cart;
    d->cartridge_port.unload_cart = &atari2600IO_unload_cart;

    // screen
    d = &IOs.emplace_back();
    d->init(HID_DISPLAY, true, true, false, true);
    setup_crt(&d->display);
    d->display.allocate_output(0, 160 * 228 * 2);
    d->display.allocate_output(1, 160 * 228 * 2);
    memset(d->display.output[0], 0, 160 * 228 * 2);
    memset(d->display.output[1], 0, 160 * 228 * 2);
    d->display.output_debug_metadata[0] = nullptr;
    d->display.output_debug_metadata[1] = nullptr;
    tia.display_ptr.make(IOs, IOs.size()-1);
    tia.cur_output = static_cast<u8 *>(d->display.output[0]);
    d->display.active_draw_buffer = 0;

    tia.display = &tia.display_ptr.get().display;
}

void core::enable_tracing()
{
    // TODO
    assert(1==0);
}

void core::disable_tracing()
{
    // TODO
    assert(1==0);
}

void core::play()
{
}

void core::pause()
{
}

void core::stop()
{
}

void core::get_framevars(framevars& out)
{
    
    out.master_frame = tia.master_frame;
    out.x = tia.hcounter;
    out.scanline = tia.vcounter;
}

void core::latch_inputs()
{
    controller1.latch();
    controller2.latch();

    auto *p = &IOs.at(2);
    JSM_CHASSIS& ch = p->chassis;
    case_switches.reset         = ch.button_state(DBCID_ch_reset);
    case_switches.p0_difficulty = ch.button_state(DBCID_ch_diff_left);
    case_switches.p1_difficulty = ch.button_state(DBCID_ch_diff_right);
    case_switches.select        = ch.button_state(DBCID_ch_game_select);

    // Now refresh input stuff! YAY!
    tia.io.INPT[0] = tia.io.INPT[1] = tia.io.INPT[2] = tia.io.INPT[3] = tia.io.INPT[4] = tia.io.INPT[5] = 0;
    riot.io.SWCHA = 0;
    riot.io.SWCHB &= 0b00110100; // Preserve the bits not driven by console switches

    // Do switches
    riot.io.SWCHB |=
            (case_switches.reset ^ 1) |
            ((case_switches.select ^ 1) << 1) |
            (case_switches.color << 3) |
            (case_switches.p0_difficulty << 6) |
            (case_switches.p1_difficulty << 7);

    // P0 controller
    riot.io.SWCHA |= controller1.read_SWCHA() << 4;

    tia.io.INPT[4] |= (controller1.read_INPT() << 7);

    //P1 controller
    riot.io.SWCHA |= controller2.read_SWCHA();

    tia.io.INPT[5] |= (controller2.read_INPT() << 7);
}

void core::killall()
{

}

u32 core::finish_frame()
{
    latch_inputs();
    u32 current_frame = tia.master_frame;
    while (tia.master_frame == current_frame) {
        finish_scanline();
        if (::dbg.do_break) break;
    }
    return 0; // TODO tia.last_used_buffer;
}

u32 core::finish_scanline()
{
    latch_inputs();

    u32 start_y = tia.vcounter;
    u32 cycles = 0;
    while (tia.vcounter == start_y) {
        cycles++;
        core::step_master(1);
        if (::dbg.do_break) break;
    }

    return 0;
}

u32 core::step_master(u32 howmany)
{
    cycles_left += (i32)howmany;
    if (howmany > 1) latch_inputs();
    while (cycles_left > 0) {
        tia.run_cycle();
        if ((master_clock % 3) == 0) {
            CPU_run_cycle();
            riot.cycle();
        }

        master_clock++;
        cycles_left--;
        if (::dbg.do_break) break;
    }
    return 0;
}

void core::load_BIOS(multi_file_set& mfs)
{
    printf("\nAtari 2600 doesn't have a BIOS...?");
}
}
