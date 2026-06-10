#include <cstdio>
#include <cstring>
#include "ngp_bus.h"


namespace KXGE {

static void sch_scanline(void *ptr, u64 key, u64 clock, u32 jitter) {
    static_cast<core *>(ptr)->schedule_scanline(key, clock, jitter);
}

static void sch_new_frame(void *ptr, u64 key, u64 clock, u32 jitter) {
    static_cast<core *>(ptr)->new_frame();
}

void core::schedule_first() {
    cur_line = 0;
    schedule_scanline(0, scheduler->current_time(), 0);
}

void core::schedule_scanline(u64 line, u64 clock, u32 jitter) {
    cur_line = static_cast<u32>(line);
    line_start_clock = clock - jitter;

    new_line();

    if (line <= HBLANK_LAST_VISIBLE || line == io.ref) issue_hblank();

    io.blnk = (line >= DISP_HEIGHT);

    if (line == 0) {
        if (cpu) cpu->intc.set_line(TMP95C061::IRQ_INT4, 1);
        vblank_fired_this_frame = false;
    }

    if (line == static_cast<u32>((io.wba_v + io.wsi_v))) try_vblank_fire();
    if (line == DISP_HEIGHT) try_vblank_fire();

    const u64 base = clock - jitter;

    const u64 next = (line + 1 >= DISP_TOTAL_HEIGHT) ? 0 : line + 1;
    scheduler->only_add_abs_w_tag(static_cast<i64>((base + DISP_LINE_CYCLES)), next, this, &sch_scanline, nullptr, 1);

    if (line == 0)
        scheduler->only_add_abs_w_tag(static_cast<i64>((base + FRAME_CYCLES)), 0, this, &sch_new_frame, nullptr, 2);
}

void core::issue_hblank() {
    if (io.hint_enable && cpu) cpu->hblank();
}

void core::try_vblank_fire() {
    if (vblank_fired_this_frame) return;
    vblank_fired_this_frame = true;
    issue_vblank();
}

void core::issue_vblank() {
    if (cpu) cpu->intc.set_line(TMP95C061::IRQ_INT4, io.vint_enable ? 0 : 1);
}

void core::new_line() {
    if (cur_line >= 1 && cur_line <= DISP_HEIGHT) draw_line(cur_line - 1);
}

void core::new_frame() {
    frame_num++;
    if (display_ptr.vec) {
        auto &disp = display_ptr.get().display;
        disp.active_draw_buffer ^= 1;
        cur_output = static_cast<u16 *>(disp.output[disp.active_draw_buffer]);
    }
}

void core::init_io_regs() {
    io = {};
    io.wsi_h = DISP_WIDTH;
    io.wsi_v = DISP_HEIGHT;
    io.ref = 0xC6;
    io.ledon = 0xFF;
    io.ledfrq = 0x80;
    io.hint_enable = false;
    io.vint_enable = true;
}

void core::reset() {
    cur_line = 0;
    line_start_clock = 0;
    frame_num = 0;
    init_io_regs();
}

u8 core::read(u32 addr) {
    addr = 0x8000 | (addr & 0x3FFF);

    if (addr >= 0x8200 && addr <= 0x83FF) {
        if (variant != K2GE) return 0x00;
        u32 i = (addr - 0x8200) >> 1;
        return (addr & 1) ? static_cast<u8>(((CRAM[i] >> 8) & 0x0F)) : static_cast<u8>((CRAM[i] & 0xFF));
    }
    if (addr >= 0x8800 && addr <= 0x88FF) return SPRAM[addr - 0x8800];
    if (addr >= 0x8C00 && addr <= 0x8C3F) {
        if (variant != K2GE) return 0x00;
        u32 s = addr & 0x3F;
        u8 b = SPRAM[256 + (s >> 1)];
        return (s & 1) ? static_cast<u8>(((b >> 4) & 0x0F)) : static_cast<u8>((b & 0x0F));
    }
    if (addr >= 0x9000 && addr <= 0x9FFF) return VRAM[addr - 0x9000];
    if (addr >= 0xA000 && addr <= 0xBFFF) return CHR_RAM[addr - 0xA000];

    if (addr >= 0x8100 && addr <= 0x8117) {
        u32 r = addr - 0x8100;
        auto &lut = (r < 8) ? io.spplt : (r < 16) ? io.sc1plt : io.sc2plt;
        u32 e = r & 7;
        return lut[e >> 2][e & 3] & 7;
    }

    switch (addr) {
        case 0x8000: return (io.hint_enable << 6) | (io.vint_enable << 7);
        case 0x8002: return io.wba_h;
        case 0x8003: return io.wba_v;
        case 0x8004: return io.wsi_h;
        case 0x8005: return io.wsi_v;
        case 0x8006: return io.ref;
        case 0x8008: {
            u64 elapsed = scheduler ? scheduler->current_time() - line_start_clock : 0;
            i64 rem = static_cast<i64>(DISP_LINE_CYCLES) - static_cast<i64>(elapsed);
            if (rem < 0) rem = 0;
            return static_cast<u8>(((rem >> 2) & 0xFF));
        }
        case 0x8009: return static_cast<u8>(cur_line);
        case 0x8010: return (io.blnk << 6) | (io.c_ovr << 7);
        case 0x8012: return (io.oowc & 7) | ((io.ctrl_dc & 0xF) << 3) | (io.neg << 7);
        case 0x8020: return io.po_h;
        case 0x8021: return io.po_v;
        case 0x8030: return io.pf << 7;
        case 0x8032: return io.s1so_h;
        case 0x8033: return io.s1so_v;
        case 0x8034: return io.s2so_h;
        case 0x8035: return io.s2so_v;
        case 0x8118:
            return (variant == K2GE) ? ((io.bgc & 7) | ((io.bg_dc & 7) << 3) | ((io.bgon & 3) << 6)) : 0x00;
        case 0x8400: return (io.ledon & 0xF8) | 0x07;
        case 0x8402: return io.ledfrq;
        case 0x87e2: return (variant == K2GE) ? (io.mode << 7) : 0x00;
        case 0x87fe: return 0x3F;
        default: return 0x00;
    }
}

void core::write(u32 addr, u8 data) {
    addr = 0x8000 | (addr & 0x3FFF);

    if (addr >= 0x8200 && addr <= 0x83FF) {
        if (variant != K2GE) return;
        u32 i = (addr - 0x8200) >> 1;
        if (addr & 1) CRAM[i] = (CRAM[i] & 0x00FF) | (static_cast<u16>((data & 0x0F)) << 8);
        else CRAM[i] = (CRAM[i] & 0xFF00) | data;
        return;
    }
    if (addr >= 0x8800 && addr <= 0x88FF) { SPRAM[addr - 0x8800] = data; return; }
    if (addr >= 0x8C00 && addr <= 0x8C3F) {
        if (variant != K2GE) return;
        u32 s = addr & 0x3F;
        u8 &b = SPRAM[256 + (s >> 1)];
        if (s & 1) b = (b & 0x0F) | ((data & 0x0F) << 4);
        else b = (b & 0xF0) | (data & 0x0F);
        return;
    }
    if (addr >= 0x9000 && addr <= 0x9FFF) { VRAM[addr - 0x9000] = data; return; }
    if (addr >= 0xA000 && addr <= 0xBFFF) { CHR_RAM[addr - 0xA000] = data; return; }

    if (addr >= 0x8100 && addr <= 0x8117) {
        u32 r = addr - 0x8100;
        auto &lut = (r < 8) ? io.spplt : (r < 16) ? io.sc1plt : io.sc2plt;
        u32 e = r & 7;
        if ((e & 3) != 0) lut[e >> 2][e & 3] = data & 7;
        return;
    }

    switch (addr) {
        case 0x8000: io.hint_enable = (data >> 6) & 1; io.vint_enable = (data >> 7) & 1; return;
        case 0x8002: io.wba_h = data; return;
        case 0x8003: io.wba_v = data; return;
        case 0x8004: io.wsi_h = data; return;
        case 0x8005: io.wsi_v = data; return;
        case 0x8006:
            io.ref = data;
            if (io.ref + 1u != DISP_TOTAL_HEIGHT)
                printf("NGP KxGE: REF+1 == %u (expected %u) -- frame/audio timing will be wrong!\n",
                       io.ref + 1u, DISP_TOTAL_HEIGHT);
            return;
        case 0x8012: io.oowc = data & 7; io.ctrl_dc = (data >> 3) & 0xF; io.neg = (data >> 7) & 1; return;
        case 0x8020: io.po_h = data; return;
        case 0x8021: io.po_v = data; return;
        case 0x8030: io.pf = (data >> 7) & 1; return;
        case 0x8032: io.s1so_h = data; return;
        case 0x8033: io.s1so_v = data; return;
        case 0x8034: io.s2so_h = data; return;
        case 0x8035: io.s2so_v = data; return;
        case 0x8118:
            io.bgc = data & 7; io.bg_dc = (data >> 3) & 7; io.bgon = (data >> 6) & 3; return;
        case 0x8400: io.ledon = (data & 0xF8) | 0x07; return;
        case 0x8402: io.ledfrq = data; return;
        case 0x87e0: if (data == 0x52) init_io_regs(); return;
        case 0x87e2: if (variant == K2GE) io.mode = (data >> 7) & 1; return;
        default: return;
    }
}

u8 core::chr_dot(u32 ch, u32 row, u32 col) const {
    u32 base = (ch & 0x1FF) * 16 + (row & 7) * 2;
    u8 byte = (col & 4) ? CHR_RAM[base] : CHR_RAM[base + 1];
    return (byte >> ((3 - (col & 3)) * 2)) & 3;
}

void core::draw_line(u32 y) {
    u16 *out = cur_output;
    if (!out) return;
    out += y * DISP_WIDTH;

    struct tile { u8 x, row, prc, pc, cpc, hf; u16 cc; };
    tile tiles[64];
    u32 ntiles = 0;
    u8 px = 0, py = 0;
    for (u32 i = 0; i < 64; i++) {
        const u8 b0 = SPRAM[i * 4 + 0];
        const u8 b1 = SPRAM[i * 4 + 1];
        u8 ox = SPRAM[i * 4 + 2];
        u8 oy = SPRAM[i * 4 + 3];
        if ((b1 >> 2) & 1) ox += px;
        if ((b1 >> 1) & 1) oy += py;
        px = ox; py = oy;
        const u8 prc = (b1 >> 3) & 3;
        if (!prc) continue;
        const u8 sx = static_cast<u8>((ox + io.po_h));
        u8 row = static_cast<u8>((y - static_cast<u8>((oy + io.po_v))));
        if (row >= 8) continue;
        if ((b1 >> 6) & 1) row ^= 7;
        const u8 cb = SPRAM[256 + (i >> 1)];
        tile &t = tiles[ntiles++];
        t.x = sx; t.row = row;
        t.cc = static_cast<u16>((((b1 & 1) << 8) | b0));
        t.prc = prc;
        t.pc = (b1 >> 5) & 1;
        t.cpc = (i & 1) ? (cb >> 4) & 0xF : cb & 0xF;
        t.hf = (b1 >> 7) & 1;
    }

    auto plane_dot = [&](u32 plane, u32 sx_in, u32 sy_in, u8 &pc, u8 &cpc) -> u8 {
        const u8 sx = static_cast<u8>((sx_in + (plane ? io.s2so_h : io.s1so_h)));
        const u8 sy = static_cast<u8>((sy_in + (plane ? io.s2so_v : io.s1so_v)));
        const u32 off = (plane ? 0x800u : 0u) + (((sy >> 3) & 31) * 32 + ((sx >> 3) & 31)) * 2;
        const u8 b0 = VRAM[off], b1 = VRAM[off + 1];
        u8 fx = sx & 7, fy = sy & 7;
        if ((b1 >> 7) & 1) fx ^= 7;
        if ((b1 >> 6) & 1) fy ^= 7;
        pc = (b1 >> 5) & 1;
        cpc = (b1 >> 1) & 0xF;
        return chr_dot(static_cast<u16>((((b1 & 1) << 8) | b0)), fy, fx);
    };

    const bool bg_valid = !(io.bgon & 1);
    const u32 bg = (variant != K2GE) ? (bg_valid ? io.bgc : 0)
                                     : (bg_valid ? CRAM[0xF0 + io.bgc] : 0);
    const u32 oowc = (variant != K2GE) ? io.oowc : CRAM[0xF8 + io.oowc];
    const u8 pf = io.pf;
    const bool win_y = (y >= io.wba_v) && (static_cast<u32>(y) < static_cast<u32>(io.wba_v) + io.wsi_v);

    const bool do_dbg = ::dbg.do_debug;
    if (do_dbg && y == 0) {
        memset(dbg_plane_seen[0], 0, sizeof(dbg_plane_seen[0]));
        memset(dbg_plane_seen[1], 0, sizeof(dbg_plane_seen[1]));
    }

    for (u32 x = 0; x < DISP_WIDTH; x++) {
        bool s_op = false; u8 s_prc = 0, s_pc = 0, s_cpc = 0, s_idx = 0;
        for (u32 t = 0; t < ntiles; t++) {
            u8 tx = static_cast<u8>((x - tiles[t].x));
            if (tx >= 8) continue;
            if (tiles[t].hf) tx ^= 7;
            const u8 idx = chr_dot(tiles[t].cc, tiles[t].row, tx);
            if (idx) { s_op = true; s_prc = tiles[t].prc; s_pc = tiles[t].pc; s_cpc = tiles[t].cpc; s_idx = idx; break; }
        }

        u8 p1_pc, p1_cpc, p2_pc, p2_cpc;
        const u8 p1 = plane_dot(0, x, y, p1_pc, p1_cpc);
        const u8 p2 = plane_dot(1, x, y, p2_pc, p2_cpc);

        u32 color = bg;
        if (s_op && s_prc == 1) color = resolve_dot(0, s_pc, s_cpc, s_idx);
        if (pf == 0) { if (p2) color = resolve_dot(2, p2_pc, p2_cpc, p2); }
        else { if (p1) color = resolve_dot(1, p1_pc, p1_cpc, p1); }
        if (s_op && s_prc == 2) color = resolve_dot(0, s_pc, s_cpc, s_idx);
        if (pf == 0) { if (p1) color = resolve_dot(1, p1_pc, p1_cpc, p1); }
        else { if (p2) color = resolve_dot(2, p2_pc, p2_cpc, p2); }
        if (s_op && s_prc == 3) color = resolve_dot(0, s_pc, s_cpc, s_idx);

        const bool inside = win_y && (x >= io.wba_h) && (x < static_cast<u32>(io.wba_h) + io.wsi_h);
        if (!inside) color = oowc;

        if (variant != K2GE && io.neg) color ^= 7;
        out[x] = static_cast<u16>(color);

        if (do_dbg) {
            dbg_plane_out[0][y * DISP_WIDTH + x] = p1 ? static_cast<u16>(resolve_dot(1, p1_pc, p1_cpc, p1)) : DBG_TRANSPARENT;
            dbg_plane_out[1][y * DISP_WIDTH + x] = p2 ? static_cast<u16>(resolve_dot(2, p2_pc, p2_cpc, p2)) : DBG_TRANSPARENT;
            const u32 m0 = (((y + io.s1so_v) & 0xFF) << 8) | ((x + io.s1so_h) & 0xFF);
            const u32 m1 = (((y + io.s2so_v) & 0xFF) << 8) | ((x + io.s2so_h) & 0xFF);
            dbg_plane_seen[0][m0 >> 3] |= 1 << (m0 & 7);
            dbg_plane_seen[1][m1 >> 3] |= 1 << (m1 & 7);
        }
    }
}

u32 core::resolve_dot(int kind, u8 pc, u8 cpc, u8 index) const {
    const u8 (*lut)[4] = (kind == 0) ? io.spplt : (kind == 1) ? io.sc1plt : io.sc2plt;
    if (variant != K2GE) return lut[pc][index];
    if (io.mode == 0) {
        const u32 region = (kind == 0) ? 0u : (kind == 1) ? 64u : 128u;
        return CRAM[region + cpc * 4 + index];
    }
    const u32 region = (kind == 0) ? 0xC0u : (kind == 1) ? 0xD0u : 0xE0u;
    return CRAM[region + pc * 8 + lut[pc][index]];
}

u32 core::color_to_rgba(u16 color) const {
    u32 r, g, b;
    if (variant == K2GE) {
        r = (color >> 0) & 0xF; g = (color >> 4) & 0xF; b = (color >> 8) & 0xF;
        r = (r << 4) | r; g = (g << 4) | g; b = (b << 4) | b;
    } else {
        u32 s = color & 7;
        r = g = b = ((7 - s) * 255) / 7;
    }
    return 0xFF000000u | (b << 16) | (g << 8) | r;
}

void core::dbg_render_plane_iso(u32 plane, u32 *outbuf, u32 out_width) {
    if (!outbuf) return;
    plane &= 1;
    const u16 *src = dbg_plane_out[plane];
    for (u32 y = 0; y < DISP_HEIGHT; y++) {
        u32 *line = outbuf + y * out_width;
        for (u32 x = 0; x < DISP_WIDTH; x++) {
            u16 v = src[y * DISP_WIDTH + x];
            line[x] = (v == DBG_TRANSPARENT) ? 0xFF000000u : color_to_rgba(v);
        }
    }
}

void core::dbg_render_chrmap(u32 plane, u32 *outbuf, u32 out_width) {
    if (!outbuf) return;
    plane &= 1;
    const u32 base = plane ? 0x800u : 0u;
    const int kind = plane ? 2 : 1;
    for (u32 my = 0; my < 256; my++) {
        u32 *line = outbuf + my * out_width;
        for (u32 mx = 0; mx < 256; mx++) {
            const u32 off = base + ((my >> 3) * 32 + (mx >> 3)) * 2;
            const u8 b0 = VRAM[off], b1 = VRAM[off + 1];
            u8 fx = mx & 7, fy = my & 7;
            if ((b1 >> 7) & 1) fx ^= 7;
            if ((b1 >> 6) & 1) fy ^= 7;
            const u8 idx = chr_dot(static_cast<u16>((((b1 & 1) << 8) | b0)), fy, fx);
            u32 c = idx ? color_to_rgba(static_cast<u16>(resolve_dot(kind, (b1 >> 5) & 1, (b1 >> 1) & 0xF, idx)))
                        : 0xFF000000u;
            const u32 m = (my << 8) | mx;
            if (!((dbg_plane_seen[plane][m >> 3] >> (m & 7)) & 1)) {
                u32 r = ((c >> 0) & 0xFF) * 5 / 16;
                u32 g = ((c >> 8) & 0xFF) * 5 / 16;
                u32 b = ((c >> 16) & 0xFF) * 5 / 16;
                c = 0xFF000000u | (b << 16) | (g << 8) | r;
            }
            line[mx] = c;
        }
    }
}

void core::dbg_render_sprites(u32 *outbuf, u32 out_width) {
    if (!outbuf) return;
    for (u32 y = 0; y < 256; y++) {
        u32 *line = outbuf + y * out_width;
        const bool yin = (y < DISP_HEIGHT);
        for (u32 x = 0; x < 256; x++)
            line[x] = (yin && x < DISP_WIDTH) ? 0xFF303030u : 0xFF121218u;
    }
    u8 px = 0, py = 0;
    for (u32 i = 0; i < 64; i++) {
        const u8 b0 = SPRAM[i * 4 + 0];
        const u8 b1 = SPRAM[i * 4 + 1];
        u8 ox = SPRAM[i * 4 + 2];
        u8 oy = SPRAM[i * 4 + 3];
        if ((b1 >> 2) & 1) ox += px;
        if ((b1 >> 1) & 1) oy += py;
        px = ox; py = oy;
        const u16 cc = static_cast<u16>((((b1 & 1) << 8) | b0));
        const u8 pc = (b1 >> 5) & 1;
        const u8 cb = SPRAM[256 + (i >> 1)];
        const u8 cpc = (i & 1) ? (cb >> 4) & 0xF : cb & 0xF;
        const bool hf = (b1 >> 7) & 1, vf = (b1 >> 6) & 1;
        const u8 sx = static_cast<u8>((ox + io.po_h));
        const u8 sy = static_cast<u8>((oy + io.po_v));
        for (u32 row = 0; row < 8; row++) {
            const u8 frow = vf ? static_cast<u8>((row ^ 7)) : static_cast<u8>(row);
            const u32 dy = (sy + row) & 0xFF;
            u32 *line = outbuf + dy * out_width;
            for (u32 col = 0; col < 8; col++) {
                const u8 fcol = hf ? static_cast<u8>((col ^ 7)) : static_cast<u8>(col);
                const u8 idx = chr_dot(cc, frow, fcol);
                if (!idx) continue;
                const u32 dx = (sx + col) & 0xFF;
                u32 c = color_to_rgba(static_cast<u16>(resolve_dot(0, pc, cpc, idx)));
                if (!(dx < DISP_WIDTH && dy < DISP_HEIGHT)) {
                    u32 r = ((c >> 0) & 0xFF) * 5 / 16, g = ((c >> 8) & 0xFF) * 5 / 16, b = ((c >> 16) & 0xFF) * 5 / 16;
                    c = 0xFF000000u | (b << 16) | (g << 8) | r;
                }
                line[dx] = c;
            }
        }
    }
}

}
