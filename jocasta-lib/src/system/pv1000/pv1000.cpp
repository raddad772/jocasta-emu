//
// Created by . on 3/28/26.
//

#include <cassert>

#include "pv1000.h"
#include "pv1000_bus.h"
#include "system/genesis/genesis_bus.h"

jsm_system *CASIO_PV1000_new()
{
    return new CASIO_PV1000::core();
}

void CASIO_PV1000_delete(jsm_system *sys) {
    delete sys;
}

namespace CASIO_PV1000 {

void core::set_audio_ring(audio_output_ring *ring) {
    audio.output_ring = ring;
    // PV1000 master clock ≈ CYCLES_PER_SEC Hz, declared sample rate 54000 Hz
    audio.master_cycles_per_audio_sample = static_cast<double>(CYCLES_PER_SEC) / 54000.0;
}


static inline float i16_to_float(i16 val)
{
    return ((static_cast<float>(static_cast<i32>(val) + 32768) / 65535.0f) * 2.0f) - 1.0f;
}

void core::sample_audio() {
    if (clock.master_cycle_count >= static_cast<u64>(audio.next_sample_cycle)) {
        audio.next_sample_cycle += audio.master_cycles_per_audio_sample;
        i16 raw_sample = vdp.sample_psg(false);
        audio.dump.write_mono(raw_sample);
        if (audio.output_ring) {
            float s = i16_to_float(raw_sample);
            audio.output_ring->push(s, s); // mono — duplicate to both channels
        }
    }
    if (!::dbg.do_debug) return;
    debug_waveform *dw = &dbg.waveforms.main.get();
    if (clock.master_cycle_count >= dw->user.next_sample_cycle) {
        if (dw->user.buf_pos < dw->samples_requested) {
            dw->user.next_sample_cycle += dw->user.cycle_stride;
            static_cast<float *>(dw->buf.ptr)[dw->user.buf_pos] = i16_to_float(vdp.sample_psg(true));
            dw->user.buf_pos++;
        }
    }

    dw = &dbg.waveforms.chan[0].get();
    if (clock.master_cycle_count >= dw->user.next_sample_cycle) {
        for (int j = 0; j < 3; j++) {
            dw = &dbg.waveforms.chan[j].get();
            if (dw->user.buf_pos < dw->samples_requested) {
                dw->user.next_sample_cycle += dw->user.cycle_stride;
                float sv = i16_to_float(vdp.sample_debug_wf(j));
                static_cast<float *>(dw->buf.ptr)[dw->user.buf_pos] = sv;
                dw->user.buf_pos++;
            }
        }
    }
}

void core::cycle() {
    if (++clock.z80_div >= Z80_DIV) {
        clock.z80_div = 0;
        cycle_z80();
    }
    if (++clock.vdp_div >= VDP_DIV) {
        clock.vdp_div = 0;
        vdp.cycle();
    }
    if (++clock.psg_div >= PSG_DIV) {
        clock.psg_div = 0;
        vdp.cycle_psg();
    }
    sample_audio();
    clock.master_cycle_count++;
}

static void IO_load_cart(jsm_system *sys, multi_file_set &mfs, physical_io_device &which_pio) {
    auto *th = dynamic_cast<core *>(sys);
    BUF* b = &mfs.files[0].buf;

    th->ROM.copy_from_buf(*b);
    th->reset();
}

static void IO_unload_cart(jsm_system *sys) {

}

void core::setup_crt(JSM_DISPLAY &d)
{
    d.kind = jsm::CRT;
    d.enabled = true;

    // NTSC Z80 @ 3.579545 MHz × Z80_DIV=5 master clock; MASTER_CYCLES_PER_FRAME = VDP_DIV×380×262
    d.fps = (3579545.0 * Z80_DIV) / (double)MASTER_CYCLES_PER_FRAME;  // ≈59.924 Hz
    // removed: d.fps_override_hint = 60;

    d.pixelometry.cols.left_hblank = 0;
    d.pixelometry.cols.visible = 248;       // hardware shows 31 tiles (248 px); last tile is off-screen right
    d.pixelometry.cols.max_visible = 248;
    d.pixelometry.cols.right_hblank = 148;  // 380 total - 248 visible = 132 blank + 16 border = 148
    d.pixelometry.offset.x = 0;

    d.pixelometry.rows.top_vblank = 0;
    d.pixelometry.rows.visible = 192;
    d.pixelometry.rows.max_visible = 192;
    d.pixelometry.rows.bottom_vblank = 70;
    d.pixelometry.offset.y = 0;

    d.geometry.physical_aspect_ratio.width = 4;
    d.geometry.physical_aspect_ratio.height = 3;

    d.pixelometry.overscan.left = d.pixelometry.overscan.right = 0;
    d.pixelometry.overscan.top = d.pixelometry.overscan.bottom = 0;
}

void core::setup_audio() {
    physical_io_device *pio = &IOs.emplace_back();
    pio->init(HID_AUDIO_CHANNEL, true, true, false, true);
    JSM_AUDIO_CHANNEL *chan = &pio->audio_channel;
    chan->sample_rate = 54000;
    chan->num = 1;
    chan->low_pass_filter = 22000;
}

void core::describe_io()
{
    if (jsm.described_inputs) return;
    jsm.described_inputs = true;
    IOs.reserve(15);

    // controllers
    physical_io_device *c1 = &IOs.emplace_back();
    controller1.device_ptr.make(IOs, IOs.size()-1);
    controller1.setup_pio(*c1, 0, "Player 1", true);

    physical_io_device *c2 = &IOs.emplace_back();
    controller2.device_ptr.make(IOs, IOs.size()-1);
    controller2.setup_pio(*c2, 1, "Player 2", false);

    // power and reset buttons
    physical_io_device* chassis = &IOs.emplace_back();
    chassis->init(HID_CHASSIS, true, true, true, true);
    HID_digital_button* b;

    b = &chassis->chassis.digital_buttons.emplace_back();
    snprintf(b->name, sizeof(b->name), "Power");
    b->state = 1;
    b->common_id = DBCID_ch_power;

    b = &chassis->chassis.digital_buttons.emplace_back();
    b->common_id = DBCID_ch_reset;
    snprintf(b->name, sizeof(b->name), "Reset");
    b->state = 0;

    // cartridge port
    physical_io_device *d = &IOs.emplace_back();
    d->init(HID_CART_PORT, true, true, true, false);
    d->cartridge_port.load_cart = &IO_load_cart;
    d->cartridge_port.unload_cart = &IO_unload_cart;

    // screen
    d = &IOs.emplace_back();
    d->init(HID_DISPLAY, true, true, false, true);
    d->display.allocate_output(0, 256 * 240);
    d->display.allocate_output(1, 256 * 240);
    d->display.output_debug_metadata[0] = nullptr;
    d->display.output_debug_metadata[1] = nullptr;
    setup_crt(d->display);
    vdp.display_ptr.make(IOs, IOs.size()-1);
    d->display.active_draw_buffer = 0;
    vdp.cur_output = static_cast<u8 *>(d->display.output[0]);
    vdp.cur_line = vdp.cur_output;

    setup_audio();

    vdp.display = &vdp.display_ptr.get().display;
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
    out.master_frame = vdp.master_frame_count;
    out.x = vdp.hpos;
    out.scanline = vdp.vpos;
    out.master_cycle = clock.master_cycle_count;
}

void core::reset() {
    cpu.reset();
    vdp.reset();
    //psg.reset();
    io.z80.bus_ack = io.z80.bus_request = false;
    clock.master_cycle_count = 0;
    audio.next_sample_cycle = 0;
}

u32 core::finish_frame() {
    if (::dbg.do_debug) {
        const float cpf = static_cast<float>(MASTER_CYCLES_PER_FRAME);
        dbg_wf_setup(dbg.waveforms.main.get(), cpf, vdp.psg_ext_enable);
        for (u32 i = 0; i < 3; i++)
            dbg_wf_setup(dbg.waveforms.chan[i].get(), cpf, vdp.sq[i].ext_enable);
    }
    u64 old_frame = vdp.master_frame_count;
    while (old_frame == vdp.master_frame_count) {
        cycle();
        if (::dbg.do_break) break;
    }
    return vdp.display->active_draw_buffer;
}

u32 core::finish_scanline() {
    u32 old_vpos = vdp.vpos;
    while (old_vpos == vdp.vpos) {
        cycle();
        if (::dbg.do_break) break;
    }
    return vdp.display->active_draw_buffer;
}

u32 core::step_master(u32 howmany) {
    for (u32 i = 0; i < howmany; i++) {
        cycle();
        if (::dbg.do_break) break;
    }
    return vdp.display->active_draw_buffer;
}

void core::load_BIOS(multi_file_set &mfs)
{
    printf("\nCasio PV-1000 doesn't have a BIOS...?");
}


}
