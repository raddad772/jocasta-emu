//
// Created by . on 11/25/25.
//
#include <cstring>
#include <cassert>
#include "commodore64.h"
#include "c64_bus.h"

#define TAG_SCANLINE 1
#define TAG_FRAME 2

#define JSM jsm_system* jsm

void C64::core::update_IRQ(u32 device_num, u8 level) {
    const u32 msk = 1 << device_num;
    u32 old = IRQ_F;
    /*static int a = 0;
    if (level && a!=device_num) {
        printf("\nIRQ UP %d", device_num);
    }
    a = device_num;*/
    if (level) IRQ_F |= msk;
    else IRQ_F &= ~msk;
    if (old != IRQ_F) {
        cpu.pins.IRQ = IRQ_F != 0;
    }
}

void C64::core::update_NMI(u32 device_num, u8 level) {
    const u32 msk = 1 << device_num;
    u32 old = NMI_F;
    if (level) NMI_F |= msk;
    else NMI_F &= ~msk;
    if (old != NMI_F) {
        cpu.pins.NMI = NMI_F != 0;
    }

}

void C64::core::set_audio_ring(audio_output_ring *ring) {
    audio.output_ring = ring;
    // SID is not yet implemented; no samples pushed until it is
}

void C64::core::load_prg(multi_file_set &mfs) {
    sideload_data.present = true;
    auto &buf = mfs.files[0].buf;
    auto ptr = static_cast<u8 *>(buf.ptr);
    assert(buf.size > 3);
    sideload_data.addr = static_cast<u16>(ptr[0]);
    sideload_data.addr |= static_cast<u16>(ptr[1]) << 8;
    assert((sideload_data.addr+(buf.size-2)) < 65536);
    sideload_data.buf.allocate(buf.size-2);
    ptr += 2;
    memcpy(sideload_data.buf.ptr, ptr, buf.size-2);

    printf("\nPRG file to %04x (%ld bytes) prepared", sideload_data.addr, buf.size-2);
}

void C64::core::sideload(multi_file_set& mfs) {
    // if ends with .prg,
    // 2 little-endian bytes indicating address, then the program load
    // don't skip boot!?!?
    sideload_data.present = false;
    char *s = mfs.files[0].name;
    if (ends_with(s, ".prg")) {
        load_prg(mfs);
    }
    else {
        printf("\nUnsupported C64 sideload file: %s", s);
    }

}

void C64::core::runtime_sideload(multi_file_set& mfs)
{
    sideload(mfs);
    complete_sideload();
}

u32 read_trace_m6502(void *ptr, u32 addr) {
    return static_cast<C64::core *>(ptr)->mem.read_main_bus(addr, 0, 0);
}

static void run_block(void *ptr, u64 key, u64 clock, u32 jitter)
{
    auto *th = static_cast<C64::core *>(ptr);
    th->run_block();
}

static void populate_opts(C64::core *th) {
    debugger_widgets_add_button(th->opts, "Load selected file", 1, 0);
}

void C64::core::complete_sideload() {
    if (sideload_data.present) {
        memcpy(mem.RAM+sideload_data.addr, sideload_data.buf.ptr, sideload_data.buf.size);
        sideload_data.present = false;
        printf("\nSIDELOAD COMPLETE!");
    }
}

static void read_opts(C64::core *th)
{
    debugger_widget &w = th->opts.at(0);
    if (w.button.value) th->complete_sideload();
}


jsm_system *Commodore64_new(jsm::systems in_kind)
{
    auto* th = new C64::core(jsm::regions::USA);
    populate_opts(th);
    //th->scheduler.max_block_size = 1;
    //th->scheduler.run.func = &run_block;
    //th->scheduler.run.ptr = th;

    snprintf(th->label, sizeof(th->label), "Commodore54");

    jsm_debug_read_trace dt;
    dt.read_trace = &read_trace_m6502;
    dt.ptr = static_cast<void *>(th);
    th->cpu.setup_tracing(&dt, &th->master_clock);
    return th;
}

void C64_delete(JSM) {
    auto *th = dynamic_cast<C64::core *>(jsm);

    for (physical_io_device &pio : th->IOs) {
    }
    th->IOs.clear();

    delete th;
}

static void sample_audio_debug_max(void *ptr, u64 key, u64 clock, u32 jitter)
{
    auto *th = static_cast<C64::core *>(ptr);

    debug_waveform *dw = &th->dbg.waveforms.main.get();
    if (dw->user.buf_pos < dw->samples_requested) {
        static_cast<float *>(dw->buf.ptr)[dw->user.buf_pos] = th->sid.output.level;
        dw->user.buf_pos++;
    }

    th->audio.next_sample_cycle_max += th->audio.master_cycles_per_max_sample;
    //th->scheduler.only_add_abs(static_cast<i64>(th->audio.next_sample_cycle_max), 0, th, &sample_audio_debug_max, nullptr);
}

static void sample_audio_debug_min(void *ptr, u64 key, u64 clock, u32 jitter)
{
    auto *th = static_cast<C64::core *>(ptr);

    // PSG
    debug_waveform *dw = &th->dbg.waveforms.chan[0].get();
    for (int j = 0; j < 3; j++) {
        dw = &th->dbg.waveforms.chan[j].get();
        if (dw->user.buf_pos < dw->samples_requested) {
            static_cast<float *>(dw->buf.ptr)[dw->user.buf_pos] = th->sid.sample_channel_debug(3);
            dw->user.buf_pos++;
        }
    }

    th->audio.next_sample_cycle_min += th->audio.master_cycles_per_min_sample;
    //th->scheduler.only_add_abs(static_cast<i64>(th->audio.next_sample_cycle_min), 0, th, &sample_audio_debug_min, nullptr);
}

void C64::core::schedule_first()
{
    //scheduler.only_add_abs(static_cast<i64>(audio.next_sample_cycle_max), 0, this, &sample_audio_debug_max, nullptr);
    //scheduler.only_add_abs(static_cast<i64>(audio.next_sample_cycle_min), 0, this, &sample_audio_debug_min, nullptr);
    //vic2.schedule_first();
    // We just do this in the SID chip basically
    //scheduler_only_add_abs((i64)audio.next_sample_cycle, 0, th, &sample_audio, nullptr);

}
void C64::core::setup_crt(JSM_DISPLAY &d)
{
    d.kind = jsm::display_kinds::CRT;
    d.enabled = 1;

    d.fps = vic2.timing.second.frames_per;
    // removed: d.fps_override_hint = d.fps;

    d.pixelometry.cols.left_hblank = 0;
    d.pixelometry.cols.visible = vic2.timing.line.pixels_per;
    d.pixelometry.cols.max_visible = vic2.timing.line.pixels_per;
    d.pixelometry.cols.right_hblank = 0;
    d.pixelometry.offset.x = 0;

    d.pixelometry.rows.top_vblank = 0;
    d.pixelometry.rows.visible = vic2.timing.frame.lines_per;
    d.pixelometry.rows.max_visible = vic2.timing.frame.lines_per;
    d.pixelometry.rows.bottom_vblank = 0;
    d.pixelometry.offset.y = 0;

    d.geometry.physical_aspect_ratio.width = 4;
    d.geometry.physical_aspect_ratio.height = 3;

    d.pixelometry.overscan.left = d.pixelometry.overscan.right = 0;
    d.pixelometry.overscan.top = d.pixelometry.overscan.bottom = 0;
}

void C64::core::setup_audio(std::vector<physical_io_device> &inIOs)
{
    physical_io_device &pio = inIOs.emplace_back();
    pio.init(HID_AUDIO_CHANNEL, true, true, false, true);
    JSM_AUDIO_CHANNEL *chan = &pio.audio_channel;
    chan->num = 1;
    chan->sample_rate = vic2.timing.second.cycles_per;
    chan->low_pass_filter = 20000;
}

/*constexpr u32 C64_keyboard_keymap[66] = {
// shift lock, shift, tab, esc, calt, fn, cmd, right shift

    // alt cr, right fn
    // arrows
    // return
    // pg up/down
    // a-z
    // run stop, ` \ = -
    // *0-9
    // [] ;' ,./
    // backspace
    JK_0, JK_1, JK_2, JK_3, JK_4, JK_5, JK_6, JK_7, JK_8, JK_9,
    //JK_A,
};*/


static void C64IO_load_cart(JSM, multi_file_set &mfs, physical_io_device &which_pio)
{
    printf("\nWARN NOT SUPPORT CART YET");
    /*auto *C64 = dynamic_cast<C64::core *>(jsm);
    buf* b = &mfs.files[0].buf;

    C64->cart.load_ROM_from_RAM(static_cast<char *>(b->ptr), b->size, which_pio);
    C64->reset();*/
}

static void C64IO_unload_cart(JSM)
{
}


void C64::core::describe_io()
{
    if (jsm.described_inputs) return;
    jsm.described_inputs = true;

    IOs.reserve(15);

    // Joystick Port 2 (CIA1 PRA bits 0-4) — the primary game port on C64
    joy2.setup(IOs, 2);
    // Joystick Port 1 (CIA1 PRB bits 0-4)
    joy1.setup(IOs, 1);

    // keyboard
    keyboard.setup(IOs);

    // power and reset buttons
    physical_io_device* chassis = &IOs.emplace_back();
    chassis->init(HID_CHASSIS, 1, 1, 1, 1);

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
    d->init(HID_CART_PORT, 1, 1, 1, 0);
    d->cartridge_port.load_cart = &C64IO_load_cart;
    d->cartridge_port.unload_cart = &C64IO_unload_cart;

    // screen
    d = &IOs.emplace_back();
    d->init(HID_DISPLAY, 1, 1, 0, 1);
    d->display.allocate_output(0, vic2.timing.frame.num_pixels);
    d->display.allocate_output(1, vic2.timing.frame.num_pixels);
    d->display.output_debug_metadata[0] = nullptr;
    d->display.output_debug_metadata[1] = nullptr;
    setup_crt(d->display);
    vic2.display_ptr.make(IOs, IOs.size()-1);
    d->display.active_draw_buffer = 0;
    vic2.cur_output = static_cast<u8 *>(d->display.output[0]);
    vic2.line_output = vic2.cur_output;

    setup_audio(IOs);

    vic2.display = &vic2.display_ptr.get().display;
}

void C64::core::play()
{
}

void C64::core::pause()
{
}

void C64::core::stop()
{
}

void C64::core::get_framevars(framevars& out)
{
    
    out.master_frame = vic2.master_frame;
    out.x = vic2.hpos;
    out.scanline = vic2.vpos;
    out.master_cycle = master_clock;
}

u32 C64::core::finish_frame()
{
    keyboard.new_data = true;
    if (::dbg.do_debug && dbg.waveforms.main.vec != nullptr) {
        debug_waveform *wf = &dbg.waveforms.main.get();
        wf->setup(vic2.timing.frame.cycles_per);
        sid.ext_enable = wf->ch_output_enabled;
        if (audio.master_cycles_per_max_sample == 0 && wf->samples_requested > 0)
            audio.master_cycles_per_max_sample = static_cast<double>(vic2.timing.frame.cycles_per) / static_cast<double>(wf->samples_requested);
        for (u32 i = 0; i < 3; i++) {
            wf = &dbg.waveforms.chan[i].get();
            wf->setup(vic2.timing.frame.cycles_per);
            sid.channels[i].ext_enable = wf->ch_output_enabled;
        }
        if (audio.master_cycles_per_min_sample == 0) {
            wf = &dbg.waveforms.chan[0].get();
            if (wf->samples_requested > 0)
                audio.master_cycles_per_min_sample = static_cast<double>(vic2.timing.frame.cycles_per) / static_cast<double>(wf->samples_requested);
        }
    }
    u64 start_frame = vic2.master_frame;
    read_opts(this);
    while (vic2.master_frame == start_frame) {
        finish_scanline();
        if (::dbg.do_break) break;
    }
    return vic2.display->active_draw_buffer;
}

u32 C64::core::finish_scanline()
{
    keyboard.new_data = true;
    i32 start_y = vic2.vpos;
    while (vic2.vpos == start_y) {
        run_block();
        //this->clock.master_cycle_count += 8;
        if (::dbg.do_break) break;
    }

    return vic2.display->active_draw_buffer;
}

u32 C64::core::step_master(u32 howmany)
{
    keyboard.new_data = true;
    cycles_deficit += howmany;
    while (cycles_deficit > 0) {
        run_block();
        cycles_deficit--;
        if (::dbg.do_break) break;
    }
    return 0;
}

void C64::core::load_BIOS(multi_file_set& mfs) {
    if (mfs.files.size() == 1) { // 1 file. first 8KB BIOS, second 8KB RAM
        assert(mfs.files[0].buf.size == 0x4000);
        mem.load_BASIC(mfs.files[0].buf, 0);
        mem.load_KERNAL(mfs.files[0].buf, 0x2000);
    }
    else {
        if (mfs.files.size() >= 2) {
            printf("\n%ld %ld", mfs.files[0].buf.size, mfs.files[1].buf.size);
            fflush(stdout);
            assert(mfs.files[0].buf.size == 0x2000);
            assert(mfs.files[1].buf.size == 0x2000);
            mem.load_BASIC(mfs.files[0].buf, 0);
            mem.load_KERNAL(mfs.files[1].buf, 0);
        }
        if (mfs.files.size() >= 3) {
            assert(mfs.files[2].buf.size == 0x1000);
            mem.load_CHARGEN(mfs.files[2].buf, 0);
        }
    }
}
