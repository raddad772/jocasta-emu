#pragma once

#include "helpers/int.h"
#include "helpers/debugger/debugger.h"

enum A26_DBLOG_CATEGORIES : u32 {
    A26_CAT_UNKNOWN = 0,
    A26_CAT_CPU_INSTRUCTION,
    A26_CAT_CPU_READ,
    A26_CAT_CPU_WRITE,
};

#define A26_EVENT_CATEGORY_TIA 0
#define A26_EVENT_CATEGORY_CPU 1

enum A26_EVENTLOG_CATEGORIES : u32 {
    A26_EVENT_RIOT_IO_READ,
    A26_EVENT_RIOT_IO_WRITE,
    A26_EVENT_TIA_READ,
    A26_EVENT_TIA_WRITE,
    A26_EVENT_MAX
};

#define dbgloglog(r_cat, r_severity, r_format, ...) if (dbg.dvptr->ids_enabled[r_cat]) dbg.dvptr->add_printf(r_cat, master_clock, r_severity, r_format, __VA_ARGS__);
