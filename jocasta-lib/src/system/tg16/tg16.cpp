//
// Created by . on 6/18/25.
//

#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <cstring>

#include "helpers/physical_io.h"

#include "tg16.h"
#include "tg16_debugger.h"
#include "tg16_bus.h"
#include "tg16_controllerport.h"

#define TAG_SCANLINE 1
#define TAG_FRAME 2
#define DRAW_CYCLES 1108
#define TG16_SAMPLE 48000

#define JTHIS TG16* this = (TG16*)jsm->ptr
#define JSM 

#define THIS TG16* this

u32 read_trace_huc6280(void *ptr, u32 addr) {
    auto *th = static_cast<TG16::core *>(ptr);
    return th->bus_read(addr, th->cpu.pins.D, false);
}

void TG16::core::set_audio_ring(audio_output_ring *ring)
{
    audio.output_ring = ring;
    // TG16 master clock: 21,477,272 Hz NTSC. Declared sample rate: 52,000 Hz.
    // One audio sample every ~413 master clock cycles.
    audio.master_cycles_per_audio_sample = 21477272.0 / 52000.0;
}

void TG16::core::vdc0_update_irqs(void *ptr, bool val)
{
    auto *th = static_cast<TG16::core *>(ptr);
    th->cpu.regs.IRQR.IRQ1 = val;
}

events_view &TG16::core::get_events_view()
{
    return dbg.events.view.get().events;
}

jsm_system *TG16_new(jsm::systems kind)
{
    return new TG16::core(kind);
}

static void set_cdrom_irq2(void *ptr, bool val) {
    auto *th = static_cast<TG16::core *>(ptr);
    th->cpu.regs.IRQR.IRQ2 = val ? 1 : 0;
}

TG16::core::core(jsm::systems kind) : cdrom(this) {
    has.load_BIOS = false;
    has.max_loaded_files = 0;
    has.max_loaded_folders = 0;
    has.save_state = true;
    has.set_audio_ring = true;
    scheduler.max_block_size = 2;
    scheduler.run.ptr = this;

    cpu.read_func = &huc_read_mem;
    cpu.write_func = &huc_write_mem;
    cpu.read_io_func = &huc_read_io;
    cpu.write_io_func = &huc_write_io;
    cpu.read_ptr = cpu.write_ptr = cpu.read_io_ptr = cpu.write_io_ptr = this;
    vdc0.irq.update_func_ptr = this;
    vdc0.irq.update_func = &vdc0_update_irqs;

    is_cd = (kind == jsm::TURBOGRAFX16_CD || kind == jsm::TURBOGRAFX16_ARCADE_CD);
    is_arcade_card = (kind == jsm::TURBOGRAFX16_ARCADE_CD);
    if (is_cd) {
        has.load_BIOS = true;
        cdrom.set_irq2 = &set_cdrom_irq2;
        cdrom.irq_ptr = this;
        if (is_arcade_card)
            snprintf(label, sizeof(label), "TurboGraFX-16 Arcade CD");
        else
            snprintf(label, sizeof(label), "TurboGraFX-16 CD");
    } else {
        snprintf(label, sizeof(label), "TurboGraFX-16");
    }

    jsm_debug_read_trace dt;
    dt.read_trace = &read_trace_huc6280;
    dt.ptr = static_cast<void*>(this);
    cpu.setup_tracing(&dt, &clock.master_cycles, 1);

    jsm.described_inputs = false;
}

void TG16_delete() {
}

static inline float u16_to_float2(u16 val)
{
    assert(val < 47431); // (1F * FF) * 6
    return static_cast<float>(val) / 47500.0f;
}

static inline float ch_to_float(i16 val)
{
    return static_cast<float>(val) / 7906.0f;
}


static inline float u16_to_float(i16 val)
{
    return static_cast<float>(val) / 20000.0f;
}

static inline float i16_to_float(i16 val)
{
    return ((static_cast<float>(static_cast<i32>(val) + 32768) / 65535.0f) * 2.0f) - 1.0f;
}

void TG16::core::sample_psg(void *ptr, u64 key, u64 clock, u32 jitter)
{
    auto *th = static_cast<TG16::core *>(ptr);
    th->audio.cycles++;
    th->audio.next_sample_cycle += th->audio.master_cycles_per_audio_sample;
    th->scheduler.only_add_abs(static_cast<i64>(th->audio.next_sample_cycle), 0, th, &sample_psg, nullptr);
    if (th->audio.output_ring) {
        float l = u16_to_float2(th->cpu.psg.out.l);
        float r = u16_to_float2(th->cpu.psg.out.r);
        th->audio.output_ring->push(l, r);
    }
}

void TG16::core::sample_adpcm(void *ptr, u64 key, u64 clock, u32 jitter)
{
    auto *th = static_cast<TG16::core *>(ptr);
    th->audio.next_adpcm_cycle += th->audio.master_cycles_per_adpcm_sample;
    th->scheduler.only_add_abs(static_cast<i64>(th->audio.next_adpcm_cycle), 0, th, &sample_adpcm, nullptr);
    th->cdrom.tick_adpcm();
}

void TG16::core::sample_cdda(void *ptr, u64 key, u64 clock, u32 jitter)
{
    auto *th = static_cast<TG16::core *>(ptr);
    th->audio.next_cdda_cycle += th->audio.master_cycles_per_cdda_sample;
    th->scheduler.only_add_abs(static_cast<i64>(th->audio.next_cdda_cycle), 0, th, &sample_cdda, nullptr);

    if (!th->cdrom.cdda_ring) return;
    i16 l, r;
    th->cdrom.get_cdda(l, r);
    th->cdrom.cdda_ring->push(static_cast<float>(l) / 32768.0f, static_cast<float>(r) / 32768.0f);
}

void TG16::core::sample_audio_debug_max(void *ptr, u64 key, u64 clock, u32 jitter)
{
    auto *th = static_cast<TG16::core *>(ptr);
    debug_waveform *dw = th->dbg.waveforms_psg.main_cache;
    if (!dw || dw->user.buf_pos >= dw->samples_requested) return;
    u32 a = (th->cpu.psg.out.l + th->cpu.psg.out.r) >> 1;
    static_cast<float *>(dw->buf.ptr)[dw->user.buf_pos] = u16_to_float(a);
    dw->user.buf_pos++;
    th->audio.next_sample_cycle_max += th->audio.master_cycles_per_max_sample;
    th->scheduler.only_add_abs(static_cast<i64>(th->audio.next_sample_cycle_max), 0, th, &sample_audio_debug_max, nullptr);
}

void TG16::core::sample_audio_debug_min(void *ptr, u64 key, u64 clock, u32 jitter)
{
    auto *th = static_cast<TG16::core *>(ptr);
    bool any_remaining = false;
    for (int j = 0; j < 6; j++) {
        debug_waveform *dw = th->dbg.waveforms_psg.chan_cache[j];
        if (dw && dw->user.buf_pos < dw->samples_requested) {
            u16 sv = th->cpu.psg.channels[j].debug_sample();
            static_cast<float *>(dw->buf.ptr)[dw->user.buf_pos] = ch_to_float(sv);
            dw->user.buf_pos++;
            any_remaining = true;
        }
    }
    if (th->is_cd) {
        debug_waveform *dw = th->dbg.waveforms_cd.main_cache;
        if (dw && dw->user.buf_pos < dw->samples_requested) {
            float s = (static_cast<float>(th->cdrom.cdda.last_l) + static_cast<float>(th->cdrom.cdda.last_r)) * 0.5f / 32768.0f;
            static_cast<float *>(dw->buf.ptr)[dw->user.buf_pos] = s;
            dw->user.buf_pos++;
            any_remaining = true;
        }
        debug_waveform *da = th->dbg.waveforms_cd.chan_cache[0];
        if (da && da->user.buf_pos < da->samples_requested) {
            i16 out = static_cast<i16>((th->cdrom.adpcm.current_output - 2048) * 10);
            static_cast<float *>(da->buf.ptr)[da->user.buf_pos] = static_cast<float>(out) / 32768.0f;
            da->user.buf_pos++;
            any_remaining = true;
        }
    }
    if (!any_remaining) return;
    th->audio.next_sample_cycle_min += th->audio.master_cycles_per_min_sample;
    th->scheduler.only_add_abs(static_cast<i64>(th->audio.next_sample_cycle_min), 0, th, &sample_audio_debug_min, nullptr);
}


static void TG16IO_insert_disc(jsm_system *ptr, physical_io_device &pio, multi_file_set &mfs) {
    auto *th = static_cast<TG16::core *>(ptr);
    th->cdrom.insert_disc(mfs);
}

static void TG16IO_remove_disc(jsm_system *ptr) {
    auto *th = static_cast<TG16::core *>(ptr);
    th->cdrom.remove_disc();
}

static void TG16IO_open_drive(jsm_system *ptr) {
    auto *th = static_cast<TG16::core *>(ptr);
    th->cdrom.open_drive();
}

static void TG16IO_close_drive(jsm_system *ptr) {
    auto *th = static_cast<TG16::core *>(ptr);
    th->cdrom.close_drive();
}

static void TG16IO_load_cart(jsm_system *ptr, multi_file_set& mfs, physical_io_device &whichpio)
{
    // 512kb usless header
    // check if bit 9 is set and discard first 512kb then
    BUF* b = &mfs.files[0].buf;

    auto *th = static_cast<TG16::core *>(ptr);
    u8 *bptr = static_cast<u8 *>(b->ptr);
    u64 sz = b->size;
    if (sz & 512) {
        bptr += 512;
        sz -= 512;
    }

   th->cart.load_ROM_from_RAM(bptr, sz, whichpio);
   th->reset();
}

static void TG16IO_unload_cart(jsm_system *ptr)
{
}

void TG16::core::setup_crt(JSM_DISPLAY &d)
{
    d.kind = jsm::CRT;
    d.enabled = true;

    d.fps = 21477272.0 / (1364.0 * 262.0);
    // removed: d.fps_override_hint = clock.timing.second.frames;

    d.pixelometry.cols.left_hblank = 16;
    d.pixelometry.cols.visible = HUC6260::CYCLE_PER_LINE;
    d.pixelometry.cols.max_visible = HUC6260::CYCLE_PER_LINE;
    d.pixelometry.cols.right_hblank = 221;
    d.pixelometry.offset.x = 0;

    d.pixelometry.rows.top_vblank = 0;
    d.pixelometry.rows.visible = 242;
    d.pixelometry.rows.max_visible = 242;
    d.pixelometry.rows.bottom_vblank = 20;
    d.pixelometry.offset.y = 0;

    d.geometry.physical_aspect_ratio.width = 4;
    d.geometry.physical_aspect_ratio.height = 3;

    d.pixelometry.overscan.left = 192;
    d.pixelometry.overscan.right = 45;
    d.pixelometry.overscan.top = d.pixelometry.overscan.bottom = 0;
}

void TG16::core::setup_audio()
{
    physical_io_device *pio = &IOs.emplace_back();
    pio->init(HID_AUDIO_CHANNEL, true, true, false, true);
    JSM_AUDIO_CHANNEL *chan = &pio->audio_channel;
    chan->sample_rate = 52000;
    chan->left = chan->right = 1;
    chan->num = 2;
    chan->low_pass_filter = 24000;

    if (is_cd) {
        pio = &IOs.emplace_back();
        pio->init(HID_AUDIO_CHANNEL, true, true, false, true);
        chan = &pio->audio_channel;
        chan->sample_rate = 128000;
        chan->left = chan->right = 1;
        chan->num = 2;
        chan->mix_volume = 1.0f;

        pio = &IOs.emplace_back();
        pio->init(HID_AUDIO_CHANNEL, true, true, false, true);
        chan = &pio->audio_channel;
        chan->sample_rate = 44100;
        chan->left = chan->right = 1;
        chan->num = 2;
        chan->mix_volume = 1.0f;
    }
}

void TG16::core::audio_rings_ready()
{
    u32 found = 0;
    for (auto &pio : IOs) {
        if (pio.kind != HID_AUDIO_CHANNEL) continue;
        if (found == 0) audio.output_ring = pio.audio_channel.ring;
        else if (found == 1) cdrom.adpcm_ring = pio.audio_channel.ring;
        else if (found == 2) cdrom.cdda_ring = pio.audio_channel.ring;
        if (++found == 3) break;
    }
    if (is_cd) {
        audio.master_cycles_per_cdda_sample = 21477272.0 / 44100.0;
        audio.master_cycles_per_adpcm_sample = 21477272.0 / 128000.0;
    }
}

void TG16::core::describe_io()
{
    if (jsm.described_inputs) return;
    jsm.described_inputs = true;
    IOs.reserve(15);

    // controllers
    physical_io_device *c1 = &IOs.emplace_back();
    controller.setup_pio(*c1, 0, "Player 1", 1);

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

    if (!is_cd) {
        physical_io_device *d = &IOs.emplace_back();
        d->init(HID_CART_PORT, true, true, true, true);
        d->cartridge_port.load_cart = &TG16IO_load_cart;
        d->cartridge_port.unload_cart = &TG16IO_unload_cart;
    } else {
        physical_io_device *d = &IOs.emplace_back();
        d->init(HID_DISC_DRIVE, true, true, true, false);
        cdrom.pio_ptr.make(IOs, IOs.size() - 1);
        cdrom.dd = &d->disc_drive;
        cdrom.dd->insert_disc = &TG16IO_insert_disc;
        cdrom.dd->remove_disc = &TG16IO_remove_disc;
        cdrom.dd->open_drive = &TG16IO_open_drive;
        cdrom.dd->close_drive = &TG16IO_close_drive;

        physical_io_device *bram_pio = &IOs.emplace_back();
        bram_pio->init(HID_NVRAM, true, true, true, true);
        snprintf(bram_pio->nvram.label, sizeof(bram_pio->nvram.label), "bram");
        bram_pio->nvram.store.requested_size = 0x800;
        bram_pio->nvram.store.fill_value = 0;
        bram_pio->nvram.store.persistent = true;
        bram_store = &bram_pio->nvram.store;
    }

    // screen
    physical_io_device *d = &IOs.emplace_back();
    d->init(HID_DISPLAY, true, true, false, true);
    d->display.allocate_output(0, HUC6260::CYCLE_PER_LINE * 480 * 2);
    d->display.allocate_output(1, HUC6260::CYCLE_PER_LINE * 480 * 2);
    d->display.output_debug_metadata[0] = nullptr;
    d->display.output_debug_metadata[1] = nullptr;
    setup_crt(d->display);
    vce.display_ptr.make(IOs, IOs.size()-1);
    d->display.active_draw_buffer = 0;
    vce.cur_output = static_cast<u16 *>(d->display.output[0]);
    vce.display = &vce.display_ptr.get().display;

    setup_audio();

    vce.display = &vce.display_ptr.get().display;
    controller_port.connect(TG16::controller_kinds::CK_2button, &controller);
}

void TG16::core::play()
{
}

void TG16::core::pause()
{
}

void TG16::core::stop()
{
}

#define PSG_CYCLES 6

void TG16::core::schedule_first()
{
    cpu.schedule_first(0);
    vce.schedule_first();
#ifndef FOR_DREAMCAST
    scheduler.only_add_abs(PSG_CYCLES, 0, this, &psg_go, nullptr);
    scheduler.only_add_abs(static_cast<i64>(audio.next_sample_cycle), 0, this, &sample_psg, nullptr);
    if (is_cd) {
        if (audio.next_cdda_cycle == 0)
            audio.next_cdda_cycle = audio.master_cycles_per_cdda_sample;
        scheduler.only_add_abs(static_cast<i64>(audio.next_cdda_cycle), 0, this, &sample_cdda, nullptr);

        if (audio.next_adpcm_cycle == 0)
            audio.next_adpcm_cycle = audio.master_cycles_per_adpcm_sample;
        scheduler.only_add_abs(static_cast<i64>(audio.next_adpcm_cycle), 0, this, &sample_adpcm, nullptr);

        cdrom.reschedule_pending_events();
    }
    // Debug waveform events are armed per-frame in finish_frame() when do_debug is true.
#endif
}

void TG16::core::psg_go(void *ptr, u64 key, u64 clock, u32 jitter)
{
     auto *th = static_cast<TG16::core *>(ptr);
    u64 cur = clock - jitter;
    th->cpu.psg.cycle();

    th->scheduler.only_add_abs(cur + PSG_CYCLES, 0, th, &psg_go, nullptr);
}

void TG16::core::get_framevars(framevars& out)
{
    out.master_frame = vce.master_frame;
    out.x = 0;
    out.scanline = vce.regs.y - 64;
    out.master_cycle = clock.master_cycles;
}

void TG16::core::reset()
{
    clock.reset();
    cpu.reset();
    vdc0.reset();
    vdc1.reset();
    vce.reset();
    cart.reset();
    if (is_cd) cdrom.reset();

    scheduler.clear();
    schedule_first();
}

void TG16::core::killall() {
    scheduler.clear();
    schedule_first();
}

//#define DO_STATS

void TG16::core::fixup_audio()
{
    if (!dbg.waveforms_psg.main_cache) {
        dbg.waveforms_psg.main_cache = &dbg.waveforms_psg.main.get();
        for (u32 i = 0; i < 6; i++) {
            dbg.waveforms_psg.chan_cache[i] = &dbg.waveforms_psg.chan[i].get();
        }
    }
    if (is_cd && !dbg.waveforms_cd.main_cache) {
        dbg.waveforms_cd.main_cache = &dbg.waveforms_cd.main.get();
        dbg.waveforms_cd.chan_cache[0] = &dbg.waveforms_cd.chan[0].get();
    }
}

u32 TG16::core::finish_frame()
{
    fixup_audio();

    if (::dbg.do_debug && dbg.waveforms_psg.main.vec != nullptr) {
        const u64 now = clock.master_cycles;

        auto *wf = &dbg.waveforms_psg.main.get();
        wf->setup(vce.regs.cycles_per_frame);
        cpu.psg.ext_enable = wf->ch_output_enabled;
        if (wf->samples_requested > 0) {
            audio.master_cycles_per_max_sample =
                static_cast<f64>(vce.regs.cycles_per_frame) / static_cast<f64>(wf->samples_requested);
            audio.next_sample_cycle_max = static_cast<f64>(now) + audio.master_cycles_per_max_sample;
            scheduler.only_add_abs(static_cast<i64>(audio.next_sample_cycle_max), 0, this, &sample_audio_debug_max, nullptr);
        }

        u32 chan_samples = dbg.waveforms_psg.chan[0].get().samples_requested;
        if (chan_samples > 0) {
            audio.master_cycles_per_min_sample =
                static_cast<f64>(vce.regs.cycles_per_frame) / static_cast<f64>(chan_samples);
            audio.next_sample_cycle_min = static_cast<f64>(now) + audio.master_cycles_per_min_sample;
            scheduler.only_add_abs(static_cast<i64>(audio.next_sample_cycle_min), 0, this, &sample_audio_debug_min, nullptr);
        }
        for (u32 i = 0; i < 6; i++) {
            auto *cw = &dbg.waveforms_psg.chan[i].get();
            cw->setup(vce.regs.cycles_per_frame);
            cpu.psg.channels[i].ext_enable = cw->ch_output_enabled;
        }
        if (is_cd && dbg.waveforms_cd.main.vec != nullptr) {
            dbg.waveforms_cd.main.get().setup(vce.regs.cycles_per_frame);
            dbg.waveforms_cd.chan[0].get().setup(vce.regs.cycles_per_frame);
        }
    }

#ifdef TG16_LYCODER2
    dbg.do_debug = 1;
#endif
    scheduler.run_til_tag_tg16<true>(TAG_FRAME);
#ifdef TG16_LYCODER2
    dbg_flush();
#endif

    return vce.display->active_draw_buffer;
}

#define MIN(x,y) ((x) < (y) ? (x) : (y))
u32 TG16::core::finish_scanline()
{
    fixup_audio();
    scheduler.run_til_tag_tg16<true>(TAG_SCANLINE);

    return vce.display->active_draw_buffer;
}

u32 TG16::core::step_master(u32 howmany)
{
    fixup_audio();
    scheduler.run_for_cycles_tg16<true>(howmany);
    return 0;
}

void TG16::core::load_BIOS(multi_file_set& mfs)
{
    if (!is_cd) { printf("\nTG16 non-CD got a BIOS?"); return; }
    if (mfs.files.empty() || !mfs.files[0].buf.ptr || mfs.files[0].buf.size == 0) {
        printf("\nTG16CD: ERROR - BIOS MFS has no file data! files=%zu", mfs.files.size());
        return;
    }
    BUF *b = &mfs.files[0].buf;
    u8 *ptr = static_cast<u8 *>(b->ptr);
    u64 sz = b->size;
    if (sz > BIOS_MAX) sz = BIOS_MAX;
    memcpy(BIOS, ptr, sz);
    bios_size = static_cast<u32>(sz);
}
