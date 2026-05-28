#pragma once

#include "helpers/int.h"
#include "helpers/physical_io.h"

namespace atari2600 {

struct CONTROLLER_INPUTS {
    u32 fire{}, up{}, down{}, left{}, right{};
};

struct CONTROLLER {
    bool connected{};
    physical_io_device *pio{};
    void latch();
    void setup(u32 num, const char*name, bool connected_in, physical_io_device *d);
    CONTROLLER_INPUTS inputs{};
    u8 read_SWCHA();
    u8 read_INPT();
};
}