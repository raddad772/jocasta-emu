//
// Created by . on 4/8/26.
//

#include "dc_bus.h"
#include "dc_debugger.h"

namespace DREAMCAST {


G2::G2(core *parent) : bus(parent) {
    for (u32 i = 0; i < 4; i++) {
        auto &c = channel[i];
        c.num = i;
        c.subsystem = static_cast<G2_DMA_channel_name>(i);
        c.bus = bus;
    }
}

bool G2_DMA_channel::ST_allowed() const {
    u32 signal = num > 0 ? 0 : (io.LEN.u >> 28) & 8;
    signal |= io.TSEL.u & 7;
    switch (signal) {
        case 0b0000:
        case 0b1000:
        case 0b1001:
        case 0b1010:
        case 0b0100:
        case 0b0101:
        case 0b1100:
        case 0b1101:
            return true;
        default:
            return false;
    }
}

bool G2_DMA_channel::DREQ_allowed() const {
    u32 signal = num > 0 ? 0 : (io.LEN.u >> 28) & 8;
    signal |= io.TSEL.u & 7;
    switch (signal) {
        case 0b1001:
        case 0b0101:
        case 0b0111:
        case 0b1101:
        case 0b1111:
            return true;
        default:
            return false;
    }
}

bool G2_DMA_channel::IRQ_allowed() const {
    u32 signal = num > 0 ? 0 : (io.LEN.u >> 28) & 8;
    signal |= io.TSEL.u & 7;
    switch (signal) {
        case 0b0010:
        case 0b1010:
        case 0b0110:
        case 0b0111:
        case 0b1110:
        case 0b1111:
            return true;
        default:
            return false;
    }
}

void G2_DMA_channel::irq_trigger() {
    if (!io.EN || running()) return;
    if (IRQ_allowed()) run();
}

bool G2_DMA_channel::running() const {
    return transfer_32.suspended || transfer_32.still_sch;
}

void G2_DMA_channel::try_run() {
    // If EN & DREQ & DREQ allowed,
    // OR if EN & ST & ST allowed
    if (!io.EN || running()) return;
    if ((ST_allowed() && io.ST) /*|| (DREQ_allowed() && DREQ)*/) run(); // DREQ not used
    /*if (IRQ_allowed()) {
        // Check IRQ signal
        switch (subsystem) {
            case G2D_AICA:

        }
    }*/
}

void G2_DMA_channel::do_transfer_32() {
    u32 tx_len = io.LEND < 32 ? io.LEND : 32;
    u32 sys_addr = io.STARD;
    u32 g2_addr = io.STAGD;

    for (u32 i = 0; i < tx_len; i += 4) {
        if (io.DIR) {
            u32 data = bus->mainbus_read<4, false>(g2_addr + i);
            bus->mainbus_write<4, false>(sys_addr + i, data);
        }
        else {
            u32 data = bus->mainbus_read<4, false>(sys_addr + i);
            bus->mainbus_write<4, false>(g2_addr + i, data);
        }
    }

    io.STAGD += tx_len;
    io.STARD += tx_len;
    io.LEND -= tx_len;
    if (io.LEND == 0) {
        dbgloglog_bus(DCD_G2_DMA_END, DBGLS_TRACE, "G2: DMA%d transfer end", num);
        if (io.LEN.disable_on_end) io.EN = 0;
        bus->aica.unpause_cpu();
        bus->holly.raise_interrupt(static_cast<HOLLY::interruptmasks>(HOLLY::hirq_SPU_DMA+num), -1);
        io.LEND = io.LEN.u & 0x7FFFFFFF;
    }
    else {
        schedule_transfer_32();
        bus->pause_cpu_for(8); // 4 bytes per cycle, pause for 8 cycles while 32 bytes are read
    }
}

static void sch_transfer_32(void *ptr, u64 key, u64 clock, u32 jitter) {
    auto *th = static_cast<G2_DMA_channel *>(ptr);
    th->do_transfer_32();
}

void G2_DMA_channel::schedule_transfer_32() {
    transfer_32.next_cycle += bus->aica.timing.transfer_32_bytes;
    transfer_32.sch_id = bus->scheduler.only_add_abs(transfer_32.next_cycle, 0, this, &sch_transfer_32, &transfer_32.still_sch);
}

void G2_DMA_channel::run() {
    printf("\nG2 DMA TRANSFER!");
    dbgloglog_bus(DCD_G2_DMA_START, DBGLS_TRACE, "G2: DMA%d transfer start dir:%d sys_addr:%08x g2_addr:%08x len:%d disable_on_end:%d", num, io.DIR, io.STAR, io.STAG, io.LEN.u & 0x7FFFFFFF, io.LEN.disable_on_end);
    io.ST = 0;
    io.STAGD = io.STAG;
    io.STARD = io.STAR;
    io.LEND = io.LEN.u & 0x7FFFFFFF;
    transfer_32.next_cycle = bus->master_cycles;
    bus->aica.pause_cpu();
    bus->pause_cpu_for(8);
    schedule_transfer_32();


}

void G2_DMA_channel::write_reg(u32 addr, u8 sz, u64 val, bool *success) {
    switch (addr) {
        case 0x00: io.STAG = val & 0x1FFFFFE0; return;
        case 0x04: io.STAR = val & 0x1FFFFFE0; return;
        case 0x08: io.LEN.u = val & 0x800FFFE0; return;
        case 0x0C: io.DIR = (val & 1); return;
        case 0x10: io.TSEL.u = val & 7; try_run(); return;
        case 0x14:
            if (val & 1) printf("\nDMA%d ST %08x", num, addr);
            io.EN = val & 1;
            try_run(); return;
        case 0x18: io.ST = val & 1; try_run(); return;
        case 0x1C: write_suspend(val); return;
    }
    *success = false;
}

void G2_DMA_channel::write_suspend(u32 val) {
    // bit 0 is write-only
    u32 old = io.SUSP;
    io.SUSP = val & 1;
    if (subsystem != G2D_AICA) return;
    if (old != io.SUSP) {
        if (!old) { // Initiate suspend
            if (!transfer_32.still_sch) {
                //printf("\nSUSP when not running or already suspended!");
                if (!transfer_32.suspended) io.SUSP = 0;
                return;
            }
            if (!io.TSEL.enable_suspend) {
                io.SUSP = 0;
                return;
            }

            bus->scheduler.delete_if_exist(transfer_32.sch_id);
            transfer_32.suspended = true;
            bus->aica.unpause_cpu();
            transfer_32.cycle_surplus = transfer_32.next_cycle - bus->master_cycles;
            if (transfer_32.cycle_surplus < 0) transfer_32.cycle_surplus = 0;
            dbgloglog_bus(DCD_G2_DMA_SUSPEND, DBGLS_TRACE, "G2: DMA%d suspend %d surplus cycles", num, static_cast<int>(transfer_32.cycle_surplus));
        }
        else { // End suspend
            if (!transfer_32.suspended) {
                return;
            }
            transfer_32.suspended = false;
            bus->aica.pause_cpu();
            transfer_32.next_cycle = bus->master_cycles - transfer_32.cycle_surplus;
            schedule_transfer_32();
        }
    }
}

void G2::dma_irq_trigger() {
    for (auto & c : channel)
        c.irq_trigger();
}

u32 G2_DMA_channel::read_suspend() {

    // bit5 should always be 0?
    // bit4 should be 0 if transfer in progress or SB_ADTSEL is 0, or 1 if DMA is ended or suspended
    // bit0 should be 0

    u32 v = transfer_32.still_sch << 4;
    if (subsystem == G2D_AICA) {
        v |= io.TSEL.enable_suspend << 4;
    }
    return v;
}

u64 G2_DMA_channel::read_reg(u32 addr, u8 sz, bool *success)
{
    switch (addr) {
        case 0x00: return io.STAG;
        case 0x04: return io.STAR;
        case 0x08: return io.LEN.u;
        case 0x0C: return io.DIR;
        case 0x10: return io.TSEL.u;
        case 0x14: return io.EN;
        case 0x18: return transfer_32.still_sch; // Enable. Use sch_still to determine if DMA is scheduled
        case 0x1C: return read_suspend();
    }
    *success = false;
    return 0;
}

u64 G2_DMA_channel::read_counter(u32 addr, u8 sz, bool *success) {
    switch (addr) {
        case 0x00: return io.STAGD & 0x1FFFFFE0;
        case 0x04: return io.STARD & 0x1FFFFFE0;
        case 0x08: return io.LEND & 0x01FFFFE0;
    }
    *success = false;
    printf("\nREAD INVALID DMA DEBUG COUNTER! %08x(%d)", addr, sz);
    return 0;
}

void G2::write_io(u32 addr, u8 sz, u64 val, bool *success) {
    if (addr >= 0x005F7800 && addr <= 0x005F7880) {
        u32 cnum = ((addr >> 5) & 3);
        channel[cnum].write_reg(addr & 0x1F, sz, val, success);
        return;
    }
    switch (addr) {
        case 0x005F7890: { SB_G2DSTO = (val & 0x00000FFF); return; }
        case 0x005F7894: { SB_G2TRTO = (val & 0x00000FFF); return; }
        case 0x005F7898: { SB_G2MDMTO = (val & 0x000000FF); return; }
        case 0x005F789C: { SB_G2MDMW = (val & 0x000000FF); return; }
        case 0x005F78A0: { UKN005F78A0 = val; return; }
        case 0x005F78A4: { UKN005F78A4 = val; return; }
        case 0x005F78A8: { UKN005F78A8 = val; return; }
        case 0x005F78AC: { UKN005F78AC = val; return; }
        case 0x005F78B0: { UKN005F78B0 = val; return; }
        case 0x005F78B4: { UKN005F78B4 = val; return; }
        case 0x005F78B8: { UKN005F78B8 = val; return; }
        case 0x005F78BC: { if ((val >> 16) == 0x4659) { SB_G2APRO.u = val & 0x00007F7F; } return; }
        default: break;
    }
    *success = false;
    printf("\nG2 write bad reg %08x(%d): %08llx", addr, sz, val);

}

u64 G2::read_io(u32 addr, u8 sz, bool *success) {
    if (addr >= 0x005F7800 && addr <= 0x005F7880) {
        u32 cnum = ((addr >> 5) & 3);
        return channel[cnum].read_reg(addr & 0x1F, sz, success);
    }
    if ((addr >= 0x005F78C0) && (addr < 0x005F7900)) {
        u32 cnum = (addr >> 7) & 3;
        return channel[cnum].read_counter(addr & 0x0F, sz, success);
    }
    switch (addr) {
        case 0x005F7890:  { return SB_G2DSTO; }
        case 0x005F7894:  { return SB_G2TRTO; }
        case 0x005F7898:  { return SB_G2MDMTO; }
        case 0x005F789C:  { return SB_G2MDMW; }
        case 0x005F78A0:  { return UKN005F78A0; }
        case 0x005F78A4:  { return UKN005F78A4; }
        case 0x005F78A8:  { return UKN005F78A8; }
        case 0x005F78AC:  { return UKN005F78AC; }
        case 0x005F78B0:  { return UKN005F78B0; }
        case 0x005F78B4:  { return UKN005F78B4; }
        case 0x005F78B8:  { return UKN005F78B8; }
        case 0x005F78BC:  { return SB_G2APRO.u; }
        default: break;
    }
    *success = false;
    printf("\nG2 read bad reg %08x(%d)", addr, sz);
    return 0;
}

void G2::reset() {

}
    

}
