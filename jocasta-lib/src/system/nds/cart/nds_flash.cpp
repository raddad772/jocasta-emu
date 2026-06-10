//
// Created by . on 4/5/25.
//

#include "nds_flash.h"
#include "../nds_bus.h"
#include "helpers/multisize_memaccess.cpp"

//#define flprintf(...) printf(__VA_ARGS__)
#define flprintf(...) (void)0
namespace NDS::CART {

static inline void flash_push_addr(ridge *cart, u32 val)
{
    cart->backup.cmd_addr = ((cart->backup.cmd_addr << 8) | (val & 0xFF)) & 0x00FFFFFF;
    cart->backup.data_in_pos++;
}

static inline void flash_write_byte(ridge *cart, u32 val, bool program_only)
{
    if (cart->backup.status.write_enable) {
        u32 old = cR8(cart->backup.store->data, cart->backup.cmd_addr);
        u32 out = program_only ? (old & (val & 0xFF)) : (val & 0xFF);
        cW8(cart->backup.store->data, cart->backup.cmd_addr, out);
        cart->backup.store->dirty = true;
        cart->backup.wrote_since_select = true;
    }
}

static inline void flash_erase(ridge *cart, u32 len)
{
    if (!cart->backup.status.write_enable) return;

    u32 start = cart->backup.cmd_addr & ~(len - 1);
    for (u32 i = 0; i < len; i++)
        cW8(cart->backup.store->data, (start + i) & cart->backup.detect.sz_mask, 0xFF);

    cart->backup.store->dirty = true;
    cart->backup.wrote_since_select = true;
}

void ridge::flash_setup()
{

}

void ridge::flash_handle_spi_cmd(u32 val)
{
    switch(backup.cmd) {
        case 0:
            printf("\nBAD FTRANSFER? %02x", val);
            break;
        case 3: // RD
            if (backup.data_in_pos < 3) {
                flprintf("\nRD/ADDR %02x", val);
                flash_push_addr(this, val);

                if (backup.data_in_pos == 3) {
                    backup.cmd_addr &= backup.detect.sz_mask;
                    flprintf("\nRD ADDR IS:%06x", backup.cmd_addr);
                }
            }
            else {
                backup.data_out.b8[0] = backup.data_out.b8[1] =
                        cR8(backup.store->data, backup.cmd_addr);
                flprintf("\nRD/DATA %06x:%02x", backup.cmd_addr, backup.data_out.b8[0]);
                backup.cmd_addr = (backup.cmd_addr + 1) & backup.detect.sz_mask;
                backup.data_in_pos++;
            }
            return;
        case 0x0B: // fast read
            if (backup.data_in_pos < 3) {
                flprintf("\nFAST RD/ADDR %02x", val);
                flash_push_addr(this, val);
                if (backup.data_in_pos == 3)
                    backup.cmd_addr &= backup.detect.sz_mask;
            }
            else if (backup.data_in_pos == 3) {
                backup.data_in_pos++;
            }
            else {
                backup.data_out.b8[0] = backup.data_out.b8[1] =
                        cR8(backup.store->data, backup.cmd_addr);
                backup.cmd_addr = (backup.cmd_addr + 1) & backup.detect.sz_mask;
                backup.data_in_pos++;
            }
            return;
        case 0x02: // page program
        case 0x0A: // page write
            if (backup.data_in_pos < 3) {
                flprintf("\nWR/ADDR %02x", val);
                flash_push_addr(this, val);

                if (backup.data_in_pos == 3) {
                    backup.cmd_addr &= backup.detect.sz_mask;
                    flprintf("\nWR ADDR IS:%06x", backup.cmd_addr);
                }
            }
            else {
                flash_write_byte(this, val, backup.cmd == 0x02);
                flprintf("\nRD/DATA %06x:%02x", backup.cmd_addr, val & 0xFF);
                inc_addr();
                backup.data_in_pos++;
            }
            return;

        case 0xD8: // sector erase
        case 0xDB: // page erase
            if (backup.data_in_pos < 3) {
                flprintf("\nERASE/ADDR %02x", val);
                flash_push_addr(this, val);
                if (backup.data_in_pos == 3) {
                    backup.cmd_addr &= backup.detect.sz_mask;
                    flash_erase(this, backup.cmd == 0xD8 ? 0x10000 : 0x100);
                }
            }
            return;
        case 0x9F: // JEDEC ID
            backup.data_out.b8[0] = 0xFF;
            backup.data_out.b8[1] = 0xFF;
            backup.data_out.b8[2] = 0xFF;
            backup.data_out.b8[3] = 0xFF;
            return;
        case 6:
            backup.status.write_enable = 1;
            break;
        case 4:
            backup.status.write_enable = 0;
            break;
        case 5:
            //printf("\nRD STATUS REG!");
            backup.data_out.b8[0] = backup.status.u;
            backup.data_out.b8[1] = backup.status.u;
            backup.data_out.b8[2] = backup.status.u;
            backup.data_out.b8[3] = backup.status.u;
            break;
        default: {
            static int a = 1;
            if (a) {
                printf("\nUnhandled flash SPI cmd(s) %02x", backup.cmd);
                a = 0;
            }
            break; }
    }
}


void ridge::flash_spi_transaction(u32 val)
{
    if (!backup.chipsel) {
        flprintf("\nCMD: %02x", val);
        backup.cmd = val;
        backup.data_in_pos = 0;
        backup.cmd_addr = 0;
        backup.wrote_since_select = false;
        switch(backup.cmd) {
            case 0x04:
                backup.status.write_enable = 0;
                break;
            case 0x06:
                backup.status.write_enable = 1;
                break;
        }
    }
    else {
        flash_handle_spi_cmd(val);
    }

    if (backup.chipsel && !io.spi.next_chipsel && backup.wrote_since_select)
        backup.status.write_enable = 0;
    backup.chipsel = io.spi.next_chipsel;
}

}
