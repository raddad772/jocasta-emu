#pragma once
#include "helpers/int.h"
#include "helpers/sys_interface.h"
#include "component/cpu/tlcs900h/tlcs900h.h"
#include "component/cpu/z80/z80.h"
namespace NGP {
struct core : jsm_system {
    core(jsm::systems kind);

    Z80::core z80{false};
    TLCS900H::core tlcs900h{};

    bool is_color{};
};
}
