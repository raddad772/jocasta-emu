#include "ngp_bus.h"

namespace NGP {

enum {
    NGP_CAT_TLCS_INSTRUCTION = 1,
    NGP_CAT_TLCS_IRQ,
    NGP_CAT_Z80_INSTRUCTION,
    NGP_CAT_Z80_IRQ,
};

static inline float i16_to_float(i16 v) { return static_cast<float>(v) / 32768.0f; }
static inline float dac_to_float(u8 v) { return (static_cast<float>(v) / 255.0f) * 2.0f - 1.0f; }

static void sample_audio_debug_max(void *ptr, u64 key, u64 clock_val, u32 jitter)
{
    auto *th = static_cast<core *>(ptr);
    if (key != th->audio.debug_generation) return;

    float ml, mr;
    if (th->apu.psg_enabled) {
        i16 l, r; th->apu.psg.mix_stereo(l, r, true);
        ml = i16_to_float(l); mr = i16_to_float(r);
    } else {
        ml = dac_to_float(th->apu.dac_l); mr = dac_to_float(th->apu.dac_r);
    }
    bool more = debug::waveform2::wf_push_stereo(th->dbg.waveforms2.main_cache, ml, mr);

    auto *dc = th->dbg.waveforms2.dac.chan_cache[0];
    if (dc && debug::waveform2::wf_push_stereo(dc, dac_to_float(th->apu.dac_l), dac_to_float(th->apu.dac_r)))
        more = true;

    if (!more) return;
    th->audio.next_sample_cycle_max += th->audio.master_cycles_per_max_sample;
    th->scheduler.only_add_abs(static_cast<i64>(th->audio.next_sample_cycle_max), key, th, &sample_audio_debug_max, nullptr);
}

static void sample_audio_debug_min(void *ptr, u64 key, u64 clock_val, u32 jitter)
{
    auto *th = static_cast<core *>(ptr);
    if (key != th->audio.debug_generation) return;
    bool any = false;
    for (u32 i = 0; i < 4; i++) {
        auto *dc = th->dbg.waveforms2.psg.chan_cache[i];
        if (!dc) continue;
        i16 l, r; th->apu.psg.sample_channel_stereo(static_cast<int>(i), l, r);
        if (debug::waveform2::wf_push_stereo(dc, i16_to_float(l), i16_to_float(r))) any = true;
    }
    if (!any) return;
    th->audio.next_sample_cycle_min += th->audio.master_cycles_per_min_sample;
    th->scheduler.only_add_abs(static_cast<i64>(th->audio.next_sample_cycle_min), key, th, &sample_audio_debug_min, nullptr);
}

u32 core::finish_frame()
{
    ensure_work_ram_bound();
    sync_rtc_pio();
    poll_power_button();
    audio.debug_generation++;

    if (::dbg.do_debug && dbg.waveforms2.main_cache) {
        const double cpf = static_cast<double>(KXGE::FRAME_CYCLES);
        const u64 now = master_clock;
        const u64 gen = audio.debug_generation;

        bool any_solo = false;
        for (u32 i = 0; i < 4; i++)
            if (dbg.waveforms2.psg.chan_cache[i] && dbg.waveforms2.psg.chan_cache[i]->ch_output_solo) any_solo = true;
        if (dbg.waveforms2.dac.chan_cache[0] && dbg.waveforms2.dac.chan_cache[0]->ch_output_solo) any_solo = true;
        audio.nosolo = !any_solo;

        dbg.waveforms2.main_cache->setup(cpf);
        if (dbg.waveforms2.dac.chan_cache[0]) dbg.waveforms2.dac.chan_cache[0]->setup(cpf);

        u32 min_samples = 0;
        for (u32 i = 0; i < 4; i++) {
            auto *wf = dbg.waveforms2.psg.chan_cache[i];
            if (wf) {
                wf->setup(cpf);
                if (debug::waveform2::wf_requested(wf) && wf->samples_requested > min_samples)
                    min_samples = wf->samples_requested;
            }
            const bool en = debug::waveform2::wf_channel_enabled(audio.nosolo, wf);
            if (i < 3) apu.psg.sw[i].ext_enable = en;
            else apu.psg.noise.ext_enable = en;
        }

        if (dbg.waveforms2.main_cache->samples_requested > 0) {
            audio.master_cycles_per_max_sample = cpf / static_cast<double>(dbg.waveforms2.main_cache->samples_requested);
            audio.next_sample_cycle_max = static_cast<double>(now) + audio.master_cycles_per_max_sample;
            scheduler.only_add_abs(static_cast<i64>(audio.next_sample_cycle_max), gen, this, &sample_audio_debug_max, nullptr);
        }
        if (min_samples > 0) {
            audio.master_cycles_per_min_sample = cpf / static_cast<double>(min_samples);
            audio.next_sample_cycle_min = static_cast<double>(now) + audio.master_cycles_per_min_sample;
            scheduler.only_add_abs(static_cast<i64>(audio.next_sample_cycle_min), gen, this, &sample_audio_debug_min, nullptr);
        }
    }

    if (::dbg.do_debug) scheduler.run_til_tag_tg16<true>(2);
    else scheduler.run_til_tag_tg16<false>(2);
    return 0;
}

static void setup_waveforms(core &th, debugger_interface *dbgr)
{
    th.dbg.waveforms2.view = dbgr->make_view(dview_waveform2);
    auto *wv = &th.dbg.waveforms2.view.get().waveform2;
    snprintf(wv->name, sizeof(wv->name), "T6W28");
    auto &root = wv->root;
    root.children.reserve(2);

    th.dbg.waveforms2.main = &root;
    th.dbg.waveforms2.main_cache = &root.data;
    snprintf(root.data.name, sizeof(root.data.name), "Stereo Out");
    root.data.kind = debug::waveform2::wk_big;
    root.data.samples_requested = 400;
    root.data.stereo = true;

    static const char *psg_names[] = { "Tone 1", "Tone 2", "Tone 3", "Noise" };
    auto &psg = root.add_child_category("PSG", 4);
    for (u32 i = 0; i < 4; i++) {
        auto *v = psg.add_child_wf(debug::waveform2::wk_small, th.dbg.waveforms2.psg.chan[i]);
        th.dbg.waveforms2.psg.chan_cache[i] = &v->data;
        snprintf(v->data.name, sizeof(v->data.name), "%s", psg_names[i]);
        v->data.samples_requested = 400;
        v->data.stereo = true;
    }

    auto &dac = root.add_child_category("DAC", 1);
    auto *dv = dac.add_child_wf(debug::waveform2::wk_small, th.dbg.waveforms2.dac.chan[0]);
    th.dbg.waveforms2.dac.chan_cache[0] = &dv->data;
    snprintf(dv->data.name, sizeof(dv->data.name), "DAC L/R");
    dv->data.samples_requested = 400;
    dv->data.stereo = true;
}

static void render_chrmap_plane1(debugger_interface *, debugger_view *dview, void *ptr, u32 out_width)
{
    auto *th = static_cast<core *>(ptr);
    image_view *iv = &dview->image;
    iv->draw_which_buf ^= 1;
    th->kxge.dbg_render_chrmap(0, static_cast<u32 *>(iv->img_buf[iv->draw_which_buf].ptr), out_width);
}

static void render_chrmap_plane2(debugger_interface *, debugger_view *dview, void *ptr, u32 out_width)
{
    auto *th = static_cast<core *>(ptr);
    image_view *iv = &dview->image;
    iv->draw_which_buf ^= 1;
    th->kxge.dbg_render_chrmap(1, static_cast<u32 *>(iv->img_buf[iv->draw_which_buf].ptr), out_width);
}

static void render_plane1_iso(debugger_interface *, debugger_view *dview, void *ptr, u32 out_width)
{
    auto *th = static_cast<core *>(ptr);
    image_view *iv = &dview->image;
    iv->draw_which_buf ^= 1;
    th->kxge.dbg_render_plane_iso(0, static_cast<u32 *>(iv->img_buf[iv->draw_which_buf].ptr), out_width);
}

static void render_plane2_iso(debugger_interface *, debugger_view *dview, void *ptr, u32 out_width)
{
    auto *th = static_cast<core *>(ptr);
    image_view *iv = &dview->image;
    iv->draw_which_buf ^= 1;
    th->kxge.dbg_render_plane_iso(1, static_cast<u32 *>(iv->img_buf[iv->draw_which_buf].ptr), out_width);
}

static void render_sprites(debugger_interface *, debugger_view *dview, void *ptr, u32 out_width)
{
    auto *th = static_cast<core *>(ptr);
    image_view *iv = &dview->image;
    iv->draw_which_buf ^= 1;
    th->kxge.dbg_render_sprites(static_cast<u32 *>(iv->img_buf[iv->draw_which_buf].ptr), out_width);
}

static void setup_image_view(core *th, debugger_interface *dbgr, cvec_ptr<debugger_view> &slot,
                             u32 w, u32 h, void (*func)(debugger_interface *, debugger_view *, void *, u32),
                             const char *label)
{
    slot = dbgr->make_view(dview_image);
    image_view *iv = &slot.get().image;
    iv->width = w;
    iv->height = h;
    iv->viewport.exists = true;
    iv->viewport.enabled = true;
    iv->viewport.p[0] = (ivec2){ 0, 0 };
    iv->viewport.p[1] = (ivec2){ static_cast<i32>(w), static_cast<i32>(h) };
    iv->update_func.ptr = th;
    iv->update_func.func = func;
    snprintf(iv->label, sizeof(iv->label), "%s", label);
}

static void setup_dbglog(core *th, debugger_interface *dbgr)
{
    cvec_ptr<debugger_view> p = dbgr->make_view(dview_dbglog);
    dbglog_view &dv = p.get().dbglog;
    th->dbg.dvptr = &dv;
    snprintf(dv.name, sizeof(dv.name), "Trace");
    dv.has_extra = true;

    dbglog_category_node &root = dv.get_category_root();
    root.children.reserve(4);

    dbglog_category_node &tlcs = root.add_node(dv, "TLCS900H", nullptr, 0, 0);
    tlcs.children.reserve(4);
    tlcs.add_node(dv, "Instruction Trace", "TLCS", NGP_CAT_TLCS_INSTRUCTION, 0x80FF80);
    tlcs.add_node(dv, "IRQ", "TLCS.IRQ", NGP_CAT_TLCS_IRQ, 0xA0FF80);
    th->cpu.cpu.dbg.dvptr = &dv;
    th->cpu.cpu.dbg.dv_id = NGP_CAT_TLCS_INSTRUCTION;
    th->cpu.cpu.dbg.irq_id = NGP_CAT_TLCS_IRQ;

    dbglog_category_node &z80 = root.add_node(dv, "Z80", nullptr, 0, 0);
    z80.children.reserve(4);
    z80.add_node(dv, "Instruction Trace", "Z80", NGP_CAT_Z80_INSTRUCTION, 0x8080FF);
    z80.add_node(dv, "IRQ", "Z80.IRQ", NGP_CAT_Z80_IRQ, 0x80A0FF);
    th->z80.dbg.dvptr = &dv;
    th->z80.dbg.dv_id = NGP_CAT_Z80_INSTRUCTION;
    th->z80.dbg.irq_id = NGP_CAT_Z80_IRQ;
}

void core::setup_debugger_interface(debugger_interface &dbgr)
{
    dbg.interface = &dbgr;
    dbgr.views.reserve(12);

    setup_waveforms(*this, &dbgr);
    setup_dbglog(this, &dbgr);
    setup_image_view(this, &dbgr, dbg.image_views.chrmap_plane1, 256, 256, &render_chrmap_plane1, "Scroll Plane 1 (CHRMAP)");
    setup_image_view(this, &dbgr, dbg.image_views.chrmap_plane2, 256, 256, &render_chrmap_plane2, "Scroll Plane 2 (CHRMAP)");
    setup_image_view(this, &dbgr, dbg.image_views.plane1_iso, KXGE::DISP_WIDTH, KXGE::DISP_HEIGHT, &render_plane1_iso, "Scroll Plane 1 (isolated)");
    setup_image_view(this, &dbgr, dbg.image_views.plane2_iso, KXGE::DISP_WIDTH, KXGE::DISP_HEIGHT, &render_plane2_iso, "Scroll Plane 2 (isolated)");
    setup_image_view(this, &dbgr, dbg.image_views.sprites, 256, 256, &render_sprites, "Sprites");

    dbgr.supported_by_core = false;
    dbgr.smallest_step = 1;
}

}
