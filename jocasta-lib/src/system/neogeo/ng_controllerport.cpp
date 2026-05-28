//
// Created by . on 5/12/26.
//

#include "ng_bus.h"
#include "component/controller/neogeo4/neogeo4.h"

namespace NEOGEO {
controller_port::controller_port(core *parent) :
bus(parent) {

}

void controller_port::connect(controller_kinds in_kind, void *ptr) {
    kind = in_kind;
    device = ptr;
}

void controller_port::write_outputs(u8 data) const {
    switch (kind) {
        case CK_NONE:
            return;
        case CK_4BUTTON:
            static_cast<controller_4button*>(device)->write_outputs(data);
            return;
    }
    NOGOHERE;
}

u8 controller_port::read_controls() const {
    switch (kind) {
        case CK_NONE:
            return 3;
        case CK_4BUTTON:
            return static_cast<controller_4button*>(device)->read_controls() & 3;
    }
    NOGOHERE;
}


u8 controller_port::read_buttons() const {
    switch (kind) {
        case CK_NONE:
            return 0xFF;
        case CK_4BUTTON:
            return static_cast<controller_4button*>(device)->read_buttons();
    }
    NOGOHERE;
}

}
