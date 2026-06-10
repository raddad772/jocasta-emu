//
// Created by . on 3/28/26.
//

#include "pv1000_debugger.h"
#include "pv1000_bus.h"
namespace CASIO_PV1000 {

static void setup_waveforms(core& th, debugger_interface *dbgr)
{
    th.dbg.waveforms.view = dbgr->make_view(dview_waveforms);
    debugger_view &dview = th.dbg.waveforms.view.get();
    waveform_view &wv = dview.waveform;
    snprintf(wv.name, sizeof(wv.name), "Audio");

    // 384 8x8 tiles, or 2x for CGB
    debug_waveform *dw = &wv.waveforms.emplace_back();
    th.dbg.waveforms.main.make(wv.waveforms, wv.waveforms.size()-1);
    snprintf(dw->name, sizeof(dw->name), "Output");
    dw->kind = dwk_main;
    dw->samples_requested = 400;

    dw = &wv.waveforms.emplace_back();
    th.dbg.waveforms.chan[0].make(wv.waveforms, wv.waveforms.size()-1);
    snprintf(dw->name, sizeof(dw->name), "Square 1");
    dw->kind = dwk_channel;
    dw->samples_requested = 200;

    dw = &wv.waveforms.emplace_back();
    th.dbg.waveforms.chan[1].make(wv.waveforms, wv.waveforms.size()-1);
    snprintf(dw->name, sizeof(dw->name), "Square 2 (-6db)");
    dw->kind = dwk_channel;
    dw->samples_requested = 200;

    dw = &wv.waveforms.emplace_back();
    th.dbg.waveforms.chan[2].make(wv.waveforms, wv.waveforms.size()-1);
    snprintf(dw->name, sizeof(dw->name), "Square 3 (-12db)");
    dw->kind = dwk_channel;
    dw->samples_requested = 200;
}

void core::setup_debugger_interface(debugger_interface &intf) {
    dbg.interface = &intf;
    intf.views.reserve(15);

    intf.supported_by_core = true;

    setup_waveforms(*this, &intf);
}
}
