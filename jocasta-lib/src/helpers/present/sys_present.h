#pragma once

#include "../physical_io.h"
#include "helpers/int.h"
#include "helpers/sys_interface.h"
#include "helpers/enums.h"

struct events_view;

struct jsm_present_context {
    bool partial{};
    u32 complete_buffer{};
    u32 draw_buffer{};
    u32 split_y{};
    i32 split_x{-1};
    u32 apple2_color_mode{};          // 0=mono  1=green phosphor  2=color NTSC
    bool apple2_respect_burst{true};  // suppress color on text rows
};

void jsm_present(jsm_system *jsm, jsm::systems which, physical_io_device &display, void *out_buf, u32 x_offset, u32 y_offset, u32 out_width, u32 out_height, events_view *ev, u32 &outcols, u32 &ourtrows, bool &updated_uv, const jsm_present_context *ctx = nullptr);
void mac512k_present(physical_io_device &device, void *out_buf, u32 out_width, u32 out_height, const jsm_present_context *ctx = nullptr);
void apple2_present(physical_io_device &device, void *out_buf, u32 x_offset, u32 y_offset, u32 out_width, u32 out_height, const jsm_present_context *ctx = nullptr);
void zx_spectrum_present(physical_io_device &device, void *out_buf, u32 out_width, u32 out_height, const jsm_present_context *ctx = nullptr);
void atari2600_present(physical_io_device &device, void *out_buf, u32 out_width, u32 out_height, const jsm_present_context *ctx = nullptr);
void DMG_present(physical_io_device &device, void *out_buf, u32 x_offset, u32 y_offset, u32 out_width, u32 out_height, bool is_event_view_present, const jsm_present_context *ctx = nullptr);
void GBC_present(physical_io_device &device, void *out_buf, u32 x_offset, u32 y_offset, u32 out_width, u32 out_height, bool is_event_view_present, const jsm_present_context *ctx = nullptr);
void NES_present(physical_io_device &device, void *out_buf, u32 x_offset, u32 y_offset, u32 out_width, u32 out_height, const jsm_present_context *ctx = nullptr);
void GBA_present(physical_io_device &device, void *out_buf, u32 out_width, u32 out_height, u32 is_event_view_present, const jsm_present_context *ctx = nullptr);
void genesis_present(physical_io_device &device, void *out_buf, u32 out_width, u32 out_height, u32 is_event_view_present, const jsm_present_context *ctx = nullptr);
void SMS_present(physical_io_device &device, void *out_buf, u32 x_offset, u32 y_offset, u32 out_width, u32 out_height, const jsm_present_context *ctx = nullptr);
void GG_present(physical_io_device &device, void *out_buf, u32 x_offset, u32 y_offset, u32 out_width, u32 out_height, const jsm_present_context *ctx = nullptr);
