#pragma once

#include <array>
#include <memory>
#include <string>

#include "full_sys.h"
#include "helpers/int.h"
#include "app_settings.h"

enum managed_window_kind {
    mwk_main,
    mwk_playback,
    mwk_opts,
    mwk_other,
    mwk_debug_console,
    mwk_debug_log,
    mwk_debug_trace,
    mwk_debug_sound,
    mwk_debug_image,
    mwk_debug_disassembly,
    mwk_debug_events,
    mwk_debug_memory,
    mwk_debug_dbglog,
    mwk_debug_source_list
};

struct managed_window {
    u32 enabled{};
    u32 id{};
    enum managed_window_kind kind{};
    char ini_key[32]{};
    char name[200]{};
};

#define MAX_MANAGED_WINDOWS 100
struct managed_windows {
    struct managed_window items[MAX_MANAGED_WINDOWS]{};
    u32 num=0;
};

struct RenderResources;
struct imgui_jocasta_app {
    bool window_steal_input{false};
    void render_source_list_view(bool update_dasm_scroll);
    managed_windows windows{};
    managed_window *register_managed_window(u32 id, enum managed_window_kind kind, const char *name, u32 default_enabled);
    void set_managed_window_enabled(managed_window *mw, bool enabled);
    bool begin_managed_window(managed_window *mw, const char *name, ImGuiWindowFlags flags = 0);
    void do_setup_onstart();
    void parse_command_line(int argc, char **argv);
    int do_setup_before_mainloop();
    void mainloop(ImGuiIO& io);
    void at_end();
    void load_path_for_system(jsm::systems system, const char *path, bool is_folder);
    void boot_bios_for_system(jsm::systems system);
    bool check_required_bios_or_warn(jsm::systems system);
    void render_bios_warning();
    void invalidate_save_manager_cache();
    void do_play();
    void do_pause();
    void render_debug_views(ImGuiIO& io, bool update_dasm_scroll, u64 cur_time);
    void render_memory_view();
    void render_event_view();
    void render_image_views(ImGuiIO& io);
    void render_dbglog_views(bool update_dasm_scroll, u64 cur_time);
    void render_trace_view(bool update_dasm_scroll);
    void render_console_view(bool update_dasm_scroll);

    void render_waveform_view(struct WVIEW &wview, u32 num);
    void render_waveform2_view(struct W2VIEW &wview, u32 num);
    void render_disassembly_views(bool update_dasm_scroll);
    void render_disassembly_view(struct DVIEW &dview, bool update_dasm_scroll, u32 num);
    void render_dbglog_view(struct DLVIEW &dview, bool update_dasm_scoll, u64 cur_time, managed_window *mw);
    void render_main_menu_bar();
    void render_managed_window_toggles(bool as_menu);
    void render_window_manager();
    void render_w2_tex(debug::waveform2::view_node &node);
    void render_w2_node(debug::waveform2::view_node &node, u32 &idpush);
    void render_w2_node_line(debug::waveform2::view_node &node, u32 &idpush);

#ifdef JSM_SDLGPU
    SDL_GPUDevice *device{};
    SDL_GPUSampler *nearest_sampler{};
    void platform_setup(SDL_GPUDevice *in_device);
#endif
#ifdef JSM_SDLR3
    SDL_Renderer *renderer{};
    void platform_setup(SDL_Renderer *mrenderer) { renderer = mrenderer; if (fsys) fsys->platform_setup(renderer); }
#endif

    full_system* create_fsys();

    int done_break = 0;
    bool last_frame_was_whole = false;
    bool playing = true;
    bool done = false;
    bool startup_has_load = false;
    bool startup_load_folder = false;
    enum jsm::systems startup_load_system{};
    char startup_load_path[1024]{};
    full_system* fsys = nullptr;
    enum jsm::systems which;
    AppSettings settings;
    bool show_settings_window = false;
    bool show_save_manager = false;
    bool show_window_manager = false;
    bool fullscreen_mode = false;
    bool show_media_window = true;
    bool show_core_options = true;
    int settings_nav = 0; // 0=General, 1=BIOS, 2=Hotkeys, 3=Input, 4=Options, 5=In Development
    bool pending_dev_features_enable = false; // waiting for user to confirm warning modal
    bool audio_needs_prime = false;           // prime_and_unlock() after first advance_time

    bool show_bios_warning = false;
    std::string bios_warning_message{};

    struct save_mgr_slot_cache {
        bool populated = false;
        bool attempted = false;
        std::string timestamp;
        std::unique_ptr<my_texture> thumb;
    };
    std::array<save_mgr_slot_cache, 10> save_manager_slot_cache{};
    full_system *save_manager_cache_owner = nullptr;
    bool save_manager_was_showing = false;

    void render_settings_window();
    void render_settings_page_general();
    void render_settings_page_bios();
    void render_settings_page_hotkeys();
    void render_settings_page_input();
    void render_settings_page_options();
    void render_settings_page_dev();
    void render_save_manager();
    void render_media_window();

    char *debugger_cols[3];
    constexpr static size_t debugger_col_sizes[3] = { 10 * 1024, 100 * 1024, 500 * 1024 };

};
