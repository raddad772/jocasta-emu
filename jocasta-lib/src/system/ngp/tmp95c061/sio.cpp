#include "sio.h"

namespace TMP95C061 {

u8 SIO::read(u8 addr)
{
    u32 n = (addr >= 0x54) ? 1 : 0;
    channel &c = sc[n];
    switch (addr - (n ? 0x54 : 0x50)) {
        case 0: return c.buffer;
        case 1: return c.cr & ~0x1C;
        case 2: return c.mod;
        default: return c.br;
    }
}

void SIO::write(u8 addr, u8 val)
{
    u32 n = (addr >= 0x54) ? 1 : 0;
    channel &c = sc[n];
    switch (addr - (n ? 0x54 : 0x50)) {
        case 0: c.buffer = val; return;
        case 1: c.cr = val; return;
        case 2: c.mod = val; return;
        default: c.br = val; return;
    }
}

}
