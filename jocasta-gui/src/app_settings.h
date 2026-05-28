#pragma once

#include <string>
#include <vector>
#include "helpers/enums.h"
#include "helpers/int.h"
#include "helpers/physical_io.h"
#include "mini/ini.h"
#include "keybinding.h"

struct RecentEntry {
    std::string cli_name;
    std::string path;
    jsm::systems system;
};

class AppSettings {
public:
    void load();
    void save();
    void ensure_defaults();

    std::string get_last_dir(jsm::systems sys) const;
    void set_last_dir(jsm::systems sys, const char* dir);

    std::string get_bios_dir(jsm::systems sys) const;
    void set_bios_dir(jsm::systems sys, const char* dir);

    std::string get_slot_rom_path(jsm::systems sys, int slot) const;
    void        set_slot_rom_path(jsm::systems sys, int slot, const char* path);

    const std::vector<RecentEntry>& get_recent() const { return recent_entries; }
    void push_recent(jsm::systems sys, const char* path);
    void clear_recent();

    // Save state slot (0-9, per core)
    int  get_state_slot(const char* core_key) const;
    void set_state_slot(const char* core_key, int slot);

    // Global saves folder (empty = default ~/Documents/emu/saves)
    std::string get_saves_dir() const;
    void        set_saves_dir(const char* dir);

    // Global defaults for save-location / SRAM behaviour
    bool get_states_in_saves_dir_global() const;
    void set_states_in_saves_dir_global(bool v);
    bool get_sram_in_saves_dir_global() const;
    void set_sram_in_saves_dir_global(bool v);
    bool get_save_sram_with_state_global() const;
    void set_save_sram_with_state_global(bool v);

    // Per-core overrides (only stored when user explicitly deviates from global)
    bool has_states_in_saves_dir_override(const char* core_key) const;
    bool get_states_in_saves_dir(const char* core_key) const;   // override value only
    void set_states_in_saves_dir(const char* core_key, bool v); // sets override
    void clear_states_in_saves_dir(const char* core_key);       // removes override

    bool has_sram_in_saves_dir_override(const char* core_key) const;
    bool get_sram_in_saves_dir(const char* core_key) const;
    void set_sram_in_saves_dir(const char* core_key, bool v);
    void clear_sram_in_saves_dir(const char* core_key);

    bool has_save_sram_with_state_override(const char* core_key) const;
    bool get_save_sram_with_state(const char* core_key) const;
    void set_save_sram_with_state(const char* core_key, bool v);
    void clear_save_sram_with_state(const char* core_key);

    // Effective values — per-core override if set, else global default
    bool effective_states_in_saves_dir(const char* core_key) const;
    bool effective_sram_in_saves_dir(const char* core_key) const;
    bool effective_save_sram_with_state(const char* core_key) const;

    // On-boot action (what happens automatically after a game is loaded)
    // 0 = Play Game (default), 1 = Load Most Recent Save, 2 = Pause
    enum OnBootAction { OnBoot_Play = 0, OnBoot_LoadRecent = 1, OnBoot_Pause = 2 };
    OnBootAction get_on_boot_action() const;
    void         set_on_boot_action(OnBootAction a);

    // Fast-forward speed (expressed as %, 200 = 2x, 800 = 8x, etc.)
    int  get_ff_speed_global() const;
    void set_ff_speed_global(int pct);
    int  get_ff_speed_core(const char* core_key) const; // 0 = use global
    void set_ff_speed_core(const char* core_key, int pct);
    void clear_ff_speed_core(const char* core_key);
    int  effective_ff_speed(const char* core_key) const;

    // Audio output sample rate (0 = not yet chosen / auto-detect)
    u32  get_audio_output_rate() const;
    void set_audio_output_rate(u32 rate);

    // Audio startup ramp cushion (0=none, 1=¼ frame, 2=½ frame, 3=1 frame)
    int  get_audio_prime_padding() const;
    void set_audio_prime_padding(int v);

    // In-development features (single global gate, default off)
    bool get_dev_features_enabled() const;
    void set_dev_features_enabled(bool v);

    // Virtual input overlay visibility — global defaults + per-core overrides
    // Global defaults (true = show by default)
    bool get_show_virtual_controller_global() const;
    void set_show_virtual_controller_global(bool v);
    bool get_show_virtual_keyboard_global() const;
    void set_show_virtual_keyboard_global(bool v);
    // Per-core overrides
    bool has_show_virtual_controller_override(const char* core_key) const;
    bool get_show_virtual_controller(const char* core_key) const;  // override value only
    void set_show_virtual_controller(const char* core_key, bool v);
    void clear_show_virtual_controller(const char* core_key);
    bool has_show_virtual_keyboard_override(const char* core_key) const;
    bool get_show_virtual_keyboard(const char* core_key) const;    // override value only
    void set_show_virtual_keyboard(const char* core_key, bool v);
    void clear_show_virtual_keyboard(const char* core_key);
    // Effective values (per-core override if set, else global)
    bool effective_show_virtual_controller(const char* core_key) const;
    bool effective_show_virtual_keyboard(const char* core_key) const;

    // Global UI state
    bool get_fullscreen() const;
    void set_fullscreen(bool enabled);
    bool get_show_save_manager() const;
    void set_show_save_manager(bool v);
    bool get_debug_window_open(const char* core_key, const char* window_key, bool default_val) const;
    void set_debug_window_open(const char* core_key, const char* window_key, bool open);
    float get_play_window_zoom(jsm::systems sys) const;
    void set_play_window_zoom(jsm::systems sys, float zoom);
    u32 get_display_layout(jsm::systems sys) const;
    void set_display_layout(jsm::systems sys, u32 layout);
    u32 get_display_layout_fav(jsm::systems sys, u32 fav_idx) const;
    void set_display_layout_fav(jsm::systems sys, u32 fav_idx, u32 layout);
    float get_virtual_scale() const;
    void  set_virtual_scale(float s);

    // Hotkey bindings
    Binding get_hotkey(const char* action) const;
    void    set_hotkey(const char* action, const Binding& b);
    void    clear_hotkey(const char* action);

    // Controller input bindings  (INI: [input.{core_key}.p{N}], player is 1-based)
    // Keyboard column (INI key suffix "_kbd")
    Binding get_input_kbd(const char* core_key, int player, int dbcid) const;
    void    set_input_kbd(const char* core_key, int player, int dbcid, const Binding& b);
    void    clear_input_kbd(const char* core_key, int player, int dbcid);
    // Gamepad column (INI key suffix "_pad")
    Binding get_input_pad(const char* core_key, int player, int dbcid) const;
    void    set_input_pad(const char* core_key, int player, int dbcid, const Binding& b);
    void    clear_input_pad(const char* core_key, int player, int dbcid);
    // Per-system default keyboard binding for P1 (returns empty for P2+)
    static Binding get_default_kbd(const char* core_key, int dbcid);
    // Returns custom kbd if set, else get_default_kbd for P1
    Binding effective_input_kbd(const char* core_key, int player, int dbcid) const;
    // DBCID helpers (dbcid is the JKEYS enum value cast to int)
    static const char* dbcid_ini_key(int dbcid);
    static const char* dbcid_label(int dbcid);

    // Chassis button hotkeys  (INI: [chassis.{core_key}], key = dbcid ini suffix)
    // These are per-core so a "Pause" key on SMS doesn't conflict with
    // the same key on a computer keyboard core.
    Binding get_chassis_hotkey(const char* core_key, JKEYS dbcid) const;
    void    set_chassis_hotkey(const char* core_key, JKEYS dbcid, const Binding& b);
    void    clear_chassis_hotkey(const char* core_key, JKEYS dbcid);
    // Returns custom if set, else built-in default for that core/button.
    Binding effective_chassis_hotkey(const char* core_key, JKEYS dbcid) const;
    // Built-in defaults (matches the original hardcoded keymap per system).
    static Binding get_default_chassis_hotkey(const char* core_key, JKEYS dbcid);
    // INI key suffix for a chassis DBCID ("pause", "diff_left", etc.)
    static const char* chassis_dbcid_ini_key(JKEYS dbcid);

    // Per-core configurable options  (INI: [core_opts.{core_key}])
    i32  get_core_option(const char* core_key, const char* opt_key, i32 default_val) const;
    void set_core_option(const char* core_key, const char* opt_key, i32 value);
    // String-valued options (used for OPTION_STRING kind, e.g. BIOS boot card name)
    std::string get_core_option_str(const char* core_key, const char* opt_key, const char* default_val) const;
    void        set_core_option_str(const char* core_key, const char* opt_key, const char* value);

    // Apple II slot configuration  (INI: [core_opts.apple2e], keys slot_0..slot_7)
    // card_name is a stable token: "empty", "disk2", "mockingboard_b"
    std::string get_slot_card(const char* core_key, u32 slot) const;
    void        set_slot_card(const char* core_key, u32 slot, const char* card_name);

    // Named slot profiles  (INI: [apple2e.profile.{name}])
    // A profile is a complete 8-slot configuration saved under a name.
    std::vector<std::string> list_slot_profiles(const char* core_key) const;
    void save_slot_profile(const char* core_key, const char* name);
    void load_slot_profile(const char* core_key, const char* name);
    void delete_slot_profile(const char* core_key, const char* name);

    // Per-core controller-connected state (port is 1-based, 1–4)
    // Default (when not in ini): port 1 connected, others not.
    bool get_controller_connected(const char* core_key, int port, bool default_val) const;
    void set_controller_connected(const char* core_key, int port, bool connected);

    static const char* sys_to_cli(jsm::systems sys);
    static const char* sys_to_label(jsm::systems sys);
    static bool sys_has_bios(jsm::systems sys);
    static std::string default_bios_dir(jsm::systems sys);
    static std::string default_saves_dir();
    static const char* sys_to_core_key(jsm::systems sys);

    std::string ini_folder_path() const;

private:
    mINI::INIStructure m_ini;
    std::vector<RecentEntry> recent_entries;
    std::string ini_path() const;
    static jsm::systems cli_to_sys(const std::string& cli);
    static std::string section_for(jsm::systems sys);
};
