//
// Created by . on 3/28/26.
//

#include "pv1000_controller.h"

u8 CASIO_PV1000_controller::read() {
    return io.input;
}

void CASIO_PV1000_controller::write(u8 val) {
    if (!connected) return;
    physical_io_device& p = device_ptr.get();
    if (p.connected) {
        io.input = 0;
        HID_digital_button* b;
#define B_GET(button_num, bit_num) { b = &p.controller.digital_buttons.at(button_num); io.input |= b->state << bit_num; }
        if (val & 1) {
            // select, start
            B_GET(7, 0);
            B_GET(6, 1);
        }
        if (val & 2) {
            // down, right
            B_GET(1, 0);
            B_GET(3, 1);
        }
        if (val & 4) {
            // left, up
            B_GET(2, 0);
            B_GET(0, 1);
        }
        if (val & 8) {
            //1, 2
            B_GET(4, 0);
            B_GET(5, 1);
        }
    }
}

void CASIO_PV1000_controller::setup_pio(physical_io_device &d, u32 num_in, const char *name, bool connected_in) {
    d.init(HID_CONTROLLER, true, connected_in, true, false);
    num = num_in;
    connected = connected_in;

    snprintf(d.controller.name, sizeof(d.controller.name), "%s", name);
    d.id = num;
    d.kind = HID_CONTROLLER;
    d.connected = connected;
    d.enabled = connected;

    JSM_CONTROLLER* cnt = &d.controller;

    // up down left right a b start select. in that order
    pio_new_button(cnt, "up", DBCID_co_up);
    pio_new_button(cnt, "down", DBCID_co_down);
    pio_new_button(cnt, "left", DBCID_co_left);
    pio_new_button(cnt, "right", DBCID_co_right);
    pio_new_button(cnt, "1", DBCID_co_fire1);
    pio_new_button(cnt, "2", DBCID_co_fire2);
    pio_new_button(cnt, "start", DBCID_co_start);
    pio_new_button(cnt, "select", DBCID_co_select);

}
