#pragma once

#include "helpers/int.h"
#include "helpers/better_irq_multiplexer.h"

namespace TLCS900H { struct core; }

namespace TMP95C061 {

enum irq_source {
    IRQ_NMI = 0,
    IRQ_INTWD,
    IRQ_INT0,
    IRQ_INT4, IRQ_INT5, IRQ_INT6, IRQ_INT7,
    IRQ_INTT0, IRQ_INTT1, IRQ_INTT2, IRQ_INTT3,
    IRQ_INTTR4, IRQ_INTTR5, IRQ_INTTR6, IRQ_INTTR7,
    IRQ_INTRX0, IRQ_INTTX0, IRQ_INTRX1, IRQ_INTTX1,
    IRQ_INTAD,
    IRQ_INTTC0, IRQ_INTTC1, IRQ_INTTC2, IRQ_INTTC3,
    IRQ_SOURCE_COUNT
};

struct INTC {
    explicit INTC(TLCS900H::core *cpu_in);
    void reset();

    void set_line(irq_source src, u32 level);
    void raise(irq_source src) { mux.trigger(static_cast<u32>(src)); }
    void set_nmi_enable(bool e);

    u8 read_inte(u8 addr);
    void write_inte(u8 addr, u8 data);

    static bool service_trampoline(void *ptr);
    bool service();

    IRQ_multiplexer_b mux;
    TLCS900H::core *cpu{};

    u8 vector[IRQ_SOURCE_COUNT]{};
    u8 priority[IRQ_SOURCE_COUNT]{};
    bool maskable[IRQ_SOURCE_COUNT]{};
    bool dma_allowed[IRQ_SOURCE_COUNT]{};
    bool enabled[IRQ_SOURCE_COUNT]{};
    u8 dma_vector[4]{};

    bool int5_line{};
    bool int5_armed{};

    bool nmi_line{true};
    bool nmi_pending{};
    bool nmi_edge_rising{};
    bool nmi_edge_falling{true};

    u8 resolved_vector{};
    u8 resolved_priority{};
    void poll();

private:
    int source_for_vector(u8 vector) const;
    int dma_channel_for_vector(u8 vector) const;
};

}
