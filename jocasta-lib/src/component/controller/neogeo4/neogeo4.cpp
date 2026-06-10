//
// Created by . on 5/12/26.
//

#include "neogeo4.h"

namespace NEOGEO {

u8 controller_4button::read_buttons() const {
    auto& bl = pio->controller.digital_buttons;
#define B_GET(num) { out |= bl.at(num).state << num; }
    u8 out = 0;
    for (u32 i = 0; i < 8; i++) {
        B_GET(i);
    }

    return out ^ 0xFF;
}
#undef B_GET

#define B_GET(num) { out |= bl.at(num).state << (num - 8); }
u8 controller_4button::read_controls() const {
    auto& bl = pio->controller.digital_buttons;
    u8 out = 0;
    B_GET(8);
    B_GET(9);
    return out ^ 3;
}
#undef B_GET

void controller_4button::write_outputs(u8 val) {

}

void controller_4button::setup_pio(physical_io_device *d, u32 num, const char *name, bool connected) {
    d->init(HID_CONTROLLER, false, false, true, true);

    snprintf(d->controller.name, sizeof(d->controller.name), "%s", name);
    d->id = num;
    d->kind = HID_CONTROLLER;
    d->connected = connected;
    d->enabled = connected;

    JSM_CONTROLLER* cnt = &d->controller;

    pio_new_button(cnt, "up", DBCID_co_up);
    pio_new_button(cnt, "down", DBCID_co_down);
    pio_new_button(cnt, "left", DBCID_co_left);
    pio_new_button(cnt, "right", DBCID_co_right);
    pio_new_button(cnt, "a", DBCID_co_fire1);
    pio_new_button(cnt, "b", DBCID_co_fire2);
    pio_new_button(cnt, "c", DBCID_co_fire3);
    pio_new_button(cnt, "d", DBCID_co_fire4);
    pio_new_button(cnt, "start", DBCID_co_start);
    pio_new_button(cnt, "select", DBCID_co_select);
    pio = d;
}
}
