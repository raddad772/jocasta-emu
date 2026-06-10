#pragma once

#include "helpers/int.h"

namespace TMP95C061 {

struct channel {
    u8 buffer{};
    u8 cr{};
    u8 mod{};
    u8 br{};
};

struct SIO {
    void reset() { sc[0] = channel{}; sc[1] = channel{}; }

    u8 read(u8 addr);
    void write(u8 addr, u8 val);

    channel sc[2]{};
};

}
