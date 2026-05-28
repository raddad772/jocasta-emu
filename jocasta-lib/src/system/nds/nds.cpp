//
// Created by . on 12/4/24.
//
#include <cstring>
#include <cassert>
#include <cstdlib>

#include "nds.h"
#include "nds_bus.h"
#include "nds_debugger.h"
#include "nds_rtc.h"
#include "system/nds/3d/nds_ge.h"
#include "nds_regs.h"
#include "nds_apu.h"

#include "helpers/multisize_memaccess.cpp"

jsm_system *NDS_new()
{
    return new NDS::core();
}

void NDS_delete(jsm_system *jsm)
{
    delete jsm;
}

namespace NDS {

static void sample_audio_debug_main(void *ptr, u64 key, u64 clk, u32 jitter)
{
    auto *th = static_cast<core *>(ptr);
    debug_waveform *dw = &th->dbg.waveforms.main.get();
    if (!dw || dw->user.buf_pos >= dw->samples_requested) return; // buffer full — stop
    // Store mono mix of left+right (scaled to float range)
    i32 mono = (th->apu.left_output + th->apu.right_output) >> 1;
    float s = ((static_cast<float>(mono) + 512.0f) / 511.5f) - 1.0f;
    if (s < -1.0f) s = -1.0f;
    if (s >  1.0f) s =  1.0f;
    static_cast<float *>(dw->buf.ptr)[dw->user.buf_pos] = s;
    dw->user.buf_pos++;
    th->audio.next_sample_cycle_max += th->audio.master_cycles_per_max_sample;
    th->scheduler.only_add_abs(static_cast<i64>(th->audio.next_sample_cycle_max), 0, th, &sample_audio_debug_main, nullptr);
}

static void sample_audio_debug_chan(void *ptr, u64 key, u64 clk, u32 jitter)
{
    auto *th = static_cast<core *>(ptr);
    bool any_remaining = false;
    for (int j = 0; j < 6; j++) {
        debug_waveform *dw = &th->dbg.waveforms.chan[j].get();
        if (dw->user.buf_pos < dw->samples_requested) {
            i16 smp = th->apu.CH[j].sample;
            float s = static_cast<float>(smp) / 32767.0f;
            if (s < -1.0f) s = -1.0f;
            if (s >  1.0f) s =  1.0f;
            static_cast<float *>(dw->buf.ptr)[dw->user.buf_pos] = s;
            dw->user.buf_pos++;
            any_remaining = true;
        }
    }
    if (!any_remaining) return; // all buffers full — stop
    th->audio.next_sample_cycle_min += th->audio.master_cycles_per_min_sample;
    th->scheduler.only_add_abs(static_cast<i64>(th->audio.next_sample_cycle_min), 0, th, &sample_audio_debug_chan, nullptr);
}

void core::set_audio_ring(audio_output_ring *ring)
{
    apu.output_ring = ring;
}

void core::do_next_scheduled_frame(void *bound_ptr, u64 key, u64 current_clock, u32 jitter)
{
    auto *th = static_cast<core *>(bound_ptr);
    th->schedule_frame(current_clock-jitter, false);
}

void core::schedule_frame(u64 start_clock, bool is_first)
{
    // Schedule out a frame!
    i64 cur_clock = start_clock;
    clock.cycles_left_this_frame += clock.timing.frame.cycles;

    //clock.cycles_left_this_frame += clock.timing.frame.cycles;

    // x lines of end hblank, start hblank
    // somewhere in there, start vblank
    for (u32 line = 0; line < clock.timing.scanline.number; line++) {
        // vblank start
        if (line == clock.timing.frame.vblank_up_on) {
            scheduler.only_add_abs(cur_clock, 1, this, &PPU::core::vblank<false>, &PPU::core::vblank<true>, nullptr);
        }
        // vblank end
        if (line == clock.timing.frame.vblank_down_on) {
            scheduler.only_add_abs(cur_clock, 0, this, &PPU::core::vblank<false>, &PPU::core::vblank<true>, nullptr);
        }

        scheduler.only_add_abs(cur_clock, 0, this, &PPU::core::hblank<false>, &PPU::core::hblank<true>, nullptr);
        scheduler.only_add_abs(cur_clock+clock.timing.scanline.cycle_of_hblank, 1, this, PPU::core::hblank<false>, PPU::core::hblank<true>, nullptr);

        // Advance clock
        cur_clock += clock.timing.scanline.cycles_total;
    }

    //printf("\nSCHEDULE NEW FRAME SET FOR CYCLE %lld", start_clock+clock.timing.frame.cycles);
    scheduler.only_add_abs_w_tag(start_clock+clock.timing.frame.cycles, 0, this, &do_next_scheduled_frame, nullptr, 1);
    if (is_first) scheduler.only_add_abs(static_cast<i64>(apu.next_sample), 0, &apu, &APU::core::master_sample_callback, nullptr);
}

u32 core::finish_frame()
{
    read_opts();

    if (::dbg.do_debug && dbg.waveforms.main.vec != nullptr) {
        const u64 now = clock.master_cycle_count7;

        // Main waveform
        auto *wf = &dbg.waveforms.main.get();
        wf->setup(static_cast<float>(clock.timing.frame.cycles));
        if (wf->samples_requested > 0) {
            audio.master_cycles_per_max_sample =
                static_cast<double>(clock.timing.frame.cycles) / static_cast<double>(wf->samples_requested);
            audio.next_sample_cycle_max = static_cast<double>(now) + audio.master_cycles_per_max_sample;
            scheduler.only_add_abs(static_cast<i64>(audio.next_sample_cycle_max), 0, this, &sample_audio_debug_main, nullptr);
        }

        // Channel waveforms
        u32 chan_samples = dbg.waveforms.chan[0].get().samples_requested;
        if (chan_samples > 0) {
            audio.master_cycles_per_min_sample =
                static_cast<double>(clock.timing.frame.cycles) / static_cast<double>(chan_samples);
            audio.next_sample_cycle_min = static_cast<double>(now) + audio.master_cycles_per_min_sample;
            scheduler.only_add_abs(static_cast<i64>(audio.next_sample_cycle_min), 0, this, &sample_audio_debug_chan, nullptr);
        }
        for (u32 i = 0; i < 6; i++) {
            dbg.waveforms.chan[i].get().setup(static_cast<float>(clock.timing.frame.cycles));
        }
    }

    if (::dbg.do_debug) scheduler.run_til_tag<true>(1);
    else scheduler.run_til_tag<false>(1);
    return 0;
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

void core::populate_opts()
{
    debugger_widgets_add_checkbox(opts, "Enable Debug Cam", true, false, 0);
}

void core::read_opts() {
    debugger_widget *w = &opts.at(0);
    ge.debug_cam.enabled = w->checkbox.value;
    if (ge.debug_cam.enabled) update_debug_cam_matrix();
}

void core::option_changed(const char* key, i32 value)
{
    if (strcmp(key, "arm7_mode") == 0) {
        if (value == 1 && !arm7.cached_mode)
            arm7_enter_cached_mode();
        else if (value == 0 && arm7.cached_mode)
            arm7_exit_cached_mode();
    }
    else if (strcmp(key, "arm9_mode") == 0) {
        if (value == 1 && !arm9.cached_mode)
            arm9_enter_cached_mode();
        else if (value == 0 && arm9.cached_mode)
            arm9_exit_cached_mode();
    }
    else if (strcmp(key, "fast_boot") == 0) {
        jsm.fast_boot = (value != 0);
    }
}

void core::get_framevars(framevars& out)
{
    
    out.master_frame = clock.master_frame;
    out.x = clock.ppu.x;
    out.scanline = clock.ppu.y;
    out.master_cycle = clock.master_cycle_count7;
}

void core::skip_BIOS()
{
    // Load 170h bytes of header into main RAM starting at 27FFE00
    u32 *hdr_start = static_cast<u32 *>(cart.ROM.ptr);
    u32 *hdr = hdr_start;

    for (i32 i = 0; i < 0x170; i += 4) {
        mainbus_write9<4, false>(this, 0x027FFE00+i, 0, hdr[i>>2]);
    }

    // Read binary addresses
    u32 bin7_offset, bin9_offset, bin9_lo, bin7_lo, bin9_size, bin7_size, arm7_entry, arm9_entry, bin9_hi, bin7_hi;
    bin9_offset = cR[4](hdr, 0x20);
    arm9_entry = cR[4](hdr, 0x24);
    bin9_lo = cR[4](hdr, 0x28);
    bin9_size = cR[4](hdr, 0x2C);

    bin7_offset = cR[4](hdr, 0x30);
    arm7_entry = cR[4](hdr, 0x34);
    bin7_lo = cR[4](hdr, 0x38);
    bin7_size = cR[4](hdr, 0x3C);

    // Copy binaries into RAM
    for (u32 i = 0; i < bin7_size; i += 4) {
        mainbus_write7<4, false>(this, bin7_lo+i, 0, cR[4](cart.ROM.ptr, bin7_offset+i));
    }
    for (u32 i = 0; i < bin9_size; i += 4) {
        mainbus_write9<4, false>(this, bin9_lo+i, 0, cR[4](cart.ROM.ptr, bin9_offset+i));
    }

    arm9.regs.R[14] = 0x03002F7C;
    arm9.regs.R_irq[0] = 0x03003F80;
    arm9.regs.R_svc[0] = 0x03003FC0;
    arm9.regs.R[15] = arm9_entry;
    arm9.regs.CPSR.mode = ARM32::modes::M_system;
    arm9.fill_regmap();
    arm9.direct_boot();
    arm9.reload_pipeline<false, false>();

    arm7.regs.R[13] = 0x0380FD80;
    arm7.regs.R_irq[0] = 0x0380FF80;
    arm7.regs.R_svc[0] = 0x0380FFC0;
    arm7.regs.R[15] = arm7_entry;
    arm7.regs.CPSR.mode = ARM32::modes::M_system;
    arm7.fill_regmap();
    arm7.reload_pipeline<false, false>();

    mainbus_write9<1, false>(this, R9_WRAMCNT, 0, 3); // setup WRAM

    mainbus_write9<4, false>(this, 0x027FF800, 0, 0x1FC2); // chip id
    mainbus_write9<4, false>(this, 0x027FF804, 0, 0x1FC2); // chip id
    mainbus_write9<2, false>(this, 0x027FF850u, 0, 0x5835); // ARM7 BIOS CRC
    mainbus_write9<2, false>(this, 0x027FF880u, 0, 0x0007); // Message from ARM9 to ARM7
    mainbus_write9<2, false>(this, 0x027FF884u, 0, 0x0006); // ARM7 boot task
    mainbus_write9<4, false>(this, 0x027FFC00u, 0, 0x1FC2); // Copy of chip ID 1
    mainbus_write9<4, false>(this, 0x027FFC04u, 0, 0x1FC2); // Copy of chip ID 2
    mainbus_write9<4, false>(this, 0x027FFC10u, 0, 0x5835); // Copy of ARM7 BIOS CRC
    mainbus_write9<4, false>(this, 0x027FFC40u, 0, 0x0001); // Boot indicator
    // Now copy 112 bytes from firmware 0x03FE00  to RAM 0x027FFC80
    for (u32 i = 0; i < 112; i++) {
        mainbus_write9<1, false>(this, 0x027FFC80 + i, 0, mem.firmware[0x03FE00+i]);
    }
    // Now write to POSTFLG registers...
    mainbus_write9<1, false>(this, 0x04000300, 0, 1);
    mainbus_write7<1, false>(this, 0x04000300, 0, 1);

    // Do some more boot stuf...
    mainbus_write9<2, false>(this, R9_POWCNT1, 0, 0x0203);

    cart.direct_boot();
    printf("\ndirect boot done!");
 }




void core::reset()
{

    // Emu resets...
    scheduler.clear();
    clock.master_cycle_count7 = 0;
    waitstates.current_transaction = 0;

    arm7.reset<false>();
    arm9.reset<false>();
    RTC_reset();
    spi.irq_id = 0;

    for (u32 i = 0; i < 4; i++) {
        timer7_t *t = &timer7[i];
        t->overflow_at = 0xFFFFFFFFFFFFFFFF;
        t->enable_at = 0xFFFFFFFFFFFFFFFF;
        timer9_t *p = &timer9[i];
        p->overflow_at = 0xFFFFFFFFFFFFFFFF;
        p->enable_at = 0xFFFFFFFFFFFFFFFF;
    }
    //clock.reset();
    SPI_reset();
    ppu.reset();
    mainbus_write9<1, true>(this, R9_VRAMCNT+0, 0, 0);
    mainbus_write9<1, true>(this, R9_VRAMCNT+1, 0, 0);
    mainbus_write9<1, true>(this, R9_VRAMCNT+2, 0, 0);
    mainbus_write9<1, true>(this, R9_VRAMCNT+3, 0, 0);
    mainbus_write9<1, true>(this, R9_VRAMCNT+4, 0, 0);
    mainbus_write9<1, true>(this, R9_VRAMCNT+6, 0, 0);
    mainbus_write9<1, true>(this, R9_WRAMCNT, 0, 3); // at R9_VRAMCNT+7
    mainbus_write9<1, true>(this, R9_VRAMCNT+8, 0, 0);
    mainbus_write9<1, true>(this, R9_VRAMCNT+9, 0, 0);

    io.arm7.IME = io.arm7.IE = io.arm7.IF = 0;
    io.arm9.IME = io.arm9.IE = io.arm9.IF = 0;
    io.arm7.POSTFLG = io.arm9.POSTFLG = 0;
    // TODO: Reset DMA and timers...
    // IPC, SPi, APU

    // Components such as RTC...
    ge.reset();
    re.reset();

    // Invalidate all block caches so stale compiled blocks from a previous
    // session can't be re-used after a ROM swap or hard reset.
    mem.RAM_block_cache.clear_all_blocks();
    mem.WRAM_share_block_cache.clear_all_blocks();
    mem.WRAM_arm7_block_cache.clear_all_blocks();
    mem.bios7_block_cache.clear_all_blocks();
    mem.bios9_block_cache.clear_all_blocks();
    mem.vram.block_cache.clear_all_blocks();
    arm9.arm9_block_cache.reset();

    if (jsm.fast_boot) skip_BIOS();
    waitstates.current_transaction = 0;
    clock.master_cycle_count7 = 0;
    clock.master_cycle_count9 = 0;

    scheduler.clear();
    clock.cycles_left_this_frame = 0;
    schedule_frame(0, true); // Schedule first frame
    printf("\nNDS reset complete!");
}
#ifndef MIN
#define MIN(x,y) ((x) < (y) ? (x) : (y))
#endif

u32 core::finish_scanline()
{
    read_opts();

    u64 scanline_cycle = clock.current7() - clock.ppu.scanline_start;
    assert(scanline_cycle < clock.timing.scanline.cycles_total);

    u64 old_clock = clock.current7();


    core::step_master(clock.timing.scanline.cycles_total - scanline_cycle);
    u64 diff = clock.current7() - old_clock;
    clock.cycles_left_this_frame -= diff;
    return ppu.display_top->active_draw_buffer;
}

u32 core::step_master(u32 howmany)
{
    bool was7 = arm7.cached_mode;
    bool was9 = arm9.cached_mode;
    if (was7) arm7_exit_cached_mode();
    if (was9) arm9_exit_cached_mode();
    read_opts();
    scheduler.run_for_cycles(howmany);
    if (was7) arm7_enter_cached_mode();
    if (was9) arm9_enter_cached_mode();
    return ppu.display_top->active_draw_buffer;
}

void core::load_BIOS(multi_file_set& mfs)
{
    
    // 7, 9, firmware
    memcpy(mem.bios7, mfs.files[0].buf.ptr, 16384);
    memcpy(mem.bios9, mfs.files[1].buf.ptr, 4096);
    memcpy(mem.firmware, mfs.files[2].buf.ptr, 256 * 1024);
}

static void NDSIO_unload_cart(jsm_system *ptr)
{
}

static void NDSIO_load_cart(jsm_system *ptr, multi_file_set &mfs, physical_io_device &pio) {
    
    BUF* b = &mfs.files[0].buf;
    auto *th = dynamic_cast<core *>(ptr);

    u32 r;
    th->cart.load_ROM_from_RAM(static_cast<char *>(b->ptr), b->size, &pio, &r);
    th->reset();
}

void core::setup_lcd(JSM_DISPLAY &d)
{
    d.kind = jsm::LCD;
    d.enabled = true;

    d.fps = static_cast<double>(clock.timing.arm7.hz) /
            static_cast<double>(clock.timing.frame.cycles);
    // removed: d.fps_override_hint = 60;
    // 256x192, but 355x263 with v and h blanks

    d.pixelometry.cols.left_hblank = 0;
    d.pixelometry.cols.visible = 256;
    d.pixelometry.cols.max_visible = 256;
    d.pixelometry.cols.right_hblank = 99;
    d.pixelometry.offset.x = 0;

    d.pixelometry.rows.top_vblank = 0;
    d.pixelometry.rows.visible = 192;
    d.pixelometry.rows.max_visible = 192;
    d.pixelometry.rows.bottom_vblank = 71;
    d.pixelometry.offset.y = 0;

    d.geometry.physical_aspect_ratio.width = 4;
    d.geometry.physical_aspect_ratio.height = 3;

    d.pixelometry.overscan.left = d.pixelometry.overscan.right = 0;
    d.pixelometry.overscan.top = d.pixelometry.overscan.bottom = 0;
}

void core::setup_audio()
{
    physical_io_device *pio = &IOs.emplace_back();
    pio->init(HID_AUDIO_CHANNEL, true, true, false, true);
    JSM_AUDIO_CHANNEL *chan = &pio->audio_channel;
    chan->num = 2;
    chan->sample_rate = 32768; // NDS hardware APU output rate
    chan->low_pass_filter = 16380;
    has.set_audio_ring = true; // core uses direct ring push in master_sample_callback
}


void core::describe_io()
{
    if (jsm.described_inputs) return;
    jsm.described_inputs = true;

    IOs.reserve(15);

    // controllers
    physical_io_device *cntroller = &IOs.emplace_back();
    controller.setup_pio(cntroller);

    // power and reset buttons
    physical_io_device* chassis = &IOs.emplace_back();
    chassis->init(HID_CHASSIS, true, true, true, true);
    HID_digital_button* b;

    b = &chassis->chassis.digital_buttons.emplace_back();
    snprintf(b->name, sizeof(b->name), "Power");
    b->state = 1;
    b->common_id = DBCID_ch_power;

    // cartridge port
    physical_io_device *d = &IOs.emplace_back();
    d->init(HID_CART_PORT, true, true, true, true);
    d->cartridge_port.load_cart = &NDSIO_load_cart;
    d->cartridge_port.unload_cart = &NDSIO_unload_cart;

    // top screen
    d = &IOs.emplace_back();
    d->init(HID_DISPLAY, true, true, false, true);
    d->display.allocate_output(0, 256 * 192 * 4);
    d->display.allocate_output(1, 256 * 192 * 4);
    memset(d->display.output[0], 0, 256*192*4);
    memset(d->display.output[1], 0, 256*192*4);
    d->display.output_debug_metadata[0] = nullptr;
    d->display.output_debug_metadata[1] = nullptr;
    setup_lcd(d->display);
    snprintf(d->display.label, sizeof(d->display.label), "Top");
    ppu.display_top_ptr.make(IOs, IOs.size()-1);
    d->display.active_draw_buffer = 0;
    ppu.cur_output_top = static_cast<u32 *>(d->display.output[0]);

    // bottom screen
    d = &IOs.emplace_back();
    d->init(HID_DISPLAY, true, true, false, true);
    d->display.allocate_output(0, 256 * 192 * 4);
    d->display.allocate_output(1, 256 * 192 * 4);
    memset(d->display.output[0], 0, 256*192*4);
    memset(d->display.output[1], 0, 256*192*4);
    d->display.output_debug_metadata[0] = nullptr;
    d->display.output_debug_metadata[1] = nullptr;
    setup_lcd(d->display);
    snprintf(d->display.label, sizeof(d->display.label), "Bottom");
    ppu.display_bot_ptr.make(IOs, IOs.size()-1);
    d->display.active_draw_buffer = 0;
    ppu.cur_output_bot = static_cast<u32 *>(d->display.output[0]);

    physical_io_device *t = &IOs.emplace_back();
    t->init(HID_TOUCHSCREEN, true, true, true, false);
    t->touchscreen.params.width = 256;
    t->touchscreen.params.height = 192;
    t->touchscreen.params.x_offset = 0;
    t->touchscreen.params.y_offset = 0;
    spi.touchscr.pio = t;

    setup_audio();

    ppu.display_top = &ppu.display_top_ptr.get().display;
    ppu.display_bot = &ppu.display_bot_ptr.get().display;
}
}
