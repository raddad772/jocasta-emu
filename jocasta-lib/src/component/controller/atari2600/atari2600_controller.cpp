//
// Created by . on 5/17/26.
//

#include "atari2600_controller.h"

namespace atari2600 {


void CONTROLLER::setup(u32 num, const char*name, bool connected_in, physical_io_device *d)
{
    pio = d;
    d->init(HID_CONTROLLER, false, false, true, true);
    connected = connected_in;

    snprintf(d->controller.name, sizeof(d->controller.name), "%s", name);
    d->id = num;
    d->kind = HID_CONTROLLER;
    d->connected = connected;
    d->enabled = connected;

    auto *cnt = &d->controller;

    // up down left right a b start select. in that order
    pio_new_button(cnt, "up", DBCID_co_up);
    pio_new_button(cnt, "down", DBCID_co_down);
    pio_new_button(cnt, "left", DBCID_co_left);
    pio_new_button(cnt, "right", DBCID_co_right);
    pio_new_button(cnt, "fire", DBCID_co_fire1);
}

u8 CONTROLLER::read_INPT() {
    if (!connected) return 1;
    return inputs.fire ^ 1;
}

u8 CONTROLLER::read_SWCHA() {
    if (!connected) return 0x0F;
    return ((inputs.up ^ 1) << 0) |
        ((inputs.down ^ 1) << 1) |
        ((inputs.left ^ 1) << 2) |
        ((inputs.right ^ 1) << 3);
}

void CONTROLLER::latch() {
    if (!connected) return;
    auto *p = pio;
    auto *bl = &p->controller.digital_buttons;

#define B_GET(button, num) { inputs. button = bl->at(num).state; }
    B_GET(up, 0);
    B_GET(down, 1);
    B_GET(left, 2);
    B_GET(right, 3);
    B_GET(fire, 4);
#undef B_GET

}
}
