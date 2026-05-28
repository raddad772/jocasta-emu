//
// Created by . on 5/9/26.
//

#include "ngp_bus.h"

namespace NGP {
core::core(jsm::systems kind) {
    is_color = kind == jsm::systems::NEOGEO_POCKET_COLOR;
}
}
