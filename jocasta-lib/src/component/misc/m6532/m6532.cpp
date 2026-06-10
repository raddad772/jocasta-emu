//
// Created by RadDad772 on 4/14/24.
//

#include "m6532.h"

void M6532::reset()
{
    // TODO
    timer.counter = 0;
    timer.cycle_mask = 0;
    timer.highspeed_mode = 0;
    timer.reload_val = 0;
    timer.cycle = 0;

    io.SWACNT = 0;
    io.SWBCNT = 0;
    io.SWCHA = 0xFF;
    io.SWCHB = 0xFF;
    io.underflow_since_timnnt = 0;
    io.underflow_since_instat = 0;
}

template u8 M6532::io_read<false>(u16 addr);
template u8 M6532::io_read<true>(u16 addr);

template<bool peek>
u8 M6532::io_read(u16 addr)
{
    u32 t;
    switch((addr & 7) | 0x280) {
        case 0x280: // SWCHA: port A, input or output R/W
            return io.SWCHA;
        case 0x281: // SWACNT: port A DDR R/W
            return io.SWACNT;
        case 0x282: // SWCHB: port B, input or output R/W
            return io.SWCHB;
        case 0x283: // SWBCNT: port B DDR R/W
            return io.SWBCNT;
        case 0x284: // INTIM: timer output (read-only)
        case 0x286:
            if constexpr (!peek) timer.highspeed_mode = 0;
            return timer.counter;
        case 0x285: // INSTAT: timer status. read-only and undocumented
        case 0x287:
            t = io.underflow_since_instat << 6;
            if constexpr (!peek) io.underflow_since_instat = 0;
            return (io.underflow_since_timnnt << 7) | t;
        default: return 0;
    }
}

void M6532::cycle()
{
    u32 do_decrement = 0;
    if (timer.highspeed_mode) do_decrement = 1;
    else {
        timer.cycle = (timer.cycle + 1) & timer.cycle_mask;
        do_decrement = timer.cycle == 0;
    }
    if (do_decrement) {
        timer.counter = (timer.counter - 1) & 0xFF;
        if (timer.counter == 0xFF) {// Underflow
            io.underflow_since_instat = 1;
            io.underflow_since_timnnt = 1;
            timer.highspeed_mode = 1;
        }
    }
}

void M6532::io_write(u16 addr, u8 val)
{
    u32 vmask;
    switch((addr & 7) | 0x280) {
        case 0x280: // SWCHA: port A, input or output R/W
            vmask = io.SWACNT;
            io.SWCHA = (io.SWCHA & (~vmask)) | (val & vmask);
            return;
        case 0x281: // SWACNT: port A DDR R/W
            io.SWACNT = val;
            return;
        case 0x282: // SWCHB: port B, input or output R/W
            vmask = io.SWBCNT;
            io.SWCHB = (io.SWCHB & (~vmask)) | (val & vmask);
            return;
        case 0x283: // SWBCNT: port B DDR R/W
            io.SWBCNT = val;
            return;
        case 0x284: // TIM1T: set 1-clock interval
            timer.counter = val & 0xFF;
            timer.reload_val = val & 0xFF;
            timer.highspeed_mode = 0;
            timer.cycle_mask = 0;
            timer.cycle = 0;

            io.underflow_since_timnnt = 0;
            return;
        case 0x285: // TIM8T: set 8-clock interval
            timer.counter = val & 0xFF;
            timer.reload_val = val & 0xFF;
            timer.highspeed_mode = 0;
            timer.cycle_mask = 7;
            timer.cycle = 7;

            io.underflow_since_timnnt = 0;
            return;
        case 0x286: // TIM64T: set 64-clock interval
            timer.counter = val & 0xFF;
            timer.reload_val = val & 0xFF;
            timer.highspeed_mode = 0;
            timer.cycle_mask = 63;
            timer.cycle = 63;

            io.underflow_since_timnnt = 0;
            return;
        case 0x287: // T1024T: set 1024-clock interval
            timer.counter = val & 0xFF;
            timer.reload_val = val & 0xFF;
            timer.highspeed_mode = 0;
            timer.cycle_mask = 1023;
            timer.cycle = 1023;

            io.underflow_since_timnnt = 0;
            return;
        default: return;
    }
}
template void M6532::bus_cycle<true>(u16 addr, u8 *data, bool rw);
template void M6532::bus_cycle<false>(u16 addr, u8 *data, bool rw);

template<bool peek>
void M6532::bus_cycle(u16 addr, u8 *data, bool rw)
{
    addr &= 0x2FF;
    if (addr & 0x200) { // 280... switches
        if (!rw)
            *data = io_read<peek>(addr);
        else
            io_write(addr, *data);
    }
    else { // RAM!
        if (!rw)
            *data = RAM[addr & 0x7F];
        else
            RAM[addr & 0x7F] = *data;
    }
}
