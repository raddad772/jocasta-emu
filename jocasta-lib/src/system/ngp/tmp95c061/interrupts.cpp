#include "interrupts.h"
#include "component/cpu/tlcs900h/tlcs900h.h"


namespace TMP95C061 {

namespace {
    struct src_def { irq_source src; u8 vector; bool maskable; bool dma_allowed; bool enabled; u8 priority; IRQ_multiplexer_b_kind kind; const char *name; };

    const src_def TABLE[IRQ_SOURCE_COUNT] = {
        { IRQ_NMI, 0x20, false, true, false, 7, IRQMBK_edge_1_to_0, "nmi" },
        { IRQ_INTWD, 0x24, false, false, true, 7, IRQMBK_edge_0_to_1, "intwd" },
        { IRQ_INT0, 0x28, true, true, true, 0, IRQMBK_edge_0_to_1, "int0" },
        { IRQ_INT4, 0x2c, true, true, true, 0, IRQMBK_edge_1_to_0, "int4" },
        { IRQ_INT5, 0x30, true, true, true, 0, IRQMBK_edge_0_to_1, "int5" },
        { IRQ_INT6, 0x34, true, true, true, 0, IRQMBK_edge_0_to_1, "int6" },
        { IRQ_INT7, 0x38, true, true, true, 0, IRQMBK_edge_0_to_1, "int7" },
        { IRQ_INTT0, 0x40, true, true, true, 0, IRQMBK_edge_0_to_1, "intt0" },
        { IRQ_INTT1, 0x44, true, true, true, 0, IRQMBK_edge_0_to_1, "intt1" },
        { IRQ_INTT2, 0x48, true, true, true, 0, IRQMBK_edge_0_to_1, "intt2" },
        { IRQ_INTT3, 0x4c, true, true, true, 0, IRQMBK_edge_0_to_1, "intt3" },
        { IRQ_INTTR4, 0x50, true, true, true, 0, IRQMBK_edge_0_to_1, "inttr4" },
        { IRQ_INTTR5, 0x54, true, true, true, 0, IRQMBK_edge_0_to_1, "inttr5" },
        { IRQ_INTTR6, 0x58, true, true, true, 0, IRQMBK_edge_0_to_1, "inttr6" },
        { IRQ_INTTR7, 0x5c, true, true, true, 0, IRQMBK_edge_0_to_1, "inttr7" },
        { IRQ_INTRX0, 0x60, true, true, true, 0, IRQMBK_edge_0_to_1, "intrx0" },
        { IRQ_INTTX0, 0x64, true, true, true, 0, IRQMBK_edge_0_to_1, "inttx0" },
        { IRQ_INTRX1, 0x68, true, true, true, 0, IRQMBK_edge_0_to_1, "intrx1" },
        { IRQ_INTTX1, 0x6c, true, true, true, 0, IRQMBK_edge_0_to_1, "inttx1" },
        { IRQ_INTAD, 0x70, true, true, true, 0, IRQMBK_edge_0_to_1, "intad" },
        { IRQ_INTTC0, 0x74, true, false, true, 0, IRQMBK_edge_0_to_1, "inttc0" },
        { IRQ_INTTC1, 0x78, true, false, true, 0, IRQMBK_edge_0_to_1, "inttc1" },
        { IRQ_INTTC2, 0x7c, true, false, true, 0, IRQMBK_edge_0_to_1, "inttc2" },
        { IRQ_INTTC3, 0x80, true, false, true, 0, IRQMBK_edge_0_to_1, "inttc3" },
    };

    struct inte_pair { irq_source lo, hi; };
    const inte_pair INTE_PAIRS[] = {
        { IRQ_INT0, IRQ_INTAD },
        { IRQ_INT4, IRQ_INT5 },
        { IRQ_INT6, IRQ_INT7 },
        { IRQ_INTT0, IRQ_INTT1 },
        { IRQ_INTT2, IRQ_INTT3 },
        { IRQ_INTTR4, IRQ_INTTR5 },
        { IRQ_INTTR6, IRQ_INTTR7 },
        { IRQ_INTRX0, IRQ_INTTX0 },
        { IRQ_INTRX1, IRQ_INTTX1 },
        { IRQ_INTTC0, IRQ_INTTC1 },
        { IRQ_INTTC2, IRQ_INTTC3 },
    };
}

INTC::INTC(TLCS900H::core *cpu_in) : mux(IRQ_SOURCE_COUNT)
{
    cpu = cpu_in;
    for (u32 n = 0; n < IRQ_SOURCE_COUNT; n++) {
        vector[n] = TABLE[n].vector;
        maskable[n] = TABLE[n].maskable;
        dma_allowed[n] = TABLE[n].dma_allowed;
        mux.setup_irq(n, TABLE[n].name, TABLE[n].kind);
    }
    cpu->service_irq = &service_trampoline;
    cpu->service_irq_ptr = this;
    reset();
}

void INTC::reset()
{
    mux.reset();
    for (u32 n = 0; n < IRQ_SOURCE_COUNT; n++) {
        priority[n] = TABLE[n].priority;
        enabled[n] = TABLE[n].enabled;
    }
    for (u32 ch = 0; ch < 4; ch++) dma_vector[ch] = 0;
    resolved_vector = resolved_priority = 0;
    int5_line = false;
    int5_armed = false;

    nmi_line = true;
    nmi_pending = false;
    nmi_edge_rising = false;
    nmi_edge_falling = true;

    mux.irqs[IRQ_INT4].input = 1;

}

void INTC::set_line(irq_source src, u32 level)
{
    if (src == IRQ_NMI) {
        if (!enabled[IRQ_NMI]) return;
        const bool v = (level != 0);
        if (v) {
            if (nmi_line) return;
            nmi_line = true;
            if (nmi_pending || !nmi_edge_rising) return;
            nmi_pending = true;
        } else {
            if (!nmi_line) return;
            nmi_line = false;
            if (nmi_pending || !nmi_edge_falling) return;
            nmi_pending = true;
        }
        return;
    }
    if (src == IRQ_INT5) {
        const bool was = int5_line;
        int5_line = (level != 0);
        if (level && !was) { int5_armed = true; mux.set_level(IRQ_INT5, 1); }
        else if (!level) { mux.irqs[IRQ_INT5].input = 0; }
        return;
    }
    mux.set_level(src, level);
}

void INTC::set_nmi_enable(bool e)
{
    enabled[IRQ_NMI] = e;
}

int INTC::source_for_vector(u8 vec) const
{
    for (u32 n = 0; n < IRQ_SOURCE_COUNT; n++) if (vector[n] == vec) return static_cast<i32>(n);
    return -1;
}

int INTC::dma_channel_for_vector(u8 vec) const
{
    for (u32 ch = 0; ch < 4; ch++) if (dma_vector[ch] && dma_vector[ch] == vec) return static_cast<i32>(ch);
    return -1;
}

void INTC::poll()
{
    resolved_priority = 0;
    resolved_vector = 0;
    u8 iff = cpu->regs.SR.IFF;

    if (int5_line && int5_armed && enabled[IRQ_INT5]) {
        mux.irqs[IRQ_INT5].IF = 1;
        mux.IF |= (1ull << IRQ_INT5);
    }

    for (int n = IRQ_SOURCE_COUNT - 1; n >= 0; --n) {
        if (!enabled[n]) continue;
        const bool pending = (n == IRQ_NMI) ? nmi_pending : (((mux.IF >> n) & 1) != 0);
        if (!pending) continue;

        if (!maskable[n]) {
            resolved_priority = priority[n];
            resolved_vector = vector[n];
            continue;
        }
        if (dma_allowed[n] && iff <= 6 && dma_channel_for_vector(vector[n]) >= 0) {
            resolved_priority = 6;
            resolved_vector = vector[n];
            continue;
        }
        if (priority[n] == 0 || priority[n] == 7) continue;
        if (priority[n] >= resolved_priority) {
            resolved_priority = priority[n];
            resolved_vector = vector[n];
        }
    }
}

bool INTC::service()
{
    poll();
    if (!resolved_priority || resolved_priority < cpu->regs.SR.IFF) return false;

    u8 vec = resolved_vector;

    int sn = source_for_vector(vec);
    if (sn == static_cast<i32>(IRQ_NMI)) {
        nmi_pending = false;
    } else if (sn >= 0 && mux.irqs[sn].kind != IRQMBK_flipflop) {
        if (sn == static_cast<i32>(IRQ_INT5)) int5_armed = false;
        mux.mask(~(1ull << sn));
    }

    int ch = dma_channel_for_vector(vec);
    if (ch >= 0) {
        if (cpu->dma(ch)) {
            dma_vector[ch] = 0;
            raise(static_cast<irq_source>(IRQ_INTTC0 + ch));
        }
    } else {
        cpu->interrupt(vec);
        u8 niff = resolved_priority;
        if (niff != 7) niff++;
        cpu->regs.SR.IFF = niff;
    }
    return true;
}

bool INTC::service_trampoline(void *ptr)
{
    return static_cast<INTC *>(ptr)->service();
}

u8 INTC::read_inte(u8 addr)
{
    auto pending = [&](irq_source s) -> u8 { return (mux.IF >> s) & 1; };

    if (addr >= 0x70 && addr <= 0x7a) {
        const inte_pair &p = INTE_PAIRS[addr - 0x70];
        u8 v = 0;
        v |= pending(p.lo) << 3;
        v |= pending(p.hi) << 7;
        return v;
    }
    if (addr == 0x7b) {
        u8 v = 0;
        v |= (nmi_edge_rising ? 1 : 0);
        v |= (mux.irqs[IRQ_INT0].kind == IRQMBK_flipflop ? 1 : 0) << 1;
        v |= (enabled[IRQ_INT0] ? 1 : 0) << 2;
        return v;
    }
    return 0;
}

void INTC::write_inte(u8 addr, u8 data)
{
    auto set_pri = [&](irq_source s, u8 pri) { priority[s] = pri & 7; };
    auto keep = [&](irq_source s, bool keep_pending) { if (!keep_pending) mux.mask(~(1ull << s)); };

    if (addr >= 0x70 && addr <= 0x7a) {
        const inte_pair &p = INTE_PAIRS[addr - 0x70];
        set_pri(p.lo, data & 7);
        keep(p.lo, (data >> 3) & 1);
        set_pri(p.hi, (data >> 4) & 7);
        keep(p.hi, (data >> 7) & 1);
        return;
    }
    if (addr == 0x7b) {
        nmi_edge_rising = (data & 1) != 0;
        mux.set_sensitivity(IRQ_INT0, ((data >> 1) & 1) ? IRQMBK_flipflop : IRQMBK_edge_0_to_1);
        enabled[IRQ_INT0] = (data >> 2) & 1;
        return;
    }
    if (addr >= 0x7c && addr <= 0x7f) {
        dma_vector[addr - 0x7c] = (data & 0x1f) << 2;
        return;
    }
}

}
