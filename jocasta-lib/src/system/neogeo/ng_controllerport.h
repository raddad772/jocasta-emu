#include "helpers/int.h"

#pragma once

namespace NEOGEO {


enum controller_kinds {
    CK_NONE = 0,
    CK_4BUTTON = 1
};

struct core;
struct controller_port {
    explicit controller_port(core *parent);
    core *bus;

    void *device{};
    controller_kinds kind{};

    void write_outputs(u8 data) const;
    [[nodiscard]] u8 read_buttons() const;
    [[nodiscard]] u8 read_controls() const;

    void connect(controller_kinds in_kind, void *ptr);
};

}