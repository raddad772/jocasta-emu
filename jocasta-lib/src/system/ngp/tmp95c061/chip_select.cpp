#include "chip_select.h"

namespace TMP95C061 {

bool CHIPSELECT::channel::selects(u32 addr) const
{
    if (!enable) return false;
    return ((addr ^ address) & ~mask & 0xFFFFFF) == 0;
}

void CHIPSELECT::reset()
{
    cs0 = {}; cs0.address = 0xFF0000; cs0.mask = 0x1FFFFF;
    cs1 = {}; cs1.address = 0xFF0000; cs1.mask = 0x3FFFFF;
    cs2 = {}; cs2.address = 0xFF0000; cs2.mask = 0x7FFFFF;
    cs3 = {}; cs3.address = 0xFF0000; cs3.mask = 0x7FFFFF;
    csx = {};
}

int CHIPSELECT::select(u32 addr) const
{
    if (cs0.selects(addr)) return 0;
    if (cs1.selects(addr)) return 1;
    if (cs2.enable && (cs2.mode ? cs2.selects(addr) : addr >= 0x000100)) return 2;
    if (cs3.selects(addr)) return 3;
    return -1;
}

static u32 set_msar(u32 address, u8 data) { return (address & 0x00FFFF) | (static_cast<u32>(data) << 16); }
static u8 get_msar(u32 address) { return (address >> 16) & 0xFF; }

static inline void put_bit(u32 &m, int bit, bool v)
{
    m = (m & ~(1u << bit)) | (static_cast<u32>(v) << bit);
}

u8 CHIPSELECT::read(u8 addr)
{
    switch (addr) {
        case 0x3c: return get_msar(cs0.address);
        case 0x3d: return ((cs0.mask >> 8) & 0x3) | (((cs0.mask >> 15) & 0x3F) << 2);
        case 0x3e: return get_msar(cs1.address);
        case 0x3f: return ((cs1.mask >> 8) & 0x3) | (((cs1.mask >> 16) & 0x3F) << 2);
        case 0x5c: return get_msar(cs2.address);
        case 0x5d: return (cs2.mask >> 15) & 0xFF;
        case 0x5e: return get_msar(cs3.address);
        case 0x5f: return (cs3.mask >> 15) & 0xFF;
        case 0x68: case 0x69: case 0x6a: case 0x6b: case 0x6c: return 0x00;
        default: return 0x00;
    }
}

void CHIPSELECT::write(u8 addr, u8 data)
{
    switch (addr) {
        case 0x3c: cs0.address = set_msar(cs0.address, data); return;
        case 0x3d: {
            put_bit(cs0.mask, 8, data & 1);
            for (int b = 9; b <= 14; b++) put_bit(cs0.mask, b, (data >> 1) & 1);
            put_bit(cs0.mask, 15, (data >> 2) & 1);
            put_bit(cs0.mask, 16, (data >> 3) & 1);
            put_bit(cs0.mask, 17, (data >> 4) & 1);
            put_bit(cs0.mask, 18, (data >> 5) & 1);
            put_bit(cs0.mask, 19, (data >> 6) & 1);
            put_bit(cs0.mask, 20, (data >> 7) & 1);
            return;
        }
        case 0x3e: cs1.address = set_msar(cs1.address, data); return;
        case 0x3f: {
            put_bit(cs1.mask, 8, data & 1);
            for (int b = 9; b <= 15; b++) put_bit(cs1.mask, b, (data >> 1) & 1);
            put_bit(cs1.mask, 16, (data >> 2) & 1);
            put_bit(cs1.mask, 17, (data >> 3) & 1);
            put_bit(cs1.mask, 18, (data >> 4) & 1);
            put_bit(cs1.mask, 19, (data >> 5) & 1);
            put_bit(cs1.mask, 20, (data >> 6) & 1);
            put_bit(cs1.mask, 21, (data >> 7) & 1);
            return;
        }
        case 0x5c: cs2.address = set_msar(cs2.address, data); return;
        case 0x5d: cs2.mask = (cs2.mask & ~(0xFFu << 15)) | (static_cast<u32>(data) << 15); return;
        case 0x5e: cs3.address = set_msar(cs3.address, data); return;
        case 0x5f: cs3.mask = (cs3.mask & ~(0xFFu << 15)) | (static_cast<u32>(data) << 15); return;
        case 0x68: cs0.timing = data & 3; cs0.byte_width = (data >> 2) & 1; cs0.enable = (data >> 4) & 1; return;
        case 0x69: cs1.timing = data & 3; cs1.byte_width = (data >> 2) & 1; cs1.enable = (data >> 4) & 1; return;
        case 0x6a: cs2.timing = data & 3; cs2.byte_width = (data >> 2) & 1; cs2.mode = (data >> 3) & 1; cs2.enable = (data >> 4) & 1; return;
        case 0x6b: cs3.timing = data & 3; cs3.byte_width = (data >> 2) & 1; cs3.cas = (data >> 3) & 1; cs3.enable = (data >> 4) & 1; return;
        case 0x6c: csx.timing = data & 3; csx.byte_width = (data >> 2) & 1; return;
        default: return;
    }
}

}
