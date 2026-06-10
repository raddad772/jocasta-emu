//
// Created by . on 4/2/26.
//
#include <cstring>
#include <cassert>
#include "helpers/cdrom_formats.h"
#include "dc_bus.h"
#include "dc_gdrom.h"
#include "dc_debugger.h"

namespace DREAMCAST::GDROM {

#define gd_printf(...)   (void)0

static constexpr u16 reply_11[] =
{
    0x0000,0x0000,0xb400,0x0019,0x0800,0x4553,0x2020,0x2020,
    0x2020,0x6552,0x2076,0x2e36,0x3334,0x3939,0x3430,0x3830
};

static constexpr u16 reply_71[] =
{
    0x0b96,0xf045,0xff7e,0x063d,0x7d4d,0xbf10,0x0007,0xcf73,0x009c,0x0cbc,0xaf1c,0x301c,0xa7e7,0xa803,0x0098,0x0fbd,0x5bbd,0x50aa,0x3923,
    0x1031,0x690e,0xe513,0xd200,0x660d,0xbf54,0xfd5f,0x7437,0x5bf4,0x0022,0x09c6,0xca0f,0xe893,0xaba4,0x6100,0x2e0e,0x4be1,0x8b76,0xa56a,
    0xe69c,0xc423,0x4b00,0x1b06,0x0191,0xe200,0xcf0d,0x38ca,0xb93a,0x91e7,0xefe5,0x004b,0x09d6,0x68d3,0xc43e,0x2daf,0x2a00,0xf90d,0x78fc,
    0xaeed,0xb399,0x5a32,0x00e7,0x0a4c,0x9722,0x825b,0x7a06,0x004c,0x0e42,0x7857,0xf546,0xfc20,0xcb6b,0x5b01,0x0086,0x0ee4,0x26b2,0x71cd,
    0xa5e3,0x0633,0x9a8e,0x0050,0x0707,0x34f5,0xe6ef,0x3200,0x130f,0x5941,0x0f56,0x3802,0x642a,0x072a,0x003e,0x1152,0x1d2a,0x765f,0xa066,
    0x2fb2,0xc797,0x6e5e,0xe252,0x5800,0xca09,0xa589,0x0adf,0x00de,0x0650,0xb849,0x00b4,0x0577,0xe824,0xbb00,0x910c,0xa289,0x628b,0x6ade,
    0x60c6,0xe700,0x0f0f,0x9611,0xd255,0xe6bf,0x0b48,0xab5c,0x00dc,0x0aba,0xd730,0x0e48,0x6378,0x000c,0x0dd2,0x8afb,0xfea3,0x3af8,0x88dd,
    0x4ba9,0xa200,0x750a,0x0d5d,0x2437,0x9dc5,0xf700,0x250b,0xdbef,0xe041,0x3e52,0x004e,0x03b7,0xe500,0xb911,0x5ade,0xcf57,0x1ab9,0x7ffc,
    0xee26,0xcd7b,0x002b,0x084b,0x09b8,0x6a70,0x009f,0x114b,0x158c,0xa387,0x4f05,0x8e37,0xde63,0x39ef,0x4bfc,0xab00,0x0b10,0xaa91,0xe10f,
    0xaee9,0x3a69,0x03f8,0xd269,0xe200,0xc107,0x3d5c,0x0082,0x08a9,0xc468,0x2ead,0x00d1,0x0ef7,0x47c6,0xcdc8,0x7c8e,0x5c00,0xb995,0x00f4,
    0x04e3,0x005b,0x0774,0xc765,0x8e84,0xc600,0x6107,0x4480,0x003f,0x0ec8,0x7872,0xd347,0x4dc2,0xc0af,0x1354,0x0031,0x0df7,0xd848,0x92e2,
    0x7f9f,0x442f,0x3368,0x0d00,0xab10,0xeafe,0x198e,0xf881,0x7c6f,0xe1de,0x06b3,0x4d00,0x6611,0x4cae,0xb7f9,0xee2f,0x8eb0,0xe17e,0x958d,
    0x006f,0x0df4,0x9d88,0xe3ca,0xb2c4,0xbb47,0x69a0,0xf300,0x480b,0x4117,0xa064,0x710e,0x0082,0x1e34,0x4d18,0x8085,0xa94c,0x660b,0x759b,
    0x6113,0x2770,0x7a81,0xcd02,0xab57,0x02df,0x5293,0xdf83,0xa848,0x9ea6,0x6f74,0x0389,0x2528,0x9652,0x67ff,0xd87a,0xb13c,0x462c,0xef84,
    0xc1e1,0xc9c6,0x96dc,0xa9aa,0x82c4,0x2758,0x7557,0x3467,0x3bfb,0xbf25,0x3bfb,0x13f6,0x96ec,0x16e5,0xfd26,0xdaa8,0xc61b,0x7f50,0xff47,
    0x5508,0xed08,0x9300,0xc49b,0x6771,0xa6ec,0x16cc,0x8720,0x0747,0x00a6,0x5d79,0xab4f,0x6fa1,0x6b7a,0xc427,0xa3da,0x94c3,0x7f4f,0xe5f3,
    0x6f1b,0xe5cc,0xe5f0,0xc99d,0xfdae,0xac39,0xe54c,0x8358,0x6525,0x7492,0x819e,0xb6a0,0x02a9,0x079b,0xe7b6,0x5779,0x4ad9,0xface,0x94b4,
    0xcc05,0x3c86,0x06dd,0xa6cd,0x2424,0xc1fa,0x48f9,0x0cc9,0xc46c,0x8296,0xf617,0x0931,0xe2c4,0xfd77,0x46cf,0xb218,0x015f,0xd16b,0x567b,
    0x94b8,0xe54a,0x196c,0xc0f0,0x70b6,0xf793,0xd1d3,0x6e2b,0x537c,0x856d,0x0cd1,0x778b,0x90ee,0x15da,0xe055,0x0958,0xfc56,0x9f31,0x46af,
    0xc3cb,0x718d,0xf275,0xc32c,0xa1bb,0xcfc4,0x5627,0x9b7c,0xaffe,0x4e3e,0xcdb4,0xaa6a,0xf3f5,0x22e3,0xe182,0x68a5,0xdbb3,0x9e8f,0x7b5e,
    0xf090,0x3f79,0x8c52,0x8861,0xae76,0x6314,0x0f19,0xce1d,0x63a1,0xb210,0xd7e2,0xb194,0xcb33,0x8528,0x9b7d,0xf4f5,0x5025,0xdb9b,0xa535,
    0x9cb0,0x9209,0x31e3,0xab40,0xf44d,0xe835,0x0ab3,0xc321,0x9c86,0x29cb,0x77a4,0xbc57,0xdad8,0x82a5,0xe880,0x72cf,0xad81,0x282e,0xd8ff,
    0xd1b6,0x972b,0xff00,0x06e1,0x3944,0x4b1c,0x19ab,0x4d5b,0x3ed6,0x5c1b,0xbb64,0x6832,0x7cf5,0x9ec9,0xb4e8,0x1b29,0x4d7f,0x8080,0x8b7e,
    0x0a1c,0x9ae6,0x49bf,0xc51e,0x67b6,0x057d,0x90e4,0x4b40,0x9baf,0xde52,0x8017,0x5681,0x3aea,0x8253,0x628c,0x96fb,0x6f97,0x16c1,0xd478,
    0xe77b,0x5ab9,0xeb2a,0x6887,0xd333,0x4531,0xfefa,0x1cf4,0x8690,0x7773,0xa9d9,0x4ad1,0xcf4a,0x23ae,0xf9db,0xd809,0xdc18,0x0d6a,0x19e4,
    0x658c,0x64c6,0xdcc7,0xe3a9,0xb191,0xc84c,0x9ec1,0x7f3b,0xa3cb,0xddcf,0x1df0,0x6e07,0xcedc,0xcd0d,0x1e7e,0x1155,0xdf8b,0xab3a,0x3bb6,
    0x526e,0xa77f,0xd100,0xbe33,0x9bf2,0x4afc,0x9dcf,0xc68f,0x7bc4,0xe7da,0x1c2a,0x6e26
};

void DRIVE::dma_start() {
    if (bus->g1.SB_GDST) printf("\nGDROM DMA REQUEST!");
}

void DRIVE::clear_interrupt()
{
    bus->holly.lower_interrupt(HOLLY::hirq_gdrom_cmd);
}

enum GDstatus {
    GD_BUSY = 0,
    GD_PAUSE = 1,
    GD_STANDBY = 2,
    GD_PLAY = 3,
    GD_SEEK = 4,
    GD_SCAN = 5,
    GD_OPEN = 6,
    GD_NODISC = 7,
    GD_RETRY = 8,
    GD_ERROR = 9
};

// next_state default gds_pio_end
void DRIVE::spi_pio_end(const u8* buffer, u32 len, gd_states next_state)
{
    assert(len < 0xFFFF);
    pio_buff.index = 0;
    pio_buff.size = len >> 1;
    pio_buff.next_state = next_state;

    if (buffer != nullptr){
        gd_printf("\nMEMCPY.... WORD1:%02x", *(u16 *)&buffer[0]);
        memcpy(pio_buff.data, buffer, len);
    }

    if (len == 0)
        setstate(next_state);
    else
        setstate(gds_pio_send_data);
}

DRIVE::DRIVE(core *parent) : bus(parent) {
    interrupt_reason.u = 0;
    byte_count.u = 0;
    sns_asc = sns_ascq = sns_key = 0;

    packet_cmd = (SPI_packet_cmd) {};
    pio_buff = (GDROM_PIOBUF) {.next_state=gds_waitcmd, .index=0, .size=0};
    memset(&pio_buff.data[0], 0, 65536);
    state = gds_waitcmd;
    error.u = 0;
    interrupt_reason.u = 0;
    features = 0;
    sector_number.u = 0;
    sector_count.u = 0;
    byte_count.u = 0;
    device_status.u = 0;
}

void DRIVE::setdisc() {
    cdda.playing = 0;
    sns_asc = 0x28;
    sns_ascq = 0;
    sns_key = 6;
    if (sector_number.status == GD_BUSY)
        sector_number.status = GD_PAUSE;
    else
        sector_number.status = GD_STANDBY;
    sector_number.disc_format = 8; // GDROM

}

void DRIVE::process_spi_cmd() {
    //gd_printf("\nPROCESS SPI PACKET! %llu ", sh4.clock.trace_cycles);
    //gd_printf("Sense: %02x %02x %02x \n", sns_asc, sns_ascq, sns_key);

    //gd_printf("SPI command %02x;", packet_cmd.data_8[0]);
    /*gd_printf("Params: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x \n",
           packet_cmd.data_8[0], packet_cmd.data_8[1], packet_cmd.data_8[2],
           packet_cmd.data_8[3], packet_cmd.data_8[4], packet_cmd.data_8[5],
           packet_cmd.data_8[6], packet_cmd.data_8[7], packet_cmd.data_8[8],
           packet_cmd.data_8[9], packet_cmd.data_8[10], packet_cmd.data_8[11]);*/
    dbgloglog_bus(DCD_GDROM_SPI_PACKET, DBGLS_TRACE, "GDROM: SPI cmd %02x (%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x)", packet_cmd.data_8[0], packet_cmd.data_8[1], packet_cmd.data_8[2], packet_cmd.data_8[3], packet_cmd.data_8[4], packet_cmd.data_8[5], packet_cmd.data_8[6], packet_cmd.data_8[7], packet_cmd.data_8[8], packet_cmd.data_8[9], packet_cmd.data_8[10], packet_cmd.data_8[11]);

    if (sns_key == 0x0 || sns_key == 0xB)
        device_status.CHECK = 0;
    else {
        //gd_printf("\nCHECK! 1...");
        device_status.CHECK = 1;
    }

    switch (packet_cmd.data_8[0]) {
        case 0: // SPI_TEST_UNIT
            gd_printf("\nSPI_TEST_UNIT");

            device_status.CHECK = sector_number.status == GD_BUSY; // Drive is ready ;)
            gd_printf("\nCHECK? %d", device_status.CHECK);

            dbgloglog_bus(DCD_GDROM_SPI_PACKET, DBGLS_TRACE, "GDROM: SPI_TEST_UNIT %02x", device_status.CHECK);
            setstate(gds_procpacketdone);
            break;
        case 0x11: // SPI_REQ_MODE
            gd_printf("SPI_REQ_MODE\n");
            dbgloglog_busn(DCD_GDROM_SPI_PACKET, DBGLS_TRACE, "GDROM: SPI_REQ_MODE");
            spi_pio_end(reinterpret_cast<const u8 *>(&reply_11[packet_cmd.data_8[2] >> 1]), packet_cmd.data_8[4], gds_pio_end);
            break;
        case 0x13: // SPI_REQ_ERR
            gd_printf("SPI_REQ_ERR cyc:%llu\n", sh4.clock.trace_cycles);
            u8 resp[10];
            resp[0] = 0xF0;
            resp[1] = 0;
            resp[2] = sns_key;//sense
            resp[3] = 0;
            resp[4] = resp[5] = resp[6] = resp[7] = 0; //Command Specific Information
            resp[8] = sns_asc;//Additional Sense Code
            resp[9] = sns_ascq;//Additional Sense Code Qualifier

            spi_pio_end(resp, packet_cmd.data_8[4], gds_pio_end);
            dbgloglog_busn(DCD_GDROM_SPI_PACKET, DBGLS_TRACE, "GDROM: SPI_REQ_ERR");

            sns_key = 0;
            sns_asc = 0;
            sns_ascq = 0;
            //GDStatus.CHECK=0;
            break;
        case 0x14: { // GET_TOC
            gd_printf("\nSPI GET TOC");
            dbgloglog_busn(DCD_GDROM_SPI_PACKET, DBGLS_TRACE, "GDROM: SPI GET TOC");

            spi_pio_end(static_cast<u8 *>(disc.gdrom_toc[packet_cmd.data_8[1] & 1].data), (packet_cmd.data_8[4]) | (packet_cmd.data_8[3] << 8), gds_pio_end);
            break;
        }
        case 0x70: // Reicast does this a bit weird so we do too!
            dbgloglog_busn(DCD_GDROM_SPI_PACKET, DBGLS_TRACE, "GDROM: SPI CMD 70");

            gd_printf("\nSPI CMD 70 %llu", sh4.clock.trace_cycles);
            setstate(gds_procpacketdone);
            break;
        case 0x71:
            dbgloglog_busn(DCD_GDROM_SPI_PACKET, DBGLS_TRACE, "GDROM: SPI CMD 71");
            gd_printf("\nSPI CMD 71");
            //gd_printf("SPI : unknown ? [0x71]\n");
            extern u32 reply_71_sz;

            spi_pio_end(reinterpret_cast<const u8 *>(&reply_71[0]), sizeof(reply_71), gds_pio_end);

            sector_number.status = GD_PAUSE;
            break;

        default:
            dbgloglog_busn(DCD_GDROM_SPI_PACKET, DBGLS_TRACE, "GDROM: SPI CMD UNKNOWN!");
            printf("\nUNKNOWN SPI COMMAND %02x", static_cast<u32>(packet_cmd.data_8[0]));
            dbg_break("UNKNOWN SPI COMMAND", bus->master_cycles);
            break;
    }
}

void DRIVE::setstate(gd_states instate)
{
    gd_states prev_state = state;
    state = instate;
    switch(state) {
        case gds_waitcmd:
            dbgloglog_busn(DCD_GDROM_STATE_CHANGE, DBGLS_TRACE, "GDROM: State: Wait for command");
            device_status.DRDY = 1;
            device_status.BSY = 0;
            break;
        case gds_procata:
            dbgloglog_busn(DCD_GDROM_STATE_CHANGE, DBGLS_TRACE, "GDROM: State: Process Data");
            device_status.DRDY = 0;   // Can't accept ATA command
            device_status.BSY = 1;    // Accessing command block to process command
            ATA_command();
            break;
        case gds_waitpacket:
            dbgloglog_busn(DCD_GDROM_STATE_CHANGE, DBGLS_TRACE, "GDROM: State: Wait for SPI Packet");
            //assert(prev_state == gds_procata); // Validate the previous command ;)

            // Prepare for packet command
            packet_cmd.index = 0;

            // Set CoD, clear BSY and IO
            interrupt_reason.CoD = 1;
            device_status.BSY = 0;
            interrupt_reason.IO = 0;

            // Make DRQ valid
            device_status.DRQ = 1;

            // ATA can optionally raise the interrupt ...
            // RaiseInterrupt(holly_GDROM_CMD);
            break;
        case gds_procpacket:
            dbgloglog_busn(DCD_GDROM_STATE_CHANGE, DBGLS_TRACE, "GDROM: State: Process SPI Packet");
            device_status.DRQ = 0;     // Can't accept ATA command
            device_status.BSY = 1;     // Accessing command block to process command
            process_spi_cmd();
            break;
        case gds_procpacketdone:
            dbgloglog_busn(DCD_GDROM_STATE_CHANGE, DBGLS_TRACE, "GDROM: State: Process Packet Done");

            device_status.DRDY = 1;
            interrupt_reason.CoD = 1;
            interrupt_reason.IO = 1;

            //Clear DRQ,BSY
            device_status.DRQ = 0;
            device_status.BSY = 0;

            //Make INTRQ valid
            bus->holly.raise_interrupt(HOLLY::hirq_gdrom_cmd, -1);

            //command finished !
            setstate(gds_waitcmd);
            break;
        case gds_pio_send_data:
        case gds_pio_get_data:
            dbgloglog_busn(DCD_GDROM_STATE_CHANGE, DBGLS_TRACE, "GDROM: State: PIO get/send");

            gd_printf("\nPIO_SEND_DATA");
            //  When preparations are complete, the following steps are carried out at the device.
            //(1)   Number of bytes to be read is set in "Byte Count" register.
            byte_count.u = static_cast<u16>(pio_buff.size << 1);
            //(2)   IO bit is set and CoD bit is cleared.
            interrupt_reason.IO = 1;
            interrupt_reason.CoD = 0;
            //(3)   DRQ bit is set, BSY bit is cleared.
            device_status.DRQ = 1;
            device_status.BSY = 0;
            //(4)   INTRQ is set, and a host interrupt is issued.
            bus->holly.raise_interrupt(HOLLY::hirq_gdrom_cmd, -1);
            break;
        case gds_pio_end:
            dbgloglog_busn(DCD_GDROM_STATE_CHANGE, DBGLS_TRACE, "GDROM: State: PIO end");

            device_status.DRQ = 0;//all data is sent !

            setstate(gds_procpacketdone);
            break;

        default:
            printf("\nGDROM UNIMPLEMENTED STATE %d", state);
            return;
    }
}

void DRIVE::reset() {
    setdisc();
    setstate(gds_waitcmd);

    device_status.NU = 0;
    device_status.BSY = 0;
    device_status.DRDY = 0; // "Response to ATA command not possible"
    device_status.DF = 0;
    device_status.DSC = 0;
    device_status.DRQ = 0;
    device_status.CORR = 0;
    device_status.CHECK = 0;
}

void DRIVE::ATA_command()
{
    error.ABORT = 0;

    if (sns_key == 0x0 || sns_key == 0xB)
        device_status.CHECK = 0;
    else {
        gd_printf("\nCHECK 1 %02x", sns_key);
        device_status.CHECK = 1;
    }

    switch(ata_cmd) {
        /*case 0x00:
            gd_printf("\nGDROM NOP!");
            device_status.BSY = 0;
            // set ABORT and ERROR in status register
            GDROM_set_interrupt();
            return;*/
        case 0x08:
            gd_printf("\nGDROM SOFT RESET!");
            reset();
            return;
        case 0xA0: // Wait for SPI packet!!!
            gd_printf("\nGDROM WAIT FOR SPI PACKET!");
            setstate(gds_waitpacket);
            return;
        case 0xEF: // Feature set
            gd_printf("\nGDROM SET FEATURES! %llu", sh4.clock.trace_cycles);

            device_status.DSC = 0;
            device_status.CHECK = 0;
            device_status.DF = 0;

            error.ABORT = 0;
            bus->holly.raise_interrupt(HOLLY::hirq_gdrom_cmd, -1);
            setstate(gds_waitcmd);
            return;
    }
    printf("\nUNKNOWN GDROM COMMAND %02x", ata_cmd);
}

static constexpr u32 mszmask[9] = { 0, 0x1FFF'FFFF, 0x1FFF'FFFE, 0, 0x1FFF'FFFC, 0, 0, 0, 0x1FFF'FFF8 };
    
u64 DRIVE::read_io(u32 addr, u8 sz, bool *success) {
    addr &= mszmask[sz];
    u64 v = read_io8(addr, success);
    if (sz >= 2) v |= read_io8(addr+1, success) << 8;
    if (sz >= 4) {
        v |= read_io8(addr+2, success) << 16;
        v |= read_io8(addr+3, success) << 24;
    }
    if (sz == 8) {
        v |= read_io8(addr+4, success) << 32;
        v |= read_io8(addr+5, success) << 40;
        v |= read_io8(addr+6, success) << 48;
        v |= read_io8(addr+7, success) << 56;
    }
    if (!(*success)) {
        printf("\nGDROM missed reg read %08x(%d)", addr, sz);
    }
    return v;

}

void DRIVE::write_io(u32 addr, u8 sz, u64 val, bool* success) {
    addr &= mszmask[sz];
    write_io8(addr, val & 0xFF, success);
    if (sz >= 2) write_io8(addr+1, (val >> 8) & 0xFF, success);
    if (sz >= 4) {
        write_io8(addr+2, (val >> 16) & 0xFF, success);
        write_io8(addr+3, (val >> 24) & 0xFF, success);
    }
    if (sz == 8) {
        write_io8(addr+4, (val >> 32) & 0xFF, success);
        write_io8(addr+5, (val >> 40) & 0xFF, success);
        write_io8(addr+6, (val >> 48) & 0xFF, success);
        write_io8(addr+7, (val >> 56) & 0xFF, success);
    }
    if (!(*success)) {
        printf("\nGDROM missed reg write to %08x(%d): %08llx", addr, sz, val);
    }

}

void DRIVE::write_io8(u32 addr, u64 val, bool* success)
{
    switch(addr) {
        // read / write
        case 0x005F7018: // Device control
            device_control.u = val & 2;
            gd_printf("\nGDROM SET INTERRUPT %llu", val & 2);
            return;
        case 0x005F7080: // Data
            if (state == gds_waitpacket)
            {
                gd_printf("\nGD CMD WRITE AT %llu: %04llx %d", sh4.clock.trace_cycles, val, pio_buff.index);
                packet_cmd.data_16[packet_cmd.index] = static_cast<u16>(val);
                packet_cmd.index += 1;
                if (packet_cmd.index == 6)
                    setstate(gds_procpacket);
            }
            else if (state == gds_pio_get_data){
                gd_printf("\nGD PIO GETDATA AT %llu: %04llx %d", sh4.clock.trace_cycles, val, pio_buff.index);
                pio_buff.data[pio_buff.index] = static_cast<u16>(val);
                pio_buff.index += 1;
                if (pio_buff.size == pio_buff.index)
                {
                    assert(pio_buff.next_state != gds_pio_get_data);
                    setstate(pio_buff.next_state);
                }
            }
            else
            {
                gd_printf("GDROM: Illegal Write to DATA\n");
            }
            return;
        case 0x005F7084: // Features
            gd_printf("\nGDROM SET FEATURES! %llu", sh4.clock.trace_cycles);
            features = val;
            gd_printf("\nGDROM SET FEATURES REG %02llx", val);
            return;
        case 0x005F7090: // Byte count lo
            byte_count.lo = static_cast<u8>(val);
            return;
        case 0x005F7094: // Byte count lo
            byte_count.hi = static_cast<u8>(val);
            return;
        case 0x005F7088: // Sector Count
            sector_count.u = val;
            gd_printf("\nGDROM SET SECTOR COUNT transfer_mode %d   mode_value %d", sector_count.transfer_mode, sector_count.mode_value);
            return;
        case 0x005F709C: // ATA Command
        // OOPS
            ata_cmd = val;
            setstate(gds_procata);
            return;
    }
    if ((addr & 3) > 0) return;
    *success = false;
}

u64 DRIVE::read_io8(u32 addr, bool* success)
{
    switch(addr) {
        case 0x005F7018: // AltStatus
            gd_printf("\n%02x AltSTATUS", device_status.u);
            return device_status.u | (1 << 4);
        case 0x005F709C: // Status
            gd_printf("\n%02x STATUS", device_status.u);
            clear_interrupt();
            return device_status.u | (1 << 4);
        case 0x005F7080: // DATA
            if (pio_buff.index == pio_buff.size)
            {
                gd_printf("\n-------------------------nGDROM: Illegal Read From DATA (underflow)\n");
            }
            else
            {
                u32 rv = pio_buff.data[pio_buff.index];
                pio_buff.index += 1;
                byte_count.u -= 2;
                if (pio_buff.index == pio_buff.size)
                {
                    assert(pio_buff.next_state != gds_pio_send_data);
                    //end of pio transfer !
                    setstate(pio_buff.next_state);
                }
                return rv;
            }
            return 0;
        case 0x005F708C: // Sector number
            return sector_number.u;
        case 0x005F7090: // Byte count lo
            return byte_count.lo;
        case 0x005F7094: // Byte count hi
            return byte_count.hi;
    }
    if ((addr & 3) > 0) return 0;
    printf("\nGOT HERE");
    *success = false;
    return 0;
}

void DRIVE::insert_disc(multi_file_set &mfs) {
        disc.parse_gdi(mfs);
        disc_inserted = true;
}


}
