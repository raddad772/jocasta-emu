//
// Created by . on 2/26/25.
//

#pragma once

#include "helpers/int.h"

#define MAX_IRQS_MULTIPLEXED 32

enum IRQ_multiplexer_b_kind {
    IRQMBK_flipflop, // just set to whatever level
    IRQMBK_edge_0_to_1, // triggered on 0->1
    IRQMBK_edge_1_to_0 // triggered on 1->0
};

struct IRQ_multiplexer_b {
    explicit IRQ_multiplexer_b(u64 max_irq) : max_irq(max_irq) {}
    void set_level(u32 num, u32 new_level);
    // Directly latch a one-shot request (a peripheral "completion" pulse), independent of the
    // line level/edge state -- mirrors ares' Interrupt::trigger(). Used by timers/ADC/WDT, which
    // pulse their IRQ each time they fire rather than holding a level/edge line.
    void trigger(u32 num);
    void reset();
    void mask(u64 val);
    void setup_irq(u32 num, const char *name, IRQ_multiplexer_b_kind kind);
    // Re-configure a line's edge/level sensitivity at runtime (e.g. a TMP95C061 INTE
    // register flipping an external pin between level- and edge-sensing). The line's
    // latched request is cleared so the new mode starts from a clean state.
    void set_sensitivity(u32 num, IRQ_multiplexer_b_kind kind);
    u64 IF{};
    u64 max_irq{};
    u64 *clock{};

    struct IRQ_multiplexer_b_irq {
        u32 input{};
        u64 IF{}; // output signal
        u32 kind{};
        char name[50]{};
    } irqs[MAX_IRQS_MULTIPLEXED]{};
};


