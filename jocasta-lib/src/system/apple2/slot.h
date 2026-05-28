#pragma once

#include <cstring>
#include "helpers/int.h"

namespace apple2 {
namespace slot {

enum type { NONE, DISK2, MOCKINGBOARD_B };

inline const char* type_to_str(type t) {
    switch (t) {
        case DISK2:          return "disk2";
        case MOCKINGBOARD_B: return "mockingboard_b";
        default:             return "empty";
    }
}

inline type str_to_type(const char* s) {
    if (!s) return NONE;
    if (strcmp(s, "disk2") == 0)          return DISK2;
    if (strcmp(s, "mockingboard_b") == 0) return MOCKINGBOARD_B;
    return NONE;
}

struct interface {
    u32 slot_num{};
    virtual ~interface() = default;
    virtual u8   io_read(u32 addr, u8 old_val, bool has_effect) = 0;
    virtual void io_write(u32 addr, u8 val) = 0;
    virtual bool has_rom() const { return false; }
    virtual u8   rom_read(u32 addr, u8 old_val) { return old_val; }
    virtual void cycle() = 0;
    virtual void reset() = 0;
};

}}
