//
// Created by Dave on 2/10/2024.
//

#pragma once

#include <cassert>
#include <cmath>

#include "helpers/int.h"
#include "helpers/debug.h"
#include "helpers/scheduler.h"
#include "helpers/debugger/debuggerdefs.h"
#include "helpers/debugger/debugger.h"
#include "helpers/elf_helpers.h"
#include "sh4_debugger.h"

#include "fsca.h"
#include "tmu.h"
#include "sh4_dmac.h"
#include "sh4_interpreter_opcodes.h"

// One of these for external mem system to write/read registers here,
// One for this to read/write main memory
namespace SH4 {
struct memaccess_t {
    void *ptr{};
    u64 (*read)(void*,u32 addr, u8 sz, bool*success){};
    void (*write)(void*, u32 addr, u8 sz, u64 val, bool*success){};
};


struct FV_t {
    float a{}, b{}, c{}, d{};
};

struct mtx {
    float xf0{}, xf1{}, xf2{}, xf3{};
    float xf4{}, xf5{}, xf6{}, xf7{};
    float xf8{}, xf9{}, xf10{}, xf11{};
    float xf12{}, xf13{}, xf14{}, xf15{};
};

union regs_SR {
    struct {
        u32 T : 1; // 0. True/false or carry/borrow flag
        u32 S : 1; // 1. Saturation for MAC instruction
        u32 _ : 2; // 2-3
        u32 IMASK : 4; // 4-7 interrupt mask bits
        u32 Q : 1; // 8 Used in DIV instructions
        u32 M : 1; // 9 Used in DIV instructions
        u32 _2 : 5; // 10-14
        u32 FD : 1; // 15. FPU disab le
        u32 _3 : 12; // 16-27
        u32 BL : 1; // 28. Exception/interrupt
        u32 RB : 1; // 29. Register bank select!
        u32 MD : 1; // 30. 1= privileged mode
        u32 _4: 1;
    };
    u32 u;
};
union regs_FPSCR {
    struct {
        u32 RM : 2; // 0-1. Rounding mode. 0=Nearest{}, 1=Zero{}, 2=Reserved{}, 3=Reserved
        u32 Flag : 5; // 2-6. exception flag              None{}, Invalid Op(V){}, /0(Z){}, Overflow(O){}, Underflow(U){}, Inexact(I)
        u32 Enable : 5; // 7-11. exception enable
        u32 Cause : 6; // 12-17. exception cause
        u32 DN : 1; // 18. Denormalization mode. 0 = do it{}, 1 = treat it as 0
        u32 PR : 1; // 19. Precision mode. 0 = single precision{}, 1 = double
        u32 SZ : 1; // 20. Size-transfer mode. 0=FMOV is 32-bit{}, 1=FMOV is 64-bit
        u32 FR : 1; // 21. Register bank select
        u32 _ : 10;
    };
    u32 u{};
};

struct REGS {
    void SR_set(u32 val);
    u32 SR_get();
    void FPSCR_set(u32 val);
    void FPSCR_bankswitch();
    void FPSCR_update(u32 old_RM, u32 old_DN);
    [[nodiscard]] u32 FPSCR_get() const;
    u32 R[16]{}; // registers
    u32 R_[8]{}; // shadow registers

    // -- accessed any time
    u32 GBR{}; // Global Bank Register
    regs_SR SR{};

    // -- accessed in privileged
    u32 SSR{}; // Saved Status Register
    u32 SPC{}; // Saved PC
    u32 VBR{}; // Vector Base Register
    u32 SGR{}; // Saved reg15
    u32 DBR{}; // Debug base register

    // System registers{}, any mode
    u32 MACL{}; // MAC-lo
    u32 MACH{}; // MAC-hi
    u32 PR{}; // Procedure register
    u32 PC{}; // Program Counter
    regs_FPSCR FPSCR{}; // Floating Point Status Control Register
    union {
        float f;
        u32 u{};
    } FPUL{};

    union {
        u32 U32[16];
        u64 U64[8]{};
        float FP32[16];
        double FP64[8];
        FV_t FV[4];
        mtx MTX;
    } fb[3]{};

    u32 QACR[2]{};  // 0xFF000038 + 3C

        union {  // PTEH
            struct {
                u32 asid : 8;
                u32 : 2;
                u32 vpn : 22;
            };
            u32 u{};
        } PTEH;  // 0xFF000000
        union {  // PTEL
            struct {
                u32 wt : 1;
                u32 sh : 1;
                u32 d : 1;
                u32 c : 1;
                u32 sz_a : 1;
                u32 pr : 2;
                u32 sz_b : 1;
                u32 v : 1;
                u32 : 1;
                u32 ppn : 19;
            };
            u32 u{};
        } PTEL;  // 0xFF000004
        u32 TTB{};  // 0xFF000008
        u32 TEA{};  // 0xFF00000C
        union {  // MMUCR
            struct {
                u32 at : 1;
                u32 : 1;
                u32 ti : 1;
                u32 : 5;
                u32 sv : 1;
                u32 sqmd : 1;
                u32 urc : 6;
                u32 : 2;
                u32 urb : 6;
                u32 : 2;
                u32 lrui : 6;
            };
            u32 u{};
        } MMUCR;  // 0xFF000010
        u32 CCN_BASRA{};  // 0xFF000014
        u32 CCN_BASRB{};  // 0xFF000018
        union {  // CCR
            struct {
                u32 OCE : 1;
                u32 WT : 1;
                u32 CB : 1;
                u32 OCI : 1;
                u32 : 1;
                u32 ORA : 1;
                u32 : 1;
                u32 OIX : 1;
                u32 ICE : 1;
                u32 : 2;
                u32 ICI : 1;
                u32 : 3;
                u32 IIX : 1;
            };
            u32 u{};
        } CCR;  // 0xFF00001C
        u32 EXPEVT{};  // 0xFF000020
        u32 TRAPA{};  // 0xFF000024
        u32 INTEVT{};  // 0xFF000028
        union {  // PTEA
            struct {
                u32 sa : 3;
                u32 tc : 1;
            };
            u32 u{};
        } PTEA;  // 0xFF000034
        u32 PMCTR1_CTRL{};  // 0xFF000084
        u32 PMCTR2_CTRL{};  // 0xFF000088
        u32 PMCTR1H{};  // 0xFF100004
        u32 PMCTR1L{};  // 0xFF100008
        u32 PMCTR2H{};  // 0xFF10000C
        u32 PMCTR2L{};  // 0xFF100010
        u32 UBC_BARA{};  // 0xFF200000
        u32 UBC_BAMRA{};  // 0xFF200004
        u32 UBC_BBRA{};  // 0xFF200008
        u32 UBC_BARB{};  // 0xFF20000C
        u32 UBC_BAMRB{};  // 0xFF200010
        u32 UBC_BBRB{};  // 0xFF200014
        u32 UBC_BRCR{};  // 0xFF200020
        u32 BSCR{};  // 0xFF800000
        u32 BCR2{};  // 0xFF800004
        u32 WCR1{};  // 0xFF800008
        u32 WCR2{};  // 0xFF80000C
        u32 WCR3{};  // 0xFF800010
        u32 MCR{};  // 0xFF800014
        u32 PCR{};  // 0xFF800018
        u32 RTCSR{};  // 0xFF80001C
        u32 RTCOR{};  // 0xFF800024
        u32 RFCR{};  // 0xFF800028
        u32 PCTRA{};  // 0xFF80002C
        u32 PDTRA{};  // 0xFF800030
        u32 PCTRB{};  // 0xFF800040
        u32 PDTRB{};  // 0xFF800044
        u32 GPIOIC{};  // 0xFF800048
        u32 SDMR{};  // 0xFF940190
        u32 CPG_FRQCR{0x08};  // 0xFFC00000
        u32 STBCR{};  // 0xFFC00004
        u32 CPG_WTCNT{};  // 0xFFC00008
        union {  // CPG_WTCSR
            struct {
                u32 CKS : 3;
                u32 IOVF : 1;
                u32 WOVF : 1;
                u32 RSTS : 1;
                u32 WT_IT : 1;
                u32 TME : 1;
            };
            u32 u{};
        } CPG_WTCSR;  // 0xFFC0000C
        u32 CPG_STBCR2{};  // 0xFFC00010
        u32 RDAYAR{};  // 0xFFC80030
        u32 RMONAR{};  // 0xFFC80034
        union {  // RCR1
            struct {
                u32 AF : 1;
                u32 : 6;
                u32 CF : 1;
            };
            u32 u{};
        } RCR1;  // 0xFFC80038
        union {  // RCR2
            struct {
                u32 : 7;
                u32 PEF : 1;
            };
            u32 u{};
        } RCR2;  // 0xFFC80040
        union {  // ICR
            struct {
                u16 : 7;
                u16 IRLM : 1;
                u16 NMIE : 1;
                u16 NMIB : 1;
                u16 : 4;
                u16 MAI : 1;
                u16 NMIL : 1;
            };
            u16 u{};
        } ICR;  // 0xFFD00000
        union {  // IPRA
            struct {
                u16 RTC : 4;
                u16 TMU2 : 4;
                u16 TMU1 : 4;
                u16 TMU0 : 4;
            };
            u16 u{};
        } IPRA;  // 0xFFD00004
        union {  // IPRB
            struct {
                u16 : 4;
                u16 SCI1 : 4;
                u16 REF : 4;
                u16 WDT : 4;
            };
            u16 u{};
        } IPRB;  // 0xFFD00008
        union {  // IPRC
            struct {
                u32 UDI : 4;
                u32 SCIF : 4;
                u32 DMAC : 4;
                u32 GPIO : 4;
            };
            u32 u{};
        } IPRC;  // 0xFFD0000C
        u32 SCIF_SCSMR2{};  // 0xFFE80000
        u32 SCIF_SCBRR2{};  // 0xFFE80004
        u32 SCIF_SCSCR2{};  // 0xFFE80008
        u32 SCIF_SCFTDR2{};  // 0xFFE8000C
        u32 SCIF_SCFSR2{};  // 0xFFE80010
        u32 SCIF_SCFRDR2{};  // 0xFFE80014
        u32 SCIF_SCFCR2{};  // 0xFFE80018
        u32 SCIF_SCFDR2{};  // 0xFFE8001C
        u32 SCIF_SCSPTR2{};  // 0xFFE80020
        u32 SCIF_SCLSR2{};  // 0xFFE80024
private:
    void swap_register_banks();
};


// Number of SH4 interrupt sources
#define SH4I_NUM 27

enum IRQ_SOURCES {
    IRQ_nmi = 0,
    IRQ_irl,
    IRQ_hitachi_udi,
    IRQ_gpio_gpioi,
    IRQ_dmac_dmte0,
    IRQ_dmac_dmte1,
    IRQ_dmac_dmte2,
    IRQ_dmac_dmte3,
    IRQ_dmac_dmae,
    IRQ_tmu0_tuni0,
    IRQ_tmu1_tuni1,
    IRQ_tmu2_tuni2,
    IRQ_tmu2_ticpi2,
    IRQ_rtc_ati,
    IRQ_rtc_pri,
    IRQ_rtc_cui,
    IRQ_sci1_eri,
    IRQ_sci1_rxi,
    IRQ_sci1_txi,
    IRQ_sci1_tei,
    IRQ_scif_eri,
    IRQ_scif_rxi,
    IRQ_scif_bri,
    IRQ_scif_txi,
    IRQ_wdt_iti,
    IRQ_ref_rcmi,
    IRQ_ref_rovi
};

static constexpr char IRQ_NAMES[27][50] = {
    "00 NMI", "01 IRL", "02 Hitachi UDI", "03 GPIO GPIOI", "04 DMAC dmte0", "05 DMAC dmte1", "06 DMAC dmte2",
    "07 DMAC dmte3", "08 DMAC dmae", "09 TMU0 tuni0", "10 TMU1 tuni1", "11 TMU2 tuni2", "12 TMU2 ticpi2",
    "13 RTC ati", "14 RTC pri", "15 RTC cui", "16 SCI1 eri", "17 SCI1 rxi", "18 SCI1 txi",
    "19 SCI1 tei", "20 SCIF eri", "21 SCIF rxi", "22 SCIF bri", "23 SCIF txi", "24 WDT iti",
    "25 REF rcmi", "26 REF rovi"
};

struct IRQ_SOURCE {
    IRQ_SOURCES source{};
    u32 priority{};
    u32 sub_priority{};
    u32 raised{};
    u32 intevt{};
};

struct ins_t;

struct core {
    explicit core(scheduler_t *scheduler_in, u64 *timer_cycles_in);
    void reset();
    void setup_tracing(jsm_debug_read_trace &rt, u64 *trace_cycles_in);
    REGS regs{};

    elf_symbol_list32 *elfs{};

    i32 cycles{};

    i32 pp_last_m{};
    i32 pp_last_n{};
    u8 SQ[2][32]{}; // store queues!
    u8 OC[8 * 1024]{}; // Operand Cache!

    struct {
        jsm_string str{100};
        jsm_string str2{100};
        bool ok{};
        u64 *cycles{};
        u64 my_cycles{};

        u32 exception_id{};
        u32 ins_id{};
        u32 irq_id{};
        u32 regread_id{};
        u32 regwrite_id{};
    } trace{};

    jsm_debug_read_trace read_trace{};

    IRQ_SOURCE interrupt_sources[SH4I_NUM]{};
    IRQ_SOURCE* interrupt_map[SH4I_NUM]{};
    u32 interrupt_highest_priority{}; // used to compare to IMASK
    TMU tmu;
    DMAC dmac;
    void trace_format(ins_t *ins);

    void *mptr{};
    u32 (*fetch_ins)(void* ptr,u32 addr){};
    u64 (*read8)(void* ptr,u32 addr){};
    u64 (*read16)(void* ptr,u32 addr){};
    u64 (*read32)(void* ptr,u32 addr){};
    u64 (*read64)(void* ptr,u32 addr){};
    void (*write8)(void*,u32 addr, u64 val){};
    void (*write16)(void*,u32 addr, u64 val){};
    void (*write32)(void*,u32 addr, u64 val){};
    void (*write64)(void*,u32 addr, u64 val){};
    u32 (*fetch_ins_debug)(void* ptr,u32 addr){};
    u64 (*read8_debug)(void* ptr,u32 addr){};
    u64 (*read16_debug)(void* ptr,u32 addr){};
    u64 (*read32_debug)(void* ptr,u32 addr){};
    u64 (*read64_debug)(void* ptr,u32 addr){};
    void (*write8_debug)(void*,u32 addr, u64 val){};
    void (*write16_debug)(void*,u32 addr, u64 val){};
    void (*write32_debug)(void*,u32 addr, u64 val){};
    void (*write64_debug)(void*,u32 addr, u64 val){};

    jsm_string console{5000};

    scheduler_t* scheduler{};
    template<bool do_debug>
    void run_cycles(i32 howmany) {
        // fetch
        cycles += static_cast<i32>(howmany);
        while(cycles > 0) {
            if constexpr (do_debug) {
                if (::dbg.do_break) {
                    cycles = 0;
                    break;
                }
            }
            fetch_and_exec<do_debug>(false);
            //if constexpr(do_debug) {
            if constexpr (false) {
                if (elfs) {
                    auto *e = elfs->find(regs.PC, 0xFFFFFFFF);
                    if (e) {
                        printf("\nELF SYMBOL %s %d %s @%08x", e->fname, e->kind, e->name, regs.PC);
                    }
                }
            }
            if ((regs.SR.BL == 0) && (interrupt_highest_priority > regs.SR.IMASK)) {
                interrupt();
            }
        }
    }

    void set_IRL(u32 level);
    void interrupt_pend(IRQ_SOURCES source, u32 onoff);
    u64 ma_read(u32 addr, u8 sz, bool* success);
    void ma_write(u32 addr, u8 sz, u64 val, bool* success);
    void give_memaccess(memaccess_t* to);

    DBG_START
        console_view *console{};
        DBG_LOG_VIEW_SIMPLE
    DBG_END


private:
    template<u8 sz, bool do_debug>
    u64 mem_read(u32 addr) {
        bool s = true;
        if (((addr & 0x1FFF'FFFF) >= 0x1C00'0000) || (addr >= 0xE0000000)) {
            if ((addr & 0xF000'0000) == 0x1000'0000 || (addr & 0xF000'0000) == 0xF000'0000) {
                if ((addr >= 0xFC00'0000) && (!regs.SR.MD)) {
                    // TODO: exception
                    printf("\nWARNING USER ACCESS %08x", addr);
                }
                u64 v = ma_read(addr, sz, &s);
                if constexpr(do_debug) {
                    if (dbg.dvptr && dbg.dvptr->ids_enabled[trace.regread_id]) {
                        sh4dbgloglog(trace.regread_id, DBGLS_TRACE, "RD REG %08x(%d): %lld", addr, s, v);
                    }
                }
                if (!s) {
                    printf("\nSH4 missed register read %08x(%d)", addr, sz);
                }
                return v;
            }
            if ((addr >= 0x7C000000) && (addr <= 0x7FFFFFFF)) {
                if (regs.CCR.OIX == 0)
                    addr = ((addr & 0x2000) >> 1) | (addr & 0xFFF);
                else
                    addr = ((addr & 0x02000000) >> 13) | (addr & 0xFFF);
                if constexpr(sz == 1) return static_cast<u8 *>(OC)[addr];
                if constexpr(sz == 2) return reinterpret_cast<u16 *>(OC)[addr >> 1];
                if constexpr(sz == 4) return reinterpret_cast<u32 *>(OC)[addr >> 2];
                if constexpr(sz == 8) return reinterpret_cast<u64 *>(OC)[addr >> 3];
                NOGOHERE;
            }
            // Operand Cache address array
            if ((addr >= 0xF4000000) && (addr < 0xF5000000)) {
                return 0;
            }
        }
        if constexpr(do_debug) {
            if constexpr(sz == 1) return read8_debug(mptr, addr);
            if constexpr(sz == 2) return read16_debug(mptr, addr);
            if constexpr(sz == 4) return read32_debug(mptr, addr);
            if constexpr(sz == 8) return read64_debug(mptr, addr);
        }
        else {
            if constexpr(sz == 1) return read8(mptr, addr);
            if constexpr(sz == 2) return read16(mptr, addr);
            if constexpr(sz == 4) return read32(mptr, addr);
            if constexpr(sz == 8) return read64(mptr, addr);
        }
        NOGOHERE;
    }

    template<u8 sz, bool do_debug>
    void mem_write(u32 addr, u64 val) {
        if (((addr & 0x1FFF'FFFF) >= 0x1C00'0000) || (addr >= 0xE0000000)) {
            if ((addr & 0xF000'0000) == 0x1000'0000 || (addr & 0xF000'0000) == 0xF000'0000) {
                if ((addr >= 0xFC000000) && (!regs.SR.MD)) {
                    // TODO: exception
                    printf("\nWARNING USER ACCESS %08x", addr);
                }
                bool s = true;
                ma_write(addr, sz, val, &s);
                if (dbg.dvptr && dbg.dvptr->ids_enabled[trace.regwrite_id]) {
                    sh4dbgloglog(trace.regwrite_id, DBGLS_TRACE, "WR REG %08x(%d): %lld", addr, s, val);
                }
                if (!s) {
                    printf("\nSH4 missed register write %08x(%d)", addr, sz);
                }
                return;
            }
            if ((addr >= 0x7C000000) && (addr <= 0x7FFFFFFF)) {
                if (regs.CCR.OIX == 0)
                    addr = ((addr & 0x2000) >> 1) | (addr & 0xFFF);
                else
                    addr = ((addr & 0x02000000) >> 13) | (addr & 0xFFF);
                if constexpr(sz == 1) static_cast<u8 *>(OC)[addr] = val;
                if constexpr(sz == 2) reinterpret_cast<u16 *>(OC)[addr >> 1] = val;
                if constexpr(sz == 4) reinterpret_cast<u32 *>(OC)[addr >> 2] = val;
                if constexpr(sz == 8) reinterpret_cast<u64 *>(OC)[addr >> 3] = val;
                return;
            }
            if ((addr >= 0xF4000000) && (addr < 0xF5000000)) {
                return;
            }
            if ((addr >= 0xE0000000) && (addr <= 0xE3FFFFFF)) { // store queue write
                u32 sqnum = (addr >> 5) & 1;
                addr &= 0x1F;
                assert(sz>=4);
                if constexpr(sz == 4) reinterpret_cast<u32 *>(SQ[sqnum])[addr >> 2] = val;
                else if constexpr(sz == 8) reinterpret_cast<u64 *>(SQ[sqnum])[addr >> 3] = val;
                else NOGOHERE;
                return;
            }

        }
        if constexpr(do_debug) {
            if constexpr(sz == 1) return write8_debug(mptr, addr, val);
            if constexpr(sz == 2) return write16_debug(mptr, addr, val);
            if constexpr(sz == 4) return write32_debug(mptr, addr, val);
            if constexpr(sz == 8) return write64_debug(mptr, addr, val);
        }
        else {
            if constexpr(sz == 1) return write8(mptr, addr, val);
            if constexpr(sz == 2) return write16(mptr, addr, val);
            if constexpr(sz == 4) return write32(mptr, addr, val);
            if constexpr(sz == 8) return write64(mptr, addr, val);
        }
        NOGOHERE;
    }

    void console_add(u32 val, u8 sz);
    void set_QACR(u32 num, u32 val);
    void lycoder_print(u32 opcode);
    void pprint(ins_t *ins, bool last_traces);
    void exec_interrupt();
    void IRQ_set_highest_priority();
    template<bool do_debug>
    void fetch_and_exec(bool is_delay_slot)
    {
        u32 opcode = fetch_ins(mptr, regs.PC);

        *tmu.master_cycles += 1;
        cycles -= 1;

        ins_t *ins = &decoded[do_debug][SH4_decoded_index][opcode];

        if constexpr(do_debug)
            trace_format(ins);
        (this->*ins->exec)(ins);
    }
    void interrupt();
    void sort_interrupts();
    void IPR_update();
    void do_sh4_decode();
    void set_FRQCR(u32 val);

#define ins(x) template<bool do_debug>void ins_##x(ins_t *ins)
    ins(EMPTY);
    ins(MOV);
    ins(MOVI);
    ins(MOVA);
    ins(MOVWI);
    ins(MOVLI);
    ins(MOVBL);
    ins(MOVWL);
    ins(MOVLL);
    ins(MOVBS);
    ins(MOVWS);
    ins(MOVLS);
    ins(MOVBP);
    ins(MOVWP);
    ins(MOVLP);
    ins(MOVBM);
    ins(MOVWM);
    ins(MOVLM);
    ins(MOVBL4);
    ins(MOVWL4);
    ins(MOVLL4);
    ins(MOVBS4);
    ins(MOVWS4);
    ins(MOVLS4);
    ins(MOVBL0);
    ins(MOVWL0);
    ins(MOVLL0);
    ins(MOVBS0);
    ins(MOVWS0);
    ins(MOVLS0);
    ins(MOVBLG);
    ins(MOVWLG);
    ins(MOVLLG);
    ins(MOVBSG);
    ins(MOVWSG);
    ins(MOVLSG);
    ins(MOVT);
    ins(SWAPB);
    ins(SWAPW);
    ins(XTRCT);
    ins(ADD);
    ins(ADDI);
    ins(ADDC);
    ins(ADDV);
    ins(CMPIM);
    ins(CMPEQ);
    ins(CMPHS);
    ins(CMPGE);
    ins(CMPHI);
    ins(CMPGT);
    ins(CMPPL);
    ins(CMPPZ);
    ins(CMPSTR);
    ins(DIV0S);
    ins(DIV0U);
    ins(DIV1);
    ins(DMULS);
    ins(DMULU);
    ins(DT);
    ins(EXTSB);
    ins(EXTSW);
    ins(EXTUB);
    ins(EXTUW);
    ins(MACL);
    ins(MACW);
    ins(MULL);
    ins(MULS);
    ins(MULU);
    ins(NEG);
    ins(NEGC);
    ins(SUB);
    ins(SUBC);
    ins(SUBV);
    ins(AND);
    ins(ANDI);
    ins(ANDM);
    ins(NOT);
    ins(OR);
    ins(ORI);
    ins(ORM);
    ins(TAS);
    ins(TST);
    ins(TSTI);
    ins(TSTM);
    ins(XOR);
    ins(XORI);
    ins(XORM);
    ins(ROTCL);
    ins(ROTCR);
    ins(ROTL);
    ins(ROTR);
    ins(SHAD);
    ins(SHAL);
    ins(SHAR);
    ins(SHLD);
    ins(SHLL);
    ins(SHLL2);
    ins(SHLL8);
    ins(SHLL16);
    ins(SHLR);
    ins(SHLR2);
    ins(SHLR8);
    ins(SHLR16);
    ins(BF);
    ins(BFS);
    ins(BT);
    ins(BTS);
    ins(BRA);
    ins(BRAF);
    ins(BSR);
    ins(BSRF);
    ins(JMP);
    ins(JSR);
    ins(RTS);
    ins(CLRMAC);
    ins(CLRS);
    ins(CLRT);
    ins(LDCSR);
    ins(LDCMSR);
    ins(LDCGBR);
    ins(LDCMGBR);
    ins(LDCVBR);
    ins(LDCMVBR);
    ins(LDCSSR);
    ins(LDCMSSR);
    ins(LDCSPC);
    ins(LDCMSPC);
    ins(LDCDBR);
    ins(LDCMDBR);
    ins(LDCRn_BANK);
    ins(LDCMRn_BANK);
    ins(LDSMACH);
    ins(LDSMMACH);
    ins(LDSMACL);
    ins(LDSMMACL);
    ins(LDSPR);
    ins(LDSMPR);
    ins(LDTLB);
    ins(MOVCAL);
    ins(NOP);
    ins(OCBI);
    ins(OCBP);
    ins(OCBWB);
    ins(PREF);
    ins(RTE);
    ins(SETS);
    ins(SETT);
    ins(SLEEP);
    ins(STCSR);
    ins(STCMSR);
    ins(STCGBR);
    ins(STCMGBR);
    ins(STCVBR);
    ins(STCMVBR);
    ins(STCSGR);
    ins(STCMSGR);
    ins(STCSSR);
    ins(STCMSSR);
    ins(STCSPC);
    ins(STCMSPC);
    ins(STCDBR);
    ins(STCMDBR);
    ins(STCRm_BANK);
    ins(STCMRm_BANK);
    ins(STSMACH);
    ins(STSMMACH);
    ins(STSMACL);
    ins(STSMMACL);
    ins(STSPR);
    ins(STSMPR);
    ins(TRAPA);
    ins(FMOV);
    ins(FMOV_LOAD);
    ins(FMOV_STORE);
    ins(FMOV_RESTORE);
    ins(FMOV_SAVE);
    ins(FMOV_INDEX_LOAD);
    ins(FMOV_INDEX_STORE);
    ins(FMOV_DR);
    ins(FMOV_DRXD);
    ins(FMOV_XDDR);
    ins(FMOV_XDXD);
    ins(FMOV_LOAD_DR);
    ins(FMOV_LOAD_XD);
    ins(FMOV_STORE_DR);
    ins(FMOV_STORE_XD);
    ins(FMOV_RESTORE_DR);
    ins(FMOV_RESTORE_XD);
    ins(FMOV_SAVE_DR);
    ins(FMOV_SAVE_XD);
    ins(FMOV_INDEX_LOAD_DR);
    ins(FMOV_INDEX_LOAD_XD);
    ins(FMOV_INDEX_STORE_DR);
    ins(FMOV_INDEX_STORE_XD);
    ins(FLDI0);
    ins(FLDI1);
    ins(FLDS);
    ins(FSTS);
    ins(FABS);
    ins(FNEG);
    ins(FADD);
    ins(FSUB);
    ins(FMUL);
    ins(FMAC);
    ins(FDIV);
    ins(FSQRT);
    ins(FCMP_EQ);
    ins(FCMP_GT);
    ins(FLOAT_single);
    ins(FTRC_single);
    ins(FIPR);
    ins(FTRV);
    ins(FABSDR);
    ins(FNEGDR);
    ins(FADDDR);
    ins(FSUBDR);
    ins(FMULDR);
    ins(FDIVDR);
    ins(FSQRTDR);
    ins(FCMP_EQDR);
    ins(FCMP_GTDR);
    ins(FLOAT_double);
    ins(FTRC_double);
    ins(FCNVDS);
    ins(FCNVSD);
    ins(LDSFPSCR);
    ins(STSFPSCR);
    ins(LDSMFPSCR);
    ins(STSMFPSCR);
    ins(LDSFPUL);
    ins(STSFPUL);
    ins(LDSMFPUL);
    ins(STSMFPUL);
    ins(FRCHG);
    ins(FSCHG);
    ins(FSRRA);
    ins(FSCA);
#undef ins
};
#ifndef SH4_NOIMPL
#include "sh4_interpreter_opcodes_impl.h"
#endif

}