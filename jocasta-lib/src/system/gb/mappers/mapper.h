#pragma once

#include "helpers/int.h"
#include "helpers/serialize/serialize.h"
#include "../gb_enums.h"
#include "system/gb/cart.h"

namespace GB {
struct CART;
struct core;
struct CLOCK;
}

namespace GB::MAPPER {

struct base {
    void *ptr;

    mappers which;

    void (*reset)(base*);
    void (*set_cart)(base*, CART*);

    void (*serialize)(base*, serialized_state &state);
    void (*deserialize)(base*, serialized_state &state);
    //void (*set_BIOS)(base*, u8*, u32);

    u32 (*CPU_read)(base*, u32, u32, u32);
    void (*CPU_write)(base*, u32, u32);
};

base* new_mapper(CLOCK* clock, core* bus, mappers which);
void delete_mapper(base* whom);

}
