#pragma once

#include "helpers/int.h"
#include "helpers/scheduler.h"
#include "component/cpu/arm32/arm32.h"
namespace DREAMCAST {
    struct core;
}

namespace AICA {
struct core;
struct timer {
    core *bus{};
    u32 num{};
    void tick();

    struct {
        u8 TIM{};
        u32 CTL{};

        u32 max{1};
    } regs;

    void write(u16 val, u8 bnum);
    u16 read(u8 bnum);
};

typedef union r16s {
    u8 b[2];
    u16 u{};
} r16s;

struct YMZ_decode {
    void reset() {
        history = 0;
        nibble = 0;
        step_size = 127;
        sample_num = 0;
    }
    i16 history{};
    u32 nibble{};
    u32 sample_num{};
    int16_t step_size{127};
    u32 addr{};
};

struct audio_channel {
    core *bus{};
    u32 num{};
    void tick();
    u16 read_LPSGCEGC() const;
    u16 read_reg8(u32 addr, bool *success);
    void write_reg8(u32 addr, u16 val, bool *success);
    void update_KYONB_KYONEX(u16 val);
    void adsr_load_attack();
    void adsr_load_release();
    void keyon();
    void keyoff();
    void end_processing();
    bool keyed_on{};
    i16 out_l{}, out_r{};
    YMZ_decode adpcm{};

    struct {
        u32 PCMS{}; // 0=16-bit PCm. 1=8-bit PCM. 2=4-bit ADPCM. 3=prohibited
        u32 PCLT{};
        u32 SSCTL{}; // 0=SDRAM input data. 1=noise input data
        u32 LPCTL{}; // 0=loop off (procesisng ends at LEA). 1=loop on
        u32 KYONB{}; // KEYON/OFF
        u32 KYONEX{}; // KYONB takes effect

        u32 LP{}; // loop end reached
        u32 SGC{}; // 0-3 envelope stage
        u32 EGC{}; // envelope level

       u32 SA{}; // Start address of sound data, in bytes


        u32 LEA{}, LSA{};

        u32 AR{}; // Attack rate 5bit
        u32 D1R{}; // Decay 1 rate 5bit
        u32 D2R{}; // Decay 2 rate 5bit
        u32 RR{}; // Release rate 5bit
        u32 DL{}; // Decay1->2 transition level, 5bit
        u32 KRS{}; // Envelope key-scaling 0...E. F = off, 0=minimum
        u32 LPSLNK{}; // Loop start link. EG go to decay 1 when read addr > loop start address. "When EG=00 there is no transition"

        u32 FNS{}; // Specifies sound pitch setting to FNS and OCT register, 10 bits.
        // FNS=0 Octave=0 = 44100kHz
        u32 OCT{}; // Octave, signed 4-bit

        u32 ALFOS{};
        u32 ALFOWS{};
        u32 PLFOS{};
        u32 PLFOWS{};
        u32 LFOF{};
        u32 LFORE{};

        u32 ISEL{};
        u32 TL{};

        u32 DIPAN{};
        u32 DISDL{};
        u32 IMXL{};

        u32 Q{};

        u32 FLV[5];

        u32 FAR{}, FD1R{};
        u32 FRR{}, FD2R{};
    } regs{};

    struct {
        i16 history{}, current{};
    } sample{};

    void get_next_sample();

    u32 counter{};
};

struct core {
    explicit core(scheduler_t *scheduler_in, u64 *master_clock_in);
    scheduler_t *scheduler{};
    u64 *master_clock{};
    ~core();

    audio_channel channels[64];
    timer timers[3];

    ARM32::core<ARM32::AT_ARM7DI, scheduler_t> cpu;

    u64 cpu_waitstates{};

    template<u8 sz, bool do_debug>
    u32 arm_read(u32 addr, u8 access, bool has_effect) {
        if (addr < 0x1F'FFFF) {
            if (has_effect) cpu_mem_delay(sz);
            if constexpr(sz == 1) return static_cast<u8 *>(RAM)[addr & 0x1F'FFFF];
            if constexpr(sz == 2) return static_cast<u16 *>(RAM)[(addr & 0x1F'FFFF) >> 1];
            if constexpr(sz == 4) return static_cast<u32 *>(RAM)[(addr & 0x1F'FFFF) >> 2];
            if constexpr(sz == 8) return static_cast<u64 *>(RAM)[(addr & 0x1F'FFFF) >> 3];
            NOGOHERE;
        }

        timing.next_cpu_cycle += timing.cpu_divisor;
        u32 r = 0;
        if ((addr >= 0x80'0000) && (addr < 0x80'7FFF)) {
            bool success = true;;
            r = read_reg<sz, do_debug>(addr, &success);
            if (!success) printf("\nIT WAS AICA ARM!");
        }
        else {
            if (addr < 0xFFFFFFE8)
                printf("\nAICA: ARM bad read %08x(%d)", addr, sz);
        }
        return r;
    }

    template<u8 sz, bool do_debug>
    void arm_write(u32 addr, u8 access, u32 val) {
        if ((addr >= 0x0000'0080) && (addr < 0x0000'0084)) {
            printf("\nARM WRITE TO %08x(%d):%08x", addr, sz, val);
        }
        if (addr < 0x1F'FFFF) {
            cpu_mem_delay(sz);
            if constexpr(sz == 1) static_cast<u8 *>(RAM)[addr & 0x1F'FFFF] = val;
            else if constexpr(sz == 2) static_cast<u16 *>(RAM)[(addr & 0x1F'FFFF) >> 1] = val;
            else if constexpr(sz == 4) static_cast<u32 *>(RAM)[(addr & 0x1F'FFFF) >> 2] = val;
            else if constexpr(sz == 8) static_cast<u64 *>(RAM)[(addr & 0x1F'FFFF) >> 3] = val;
            else {
                NOGOHERE;
            }
            return;
        }

        timing.next_cpu_cycle += timing.cpu_divisor;
        if ((addr >= 0x80'0000) && (addr < 0x80'7FFF)) {
            bool success = true;
            write_reg<sz, do_debug>(addr, val, &success);
            if (!success) printf("\nIT WAS AICA ARM!");
            return;
        }
        printf("\nAICA: ARM bad write %08x(%d):%08x", addr, sz, val);
        dbg_break("YO", 0);
    }

    void cpu_mem_delay(u8 sz);

    template<u8 sz, bool do_debug>
    u64 read_reg(u32 addr, bool *success) {
        static constexpr u32 mszmask[9] = { 0, 0xF'FFFF, 0xF'FFFE, 0, 0xF'FFFC, 0, 0, 0, 0xF'FFF8 };
        addr &= mszmask[sz];
        addr |= 0x80'0000;
        u64 v = read_reg8(addr, success);
        if constexpr (sz >= 2) v |= read_reg8(addr+1, success) << 8;
        if constexpr (sz >= 4) {
            v |= read_reg8(addr+2, success) << 16;
            v |= read_reg8(addr+3, success) << 24;
        }
        if constexpr (sz == 8) {
            v |= read_reg8(addr+4, success) << 32;
            v |= read_reg8(addr+5, success) << 40;
            v |= read_reg8(addr+6, success) << 48;
            v |= read_reg8(addr+7, success) << 56;
        }
        if (!*success) {
            printf("\nAICA missed reg read %08x(%d)", addr, sz);
        }
        return v;
    }

    template<u8 sz, bool do_debug>
    void write_reg(u32 addr, u64 val, bool *success) {
        static constexpr u32 mszmask[9] = { 0, 0xF'FFFF, 0xF'FFFE, 0, 0xF'FFFC, 0, 0, 0, 0xF'FFF8 };
        addr &= mszmask[sz];
        addr |= 0x80'0000;
        write_reg8(addr, val & 0xFF, success);
        if constexpr (sz >= 2) write_reg8(addr+1, (val >> 8) & 0xFF, success);
        if constexpr (sz >= 4) {
            write_reg8(addr+2, (val >> 16) & 0xFF, success);
            write_reg8(addr+3, (val >> 24) & 0xFF, success);
        }
        if constexpr (sz == 8) {
            write_reg8(addr+4, (val >> 32) & 0xFF, success);
            write_reg8(addr+5, (val >> 40) & 0xFF, success);
            write_reg8(addr+6, (val >> 48) & 0xFF, success);
            write_reg8(addr+7, (val >> 56) & 0xFF, success);
        }
        if (!*success) {
            printf("\nAICA missed reg write to %08x(%d): %08llx", addr, sz, val);
        }
    }

    u64 read_reg8(u32 addr, bool *success);

    struct {
        u32 cpu_reset_id{};
    } trace{};

    void write_reg8(u32 addr, u16 val, bool *success);

    void schedule_first();
    void try_dma();
    void pause_cpu();
    void unpause_cpu();

    bool cpu_paused{};

    u8 regsmem[0x8000]{};
    struct {
        u32 ARMRST{};

        u32 MVOL{}; // Main volume??
        u32 VER{};
        u32 DAC18B{};
        u32 MEM8MB{};
        u32 MONO{};
        u32 MSLC{}; // Monitor channel #

        union {
            struct {
                u32 EFPAN : 5;
                u32 _ : 3;
                u32 EFSDL : 4;
            };
            u32 u{};
        } DSP_OUT_1[17];

        u32 RBP{};

        u32 DMEA{};
        u32 DGATE{};
        u32 DRGA{};
        u32 DDIR{};
        u32 DEXE{};
        u32 DLG{};

        u32 MRWINH{};
        u32 TSCD{};

        u8 MIBUF{}, MOBUF{};
        u32 TSTB0{};
        u32 RBL{};
        u32 MOFUL{};
        u32 MOEMP{1};
        u32 MIOVF{};
        u32 MIFUL{};
        u32 AFSET{};

        // SCIPD - irq.IF
        // SCIEB - irq.IE
        // SCIRE - no value stored
        u32 SCILV0{0x18}, SCILV1{0x50}, SCILV2{0x08};

        u32 MC_IE{}; // MCIEB
        u32 MC_IF{}; // MCIPD
        u32 RP{};
        // MCIRE - no value stored

        u32 INTREQ{}; // Interrupt request #
    } regs{};
    struct {
        u16 COEF[128];
        u32 MADRS[64];
        u64 MPRO[128];
        u32 TEMP[128];
        u32 MEMS[32];
        u32 MEMS_L[32]; // lower value to latch on write to HI
        u32 MIXS[16];
        u32 EFREG[16];
        u32 EXTS[2];

    } dsp{};

    struct {
        double master_clock_speed{};
        double cpu_speed{};

        double cpu_divisor{};
        double rtc_divisor{};
        double audio_divisor{};
        double ram_16bit_divisor{};
        double transfer_32_bytes{};

        long double last_ram_cycle{};
        long double next_rtc_cycle{};
        long double next_audio_cycle{};
        long double next_cpu_cycle{}; // This is added to during instruction execution...

        double cpu_accumulated_waits{};
        double cpu_surplus{};

        u32 cpu_still_sched{};
        u32 cpu_sched_id{};
    } timing{};
    struct {
        u32 counter{};
        u32 enable_write{};
    } rtc{};

    void reset();
    void *RAM{};

    void cpu_tick();
    void rtc_tick();
    void audio_tick();
    void raise_interrupt(int num);
    void recalc_interrupts_arm();
    void recalc_interrupts_sh4();
    void kyonex();
    u32 old_interrupt_out{};

    void *ext_irq_ptr{};
    void (*ext_irq)(void *ptr);

    struct {
        u32 IE{};//{0x48};
        u32 IF{};
    } irq{};

    DBG_START
    DBG_LOG_VIEW_SIMPLE
    DBG_END

private:
    void schedule_next_cpu_tick();
    void schedule_next_rtc_tick();
    void schedule_next_audio_tick();
    void setRTC_now();
    void deschedule_cpu();
};
}