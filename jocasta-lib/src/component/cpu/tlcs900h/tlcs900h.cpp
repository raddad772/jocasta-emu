#include "tlcs900h.h"
#include "helpers/debug.h"
#include "helpers/debugger/debugger.h"
#include "helpers/serialize/serialize.h"
#include "tlcs900h_disassembler.h"

namespace TLCS900H {

core::core(scheduler_t *scheduler_in, u64 master_clock_freq, u32 divider)
    : scheduler(scheduler_in),
    clock_div(static_cast<double>(divider)), clocks_per_second(master_clock_freq)
    {
    clock = &local_clock;
    DBG_TRACE_VIEW_INIT;
}

void core::setup_tracing(jsm_debug_read_trace *strct, u64 *trace_cycle_ptr, i32 source_id)
{
    trace.strct.ptr = this;
    trace.strct.read_trace = strct->read_trace;
    trace.ok = 1;
    trace.cycles = trace_cycle_ptr;
    trace.source_id = source_id;
}

void core::trace_format()
{
    u32 do_dbglog = 0;
    if (dbg.dvptr) {
        do_dbglog = dbg.dvptr->ids_enabled[dbg.dv_id];
    }
    if (do_dbglog) {
        trace.str.quickempty();
        trace.str2.quickempty();
        u32 pc = regs.PC;
        disassemble(*this, pc, trace.strct, trace.str);

        trace.str2.sprintf(
            "PC:%06x  XWA0:%08x  XBC0:%08x  XDE0:%08x  XHL0:%08x  XIX:%08x  XIY:%08x  XIZ:%08x  XSP:%08x  "
            "SR:%04x  F_:%02x  INTNEST:%04x  RFP:%d  IFF:%d",
            regs.PC,
            regs.R[XWA0].dw, regs.R[XBC0].dw, regs.R[XDE0].dw, regs.R[XHL0].dw,
            regs.R[XIX].dw, regs.R[XIY].dw, regs.R[XIZ].dw, regs.R[XSP].dw,
            regs.SR.u, regs.F_, regs.INTNEST,
            static_cast<i32>(regs.SR.RFP), static_cast<i32>(regs.SR.IFF)
        );

        u64 tc = trace.cycles ? *trace.cycles : 0;
        dbglog_view *dv = dbg.dvptr;
        dv->add_printf(dbg.dv_id, tc, DBGLS_TRACE, "%06x  %s", regs.PC, trace.str.ptr);
        dv->extra_printf("%s", trace.str2.ptr);
    }
}

void core::step(u32 clocks) {
    next_cycle += static_cast<double>(clocks) * clock_div;
    *clock = static_cast<u64>(next_cycle);
}

void core::schedule_next() {
    // Register both instantiations; run_til_tag_tg16<do_debug> selects func[do_debug] at dispatch.
    sched_id = scheduler->only_add_abs(static_cast<i64>(next_cycle), 0, this, &internal_cycle<false>, &internal_cycle<true>, &sched_still);
}

void core::schedule_first(u64 start) {
    next_cycle = static_cast<double>(start);
    *clock = start;
    schedule_next();
}

template<bool do_debug>
void core::internal_cycle(void *ptr, u64 key, u64 clock_now, u32 jitter) {
    auto &th = *static_cast<core *>(ptr);
    if (th.service_irq && th.service_irq(th.service_irq_ptr)) {
        th.halted = false;
        th.schedule_next();
        return;
    }
    if (th.halted) {
        i64 tc = th.scheduler->next_event_timecode();
        if (tc >= 0) {
            th.next_cycle = static_cast<double>(tc);
            *th.clock = static_cast<u64>(tc);
            th.sched_id = th.scheduler->add_after_first_event(&th, &internal_cycle<false>, &internal_cycle<true>, &th.sched_still);
        } else {
            th.step(16);
            th.schedule_next();
        }
        return;
    }
    th.decode_and_exec<do_debug>();
    th.schedule_next();
}

template<bool do_debug>
void core::do_interrupt(u8 vector) {
    if constexpr (do_debug) {
        if (dbg.dvptr && dbg.irq_id && dbg.dvptr->ids_enabled[dbg.irq_id])
            dbg.dvptr->add_printf(dbg.irq_id, trace.cycles ? *trace.cycles : 0, DBGLS_TRACE,
                                  "IRQ  vec=%02X  (from PC=%06X)", vector, regs.PC);
    }
    prefetch<do_debug>(34);
    push<do_debug>(4, regs.PC);
    push<do_debug>(2, load_SR());
    regs.PC = load<do_debug>(op_mem(0xFFFF00 | vector, 4));
    regs.INTNEST++;
    invalidate();
}

template<bool do_debug>
bool core::do_dma(u8 channel) {
    u32 source = regs.dmas[channel].dw;
    u32 target = regs.dmad[channel].dw;
    u16 length = regs.dmam[channel].w[0];
    u16 config = regs.dmam[channel].w[1];

    u8 mode = (config >> 2) & 7;
    u8 sz = (config & 3) == 0 ? 1 : (config & 3) == 1 ? 2 : 4;

    prefetch<do_debug>(6);
    if (mode <= 4) {
        step(2);
        u32 data = bus_read<do_debug>(sz, source);
        step(4);
        bus_write<do_debug>(sz, target, data);
    } else {
        step(4);
    }

    switch (mode) {
        case 0: target += sz; break;
        case 1: target -= sz; break;
        case 2: source += sz; break;
        case 3: source -= sz; break;
        case 4: break;
        case 5: source += 1; break;
        case 6: break;
        case 7: break;
    }

    regs.dmas[channel].dw = source;
    regs.dmad[channel].dw = target;
    length = static_cast<u16>((length - 1));
    regs.dmam[channel].w[0] = length;
    return length == 0;
}

void core::interrupt(u8 vector) {
    if (::dbg.do_debug) do_interrupt<true>(vector);
    else do_interrupt<false>(vector);
}

bool core::dma(u8 channel) {
    return ::dbg.do_debug ? do_dma<true>(channel) : do_dma<false>(channel);
}

u32 core::width(u32 ) const {
    return 2;
}

u32 core::speed(u32 sz, u32 ) const {
    return sz == 1 ? 2 : 4;
}

template<bool do_debug>
u32 core::bus_read(u8 sz, u32 addr) {
    addr &= 0xFFFFFF;
    MAR = addr;
    u32 v;
    if constexpr (do_debug) {
        switch (sz) {
            case 1: v = read8_debug(mem_ptr, addr); break;
            case 2: v = read16_debug(mem_ptr, addr); break;
            case 4: v = read32_debug(mem_ptr, addr); break;
            nodefault;
        }
    } else {
        switch (sz) {
            case 1: v = read8(mem_ptr, addr); break;
            case 2: v = read16(mem_ptr, addr); break;
            case 4: v = read32(mem_ptr, addr); break;
            nodefault;
        }
    }
    MDR = v;
    return v;
}

template<bool do_debug>
void core::bus_write(u8 sz, u32 addr, u32 val) {
    addr &= 0xFFFFFF;
    MAR = addr;
    MDR = val;
    if constexpr (do_debug) {
        switch (sz) {
            case 1: write8_debug(mem_ptr, addr, val); return;
            case 2: write16_debug(mem_ptr, addr, val); return;
            case 4: write32_debug(mem_ptr, addr, val); return;
            nodefault;
        }
    } else {
        switch (sz) {
            case 1: write8(mem_ptr, addr, val); return;
            case 2: write16(mem_ptr, addr, val); return;
            case 4: write32(mem_ptr, addr, val); return;
            nodefault;
        }
    }
}

void core::invalidate() {
    if (PIC) {
        step(PIC);
        PIC = 0;
    }
    PIQ_size = 0;
}

template<bool do_debug>
void core::prefetch(u32 clocks) {
    PIC += clocks;
    while (PIC) {
        if (PIQ_size >= 3) break;
        u32 address = regs.PC + PIQ_size;
        u32 sz = width(address);
        u32 wait = speed(sz, address);
        if (wait > PIC) break;
        PIC -= wait;
        if (sz == 1 || (address & 1)) {
            PIQ[PIQ_size++] = bus_read<do_debug>(1, address);
        } else {
            u16 w = bus_read<do_debug>(2, address);
            PIQ[PIQ_size++] = w;
            PIQ[PIQ_size++] = w >> 8;
        }
    }
}

template<bool do_debug>
u8 core::fetch8() {
    prefetch<do_debug>(0);
    if (PIQ_size == 0) {
        u32 address = regs.PC;
        if (width(address) == 1 || (address & 1)) {
            PIQ[PIQ_size++] = bus_read<do_debug>(1, address);
        } else {
            u16 w = bus_read<do_debug>(2, address);
            PIQ[PIQ_size++] = w;
            PIQ[PIQ_size++] = w >> 8;
        }
    }
    u8 b = PIQ[0];
    PIQ[0] = PIQ[1];
    PIQ[1] = PIQ[2];
    PIQ[2] = PIQ[3];
    PIQ_size--;
    regs.PC++;
    return b;
}

template<bool do_debug>
u16 core::fetch16() {
    u8 d0 = fetch8<do_debug>();
    u8 d1 = fetch8<do_debug>();
    return d0 | (d1 << 8);
}

template<bool do_debug>
u32 core::fetch24() {
    u8 d0 = fetch8<do_debug>();
    u8 d1 = fetch8<do_debug>();
    u8 d2 = fetch8<do_debug>();
    return d0 | (d1 << 8) | (d2 << 16);
}

template<bool do_debug>
u32 core::fetch32() {
    u8 d0 = fetch8<do_debug>();
    u8 d1 = fetch8<do_debug>();
    u8 d2 = fetch8<do_debug>();
    u8 d3 = fetch8<do_debug>();
    return d0 | (d1 << 8) | (d2 << 16) | (d3 << 24);
}

template<bool do_debug>
u32 core::fetch_imm(u8 sz) {
    switch (sz) {
        case 1: return fetch8<do_debug>();
        case 2: return fetch16<do_debug>();
        case 4: return fetch32<do_debug>();
        nodefault;
    }
}

template<bool do_debug>
u32 core::load(const operand &op) {
    switch (op.kind) {
        case OPK_REG:
            if (!regs.valid_id(op.id)) return 0;
            switch (op.size) {
                case 1: return regs.reg8(op.id);
                case 2: return regs.reg16(op.id);
                case 4: return regs.reg32(op.id);
                nodefault;
            }
        case OPK_MEM:
            if (PIC) { step(PIC); PIC = 0; }
            return bus_read<do_debug>(op.size, op.addr);
        case OPK_IMM:
            return op.val;
        case OPK_CR:
            return cr_load(op.id, op.size);
        nodefault;
    }
}

template<bool do_debug>
void core::store(const operand &op, u32 val) {
    switch (op.kind) {
        case OPK_REG:
            if (!regs.valid_id(op.id)) return;
            switch (op.size) {
                case 1: regs.set_reg8(op.id, val); return;
                case 2: regs.set_reg16(op.id, val); return;
                case 4: regs.set_reg32(op.id, val); return;
                nodefault;
            }
        case OPK_MEM:
            if (PIC) { step(PIC); PIC = 0; }
            bus_write<do_debug>(op.size, op.addr, val);
            return;
        case OPK_CR:
            cr_store(op.id, op.size, val);
            return;
        nodefault;
    }
}

static RSPLIT *cr_ptr(REGS &regs, u8 id) {
    if (id < 0x10) return &regs.dmas[id >> 2];
    if (id < 0x20) return &regs.dmad[(id >> 2) & 3];
    if (id < 0x30) return &regs.dmam[(id >> 2) & 3];
    return nullptr;
}

u32 core::cr_load(u8 id, u8 sz) {
    if ((id & ~1) == 0x3C) {
        switch (sz) {
            case 1: return (regs.INTNEST >> ((id & 1) * 8)) & 0xFF;
            case 2: return regs.INTNEST & 0xFFFF;
            default: return regs.INTNEST;
        }
    }
    if (sz == 4 && (id & 2)) return 0;
    RSPLIT *p = cr_ptr(regs, id);
    if (!p) return 0;
    switch (sz) {
        case 1: return p->b[id & 3];
        case 2: return p->w[(id >> 1) & 1];
        default: return p->dw;
    }
}

void core::cr_store(u8 id, u8 sz, u32 val) {
    if ((id & ~1) == 0x3C) {
        if (sz == 1) {
            u32 shift = (id & 1) * 8;
            regs.INTNEST = (regs.INTNEST & ~(0xFFu << shift)) | ((val & 0xFF) << shift);
        } else if (sz == 2) {
            regs.INTNEST = (regs.INTNEST & 0xFFFF0000) | (val & 0xFFFF);
        } else {
            regs.INTNEST = val & 0xFFFF;
        }
        return;
    }
    if (sz == 4 && (id & 2)) return;
    RSPLIT *p = cr_ptr(regs, id);
    if (!p) return;
    switch (sz) {
        case 1: p->b[id & 3] = val; return;
        case 2: p->w[(id >> 1) & 1] = val; return;
        default: p->dw = val; return;
    }
}

template<bool do_debug>
void core::push(u8 sz, u32 val) {
    regs.R[XSP].dw -= sz;
    bus_write<do_debug>(sz, regs.R[XSP].dw, val);
}

template<bool do_debug>
u32 core::pop(u8 sz) {
    u32 v = bus_read<do_debug>(sz, regs.R[XSP].dw);
    regs.R[XSP].dw += sz;
    return v;
}

u8 core::load_F() const {
    return regs.SR.F & 0xD7;
}

void core::store_F(u8 data) {
    regs.SR.F = data & 0xD7;
}

u16 core::load_SR() const {
    return load_F()
         | (regs.SR.RFP << 8)
         | (1u << 11)
         | (regs.SR.IFF << 12)
         | (1u << 15);
}

void core::store_SR(u16 data) {
    store_F(data & 0xFF);
    regs.SR.RFP = (data >> 8) & 3;
    regs.SR.IFF = (data >> 12) & 7;
}

template<bool do_debug>
void core::decode_and_exec() {
    if (halted) return;
    if constexpr (do_debug) trace_format();
    decode<do_debug>();
}

void core::reset() {
    for (auto &r : regs.R) r.dw = 0;
    regs.IR.dw = 0;
    regs.SR.u = 0;
    regs.SR.SYSM = 1;
    regs.SR.MAX = 1;
    regs.SR.IFF = 7;
    regs.SR.RFP = 0;
    regs.F_ = 0;
    regs.INTNEST = 0;
    halted = false;
    PIQ_size = 0;
    PIC = 0;
    OP = 0;
    regs.R[XSP].dw = 0x100;
    regs.PC = bus_read<false>(2, VECTOR_ADDR) | (bus_read<false>(1, VECTOR_ADDR + 2) << 16);
    invalidate();
}

void core::power() {
    *clock = 0;
    next_cycle = 0;
    reset();
}

#define INST(rt, name, ...) \
    template rt core::name<false>(__VA_ARGS__); \
    template rt core::name<true>(__VA_ARGS__);
INST(u32, bus_read, u8, u32)
INST(void, bus_write, u8, u32, u32)
INST(void, prefetch, u32)
INST(u8, fetch8)
INST(u16, fetch16)
INST(u32, fetch24)
INST(u32, fetch32)
INST(u32, fetch_imm, u8)
INST(u32, load, const operand &)
INST(void, store, const operand &, u32)
INST(void, push, u8, u32)
INST(u32, pop, u8)
INST(void, decode_and_exec)
INST(void, do_interrupt, u8)
INST(bool, do_dma, u8)
#undef INST

#define S(x) Sadd(state, &(x), sizeof(x))
void core::serialize(serialized_state &state)
{
    for (auto &r : regs.R) S(r.dw);
    S(regs.IR.dw);
    S(regs.INTNEST);
    S(regs.PC);
    S(regs.SR.u);
    S(regs.F_);
    for (auto &r : regs.dmas) S(r.dw);
    for (auto &r : regs.dmad) S(r.dw);
    for (auto &r : regs.dmam) S(r.dw);
    S(halted);
    for (auto &b : PIQ) S(b);
    S(PIQ_size);
    S(PIC);
    S(OP);
    S(next_cycle);
}
#undef S

#define L(x) Sload(state, &(x), sizeof(x))
void core::deserialize(serialized_state &state)
{
    for (auto &r : regs.R) L(r.dw);
    L(regs.IR.dw);
    L(regs.INTNEST);
    L(regs.PC);
    L(regs.SR.u);
    L(regs.F_);
    for (auto &r : regs.dmas) L(r.dw);
    for (auto &r : regs.dmad) L(r.dw);
    for (auto &r : regs.dmam) L(r.dw);
    L(halted);
    for (auto &b : PIQ) L(b);
    L(PIQ_size);
    L(PIC);
    L(OP);
    L(next_cycle);
}
#undef L

}
