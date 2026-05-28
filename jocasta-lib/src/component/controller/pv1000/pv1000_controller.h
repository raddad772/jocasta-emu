#pragma once

#include "helpers/int.h"
#include "helpers/sys_interface.h"
#include "helpers/cvec.h"

struct CASIO_PV1000_controller {
    cvec_ptr<physical_io_device> device_ptr{};

    u8 read();
    void write(u8 val);
    void setup_pio(physical_io_device &d, u32 num_in, const char *name, bool connected_in);

    bool connected{};
    u32 num{};

    struct {
        u32 input{};
    } io{};
};