//
// Created by Dave on 4/14/24.
//

#include <cstdio>
#include <cstring>
#include "cart.h"
#include "atari2600_bus.h"

namespace atari2600 {

template void CART::bus_cycle<false>(u16 addr, u8 *data, bool rw);
template void CART::bus_cycle<true>(u16 addr, u8 *data, bool rw);

template<bool peek>
void CART::bus_cycle(u16 addr, u8 *data, bool rw)
{
    addr &= addr_mask;
    if (!rw) {
        const u8 *rom_ptr = ROM.ptr;
        *data = rom_ptr[addr];
    }
    else {
        printf("\nUNSUPPORTED WRITE TO CART %03x", addr);
    }
}

void CART::load(multi_file_set &mfs, physical_io_device &which_pio)
{
    auto &fb = mfs.files[0].buf;
    ROM.allocate(fb.size);
    memcpy(ROM.ptr, fb.ptr, fb.size);
    addr_mask = fb.size - 1;
}

void CART::reset() {

}
}