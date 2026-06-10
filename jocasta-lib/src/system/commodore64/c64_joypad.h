//
// Created by . on 5/22/26.
//

#pragma once

#include "helpers/int.h"
#include "helpers/physical_io.h"
#include "helpers/cvec.h"

namespace C64 {

struct JOYPORT {
    // Set up a HID_CONTROLLER physical_io_device for this port.
    void setup(std::vector<physical_io_device>& IOs, u32 port_num);

    // Returns the pin state: 0xFF with bits 0-4 cleared for any pressed button
    // (active-low hardware: pressed = 0, released = 1).
    // Bit 0 = Up, Bit 1 = Down, Bit 2 = Left, Bit 3 = Right, Bit 4 = Fire
    u8 pin_state();

    cvec_ptr<physical_io_device> pio;
};

} // namespace C64
