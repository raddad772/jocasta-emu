//
// Created by . on 12/4/24.
//
#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <cstring>

#include "gba.h"
#include "gba_bus.h"
#include "gba_dma.h"
#include "gba_debugger.h"

#include "helpers/multisize_memaccess.cpp"


// 240x160, but 308x228 with v and h blanks
#define MASTER_CYCLES_PER_SCANLINE 1232
#define HBLANK_CYCLES 226
#define MASTER_CYCLES_BEFORE_HBLANK (MASTER_CYCLES_PER_SCANLINE - HBLANK_CYCLES)
#define MASTER_CYCLES_PER_FRAME (228 * MASTER_CYCLES_PER_SCANLINE)
// GBA crystal is exactly 2^24 = 16,777,216 Hz; MASTER_CYCLES_PER_FRAME * 60 = 16,853,760 (wrong)
#define MASTER_CYCLES_PER_SECOND 16777216
#define SCANLINE_HBLANK 1006

jsm_system *GBA_new()
{
    return new GBA::core();
}

void GBA_delete(jsm_system *sys)
{
    auto *th = dynamic_cast<GBA::core *>(sys);
    for (auto &pio : th->IOs) {
        if (pio.kind == HID_CART_PORT) {
            if (pio.cartridge_port.unload_cart) pio.cartridge_port.unload_cart(sys);
        }
    }
}

void GBA::core::pre_run() {
}

void GBA::core::post_run() {
#if GBA_CACHED_INTERPRETER
    //IWRAM_cache.gc_stale_blocks(IWRAM_cache.exec_counter > 0x10000 ? 0x1000 : IWRAM_cache.exec);
    //EWRAM_cache.gc_stale_blocks();
#endif
}

static void sample_audio_debug_max(void *ptr, u64 key, u64 clock, u32 jitter)
{
    auto *th = static_cast<GBA::core *>(ptr);
    debug_waveform *dw = th->audio.main_waveform;
    if (!dw || dw->user.buf_pos >= dw->samples_requested) return; // buffer full — stop
    dw->user.next_sample_cycle += dw->user.cycle_stride;
    static_cast<float *>(dw->buf.ptr)[dw->user.buf_pos] = th->apu.mix_sample(true);
    dw->user.buf_pos++;
    th->audio.next_sample_cycle_max += th->audio.master_cycles_per_max_sample;
    th->scheduler.only_add_abs(static_cast<i64>(th->audio.next_sample_cycle_max), 0, th, &sample_audio_debug_max, nullptr);
}

static void sample_audio_debug_min(void *ptr, u64 key, u64 clock, u32 jitter)
{
    auto *th = static_cast<GBA::core *>(ptr);
    // Check if all channel buffers are full before rescheduling
    bool any_remaining = false;
    for (int j = 0; j < 6; j++) {
        debug_waveform *dw = &th->dbg.waveforms.chan[j].get();
        if (dw->user.buf_pos < dw->samples_requested) {
            const float sv = th->apu.sample_channel(j);
            static_cast<float *>(dw->buf.ptr)[dw->user.buf_pos] = sv;
            dw->user.buf_pos++;
            any_remaining = true;
        }
    }
    if (!any_remaining) return; // all buffers full — stop
    th->audio.next_sample_cycle_min += th->audio.master_cycles_per_min_sample;
    th->scheduler.only_add_abs(static_cast<i64>(th->audio.next_sample_cycle_min), 0, th, &sample_audio_debug_min, nullptr);
}



u32 GBA::core::finish_frame()
{
#ifndef FOR_DREAMCAST
#ifdef GBA_STATS
    u64 master_start = clock.master_cycle_count;
    u64 arm_start = timing.arm_cycles;
    u64 tm0_start = timing.timer0_cycles;
#endif
    if (::dbg.do_debug) {
        memset(dbg_info.bg_scrolls, 0, sizeof(dbg_info.bg_scrolls));
    }
    // Arm debug audio waveforms for this frame (only when debugger is active
    // and waveforms have been set up). The callbacks stop rescheduling
    // themselves once their buffers are full — no cost when not debugging.
    if (::dbg.do_debug && dbg.waveforms.main.vec != nullptr) {
        const u64 now = clock.master_cycle_count;
        audio.main_waveform = &dbg.waveforms.main.get();
        audio.main_waveform->setup(MASTER_CYCLES_PER_FRAME);
        if (audio.main_waveform->samples_requested > 0) {
            audio.master_cycles_per_max_sample =
                static_cast<float>(MASTER_CYCLES_PER_FRAME) / static_cast<float>(audio.main_waveform->samples_requested);
            audio.next_sample_cycle_max = now + audio.master_cycles_per_max_sample;
            scheduler.only_add_abs(static_cast<i64>(audio.next_sample_cycle_max), 0, this, &sample_audio_debug_max, nullptr);
        }
        auto *chan0 = &dbg.waveforms.chan[0].get();
        if (chan0->samples_requested > 0) {
            audio.master_cycles_per_min_sample =
                static_cast<float>(MASTER_CYCLES_PER_FRAME) / static_cast<float>(chan0->samples_requested);
            audio.next_sample_cycle_min = now + audio.master_cycles_per_min_sample;
            scheduler.only_add_abs(static_cast<i64>(audio.next_sample_cycle_min), 0, this, &sample_audio_debug_min, nullptr);
        }
        for (u32 i = 0; i < 6; i++) {
            auto *cw = &dbg.waveforms.chan[i].get();
            cw->setup(MASTER_CYCLES_PER_FRAME);
            if (i < 4) apu.channels[i].ext_enable = cw->ch_output_enabled;
            else       apu.fifo[i - 4].ext_enable  = cw->ch_output_enabled;
        }
    }
#endif
    if (::dbg.do_debug) scheduler.run_til_tag<true>(2);
    else scheduler.run_til_tag<false>(2);
    post_run();

#ifdef GBA_STATS
    u64 master_num_cycles = (clock.master_cycle_count - master_start) * 60;
    u64 arm_num_cycles = (timing.arm_cycles - arm_start) * 60;
    u64 tm0_num_cycles = (timing.timer0_cycles - tm0_start) * 60;
    double master_div = (double)MASTER_CYCLES_PER_SECOND / (double)master_num_cycles;
    double arm_div = (double)MASTER_CYCLES_PER_SECOND / (double)arm_num_cycles;
    double tm0_div = (double)MASTER_CYCLES_PER_SECOND / (double)tm0_num_cycles;
    double arm_spd = (arm_div) * 100.0;
    double tm0_spd = ((double)timer[0].reload_ticks / tm0_div) * 100.0;
    double master_spd = master_div * 100.0;
    printf("\n\nSCANLINE:%d FRAME:%lld", clock.ppu.y, clock.master_frame);
    printf("\nEFFECTIVE MASTER FREQ IS %lld. DIVISOR %f, RUNNING AT %f SPEED", master_num_cycles, master_div, master_spd);
    printf("\nEFFECTIVE ARM FREQ IS %lld. DIVISOR %f, RUNNING AT %f SPEED", arm_num_cycles, arm_div, arm_spd);
    printf("\nEFFECTIVE TIMER0 FREQ IS %lld. DIVISOR %f, RUNNING AT %f SPEED", tm0_num_cycles, tm0_div, tm0_spd);
#endif

    return ppu.display->active_draw_buffer;
}

void GBA::core::play()
{
}

void GBA::core::pause()
{
}

void GBA::core::stop()
{
}

void GBA::core::get_framevars(framevars &out)
{
    out.master_frame = clock.master_frame;
    out.x = static_cast<i32>(clock.ppu.x);
    out.scanline = clock.ppu.y;
    out.master_cycle = clock.master_cycle_count;
}

void GBA::core::option_changed(const char* key, i32 value)
{
    if (strcmp(key, "fast_boot") == 0) {
        jsm.fast_boot = (value != 0);
    } else if (strcmp(key, "cached_interp") == 0) {
        if (value == 1 && !cpu.cached_mode) {
            EWRAM_cache.clear_all_blocks();
            IWRAM_cache.clear_all_blocks();
            cpu.enter_cached_mode();
            set_step_cpu();
        } else if (value == 0 && cpu.cached_mode) {
            cpu.exit_cached_mode();
            set_step_cpu();
        }
    }
}

void GBA::core::skip_BIOS()
{
/*
SWI 00h (GBA/NDS7/NDS9) - SoftReset
Clears 200h bytes of RAM (containing stacks, and BIOS IRQ vector/flags)
*/
    printf("\nSKIP GBA BIOS!");
    for (u32 i = 0x3007E00; i < 0x3008000; i++) {
        cW[1](WRAM_fast, i - 0x3000000, 0);
    }

    // , initializes system, supervisor, and irq stack pointers,
    // sets R0-R12, LR_svc, SPSR_svc, LR_irq, and SPSR_irq to zero, and enters system mode.
    for (u32 i = 0; i < 13; i++) {
        cpu.regs.R[i] = 0;
    }
    cpu.regs.R_svc[1] = 0;
    cpu.regs.R_irq[1] = 0;
    cpu.regs.SPSR_svc = 0;
    cpu.regs.SPSR_irq = 0;
    cpu.regs.CPSR.mode = ARM32::M_system;
    cpu.fill_regmap();
    /*
Host  sp_svc    sp_irq    sp_svc    zerofilled area       return address
  GBA   3007FE0h  3007FA0h  3007F00h  [3007E00h..3007FFFh]  Flag[3007FFAh] */

    cpu.regs.R_svc[0] = 0x03007FE0;
    cpu.regs.R_irq[0] = 0x03007FA0;
    cpu.regs.R[13] = 0x03007F00;

    cpu.regs.R[15] = 0x08000000;
    cpu.reload_pipeline<false, false>();

    // The GBA BIOS initializes WAITCNT = 0x4317 before jumping to the cartridge.
    // Reproduce that here so timing is correct in fast-boot mode.
    // Low byte 0x17: SRAM=3(9cyc), WS0 N=1(4cyc), WS0 S=1(2cyc), WS1 N=0, WS1 S=0
    // High byte 0x43: WS2 N=3(9cyc), WS2 S=0, PHI=0, prefetch=1
    waitstates.io.sram    = 3;
    waitstates.io.ws0_n   = 1;
    waitstates.io.ws0_s   = 1;
    waitstates.io.ws1_n   = 0;
    waitstates.io.ws1_s   = 0;
    waitstates.io.ws2_n   = 3;
    waitstates.io.ws2_s   = 0;
    waitstates.io.phi_term  = 0;
    waitstates.io.empty_bit = 0;
    set_waitstates();
    cart.prefetch.enable = true;
    enable_prefetch();
}

static void tick_APU(void *ptr, u64 key, u64 clock, u32 jitter)
{
    auto *th = static_cast<GBA::core *>(ptr);
    th->apu.cycle();
    clock -= jitter;
    th->scheduler.only_add_abs(clock + 16, 0, th, &tick_APU, nullptr);
}


static void sample_audio(void *ptr, u64 key, u64 clock, u32 jitter) {
    auto *th = static_cast<GBA::core *>(ptr);
    /*if (th->audio.buf) {

        th->audio.next_sample_cycle += th->audio.master_cycles_per_audio_sample;
        th->scheduler.only_add_abs(static_cast<i64>(th->audio.next_sample_cycle), 0, th, &sample_audio, nullptr);
        if (th->audio.buf->upos < (th->audio.buf->samples_len << 1)) {
            th->apu.mix_sample(false);
            static_cast<float *>(th->audio.buf->ptr)[th->audio.buf->upos] = th->apu.output.float_l;
            static_cast<float *>(th->audio.buf->ptr)[th->audio.buf->upos+1] = th->apu.output.float_r;
        }
        th->audio.buf->upos+=2;
    }*/
    th->apu.mix_sample(false);
    th->audio.output_ring->push(th->apu.output.float_l, th->apu.output.float_r);
    th->audio.next_sample_cycle += th->audio.master_cycles_per_audio_sample;
    th->scheduler.only_add_abs(static_cast<i64>(th->audio.next_sample_cycle), 0, th, &sample_audio, nullptr);
}

void GBA::core::set_audio_ring(audio_output_ring *ring)
{
    audio.output_ring = ring;
    // GBA master clock: 16,777,216 Hz.  Audio output: 262,144 Hz.
    // Cycles per sample = 16,777,216 / 262,144 = 64.
    audio.master_cycles_per_audio_sample = (float)MASTER_CYCLES_PER_SECOND / 262144.0f;
}


void GBA::core::schedule_audio_events()
{
#ifndef FOR_DREMACAST
    const u64 now = clock.master_cycle_count;
    if (audio.master_cycles_per_audio_sample > 0) {
        if (audio.next_sample_cycle <= now) audio.next_sample_cycle = now + audio.master_cycles_per_audio_sample;
        scheduler.only_add_abs(static_cast<i64>(audio.next_sample_cycle), 0, this, &sample_audio, nullptr);
    }
    scheduler.only_add_abs(static_cast<i64>(now + 16), 0, this, &tick_APU, nullptr);
#endif
}

void GBA::core::reset_audio_schedule(u64 now)
{
    audio.next_sample_cycle_max = audio.master_cycles_per_max_sample > 0 ? now + audio.master_cycles_per_max_sample : 0;
    audio.next_sample_cycle_min = audio.master_cycles_per_min_sample > 0 ? now + audio.master_cycles_per_min_sample : 0;
    audio.next_sample_cycle = audio.master_cycles_per_audio_sample > 0 ? now + audio.master_cycles_per_audio_sample : 0;
}

void GBA::core::schedule_first()
{
    schedule_audio_events();
    PPU::core::schedule_frame(&ppu, 0, clock.master_cycle_count, 0);
}

void GBA::core::reset()
{
    scheduler.clear();
    waitstates.current_transaction = 0;
    clock.reset();
    EWRAM_cache.reset();
    IWRAM_cache.reset();
    cpu.reset<GBA_CACHED_INTERPRETER>();
    ppu.reset();
    reset_audio_schedule(clock.master_cycle_count);

    for (auto & i : io.SIO.multi) {
        i = 0xFFFF;
    }
    io.SIO.send = 0xFFFF;

    if (jsm.fast_boot) skip_BIOS();

    schedule_first();
    set_step_cpu();
    //printf("\nGBA reset!");
}


u32 GBA::core::finish_scanline()
{
    pre_run();
    bool was_cached = cpu.cached_mode;
    if (was_cached) { cpu.exit_cached_mode(); set_step_cpu(); }
    if (::dbg.do_debug) scheduler.run_til_tag<true>(1);
    else scheduler.run_til_tag<false>(1);
    if (was_cached) { cpu.enter_cached_mode(); set_step_cpu(); }

    post_run();
    return ppu.display->active_draw_buffer;
}

u32 GBA::core::step_master(u32 howmany)
{
    bool was_cached = cpu.cached_mode;
    if (was_cached) { cpu.exit_cached_mode(); set_step_cpu(); }
    pre_run();
    scheduler.run_for_cycles(howmany);
    post_run();
    if (was_cached) { cpu.enter_cached_mode(); set_step_cpu(); }
    return 0;
}

void GBA::core::load_BIOS(multi_file_set &mfs)
{
    memcpy(BIOS.data, mfs.files[0].buf.ptr, 16384);
    BIOS.has = true;
}

static void GBAIO_unload_cart(jsm_system *sys)
{
}

static void GBAIO_load_cart(jsm_system *sys, multi_file_set &mfs, physical_io_device &pio) {
    auto *th = dynamic_cast<GBA::core *>(sys);
    BUF* b = &mfs.files[0].buf;

    u32 r;
    th->cart.load_ROM_from_RAM(static_cast<const char *>(b->ptr), b->size, &pio, &r);
    th->reset();
    th->ROM_store.resize(b->size >> 1);
}

void GBA::core::setup_lcd(JSM_DISPLAY &d)
{
    d.kind = jsm::LCD;
    d.enabled = true;

    // GBA crystal = 2^24 Hz = 16,777,216 Hz; 228 lines × 1232 cycles/line ... ≈59.7275 Hz
    d.fps = 16777216.0 / (228.0 * 1232.0);
    // removed: d.fps_override_hint = 60;
    // 240x160, but 308x228 with v and h blanks

    d.pixelometry.cols.left_hblank = 0;
    d.pixelometry.cols.visible = 240;
    d.pixelometry.cols.max_visible = 240;
    d.pixelometry.cols.right_hblank = 68;
    d.pixelometry.offset.x = 0;

    d.pixelometry.rows.top_vblank = 0;
    d.pixelometry.rows.visible = 160;
    d.pixelometry.rows.max_visible = 160;
    d.pixelometry.rows.bottom_vblank = 68;
    d.pixelometry.offset.y = 0;

    d.geometry.physical_aspect_ratio.width = 3;
    d.geometry.physical_aspect_ratio.height = 2;

    d.pixelometry.overscan.left = d.pixelometry.overscan.right = 0;
    d.pixelometry.overscan.top = d.pixelometry.overscan.bottom = 0;
}

void GBA::core::setup_audio()
{
    physical_io_device &pio = IOs.emplace_back();
    pio.init(HID_AUDIO_CHANNEL, true, true, false, true);
    JSM_AUDIO_CHANNEL *chan = &pio.audio_channel;
    chan->num = 2;
    chan->left = chan->right = true;
    chan->sample_rate = 262144;
    chan->low_pass_filter = 24000;
    chan->mix_volume = 1.0f;
}


void GBA::core::describe_io()
{
    if (jsm.described_inputs) return;
    jsm.described_inputs = true;

    IOs.reserve(15);

    // controllers
    physical_io_device *cnt = &IOs.emplace_back();
    controller.setup_pio(cnt);

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
    d->init(HID_CART_PORT, true, true, true, false);
    d->cartridge_port.load_cart = &GBAIO_load_cart;
    d->cartridge_port.unload_cart = &GBAIO_unload_cart;

    // screen
    d = &IOs.emplace_back();
    d->init(HID_DISPLAY, true, true, false, true);
    d->display.allocate_output(0, 240 * 160 * 2);
    d->display.allocate_output(1, 240 * 160 * 2);
    d->display.output_debug_metadata[0] = nullptr;
    d->display.output_debug_metadata[1] = nullptr;
    setup_lcd(d->display);
    ppu.display_ptr.make(IOs, IOs.size()-1);
    d->display.active_draw_buffer = 0;
    //d->display.last_displayed = 1;
    ppu.cur_output = static_cast<u16 *>(d->display.output[0]);

    setup_audio();

    ppu.display = &ppu.display_ptr.get().display;
}
