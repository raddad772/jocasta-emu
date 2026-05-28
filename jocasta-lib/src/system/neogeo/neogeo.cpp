//
// Created by . on 5/11/26.
//
#include <cassert>
#include <cctype>

#include "neogeo.h"
#include "ng_bus.h"


jsm_system *neogeo_new(jsm::systems variant) {
    return new NEOGEO::core(variant);
}

void neogeo_delete(jsm_system *sys) {
    auto *th = dynamic_cast<NEOGEO::core *>(sys);
    for (auto &pio : th->IOs) {
        if (pio.kind == HID_CART_PORT) {
            if (pio.cartridge_port.unload_cart) pio.cartridge_port.unload_cart(sys);
        }
    }
}

namespace NEOGEO {
static void generate_LO_ROM(u8 *out)
{
    u8 byte_present[256]{};
    const u64 vbits = 0xF7B3D591E6A2C480ULL;

    for (u32 i = 0; i < (64 * 1024); i++) out[i] = 0xFF;

    // Generated using Ares' Neo Geo LSPC vscale method, which creates a bit-exact equivalent.
    // Many thanks to the Ares maintainers for sauce
    for (u32 y = 0; y < 256; y++) {
        u8 upper = (vbits >> (((y & 15) ^ 1) * 4)) & 15;
        u8 lower = (vbits >> (((y >> 4) ^ 1) * 4)) & 15;

        byte_present[(upper << 4) | lower] = 1;

        u32 x = 0;
        for (u32 b = 0; b < 256; b++) {
            if (byte_present[b]) out[(y << 8) | x++] = b;
        }
    }
}

void core::set_audio_ring(audio_output_ring *ring)
{
    ym2610.output_ring = ring;
}


void core::populate_opts()
{

}

void core::read_opts()
{

}

static inline float i16_to_float(i16 val)
{
    if (val == 0) return 0.0f;
    return static_cast<float>(val) / 32768.0f;
}

static void sample_audio_debug_max(void *ptr, u64 key, u64 clock_val, u32 jitter)
{
    auto *th = static_cast<core *>(ptr);
    if (key != th->audio.debug_generation) return;
    auto *dw = th->dbg.waveforms2.main_cache;
    if (!debug::waveform2::wf_push_stereo(dw, i16_to_float(static_cast<i16>(th->ym2610.mix.left_output)), i16_to_float(static_cast<i16>(th->ym2610.mix.right_output)))) {
        return;
    }
    th->audio.next_sample_cycle_max += th->audio.master_cycles_per_max_sample;
    th->scheduler.only_add_abs(static_cast<i64>(th->audio.next_sample_cycle_max), key, th, &sample_audio_debug_max, nullptr);
}

static void sample_audio_debug_min(void *ptr, u64 key, u64 clock_val, u32 jitter)
{
    auto *th = static_cast<core *>(ptr);
    if (key != th->audio.debug_generation) return;
    bool any_remaining = false;

    for (u32 i = 0; i < YM2610::core::NUM_FM_CHANNELS; i++) {
        auto *dc = th->dbg.waveforms2.fm.chan_cache[i];
        if (debug::waveform2::wf_push_mono(dc, i16_to_float(static_cast<i16>(th->ym2610.sample_channel(i) << 2)))) {
            any_remaining = true;
        }
    }
    for (u32 i = 0; i < 6; i++) {
        auto *dc = th->dbg.waveforms2.adpcm_a.chan_cache[i];
        if (dc && dc->user.buf_pos < dc->samples_requested) {
            i32 acc = th->ym2610.adpcm_a.ch[i].accumulator;
            if (acc & 0x800) acc |= ~0xFFF;
            float sample = acc == 0 ? 0.0f : static_cast<float>(acc) / 2048.0f;
            if (debug::waveform2::wf_push_mono(dc, sample)) {
                any_remaining = true;
            }
        }
    }
    auto *dc = th->dbg.waveforms2.adpcm_b.chan_cache[0];
    if (debug::waveform2::wf_push_mono(dc, i16_to_float(static_cast<i16>(th->ym2610.adpcm_b.accumulator)))) {
        any_remaining = true;
    }
    for (u32 i = 0; i < 3; i++) {
        dc = th->dbg.waveforms2.ssg.chan_cache[i];
        i32 sample = th->ym2610.ssg.sample_channel(static_cast<int>(i));
        if (debug::waveform2::wf_push_mono(dc, static_cast<float>(sample) / 8192.0f)) {
            any_remaining = true;
        }
    }

    if (!any_remaining) return;
    th->audio.next_sample_cycle_min += th->audio.master_cycles_per_min_sample;
    th->scheduler.only_add_abs(static_cast<i64>(th->audio.next_sample_cycle_min), key, th, &sample_audio_debug_min, nullptr);
}

static void neogeoIO_load_cart(jsm_system *sys, multi_file_set &mfs, physical_io_device &which_pio)
{
    auto *th = dynamic_cast<core *>(sys);

    th->cart.load(mfs, which_pio);
}

static void neogeoIO_unload_cart(jsm_system *sys)
{
}

void core::setup_crt(JSM_DISPLAY &d)
{
    d.kind = jsm::CRT;
    d.enabled = true;

    // fps derived from crystal / (pixel_div * pixels/line * lines/frame)
    // MVS: 24.000 MHz ... 59.1856 Hz,  AES: 24.167829 MHz ... 59.599 Hz
    d.fps = clock.fps;
    // removed: d.fps_override_hint = 60;

    d.pixelometry.cols.left_hblank = 0; // 0
    d.pixelometry.cols.visible = 320;  // 320x224
    d.pixelometry.cols.max_visible = 320;
    d.pixelometry.cols.right_hblank = 54;
    d.pixelometry.offset.x = 0;

    d.pixelometry.rows.top_vblank = 0;
    d.pixelometry.rows.visible = 224;
    d.pixelometry.rows.max_visible = 224;
    d.pixelometry.rows.bottom_vblank = 38; // TODO: update these for PAL. they're currently not really used
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
    chan->sample_rate = clock.cycles_per_second / YM2610_FM_DIV;
    chan->left = chan->right = 1;
    chan->low_pass_filter = 24000;
    chan->mix_volume = 1.0f;

    pio = &IOs.emplace_back();
    pio->init(HID_AUDIO_CHANNEL, true, true, false, true);
    chan = &pio->audio_channel;
    chan->num = 2;
    chan->sample_rate = clock.cycles_per_second / YM2610_SSG_SAMPLE_DIV;
    chan->left = chan->right = 1;
    chan->low_pass_filter = 8000;
    chan->mix_volume = 1.0f;
}

void core::audio_rings_ready()
{
    u32 found = 0;
    for (auto &pio : IOs) {
        if (pio.kind != HID_AUDIO_CHANNEL) continue;
        if (found == 0)
            ym2610.output_ring = pio.audio_channel.ring;
        else if (found == 1)
            audio.ssg_ring = pio.audio_channel.ring;
        if (++found == 2) break;
    }
}

void core::describe_io()
{
    if (described_inputs) return;
    described_inputs = true;
    IOs.reserve(15);

    // controllers
    physical_io_device *c1 = &IOs.emplace_back();
    controller1.setup_pio(c1, 0, "Player 1", 1);

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
    d->cartridge_port.load_cart = &neogeoIO_load_cart;
    d->cartridge_port.unload_cart = &neogeoIO_unload_cart;

    // screen
    d = &IOs.emplace_back();
    d->init(HID_DISPLAY, true, true, false, true);
    d->display.allocate_output(0, 320 * 224 * 4);
    d->display.allocate_output(1, 320 * 224 * 4);
    d->display.output_debug_metadata[0] = nullptr;
    d->display.output_debug_metadata[1] = nullptr;
    setup_crt(d->display);
    lpsc.display_ptr.make(IOs, IOs.size()-1);
    d->display.active_draw_buffer = 0;
    lpsc.cur_output = static_cast<u32 *>(d->display.output[0]);

    setup_audio();

    lpsc.display = &lpsc.display_ptr.get().display;
    controllerport1.connect(CK_4BUTTON, &controller1);
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
    out.master_frame = lpsc.frame_num;
    out.x = 0;
    out.scanline = lpsc.raster_line_counter;
    out.master_cycle = master_clock;
}


u32 core::finish_frame()
{
    read_opts();
    audio.debug_generation++;
#ifdef DO_STATS
    samples_pushed = 0;
#endif

    if (::dbg.do_debug && dbg.waveforms2.main_cache) {
        const u64 now = master_clock;
        const u64 debug_generation = audio.debug_generation;
        u32 min_samples_requested = 0;
        bool any_solo = false;
        auto note_solo = [&any_solo](debug::waveform2::wf *wf) {
            if (wf && wf->ch_output_solo) any_solo = true;
        };

        for (u32 i = 0; i < 4; i++) note_solo(dbg.waveforms2.fm.chan_cache[i]);
        for (u32 i = 0; i < 6; i++) note_solo(dbg.waveforms2.adpcm_a.chan_cache[i]);
        note_solo(dbg.waveforms2.adpcm_b.chan_cache[0]);
        for (u32 i = 0; i < 3; i++) note_solo(dbg.waveforms2.ssg.chan_cache[i]);
        audio.nosolo = !any_solo;

        auto channel_enabled = [this](debug::waveform2::wf *wf) {
            return debug::waveform2::wf_channel_enabled(audio.nosolo, wf);
        };
        auto setup_min_wf = [this, &min_samples_requested](debug::waveform2::wf *wf) {
            if (!wf) return;
            wf->setup(clock.cycles_per_frame);
            if (debug::waveform2::wf_requested(wf) && (wf->samples_requested > min_samples_requested))
                min_samples_requested = wf->samples_requested;
        };

        dbg.waveforms2.main_cache->setup(clock.cycles_per_frame);
        for (u32 i = 0; i < 4; i++) {
            auto *wf = dbg.waveforms2.fm.chan_cache[i];
            setup_min_wf(wf);
            ym2610.channel[i].ext_enable = channel_enabled(wf);
        }
        for (u32 i = 0; i < 6; i++) {
            auto *wf = dbg.waveforms2.adpcm_a.chan_cache[i];
            setup_min_wf(wf);
            ym2610.adpcm_a.ch[i].ext_enable = channel_enabled(wf);
        }
        setup_min_wf(dbg.waveforms2.adpcm_b.chan_cache[0]);
        ym2610.adpcm_b.ext_enable = channel_enabled(dbg.waveforms2.adpcm_b.chan_cache[0]);
        for (u32 i = 0; i < 3; i++) {
            auto *wf = dbg.waveforms2.ssg.chan_cache[i];
            setup_min_wf(wf);
            ym2610.ssg.sw[i].ext_enable = channel_enabled(wf);
        }

        if (dbg.waveforms2.main_cache->samples_requested > 0) {
            audio.master_cycles_per_max_sample = static_cast<double>(clock.cycles_per_frame) /
                                                 static_cast<double>(dbg.waveforms2.main_cache->samples_requested);
            audio.next_sample_cycle_max = static_cast<double>(now) + audio.master_cycles_per_max_sample;
            scheduler.only_add_abs(static_cast<i64>(audio.next_sample_cycle_max), debug_generation, this,
                                   &sample_audio_debug_max, nullptr);
        }

        if (min_samples_requested > 0) {
            audio.master_cycles_per_min_sample = static_cast<double>(clock.cycles_per_frame) /
                                                 static_cast<double>(min_samples_requested);
            audio.next_sample_cycle_min = static_cast<double>(now) + audio.master_cycles_per_min_sample;
            scheduler.only_add_abs(static_cast<i64>(audio.next_sample_cycle_min), debug_generation, this,
                                   &sample_audio_debug_min, nullptr);
        }

    }

    if (::dbg.do_debug) scheduler.run_til_tag_tg16<true>(2);
    else scheduler.run_til_tag_tg16<false>(2);
#ifdef DO_STATS
    u32 per_fps = static_cast<u32>(static_cast<float>(samples_pushed) * clock.fps);
    printf("\nSAMPLES PUSHED %d/sec", per_fps);
#endif
    //return vdp.display->active_draw_buffer;
    return 0;
}

u32 core::finish_scanline()
{
    if (::dbg.do_debug) scheduler.run_til_tag_tg16<true>(1);
    else scheduler.run_til_tag_tg16<false>(1);

    //return vdp.display->active_draw_buffer;
    return 0;
}

u32 core::step_master(u32 howmany)
{
    scheduler.run_for_cycles_tg16<true>(howmany);
    return 0;
}

void core::load_BIOS(multi_file_set &mfs)
{
    generate_LO_ROM(ROMs.lo);

    const char *wanted = is_MVS ? "sp-s2.sp1" : "neo-po.bin";
    auto valid_bios = [](auto &file) {
        return (file.buf.ptr != nullptr) && (file.buf.size > 0);
    };

    for (auto &file : mfs.files) {
        const char *a = file.name;
        const char *b = wanted;
        while (*a && *b && std::tolower((unsigned char)*a) == std::tolower((unsigned char)*b)) {
            a++;
            b++;
        }
        if (*a == 0 && *b == 0) {
            if (valid_bios(file)) {
                ROMs.BIOS.copy_from_buf(file.buf);
            }
            else {
                ROMs.BIOS.allocate(0);
                printf("\nNeoGeo BIOS %s was found but could not be loaded", wanted);
            }
            return;
        }
    }

    if ((mfs.files.size() == 1) && valid_bios(mfs.files[0])) {
        ROMs.BIOS.copy_from_buf(mfs.files[0].buf);
    }
    else {
        ROMs.BIOS.allocate(0);
        printf("\nNeoGeo BIOS %s not found", wanted);
    }
}

}
