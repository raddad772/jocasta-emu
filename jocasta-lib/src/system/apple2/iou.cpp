//
// Created by . on 8/30/24.
//


#include "apple2_bus.h"
#include "iou.h"
#include <cstring>

namespace apple2 {

// ── Joystick / paddle helpers ─────────────────────────────────────────────────
// Button layout in joystick_ptr controller: [0]=up [1]=down [2]=left [3]=right
//                                           [4]=fire1(PB0)  [5]=fire2(PB1)

u32 IOU::get_paddle_value(u32 axis)
{
    if (!joystick_ptr.vec) return 127;
    const auto &bl = joystick_ptr.get().controller.digital_buttons;
    if (bl.size() < 4) return 127;
    // axis 0 = X (paddle 0): left=low, right=high
    // axis 1 = Y (paddle 1): up=low,  down=high
    bool neg = (axis == 0) ? (bl[2].state != 0) : (bl[0].state != 0);
    bool pos = (axis == 0) ? (bl[3].state != 0) : (bl[1].state != 0);
    if (neg) return 10;
    if (pos) return 240;
    return 127;
}

bool IOU::get_joystick_button(u32 idx)
{
    if (!joystick_ptr.vec) return false;
    const auto &bl = joystick_ptr.get().controller.digital_buttons;
    u32 slot = 4 + idx;
    return (slot < (u32)bl.size()) && (bl[slot].state != 0);
}

// ─────────────────────────────────────────────────────────────────────────────

void IOU::reset()
{
    if (!display)
        display = &display_ptr.get().display;

    bus->clock.crt_x = bus->clock.crt_y = 0;
    bus->clock.frames_since_restart = 0;
    flash_counter = 0;
    flash = 0;
    key_latch = 0;
    memset(last_key_states, 0, sizeof(last_key_states));
    io.VBL = 0;
    io.HIRES = io.PAGE2 = 0;
    paddle_trigger_time = 0;
}

void IOU::advance_frame()
{
    bus->clock.crt_y = 0;
    bus->clock.frames_since_restart++;
    bus->clock.master_frame++;

    display->active_draw_buffer ^= 1;
    cur_output = static_cast<u8 *>(display->output[display->active_draw_buffer]);

    flash_counter = (flash_counter + 1) % 32;
    if (flash_counter == 0) {
        flash ^= 1;
    }
    // TODO: flash counter, keyboard auto-repeat,
}

void IOU::advance_line()
{
    bus->clock.crt_x = 0;
    bus->clock.crt_y++;
    switch(bus->clock.crt_y) {
        case 262:
            advance_frame();
            break;
    }
}

enum {
    gm_text,
    gm_lores,
    gm_hires,
    gm_mixed
};

enum {
    apple2_video_ROM_unknown,
    apple2_video_ROM_char_major_128,
    apple2_video_ROM_char_major_256,
    apple2_video_ROM_row_major_128,
    apple2_video_ROM_row_major_256,
    apple2_video_ROM_row_major_64x2,
    apple2_video_ROM_row_major_64x4
};

static u8 reverse7(u8 v)
{
    u8 out = 0;
    for (u32 i = 0; i < 7; i++) {
        out |= ((v >> i) & 1) << (6 - i);
    }
    return out;
}

static u32 bit_count7(u8 v)
{
    u32 out = 0;
    v &= 0x7F;
    for (u32 i = 0; i < 7; i++) {
        out += (v >> i) & 1;
    }
    return out;
}

static bool read_video_ROM_candidate(simplebuf8 &ROM, u32 layout, u32 base, u32 glyph, u32 row, u8 *out)
{
    u32 addr = 0;
    switch(layout) {
        case apple2_video_ROM_char_major_128:
            addr = base + ((glyph & 0x7F) << 3) + row;
            break;
        case apple2_video_ROM_char_major_256:
            addr = base + ((glyph & 0xFF) << 3) + row;
            break;
        case apple2_video_ROM_row_major_128:
            addr = base + (row << 7) + (glyph & 0x7F);
            break;
        case apple2_video_ROM_row_major_256:
            addr = base + (row << 8) + (glyph & 0xFF);
            break;
        case apple2_video_ROM_row_major_64x2:
            addr = base + ((glyph & 0x40) << 3) + (row << 6) + (glyph & 0x3F);
            break;
        case apple2_video_ROM_row_major_64x4:
            addr = base + ((glyph & 0xC0) << 3) + (row << 6) + (glyph & 0x3F);
            break;
        default:
            return false;
    }

    if (addr >= ROM.sz) return false;
    *out = ROM.ptr[addr] & 0x7F;
    return true;
}

static void score_glyph(simplebuf8 &ROM, u32 layout, u32 base, u32 glyph, const u8 *expected, u32 *normal, u32 *reversed)
{
    for (u32 row = 0; row < 8; row++) {
        u8 v = 0;
        if (!read_video_ROM_candidate(ROM, layout, base, glyph, row, &v)) return;

        u8 e = expected[row] & 0x7F;
        u8 r = reverse7(e);
        *normal += 7 - bit_count7(v ^ e);
        *reversed += 7 - bit_count7(v ^ r);
    }
}

void IOU::detect_video_ROM()
{
    static constexpr u8 A[8] = {
        0b0001000,
        0b0010100,
        0b0100010,
        0b0100010,
        0b0111110,
        0b0100010,
        0b0100010,
        0b0000000
    };
    static constexpr u8 P[8] = {
        0b0000000,
        0b0000000,
        0b0011110,
        0b0100010,
        0b0100010,
        0b0011110,
        0b0000010,
        0b0000010
    };
    static constexpr u8 RBRACKET[8] = {
        0b0111110,
        0b0110000,
        0b0110000,
        0b0110000,
        0b0110000,
        0b0110000,
        0b0111110,
        0b0000000
    };
    static constexpr u8 SPACE[8] = {};

    video_ROM_layout = apple2_video_ROM_char_major_128;
    video_ROM_text_base = 0;
    video_ROM_reverse_bits = 0;
    video_ROM_screen_codes = 0;

    if ((ROM.ptr == nullptr) || (ROM.sz < 1024))
        return;

    u32 best_score = 0;
    for (u32 base = 0; (base + 1024) <= ROM.sz; base += 1024) {
        for (u32 layout = apple2_video_ROM_char_major_128; layout <= apple2_video_ROM_row_major_64x4; layout++) {
            u32 normal = 0;
            u32 reversed = 0;
            u32 screen_codes = (layout == apple2_video_ROM_char_major_128) ||
                (layout == apple2_video_ROM_row_major_128) ||
                (layout == apple2_video_ROM_row_major_64x2);
            score_glyph(ROM, layout, base, 0x01, A, &normal, &reversed);
            score_glyph(ROM, layout, base, 0x20, SPACE, &normal, &reversed);
            score_glyph(ROM, layout, base, 0x70, P, &normal, &reversed);
            score_glyph(ROM, layout, base, 0x1D, RBRACKET, &normal, &reversed);

            u32 score = normal;
            u32 reverse = 0;
            if (reversed > normal) {
                score = reversed;
                reverse = 1;
            }

            if (score > best_score) {
                best_score = score;
                video_ROM_layout = layout;
                video_ROM_text_base = base;
                video_ROM_reverse_bits = reverse;
                video_ROM_screen_codes = screen_codes;
            }
        }
    }

    if (best_score < 80) {
        video_ROM_layout = apple2_video_ROM_unknown;
        video_ROM_text_base = 0;
        video_ROM_reverse_bits = 0;
        video_ROM_screen_codes = 0;
    }
}

static u32 apple2_text_glyph(u8 ch)
{
    if ((ch & 0xE0) == 0xE0)
        return ch & 0x7F;
    return ch & 0x3F;
}

u8 IOU::read_character_ROM(u32 glyph, u32 row)
{
    u8 out = 0;
    read_video_ROM_candidate(ROM, video_ROM_layout, video_ROM_text_base, glyph, row, &out);
    return out;
}

void IOU::pixels_text()
{
    if (bus->clock.crt_y >= 192) return;
    if (bus->clock.long_cycle_counter >= 40) return;

    u32 y = bus->clock.crt_y;
    u32 col = bus->clock.long_cycle_counter;
    u32 row = y >> 3;
    u32 line_in_text = y & 7;

    u8 *line_output = cur_output + (y * 560);
    u32 x = col * 14;

    u32 page_base = io.PAGE2 ? 0x0800 : 0x0400;
    u32 text_addr = page_base + ((row & 7) << 7) + ((row >> 3) * 40) + col;

    u8 ch = bus->mmu.RAM.ptr[text_addr];

    u32 glyph = video_ROM_screen_codes ? apple2_text_glyph(ch) : (ch & 0x7F);
    u8 dots = read_character_ROM(glyph, line_in_text);

    bool inverse = (ch & 0xC0) == 0x00;
    bool flashing = (ch & 0xC0) == 0x40;
    if (inverse || (flashing && flash))
        dots ^= 0x7F;

    for (u32 dot = 0; dot < 7; dot++) {
        u32 bit = video_ROM_reverse_bits ? (6 - dot) : dot;
        u32 pixel = (dots >> bit) & 1;
        if (x == 0) line_output[x++] = pixel | 0x80;
        else line_output[x++] = pixel;
        line_output[x++] = pixel;
    }
}

void IOU::pixels_lores()
{
}

void IOU::pixels_hires()
{
    if (bus->clock.crt_y >= 192) return;
    if (bus->clock.long_cycle_counter >= 40) return;

    u32 y = bus->clock.crt_y;
    u32 col = bus->clock.long_cycle_counter;

    u8 *line_output = cur_output + (y * 560);
    u32 x = col * 14;

    u32 page_base = io.PAGE2 ? 0x4000u : 0x2000u;
    u32 addr = page_base + ((y & 7u) << 10) + (((y >> 3) & 7u) <<  7) + ((y >> 6) * 40u) + col;

    u8 byte = bus->mmu.RAM.ptr[addr];
    u8 palette = (byte >> 7) & 1u;

    for (u32 dot = 0; dot < 7; dot++) {
        u8 out = ((byte >> dot) & 1u) | (palette << 1);
        line_output[x++] = out;
        line_output[x++] = out;
    }
}

void IOU::pixels()
{
    u32 y = bus->clock.crt_y;
    bool use_text = io.TEXT || (io.MIXED && y >= 160);

    if (use_text)
        pixels_text();
    else if (io.HIRES)
        pixels_hires();
    else
        pixels_lores();
}

void IOU::cycle()
{
    // for visible lines, 40 CPU cycles = output, 25 = blank (with the last elongated)
    // so for 40-col text mode, 1 clock = 14 pixels from the ROM
    // 455? virtual NTSC pixels,
    // 280 of which are visible, the rest blanked?


    // 262 NTSC lines
    // 192 lines are active display
    // 70 are blank
    if (bus->clock.long_cycle_counter == 0) {
        advance_line();
    }


    if (bus->clock.crt_y < 192) { // 192 visible lines
        if (bus->clock.long_cycle_counter < 40) {// up to 40*7 pixels wide
            pixels();

            if ((bus->clock.crt_y == 191) && (bus->clock.long_cycle_counter == 39)) {
                io.VBL = 1;
            }
        }
    }
    if ((bus->clock.crt_y == 261) && (bus->clock.long_cycle_counter == 39)) {
        io.VBL = 0;
    }
}

void IOU::keystrobe()
{
    io.KEYSTROBE = 0;
}


static JKEYS apple2_keyboard_keymap[] = {
        JK_1, JK_2, JK_3, JK_4, JK_5,
        JK_0, JK_9, JK_8, JK_7, JK_6,
        JK_Q, JK_W, JK_E, JK_R, JK_T,
        JK_P, JK_O, JK_I, JK_U, JK_Y,
        JK_A, JK_S, JK_D, JK_F, JK_G,
        JK_L, JK_K, JK_J, JK_H,
        JK_Z, JK_X, JK_C, JK_V,
        JK_M, JK_N, JK_B,
        JK_SPACE, JK_ESC, JK_CTRL,
        JK_SHIFT, JK_RSHIFT, JK_LEFT, JK_RIGHT,
        JK_ENTER, JK_BACKSPACE, JK_SEMICOLON, JK_MINUS,
        JK_EQUALS, JK_COMMA, JK_SLASH_FW, JK_DOT,
        JK_QUOTE, JK_APOSTROPHE, JK_SQUARE_BRACKET_LEFT, JK_SQUARE_BRACKET_RIGHT, JK_SLASH_BW,
        JK_UP, JK_DOWN, JK_TAB, JK_TILDE, JK_CAPS, JK_OPTION, JK_CMD,
        JK_NUM0, JK_NUM1, JK_NUM2, JK_NUM3, JK_NUM4,
        JK_NUM5, JK_NUM6, JK_NUM7, JK_NUM8, JK_NUM9,
        JK_NUM_ENTER, JK_NUM_DOT, JK_NUM_PLUS, JK_NUM_MINUS,
        JK_NUM_DIVIDE, JK_NUM_STAR, JK_NUM_CLEAR
};

void IOU::setup_keyboard(std::vector<physical_io_device> &IOs)
{
    physical_io_device &d = IOs.emplace_back();
    d.init(HID_KEYBOARD, false, false, true, true);

    d.id = 0;
    d.kind = HID_KEYBOARD;
    d.connected = true;
    d.enabled = true;

    JSM_KEYBOARD* kbd = &d.keyboard;
    kbd->num_keys = sizeof(apple2_keyboard_keymap) / sizeof(apple2_keyboard_keymap[0]);

    for (u32 i = 0; i < kbd->num_keys; i++) {
        kbd->key_defs[i] = apple2_keyboard_keymap[i];
    }
}


static bool is_matrix_key(enum JKEYS jkey)
{
#define CK(x) (jkey == x)
    if (CK(JK_SHIFT) || CK(JK_RSHIFT) || CK(JK_CTRL) || CK(JK_CMD) || CK(JK_OPTION) || CK(JK_CAPS)) return 0;
    return 1;
#undef CK
}

bool IOU::AKD()
{
    auto *pio = &keyboard_ptr.get();
    JSM_KEYBOARD &kbd = pio->keyboard;
    for (u32 i = 0; i < kbd.num_keys; i++) {
        if (is_matrix_key(kbd.key_defs[i])) {
            if (kbd.key_states[i])
                return true;
        }
    }
    return false;
}

static bool is_key_down(JSM_KEYBOARD &kbd, enum JKEYS key)
{
    for (u32 i = 0; i < kbd.num_keys; i++) {
        if (kbd.key_defs[i] == key)
            return kbd.key_states[i] != 0;
    }
    return false;
}

static u8 apple2_keycode(enum JKEYS key, bool shift, bool ctrl)
{
    u8 c = 0;
    switch(key) {
        case JK_A: c = 'A'; break;
        case JK_B: c = 'B'; break;
        case JK_C: c = 'C'; break;
        case JK_D: c = 'D'; break;
        case JK_E: c = 'E'; break;
        case JK_F: c = 'F'; break;
        case JK_G: c = 'G'; break;
        case JK_H: c = 'H'; break;
        case JK_I: c = 'I'; break;
        case JK_J: c = 'J'; break;
        case JK_K: c = 'K'; break;
        case JK_L: c = 'L'; break;
        case JK_M: c = 'M'; break;
        case JK_N: c = 'N'; break;
        case JK_O: c = 'O'; break;
        case JK_P: c = 'P'; break;
        case JK_Q: c = 'Q'; break;
        case JK_R: c = 'R'; break;
        case JK_S: c = 'S'; break;
        case JK_T: c = 'T'; break;
        case JK_U: c = 'U'; break;
        case JK_V: c = 'V'; break;
        case JK_W: c = 'W'; break;
        case JK_X: c = 'X'; break;
        case JK_Y: c = 'Y'; break;
        case JK_Z: c = 'Z'; break;
        default: break;
    }
    if (c) {
        if (ctrl) return (c - 'A') + 1;
        return c;
    }

    switch(key) {
        case JK_NUM0: return '0';
        case JK_NUM1: return '1';
        case JK_NUM2: return '2';
        case JK_NUM3: return '3';
        case JK_NUM4: return '4';
        case JK_NUM5: return '5';
        case JK_NUM6: return '6';
        case JK_NUM7: return '7';
        case JK_NUM8: return '8';
        case JK_NUM9: return '9';
        case JK_0: return shift ? ')' : '0';
        case JK_1: return shift ? '!' : '1';
        case JK_2: return shift ? '@' : '2';
        case JK_3: return shift ? '#' : '3';
        case JK_4: return shift ? '$' : '4';
        case JK_5: return shift ? '%' : '5';
        case JK_6:
            if (ctrl) return 0x1E;
            return shift ? '^' : '6';
        case JK_7: return shift ? '&' : '7';
        case JK_8: return shift ? '*' : '8';
        case JK_9: return shift ? '(' : '9';
        case JK_SPACE: return ' ';
        case JK_ESC: return 0x1B;
        case JK_TAB: return 0x09;
        case JK_ENTER: return 0x0D;
        case JK_NUM_ENTER: return 0x0D;
        case JK_BACKSPACE:
        case JK_LEFT:
            return 0x08;
        case JK_DOWN: return 0x0A;
        case JK_UP: return 0x0B;
        case JK_RIGHT: return 0x15;
        case JK_SEMICOLON: return shift ? ':' : ';';
        case JK_QUOTE:
        case JK_APOSTROPHE: return shift ? '"' : '\'';
        case JK_NUM_MINUS: return '-';
        case JK_MINUS:
            if (ctrl) return 0x1F;
            return shift ? '_' : '-';
        case JK_NUM_PLUS: return '+';
        case JK_NUM_CLEAR: return '=';
        case JK_EQUALS: return shift ? '+' : '=';
        case JK_SQUARE_BRACKET_LEFT:
            if (ctrl) return 0x1B;
            return shift ? '{' : '[';
        case JK_SQUARE_BRACKET_RIGHT:
            if (ctrl) return 0x1D;
            return shift ? '}' : ']';
        case JK_NUM_DOT:
            return '.';
        case JK_NUM_DIVIDE:
            return '/';
        case JK_NUM_STAR:
            return '*';
        case JK_TILDE: return shift ? '~' : '`';
        case JK_COMMA: return shift ? '<' : ',';
        case JK_SLASH_FW: return shift ? '?' : '/';
        case JK_SLASH_BW:
            if (ctrl) return 0x1C;
            return shift ? '|' : '\\';
        case JK_DOT: return shift ? '>' : '.';
        default:
            return 0;
    }
}

u8 IOU::read_keyboard(bool has_effect)
{
    auto *pio = &keyboard_ptr.get();
    JSM_KEYBOARD &kbd = pio->keyboard;
    bool shift = is_key_down(kbd, JK_SHIFT) || is_key_down(kbd, JK_RSHIFT);
    bool ctrl = is_key_down(kbd, JK_CTRL);
    io.pushbutton[0] = is_key_down(kbd, JK_OPTION) || get_joystick_button(0);
    io.pushbutton[1] = is_key_down(kbd, JK_CMD)    || get_joystick_button(1);

    if (has_effect) {
        for (u32 i = 0; i < kbd.num_keys; i++) {
            u32 key_down = kbd.key_states[i] != 0;
            if (key_down && !last_key_states[i]) {
                u8 code = apple2_keycode(kbd.key_defs[i], shift, ctrl);
                if (code != 0) {
                    key_latch = code & 0x7F;
                    io.KEYSTROBE = 1;
                }
            }
            last_key_states[i] = key_down;
        }
    }

    return key_latch | (io.KEYSTROBE << 7);
}


void IOU::access_c0xx(u32 addr, bool has_effect,  bool is_write, u32 *r, u32 *MSB) {
    u32 addr3 = addr & 0xFFF0;
    if (!is_write && (addr < 0xC010)) {
        *r = read_keyboard(has_effect);
    }
    else if (!is_write && (addr == 0xC010)) {
        *r = read_keyboard(has_effect) & 0x7F;
        *MSB = AKD() << 7;
        if (has_effect)
            keystrobe();
    }
    else if (is_write && (addr3 == 0xC010) && has_effect) {
        keystrobe();
    }

    if (has_effect && (addr3 == 0xC020))
        io.CSSTOUT ^= 1;
    if (has_effect && (addr3 == 0xC030))
        io.SPKR ^= 1;

    if (addr3 == 0xC060) {
        auto *pio = &keyboard_ptr.get();
        JSM_KEYBOARD &kbd = pio->keyboard;
        io.pushbutton[0] = is_key_down(kbd, JK_OPTION) || get_joystick_button(0);
        io.pushbutton[1] = is_key_down(kbd, JK_CMD)    || get_joystick_button(1);
        // Multiplexed bit...
        u32 addr4 = addr & 7;
        switch(addr4) {
            case 0:
                *MSB = io.cassette_in << 7;
                break;
            case 1:
            case 2:
            case 3:
                *MSB = io.pushbutton[addr4 - 1] << 7;
                //PB0 = open apple (option) / joystick fire 1
                //PB1 = closed apple (command) / joystick fire 2
                break;
            case 4:
            case 5: {
                // Paddle timer: strobed by $C070; bit 7 = 1 while timer still counting.
                // Apple II PREAD loop ≈ 9 CPU cycles × 14 master clocks = 126 per count.
                // paddle_value 0-255 maps to ~0-240 counts.
                u32 axis  = addr4 - 4;   // 0=X, 1=Y
                u32 pval  = get_paddle_value(axis);
                u64 thr   = (u64)pval * 126u;
                u64 elapsed = bus->clock.master_cycles - paddle_trigger_time;
                *MSB = (elapsed < thr) ? 0x80u : 0u;
                break;
            }
            case 6:
            case 7:
                // Paddles 2-3 not wired; always report expired
                *MSB = 0;
                break;
        }
    }

    // $C070-$C07F: PDLTRIG — strobe all paddle timers
    if (has_effect && (addr >= 0xC070) && (addr < 0xC080)) {
        paddle_trigger_time = bus->clock.master_cycles;
    }
}
}
