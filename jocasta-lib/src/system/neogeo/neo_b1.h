//
// Created by . on 5/12/26.
//
#include "helpers/int.h"
#pragma once
#include <memory>

namespace NEOGEO {
struct core;

struct NEOB1 {
    explicit NEOB1(core *parent);
    core *bus;
    std::unique_ptr<u32[]> debug_output{};
    template<bool do_debug> void draw_line(u32 y, u32 *linebuf) const;
};
}
