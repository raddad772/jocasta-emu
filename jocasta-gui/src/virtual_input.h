#pragma once

struct full_system;

// Renders virtual controller / keyboard windows for systems that support them.
// Called from inside update_input() so that virtual presses are ORed into
// device state before advance_time() runs.
// show=false suppresses all windows (e.g. fullscreen).
// show_controller / show_keyboard gate the respective overlay types independently.
void render_virtual_inputs(full_system *fsys, bool show = true,
                           bool show_controller = true, bool show_keyboard = true);

// Global scale factor for all virtual controller / keyboard widgets.
// Initialise once from AppSettings at startup; updated live from the Settings page.
void  set_virtual_scale(float s);
float get_virtual_scale();
