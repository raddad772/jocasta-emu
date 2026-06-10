#pragma once

#include "helpers/int.h"

struct M6532 {
    u8 RAM[128]{};

    void reset();
    void cycle();
    template<bool peek> void bus_cycle(u16 addr, u8 *data, bool rw);

    struct {
        u32 cycle_mask{};
        u32 counter{};
        u32 reload_val{};
        u32 highspeed_mode{};
        u32 cycle{};
    } timer{};

    struct {
        u8 underflow_since_instat{};
        u8 underflow_since_timnnt{};

        u8 SWACNT{}, SWBCNT{}; // Direction control for...
        u8 SWCHA{}, SWCHB{}; // These!

    } io{};

private:
    template<bool peek> u8 io_read(u16 addr);
    void io_write(u16 addr, u8 val);
};
