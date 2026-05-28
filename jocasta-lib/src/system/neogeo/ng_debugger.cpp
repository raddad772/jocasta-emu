//
// Created by . on 5/12/26.
//

#include "ng_debugger.h"
#include "ng_bus.h"
#include "helpers/color.h"

namespace NEOGEO {
static int render_imask(cpu_reg_context*ctx, void *outbuf, size_t outbuf_sz)
{
    return snprintf(static_cast<char *>(outbuf), outbuf_sz, "%d", ctx->int32_data);
}

static int render_csr(cpu_reg_context*ctx, void *outbuf, size_t outbuf_sz)
{
    return snprintf(static_cast<char *>(outbuf), outbuf_sz, "%c%c%c%c%c",
                    ctx->int32_data & 0x10 ? 'X' : 'x',
                    ctx->int32_data & 8 ? 'N' : 'n',
                    ctx->int32_data & 4 ? 'Z' : 'z',
                    ctx->int32_data & 2 ? 'V' : 'v',
                    ctx->int32_data & 1 ? 'C' : 'c'
    );
}

static disassembly_vars get_disassembly_vars_m68k(void *genptr, disassembly_view &dv)
{
    auto *th = static_cast<core *>(genptr);
    disassembly_vars dvar;
    dvar.address_of_executing_instruction = th->m68k.debug.ins_PC & 0xFFFFFF;
    dvar.current_clock_cycle = th->master_clock;
    return dvar;
}

static void get_disassembly_m68k(void *genptr, disassembly_view &dview, disassembly_entry &entry)
{
    auto *th = static_cast<core*>(genptr);
    th->m68k.disassemble_entry(entry);
}


static void setup_dbglog(debugger_interface *dbgr, core *th)
{
    cvec_ptr p = dbgr->make_view(dview_dbglog);
    debugger_view *dview = &p.get();
    dbglog_view &dv = dview->dbglog;
    th->dbg.dvptr = &dv;
    snprintf(dv.name, sizeof(dv.name), "Trace");
    dv.has_extra = true;

    dbglog_category_node &root = dv.get_category_root();
    root.children.reserve(10);
    dbglog_category_node &m68k = root.add_node(dv, "M68000", nullptr, 0, 0);
    m68k.children.reserve(10);
    m68k.add_node(dv, "Instruction Trace", "M68k", NG_CAT_M68K_INSTRUCTION, 0x80FF80);
    th->m68k.dbg.dvptr = &dv;
    th->m68k.dbg.dv_id = NG_CAT_M68K_INSTRUCTION;
    th->m68k.dbg.irq_id = NG_CAT_M68K_IRQ;

    m68k.add_node(dv, "IRQ", "M68k.IRQ", NG_CAT_M68K_IRQ, 0xA0AF80);
    m68k.add_node(dv, "Bus R/W", "M68k.BUS", NG_CAT_M68K_BUSRW, 0xA0AF80);

    dbglog_category_node &z80 = root.add_node(dv, "Z80", nullptr, 0, 0);
    z80.children.reserve(10);
    z80.add_node(dv, "Instruction Trace", "Z80", NG_CAT_Z80_INSTRUCTION, 0x8080FF);
    th->z80.dbg.dvptr = &dv;
    th->z80.dbg.dv_id = NG_CAT_Z80_INSTRUCTION;
    th->z80.dbg.irq_id = NG_CAT_Z80_IRQ;
    z80.add_node(dv, "IRQ", "Z80.IRQ", NG_CAT_Z80_IRQ, 0x8080FF);
    z80.add_node(dv, "Bus R/W", "Z80.BUS", NG_CAT_Z80_BUSRW, 0x8080FF);

    dbglog_category_node &lspc = root.add_node(dv, "LSPC", nullptr, 0, 0);
    lspc.children.reserve(5);
    lspc.add_node(dv, "VRAM", "LSPC.VRAM", NG_CAT_LSPC_VRAM, 0xFFB060);
}

static void create_and_bind_registers_m68k(core* th, disassembly_view *dv)
{
    u32 tkindex = 0;
    for (u32 i = 0; i < 8; i++) {
        cpu_reg_context *rg = &dv->cpu.regs.emplace_back();
        snprintf(rg->name, sizeof(rg->name), "D%d", i);
        rg->kind = RK_int32;
        rg->custom_render = nullptr;
        rg->index = tkindex++;
    }
    for (u32 i = 0; i < 7; i++) {
        cpu_reg_context *rg = &dv->cpu.regs.emplace_back();
        snprintf(rg->name, sizeof(rg->name), "A%d", i);
        rg->kind = RK_int32;
        rg->custom_render = nullptr;
        rg->index = tkindex++;
    }
    cpu_reg_context *rg = &dv->cpu.regs.emplace_back();
    snprintf(rg->name, sizeof(rg->name), "PC");
    rg->kind = RK_int32;
    rg->custom_render = nullptr;
    rg->index = tkindex++;

    rg = &dv->cpu.regs.emplace_back();
    snprintf(rg->name, sizeof(rg->name), "USP");
    rg->kind = RK_int32;
    rg->custom_render = nullptr;
    rg->index = tkindex++;

    rg = &dv->cpu.regs.emplace_back();
    snprintf(rg->name, sizeof(rg->name), "SSP");
    rg->kind = RK_int32;
    rg->custom_render = nullptr;
    rg->index = tkindex++;

    rg = &dv->cpu.regs.emplace_back();
    snprintf(rg->name, sizeof(rg->name), "SR");
    rg->kind = RK_int32;
    rg->custom_render = nullptr;
    rg->index = tkindex++;

    rg = &dv->cpu.regs.emplace_back();
    snprintf(rg->name, sizeof(rg->name), "supervisor");
    rg->kind = RK_bool;
    rg->custom_render = nullptr;
    rg->index = tkindex++;

    rg = &dv->cpu.regs.emplace_back();
    snprintf(rg->name, sizeof(rg->name), "trace");
    rg->kind = RK_bool;
    rg->custom_render = nullptr;
    rg->index = tkindex++;

    rg = &dv->cpu.regs.emplace_back();
    snprintf(rg->name, sizeof(rg->name), "IMASK");
    rg->kind = RK_int32;
    rg->custom_render = &render_imask;
    rg->index = tkindex++;

    rg = &dv->cpu.regs.emplace_back();
    snprintf(rg->name, sizeof(rg->name), "CSR");
    rg->kind = RK_int32;
    rg->custom_render = &render_csr;
    rg->index = tkindex++;

    rg = &dv->cpu.regs.emplace_back();
    snprintf(rg->name, sizeof(rg->name), "IR");
    rg->kind = RK_bool;
    rg->custom_render = nullptr;
    rg->index = tkindex++;

    rg = &dv->cpu.regs.emplace_back();
    snprintf(rg->name, sizeof(rg->name), "IRC");
    rg->kind = RK_bool;
    rg->custom_render = nullptr;
    rg->index = tkindex++;

#define BIND(dn, index) th->dbg.dasm_m68k. dn = &dv->cpu.regs.at(index)
    for (u32 i = 0; i < 8; i++) {
        BIND(D[i], i);
    }

    for (u32 i = 0; i < 7; i++) {
        BIND(A[i], i+8);
    }
    // 15 now
    BIND(PC, 15);
    BIND(USP, 16);
    BIND(SSP, 17);
    BIND(SR, 18);
    BIND(supervisor, 19);
    BIND(trace, 20);
    BIND(IMASK, 21);
    BIND(CSR, 22);
    BIND(IR, 23);
    BIND(IRC, 24);
#undef BIND
}

static void fill_disassembly_view_m68k(void *genptr, disassembly_view &dview)
{
    auto *th = static_cast<core *>(genptr);
    for (u32 i = 0; i < 8; i++) {
        th->dbg.dasm_m68k.D[i]->int32_data = th->m68k.regs.D[i];
        if (i < 7) th->dbg.dasm_m68k.A[i]->int32_data = th->m68k.regs.A[i];
    }
    th->dbg.dasm_m68k.PC->int32_data = th->m68k.regs.PC;
    th->dbg.dasm_m68k.SR->int32_data = th->m68k.regs.SR.u;
    if (th->dbg.dasm_m68k.SR->int32_data & 0x2000) { // supervisor mode
        th->dbg.dasm_m68k.USP->int32_data = th->m68k.regs.ASP;
        th->dbg.dasm_m68k.SSP->int32_data = th->m68k.regs.A[7];
    }
    else { // user mode
        th->dbg.dasm_m68k.USP->int32_data = th->m68k.regs.A[7];
        th->dbg.dasm_m68k.SSP->int32_data = th->m68k.regs.ASP;
    }
    th->dbg.dasm_m68k.supervisor->bool_data = th->m68k.regs.SR.S;
    th->dbg.dasm_m68k.trace->bool_data = th->m68k.regs.SR.T;
    th->dbg.dasm_m68k.IMASK->int32_data = th->m68k.regs.SR.I;
    th->dbg.dasm_m68k.CSR->int32_data = th->m68k.regs.SR.u & 0x1F;
    th->dbg.dasm_m68k.IR->int32_data = th->m68k.regs.IR;
    th->dbg.dasm_m68k.IRC->int32_data = th->m68k.regs.IRC;
}

static int print_addr(void *, u32 addr, char *out, size_t out_sz) {
    return snprintf(out, out_sz, "%06x", addr);
}

static void render_image_view_lpsc_output(debugger_interface *dbgr, debugger_view *dview, void *ptr, u32 out_width)
{
    auto *th = static_cast<core *>(ptr);
    image_view *iv = &dview->image;
    iv->draw_which_buf ^= 1;
    u32 *outbuf = static_cast<u32 *>(iv->img_buf[iv->draw_which_buf].ptr);
    const u32 *inbuf = th->lpsc.debug_output.get();

    if (!inbuf) return;

    for (u32 y = 0; y < 224; y++) {
        u32 *out_line = outbuf + (y * out_width);
        const u32 *in_line = inbuf + (y * 320);
        for (u32 x = 0; x < 320; x++) {
            out_line[x] = neogeo_to_screen(in_line[x]);
        }
    }
}

static void setup_image_view_lpsc_output(core *th, debugger_interface *dbgr)
{
    debugger_view *dview;
    th->dbg.image_views.lpsc_output = dbgr->make_view(dview_image);
    dview = &th->dbg.image_views.lpsc_output.get();
    image_view *iv = &dview->image;

    iv->width = 320;
    iv->height = 224;
    iv->viewport.exists = true;
    iv->viewport.enabled = true;
    iv->viewport.p[0] = (ivec2){ 0, 0 };
    iv->viewport.p[1] = (ivec2){ 320, 224 };

    iv->update_func.ptr = th;
    iv->update_func.func = &render_image_view_lpsc_output;
    snprintf(iv->label, sizeof(iv->label), "LPSC Output");
}

static void render_image_view_neo_b1_output(debugger_interface *dbgr, debugger_view *dview, void *ptr, u32 out_width)
{
    auto *th = static_cast<core *>(ptr);
    image_view *iv = &dview->image;
    iv->draw_which_buf ^= 1;
    u32 *outbuf = static_cast<u32 *>(iv->img_buf[iv->draw_which_buf].ptr);
    const u32 *inbuf = th->nb1.debug_output.get();

    if (!inbuf) return;

    for (u32 y = 0; y < 224; y++) {
        u32 *out_line = outbuf + (y * out_width);
        const u32 *in_line = inbuf + (y * 320);
        for (u32 x = 0; x < 320; x++) {
            out_line[x] = neogeo_to_screen(in_line[x]);
        }
    }
}

static void setup_image_view_neo_b1_output(core *th, debugger_interface *dbgr)
{
    debugger_view *dview;
    th->dbg.image_views.neo_b1_output = dbgr->make_view(dview_image);
    dview = &th->dbg.image_views.neo_b1_output.get();
    image_view *iv = &dview->image;

    iv->width = 320;
    iv->height = 224;
    iv->viewport.exists = true;
    iv->viewport.enabled = true;
    iv->viewport.p[0] = (ivec2){ 0, 0 };
    iv->viewport.p[1] = (ivec2){ 320, 224 };

    iv->update_func.ptr = th;
    iv->update_func.func = &render_image_view_neo_b1_output;
    snprintf(iv->label, sizeof(iv->label), "Neo-B1 Output");
}


static void setup_m68k_disassembly(debugger_interface *dbgr, core* th)
{
    cvec_ptr<debugger_view> p = dbgr->make_view(dview_disassembly);
    debugger_view *dview = &p.get();
    disassembly_view *dv = &dview->disassembly;
    dv->mem_end = 0xFFFFFF;
    dv->addr_column_size = 6;
    dv->has_context = 1;
    dv->processor_name.sprintf("m68000");

    create_and_bind_registers_m68k(th, dv);
    dv->fill_view.ptr = static_cast<void *>(th);
    dv->fill_view.func = &fill_disassembly_view_m68k;

    dv->get_disassembly.ptr = static_cast<void *>(th);
    dv->get_disassembly.func = &get_disassembly_m68k;

    dv->get_disassembly_vars.ptr = static_cast<void *>(th);
    dv->get_disassembly_vars.func = &get_disassembly_vars_m68k;

    dv->print_addr.ptr = static_cast<void *>(th);
    dv->print_addr.func = &print_addr;
}


static void setup_waveforms(core &th, debugger_interface *dbgr)
{
    th.dbg.waveforms2.view = dbgr->make_view(dview_waveform2);
    auto *dview = &th.dbg.waveforms2.view.get();
    auto *wv = &dview->waveform2;
    snprintf(wv->name, sizeof(wv->name), "YM2610");
    auto &root = wv->root;
    root.children.reserve(4);

    th.dbg.waveforms2.main = &root;
    th.dbg.waveforms2.main_cache = &root.data;
    snprintf(root.data.name, sizeof(root.data.name), "Stereo Out");
    root.data.kind = debug::waveform2::wk_big;
    root.data.samples_requested = 400;
    root.data.stereo = true;

    auto &fm = root.add_child_category("FM", 4);
    for (u32 i = 0; i < 4; i++) {
        auto *v = fm.add_child_wf(debug::waveform2::wk_small, th.dbg.waveforms2.fm.chan[i]);
        th.dbg.waveforms2.fm.chan_cache[i] = &v->data;
        snprintf(v->data.name, sizeof(v->data.name), "FM%d", i + 1);
        v->data.samples_requested = 400;
    }

    auto &adpcm_a = root.add_child_category("ADPCM-A", 6);
    for (u32 i = 0; i < 6; i++) {
        auto *v = adpcm_a.add_child_wf(debug::waveform2::wk_small, th.dbg.waveforms2.adpcm_a.chan[i]);
        th.dbg.waveforms2.adpcm_a.chan_cache[i] = &v->data;
        snprintf(v->data.name, sizeof(v->data.name), "A%d", i + 1);
        v->data.samples_requested = 400;
    }

    auto &adpcm_b_cat = root.add_child_category("ADPCM-B", 1);
    auto *v = adpcm_b_cat.add_child_wf(debug::waveform2::wk_small, th.dbg.waveforms2.adpcm_b.chan[0]);
    th.dbg.waveforms2.adpcm_b.chan_cache[0] = &v->data;
    snprintf(v->data.name, sizeof(v->data.name), "B");
    v->data.samples_requested = 400;

    static const char *ssg_names[] = {"A", "B", "C"};
    auto &ssg_cat = root.add_child_category("SSG", 3);
    for (u32 i = 0; i < 3; i++) {
        auto *sv = ssg_cat.add_child_wf(debug::waveform2::wk_small, th.dbg.waveforms2.ssg.chan[i]);
        th.dbg.waveforms2.ssg.chan_cache[i] = &sv->data;
        snprintf(sv->data.name, sizeof(sv->data.name), "%s", ssg_names[i]);
        sv->data.samples_requested = 400;
    }
}

void core::setup_debugger_interface(debugger_interface &dbgr) {
    dbg.interface = &dbgr;
    dbgr.views.reserve(15);

    setup_m68k_disassembly(&dbgr, this);
    setup_dbglog(&dbgr, this);
    setup_waveforms(*this, &dbgr);
    setup_image_view_lpsc_output(this, &dbgr);
    setup_image_view_neo_b1_output(this, &dbgr);
    dbgr.supported_by_core = false;
    dbgr.smallest_step = 2;
}
}
