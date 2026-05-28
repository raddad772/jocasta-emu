#pragma once

#include "helpers/int.h"
#include "helpers/physical_io.h"

namespace NEOGEO {
struct controller_4button {
    [[nodiscard]] u8 read_controls() const;
    [[nodiscard]] u8 read_buttons() const;
    void write_outputs(u8 val);

    physical_io_device *pio{};

    void setup_pio(physical_io_device *d, u32 num, const char*name, bool connected);
};
}