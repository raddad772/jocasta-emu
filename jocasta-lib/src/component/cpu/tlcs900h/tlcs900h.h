#pragma once

#include <cstdlib>
#include <cstdio>

#include "helpers/int.h"
#include "helpers/scheduler.h"
#include "helpers/debug.h"
#include "helpers/debugger/debuggerdefs.h"

struct dbglog_view;
struct serialized_state;

namespace TLCS900H {

enum reg_names {
    XWA0 = 0, XBC0, XDE0, XHL0,
    XWA1, XBC1, XDE1, XHL1,
    XWA2, XBC2, XDE2, XHL2,
    XWA3, XBC3, XDE3, XHL3,
    XIX = 16, XIY, XIZ, XSP
};

union RSPLIT {
    struct { u8 b0, b1, b2, b3; };
    u8 b[4];
    struct { u16 w_lo, w_hi; };
    u16 w[2];
    u32 dw{};
};

struct REGS {
    RSPLIT R[21];
    RSPLIT IR{};

    u32 INTNEST{};
    u32 PC{};

    union {
        struct {
            union {
                u8 F;
                u8 lo;
            };
            u8 hi;
        };
        struct {
            u16 C : 1;
            u16 N : 1;
            u16 V : 1;
            u16 : 1;
            u16 H : 1;
            u16 : 1;
            u16 Z : 1;
            u16 S : 1;
            u16 RFP : 3;
            u16 MAX : 1;
            u16 IFF : 3;
            u16 SYSM : 1;
        };
        u16 u{};
    } SR{};

    u8 F_{};

    RSPLIT dmas[4]{}, dmad[4]{}, dmam[4]{};

    static constexpr u8 SCRATCH = 20;

    [[nodiscard]] u8 slot_for(u8 id) const {
        if (id < 0x40) return id >> 2;
        switch (id & 0xF0) {
            case 0xD0: return (((SR.RFP - 1) & 3) << 2) + ((id & 0x0F) >> 2);
            case 0xE0: return ((SR.RFP & 3) << 2) + ((id & 0x0F) >> 2);
            case 0xF0: return 16 + ((id & 0x0F) >> 2);
            default: return SCRATCH;
        }
    }

    static bool valid_id(u8 id) {
        return id < 0x40 || (id >= 0xD0);
    }

    [[nodiscard]] u8 reg8(u8 id) const { return R[slot_for(id)].b[id & 3]; }
    [[nodiscard]] u16 reg16(u8 id) const { return R[slot_for(id)].w[(id >> 1) & 1]; }
    [[nodiscard]] u32 reg32(u8 id) const { return R[slot_for(id)].dw; }

    void set_reg8(u8 id, u8 v) { R[slot_for(id)].b[id & 3] = v; }
    void set_reg16(u8 id, u16 v) { R[slot_for(id)].w[(id >> 1) & 1] = v; }
    void set_reg32(u8 id, u32 v) { R[slot_for(id)].dw = v; }
};

enum operand_kind : u8 {
    OPK_NONE = 0,
    OPK_REG,
    OPK_MEM,
    OPK_IMM,
    OPK_CR,
};

struct operand {
    u8 kind{OPK_NONE};
    u8 size{};
    u8 id{};
    u32 addr{};
    u32 val{};
};

inline operand op_reg(u8 id, u8 sz) { return {OPK_REG, sz, id, 0, 0}; }
inline operand op_mem(u32 addr, u8 sz) { return {OPK_MEM, sz, 0, addr, 0}; }
inline operand op_imm(u32 val, u8 sz) { return {OPK_IMM, sz, 0, 0, val}; }
inline operand op_cr(u8 id, u8 sz) { return {OPK_CR, sz, id, 0, 0}; }

struct core {
    core(scheduler_t *scheduler_in, u64 master_clock_freq, u32 divider);
    void reset();
    void power();

    void schedule_first(u64 start);
    void schedule_next();
    template<bool do_debug> static void internal_cycle(void *ptr, u64 key, u64 clock, u32 jitter);

    template<bool do_debug> void do_interrupt(u8 vector);
    template<bool do_debug> bool do_dma(u8 channel);
    void interrupt(u8 vector);
    bool dma(u8 channel);

    bool (*service_irq)(void *ptr){};
    void *service_irq_ptr{};

    scheduler_t *scheduler{};
    double clock_div{1.0};
    double next_cycle{};
    u64 clocks_per_second{};
    u32 sched_still{};
    u64 sched_id{};

    template<bool do_debug> void decode_and_exec();

    REGS regs{};
    bool halted{};

    i32 my_cycles{};
    u8 cur_sz{};
    u8 cur_reg{};

    u64 *clock{};
    u64 local_clock{};

    u8 PIQ[4]{};
    u8 PIQ_size{};
    u8 PIC{};
    u8 OP{};
    u32 MAR{};
    u32 MDR{};

    void *mem_ptr{};

    u8 (*read8)(void *, u32 addr){};
    u16 (*read16)(void *, u32 addr){};
    u32 (*read32)(void *, u32 addr){};
    void (*write8)(void *, u32 addr, u8 val){};
    void (*write16)(void *, u32 addr, u16 val){};
    void (*write32)(void *, u32 addr, u32 val){};

    u8 (*read8_debug)(void *, u32 addr){};
    u16 (*read16_debug)(void *, u32 addr){};
    u32 (*read32_debug)(void *, u32 addr){};
    void (*write8_debug)(void *, u32 addr, u8 val){};
    void (*write16_debug)(void *, u32 addr, u16 val){};
    void (*write32_debug)(void *, u32 addr, u32 val){};

    u8 (*read8_peek)(void *, u32 addr){};
    u16 (*read16_peek)(void *, u32 addr){};
    u32 (*read32_peek)(void *, u32 addr){};

    void (*idle)(void *, u32 num){};

    static constexpr u32 VECTOR_ADDR = 0x00FF'FF00;

    void step(u32 clocks);
    [[nodiscard]] u32 width(u32 addr) const;
    [[nodiscard]] u32 speed(u32 sz, u32 addr) const;

    template<bool do_debug> u32 bus_read(u8 sz, u32 addr);
    template<bool do_debug> void bus_write(u8 sz, u32 addr, u32 val);

    void invalidate();
    template<bool do_debug> void prefetch(u32 clocks);
    template<bool do_debug> u8 fetch8();
    template<bool do_debug> u16 fetch16();
    template<bool do_debug> u32 fetch24();
    template<bool do_debug> u32 fetch32();
    template<bool do_debug> u32 fetch_imm(u8 sz);

    template<bool do_debug> u32 load(const operand &op);
    template<bool do_debug> void store(const operand &op, u32 val);

    u32 cr_load(u8 id, u8 sz);
    void cr_store(u8 id, u8 sz, u32 val);

    template<bool do_debug> void push(u8 sz, u32 val);
    template<bool do_debug> u32 pop(u8 sz);

    [[nodiscard]] u8 load_F() const;
    void store_F(u8 data);
    [[nodiscard]] u16 load_SR() const;
    void store_SR(u16 data);

    [[nodiscard]] bool condition(u8 code) const;
    [[nodiscard]] bool parity(u32 data, u8 sz) const;
    u32 ADD(u32 target, u32 source, u8 sz, u8 carry);
    u32 SUB(u32 target, u32 source, u8 sz, u8 carry);
    u32 AND(u32 target, u32 source, u8 sz);
    u32 OR(u32 target, u32 source, u8 sz);
    u32 XOR(u32 target, u32 source, u8 sz);
    u32 INC(u32 target, u32 source, u8 sz);
    u32 DEC(u32 target, u32 source, u8 sz);

    template<bool do_debug> void decode();
    template<bool do_debug> void undefined();
    template<bool do_debug> void decode_reg(const operand &reg);
    template<bool do_debug> void decode_src(const operand &source);
    template<bool do_debug> void decode_dst(u32 address);
    template<bool do_debug> u32 ea_extended(u8 mode);

    void set_rfp(u8 val);

    template<bool do_debug> void ins_NOP();
    template<bool do_debug> void ins_LD(const operand &dst, const operand &src);
    template<bool do_debug> void ins_PUSH(const operand &src);
    template<bool do_debug> void ins_POP(const operand &dst);
    template<bool do_debug> void ins_ADD(const operand &dst, const operand &src);
    template<bool do_debug> void ins_ADC(const operand &dst, const operand &src);
    template<bool do_debug> void ins_SUB(const operand &dst, const operand &src);
    template<bool do_debug> void ins_SBC(const operand &dst, const operand &src);
    template<bool do_debug> void ins_AND(const operand &dst, const operand &src);
    template<bool do_debug> void ins_OR(const operand &dst, const operand &src);
    template<bool do_debug> void ins_XOR(const operand &dst, const operand &src);
    template<bool do_debug> void ins_CP(const operand &dst, const operand &src);
    template<bool do_debug> void ins_INC(const operand &dst, const operand &src);
    template<bool do_debug> void ins_DEC(const operand &dst, const operand &src);

    template<bool do_debug> void ins_JP(const operand &src);
    template<bool do_debug> void ins_JR(const operand &src);
    template<bool do_debug> void ins_CALL(const operand &src);
    template<bool do_debug> void ins_CALR(const operand &src);
    template<bool do_debug> void ins_RET();
    template<bool do_debug> void ins_RETD(const operand &src);
    template<bool do_debug> void ins_RETI();
    template<bool do_debug> void ins_SWI(u8 vector);
    template<bool do_debug> void ins_HALT();

    template<bool do_debug> void ins_RCF();
    template<bool do_debug> void ins_SCF();
    template<bool do_debug> void ins_CCF();
    template<bool do_debug> void ins_ZCF();
    template<bool do_debug> void ins_INCF();
    template<bool do_debug> void ins_DECF();
    template<bool do_debug> void ins_LDF(u8 val);
    template<bool do_debug> void ins_EI(u8 val);
    template<bool do_debug> void ins_EX(const operand &dst, const operand &src);

    [[nodiscard]] operand expand_reg(const operand &r) const;
    u32 algorithm_rotated(u32 result, u8 sz);

    template<bool do_debug> void ins_MUL(const operand &dst, const operand &src);
    template<bool do_debug> void ins_MULS(const operand &dst, const operand &src);
    template<bool do_debug> void ins_DIV(const operand &dst, const operand &src);
    template<bool do_debug> void ins_DIVS(const operand &dst, const operand &src);
    template<bool do_debug> void ins_RLC(const operand &dst, const operand &amount);
    template<bool do_debug> void ins_RRC(const operand &dst, const operand &amount);
    template<bool do_debug> void ins_RL(const operand &dst, const operand &amount);
    template<bool do_debug> void ins_RR(const operand &dst, const operand &amount);
    template<bool do_debug> void ins_SLA(const operand &dst, const operand &amount);
    template<bool do_debug> void ins_SRA(const operand &dst, const operand &amount);
    template<bool do_debug> void ins_SLL(const operand &dst, const operand &amount);
    template<bool do_debug> void ins_SRL(const operand &dst, const operand &amount);
    template<bool do_debug> void ins_BIT(const operand &src, const operand &off);
    template<bool do_debug> void ins_RES(const operand &dst, const operand &off);
    template<bool do_debug> void ins_SET(const operand &dst, const operand &off);
    template<bool do_debug> void ins_CHG(const operand &dst, const operand &off);
    template<bool do_debug> void ins_TSET(const operand &dst, const operand &off);
    template<bool do_debug> void ins_CPL(const operand &dst);
    template<bool do_debug> void ins_NEG(const operand &dst);
    template<bool do_debug> void ins_EXTZ(const operand &dst);
    template<bool do_debug> void ins_EXTS(const operand &dst);
    template<bool do_debug> void ins_DAA(const operand &dst);
    template<bool do_debug> void ins_PAA(const operand &dst);
    template<bool do_debug> void ins_MIRR(const operand &dst);
    template<bool do_debug> void ins_MINC(u32 modulo, const operand &dst, const operand &src);
    template<bool do_debug> void ins_MDEC(u32 modulo, const operand &dst, const operand &src);
    template<bool do_debug> void ins_DJNZ(const operand &dst, const operand &off);
    template<bool do_debug> void ins_SCC(u8 code, const operand &dst);
    template<bool do_debug> void ins_BS1F(const operand &src);
    template<bool do_debug> void ins_BS1B(const operand &src);
    template<bool do_debug> void ins_MULA(const operand &dst);
    template<bool do_debug> void ins_LINK(const operand &dst, const operand &off);
    template<bool do_debug> void ins_UNLK(const operand &dst);
    template<bool do_debug> void ins_ANDCF(const operand &src, const operand &off);
    template<bool do_debug> void ins_ORCF(const operand &src, const operand &off);
    template<bool do_debug> void ins_XORCF(const operand &src, const operand &off);
    template<bool do_debug> void ins_LDCF(const operand &src, const operand &off);
    template<bool do_debug> void ins_STCF(const operand &dst, const operand &off);

    template<bool do_debug> void ins_LDI(u8 sz, i32 adjust);
    template<bool do_debug> void ins_LDIR(u8 sz, i32 adjust);
    template<bool do_debug> void ins_CPI(u8 sz, i32 adjust);
    template<bool do_debug> void ins_CPIR(u8 sz, i32 adjust);
    template<bool do_debug> void ins_RLD(const operand &mem);
    template<bool do_debug> void ins_RRD(const operand &mem);

    void setup_tracing(jsm_debug_read_trace *strct, u64 *trace_cycle_ptr, i32 source_id);
    void trace_format();

    struct {
        jsm_debug_read_trace strct{};
        jsm_string str{100}, str2{100};
        u32 ins_PC{};
        i32 source_id{};

        struct {
            struct dbglog_view *view{};
            u32 id{};
            u32 id_read{}, id_write{};
        } dbglog{};
        u32 ok{};
        u64 my_cycles{};
        u64 *cycles{};
    } trace{};

    DBG_START
        DBG_EVENT_VIEW_START
        INT{}, DMA{}
        DBG_EVENT_VIEW_END

        DBG_TRACE_VIEW

        DBG_LOG_VIEW_SIMPLE
        u32 irq_id{};
    DBG_END

    void serialize(serialized_state &state);
    void deserialize(serialized_state &state);
};

typedef void (*ins_func)(core &);

}
