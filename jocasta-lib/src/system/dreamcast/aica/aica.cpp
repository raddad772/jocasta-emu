//
// Created by . on 4/3/26.
//
#include <ctime>

#include "helpers/multisize_memaccess.cpp"
#include "helpers/setbits.h"

#include "aica.h"
#include "../dc_bus.h"
#include "../dc_debugger.h"

#include <cstdlib>

namespace AICA {
void timer::tick() {
    regs.TIM++;
    if (regs.TIM == regs.max) {
        regs.TIM = 0;
        bus->raise_interrupt(6 + num);
    }
}

void timer::write(u16 val, u8 bnum) {
    if (!bnum) {
        regs.TIM = val & 0xFF;
    }
    else {
        regs.CTL = val & 7;
        regs.max = 1 << regs.CTL;
    }
}

u16 timer::read(u8 bnum) {
    if (!bnum) return regs.TIM;
    return regs.CTL;
}

void core::schedule_first() {
    // 32768Hz RTC ticks,
    // 44.1kHz sample updates,
    //ARM speed is 22.579_200 MHz... but can only do 5644800 16-bit reads per second

    // main bus is 200000000,
    // 44.1khz divisor is   /512 off ARM
    // 35.43083900227 master cycles per ARM memory access allowed
    // 689.0625 ARM cycles per RTC cycle, or 6103.515625 off main...
    //

    // Dreamcast main bus speed is

    // So, ARM must go
    timing.next_audio_cycle = timing.next_rtc_cycle = timing.next_cpu_cycle = 0;
    if (!regs.ARMRST) schedule_next_cpu_tick();
    schedule_next_rtc_tick();
    schedule_next_audio_tick();
}

void core::recalc_interrupts_sh4() {
    u32 interrupt_out = (regs.MC_IE & regs.MC_IF) != 0;
    if (interrupt_out) {
        ext_irq(ext_irq_ptr);
    }
    old_interrupt_out = interrupt_out;
}

void core::recalc_interrupts_arm() {
    if (irq.IE & irq.IF) {
        for (u32 i = 0; i < 11; i++) {
            u32 bit = 1 << i;
            if (irq.IE & irq.IF & bit) {
                u32 SCI_bit = i < 7 ? 1 << i : 1 << 7;
                u32 b0 = (regs.SCILV0 >> SCI_bit) & 1;
                u32 b1 = (regs.SCILV1 >> SCI_bit) & 1;
                u32 b2 = (regs.SCILV2 >> SCI_bit) & 1;
                regs.INTREQ &= ~7;
                regs.INTREQ |= b0 | (b1 << 1) | (b2 << 2);
            }
        }
        cpu.regs.FIQ_line = true;
    }
    else {
        regs.INTREQ &= ~7;
        cpu.regs.FIQ_line = false;
    }
}

void core::raise_interrupt(int num) {
    u32 bit = 1 << num;
    irq.IF |= bit;
    regs.MC_IF |= bit;

    recalc_interrupts_arm();
    recalc_interrupts_sh4();
}

static void sch_cpu_tick(void *ptr, u64 key, u64 clock, u32 jitter) {
    auto *th = static_cast<core *>(ptr);
    th->cpu_tick();
}

static void sch_rtc_tick(void *ptr, u64 key, u64 clock, u32 jitter) {
    auto *th = static_cast<core *>(ptr);
    th->rtc_tick();
}

static void sch_audio_tick(void *ptr, u64 key, u64 clock, u32 jitter) {
    auto *th = static_cast<core *>(ptr);
    th->audio_tick();
}

void core::pause_cpu() {
    cpu_paused = true;
    if (!timing.cpu_still_sched) return; // CPU is paused or in reset!
    deschedule_cpu();
    cpu_paused = true;
    timing.cpu_surplus = timing.next_cpu_cycle - *master_clock;
    if (timing.cpu_surplus < 0) timing.cpu_surplus = 0;
}

void core::unpause_cpu() {
    cpu_paused = false;
    if (timing.cpu_still_sched || regs.ARMRST) return; // CPU is not paused, or ARMRST is asserted

    timing.next_cpu_cycle = *master_clock + timing.cpu_surplus;
    schedule_next_cpu_tick();
}

void core::schedule_next_cpu_tick() {
    timing.cpu_sched_id = scheduler->only_add_abs(timing.next_cpu_cycle, 0, this, &sch_cpu_tick, &timing.cpu_still_sched);
    // CPU cycle time is incremented during execution
}

void core::schedule_next_rtc_tick() {
    scheduler->only_add_abs(timing.next_rtc_cycle, 0, this, &sch_rtc_tick, nullptr);
    timing.next_rtc_cycle += timing.rtc_divisor;
}

void core::schedule_next_audio_tick() {
    scheduler->only_add_abs(timing.next_audio_cycle, 0, this, &sch_audio_tick, nullptr);
    timing.next_audio_cycle += timing.audio_divisor;
}

template <u8 sz, bool do_debug>
static u32 arm_read_data(void *ptr, u32 addr, u8 access) {
    auto *th = static_cast<core *>(ptr);
    return th->arm_read<sz, do_debug>(addr, access, true);
}
template <u8 sz, bool do_debug>
static void arm_write_data(void *ptr, u32 addr, u8 access, u32 val) {
    auto *th = static_cast<core *>(ptr);
    th->arm_write<sz, do_debug>(addr, access, val);
}


void core::try_dma() {
    // TODO: this
    // just DMA reg->RAM or vice versa,
    // and set the correct IF bit
}

void core::cpu_mem_delay(u8 sz) {
    u32 r = sz == 4 ? 2 : 1;
    timing.next_cpu_cycle += r * timing.ram_16bit_divisor;
}

template<u8 sz, bool do_debug>
static u32 arm_read_ins(void *ptr, u32 addr, u8 access) {
    return arm_read_data<sz, do_debug>(ptr, addr, access);
}

static void arm_idle(void *ptr, u32 num) {
    auto *th = static_cast<core *>(ptr);
    th->timing.next_cpu_cycle += num * th->timing.cpu_divisor;
}

static constexpr u32 mszmask[9] = { 0, 0xF'FFFF, 0xF'FFFE, 0, 0xF'FFFC, 0, 0, 0, 0xF'FFF8 };

u64 core::read_reg8(u32 addr, bool *success) {
    // 40 regs per channel?
    if ((addr & 3) > 1) return 0;
    if (addr < 0x80'2000) {
        return channels[getbits<7, 12>(addr)].read_reg8(addr & 0x7F, success);
    }
    if (addr <= 0x80'2044) {
        return addr & 1 ? regs.DSP_OUT_1[(addr >> 2) & 0x1F].EFSDL :  regs.DSP_OUT_1[(addr >> 2) & 0x1F].EFPAN;
    }
    u32 v = 0;
    switch (addr) {
        case 0x0080'2800:
            setbits<0, 3>(v, regs.MVOL);
            setbits<4, 7>(v, regs.VER);
            return v;
        case 0x0080'2801:
            setbit<0>(v, regs.DAC18B);
            setbit<1>(v, regs.MEM8MB);
            setbit<7>(v, regs.MONO);
            return v;
        case 0x0080'2804: return getbits<11, 18>(regs.RBP);
        case 0x0080'2805:
            setbits<0, 3>(v, getbits<19, 22>(regs.RBP));
            setbits<5, 6>(v, regs.RBL);
            setbit<7>(v, regs.TSTB0);
            return v;
        case 0x0080'2808: return 0;
        case 0x0080'2809: return 0;
        case 0x0080'280C: return 0;
        case 0x0080'280D:
            setbits<0, 5>(v, regs.MSLC);
            setbit<6>(v, regs.AFSET);
            return v;
        case 0x0080'2810: return getbyte<0>(channels[regs.MSLC].regs.EGC);
        case 0x0080'2811: {
            auto &c = channels[regs.MSLC];
            setbits<0, 4>(v, getbits<8, 12>(c.regs.EGC));
            setbits<5, 6>(v, c.regs.SGC);
            setbit<7>(v, c.regs.LP);
            return v;
        }
        case 0x0080'2814: return getbyte<0>(channels[regs.MSLC].adpcm.sample_num);
        case 0x0080'2815: return getbyte<1>(channels[regs.MSLC].adpcm.sample_num);

        case 0x0080'2880:
            setbits<0, 3>(v, regs.MRWINH);
            setbit<4>(v, regs.TSTB0);
            setbits<5, 7>(v, regs.TSCD);
            return v;
        case 0x0080'2881:
            setbits<1, 7>(v, getbits<16, 22>(regs.DMEA));
            return v;
        case 0x0080'2884: return getbyte<0>(regs.DMEA);
        case 0x0080'2885: return getbyte<1>(regs.DMEA);
        case 0x0080'2888: return regs.DRGA << 1;
        case 0x0080'2889:
            setbits<0, 6>(v, getbits<8, 14>(regs.DRGA));
            setbit<7>(v, regs.DGATE);
            return v;
        case 0x0080'288C:
            setbits<1, 7>(v, getbits<1, 7>(regs.DLG));
            setbit<0>(v, regs.DEXE);
            return v;
        case 0x0080'288D:
            setbits<0, 6>(v, getbits<8, 14>(regs.DLG));
            setbit<7>(v, regs.DDIR);
            return v;
        case 0x0080'2890: return timers[0].read(0);
        case 0x0080'2891: return timers[0].read(1);
        case 0x0080'2894: return timers[1].read(0);
        case 0x0080'2895: return timers[1].read(1);
        case 0x0080'2898: return timers[2].read(0);
        case 0x0080'2899: return timers[2].read(1);
        case 0x0080'289C: return getbyte<0>(irq.IE);
        case 0x0080'289D: return getbyte<1>(irq.IE);
        case 0x0080'28A0: return getbyte<0>(irq.IF);
        case 0x0080'28A1: return getbyte<1>(irq.IF);
        case 0x0080'28A4: return getbyte<0>(irq.IF);
        case 0x0080'28A5: return getbyte<1>(irq.IF);
        case 0x0080'28A8: return getbyte<0>(regs.SCILV0);
        case 0x0080'28A9: return getbyte<1>(regs.SCILV0);
        case 0x0080'28AC: return getbyte<0>(regs.SCILV1);
        case 0x0080'28AD: return getbyte<1>(regs.SCILV1);
        case 0x0080'28B0: return getbyte<0>(regs.SCILV2);
        case 0x0080'28B1: return getbyte<1>(regs.SCILV2);
        case 0x0080'28B4: return getbyte<0>(regs.MC_IE);
        case 0x0080'28B5: return getbyte<1>(regs.MC_IE);
        case 0x0080'28B8: return getbyte<0>(regs.MC_IF);
        case 0x0080'28B9: return getbyte<1>(regs.MC_IF);
        case 0x0080'28BC: return getbyte<0>(regs.MC_IF);
        case 0x0080'28BD: return getbyte<1>(regs.MC_IF);

        case 0x0080'2C00: return regs.ARMRST;
        case 0x0080'2C01: return 0;
        case 0x0080'2D00: return regs.INTREQ;
        case 0x0080'2D01: return 0;
        case 0x0080'2D04: return regs.INTREQ;
        case 0x0080'2D05: return regs.INTREQ;
        case 0x0081'0000: return getbyte<2>(rtc.counter);
        case 0x0081'0001: return getbyte<3>(rtc.counter);
        case 0x0081'0004: return getbyte<0>(rtc.counter);
        case 0x0081'0005: return getbyte<1>(rtc.counter);
        case 0x0081'0008: return rtc.enable_write;
        case 0x0081'0009: return 0;
    }
#define DSPA(a,b) if ((addr >= (a)) && (addr <= (b))) { u32 n = (addr - (a)) >> 2;
    DSPA(0x0080'3000, 0x0080'31FF)
        return getbyte(dsp.COEF[n] << 3, addr & 1);
    }
    DSPA(0x0080'3200, 0x0080'32FF)
        return getbyte(dsp.MADRS[n] >> 1, addr & 1);
    }
    DSPA(0x0080'3400, 0x0080'3BFF)
    // n = addr >> 2
    // n & 3 is MPRO portion
    // n >> 2 is MPRO num
        u64 ve = dsp.MPRO[n >> 2];
        switch (n & 3) {
            case 0: return getbyte(ve >> 48, addr & 1);
            case 1: return getbyte((ve >> 32) & 0xFFFF, addr & 1);
            case 2: return getbyte((ve >> 16) & 0xFFFF, addr & 1);
            case 3: return getbyte(ve & 0xFFFF, addr & 1);
        }
    }

    DSPA(0x0080'4000, 0x0080'43FF)
        v = dsp.TEMP[n >> 1];
        switch (n & 1) {
            case 0: return getbyte(v & 0xFF, addr & 1);
            case 1: return getbyte(v >> 8, addr & 1);
        }
    }

    DSPA(0x0080'4400, 0x0080'44FF)
        v = dsp.MEMS[n >> 1];
        switch (n & 1) {
            case 0: return getbyte(v & 0xFF, addr & 1);
            case 1: return getbyte(v >> 8, addr & 1);
        }
    }

    DSPA(0x0080'4500, 0x0080'457F)
        v = dsp.MIXS[n >> 1];
        switch (n & 1) {
            case 0: return getbyte(v & 0xF, addr & 1);
            case 1: return getbyte(v >> 4, addr & 1);
        }
    }

    DSPA(0x0080'4580, 0x0080'45BF)
        return getbyte(dsp.EFREG[n], addr & 1);
    }

    DSPA(0x0080'45C0, 0x0080'45C7)
        return getbyte(dsp.EXTS[n], addr & 1);
    }
#undef DSPA
    *success = false;
    return 0;
}

void core::deschedule_cpu() {
    if (timing.cpu_still_sched) {
        scheduler->delete_if_exist(timing.cpu_sched_id);
    }
}

void core::write_reg8(u32 addr, u16 val, bool *success) {
    // Eachc hannel is 0x80 long
    // so bits 00..7F are the reg # so bits 0-6
    //
    if ((addr & 3) > 1) return;
    if (addr < 0x80'2000) {
        channels[getbits<7, 12>(addr)].write_reg8(addr & 0x7F, val, success);
        return;
    }
    if (addr <= 0x80'2044) {
        if (addr & 1) regs.DSP_OUT_1[(addr >> 2) & 0x1F].EFSDL = val & 0x1F;
        else regs.DSP_OUT_1[(addr >> 2) & 0x1F].EFPAN = val & 0x1F;;
        return;
    }
    switch (addr) {
        case 0x0080'2800:
            regs.MVOL = getbits<0, 3>(val);
            regs.VER = getbits<4, 7>(val);
            return;
        case 0x0080'2801:
            regs.DAC18B = getbit<0>(val);
            regs.MEM8MB = getbit<1>(val);
            regs.MEM8MB = 0;
            regs.MONO = getbit<7>(val);
            return;
        case 0x0080'2804:
            setbits<11, 18>(regs.RBP, val);
            return;
        case 0x0080'2805:
            setbits<19, 22>(regs.RBP, getbits<0, 3>(val));
            regs.RBL = getbits<5, 6>(val);
            regs.TSTB0 = getbit<7>(val);
            return;
        case 0x0080'2808: return; //regs.MIBUF = val & 0xFF; return;
        case 0x0080'2809:
            //regs.MIFUL = (val >> 9) & 1;
            //regs.MIOVF = (val >> 10) & 1;
            //regs.MOEMP = (val >> 11) & 1;
            //regs.MOFUL = (val >> 12) & 1;
            return;
        case 0x0080'280C:
            return;
        case 0x0080'280D:
            regs.MSLC = getbits<0, 5>(val);
            regs.AFSET = getbit<6>(val);
            return;
        case 0x0080'2810: setbyte<0>(channels[regs.MSLC].regs.EGC, val); return;
        case 0x0080'2811: {
            auto &c = channels[regs.MSLC];
            setbits<8, 12>(c.regs.EGC, getbits<0,4>(val));
            c.regs.SGC = getbits<5, 6>(val);
            c.regs.LP = getbit<7>(val);
            return;
        }

        case 0x0080'2814:
        case 0x0080'2815: {
            auto &c = channels[regs.MSLC];
            printf("\nWARN TRY TO WRITE SAMPLE POS??");
            return;
        }

        case 0x0080'2880:
            regs.MRWINH = getbits<0, 3>(val);
            regs.TSTB0 = getbit<4>(val);
            regs.TSCD = getbits<5, 7>(val);
            return;
        case 0x0080'2881: setbits<16, 22>(regs.DMEA, getbits<1, 7>(val)); return;
        case 0x0080'2884: setbyte<0>(regs.DMEA, val); return;
        case 0x0080'2885: setbyte<1>(regs.DMEA, val); return;
        case 0x0080'2888: setbits<1, 7>(regs.DRGA, getbits<1, 7>(val));
        case 0x0080'2889:
            setbits<8, 14>(regs.DRGA, getbits<0, 6>(val));
            regs.DGATE = getbit<7>(val);
            return;
        case 0x0080'288C:
            setbits<1, 7>(regs.DLG, getbits<1, 7>(val));
            regs.DEXE = getbit<0>(val);
            try_dma();
            return;
        case 0x0080'288D:
            setbits<8, 14>(regs.DLG, getbits<0, 6>(val));
            regs.DDIR = getbit<7>(val);
            return;
        case 0x0080'2890: timers[0].write(val, 0); return;
        case 0x0080'2891: timers[0].write(val, 1); return;
        case 0x0080'2894: timers[1].write(val, 0); return;
        case 0x0080'2895: timers[1].write(val, 1); return;
        case 0x0080'2898: timers[2].write(val, 0); return;
        case 0x0080'2899: timers[2].write(val, 1); return;
        case 0x0080'289C: setbits<0, 7>(irq.IE, val); recalc_interrupts_arm(); printf("\nLOW for IRQ IE: %02x", val); return;
        case 0x0080'289D: setbits<8, 10>(irq.IE, getbits<0, 2>(val)); recalc_interrupts_arm(); printf("\nHI for IRQ IE: %02x", val); return;
        case 0x0080'28A0: irq.IF |= val & 0b100000; recalc_interrupts_arm(); return;
        case 0x0080'28A1: return;
        case 0x0080'28A4: irq.IF &= (val & 0xFF) ^ 0x7FF; recalc_interrupts_arm(); return;
        case 0x0080'28A5: irq.IF &= ((val << 8) & 0x700) ^ 0x7FF; recalc_interrupts_arm(); return;
        case 0x0080'28A8: setbyte<0>(regs.SCILV0, val); recalc_interrupts_arm(); return;
        case 0x0080'28A9: setbits<8, 10>(regs.SCILV0, getbits<0, 2>(val)); recalc_interrupts_arm(); return;
        case 0x0080'28AC: setbyte<0>(regs.SCILV1, val); recalc_interrupts_arm(); return;
        case 0x0080'28AD: setbits<8, 10>(regs.SCILV1, getbits<0, 2>(val)); recalc_interrupts_arm(); return;
        case 0x0080'28B0: setbyte<0>(regs.SCILV2, val); recalc_interrupts_arm(); return;
        case 0x0080'28B1: setbits<8, 10>(regs.SCILV2, getbits<0, 2>(val)); recalc_interrupts_arm(); return;
        case 0x0080'28B4: setbyte<0>(regs.MC_IE, val);recalc_interrupts_sh4(); return;
        case 0x0080'28B5: setbits<8, 10>(regs.MC_IE, getbits<0, 2>(val)); recalc_interrupts_sh4(); return;
        case 0x0080'28B8: regs.MC_IF |= val & 0b100000; recalc_interrupts_sh4(); return;
        case 0x0080'28B9: return;
        case 0x0080'28BC: regs.MC_IF &= (val & 0xFF) ^ 0x7FF; recalc_interrupts_sh4(); return;
        case 0x0080'28BD: regs.MC_IF &= ((val << 8) & 0x700) ^ 0x7FF; recalc_interrupts_sh4(); return;

        case 0x0080'2C00: {
            if (val & 1) {
                dbgloglog_aican(trace.cpu_reset_id, DBGLS_TRACE, "AICA: ARMRST set. Pausing CPU");
                deschedule_cpu();
            }
            else {
                printf("\nAICA: ARMRST UNSET!");
                if ((regs.ARMRST & 1) && (!cpu_paused)) {
                    printf("\nAICA: RESETTING ARM!");
                    timing.next_cpu_cycle = *master_clock;
                    dbgloglog_aican(trace.cpu_reset_id, DBGLS_TRACE, "AICA: ARMRST unset. Resetting CPU");
                    cpu.reset<false>();
                    schedule_next_cpu_tick();
                    //dbg_break("ARMRST UNSET", 0);
                }
            }
            regs.ARMRST = val & 1;
            return;
        }
        case 0x0080'2C01: return;
        case 0x0080'2D00:
            regs.INTREQ = regs.INTREQ | (val & 0b11111000);
            return;
        case 0x0080'2D01: return;
        case 0x0080'2D04:
            regs.INTREQ &= (val & 0xFF) ^ 0xFF;
            return;
        case 0x0080'2D05: return;
        case 0x0081'0000:
            if (rtc.enable_write) {
                setbyte<2>(rtc.counter, val);
            }
            return;
        case 0x0081'0001:
            if (rtc.enable_write) {
                rtc.enable_write = 0;
                setbyte<3>(rtc.counter, val);
            }
            return;

        case 0x0081'0004: if (rtc.enable_write) setbyte<0>(rtc.counter, val); return;
        case 0x0081'0005: if (rtc.enable_write) setbyte<1>(rtc.counter, val); return;
        case 0x0081'0008: rtc.enable_write = val & 1; return;
        case 0x0081'0009: return;
    }
    // writereg8 START HERE
#define DSPA(a,b) if ((addr >= (a)) && (addr <= (b))) { u32 n = (addr - (a)) >> 2;
    DSPA(0x0080'3000, 0x0080'31FF)
        setbyte(dsp.COEF[n], addr & 1, (addr & 1) ? val : val >> 3);
        return;
    }
    DSPA(0x0080'3200, 0x0080'32FF)
        setbyte(dsp.MADRS[n], addr & 1, addr & 1 ? val : val << 1);
        return;
    }
    DSPA(0x0080'3400, 0x0080'3BFF)
        // n = addr >> 2
        // n & 3 is MPRO portion
        // n >> 2 is MPRO num
        switch (n & 3) {
            case 0: setbyte(dsp.MPRO[n >> 2], (addr & 1) + 6, val); return;
            case 1: setbyte(dsp.MPRO[n >> 2], (addr & 1) + 4, val); return;
            case 2: setbyte(dsp.MPRO[n >> 2], (addr & 1) + 2, val); return;
            case 3: setbyte(dsp.MPRO[n >> 2], addr & 1, val); return;
        }
        return;
    }

    DSPA(0x0080'4000, 0x0080'43FF)
        switch (n & 1) {
            case 0: if (!(addr & 1)) setbyte<0>(dsp.TEMP[n >> 1], val); return;
            case 1: setbyte(dsp.TEMP[n >> 1], 1 + (addr & 1), val); return;
        }
        return;
    }

    DSPA(0x0080'4400, 0x0080'44FF)
        switch (n & 1) {
            case 0: if (!(addr & 1)) dsp.MEMS_L[n >> 1] = val & 0xFF; return;
            case 1:
                setbyte<0>(dsp.MEMS[n >> 1], dsp.MEMS_L[n >> 1]);
                setbyte(dsp.MEMS[n >> 1], (addr & 1) + 1, val);
                return;
        }
        return;
    }

    DSPA(0x0080'4500, 0x0080'457F)
        switch (n & 1) {
            case 0: if (!(addr & 1)) setbits<0, 3>(dsp.MIXS[n >> 1], val); return;
            case 1:
                if (addr & 1) setbits<12, 19>(dsp.MIXS[n >> 1], val);
                else setbits<4, 11>(dsp.MIXS[n >> 1], val);
                return;
        }
        return;
    }

    DSPA(0x0080'4580, 0x0080'45BF)
        setbyte(dsp.EFREG[n], addr & 1, val);
        return;
    }

    DSPA(0x0080'45C0, 0x0080'45C7)
        setbyte(dsp.EXTS[n], addr & 1, val);
        return;
    }
#undef DSPA

    *success = false;
}


core::core(scheduler_t *scheduler_in, u64 *master_clock_in) :
    scheduler(scheduler_in),
    master_clock(master_clock_in),
    cpu(scheduler_in, master_clock_in, &cpu_waitstates)
{
    RAM = static_cast<u8 *>(malloc(2 * 1024 * 1024));

    cpu.read_ptr = this;
    cpu.fetch_ptr = this;
    cpu.write_ptr = this;
    cpu.read_func8 = &arm_read_data<1, false>;
    cpu.read_func16 = &arm_read_data<2, false>;
    cpu.read_func32= &arm_read_data<4, false>;
    cpu.write_func8 = &arm_write_data<1, false>;
    cpu.write_func16 = &arm_write_data<2, false>;
    cpu.write_func32 = &arm_write_data<4, false>;
    cpu.fetch_ins_func16 = &arm_read_ins<2, false>;
    cpu.fetch_ins_func32 = &arm_read_ins<4, false>;

    cpu.read_func8_debug = &arm_read_data<1, true>;
    cpu.read_func16_debug = &arm_read_data<2, true>;
    cpu.read_func32_debug = &arm_read_data<4, true>;
    cpu.write_func8_debug = &arm_write_data<1, true>;
    cpu.write_func16_debug = &arm_write_data<2, true>;
    cpu.write_func32_debug = &arm_write_data<4, true>;
    cpu.fetch_ins_func16_debug = &arm_read_ins<2, true>;
    cpu.fetch_ins_func32_debug = &arm_read_ins<4, true>;

    cpu.idle_ptr = this;
    cpu.idle_func = &arm_idle;

    timing.master_clock_speed = 200000000;
    timing.cpu_speed = 22579200;
    timing.cpu_divisor = timing.master_clock_speed / timing.cpu_speed;
    timing.rtc_divisor = timing.master_clock_speed;
    timing.audio_divisor = timing.master_clock_speed / 44100.0;
    timing.ram_16bit_divisor = timing.master_clock_speed / 5644800.0;
    timing.transfer_32_bytes = timing.ram_16bit_divisor * 16.0;

    for (u32 i = 0; i < 3; i++) {
        timers[i].bus = this;
        timers[i].num = i;
    }

    for (u32 i = 0; i < 64; i++) {
        auto &c = channels[i];
        c.num = i;
        c.bus = this;
    }
}

core::~core() {
    if (RAM) free(RAM);
    RAM = nullptr;
}

void core::cpu_tick() {
    // This can happen UP TO 22.5 million times per second
    // HOWEVER its real speed will be much closer to 2.5mHz because of 16-bit read speeds (for 32-bit instruction fetches)
    if (::dbg.do_debug) {
        cpu.IRQcheck<true, false, false>();
        cpu.run_ARM<true>(); // no THUMB on this one, so just go straight to ARM
    }
    else {
        cpu.IRQcheck<false, false, false>();
        cpu.run_ARM<false>(); // no THUMB on this one, so just go straight to ARM
    }

    schedule_next_cpu_tick();
}

void core::rtc_tick() {
    // TODO: IRQs? ???
    rtc.counter++;
    schedule_next_rtc_tick();
}

#define CLAMP(x, low, high) ((x) < (low) ? (low) : ((x) > (high) ? (high) : (x)))

static inline int16_t ymz_step(u8 step, i16 &history, i16 &step_size)
{
    static const int step_table[8] = {
        230, 230, 230, 230, 307, 409, 512, 614
    };

    i32 sign = step & 8;
    i32 delta = step & 7;
    int diff = ((1+(delta<<1)) * step_size) >> 3;
    int newval = history;
    int nstep = (step_table[delta] * step_size) >> 8;
    // Only found in the official AICA encoder
    // but it's possible all chips (including ADPCM-B) does this.
    diff = CLAMP(diff, 0, 32767);
    if (sign > 0)
        newval -= diff;
    else
        newval += diff;
    step_size = CLAMP(nstep, 127, 24576);
    history = newval = CLAMP(newval, -32768, 32767);
    return newval;
}

void audio_channel::get_next_sample() {
    sample.history = sample.current;
    switch (regs.SSCTL) {
        case 0: //16-bit PCM
            sample.current = static_cast<i16>(cR16(bus->RAM, adpcm.addr));
            adpcm.addr = (adpcm.addr + 2) & 0x1F'FFFE;
            break;
        case 1: //8-bit PCM
            sample.current = static_cast<i16>(static_cast<u16>(cR8(bus->RAM, adpcm.addr) << 8));
            adpcm.addr = (adpcm.addr + 1) & 0x1F'FFFF;
            break;
        case 2: {
            u8 step = cR8(bus->RAM, adpcm.addr) << adpcm.nibble;
            step >>= 4;
            if (!adpcm.nibble)
                adpcm.addr = (adpcm.addr + 1) & 0x1F'FFFF;
            adpcm.nibble ^= 4;
            adpcm.history = static_cast<i16>(static_cast<i32>(adpcm.history) * 254 / 256); // Literal high-pass filter...
            sample.current = ymz_step(step, adpcm.history, adpcm.step_size);
            break;
        }
        case 3:
            printf("\nLong-stream not support!");
            return;
    }
    // Check loop end
    if (adpcm.sample_num == regs.LEA) {
        regs.LP = 1;
        if (!regs.LPCTL) { // Processing ends at LEA...
            end_processing();
        }
        else {
            u32 start_samples = regs.LSA;
            adpcm.sample_num = regs.LSA;
            switch (regs.SSCTL) {
                case 0: start_samples <<= 1; break;
                case 1: break;
                case 2: start_samples >>= 1; break;
            }
            adpcm.addr = regs.SA + start_samples;
        }
    }
}

void audio_channel::tick() {
    // TODO:
    /*
     SO.
     (FNS + 0x400) << or >> OCT.
     each 0x400, there is a phase increment

     counter >> 10 is sample index


     *
     */
    if (!keyed_on) return;
    u32 LFO = 0;
    u32 FNS = regs.FNS + 0x400;
    // TODO: LFO
    u32 pitch_advance;
    if (regs.OCT >= 8) {
        u32 shift = (regs.OCT ^ 0xF) + 1;
        pitch_advance = LFO + FNS;
        pitch_advance >>= shift;
    }
    else {
        u32 OCT = regs.OCT;
        if (regs.PCMS == 2) {
            if (OCT >= 2) {
                FNS = 0;
                OCT = 2;
            }
        }
        pitch_advance = LFO + FNS;
        pitch_advance <<= OCT;
    }

    while (counter >= 0x400) {
        get_next_sample();
        counter -= 0x400;
    }

    // Now linearly interpolate based on the counter between history and current
    i32 history_mul = 0x400 - counter;
    i32 current_mul = counter;
    i32 val = (static_cast<i32>(sample.history) * history_mul + static_cast<i32>(sample.current) * current_mul) >> 10;
    val = CLAMP(val, -0x8000, 0x7FFF);

    // Now apply envelope and volume!?!?
}

u16 audio_channel::read_LPSGCEGC() const {
    u16 v = regs.EGC;
    v |= regs.SGC << 13;
    v |= regs.LP << 15;
    return v;
}

u16 audio_channel::read_reg8(u32 addr, bool *success) {
    u32 v = 0;
    switch (addr) {
        case 0x00:
            setbits<0, 6>(v, getbits<16, 22>(regs.SA));
            setbit<7>(v, getbit<0>(regs.PCMS));
            return v;
        case 0x01:
            setbit<0>(v, getbit<1>(regs.PCMS));
            setbit<1>(v, regs.LPCTL);
            setbit<2>(v, regs.SSCTL);
            setbit<6>(v, regs.KYONB);
            setbit<7>(v, regs.KYONEX);
            return v;
        case 0x04: return getbyte<0>(regs.SA);
        case 0x05: return getbyte<1>(regs.SA);
        case 0x08: return getbyte<0>(regs.LSA);
        case 0x09: return getbyte<1>(regs.LSA);
        case 0x0C: return getbyte<0>(regs.LEA);
        case 0x0D: return getbyte<1>(regs.LEA);
        case 0x10:
            setbits<0, 4>(v, regs.AR);
            setbits<6, 7>(v, getbits<0, 1>(regs.D1R));
            return v;
        case 0x11:
            setbits<0, 2>(v, getbits<2, 4>(regs.D1R));
            setbits<3, 7>(v, regs.D2R);
            return v;
        case 0x14:
            setbits<0, 4>(v, regs.RR);
            setbits<5, 7>(v, getbits<0, 2>(regs.DL));
            return v;
        case 0x15:
            setbits<0, 1>(v, getbits<3, 4>(regs.DL));
            setbits<2, 3>(v, regs.KRS);
            setbit<6>(v, regs.LPSLNK);
            return v;
        case 0x18: return getbyte<0>(regs.FNS);
        case 0x19:
            setbits<0, 1>(v, getbits<8, 9>(regs.FNS));
            setbits<3, 6>(v, regs.OCT);
            return v;
        case 0x1C:
            setbits<0, 2>(v, regs.ALFOS);
            setbits<3, 4>(v, regs.ALFOWS);
            setbits<5, 7>(v, regs.PLFOS);;
            return v;
        case 0x1D:
            setbits<0,1>(v, regs.PLFOWS);
            setbits<2,6>(v, regs.LFOF);
            setbit<7>(v, regs.LFORE);
            return v;
        case 0x20:
            setbits<3, 6>(v, regs.ISEL);
            setbit<7>(v, getbit<0>(regs.TL));
            return v;
        case 0x21:
            setbits<0, 6>(v, getbits<1, 7>(regs.TL));
            return v;
        case 0x24: return regs.DIPAN;
        case 0x25:
            setbits<0, 3>(v, regs.DISDL);;
            setbits<4, 7>(v, regs.IMXL);;
            return v;
        case 0x28: return regs.Q;
        case 0x29: return 0;
        case 0x2C: return getbyte<0>(regs.FLV[0]);
        case 0x2D: return getbyte<1>(regs.FLV[0]);
        case 0x30: return getbyte<0>(regs.FLV[1]);
        case 0x31: return getbyte<1>(regs.FLV[1]);
        case 0x34: return getbyte<0>(regs.FLV[2]);
        case 0x35: return getbyte<1>(regs.FLV[2]);
        case 0x38: return getbyte<0>(regs.FLV[3]);
        case 0x39: return getbyte<1>(regs.FLV[3]);
        case 0x3C: return getbyte<0>(regs.FLV[4]);
        case 0x3D: return getbyte<1>(regs.FLV[4]);
        case 0x40: return regs.FD1R;
        case 0x41: return regs.FAR;
        case 0x44: return regs.FRR;
        case 0x45: return regs.FD2R;
    }
    *success = false;
    printf("\nREAD BAD AICA CH REG %08x", addr);
    return 0;
}

void audio_channel::keyon() {
    adsr_load_attack();
    adpcm.reset();
    regs.LP = 0;
    adpcm.addr = regs.SA;
    keyed_on = true;
    printf("\nKEYON %d", num);
}

void audio_channel::adsr_load_attack() {
    printf("\nAICA: WARN ATTACK %d", num);
}

void audio_channel::adsr_load_release() {
    //printf("\nAICA: WARN RELEASE %d", num);
}

void audio_channel::keyoff() {
    adsr_load_release();
    keyed_on = false;
}

void audio_channel::end_processing() {
    keyed_on = false;
}

void audio_channel::update_KYONB_KYONEX(u16 val) {
    regs.KYONB = (val >> 6) & 1;
    if (val & 0x80) {
        bus->kyonex();
    }
}

void audio_channel::write_reg8(u32 addr, u16 val, bool *success) {
    switch (addr) {
        case 0x00:
            setbits<16, 22>(regs.SA, getbits<0, 6>(val));
            setbits<0, 0>(regs.PCMS, getbits<7, 7>(val));
            return;
        case 0x01:
            setbits<1, 1>(regs.PCMS, getbits<0, 0>(val));
            regs.LPCTL = getbit<1>(val);
            regs.SSCTL = getbit<2>(val);
            update_KYONB_KYONEX(val);
            return;
        case 0x04: setbyte<0>(regs.SA, val); return;
        case 0x05: setbyte<1>(regs.SA, val); return;
        case 0x08: setbyte<0>(regs.LSA, val); return;
        case 0x09: setbyte<1>(regs.LSA, val); return;
        case 0x0C: setbyte<0>(regs.LEA, val); return;
        case 0x0D: setbyte<1>(regs.LEA, val); return;
        case 0x10:
            regs.AR = getbits<0, 4>(val);
            setbits<0, 1>(regs.D1R, getbits<6, 7>(val));
            return;
        case 0x11:
            setbits<2, 4>(regs.D1R, getbits<0, 2>(val));
            regs.D2R = getbits<3, 7>(val);
            return;
        case 0x14:
            regs.RR = getbits<0, 4>(val);
            setbits<0, 2>(regs.DL, getbits<5, 7>(val));
            return;
        case 0x15:
            setbits<3, 4>(regs.DL, getbits<0, 1>(val));
            regs.KRS = getbits<2, 3>(val);
            regs.LPSLNK = getbit<6>(val);
            return;
        case 0x18: setbyte<0>(regs.FNS, val); return;
        case 0x19:
            setbits<8, 9>(regs.FNS, getbits<0, 1>(val));
            regs.OCT = getbits<3, 6>(val);
            return;
        case 0x1C:
            regs.ALFOS = getbits<0,2>(val);
            regs.ALFOWS = getbits<3,4>(val);
            regs.PLFOS = getbits<5,7>(val);
            return;
        case 0x1D:
            regs.PLFOWS = getbits<0,1>(val);
            regs.LFOF = getbits<2,6>(val);
            regs.LFORE = getbit<7>(val);
            return;
        case 0x20:
            regs.ISEL = getbits<3, 6>(val);
            setbit<0>(regs.TL, getbit<7>(val));
            return;
        case 0x21: setbits<1, 7>(regs.TL, getbits<0, 6>(val)); return;
        case 0x24: regs.DIPAN = getbits<0,4>(val); return;
        case 0x25:
            regs.DISDL = getbits<0,3>(val);
            regs.IMXL = getbits<4,7>(val);
            return;
        case 0x28: regs.Q = getbits<0, 4>(val); return;
        case 0x29: return;
        case 0x2C: setbyte<0>(regs.FLV[0], val); return;
        case 0x2D: setbits<8, 12>(regs.FLV[0], val); return;
        case 0x30: setbyte<0>(regs.FLV[1], val); return;
        case 0x31: setbits<8, 12>(regs.FLV[1], val); return;
        case 0x34: setbyte<0>(regs.FLV[2], val); return;
        case 0x35: setbits<8, 12>(regs.FLV[2], val); return;
        case 0x38: setbyte<0>(regs.FLV[3], val); return;
        case 0x39: setbits<8, 12>(regs.FLV[3], val); return;
        case 0x3C: setbyte<0>(regs.FLV[4], val); return;
        case 0x3D: setbits<8, 12>(regs.FLV[4], val); return;
        case 0x40: regs.FD1R = getbits<0, 4>(val); return;
        case 0x41: regs.FAR = getbits<0, 4>(val); return;
        case 0x44: regs.FRR = getbits<0, 4>(val); return;
        case 0x45: regs.FD2R = getbits<0, 4>(val); return;
    }
    //*success = false;
    //printf("\nWRITE BAD AICA CH%d REG %08x: %02x", num, addr, val);
}

void core::audio_tick() {
    i32 out_l = 0, out_r = 0;
    raise_interrupt(10);
    for (auto & t : timers) {
        t.tick();
    }
    for (auto &c: channels) {
        c.tick();
        out_l += c.out_l;
        out_r += c.out_r;
    }

    out_l = CLAMP(out_l, -0x8000, 0x7FFF);
    out_r = CLAMP(out_r, -0x8000, 0x7FFF);

    // TODO: get sample to audio output stream
    schedule_next_audio_tick();
}

void core::setRTC_now() {
    time_t rawtime = time(NULL);
    tm localtm, gmtm;
    localtm = *localtime(&rawtime);
    gmtm = *gmtime(&rawtime);
    gmtm.tm_isdst = -1;
    time_t time_offset = mktime(&localtm) - mktime(&gmtm);
    // 1/1/50 to 1/1/70 is 20 years and 5 leap days
    rtc.counter = static_cast<u32>((20 * 365 + 5) * 24 * 60 * 60 + rawtime + time_offset);
}

void core::kyonex() {
    for (auto & c : channels) {
        if (c.keyed_on != c.regs.KYONEX) {
            if (c.regs.KYONEX) c.keyon();
            else c.keyoff();
        }
    }
}

void core::reset() {
    cpu.reset<false>();
    regs.MOEMP = 1;

    regs.ARMRST = 1;
    cpu_paused = false;
    timing.last_ram_cycle = 0;
    timing.next_audio_cycle = timing.next_rtc_cycle = timing.next_cpu_cycle = 0;
    setRTC_now();
}
}
