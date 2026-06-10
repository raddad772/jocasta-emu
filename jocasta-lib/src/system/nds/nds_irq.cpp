//
// Created by . on 1/20/25.
//

#include "nds_irq.h"
#include "nds_bus.h"
namespace NDS {
template void core::eval_irqs_7<true>();
template void core::eval_irqs_7<false>();
template <bool do_debug>
void core::eval_irqs_7()
{
    arm7.regs.IRQ_line = (!!(io.arm7.IE & io.arm7.IF)) & io.arm7.IME;
    if (arm7_ins) arm7.IRQcheck<do_debug, false, true>();
    else arm7.IRQcheck<do_debug, false, false>();
}

template void core::eval_irqs_9<true>();
template void core::eval_irqs_9<false>();
template <bool do_debug>
void core::eval_irqs_9()
{
    // Bit 21 can't go down while certain conditions are true
    //printf("\neval_irqs_9 IF:%08x IE:%08x IME:%d", io.arm9.IF, io.arm9.IE, io.arm9.IME);
    if (!(io.arm9.IF & (1 << 21)))
        io.arm9.IF |= ge.check_irq() << 21;
    arm9.regs.IRQ_line = (!!(io.arm9.IE & io.arm9.IF)) & io.arm9.IME;
    //printf("\nARM9 IRQ line:%d", arm9.regs.IRQ_line);
    if (arm9_ins) arm9.IRQcheck<do_debug, false, true>();
    else arm9.IRQcheck<do_debug, false, false>();
}

void core::eval_irqs()
{
    /*
0     LCD V-Blank
  1     LCD H-Blank
  2     LCD V-Counter Match
  3     Timer 0 Overflow
  4     Timer 1 Overflow
  5     Timer 2 Overflow
  6     Timer 3 Overflow
  7     NDS7 only: SIO/RCNT/RTC (Real Time Clock)
  8     DMA 0
  9     DMA 1
  10    DMA 2
  11    DMA 3
  12    Keypad
  13    GBA-Slot (external IRQ source) / DSi: None such
  14    Not used                       / DSi9: NDS-Slot Card change?
  15    Not used                       / DSi: dito for 2nd NDS-Slot?
  16    IPC Sync
  17    IPC Send FIFO Empty
  18    IPC Recv FIFO Not Empty
  19    NDS-Slot Game Card Data Transfer Completion
  20    NDS-Slot Game Card IREQ_MC
  21    NDS9 only: Geometry Command FIFO
  22    NDS7 only: Screens unfolding
  23    NDS7 only: SPI bus
  24    NDS7 only: Wifi    / DSi9: XpertTeak DSP
  25    Not used           / DSi9: Camera
  26    Not used           / DSi9: Undoc, IF.26 set on FFh-filling 40021Axh
  27    Not used           / DSi:  Maybe IREQ_MC for 2nd gamecard?
  28    Not used           / DSi: NewDMA0
  29    Not used           / DSi: NewDMA1
  30    Not used           / DSi: NewDMA2
  31    Not used           / DSi: NewDMA3     */
    arm7.regs.IRQ_line = (!!(io.arm7.IE & io.arm7.IF & 0x3FFF)) & io.arm7.IME;
    arm9.regs.IRQ_line = (!!(io.arm9.IE & io.arm9.IF & 0x3FFF)) & io.arm9.IME;
}

template void core::update_IF7<true>(u32);
template void core::update_IF7<false>(u32);
template <bool do_debug>
void core::update_IF7(u32 bitnum)
{
    u32 old_IF = io.arm7.IF;
    io.arm7.IF |= 1 << bitnum;
    if (old_IF != io.arm7.IF) {
        eval_irqs_7<do_debug>();
    }
}

template void core::update_IF9<true>(u32);
template void core::update_IF9<false>(u32);

template <bool do_debug>
void core::update_IF9(u32 bitnum)
{
    u32 old_IF = io.arm9.IF;
    io.arm9.IF |= 1 << bitnum;
    if (old_IF != io.arm9.IF) {
        //printf("\nARM9 IF:%05x  ANY:%05x", io.arm9.IF, io.arm9.IE & io.arm9.IF);
    }
    eval_irqs_9<do_debug>();
}

template void core::update_IFs_card<true>(u32 bitnum);
template void core::update_IFs_card<false>(u32 bitnum);

template <bool do_debug>
void core::update_IFs_card(u32 bitnum)
{
    if (io.rights.nds_slot_is7) update_IF7<do_debug>(bitnum);
    else update_IF9<do_debug>(bitnum);
}


template <bool do_debug>
void core::update_IFs(u32 bitnum)
{
    update_IF7<do_debug>(bitnum);
    update_IF9<do_debug>(bitnum);
}
}