//
// Created by . on 5/12/26.
//
#pragma once

enum NG_DBLOG_CATEGORIES {
    NG_CAT_UNKNOWN = 0,
    NG_CAT_M68K_INSTRUCTION = 1,
    NG_CAT_M68K_IRQ,
    NG_CAT_M68K_BUSRW,

    NG_CAT_Z80_INSTRUCTION,
    NG_CAT_Z80_IRQ,
    NG_CAT_Z80_BUSRW,

    NG_CAT_LSPC_VRAM,
};

#define dbgloglog(r_cat, r_severity, r_format, ...) if (dbg.dvptr->ids_enabled[r_cat]) dbg.dvptr->add_printf(r_cat, master_clock, r_severity, r_format, __VA_ARGS__);
#define dbgloglog_bus(r_cat, r_severity, r_format, ...) if (bus->dbg.dvptr->ids_enabled[r_cat]) bus->dbg.dvptr->add_printf(r_cat, master_clock, r_severity, r_format, __VA_ARGS__);

#define dbgloglogn(r_cat, r_severity, r_format) if (dbg.dvptr->ids_enabled[r_cat]) dbg.dvptr->add_printf(r_cat, master_clock, r_severity, r_format);
