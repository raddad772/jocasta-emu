#include "keybinding.h"
#include <cstdio>
#include <cstring>

// -----------------------------------------------------------------------
// is_held / was_pressed
// -----------------------------------------------------------------------

bool Binding::is_held() const
{
    switch (source) {
        case BindingSource::Keyboard:
            return key != ImGuiKey_None && ImGui::IsKeyDown(key);
        case BindingSource::MouseButton:
            return mouse_btn >= 0 && ImGui::IsMouseDown(mouse_btn);
        case BindingSource::GamepadButton:
        case BindingSource::GamepadAxis:
            // SDL-backed gamepad/axis support to be wired in later
            return false;
        default:
            return false;
    }
}

bool Binding::was_pressed() const
{
    switch (source) {
        case BindingSource::Keyboard:
            return key != ImGuiKey_None && ImGui::IsKeyPressed(key, /*repeat=*/false);
        case BindingSource::MouseButton:
            return mouse_btn >= 0 && ImGui::IsMouseClicked(mouse_btn, /*repeat=*/false);
        default:
            return false;
    }
}

// -----------------------------------------------------------------------
// display_name
// -----------------------------------------------------------------------

std::string Binding::display_name() const
{
    char buf[64];
    switch (source) {
        case BindingSource::None:
            return "(none)";
        case BindingSource::Keyboard: {
            const char* n = ImGui::GetKeyName(key);
            return n ? n : "?";
        }
        case BindingSource::MouseButton:
            snprintf(buf, sizeof(buf), "Mouse%d", mouse_btn);
            return buf;
        case BindingSource::GamepadButton:
            snprintf(buf, sizeof(buf), "Pad:%d", gamepad_btn);
            return buf;
        case BindingSource::GamepadAxis:
            snprintf(buf, sizeof(buf), "Axis%d%s", gamepad_axis, axis_positive ? "+" : "-");
            return buf;
    }
    return "?";
}

// -----------------------------------------------------------------------
// to_ini / from_ini
// -----------------------------------------------------------------------

std::string Binding::to_ini() const
{
    char buf[128];
    switch (source) {
        case BindingSource::None:
            return "none";
        case BindingSource::Keyboard:
            snprintf(buf, sizeof(buf), "key:%s", ImGui::GetKeyName(key));
            return buf;
        case BindingSource::MouseButton:
            snprintf(buf, sizeof(buf), "mouse:%d", mouse_btn);
            return buf;
        case BindingSource::GamepadButton:
            snprintf(buf, sizeof(buf), "pad:%d", gamepad_btn);
            return buf;
        case BindingSource::GamepadAxis:
            snprintf(buf, sizeof(buf), "axis:%d:%s:%.2f",
                     gamepad_axis, axis_positive ? "+" : "-", axis_threshold);
            return buf;
    }
    return "none";
}

Binding Binding::from_ini(const std::string& s)
{
    Binding b;
    if (s.empty() || s == "none") return b;

    if (s.rfind("key:", 0) == 0) {
        std::string kname = s.substr(4);
        for (ImGuiKey k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; k = (ImGuiKey)(k + 1)) {
            const char* n = ImGui::GetKeyName(k);
            if (n && kname == n) {
                b.source = BindingSource::Keyboard;
                b.key    = k;
                return b;
            }
        }
        return b; // unknown key name → None
    }
    if (s.rfind("mouse:", 0) == 0) {
        b.source    = BindingSource::MouseButton;
        b.mouse_btn = std::stoi(s.substr(6));
        return b;
    }
    if (s.rfind("pad:", 0) == 0) {
        b.source      = BindingSource::GamepadButton;
        b.gamepad_btn = std::stoi(s.substr(4));
        return b;
    }
    if (s.rfind("axis:", 0) == 0) {
        int   axis = -1;
        char  dir  = '+';
        float thr  = 0.5f;
        sscanf(s.c_str(), "axis:%d:%c:%f", &axis, &dir, &thr);
        b.source         = BindingSource::GamepadAxis;
        b.gamepad_axis   = axis;
        b.axis_positive  = (dir == '+');
        b.axis_threshold = thr;
        return b;
    }
    return b;
}

// -----------------------------------------------------------------------
// factory helpers
// -----------------------------------------------------------------------

Binding Binding::make_keyboard(ImGuiKey k)
{
    Binding b; b.source = BindingSource::Keyboard; b.key = k; return b;
}
Binding Binding::make_gamepad_button(int btn)
{
    Binding b; b.source = BindingSource::GamepadButton; b.gamepad_btn = btn; return b;
}
Binding Binding::make_gamepad_axis(int axis, bool positive, float threshold)
{
    Binding b;
    b.source         = BindingSource::GamepadAxis;
    b.gamepad_axis   = axis;
    b.axis_positive  = positive;
    b.axis_threshold = threshold;
    return b;
}
Binding Binding::make_mouse_button(int btn)
{
    Binding b; b.source = BindingSource::MouseButton; b.mouse_btn = btn; return b;
}
Binding Binding::default_fast_forward()
{
    return make_keyboard(ImGuiKey_GraveAccent);
}

// -----------------------------------------------------------------------
// capture_any
// -----------------------------------------------------------------------

Binding Binding::capture_any()
{
    // Keyboard — scan all named keys, skip gamepad virtual keys
    for (ImGuiKey k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; k = (ImGuiKey)(k + 1)) {
        if (k >= ImGuiKey_GamepadStart && k <= ImGuiKey_GamepadRStickDown) continue;
        // Skip bare modifier keys as sole binding (use them in combination later)
        if (k == ImGuiKey_LeftCtrl  || k == ImGuiKey_RightCtrl  ||
            k == ImGuiKey_LeftShift || k == ImGuiKey_RightShift ||
            k == ImGuiKey_LeftAlt   || k == ImGuiKey_RightAlt   ||
            k == ImGuiKey_LeftSuper || k == ImGuiKey_RightSuper) continue;
        if (ImGui::IsKeyPressed(k, /*repeat=*/false))
            return make_keyboard(k);
    }
    // Mouse buttons
    for (int m = 0; m < 5; m++) {
        if (ImGui::IsMouseClicked(m, /*repeat=*/false))
            return make_mouse_button(m);
    }
    // Gamepad/axis: wired in later via SDL
    return Binding{};
}
