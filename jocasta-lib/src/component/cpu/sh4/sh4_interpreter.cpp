//
// Created by Dave on 2/10/2024.
//
#include <cstring>
#include <cassert>
#include <cstdio>

#include "helpers/debugger/debugger.h"
#include "sh4_interpreter.h"
#include "sh4_interpreter_opcodes.h"
#include "fsca.h"
#include "sh4_debugger.h"
#include "tmu.h"
//#include "system/dreamcast/dreamcast.h"
#include "sh4_interrupts.h"

// NOLINTNEXTLINE(bugprone-suspicious-include)
#include "helpers/multisize_memaccess.cpp"

//#define BRK 0x8c002774


// Endianness is little.

// disassembly printf args
#define SH_DISA_P_ARGS "\ncyc:%05d  %08x %s   ", *trace.cycles, regs.PC, disassembled[decoded_index][opcode]


#define CLOCK_DIVIDER 1
#define TIMER_MULTIPLIER (8)
namespace SH4 {

void REGS::SR_set(u32 val) {
    val &= 0x700083F3;
    u32 old_RB = SR.RB;

    SR.u = val;
    if(SR.MD) {
        if (old_RB != SR.RB) {
            swap_register_banks();
        }
    }
    else {
        if (SR.RB)
        {
            //printf("\nUpdateSR MD=0;RB=1 , this must noUpdateSR MD=0;RB=1 , this must not happen!t happen!"); // reicast says
            SR.RB = 0;//error - must always be 0 if not privileged
        }
        if (old_RB)
            swap_register_banks();
    }

    /*if (!regs.SR.MD) should_be = 0;
    else should_be = regs.SR.RB;
    if (should_be != regs.currently_banked_rb) {
#ifdef IRQ_DBG
        dbg_LT_printf(DBGC_RED "\nSWAP REGISTER BANKS cur:%d shouldbe:%d R0:%08x R0_BANKEd:%08x PC:%08X cyc:%llu" DBGC_RST, regs.currently_banked_rb, should_be, regs.R[0], regs.R_[0], regs.PC, clock.trace_cycles);
        printf(DBGC_RED "\nSWAP REGISTER BANKS cur:%d shouldbe:%d R0:%08x R0_BANKEd:%08x PC:%08X cyc:%llu" DBGC_RST, regs.currently_banked_rb, should_be, regs.R[0], regs.R_[0], regs.PC, clock.trace_cycles);
#endif
        regs.currently_banked_rb = should_be;
        swap_register_banks();
    }*/
}

u32 REGS::SR_get() {
    return SR.u;
}

void REGS::FPSCR_update(u32 old_RM, u32 old_DN)
{
    // 0=nearest, 1=zero, 2 & 3 = reserved
    /*assert(RM < 2);
    switch(RM) {
        case 0: // nearest
            fesetround(FE_TONEAREST);
            break;
        case 1: // to zero
            fesetround(FE_TOWARDZERO);
            break;
        case 2:
        case 3:
            printf("\nInvalid FPSCR RM %d", RM);
            break;
    }*/
    // Thanks to Reicast for this
/*    if ((old_RM!=RM) || (old_dn!=fpscr.DN))
    {
        old_rm=fpscr.RM ;
        old_dn=fpscr.DN ;*/

        //Correct rounding is required by some games (SOTB, etc)
#if BUILD_COMPILER == COMPILER_VC
        if (FPSCR.RM == 1)  //if round to 0 , set the flag
            _controlfp(_RC_CHOP, _MCW_RC);
        else
            _controlfp(_RC_NEAR, _MCW_RC);

        if (FPSCR.DN)     //denormals are considered 0
            _controlfp(_DN_FLUSH, _MCW_DN);
        else
            _controlfp(_DN_SAVE, _MCW_DN);
#else

        #if HOST_CPU==CPU_X86 || HOST_CPU==CPU_X64

            u32 temp=0x1f80;	//no flush to zero && round to nearest

			if (FPSCR.RM==1)  //if round to 0 , set the flag
				temp|=(3<<13);

			if (FPSCR.DN)     //denormals are considered 0
				temp|=(1<<15);
			asm("ldmxcsr %0" : : "m"(temp));
    #elif HOST_CPU==CPU_ARM
		static const unsigned int x = 0x04086060;
		unsigned int y = 0x02000000;
		if (FPSCR.RM==1)  //if round to 0 , set the flag
			y|=3<<22;

		if (FPSCR.DN)
			y|=1<<24;


		int raa;

		asm volatile
			(
				"fmrx   %0, fpscr   \n\t"
				"and    %0, %0, %1  \n\t"
				"orr    %0, %0, %2  \n\t"
				"fmxr   fpscr, %0   \n\t"
				: "=r"(raa)
				: "r"(x), "r"(y)
			);
	#elif HOST_CPU == CPU_ARM64
		static const unsigned long off_mask = 0x04080000;
        unsigned long on_mask = 0x02000000;    // DN=1 Any operation involving one or more NaNs returns the Default NaN

        if (FPSCR.RM == 1)		// if round to 0, set the flag
        	on_mask |= 3 << 22;

        if (FPSCR.DN)
        	on_mask |= 1 << 24;	// flush denormalized numbers to zero

        asm volatile
            (
                "MRS    x10, FPCR     \n\t"
                "AND    x10, x10, %0  \n\t"
                "ORR    x10, x10, %1  \n\t"
                "MSR    FPCR, x10     \n\t"
                :
                : "r"(off_mask), "r"(on_mask)
            );
    #else
		#if !defined(TARGET_EMSCRIPTEN)
			printf("SetFloatStatusReg: Unsupported platform\n");
		#endif
	#endif
#endif
    //}
}


u32 REGS::FPSCR_get() const
{
    return FPSCR.u;
}

void REGS::FPSCR_bankswitch()
{
    memcpy(&fb[2], &fb[0], 64);
    memcpy(&fb[0], &fb[1], 64);
    memcpy(&fb[1], &fb[2], 64);
}

void REGS::FPSCR_set(u32 val) {
    // If floating-point select register changed
    if (FPSCR.FR != ((val >> 21) & 1)) {
        FPSCR_bankswitch();
    }
    u32 old_RM = FPSCR.RM;
    u32 old_DN = FPSCR.DN;
    FPSCR.u = val;
    // Make sure correct FP rounding mode
    FPSCR_update(old_RM, old_DN);
}

void core::trace_format(ins_t *ins) {
    bool do_dbglog = false;
    if (dbg.dvptr) {
        do_dbglog = dbg.dvptr->ids_enabled[dbg.dv_id];
    }
    if (do_dbglog) {
        trace.str.quickempty();
        dbglog_view *dv = dbg.dvptr;
        dv->add_printf(dbg.dv_id, *trace.cycles, DBGLS_TRACE, "%08x  %s", regs.PC, disassembled[SH4_decoded_index][ins->opcode]);
        bool spaces_needed = false;
        if (ins->Rn != -1) {
            spaces_needed = true;
            trace.str.sprintf("R%02d:%08x", ins->Rn, regs.R[ins->Rn]);
        }
        if (ins->Rm != -1) {
            if (spaces_needed) trace.str.sprintf("  ");
            spaces_needed = true;
            trace.str.sprintf("R%02d:%08x", ins->Rm, regs.R[ins->Rm]);
        }
        if (spaces_needed) trace.str.sprintf("  ");
        trace.str.sprintf("IMASK:%x  HIGHEST:%x  T:%d", regs.SR.IMASK, interrupt_highest_priority, regs.SR.T);
        dv->extra_printf("%s", trace.str.ptr);
    }
}

void core::pprint(ins_t *ins, bool last_traces)
{
    u32 had_any = 0;
    i32 last_n = -1;
    i32 last_m = -1;
    if (!last_traces)
        dbg_seek_in_line(45);
    else
        dbg_LT_seek_in_line(45);
    if (ins->Rn != -1) {
        if (!last_traces)
            dbg_printf("R%d:%08x", ins->Rn, regs.R[ins->Rn]);
        else
            dbg_LT_printf("R%d:%08x", ins->Rn, regs.R[ins->Rn]);

        had_any = 1;
        last_n = static_cast<i32>(ins->Rn);
    }
    if (ins->Rm != -1) {
        if (had_any) {
            if (!last_traces)
                dbg_printf(" ");
            else
                dbg_LT_printf(" ");
        }
        had_any = 1;
        if (!last_traces)
            dbg_printf("R%d:%08x", ins->Rm, regs.R[ins->Rm]);
        else
            dbg_LT_printf("R%d:%08x", ins->Rm, regs.R[ins->Rm]);
        last_m = (i32)ins->Rm;
    }
    if ((pp_last_m != -1) && (pp_last_m != ins->Rm) && (pp_last_m != ins->Rn)) {
        if (had_any) {
            if (!last_traces)
                dbg_printf(" ");
            else
                dbg_LT_printf(" ");
        }
        had_any = 1;
        if (!last_traces)
            dbg_printf("R%d:%08x", pp_last_m, regs.R[pp_last_m]);
        else
            dbg_LT_printf("R%d:%08x", pp_last_m, regs.R[pp_last_m]);
    }
    if ((pp_last_n != -1) && (pp_last_n != ins->Rm) && (pp_last_n != ins->Rn)) {
        if (had_any) {
            if (!last_traces)
                dbg_printf(" ");
            else
                dbg_LT_printf(" ");
        }
        had_any = 1;
        if (!last_traces)
            dbg_printf("R%d:%08x", pp_last_n, regs.R[pp_last_n]);
        else
            dbg_LT_printf("R%d:%08x", pp_last_n, regs.R[pp_last_n]);
    }
    if ((ins->Rn != 0) && (ins->Rm != 0) && (pp_last_m != 0) && (pp_last_n != 0)) {
        if (had_any) {
            if (!last_traces)
                dbg_printf(" ");
            else
                dbg_LT_printf(" ");
        }
        if (!last_traces)
            dbg_printf(" R0:%08x", regs.R[0]);
        else
            dbg_LT_printf("R0:%08x", regs.R[0]);
    }

    if (!last_traces)
        dbg_printf(" IMASK:%x HIGHEST:%x T:%d", regs.SR.IMASK, interrupt_highest_priority, regs.SR.T);
    else
        dbg_LT_printf(" IMASK:%x HIGHEST:%x T:%d", regs.SR.IMASK, interrupt_highest_priority, regs.SR.T);


    pp_last_m = last_m;
    pp_last_n = last_n;
}

void core::lycoder_print(u32 opcode)
{
    if (::dbg.do_debug)
        dbg_printf("\n%08x %04x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x",
                   regs.PC, opcode,
                   regs.R[0], regs.R[1], regs.R[2],
                   regs.R[3], regs.R[4], regs.R[5],
                   regs.R[6], regs.R[7], regs.R[8],
                   regs.R[9], regs.R[10], regs.R[11],
                   regs.R[12], regs.R[13], regs.R[14],
                   regs.R[15], regs.SR_get(), regs.FPSCR_get());
}

core::core(scheduler_t *scheduler_in, u64 *timer_cycles_in) :
    tmu(this, timer_cycles_in),
    dmac(this),
    scheduler(scheduler_in)
    {
    trace.my_cycles = 0;
    trace.ok = false;
    trace.cycles = &trace.my_cycles;
    pp_last_m = pp_last_n = -1;
    do_sh4_decode();
    init_interrupt_struct(interrupt_sources, interrupt_map);
    interrupt_highest_priority = 0;
    reset();
    //printf("\nINS! %s\n", disassembled[0][0x6772]);

#ifndef TEST_SH4
    //tmu.reset();
#endif
}

void REGS::swap_register_banks()
{
    //printf(DBGC_RED "\nBANK SWAP RB:%d" DBGC_RST, SR.RB);
    for (u32 i = 0; i < 8; i++) {
        u32 t = R[i];
        R[i] = R_[i];
        R_[i] = t;
    }
}


void core::reset()
{
    /* for SR,
     * MD bit = 1, RB bit = 1, BL bit = 1, FD bit = 0,
I3ñI0 = 1111 (H'F), reserved bits = 0, others
undefined
     */
    /*
     EXPEVT = H'00000000;
VBR = H'00000000;
SR.MD = 1;
SR.RB = 1;
SR.BL = 1;
SR.(I0-I3) = B'1111;
SR.FD=0;
Initialize_CPU();
Initialize_Module(PowerOn);
PC = H'A0000000;*
     */
    tmu.reset();
    dmac.reset();
    regs.VBR = 0;
    regs.PC = 0xA0000000;
    regs.GBR = 0x8c000000;
    regs.FPSCR_set(0x00004001);
    regs.SR_set((regs.SR_get() &  0b11110011) | 0b01110000000000000000000011110000);
    set_FRQCR(8);
}

u64 core::ma_read(u32 addr, u8 sz, bool* success) {
    u32 full_addr = addr;
    u32 up_addr = addr | 0xE0000000;
    addr &= 0x1FFFFFFF;

    if ((up_addr >= 0xFFD80000) && (up_addr <= 0xFFD8002C)) {
        return tmu.read(full_addr, sz, success);
    }

    // OC address array
    if ((up_addr >= 0xF4000000) && (up_addr <= 0xF4FFFFFF)) {
        return 0;
    }

    if ((up_addr >= 0xFFA0'0000) && (up_addr <= 0xFFA0'FFFF)) {
        return dmac.read(full_addr, sz, success);
    }

    switch (addr | 0xE0000000) {
// NOLINTNEXTLINE(bugprone-suspicious-include)
        case 0xFF000000:  { return regs.PTEH.u; }
        case 0xFF000004:  { return regs.PTEL.u; }
        case 0xFF000008:  { return regs.TTB; }
        case 0xFF00000C:  { return regs.TEA; }
        case 0xFF000010:  { return regs.MMUCR.u; }
        case 0xFF000014:  { return regs.CCN_BASRA; }
        case 0xFF000018:  { return regs.CCN_BASRB; }
        case 0xFF00001C:  { return regs.CCR.u; }
        case 0xFF000020:  { return regs.EXPEVT; }
        case 0xFF000024:  { return regs.TRAPA; }
        case 0xFF000028:  { return regs.INTEVT; }
        case 0xFF000034:  { return regs.PTEA.u; }
        case 0xFF000038:  { return regs.QACR[0]; }
        case 0xFF00003C:  { return regs.QACR[1]; }
        case 0xFF000084:  { return regs.PMCTR1_CTRL; }
        case 0xFF000088:  { return regs.PMCTR2_CTRL; }
        case 0xFF100004: { return (*tmu.master_cycles >> 32) & 0xFFFF; }
        case 0xFF100008: { return *tmu.master_cycles & 0xFFFFFFFF; }
        case 0xFF10000C: { return (*tmu.master_cycles >> 32) & 0xFFFF; }
        case 0xFF100010: { return *tmu.master_cycles & 0xFFFFFFFF; }
        case 0xFF200000:  { return regs.UBC_BARA; }
        case 0xFF200004:  { return regs.UBC_BAMRA; }
        case 0xFF200008:  { return regs.UBC_BBRA; }
        case 0xFF20000C:  { return regs.UBC_BARB; }
        case 0xFF200010:  { return regs.UBC_BAMRB; }
        case 0xFF200014:  { return regs.UBC_BBRB; }
        case 0xFF200020:  { return regs.UBC_BRCR; }
        case 0xFF800000:  { return regs.BSCR; }
        case 0xFF800004:  { return regs.BCR2; }
        case 0xFF800008:  { return regs.WCR1; }
        case 0xFF80000C:  { return regs.WCR2; }
        case 0xFF800010:  { return regs.WCR3; }
        case 0xFF800014:  { return regs.MCR; }
        case 0xFF800018:  { return regs.PCR; }
        case 0xFF80001C:  { return regs.RTCSR; }
        case 0xFF800024:  { return regs.RTCOR; }
        case 0xFF800028: { return 0xa400; }
        case 0xFF80002C:  { return regs.PCTRA; }
        case 0xFF800040:  { return regs.PCTRB; }
        case 0xFF800044:  { return regs.PDTRB; }
        case 0xFF800048:  { return regs.GPIOIC; }
        case 0xFF940190:  { return regs.SDMR; }
        case 0xFFC00004:  { return regs.STBCR | 0x00000003; }
        case 0xFFC0000C:  { return regs.CPG_WTCSR.u | 0x0000A500; }
        case 0xFFC80034:  { return regs.RMONAR; }
        case 0xFFC80038:  { return regs.RCR1.u | 0x00000000; }
        case 0xFFC80040:  { return regs.RCR2.u | 0x00000000; }
        case 0xFFD00000:  { return regs.ICR.u; }
        case 0xFFD00004:  { return regs.IPRA.u; }
        case 0xFFD00008:  { return regs.IPRB.u; }
        case 0xFFD0000C:  { return regs.IPRC.u; }
        case 0xFFE80000:  { return regs.SCIF_SCSMR2; }
        case 0xFFE80004:  { return regs.SCIF_SCBRR2; }
        case 0xFFE80008:  { return regs.SCIF_SCSCR2; }
        case 0xFFE8000C:  { return regs.SCIF_SCFTDR2; }
        case 0xFFE80010: { if (sz==2) return 0x60; return regs.SCIF_SCFSR2; }
        case 0xFFE80014:  { return regs.SCIF_SCFRDR2; }
        case 0xFFE80018:  { return regs.SCIF_SCFCR2; }
        case 0xFFE8001C:  { return regs.SCIF_SCFDR2; }
        case 0xFFE80020:  { return regs.SCIF_SCSPTR2; }
        case 0xFFE80024:  { return regs.SCIF_SCLSR2; }        case 0xFF000030: // Undocumented CPU_VERSION
        return 0x040205c1;
        case 0xFF800030: { // PDTRA
            assert(sz==2);
            // PDTRA from Bus Control
            // Note: I got it from Deecy...
            // Note: I have absolutely no idea what's going on here.
            //       This is directly taken from Flycast, kind already got it from Chankast.
            //       This is needed for the bios to work properly, without it, it will
            //       go to sleep mode with all interrupts disabled early on.
            // Note future: Apparently this is using GPIO pins, 2 of which are tied together?
            u32 tpctra = regs.PCTRA;
            u32 tpdtra = regs.PDTRA;

            u16 tfinal = 0;
            if ((tpctra & 0xf) == 0x8) {
                tfinal = 3;
            } else if ((tpctra & 0xf) == 0xB) {
                tfinal = 3;
            } else {
                tfinal = 0;
            }

            if (((tpctra & 0xf) == 0xB) && ((tpdtra & 0xf) == 2)) {
                tfinal = 0;
            } else if (((tpctra & 0xf) == 0xC) && ((tpdtra & 0xf) == 2)) {
                tfinal = 3;
            }

            tfinal |= 0; // 0=VGA, 2=RGB, 3=composite //@intFromEnum(self._dc.?.cable_type) << 8;

            return 0x300 | tfinal;
        }
    }

    *success = false;
    return 0;
}

void core::set_QACR(u32 num, u32 val)
{
    regs.QACR[num] = val & 0x1C;
}

void core::console_add(u32 val, u8 sz)
{
    auto add_char = [this](u8 ch) {
        if (ch == '\r') {
            return;
        }

        if (ch == '\n' || (console.cur - console.ptr) >= (console.allocated_len - 1)) {
            printf("\n(CONSOLE) %s", console.ptr);
            console.quickempty();
        }
        else {
            console.sprintf("%c", ch);
        }
    };

    if (sz == 1) {
        add_char(val & 0xFF);
    }
    else if (sz == 2) {
        add_char(val & 0xFF);
        add_char((val >> 8) & 0xFF);
    }
    else if (sz == 4) {
        add_char(val & 0xFF);
        add_char((val >> 8) & 0xFF);
        add_char((val >> 16) & 0xFF);
        add_char((val >> 24) & 0xFF);
    }
}

void core::ma_write(u32 addr, u8 sz, u64 val, bool* success) {
    // 1F000000-1FFFFFFF also mirrors this in correct memory config
    u32 full_addr = addr;
    u32 up_addr = addr | 0xF0000000;
    addr &= 0x1FFFFFFF;

    if ((up_addr >= 0xFFA0'0000) && (up_addr <= 0xFFA0'FFFF)) {
        dmac.write(full_addr, sz, val, success);
        return;
    }
    if ((up_addr >= 0xFFD80000) && (up_addr <= 0xFFD8002C)) {
        tmu.write(full_addr, sz, val, success);
        return;
    }

    // OC address array
    if ((up_addr >= 0xF4000000) && (up_addr <= 0xF4FFFFFF)) {
        return;
    }
    if ((up_addr >= 0xF0000000) && (up_addr <= 0xF0FFFFFF)) { // ICACHE array
        return;
    }

    switch(up_addr) {
        case 0xFF000038: { set_QACR(0, val); return; }
        case 0xFF00003C: { set_QACR(1, val); return; }
        case 0xFF000000: { regs.PTEH.u = val & 0xFFFFFCFF; return; }
        case 0xFF000004: { regs.PTEL.u = val & 0x1FFFFDFF; return; }
        case 0xFF000008: { regs.TTB = val; return; }
        case 0xFF00000C: { regs.TEA = val; return; }
        case 0xFF000010: { regs.MMUCR.u = val & 0xFCFCFF05; return; }
        case 0xFF000014: { regs.CCN_BASRA = val; return; }
        case 0xFF000018: { regs.CCN_BASRB = val; return; }
        case 0xFF00001C: { regs.CCR.u = val & 0x000089AF; return; }
        case 0xFF000020: { regs.EXPEVT = (val & 0xFFF); return; }
        case 0xFF000024: { regs.TRAPA = (val & 0xFF); return; }
        case 0xFF000028: { regs.INTEVT = (val & 0xFFFF); return; }
        case 0xFF000034: { regs.PTEA.u = val & 0x0000000F; return; }
        case 0xFF000084: { regs.PMCTR1_CTRL = val; printf("\nPerformance counters not implemented..."); return; }
        case 0xFF000088: { regs.PMCTR2_CTRL = val; printf("\nPerformance counters not implemented..."); return; }
        case 0xFF200000: { regs.UBC_BARA = val; return; }
        case 0xFF200004: { regs.UBC_BAMRA = val; return; }
        case 0xFF200008: { regs.UBC_BBRA = val; return; }
        case 0xFF20000C: { regs.UBC_BARB = val; return; }
        case 0xFF200010: { regs.UBC_BAMRB = val; return; }
        case 0xFF200014: { regs.UBC_BBRB = val; return; }
        case 0xFF200020: { regs.UBC_BRCR = val; return; }
        case 0xFF800000: { regs.BSCR = val; return; }
        case 0xFF800004: { regs.BCR2 = val; return; }
        case 0xFF800008: { regs.WCR1 = val; return; }
        case 0xFF80000C: { regs.WCR2 = val; return; }
        case 0xFF800010: { regs.WCR3 = val; return; }
        case 0xFF800014: { regs.MCR = val; return; }
        case 0xFF800018: { regs.PCR = val; return; }
        case 0xFF80001C: { regs.RTCSR = val; return; }
        case 0xFF800024: { regs.RTCOR = val; return; }
        case 0xFF800028: { regs.RFCR = 0b1010010000000000 | (val & 0x1FF); return; }
        case 0xFF80002C: { regs.PCTRA = (val & 0xFFFF); return; }
        case 0xFF800030: { regs.PDTRA = val; return; }
        case 0xFF800040: { regs.PCTRB = val; return; }
        case 0xFF800044: { regs.PDTRB = val; return; }
        case 0xFF800048: { regs.GPIOIC = val; return; }
        case 0xFF940190: { regs.SDMR = val; return; }
        case 0xFFC00000: { set_FRQCR(val); return; }
        case 0xFFC00004: { regs.STBCR = (val) | 0x00000003; return; }
        case 0xFFC00008: { regs.CPG_WTCNT = val; return; }
        case 0xFFC0000C: { regs.CPG_WTCSR.u = (val & 0x0000007F) | 0x0000A500; return; }
        case 0xFFC00010: { regs.CPG_STBCR2 = val; return; }
        case 0xFFC80030: { regs.RDAYAR = val; return; }
        case 0xFFC80034: { regs.RMONAR = val; return; }
        case 0xFFC80038: { regs.RCR1.u = (val & 0x00000081) | 0x00000000; return; }
        case 0xFFC80040: { regs.RCR2.u = (val & 0x00000080) | 0x00000000; return; }
        case 0xFFD00000: { regs.ICR.u = val & 0x0000C380; IPR_update(); return; }
        case 0xFFD00004: { regs.IPRA.u = val & 0x0000FFFF; IPR_update(); return; }
        case 0xFFD00008: { regs.IPRB.u = val & 0x0000FFF0; IPR_update(); return; }
        case 0xFFD0000C: { regs.IPRC.u = val & 0x0000FFFF; IPR_update(); return; }
        case 0xFFE80000: { regs.SCIF_SCSMR2 = val; return; }
        case 0xFFE80004: { regs.SCIF_SCBRR2 = val; return; }
        case 0xFFE80008: { regs.SCIF_SCSCR2 = val; return; }
        case 0xFFE8000C: { regs.SCIF_SCFTDR2 = val; console_add(val, sz); return; }
        case 0xFFE80010: { regs.SCIF_SCFSR2 = val; return; }
        case 0xFFE80014: { regs.SCIF_SCFRDR2 = val; return; }
        case 0xFFE80018: { regs.SCIF_SCFCR2 = val; return; }
        case 0xFFE8001C: { regs.SCIF_SCFDR2 = val; return; }
        case 0xFFE80020: { regs.SCIF_SCSPTR2 = val; return; }
        case 0xFFE80024: { regs.SCIF_SCLSR2 = val; return; }
    }

    *success = false;
}

void core::setup_tracing(jsm_debug_read_trace &rt, u64 *trace_cycles_in)
{
    trace.ok = true;
    jsm_copy_read_trace(&read_trace, &rt);
    trace.cycles = trace_cycles_in;
}

void core::set_FRQCR(u32 val) {
    regs.CPG_FRQCR = val;
    if (false) {
        printf("\nFRQCR: %08x", regs.CPG_FRQCR);
        u32 pc_shift = 0;

#define CC(val, shift) case val: pc_shift = shift; break;

        switch (val & 0xF) {
            CC(0x8, 1);
            CC(0xA, 2);
            CC(0xC, 3);
            default:
            pc_shift = 1;
            printf("\nPERIPARH CLOCK DIV BAD VALUE %03x", val & 0xFFF);
        }
#undef CC
        tmu.pclock_shift = pc_shift;
    }
}

/* syscalls - https://mc.pp.se/dc/syscalls.html
 * Crazy Taxi
 * instructions - http://shared-ptr.com/sh_insns.html
 * originaldave_ — Today at 6:28 PM
how do you HLE the functions, is there documentation available?
[6:28 PM]
did you disassemble them and figure them out
[6:28 PM]
I kinda have this idea, that Dreamcast is, with the exception of a few emulators, not very well-documente

Senryoku — Today at 6:30 PM
Basically the bios loads two files "IP.bin" at 0x8000 and "1STREAD.BIN" (this one can have different names depending on the game) at 0x10000 and then jumps to, uuh, 0x8300 I think? I'd have to check that
[6:30 PM]
So I do that manually


 */
}
