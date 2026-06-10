//
// Created by . on 4/2/26.
//

#include <cassert>
#include <cstdio>

#include "dc_bus.h"
#include "dc_maple.h"
#include "holly/holly.h"

namespace DREAMCAST::MAPLE {

u32 PORT::read(bool& more)
{
    switch(device_kind) {
        case DK_NONE:
            more = false;
            return 0xFFFFFFFF;
        case DK_CONTROLLER:
            return read_device(device_ptr, more);
    }
    NOGOHERE;
    return 0;
}

void PORT::write(u32 data)
{
    switch(device_kind) {
        case DK_NONE:
            return;
        case DK_CONTROLLER:
            write_device(device_ptr, data);
            return;
    }
    NOGOHERE;
}

void core::reset() {

}

u64 core::read_io(u32 addr, u8 sz, bool *success) {
    switch (addr) {
        case 0x005F6C04:  { return SB_MDSTAR; }
        case 0x005F6C10:  { return SB_MDTSEL; }
        case 0x005F6C14:  { return SB_MDEN; }
        case 0x005F6C18:  { return SB_MDST; }
        case 0x005F6C80:  { return SB_MSYS.u; }
        case 0x005F6C8C:  { return SB_MDAPRO.u; }
        case 0x005F6CE8:  { return SB_MMSEL; }
    }
    *success = false;
    printf("\nMAPLE read bad reg %08x(%d)", addr, sz);
    return 0;
}

union MAPLE_CMD {
    struct {
        u32 transfer_len: 8;
        u32 pattern: 3;
        u32 : 5;
        u32 port_select: 2;
        u32 : 13;
        u32 end_flag: 1;
    };
    u32 u{};
};

    static void dump_RAM_to_console(u32 print_addr, void *src, u32 len)
    {
        auto *ptr = static_cast<u8 *>(src);
        // 16 bytes per line
        u32 bytes_printed = 0;
        for (u32 laddr = 0; laddr < len; laddr += 16) {
            printf("\n%08X   ", print_addr+bytes_printed);
            for (u32 i = 0; i < 16; i++) {
                printf("%02X ", static_cast<u32>(*ptr));
                ptr++;
                bytes_printed++;
                if (bytes_printed >= len) return;
            }
        }
    }

void core::dma_init()
{
    if (SB_MDEN == 0) {
        printf("\nCan't enable maple dma if disabled globally!");
        return;
    }
    if (vblank_repeat_trigger && SB_MDTSEL == 1) {
        printf("\nSkipping MapleDMA due to vblank repeat trigger");
        return;
    }
    if (SB_MDTSEL == 1) vblank_repeat_trigger = true;
    //printf("\nMAPLE DMA TRANSFER cycle:%llu", bus->master_cycles);
    u32 caddr = SB_MDSTAR;
    for (u32 i = 0; i < 0xFFFF; i++) {
        MAPLE_CMD cmd;
        cmd.u = bus->mainbus_read<4, true>(caddr);
        caddr+=4;
        //printf("\nMAPLE CMD #%d (%08x): %08x (pattern:%03d transfer_len:%d port: %d)", i, caddr, cmd.u, cmd.pattern, cmd.transfer_len, cmd.port_select);
        assert(cmd.pattern == 0b000);

        u32 receieve_ptr = bus->mainbus_read<4, true>(caddr);
        caddr += 4;

        for (i32 tx_index = 0; tx_index < (cmd.transfer_len+1); tx_index++) {
            u32 data = bus->mainbus_read<4, true>(caddr);
            caddr += 4;
            //printf("\nWRITE CMD WORD %08x", data);
            ports[cmd.port_select].write(data);
        }

        // Now receive
        bool more;
        u32 num_transfer = 0;
        for (u32 rx_ct = 0; rx_ct < 128; rx_ct++) {
            u32 data = ports[cmd.port_select].read(more);
            //printf("\n%08x more:%d write to %08x", data, more, receieve_ptr);
            bus->mainbus_write<4, true>(receieve_ptr, data);
            //printf("\n%08x %d", data, more);
            receieve_ptr += 4;
            num_transfer++;
            if ((rx_ct == 0) && (data == 0xFFFFFFFF)) break;
            if (!more) {
                break;
            }
        }
        if (cmd.end_flag) break;
    }
    bus->holly.raise_interrupt(HOLLY::hirq_maple_dma, -1);
    SB_MDST = 0;
}

void core::write_io(u32 addr, u8 sz, u64 val, bool *success) {
    switch (addr) {
        case 0x005F6C04: { SB_MDSTAR = (val & 0x1FFFFFE0); return; }
        case 0x005F6C10: { SB_MDTSEL = (val & 1); return; }
        case 0x005F6C14: { SB_MDEN = (val & 1); return; }
        case 0x005F6C88: // SB_MSHTCL
            if (val & 1) vblank_repeat_trigger = false;
            return;
        case 0x005F6C18: {
            SB_MDST = (val & 1);
            if (val & 1) {
                dma_init();
            };
            return;
        }
        case 0x005F6C80: { SB_MSYS.u = val & 0xFFFF130F; return; }
        case 0x005F6C8C: { if ((val >> 16) == 0x6155) { SB_MDAPRO.u = val & 0x00007F7F; } return; }
        case 0x005F6CE8: { SB_MMSEL = (val & 1); return; }
    }
    *success = false;
    printf("\nMAPLE write bad reg %08x(%d): %08llx", addr, sz, val);
}

}