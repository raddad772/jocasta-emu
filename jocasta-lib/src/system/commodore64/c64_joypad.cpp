//
// Created by . on 5/22/26.
//
#include <cstdio>
#include "c64_joypad.h"

namespace C64 {

void JOYPORT::setup(std::vector<physical_io_device>& IOs, u32 port_num)
{
    physical_io_device& d = IOs.emplace_back();
    d.init(HID_CONTROLLER, 0, 0, 1, 1);
    pio.make(IOs, IOs.size() - 1);

    snprintf(d.controller.name, sizeof(d.controller.name), "Joystick Port %u", port_num);
    d.id = port_num;
    d.kind = HID_CONTROLLER;
    d.connected = 1;
    d.enabled = 1;

    JSM_CONTROLLER* cnt = &d.controller;
    // Button order must match the bit positions read by pin_state()
    pio_new_button(cnt, "up",    DBCID_co_up);    // bit 0
    pio_new_button(cnt, "down",  DBCID_co_down);  // bit 1
    pio_new_button(cnt, "left",  DBCID_co_left);  // bit 2
    pio_new_button(cnt, "right", DBCID_co_right); // bit 3
    pio_new_button(cnt, "fire",  DBCID_co_fire1); // bit 4
}

u8 JOYPORT::pin_state()
{
    auto& cnt = pio.get().controller;
    const auto& bl = cnt.digital_buttons;
    u8 pressed = 0;
    if (bl.size() >= 5) {
        if (bl[0].state) pressed |= 0x01; // up
        if (bl[1].state) pressed |= 0x02; // down
        if (bl[2].state) pressed |= 0x04; // left
        if (bl[3].state) pressed |= 0x08; // right
        if (bl[4].state) pressed |= 0x10; // fire
    }

    return static_cast<u8>(~pressed);
}

} // namespace C64
