//
// Created by . on 8/29/24.
//

#include <cstdio>
#include <cassert>
#include <cstring>
#include <cstdlib>

#include "helpers/int.h"
#include "helpers/sys_interface.h"
#include "helpers/physical_io.h"

#include "component/cpu/m6502/m6502.h"
#include "component/cpu/m6502/m6502_opcodes.h"

#include "apple2.h"
#include "apple2_bus.h"
#include "iou.h"

/*
$D000 - $DFFF (53248 - 57343): Bank-Switched RAM (2 Banks RAM, 1 Bank ROM)
$E000 - $FFFF (57344 - 65535): Bank-Switched RAM (1 Bank RAM, 1 Bank ROM)
 */

jsm_system *apple2_new(const system_config& cfg)
{
    return new apple2::core(cfg);
}

void apple2_delete(jsm_system *sys)
{
}

namespace apple2 {


#define MAX_WIDTH 560
#define MAX_HEIGHT 192

void core::setup_crt(JSM_DISPLAY &d)
{
    d.kind = jsm::display_kinds::CRT;
    d.enabled = true;

    d.fps=59.92;
    // removed: d.fps_override_hint = 60;

    d.pixelometry.cols.left_hblank = 0;
    d.pixelometry.cols.visible = MAX_WIDTH;
    d.pixelometry.cols.max_visible = MAX_WIDTH;
    d.pixelometry.cols.right_hblank = 96;
    d.pixelometry.offset.x = 0;

    d.pixelometry.rows.top_vblank = 0;
    d.pixelometry.rows.visible = MAX_HEIGHT;
    d.pixelometry.rows.max_visible = MAX_HEIGHT;
    d.pixelometry.rows.bottom_vblank = 70;
    d.pixelometry.offset.y = 0;

    d.geometry.physical_aspect_ratio.width = 4;
    d.geometry.physical_aspect_ratio.height = 3;

    d.pixelometry.overscan.left = d.pixelometry.overscan.right = d.pixelometry.overscan.top = d.pixelometry.overscan.bottom = 0;
}

static void apple2_IO_close_drive(jsm_system *ptr)
{

}

static void apple2_IO_remove_disc(jsm_system *ptr)
{

}

static void apple2_IO_open_drive(jsm_system *ptr)
{
}

static void apple2_IO_insert_disk(jsm_system *jsm, physical_io_device& pio, multi_file_set& mfs)
{
    auto *th = static_cast<core *>(jsm);
    for (auto& sp : th->slots) {
        auto *d2 = dynamic_cast<slot::disk2 *>(sp.get());
        if (!d2) continue;
        for (u32 di = 0; di < 2; di++) {
            if (!d2->iwm.drive[di].connected) continue;
            if (&d2->iwm.drive[di].ptr.get() != &pio) continue;
            floppy::apple2::DISC &aflpy = d2->iwm.my_disks.emplace_back();
            if (!aflpy.load(mfs.files[0].name, mfs.files[0].buf)) {
                printf("\nFailed to load disk: %s", mfs.files[0].name);
                d2->iwm.my_disks.pop_back();
                return;
            }
            d2->iwm.drive[di].disc = &aflpy;
            return;
        }
    }
}

void core::setup_audio(float fps) {
    physical_io_device &pio = IOs.emplace_back();
    pio.init(HID_AUDIO_CHANNEL, true, true, false, true);
    JSM_AUDIO_CHANNEL *chan = &pio.audio_channel;
    chan->sample_rate = 1'020'484;
    chan->low_pass_filter = 8000;
}

void core::set_audio_ring(audio_output_ring *ring)
{
    iou.audio_ring = ring;
}

void core::describe_io()
{
    if (described_inputs) return;
    described_inputs = true;

    IOs.reserve(15);

    // Keyboard
    iou.setup_keyboard(IOs);
    iou.keyboard_ptr.make(IOs, IOs.size()-1);

    // Joystick (game I/O connector): paddle 0/1 + open/closed apple buttons
    {
        physical_io_device &joy = IOs.emplace_back();
        joy.init(HID_CONTROLLER, 0, 0, 1, 1);
        joy.id = 0;
        joy.connected = 1;
        joy.enabled   = 1;
        snprintf(joy.controller.name, sizeof(joy.controller.name), "Joystick");
        JSM_CONTROLLER *cnt = &joy.controller;
        pio_new_button(cnt, "up",     DBCID_co_up);
        pio_new_button(cnt, "down",   DBCID_co_down);
        pio_new_button(cnt, "left",   DBCID_co_left);
        pio_new_button(cnt, "right",  DBCID_co_right);
        pio_new_button(cnt, "fire1",  DBCID_co_fire1); // Open Apple  / PB0
        pio_new_button(cnt, "fire2",  DBCID_co_fire2); // Closed Apple / PB1
        iou.joystick_ptr.make(IOs, IOs.size()-1);
    }


    // power and reset buttons
    physical_io_device* chassis = &IOs.emplace_back();
    chassis->init(HID_CHASSIS, true, true, true, true);
    HID_digital_button* b;
    b = &chassis->chassis.digital_buttons.emplace_back();
    snprintf(b->name, sizeof(b->name), "Power");
    b->state = 1;
    b->common_id = DBCID_ch_power;

    physical_io_device *d = nullptr;

    for (auto& sp : slots) {
        auto *d2 = dynamic_cast<slot::disk2 *>(sp.get());
        if (!d2) continue;
        for (u32 di = 0; di < 2; di++) {
            d = &IOs.emplace_back();
            d->init(HID_DISC_DRIVE, true, true, true, false);
            d->disc_drive.insert_disc = &apple2_IO_insert_disk;
            d->disc_drive.remove_disc = &apple2_IO_remove_disc;
            d->disc_drive.close_drive = &apple2_IO_close_drive;
            d->disc_drive.open_drive  = &apple2_IO_open_drive;
            d2->iwm.drive[di].device    = nullptr;
            d2->iwm.drive[di].ptr.make(IOs, IOs.size()-1);
            d2->iwm.drive[di].connected = true;
        }
    }

    // screen
    d = &IOs.emplace_back();
    d->init(HID_DISPLAY, true, true, false, true);
    iou.display_ptr.make(IOs, IOs.size()-1);
    d->display.allocate_output(0, MAX_WIDTH*MAX_HEIGHT);
    d->display.allocate_output(1, MAX_WIDTH*MAX_HEIGHT);
    d->display.output_debug_metadata[0] = nullptr;
    d->display.output_debug_metadata[1] = nullptr;
    iou.cur_output = (u8 *)d->display.output[0];
    d->display.active_draw_buffer = 0;
    //d->display.last_displayed = 1;
    setup_crt(d->display);

    setup_audio(59.92);

    iou.display = &iou.display_ptr.get().display;
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

void core::get_framevars(framevars &out)
{
    out.master_frame = clock.frames_since_restart;
    out.x = clock.crt_x;
    out.scanline = clock.crt_y;
}

void core::killall()
{

}

u32 core::finish_frame()
{
    
    u32 current_frame = clock.frames_since_restart;
    u32 scanlines = 0;
    while(current_frame == clock.frames_since_restart) {
        scanlines++;
        finish_scanline();
        if (::dbg.do_break) break;
    }
    return iou.display->active_draw_buffer;
}

u32 core::finish_scanline()
{
    
    u32 current_y = clock.crt_y;

    while(current_y == clock.crt_y) {
        cycle();
        if (::dbg.do_break) break;
    }
    return 0;
}

u32 core::step_master(u32 howmany)
{
    
    i32 todo = (howmany >> 1);
    if (todo == 0) todo = 1;
    for (i32 i = 0; i < todo; i++) {
        cycle();
        if (::dbg.do_break) break;
    }
    return 0;
}

void core::load_BIOS(multi_file_set &mfs)
{
    auto *b = &mfs.files[0].buf;
    mmu.ROM.allocate(b->size);
    memcpy(mmu.ROM.ptr, b->ptr, b->size);

    b = &mfs.files[1].buf;
    iou.ROM.allocate(b->size);
    memcpy(iou.ROM.ptr, b->ptr, b->size);
    iou.detect_video_ROM();

    if (mfs.files.size() > 2 && mfs.files[2].buf.size > 0) {
        b = &mfs.files[2].buf;
        for (auto& sp : slots) {
            auto *d2 = dynamic_cast<slot::disk2 *>(sp.get());
            if (!d2) continue;
            d2->rom.allocate(b->size);
            memcpy(d2->rom.ptr, b->ptr, b->size);
            break;
        }
    }
}

}
