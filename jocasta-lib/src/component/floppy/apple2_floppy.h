#pragma once

#include "generic_floppy.h"

namespace floppy::apple2 {
struct DISC {
    generic::DISC<35,16,3328> disc{};
    bool write_protect{true};

    void save();
    bool load(const char *fname, BUF &b);
    void fill_tracks();

    bool load_nib(BUF &b);
    bool load_plain(BUF &b, u32 sector_order);
    void encode_track(generic::TRACK<16, 3328> &track);
};
}
