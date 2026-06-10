#pragma once

#include "helpers/int.h"

namespace apple2 {
struct core;
struct IOU {
    explicit IOU(core* parent) : bus(parent) {}
    core *bus{};
    audio_output_ring *audio_ring{};

    struct io {
        u32 VBL{};
        u32 PAGE2{};     // Motherboard RAM read/write
        u32 HIRES{};     // Page2 does not switch $2000-3FFF

        u32 MIXED{};
        u32 COL80{};
        u32 KEYSTROBE{};
        u32 ALTCHRSET{};
        u32 TEXT{};
        u32 AN0{}, AN1{}, AN2{}, AN3{};
        u32 CSSTOUT{};
        u32 SPKR{};
        u32 AKD{};

        u32 pushbutton[3]{};
        u32 cassette_in{};
        u32 paddle[4]{};

    } io{};
    u32 flash{}, flash_counter{};
    u8 key_latch{};
    u32 last_key_states[100]{};
    simplebuf8 ROM{};
    u32 video_ROM_layout{};
    u32 video_ROM_text_base{};
    u32 video_ROM_reverse_bits{};
    u32 video_ROM_screen_codes{};

    void cycle();
    void reset();
    void detect_video_ROM();
    void update_switches();
    void access_c0xx(u32 addr, bool has_effect, bool is_write, u32 *r, u32 *MSB);
    u8 read_keyboard(bool has_effect);

    u8 *cur_output{};
    cvec_ptr<physical_io_device> display_ptr{};
    JSM_DISPLAY *display{};
    cvec_ptr<physical_io_device> keyboard_ptr{};
    cvec_ptr<physical_io_device> joystick_ptr{};
    u64 paddle_trigger_time{};          // master_cycles when $C070 was last strobed
    u32 get_paddle_value(u32 axis);       // 0-255 synthetic; axis 0=X, 1=Y
    bool get_joystick_button(u32 idx);   // idx 0=fire1, 1=fire2
    void advance_frame();
    void advance_line();
    void pixels();
    void pixels_text();
    void pixels_lores();
    void pixels_hires();
    u8 read_character_ROM(u32 glyph, u32 row);
    void keystrobe();
    void setup_keyboard(std::vector<physical_io_device> &IOs);
    bool AKD();
};
}
