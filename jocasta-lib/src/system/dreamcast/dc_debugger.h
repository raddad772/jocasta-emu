#pragma once

enum DC_DBLOG_CATEGORIES {
    DCD_UNKNOWN = 0,

    DCD_SH4_INSTRUCTION,
    DCD_SH4_EXCEPTION,
    DCD_SH4_IRQ,
    DCD_SH4_REGREAD,
    DCD_SH4_REGWRITE,

    DCD_GENERAL,
    DCD_BUS_CONSOLE,
    DCD_BUS_READ,
    DCD_BUS_WRITE,

    DCD_G2_DMA_START,
    DCD_G2_DMA_END,
    DCD_G2_DMA_SUSPEND,

    DCD_AICA_ARM_INSTRUCTION,
    DCD_AICA_ARM_RESET,
    DCD_AICA_ARM_EXCEPTION,

    DCD_GDROM_GENERAL,
    DCD_GDROM_CMD_WRITE,
    DCD_GDROM_ATA_CMD,
    DCD_GDROM_SPI_PACKET,
    DCD_GDROM_STATE_CHANGE,
};

#define dbgloglog_aica(r_cat, r_severity, r_format, ...) if (dbg.dvptr->ids_enabled[r_cat]) dbg.dvptr->add_printf(r_cat, *master_clock, r_severity, r_format, __VA_ARGS__)
#define dbgloglog_aican(r_cat, r_severity, r_format) if (dbg.dvptr->ids_enabled[r_cat]) dbg.dvptr->add_printf(r_cat, *master_clock, r_severity, r_format)
#define dbgloglog(r_cat, r_severity, r_format, ...) if (dbg.dvptr->ids_enabled[r_cat]) dbg.dvptr->add_printf(r_cat, master_cycles, r_severity, r_format, __VA_ARGS__)
#define dbgloglog_bus(r_cat, r_severity, r_format, ...) if (bus->dbg.dvptr->ids_enabled[r_cat]) bus->dbg.dvptr->add_printf(r_cat, bus->master_cycles, r_severity, r_format, __VA_ARGS__)
#define dbgloglog_thbus(r_cat, r_severity, r_format, ...) if (th->bus->dbg.dvptr->ids_enabled[r_cat]) th->bus->dbg.dvptr->add_printf(r_cat, th->bus->master_cycles+th->bus->clock.waitstates, r_severity, r_format, __VA_ARGS__)
#define dbgloglog_busn(r_cat, r_severity, r_format) if (bus->dbg.dvptr->ids_enabled[r_cat]) bus->dbg.dvptr->add_printf(r_cat, bus->master_cycles, r_severity, r_format)

