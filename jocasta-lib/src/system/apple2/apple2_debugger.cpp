//
// Created by . on 5/16/26.
//

#include "apple2_debugger.h"
#include "apple2_bus.h"

namespace apple2 {
void core::setup_debugger_interface(debugger_interface &intf) {
    dbg.interface = &intf;
    auto *dbgr = dbg.interface;

    dbgr->supported_by_core = true;
    dbgr->smallest_step = 8;
    dbgr->views.reserve(15);
}
}