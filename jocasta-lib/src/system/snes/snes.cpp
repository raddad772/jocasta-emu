//
// Created by . on 2/11/25.
//

#include <cassert>
#include <cstdlib>

#include "snes.h"
#include "snes_debugger.h"
#include "snes_bus.h"
#include "snes_cart.h"
#include "snes_ppu.h"
#include "snes_apu.h"
#include "r5a22.h"

#define TAG_SCANLINE 1
#define TAG_FRAME 2


#define JSM jsm_system* jsm

SNES::core::core() : r5a22(this, &this->clock.master_cycle_count), apu(this, &clock.master_cycle_count), scheduler(&clock.master_cycle_count), cart(this), ppu(this), mem(this) {
    has.save_state = false;
    has.load_BIOS = false;
    has.set_audio_ring = true;
}

u32 read_trace_wdc65816(void *ptr, u32 addr) {
    return static_cast<SNES::core *>(ptr)->mem.read_bus_A(addr, 0, 0);
}

void SNES::core::set_audio_ring(audio_output_ring *ring)
{
    audio.output_ring = ring;
    // SNES SPC700 DSP outputs at 32000 Hz; sample_audio() fires via apu.dsp.sample callback
}

template<bool debug>
static void run_block(void *ptr, u64 key, u64 clock, u32 jitter)
{
    auto *th = static_cast<SNES::core *>(ptr);
    SNES::R5A22::cycle(th, 0, 0, 0);
}

static inline float i16_to_float(i16 val)
{
    return ((static_cast<float>(static_cast<i32>(val) + 32768) / 65535.0f) * 2.0f) - 1.0f;
}


static void sample_audio(void *ptr, u64 key, u64 clock, u32 jitter)
{
    auto* th = static_cast<SNES::core *>(ptr);
    th->audio.cycles++;
    if (th->audio.output_ring) {
        th->audio.output_ring->push(
            i16_to_float(static_cast<i16>(th->apu.dsp.output.l)),
            i16_to_float(static_cast<i16>(th->apu.dsp.output.r))
        );
    }
}

jsm_system *SNES_new()
{
    auto* th = new SNES::core();

    th->scheduler.max_block_size = 20;
    th->scheduler.run.func[0] = &run_block<false>;
    th->scheduler.run.func[1] = &run_block<true>;
    th->scheduler.run.ptr = th;

    th->apu.dsp.sample.func = &sample_audio;
    th->apu.dsp.sample.ptr = th;

    snprintf(th->label, sizeof(th->label), "SNES");

    jsm_debug_read_trace dt;
    dt.read_trace = &read_trace_wdc65816;
    dt.ptr = static_cast<void *>(th);
    th->r5a22.setup_tracing(&dt);

    th->apu.cpu.setup_tracing(&dt);
    return th;
}

void SNES_delete(JSM) {
    SNES::core *snes = dynamic_cast<SNES::core *>(jsm);

    for (physical_io_device &pio : snes->IOs) {
        if (pio.kind == HID_CART_PORT) {
            if (pio.cartridge_port.unload_cart) pio.cartridge_port.unload_cart(jsm);
        }
    }
    snes->IOs.clear();

    delete snes;
}

static void sample_audio_debug_max(void *ptr, u64 key, u64 clock, u32 jitter)
{
    auto *th = static_cast<SNES::core *>(ptr);
    debug_waveform *dw = &th->dbg.waveforms.main.get();
    if (dw->user.buf_pos >= dw->samples_requested) return; // buffer full — stop
    const i32 smp = (th->apu.dsp.output.l + th->apu.dsp.output.r) >> 1;
    static_cast<float *>(dw->buf.ptr)[dw->user.buf_pos] = i16_to_float(static_cast<i16>(smp));
    dw->user.buf_pos++;
    th->audio.next_sample_cycle_max += th->audio.master_cycles_per_max_sample;
    th->scheduler.only_add_abs(static_cast<i64>(th->audio.next_sample_cycle_max), 0, th, &sample_audio_debug_max, nullptr);
}

static void sample_audio_debug_min(void *ptr, u64 key, u64 clock, u32 jitter)
{
    auto *th = static_cast<SNES::core *>(ptr);
    bool any_remaining = false;
    for (int j = 0; j < 8; j++) {
        debug_waveform *dw = &th->dbg.waveforms.chan[j].get();
        if (dw->user.buf_pos < dw->samples_requested) {
            i16 sv = th->apu.dsp.channel[j].output.debug;
            static_cast<float *>(dw->buf.ptr)[dw->user.buf_pos] = i16_to_float(sv);
            dw->user.buf_pos++;
            any_remaining = true;
        }
    }
    if (!any_remaining) return; // all buffers full — stop
    th->audio.next_sample_cycle_min += th->audio.master_cycles_per_min_sample;
    th->scheduler.only_add_abs(static_cast<i64>(th->audio.next_sample_cycle_min), 0, th, &sample_audio_debug_min, nullptr);
}


void SNES::core::schedule_audio_events()
{
    const u64 now = clock.master_cycle_count;
    if (audio.master_cycles_per_max_sample > 0) {
        if (audio.next_sample_cycle_max <= now) audio.next_sample_cycle_max = now + audio.master_cycles_per_max_sample;
        scheduler.only_add_abs(static_cast<i64>(audio.next_sample_cycle_max), 0, this, &sample_audio_debug_max, nullptr);
    }
    if (audio.master_cycles_per_min_sample > 0) {
        if (audio.next_sample_cycle_min <= now) audio.next_sample_cycle_min = now + audio.master_cycles_per_min_sample;
        scheduler.only_add_abs(static_cast<i64>(audio.next_sample_cycle_min), 0, this, &sample_audio_debug_min, nullptr);
    }
    //scheduler_only_add_abs((i64)audio.next_sample_cycle, 0, th, &sample_audio, nullptr);
}

void SNES::core::reset_audio_schedule(u64 now)
{
    audio.cycles = 0;
    audio.next_debug_cycle = 0;
    audio.next_sample_cycle_max = audio.master_cycles_per_max_sample > 0 ? now + audio.master_cycles_per_max_sample : 0;
    audio.next_sample_cycle_min = audio.master_cycles_per_min_sample > 0 ? now + audio.master_cycles_per_min_sample : 0;
    audio.next_sample_cycle = audio.master_cycles_per_audio_sample > 0 ? now + audio.master_cycles_per_audio_sample : 0;
}

void SNES::core::schedule_first()
{
    schedule_audio_events();
    apu.schedule_first();
    ppu.schedule_first();
    r5a22.schedule_first();
}

static void SNESIO_load_cart(JSM, multi_file_set &mfs, physical_io_device &which_pio)
{
    auto *snes = dynamic_cast<SNES::core *>(jsm);
    BUF* b = &mfs.files[0].buf;

    snes->cart.load_ROM_from_RAM(static_cast<char *>(b->ptr), b->size, which_pio);
    snes->reset();
}

static void SNESIO_unload_cart(JSM)
{
}

static void setup_crt(JSM_DISPLAY *d)
{
    d->kind = jsm::display_kinds::CRT;
    d->enabled = 1;

    // NTSC crystal 21.477270 MHz, 1364 master cycles/line × 262 lines ... ≈60.0985 Hz
    d->fps = 21477270.0 / (1364.0 * 262.0);
    // removed: d->fps_override_hint = 60;

    d->pixelometry.cols.left_hblank = 42;
    d->pixelometry.cols.visible = 512;
    d->pixelometry.cols.max_visible = 512;
    d->pixelometry.cols.right_hblank = 42;
    d->pixelometry.offset.x = 0;

    d->pixelometry.rows.top_vblank = 0;
    d->pixelometry.rows.visible = 224;
    d->pixelometry.rows.max_visible = 224;
    d->pixelometry.rows.bottom_vblank = 38;
    d->pixelometry.offset.y = 0;

    d->geometry.physical_aspect_ratio.width = 4;
    d->geometry.physical_aspect_ratio.height = 3;

    d->pixelometry.overscan.left = d->pixelometry.overscan.right = 0;
    d->pixelometry.overscan.top = d->pixelometry.overscan.bottom = 0;
}

static void setup_audio(std::vector<physical_io_device> &inIOs)
{
    physical_io_device &pio = inIOs.emplace_back();
    pio.init(HID_AUDIO_CHANNEL, true, true, false, true);
    JSM_AUDIO_CHANNEL *chan = &pio.audio_channel;
    chan->num = 2;
    chan->left = chan->right = 1;
    chan->sample_rate = 32000;
    //chan->sample_rate = 64000;
    chan->low_pass_filter = 16000;
}

void SNES::core::describe_io()
{
    if (jsm.described_inputs) return;
    jsm.described_inputs = 1;

    IOs.reserve(15);

    // controllers
    physical_io_device &c1 = IOs.emplace_back();
    physical_io_device &c2 = IOs.emplace_back();
    c1.init(HID_CONTROLLER, 1, 1, 1, 1);
    c2.init(HID_CONTROLLER, 1, 1, 1, 1);
    SNES_joypad::setup_pio(c1, 0, "Player 1", 1);
    controller1.pio_ptr.make(IOs, IOs.size()-2);
    controller2.pio_ptr.make(IOs, IOs.size()-1);

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
    d->cartridge_port.load_cart = &SNESIO_load_cart;
    d->cartridge_port.unload_cart = &SNESIO_unload_cart;

    // screen
    d = &IOs.emplace_back();
    d->init(HID_DISPLAY, 1, 1, 0, 1);
    d->display.allocate_output(0, 512 * 224 * 2);
    d->display.allocate_output(1, 512 * 224 * 2);
    d->display.output_debug_metadata[0] = nullptr;
    d->display.output_debug_metadata[1] = nullptr;
    setup_crt(&d->display);
    ppu.display_ptr.make(IOs, IOs.size()-1);
    d->display.active_draw_buffer = 0;
    ppu.cur_output = static_cast<u16 *>(d->display.output[0]);

    setup_audio(IOs);

    ppu.display = &ppu.display_ptr.get().display;
    r5a22.controller_port1.connect(controller::kinds::standard, &controller1);
    //r5a22.controller_port2.connect(controller_kinds::standard, &controller2);
}

void SNES::core::play()
{
}

void SNES::core::pause()
{
}

void SNES::core::stop()
{
}

void SNES::core::get_framevars(framevars& out)
{
    
    out.master_frame = clock.master_frame;
    const u64 line_cycles = clock.master_cycle_count >= clock.ppu.scanline_start ? clock.master_cycle_count - clock.ppu.scanline_start : 0;
    out.x = static_cast<i32>(line_cycles >> 2) - 21;
    out.scanline = clock.ppu.y;
    out.master_cycle = clock.master_cycle_count;
}

void SNES::core::reset()
{
    scheduler.clear();
    clock.reset();
    r5a22.reset();
    apu.reset();
    ppu.reset();
    reset_audio_schedule(clock.master_cycle_count);

    schedule_first();
    //printf("\nSNES reset!");
}

//#define DO_STATS

u32 SNES::core::finish_frame()
{
#ifdef DO_STATS
    u64 spc_start = apu.cpu.int_clock;
    u64 wdc_start = r5a22.cpu.int_clock;
#endif

    if (::dbg.do_debug && dbg.waveforms.main.vec != nullptr) {
        const u64 now = clock.master_cycle_count;

        // Reset main waveform buffer and arm sampler
        debug_waveform *wf = &dbg.waveforms.main.get();
        wf->setup(clock.timing.frame.master_cycles);
        apu.dsp.ext_enable = wf->ch_output_enabled;
        if (wf->samples_requested > 0) {
            audio.master_cycles_per_max_sample = static_cast<double>(clock.timing.frame.master_cycles) / static_cast<double>(wf->samples_requested);
            if (audio.next_sample_cycle_max <= static_cast<double>(now)) audio.next_sample_cycle_max = static_cast<double>(now) + audio.master_cycles_per_max_sample;
            scheduler.only_add_abs(static_cast<i64>(audio.next_sample_cycle_max), 0, this, &sample_audio_debug_max, nullptr);
        }

        // Reset channel waveform buffers and arm sampler
        for (u32 i = 0; i < 8; i++) {
            wf = &dbg.waveforms.chan[i].get();
            wf->setup(clock.timing.frame.master_cycles);
            apu.dsp.channel[i].ext_enable = wf->ch_output_enabled;
        }
        wf = &dbg.waveforms.chan[0].get();
        if (wf->samples_requested > 0) {
            audio.master_cycles_per_min_sample = static_cast<double>(clock.timing.frame.master_cycles) / static_cast<double>(wf->samples_requested);
            if (audio.next_sample_cycle_min <= static_cast<double>(now)) audio.next_sample_cycle_min = static_cast<double>(now) + audio.master_cycles_per_min_sample;
            scheduler.only_add_abs(static_cast<i64>(audio.next_sample_cycle_min), 0, this, &sample_audio_debug_min, nullptr);
        }
    }

    if (::dbg.do_debug) scheduler.run_til_tag<true>(TAG_FRAME);
    else scheduler.run_til_tag<false>(TAG_FRAME);
#ifdef DO_STATS
    u64 spc_num_cycles = (apu.cpu.int_clock - spc_start) * 60;
    u64 wdc_num_cycles = (r5a22.cpu.int_clock - wdc_start) * 60;
    double spc_div = (double)clock.timing.frame.master_cycles / (double)spc_num_cycles;
    double wdc_div = (double)clock.timing.frame.master_cycles / (double)wdc_num_cycles;
    printf("\nSCANLINE:%d FRAME:%lld", clock.ppu.y, clock.master_frame);
    printf("\n\nEFFECTIVE 65816 FREQ IS %lld. RUNNING AT SPEED",wdc_num_cycles);
    printf("\nEFFECTIVE SPC700 FREQ IS %lld. RUNNING AT SPEED", spc_num_cycles);

#endif
    return ppu.display->active_draw_buffer;
}

u32 SNES::core::finish_scanline()
{
    //read_opts(jsm, th);
    if (::dbg.do_debug) scheduler.run_til_tag<true>(TAG_SCANLINE);
    else scheduler.run_til_tag<false>(TAG_SCANLINE);

    return ppu.display->active_draw_buffer;
}

u32 SNES::core::step_master(u32 howmany)
{
    //read_opts(jsm, th);
    //printf("\nRUN FOR %d CYCLES:", howmany);
    //u64 cur = clock.master_cycle_count;
    scheduler.run_for_cycles(howmany);
    //u64 dif = clock.master_cycle_count - cur;
    //printf("\nRAN %lld CYCLES", dif);
    return 0;
}

void SNESJ_load_BIOS(JSM, multi_file_set* mfs)
{
    printf("\nSNES doesn't have a BIOS...?");
}
