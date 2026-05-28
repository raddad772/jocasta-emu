#pragma once

#define sh4dbgloglog(r_cat, r_severity, r_format, ...) if (dbg.dvptr->ids_enabled[r_cat]) dbg.dvptr->add_printf(r_cat, *tmu.master_cycles, r_severity, r_format, __VA_ARGS__)
#define sh4dbgloglog_bus(r_cat, r_severity, r_format, ...) if (bus->dbg.dvptr->ids_enabled[r_cat]) bus->dbg.dvptr->add_printf(r_cat, *cpu->tmu.master_cycles, r_severity, r_format, __VA_ARGS__)