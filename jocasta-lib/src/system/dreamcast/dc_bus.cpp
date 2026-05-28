//
// Created by . on 4/2/26.
//

#include "helpers/multisize_memaccess.cpp"
#include "dc_bus.h"
#include "dc_debugger.h"


namespace DREAMCAST {

template<u8 sz, bool do_debug>
static u64 cpu_read(void *ptr, u32 addr) {
    auto *th = static_cast<core *>(ptr);
    return th->mainbus_read<sz, do_debug>(addr);
}

template<u8 sz, bool do_debug>
static void cpu_write(void *ptr, u32 addr, u64 val) {
    auto *th = static_cast<core *>(ptr);
    th->mainbus_write<sz, do_debug>(addr, val);
}

template<bool do_debug>
static u32 cpu_fetch_ins(void *ptr, u32 addr) {
    return cpu_read<2, do_debug>(ptr, addr);
}

void core::pause_cpu_for(u32 num) {
    cpu_pause_cycles += num;
}

static void sch_run_block(void *bound_ptr, u64 num, u64 current_clock, u32 jitter)
{
    auto *th = static_cast<core *>(bound_ptr);
    th->run_block(num);
}

void core::run_block(u64 num) {
    i32 n = static_cast<i32>(num);
    n -= cpu_pause_cycles;
    cpu_pause_cycles = 0;
    cpu.elfs = &elf_symbols;
    cpu.run_cycles<true>(n);

}

static u32 read_trace_cpu(void *ptr, u32 addr, u8 sz)
{
    auto *th = static_cast<core *>(ptr);
    switch (sz) {
        case 1: return th->mainbus_read<1, false>(addr);
        case 2: return th->mainbus_read<2, false>(addr);
        case 4: return th->mainbus_read<4, false>(addr);
        case 8: return th->mainbus_read<8, false>(addr);
    }
    NOGOHERE;
}

u64 core::read_flash(u32 addr, u8 sz) {
    //*success = true;
    u32 full_addr = addr;
    addr &= 0x1FFFF;
    if (sz == 1) {
        switch (addr) {
            case 0x1A002:
            case 0x1A0A2:
                if (settings.region <= 2)
                    return '0' + settings.region;
                break;
            case 0x1A003:
            case 0x1A0A3:
                if (settings.language <= 5)
                    return '0' + settings.language;
                break;
            case 0x1A004:
            case 0x1A0A4: {
                if (settings.broadcast <= 3)
                    return '0' + settings.broadcast;
                break;
            }
        }
    }
    return cR[sz](flash.buf.ptr, addr);
}

u64 core::read_io(u32 addr, u8 sz, bool *success) {
    switch (addr) {
        case 0x005F6800:  { return io.SB_C2DSTAT; }
        case 0x005F6804:  { return io.SB_C2DLEN; }
        case 0x005F6808:  { return io.SB_C2DST; }
        case 0x005F6810:  { return io.SB_SDSTAW; }
        case 0x005F6814:  { return io.SB_SDBAAW; }
        case 0x005F6818:  { return io.SB_SDWLT; }
        case 0x005F681C:  { return io.SB_SDLAS; }
        case 0x005F6820:  { return io.SB_SDST; }
        case 0x005F6840:  { return io.SB_DBREQM; }
        case 0x005F6844:  { return io.SB_BAVLWC; }
        case 0x005F6848:  { return io.SB_C2DPRYC; }
        case 0x005F684C:  { return io.SB_C2DMAXL; }
        case 0x005F6884:  { return io.SB_LMMODE0; }
        case 0x005F6888:  { return io.SB_LMMODE1; }
        case 0x005F688C: // SB_FFST
            // REICAST here
            io.SB_FFST_rc++;
            if (io.SB_FFST_rc & 0x8)
            {
                io.SB_FFST ^= 31;
            }
            return io.SB_FFST; // does the fifo status has really to be faked ?
        case 0x005F689C: // SB_REVISION
            return 0x0B;
        case 0x005F68A0:  { return io.SB_RBSPLT; }
        case 0x005F68A4:  { return io.SB_UKN5F68A4; }
        case 0x005F68AC:  { return io.SB_UKN5F68AC; }
        case 0x005F6900: { return (io.SB_ISTNRM.u) | ((io.SB_ISTEXT.u > 0) << 30) | ((io.SB_ISTERR.u > 0) << 31); }
        case 0x005F6904:  { return io.SB_ISTEXT.u; }
        case 0x005F6908:  { return io.SB_ISTERR.u; }
        case 0x005F6910:  { return io.SB_IML2NRM; }
        case 0x005F6914:  { return io.SB_IML2EXT.u; }
        case 0x005F6918:  { return io.SB_IML2ERR.u; }
        case 0x005F6920:  { return io.SB_IML4NRM; }
        case 0x005F6924:  { return io.SB_IML4EXT.u; }
        case 0x005F6928:  { return io.SB_IML4ERR.u; }
        case 0x005F6930:  { return io.SB_IML6NRM; }
        case 0x005F6934:  { return io.SB_IML6EXT.u; }
        case 0x005F6938:  { return io.SB_IML6ERR.u; }
        case 0x005F6940:  { return io.SB_PDTNRM; }
        case 0x005F6944:  { return io.SB_PDTEXT; }
        case 0x005F6950:  { return io.SB_G2DTNRM; }
        case 0x005F6954:  { return io.SB_G2DTEXT; }
    }
    *success = false;
    printf("\nBUS read bad reg %08x(%d)", addr, sz);
    return 0;
}

void core::write_C2DST(u32 val)
{
    if (io.SB_C2DST) {
        printf("\nCH2 DMA START!");
        u32 dst = io.SB_C2DSTAT & 0x03FFFFE0;
        u32 addr = 0x10000000 | dst;
        u32 len = io.SB_C2DLEN;

        if ((dst & 0x01000000) == 0) {
            if ((dst & 0x00800000) == 0) {
                holly.TA_FIFO_DMA(cpu.dmac.channels[2].SAR, len, holly.RAM, (8 * 1024 * 1024));
            }
            else {
                printf(DBGC_RED "\nUNSUPPORTED CH2 DMA TO YUV FIFO %08x" DBGC_RST, addr);
            }
        }
        else {
            u32 src = cpu.dmac.channels[2].SAR & 0x1FFFFFE0;
            for (u32 i = 0; i < len; i += 4) {
                mainbus_write<4, false>(addr + i, mainbus_read<4, false>(src + i));
            }
            cpu.dmac.channels[2].SAR = src + len;
            io.SB_C2DSTAT = (dst + len) & 0x03FFFFE0;
        }
        io.SB_C2DLEN = 0;
        io.SB_C2DST = 0;

        cpu.dmac.channels[2].end_transfer();
        holly.raise_interrupt(HOLLY::hirq_ch2_dma, 200);
    }

}

void core::write_SDST(u32 val)
{
    if (val & 1)
        printf(DBGC_RED "\nSORT DMA START REQUEST" DBGC_RST);
}


void core::write_io(u32 addr, u8 sz, u64 val, bool *success) {
    switch (addr) {
        case 0x005F6800: { io.SB_C2DSTAT = (val & 0x03FFFFE0); /*printf("\nSB-C2DSTAT WRITE %08llx cyc:%llu", val, sh4.trace_cycles)*/(void)0; return; }
        case 0x005F6804: { io.SB_C2DLEN = (val & 0x00FFFFE0); return; }
        case 0x005F6808: { io.SB_C2DST = (val & 1);  write_C2DST(val); return; }
        case 0x005F6810: { io.SB_SDSTAW = (val & 0x07FFFFE0); return; }
        case 0x005F6814: { io.SB_SDBAAW = (val & 0x07FFFFE0); return; }
        case 0x005F6818: { io.SB_SDWLT = (val & 1); return; }
        case 0x005F681C: { io.SB_SDLAS = (val & 1); return; }
        case 0x005F6820: { io.SB_SDST = (val & 1); write_SDST(val); return; }
        case 0x005F6840: { io.SB_DBREQM = (val & 1); return; }
        case 0x005F6844: { io.SB_BAVLWC = val; return; }
        case 0x005F6848: { io.SB_C2DPRYC = val; return; }
        case 0x005F684C: { io.SB_C2DMAXL = val; return; }
        case 0x005F6884: { io.SB_LMMODE0 = (val & 1); return; }
        case 0x005F6888: { io.SB_LMMODE1 = (val & 1); return; }
        case 0x005F68A0: { io.SB_RBSPLT = val; return; }
        case 0x005F68A4: { io.SB_UKN5F68A4 = val; return; }
        case 0x005F68AC: { io.SB_UKN5F68AC = val; return; }
        case 0x005F6900: { io.SB_ISTNRM.u &= ~val; holly.recalc_interrupts()/*; printf("\nSB_ISTNRM wrote: %08llx cyc:%llu", val, sh4.trace_cycles);*/; return; }
        case 0x005F6904: { warn_printf("\nWarning, writes to SB_ISTEXT at 0x005F6904 are not allowed!"); return; }
        case 0x005F6908: { io.SB_ISTERR.u &= ~val; holly.recalc_interrupts()/*; printf("\nSB_ISTERR write: %08llx cyc:%llu", val, sh4.trace_cycles)*/; return; }
        case 0x005F6910: { io.SB_IML2NRM = val; holly.recalc_interrupts(); /*printf("\nSB_IML2NRM wrote: %08llu cyc: %llx", val, sh4.trace_cycles)*/; return; }
        case 0x005F6914: { io.SB_IML2EXT.u = val & 0x0000000F; holly.recalc_interrupts()/*; printf("\nSB_IML2EXT write: %08llx cyc:%llu", val, sh4.trace_cycles)*/; return; }
        case 0x005F6918: { io.SB_IML2ERR.u = val & 0x9FFFFFFF; holly.recalc_interrupts()/*; printf("\nSB_IML2ERR write: %08llx cyc:%llu", val, sh4.trace_cycles)*/; return; }
        case 0x005F6920: { io.SB_IML4NRM = val; holly.recalc_interrupts(); /*printf("\nSB_IML4NRM wrote: %08llx cyc: %llu", val, sh4.trace_cycles)*/; return; }
        case 0x005F6924: { io.SB_IML4EXT.u = val & 0x0000000F; holly.recalc_interrupts()/*; printf("\nSB_IML4EXT write: %08llx cyc:%llu", val, sh4.trace_cycles)*/; return; }
        case 0x005F6928: { io.SB_IML4ERR.u = val & 0x9FFFFFFF; holly.recalc_interrupts()/*; printf("\nSB_IML4ERR write: %08llx cyc:%llu", val, sh4.trace_cycles)*/; return; }
        case 0x005F6930: { io.SB_IML6NRM = val; holly.recalc_interrupts(); /*printf("\nSB_IML6NRM wrote: %08llx cyc: %llu", val, sh4.trace_cycles)*/; return; }
        case 0x005F6934: { io.SB_IML6EXT.u = val & 0x0000000F; holly.recalc_interrupts()/*; printf("\nSB_IML6EXT write: %08llx cyc:%llu", val, sh4.trace_cycles)*/; return; }
        case 0x005F6938: { io.SB_IML6ERR.u = val & 0x9FFFFFFF; holly.recalc_interrupts()/*; printf("\nSB_IML6ERR write: %08llx cyc:%llu", val, sh4.trace_cycles)*/; return; }
        case 0x005F6940: { io.SB_PDTNRM = val & 0b111111'11111111'11111111; return; }
        case 0x005F6944: { io.SB_PDTEXT = val & 0b1111; return; }
        case 0x005F6950: { io.SB_G2DTNRM = val & 0b111111'11111111'11111111; return; }
        case 0x005F6954: { io.SB_G2DTEXT = val & 0b1111; return; }
    }

    *success = false;
    printf("\nBUS write bad reg %08x(%d): %08llx", addr, sz, val);
}

u64 core::read_io_g1(u32 addr, u8 sz, bool *success) {
    switch (addr) {
        case 0x005F7404:  { return g1.SB_GDSTAR; }
        case 0x005F7408:  { return g1.SB_GDLEN; }
        case 0x005F740C:  { return g1.SB_GDDIR; }
        case 0x005F7414:  { return g1.SB_GDEN; }
        case 0x005F7418:  { return g1.SB_GDST; }
        case 0x005F74EC: return 3; // Part of GDROM unlock voodoo magic, 3 means success
        case 0x005f74b0: return (0x0 << 4) | (0x1); // SB_G1SYSM

    }
    *success = false;
    printf("\nG1 read bad reg %08x(%d)", addr, sz);
    return 0;
}

void core::write_io_g1(u32 addr, u8 sz, u64 val, bool *success) {
    switch (addr) {
        case 0x005F7404: { g1.SB_GDSTAR = (val & 0x1FFFFFE0); return; }
        case 0x005F7408: { g1.SB_GDLEN = (val & 0x01FFFFFF); return; }
        case 0x005F740C: { g1.SB_GDDIR = (val & 1); return; }
        case 0x005F7414: { g1.SB_GDEN = (val & 1); return; }
        case 0x005F7418: {
            g1.SB_GDST = (val & 1);
            // TODO: gdrom.dma_start();
            if (g1.SB_GDEN) printf("\nWARN GDROM DMA START! STAR:%08x LEN:%d DIR:%d EN:%d", g1.SB_GDSTAR, g1.SB_GDLEN, g1.SB_GDDIR, g1.SB_GDEN);
            return;
        }
        case 0x005F7480: { g1.SB_G1RRC = val; return; }
        case 0x005F7484: { g1.SB_G1RWC = val; return; }
        case 0x005F7488: { g1.SB_G1FRC = val; return; }
        case 0x005F748C: { g1.SB_G1FWC = val; return; }
        case 0x005F7490: { g1.SB_G1CRC = val; return; }
        case 0x005F7494: { g1.SB_G1CWC = val; return; }
        case 0x005F74A0: { g1.SB_G1GDRC = val; return; }
        case 0x005F74A4: { g1.SB_G1GDWC = val; return; }
        case 0x005F74B4: { g1.SB_G1CRDYC = val; return; }
        case 0x005F74B8: { if ((val >> 16) == 0x8843) { g1.SB_GDAPRO.u = val & 0x00007FFF; } return; }
        case 0x005F74E4: // Secret GDROM unlock register!
            return;
    }
    *success = false;
    printf("\nG1 write bad reg %08x(%d): %08llx", addr, sz, val);
}

u64 core::read_io_g2(u32 addr, u8 sz, bool *success) {
    return g2.read_io(addr, sz, success);
}

void core::write_io_g2(u32 addr, u8 sz, u64 val, bool *success) {
    g2.write_io(addr, sz, val, success);;
}

static void aica_irq(void *ptr) {
    auto *th = static_cast<core*>(ptr);
    th->holly.raise_interrupt(HOLLY::hirq_aica, -1);
}
    
core::core() :
    scheduler(&master_cycles),
    cpu(&scheduler, &master_cycles),
    holly(this),
    maple(this),
    g2(this),
    gdrom(this),
    aica(&scheduler, &master_cycles)
{
    has.load_BIOS = true;
    has.max_loaded_files = 0;
    has.max_loaded_folders = 0;
    has.save_state = false;

    scheduler.max_block_size = 64;
    scheduler.run.func[0] = scheduler.run.func[1] = &sch_run_block;
    scheduler.run.ptr = this;

    cpu.mptr = this;
    cpu.read8 = &cpu_read<1, false>;
    cpu.read16 = &cpu_read<2, false>;
    cpu.read32 = &cpu_read<4, false>;
    cpu.read64 = &cpu_read<8, false>;
    cpu.write8 = &cpu_write<1, false>;
    cpu.write16 = &cpu_write<2, false>;
    cpu.write32 = &cpu_write<4, false>;
    cpu.write64 = &cpu_write<8, false>;
    cpu.fetch_ins = &cpu_fetch_ins<false>;

    cpu.read8_debug = &cpu_read<1, true>;
    cpu.read16_debug = &cpu_read<2, true>;
    cpu.read32_debug = &cpu_read<4, true>;
    cpu.read64_debug = &cpu_read<8, true>;
    cpu.write8_debug = &cpu_write<1, true>;
    cpu.write16_debug = &cpu_write<2, true>;
    cpu.write32_debug = &cpu_write<4, true>;
    cpu.write64_debug = &cpu_write<8, true>;
    cpu.fetch_ins_debug = &cpu_fetch_ins<true>;

    aica.ext_irq = &aica_irq;
    aica.ext_irq_ptr = this;

    snprintf(label, sizeof(label), "Dreamcast");
    jsm_debug_read_trace dt;
    dt.read_trace_arm = &read_trace_cpu;
    dt.ptr = this;
    cpu.setup_tracing(dt, &master_cycles);

    described_inputs = false;

    RAM = malloc(0x1000000);
    setup_mmap();
}

core::~core() {
    if (RAM) free(RAM);
    RAM = nullptr;
}
}
