#pragma once

#include "helpers/int.h"

namespace TMP95C061 {

struct CHIPSELECT {
    void reset();

    u8 read(u8 addr);
    void write(u8 addr, u8 data);

    int select(u32 addr) const;

    struct channel {
        u32 address{};
        u32 mask{};
        bool enable{};
        bool byte_width{};
        u8 timing{};
        bool mode{};
        bool cas{};
        bool selects(u32 addr) const;
    };

    channel cs0, cs1, cs2, cs3, csx;
};

}
