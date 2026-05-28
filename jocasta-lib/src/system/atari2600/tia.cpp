//
// Created by . on 4/14/24.
//

#include <cstring>
#include <cassert>

#include "tia.h"
#include "atari2600_bus.h"
#include "atari2600_debugger.h"

#define dprintf(...) (void)(0)
namespace atari2600 {
void TIA::reset()
{
    hcounter = 0;
    pcounter = 0;
    vcounter = 0;
    clock_div = 0;
    x = 0;
    hblank = 1;
    hsync = 0;
    cpu_RDY = 0;
    missed_vblank = 0;
    frames_since_restart = 0;
    io.vblank = io.vsync = 0;
    for (u32 i = 0; i < TIA_WQ_MAX; i++) {
        write_queue[i].active = 0;
    }
}

#define IN_VBLANK_IN (vcounter < timing.display_line_start)
#define IN_DISPLAY ((vcounter >= timing.display_line_start) && (vcounter < timing.vblank_out_start))
#define IN_VBLANK_OUT (sy >= timing.vblank_out_start)
#define OUT_OF_FRAME (vcounter > (timing.vblank_out_lines + timing.vblank_out_start))

void TIA::flip_buffer()
{
    // Update meta state, so emulator knows we have completed a frame
    master_frame++;
    frames_since_restart++;

    // Flip framebuffer
    display->active_draw_buffer ^= 1;
    cur_output = static_cast<u8 *>(display->output[display->active_draw_buffer]);
}

void TIA::WQ_add(u32 num, u32 val, u32 delay)
{
    for (u32 i = 0; i < TIA_WQ_MAX; i++) {
        if (write_queue[i].active == 0) {
            write_queue[i].active = 1;
            write_queue[i].delay = delay;
            write_queue[i].data = val;
            write_queue[i].address = num;
            return;
        }
    }
    printf("\nWRITE QUEUE FULL! ERROR!");
}


// leaned heavy on Ares for this p_width, p_start and p_step logic, and m_ and ball_ versions.
// it's so different from docs... didn't get it til I implemented it
u32 TIA::m_width(u32 num)
{
    return 1 << m[num].size;
}

void TIA::m_start(u32 num)
{
    m[num].start_counter = 4;
    m[num].starting = 1;
}

void TIA::m_step(u32 num, u32 clocks)
{
    while (clocks--) {
        m[num].counter++;

        if (m[num].counter == 156) m_start(num);
        if (m[num].counter == 160) m[num].counter = 0;

        if (m[num].starting && (m[num].start_counter-- == 0)) {
            m[num].starting = 0;
            m[num].pixel_counter = 1;
            m[num].width_counter = (i32)m_width(num);
        }

        m[num].output = 0;
        if (m[num].pixel_counter) {
            if (--m[num].width_counter == 0) {
                m[num].pixel_counter--;
                m[num].width_counter = (i32)m_width(num);
            }
            m[num].output = m[num].enable;
        }
    }
}

u32 TIA::p_width(u32 num)
{
    switch(P[num].size) {
        case 5:
            return 2;
        case 7:
            return 4;
        default:
            return 1;
    }
}

void TIA::p_start(u32 num, u32 copy)
{
    P[num].copy = copy;
    P[num].start_counter = 5;
    P[num].starting = 1;
}


void TIA::p_step(u32 num, u32 clocks)
{
    u32 size = P[num].size;
    if (!clocks) return;
    while (clocks--) {
        P[num].counter++;
        u32 first  = size == 1 || size == 3;
        u32 second = size == 2 || size == 3 || size == 6;
        u32 third  = size == 4 || size == 6;

        if (first && P[num].counter == 12) p_start(num, true);
        if (second && P[num].counter == 28) p_start(num, true);
        if (third && P[num].counter == 60) p_start(num, true);
        if (P[num].counter == 156) p_start(num, false);
        if (P[num].counter == 160) P[num].counter = 0;

        if (P[num].starting && (P[num].start_counter-- == 0)) {
            P[num].starting = 0;
            P[num].pixel_counter = 8;
            P[num].width_counter = (i32)p_width(num);
        }

        P[num].output = 0;
        if (P[num].pixel_counter) {
            if (--P[num].width_counter == 0) {
                P[num].pixel_counter--;
                P[num].width_counter = (i32)p_width(num);
            }
            if (!P[num].copy && m[num].locked_to_player && P[num].pixel_counter == 4) {
                m[num].counter = -4;
            }
            u32 bnum = P[num].reflect ? (7 - P[num].pixel_counter) : P[num].pixel_counter;
            P[num].output = (P[num].GRP[P[num].delay] >> bnum) & 1;
        }

    }
}

void TIA::ball_step(u32 clocks)
{
    if (!clocks) return;
    while (clocks--) {
        if (++ball.counter == 160) ball.counter = 0;

        ball.output = ball.enable[ball.delay] && ball.counter < (1 << ball.size);
    }
}

void TIA::WQ_finish(WQ_item* item)
{
    item->active = 0;
    u32 val = item->data;
    switch(item->address) {
        case 0x01: // VBLANK
            io.vblank = (val >> 1) & 1;
            io.inpt_4_5_ctrl = (val >> 6) & 1;
            io.inpt_0_3_ctrl = (val >> 7) & 1;
            return;
        case 0x0B: // REFP0 reflect player 0
            P[0].reflect = (val >> 3) & 1;
            return;
        case 0x0C: // REFP1 reflect player 1
            P[1].reflect = (val >> 3) & 1;
            return;
        case 0x0D: // PF0 playfield register byte 0 — bits 4-7 = pf[16-19]
            io.pf &= ~(0xF << 16);
            io.pf |= ((val >> 4) & 0xF) << 16;
            return;
        case 0x0E: // PF1 playfield register byte 1 — reversed into pf[8-15]: PF1.7=15, PF1.0...8
            io.pf &= ~(0xFF << 8);
            io.pf |= ((val >> 7) & 1) << 15;
            io.pf |= ((val >> 6) & 1) << 14;
            io.pf |= ((val >> 5) & 1) << 13;
            io.pf |= ((val >> 4) & 1) << 12;
            io.pf |= ((val >> 3) & 1) << 11;
            io.pf |= ((val >> 2) & 1) << 10;
            io.pf |= ((val >> 1) & 1) << 9;
            io.pf |= ((val >> 0) & 1) << 8;
            return;
        case 0x0F: // PF2 playfield register byte 2 — PF2.0-7 = pf[0-7]
            io.pf &= ~0xFF;
            io.pf |= val & 0xFF;
            return;
        case 0x1B: // GRP0 graphics player 0
            P[0].GRP[0] = val;
            P[1].GRP[1] = P[1].GRP[0];
            return;
        case 0x1C: // GRP1 graphics player 1
            P[1].GRP[0] = val;
            P[0].GRP[1] = P[0].GRP[0];
            ball.enable[1] = ball.enable[0];
            return;
        case 0x1D: // ENAM0 enable missile 0
            m[0].enable = (val >> 1) & 1;
            return;
        case 0x1E: // ENAM1 enable missile 1
            m[1].enable = (val >> 1) & 1;
            return;
        case 0x1F: // ENABL enable ball
            ball.enable[0] = (val >> 1) & 1;
            return;
        case 0x20: // HMP0 horizontal motion player 0
            P[0].hm = (i32)(val >> 4) & 15;
            return;
        case 0x21: // HMP1 horizontal motion player 1
            P[1].hm = (i32)(val >> 4) & 15;
            return;
        case 0x22: // HMM0 horizontal motion missile 0
            m[0].hm = (i32)(val >> 4) & 15;
            return;
        case 0x23: // HMM1 horizontal motion missile 1
            m[1].hm = (i32)(val >> 4) & 15;
            return;
        case 0x24: // HMBL horizontal motion ball
            ball.hm = (i32)(val >> 4) & 15;
            return;
        case 0x2A: // HMOVE apply horizontal motion <strobe>
        {
            p_step(0, P[0].hm ^ 8);
            p_step(1, P[1].hm ^ 8);
            m_step(0, m[0].hm ^ 8);
            m_step(1, m[1].hm ^ 8);
            ball_step(ball.hm ^ 8);
            io.hmoved = 1;
            return;
        }
        case 0x2B: // HMCLR clear horizontal motion registers <strobe>
            P[0].hm = P[1].hm = m[0].hm = m[1].hm = ball.hm = 0;
            return;

    }
}


void TIA::WQ_cycle() {
    for (u32 i = 0; i < TIA_WQ_MAX; i++) {
        if (write_queue[i].active) {
            WQ_item* item = &write_queue[i];
            item->delay--;
            if (item->delay == 0)
                WQ_finish(item);
        }
    }
}

u8 TIA::read(u16 addr)
{
    switch((addr & 0x0F) | 0x30) {
        case 0x30: // CXM0P read collision data for M0-P1, M0-P0
            return (col.m0_p1 << 7) | (col.m0_p0 << 6);
        case 0x31: // CXM1P read collision data for M1-P0, M1-P1
            return (col.m1_p0 << 7) | (col.m1_p1 << 6);
        case 0x32: // CXP0FB read collision data for P0-PF, P0-BALL
            return (col.p0_pf << 7) | (col.p0_ball << 6);
        case 0x33: // CXP1FB read collision data for P1-PF, P1-BALL
            return (col.p1_pf << 7) | (col.p1_ball << 6);
        case 0x34: // CXM0FB read collision data for M0-PF, M0-BALL
            return (col.m0_pf << 7) | (col.m0_ball << 6);
        case 0x35: // CXM1FB read collision data for M1-PF, M1-BALL
            return (col.m1_pf << 7) | (col.m1_ball << 6);
        case 0x36: // CXBLPF read collision data for BALL-PF
            return (col.ball_pf << 7);
        case 0x37: // CXPPMM read collision data for P0-P1, M0-M1
            return (col.p0_p1 << 7) | (col.m0_m1 << 6);
        case 0x38: // INPT0 read pot 0
            return io.INPT[0];
        case 0x39: // INPT1 read pot 1
            return io.INPT[1];
        case 0x3A: // INPT2 read pot 2
            return io.INPT[2];
        case 0x3B: // INPT3 read pot 3
            return io.INPT[3];
        case 0x3C: // INPT4 read pot 4
            return io.INPT[4];
        case 0x3D: // INPT5 read pot 5
            return io.INPT[5];
        default:
            printf("\nUnknown TIA read %02x", addr);
            return 0;
    }
}


static const i32 centering_offsets[8] = { 3, 3, 3, 3, 3, 6, 3, 10};
void TIA::update_RESMPn(u32 num)
{
    // If RESMP behavior is enabled...
    // "As long as Bit 1 is set, the missile is hidden and its horizontal position is
    // centered on the players position. The centering offset is +3 for normal,
    // +6 for double, and +10 quad sized player (that is giving good centering
    // results with missile widths of 2, 4, and 8 respectively).
    if (m[num].locked_to_player)
        m[num].counter = P[num].counter + centering_offsets[P[num].size];
    // TODO: do real centering logic here, or is this good!??!
}

void TIA::update_RESMP()
{
    update_RESMPn(0);
    update_RESMPn(1);
}

void TIA::vsync(u32 val)
{
    // 0...1
    dprintf("\nTIA_vsync! %d %d", val, io.vsync);
    if (val && (io.vsync != val)) {
        new_frame();
    }
    io.vsync = val;
}

void TIA::write(u16 addr, u8 *data)
{
    // b-f, 1b-24
    u32 val = *data;
    i32 msx = (i32)hcounter - 68;
#define DELAY(num, howlong) case num: WQ_add(num, val, howlong); return
    switch(addr & 0x3F) {
        DELAY(0x01, 1);
        DELAY(0x0B, 1);
        DELAY(0x0C, 1);
        DELAY(0x0D, 2);
        DELAY(0x0E, 2);
        DELAY(0x0F, 2);
        DELAY(0x1B, 1);
        DELAY(0x1C, 1);
        DELAY(0x1D, 1);
        DELAY(0x1E, 1);
        DELAY(0x1F, 1);
        DELAY(0x20, 2);
        DELAY(0x21, 2);
        DELAY(0x22, 2);
        DELAY(0x23, 2);
        DELAY(0x24, 2);
        DELAY(0x2A, 6);
        DELAY(0x2B, 2);
        case 0x00: // VSYNC
            vsync((val >> 1) & 1);
            return;
        case 0x02: // halt processor until end of current scanline
            cpu_RDY = 1;
            return;
        case 0x03: // RSYNC
            hcounter = 0;
            pcounter = 0;
            clock_div = 0;
            x = 0;
            hblank = 1;
            dprintf("\nTIA RSYNC HC0");
            return;
        case 0x04: // NUSIZ0...number and size of missile 0
            P[0].size = val & 7;
            m[0].size = (val >> 4) & 3;
            return;
        case 0x05: // NUSIZ1...number and size of missile 1
            P[1].size = val & 7;
            m[1].size = (val >> 4) & 3;
            return;
        case 0x06: // COLUP0 colum-lum player 0 missile 0
            io.COLUP0 = (val >> 1) & 0x7F;
            return;
        case 0x07: // COLUP1 colum-lum player 1 missile 1
            io.COLUP1 = (val >> 1) & 0x7F;
            return;
        case 0x08: // COLUPF colum-lum playfield and ball
            io.COLUPF = (val >> 1) & 0x7F;
            return;
        case 0x09: // COLUBK colum-lum background
            io.COLUBK = (val >> 1) & 0x7F;
            return;
        case 0x0A: // CTRLPF control playfield ball size & collissions
            io.CTRLPF.u = val & 0b110111;
            return;
        case 0x10: // RESP0 reset player 0 <strobe>
            P[0].counter = -4;
            return;
        case 0x11: // RESP1 reset player 1 <strobe>
            P[1].counter = -4;
            return;
        case 0x12: // RESM0 reset missile 0 <strobe>
            m[0].counter = -4;
            return;
        case 0x13: // RESM1 reset missile 1 <strobe>
            m[1].counter = -4;
            return;
        case 0x14: // RESBL reset ball <strobe>
            ball.counter = -4;
            return;
        case 0x15: // AUDC0 audio control 0
            return;
        case 0x16: // AUDC1 audio control 1
            return;
        case 0x17: // AUDF0 audio frequency 0
            return;
        case 0x18: // AUDF1 audio frequency 1
            return;
        case 0x19: // AUDV0 audio volume 0
            return;
        case 0x1A: // AUDV1 audio volume 1
            return;
        case 0x25: // VDELP0 vertical delay player 0
            P[0].delay = (i32)val & 1;
            return;
        case 0x26: // VDELP1 vertical delay player 1
            P[1].delay = (i32)val & 1;
            return;
        case 0x27: // VDELBL vertical delay ball
            ball.delay = ((i32)val & 1);
            return;
        case 0x28: // RESMP0 reset missile 0 to player 0
            m[0].locked_to_player = (val >> 1) & 1;
            return;
        case 0x29: // RESMP1 reset missile 1 to player 1
            m[1].locked_to_player = (val >> 1) & 1;
            return;
        case 0x2C: // CXCLR clear collision latches <strobe>
            memset(&col, 0, sizeof(atari_TIA_col));
            return;
        default:
            dprintf("\nUnknow TIA write to %02x", addr & 0x3F);
            return;
    }
#undef DELAY
}

void TIA::new_frame()
{
    if (::dbg.do_debug) debugger_report_frame(dbg.interface);
    // Update internal state
    vcounter = 0;
    io.hmoved = 0;

    if (!missed_vblank) {
        flip_buffer();
    }
    missed_vblank = 0;
}

/*
 * Normal (REF=0) : PF0.4-7 PF1.7-0 PF2.0-7  PF0.4-7 PF1.7-0 PF2.0-7
 * Mirror (REF=1) : PF0.4-7 PF1.7-0 PF2.0-7  PF2.7-0 PF1.0-7 PF0.7-4
 */

// Missile size depending on NUSIZx.missile_size
static constexpr u32 M_SIZE[4] = { 1, 2, 4, 8};

// A table to help us exract correct bit based on counter-position inside player sprite and if it's flipped
static constexpr u32 PLAYER_SHIFTS[2][8] = {
        // Normal. MSB first left to right
        {7, 6, 5, 4, 3, 2, 1, 0},
        // Mirrored. LSB first left to right
        {0, 1, 2, 3, 4, 5, 6, 7}
};

// A table to help us extract playfield bits for any of the 40 valid horizontal positions, including with mirroring, and considering the odd way playfields are written
static constexpr u32 PF_SHIFTS[2][40] = {
        // normal
        {   // left half
            16, 17, 18, 19,  // PF0.4-7
            15, 14, 13, 12, 11, 10, 9, 8, // PF1.7-0
            0, 1, 2, 3, 4, 5, 6, 7, // PF2.0-7
            // right half
            16, 17, 18, 19,
            15, 14, 13, 12, 11, 10, 9, 8,
            0, 1, 2, 3, 4, 5, 6, 7
         },
        // mirrored
        {
            // left half
            16, 17, 18, 19,  // PF0.4-7
            15, 14, 13, 12, 11, 10, 9, 8, // PF1.7-0
            0, 1, 2, 3, 4, 5, 6, 7, // PF2.0-7
            // right half PF2.7-0 PF1.0-7 PF0.7-4
            7, 6, 5, 4, 3, 2, 1, 0, // PF2.7-0
            8, 9, 10, 11, 12, 13, 14, 15, // PF1.0-7
            19, 18, 17, 16 // PF0.7-4
        }
};

static constexpr u32 NUSIZ_copies[8][10] = {
        {1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, // 0  One copy              (X.........)
        {1, 0, 1, 0, 0, 0, 0, 0, 0, 0}, // 1  Two copies - close    (X.X.......)
        {1, 0, 0, 0, 1, 0, 0, 0, 0, 0}, // 2  Two copies - medium   (X...X.....)
        {1, 0, 1, 0, 1, 0, 0, 0, 0, 0}, // 3  Three copies - close  (X.X.X.....)
        {1, 0, 0, 0, 0, 0, 0, 0, 1, 0}, // 4  Two copies - wide     (X.......X.)
        {1, 1, 0, 0, 0, 0, 0, 0, 0, 0}, // 5  Double sized player   (XX........)
        {1, 0, 0, 0, 1, 0, 0, 0, 1, 0}, // 6  Three copies - medium (X...X...X.)
        {1, 1, 1, 1, 0, 0, 0, 0, 0, 0}  // 7  Quad sized player     (XXXX......)
};

static constexpr u32 DIFF_SHIFTS[8] = {
        0, 0, 0, 0, 0, 1, 0, 2
};

u32 TIA::get_player_pixel(u32 msx, u32 player_num)
{
    // No player pixels off left side of screen
    if (hcounter < 68) return 0;

    // Invisible players give no pixels, skip all the checks!
    if (P[player_num].GRP[P[player_num].delay] == 0) return 0;

    i32 player_x = P[player_num].counter; // Player X
    // If we're not within 80 pixels of the position, return 0
    if (((i32)msx - player_x) >= 80) return 0;

    // We haven't yet gotten to the left edge of the player X...
    if ((i32)msx < player_x) return 0;

    u32 pm_num_size = P[player_num].size;
    u32 offset = (u32)((i32)msx - player_x);

    // Check if the current screen position falls inside a copy slot
    if (!NUSIZ_copies[pm_num_size][offset >> 3]) return 0;

    u32 player_px = (offset >> DIFF_SHIFTS[pm_num_size]) & 7;
    // Extract bit from GRP
    return (P[player_num].GRP[P[player_num].delay] >> PLAYER_SHIFTS[P[player_num].reflect][player_px]) & 1;
}

static constexpr u32 BALL_SIZES[4] = {1, 2, 4, 8};

u32 TIA::get_ball_pixel(u32 msx)
{
    if (ball.enable[ball.delay] == 0) return 0; // Ball is not enabled
    if ((i32)msx < ball.counter) return 0; // We aren't at left edge of ball yet

    u32 ball_size = BALL_SIZES[io.CTRLPF.ball_size];
    return (i32)msx < (ball.counter + (i32)ball_size);
}

u32 TIA::get_missile_pixel(u32 msx, u32 missile_num)
{
    if (m[missile_num].enable == 0) return 0; // Missile not enabled
    if (m[missile_num].locked_to_player) return 0; // Missile is hidden and centering
    if ((i32)msx < m[missile_num].counter) return 0; // We aren't at left edge of missile yet

    u32 missile_size = BALL_SIZES[(m[missile_num].size)];
    return (i32)msx < (m[missile_num].counter + (i32)missile_size);
}

void TIA::new_scanline()
{
    vcounter++;
    io.hmoved = 0;

    if (vcounter > 262) { // 312 for PAL. this is just in case a game missed vblank
        //printf("\nVCOUNTER>262 refresh output! %d", master_frame + 1);
        vcounter = 0;

        flip_buffer();
        missed_vblank = 1;
    }
}

void TIA::run_cycle()
{
    // A frame is...
    // 40 lines vsync
    // 192 lines NTSC
    //
    // TIA write queue
    WQ_cycle();

    hcounter = pcounter;
    i32 screen_x = (i32)pcounter - 68;
    hblank = (pcounter < 68) || (io.hmoved && (pcounter < 76));
    if (screen_x >= 0)
        x = (u32)screen_x;

    // Every visible CLK, except during HMOVE blank
    if (!hblank) {
        // Advance all object counters one color clock
        ball_step(1);
        p_step(0, 1);
        p_step(1, 1);
        m_step(0, 1);
        m_step(1, 1);
    }

    if (IN_DISPLAY && screen_x >= 0 && screen_x < 160) {
        u32 row = vcounter - timing.display_line_start;
        if (hblank || io.vblank) {
            cur_output[row * 160 + screen_x] = 0;
        }
        else {
            // Playfield pixel: 160 visible pixels / 40 PF bits = 4 pixels per bit
            if ((screen_x & 3) == 0) {
                u32 pf_x = (u32)screen_x >> 2;
                io.pf_pixel = (io.pf >> PF_SHIFTS[io.CTRLPF.mirror][pf_x]) & 1;
            }
            u32 pf_pixel = io.pf_pixel;

            u32 p0 = P[0].output;
            u32 p1 = P[1].output;
            u32 m0 = m[0].output;
            u32 m1 = m[1].output;
            u32 bl = ball.output;

            // Collision detection — latch any overlap this pixel
            if (m0 && p1) col.m0_p1 = 1;
            if (m0 && p0) col.m0_p0 = 1;
            if (m1 && p0) col.m1_p0 = 1;
            if (m1 && p1) col.m1_p1 = 1;
            if (p0 && pf_pixel) col.p0_pf = 1;
            if (p0 && bl) col.p0_ball = 1;
            if (p1 && pf_pixel) col.p1_pf = 1;
            if (p1 && bl) col.p1_ball = 1;
            if (m0 && pf_pixel) col.m0_pf = 1;
            if (m0 && bl) col.m0_ball = 1;
            if (m1 && pf_pixel) col.m1_pf = 1;
            if (m1 && bl) col.m1_ball = 1;
            if (bl && pf_pixel) col.ball_pf = 1;
            if (p0 && p1) col.p0_p1 = 1;
            if (m0 && m1) col.m0_m1 = 1;

            u8 out_color = io.COLUBK;

            if (!io.CTRLPF.priority) {
                // Normal priority: players/missiles in front of PF/ball
                if (pf_pixel || bl)
                    out_color = io.CTRLPF.score_mode ? (screen_x < 80 ? io.COLUP0 : io.COLUP1) : io.COLUPF;
                if (m1 || p1) out_color = io.COLUP1;
                if (m0 || p0) out_color = io.COLUP0;
            } else {
                // PF priority: PF/ball in front of players/missiles
                if (m1 || p1) out_color = io.COLUP1;
                if (m0 || p0) out_color = io.COLUP0;
                if (pf_pixel || bl)
                    out_color = io.CTRLPF.score_mode ? (screen_x < 80 ? io.COLUP0 : io.COLUP1) : io.COLUPF;
            }
            cur_output[row * 160 + screen_x] = out_color;
        }
    }

    if (pcounter == 0)
        cpu_RDY = 0;

    CLK++;
    pcounter++;
    if (pcounter >= 228) {
        pcounter = 0;
        hcounter = 0;
        x = 0;
        hblank = 1;
        new_scanline();
    }
}

void TIA::bus_cycle(u16 addr, u8 *data, bool rw)
{
    if (!rw) *data = read(addr);
    else write(addr, data);
}
}
