#pragma once
#include <cstdint>
#include <string>
#include "imgui.h"

// -----------------------------------------------------------------------
// BindingSource — what kind of input device this binding uses
// -----------------------------------------------------------------------
enum class BindingSource : uint8_t {
    None = 0,
    Keyboard,
    GamepadButton,  // SDL button index
    GamepadAxis,    // SDL axis index + direction + threshold
    MouseButton,    // ImGui mouse button index (0=left, 1=right, 2=middle)
};

// -----------------------------------------------------------------------
// Binding — one input binding (keyboard key, pad button, axis, or mouse)
// -----------------------------------------------------------------------
struct Binding {
    BindingSource source        = BindingSource::None;

    // Keyboard
    ImGuiKey      key           = ImGuiKey_None;

    // Gamepad button (SDL GameController button index)
    int           gamepad_btn   = -1;

    // Gamepad axis
    int           gamepad_axis  = -1;
    float         axis_threshold = 0.5f;
    bool          axis_positive  = true;

    // Mouse button
    int           mouse_btn     = -1;

    // ---- queries ----
    bool is_none()     const { return source == BindingSource::None; }
    bool is_held()     const; // true every frame the input is held
    bool was_pressed() const; // true only on the first frame of press

    // ---- display & serialisation ----
    std::string   display_name() const;
    std::string   to_ini()       const;
    static Binding from_ini(const std::string& s);

    // ---- factory helpers ----
    static Binding make_keyboard(ImGuiKey k);
    static Binding make_gamepad_button(int btn);
    static Binding make_gamepad_axis(int axis, bool positive, float threshold = 0.5f);
    static Binding make_mouse_button(int btn);
    static Binding default_fast_forward(); // keyboard: GraveAccent (~)

    // ---- capture ----
    // Returns a filled Binding if any input was pressed this frame, else None.
    static Binding capture_any();
};
