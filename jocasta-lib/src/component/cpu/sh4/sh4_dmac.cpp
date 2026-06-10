//
// Created by . on 4/2/26.
//

#include <cstdio>

#include "sh4_dmac.h"
#include "sh4_interpreter.h"

#include "helpers/int.h"

namespace SH4 {
DMAC::DMAC(core *parent) : cpu(parent) {
    for (u32 i = 0; i < 4; i++) {
        auto & c = channels[i];
        c.dmac = this;
        c.num = i;
    }
}

bool CHANNEL::can_transfer() {
    return dmac->DMAOR.DME && CHCR.DE;
}

void CHANNEL::update_IRQs() {
    dmac->cpu->interrupt_pend(static_cast<IRQ_SOURCES>(IRQ_dmac_dmte0 + num), CHCR.TE && CHCR.IE);
}

void CHANNEL::end_transfer() {
    // DMAC_CHCR(2).TE = 1;
    // DMAC_DMATCR(2) = 0;
    DMATCR = 0;
    CHCR.TE = 1;
    update_IRQs();
}

void DMAC::reset() {
    // TODO: this?
}

void DMAC::write(u32 addr, u8 sz, u64 val, bool *success)
{
    addr |= 0xF0000000;
    switch (addr) {
        case 0xFFA00000: { channels[0].SAR = val; return; }
        case 0xFFA00004: { channels[0].DAR = val; return; }
        case 0xFFA00008: { channels[0].DMATCR = val; return; }
        case 0xFFA0000C: { channels[0].CHCR.u = (val & 0x0000FF77) | 0x00000000;
            if ((!DMAOR.DDT) && (val & 1))
                printf("\nWARN CHCHR0 POTENTIAL TRIGGER!?");
            channels[0].update_IRQs();
            return; }
        case 0xFFA00010: { channels[1].SAR = val; return; }
        case 0xFFA00014: { channels[1].DAR = val; return; }
        case 0xFFA00018: { channels[1].DMATCR = val; return; }
        case 0xFFA0001C: { channels[1].CHCR.u = (val & 0x0000FF77) | 0x00000000;
            if ((!DMAOR.DDT) && (val & 1))
                printf("\nWARN CHCHR1 POTENTIAL TRIGGER!?");
            channels[1].update_IRQs();
            return; }
        case 0xFFA00020: { channels[2].SAR = val; return; }
        case 0xFFA00024: { channels[2].DAR = val; return; }
        case 0xFFA00028: { channels[2].DMATCR = val; return; }
        case 0xFFA0002C: { channels[2].CHCR.u = (val & 0x00003007) | 0x000042C0;
            if ((!DMAOR.DDT) && (val & 1))
                printf("\nWARN CHCHR2 POTENTIAL TRIGGER!?");
            channels[2].update_IRQs();
            return; }
        case 0xFFA00030: { channels[3].SAR = val; return; }
        case 0xFFA00034: { channels[3].DAR = val; return; }
        case 0xFFA00038: { channels[3].DMATCR = val; return; }
        case 0xFFA0003C: { channels[3].CHCR.u = (val & 0x0000FF77) | 0x00000000;
            if ((!DMAOR.DDT) && (val & 1))
                printf("\nWARN CHCHR3 POTENTIAL TRIGGER!?");
            channels[3].update_IRQs();
            return; }
        case 0xFFA00040: { DMAOR.u = (val & 0x00000006) | 0x00008201; return; }
    }
    *success = false;
}

u64 DMAC::read(u32 addr, u8 sz, bool *success)
{
    addr |= 0xF0000000;
    switch (addr) {
        case 0xFFA00000:  { return channels[0].SAR; }
        case 0xFFA00004:  { return channels[0].DAR; }
        case 0xFFA00008:  { return channels[0].DMATCR; }
        case 0xFFA0000C:  { return channels[0].CHCR.u | 0x00000000; }
        case 0xFFA00010:  { return channels[1].SAR; }
        case 0xFFA00014:  { return channels[1].DAR; }
        case 0xFFA00018:  { return channels[1].DMATCR; }
        case 0xFFA0001C:  { return channels[1].CHCR.u | 0x00000000; }
        case 0xFFA00020:  { return channels[2].SAR; }
        case 0xFFA00024:  { return channels[2].DAR; }
        case 0xFFA00028:  { return channels[2].DMATCR; }
        case 0xFFA0002C:  { return channels[2].CHCR.u | 0x000042C0; }
        case 0xFFA00030:  { return channels[3].SAR; }
        case 0xFFA00034:  { return channels[3].DAR; }
        case 0xFFA00038:  { return channels[3].DMATCR; }
        case 0xFFA0003C:  { return channels[3].CHCR.u | 0x00000000; }
        case 0xFFA00040:  { return DMAOR.u; }
    }
    *success = false;
    return 0;
}
}
