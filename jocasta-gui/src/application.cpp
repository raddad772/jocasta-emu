// GetContentRegionAvail().x

#include <cstdio>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <system_error>
#include <unordered_map>
#include <string>
#include "build.h"
#include "application.h"

#include "imgui_internal.h"
#include <SDL3/SDL.h>

#include "helpers/cvec.h"
// FRAME_MULTI replaced by per-core/global ff_speed setting in AppSettings


#ifdef JSM_OPENGL
#if defined(IMGUI_IMPL_OPENGL_ES2)
#include <SDL3/SDL_opengles2.h>
#else
#include <SDL3/SDL_opengl.h>
#endif

#ifdef __EMSCRIPTEN__
#include "../libs/emscripten/emscripten_mainloop_stub.h"
#endif
#endif // jsm_opengl



#include "helpers/debug.h"
#include "keymap_translate.h"
#include "my_texture.h"
#include "full_sys.h"
#include "virtual_input.h"
#include "helpers/path_util.h"
#include "helpers/serialize/serialize.h"
#include "tinyfiledialogs.h"
#include "bios_checker.h"


#ifdef __EMSCRIPTEN__
#include "../libs/emscripten/emscripten_mainloop_stub.h"
#endif

#ifdef JSM_SDLGPU
void imgui_jocasta_app::platform_setup(SDL_GPUDevice *in_device)
{
    device = in_device;
    if (fsys) fsys->platform_setup(in_device);

    // Apply persisted virtual-input scale on startup.
    set_virtual_scale(settings.get_virtual_scale());

    if (!nearest_sampler) {
        SDL_GPUSamplerCreateInfo si = {};
        si.min_filter    = SDL_GPU_FILTER_NEAREST;
        si.mag_filter    = SDL_GPU_FILTER_NEAREST;
        si.mipmap_mode   = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        si.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        si.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        nearest_sampler = SDL_CreateGPUSampler(device, &si);
    }
}

static void cb_set_sampler_nearest(const ImDrawList*, const ImDrawCmd* cmd)
{
    auto *rs = (ImGui_ImplSDLGPU3_RenderState*)ImGui::GetPlatformIO().Renderer_RenderState;
    if (rs) rs->SamplerCurrent = (SDL_GPUSampler*)cmd->UserCallbackData;
}

static void cb_restore_sampler(const ImDrawList*, const ImDrawCmd*)
{
    auto *rs = (ImGui_ImplSDLGPU3_RenderState*)ImGui::GetPlatformIO().Renderer_RenderState;
    if (rs) rs->SamplerCurrent = rs->SamplerDefault;
}

static void push_nearest_sampler(void *s)
{
    ImGui::GetWindowDrawList()->AddCallback(cb_set_sampler_nearest, s);
}

static void pop_sampler()
{
    ImGui::GetWindowDrawList()->AddCallback(cb_restore_sampler, nullptr);
}
#else
static void push_nearest_sampler(void*) {}
static void pop_sampler() {}
#endif

struct load_system_entry {
    const char *label;
    const char *cli_name;
    jsm::systems system;
    bool supports_folder = false;
    const char* const* filters = nullptr;
    // Per-device filter overrides (nullptr = fall back to `filters`)
    const char* const* disc_filters = nullptr;
    const char* const* tape_filters = nullptr;
};

static const char* const FILT_APPLE2E[]   = {"*.dsk","*.nib","*.do","*.po","*.2mg",nullptr};
static const char* const FILT_ATARI2600[] = {"*.a26","*.bin","*.rom",nullptr};
static const char* const FILT_PV1000[]    = {"*.bin",nullptr};
static const char* const FILT_C64[]       = {"*.d64","*.t64","*.prg","*.crt","*.tap",nullptr};
static const char* const FILT_C64_SIDELOAD[] = {"*.prg",nullptr};
static const char* const FILT_COSMAC[]    = {"*.ch8","*.bin",nullptr};
static const char* const FILT_DC[]        = {"*.cdi","*.gdi","*.chd",nullptr};
static const char* const FILT_GAL[]       = {"*.gal",nullptr};
static const char* const FILT_GB[]        = {"*.gb",nullptr};
static const char* const FILT_GBA[]       = {"*.gba",nullptr};
static const char* const FILT_GBC[]       = {"*.gbc",nullptr};
static const char* const FILT_GG[]        = {"*.gg",nullptr};
static const char* const FILT_GEN[]       = {"*.md","*.bin","*.gen","*.smd",nullptr};
static const char* const FILT_MAC[]       = {"*.img","*.dsk","*.dc42",nullptr};
static const char* const FILT_NEOGEO[]    = {"*.neo",nullptr};
static const char* const FILT_NGP[]       = {"*.ngp",nullptr};
static const char* const FILT_NGPC[]      = {"*.ngc","*.ngp",nullptr};
static const char* const FILT_NES[]       = {"*.nes","*.unf",nullptr};
static const char* const FILT_NDS[]       = {"*.nds",nullptr};
static const char* const FILT_PS1[]       = {"*.bin","*.cue","*.iso","*.chd",nullptr};
static const char* const FILT_SG1000[]    = {"*.sg","*.bin",nullptr};
static const char* const FILT_SMS[]       = {"*.sms","*.bin",nullptr};
static const char* const FILT_SNES[]      = {"*.sfc","*.smc","*.bin","*.smw","*.fig","*.swc",nullptr};
static const char* const FILT_PCE[]       = {"*.pce","*.bin",nullptr};
static const char* const FILT_ZX[]        = {"*.sna","*.z80","*.tap","*.tzx","*.trd","*.scl",nullptr};
static const char* const FILT_ZX_TAPE[]   = {"*.tap","*.tzx",nullptr};

// Columns: label, cli_name, system, supports_folder, filters, disc_filters, tape_filters
// disc_filters / tape_filters: nullptr means fall back to `filters`
static constexpr load_system_entry LOAD_SYSTEMS[] = {
    { "Apple IIe",           "apple2e",      jsm::systems::APPLEIIe,              false, FILT_APPLE2E,  FILT_APPLE2E, nullptr   },
    { "Atari 2600",          "atari2600",    jsm::systems::ATARI2600,             false, FILT_ATARI2600, nullptr,     nullptr   },
    { "Casio PV-1000",       "pv1000",       jsm::systems::CASIO_PV1000,          false, FILT_PV1000,   nullptr,     nullptr   },
    { "Commodore 64",        "c64",          jsm::systems::COMMODORE64,           true,  FILT_C64,      nullptr,     nullptr   },
    { "Cosmac VIP 2K",       "vip2k",        jsm::systems::COSMAC_VIP_2k,         false, FILT_COSMAC,   nullptr,     nullptr   },
    { "Cosmac VIP 4K",       "vip4k",        jsm::systems::COSMAC_VIP_4k,         false, FILT_COSMAC,   nullptr,     nullptr   },
    { "Dreamcast",           "dreamcast",    jsm::systems::DREAMCAST,             false, FILT_DC,       FILT_DC,     nullptr   },
    { "Galaksija",           "galaksija",    jsm::systems::GALAKSIJA,             false, FILT_GAL,      nullptr,     nullptr   },
    { "Game Boy",            "gb",           jsm::systems::DMG,                   false, FILT_GB,       nullptr,     nullptr   },
    { "Game Boy Advance",    "gba",          jsm::systems::GBA,                   false, FILT_GBA,      nullptr,     nullptr   },
    { "Game Boy Color",      "gbc",          jsm::systems::GBC,                   false, FILT_GBC,      nullptr,     nullptr   },
    { "Game Gear",           "gg",           jsm::systems::GG,                    false, FILT_GG,       nullptr,     nullptr   },
    { "Genesis (Japan)",     "genesis-jp",   jsm::systems::GENESIS_JAP,           false, FILT_GEN,      nullptr,     nullptr   },
    { "Genesis (USA)",       "genesis",      jsm::systems::GENESIS_USA,           false, FILT_GEN,      nullptr,     nullptr   },
    { "Mac 128K",            "mac128k",      jsm::systems::MAC128K,               false, FILT_MAC,      FILT_MAC,    nullptr   },
    { "Mac 512K",            "mac512k",      jsm::systems::MAC512K,               false, FILT_MAC,      FILT_MAC,    nullptr   },
    { "Mac Plus 1MB",        "macplus",      jsm::systems::MACPLUS_1MB,           false, FILT_MAC,      FILT_MAC,    nullptr   },
    { "Mega Drive (PAL)",    "megadrive-pal",jsm::systems::MEGADRIVE_PAL,         false, FILT_GEN,      nullptr,     nullptr   },
    { "Neo Geo AES",         "neogeo-aes",   jsm::systems::NEOGEO_AES,            false, FILT_NEOGEO,   nullptr,     nullptr   },
    { "Neo Geo MVS",         "neogeo-mvs",   jsm::systems::NEOGEO_MVS,            false, FILT_NEOGEO,   nullptr,     nullptr   },
    { "Neo Geo Pocket",      "ngp",          jsm::systems::NEOGEO_POCKET,         false, FILT_NGP,      nullptr,     nullptr   },
    { "Neo Geo Pocket Color","ngpc",         jsm::systems::NEOGEO_POCKET_COLOR,   false, FILT_NGPC,     nullptr,     nullptr   },
    { "NES",                 "nes",          jsm::systems::NES,                   false, FILT_NES,      nullptr,     nullptr   },
    { "Nintendo DS",         "nds",          jsm::systems::NDS,                   false, FILT_NDS,      nullptr,     nullptr   },
    { "PlayStation",         "ps1",          jsm::systems::PS1,                   false, FILT_PS1,      FILT_PS1,    nullptr   },
    { "SG-1000",             "sg1000",       jsm::systems::SG1000,                false, FILT_SG1000,   nullptr,     nullptr   },
    { "Sega Master System",  "sms",          jsm::systems::SMS1,                  false, FILT_SMS,      nullptr,     nullptr   },
    { "Sega Master System II","sms2",        jsm::systems::SMS2,                  false, FILT_SMS,      nullptr,     nullptr   },
    { "SNES",                "snes",         jsm::systems::SNES,                  false, FILT_SNES,     nullptr,     nullptr   },
    { "TurboGrafx-16",       "tg16",         jsm::systems::TURBOGRAFX16,          false, FILT_PCE,      nullptr,     nullptr   },
    { "ZX Spectrum 48K",     "zx48",         jsm::systems::ZX_SPECTRUM_48K,       false, FILT_ZX,       nullptr,     FILT_ZX_TAPE },
    { "ZX Spectrum 128K",    "zx128",        jsm::systems::ZX_SPECTRUM_128K,      false, FILT_ZX,       nullptr,     FILT_ZX_TAPE },
};


static bool load_name_char_equal(char a, char b)
{
    auto norm = [](char c) -> char {
        c = (char)std::tolower((unsigned char)c);
        if (c == '_' || c == ' ') c = '-';
        return c;
    };
    return norm(a) == norm(b);
}

static bool load_name_equal(const char *a, const char *b)
{
    while (*a && *b) {
        if (!load_name_char_equal(*a, *b))
            return false;
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}

static bool find_load_system(const char *name, jsm::systems &out)
{
    for (auto &entry : LOAD_SYSTEMS) {
        if (load_name_equal(name, entry.cli_name) || load_name_equal(name, entry.label)) {
            out = entry.system;
            return true;
        }
    }
    return false;
}

static bool parse_load_spec(const char *spec, jsm::systems &system, char *path, size_t path_sz)
{
    const char *delim = strchr(spec, ':');
    if (!delim) return false;

    char system_name[128];
    size_t system_len = (size_t)(delim - spec);
    if (system_len == 0 || system_len >= sizeof(system_name)) return false;
    memcpy(system_name, spec, system_len);
    system_name[system_len] = 0;

    if (!find_load_system(system_name, system)) return false;

    snprintf(path, path_sz, "%s", delim + 1);
    return path[0] != 0;
}

static std::string display_filename_from_path(const std::string& path)
{
    size_t slash = path.find_last_of("/\\");
    if (slash == std::string::npos) return path;
    if (slash + 1 >= path.size()) return std::string();
    return path.substr(slash + 1);
}

static void ensure_trailing_slash(char* buf, size_t sz)
{
    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] != '/' && len + 1 < sz) {
        buf[len]     = '/';
        buf[len + 1] = '\0';
    }
}

static void ensure_trailing_slash(std::string& path)
{
    if (!path.empty() && path.back() != '/')
        path.push_back('/');
}

static std::string existing_dialog_dir(const std::string& path, bool trailing_slash)
{
    std::error_code ec;
    if (!path.empty()) {
        std::filesystem::path p(path);
        if (std::filesystem::is_directory(p, ec)) {
            std::string out = p.string();
            if (trailing_slash) ensure_trailing_slash(out);
            return out;
        }
        ec.clear();
        if (std::filesystem::is_regular_file(p, ec)) {
            std::filesystem::path parent = p.parent_path();
            ec.clear();
            if (!parent.empty() && std::filesystem::is_directory(parent, ec)) {
                std::string out = parent.string();
                if (trailing_slash) ensure_trailing_slash(out);
                return out;
            }
        }
    }

    const char *home = get_user_dir();
    std::string out = (home && home[0]) ? home : ".";
    if (trailing_slash) ensure_trailing_slash(out);
    return out;
}

static void default_load_path(char *out, size_t out_sz, jsm::systems system, AppSettings& settings)
{
    std::string saved = settings.get_last_dir(system);
    if (!saved.empty()) {
        std::string safe = existing_dialog_dir(saved, true);
        snprintf(out, out_sz, "%s", safe.c_str());
        return;
    }
    u32 worked = 0;
    GET_HOME_BASE_SYS(out, out_sz, system, nullptr, &worked);
    if (worked) {
        std::string safe = existing_dialog_dir(out, true);
        snprintf(out, out_sz, "%s", safe.c_str());
        return;
    }
    construct_path_with_home(out, out_sz, "Documents");
    std::string safe = existing_dialog_dir(out, true);
    snprintf(out, out_sz, "%s", safe.c_str());
}

static bool regular_file_exists(const std::string& path)
{
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

static std::string join_path(const std::string& dir, const char *name)
{
    if (dir.empty()) return name ? std::string(name) : std::string();
    if (dir.back() == '/') return dir + name;
    return dir + "/" + name;
}

static void update_input(full_system* fsys, AppSettings& settings, const char* core_key, u32 *hotkeys, ImGuiIO& io, bool fullscreen = false) {
    using IM = full_system::InputMode;
    const bool kbd_passthrough = !fsys->needs_input_mode()
                                 || fsys->input_mode == IM::KEYBOARD
                                 || fsys->input_mode == IM::BOTH;
    const bool kbd_joypad      = !fsys->needs_input_mode()
                                 || fsys->input_mode == IM::JOYPAD
                                 || fsys->input_mode == IM::BOTH;

    // Keyboard passthrough — suppressed in JOYPAD mode
    if (fsys->io.keyboard.vec && kbd_passthrough) {
        auto &pio = fsys->io.keyboard.get();
        if (pio.connected && pio.enabled) {
            JSM_KEYBOARD *kbd = &pio.keyboard;
            for (u32 i = 0; i < kbd->num_keys; i++) {
                u32 pc = (kbd->key_defs[i] != JK_NONE)
                       ? (ImGui::IsKeyDown(jk_to_imgui(kbd->key_defs[i])) ? 1u : 0u)
                       : 0u;
                kbd->key_states[i] = pc | kbd->virtual_key_states[i];
            }
        }
    } else if (fsys->io.keyboard.vec && !kbd_passthrough) {
        // Clear all keys so nothing stays stuck when we switch modes
        auto &pio = fsys->io.keyboard.get();
        if (pio.connected && pio.enabled) {
            JSM_KEYBOARD *kbd = &pio.keyboard;
            for (u32 i = 0; i < kbd->num_keys; i++)
                kbd->key_states[i] = 0;
        }
    }
    {
        const char* ck = fsys && fsys->sys ? AppSettings::sys_to_core_key(fsys->sys->kind) : nullptr;
        bool show_vc = ck ? settings.effective_show_virtual_controller(ck) : settings.get_show_virtual_controller_global();
        bool show_vk = ck ? settings.effective_show_virtual_keyboard(ck)   : settings.get_show_virtual_keyboard_global();
        render_virtual_inputs(fsys, !fullscreen, show_vc, show_vk);
    }
    hotkeys[0] = 0; // reserved
    hotkeys[1] = 0; // reserved

    // Controllers 1-4
    cvec_ptr<physical_io_device>* controllers[4] = {
        &fsys->io.controller1, &fsys->io.controller2,
        &fsys->io.controller3, &fsys->io.controller4
    };
    for (int pnum = 0; pnum < 4; pnum++) {
        auto* cptr = controllers[pnum];
        if (!cptr->vec) continue;
        auto &pio = cptr->get();
        if (!pio.connected || !pio.enabled) continue;
        JSM_CONTROLLER *ctr = &pio.controller;
        for (auto &db : ctr->digital_buttons) {
            db.state = 0;
            // Keyboard: custom if set, else per-system default for P1
            // Suppressed in KEYBOARD mode when the core needs mode switching
            if (kbd_joypad) {
                Binding kbd = settings.effective_input_kbd(core_key, pnum + 1, (int)db.common_id);
                if (!kbd.is_none())
                    db.state |= kbd.is_held() ? 1 : 0;
            }
            // Gamepad: custom if set, else ImGui auto-map for P1
            Binding pad = settings.get_input_pad(core_key, pnum + 1, (int)db.common_id);
            if (!pad.is_none()) {
                db.state |= pad.is_held() ? 1 : 0;
            } else if (pnum == 0) {
                db.state |= ImGui::IsKeyDown(jk_to_imgui_gp(db.common_id));
            }
            db.state |= db.virtual_state; // virtual controller UI
        }
    }

    // Chassis buttons — skipping power and reset (handled by the emulator core directly).
    // DBK_BUTTON: pulse high for one frame when the hotkey is held OR a GUI click fired.
    // DBK_SWITCH: toggle on hotkey-press edge or GUI click; state persists across frames.
    if (fsys->io.chassis.vec) {
        auto &pio = fsys->io.chassis.get();
        if (pio.connected && pio.enabled) {
            for (auto &db : pio.chassis.digital_buttons) {
                if (db.common_id == DBCID_ch_power || db.common_id == DBCID_ch_reset)
                    continue;

                Binding hk = settings.effective_chassis_hotkey(core_key, db.common_id);

                bool gui   = fsys->chassis_gui_pulse[(int)db.common_id];
                fsys->chassis_gui_pulse[(int)db.common_id] = false;

                if (db.kind == DBK_SWITCH) {
                    // Toggle on rising edge only
                    if (hk.was_pressed() || gui)
                        db.state ^= 1u;
                } else {
                    // Momentary: high while hotkey held, or for one frame on GUI click
                    db.state = (hk.is_held() ? 1u : 0u) | (gui ? 1u : 0u);
                }
            }
        }
    }
}

// ── Dual-screen layout ───────────────────────────────────────────────────────

struct DualScreenPlacement {
    ImVec2 pos1, size1;   // primary (top) screen in content-area coords
    ImVec2 pos2, size2;   // secondary (touch) screen in content-area coords
    ImVec2 total_size;    // bounding box of both screens
};

static DualScreenPlacement compute_dual_layout(DisplayLayout layout, ImVec2 avail, ImVec2 n1, ImVec2 n2)
{
    DualScreenPlacement p{};
    auto fit = [](float aw, float ah, float nw, float nh) -> float {
        return (aw <= 0 || ah <= 0) ? 1.f : std::min(aw / nw, ah / nh);
    };

    switch (layout) {
        default:
        case DisplayLayout::TopBottom:
        case DisplayLayout::Swapped: {
            float scale = fit(avail.x, avail.y, n1.x, n1.y + n2.y);
            ImVec2 s1 = {n1.x * scale, n1.y * scale};
            ImVec2 s2 = {n2.x * scale, n2.y * scale};
            float tw = std::max(s1.x, s2.x);
            p.total_size = {tw, s1.y + s2.y};
            if (layout == DisplayLayout::TopBottom) {
                p.pos1  = {(tw - s1.x) * 0.5f, 0};
                p.size1 = s1;
                p.pos2  = {(tw - s2.x) * 0.5f, s1.y};
                p.size2 = s2;
            } else {
                p.pos2  = {(tw - s2.x) * 0.5f, 0};
                p.size2 = s2;
                p.pos1  = {(tw - s1.x) * 0.5f, s2.y};
                p.size1 = s1;
            }
            break;
        }
        case DisplayLayout::SideBySide: {
            float scale = fit(avail.x, avail.y, n1.x + n2.x, std::max(n1.y, n2.y));
            ImVec2 s1 = {n1.x * scale, n1.y * scale};
            ImVec2 s2 = {n2.x * scale, n2.y * scale};
            float th = std::max(s1.y, s2.y);
            p.total_size = {s1.x + s2.x, th};
            p.pos1  = {0,    (th - s1.y) * 0.5f};
            p.size1 = s1;
            p.pos2  = {s1.x, (th - s2.y) * 0.5f};
            p.size2 = s2;
            break;
        }
        case DisplayLayout::BigTopLittleBottom:
        case DisplayLayout::BigBottomLittleTop: {
            // Big screen gets 75% of height, little gets 25%
            float big_h  = avail.y * 0.75f;
            float lil_h  = avail.y * 0.25f;
            bool big_is_top = (layout == DisplayLayout::BigTopLittleBottom);
            ImVec2 nb = big_is_top ? n1 : n2;
            ImVec2 nl = big_is_top ? n2 : n1;
            float scale_b = fit(avail.x, big_h, nb.x, nb.y);
            float scale_l = fit(avail.x, lil_h, nl.x, nl.y);
            ImVec2 sb = {nb.x * scale_b, nb.y * scale_b};
            ImVec2 sl = {nl.x * scale_l, nl.y * scale_l};
            float tw = std::max(sb.x, sl.x);
            p.total_size = {tw, sb.y + sl.y};
            if (big_is_top) {
                p.pos1  = {(tw - sb.x) * 0.5f, 0};     p.size1 = sb;
                p.pos2  = {(tw - sl.x) * 0.5f, sb.y};  p.size2 = sl;
            } else {
                p.pos2  = {(tw - sb.x) * 0.5f, 0};     p.size2 = sb;
                p.pos1  = {(tw - sl.x) * 0.5f, sb.y};  p.size1 = sl;
            }
            break;
        }
    }
    return p;
}

// Draw both screens inside an already-open ImGui window.
// content_origin is the screen-space position of the top-left of the draw area.
static void render_dual_screens(full_system *fsys, ImGuiIO& io, void *nearest_sampler,
                                 ImVec2 content_origin, const DualScreenPlacement& pl, bool btn_down)
{
    // Screen 1 (top / primary — no touch)
    ImGui::SetCursorScreenPos({content_origin.x + pl.pos1.x, content_origin.y + pl.pos1.y});
    bool nn = fsys->output.nearest_neighbor || fsys->output2.nearest_neighbor;
    if (nn) push_nearest_sampler(nearest_sampler);
    ImGui::Image(fsys->output.backbuffer_texture.for_image(), pl.size1,
                 fsys->output.with_overscan.uv0, fsys->output.with_overscan.uv1);

    // Screen 2 (bottom / touch screen)
    ImGui::SetCursorScreenPos({content_origin.x + pl.pos2.x, content_origin.y + pl.pos2.y});
    ImGui::Image(fsys->output2.backbuffer_texture.for_image(), pl.size2,
                 fsys->output2.with_overscan.uv0, fsys->output2.with_overscan.uv1);
    if (nn) pop_sampler();

    // Touch: map mouse → screen2 native coordinates
    if (fsys->io.touchscreen.vec && pl.size2.x > 0 && pl.size2.y > 0) {
        JSM_DISPLAY *d2 = fsys->output2.display;
        float nw = d2 ? d2->pixelometry.cols.visible : 256.f;
        float nh = d2 ? d2->pixelometry.rows.visible : 192.f;
        float lx = io.MousePos.x - content_origin.x - pl.pos2.x;
        float ly = io.MousePos.y - content_origin.y - pl.pos2.y;
        i32 tx = (i32)(lx * nw / pl.size2.x);
        i32 ty = (i32)(ly * nh / pl.size2.y);
        fsys->update_touch(tx, ty, btn_down);
    }
}

static void render_debugger_widget(debugger_widget &widget);  // defined later in this file

static std::pair<ImVec2,ImVec2> render_emu_window(full_system *fsys, ImGuiIO& io, void *nearest_sampler, AppSettings& settings, const char* core_key, bool handle_touch = true)
{
    if (!fsys || !fsys->sys) return {{},{}};
    ImVec2 win_pos{}, win_size{};

    bool composite = fsys->uses_composite_layout();

    // Native (1x) sizes for primary screen and (if composite) secondary screen
    ImVec2 n1 = fsys->output_size();
    ImVec2 n2 = {};
    if (fsys->has_second_display() && fsys->output2.display)
        n2 = {fsys->output2.with_overscan.x_size, fsys->output2.with_overscan.y_size};

    // In composite mode the "native" window size is the 1:1 bounding box of both screens.
    // Do NOT use compute_dual_layout for this — that function fits to an available area;
    // passing a huge avail gives a huge scaled result, not the 1x natural size.
    ImVec2 native = n1;
    if (composite && n2.x > 0 && n2.y > 0) {
        switch (fsys->current_layout) {
            case DisplayLayout::SideBySide:
                native = {n1.x + n2.x, std::max(n1.y, n2.y)};
                break;
            default: // TopBottom, Swapped, BigTop, BigBottom all stack vertically
                native = {std::max(n1.x, n2.x), n1.y + n2.y};
                break;
        }
    }

    const ImGuiStyle& style = ImGui::GetStyle();
    float bottom_ctrl_h = ImGui::GetFrameHeightWithSpacing();  // NN + hide_overscan row
    for (auto& pw : fsys->present_widgets) {
        if (pw.widget.kind == JSMD_radiogroup)
            bottom_ctrl_h += ImGui::GetTextLineHeightWithSpacing() * 3.0f;
        else
            bottom_ctrl_h += ImGui::GetFrameHeightWithSpacing();
    }
    float deco_h = ImGui::GetFrameHeight()                  // title bar
                 + style.WindowPadding.y * 2.0f             // top + bottom padding
                 + ImGui::GetFrameHeightWithSpacing()       // top controls row (1:1, layout)
                 + ImGui::GetTextLineHeightWithSpacing()    // status message row
                 + bottom_ctrl_h                            // bottom display controls
#ifndef NDEBUG
                 + ImGui::GetTextLineHeightWithSpacing()    // COORDS row (debug only)
#endif
                 ;

    // Build title — composite mode drops the per-screen label, SeparateWindows keeps "Top"
    char window_title[256];
    if (!composite && fsys->has_second_display() && fsys->output.display && fsys->output.display->label[0]) {
        snprintf(window_title, sizeof(window_title), "%s %s  |  FPS: %.1f###%s_%s",
                 fsys->sys->label, fsys->output.display->label,
                 fsys->actual_fps,
                 fsys->sys->label, fsys->output.display->label);
    } else {
        snprintf(window_title, sizeof(window_title), "%s  |  FPS: %.1f###%s",
                 fsys->sys->label, fsys->actual_fps, fsys->sys->label);
    }

    // On first appearance open at the saved per-system zoom
    if (native.x > 0 && native.y > 0) {
        float zoom = settings.get_play_window_zoom(fsys->sys->kind);
        ImGui::SetNextWindowSize(
            ImVec2(native.x * zoom + style.WindowPadding.x * 2.0f, native.y * zoom + deco_h),
            ImGuiCond_Appearing);
    }

    if (ImGui::Begin(window_title, nullptr,
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        win_pos  = ImGui::GetWindowPos();
        win_size = ImGui::GetWindowSize();
        bool window_appearing = ImGui::IsWindowAppearing();
        bool reset_zoom_clicked = false;

        // Layout controls — shown whenever a second display exists
        if (fsys->has_second_display()) {
            if (ImGui::Button(display_layout_name(fsys->current_layout))) {
                fsys->cycle_layout();
                fsys->save_layout_settings(settings);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click to cycle layout");
            ImGui::SameLine();
            if (ImGui::SmallButton("F1")) {
                fsys->set_layout(fsys->fav_layout[0]);
                fsys->save_layout_settings(settings);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", display_layout_name(fsys->fav_layout[0]));
            ImGui::SameLine();
            if (ImGui::SmallButton("F2")) {
                fsys->set_layout(fsys->fav_layout[1]);
                fsys->save_layout_settings(settings);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", display_layout_name(fsys->fav_layout[1]));
        }

        // Available content area (minus reserved rows)
        ImVec2 avail = ImGui::GetContentRegionAvail();
        avail.y -= ImGui::GetTextLineHeightWithSpacing();        // status message row (always reserved)
        avail.y -= bottom_ctrl_h;                               // NN + hide_overscan + present_widgets
#ifndef NDEBUG
        avail.y -= ImGui::GetTextLineHeightWithSpacing();        // COORDS row (debug only)
#endif

        float scale = 1.0f;
        i32 native_x = 0, native_y = 0;
        bool btn_down = ImGui::IsMouseDown(0);

        if (composite && n2.x > 0 && n2.y > 0) {
            // ── Composite: both screens drawn inside this one window ─────────
            DualScreenPlacement pl = compute_dual_layout(fsys->current_layout, avail, n1, n2);
            ImVec2 content_origin = ImGui::GetCursorScreenPos();
            render_dual_screens(fsys, io, nearest_sampler, content_origin, pl, btn_down);
            // Advance cursor past the composite block so the status row renders below
            ImGui::SetCursorScreenPos({content_origin.x, content_origin.y + pl.total_size.y});
            if (native.x > 0 && native.y > 0)
                scale = std::min(avail.x / native.x, avail.y / native.y);
        } else {
            // ── Single screen (SeparateWindows mode or no second display) ────
            if (native.x > 0.0f && native.y > 0.0f)
                scale = std::min(avail.x / native.x, avail.y / native.y);
            if (scale <= 0.0f) scale = 1.0f;

            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {0,0});
            if (fsys->output.nearest_neighbor) push_nearest_sampler(nearest_sampler);
            ImGui::ImageButton("RENDER GAME", fsys->output.backbuffer_texture.for_image(),
                               ImVec2(native.x * scale, native.y * scale),
                               fsys->output_uv0(), fsys->output_uv1());
            if (fsys->output.nearest_neighbor) pop_sampler();
            ImGui::PopStyleVar();

            ImVec2 img_min = ImGui::GetItemRectMin();
            native_x = (scale > 0.0f) ? (i32)((io.MousePos.x - img_min.x) / scale) : 0;
            native_y = (scale > 0.0f) ? (i32)((io.MousePos.y - img_min.y) / scale) : 0;
            if (handle_touch)
                fsys->update_touch(native_x, native_y, btn_down);
        }

        if (scale <= 0.0f) scale = 1.0f;
        if (!reset_zoom_clicked && !window_appearing && native.x > 0.0f && native.y > 0.0f && !ImGui::IsMouseDown(0)) {
            float saved_zoom = settings.get_play_window_zoom(fsys->sys->kind);
            if (scale > saved_zoom + 0.01f || scale < saved_zoom - 0.01f)
                settings.set_play_window_zoom(fsys->sys->kind, scale);
        }

        // Status message row — space always reserved; text fades over 3 s then clears
        if (fsys->status_msg[0]) {
            double elapsed = ImGui::GetTime() - fsys->status_msg_time;
            if (elapsed < 3.0) {
                float alpha = (elapsed > 2.0) ? (float)(1.0 - (elapsed - 2.0)) : 1.0f;
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 1.0f, 0.8f, alpha));
                ImGui::TextUnformatted(fsys->status_msg);
                ImGui::PopStyleColor();
            } else {
                fsys->status_msg[0] = '\0';
                ImGui::NewLine();
            }
        } else {
            ImGui::NewLine();
        }

        // Bottom display controls: 1:1, NN, hide overscan, core-specific present_widgets
        {
            if (ImGui::Button("1:1##bottom")) {
                settings.set_play_window_zoom(fsys->sys->kind, 1.0f);
                if (native.x > 0 && native.y > 0)
                    ImGui::SetWindowSize(ImVec2(native.x + style.WindowPadding.x * 2.0f, native.y + deco_h));
                reset_zoom_clicked = true;
            }
            ImGui::SameLine();
            bool nn = fsys->output.nearest_neighbor;
            if (ImGui::Checkbox("Nearest Neighbor", &nn)) {
                fsys->output.nearest_neighbor = nn;
                if (core_key) settings.set_core_option(core_key, "nearest_neighbor", nn ? 1 : 0);
            }
            if (!composite && fsys->can_hide_overscan()) {
                ImGui::SameLine();
                bool ho = fsys->output.hide_overscan;
                if (ImGui::Checkbox("Hide Overscan", &ho)) {
                    fsys->output.hide_overscan = ho;
                    if (core_key) settings.set_core_option(core_key, "hide_overscan", ho ? 1 : 0);
                }
            }
            for (auto& pw : fsys->present_widgets) {
                u32 old_val = (pw.widget.kind == JSMD_radiogroup)
                    ? pw.widget.radiogroup.value : pw.widget.checkbox.value;
                render_debugger_widget(pw.widget);
                u32 new_val = (pw.widget.kind == JSMD_radiogroup)
                    ? pw.widget.radiogroup.value : pw.widget.checkbox.value;
                if (new_val != old_val && core_key)
                    settings.set_core_option(core_key, pw.ini_key, (i32)new_val);
            }
        }

#ifndef NDEBUG
        if (native_x < 0 || native_y < 0)
            ImGui::Text("COORDS: invalid");
        else
            ImGui::Text("COORDS: %d %d", native_x, native_y);
#endif
    }
    ImGui::End();
    return {win_pos, win_size};
}

static void render_emu_window2(full_system *fsys, ImGuiIO& io, void *nearest_sampler, AppSettings& settings, ImVec2 sibling_pos, ImVec2 sibling_size)
{
    if (!fsys || !fsys->sys || !fsys->has_second_display()) return;

    auto &out = fsys->output2;
    if (!out.display || !out.backbuffer_backer) return;

    ImVec2 native = {out.with_overscan.x_size, out.with_overscan.y_size};
    const ImGuiStyle& style = ImGui::GetStyle();
    float deco_h = ImGui::GetFrameHeight()
                 + style.WindowPadding.y * 2.0f
                 + ImGui::GetFrameHeightWithSpacing()
                 + ImGui::GetTextLineHeightWithSpacing()
#ifndef NDEBUG
                 + ImGui::GetTextLineHeightWithSpacing()
#endif
                 ;

    char window_title[256];
    snprintf(window_title, sizeof(window_title), "%s %s###%s_%s",
             fsys->sys->label, out.display->label,
             fsys->sys->label, out.display->label);

    if (native.x > 0 && native.y > 0) {
        float zoom = settings.get_play_window_zoom(fsys->sys->kind);
        if (sibling_size.x > 0)
            ImGui::SetNextWindowPos(
                ImVec2(sibling_pos.x + sibling_size.x + 4, sibling_pos.y),
                ImGuiCond_Appearing);
        ImGui::SetNextWindowSize(
            ImVec2(native.x * zoom + style.WindowPadding.x * 2.0f, native.y * zoom + deco_h),
            ImGuiCond_Appearing);
    }

    if (ImGui::Begin(window_title, nullptr,
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        ImVec2 avail = ImGui::GetContentRegionAvail();
        avail.y -= ImGui::GetTextLineHeightWithSpacing();
#ifndef NDEBUG
        avail.y -= ImGui::GetTextLineHeightWithSpacing();
#endif
        float scale = 1.0f;
        if (native.x > 0.0f && native.y > 0.0f)
            scale = std::min(avail.x / native.x, avail.y / native.y);
        if (scale <= 0.0f) scale = 1.0f;

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {0,0});
        if (out.nearest_neighbor) push_nearest_sampler(nearest_sampler);
        ImGui::ImageButton("RENDER GAME 2", out.backbuffer_texture.for_image(),
                           ImVec2(native.x * scale, native.y * scale),
                           out.with_overscan.uv0, out.with_overscan.uv1);
        if (out.nearest_neighbor) pop_sampler();
        ImGui::PopStyleVar();

        ImVec2 img_min = ImGui::GetItemRectMin();
        i32 native_x = (scale > 0.0f) ? (i32)((io.MousePos.x - img_min.x) / scale) : 0;
        i32 native_y = (scale > 0.0f) ? (i32)((io.MousePos.y - img_min.y) / scale) : 0;
        bool btn_down = ImGui::IsMouseDown(0);
        fsys->update_touch(native_x, native_y, btn_down);

        if (ImGui::Button("1:1##win2")) {
            if (native.x > 0 && native.y > 0)
                ImGui::SetWindowSize(ImVec2(native.x + style.WindowPadding.x * 2.0f, native.y + deco_h));
        }
        ImGui::SameLine();
        ImGui::Checkbox("Nearest Neighbor", &out.nearest_neighbor);
#ifndef NDEBUG
        if (native_x < 0 || native_y < 0)
            ImGui::Text("COORDS: invalid");
        else
            ImGui::Text("COORDS: %d %d", native_x, native_y);
#endif
    }
    ImGui::End();
}

static void render_emu_fullscreen(full_system *fsys, ImGuiIO& io, void *nearest_sampler)
{
    if (!fsys || !fsys->sys) return;

    float menu_h  = ImGui::GetFrameHeight();
    float avail_w = io.DisplaySize.x;
    float avail_h = io.DisplaySize.y - menu_h;

    ImGui::SetNextWindowPos(ImVec2(0, menu_h));
    ImGui::SetNextWindowSize(ImVec2(avail_w, avail_h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,    ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 1));
    ImGui::Begin("##emu_fs", nullptr,
                 ImGuiWindowFlags_NoDecoration  |
                 ImGuiWindowFlags_NoMove        |
                 ImGuiWindowFlags_NoScrollbar   |
                 ImGuiWindowFlags_NoSavedSettings |
                 ImGuiWindowFlags_NoBringToFrontOnFocus |
                 ImGuiWindowFlags_NoNav);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);

    bool btn_down = ImGui::IsMouseDown(0);

    if (fsys->has_second_display() && fsys->output2.display && fsys->output2.with_overscan.x_size > 0) {
        // ── Dual-screen composite layout ─────────────────────────────────────
        ImVec2 n1 = fsys->output_size();
        ImVec2 n2 = {fsys->output2.with_overscan.x_size, fsys->output2.with_overscan.y_size};
        // SeparateWindows has no meaning in fullscreen — treat as TopBottom
        DisplayLayout layout = fsys->current_layout;
        if (layout == DisplayLayout::SeparateWindows)
            layout = DisplayLayout::TopBottom;

        DualScreenPlacement pl = compute_dual_layout(layout, {avail_w, avail_h}, n1, n2);
        float off_x = floorf((avail_w - pl.total_size.x) * 0.5f);
        float off_y = floorf((avail_h - pl.total_size.y) * 0.5f);
        ImVec2 content_origin = {off_x, menu_h + off_y};

        render_dual_screens(fsys, io, nearest_sampler, content_origin, pl, btn_down);

        // Status overlay — bottom-left of the composite area
        if (fsys->status_msg[0]) {
            double elapsed = ImGui::GetTime() - fsys->status_msg_time;
            if (elapsed < 3.0) {
                float alpha = (elapsed > 2.0f) ? (float)(1.0 - (elapsed - 2.0)) : 1.0f;
                ImGui::SetCursorScreenPos(ImVec2(off_x + 8.0f,
                    menu_h + off_y + pl.total_size.y - ImGui::GetFrameHeight() - 4.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 1.0f, 0.8f, alpha));
                ImGui::TextUnformatted(fsys->status_msg);
                ImGui::PopStyleColor();
            } else {
                fsys->status_msg[0] = '\0';
            }
        }
    } else {
        // ── Single screen ─────────────────────────────────────────────────────
        auto& v = fsys->should_hide_overscan()
            ? fsys->output.without_overscan
            : fsys->output.with_overscan;
        float aspect = (v.y_size > 0.0f) ? (v.x_size / v.y_size) : (4.0f / 3.0f);
        float img_w = avail_w;
        float img_h = avail_w / aspect;
        if (img_h > avail_h) { img_h = avail_h; img_w = avail_h * aspect; }
        float off_x = floorf((avail_w - img_w) * 0.5f);
        float off_y = floorf((avail_h - img_h) * 0.5f);

        ImGui::SetCursorPos(ImVec2(off_x, off_y));
        if (fsys->output.nearest_neighbor) push_nearest_sampler(nearest_sampler);
        ImGui::Image(fsys->output.backbuffer_texture.for_image(),
                     ImVec2(img_w, img_h),
                     fsys->output_uv0(), fsys->output_uv1());
        if (fsys->output.nearest_neighbor) pop_sampler();

        // Status message overlay (bottom-left of the image)
        if (fsys->status_msg[0]) {
            double elapsed = ImGui::GetTime() - fsys->status_msg_time;
            if (elapsed < 3.0) {
                float alpha = (elapsed > 2.0f) ? (float)(1.0 - (elapsed - 2.0)) : 1.0f;
                ImGui::SetCursorPos(ImVec2(off_x + 8.0f,
                                           off_y + img_h - ImGui::GetFrameHeight() - 4.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 1.0f, 0.8f, alpha));
                ImGui::TextUnformatted(fsys->status_msg);
                ImGui::PopStyleColor();
            } else {
                fsys->status_msg[0] = '\0';
            }
        }
    }

    ImGui::End();
}

#define MEMORY_VIEW_DEFAULT_ENABLE 0
#define EVENT_VIEWER_DEFAULT_ENABLE 0
#define DISASM_VIEW_DEFAULT_ENABLE 0
#define SOURCE_LIST_VIEW_DEFAULT_ENABLE 0
#define IMAGE_VIEW_DEFAULT_ENABLE 0
#define DBGLOG_VIEW_DEFAULT_ENABLE 0
#define TRACE_VIEW_DEFAULT_ENABLE 0
#define CONSOLE_VIEW_DEFAULT_ENABLE 0
#define WAVEFORM_VIEW_DEFAULT_ENABLE 0

int hexfilter(ImGuiInputTextCallbackData *data)
{
    u32 c = data->EventChar;
    if (!(((c >= '0') && (c <= '9')) || ((c >= 'a') && (c <= 'f')) || ((c >= 'A') && (c <= 'F')))) return 1;

    return 0;
}

void imgui_jocasta_app::render_memory_view() {
    if (fsys && fsys->memory.view && fsys->debugger_setup) {
        managed_window *mw = register_managed_window(0x30000, mwk_debug_memory, "Memory Viewer",
                                                            MEMORY_VIEW_DEFAULT_ENABLE);
        memory_view *mv = fsys->memory.view;
        u32 num_modules = mv->num_modules();
        if (mw->enabled && num_modules > 0) {
            if (begin_managed_window(mw, "Memory Viewer")) {
                // Dropdown, numeric input, table
                static ImGuiComboFlags flags = 0;
                char *module_names[50];
                for (u32 i = 0; i < num_modules; i++) {
                    memory_view_module *mm = mv->get_module(i);
                    assert(mm);
                    module_names[i] = mm->name;
                }
                int item_selected = mv->current_id;
                if (ImGui::BeginCombo("Memory Select", module_names[mv->current_id], flags)) {
                    for (int n = 0; n < num_modules; n++) {
                        const bool is_selected = (item_selected == n);
                        if (ImGui::Selectable(module_names[n], is_selected))
                            item_selected = n;

                        // Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
                        if (is_selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                u32 old_selected = mv->current_id;
                mv->current_id = item_selected;
                memory_view_module *mm = mv->get_module(mv->current_id);
                if (old_selected != item_selected) {
                    mv->display_start_addr &= (1 << (4 * mm->addr_digits)) - 1;
                }


                char format_string[20];
                snprintf(format_string, 20, "%%0%dx", mm->addr_digits);

                // Now get input
                //ImGui::SameLine();
                static char addr_str[18];
                snprintf(addr_str, 18, format_string, mv->display_start_addr);
                mm->input_buf = addr_str;
                u32 old_entered_address = mv->display_start_addr;

                ImGui::InputText("Addr", addr_str, 18, ImGuiInputTextFlags_CallbackCharFilter, &hexfilter, mm);
                addr_str[mm->addr_digits] = 0;
                u32 newly_entered_address = strtol(addr_str, nullptr, 16) & 0xFFFFFFF0;
                mv->display_start_addr = newly_entered_address;
                u32 entered_address_changed = mv->display_start_addr != old_entered_address;

                u32 old_top_displayed_line = mv->display_start_addr << 4;

                int item_selected_text = mm->text_views.current;
                if (mm->text_views.num > 1) {
                    if (ImGui::BeginCombo("Text Char. Set", mm->text_views.names[mm->text_views.current], flags)) {
                        for (int n = 0; n < mm->text_views.num; n++) {
                            const bool is_selected = (item_selected_text == n);
                            if (ImGui::Selectable(mm->text_views.names[n], is_selected))
                                item_selected_text = n;

                            // Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
                            if (is_selected)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    mm->text_views.current = item_selected_text;
                }


                // OK start clipper
                static ImGuiTableFlags itflags =
                        ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter |
                        ImGuiTableFlags_BordersV | ImGuiTableFlags_SizingStretchProp;

                if (ImGui::BeginTable("mem_view_table", 3, itflags)) {
                    u32 top_displayed_line = 0xFFFFFFFF;
                    ImGui::TableSetupScrollFreeze(0, 1); // Make top row always visible
                    ImGui::TableSetupColumn("Addr", ImGuiTableColumnFlags_None, mm->addr_digits);
                    ImGui::TableSetupColumn("Data", ImGuiTableColumnFlags_None, 32 + 16);
                    ImGui::TableSetupColumn("Text", ImGuiTableColumnFlags_None, 17);
                    ImGui::TableHeadersRow();
                    ImGuiListClipper clipper;
                    u32 num_lines = ((mm->addr_end - mm->addr_start) + 1) >> 4;
                    clipper.Begin(num_lines);
                    char hex_buf[50];
                    char ascii_buf[18];
                    u32 old_end = 0;
                    u32 j = 0;
                    while (clipper.Step()) {
                        if (j == 1) {
                            if ((old_end == 1) && (clipper.DisplayStart == 1)) top_displayed_line = 0;
                            else if (clipper.DisplayStart < top_displayed_line) top_displayed_line = clipper.DisplayStart;
                        }

                        old_end = clipper.DisplayEnd;
                        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++) {
                            u8 data_buf[16];
                            u32 addr = (row << 4) - mm->addr_start;
                            memory_view_get_line(mm, addr, reinterpret_cast<char *>(data_buf));
                            hex_buf[0] = 0;
                            ascii_buf[0] = 0;
                            char *hp = hex_buf;
                            char *ap = ascii_buf;
                            for (u32 i = 0; i < 16; i++) {
                                u32 db = data_buf[i];
                                assert(db < 256);
                                snprintf(hp, 50 - (i * 3), "%02X ", db);
                                hp += 3;

                                if (mm->render_ascii) {
                                    db = mm->render_ascii(mm->render_ascii_ptr, item_selected_text, db);
                                }
                                else {
                                    if ((db >= 32) && (db <= 126)) {

                                    }
                                    else {
                                        db = '.';
                                    }
                                }
                                snprintf(ap, 18 - i, "%c", db);
                                ap++;
                            }
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::Text(format_string, row << 4);

                            ImGui::TableSetColumnIndex(1);
                            ImGui::Text(" %s", hex_buf);

                            ImGui::TableSetColumnIndex(2);
                            ImGui::Text(" %s", ascii_buf);
                        }
                        j++;
                    }
                    top_displayed_line <<= 4;
                    static u32 old_displayed_line_gui = 0;
                    if (old_top_displayed_line != top_displayed_line) {
                        // Address changed here...
                        mv->display_start_addr = top_displayed_line;
                    }
                    old_displayed_line_gui = top_displayed_line;
                    if (entered_address_changed) {
                        mv->display_start_addr = newly_entered_address;
                    }
                    float scrl = clipper.ItemsHeight * static_cast<float>(mv->display_start_addr >> 4);
                    float cur_scroll = ImGui::GetScrollY();
                    if (cur_scroll != scrl) {
                        ImGui::SetScrollY(scrl);
                    }

                    ImGui::EndTable();
                }
            }
            ImGui::End(); // end window
        }
    }

}

void imgui_jocasta_app::render_event_view()
{
    managed_window *mw = register_managed_window(0x25, mwk_debug_events, "Event Viewer", EVENT_VIEWER_DEFAULT_ENABLE);
    if (mw->enabled && fsys && fsys->events.view && fsys->has_played_once) {
        if (begin_managed_window(mw, "Event Viewer")) {
            static bool ozoom = false;
            ImGui::Checkbox("2x Zoom", &ozoom);
            fsys->events_view_present();
            ImGui::Image(fsys->events.texture.for_image(), fsys->events.texture.zoom_sz_for_display(ozoom ? 2 : 1),
                         fsys->events.texture.uv0, fsys->events.texture.uv1, {1.0, 1.0, 1.0, 1.0}, {1.0, 1.0, 1.0, 1.0});
            static bool things_open[50];
            static float color_edits[50 * 10][3];
            u32 idx = 0;
            for (auto &cat:fsys->events.view->categories) {
                ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen;
                if (ImGui::TreeNodeEx(cat.name, flags)) {
                    for (u32 evi = 0; evi < fsys->events.view->events.size(); evi++) {
                        auto &event = fsys->events.view->events.at(evi);
                        if (event.category_id == cat.id) {
                            color_edits[idx][0] = (float) (event.color & 0xFF) / 255.0;
                            color_edits[idx][1] = (float) ((event.color >> 8) & 0xFF) / 255.0;
                            color_edits[idx][2] = (float) ((event.color >> 16) & 0xFF) / 255.0;
                            bool mval = event.display_enabled;
                            ImGui::PushID(evi*2);
                            ImGui::Checkbox("", &mval);
                            event.display_enabled = mval;
                            ImGui::PopID();
                            ImGui::SameLine();
                            ImGui::PushID((evi*2)+1);
                            ImGui::ColorEdit3(event.name, color_edits[idx], ImGuiColorEditFlags_NoInputs);
                            ImGui::PopID();
                            idx++;
                        }
                    }
                    ImGui::TreePop();
                }
            }
        }
        ImGui::End(); // end window
    }
}

static bool rn_checkboxes[MAX_DBGLOG_IDS*4];
static bool rn_checkboxes_break[MAX_DBGLOG_IDS*4];

static bool render_node(dbglog_view &view, dbglog_category_node &node, u32 *id_ptr, u64 cur_time) {
    // If we're a leaf...
    u32 id = (*id_ptr)++;
    bool any_checked = false;
    if (!node.children.empty() == 0) {
        // We are a leaf
        rn_checkboxes[id] = node.enabled;
        rn_checkboxes_break[id] = node.break_on_fire;
        ImGui::Checkbox(node.name, &rn_checkboxes[id]);
        ImGui::SameLine();
        char foo[1024];
        i64 r = static_cast<i64>(view.id_to_last_fires[node.category_id]);
        if (r != 0) {
            i64 d = static_cast<i64>(cur_time) - r;
            snprintf(foo, sizeof(foo), "(break) (last:%lld)", d);
        }
        else {
            snprintf(foo, sizeof(foo), "(break) (last:never)");
        }
        ImGui::PushID(id+400);
        ImGui::Checkbox(foo, &rn_checkboxes_break[id]);
        ImGui::PopID();
        node.enabled = rn_checkboxes[id];
        any_checked |= node.enabled;

        node.break_on_fire = rn_checkboxes_break[id];
        view.ids_enabled[node.category_id] = rn_checkboxes[id];
        view.id_break[node.category_id] = rn_checkboxes_break[id];
    } else {
        // We are a further branch
        //ImGui::SetNextItemOpen(true);
        ImGui::PushID(id);
        bool my_checked = false;
        if (ImGui::TreeNodeEx(node.name)) {
            for (auto &e : node.children) {
                my_checked |= render_node(view, e, id_ptr, cur_time);
            }
            ImGui::PushID(id + 1200);
            bool old_cbox = my_checked;
            ImGui::Checkbox("((multiselect))", &my_checked);
            ImGui::PopID();
            ImGui::TreePop();
            if (old_cbox != my_checked) {
                for (auto &e: node.children) {
                    e.enabled = my_checked;
                }
            }
            any_checked |= my_checked;
        }
        ImGui::PopID();
    }
    return any_checked;
}

static ImVec4 get_iv4(u32 color)
{
    return {static_cast<float>((color >> 16) & 0xFF) / 255.0f,
                  static_cast<float>((color >> 8) & 0xFF) / 255.0f,
                  static_cast<float>((color) & 0xFF) / 255.0f,
                  1.0f};
}

void imgui_jocasta_app::render_dbglog_view(DLVIEW &dview, bool update_dasm_scroll, u64 cur_time, managed_window *mw) {
    // 2 views
    char wname[100];
    dbglog_view &dlv = dview.view->dbglog;
    snprintf(wname, sizeof(wname), "%s (visibility tree)", dlv.name);
    if (begin_managed_window(mw, wname)) {
        // Now do a tree of checkboxes!!!
        u32 bid = 0;
        dbglog_category_node &root = dlv.get_category_root();
        bool any_checked = false;
        for (auto &c : root.children) {
            any_checked |= render_node(dlv, c, &bid, cur_time);
        }
    }
    ImGui::End();
    if (!mw->enabled) return;

    snprintf(wname, sizeof(wname), "%s", dlv.name);
    static char text[8 * 1024 * 1024];
    static int first = 1;
    if (first) {
        first = 0;
        memset(text, 0, 10);
    }
    if (begin_managed_window(mw, wname)) {
        static ImGuiTableFlags flags =
                ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter |
                ImGuiTableFlags_BordersV | ImGuiTableFlags_SizingStretchProp;
        if (ImGui::BeginTable("tabley_table", dlv.has_extra ? 5 : 4, flags)) {
            ImGui::TableSetupScrollFreeze(0, 1); // Make top row always visible
            ImGui::TableSetupColumn("Category", ImGuiTableColumnFlags_None, 1);
            ImGui::TableSetupColumn("Timecode", ImGuiTableColumnFlags_None, 1);
            ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_None, 1);
            ImGui::TableSetupColumn("Entry", ImGuiTableColumnFlags_None, 5);
            if (dlv.has_extra)
                ImGui::TableSetupColumn("Extra", ImGuiTableColumnFlags_None, 5);

            ImGui::TableHeadersRow();

            ImGuiListClipper clipper;
            u32 num_items = dlv.count_visible_lines();

            /*if (dlv.updated) {
                dlv.updated = 0;
                float scrl = clipper.ItemsHeight * num_items;
                float cur_scroll = ImGui::GetScrollY();
                printf("\nUPDATED! cur_scroll:%f scrl:%f IH:%f", cur_scroll, scrl, clipper.ItemsHeight * 8);
                //if ((cur_scroll > scrl) || (scrl < (cur_scroll + (clipper.ItemsHeight * 8))))
                    ImGui::SetScrollY(10);
            }*/

            clipper.Begin(num_items);
            while (clipper.Step()) {
                u32 idx = dlv.get_nth_visible(clipper.DisplayStart);
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++) {
                    ImGui::TableNextRow();
                    dbglog_entry *e = &dlv.items.data[idx];
                    dbglog_category_node *parent = dlv.id_to_category[e->category_id]->parent;

                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextColored(get_iv4(parent->color), "%s", parent->name);

                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%lld", e->timecode);

                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextColored(get_iv4(dlv.id_to_color[e->category_id]), "%s", dlv.id_to_category[e->category_id]->short_name);

                    ImGui::TableSetColumnIndex(3);
                    ImGui::TextColored(get_iv4(dlv.id_to_color[e->category_id]), "%s", e->text.ptr);

                    if (dlv.has_extra) {
                        ImGui::TableSetColumnIndex(4);
                        ImGui::TextColored(get_iv4(dlv.id_to_color[e->category_id]), "%s", e->extra.ptr);
                    }
                    idx = dlv.get_next_visible(idx);
                }
                if (dlv.updated) {
                    dlv.updated--;
                    ImGuiContext& g = *GImGui;
                    ImGuiWindow* window = g.CurrentWindow;
                    ImGui::SetScrollY(window, window->ScrollMax.y);
                }
            }
            ImGui::EndTable();

        }
    }
    ImGui::End();
}


void imgui_jocasta_app::render_disassembly_view(DVIEW &dview, bool update_dasm_scroll, u32 num) {
    disassembly_view *dasm = &dview.view->disassembly;
    std::vector<disassembly_entry_strings> &dasm_rows = dview.dasm_rows;
    char wname[100];
    snprintf(wname, sizeof(wname), "%s Disassembly View", dasm->processor_name.ptr);
    managed_window *mw = register_managed_window(0x400 + num, mwk_debug_disassembly, wname, DISASM_VIEW_DEFAULT_ENABLE);
    if (mw->enabled && fsys && fsys->enable_debugger) {
        if (begin_managed_window(mw, wname)) {
            static ImGuiTableFlags flags =
                    ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter |
                    ImGuiTableFlags_BordersV | ImGuiTableFlags_Resizable;


            static const float TEXT_BASE_WIDTH = ImGui::CalcTextSize("A").x;
            static const float TEXT_BASE_HEIGHT = ImGui::GetTextLineHeightWithSpacing();

            disassembly_vars dv = dasm->get_disassembly_vars.func(dasm->get_disassembly_vars.ptr, *dasm);
            u32 cur_line_num = dasm->get_rows(dv.address_of_executing_instruction,
                                                         20,
                                                         100, dasm_rows);
            u32 numcols = (dasm ? dasm->has_context ? 3 : 2 : 2);

            // When using ScrollX or ScrollY we need to specify a size for our table container!
            // Otherwise by default the table will fit all available space, like a BeginChild() call.
            ImVec2 outer_size = ImVec2(TEXT_BASE_WIDTH * 80, TEXT_BASE_HEIGHT * 20);
            if (ImGui::BeginTable("table_dasm", numcols, flags, outer_size)) {
                ImGui::TableSetupScrollFreeze(0, 1); // Make top row always visible
                ImGui::TableSetupColumn("addr", ImGuiTableColumnFlags_None);
                ImGui::TableSetupColumn("disassembly", ImGuiTableColumnFlags_None);
                if (numcols == 3) ImGui::TableSetupColumn("context at last execution", ImGuiTableColumnFlags_None);
                ImGui::TableHeadersRow();
                ImGuiListClipper clipper;
                if (update_dasm_scroll) {
                    float scrl = clipper.ItemsHeight * (cur_line_num - 2);
                    float cur_scroll = ImGui::GetScrollY();
                    if ((cur_scroll > scrl) || (scrl < (cur_scroll + (clipper.ItemsHeight * 8))))
                        ImGui::SetScrollY(scrl);
                }
                clipper.Begin(100);
                while (clipper.Step()) {
                    for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++) {
                        ImGui::TableNextRow();
                        auto &strs = dasm_rows.at(row);
                        //if (!strs) continue;

                        ImGui::TableSetColumnIndex(0);
                        ImGui::Selectable(strs.addr, row == cur_line_num,
                                          ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_Disabled);
                        //ImGui::Text("%s", strs.addr);

                        ImGui::TableSetColumnIndex(1);
                        ImGui::Text("%s", strs.dasm);
                        //ImGui::Selectable(strs.dasm, row == cur_line_num, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_Disabled);

                        if (numcols == 3) {
                            ImGui::TableSetColumnIndex(2);
                            ImGui::Text("%s", strs.context);
                        }
                    }
                }
                ImGui::EndTable();
                dasm->fill_view.func(dasm->fill_view.ptr, *dasm);

                ImGui::SameLine();
                outer_size = ImVec2(TEXT_BASE_WIDTH * 30, TEXT_BASE_HEIGHT * 20);
                if (ImGui::BeginTable("table_registers", 2, flags, outer_size)) {
                    ImGui::TableSetupScrollFreeze(0, 1); // Make top row always visible
                    ImGui::TableSetupColumn("register", ImGuiTableColumnFlags_None);
                    ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_None);
                    ImGui::TableHeadersRow();

                    ImGuiListClipper clipper;
                    clipper.Begin(dasm->cpu.regs.size());
                    while (clipper.Step()) {
                        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++) {
                            ImGui::TableNextRow();
                            auto &ctx = dasm->cpu.regs.at(row);

                            ImGui::TableSetColumnIndex(0);
                            ImGui::Text("%s", ctx.name);

                            ImGui::TableSetColumnIndex(1);
                            char rndr[50];
                            ctx.render(rndr, sizeof(rndr));
                            ImGui::Text("%s", rndr);
                        }
                    }
                    ImGui::EndTable();

                }

            }
        }
        ImGui::End();
    }
}

void imgui_jocasta_app::render_source_list_view(bool update_dasm_scroll) {
    if (fsys && fsys->source_listing.view) {
        source_listing::view &lv = *fsys->source_listing.view;
        char wname[100];
        snprintf(wname, sizeof(wname), "Source Listing");
        managed_window *mw = register_managed_window(0x6600, mwk_debug_source_list, wname, SOURCE_LIST_VIEW_DEFAULT_ENABLE);
        if (mw->enabled && fsys->enable_debugger) {
            if (begin_managed_window(mw, wname)) {
                static ImGuiTableFlags flags =
                        ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter |
                        ImGuiTableFlags_BordersV | ImGuiTableFlags_Resizable;
                source_listing::realtime_vars rv;;
                lv.get_realtime_vars.func(lv.get_realtime_vars.ptr, lv, rv);
                static u32 cur_line_num = 0;
                if (cur_line_num != rv.line_of_executing_instruction) {
                    update_dasm_scroll = true;
                    cur_line_num = rv.line_of_executing_instruction;
                }
                if (!rv.instruction_in_list) update_dasm_scroll = false;
                static float item_height = 1.0f;
                static const float TEXT_BASE_WIDTH = ImGui::CalcTextSize("A").x;
                static const float TEXT_BASE_HEIGHT = ImGui::GetTextLineHeightWithSpacing();
                ImVec2 outer_size = ImVec2(TEXT_BASE_WIDTH * 110, TEXT_BASE_HEIGHT * 30);
                if (ImGui::BeginTable("table_source_list", 2, flags, outer_size)) {
                    ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, TEXT_BASE_WIDTH * 8);
                    ImGui::TableSetupColumn("Listing", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableHeadersRow();
                    ImGuiListClipper clipper;
                    if (update_dasm_scroll) {
                        float scrl = item_height * (cur_line_num - 2);
                        float cur_scroll = ImGui::GetScrollY();
                        //if ((cur_scroll > scrl) || (scrl < (cur_scroll + (item_height * 8))))
                            ImGui::SetScrollY(scrl);
                    }
                    clipper.Begin(lv.lines.size());
                    char addrstr[50];
                    while (clipper.Step()) {
                        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++) {
                            ImGui::TableNextRow();
                            item_height = clipper.ItemsHeight;
                            ImGui::TableSetColumnIndex(0);
                            auto &l = lv.lines[row];
                            snprintf(addrstr, sizeof(addrstr), "%06x", l.addr+lv.base_addr);
                            ImGui::PushID(row);
                            ImGui::Selectable(addrstr, row == cur_line_num, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_Disabled);
                            ImGui::PopID();
                            ImGui::TableSetColumnIndex(1);
                            ImGui::Text("%s", lv.lines[row].text.ptr);
                            //ImGui::Text("%d / %ld", row, lv.lines.size());
                        }
                    }
                }
                ImGui::EndTable();
            }
            ImGui::End();
        }
    }
}

void imgui_jocasta_app::render_disassembly_views(bool update_dasm_scroll) {
    u32 i = 0;
    for (auto &dv : fsys->dasm_views) {
        render_disassembly_view(dv, update_dasm_scroll, i++);
    }
}

void imgui_jocasta_app::render_w2_tex(debug::waveform2::view_node &node) {
    auto *n = static_cast<W2FORM *>(node.user_ptr);
    fsys->waveform2_wf_present(*n);
    ImGui::SetNextWindowSizeConstraints(ImVec2(n->len, n->height+20),
                              ImVec2(n->len+20, n->height+30));
    auto *wf = &node.data;
    if (ImGui::BeginChild(wf->name, ImVec2(-FLT_MIN, 0.0f), ImGuiChildFlags_None)) {
        ImGui::Checkbox(wf->name, &wf->ch_output_solo);
        ImGui::Image(n->tex.for_image(), n->tex.sz_for_display, n->tex.uv0, n->tex.uv1);
    }
    ImGui::EndChild();
}

void imgui_jocasta_app::render_w2_node_line(debug::waveform2::view_node &nn, u32 &idpush) {
    u32 total_on_line = 0;
    u32 total_per_line = 400; // 2 medium (200x80) or 1 large (400x80) or 4 tiny(100x40)
    if (nn.children.size() > 9) total_per_line *= 2; // 2 large, 4 medium, 8 tiny
    if (nn.children.size() > 24) total_per_line += total_per_line >> 2; // 3 large, 8 medium, 16 tiny
    bool first = true;
    for (auto &node : nn.children) {
        bool make_new_line = false;
        auto *v = static_cast<W2FORM *>(node.user_ptr);
        total_on_line += v->len;
        if (total_on_line < total_per_line) {}
        else {
            make_new_line = true;
        }
        if (!make_new_line) {
            if (!first) ImGui::SameLine();
        }
        else {
            total_on_line = 0;
        }
        first = false;
        render_w2_tex(node);
    }
}

void imgui_jocasta_app::render_w2_node(debug::waveform2::view_node &node, u32 &idpush) {
    if (node.children.empty()) {
        // Leaf!
        render_w2_tex(node);
    }
    else {
        ImGui::PushID(idpush++);
        if (ImGui::TreeNodeEx(node.name)) {
            if (node.children[0].children.empty())
                render_w2_node_line(node, idpush);
            else
                render_w2_node(node, idpush);
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
}

void imgui_jocasta_app::render_waveform2_view(W2VIEW &wview, u32 num) {
    bool any_solo_next = false;
    managed_window *mw = register_managed_window(0x600 + num, mwk_debug_sound, wview.view->name, WAVEFORM_VIEW_DEFAULT_ENABLE);
    u32 idpush = 0;
    if (mw->enabled) {
        if (begin_managed_window(mw, wview.view->name)) {
            // Master view
            render_w2_tex(wview.view->root);

            // Nodes and trees!
            for (auto &node: wview.view->root.children) {
                render_w2_node(node, idpush);
            }
        }
        ImGui::End();
    }
}

void imgui_jocasta_app::render_waveform_view(WVIEW &wview, u32 num)
{
    managed_window *mw = register_managed_window(0x8600 + num, mwk_debug_sound, wview.view->name, WAVEFORM_VIEW_DEFAULT_ENABLE);
    if (mw->enabled) {
        fsys->waveform_view_present(wview);
        u32 num_per_line = 2;
        bool first = true;

        if (begin_managed_window(mw, wview.view->name)) {
            if (wview.waveforms[0].wf->default_clock_divider != 0) {
                if (wview.waveforms[0].wf->clock_divider == 0)
                    wview.waveforms[0].wf->clock_divider = wview.waveforms[0].wf->default_clock_divider;
                static int a;
                static bool rv = false;
                a = wview.waveforms[0].wf->clock_divider;
                ImGui::DragInt("Clock divider", &a, 0.5f, 10, 1500, "%d");
                /*
                ImGui::Checkbox("Randomly vary divider", &rv);
                static int b = 10;
                ImGui::DragInt("Vary by +/-", &b, 0.5f, 1, 100, "%d");
                if (rv) {
                    int rn = ((int)(arc4random() % (b * 2))) - b;
                    float perc = (float)rn / 100.0f;
                    float vary = perc * (float)wview.waveforms[0].wf->default_clock_divider;
                    wview.waveforms[0].wf->clock_divider = wview.waveforms[0].wf->default_clock_divider + vary;
                }
                else {*/
                wview.waveforms[0].wf->clock_divider = a;
                //}
            }
            u32 on_line = 0;
            u32 last_kind = 0;
            if (wview.waveforms.size() > 9) num_per_line = 4;
            if (wview.waveforms.size() > 17) num_per_line = 6;
            for (auto &wf: wview.waveforms) {
                bool make_new_line = false;
                switch (wf.wf->kind) {
                    case dwk_main:
                        on_line += 2;
                        break;
                    case dwk_channel:
                        on_line++;
                        break;
                    default:
                        assert(1 == 2);
                }

                if (on_line < num_per_line) {
                }
                else {
                    make_new_line = true;
                }
                if (last_kind == dwk_main) {
                    on_line = 0;
                } else if (!make_new_line && !first) ImGui::SameLine();
                first = false;
                if (on_line >= num_per_line) on_line = 0;
                last_kind = wf.wf->kind;
                ImGui::SetNextWindowSizeConstraints(ImVec2(wf.wf->samples_requested, wf.height),
                                                    ImVec2(wf.wf->samples_requested + 20, wf.height + 30));
                if (ImGui::BeginChild(wf.wf->name, ImVec2(-FLT_MIN, 0.0f), ImGuiChildFlags_None)) {
                    ImGui::Checkbox(wf.wf->name, &wf.output_enabled);
                    wf.wf->ch_output_enabled = wf.output_enabled;
                    ImGui::Image(wf.tex.for_image(), wf.tex.sz_for_display, wf.tex.uv0, wf.tex.uv1);
                }
                ImGui::EndChild();
            }
        }
        ImGui::End();
    }
}

static void render_radiogroup(debugger_widget &widget)
{
    if (widget.same_line) ImGui::SameLine();

    ImGuiWindowFlags window_flags = ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX;
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 5.0f);
    static int draw_lines = 2;
    static int max_height_in_lines = 4;

    ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, ImGui::GetTextLineHeightWithSpacing() * 2), ImVec2(FLT_MAX, ImGui::GetTextLineHeightWithSpacing() * max_height_in_lines));
    ImGui::BeginChild(widget.radiogroup.title, ImVec2(-FLT_MIN, 0.0f), window_flags);
    int ch = static_cast<int>(widget.radiogroup.value);
    //         ImGui::RadioButton("radio a", &e, 0); ImGui::SameLine();
    //        ImGui::RadioButton("radio b", &e, 1); ImGui::SameLine();
    //        ImGui::RadioButton("radio c", &e, 2);
    for (auto &cb : widget.radiogroup.buttons) {
        if (cb.same_line) ImGui::SameLine();
        ImGui::RadioButton(cb.checkbox.text, &ch, static_cast<int>(cb.checkbox.value));
    }
    widget.radiogroup.value = static_cast<u32>(ch);

    ImGui::EndChild();
    ImGui::PopStyleVar();
}

static void render_textbox(debugger_widget &widget)
{
    if (widget.same_line) ImGui::SameLine();
    ImGui::Text("%s", widget.textbox.contents.ptr);
}

static void render_colorkey(debugger_widget &widget)
{
    debugger_widget_colorkey *ck = &widget.colorkey;
    ImGui::Text("%s", ck->title);
    for (u32 i = 0; i < ck->num_items; i++) {
        debugger_widget_colorkey_item *item = &ck->items[i];
        ImGui::PushID(i);
        ImGui::BeginDisabled();
        u32 r = item->color & 0xFF;
        u32 g = (item->color >> 8) & 0xFF;
        u32 b = (item->color >> 16) & 0xFF;
        u32 cp = item->color | 0xFF000000;
        ImGui::PushStyleColor(ImGuiCol_Button, cp);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, cp);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, cp);
        ImGui::Button(" ");
        ImGui::EndDisabled();
        ImGui::PopStyleColor(3);
        ImGui::PopID();
        ImGui::GetIO();
        ImGui::SameLine();
        ImGui::Text("%s", item->name);
    }
}

static void render_button(debugger_widget &widget)
{
    bool mval = false;
    bool disabled = widget.enabled ? false : true;
    if (widget.same_line) ImGui::SameLine();
    ImGui::BeginDisabled(disabled);
    mval = ImGui::Button(widget.button.text);
    ImGui::EndDisabled();
    widget.button.value = mval ? 1 : 0;
}


static void render_checkbox(debugger_widget &widget)
{
    bool mval = widget.checkbox.value ? true : false;
    bool disabled = widget.enabled ? false : true;
    if (widget.same_line) ImGui::SameLine();
    ImGui::BeginDisabled(disabled);
    ImGui::Checkbox(widget.checkbox.text, &mval);
    ImGui::EndDisabled();
    widget.checkbox.value = mval ? 1 : 0;
}

static void render_debugger_widget(debugger_widget &widget)
{
    switch(widget.kind) {
        case JSMD_button: {
            render_button(widget);
            break; }
        case JSMD_checkbox: {
            render_checkbox(widget);
            break; }
        case JSMD_radiogroup:
            render_radiogroup(widget);
            break;
        case JSMD_textbox:
            render_textbox(widget);
            break;
        case JSMD_colorkey:
            break;
        default:
            printf("\nWHAT KIND BAD %d", widget.kind);
            break;
    }
}

static void render_debugger_post_widgets(std::vector<debugger_widget> &options)
{
    for (auto& widget : options) {
        if (widget.post)
            render_debugger_widget(widget);
    }
};

static void render_debugger_widgets(std::vector<debugger_widget> &options)
{
    for (auto &widget : options) {
        if (!widget.post)
        render_debugger_widget(widget);
    }
}

void imgui_jocasta_app::render_console_view(bool update_dasm_scroll)
{
    u32 wi=0;
    static char text[3 * 1024 * 1024];
    for (auto &myv: fsys->console_views) {
        managed_window *mw = register_managed_window(0xC00 + (wi++), mwk_debug_console, myv.view->console.name, CONSOLE_VIEW_DEFAULT_ENABLE);
        console_view *tv = &myv.view->console;
        if (mw->enabled) {
            if (begin_managed_window(mw, myv.view->console.name)) {
                tv->render_to_buffer(text, sizeof(text));
                static ImGuiInputTextFlags flags = ImGuiInputTextFlags_ReadOnly;
                ImGui::InputTextMultiline("##source", text, IM_ARRAYSIZE(text), ImVec2(-FLT_MIN, -FLT_MIN), flags);

                if (tv->updated) {
                    tv->updated = 0;
                    ImGuiContext& g = *GImGui;
                    const char* child_window_name = nullptr;
                    ImFormatStringToTempBuffer(&child_window_name, nullptr, "%s/%s_%08X", g.CurrentWindow->Name, "##source", ImGui::GetID("##source"));
                    ImGuiWindow* child_window = ImGui::FindWindowByName(child_window_name);
                    ImGui::SetScrollY(child_window, child_window->ScrollMax.y);
                }
            }
            ImGui::End();
        }
    }
}

void imgui_jocasta_app::render_trace_view(bool update_dasm_scroll)
{
    u32 wi = 0;
    static const float TEXT_BASE_WIDTH = ImGui::CalcTextSize("A").x;
    static const float TEXT_BASE_HEIGHT = ImGui::GetTextLineHeightWithSpacing();
    for (auto &myv: fsys->trace_views) {
        managed_window *mw = register_managed_window(0x800 + (wi++), mwk_debug_trace, myv.view->trace.name, TRACE_VIEW_DEFAULT_ENABLE);
        trace_view *tv = &myv.view->trace;
        if (mw->enabled) {
            if (begin_managed_window(mw, myv.view->trace.name)) {
                static ImGuiTableFlags flags =
                        ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter |
                        ImGuiTableFlags_BordersV | ImGuiTableFlags_SizingStretchProp;
                u32 total_sz = 0;
                float widths[MAX_TRACE_COLS];
                char mf[500];
                u32 numcols = tv->columns.size();
                trace_view_col *col_ptrs[MAX_TRACE_COLS] = {};
                for (u32 i = 0; i < numcols; i++) {
                    trace_view_col &c = tv->columns.at(i);
                    u32 sz = 0;
                    if (c.default_size <= 0)
                        sz = 10;
                    else
                        sz = c.default_size + 1;
                    total_sz += sz;

                    widths[i] = static_cast<float>(sz) * TEXT_BASE_WIDTH;
                    col_ptrs[i] = &c;
                }
                ImVec2 outer_size = ImVec2(TEXT_BASE_WIDTH * static_cast<float>(total_sz + 5), TEXT_BASE_HEIGHT * 20);
                if (ImGui::BeginTable("tabley_table", static_cast<int>(numcols), flags, outer_size)) {
                    ImGui::TableSetupScrollFreeze(0, 1); // Make top row always visible
                    for (u32 c = 0; c < numcols; c++) {
                        auto &[name, default_size] = tv->columns.at(c);
                        ImGui::TableSetupColumn(name, ImGuiTableColumnFlags_None, widths[c]);
                    }
                    ImGui::TableHeadersRow();
                    ImGuiListClipper clipper;

                    if (update_dasm_scroll && tv->autoscroll) {
                        if (tv->display_end_top) ImGui::SetScrollY(0);
                        else {
                            float scrl = clipper.ItemsHeight * (tv->num_trace_lines);
                            float cur_scroll = ImGui::GetScrollY();
                            if ((cur_scroll > scrl) || (scrl < (cur_scroll + (clipper.ItemsHeight * 8))))
                                ImGui::SetScrollY(scrl);
                        }
                    }
                    clipper.Begin(tv->num_trace_lines);
                    while (clipper.Step()) {
                        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++) {
                            ImGui::TableNextRow();
                            auto *ln = tv->get_line(row);
                            for (int col_num = 0; col_num < numcols; col_num++) {
                                //trace_view_col *my_col = col_ptrs[col_num];
                                ImGui::TableSetColumnIndex(col_num);

                                jsm_string *mstr = &ln->cols[col_num];
                                ImGui::TableSetColumnIndex(col_num);
                                ImGui::Text("%s", mstr->ptr);
                            }
                        }
                    }
                    ImGui::EndTable();
                }
            }
            ImGui::End();
        }
    }
}

void imgui_jocasta_app::render_dbglog_views(bool update_dasm_scroll, u64 cur_time)
{
    u32 i = 0;
    for (auto &myv : fsys->dlviews) {
        managed_window *mw = register_managed_window(0x2500 + (i++), mwk_debug_dbglog, myv.view->dbglog.name, DBGLOG_VIEW_DEFAULT_ENABLE);
        if (mw->enabled) {
            render_dbglog_view(myv, update_dasm_scroll, cur_time, mw);
        }
    }
}

void imgui_jocasta_app::render_image_views(ImGuiIO& io)
{
    u32 i=0;
    for (auto &myv: fsys->images) {
        managed_window *mw = register_managed_window(0x500 + (i++), mwk_debug_image, myv.view->image.label, IMAGE_VIEW_DEFAULT_ENABLE);
        if (mw->enabled) {
            if (begin_managed_window(mw, myv.view->image.label)) {
                if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && myv.view->image.FPS_controls.enable) {
                    window_steal_input = true;
                    // TODO: get inputs here!
                    myv.view->image.FPS_controls.rot[1] += 0.00625f;
                }
                render_debugger_widgets(myv.view->options);
                fsys->image_view_present(*myv.view, myv.texture);
                ImGui::Image(myv.texture.for_image(), myv.texture.sz_for_display, myv.texture.uv0, myv.texture.uv1);
                {
                    ImVec2 img_min = ImGui::GetItemRectMin();
                    myv.view->image.mouse_x = (i32)(io.MousePos.x - img_min.x);
                    myv.view->image.mouse_y = (i32)(io.MousePos.y - img_min.y);
                }
                render_debugger_post_widgets(myv.view->options);
            }
            ImGui::End();
        }
    }
}

static void render_opt_view(full_system *fsys, AppSettings& settings, const char* core_key, bool* p_open = nullptr)
{
    if (!fsys || !fsys->sys) return;

    // Legacy debugger-widget options (kept for backward compat)
    std::vector<debugger_widget>& legacy_opts = fsys->sys->opts;

    // Only runtime action widgets (e.g. C64 "Load selected file") stay here.
    // Per-core ini options and controller-connected toggles live in Settings.
    bool has_legacy = !legacy_opts.empty();
    if (!has_legacy) return;

    {
        ImVec2 vp = ImGui::GetMainViewport()->Size;
        ImGui::SetNextWindowPos(ImVec2(vp.x - 430.0f, 30.0f), ImGuiCond_FirstUseEver);
    }
    ImGui::SetNextWindowSize(ImVec2(380, 0), ImGuiCond_Appearing);
    if (ImGui::Begin("Core Options", p_open)) {
        for (auto& w : legacy_opts)
            render_debugger_widget(w);
    }
    ImGui::End();
}

void imgui_jocasta_app::render_debug_views(ImGuiIO& io, bool update_dasm_scroll, u64 cur_time)
{
    render_event_view();
    render_memory_view();
    render_disassembly_views(update_dasm_scroll);
    render_dbglog_views(update_dasm_scroll, cur_time);
    render_image_views(io);
    render_trace_view(update_dasm_scroll);
    render_console_view(update_dasm_scroll);
    render_source_list_view(update_dasm_scroll);
    u32 i = 0;
    for (auto &wv : fsys->waveform_views) {
        render_waveform_view(wv, i++);
    }
    for (auto &wv : fsys->waveform2_views) {
        render_waveform2_view(wv, i++);
    }
    render_window_manager();
}

void imgui_jocasta_app::render_managed_window_toggles(bool as_menu)
{
    if (windows.num == 0) {
        if (as_menu)
            ImGui::MenuItem("No managed windows", nullptr, false, false);
        return;
    }

    managed_window_kind old_mwk = windows.items[0].kind;
    for (u32 i = 0; i < windows.num; i++) {
        managed_window *mw = &windows.items[i];
        if ((i > 0) && (mw->kind != old_mwk)) {
            old_mwk = mw->kind;
            ImGui::Separator();
        }

        bool mval = mw->enabled ? true : false;
        ImGui::PushID(mw->id);
        if (as_menu)
            ImGui::MenuItem(mw->name, nullptr, &mval);
        else
            ImGui::Checkbox(mw->name, &mval);
        ImGui::PopID();
        set_managed_window_enabled(mw, mval);
    }
}

// ---------------------------------------------------------------------------
// Per-core static button definitions — mirrors pio_new_button calls in each core.
// Used for the binding UI when no live system of that type is loaded.
// ---------------------------------------------------------------------------
struct CoreButtonDef { int dbcid; const char* name; };
#define CBD(suffix, nm) { DBCID_co_##suffix, nm }

// Chassis button definitions per-core (for the Input tab binding UI)
struct ChassisDef { JKEYS dbcid; const char* name; DIGITAL_BUTTON_KIND kind; };
static const ChassisDef chassis_sms[]      = {
    { DBCID_ch_pause,       "Pause",            DBK_BUTTON },
};
static const ChassisDef chassis_atari2600[] = {
    { DBCID_ch_diff_left,   "Left Difficulty",  DBK_SWITCH },
    { DBCID_ch_diff_right,  "Right Difficulty", DBK_SWITCH },
    { DBCID_ch_game_select, "Game Select",      DBK_BUTTON },
};
#define CHASDE(arr) arr, (int)(sizeof(arr)/sizeof(arr[0]))
#define CHASNONE    nullptr, 0
static const CoreButtonDef cbd_gameboy[]    = { CBD(up,"Up"),CBD(down,"Down"),CBD(left,"Left"),CBD(right,"Right"), CBD(fire1,"A"),CBD(fire2,"B"),CBD(start,"Start"),CBD(select,"Select") };
static const CoreButtonDef cbd_gba[]        = { CBD(up,"Up"),CBD(down,"Down"),CBD(left,"Left"),CBD(right,"Right"), CBD(fire1,"B"),CBD(fire2,"A"),CBD(shoulder_left,"L shoulder"),CBD(shoulder_right,"r shoulder"),CBD(start,"Start"),CBD(select,"Select") };
static const CoreButtonDef cbd_nes[]        = { CBD(up,"Up"),CBD(down,"Down"),CBD(left,"Left"),CBD(right,"Right"), CBD(fire1,"A"),CBD(fire2,"B"),CBD(start,"Start"),CBD(select,"Select") };
static const CoreButtonDef cbd_snes[]       = { CBD(up,"Up"),CBD(down,"Down"),CBD(left,"Left"),CBD(right,"Right"), CBD(fire1,"A"),CBD(fire2,"B"),CBD(fire3,"X"),CBD(fire4,"Y"), CBD(shoulder_left,"L shoulder"),CBD(shoulder_right,"r shoulder"),CBD(start,"Start"),CBD(select,"Select") };
static const CoreButtonDef cbd_genesis[]    = { CBD(up,"Up"),CBD(down,"Down"),CBD(left,"Left"),CBD(right,"Right"), CBD(fire1,"A"),CBD(fire2,"B"),CBD(fire3,"C"), CBD(fire4,"X"),CBD(fire5,"Y"),CBD(fire6,"Z"),CBD(start,"Start"),CBD(select,"mode") };
static const CoreButtonDef cbd_sms[]        = { CBD(up,"Up"),CBD(down,"Down"),CBD(left,"Left"),CBD(right,"Right"), CBD(fire1,"1"),CBD(fire2,"2"),CBD(start,"Pause") };
static const CoreButtonDef cbd_sg1000[]     = { CBD(up,"Up"),CBD(down,"Down"),CBD(left,"Left"),CBD(right,"Right"), CBD(fire1,"1"),CBD(fire2,"2") };
static const CoreButtonDef cbd_tg16[]       = { CBD(up,"Up"),CBD(right,"Right"),CBD(down,"Down"),CBD(left,"Left"), CBD(fire1,"i"),CBD(fire2,"ii"),CBD(select,"Select"),CBD(start,"run") };
static const CoreButtonDef cbd_atari2600[]  = { CBD(up,"Up"),CBD(down,"Down"),CBD(left,"Left"),CBD(right,"Right"), CBD(fire1,"fire") };
static const CoreButtonDef cbd_neogeo[]     = { CBD(up,"Up"),CBD(down,"Down"),CBD(left,"Left"),CBD(right,"Right"), CBD(fire1,"A"),CBD(fire2,"B"),CBD(fire3,"C"),CBD(fire4,"d"),CBD(start,"Start"),CBD(select,"Select") };
static const CoreButtonDef cbd_dreamcast[]  = { CBD(up,"Up"),CBD(down,"Down"),CBD(left,"Left"),CBD(right,"Right"), CBD(fire1,"A"),CBD(fire2,"B"),CBD(fire4,"X"),CBD(fire5,"Y"),CBD(start,"Start") };
static const CoreButtonDef cbd_ps1[]        = { CBD(select,"Select"),CBD(start,"Start"), CBD(up,"Up"),CBD(right,"Right"),CBD(down,"Down"),CBD(left,"Left"), CBD(shoulder_left2,"L2"),CBD(shoulder_right2,"R2"),CBD(shoulder_left,"L1"),CBD(shoulder_right,"R1"), CBD(fire4,"triangle"),CBD(fire5,"circle"),CBD(fire2,"cross"),CBD(fire1,"square") };
static const CoreButtonDef cbd_nds[]        = { CBD(up,"Up"),CBD(down,"Down"),CBD(left,"Left"),CBD(right,"Right"), CBD(fire1,"B"),CBD(fire2,"A"),CBD(fire3,"Y"),CBD(fire4,"X"), CBD(shoulder_left,"L shoulder"),CBD(shoulder_right,"r shoulder"),CBD(start,"Start"),CBD(select,"Select") };
static const CoreButtonDef cbd_pv1000[]     = { CBD(up,"Up"),CBD(down,"Down"),CBD(left,"Left"),CBD(right,"Right"), CBD(fire1,"1"),CBD(fire2,"2"),CBD(start,"Start"),CBD(select,"Select") };
#undef CBD
#define CBDE(arr) arr, (int)(sizeof(arr)/sizeof(arr[0]))
#define CBDNONE   nullptr, 0

// ---------------------------------------------------------------------------
// Settings-window core entry table (shared by Input and Options pages)
// ---------------------------------------------------------------------------
struct CoreEntry {
    const char* label; const char* key;
    int max_joypads; const CoreButtonDef* buttons; int num_buttons;
    const ChassisDef* chassis; int num_chassis;
    bool always_connected = false; // handheld — controller is the device; no port-connected toggle
};
static const CoreEntry settings_cores[] = {
    { "Apple IIe",          "apple2e",   0, CBDNONE, CHASNONE },
    { "Atari 2600",         "atari2600", 2, CBDE(cbd_atari2600), CHASDE(chassis_atari2600) },
    { "Casio PV-1000",      "pv1000",    2, CBDE(cbd_pv1000),    CHASNONE },
    { "Commodore 64",       "c64",       0, CBDNONE, CHASNONE },
    { "COSMAC VIP",         "cosmac",    0, CBDNONE, CHASNONE },
    { "Dreamcast",          "dreamcast", 1, CBDE(cbd_dreamcast), CHASNONE },
    { "Galaksija",          "galaksija", 0, CBDNONE, CHASNONE },
    { "Game Boy",           "gameboy",   1, CBDE(cbd_gameboy),   CHASNONE, true },
    { "Game Boy Advance",   "gba",       1, CBDE(cbd_gba),       CHASNONE, true },
    { "Genesis",            "genesis",   2, CBDE(cbd_genesis),   CHASNONE },
    { "Mac",                "mac",       0, CBDNONE, CHASNONE },
    { "Neo Geo",            "neogeo",    1, CBDE(cbd_neogeo),    CHASNONE },
    { "Neo Geo Pocket",     "ngp",       0, CBDNONE, CHASNONE },
    { "NES",                "nes",       2, CBDE(cbd_nes),        CHASNONE },
    { "Nintendo DS",        "nds",       1, CBDE(cbd_nds),        CHASNONE, true },
    { "PlayStation",        "ps1",       1, CBDE(cbd_ps1),        CHASNONE },
    { "Sega Master System", "sms",       2, CBDE(cbd_sms),        CHASDE(chassis_sms) },
    { "SG-1000",            "sg1000",    2, CBDE(cbd_sg1000),     CHASNONE },
    { "SNES",               "snes",      2, CBDE(cbd_snes),       CHASNONE },
    { "TurboGrafx-16",      "tg16",      1, CBDE(cbd_tg16),       CHASNONE },
    { "ZX Spectrum",        "zx",        0, CBDNONE, CHASNONE },
};
static const int num_settings_cores = (int)(sizeof(settings_cores) / sizeof(settings_cores[0]));

// ---------------------------------------------------------------------------
// Settings page: General
// ---------------------------------------------------------------------------
void imgui_jocasta_app::render_settings_page_general()
{
    ImGui::Spacing();
    ImGui::SeparatorText("Fast-Forward");

    ImGui::AlignTextToFramePadding();
    ImGui::Text("Max speed (global)");
    ImGui::SameLine(180);
    {
        int ff_speed = settings.get_ff_speed_global();
        ImGui::SetNextItemWidth(100);
        if (ImGui::InputInt("##ff_global", &ff_speed, 100, 100)) {
            ff_speed = ((ff_speed + 50) / 100) * 100;
            if (ff_speed < 200) ff_speed = 200;
            settings.set_ff_speed_global(ff_speed);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%%  (%.0fx)", ff_speed / 100.0f);
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Saves folder");

    {
        static char saves_buf[1024] = {};
        static bool saves_buf_init  = false;
        if (!saves_buf_init) {
            snprintf(saves_buf, sizeof(saves_buf), "%s",
                     settings.get_saves_dir().c_str());
            saves_buf_init = true;
        }
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Path");
        ImGui::SameLine(180);
        {
            const float fp      = ImGui::GetStyle().FramePadding.x;
            const float sp      = ImGui::GetStyle().ItemSpacing.x;
            float browse_w = ImGui::CalcTextSize("Browse...").x + fp * 2.0f;
            float open_w   = ImGui::CalcTextSize("Open").x      + fp * 2.0f;
            float input_w  = ImGui::GetContentRegionAvail().x - browse_w - open_w - sp * 2.0f;
            if (input_w < 80.0f) input_w = 80.0f;
            ImGui::SetNextItemWidth(input_w);
            if (ImGui::InputText("##saves_dir", saves_buf, sizeof(saves_buf),
                                 ImGuiInputTextFlags_EnterReturnsTrue))
                settings.set_saves_dir(saves_buf);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Base folder for save states and SRAM files.\n"
                                  "Leave empty for ~/Documents/saves.\n"
                                  "Per-core sub-folders are created automatically.");
            ImGui::SameLine();
            if (ImGui::Button("Browse...##saves")) {
                std::string cur = settings.get_saves_dir();
                std::string start = existing_dialog_dir(cur, false);
                const char* picked = tinyfd_selectFolderDialog("Saves Directory",
                                         start.c_str());
                if (picked) {
                    snprintf(saves_buf, sizeof(saves_buf), "%s", picked);
                    settings.set_saves_dir(picked);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Open##saves")) {
                std::string path = settings.get_saves_dir();
                if (!path.empty()) SDL_OpenURL(("file://" + path).c_str());
            }
        }
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Save defaults (per-core can override)");

    {
        bool g_states = settings.get_states_in_saves_dir_global();
        if (ImGui::Checkbox("Save states in saves folder##g", &g_states))
            settings.set_states_in_saves_dir_global(g_states);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Default: store .jsst files in the central saves folder.");

        bool g_sram = settings.get_sram_in_saves_dir_global();
        if (ImGui::Checkbox("SRAM in saves folder##g", &g_sram))
            settings.set_sram_in_saves_dir_global(g_sram);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Default: store .sram files in the central saves folder.");

        bool g_restore = settings.get_save_sram_with_state_global();
        if (ImGui::Checkbox("Restore SRAM when loading a save state##g", &g_restore))
            settings.set_save_sram_with_state_global(g_restore);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Default: restore SRAM from the bundled copy when loading a state.\n"
                              "SRAM is always saved into the .jsst zip regardless.");
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Audio");

    {
        // Rate dropdown — populated from the device probe (available on first core load).
        // Before any core is loaded, show the stored rate or "Auto-detect".
        const auto& rates = fsys ? fsys->available_audio_rates : std::vector<u32>{};
        u32 cur_rate = settings.get_audio_output_rate();

        ImGui::AlignTextToFramePadding();
        ImGui::Text("Output sample rate");
        ImGui::SameLine(180);
        ImGui::SetNextItemWidth(160);

        char preview[32];
        if (cur_rate == 0)
            snprintf(preview, sizeof(preview), "Auto-detect");
        else
            snprintf(preview, sizeof(preview), "%u Hz", cur_rate);

        if (ImGui::BeginCombo("##audio_rate", preview)) {
            // "Auto-detect" entry — clears stored rate so next core load re-selects
            bool sel = (cur_rate == 0);
            if (ImGui::Selectable("Auto-detect", sel)) {
                settings.set_audio_output_rate(0);
            }
            for (u32 r : rates) {
                char lbl[32];
                snprintf(lbl, sizeof(lbl), "%u Hz", r);
                bool is_sel = (r == cur_rate);
                if (ImGui::Selectable(lbl, is_sel))
                    settings.set_audio_output_rate(r);
                if (is_sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Output sample rate sent to the audio device.\n"
                              "Takes effect when the next core is loaded.\n"
                              "Prefer 48000 Hz to avoid double-resampling.");
        if (!rates.empty() && cur_rate != 0) {
            ImGui::SameLine();
            ImGui::TextDisabled("(takes effect on next core load)");
        }
        if (rates.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("(load a core to populate)");
        }
    }

    {
        static const char* pad_labels[] = { "None", "1/4 frame", "1/2 frame", "1 frame" };
        int cur_pad = settings.get_audio_prime_padding();

        ImGui::AlignTextToFramePadding();
        ImGui::Text("Startup ramp cushion");
        ImGui::SameLine(180);
        ImGui::SetNextItemWidth(160);

        if (ImGui::BeginCombo("##audio_pad", pad_labels[cur_pad])) {
            for (int i = 0; i < 4; i++) {
                bool sel = (i == cur_pad);
                if (ImGui::Selectable(pad_labels[i], sel))
                    settings.set_audio_prime_padding(i);
                if (sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Prepend a silent-to-audio ramp of this length\n"
                              "before the first sample when resuming play.\n"
                              "Prevents a click if the audio device woke up\n"
                              "mid-frame. 1/4 frame is a good default.");
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Virtual Input");

    {
        ImGui::AlignTextToFramePadding();
        ImGui::Text("UI scale");
        ImGui::SameLine(180);
        float vs = settings.get_virtual_scale();
        ImGui::SetNextItemWidth(160.0f);
        if (ImGui::SliderFloat("##virtual_scale", &vs, 0.3f, 2.0f, "%.2f")) {
            settings.set_virtual_scale(vs);
            set_virtual_scale(vs);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Scale factor for virtual controller and keyboard overlay windows.");
    }
    {
        bool vc = settings.get_show_virtual_controller_global();
        if (ImGui::Checkbox("Show virtual controllers", &vc))
            settings.set_show_virtual_controller_global(vc);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Show on-screen gamepad overlay by default.\nPer-core overrides available in Options.");
    }
    {
        bool vk = settings.get_show_virtual_keyboard_global();
        if (ImGui::Checkbox("Show virtual keyboards", &vk))
            settings.set_show_virtual_keyboard_global(vk);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Show on-screen keyboard/keypad overlay by default.\nPer-core overrides available in Options.");
    }
}

// ---------------------------------------------------------------------------
// Settings page: BIOS
// ---------------------------------------------------------------------------
void imgui_jocasta_app::render_settings_page_bios()
{
    static bios_check_cache bios_cache;
    static bool bios_cache_initialized = false;
    if (!bios_cache_initialized) {
        bios_cache.invalidate();
        bios_cache_initialized = true;
    }

    if (ImGui::SmallButton("Re-check all")) bios_cache.invalidate();
    ImGui::SameLine();
    ImGui::TextWrapped("Set the directory containing BIOS files for each system. Dimmed paths are defaults.");

    if (bios_cache.dirty) {
        bios_cache.run_checks([&](jsm::systems sys) -> std::string {
            std::string c = settings.get_bios_dir(sys);
            return c.empty() ? AppSettings::default_bios_dir(sys) : c;
        });
    }

    ImGui::Spacing();

    auto status_color = [](bios_status s) -> ImVec4 {
        switch (s) {
            case bios_status::ok:           return ImVec4(0.2f, 0.9f, 0.2f, 1.f);
            case bios_status::unknown_hash: return ImVec4(0.9f, 0.8f, 0.1f, 1.f);
            case bios_status::bad_hash:     return ImVec4(1.0f, 0.4f, 0.0f, 1.f);
            case bios_status::missing:      return ImVec4(0.9f, 0.2f, 0.2f, 1.f);
            default:                        return ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
        }
    };
    auto status_label = [](bios_status s) -> const char* {
        switch (s) {
            case bios_status::ok:           return "OK";
            case bios_status::unknown_hash: return "Present";
            case bios_status::bad_hash:     return "Bad hash";
            case bios_status::missing:      return "Missing";
            default:                        return "?";
        }
    };

    for (auto& entry : LOAD_SYSTEMS) {
        if (!AppSettings::sys_has_bios(entry.system)) continue;
        std::string configured = settings.get_bios_dir(entry.system);
        std::string effective  = configured.empty()
            ? AppSettings::default_bios_dir(entry.system)
            : configured;
        char buf[512];
        snprintf(buf, sizeof(buf), "%s", effective.c_str());
        ImGui::PushID((int)entry.system);

        {
            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            const float fp      = ImGui::GetStyle().FramePadding.x;
            float browse_w = ImGui::CalcTextSize("Browse...").x + fp * 2.0f;
            float open_w   = ImGui::CalcTextSize("Open").x      + fp * 2.0f;
            float reset_w  = ImGui::CalcTextSize("Reset").x     + fp * 2.0f;
            float label_w  = ImGui::CalcTextSize(entry.label).x;
            float avail    = ImGui::GetContentRegionAvail().x;
            float reserved = label_w + spacing + browse_w + spacing + open_w + spacing;
            if (!configured.empty()) reserved += reset_w + spacing;
            float input_w = avail - reserved;
            if (input_w < 80.0f) input_w = 80.0f;

            ImGui::Text("%s", entry.label);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(input_w);
            if (configured.empty()) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            bool edited = ImGui::InputText("##bios", buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue);
            if (configured.empty()) ImGui::PopStyleColor();
            if (edited) { settings.set_bios_dir(entry.system, buf); bios_cache.invalidate(); }
            ImGui::SameLine();
            if (ImGui::Button("Browse...")) {
                std::string start = existing_dialog_dir(effective, false);
                const char* picked = tinyfd_selectFolderDialog("BIOS Directory", start.c_str());
                if (picked) { settings.set_bios_dir(entry.system, picked); bios_cache.invalidate(); }
            }
            ImGui::SameLine();
            if (ImGui::Button("Open")) {
                SDL_OpenURL(("file://" + effective).c_str());
            }
            if (!configured.empty()) {
                ImGui::SameLine();
                if (ImGui::Button("Reset")) { settings.set_bios_dir(entry.system, ""); bios_cache.invalidate(); }
            }
        }

        const bios_sys_spec* spec = find_bios_spec(entry.system);
        if (spec) {
            ImGui::Indent(16.f);
            for (int fi = 0; fi < spec->num_files; fi++) {
                const bios_file_spec& fs = spec->files[fi];
                bios_status st = bios_cache.get(entry.system, fi);
                ImVec4 col = (fs.optional && st == bios_status::missing)
                    ? ImVec4(0.5f, 0.5f, 0.5f, 1.f)
                    : status_color(st);
                ImGui::TextColored(col, "[%s]", status_label(st));
                ImGui::SameLine();
                ImGui::TextDisabled("%s", fs.filename);
                ImGui::SameLine();
                ImGui::TextDisabled("— %s", fs.label);
            }
            ImGui::Unindent(16.f);
        }

        ImGui::PopID();
        ImGui::Spacing();
    }
}

// ---------------------------------------------------------------------------
// Settings page: Hotkeys
// ---------------------------------------------------------------------------
void imgui_jocasta_app::render_settings_page_hotkeys()
{
    static bool capturing_ff = false;
    static bool capturing_fs = false;

    ImGui::Spacing();

    // Fast Forward binding
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Fast Forward");
    ImGui::SameLine(160);
    Binding ff_bind = settings.get_hotkey("fast_forward");
    if (capturing_ff) {
        ImGui::Button("Press a key...", ImVec2(160, 0));
        Binding captured = Binding::capture_any();
        if (!captured.is_none()) {
            settings.set_hotkey("fast_forward", captured);
            capturing_ff = false;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            capturing_ff = false;
    } else {
        if (ImGui::Button(ff_bind.display_name().c_str(), ImVec2(160, 0)))
            capturing_ff = true;
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset##ff_reset")) {
            settings.clear_hotkey("fast_forward");
            capturing_ff = false;
        }
    }

    // Fullscreen binding
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Fullscreen");
    ImGui::SameLine(160);
    Binding fs_bind = settings.get_hotkey("fullscreen");
    if (capturing_fs) {
        ImGui::Button("Press a key...", ImVec2(160, 0));
        Binding captured = Binding::capture_any();
        if (!captured.is_none()) {
            settings.set_hotkey("fullscreen", captured);
            capturing_fs = false;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            capturing_fs = false;
    } else {
        if (ImGui::Button(fs_bind.display_name().c_str(), ImVec2(160, 0)))
            capturing_fs = true;
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset##fs_reset")) {
            settings.clear_hotkey("fullscreen");
            capturing_fs = false;
        }
    }

    // Save-state and other hotkeys
    {
        struct HkRow { const char* label; const char* key; const char* reset_id; };
        static const HkRow hk_rows[] = {
            { "Play / Pause",      "pause",             "##rst_pp"  },
            { "Save State",        "save_state",        "##rst_ss"  },
            { "Load State",        "load_state",        "##rst_ls"  },
            { "Previous Slot",     "prev_slot",         "##rst_ps"  },
            { "Next Slot",         "next_slot",         "##rst_ns"  },
            { "Screenshot",        "screenshot",        "##rst_sc"  },
            { "Input Mode Toggle", "input_mode_toggle", "##rst_imt" },
        };
        static const int num_hk_rows = (int)(sizeof(hk_rows)/sizeof(hk_rows[0]));
        static int capturing_hk = -1;
        for (int hi = 0; hi < num_hk_rows; hi++) {
            ImGui::PushID(hi + 200);
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%s", hk_rows[hi].label);
            ImGui::SameLine(160);
            Binding b = settings.get_hotkey(hk_rows[hi].key);
            if (capturing_hk == hi) {
                ImGui::Button("Press a key...", ImVec2(160, 0));
                Binding cap = Binding::capture_any();
                if (!cap.is_none()) {
                    settings.set_hotkey(hk_rows[hi].key, cap);
                    capturing_hk = -1;
                }
                if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
                    capturing_hk = -1;
            } else {
                if (ImGui::Button(b.display_name().c_str(), ImVec2(160, 0)))
                    capturing_hk = hi;
                ImGui::SameLine();
                if (ImGui::SmallButton(hk_rows[hi].reset_id)) {
                    settings.clear_hotkey(hk_rows[hi].key);
                    capturing_hk = -1;
                }
            }
            ImGui::PopID();
        }
    }
}

// ---------------------------------------------------------------------------
// Settings page: Input (per-core key bindings)
// ---------------------------------------------------------------------------
void imgui_jocasta_app::render_settings_page_input()
{
    struct InputCapture {
        char core_key[64] = {};
        int  player = -1;
        int  dbcid  = -1;
        bool is_pad = false;
    };
    static InputCapture ic;
    static int selected_core = 0;
    if (selected_core >= num_settings_cores) selected_core = 0;

    // Left: scrollable core list
    ImGui::BeginChild("##input_core_list", ImVec2(148, 0), true);
    for (int i = 0; i < num_settings_cores; i++) {
        if (ImGui::Selectable(settings_cores[i].label, selected_core == i))
            selected_core = i;
    }
    ImGui::EndChild();
    ImGui::SameLine();

    // Right: bindings for selected core
    ImGui::BeginChild("##input_core_content", ImVec2(0, 0), false);
    {
        int i = selected_core;
        bool live = (fsys && fsys->sys &&
                     AppSettings::sys_to_core_key(which) != nullptr &&
                     strcmp(AppSettings::sys_to_core_key(which), settings_cores[i].key) == 0);

        int num_players = settings_cores[i].max_joypads;
        if (live) {
            num_players = 0;
            if (fsys->io.controller1.vec) num_players++;
            if (fsys->io.controller2.vec) num_players++;
            if (fsys->io.controller3.vec) num_players++;
            if (fsys->io.controller4.vec) num_players++;
        }

        if (num_players == 0) {
            ImGui::TextDisabled("No gamepad support for this core.");
        } else {
            cvec_ptr<physical_io_device>* cptrs[4] = {
                &fsys->io.controller1, &fsys->io.controller2,
                &fsys->io.controller3, &fsys->io.controller4
            };

            if (ImGui::BeginTabBar("##players")) {
                static const char* player_labels[] = { "P1", "P2", "P3", "P4" };
                for (int p = 1; p <= num_players; p++) {
                    if (ImGui::BeginTabItem(player_labels[p - 1])) {
                        struct BtnRow { int dbcid; const char* name; };
                        BtnRow rows[20];
                        int nrows = 0;

                        if (live && cptrs[p-1]->vec) {
                            auto& dbs = cptrs[p-1]->get().controller.digital_buttons;
                            for (auto& db : dbs)
                                if (nrows < 20) rows[nrows++] = { (int)db.common_id, db.name };
                        } else {
                            for (int bi = 0; bi < settings_cores[i].num_buttons && nrows < 20; bi++)
                                rows[nrows++] = { settings_cores[i].buttons[bi].dbcid, settings_cores[i].buttons[bi].name };
                        }

                        if (ImGui::BeginTable("##btntbl", 5,
                                ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp)) {
                            ImGui::TableSetupColumn("Button",   ImGuiTableColumnFlags_WidthStretch, 1.0f);
                            ImGui::TableSetupColumn("Keyboard", ImGuiTableColumnFlags_WidthStretch, 2.0f);
                            ImGui::TableSetupColumn("##clrkbd", ImGuiTableColumnFlags_WidthFixed, 22.0f);
                            ImGui::TableSetupColumn("Gamepad",  ImGuiTableColumnFlags_WidthStretch, 2.0f);
                            ImGui::TableSetupColumn("##clrpad", ImGuiTableColumnFlags_WidthFixed, 22.0f);
                            ImGui::TableHeadersRow();

                            for (int bi = 0; bi < nrows; bi++) {
                                int dbcid = rows[bi].dbcid;
                                const char* btn_label = rows[bi].name;

                                ImGui::PushID(bi + p * 100);
                                ImGui::TableNextRow();

                                ImGui::TableSetColumnIndex(0);
                                ImGui::AlignTextToFramePadding();
                                ImGui::TextUnformatted(btn_label);

                                auto render_slot = [&](bool is_pad, int col_binding, int col_clear) {
                                    bool capturing = (ic.player == p && ic.dbcid == dbcid &&
                                                      strcmp(ic.core_key, settings_cores[i].key) == 0 &&
                                                      ic.is_pad == is_pad);
                                    ImGui::TableSetColumnIndex(col_binding);
                                    if (capturing) {
                                        ImGui::Button("Press a key...", ImVec2(-1, 0));
                                        Binding captured = Binding::capture_any();
                                        if (!captured.is_none()) {
                                            if (is_pad) settings.set_input_pad(settings_cores[i].key, p, dbcid, captured);
                                            else        settings.set_input_kbd(settings_cores[i].key, p, dbcid, captured);
                                            ic.player = -1; ic.dbcid = -1;
                                        }
                                        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
                                            ic.player = -1;
                                    } else {
                                        bool has_custom = false;
                                        std::string bname;
                                        if (is_pad) {
                                            Binding b = settings.get_input_pad(settings_cores[i].key, p, dbcid);
                                            if (!b.is_none()) { has_custom = true; bname = b.display_name(); }
                                            else              { bname = (p == 1) ? "(auto)" : "(none)"; }
                                        } else {
                                            Binding custom = settings.get_input_kbd(settings_cores[i].key, p, dbcid);
                                            if (!custom.is_none()) {
                                                has_custom = true;
                                                bname = custom.display_name();
                                            } else {
                                                Binding def = AppSettings::get_default_kbd(settings_cores[i].key, dbcid);
                                                if (!def.is_none()) bname = def.display_name();
                                                else                bname = "(none)";
                                            }
                                        }
                                        if (!has_custom) {
                                            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                                            if (ImGui::Button(bname.c_str(), ImVec2(-1, 0))) {
                                                snprintf(ic.core_key, sizeof(ic.core_key), "%s", settings_cores[i].key);
                                                ic.player = p; ic.dbcid = dbcid; ic.is_pad = is_pad;
                                            }
                                            ImGui::PopStyleColor();
                                        } else {
                                            if (ImGui::Button(bname.c_str(), ImVec2(-1, 0))) {
                                                snprintf(ic.core_key, sizeof(ic.core_key), "%s", settings_cores[i].key);
                                                ic.player = p; ic.dbcid = dbcid; ic.is_pad = is_pad;
                                            }
                                        }
                                    }
                                    ImGui::TableSetColumnIndex(col_clear);
                                    bool has_custom_check = is_pad
                                        ? !settings.get_input_pad(settings_cores[i].key, p, dbcid).is_none()
                                        : !settings.get_input_kbd(settings_cores[i].key, p, dbcid).is_none();
                                    if (has_custom_check) {
                                        if (ImGui::SmallButton("x")) {
                                            if (is_pad) settings.clear_input_pad(settings_cores[i].key, p, dbcid);
                                            else        settings.clear_input_kbd(settings_cores[i].key, p, dbcid);
                                            if (ic.player == p && ic.dbcid == dbcid && ic.is_pad == is_pad)
                                                ic.player = -1;
                                        }
                                    }
                                };

                                render_slot(false, 1, 2);
                                render_slot(true,  3, 4);

                                ImGui::PopID();
                            }
                            ImGui::EndTable();
                        }
                        ImGui::EndTabItem();
                    }
                }
                ImGui::EndTabBar();
            }
        }

        // Chassis button bindings
        if (settings_cores[i].num_chassis > 0) {
            ImGui::Spacing();
            ImGui::SeparatorText("Chassis Controls");

            struct CaptureChassis { char core_key[64] = {}; int idx = -1; };
            static CaptureChassis cc;

            if (ImGui::BeginTable("##chstbl", 3,
                    ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("Button",   ImGuiTableColumnFlags_WidthStretch, 1.0f);
                ImGui::TableSetupColumn("Binding",  ImGuiTableColumnFlags_WidthStretch, 2.0f);
                ImGui::TableSetupColumn("##clrchs", ImGuiTableColumnFlags_WidthFixed,  22.0f);
                ImGui::TableHeadersRow();

                for (int ci = 0; ci < settings_cores[i].num_chassis; ci++) {
                    const ChassisDef& cd = settings_cores[i].chassis[ci];
                    ImGui::PushID(ci + 5000);
                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(0);
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted(cd.name);
                    ImGui::SameLine();
                    ImGui::TextDisabled(cd.kind == DBK_SWITCH ? "(switch)" : "(button)");

                    ImGui::TableSetColumnIndex(1);
                    bool capturing_this = (cc.idx == ci &&
                                           strcmp(cc.core_key, settings_cores[i].key) == 0);
                    Binding b = settings.effective_chassis_hotkey(settings_cores[i].key, cd.dbcid);
                    if (capturing_this) {
                        ImGui::Button("Press a key...", ImVec2(-1, 0));
                        Binding cap = Binding::capture_any();
                        if (!cap.is_none()) {
                            settings.set_chassis_hotkey(settings_cores[i].key, cd.dbcid, cap);
                            cc.idx = -1;
                        }
                        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
                            cc.idx = -1;
                    } else {
                        if (ImGui::Button(b.display_name().c_str(), ImVec2(-1, 0))) {
                            cc.idx = ci;
                            snprintf(cc.core_key, sizeof(cc.core_key), "%s", settings_cores[i].key);
                        }
                    }

                    ImGui::TableSetColumnIndex(2);
                    Binding custom = settings.get_chassis_hotkey(settings_cores[i].key, cd.dbcid);
                    if (!custom.is_none()) {
                        if (ImGui::SmallButton("x")) {
                            settings.clear_chassis_hotkey(settings_cores[i].key, cd.dbcid);
                            if (cc.idx == ci && strcmp(cc.core_key, settings_cores[i].key) == 0)
                                cc.idx = -1;
                        }
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Reset to default");
                    }

                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
        }
    }
    ImGui::EndChild(); // ##input_core_content
}

// ---------------------------------------------------------------------------
// Settings page: Options (per-core)
// ---------------------------------------------------------------------------
void imgui_jocasta_app::render_settings_page_options()
{
    static int selected_core = 0;
    if (selected_core >= num_settings_cores) selected_core = 0;

    // Left: scrollable core list
    ImGui::BeginChild("##opt_core_list", ImVec2(148, 0), true);
    for (int i = 0; i < num_settings_cores; i++) {
        if (ImGui::Selectable(settings_cores[i].label, selected_core == i))
            selected_core = i;
    }
    ImGui::EndChild();
    ImGui::SameLine();

    // Right: options for selected core
    ImGui::BeginChild("##opt_core_content", ImVec2(0, 0), false);
    {
        int i = selected_core;
        const char* ck = settings_cores[i].key;
        bool live = (fsys && fsys->sys &&
                     AppSettings::sys_to_core_key(which) != nullptr &&
                     strcmp(AppSettings::sys_to_core_key(which), ck) == 0);

        // Controllers — hidden for handhelds where the controller is built into the device
        int max_ports = settings_cores[i].max_joypads;
        if (max_ports > 0 && !settings_cores[i].always_connected) {
            ImGui::SeparatorText("Controllers");
            for (int port = 1; port <= max_ports; port++) {
                bool connected = settings.get_controller_connected(ck, port, port == 1);
                char label[40];
                snprintf(label, sizeof(label), "Controller connected to port %d", port);
                ImGui::PushID(port + 100);
                if (ImGui::Checkbox(label, &connected)) {
                    settings.set_controller_connected(ck, port, connected);
                    if (live) fsys->sys->controller_connected_changed(port, connected);
                }
                ImGui::PopID();
            }
            ImGui::Spacing();
        }

        // Virtual input per-core overrides
        {
            ImGui::SeparatorText("Virtual Input");

            // Helper: renders one override row (controller or keyboard).
            // label/tip describe what it controls; global_val is the current global default.
            auto virt_override_row = [&](
                const char* label, const char* tip,
                bool  global_val,
                bool  has_override,
                bool  override_val,
                auto  fn_set,
                auto  fn_clear)
            {
                ImGui::PushID(label);
                bool use_override = has_override;
                if (ImGui::Checkbox("##ov", &use_override)) {
                    if (use_override) fn_set(global_val);
                    else              fn_clear();
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Enable per-core override");
                ImGui::SameLine();
                bool val = has_override ? override_val : global_val;
                ImGui::BeginDisabled(!has_override);
                if (ImGui::Checkbox(label, &val)) {
                    if (has_override) fn_set(val);
                }
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered()) {
                    if (has_override)
                        ImGui::SetTooltip("%s\n(per-core override)", tip);
                    else
                        ImGui::SetTooltip("%s\n(using global default: %s)", tip, global_val ? "on" : "off");
                }
                ImGui::PopID();
            };

            virt_override_row(
                "Show virtual controller", "Show gamepad overlay for this core.",
                settings.get_show_virtual_controller_global(),
                settings.has_show_virtual_controller_override(ck),
                settings.get_show_virtual_controller(ck),
                [&](bool v){ settings.set_show_virtual_controller(ck, v); },
                [&](){ settings.clear_show_virtual_controller(ck); }
            );

            virt_override_row(
                "Show virtual keyboard", "Show keyboard/keypad overlay for this core.",
                settings.get_show_virtual_keyboard_global(),
                settings.has_show_virtual_keyboard_override(ck),
                settings.get_show_virtual_keyboard(ck),
                [&](bool v){ settings.set_show_virtual_keyboard(ck, v); },
                [&](){ settings.clear_show_virtual_keyboard(ck); }
            );
            ImGui::Spacing();
        }

        // Per-core fast-forward override
        {
            ImGui::SeparatorText("Performance");
            int core_ff = settings.get_ff_speed_core(ck);
            bool has_custom = (core_ff > 0);
            if (ImGui::Checkbox("Override fast-forward speed", &has_custom)) {
                if (has_custom)
                    settings.set_ff_speed_core(ck, settings.get_ff_speed_global());
                else
                    settings.clear_ff_speed_core(ck);
                core_ff = settings.get_ff_speed_core(ck);
            }
            if (has_custom) {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(100);
                if (ImGui::InputInt("##core_ff", &core_ff, 100, 100)) {
                    core_ff = ((core_ff + 50) / 100) * 100;
                    if (core_ff < 200) core_ff = 200;
                    settings.set_ff_speed_core(ck, core_ff);
                }
                ImGui::SameLine();
                ImGui::TextDisabled("%%  (%.0fx)", core_ff / 100.0f);
            } else {
                ImGui::SameLine();
                ImGui::TextDisabled("global (%d%%)", settings.get_ff_speed_global());
            }
            ImGui::Spacing();
        }

        // Save-state / SRAM options
        {
            ImGui::SeparatorText("Saves");

            auto save_override_row = [&](
                const char* label, const char* tip,
                bool  global_val,
                bool  has_override,
                bool  override_val,
                auto  fn_set,
                auto  fn_clear
            ) {
                ImGui::PushID(label);
                bool ov = has_override;
                if (ImGui::Checkbox("##ov", &ov)) {
                    if (ov) fn_set(global_val);
                    else    fn_clear();
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Override the global default for this core.");
                ImGui::SameLine();
                if (has_override) {
                    bool val = override_val;
                    if (ImGui::Checkbox(label, &val)) fn_set(val);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
                } else {
                    ImGui::BeginDisabled();
                    bool display = global_val;
                    ImGui::Checkbox(label, &display);
                    ImGui::EndDisabled();
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Using global default (%s). Check the box on the left to override.",
                                          global_val ? "ON" : "OFF");
                }
                ImGui::PopID();
            };

            save_override_row(
                "Save states in saves folder",
                "Store .jsst files in the central saves folder instead of the ROM's directory.",
                settings.get_states_in_saves_dir_global(),
                settings.has_states_in_saves_dir_override(ck),
                settings.get_states_in_saves_dir(ck),
                [&](bool v){ settings.set_states_in_saves_dir(ck, v);
                             if (live) fsys->states_in_saves_dir = v; },
                [&]()      { settings.clear_states_in_saves_dir(ck);
                             if (live) fsys->states_in_saves_dir =
                                 settings.get_states_in_saves_dir_global(); }
            );

            save_override_row(
                "SRAM in saves folder",
                "Store .sram files in the central saves folder instead of the ROM's directory.",
                settings.get_sram_in_saves_dir_global(),
                settings.has_sram_in_saves_dir_override(ck),
                settings.get_sram_in_saves_dir(ck),
                [&](bool v){ settings.set_sram_in_saves_dir(ck, v);
                             if (live) fsys->sram_in_saves_dir = v; },
                [&]()      { settings.clear_sram_in_saves_dir(ck);
                             if (live) fsys->sram_in_saves_dir =
                                 settings.get_sram_in_saves_dir_global(); }
            );

            save_override_row(
                "Restore SRAM when loading a save state",
                "SRAM is always bundled in the .jsst zip.\n"
                "Enable to restore it when loading; disable to keep current on-disk SRAM.",
                settings.get_save_sram_with_state_global(),
                settings.has_save_sram_with_state_override(ck),
                settings.get_save_sram_with_state(ck),
                [&](bool v){ settings.set_save_sram_with_state(ck, v);
                             if (live) fsys->sram_in_state = v; },
                [&]()      { settings.clear_save_sram_with_state(ck);
                             if (live) fsys->sram_in_state =
                                 settings.get_save_sram_with_state_global(); }
            );

            ImGui::Spacing();
        }

        // Display options
        {
            ImGui::SeparatorText("Display");

            // Nearest Neighbor
            {
                i32 nn_saved = settings.get_core_option(ck, "nearest_neighbor", -1);
                bool nn = (nn_saved > 0);
                if (ImGui::Checkbox("Nearest Neighbor", &nn)) {
                    settings.set_core_option(ck, "nearest_neighbor", nn ? 1 : 0);
                    if (live) {
                        fsys->output.nearest_neighbor = nn;
                        fsys->setup_present_widgets(settings, ck);
                    }
                }
                if (ImGui::IsItemHovered() && nn_saved < 0)
                    ImGui::SetTooltip("Not yet set — will use automatic default on load.");
            }

            // Hide Overscan
            {
                bool ho = (settings.get_core_option(ck, "hide_overscan", 0) != 0);
                if (ImGui::Checkbox("Hide Overscan", &ho)) {
                    settings.set_core_option(ck, "hide_overscan", ho ? 1 : 0);
                    if (live) {
                        fsys->output.hide_overscan = ho;
                        fsys->setup_present_widgets(settings, ck);
                    }
                }
            }

            // Core-specific present_widgets
            if (live) {
                for (auto& pw : fsys->present_widgets) {
                    u32 old_val = (pw.widget.kind == JSMD_radiogroup)
                        ? pw.widget.radiogroup.value : pw.widget.checkbox.value;
                    render_debugger_widget(pw.widget);
                    u32 new_val = (pw.widget.kind == JSMD_radiogroup)
                        ? pw.widget.radiogroup.value : pw.widget.checkbox.value;
                    if (new_val != old_val)
                        settings.set_core_option(ck, pw.ini_key, (i32)new_val);
                }
            } else if (strcmp(ck, "apple2e") == 0) {
                i32 cm = settings.get_core_option(ck, "color_mode", 0);
                int ch = cm;
                ImGui::RadioButton("Monochrome",    &ch, 0); ImGui::SameLine();
                ImGui::RadioButton("Green Phosphor",&ch, 1); ImGui::SameLine();
                ImGui::RadioButton("Color NTSC",    &ch, 2);
                if (ch != cm)
                    settings.set_core_option(ck, "color_mode", ch);
                bool rb = (settings.get_core_option(ck, "respect_burst", 1) != 0);
                if (ImGui::Checkbox("Respect color burst suppression", &rb))
                    settings.set_core_option(ck, "respect_burst", rb ? 1 : 0);

                ImGui::Spacing();
                ImGui::SeparatorText("Expansion Slots");
                ImGui::TextDisabled("Changes take effect on next launch.");
                ImGui::Spacing();

                // Card types available for selection
                struct CardEntry { const char* token; const char* label; bool available; };
                static const CardEntry cards[] = {
                    { "empty",          "--- (empty)",               true  },
                    { "disk2",          "Disk II",                   true  },
                    { "mockingboard_b", "Mockingboard B (coming soon)", false },
                };
                static const int NUM_CARDS = (int)(sizeof(cards)/sizeof(cards[0]));

                if (ImGui::BeginTable("##slots", 2, ImGuiTableFlags_SizingFixedFit)) {
                    ImGui::TableSetupColumn("Slot",  ImGuiTableColumnFlags_WidthFixed, 50.f);
                    ImGui::TableSetupColumn("Card",  ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableHeadersRow();

                    for (u32 si = 1; si < 8; si++) {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::Text("Slot %u", si);
                        ImGui::TableSetColumnIndex(1);

                        std::string cur = settings.get_slot_card(ck, si);
                        int cur_idx = 0;
                        for (int ci = 0; ci < NUM_CARDS; ci++)
                            if (cur == cards[ci].token) { cur_idx = ci; break; }

                        ImGui::PushID((int)si);
                        ImGui::SetNextItemWidth(-1.f);
                        if (ImGui::BeginCombo("##card", cards[cur_idx].label)) {
                            for (int ci = 0; ci < NUM_CARDS; ci++) {
                                if (!cards[ci].available) {
                                    ImGui::TextDisabled("%s", cards[ci].label);
                                    continue;
                                }
                                bool sel = (ci == cur_idx);
                                if (ImGui::Selectable(cards[ci].label, sel))
                                    settings.set_slot_card(ck, si, cards[ci].token);
                                if (sel) ImGui::SetItemDefaultFocus();
                            }
                            ImGui::EndCombo();
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }

                ImGui::Spacing();
                ImGui::SeparatorText("Slot Profiles");

                // List saved profiles
                auto profiles = settings.list_slot_profiles(ck);
                for (auto& pname : profiles) {
                    ImGui::PushID(pname.c_str());
                    if (ImGui::Button("Load")) settings.load_slot_profile(ck, pname.c_str());
                    ImGui::SameLine();
                    if (ImGui::Button("X"))    settings.delete_slot_profile(ck, pname.c_str());
                    ImGui::SameLine();
                    ImGui::TextUnformatted(pname.c_str());
                    ImGui::PopID();
                }

                // Save new profile
                {
                    static char profile_name_buf[64] = {};
                    ImGui::SetNextItemWidth(160.f);
                    ImGui::InputText("##pname", profile_name_buf, sizeof(profile_name_buf));
                    ImGui::SameLine();
                    bool name_ok = profile_name_buf[0] != '\0';
                    if (!name_ok) ImGui::BeginDisabled();
                    if (ImGui::Button("Save Profile")) {
                        settings.save_slot_profile(ck, profile_name_buf);
                        profile_name_buf[0] = '\0';
                    }
                    if (!name_ok) ImGui::EndDisabled();
                }
            }

            ImGui::Spacing();
        }

        // Boot and core-specific options
        {
            // Resolve representative jsm::systems for this core key
            static const jsm::systems all_sys[] = {
                jsm::systems::NDS,            jsm::systems::GBA,
                jsm::systems::DMG,            jsm::systems::GBC,
                jsm::systems::NES,            jsm::systems::SNES,
                jsm::systems::GENESIS_USA,    jsm::systems::MEGADRIVE_PAL,
                jsm::systems::SMS1,           jsm::systems::SMS2,
                jsm::systems::GG,             jsm::systems::SG1000,
                jsm::systems::DREAMCAST,      jsm::systems::PS1,
                jsm::systems::NEOGEO_AES,     jsm::systems::NEOGEO_POCKET,
                jsm::systems::APPLEIIe,       jsm::systems::COMMODORE64,
                jsm::systems::ZX_SPECTRUM_48K,jsm::systems::TURBOGRAFX16,
                jsm::systems::ATARI2600,      jsm::systems::CASIO_PV1000,
                jsm::systems::GALAKSIJA,      jsm::systems::COSMAC_VIP_2k,
                jsm::systems::MAC128K,
            };
            jsm::systems rep = live ? fsys->sys->kind : jsm::systems::DMG;
            if (!live) {
                for (auto s : all_sys) {
                    const char* k = AppSettings::sys_to_core_key(s);
                    if (k && strcmp(k, ck) == 0) { rep = s; break; }
                }
            }

            // Fast boot
            bool supports_fast_boot = (rep == jsm::systems::DMG ||
                                       rep == jsm::systems::GBC ||
                                       rep == jsm::systems::GBA ||
                                       rep == jsm::systems::NDS);
            if (supports_fast_boot) {
                ImGui::SeparatorText("Boot");
                i32 fb_val = settings.get_core_option(ck, "fast_boot", 1);
                bool fb = (fb_val != 0);
                if (ImGui::Checkbox("Fast Boot", &fb)) {
                    settings.set_core_option(ck, "fast_boot", fb ? 1 : 0);
                    if (live) fsys->sys->option_changed("fast_boot", fb ? 1 : 0);
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Skip the BIOS boot animation and jump\n"
                                      "directly to the game on reset.");
                ImGui::Spacing();
            }

            // Core-specific options (arm7/9 mode, memory card mode, etc.)
            std::vector<jsm_core_option> opt_buf;
            std::vector<jsm_core_option>* opts_ptr = nullptr;
            if (live) {
                opts_ptr = &fsys->sys->options;
            } else {
                populate_core_options(rep, opt_buf);
                for (auto& opt : opt_buf)
                    opt.value = settings.get_core_option(ck, opt.key, opt.value);
                opts_ptr = &opt_buf;
            }

            if (opts_ptr && !opts_ptr->empty()) {
                ImGui::SeparatorText("Options");
                for (auto& opt : *opts_ptr) {
                    ImGui::PushID(opt.key);
                    if (opt.kind == jsm_core_option::OPTION_BOOL) {
                        bool val = (opt.value != 0);
                        if (ImGui::Checkbox(opt.label, &val)) {
                            opt.value = val ? 1 : 0;
                            settings.set_core_option(ck, opt.key, opt.value);
                            if (live) fsys->sys->option_changed(opt.key, opt.value);
                        }
                    } else if (opt.kind == jsm_core_option::OPTION_STRING) {
                        float input_w = ImGui::GetContentRegionAvail().x * 0.5f;
                        ImGui::SetNextItemWidth(input_w > 80.0f ? input_w : 80.0f);

                        struct TabCBData { std::string dir; };
                        static TabCBData cb_data;
                        if (live && fsys)
                            cb_data.dir = fsys->sram_save_dir;
                        else {
                            std::string base = settings.get_saves_dir();
                            cb_data.dir = base.empty() ? std::string() : (base + "/" + ck);
                        }

                        auto tab_cb = [](ImGuiInputTextCallbackData *d) -> int {
                            auto *td = static_cast<TabCBData *>(d->UserData);
                            if (td->dir.empty()) return 0;
                            std::string prefix(d->Buf, static_cast<size_t>(d->BufTextLen));
                            std::error_code ec;
                            for (auto &entry : std::filesystem::directory_iterator(td->dir, ec)) {
                                if (entry.path().extension() != ".mcd") continue;
                                std::string stem = entry.path().stem().string();
                                if (stem.size() < prefix.size()) continue;
                                bool ok = true;
                                for (size_t i = 0; i < prefix.size(); i++) {
                                    if (tolower((unsigned char)stem[i]) != tolower((unsigned char)prefix[i])) {
                                        ok = false; break;
                                    }
                                }
                                if (!ok) continue;
                                d->DeleteChars(0, d->BufTextLen);
                                d->InsertChars(0, stem.c_str());
                                break;
                            }
                            return 0;
                        };

                        ImGui::InputText(opt.label, opt.str_value, sizeof(opt.str_value),
                                         ImGuiInputTextFlags_CallbackCompletion, tab_cb, &cb_data);
                        if (ImGui::IsItemDeactivatedAfterEdit()) {
                            settings.set_core_option_str(ck, opt.key, opt.str_value);
                            if (live) fsys->apply_core_options(settings, ck);
                        }
                    } else { // OPTION_ENUM
                        int idx = opt.choice_index();
                        std::string items;
                        for (int ci = 0; ci < opt.num_choices; ci++) {
                            items += opt.choices[ci].label;
                            items += '\0';
                        }
                        items += '\0';
                        float combo_w = ImGui::GetContentRegionAvail().x * 0.5f;
                        ImGui::SetNextItemWidth(combo_w > 80.0f ? combo_w : 80.0f);
                        if (ImGui::Combo(opt.label, &idx, items.c_str())) {
                            if (idx >= 0 && idx < opt.num_choices) {
                                opt.value = opt.choices[idx].value;
                                settings.set_core_option(ck, opt.key, opt.value);
                                if (live) fsys->sys->option_changed(opt.key, opt.value);
                            }
                        }
                    }
                    ImGui::PopID();
                }
            }
        }
    }
    ImGui::EndChild(); // ##opt_core_content
}

bool imgui_jocasta_app::check_required_bios_or_warn(jsm::systems system)
{
    if (!AppSettings::sys_has_bios(system))
        return true;

    const bios_sys_spec *spec = find_bios_spec(system);
    if (!spec)
        return true;

    std::string dir = settings.get_bios_dir(system);
    if (dir.empty())
        dir = AppSettings::default_bios_dir(system);

    std::vector<std::string> missing;

    // NeoGeo accepts either the system-specific BIOS file or neogeo.zip.
    if (system == jsm::systems::NEOGEO_AES || system == jsm::systems::NEOGEO_MVS) {
        if (!regular_file_exists(join_path(dir, "neogeo.zip")) &&
            !regular_file_exists(join_path(dir, spec->files[0].filename))) {
            missing.emplace_back(std::string(spec->files[0].filename) + " or neogeo.zip");
        }
    }
    else {
        for (int i = 0; i < spec->num_files; i++) {
            if (spec->files[i].optional) continue;
            if (check_bios_file(dir, spec->files[i]) == bios_status::missing)
                missing.emplace_back(spec->files[i].filename);
        }
    }

    if (missing.empty())
        return true;

    std::string msg = std::string(AppSettings::sys_to_label(system)) +
        " requires BIOS files before it can start.\n\nLooked in:\n" + dir +
        "\n\nMissing:\n";
    for (const auto& item : missing)
        msg += "  " + item + "\n";
    msg += "\nPut the BIOS files there, or choose a different BIOS directory in Settings.";

    bios_warning_message = std::move(msg);
    show_bios_warning = true;
    return false;
}

// ---------------------------------------------------------------------------
// Settings → In Development page
// ---------------------------------------------------------------------------
void imgui_jocasta_app::render_settings_page_dev()
{
    bool enabled = settings.get_dev_features_enabled();

    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "In-Development Features");
    ImGui::Spacing();
    ImGui::TextWrapped(
        "These features exist in the emulator but are not yet considered stable. "
        "Enabling them may cause crashes, incorrect behaviour, or save-state corruption.");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextDisabled("Cores:");
    ImGui::BulletText("Neo Geo Pocket / Neo Geo Pocket Color");
    ImGui::BulletText("Dreamcast");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    bool checkbox_val = enabled;
    if (ImGui::Checkbox("Enable in-development features", &checkbox_val)) {
        if (checkbox_val && !enabled) {
            // User just ticked it on — ask for confirmation via modal
            pending_dev_features_enable = true;
            ImGui::OpenPopup("##dev_warn");
        } else if (!checkbox_val && enabled) {
            settings.set_dev_features_enabled(false);
        }
    }

    // Warning confirmation modal
    if (pending_dev_features_enable)
        ImGui::OpenPopup("##dev_warn");

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("##dev_warn", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.2f, 1.0f), "Warning");
        ImGui::Spacing();
        ImGui::TextWrapped(
            "These features are in development and may be\n"
            "more curse than blessing!");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        if (ImGui::Button("Enable anyway", ImVec2(130, 0))) {
            settings.set_dev_features_enabled(true);
            pending_dev_features_enable = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(80, 0))) {
            pending_dev_features_enable = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void imgui_jocasta_app::render_bios_warning()
{
    if (show_bios_warning)
        ImGui::OpenPopup("Missing BIOS");

    if (ImGui::BeginPopupModal("Missing BIOS", &show_bios_warning,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("%s", bios_warning_message.c_str());
        ImGui::Spacing();

        if (ImGui::Button("BIOS Settings", ImVec2(140, 0))) {
            settings_nav = 1;
            show_settings_window = true;
            show_bios_warning = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("OK", ImVec2(100, 0))) {
            show_bios_warning = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// ---------------------------------------------------------------------------
// Settings window shell — nav panel + dispatch
// ---------------------------------------------------------------------------
void imgui_jocasta_app::render_settings_window()
{
    if (!show_settings_window) return;

    ImGui::SetNextWindowSize(ImVec2(720, 520), ImGuiCond_Appearing);
    if (!ImGui::Begin("Settings", &show_settings_window)) {
        ImGui::End();
        return;
    }

    // Header: config file path + Open Folder
    {
        std::string folder = settings.ini_folder_path();
        ImGui::TextDisabled("Config: %s/jocasta.ini", folder.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Open Folder")) {
            std::string url = "file://" + folder;
            SDL_OpenURL(url.c_str());
        }
    }
    ImGui::Separator();
    ImGui::Spacing();

    // Left nav panel
    ImGui::BeginChild("##nav", ImVec2(155, 0), true);
    if (ImGui::Selectable("General",        settings_nav == 0)) settings_nav = 0;
    if (ImGui::Selectable("BIOS",           settings_nav == 1)) settings_nav = 1;
    if (ImGui::Selectable("Hotkeys",        settings_nav == 2)) settings_nav = 2;
    if (ImGui::Selectable("Input",          settings_nav == 3)) settings_nav = 3;
    if (ImGui::Selectable("Options",        settings_nav == 4)) settings_nav = 4;
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    if (ImGui::Selectable("In Development", settings_nav == 5)) settings_nav = 5;
    ImGui::EndChild();
    ImGui::SameLine();

    // Right content panel
    ImGui::BeginChild("##content", ImVec2(0, 0), false);
    switch (settings_nav) {
        case 0: render_settings_page_general(); break;
        case 1: render_settings_page_bios();    break;
        case 2: render_settings_page_hotkeys(); break;
        case 3: render_settings_page_input();   break;
        case 4: render_settings_page_options(); break;
        case 5: render_settings_page_dev();     break;
    }
    ImGui::EndChild();
    ImGui::End();
}

void imgui_jocasta_app::render_main_menu_bar()
{
    if (!ImGui::BeginMainMenuBar()) return;

    if (ImGui::BeginMenu("File")) {
        // Shared helper: find a LOAD_SYSTEMS entry by system kind
        auto find_sys = [](jsm::systems sys) -> const load_system_entry* {
            for (auto& e : LOAD_SYSTEMS)
                if (e.system == sys) return &e;
            return nullptr;
        };

        bool dev_on = settings.get_dev_features_enabled();

        // Returns true if this system should appear in menus
        auto sys_visible = [&](jsm::systems s) -> bool {
            if (!dev_on) {
                if (s == jsm::systems::DREAMCAST)            return false;
                if (s == jsm::systems::NEOGEO_POCKET)        return false;
                if (s == jsm::systems::NEOGEO_POCKET_COLOR)  return false;
            }
            return true;
        };

        // Shared manufacturer-group renderer — action callback varies per menu
        auto render_groups = [&](auto action) {
            if (ImGui::BeginMenu("Apple")) {
                for (auto s : { jsm::systems::APPLEIIe, jsm::systems::MAC128K, jsm::systems::MAC512K, jsm::systems::MACPLUS_1MB })
                    if (auto* e = find_sys(s)) action(*e);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Nintendo")) {
                for (auto s : { jsm::systems::NES, jsm::systems::SNES, jsm::systems::DMG, jsm::systems::GBC, jsm::systems::GBA, jsm::systems::NDS })
                    if (auto* e = find_sys(s)) action(*e);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Sega")) {
                for (auto s : { jsm::systems::SG1000, jsm::systems::SMS1, jsm::systems::SMS2, jsm::systems::GG,
                                jsm::systems::GENESIS_JAP, jsm::systems::GENESIS_USA, jsm::systems::MEGADRIVE_PAL,
                                jsm::systems::DREAMCAST })
                    if (sys_visible(s)) if (auto* e = find_sys(s)) action(*e);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Sinclair")) {
                for (auto s : { jsm::systems::ZX_SPECTRUM_48K, jsm::systems::ZX_SPECTRUM_128K })
                    if (auto* e = find_sys(s)) action(*e);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("SNK")) {
                for (auto s : { jsm::systems::NEOGEO_AES, jsm::systems::NEOGEO_MVS, jsm::systems::NEOGEO_POCKET, jsm::systems::NEOGEO_POCKET_COLOR })
                    if (sys_visible(s)) if (auto* e = find_sys(s)) action(*e);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("COSMAC VIP")) {
                for (auto s : { jsm::systems::COSMAC_VIP_2k, jsm::systems::COSMAC_VIP_4k })
                    if (auto* e = find_sys(s)) action(*e);
                ImGui::EndMenu();
            }
            ImGui::Separator();
            for (auto s : { jsm::systems::ATARI2600, jsm::systems::CASIO_PV1000,
                            jsm::systems::COMMODORE64, jsm::systems::GALAKSIJA,
                            jsm::systems::PS1, jsm::systems::TURBOGRAFX16 })
                if (auto* e = find_sys(s)) action(*e);
        };

        // ── File → Recent ─────────────────────────────────────────────────────
        if (ImGui::BeginMenu("Recent")) {
            const auto& recent = settings.get_recent();
            if (recent.empty()) {
                ImGui::TextDisabled("(none)");
            } else {
                for (const auto& entry : recent) {
                    if (entry.path.empty()) continue;
                    const char* fname = strrchr(entry.path.c_str(), '/');
                    fname = fname ? fname + 1 : entry.path.c_str();
                    if (!fname || fname[0] == '\0') continue;
                    char lbl[512];
                    snprintf(lbl, sizeof(lbl), "%s  (%s)", fname,
                             AppSettings::sys_to_label(entry.system));
                    if (ImGui::MenuItem(lbl))
                        load_path_for_system(entry.system, entry.path.c_str(), false);
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Clear"))
                    settings.clear_recent();
            }
            ImGui::EndMenu();
        }

        ImGui::Separator();

        // ── File → New ────────────────────────────────────────────────────────
        // Powers on the system with no media.  The user can then insert media
        // via the Media window, or press Play to boot straight to the BIOS.
        if (ImGui::BeginMenu("New")) {
            render_groups([&](const load_system_entry& e) {
                if (ImGui::MenuItem(e.label))
                    boot_bios_for_system(e.system);
            });
            ImGui::EndMenu();
        }

        // ── File → Load ───────────────────────────────────────────────────────
        // Opens a file dialog for the chosen system.  If that system is already
        // running, only the media is swapped; otherwise a new core is created.
        if (ImGui::BeginMenu("Load")) {
            render_groups([&](const load_system_entry& entry) {
                int nf = 0;
                if (entry.filters) { while (entry.filters[nf]) nf++; }
                char sp[500];
                if (entry.supports_folder) {
                    if (ImGui::BeginMenu(entry.label)) {
                        default_load_path(sp, sizeof(sp), entry.system, settings);
                        if (ImGui::MenuItem("File...")) {
                            const char* p = tinyfd_openFileDialog("Load", sp, nf, entry.filters, nullptr, 0);
                            if (p) load_path_for_system(entry.system, p, false);
                        }
                        if (ImGui::MenuItem("Folder...")) {
                            const char* p = tinyfd_selectFolderDialog("Load Folder", sp);
                            if (p) load_path_for_system(entry.system, p, true);
                        }
                        ImGui::EndMenu();
                    }
                } else {
                    if (ImGui::MenuItem(entry.label)) {
                        default_load_path(sp, sizeof(sp), entry.system, settings);
                        const char* p = tinyfd_openFileDialog("Load", sp, nf, entry.filters, nullptr, 0);
                        if (p) load_path_for_system(entry.system, p, false);
                    }
                }
            });

            ImGui::EndMenu();
        }

        ImGui::Separator();
        Binding ss_bind = settings.get_hotkey("screenshot");
        std::string ss_shortcut = ss_bind.is_none() ? "" : ss_bind.display_name();
        if (ImGui::MenuItem("Screenshot", ss_shortcut.empty() ? nullptr : ss_shortcut.c_str(), false, fsys != nullptr))
            fsys->screenshot = true;
        bool save_states_ok = fsys != nullptr;

        ImGui::Separator();
        if (ImGui::MenuItem("Save State", "F5", false, save_states_ok)) {
            const char* ck = AppSettings::sys_to_core_key(which);
            fsys->save_state_to_slot(settings.get_state_slot(ck));
            invalidate_save_manager_cache();
        }
        if (ImGui::MenuItem("Load State", "F7", false, save_states_ok)) {
            const char* ck = AppSettings::sys_to_core_key(which);
            fsys->load_state_from_slot(settings.get_state_slot(ck));
        }
        if (ImGui::MenuItem("Save Manager...", nullptr, show_save_manager, save_states_ok)) {
            show_save_manager = !show_save_manager;
            settings.set_show_save_manager(show_save_manager);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Settings..."))
            show_settings_window = true;
        // On Boot action — what happens automatically when a game is loaded
        if (ImGui::BeginMenu("On Boot")) {
            auto cur = settings.get_on_boot_action();
            if (ImGui::MenuItem("Play Game",             nullptr, cur == AppSettings::OnBoot_Play))
                settings.set_on_boot_action(AppSettings::OnBoot_Play);
            if (ImGui::MenuItem("Load Most Recent Save", nullptr, cur == AppSettings::OnBoot_LoadRecent))
                settings.set_on_boot_action(AppSettings::OnBoot_LoadRecent);
            if (ImGui::MenuItem("Pause",                 nullptr, cur == AppSettings::OnBoot_Pause))
                settings.set_on_boot_action(AppSettings::OnBoot_Pause);
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Quit"))
            done = true;
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("System")) {
        bool has_sys = (fsys != nullptr && fsys->sys != nullptr);

        // Play / Pause toggle
        bool is_playing = has_sys && (fsys->run_state == FSS_play);
        if (ImGui::MenuItem(is_playing ? "Pause" : "Play", nullptr, false, has_sys)) {
            if (is_playing) do_pause();
            else            do_play();
        }

        // Reset — only shown for systems that have a physical reset button
        {
            bool has_reset = has_sys &&
                             fsys->io.chassis.vec &&
                             fsys->io.chassis.get().chassis.find_button(DBCID_ch_reset) != nullptr;
            if (ImGui::MenuItem("Reset", nullptr, false, has_reset)) {
                fsys->sys->reset();
            }
        }

        ImGui::Separator();

        // Debugging (trace) toggle — mirrors the "Trace enabled" checkbox in the Play window
        bool dbg_on = dbg.do_debug;
        if (ImGui::MenuItem("Enable Debugging", nullptr, dbg_on, has_sys)) {
            if (dbg_on) dbg_disable_trace();
            else        dbg_enable_trace();
        }

        ImGui::Separator();

        // Close — destroys the loaded core
        if (ImGui::MenuItem("Close", nullptr, false, has_sys)) {
            delete fsys;
            fsys = nullptr;
        }

        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View")) {
        Binding fs_bind = settings.get_hotkey("fullscreen");
        std::string fs_shortcut = fs_bind.is_none() ? "" : fs_bind.display_name();
        if (ImGui::MenuItem("Fullscreen", fs_shortcut.empty() ? nullptr : fs_shortcut.c_str(),
                            fullscreen_mode, fsys != nullptr)) {
            fullscreen_mode = !fullscreen_mode;
            settings.set_fullscreen(fullscreen_mode);
        }
        ImGui::Separator();
        {
            bool vc = settings.get_show_virtual_controller_global();
            if (ImGui::MenuItem("Virtual Controller", nullptr, vc, fsys != nullptr))
                settings.set_show_virtual_controller_global(!vc);
            bool vk = settings.get_show_virtual_keyboard_global();
            if (ImGui::MenuItem("Virtual Keyboard", nullptr, vk, fsys != nullptr))
                settings.set_show_virtual_keyboard_global(!vk);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Media (Cartridge / Disc)", nullptr, show_media_window, fsys != nullptr))
            show_media_window = !show_media_window;
        if (ImGui::MenuItem("Core Options", nullptr, show_core_options, fsys != nullptr))
            show_core_options = !show_core_options;
        ImGui::Separator();
        if (ImGui::MenuItem("Debug Window Manager...", nullptr, show_window_manager))
            show_window_manager = !show_window_manager;
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Debug")) {
        render_managed_window_toggles(true);
        ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
}

void imgui_jocasta_app::render_media_window()
{
    if (!fsys || !fsys->sys) return;

    // Look up system entry for filter lists
    const load_system_entry* se = nullptr;
    for (auto& e : LOAD_SYSTEMS)
        if (e.system == which) { se = &e; break; }

    // Helper: return the directory part of a path (empty if none)
    auto path_dir = [](const std::string& p) -> std::string {
        auto pos = p.rfind('/');
        return (pos != std::string::npos) ? p.substr(0, pos) : std::string();
    };

    // Helper: count non-null entries in a filter list
    auto count_filters = [](const char* const* f) -> int {
        if (!f) return 0;
        int n = 0;
        while (f[n]) n++;
        return n;
    };

    // Helper: build the start-path for a dialog (prefer loaded path dir, then last-dir)
    auto start_path = [&](const std::string& loaded) -> std::string {
        if (!loaded.empty()) {
            std::string d = path_dir(loaded);
            if (!d.empty()) return existing_dialog_dir(d, true);
        }
        char sp[512];
        default_load_path(sp, sizeof(sp), which, settings);
        return sp;
    };

    auto& IOs = fsys->sys->IOs;

    // Count devices per type for window title numbering
    int n_cart = 0, n_disc = 0, n_cass = 0;
    for (auto& pio : IOs) {
        if      (pio.kind == HID_CART_PORT)      n_cart++;
        else if (pio.kind == HID_DISC_DRIVE)      n_disc++;
        else if (pio.kind == HID_AUDIO_CASSETTE)  n_cass++;
    }

    int cur_cart = 0, cur_disc = 0, cur_cass = 0, media_idx = 0;

    for (u32 i = 0; i < (u32)IOs.size(); i++) {
        auto& pio = IOs[i];
        if (pio.kind != HID_CART_PORT &&
            pio.kind != HID_DISC_DRIVE &&
            pio.kind != HID_AUDIO_CASSETTE)
            continue;

        // Build window title — use ### so ImGui uses the ID, not the visible label,
        // to track window position/size across renames.
        char wintitle[64];
        if (pio.kind == HID_CART_PORT) {
            cur_cart++;
            if (n_cart == 1) snprintf(wintitle, sizeof(wintitle), "Cartridge###mdev_%u", i);
            else             snprintf(wintitle, sizeof(wintitle), "Cartridge %d###mdev_%u", cur_cart, i);
        } else if (pio.kind == HID_DISC_DRIVE) {
            cur_disc++;
            if (n_disc == 1) snprintf(wintitle, sizeof(wintitle), "Disc Drive###mdev_%u", i);
            else             snprintf(wintitle, sizeof(wintitle), "Disc Drive %d###mdev_%u", cur_disc, i);
        } else {
            cur_cass++;
            if (n_cass == 1) snprintf(wintitle, sizeof(wintitle), "Cassette###mdev_%u", i);
            else             snprintf(wintitle, sizeof(wintitle), "Cassette %d###mdev_%u", cur_cass, i);
        }

        // Always-open window (nullptr p_open = no close button)
        ImGui::SetNextWindowPos(ImVec2(10.0f, 30.0f + media_idx * 175.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(360, 0), ImGuiCond_Appearing);
        media_idx++;
        if (!ImGui::Begin(wintitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::End();
            continue;
        }

        std::string loaded;
        if (auto it = fsys->media_paths.find(i); it != fsys->media_paths.end())
            loaded = it->second;
        const std::string fname = display_filename_from_path(loaded);

        // ── Cartridge slot ─────────────────────────────────────────────────
        if (pio.kind == HID_CART_PORT) {
            if (loaded.empty()) {
                ImGui::TextDisabled("(no cartridge)");
            } else {
                ImGui::TextUnformatted(fname.c_str(), fname.c_str() + fname.size());
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", loaded.c_str());
            }

            ImGui::Spacing();

            const char* const* filt = se ? se->filters : nullptr;
            int nf = count_filters(filt);
            std::string sp = start_path(loaded);

            if (ImGui::Button("Load...")) {
                const char* p = tinyfd_openFileDialog("Load Cartridge", sp.c_str(),
                                                      nf, filt, nullptr, 0);
                if (p) {
                    if (fsys->insert_media(pio, i, p, false)) {
                        fsys->on_media_inserted(i, p, false);
                        if (const char* ck = AppSettings::sys_to_core_key(which))
                            fsys->apply_core_options(settings, ck);
                        invalidate_save_manager_cache();
                    }
                    std::string d = path_dir(p);
                    if (!d.empty()) settings.set_last_dir(which, d.c_str());
                }
            }

            if (pio.cartridge_port.unload_cart) {
                ImGui::SameLine();
                if (ImGui::Button("Eject"))
                    fsys->eject_media(pio, i);
            }
        }

        // ── Disc drive ──────────────────────────────────────────────────────
        else if (pio.kind == HID_DISC_DRIVE) {
            // Activity light — red when motor is spinning
            {
                bool active = pio.disc_drive.motor_on;
                ImVec4 col = active ? ImVec4(1.f, 0.15f, 0.15f, 1.f)
                                    : ImVec4(0.25f, 0.05f, 0.05f, 1.f);
                ImGui::TextColored(col, active ? "[*]" : "[ ]");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(active ? "Drive motor on" : "Drive motor off");
                ImGui::SameLine();
            }

            if (loaded.empty()) {
                ImGui::TextDisabled("(no disc)");
            } else {
                ImGui::TextUnformatted(fname.c_str(), fname.c_str() + fname.size());
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", loaded.c_str());
            }

            ImGui::Spacing();

            const char* const* filt = se
                ? (se->disc_filters ? se->disc_filters : se->filters) : nullptr;
            int nf = count_filters(filt);
            std::string sp = start_path(loaded);

            if (ImGui::Button("Insert...")) {
                const char* p = tinyfd_openFileDialog("Insert Disc", sp.c_str(),
                                                      nf, filt, nullptr, 0);
                if (p) {
                    if (fsys->insert_media(pio, i, p, false)) {
                        fsys->on_media_inserted(i, p, false);
                        if (const char* ck = AppSettings::sys_to_core_key(which))
                            fsys->apply_core_options(settings, ck);
                        invalidate_save_manager_cache();
                    }
                    std::string d = path_dir(p);
                    if (!d.empty()) settings.set_last_dir(which, d.c_str());
                }
            }

            if (pio.disc_drive.remove_disc) {
                ImGui::SameLine();
                if (ImGui::Button("Eject"))
                    fsys->eject_media(pio, i);
            }
            if (pio.disc_drive.open_drive) {
                ImGui::SameLine();
                if (ImGui::Button("Open Drive"))
                    pio.disc_drive.open_drive(fsys->sys);
            }
            if (pio.disc_drive.close_drive) {
                ImGui::SameLine();
                if (ImGui::Button("Close Drive"))
                    pio.disc_drive.close_drive(fsys->sys);
            }
        }

        // ── Cassette ────────────────────────────────────────────────────────
        else if (pio.kind == HID_AUDIO_CASSETTE) {
            bool is_playing = (fsys->cassette_state == full_system::CassetteState::Playing);

            if (loaded.empty()) {
                ImGui::TextDisabled("(no tape)");
            } else {
                ImGui::TextUnformatted(fname.c_str(), fname.c_str() + fname.size());
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", loaded.c_str());
                ImGui::SameLine();
                ImGui::TextDisabled(is_playing ? "[Playing]" : "[Stopped]");
            }

            ImGui::Spacing();

            const char* const* filt = se
                ? (se->tape_filters ? se->tape_filters : se->filters) : nullptr;
            int nf = count_filters(filt);
            std::string sp = start_path(loaded);

            if (ImGui::Button("Insert...")) {
                const char* p = tinyfd_openFileDialog("Insert Tape", sp.c_str(),
                                                      nf, filt, nullptr, 0);
                if (p) {
                    if (fsys->insert_media(pio, i, p, false)) {
                        fsys->on_media_inserted(i, p, false);
                        if (const char* ck = AppSettings::sys_to_core_key(which))
                            fsys->apply_core_options(settings, ck);
                        invalidate_save_manager_cache();
                    }
                    std::string d = path_dir(p);
                    if (!d.empty()) settings.set_last_dir(which, d.c_str());
                }
            }
            if (pio.audio_cassette.remove_tape) {
                ImGui::SameLine();
                if (ImGui::Button("Eject"))
                    fsys->eject_media(pio, i);
            }

            ImGui::Spacing();

            // Transport controls
            bool any_transport = pio.audio_cassette.rewind ||
                                 pio.audio_cassette.stop   ||
                                 pio.audio_cassette.play;
            if (any_transport) {
                if (pio.audio_cassette.rewind) {
                    if (ImGui::Button("|<  Rewind")) {
                        pio.audio_cassette.rewind(fsys->sys);
                        fsys->cassette_state = full_system::CassetteState::Stopped;
                    }
                    ImGui::SameLine();
                }
                if (pio.audio_cassette.stop) {
                    if (ImGui::Button("[]  Stop")) {
                        pio.audio_cassette.stop(fsys->sys);
                        fsys->cassette_state = full_system::CassetteState::Stopped;
                    }
                    ImGui::SameLine();
                }
                if (pio.audio_cassette.play) {
                    if (is_playing)
                        ImGui::PushStyleColor(ImGuiCol_Button,
                            ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                    if (ImGui::Button(">  Play")) {
                        pio.audio_cassette.play(fsys->sys);
                        fsys->cassette_state = full_system::CassetteState::Playing;
                    }
                    if (is_playing) ImGui::PopStyleColor();
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip(is_playing ? "Playing" : "Click to play");
                }
            }
        }

        ImGui::End();
    }
}

void imgui_jocasta_app::invalidate_save_manager_cache()
{
    for (auto &sc : save_manager_slot_cache) {
        sc.populated = false;
        sc.attempted = false;
        sc.timestamp.clear();
        sc.thumb.reset();
    }
    save_manager_cache_owner = fsys;
}

void imgui_jocasta_app::render_save_manager()
{
    if (!show_save_manager) {
        save_manager_was_showing = false;
        return;
    }

    bool just_opened = !save_manager_was_showing;
    save_manager_was_showing = true;

    if (save_manager_cache_owner != fsys || just_opened)
        invalidate_save_manager_cache();

    ImGui::SetNextWindowSize(ImVec2(700, 400), ImGuiCond_Appearing);
    if (!ImGui::Begin("Save Manager", &show_save_manager)) {
        if (!show_save_manager) settings.set_show_save_manager(false); // X button closed it
        ImGui::End();
        return;
    }

    if (!fsys) {
        ImGui::TextDisabled("No core loaded.");
        ImGui::End();
        return;
    }

    const char *ck    = AppSettings::sys_to_core_key(which);
    int  cur_slot     = settings.get_state_slot(ck);

    // Lazy-load slot thumbnails (one attempt per slot per cache lifetime)
    for (int s = 0; s <= 9; s++) {
        auto &sc = save_manager_slot_cache[s];
        if (sc.attempted) continue;
        sc.attempted = true;

        slot_thumbnail info;
        if (fsys->read_slot_thumbnail(s, info)) {
            sc.populated  = true;
            sc.timestamp  = std::move(info.timestamp);
            if (info.valid && info.width > 0 && info.height > 0) {
                sc.thumb = std::make_unique<my_texture>();
                char lbl[32];
                snprintf(lbl, sizeof(lbl), "savemgr_slot%d", s);
#ifdef JSM_SDLR3
                sc.thumb->setup(renderer, lbl, info.width, info.height);
#elif defined(JSM_SDLGPU)
                sc.thumb->setup(device, lbl, info.width, info.height);
#elif defined(JSM_OPENGL)
                sc.thumb->setup(lbl, info.width, info.height);
#endif
                sc.thumb->upload_data(info.rgba.data(), info.rgba.size(),
                                      info.width, info.height);
            }
        }
    }

    // Layout: 5-column grid; thumbnails scale to fill each cell
    const float lhs     = ImGui::GetTextLineHeightWithSpacing();
    const float lh      = ImGui::GetTextLineHeight();
    const float pad     = ImGui::GetStyle().ItemSpacing.y;
    // Thumbnail max: narrower cells since we have 5 columns
    static const float THUMB_MAX_W = 120.0f;
    static const float THUMB_MAX_H = 90.0f;
    const float cell_h = lhs + THUMB_MAX_H + lh + pad * 3.0f;

    const ImGuiTableFlags tflags =
        ImGuiTableFlags_BordersOuter |
        ImGuiTableFlags_BordersInnerV |
        ImGuiTableFlags_SizingStretchSame;

    if (ImGui::BeginTable("##savemgr_grid", 5, tflags)) {
        for (int s = 0; s <= 9; s++) {
            ImGui::TableNextColumn();
            ImGui::PushID(s);

            auto &sc      = save_manager_slot_cache[s];
            bool selected = (s == cur_slot);

            // Use a push-count so Pop is always exactly balanced with Push
            int n_color_pushed = 0;
            if (selected) {
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.20f, 0.42f, 0.80f, 0.22f));
                n_color_pushed++;
            }

            char child_id[16];
            snprintf(child_id, sizeof(child_id), "##slot%d", s);
            ImGuiChildFlags cf = selected
                ? (ImGuiChildFlags_Borders | ImGuiChildFlags_FrameStyle)
                : ImGuiChildFlags_Borders;

            if (ImGui::BeginChild(child_id, ImVec2(0.0f, cell_h), cf,
                                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
                // Slot header
                if (selected)
                    ImGui::TextColored(ImVec4(0.50f, 0.88f, 1.0f, 1.0f), "Slot %d \xe2\x80\xa2", s);
                else
                    ImGui::Text("Slot %d", s);

                if (sc.populated) {
                    if (sc.thumb && sc.thumb->is_good) {
                        float tw    = (float)sc.thumb->width;
                        float th    = (float)sc.thumb->height;
                        float scale = (tw / th > THUMB_MAX_W / THUMB_MAX_H)
                                        ? THUMB_MAX_W / tw
                                        : THUMB_MAX_H / th;
                        ImGui::Image(sc.thumb->for_image(), ImVec2(tw * scale, th * scale));
                    } else {
                        ImGui::Dummy(ImVec2(THUMB_MAX_W, THUMB_MAX_H));
                        ImGui::GetWindowDrawList()->AddRect(
                            ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                            IM_COL32(100, 100, 100, 120));
                    }
                    ImGui::TextDisabled("%s", sc.timestamp.c_str());
                } else {
                    ImVec2 ph_tl = ImGui::GetCursorScreenPos();
                    ImGui::Dummy(ImVec2(THUMB_MAX_W, THUMB_MAX_H));
                    ImDrawList *dl = ImGui::GetWindowDrawList();
                    dl->AddRect(ph_tl,
                        ImVec2(ph_tl.x + THUMB_MAX_W, ph_tl.y + THUMB_MAX_H),
                        IM_COL32(70, 70, 70, 100));
                    const char *empty_lbl = "empty";
                    ImVec2 ts = ImGui::CalcTextSize(empty_lbl);
                    dl->AddText(
                        ImVec2(ph_tl.x + (THUMB_MAX_W - ts.x) * 0.5f,
                               ph_tl.y + (THUMB_MAX_H - ts.y) * 0.5f),
                        IM_COL32(80, 80, 80, 180), empty_lbl);
                    ImGui::TextDisabled(" ");
                }
            }
            ImGui::EndChild();

            // Pop exactly what we pushed — no mismatch possible
            ImGui::PopStyleColor(n_color_pushed);

            // Click = select slot; double-click = load
            if (ImGui::IsItemClicked())
                settings.set_state_slot(ck, s);
            if (sc.populated && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                fsys->load_state_from_slot(s);
                settings.set_state_slot(ck, s);
            }

            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ImGui::Separator();

    // Action bar
    ImGui::Text("Slot: %d", cur_slot);
    ImGui::SameLine();
    if (ImGui::Button("Save##sm")) {
        fsys->save_state_to_slot(cur_slot);
        invalidate_save_manager_cache();
    }
    ImGui::SameLine();
    bool can_load = save_manager_slot_cache[cur_slot].populated;
    if (!can_load) ImGui::BeginDisabled();
    if (ImGui::Button("Load##sm"))
        fsys->load_state_from_slot(cur_slot);
    if (!can_load) ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("(double-click slot to load)");

    ImGui::End();
}

void imgui_jocasta_app::render_window_manager()
{
    if (!show_window_manager) return;
    ImGui::SetNextWindowSize(ImVec2(0, 0), ImGuiCond_Appearing); // auto-size on open
    if (ImGui::Begin("Debug Window Manager", &show_window_manager,
                     ImGuiWindowFlags_AlwaysAutoResize)) {
        if (windows.num == 0)
            ImGui::TextDisabled("No debug windows available");
        else
            render_managed_window_toggles(false);
    }
    ImGui::End();
}

full_system* imgui_jocasta_app::create_fsys()
{
    auto *f = new full_system();
#ifdef JSM_SDLR3
    f->platform_setup(renderer);
#endif
#ifdef JSM_SDLGPU
    f->platform_setup(device);
#endif
    return f;
}

void imgui_jocasta_app::load_path_for_system(jsm::systems system, const char *path, bool is_folder)
{
    // Own the path immediately. The caller may pass entry.path.c_str() from
    // settings.recent_entries (Recent menu). push_recent() erases and reinserts
    // that entry, destroying its std::string and leaving the raw pointer dangling
    // before set_initial_media_path() and strrchr() can use it below.
    const std::string path_owned(path);
    path = path_owned.c_str();

    if (!check_required_bios_or_warn(system))
        return;

    // ── Same-system fast path: swap media without tearing down the core ──────
    // If the exact same system is already running we just insert the new medium
    // and reset, which is much faster and preserves any unsaved SRAM state.
    //
    // Disc-based systems (PS1, Neo Geo CD, …) are excluded from this path:
    // reset() alone cannot cleanly unwind mid-execution CDROM, SPU, and IRQ
    // state left over from the running game.  Cart/cassette systems are safe
    // because their reset is CPU+RAM complete and SRAM is preserved.
    if (fsys && fsys->sys && fsys->sys->kind == system) {
        auto& IOs = fsys->sys->IOs;

        bool has_disc_drive = false;
        for (auto& pio : IOs)
            if (pio.kind == HID_DISC_DRIVE) { has_disc_drive = true; break; }

        // Disc systems skip the fast path entirely — fall through to full teardown.
        if (!has_disc_drive) {
            static const IO_CLASSES prio[] = {
                HID_AUDIO_CASSETTE, HID_CART_PORT
            };
            for (auto kind : prio) {
                for (u32 i = 0; i < (u32)IOs.size(); i++) {
                    if (IOs[i].kind != kind) continue;
                    if (fsys->insert_media(IOs[i], i, path, is_folder)) {
                        // Update ROMs/save paths and prime pending SRAM before
                        // reset + apply_core_options.
                        fsys->on_media_inserted(i, path, is_folder);

                        // Silence stale audio immediately; the new game will prime
                        // after its first frame like any fresh play.
                        fsys->audio.lock_all();
                        audio_needs_prime = (fsys->run_state == FSS_play);

                        fsys->sys->reset();
                        fsys->blank_output_until_next_frame();
                        fsys->reset_fps_meter();

                        // Re-apply options so the new cart's SRAM is opened from disk,
                        // controller state is correct, and directory prefs are current.
                        if (const char* ck = AppSettings::sys_to_core_key(system))
                            fsys->apply_core_options(settings, ck);

                        // Housekeeping (same as the full-teardown path below)
                        if (!is_folder) {
                            settings.push_recent(system, path);
                            fsys->set_initial_media_path(path);
                        }
                        const char* ls = strrchr(path, '/');
                        if (ls && ls > path) {
                            char dir[1024];
                            size_t dl = (size_t)(ls - path);
                            if (dl < sizeof(dir)) {
                                memcpy(dir, path, dl);
                                dir[dl] = '\0';
                                settings.set_last_dir(system, dir);
                            }
                        }

                        invalidate_save_manager_cache();
                    }
                    return; // Whether insert succeeded or not, we tried the primary slot
                }
            }
        }
        // No media found, or disc system: fall through to full teardown
    }

    // ── Full teardown + new system ────────────────────────────────────────────
    if (fsys) {
        fsys->run_state = FSS_pause;
        fsys->destroy_system();
        delete fsys;
    }
    windows.num = 0;
    which = system;
    fsys = create_fsys();

    // Pass configured BIOS dir to full_system (fall back to computed default)
    std::string bios_dir = settings.get_bios_dir(system);
    if (bios_dir.empty()) bios_dir = AppSettings::default_bios_dir(system);
    snprintf(fsys->bios_override_dir, sizeof(fsys->bios_override_dir), "%s", bios_dir.c_str());

    fsys->setup_system_from_path(system, path, is_folder, &settings);
    // Apply saved core options and controller-connected state
    if (const char* ck = AppSettings::sys_to_core_key(system))
        fsys->apply_core_options(settings, ck);
    fsys->load_layout_settings(settings);
    invalidate_save_manager_cache();
    fsys->has_played_once = false;
    last_frame_was_whole = false;

    // Save last used directory for this system
    const char* last_slash = strrchr(path, '/');
    if (last_slash && last_slash > path) {
        char dir[1024];
        size_t dir_len = (size_t)(last_slash - path);
        if (dir_len < sizeof(dir)) {
            memcpy(dir, path, dir_len);
            dir[dir_len] = 0;
            settings.set_last_dir(system, dir);
        }
    }
    // Add to recent files (only for file loads, not folder loads)
    if (!is_folder) {
        settings.push_recent(system, path);
        // Record the initial medium in the media window
        fsys->set_initial_media_path(path);
    }

    // ── On-boot action ────────────────────────────────────────────────────────
    switch (settings.get_on_boot_action()) {
        case AppSettings::OnBoot_Play:
            do_play();
            break;
        case AppSettings::OnBoot_LoadRecent: {
            const char* ck = AppSettings::sys_to_core_key(system);
            int slot = ck ? settings.get_state_slot(ck) : 0;
            fsys->load_state_from_slot(slot);
            do_play();
            break;
        }
        case AppSettings::OnBoot_Pause:
        default:
            // Stay paused — run_state is already FSS_pause
            break;
    }
}

void imgui_jocasta_app::boot_bios_for_system(jsm::systems system)
{
    if (!check_required_bios_or_warn(system))
        return;

    if (fsys) {
        fsys->run_state = FSS_pause;
        fsys->destroy_system();
        delete fsys;
    }
    windows.num = 0;
    which = system;
    fsys = create_fsys();

    std::string bios_dir = settings.get_bios_dir(system);
    if (bios_dir.empty()) bios_dir = AppSettings::default_bios_dir(system);
    snprintf(fsys->bios_override_dir, sizeof(fsys->bios_override_dir), "%s", bios_dir.c_str());

    fsys->setup_system_bios_only(system, &settings);
    if (const char* ck = AppSettings::sys_to_core_key(system))
        fsys->apply_core_options(settings, ck);
    fsys->load_layout_settings(settings);
    invalidate_save_manager_cache();
    fsys->has_played_once = false;
    last_frame_was_whole  = false;

}

void imgui_jocasta_app::parse_command_line(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        bool is_folder = false;
        const char *spec = nullptr;

        if (!strcmp(argv[i], "--load") && (i + 1 < argc)) {
            spec = argv[++i];
        }
        else if (!strcmp(argv[i], "--load-folder") && (i + 1 < argc)) {
            spec = argv[++i];
            is_folder = true;
        }
        else if (strchr(argv[i], ':')) {
            spec = argv[i];
        }

        if (spec) {
            jsm::systems system;
            char path[sizeof(startup_load_path)];
            if (parse_load_spec(spec, system, path, sizeof(path))) {
                startup_has_load = true;
                startup_load_folder = is_folder;
                startup_load_system = system;
                snprintf(startup_load_path, sizeof(startup_load_path), "%s", path);
            }
            else {
                printf("\nCould not parse load spec '%s'. Use --load system:/path/to/file", spec);
            }
        }
    }
}

void imgui_jocasta_app::do_setup_onstart()
{
    settings.load();
    fullscreen_mode = settings.get_fullscreen();
    show_save_manager = settings.get_show_save_manager();

    debugger_cols[0] = (char *)malloc(debugger_col_sizes[0]);
    debugger_cols[1] = (char *)malloc(debugger_col_sizes[1]);
    debugger_cols[2] = (char *)malloc(debugger_col_sizes[2]);
}


int imgui_jocasta_app::do_setup_before_mainloop()
{
    //which = jsm::systems::ATARI2600;
    //which = jsm::systems::GBC;
    //which = jsm::systems::APPLEIIe;
    //which = jsm::systems::DMG;
    //which = jsm::systems::PS1;
    //which = jsm::systems::SMS2;
    //which = jsm::systems::GG;
    //which = jsm::systems::ZX_SPECTRUM_48K;
    //which = jsm::systems::ZX_SPECTRUM_128K;
    //which = jsm::systems::SG1000;
    //which = jsm::systems::MAC512K;
    //which = jsm::systems::MACPLUS_1MB;
    //which = jsm::systems::DREAMCAST;
    //which = jsm::systems::GBA;
    //which = jsm::systems::SNES;
    //which = jsm::systems::COSMAC_VIP_2k;
    //which = jsm::systems::COSMAC_VIP_4k;
    //which = jsm::systems::GENESIS_USA;
    //which = jsm::systems::MEGADRIVE_PAL;
    //which = jsm::systems::NDS;
    //which = jsm::systems::TURBOGRAFX16;
    //which = jsm::systems::NES;
    //which = jsm::systems::COMMODORE64;
    //which = jsm::systems::GALAKSIJA;
    //which = jsm::systems::CASIO_PV1000;
    //which = jsm::systems::NEOGEO_AES;
    //dbg_enable_trace();

    if (startup_has_load) {
        which = startup_load_system;
        if (!check_required_bios_or_warn(which))
            return 0;
        fsys = create_fsys();
        fsys->setup_system_from_path(which, startup_load_path, startup_load_folder, &settings);
        if (const char* ck = AppSettings::sys_to_core_key(which))
            fsys->apply_core_options(settings, ck);
        fsys->load_layout_settings(settings);
        if (!fsys->worked) {
            printf("\nCould not load ROM! %d", fsys->worked);
            delete fsys;
            fsys = nullptr;
            return -1;
        }
        fsys->run_state = FSS_pause;
        fsys->has_played_once = false;
        fsys->setup_tracing();
    }
    return 0;
}

managed_window *imgui_jocasta_app::register_managed_window(u32 id, enum managed_window_kind kind, const char *name, u32 default_enabled)
{
    if (windows.num > 0) {
        for (u32 i = 0; i < windows.num; i++) {
            managed_window *mw = &windows.items[i];
            if (mw->id == id) {
                return mw;
            }
        }
    }
    managed_window *out = &windows.items[windows.num];
    windows.num++;
    out->id = id;
    out->kind = kind;
    snprintf(out->ini_key, sizeof(out->ini_key), "w_%08x", id);
    snprintf(out->name, sizeof(out->name), "%s", name);
    const char *core_key = AppSettings::sys_to_core_key(which);
    out->enabled = settings.get_debug_window_open(core_key, out->ini_key, default_enabled != 0) ? 1 : 0;
    return out;
}

void imgui_jocasta_app::set_managed_window_enabled(managed_window *mw, bool enabled)
{
    if (!mw) return;
    u32 new_enabled = enabled ? 1 : 0;
    if (mw->enabled == new_enabled) return;
    mw->enabled = new_enabled;

    const char *core_key = AppSettings::sys_to_core_key(which);
    settings.set_debug_window_open(core_key, mw->ini_key, enabled);
}

bool imgui_jocasta_app::begin_managed_window(managed_window *mw, const char *name, ImGuiWindowFlags flags)
{
    bool open = mw && mw->enabled;
    bool visible = ImGui::Begin(name, &open, flags);
    if (mw && (mw->enabled != (open ? 1u : 0u)))
        set_managed_window_enabled(mw, open);
    return visible;
}

void imgui_jocasta_app::do_play()
{
    if (!fsys) return;
    fsys->run_state = FSS_play;
    dbg_unbreak();
    // Lock all streams and mark that we need to prime after the first frame.
    // The audio callback outputs silence until prime_and_unlock() is called.
    fsys->audio.lock_all();
    audio_needs_prime = true;
}

void imgui_jocasta_app::do_pause()
{
    if (!fsys) return;
    fsys->run_state = FSS_pause;
    audio_needs_prime = false;
    // Fade out over ~5 callback periods (~83 ms at 60 fps) to avoid clicks.
    fsys->audio.begin_fadeout(5);
}

void imgui_jocasta_app::mainloop(ImGuiIO& io) {
    if (fsys) {
        framevars start_fv = fsys->get_framevars();

        fsys->enable_debugger = true;

        // Auto-enable debug tracing whenever any debug view is open
        {
            bool any_debug_open = false;
            for (u32 i = 0; i < windows.num; i++) {
                if (windows.items[i].enabled && windows.items[i].kind >= mwk_debug_console) {
                    any_debug_open = true;
                    break;
                }
            }
            if (any_debug_open && !dbg.do_debug)
                dbg_enable_trace();
        }

        if (dbg.do_break) {
            if (fsys->run_state == FSS_play)
                fsys->run_state = FSS_pause;
        }

        static u32 hotkeys[2];
        const char* core_key = AppSettings::sys_to_core_key(which);
        update_input(fsys, settings, core_key ? core_key : "", hotkeys, io, fullscreen_mode);

        // When a core routes keyboard input directly to the emulated machine,
        // suppress hotkeys that would otherwise fire on those same keys.
        // This covers keyboard-only cores (Apple IIe, C64, etc.) as well as
        // dual keyboard+controller cores that are not currently in JOYPAD mode.
        using IM = full_system::InputMode;
        const bool suppress_kbd_hotkeys = fsys->io.keyboard.vec &&
                                          (!fsys->needs_input_mode() ||
                                           fsys->input_mode != IM::JOYPAD);

        // Save-state slot hotkeys
        if (!suppress_kbd_hotkeys) {
            int slot = settings.get_state_slot(core_key);
            if (settings.get_hotkey("screenshot").was_pressed())
                fsys->screenshot = true;
            if (settings.get_hotkey("save_state").was_pressed()) {
                fsys->save_state_to_slot(slot);
                invalidate_save_manager_cache();
            }
            if (settings.get_hotkey("load_state").was_pressed())
                fsys->load_state_from_slot(slot);
            if (settings.get_hotkey("prev_slot").was_pressed())
                settings.set_state_slot(core_key, (slot + 9) % 10);
            if (settings.get_hotkey("next_slot").was_pressed())
                settings.set_state_slot(core_key, (slot + 1) % 10);
        }

        static u32  key_was_down = 0;
        static u64  wc_last_ns   = 0;   // wall-clock fallback state
        static double wc_accum   = 0.0;

        // Reset wall-clock accumulator whenever a new core is loaded
        // (detected by the fsys pointer change, handled at the top of
        //  this block — just zero the statics defensively here too)
        u32 ff_speed    = (u32)settings.effective_ff_speed(core_key);
        bool ff_held    = !suppress_kbd_hotkeys && settings.get_hotkey("fast_forward").is_held();
        u32 frame_multi = ff_held ? (ff_speed / 100u) : 1u;

        // Pause emulation while any menu is open (avoids missed inputs / desyncs)
        bool menu_open = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
        if (fsys->run_state == FSS_play && !menu_open) {
            bool ran_any_frame = false;
            if (ff_held) {
                // ── Fast-forward ──────────────────────────────────────────
                // Run frame_multi frames immediately; discard stale audio
                // buffers if we just left normal-speed playback.
                if (key_was_down == 0)
                    fsys->begin_fastforward();
                fsys->advance_time(0, 0, frame_multi);
                ran_any_frame = true;
            } else {
                // ── Wall-clock accumulator (all cores) ────────────────────
                // Measure real elapsed time; run however many emulator frames
                // are needed to stay current.  The audio ring fills passively
                // as frames run and is drained independently by the audio
                // callback — no feedback loop between audio and timing.
                u64 now_ns = SDL_GetTicksNS();
                if (wc_last_ns == 0) wc_last_ns = now_ns;
                double elapsed = (double)(now_ns - wc_last_ns) * 1e-9;
                wc_last_ns = now_ns;
                if (elapsed > 0.1) elapsed = 0.1; // clamp runaway catch-up
                double core_fps = (fsys->output.display && fsys->output.display->fps > 0.0)
                                  ? fsys->output.display->fps : 60.0;
                wc_accum += elapsed;
                double frame_time = 1.0 / core_fps;
                for (int n = 0; n < 4 && wc_accum >= frame_time; n++) {
                    fsys->advance_time(0, 0, 1);
                    wc_accum -= frame_time;
                    ran_any_frame = true;
                }
            }

            // After the first emulated frame, prepend the startup ramp and
            // unlock all audio streams so the callback can start consuming.
            if (ran_any_frame && audio_needs_prime) {
                static const float pad_fracs[] = { 0.0f, 0.25f, 0.5f, 1.0f };
                int pi = settings.get_audio_prime_padding();
                if (pi < 0) pi = 0;
                if (pi > 3) pi = 3;
                fsys->audio.prime_and_unlock(pad_fracs[pi]);
                audio_needs_prime = false;
            }

            // Returning to normal speed after fast-forward
            if (key_was_down && !ff_held)
                fsys->end_fastforward();

            last_frame_was_whole = true;
            fsys->has_played_once = true;
        } else {
            // Paused — keep wall-clock baseline fresh so we don't
            // accumulate a huge debt while paused
            wc_last_ns = SDL_GetTicksNS();
            wc_accum   = 0.0;
        }
        key_was_down = ff_held ? 1 : 0;

        // Play / Pause toggle
        if (!suppress_kbd_hotkeys && settings.get_hotkey("pause").was_pressed()) {
            if (fsys->run_state == FSS_play) do_pause();
            else                             do_play();
        }

        // Toggle fullscreen
        if (settings.get_hotkey("fullscreen").was_pressed()) {
            fullscreen_mode = !fullscreen_mode;
            settings.set_fullscreen(fullscreen_mode);
        }

        // Cycle input mode (keyboard / joypad / both) — only meaningful for
        // cores that expose both a keyboard device and controller inputs
        if (fsys->needs_input_mode() &&
            settings.get_hotkey("input_mode_toggle").was_pressed())
            fsys->cycle_input_mode();

        // Layout hotkeys (only meaningful when a second display is present)
        if (!suppress_kbd_hotkeys && fsys->has_second_display()) {
            if (settings.get_hotkey("layout_cycle").was_pressed()) {
                fsys->cycle_layout();
                fsys->save_layout_settings(settings);
            }
            if (settings.get_hotkey("layout_fav1").was_pressed()) {
                fsys->set_layout(fsys->fav_layout[0]);
                fsys->save_layout_settings(settings);
            }
            if (settings.get_hotkey("layout_fav2").was_pressed()) {
                fsys->set_layout(fsys->fav_layout[1]);
                fsys->save_layout_settings(settings);
            }
        }

        fsys->update_fps_meter(ImGui::GetTime());
        fsys->present();

        if (fullscreen_mode) {
            render_emu_fullscreen(fsys, io, nearest_sampler);
            render_settings_window();
            render_main_menu_bar();
            return;
        }

        auto [w1_pos, w1_size] = render_emu_window(fsys, io, nearest_sampler, settings, core_key, !fsys->has_second_display());
        if (!fsys->uses_composite_layout())
            render_emu_window2(fsys, io, nearest_sampler, settings, w1_pos, w1_size);

        {
            ImVec2 vp = ImGui::GetMainViewport()->Size;
            ImGui::SetNextWindowPos(ImVec2(vp.x - 270.0f, 30.0f), ImGuiCond_FirstUseEver);
        }
        if (ImGui::Begin("Play", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            static int steps[4] = { 100, 1, 1, 1 };
            bool play_pause = false;
            bool step_clocks = false, step_scanlines = false, step_frames = false;
            bool trace_enabled = dbg.do_debug;
            ImGuiWindowFlags window_flags = ImGuiWindowFlags_None;

            // ── Input mode (only for cores with keyboard + controller I/O) ──
            if (fsys->needs_input_mode()) {
                using IM = full_system::InputMode;
                ImGui::Separator();
                ImGui::AlignTextToFramePadding();
                ImGui::Text("Input mode:");
                ImGui::SameLine();

                const struct { IM mode; const char* label; const char* tip; } modes[] = {
                    { IM::KEYBOARD, "Keyboard", "Keystrokes go to the emulated keyboard.\nController keyboard bindings are inactive." },
                    { IM::JOYPAD,   "Joypad",   "Keyboard bindings drive the controller.\nKeyboard passthrough is paused." },
                    { IM::BOTH,     "Both",     "Keystrokes go to the emulated keyboard\nAND drive controller bindings simultaneously.\nBest when joypad is on a physical gamepad." },
                };
                for (auto& m : modes) {
                    bool sel = (fsys->input_mode == m.mode);
                    if (sel) ImGui::PushStyleColor(ImGuiCol_Button,
                                 ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                    if (ImGui::Button(m.label)) fsys->input_mode = m.mode;
                    if (sel) ImGui::PopStyleColor();
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", m.tip);
                    ImGui::SameLine();
                }
                ImGui::NewLine();
                ImGui::Separator();
            }

            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 5.0f);
            if (ImGui::BeginChild("Playback Controls", ImVec2(200, 150), ImGuiChildFlags_None, window_flags)) {
                play_pause = ImGui::Button("Play/pause");
                ImGui::SameLine();
                if (ImGui::Button("Screenshot"))
                    fsys->screenshot = true;

                ImGui::PushID(1);
                step_clocks = ImGui::Button("Step");
                ImGui::PopID();
                ImGui::SameLine();
                ImGui::InputInt("cycles", &steps[0]);

                ImGui::PushID(2);
                step_scanlines = ImGui::Button("Step");
                ImGui::PopID();
                ImGui::SameLine();
                ImGui::InputInt("lines", &steps[1]);

                ImGui::PushID(3);
                step_frames = ImGui::Button("Step");
                ImGui::PopID();
                ImGui::SameLine();
                ImGui::InputInt("frames", &steps[2]);

                ImGui::PushID(4);
                ImGui::PopID();

                ImGui::PushID(5);
                ImGui::Checkbox("Debugging enabled (slower)", &trace_enabled);
                ImGui::PopID();
                if (dbg.do_debug != trace_enabled) {
                    if (trace_enabled) dbg_enable_trace();
                    else dbg_disable_trace();
                }
                fsys->signal = ImGui::Button("Signal");
            }
            ImGui::EndChild();
            ImGui::PopStyleVar();

            if (fsys->supports_runtime_sideload()) {
                if (ImGui::Button("Sideload File...")) {
                    char sp[500];
                    default_load_path(sp, sizeof(sp), which, settings);
                    const char* p = tinyfd_openFileDialog("Sideload File", sp, 1, FILT_C64_SIDELOAD, nullptr, 0);
                    if (p && fsys->sideload_file(p)) {
                        const char* last_slash = strrchr(p, '/');
                        if (last_slash && last_slash > p) {
                            char dir[1024];
                            size_t dir_len = (size_t)(last_slash - p);
                            if (dir_len < sizeof(dir)) {
                                memcpy(dir, p, dir_len);
                                dir[dir_len] = 0;
                                settings.set_last_dir(which, dir);
                            }
                        }
                    }
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Load a file into the running core without resetting it");
            }

            if (play_pause) {
                if (fsys->run_state == FSS_pause) do_play();
                else                              do_pause();
            }
            bool stepped = false;
            if (step_scanlines) { dbg_unbreak(); fsys->advance_time(0, steps[1], 0); last_frame_was_whole = false; stepped = true; }
            if (step_clocks)    { dbg_unbreak(); fsys->advance_time(steps[0], 0, 0); last_frame_was_whole = false; stepped = true; }
            if (step_frames)    { dbg_unbreak(); fsys->advance_time(0, 0, steps[2]); last_frame_was_whole = true; stepped = true; }
            if (stepped) fsys->present();

            // ── Chassis controls (pause buttons, switches, etc.) ─────────────
            if (fsys->io.chassis.vec) {
                auto& pio = fsys->io.chassis.get();
                bool any_shown = false;
                for (auto& db : pio.chassis.digital_buttons) {
                    if (db.common_id == DBCID_ch_power || db.common_id == DBCID_ch_reset)
                        continue;
                    any_shown = true;
                }
                if (any_shown) {
                    ImGui::Separator();
                    bool first = true;
                    for (auto& db : pio.chassis.digital_buttons) {
                        if (db.common_id == DBCID_ch_power || db.common_id == DBCID_ch_reset)
                            continue;
                        if (!first) ImGui::SameLine();
                        first = false;
                        ImGui::PushID((int)db.common_id);
                        if (db.kind == DBK_SWITCH) {
                            // Toggle button — highlighted when active
                            if (db.state)
                                ImGui::PushStyleColor(ImGuiCol_Button,
                                    ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                            if (ImGui::Button(db.name))
                                fsys->chassis_gui_pulse[(int)db.common_id] = true;
                            if (db.state) ImGui::PopStyleColor();
                            if (ImGui::IsItemHovered())
                                ImGui::SetTooltip("%s: %s (click or hotkey to toggle)",
                                    db.name, db.state ? "ON" : "OFF");
                        } else {
                            // Momentary button — pulse for one frame
                            if (ImGui::Button(db.name))
                                fsys->chassis_gui_pulse[(int)db.common_id] = true;
                            if (ImGui::IsItemHovered())
                                ImGui::SetTooltip("%s: click or hold hotkey", db.name);
                        }
                        ImGui::PopID();
                    }
                }
            }

            framevars fv = fsys->get_framevars();
            ImGui::SameLine();
            ImGui::Text("Frame: %lld", fv.master_frame);
            ImGui::Text("X,Y:%d,%d", fv.x, fv.scanline);

            ImGui::Separator();
            if (ImGui::SmallButton("Save Manager")) {
                show_save_manager = !show_save_manager;
                settings.set_show_save_manager(show_save_manager);
            }
        }
        ImGui::End();

        framevars end_fv = fsys->get_framevars();
        bool update_dasm_scroll = (start_fv.master_cycle != end_fv.master_cycle);

        if (show_core_options)
            render_opt_view(fsys, settings, AppSettings::sys_to_core_key(which), &show_core_options);
        if (show_media_window)
            render_media_window();
        render_debug_views(io, update_dasm_scroll, end_fv.master_cycle);
    }

    render_save_manager();
    render_settings_window();
    render_main_menu_bar();
    render_bios_warning();

    // ── Save-state version-mismatch warning ───────────────────────────────────
    if (fsys && fsys->load_version_warned) {
        ImGui::OpenPopup("##ver_warn");
        fsys->load_version_warned = false; // consume — modal will show once
    }
    if (ImGui::BeginPopupModal("##ver_warn", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Save State Version Warning");
        ImGui::Separator();
        ImGui::Spacing();

        char cur_ver[32];
        snprintf(cur_ver, sizeof(cur_ver), "%d.%d",
                 JSST_VERSION_MAJOR, JSST_VERSION_MINOR);

        if (fsys) {
            ImGui::TextWrapped(
                "This save state was created with version %s of the save format.\n"
                "The current version is %s.\n\n"
                "The save was loaded, but it may be incompatible and could cause "
                "graphical glitches, crashes, or incorrect behaviour.",
                fsys->load_version_str, cur_ver);
        }

        ImGui::Spacing();
        ImGui::SetNextItemWidth(-1);
        if (ImGui::Button("OK", ImVec2(120, 0)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // ── Save-state BIOS mismatch warning ─────────────────────────────────────
    if (fsys && fsys->load_bios_warned) {
        ImGui::OpenPopup("##bios_state_warn");
        fsys->load_bios_warned = false;
    }
    if (ImGui::BeginPopupModal("##bios_state_warn", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Save State BIOS Warning");
        ImGui::Separator();
        ImGui::Spacing();
        if (fsys)
            ImGui::TextWrapped("%s", fsys->load_bios_warning_str);
        ImGui::Spacing();
        if (ImGui::Button("OK", ImVec2(120, 0)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void imgui_jocasta_app::at_end()
{
    if (fsys) {
        fsys->destroy_system();
        delete fsys;
        fsys = nullptr;
    }
}

#ifdef JSM_OPENGL
int opengl_main(imgui_jocasta_app &app)
{
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
    {
        printf("Error: SDL_Init(): %s\n", SDL_GetError());
        return -1;
    }


#if defined(IMGUI_IMPL_OPENGL_ES2)
    // GL ES 2.0 + GLSL 100
    const char* glsl_version = "#version 100";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#elif defined(__APPLE__)
    // GL 3.2 Core + GLSL 150
    const char* glsl_version = "#version 150";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG); // Always required on Mac
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#else
    // GL 3.0 + GLSL 130
    const char* glsl_version = "#version 130";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif

    // Create window with graphics context
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    Uint32 window_flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN;
    SDL_Window* window = SDL_CreateWindow("Jocasta", 1280, 720, window_flags);
    if (window == nullptr)
    {
        printf("Error: SDL_CreateWindow(): %s\n", SDL_GetError());
        return -1;
    }
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, gl_context);
    SDL_GL_SetSwapInterval(-1); // Enable adaptive vsync
    SDL_ShowWindow(window);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;

    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular ones.
    ImGuiStyle& style = ImGui::GetStyle();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    //ImGui::StyleColorsLight();

    // Setup Platform/Renderer backends
    ImGui_ImplSDL3_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // Load Fonts
    // - If no fonts are loaded, dear imgui will use the default font. You can also load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
    // - AddFontFromFileTTF() will return the ImFont* so you can store it if you need to select the font among multiple.
    // - If the file cannot be loaded, the function will return a nullptr. Please handle those errors in your application (e.g. use an assertion, or display an error and quit).
    // - The fonts will be rasterized at a given size (w/ oversampling) and stored into a texture when calling ImFontAtlas::Build()/GetTexDataAsXXXX(), which ImGui_ImplXXXX_NewFrame below will call.
    // - Use '#define IMGUI_ENABLE_FREETYPE' in your imconfig file to use Freetype for higher quality font rendering.
    // - Read 'docs/FONTS.md' for more instructions and details.
    // - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
    // - Emscripten allows preloading a file or folder to be accessible at runtime. See Makefile for details.
    //io.Fonts->AddFontDefault();
#ifndef IMGUI_DISABLE_FILE_FUNCTIONS
    //io.Fonts->AddFontFromFileTTF("fonts/segoeui.ttf", 18.0f);
    //io.Fonts->AddFontFromFileTTF("fonts/DroidSans.ttf", 16.0f);
    //io.Fonts->AddFontFromFileTTF("fonts/Roboto-Medium.ttf", 16.0f);
    //io.Fonts->AddFontFromFileTTF("fonts/Cousine-Regular.ttf", 15.0f);
    //io.Fonts->AddFontFromFileTTF("fonts/ProggyTiny.ttf", 10.0f);
    //ImFont* font = io.Fonts->AddFontFromFileTTF("fonts/ArialUni.ttf", 18.0f, nullptr, io.Fonts->GetGlyphRangesJapanese());
    //IM_ASSERT(font != nullptr);
#endif

#ifdef __EMSCRIPTEN__
    // For an Emscripten build we are disabling file-system access, so let's not attempt to do a fopen() of the imgui.ini file.
    // You may manually call LoadIniSettingsFromMemory() to load settings from your own storage.
    io.IniFilename = nullptr;
    EMSCRIPTEN_MAINLOOP_BEGIN
#else
    int a = app.do_setup_before_mainloop();
    if (a) {
        return a;
    }
    while (!app.done)
#endif
    {
        // Poll and handle events (inputs, window resize, etc.)
        // You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
        // - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
        // - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
        // Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
        //io.WantCaptureKeyboard = false;
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT)
                app.done = true;
            if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window))
                app.done = true;
        }
        if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED)
        {
            SDL_Delay(10);
            continue;
        }

        // React to changes in screen size
        int width, height;
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        app.mainloop(io);

        // Rendering
        ImGui::Render();
        ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
        glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
        glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        // Update and Render additional Platform Windows
        // (Platform functions may change the current OpenGL context, so we save/restore it to make it easier to paste this code elsewhere.
        //  For this specific demo app we could also call SDL_GL_MakeCurrent(window, gl_context) directly)
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            SDL_Window* backup_current_window = SDL_GL_GetCurrentWindow();
            SDL_GLContext backup_current_context = SDL_GL_GetCurrentContext();
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            SDL_GL_MakeCurrent(backup_current_window, backup_current_context);
        }

        SDL_GL_SwapWindow(window);
    }
#ifdef __EMSCRIPTEN__
    EMSCRIPTEN_MAINLOOP_END;
#endif

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DestroyContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();

    app.at_end();
    return 0;
}
#endif

#ifdef JSM_SDLGPU
static int sdlgpu_main(imgui_jocasta_app &app) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
    {
        printf("Error: SDL_Init(): %s\n", SDL_GetError());
        return -1;
    }
    // Create SDL window graphics context
    float main_scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    SDL_WindowFlags window_flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    SDL_Window* window = SDL_CreateWindow("Jocasta Emulators", (int)(1280 * main_scale), (int)(800 * main_scale), window_flags);
    if (window == nullptr)
    {
        printf("Error: SDL_CreateWindow(): %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_ShowWindow(window);

    // Create GPU Device
    SDL_GPUDevice* gpu_device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_METALLIB, true, nullptr);
    if (gpu_device == nullptr)
    {
        printf("Error: SDL_CreateGPUDevice(): %s\n", SDL_GetError());
        return 1;
    }
    // Claim window for GPU Device
    if (!SDL_ClaimWindowForGPUDevice(gpu_device, window))
    {
        printf("Error: SDL_ClaimWindowForGPUDevice(): %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetGPUSwapchainParameters(gpu_device, window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, SDL_GPU_PRESENTMODE_VSYNC);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsLight();

    // Setup scaling
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(main_scale);        // Bake a fixed style scale. (until we have a solution for dynamic style scaling, changing this requires resetting Style + calling this again)
    style.FontScaleDpi = main_scale;        // Set initial font scale. (using io.ConfigDpiScaleFonts=true makes this unnecessary. We leave both here for documentation purpose)

    // Setup Platform/Renderer backends
    ImGui_ImplSDL3_InitForSDLGPU(window);
    ImGui_ImplSDLGPU3_InitInfo init_info = {};
    app.platform_setup(gpu_device);
    init_info.Device = gpu_device;
    init_info.ColorTargetFormat = SDL_GetGPUSwapchainTextureFormat(gpu_device, window);
    init_info.MSAASamples = SDL_GPU_SAMPLECOUNT_1;                      // Only used in multi-viewports mode.
    init_info.SwapchainComposition = SDL_GPU_SWAPCHAINCOMPOSITION_SDR;  // Only used in multi-viewports mode.
    init_info.PresentMode = SDL_GPU_PRESENTMODE_VSYNC;
    ImGui_ImplSDLGPU3_Init(&init_info);
        // Load Fonts
    // - If no fonts are loaded, dear imgui will use the default font. You can also load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
    // - AddFontFromFileTTF() will return the ImFont* so you can store it if you need to select the font among multiple.
    // - If the file cannot be loaded, the function will return a nullptr. Please handle those errors in your application (e.g. use an assertion, or display an error and quit).
    // - Use '#define IMGUI_ENABLE_FREETYPE' in your imconfig file to use Freetype for higher quality font rendering.
    // - Read 'docs/FONTS.md' for more instructions and details. If you like the default font but want it to scale better, consider using the 'ProggyVector' from the same author!
    // - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
    //style.FontSizeBase = 20.0f;
    //io.Fonts->AddFontDefault();
    //io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\segoeui.ttf");
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf");
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf");
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf");
    //ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf");
    //IM_ASSERT(font != nullptr);

    // Our state
    int a = app.do_setup_before_mainloop();
    if (a) return a;

    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    // Main loop
    bool quit = false;
    while (!quit && !app.done)
    {
        // Poll and handle events (inputs, window resize, etc.)
        // You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
        // - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
        // - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
        // Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
        // [If using SDL_MAIN_USE_CALLBACKS: call ImGui_ImplSDL3_ProcessEvent() from your SDL_AppEvent() function]
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT)
                quit = true;
            if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window))
                quit = true;
        }

        // [If using SDL_MAIN_USE_CALLBACKS: all code below would likely be your SDL_AppIterate() function]

        // Start the Dear ImGui frame
        ImGui_ImplSDLGPU3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        // 1. Show the big demo window (Most of the sample code is in ImGui::ShowDemoWindow()! You can browse its code to learn more about Dear ImGui!).
        app.mainloop(io);
        // Rendering
        ImGui::Render();
        ImDrawData* draw_data = ImGui::GetDrawData();
        const bool is_minimized = (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f);

        SDL_GPUCommandBuffer* command_buffer = SDL_AcquireGPUCommandBuffer(gpu_device); // Acquire a GPU command buffer

        SDL_GPUTexture* swapchain_texture;
        SDL_WaitAndAcquireGPUSwapchainTexture(command_buffer, window, &swapchain_texture, nullptr, nullptr); // Acquire a swapchain texture

        if (swapchain_texture != nullptr && !is_minimized)
        {
            // This is mandatory: call ImGui_ImplSDLGPU3_PrepareDrawData() to upload the vertex/index buffer!
            ImGui_ImplSDLGPU3_PrepareDrawData(draw_data, command_buffer);

            // Setup and start a render pass
            SDL_GPUColorTargetInfo target_info = {};
            target_info.texture = swapchain_texture;
            target_info.clear_color = SDL_FColor { clear_color.x, clear_color.y, clear_color.z, clear_color.w };
            target_info.load_op = SDL_GPU_LOADOP_CLEAR;
            target_info.store_op = SDL_GPU_STOREOP_STORE;
            target_info.mip_level = 0;
            target_info.layer_or_depth_plane = 0;
            target_info.cycle = false;
            SDL_GPURenderPass* render_pass = SDL_BeginGPURenderPass(command_buffer, &target_info, 1, nullptr);

            // Render ImGui
            ImGui_ImplSDLGPU3_RenderDrawData(draw_data, command_buffer, render_pass);

            SDL_EndGPURenderPass(render_pass);
        }

        // Submit the command buffer
        SDL_SubmitGPUCommandBuffer(command_buffer);
    }

    // Cleanup
    // [If using SDL_MAIN_USE_CALLBACKS: all code below would likely be your SDL_AppQuit() function]
    SDL_WaitForGPUIdle(gpu_device);
    ImGui_ImplSDL3_Shutdown();
    ImGui_ImplSDLGPU3_Shutdown();
    ImGui::DestroyContext();

    SDL_ReleaseWindowFromGPUDevice(gpu_device, window);
    SDL_DestroyGPUDevice(gpu_device);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;


}
#endif

#ifdef JSM_SDLR3
static int sldr3_main(imgui_jocasta_app &app) {
    // Setup SDL
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
    {
        printf("Error: SDL_Init(): %s\n", SDL_GetError());
        return -1;
    }
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "vulkan");
    SDL_SetHint(SDL_HINT_RENDER_VSYNC, 0);

    // Create window with SDL_Renderer graphics context
    Uint32 window_flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN;
    SDL_Window* window = SDL_CreateWindow("Jocasta GUI", 1280, 720, window_flags);
    if (window == nullptr)
    {
        printf("Error: SDL_CreateWindow(): %s\n", SDL_GetError());
        return -1;
    }
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    app.platform_setup(renderer);
    SDL_SetRenderVSync(renderer, 1);
    if (renderer == nullptr)
    {
        SDL_Log("Error: SDL_CreateRenderer(): %s\n", SDL_GetError());
        return -1;
    }
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_ShowWindow(window);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;         // Enable Docking

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsLight();

    // Setup Platform/Renderer backends
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    // Load Fonts
    // - If no fonts are loaded, dear imgui will use the default font. You can also load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
    // - AddFontFromFileTTF() will return the ImFont* so you can store it if you need to select the font among multiple.
    // - If the file cannot be loaded, the function will return a nullptr. Please handle those errors in your application (e.g. use an assertion, or display an error and quit).
    // - The fonts will be rasterized at a given size (w/ oversampling) and stored into a texture when calling ImFontAtlas::Build()/GetTexDataAsXXXX(), which ImGui_ImplXXXX_NewFrame below will call.
    // - Use '#define IMGUI_ENABLE_FREETYPE' in your imconfig file to use Freetype for higher quality font rendering.
    // - Read 'docs/FONTS.md' for more instructions and details.
    // - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
    // - Our Emscripten build process allows embedding fonts to be accessible at runtime from the "fonts/" folder. See Makefile.emscripten for details.
    //io.Fonts->AddFontDefault();
    //io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\segoeui.ttf", 18.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf", 16.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf", 16.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf", 15.0f);
    //ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf", 18.0f, nullptr, io.Fonts->GetGlyphRangesJapanese());
    //IM_ASSERT(font != nullptr);

    int a = app.do_setup_before_mainloop();
    if (a) {
        return a;
    }
    while (!app.done)
#ifdef __EMSCRIPTEN__
        // For an Emscripten build we are disabling file-system access, so let's not attempt to do a fopen() of the imgui.ini file.
    // You may manually call LoadIniSettingsFromMemory() to load settings from your own storage.
    io.IniFilename = nullptr;
    EMSCRIPTEN_MAINLOOP_BEGIN
#else
        while (!app.done)
#endif
        {
        // Poll and handle events (inputs, window resize, etc.)
        // You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
        // - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
        // - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
        // Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
        //io.WantCaptureKeyboard = false;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT)
                app.done = true;
            if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window))
                app.done = true;
        }
        if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) {
            SDL_Delay(10);
            continue;
        }

        // React to changes in screen size
        int width, height;
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        app.mainloop(io);

        // Rendering
        ImGui::Render();
        ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
            SDL_SetRenderDrawColorFloat(renderer, clear_color.x, clear_color.y, clear_color.z, clear_color.w);
            SDL_RenderClear(renderer);
            ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
            SDL_RenderPresent(renderer);
        }
#ifdef __EMSCRIPTEN__
    EMSCRIPTEN_MAINLOOP_END;
#endif
    // Cleanup
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    app.at_end();
    return 0;

}
#endif


// Main code
int main(int argc, char** argv)
{
    imgui_jocasta_app app;
    app.do_setup_onstart();
    app.parse_command_line(argc, argv);

#ifdef JSM_OPENGL
    return opengl_main(app);
#endif

#ifdef JSM_METAL
    return metal_main(app);
#endif

#ifdef JSM_SDLR3
    return sldr3_main(app);
#endif

#ifdef JSM_SDLGPU
    return sdlgpu_main(app);
#endif
}
