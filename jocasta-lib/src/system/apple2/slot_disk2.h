#pragma once

#include "slot.h"
#include "helpers/simplebuf.h"
#include "component/misc/apple_iwm/apple_iwm.h"
#include "component/floppy/apple2_floppy.h"

namespace apple2 {
struct core;

namespace slot {

struct disk2 : interface {
    disk2(core *bus, u32 slot_in);

    APPLE_IWM::IWM<APPLE_IWM::apple2, core, floppy::apple2::DISC> iwm;
    simplebuf8 rom{};

    bool has_rom() const override;
    u8   rom_read(u32 addr, u8 old_val) override;
    u8   io_read(u32 addr, u8 old_val, bool has_effect) override;
    void io_write(u32 addr, u8 val) override;
    void cycle() override;
    void reset() override;
};

}}
