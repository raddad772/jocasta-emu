#pragma once

#include "helpers/int.h"
#include "helpers/physical_io.h"
#include "helpers/cvec.h"
#include "helpers/scheduler.h"
#include "helpers/serialize/serialize.h"

namespace CDP1861 {

struct BUS {
    u8 DMA_OUT;
    u8 SC;
    u8 D;
    u8 IRQ;
    u8 EF1;
};

struct core {
    explicit core() = default;
    void reset();
    void cycle();
    u8 INP(u8 old);
    void OUT(u8 val);

    BUS bus{};
    u8 *cur_output{};
    u8 *line_output{};
    cvec_ptr<physical_io_device> display_ptr{};
    JSM_DISPLAY *display{};
    u16 x{}, y{};
    u64 master_frame{};

    void serialize(serialized_state &state);
    void deserialize(serialized_state &state);

private:
    void new_scanline();
    void new_frame();

    bool display_area;
    struct {
        u8 enable{1};
        struct {
            u8 enable{1};
        } latch{};
    } io{};
};
};