//
// Created by . on 4/3/26.
//

#include "dc_debugger.h"
#include "dc_bus.h"

#include "helpers/multisize_memaccess.cpp"

namespace DREAMCAST {

static void render_image_view_sysinfo(debugger_interface *dbgr, debugger_view *dview, void *ptr, u32 out_width) {
    auto *th = static_cast<core *>(ptr);
    //memset(ptr, 0, out_width * 4 * 10);
    debugger_widget_textbox *tb = &dview->options[0].textbox;
    tb->clear();
    tb->sprintf("---CPU IRQs");
    tb->sprintf("\nBL:%d  IMASK:%01x", th->cpu.regs.SR.BL, th->cpu.regs.SR.IMASK);
    tb->sprintf("\nIRQs that can trigger:");
    for (u32 i = 0; i < SH4I_NUM; i++) {
        auto &p = th->cpu.interrupt_map[i];
        if (p->priority >= th->cpu.regs.SR.IMASK) {
            tb->sprintf("\n %s  - priority:%d  INTEVT:%08x", SH4::IRQ_NAMES[p->source], p->priority, p->intevt);
        }
    }
    tb->sprintf("\n\n---HOLLY IRQs");
    // NRM first
    for (u32 i = 0; i < 22; i++) {
        u32 bit = 1 << i;
        if ((th->io.SB_IML2NRM | th->io.SB_IML4NRM | th->io.SB_IML6NRM) & bit) {
            tb->sprintf("\n NRM %s ", HOLLY::HIRQ_NRM_NAMES[i]);
            if (th->io.SB_IML2NRM & bit)
                tb->sprintf("2");
            if (th->io.SB_IML4NRM & bit)
                tb->sprintf("4");
            if (th->io.SB_IML6NRM & bit)
                tb->sprintf("6");
        }
    }

    // EXT next
    for (u32 i = 0; i < 22; i++) {
        u32 bit = 1 << i;
        if ((th->io.SB_IML2EXT.u | th->io.SB_IML4EXT.u | th->io.SB_IML6EXT.u) & bit) {
            tb->sprintf("\n NRM %s ", HOLLY::HIRQ_EXT_NAMES[i]);
            if (th->io.SB_IML2EXT.u & bit)
                tb->sprintf("2");
            if (th->io.SB_IML4EXT.u & bit)
                tb->sprintf("4");
            if (th->io.SB_IML6EXT.u & bit)
                tb->sprintf("6");
        }
    }
    tb->sprintf("\n\nAICA still sch: %d  next cycle:%lld  CPU paused:%d  @%lld", th->aica.timing.cpu_still_sched, static_cast<u64>(th->aica.timing.next_cpu_cycle), th->aica.cpu_paused, th->master_cycles);
}

static void setup_image_view_sysinfo(core* th, debugger_interface *dbgr) {
    debugger_view *dview;
    th->dbg.image_views.sysinfo = dbgr->make_view(dview_image);
    dview = &th->dbg.image_views.sysinfo.get();
    image_view *iv = &dview->image;

    iv->width = 10;
    iv->height = 10;
    iv->viewport.exists = true;
    iv->viewport.enabled = true;
    iv->viewport.p[0] = (ivec2){ 0, 0 };
    iv->viewport.p[1] = (ivec2){ 10, 10 };

    iv->update_func.ptr = th;
    iv->update_func.func = &render_image_view_sysinfo;

    snprintf(iv->label, sizeof(iv->label), "Sys Info View");

    debugger_widgets_add_textbox(dview->options, "blah!", 1);
}

void setup_console_view(core* th, debugger_interface *dbgr)
{
    th->dbg.console_view = dbgr->make_view(dview_console);
    debugger_view *dview = &th->dbg.console_view.get();

    console_view *cv = &dview->console;

    snprintf(cv->name, sizeof(cv->name), "System Console");

    th->cpu.dbg.console = cv;
}


static void setup_dbglog(debugger_interface *dbgr, core *th) {
    cvec_ptr p = dbgr->make_view(dview_dbglog);
    debugger_view *dview = &p.get();
    dbglog_view &dv = dview->dbglog;
    th->dbg.dvptr = &dv;
    snprintf(dv.name, sizeof(dv.name), "Trace");
    dv.has_extra = true;

    dbglog_category_node &root = dv.get_category_root();
    root.children.reserve(10);
    dbglog_category_node &sh4 = root.add_node(dv, "SH4", nullptr, 0, 0x80FF80);
    sh4.children.reserve(10);
    sh4.add_node(dv, "Instructions", "SH4", DCD_SH4_INSTRUCTION, 0x80FF80);
    th->cpu.dbg.dvptr = &dv;
    th->cpu.dbg.dv_id = DCD_SH4_INSTRUCTION;
    th->cpu.trace.exception_id = DCD_SH4_EXCEPTION;
    th->cpu.trace.ins_id = DCD_SH4_INSTRUCTION;
    th->cpu.trace.irq_id = DCD_SH4_IRQ;
    th->cpu.trace.regread_id = DCD_SH4_REGREAD;
    th->cpu.trace.regwrite_id = DCD_SH4_REGWRITE;
    sh4.add_node(dv, "Exceptions", "Except", DCD_SH4_EXCEPTION, 0xFFFFFF);
    sh4.add_node(dv, "IRQs", "IRQ", DCD_SH4_IRQ, 0xFFFFFF);
    sh4.add_node(dv, "Register Reads", "RegRead", DCD_SH4_REGREAD, 0xFFFFFF);
    sh4.add_node(dv, "Register Writes", "RegWrite", DCD_SH4_REGWRITE, 0xFFFFFF);

    u32 dma_c = 0x5020FF;
    dbglog_category_node &bus = root.add_node(dv, "Bus", nullptr, 0, dma_c);
    bus.children.reserve(10);
    bus.add_node(dv, "Current Issue", "Issue", DCD_GENERAL, dma_c);
    bus.add_node(dv, "Console Output", "ConsleOut", DCD_BUS_CONSOLE, dma_c);
    bus.add_node(dv, "Bus Reads", "BusRd", DCD_BUS_READ, dma_c);
    bus.add_node(dv, "Bus Writes", "BusWr", DCD_BUS_WRITE, dma_c);

    dma_c = 0x5020FF;
    dbglog_category_node &g2 = root.add_node(dv, "G2", nullptr, 0, dma_c);
    g2.children.reserve(10);
    g2.add_node(dv, "DMA Start", "DMAstart", DCD_G2_DMA_START, dma_c);
    g2.add_node(dv, "DMA End", "DMAend", DCD_G2_DMA_END, dma_c);
    g2.add_node(dv, "DMA Suspend", "DMAsus", DCD_G2_DMA_SUSPEND, dma_c);
    dma_c = 0x5020FF;

    dbglog_category_node &aica = root.add_node(dv, "AICA", nullptr, 0, dma_c);
    aica.children.reserve(10);
    aica.add_node(dv, "ARM7DI Instruction", "arm.ins", DCD_AICA_ARM_INSTRUCTION, dma_c);
    aica.add_node(dv, "ARM7DI Reset", "arm.rst", DCD_AICA_ARM_RESET, dma_c);
    aica.add_node(dv, "ARM7DI Exception", "arm.exc", DCD_AICA_ARM_EXCEPTION, dma_c);
    th->aica.dbg.dvptr = &dv;
    th->aica.dbg.dv_id = 0;
    th->aica.cpu.dbg.dvptr = &dv;
    th->aica.cpu.dbg.dv_id = DCD_AICA_ARM_INSTRUCTION;
    th->aica.cpu.trace.exception_id = DCD_AICA_ARM_EXCEPTION;
    th->aica.trace.cpu_reset_id = DCD_AICA_ARM_RESET;

    dbglog_category_node &gdrom = root.add_node(dv, "GDROM", nullptr, 0, dma_c);
    gdrom.children.reserve(15);
    gdrom.add_node(dv, "General", "gdrom.gen", DCD_GDROM_GENERAL, dma_c);
    gdrom.add_node(dv, "CMD", "gdrom.cmd", DCD_GDROM_CMD_WRITE, dma_c);
    gdrom.add_node(dv, "ATA CMD", "gdrom.ata", DCD_GDROM_ATA_CMD, dma_c);
    gdrom.add_node(dv, "SPI CMD", "gdrom.spi", DCD_GDROM_SPI_PACKET, dma_c);
    gdrom.add_node(dv, "State Change", "gdrom.state", DCD_GDROM_STATE_CHANGE, dma_c);
}

static void readcpumembus(void *ptr, u32 addr, void *dest)
{
    // Read 16 bytes from addr into dest
    auto *th = static_cast<core *>(ptr);
    u32 offset = 0;
    for (u32 i = 0; i < 4; i++) {
        u32 a = th->mainbus_read<4, true>((addr + offset) & 0x1FFF'FFFF);
        cW32(dest, offset, a);
        offset += 4;
    }
}


static void setup_memory_view(core *th, debugger_interface *dbgr) {
    th->dbg.memory = dbgr->make_view(dview_memory);
    debugger_view *dview = &th->dbg.memory.get();
    memory_view *mv = &dview->memory;
    mv->add_module("CPU Bus", 0, 8, 0, 0x1EFFFFFF, th, &readcpumembus);
}

void core::setup_debugger_interface(debugger_interface &dbgr)
{
    dbg.interface = &dbgr;
    dbgr.views.reserve(15);

    dbgr.supported_by_core = true;
    dbgr.smallest_step = 1;
    //setup_console_view(this, &dbgr);
    setup_dbglog(&dbgr, this);
    /*setup_waveforms(*this, &dbgr);*/
    setup_image_view_sysinfo(this, &dbgr);
    /*setup_image_view_spuinfo(this, &dbgr);
    setup_image_view_vram(this, &dbgr);*/
    setup_memory_view(this, &dbgr);
}

}