#include "apple2_bus.h"
#include "slot_disk2.h"

namespace apple2 {
namespace slot {

disk2::disk2(core *bus, u32 slot_in)
    : iwm(bus, 2)
{
    slot_num = slot_in;
}

bool disk2::has_rom() const
{
    return !!rom.ptr;
}

u8 disk2::rom_read(u32 addr, u8 old_val)
{
    return rom.ptr ? rom.ptr[addr & rom.mask] : old_val;
}

u8 disk2::io_read(u32 addr, u8 old_val, bool has_effect)
{
    return (u8)iwm.do_read(addr, 0xFF, old_val, has_effect);
}

void disk2::io_write(u32 addr, u8 val)
{
    iwm.do_write(addr, 0xFF, val);
}

void disk2::cycle()
{
    iwm.clock();
}

void disk2::reset()
{
    iwm.reset();
}

}}
