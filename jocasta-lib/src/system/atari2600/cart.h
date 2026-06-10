#pragma once

#include "helpers/int.h"
#include "helpers/simplebuf.h"
#include "helpers/physical_io.h"

namespace atari2600 {

struct CART {
    simplebuf8 ROM{};
    u32 addr_mask;

    template<bool peek> void bus_cycle(u16 addr, u8 *data, bool rw);
    void load(multi_file_set &mfs, physical_io_device &which_pio);
    void reset();
};

}
