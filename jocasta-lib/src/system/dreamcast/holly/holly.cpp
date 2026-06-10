//
// Created by . on 4/2/26.
//
#include <cstring>
#include <cassert>
#include <cmath>
#include "helpers/multisize_memaccess.cpp"
#include <bit>
#include "../dc_bus.h"



#define printf_TA_parse(...)

#define CLAMP(x, low, high) ((x) < (low) ? (low) : ((x) > (high) ? (high) : (x)))

#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif

#ifndef MAX
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif

namespace DREAMCAST::HOLLY {
core::core(DREAMCAST::core *parent) :
    bus(parent)
    {
    RAM = static_cast<u8 *>(malloc(8 * 1024 * 1024));
    // NTSC default until SPG_LOAD is written by software.
    // Use 59.94 Hz (NTSC standard); integer /60 truncates 1/3 cycle/frame (~20 Hz drift).
    timing.cycles_per_frame = (u64)(CYCLES_PER_SEC / 59.94);
    SPG_LOAD.vcount = 600;
    SPG_VBLANK_INT.vblank_in_line = 500;
    SPG_VBLANK_INT.vblank_out_line = 2;
    recalc_frame_timing();
}

core::~core() {
    if (RAM) free(RAM);
    RAM = nullptr;
}


void core::reset() {

}

void core::new_frame() {
    frame_start_cycle = bus->master_cycles;
}

static void delayed_raise_interrupt(void* ptr, u64 key, u64 clock, u32 jitter)
{
    auto *th = static_cast<core *>(ptr);
    th->raise_interrupt(static_cast<interruptmasks>(key), 0);
}


void core::raise_interrupt(interruptmasks irq_num, i64 delay)
{
    if (delay > 0) {
        i64 tcode = static_cast<i64>(bus->master_cycles) + delay;
        u64 key = irq_num;

        bus->scheduler.add_or_run_abs(tcode, key, this, &delayed_raise_interrupt, nullptr);
        return;
    }
    u32 imask = 1 << (irq_num & 0xFF);
    ///G2DTNRM
    if (irq_num & 0x100) {
        if (bus->io.SB_G2DTEXT & imask) bus->g2.dma_irq_trigger();
        if (bus->io.SB_PDTEXT & imask) pvr_dma_irq_trigger();
        bus->io.SB_ISTEXT.u |= imask;
    }
    else if (irq_num & 0x200) {
        bus->io.SB_ISTERR.u |= imask;
    }
    else { // normal
        if (bus->io.SB_G2DTNRM & imask) bus->g2.dma_irq_trigger();
        if (bus->io.SB_PDTNRM & imask) pvr_dma_irq_trigger();
        bus->io.SB_ISTNRM.u |= imask;
    }
#ifdef SH4_IRQ_DBG
    printf(DBGC_BLUE "\nHOLLY RAISE INTTERUPT %s val:%08x SB_ISTNRM:%08x SB_ISTEXT:%08x cyc:%llu" DBGC_RST, irq_strings[irq_num & 0xFF], imask, bus->io.SB_ISTNRM.u, bus->io.SB_ISTEXT.u, sh4.clock.trace_cycles);
#endif
    recalc_interrupts();
}


void core::eval_interrupt(interruptmasks irq_num, bool is_true)
{
    if (is_true) raise_interrupt(irq_num, -1);
    else lower_interrupt(irq_num);
}

void core::lower_interrupt(interruptmasks irq_num)
{
    u32 imask = (1 << (irq_num & 0xFF)) ^ 0xFFFFFFFF;
    if (irq_num & 0x100)
        bus->io.SB_ISTEXT.u &= imask;
    else if (irq_num & 0x200)
        bus->io.SB_ISTERR.u &= imask;
    else
        bus->io.SB_ISTNRM.u &= imask;
    recalc_interrupts();
}

void core::vblank_in()
{
    in_vblank = 1;
    copy_fb();
    raise_interrupt(hirq_vblank_in, -1);
}

u32 core::get_SPG_line() {
    u32 cycle_num = get_frame_cycle();
    return cycle_num / timing.cycles_per_line;
}

u32 core::get_frame_cycle() {
    return static_cast<u32>(bus->master_cycles - frame_start_cycle);
}


void core::vblank_out() {
    in_vblank = 0;
    raise_interrupt(hirq_vblank_out, -1);
    // TODO for TA
    //ta.list_type = HPL_none;

    if ((bus->maple.SB_MDTSEL == 1) && bus->maple.SB_MDEN) {
        bus->maple.dma_init();
    }
}

#define NI 0b1111
    static constexpr u32 IRQ_outputs[7] = {
    NI, // no interrupt
    NI, // no interrupt
    0b1101, // level 2
    NI, // no intterupt
    0b1011, // level 4
    NI, // no interrupt
    0b1001  // level 6
};
#undef NI

u64 core::read_io(u32 addr, u8 sz, bool *success) {
    addr = (addr & 0x0000FFFF) | 0x005F0000;
    u32 v;
    switch (addr) {
        case 0x005F74B0:
            return (0b0000 << 4) | // sys mode mass production
                        0b0100; // region north america
            case 0x005F8000: return 0x17FD11DB; // Device ID
        case 0x005F8004: return 0x11; // Revision
        case 0x005F8030:  { return SPAN_SORT_CFG; }
        case 0x005F8040:  { return VO_BORDER_COL.u; }
        case 0x005F8044:  { return FB_R_CTRL.u; }
        case 0x005F8048:  { return FB_W_CTRL.u; }
        case 0x005F804C:  { return FB_W_LINESTRIDE.u; }
        case 0x005F8050:  { return FB_R_SOF1.u; }
        case 0x005F8054:  { return FB_R_SOF2.u; }
        case 0x005F805C:  { return FB_R_SIZE.u; }
        case 0x005F8060:  { return FB_W_SOF1.u; }
        case 0x005F8064:  { return FB_W_SOF2.u; }
        case 0x005F8068:  { return FB_X_CLIP.u; }
        case 0x005F806C:  { return FB_Y_CLIP.u; }
        case 0x005F8074:  { return FPU_SHAD_SCALE.u; }
        case 0x005F807C:  { return FPU_PARAM_CFG.u; }
        case 0x005F8080:  { return HALF_OFFSET.u; }
        case 0x005F808C:  { return ISP_BACKGND_T.u; }
        case 0x005F8098:  { return ISP_FEED_CFG.u; }
        case 0x005F80A0:  { return SDRAM_REFRESH; }
        case 0x005F80A8:  { return SDRAM_CFG; }
        case 0x005F80B0:  { return FOG_COL_RAM; }
        case 0x005F80B4:  { return FOG_COL_VERT.u; }
        case 0x005F80B8:  { return FOG_DENSITY.u; }
        case 0x005F80BC:  { return FOG_CLAMP_MAX; }
        case 0x005F80C0:  { return FOG_CLAMP_MIN; }
        case 0x005F80C8:  { return SPG_HBLANK_INT.u; }
        case 0x005F80CC:  { return SPG_VBLANK_INT.u; }
        case 0x005F80D0:  { return SPG_CONTROL.u; }
        case 0x005F80D4:  { return SPG_HBLANK.u; }
        case 0x005F80D8:  { return SPG_LOAD.u; }
        case 0x005F80DC:  { return SPG_VBLANK.u; }
        case 0x005F80E0:  { return SPG_WIDTH.u; }
        case 0x005F80E4:  { return TEXT_CONTROL.u; }
        case 0x005F80E8:  { return VO_CONTROL.u; }
        case 0x005F80EC:  { return VO_STARTX; }
        case 0x005F80F0:  { return VO_STARTY.u; }
        case 0x005F80F4:  { return SCALER_CTL.u; }
        case 0x005F810C: // SPG_STATUS read-only
            // determine scanline
            v = get_SPG_line() & 0x3FF;
            //TODO: blank, hsync, fieldnum
            v |= (in_vblank) << 13;
            return v;
        case 0x005F8110:  { return FB_BURSTCTRL.u; }
        case 0x005F8118:  { return Y_COEFF.u; }
        case 0x005F8124:  { return TA_OL_BASE.u; }
        case 0x005F8128:  { return TA_ISP_BASE; }
        case 0x005F812C:  { return TA_OL_LIMIT.u; }
        case 0x005F8130:  { return TA_ISP_LIMIT.u; }
        case 0x005F8134:  { return TA_NEXT_OPB; }
        case 0x005F8138:  { return TA_ITP_CURRENT; }
        case 0x005F813C:  { return TA_GLOB_TILE_CLIP.u; }
        case 0x005F8140:  { return TA_ALLOC_CTRL.u; }
        case 0x005f8144: // TA_LIST_INIT
            return 0;
        case 0x005F8160:  { return TA_LIST_CONT; }
        case 0x005F8164:  { return TA_NEXT_OPB_INIT; }
    }
    if ((addr >= 0x005F8200) && (addr <= 0x005F83FC)) {
        return FOG_TABLE[(addr - 0x005F8200) >> 2];
    }

    *success = false;
    printf("\nHOLLY read bad reg %08x(%d)", addr, sz);
    return 0;
}

    void core::recalc_interrupts() {
    u32 level2 = (bus->io.SB_IML2NRM & bus->io.SB_ISTNRM.u) & 0x3FFFFF;
    level2 |= (bus->io.SB_IML2EXT.u & bus->io.SB_ISTEXT.u);
    level2 |= (bus->io.SB_IML2ERR.u & bus->io.SB_ISTERR.u);

    u32 level4 = (bus->io.SB_IML4NRM & bus->io.SB_ISTNRM.u);
    level4 |= (bus->io.SB_IML4EXT.u & bus->io.SB_ISTEXT.u);
    level4 |= (bus->io.SB_IML4ERR.u & bus->io.SB_ISTERR.u);

    u32 level6 = (bus->io.SB_IML6NRM & bus->io.SB_ISTNRM.u);
    level6 |= (bus->io.SB_IML6EXT.u & bus->io.SB_ISTEXT.u);
    level6 |= (bus->io.SB_IML6ERR.u & bus->io.SB_ISTERR.u);

    u32 highest_level = 0;
    if (level2) highest_level = 2;
    if (level4) highest_level = 4;
    if (level6) highest_level = 6;
    if ((highest_level != 0) && (dbg.do_debug)) {
        //printf(DBGC_RED "\nHIGHEST LEVEL: %d l2:%04x l4:%04x l6:%04x cyc:%llu" DBGC_RST, highest_level, bus->io.SB_IML2NRM, bus->io.SB_IML4NRM, bus->io.SB_IML6NRM, bus->trace_cycles);
    }
    //if (highest_level != sh4.IRL_irq_level) {
    //printf("\nINTERRUPT HIGHEST LEVEL CHANGE TO %d cyc:%llu", highest_level, sh4.clock.trace_cycles);
    //printf("\nIML6NRN: %08x", bus->io.SB_IML6NRM & bus->io.SB_ISTNRM.u & 0x3FFFFF);
    //}
    //printf("\nHOLLY RAISING INTERRUPT ON STEP %llu", sh4.clock.trace_cycles);
    u32 lv = IRQ_outputs[highest_level];
#ifdef DCDBG_HOLLY_IRQ
    printf("\nSET HOLLY IRQ OUT LEVEL TO %d", lv);
#endif
    static u32 old_level = 0xF;
    if (old_level != lv) {
        //printf("\nSET HOLLY IRQ OUT LEVEL TO %d", lv);
        old_level = lv;
    }
    bus->cpu.set_IRL(lv);
}

void core::soft_reset()
{
    printf("\nHOLLY soft reset!");
    ta.cmd_buffer_index = 0;
    ta.list_type = HPL_none;
    ta.vtx_list.clear();
    for (auto & t : ta.poly_lists) {
        t.clear();
    }
}

static void dump_RAM_to_console(u32 print_addr, void *src, u32 len)
{
    auto *ptr = static_cast<u8 *>(src);
    // 16 bytes per line
    u32 bytes_printed = 0;
    for (u32 laddr = 0; laddr < len; laddr += 16) {
        printf("\n%08X   ", print_addr+bytes_printed);
        for (u32 i = 0; i < 16; i++) {
            printf("%02X ", static_cast<u32>(*ptr));
            ptr++;
            bytes_printed++;
            if (bytes_printed >= len) return;
        }
    }
}

// FIFO DMA ! Transfer data!
void core::TA_FIFO_DMA(u32 src_addr, u32 tx_len, void *src, u32 src_len)
{
    if (tx_len == 0) {
        printf("\nHOLLY TA DMA TRANSFER SIZE 0!?");
        return;
    }
    /*if ((src_addr+tx_len) >= src_len) {
        printf(DBGC_RED "\nTOO LONG DMA TRANSFER CH2 %08x" DBGC_RST, src_addr);
        return;
    }*/
    //dump_RAM_to_console(src_addr, static_cast<u8 *>(bus->RAM) + (src_addr & 0xFFFFFF), tx_len);
    TA_FIFO_load(src_addr & 0xFFFFFF, tx_len, bus->RAM);
    bus->cpu.dmac.channels[2].SAR = src_addr + tx_len;
    bus->cpu.dmac.channels[2].CHCR.u &= 0xFFFFFFFE;
    bus->cpu.dmac.channels[2].DMATCR = 0x00000000;

    bus->io.SB_C2DST = 0x00000000;
    bus->io.SB_C2DLEN = 0x00000000;
}

// Actually take in the data
void core::TA_FIFO_load(u32 src_addr, u32 tx_len, void* src)
{
    u32 bytes_tx = 0;
    auto *src_u8 = static_cast<u8 *>(src);
    for (u32 i = 0; i < tx_len; i+= 32) {
        if (ta.cmd_buffer_index >= 64) {
            printf("\nWARNING TOO BIG!");
            NOGOHERE;
        }
        memcpy(ta.cmd_buffer + ta.cmd_buffer_index, (src_u8+src_addr+i), 32);
        ta.cmd_buffer_index += 32;
        bytes_tx += 32;
        TA_cmd();
    }
    if (bytes_tx < tx_len) {
        printf("\nREVISIT THIS FOOL");
        printf("\nREVISIT THIS FOOL");
        printf("\nREVISIT THIS FOOL");
        printf("\nREVISIT THIS FOOL");
        printf("\nREVISIT THIS FOOL");
        printf("\nREVISIT THIS FOOL");
    }
}

void core::process_global_pcw(PCW cmd) {
    // Group control
    if (cmd.group_en) {
        ta.global_params.strip_len = cmd.strip_len;
        ta.global_params.user_clip = cmd.user_clip;
    }
    ta.global_params.u = (ta.global_params.u & 0xFFFF0000) | cmd.u;
    update_poly_vertex_type();
}

u32 core::get_polygon_typenum(u8 wrd) {
    u8 masked = wrd & 0b1111101;
    switch (masked) {
        case 0b0000000:
        case 0b0000100:
        case 0b0000001:
        case 0b0000101: return 0; // PT 0
        case 0b0010000:
        case 0b0010100:
        case 0b0010001:
        case 0b0010101: return 0; // VT 1
        case 0b0100000:
        case 0b0100100:
        case 0b0100001:
        case 0b0100101: return 1; // VT 2
        case 0b0110000:
        case 0b0110100:
        case 0b0110001:
        case 0b0110101: return 0; // VT 2
        case 0b1000000:
        case 0b1000100:
        case 0b1000001:
        case 0b1000101: return 3; // VT 9
        case 0b1100000:
        case 0b1100100:
        case 0b1100001:
        case 0b1100101: return 4; // VT 10
        case 0b1110000:
        case 0b1110100:
        case 0b1110001:
        case 0b1110101: return 3; // VT 10
        case 0b0001000:
        case 0b0001100: return 0; // VT 3
        case 0b0001001:
        case 0b0001101: return 0; // VT 4
        case 0b0011000:
        case 0b0011100: return 0; // VT 5
        case 0b0011001:
        case 0b0011101: return 0; // VT 6
        case 0b0101000: return 1; // VT 7
        case 0b0101100: return 2; // VT 7
        case 0b0101001: return 1; // VT 8
        case 0b0101101: return 2; // VT 8
        case 0b0111000:
        case 0b0111100: return 0; // VT 7
        case 0b0111001:
        case 0b0111101: return 0; // VT 8
        case 0b1001000:
        case 0b1001100: return 3; // VT 11
        case 0b1001001:
        case 0b1001101: return 3; // VT 12
        case 0b1101000:
        case 0b1101100: return 4; // VT 13
        case 0b1101001:
        case 0b1101101: return 4; // VT 14
        case 0b1111000:
        case 0b1111100: return 3; // VT 13
        case 0b1111001:
        case 0b1111101: return 3; // VT 14
    }
    printf("\nBad polygon type!?!?!?");
    return 0;
}

u32 core::get_vertex_typenum(u8 wrd) {
    u8 masked = wrd & 0b1111101;
    switch (masked) {
        case 0b0000000:
        case 0b0000100:
        case 0b0000001:
        case 0b0000101: return 0; // PT 0
        case 0b0010000:
        case 0b0010100:
        case 0b0010001:
        case 0b0010101: return 1; // VT 1
        case 0b0100000:
        case 0b0100100:
        case 0b0100001:
        case 0b0100101: return 2; // VT 2
        case 0b0110000:
        case 0b0110100:
        case 0b0110001:
        case 0b0110101: return 2; // VT 2
        case 0b1000000:
        case 0b1000100:
        case 0b1000001:
        case 0b1000101: return 9; // VT 9
        case 0b1100000:
        case 0b1100100:
        case 0b1100001:
        case 0b1100101: return 10; // VT 10
        case 0b1110000:
        case 0b1110100:
        case 0b1110001:
        case 0b1110101: return 10; // VT 10
        case 0b0001000:
        case 0b0001100: return 3; // VT 3
        case 0b0001001:
        case 0b0001101: return 4; // VT 4
        case 0b0011000:
        case 0b0011100: return 5; // VT 5
        case 0b0011001:
        case 0b0011101: return 6; // VT 6
        case 0b0101000: return 7; // VT 7
        case 0b0101100: return 7; // VT 7
        case 0b0101001: return 8; // VT 8
        case 0b0101101: return 8; // VT 8
        case 0b0111000:
        case 0b0111100: return 7; // VT 7
        case 0b0111001:
        case 0b0111101: return 8; // VT 8
        case 0b1001000:
        case 0b1001100: return 11; // VT 11
        case 0b1001001:
        case 0b1001101: return 12; // VT 12
        case 0b1101000:
        case 0b1101100: return 13; // VT 13
        case 0b1101001:
        case 0b1101101: return 14; // VT 14
        case 0b1111000:
        case 0b1111100: return 13; // VT 13
        case 0b1111001:
        case 0b1111101: return 14; // VT 14
    }
    printf("\nBad vertex type!?!?!?");
    return 0;

}

u32 core::poly_size(u32 poly_kind) {
    switch(get_polygon_typenum(ta.global_params.u & 0xFF)) {
        case 0:
        case 1:
        case 3: return 32;
        case 2:
        case 4: return 64;
        default: break;
    }
    NOGOHERE;
}

u32 core::vertex_size(u32 vertex_kind) {
    switch (vertex_kind) {
        case 0:
        case 1:
        case 2:
        case 3:
        case 4:
        case 7:
        case 8:
        case 9:
        case 10: return 32;

        case 5:
        case 6:
        case 11:
        case 12:
        case 13:
        case 14: return 64;
        default: NOGOHERE;
    }
}

u32 core::get_sprite_typenum(u8 wrd) {
    return (wrd >> 3) & 1;
}

u32 core::sprite_vertex_size(u32 vertex_kind) {
    return 64;
}


u32 core::TA_cmd_len(PCW cmd) {
    switch (cmd.para_type) {
        case 0:
        case 1:
        case 2:
        case 3:
        case 5:
        case 6:
            return 32;

        case 4:
            return poly_size(get_polygon_typenum(cmd.u & 0xFF));

        case 7:
            switch (ta.list_type) {
            case 1:
            case 3:
                    return 64;
            case 5:
                    return sprite_vertex_size(get_sprite_typenum(ta.global_params.u & 0xFF));
            default:
                    return vertex_size(get_vertex_typenum(ta.global_params.u & 0xFF));
            }
    }

    NOGOHERE;
}

void core::update_poly_vertex_type() {
    ta.poly_kind = get_polygon_typenum(ta.global_params.u & 0xFF);
    ta.vertex_kind = get_vertex_typenum(ta.global_params.u & 0xFF);
}

u32 FLOAT_TO_COL8(float f) {
    float a = floor((f * 255.0f) + .5f);
    a = CLAMP(a, 0.0f, 255.0f);
    return static_cast<u32>(a);

}
void core::parse_vertex(PCW cmd) {
    auto &v = ta.vtx_list.emplace_back();
    ta.end_of_strip = cmd.end_of_strip;
    //if (ta.end_of_strip) printf("\nEND OF STRIP!?");
    switch (ta.vertex_kind) {
//#define VERTSCALE * .5f
#define VERTSCALE
        case 0: {
            float *p = reinterpret_cast<float *>(&ta.cmd_buffer[4]);
            v.x = (*p++) VERTSCALE;
            v.y = (*p++) VERTSCALE;
            v.z= *p++;
            u32 *c = reinterpret_cast<u32 *>(&ta.cmd_buffer[24]);
            v.a = (*c >> 24);
            v.r = (*c >> 16) & 0xFF;
            v.g = (*c >> 8) & 0xFF;
            v.b = (*c) & 0xFF;
            return;
        }
        case 1: {
            float *p = reinterpret_cast<float *>(&ta.cmd_buffer[4]);
            v.x = (*p++) VERTSCALE;
            v.y = (*p++) VERTSCALE;
            v.z= *p++;
            //v.x /= 10;
            //v.y /= 10;
            v.a = FLOAT_TO_COL8(*p++);
            v.r = FLOAT_TO_COL8(*p++);
            v.g = FLOAT_TO_COL8(*p++);
            v.b = FLOAT_TO_COL8(*p++);
            return; }
        case 3: {
            float *p = reinterpret_cast<float *>(&ta.cmd_buffer[4]);
            v.x = (*p++) VERTSCALE;
            v.y = (*p++) VERTSCALE;
            v.z= *p++;
            v.u = *p++;
            v.v = *p++;
            u32 *c = reinterpret_cast<u32 *>(&ta.cmd_buffer[24]);
            v.a = (*c >> 24);
            v.r = (*c >> 16) & 0xFF;
            v.g = (*c >> 8) & 0xFF;
            v.b = (*c) & 0xFF;
            c++;
            v.offset.a = (*c >> 24);
            v.offset.r = (*c >> 16) & 0xFF;
            v.offset.g = (*c >> 8) & 0xFF;
            v.offset.b = (*c) & 0xFF;
            return;

        }
    }
    printf("\nMISSING VERTEX PARSE %d!", ta.vertex_kind);
}

void core::parse_polygon() {
    ta.cur_poly.isp_tsp_word.u = cR64(ta.cmd_buffer, 4);
    auto &cp = ta.cur_poly;
    cp.isp_tsp_word.texture = ta.global_params.texture;
    cp.isp_tsp_word.offset = ta.global_params.offset;
    cp.isp_tsp_word.gouraud = ta.global_params.gouraud;
    cp.isp_tsp_word.uv_16bit = ta.global_params.uv_is_16bit;
    cp.tsp_word.u = cR64(ta.cmd_buffer, 8);
    cp.tcw.normal.u = cR64(ta.cmd_buffer, 12);
    switch (ta.poly_kind) {
        case 0: return;
        case 1: {
            float *p = reinterpret_cast<float *>(&ta.cmd_buffer[0x10]);
            cp.face.a = FLOAT_TO_COL8(*p++);
            cp.face.r = FLOAT_TO_COL8(*p++);
            cp.face.g = FLOAT_TO_COL8(*p++);
            cp.face.b = FLOAT_TO_COL8(*p++);
            return; }
        default: break;
    }
    printf("\nMISSING POLY PARSE %d!", ta.poly_kind);
}

void core::parse_mv() {
    printf("\nWARN NO MV PARSE MISSING!");
}

static inline float MIN3(float a, float b, float c)
{
    const i32 mab = MIN(a,b);
    return MIN(mab, c);
}

static inline float MAX3(float a, float b, float c)
{
    const i32 mab = MAX(a,b);
    return MAX(mab, c);
}

static inline float cpz(VERTEX *v0, VERTEX *v1, VERTEX *v2) {
    return (v1->x - v0->x) * (v2->y - v0->y) - (v1->y - v0->y) * (v2->x - v0->x);
}

static inline void compute_barycentric(float cp, VERTEX *p, VERTEX *v0, VERTEX *v1, VERTEX *v2, float *lambdas) {
    if (cp == 0) {
        lambdas[0] = lambdas[1] = lambdas[2] = 1.0f/3.0f;
        return;
    }
    float r = 1.0f / cp;
    lambdas[0] = cpz(v1, v2, p) * r;
    lambdas[1] = cpz(v2, v0, p) * r;
    lambdas[2] = (1.0f - lambdas[0]) - lambdas[1];
}

static inline bool check3(VERTEX *a, VERTEX *b, VERTEX *c) {
    float cp = cpz(a, b, c);
    if (cp < 0) return false;
    if (cp == 0) {
        if (b->y > a->y) return false;
        if (b->y == a->y && b->x < a->x) return false;
    }
    return true;
}    
    
static inline bool is_inside_triangle(VERTEX *p, VERTEX *v0, VERTEX *v1, VERTEX *v2) {
    if (!check3(v0, v1, p)) return false;
    if (!check3(v1, v2, p)) return false;
    if (!check3(v2, v0, p)) return false;
    return true;
}
    
    
void core::draw_line(VERTEX *v0, VERTEX *v1) {
    i32 x0 = v0->x;
    i32 y0 = v0->y;
    i32 x1 = v1->x;
    i32 y1 = v1->y;

    i32 dx = std::abs(x1 - x0);
    i32 dy = std::abs(y1 - y0);
    i32 sx = (x0 < x1) ? 1 : -1;
    i32 sy = (y0 < y1) ? 1 : -1;
    i32 err = dx - dy;

    const i32 steps = std::max(dx, dy);

    i32 r0, g0, b0;
    i32 r1, g1, b1;

    r0 = static_cast<i32>(v0->r);
    g0 = static_cast<i32>(v0->g);
    b0 = static_cast<i32>(v0->b);

    r1 = static_cast<i32>(v1->r);
    g1 = static_cast<i32>(v1->g);
    b1 = static_cast<i32>(v1->b);

    VERTEX p;
    i32 step = 0;

    for (;;) {
        p.x = x0;
        p.y = y0;

        i32 mr, mg, mb;

        if (steps == 0) {
            mr = r0;
            mg = g0;
            mb = b0;
        } else {
            // Exact endpoints:
            // step == 0      -> v0 color
            // step == steps  -> v1 color
            mr = r0 + ((r1 - r0) * step) / steps;
            mg = g0 + ((g1 - g0) * step) / steps;
            mb = b0 + ((b1 - b0) * step) / steps;
        }
        mr = CLAMP(mr, 0, 255);
        mg = CLAMP(mg, 0, 255);
        mb = CLAMP(mb, 0, 255);

        (this->*setpix)(static_cast<i32>(p.x), static_cast<i32>(p.y), mr, mg, mb);
        if (x0 == x1 && y0 == y1) {
            break;
        }

        i32 e2 = err << 1;

        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }

        ++step;
    }
}
    
void core::draw_tri(VERTEX *v0, VERTEX *v1, VERTEX *v2) {
    float minX = MIN3(v0->x, v1->x, v2->x);
    float minY = MIN3(v0->y, v1->y, v2->y);
    float maxX = MAX3(v0->x, v1->x, v2->x);
    float maxY = MAX3(v0->y, v1->y, v2->y);
    minX = MAX(minX, x_min);
    maxX = MIN(maxX, x_max);
    minY = MAX(minY, y_min);
    maxY = MIN(maxY, y_max);
    if (minX > maxX || minY > maxY) {
        //printf("\nQUIT HERE");
        return;
    }
    draw_line(v0, v1);
    draw_line(v1, v2);
    draw_line(v2, v0);
    return;

    float r_mul, g_mul, b_mul, u, v;
    i32 mr, mg, mb;

    float cross_product_z = cpz(v0, v1, v2);
    if (cross_product_z < 0) {
        VERTEX *sa = v0;
        v0 = v1;
        v1 = sa;
        cross_product_z = cpz(v0, v1, v2);
    }
    //printf("\n\nDRAW TRI: minX maxY/minY maxY %f %f/%f %f", minX, maxX, minY, maxY);
    for (u32 i = 0; i < 3; i++) {
        VERTEX *ev;
        switch (i) {
            case 0: ev = v0; break;
            case 1: ev = v1; break;
            case 2: ev = v2; break;
        }
        //printf("\nX Y %f %f  RGB %02x %02x %02x", ev->x, ev->y, ev->r, ev->g, ev->b);
    }

    // Initialise our point
    VERTEX p;
    float lambda[3];
    for (p.y = floor(minY); p.y < ceil(maxY); p.y++) {
        for (p.x = floor(minX); p.x < ceil(maxX); p.x++) {
            if (is_inside_triangle(&p, v0, v1, v2)) {
                compute_barycentric(cross_product_z, &p, v0, v1, v2, lambda);
                // TODO: FIX!
                r_mul = ((lambda[0] * v0->r) + (lambda[1] * v1->r) + (lambda[2] * v2->r));
                g_mul = ((lambda[0] * v0->g) + (lambda[1] * v1->g) + (lambda[2] * v2->g));
                b_mul = ((lambda[0] * v0->b) + (lambda[1] * v1->b) + (lambda[2] * v2->b));
                mr = r_mul;
                mg = g_mul;
                mb = b_mul;

                mr = CLAMP(mr, 0, 255);
                mg = CLAMP(mg, 0, 255);
                mb = CLAMP(mb, 0, 255);
                (this->*setpix)(static_cast<i32>(p.x), static_cast<i32>(p.y), mr, mg, mb);
            }
        }
    }
}


void core::ingest_tri(VERTEX *v0, VERTEX *v1, VERTEX *v2) {
    auto &list_p = ta.poly_lists[ta.list_type].emplace_back();
    ta.cur_poly.num_vtx = 3;
    list_p.copy(ta.cur_poly);
    list_p.vtx[0].copy(v0);
    list_p.vtx[1].copy(v1);
    list_p.vtx[2].copy(v2);
    list_p.num_vtx = 3;
    if (ta.cur_poly.pcw.col_type == 2) { // Intensity Mode 1
        ta.intensity.a = ta.cur_poly.face.a;
        ta.intensity.r = ta.cur_poly.face.r;
        ta.intensity.g = ta.cur_poly.face.g;
        ta.intensity.b = ta.cur_poly.face.b;
    }
    if (ta.cur_poly.pcw.col_type >= 2) { // Intensity Mode 2
        for (auto &v : list_p.vtx) {
            v.a = ta.cur_poly.face.a;
            v.r = ta.cur_poly.face.r;
            v.g = ta.cur_poly.face.g;
            v.b = ta.cur_poly.face.b;
        }
    }
    for (auto &v : list_p.vtx) {
        v.a = CLAMP(v.a, 0, 255);
        v.r = CLAMP(v.r, 0, 255);
        v.g = CLAMP(v.g, 0, 255);
        v.b = CLAMP(v.b, 0, 255);
    }
}

void core::ingest_quad(VERTEX *v0, VERTEX *v1, VERTEX *v2, VERTEX *v3) {
    printf("\nWARN NO INGEST QUAD!");
}


void core::end_parse() {
    if (ta.vtx_list.size() == 3) {
        ingest_tri(&ta.vtx_list[0], &ta.vtx_list[1], &ta.vtx_list[2]);
        return;
    }
    if (ta.end_of_strip) {
        auto *v0 = &ta.vtx_list[0];
        auto *v1 = &ta.vtx_list[1];
        for (u32 i = 2; i < ta.vtx_list.size(); i++) {
            auto *v2 = &ta.vtx_list[i];
            ingest_tri(v0, v1, v2);
            v0 = v1;
            v1 = v2;
        }
        return;
    }
    if (ta.vtx_list.size() %4 == 0) {
        for (u32 i = 0; i < ta.vtx_list.size() - 3; i += 4) {
            ingest_quad(&ta.vtx_list[i], &ta.vtx_list[i + 1], &ta.vtx_list[i + 2], &ta.vtx_list[i + 3]);
        }
    }
    else {
        if (ta.vtx_list.size() % 3 != 0) {
            printf("\nWARN NON-STRIP NON-ARRAY POLY DATA! %ld", ta.vtx_list.size());
            return;
        }
        for (u32 i = 0; i < ta.vtx_list.size() - 2; i += 3) {
            ingest_tri(&ta.vtx_list[i], &ta.vtx_list[i + 1], &ta.vtx_list[i + 2]);
        }
    }
}

void core::copy_fb() {
    auto* ptr = reinterpret_cast<u8 *>(RAM);
    u32 *where = cur_output;
    ptr += (FB_R_SOF1.field << 2);
    display->cur_output_width = FB_R_SIZE.fb_x_size + 1;
    display->cur_output_height = FB_R_SIZE.fb_y_size + 1;
    static constexpr u32 px_size[4] = { 2, 2, 3, 4};
    u32 px_stride = px_size[FB_R_CTRL.fb_depth];

    u32* out;
    //printf("\nW_LINE_STRIDE x8: %d", FB_W_LINESTRIDE.line_stride * 8);
    //printf("\nR %dx%d MODULUS: %d", FB_R_SIZE.fb_x_size, FB_R_SIZE.fb_y_size, FB_R_SIZE.fb_modulus);
    for (u32 y = 0; y <= FB_R_SIZE.fb_y_size; y++) {
        //if (y < 2) printf("\nREAD LINE START %d: %08lx", y, reinterpret_cast<u8 *>(ptr) - RAM);
        out = (where + (y * 640));
        for (u32 x = 0; x <= FB_R_SIZE.fb_x_size; x++) {
            u32 r, g, b;
            u32 v;
            switch (FB_R_CTRL.fb_depth) {
                case 0: // 555
                    v = cR32(ptr, 0);
                    r = (v & 0x31) << 3;
                    g = ((v >> 5) & 0x31) << 3;
                    b = ((v >> 10) & 0x31) << 3;
                    break;
                case 1: // 565
                    v = cR32(ptr, 0);
                    r = (v & 0x31) << 3;
                    g = ((v >> 5) & 0x63) << 2;
                    b = ((v >> 11) & 0x31) << 3;
                    break;
                case 2: // 888
                    v = cR32(ptr, 0);
                    r = v & 0xFF;
                    g = (v >> 8) & 0xFF;
                    b = (v >> 16) & 0xFF;
                    break;
                case 3: // 0888
                    v = cR32(ptr, 0);
                    r = v & 0xFF;
                    g = (v >> 8) & 0xFF;
                    b = (v >> 16) & 0xFF;
                    break;
            }

            *out = (b << 0) | (g << 8) | (r << 16) | 0xFF000000;
            ptr+= px_stride;
            out++;
        }
        ptr += (FB_R_SIZE.fb_modulus - 1) * px_stride;
    }
}


void core::setpix_rgb565(i32 x, i32 y, u32 r, u32 g, u32 b) {
    // Get framebuf addr start
    if ((x < x_min) || (x > x_max) || (y < y_min) || (y > y_max)) return;
    u32 addr_start = FB_W_SOF1.u;

    // Calculate location

    // Set pixel
    u32 ir = r >> 3;
    u32 ig = g >> 2;
    u32 ib = b >> 3;
    u32 pix = (ir << 11) | (ig << 5) | ib;
    //static constexpr u32 px_size[8] = { 2, 2, 2, 2, 3, 4, 4, 4};

    u32 addr = addr_start + (y * (FB_W_LINESTRIDE.u ) * 8) + (x * 2);
    //if (y < 2) printf("\nWRITE LINE START %d: %08x", y, addr_start + (y * (FB_W_LINESTRIDE.u ) * 8));

    cW16(RAM, addr, pix);

    addr = addr_start + (y * (FB_W_LINESTRIDE.u) * 8) + (20);
    cW16(RAM, addr, pix);
}

void core::setup_setpix() {
    switch (FB_W_CTRL.fb_packmode) {
        case 1:
            setpix = &core::setpix_rgb565;
            return;
        default:
            printf("\nBAD SETPIX! %d", FB_W_CTRL.fb_packmode);
            setpix = &core::setpix_rgb565;
            return;
    }
}

void core::parse_sprite() {
    printf("\nWARN NO SPRITE PARSE");
}

// Exec TA CMD
void core::TA_cmd() {

    if (ta.cmd_buffer_index == 0) return; // No TA cmd...
    if (ta.cmd_buffer_index % 32 != 0) return; // All commands are 32 or 64 bytes long.

    // First, swap around some values!
    /*auto *vb = reinterpret_cast<u64 *>(ta.cmd_buffer + (ta.cmd_buffer_index - 32));
    for (u32 i = 0; i < 4; i++) {
        vb[i] = ((vb[i] << 32) | (vb[i] >> 32));
    }*/

    // OK *try* to parse a command.
    assert(ta.cmd_buffer_index <= 64);
    PCW cmd;
    cmd.u = cR32(ta.cmd_buffer, 0);
    if (ta.cmd_buffer_index < TA_cmd_len(cmd)) return; // Return and try again in 32 more bytes...

    //printf("\n\nTA CMD %d WORD %04x!", cmd.para_type, cmd.u);

    PCW_paratype ptype = static_cast<PCW_paratype>(cmd.para_type);
    if (cmd.para_type < 6) {
        //ta.first_gp = false;
        printf_TA_parse("\nTA LIST TYPE %d", cmd.list_type);
        ta.list_type = cmd.list_type;
        update_poly_vertex_type();
    }
    switch (cmd.para_type) {
        case 4:
        case 5:
        case 6:
            process_global_pcw(cmd);
            break;
    }
    ta.cmd_buffer_index = 0;
    switch (cmd.para_type) {
        case 0: {
            // ctrl. end of list
            printf_TA_parse("\nEND OF LIST!");
            if (!ta.vtx_list.empty()) end_parse();
            ta.vtx_list.clear();
            ta.first_gp = true;
            interruptmasks irqn = hirq_render_done_video;
            switch (ta.list_type) {
                case HPL_opaque: irqn = hirq_opaque_list; break;
                case HPL_opaque_mv: irqn = hirq_opaque_modifier_list; break;
                case HPL_punchthru: irqn = hirq_punchthru; break;
                case HPL_translucent: irqn = hirq_translucent_list; break;
                case HPL_translucent_mv: irqn = hirq_translucent_modifier_list; break;
            }
            if (irqn == hirq_render_done_video) {
                printf("\nNO IRQ FOR THIS!");
            }
            else {
                raise_interrupt(irqn, 200);
            }
            ta.list_type = HPL_none;
            return; }
        case 1: // ctrl. user_tile_clip
            printf_TA_parse("\nctrl. USER TILE CLIP");
            return;
        case 2: // ctrl. object_list_setup
            printf("\nctrl. OBJECT_LIST_SETUP MISSING");
            return;
        case 3: // ctrl. reserved
            printf("\nctrl. RESERVED MISSING");
            return;
        case 4: // global. polygon/modifier volume
            if (ta.vtx_list.size() > 0) end_parse();
            ta.vtx_list.clear();
            printf_TA_parse("\nctrl. global polygon/modifier volume");
            // finish old polygon
            // start new polygon
            ta.cur_poly.clear();
            ta.cur_poly.sprite = false;
            ta.cur_poly.pcw = cmd;
            if ((ta.list_type == 0) || (ta.list_type == 2))
                parse_polygon();
            else
                parse_mv();
            return;
        case 5: // global. sprite
            if (ta.vtx_list.size() > 0) end_parse();
            ta.vtx_list.clear();

            printf_TA_parse("\nctrl. sprite");
            ta.cur_poly.clear();
            parse_sprite();
            ta.cur_poly.sprite = true;
            return;
        case 6: // global. reserved
            printf_TA_parse("\nctrl. global reserved MISSING");
            return;
        case 7: // vertex
            printf_TA_parse("\nctrl. vertex");
            parse_vertex(cmd);
            return;
    }

    ta.cmd_buffer_index = 0;
}

void core::pvr_dma_init(u64 val)
{
    if (val & 1 & SB_PDEN)
        printf("\nPVR PALETTE DMA!?!?!?");
}

void core::pvr_dma_irq_trigger() {
    printf("\nPVR DMA IRQ TRIGGER!?");
}

void core::start_render() {
    if (true) {
        printf("\n\nRENDER FRAME!");
        printf("\nOpaque polys: %ld", ta.poly_lists[0].size());
        printf("\nOpaque MVs: %ld", ta.poly_lists[1].size());
        printf("\nTranslucent polys: %ld", ta.poly_lists[2].size());
        printf("\nTranslucent MVs: %ld", ta.poly_lists[3].size());
        printf("\nPunch-through polys: %ld", ta.poly_lists[4].size());
        printf("\nSprites: %ld", ta.poly_lists[5].size());
    }

    setup_setpix();
    for (auto & p : ta.poly_lists[0]) {
        //printf("\n\nPOLYGON. VTX:%d", p.num_vtx);
        for (u32 i = 0; i < p.num_vtx; i++) {
            auto &v = p.vtx[i];
            //printf("\nXYZ %f  %f  %f", v.x, v.y, v.z);
            //printf("\nRGBA %02x  %02x  %02x  %02x", v.r, v.g, v.b, v.a);
            draw_tri(&p.vtx[0], &p.vtx[1], &p.vtx[2]);
        }
    }

    raise_interrupt(hirq_render_done_tsp, 200);
    raise_interrupt(hirq_render_done_isp, 400);
    raise_interrupt(hirq_render_done_video, 600);
}

void core::update_dma_trigger(u64 val) {
    printf("\nUPDATE HOLLY PALLETE DMA TRIGGER %lld", val);
    SB_PDTSEL = (val & 1);
}

void core::TA_list_init() {
    printf("\nTA LIST INIT!");

    if (!TA_LIST_CONT) {
        TA_NEXT_OPB = TA_NEXT_OPB_INIT;
        TA_ITP_CURRENT = TA_ISP_BASE;
    }
    for (auto & l : ta.poly_lists) {
        l.clear();
    }

    ta.first_gp = true;
    ta.cmd_buffer_index = 0;
    ta.list_type = HPL_none;
    ta.cur_poly.num_vtx = 0;
}

    void core::write_ta_fifo(u32 addr, u8 sz, u64 val, bool *success) {
    auto push32 = [&](u32 v) {
        cW32(ta.cmd_buffer, ta.cmd_buffer_index, v);
        ta.cmd_buffer_index += 4;

        if ((ta.cmd_buffer_index & 0x1F) == 0) {
            TA_cmd();
        }

        assert(ta.cmd_buffer_index < sizeof(ta.cmd_buffer));
    };

    if (sz == 4) {
        push32(static_cast<u32>(val));
        return;
    }

    if (sz == 8) {
        push32(static_cast<u32>(val));
        push32(static_cast<u32>(val >> 32));
        return;
    }

    // Optional: handle 1/2 if you want, but TA input should normally be 4/8.
    printf("\nWARN TA FIFO write size %d addr=%08x val=%08llx", sz, addr, val);
}

void core::write_io(u32 addr, u8 sz, u64 val, bool *success) {

    addr = (addr & 0x0000FFFF) | 0x005F0000;
    switch (addr) {
        case 0x005F7C00: { SB_PDSTAP = (val & 0x0FFFFFE0); return; }
        case 0x005F7C04: { SB_PDSTAR = (val & 0x0FFFFFE0); return; }
        case 0x005F7C08: { SB_PDSLEN = (val & 0x00FFFFE0); return; }
        case 0x005F7C0C: { SB_PDDIR = (val & 1); return; }
        case 0x005F7C10: { update_dma_trigger(val); return; }
        case 0x005F7C14: { SB_PDEN = (val & 1); return; }
        case 0x005F7C18: { SB_PDST = (val & 1); pvr_dma_init(val); return; }
        case 0x005F7C80: { if ((val >> 16) == 0x6702) { SB_PDAPRO.u = val & 0x00007F7F; } return; }
        case 0x005F802C: REGION_BASE = val & 0x00FF'FFFC; return;
        case 0x005F8020: PARAM_BASE = val & 0x00F0'0000; return;
        case 0x005F8008:
            soft_reset();
            return;
        case 0x005F8014:
            start_render();
            return;
        case 0x005f8144: // TA_LIST_INIT
            if (val & 0x80000000) {
                TA_list_init();
            }
            return;
        case 0x005f8078: // FPU_CULL_VAL (float)
            FPU_CULL_VAL.u = val;
            return;
        case 0x005f8084: // FPU_PERP_VAL (float)
            FPU_PERP_VAL.u = val;
            return;
        case 0x005f8088: // ISP_BACKGND_D (float)
            ISP_BACKGND_D.u = val;
            return;

        case 0x005F8030: { SPAN_SORT_CFG = val; return; }
        case 0x005F8040: { VO_BORDER_COL.u = val & 0x01FFFFFF; return; }
        case 0x005F8044: { FB_R_CTRL.u = val & 0x00FFFF7F; return; }
        case 0x005F8048: { FB_W_CTRL.u = val & 0x00FFFF0F; return; }
        case 0x005F804C: { FB_W_LINESTRIDE.u = val & 0x000001FF; return; }
        case 0x005F8050: { FB_R_SOF1.u = val & 0x00FFFFFC; return; }
        case 0x005F8054: { FB_R_SOF2.u = val & 0x00FFFFFC; return; }
        case 0x005F805C: { FB_R_SIZE.u = val & 0x3FFFFFFF; return; }
        case 0x005F8060: { FB_W_SOF1.u = val & 0x01FFFFFC; return; }
        case 0x005F8064: { FB_W_SOF2.u = val & 0x01FFFFFC; return; }
        case 0x005F8068: { FB_X_CLIP.u = val & 0x07FF07FF; x_min = FB_X_CLIP.min; x_max = FB_X_CLIP.max;
            //printf("\nCLIP X %f %f", x_min, x_max);
            return; }
        case 0x005F806C: { FB_Y_CLIP.u = val & 0x03FF03FF; y_min = FB_Y_CLIP.min; y_max = FB_Y_CLIP.max;
            //printf("\nCLIP Y %f %f", y_min, y_max);
            return; }
        case 0x005F8074: { FPU_SHAD_SCALE.u = val & 0x000001FF; return; }
        case 0x005F807C: { FPU_PARAM_CFG.u = val & 0x002FFFFF; return; }
        case 0x005F8080: { HALF_OFFSET.u = val & 0x00000007; return; }
        case 0x005F808C: { ISP_BACKGND_T.u = val & 0x1FFFFFFF; return; }
        case 0x005F8098: { ISP_FEED_CFG.u = val & 0x00FFFFF9; return; }
        case 0x005F80A0: { SDRAM_REFRESH = (val & 0x000000FF); return; }
        case 0x005F80A8: { SDRAM_CFG = val; return; }
        case 0x005F80B0: { FOG_COL_RAM = val; return; }
        case 0x005F80B4: { FOG_COL_VERT.u = val & 0x00FFFFFF; return; }
        case 0x005F80B8: { FOG_DENSITY.u = val & 0x0000FFFF; return; }
        case 0x005F80BC: { FOG_CLAMP_MAX = val; return; }
        case 0x005F80C0: { FOG_CLAMP_MIN = val; return; }
        case 0x005F80C8: { SPG_HBLANK_INT.u = val & 0x03FF33FF; return; }
        case 0x005F80CC: { SPG_VBLANK_INT.u = val & 0x03FF03FF; recalc_frame_timing()/*; printf("\nSPG_VBLANK_INT wrote: %08llx", val)*/; return; }
        case 0x005F80D0: { SPG_CONTROL.u = val & 0x000003FF; return; }
        case 0x005F80D4: { SPG_HBLANK.u = val & 0x03FF03FF; return; }
        case 0x005F80D8: { SPG_LOAD.u = val & 0x03FF03FF; recalc_frame_timing(); return; }
        case 0x005F80DC: { SPG_VBLANK.u = val & 0x03FF03FF; return; }
        case 0x005F80E0: { SPG_WIDTH.u = val & 0xFFFFFF7F; return; }
        case 0x005F80E4: { TEXT_CONTROL.u = val & 0x00031F1F; return; }
        case 0x005F80E8: { VO_CONTROL.u = val & 0x003F01FF; return; }
        case 0x005F80EC: { VO_STARTX = (val & 0x000003FF); return; }
        case 0x005F80F0: { VO_STARTY.u = val & 0x03FF03FF; return; }
        case 0x005F80F4: { SCALER_CTL.u = val & 0x0007FFFF; return; }
        case 0x005F8110: { FB_BURSTCTRL.u = val & 0x0007FF3F; return; }
        case 0x005F8118: { Y_COEFF.u = val & 0x0000FFFF; return; }
        case 0x005F8124: { TA_OL_BASE.u = val & 0x00FFFFE0; return; }
        case 0x005F8128: { TA_ISP_BASE = (val & 0x00FFFFFC); return; }
        case 0x005F812C: { TA_OL_LIMIT.u = val & 0x00FFFFE0; return; }
        case 0x005F8130: { TA_ISP_LIMIT.u = val & 0x00FFFFFC; return; }
        case 0x005F813C: { TA_GLOB_TILE_CLIP.u = val & 0x000F003F; return; }
        case 0x005F8140: { TA_ALLOC_CTRL.u = val & 0x00130333; return; }
        case 0x005F8160: {
            if (val & 0x80000000) {
                ta.first_gp = true;
            }
            TA_LIST_CONT = (val >> 31) & 1;
            return;
        }
        case 0x005F8164: { TA_NEXT_OPB_INIT = (val & 0x00FFFFE0); return; }
    }
    if ((addr >= 0x005F8200) && (addr <= 0x005F83FC)) {
        FOG_TABLE[(addr - 0x005F8200) >> 2] = val;
        return;
    }

    *success = false;
    printf("\nHOLLY write bad reg %08x(%d): %08llx", addr, sz, val);
}

u64 core::schedule_frame(scheduler_callback vblankfunc, u64 cur_clock) {
    bus->scheduler.only_add_abs(cur_clock, 0, bus, vblankfunc, nullptr);
    for (u32 line = 0; line < SPG_LOAD.vcount; line++) {
        if (line == SPG_VBLANK_INT.vblank_out_line) {
            bus->scheduler.only_add_abs(cur_clock, 0, bus, vblankfunc, nullptr);
        }
        if (line == SPG_VBLANK_INT.vblank_in_line) {
            bus->scheduler.only_add_abs(cur_clock, 1, bus, vblankfunc, nullptr);
        }

        //scheduler.only_add_abs(cur_clock + (CYCLES_PER_SCANLINE - HBLANK_CYCLES), 1, this, &DREAMCAST::hblank, nullptr);
        cur_clock += timing.cycles_per_line;
    }
    return cur_clock;
}

void core::recalc_frame_timing() {
    // We need to know:
    // kind line to vblank in IRQ. cycle # in frame
    // kind line to vblank out IRQ. cycle # in frame
    // how many cycles per line
    // how many lines in frame
    //clock.cycles_per_frame;
    if (!SPG_LOAD.vcount || !SPG_VBLANK_INT.vblank_in_line || !SPG_VBLANK_INT.vblank_out_line) {
        printf("\nERROR UNDERFLOW! MAKE SOME REASONABLE DEFAULTS!!");
    }

    timing.cycles_per_line = timing.cycles_per_frame / SPG_LOAD.vcount;
    timing.interrupt.vblank_in_start = SPG_VBLANK_INT.vblank_in_line * timing.cycles_per_line;
    timing.interrupt.vblank_out_start = SPG_VBLANK_INT.vblank_out_line * timing.cycles_per_line;
}

}
