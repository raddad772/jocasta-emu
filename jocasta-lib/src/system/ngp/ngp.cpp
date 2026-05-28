//
// Created by . on 5/9/26.
//

#include "ngp.h"
#include "ngp_bus.h"

jsm_system *ngp_new(jsm::systems variant) {
    return new NGP::core(variant);
}

void ngp_delete(jsm_system *sys) {
    auto *th = dynamic_cast<NGP::core *>(sys);
    for (auto &pio : th->IOs) {
        if (pio.kind == HID_CART_PORT) {
            if (pio.cartridge_port.unload_cart) pio.cartridge_port.unload_cart(sys);
        }
    }
}
