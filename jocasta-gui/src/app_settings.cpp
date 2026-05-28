#include "app_settings.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <filesystem>
#include <system_error>
#include "helpers/physical_io.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string slot_key(u32 slot) {
    char buf[16]; snprintf(buf, sizeof(buf), "slot_%u", slot);
    return buf;
}

static std::string profile_section(const char* core_key, const char* name) {
    return std::string(core_key) + ".profile." + name;
}

std::string AppSettings::ini_path() const
{
#if defined(_WIN32)
    const char* appdata = getenv("APPDATA");
    std::string base = appdata ? appdata : ".";
    return base + "\\jocasta-emu\\jocasta-emu.ini";
#elif defined(__APPLE__)
    const char* home = getenv("HOME");
    std::string base = home ? home : ".";
    return base + "/Library/Application Support/jocasta-emu/jocasta-emu.ini";
#else
    // XDG Base Directory spec
    const char* xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && xdg[0]) return std::string(xdg) + "/jocasta-emu/jocasta-emu.ini";
    const char* home = getenv("HOME");
    std::string base = home ? home : ".";
    return base + "/.config/jocasta-emu/jocasta-emu.ini";
#endif
}

std::string AppSettings::section_for(jsm::systems sys)
{
    const char* cli = sys_to_cli(sys);
    if (!cli || cli[0] == '\0') return "";
    return std::string("system.") + cli;
}

// ---------------------------------------------------------------------------
// Static mapping tables
// ---------------------------------------------------------------------------

const char* AppSettings::sys_to_cli(jsm::systems sys)
{
    switch (sys) {
        case jsm::systems::APPLEIIe:            return "apple2e";
        case jsm::systems::ATARI2600:           return "atari2600";
        case jsm::systems::CASIO_PV1000:        return "pv1000";
        case jsm::systems::COMMODORE64:         return "c64";
        case jsm::systems::COSMAC_VIP_2k:       return "vip2k";
        case jsm::systems::COSMAC_VIP_4k:       return "vip4k";
        case jsm::systems::DREAMCAST:           return "dreamcast";
        case jsm::systems::GALAKSIJA:           return "galaksija";
        case jsm::systems::DMG:                 return "gb";
        case jsm::systems::GBA:                 return "gba";
        case jsm::systems::GBC:                 return "gbc";
        case jsm::systems::GG:                  return "gg";
        case jsm::systems::GENESIS_JAP:         return "genesis-jp";
        case jsm::systems::GENESIS_USA:         return "genesis";
        case jsm::systems::MAC128K:             return "mac128k";
        case jsm::systems::MAC512K:             return "mac512k";
        case jsm::systems::MACPLUS_1MB:         return "macplus";
        case jsm::systems::MEGADRIVE_PAL:       return "megadrive-pal";
        case jsm::systems::NEOGEO_AES:          return "neogeo-aes";
        case jsm::systems::NEOGEO_MVS:          return "neogeo-mvs";
        case jsm::systems::NEOGEO_POCKET:       return "ngp";
        case jsm::systems::NEOGEO_POCKET_COLOR: return "ngpc";
        case jsm::systems::NES:                 return "nes";
        case jsm::systems::NDS:                 return "nds";
        case jsm::systems::PS1:                 return "ps1";
        case jsm::systems::SG1000:              return "sg1000";
        case jsm::systems::SMS1:                return "sms";
        case jsm::systems::SMS2:                return "sms2";
        case jsm::systems::SNES:                return "snes";
        case jsm::systems::TURBOGRAFX16:        return "tg16";
        case jsm::systems::ZX_SPECTRUM_48K:     return "zx48";
        case jsm::systems::ZX_SPECTRUM_128K:    return "zx128";
        default:                                return "";
    }
}

const char* AppSettings::sys_to_label(jsm::systems sys)
{
    switch (sys) {
        case jsm::systems::APPLEIIe:            return "Apple IIe";
        case jsm::systems::ATARI2600:           return "Atari 2600";
        case jsm::systems::CASIO_PV1000:        return "Casio PV-1000";
        case jsm::systems::COMMODORE64:         return "Commodore 64";
        case jsm::systems::COSMAC_VIP_2k:       return "Cosmac VIP 2K";
        case jsm::systems::COSMAC_VIP_4k:       return "Cosmac VIP 4K";
        case jsm::systems::DREAMCAST:           return "Dreamcast";
        case jsm::systems::GALAKSIJA:           return "Galaksija";
        case jsm::systems::DMG:                 return "Game Boy";
        case jsm::systems::GBA:                 return "Game Boy Advance";
        case jsm::systems::GBC:                 return "Game Boy Color";
        case jsm::systems::GG:                  return "Game Gear";
        case jsm::systems::GENESIS_JAP:         return "Genesis (Japan)";
        case jsm::systems::GENESIS_USA:         return "Genesis (USA)";
        case jsm::systems::MAC128K:             return "Mac 128K";
        case jsm::systems::MAC512K:             return "Mac 512K";
        case jsm::systems::MACPLUS_1MB:         return "Mac Plus 1MB";
        case jsm::systems::MEGADRIVE_PAL:       return "Mega Drive (PAL)";
        case jsm::systems::NEOGEO_AES:          return "Neo Geo AES";
        case jsm::systems::NEOGEO_MVS:          return "Neo Geo MVS";
        case jsm::systems::NEOGEO_POCKET:       return "Neo Geo Pocket";
        case jsm::systems::NEOGEO_POCKET_COLOR: return "Neo Geo Pocket Color";
        case jsm::systems::NES:                 return "NES";
        case jsm::systems::NDS:                 return "Nintendo DS";
        case jsm::systems::PS1:                 return "PlayStation";
        case jsm::systems::SG1000:              return "SG-1000";
        case jsm::systems::SMS1:                return "Sega Master System";
        case jsm::systems::SMS2:                return "Sega Master System II";
        case jsm::systems::SNES:                return "SNES";
        case jsm::systems::TURBOGRAFX16:        return "TurboGrafx-16";
        case jsm::systems::ZX_SPECTRUM_48K:     return "ZX Spectrum 48K";
        case jsm::systems::ZX_SPECTRUM_128K:    return "ZX Spectrum 128K";
        default:                                return "Unknown";
    }
}

bool AppSettings::sys_has_bios(jsm::systems sys)
{
    switch (sys) {
        case jsm::systems::APPLEIIe:
        case jsm::systems::COMMODORE64:
        case jsm::systems::DREAMCAST:
        case jsm::systems::GALAKSIJA:
        case jsm::systems::DMG:
        case jsm::systems::GBA:
        case jsm::systems::GBC:
        case jsm::systems::MAC128K:
        case jsm::systems::MAC512K:
        case jsm::systems::MACPLUS_1MB:
        case jsm::systems::NEOGEO_AES:
        case jsm::systems::NEOGEO_MVS:
        case jsm::systems::NEOGEO_POCKET:
        case jsm::systems::NEOGEO_POCKET_COLOR:
        case jsm::systems::NDS:
        case jsm::systems::PS1:
        case jsm::systems::ZX_SPECTRUM_48K:
        case jsm::systems::ZX_SPECTRUM_128K:
            return true;
        default:
            return false;
    }
}

std::string AppSettings::default_bios_dir(jsm::systems sys)
{
#if defined(_WIN32)
    const char* home = getenv("USERPROFILE");
#else
    const char* home = getenv("HOME");
#endif
    if (!home || !home[0]) home = ".";
    std::string base = std::string(home) + "/Documents/";
    switch (sys) {
        case jsm::systems::APPLEIIe:            return base + "apple2";
        case jsm::systems::COMMODORE64:         return base + "commodore64";
        case jsm::systems::DREAMCAST:           return base + "dreamcast";
        case jsm::systems::GALAKSIJA:           return base + "galaksija";
        case jsm::systems::DMG:                 return base + "gameboy";
        case jsm::systems::GBA:                 return base + "gba";
        case jsm::systems::GBC:                 return base + "gameboy";
        case jsm::systems::MAC128K:
        case jsm::systems::MAC512K:
        case jsm::systems::MACPLUS_1MB:         return base + "mac";
        case jsm::systems::NEOGEO_AES:
        case jsm::systems::NEOGEO_MVS:          return base + "neogeo";
        case jsm::systems::NEOGEO_POCKET:
        case jsm::systems::NEOGEO_POCKET_COLOR: return base + "ngp";
        case jsm::systems::NDS:                 return base + "nds";
        case jsm::systems::PS1:                 return base + "ps1";
        case jsm::systems::ZX_SPECTRUM_48K:
        case jsm::systems::ZX_SPECTRUM_128K:    return base + "zx_spectrum";
        default:                                return base;
    }
}

std::string AppSettings::default_saves_dir()
{
#if defined(_WIN32)
    const char* home = getenv("USERPROFILE");
#else
    const char* home = getenv("HOME");
#endif
    if (!home || !home[0]) home = ".";
    return std::string(home) + "/Documents/saves";
}

std::string AppSettings::ini_folder_path() const
{
    std::string p = ini_path();
    size_t slash = p.rfind('/');
    return (slash != std::string::npos) ? p.substr(0, slash) : p;
}

jsm::systems AppSettings::cli_to_sys(const std::string& cli)
{
    static const jsm::systems all[] = {
        jsm::systems::APPLEIIe, jsm::systems::ATARI2600, jsm::systems::CASIO_PV1000,
        jsm::systems::COMMODORE64, jsm::systems::COSMAC_VIP_2k, jsm::systems::COSMAC_VIP_4k,
        jsm::systems::DREAMCAST, jsm::systems::GALAKSIJA, jsm::systems::DMG,
        jsm::systems::GBA, jsm::systems::GBC, jsm::systems::GG,
        jsm::systems::GENESIS_JAP, jsm::systems::GENESIS_USA,
        jsm::systems::MAC128K, jsm::systems::MAC512K, jsm::systems::MACPLUS_1MB,
        jsm::systems::MEGADRIVE_PAL, jsm::systems::NEOGEO_AES, jsm::systems::NEOGEO_MVS,
        jsm::systems::NEOGEO_POCKET, jsm::systems::NEOGEO_POCKET_COLOR,
        jsm::systems::NES, jsm::systems::NDS, jsm::systems::PS1,
        jsm::systems::SG1000, jsm::systems::SMS1, jsm::systems::SMS2,
        jsm::systems::SNES, jsm::systems::TURBOGRAFX16,
        jsm::systems::ZX_SPECTRUM_48K, jsm::systems::ZX_SPECTRUM_128K,
    };
    for (auto s : all) {
        const char* c = sys_to_cli(s);
        if (c && cli == c) return s;
    }
    return jsm::systems::DMG; // fallback sentinel
}

// ---------------------------------------------------------------------------
// load / save
// ---------------------------------------------------------------------------

static bool is_valid_rom_path(const char* path)
{
    if (!path || path[0] != '/') return false;  // must be absolute
    size_t len = strlen(path);
    if (len < 4) return false;                  // too short to be real
    if (strchr(path, '\n') || strchr(path, '\r')) return false;
    return true;
}

void AppSettings::load()
{
    namespace fs = std::filesystem;
    const std::string path     = ini_path();
    const std::string tmp_path = path + ".tmp";
    std::error_code ec;

    // Recovery: a .tmp means a previous save() was either interrupted mid-write
    // or succeeded but couldn't delete the backup.
    if (fs::exists(tmp_path, ec) && !ec) {
        bool main_exists = fs::exists(path, ec) && !ec;
        if (!main_exists) {
            // Main file is missing — recover from backup
            fs::rename(tmp_path, path, ec);
        } else {
            // Both exist: write succeeded but backup delete failed.
            // Main is the good copy; just remove the stale backup.
            fs::remove(tmp_path, ec);
        }
    }

    recent_entries.clear();
    m_ini.clear();

    mINI::INIFile file(path);
    file.read(m_ini);

    // Parse recent entries out of m_ini
    if (m_ini.has("recent")) {
        auto& sec = m_ini["recent"];
        for (int i = 0; i < 10; i++) {
            std::string key = std::to_string(i);
            if (!sec.has(key)) continue;
            const std::string val = sec.get(key);
            size_t colon = val.find(':');
            if (colon == std::string::npos) continue;
            std::string cli  = val.substr(0, colon);
            std::string path = val.substr(colon + 1);
            if (cli.empty() || !is_valid_rom_path(path.c_str())) continue;
            jsm::systems sys = cli_to_sys(cli);
            const char* check = sys_to_cli(sys);
            if (!check || cli != check) continue; // unknown cli name
            RecentEntry e;
            e.cli_name = cli;
            e.path     = path;
            e.system   = sys;
            recent_entries.push_back(e);
        }
    }

    ensure_defaults();
}

void AppSettings::ensure_defaults()
{
    // Global fast-forward speed
    if (!m_ini.has("settings") || !m_ini["settings"].has("ff_speed"))
        m_ini["settings"]["ff_speed"] = "800";

    // On-boot action (0 = Play Game)
    if (!m_ini.has("settings") || !m_ini["settings"].has("on_boot"))
        m_ini["settings"]["on_boot"] = "0";

    if (!m_ini.has("settings") || !m_ini["settings"].has("fullscreen"))
        m_ini["settings"]["fullscreen"] = "0";

    // Default hotkey bindings
    if (!m_ini.has("hotkeys") || !m_ini["hotkeys"].has("fast_forward"))
        m_ini["hotkeys"]["fast_forward"] = Binding::default_fast_forward().to_ini();
    if (!m_ini.has("hotkeys") || !m_ini["hotkeys"].has("fullscreen"))
        m_ini["hotkeys"]["fullscreen"] = Binding::make_keyboard(ImGuiKey_F11).to_ini();
    if (!m_ini.has("hotkeys") || !m_ini["hotkeys"].has("save_state"))
        m_ini["hotkeys"]["save_state"] = Binding::make_keyboard(ImGuiKey_F5).to_ini();
    if (!m_ini.has("hotkeys") || !m_ini["hotkeys"].has("load_state"))
        m_ini["hotkeys"]["load_state"] = Binding::make_keyboard(ImGuiKey_F7).to_ini();
    if (!m_ini.has("hotkeys") || !m_ini["hotkeys"].has("prev_slot"))
        m_ini["hotkeys"]["prev_slot"] = Binding::make_keyboard(ImGuiKey_F6).to_ini();
    if (!m_ini.has("hotkeys") || !m_ini["hotkeys"].has("next_slot"))
        m_ini["hotkeys"]["next_slot"] = Binding::make_keyboard(ImGuiKey_F8).to_ini();
    if (!m_ini.has("hotkeys") || !m_ini["hotkeys"].has("screenshot"))
        m_ini["hotkeys"]["screenshot"] = Binding::make_keyboard(ImGuiKey_F12).to_ini();
    if (!m_ini.has("hotkeys") || !m_ini["hotkeys"].has("input_mode_toggle"))
        m_ini["hotkeys"]["input_mode_toggle"] = Binding{}.to_ini(); // unbound by default
    if (!m_ini.has("hotkeys") || !m_ini["hotkeys"].has("pause"))
        m_ini["hotkeys"]["pause"] = Binding::make_keyboard(ImGuiKey_Space).to_ini();
    if (!m_ini.has("hotkeys") || !m_ini["hotkeys"].has("layout_cycle"))
        m_ini["hotkeys"]["layout_cycle"] = Binding{}.to_ini();
    if (!m_ini.has("hotkeys") || !m_ini["hotkeys"].has("layout_fav1"))
        m_ini["hotkeys"]["layout_fav1"] = Binding{}.to_ini();
    if (!m_ini.has("hotkeys") || !m_ini["hotkeys"].has("layout_fav2"))
        m_ini["hotkeys"]["layout_fav2"] = Binding{}.to_ini();

    // Chassis hotkeys are stored per-core in [chassis.{core_key}] — no global defaults needed.

    static const jsm::systems bios_systems[] = {
        jsm::systems::APPLEIIe, jsm::systems::COMMODORE64, jsm::systems::DREAMCAST,
        jsm::systems::GALAKSIJA, jsm::systems::DMG, jsm::systems::GBA, jsm::systems::GBC,
        jsm::systems::MAC128K, jsm::systems::MAC512K, jsm::systems::MACPLUS_1MB,
        jsm::systems::NEOGEO_AES, jsm::systems::NEOGEO_MVS,
        jsm::systems::NEOGEO_POCKET, jsm::systems::NEOGEO_POCKET_COLOR,
        jsm::systems::NDS, jsm::systems::PS1,
        jsm::systems::ZX_SPECTRUM_48K, jsm::systems::ZX_SPECTRUM_128K,
    };
    bool wrote_defaults = false;
    for (auto sys : bios_systems) {
        std::string sec = section_for(sys);
        if (sec.empty()) continue;
        if (!m_ini.has(sec) || !m_ini[sec].has("bios_dir")) {
            m_ini[sec]["bios_dir"] = default_bios_dir(sys);
            wrote_defaults = true;
        }
    }
    // Apple IIe default slot configuration: slot 6 = disk2, rest empty.
    // slot 4 reserved for Mockingboard B (not yet implemented).
    {
        static const char* apple2e_ck = "apple2e";
        std::string sec = std::string("core_opts.") + apple2e_ck;
        static const char* slot_defaults[8] = {
            "empty","empty","empty","empty","empty","empty","disk2","empty"
        };
        for (u32 i = 0; i < 8; i++) {
            std::string k = slot_key(i);
            if (!m_ini.has(sec) || !m_ini[sec].has(k)) {
                m_ini[sec][k] = slot_defaults[i];
                wrote_defaults = true;
            }
        }
        // Create a "Default" profile if none exists
        std::string prof_sec = profile_section(apple2e_ck, "Default");
        if (!m_ini.has(prof_sec)) {
            for (u32 i = 0; i < 8; i++)
                m_ini[prof_sec][slot_key(i)] = slot_defaults[i];
            wrote_defaults = true;
        }
    }

    if (wrote_defaults) save();
}

void AppSettings::save()
{
    namespace fs = std::filesystem;
    const std::string path     = ini_path();
    const std::string tmp_path = path + ".tmp";

    // Read current file so we preserve any sections we don't manage in memory
    mINI::INIFile file(path);
    mINI::INIStructure on_disk;
    file.read(on_disk);

    // Merge m_ini on top of on_disk (m_ini wins for sections it owns)
    for (auto& section : m_ini) {
        for (auto& kv : section.second) {
            on_disk[section.first][kv.first] = kv.second;
        }
    }

    // Sync recent entries
    on_disk["recent"].clear();
    for (int i = 0; i < (int)recent_entries.size() && i < 10; i++) {
        on_disk["recent"][std::to_string(i)] =
            recent_entries[i].cli_name + ":" + recent_entries[i].path;
    }

    // Ensure the config directory exists (important on first run)
    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);

    // Back up the existing ini before overwriting.
    // If we crash during the write, .tmp is the last-known-good copy.
    ec = {};
    if (fs::exists(path, ec) && !ec) {
        fs::copy_file(path, tmp_path, fs::copy_options::overwrite_existing, ec);
        // Backup failure is non-fatal — proceed with write regardless
    }

    // Write the new ini (ofstream opened inside generate(), closed on return)
    bool ok = file.generate(on_disk);

    // Backup no longer needed once the new file is confirmed written
    if (ok) {
        ec = {};
        fs::remove(tmp_path, ec);
    }
}

// ---------------------------------------------------------------------------
// last_dir
// ---------------------------------------------------------------------------

std::string AppSettings::get_last_dir(jsm::systems sys) const
{
    std::string sec = section_for(sys);
    if (sec.empty() || !m_ini.has(sec)) return "";
    auto s = m_ini.get(sec);
    if (!s.has("last_dir")) return "";
    return s.get("last_dir");
}

void AppSettings::set_last_dir(jsm::systems sys, const char* dir)
{
    std::string sec = section_for(sys);
    if (sec.empty()) return;
    m_ini[sec]["last_dir"] = dir ? dir : "";
    save();
}

// ---------------------------------------------------------------------------
// bios_dir
// ---------------------------------------------------------------------------

std::string AppSettings::get_bios_dir(jsm::systems sys) const
{
    std::string sec = section_for(sys);
    if (sec.empty() || !m_ini.has(sec)) return "";
    auto s = m_ini.get(sec);
    if (!s.has("bios_dir")) return "";
    return s.get("bios_dir");
}

void AppSettings::set_bios_dir(jsm::systems sys, const char* dir)
{
    std::string sec = section_for(sys);
    if (sec.empty()) return;
    m_ini[sec]["bios_dir"] = dir ? dir : "";
    save();
}

std::string AppSettings::get_slot_rom_path(jsm::systems sys, int slot) const
{
    std::string sec = section_for(sys);
    if (sec.empty()) return {};
    std::string key = std::string("slot") + std::to_string(slot) + "_rom";
    mINI::INIFile file(ini_path());
    mINI::INIStructure ini;
    if (!file.read(ini)) return {};
    if (!ini.has(sec)) return {};
    auto& s = ini[sec];
    if (!s.has(key)) return {};
    return s.get(key);
}

void AppSettings::set_slot_rom_path(jsm::systems sys, int slot, const char* path)
{
    std::string sec = section_for(sys);
    if (sec.empty()) return;
    std::string key = std::string("slot") + std::to_string(slot) + "_rom";
    m_ini[sec][key] = path ? path : "";
    save();
}

// ---------------------------------------------------------------------------
// push_recent
// ---------------------------------------------------------------------------

void AppSettings::push_recent(jsm::systems sys, const char* path)
{
    if (!is_valid_rom_path(path)) return;
    const char* cli = sys_to_cli(sys);
    if (!cli || cli[0] == '\0') return;

    // Copy path into a std::string NOW, before erasing anything from
    // recent_entries. If `path` is entry.path.c_str() for an entry already
    // in the list (e.g. re-opening from the Recent menu), the erase below
    // would destroy that string and leave `path` dangling.
    std::string path_str(path);

    // Remove existing entry with same path
    recent_entries.erase(
        std::remove_if(recent_entries.begin(), recent_entries.end(),
            [&path_str](const RecentEntry& e){ return e.path == path_str; }),
        recent_entries.end());

    // Insert at front
    RecentEntry entry;
    entry.cli_name = cli;
    entry.path     = path_str;
    entry.system   = sys;
    recent_entries.insert(recent_entries.begin(), entry);

    if (recent_entries.size() > 10)
        recent_entries.resize(10);

    save();
}

void AppSettings::clear_recent()
{
    recent_entries.clear();
    save();
}

// ---------------------------------------------------------------------------
// fast-forward speed
// ---------------------------------------------------------------------------

AppSettings::OnBootAction AppSettings::get_on_boot_action() const
{
    if (!m_ini.has("settings")) return OnBoot_Play;
    auto sec = m_ini.get("settings");
    if (!sec.has("on_boot")) return OnBoot_Play;
    try {
        int v = std::stoi(sec.get("on_boot"));
        if (v == 1) return OnBoot_LoadRecent;
        if (v == 2) return OnBoot_Pause;
        return OnBoot_Play;
    } catch (...) { return OnBoot_Play; }
}

void AppSettings::set_on_boot_action(OnBootAction a)
{
    m_ini["settings"]["on_boot"] = std::to_string((int)a);
    save();
}

int AppSettings::get_ff_speed_global() const
{
    if (!m_ini.has("settings")) return 800;
    auto sec = m_ini.get("settings");
    if (!sec.has("ff_speed")) return 800;
    try { return std::stoi(sec.get("ff_speed")); } catch (...) { return 800; }
}

void AppSettings::set_ff_speed_global(int pct)
{
    m_ini["settings"]["ff_speed"] = std::to_string(pct);
    save();
}

int AppSettings::get_ff_speed_core(const char* core_key) const
{
    if (!core_key || !core_key[0]) return 0;
    std::string sec = std::string("core.") + core_key;
    if (!m_ini.has(sec)) return 0;
    auto s = m_ini.get(sec);
    if (!s.has("ff_speed")) return 0;
    try { return std::stoi(s.get("ff_speed")); } catch (...) { return 0; }
}

void AppSettings::set_ff_speed_core(const char* core_key, int pct)
{
    if (!core_key || !core_key[0]) return;
    m_ini[std::string("core.") + core_key]["ff_speed"] = std::to_string(pct);
    save();
}

void AppSettings::clear_ff_speed_core(const char* core_key)
{
    if (!core_key || !core_key[0]) return;
    std::string sec = std::string("core.") + core_key;
    if (m_ini.has(sec))
        m_ini[sec].remove("ff_speed");
    save();
}

int AppSettings::effective_ff_speed(const char* core_key) const
{
    int core_val = get_ff_speed_core(core_key);
    return (core_val > 0) ? core_val : get_ff_speed_global();
}

bool AppSettings::get_fullscreen() const
{
    if (!m_ini.has("settings")) return false;
    auto sec = m_ini.get("settings");
    if (!sec.has("fullscreen")) return false;
    try { return std::stoi(sec.get("fullscreen")) != 0; } catch (...) { return false; }
}

void AppSettings::set_fullscreen(bool enabled)
{
    m_ini["settings"]["fullscreen"] = enabled ? "1" : "0";
    save();
}

u32 AppSettings::get_audio_output_rate() const
{
    if (!m_ini.has("settings")) return 0;
    auto sec = m_ini.get("settings");
    if (!sec.has("audio_output_rate")) return 0;
    try { return (u32)std::stoul(sec.get("audio_output_rate")); } catch (...) { return 0; }
}

void AppSettings::set_audio_output_rate(u32 rate)
{
    m_ini["settings"]["audio_output_rate"] = std::to_string(rate);
    save();
}

int AppSettings::get_audio_prime_padding() const
{
    if (!m_ini.has("settings")) return 1; // default: quarter-frame ramp
    auto sec = m_ini.get("settings");
    if (!sec.has("audio_prime_padding")) return 1;
    try {
        int v = std::stoi(sec.get("audio_prime_padding"));
        if (v < 0 || v > 3) return 1;
        return v;
    } catch (...) { return 1; }
}

void AppSettings::set_audio_prime_padding(int v)
{
    if (v < 0) v = 0;
    if (v > 3) v = 3;
    m_ini["settings"]["audio_prime_padding"] = std::to_string(v);
    save();
}

bool AppSettings::get_dev_features_enabled() const
{
    if (!m_ini.has("settings")) return false;
    auto sec = m_ini.get("settings");
    if (!sec.has("dev_features_enabled")) return false;
    try { return std::stoi(sec.get("dev_features_enabled")) != 0; } catch (...) { return false; }
}

void AppSettings::set_dev_features_enabled(bool v)
{
    m_ini["settings"]["dev_features_enabled"] = v ? "1" : "0";
    save();
}

bool AppSettings::get_show_save_manager() const
{
    if (!m_ini.has("settings")) return false;
    auto sec = m_ini.get("settings");
    if (!sec.has("show_save_manager")) return false;
    try { return std::stoi(sec.get("show_save_manager")) != 0; } catch (...) { return false; }
}

void AppSettings::set_show_save_manager(bool v)
{
    m_ini["settings"]["show_save_manager"] = v ? "1" : "0";
    save();
}

bool AppSettings::get_debug_window_open(const char* core_key, const char* window_key, bool default_val) const
{
    if (!core_key || !core_key[0] || !window_key || !window_key[0]) return default_val;
    std::string sec = std::string("debug_windows.") + core_key;
    if (!m_ini.has(sec)) return default_val;
    auto s = m_ini.get(sec);
    if (!s.has(window_key)) return default_val;
    return s.get(window_key) == "1";
}

void AppSettings::set_debug_window_open(const char* core_key, const char* window_key, bool open)
{
    if (!core_key || !core_key[0] || !window_key || !window_key[0]) return;
    m_ini[std::string("debug_windows.") + core_key][window_key] = open ? "1" : "0";
    save();
}

float AppSettings::get_virtual_scale() const
{
    if (!m_ini.has("settings")) return 0.70f;
    auto sec = m_ini.get("settings");
    if (!sec.has("virtual_scale")) return 0.70f;
    float v = 0.70f;
    try { v = std::stof(sec.get("virtual_scale")); } catch (...) { v = 0.70f; }
    if (v < 0.3f) v = 0.3f;
    if (v > 2.0f) v = 2.0f;
    return v;
}

void AppSettings::set_virtual_scale(float s)
{
    if (s < 0.3f) s = 0.3f;
    if (s > 2.0f) s = 2.0f;
    char buf[32];
    snprintf(buf, sizeof(buf), "%.4f", s);
    m_ini["settings"]["virtual_scale"] = buf;
    save();
}

float AppSettings::get_play_window_zoom(jsm::systems sys) const
{
    std::string sec = section_for(sys);
    if (sec.empty() || !m_ini.has(sec)) return 1.0f;
    auto s = m_ini.get(sec);
    if (!s.has("play_window_zoom")) return 1.0f;

    float zoom = 1.0f;
    try { zoom = std::stof(s.get("play_window_zoom")); } catch (...) { zoom = 1.0f; }
    if (zoom < 0.25f) zoom = 0.25f;
    if (zoom > 8.0f) zoom = 8.0f;
    return zoom;
}

void AppSettings::set_play_window_zoom(jsm::systems sys, float zoom)
{
    std::string sec = section_for(sys);
    if (sec.empty()) return;

    if (zoom < 0.25f) zoom = 0.25f;
    if (zoom > 8.0f) zoom = 8.0f;

    char buf[32];
    snprintf(buf, sizeof(buf), "%.3f", zoom);
    m_ini[sec]["play_window_zoom"] = buf;
    save();
}

// ---------------------------------------------------------------------------
// dual-screen layout settings
// ---------------------------------------------------------------------------

u32 AppSettings::get_display_layout(jsm::systems sys) const
{
    std::string sec = section_for(sys);
    if (sec.empty() || !m_ini.has(sec)) return 0;
    auto s = m_ini.get(sec);
    if (!s.has("display_layout")) return 0;
    try { return static_cast<u32>(std::stoul(s.get("display_layout"))); } catch (...) { return 0; }
}

void AppSettings::set_display_layout(jsm::systems sys, u32 layout)
{
    std::string sec = section_for(sys);
    if (sec.empty()) return;
    m_ini[sec]["display_layout"] = std::to_string(layout);
    save();
}

u32 AppSettings::get_display_layout_fav(jsm::systems sys, u32 fav_idx) const
{
    std::string sec = section_for(sys);
    std::string key = std::string("display_layout_fav") + std::to_string(fav_idx);
    u32 def = (fav_idx == 0) ? 2u : 1u; // defaults: SideBySide=2, Swapped=1
    if (sec.empty() || !m_ini.has(sec)) return def;
    auto s = m_ini.get(sec);
    if (!s.has(key)) return def;
    try { return static_cast<u32>(std::stoul(s.get(key))); } catch (...) { return def; }
}

void AppSettings::set_display_layout_fav(jsm::systems sys, u32 fav_idx, u32 layout)
{
    std::string sec = section_for(sys);
    if (sec.empty()) return;
    std::string key = std::string("display_layout_fav") + std::to_string(fav_idx);
    m_ini[sec][key] = std::to_string(layout);
    save();
}

// ---------------------------------------------------------------------------
// hotkey bindings
// ---------------------------------------------------------------------------

Binding AppSettings::get_hotkey(const char* action) const
{
    if (!action || !action[0]) return Binding{};
    if (!m_ini.has("hotkeys")) return Binding{};
    auto sec = m_ini.get("hotkeys");
    if (!sec.has(action)) return Binding{};
    return Binding::from_ini(sec.get(action));
}

void AppSettings::set_hotkey(const char* action, const Binding& b)
{
    if (!action || !action[0]) return;
    m_ini["hotkeys"][action] = b.to_ini();
    save();
}

void AppSettings::clear_hotkey(const char* action)
{
    if (!action || !action[0]) return;
    // Reset to the default for known actions
    Binding def;
    if (std::string(action) == "fast_forward") def = Binding::default_fast_forward();
    else if (std::string(action) == "fullscreen") def = Binding::make_keyboard(ImGuiKey_F11);
    else if (std::string(action) == "pause")      def = Binding::make_keyboard(ImGuiKey_Space);
    m_ini["hotkeys"][action] = def.to_ini();
    save();
}

// ---------------------------------------------------------------------------
// sys_to_core_key — maps individual system enums to their shared core key
// ---------------------------------------------------------------------------

const char* AppSettings::sys_to_core_key(jsm::systems sys)
{
    switch (sys) {
        case jsm::systems::DMG:
        case jsm::systems::GBC:                return "gameboy";
        case jsm::systems::GBA:                return "gba";
        case jsm::systems::NES:                return "nes";
        case jsm::systems::SNES:               return "snes";
        case jsm::systems::GENESIS_JAP:
        case jsm::systems::GENESIS_USA:
        case jsm::systems::MEGADRIVE_PAL:      return "genesis";
        case jsm::systems::SMS1:
        case jsm::systems::SMS2:
        case jsm::systems::GG:                 return "sms";
        case jsm::systems::SG1000:             return "sg1000";
        case jsm::systems::DREAMCAST:          return "dreamcast";
        case jsm::systems::MAC128K:
        case jsm::systems::MAC512K:
        case jsm::systems::MACPLUS_1MB:        return "mac";
        case jsm::systems::APPLEIIe:           return "apple2e";
        case jsm::systems::NEOGEO_AES:
        case jsm::systems::NEOGEO_MVS:         return "neogeo";
        case jsm::systems::NEOGEO_POCKET:
        case jsm::systems::NEOGEO_POCKET_COLOR: return "ngp";
        case jsm::systems::PS1:                return "ps1";
        case jsm::systems::NDS:                return "nds";
        case jsm::systems::ATARI2600:          return "atari2600";
        case jsm::systems::TURBOGRAFX16:       return "tg16";
        case jsm::systems::ZX_SPECTRUM_48K:
        case jsm::systems::ZX_SPECTRUM_128K:   return "zx";
        case jsm::systems::COSMAC_VIP_2k:
        case jsm::systems::COSMAC_VIP_4k:      return "cosmac";
        case jsm::systems::COMMODORE64:        return "c64";
        case jsm::systems::CASIO_PV1000:       return "pv1000";
        case jsm::systems::GALAKSIJA:          return "galaksija";
        default:                               return nullptr;
    }
}

// ---------------------------------------------------------------------------
// Global saves folder
// ---------------------------------------------------------------------------

std::string AppSettings::get_saves_dir() const
{
    if (!m_ini.has("settings")) return {};
    auto s = m_ini.get("settings");
    if (!s.has("saves_dir")) return {};
    return s.get("saves_dir");
}

void AppSettings::set_saves_dir(const char* dir)
{
    m_ini["settings"]["saves_dir"] = dir ? dir : "";
    save();
}

// ---------------------------------------------------------------------------
// Global defaults for save-location / SRAM behaviour  ([settings])
// ---------------------------------------------------------------------------

bool AppSettings::get_states_in_saves_dir_global() const
{
    if (!m_ini.has("settings")) return false;
    auto s = m_ini.get("settings");
    if (!s.has("states_in_saves_dir")) return false;
    return s.get("states_in_saves_dir") == "1";
}
void AppSettings::set_states_in_saves_dir_global(bool v)
{
    m_ini["settings"]["states_in_saves_dir"] = v ? "1" : "0";
    save();
}

bool AppSettings::get_sram_in_saves_dir_global() const
{
    if (!m_ini.has("settings")) return false;
    auto s = m_ini.get("settings");
    if (!s.has("sram_in_saves_dir")) return false;
    return s.get("sram_in_saves_dir") == "1";
}
void AppSettings::set_sram_in_saves_dir_global(bool v)
{
    m_ini["settings"]["sram_in_saves_dir"] = v ? "1" : "0";
    save();
}

bool AppSettings::get_save_sram_with_state_global() const
{
    if (!m_ini.has("settings")) return false;
    auto s = m_ini.get("settings");
    if (!s.has("save_sram_with_state")) return false;
    return s.get("save_sram_with_state") == "1";
}
void AppSettings::set_save_sram_with_state_global(bool v)
{
    m_ini["settings"]["save_sram_with_state"] = v ? "1" : "0";
    save();
}

// ---------------------------------------------------------------------------
// Per-core overrides  ([core.{key}], key only present when overriding global)
// ---------------------------------------------------------------------------

// Helper: does a per-core key exist?
static bool core_has(const mINI::INIStructure& ini,
                     const std::string& sec, const char* key)
{
    if (!ini.has(sec)) return false;
    return ini.get(sec).has(key);
}
static bool core_get_bool(const mINI::INIStructure& ini,
                          const std::string& sec, const char* key)
{
    return ini.get(sec).get(key) == "1";
}

// ---------------------------------------------------------------------------
// Virtual input overlay — global defaults + per-core overrides
// ---------------------------------------------------------------------------

bool AppSettings::get_show_virtual_controller_global() const
{
    if (!m_ini.has("settings")) return true;
    auto sec = m_ini.get("settings");
    if (!sec.has("show_virtual_controller")) return true;
    try { return std::stoi(sec.get("show_virtual_controller")) != 0; } catch (...) { return true; }
}
void AppSettings::set_show_virtual_controller_global(bool v)
{
    m_ini["settings"]["show_virtual_controller"] = v ? "1" : "0";
    save();
}

bool AppSettings::get_show_virtual_keyboard_global() const
{
    if (!m_ini.has("settings")) return true;
    auto sec = m_ini.get("settings");
    if (!sec.has("show_virtual_keyboard")) return true;
    try { return std::stoi(sec.get("show_virtual_keyboard")) != 0; } catch (...) { return true; }
}
void AppSettings::set_show_virtual_keyboard_global(bool v)
{
    m_ini["settings"]["show_virtual_keyboard"] = v ? "1" : "0";
    save();
}

bool AppSettings::has_show_virtual_controller_override(const char* core_key) const
{
    if (!core_key || !core_key[0]) return false;
    return core_has(m_ini, std::string("core.") + core_key, "show_virtual_controller");
}
bool AppSettings::get_show_virtual_controller(const char* core_key) const
{
    if (!core_key || !core_key[0]) return true;
    std::string sec = std::string("core.") + core_key;
    if (!core_has(m_ini, sec, "show_virtual_controller")) return true;
    return core_get_bool(m_ini, sec, "show_virtual_controller");
}
void AppSettings::set_show_virtual_controller(const char* core_key, bool v)
{
    if (!core_key || !core_key[0]) return;
    m_ini[std::string("core.") + core_key]["show_virtual_controller"] = v ? "1" : "0";
    save();
}
void AppSettings::clear_show_virtual_controller(const char* core_key)
{
    if (!core_key || !core_key[0]) return;
    std::string sec = std::string("core.") + core_key;
    if (m_ini.has(sec)) m_ini[sec].remove("show_virtual_controller");
    save();
}
bool AppSettings::effective_show_virtual_controller(const char* core_key) const
{
    if (has_show_virtual_controller_override(core_key))
        return get_show_virtual_controller(core_key);
    return get_show_virtual_controller_global();
}

bool AppSettings::has_show_virtual_keyboard_override(const char* core_key) const
{
    if (!core_key || !core_key[0]) return false;
    return core_has(m_ini, std::string("core.") + core_key, "show_virtual_keyboard");
}
bool AppSettings::get_show_virtual_keyboard(const char* core_key) const
{
    if (!core_key || !core_key[0]) return true;
    std::string sec = std::string("core.") + core_key;
    if (!core_has(m_ini, sec, "show_virtual_keyboard")) return true;
    return core_get_bool(m_ini, sec, "show_virtual_keyboard");
}
void AppSettings::set_show_virtual_keyboard(const char* core_key, bool v)
{
    if (!core_key || !core_key[0]) return;
    m_ini[std::string("core.") + core_key]["show_virtual_keyboard"] = v ? "1" : "0";
    save();
}
void AppSettings::clear_show_virtual_keyboard(const char* core_key)
{
    if (!core_key || !core_key[0]) return;
    std::string sec = std::string("core.") + core_key;
    if (m_ini.has(sec)) m_ini[sec].remove("show_virtual_keyboard");
    save();
}
bool AppSettings::effective_show_virtual_keyboard(const char* core_key) const
{
    if (has_show_virtual_keyboard_override(core_key))
        return get_show_virtual_keyboard(core_key);
    return get_show_virtual_keyboard_global();
}

bool AppSettings::has_states_in_saves_dir_override(const char* core_key) const
{
    if (!core_key || !core_key[0]) return false;
    return core_has(m_ini, std::string("core.") + core_key, "states_in_saves_dir");
}
bool AppSettings::get_states_in_saves_dir(const char* core_key) const
{
    if (!core_key || !core_key[0]) return false;
    std::string sec = std::string("core.") + core_key;
    return core_has(m_ini, sec, "states_in_saves_dir") &&
           core_get_bool(m_ini, sec, "states_in_saves_dir");
}
void AppSettings::set_states_in_saves_dir(const char* core_key, bool v)
{
    if (!core_key || !core_key[0]) return;
    m_ini[std::string("core.") + core_key]["states_in_saves_dir"] = v ? "1" : "0";
    save();
}
void AppSettings::clear_states_in_saves_dir(const char* core_key)
{
    if (!core_key || !core_key[0]) return;
    std::string sec = std::string("core.") + core_key;
    if (m_ini.has(sec)) m_ini[sec].remove("states_in_saves_dir");
    save();
}
bool AppSettings::effective_states_in_saves_dir(const char* core_key) const
{
    if (has_states_in_saves_dir_override(core_key))
        return get_states_in_saves_dir(core_key);
    return get_states_in_saves_dir_global();
}

bool AppSettings::has_sram_in_saves_dir_override(const char* core_key) const
{
    if (!core_key || !core_key[0]) return false;
    return core_has(m_ini, std::string("core.") + core_key, "sram_in_saves_dir");
}
bool AppSettings::get_sram_in_saves_dir(const char* core_key) const
{
    if (!core_key || !core_key[0]) return false;
    std::string sec = std::string("core.") + core_key;
    return core_has(m_ini, sec, "sram_in_saves_dir") &&
           core_get_bool(m_ini, sec, "sram_in_saves_dir");
}
void AppSettings::set_sram_in_saves_dir(const char* core_key, bool v)
{
    if (!core_key || !core_key[0]) return;
    m_ini[std::string("core.") + core_key]["sram_in_saves_dir"] = v ? "1" : "0";
    save();
}
void AppSettings::clear_sram_in_saves_dir(const char* core_key)
{
    if (!core_key || !core_key[0]) return;
    std::string sec = std::string("core.") + core_key;
    if (m_ini.has(sec)) m_ini[sec].remove("sram_in_saves_dir");
    save();
}
bool AppSettings::effective_sram_in_saves_dir(const char* core_key) const
{
    if (has_sram_in_saves_dir_override(core_key))
        return get_sram_in_saves_dir(core_key);
    return get_sram_in_saves_dir_global();
}

// ---------------------------------------------------------------------------
// State slot & SRAM-with-state (per core)
// ---------------------------------------------------------------------------

int AppSettings::get_state_slot(const char* core_key) const
{
    if (!core_key || !core_key[0]) return 0;
    std::string sec = std::string("core.") + core_key;
    if (!m_ini.has(sec)) return 0;
    auto s = m_ini.get(sec);
    if (!s.has("state_slot")) return 0;
    try { int v = std::stoi(s.get("state_slot")); return (v >= 0 && v <= 9) ? v : 0; }
    catch (...) { return 0; }
}

void AppSettings::set_state_slot(const char* core_key, int slot)
{
    if (!core_key || !core_key[0]) return;
    if (slot < 0 || slot > 9) return;
    m_ini[std::string("core.") + core_key]["state_slot"] = std::to_string(slot);
    save();
}

bool AppSettings::has_save_sram_with_state_override(const char* core_key) const
{
    if (!core_key || !core_key[0]) return false;
    return core_has(m_ini, std::string("core.") + core_key, "save_sram_with_state");
}
bool AppSettings::get_save_sram_with_state(const char* core_key) const
{
    if (!core_key || !core_key[0]) return false;
    std::string sec = std::string("core.") + core_key;
    return core_has(m_ini, sec, "save_sram_with_state") &&
           core_get_bool(m_ini, sec, "save_sram_with_state");
}
void AppSettings::set_save_sram_with_state(const char* core_key, bool v)
{
    if (!core_key || !core_key[0]) return;
    m_ini[std::string("core.") + core_key]["save_sram_with_state"] = v ? "1" : "0";
    save();
}
void AppSettings::clear_save_sram_with_state(const char* core_key)
{
    if (!core_key || !core_key[0]) return;
    std::string sec = std::string("core.") + core_key;
    if (m_ini.has(sec)) m_ini[sec].remove("save_sram_with_state");
    save();
}
bool AppSettings::effective_save_sram_with_state(const char* core_key) const
{
    if (has_save_sram_with_state_override(core_key))
        return get_save_sram_with_state(core_key);
    return get_save_sram_with_state_global();
}

// ---------------------------------------------------------------------------
// DBCID helpers
// ---------------------------------------------------------------------------

const char* AppSettings::dbcid_ini_key(int dbcid)
{
    switch (dbcid) {
        case DBCID_co_up:              return "up";
        case DBCID_co_down:            return "down";
        case DBCID_co_left:            return "left";
        case DBCID_co_right:           return "right";
        case DBCID_co_fire1:           return "fire1";
        case DBCID_co_fire2:           return "fire2";
        case DBCID_co_fire3:           return "fire3";
        case DBCID_co_fire4:           return "fire4";
        case DBCID_co_fire5:           return "fire5";
        case DBCID_co_fire6:           return "fire6";
        case DBCID_co_shoulder_left:   return "lshoulder";
        case DBCID_co_shoulder_right:  return "rshoulder";
        case DBCID_co_shoulder_left2:  return "lshoulder2";
        case DBCID_co_shoulder_right2: return "rshoulder2";
        case DBCID_co_start:           return "start";
        case DBCID_co_select:          return "select";
        default:                       return "";
    }
}

const char* AppSettings::dbcid_label(int dbcid)
{
    switch (dbcid) {
        case DBCID_co_up:              return "Up";
        case DBCID_co_down:            return "Down";
        case DBCID_co_left:            return "Left";
        case DBCID_co_right:           return "Right";
        case DBCID_co_fire1:           return "Fire 1";
        case DBCID_co_fire2:           return "Fire 2";
        case DBCID_co_fire3:           return "Fire 3";
        case DBCID_co_fire4:           return "Fire 4";
        case DBCID_co_fire5:           return "Fire 5";
        case DBCID_co_fire6:           return "Fire 6";
        case DBCID_co_shoulder_left:   return "L Shoulder";
        case DBCID_co_shoulder_right:  return "R Shoulder";
        case DBCID_co_shoulder_left2:  return "L Shoulder 2";
        case DBCID_co_shoulder_right2: return "R Shoulder 2";
        case DBCID_co_start:           return "Start";
        case DBCID_co_select:          return "Select";
        default:                       return "(unknown)";
    }
}

// ---------------------------------------------------------------------------
// Controller input bindings
// ---------------------------------------------------------------------------

// Internal helpers: read/write a single binding slot (suffix = "_kbd" or "_pad")
static Binding input_get(const mINI::INIStructure& ini, const char* core_key, int player, int dbcid, const char* suffix)
{
    if (!core_key || !core_key[0]) return Binding{};
    if (player < 1 || player > 4)  return Binding{};
    const char* base = AppSettings::dbcid_ini_key(dbcid);
    if (!base || !base[0])         return Binding{};
    std::string sec = std::string("input.") + core_key + ".p" + std::to_string(player);
    std::string key = std::string(base) + suffix;
    if (!ini.has(sec)) return Binding{};
    auto s = ini.get(sec);
    if (!s.has(key)) return Binding{};
    return Binding::from_ini(s.get(key));
}

static void input_set(mINI::INIStructure& ini, const char* core_key, int player, int dbcid, const char* suffix, const Binding& b)
{
    if (!core_key || !core_key[0]) return;
    if (player < 1 || player > 4)  return;
    const char* base = AppSettings::dbcid_ini_key(dbcid);
    if (!base || !base[0]) return;
    std::string sec = std::string("input.") + core_key + ".p" + std::to_string(player);
    std::string key = std::string(base) + suffix;
    ini[sec][key] = b.to_ini();
}

static void input_clear(mINI::INIStructure& ini, const char* core_key, int player, int dbcid, const char* suffix)
{
    if (!core_key || !core_key[0]) return;
    if (player < 1 || player > 4)  return;
    const char* base = AppSettings::dbcid_ini_key(dbcid);
    if (!base || !base[0]) return;
    std::string sec = std::string("input.") + core_key + ".p" + std::to_string(player);
    std::string key = std::string(base) + suffix;
    if (ini.has(sec))
        ini[sec].remove(key);
}

// --- Keyboard bindings ---

Binding AppSettings::get_input_kbd(const char* core_key, int player, int dbcid) const
{
    return input_get(m_ini, core_key, player, dbcid, "_kbd");
}

void AppSettings::set_input_kbd(const char* core_key, int player, int dbcid, const Binding& b)
{
    input_set(m_ini, core_key, player, dbcid, "_kbd", b);
    save();
}

void AppSettings::clear_input_kbd(const char* core_key, int player, int dbcid)
{
    input_clear(m_ini, core_key, player, dbcid, "_kbd");
    save();
}

// --- Gamepad bindings ---

Binding AppSettings::get_input_pad(const char* core_key, int player, int dbcid) const
{
    return input_get(m_ini, core_key, player, dbcid, "_pad");
}

void AppSettings::set_input_pad(const char* core_key, int player, int dbcid, const Binding& b)
{
    input_set(m_ini, core_key, player, dbcid, "_pad", b);
    save();
}

void AppSettings::clear_input_pad(const char* core_key, int player, int dbcid)
{
    input_clear(m_ini, core_key, player, dbcid, "_pad");
    save();
}

// --- Per-system default keyboard bindings for P1 ---

Binding AppSettings::get_default_kbd(const char* core_key, int dbcid)
{
    if (!core_key || !core_key[0]) return Binding{};

    auto kb = [](ImGuiKey k) { return Binding::make_keyboard(k); };

    // Universal d-pad / meta buttons
    switch (dbcid) {
        case DBCID_co_up:     return kb(ImGuiKey_UpArrow);
        case DBCID_co_down:   return kb(ImGuiKey_DownArrow);
        case DBCID_co_left:   return kb(ImGuiKey_LeftArrow);
        case DBCID_co_right:  return kb(ImGuiKey_RightArrow);
        case DBCID_co_start:  return kb(ImGuiKey_Enter);
        case DBCID_co_select: return kb(ImGuiKey_Tab);
        default: break;
    }

    // Per-core fire / shoulder defaults
    std::string ck(core_key);

    if (ck == "gameboy" || ck == "nes") {
        // fire1=A button (X key), fire2=B button (Z key)
        switch (dbcid) {
            case DBCID_co_fire1: return kb(ImGuiKey_X); // A
            case DBCID_co_fire2: return kb(ImGuiKey_Z); // B
            default: break;
        }
    } else if (ck == "gba") {
        // fire1=B(Z), fire2=A(X), L(Q), R(W)
        switch (dbcid) {
            case DBCID_co_fire1:          return kb(ImGuiKey_Z); // B
            case DBCID_co_fire2:          return kb(ImGuiKey_X); // A
            case DBCID_co_shoulder_left:  return kb(ImGuiKey_Q); // L
            case DBCID_co_shoulder_right: return kb(ImGuiKey_W); // R
            default: break;
        }
    } else if (ck == "snes") {
        // fire1=A(X), fire2=B(Z), fire3=X(S), fire4=Y(A), L(Q), R(W)
        switch (dbcid) {
            case DBCID_co_fire1:          return kb(ImGuiKey_X); // A
            case DBCID_co_fire2:          return kb(ImGuiKey_Z); // B
            case DBCID_co_fire3:          return kb(ImGuiKey_S); // X
            case DBCID_co_fire4:          return kb(ImGuiKey_A); // Y
            case DBCID_co_shoulder_left:  return kb(ImGuiKey_Q); // L
            case DBCID_co_shoulder_right: return kb(ImGuiKey_W); // R
            default: break;
        }
    } else if (ck == "genesis") {
        // fire1=A(Z), fire2=B(X), fire3=C(C), fire4=X(A), fire5=Y(S), fire6=Z(D)
        switch (dbcid) {
            case DBCID_co_fire1: return kb(ImGuiKey_Z); // A
            case DBCID_co_fire2: return kb(ImGuiKey_X); // B
            case DBCID_co_fire3: return kb(ImGuiKey_C); // C
            case DBCID_co_fire4: return kb(ImGuiKey_A); // X
            case DBCID_co_fire5: return kb(ImGuiKey_S); // Y
            case DBCID_co_fire6: return kb(ImGuiKey_D); // Z
            default: break;
        }
    } else if (ck == "sms" || ck == "sg1000" || ck == "pv1000" || ck == "tg16") {
        switch (dbcid) {
            case DBCID_co_fire1: return kb(ImGuiKey_Z);
            case DBCID_co_fire2: return kb(ImGuiKey_X);
            default: break;
        }
    } else if (ck == "atari2600") {
        switch (dbcid) {
            case DBCID_co_fire1: return kb(ImGuiKey_Z); // fire
            default: break;
        }
    } else if (ck == "neogeo") {
        // fire1=A(Z), fire2=B(X), fire3=C(A), fire4=D(S)
        switch (dbcid) {
            case DBCID_co_fire1: return kb(ImGuiKey_Z);
            case DBCID_co_fire2: return kb(ImGuiKey_X);
            case DBCID_co_fire3: return kb(ImGuiKey_A);
            case DBCID_co_fire4: return kb(ImGuiKey_S);
            default: break;
        }
    } else if (ck == "dreamcast") {
        // fire1=A(Z), fire2=B(X), fire4=X(A), fire5=Y(S)
        switch (dbcid) {
            case DBCID_co_fire1: return kb(ImGuiKey_Z); // A
            case DBCID_co_fire2: return kb(ImGuiKey_X); // B
            case DBCID_co_fire4: return kb(ImGuiKey_A); // X
            case DBCID_co_fire5: return kb(ImGuiKey_S); // Y
            default: break;
        }
    } else if (ck == "ps1") {
        // fire1=square(Z), fire2=cross(X), fire4=triangle(A), fire5=circle(S)
        // L1(Q), R1(W), L2(1), R2(2)
        switch (dbcid) {
            case DBCID_co_fire1:           return kb(ImGuiKey_Z); // square
            case DBCID_co_fire2:           return kb(ImGuiKey_X); // cross
            case DBCID_co_fire4:           return kb(ImGuiKey_A); // triangle
            case DBCID_co_fire5:           return kb(ImGuiKey_S); // circle
            case DBCID_co_shoulder_left:   return kb(ImGuiKey_Q); // L1
            case DBCID_co_shoulder_right:  return kb(ImGuiKey_W); // R1
            case DBCID_co_shoulder_left2:  return kb(ImGuiKey_1); // L2
            case DBCID_co_shoulder_right2: return kb(ImGuiKey_2); // R2
            default: break;
        }
    } else if (ck == "nds") {
        // fire1=B(Z), fire2=A(X), fire3=Y(A), fire4=X(S), L(Q), R(W)
        switch (dbcid) {
            case DBCID_co_fire1:          return kb(ImGuiKey_Z); // B
            case DBCID_co_fire2:          return kb(ImGuiKey_X); // A
            case DBCID_co_fire3:          return kb(ImGuiKey_A); // Y
            case DBCID_co_fire4:          return kb(ImGuiKey_S); // X
            case DBCID_co_shoulder_left:  return kb(ImGuiKey_Q); // L
            case DBCID_co_shoulder_right: return kb(ImGuiKey_W); // R
            default: break;
        }
    }

    return Binding{};
}

Binding AppSettings::effective_input_kbd(const char* core_key, int player, int dbcid) const
{
    Binding custom = get_input_kbd(core_key, player, dbcid);
    if (!custom.is_none()) return custom;
    if (player == 1) return get_default_kbd(core_key, dbcid);
    return Binding{};
}

// ---------------------------------------------------------------------------
// Chassis button hotkeys  (INI section: [chassis.{core_key}])
// ---------------------------------------------------------------------------

const char* AppSettings::chassis_dbcid_ini_key(JKEYS dbcid)
{
    switch (dbcid) {
        case DBCID_ch_pause:       return "pause";
        case DBCID_ch_diff_left:   return "diff_left";
        case DBCID_ch_diff_right:  return "diff_right";
        case DBCID_ch_game_select: return "game_select";
        default:                   return nullptr;
    }
}

Binding AppSettings::get_default_chassis_hotkey(const char* core_key, JKEYS dbcid)
{
    if (!core_key || !core_key[0]) return Binding{};
    std::string ck(core_key);
    auto kb = [](ImGuiKey k) { return Binding::make_keyboard(k); };

    if (ck == "sms") {
        if (dbcid == DBCID_ch_pause) return kb(ImGuiKey_W);
    } else if (ck == "atari2600") {
        if (dbcid == DBCID_ch_diff_left)   return kb(ImGuiKey_B);
        if (dbcid == DBCID_ch_diff_right)  return kb(ImGuiKey_N);
        if (dbcid == DBCID_ch_game_select) return kb(ImGuiKey_M);
    }
    return Binding{};
}

Binding AppSettings::get_chassis_hotkey(const char* core_key, JKEYS dbcid) const
{
    if (!core_key || !core_key[0]) return Binding{};
    const char* ikey = chassis_dbcid_ini_key(dbcid);
    if (!ikey) return Binding{};
    std::string sec = std::string("chassis.") + core_key;
    if (!m_ini.has(sec)) return Binding{};
    auto s = m_ini.get(sec);
    if (!s.has(ikey)) return Binding{};
    return Binding::from_ini(s.get(ikey));
}

void AppSettings::set_chassis_hotkey(const char* core_key, JKEYS dbcid, const Binding& b)
{
    if (!core_key || !core_key[0]) return;
    const char* ikey = chassis_dbcid_ini_key(dbcid);
    if (!ikey) return;
    m_ini[std::string("chassis.") + core_key][ikey] = b.to_ini();
    save();
}

void AppSettings::clear_chassis_hotkey(const char* core_key, JKEYS dbcid)
{
    if (!core_key || !core_key[0]) return;
    const char* ikey = chassis_dbcid_ini_key(dbcid);
    if (!ikey) return;
    std::string sec = std::string("chassis.") + core_key;
    if (m_ini.has(sec)) m_ini[sec].remove(ikey);
    save();
}

Binding AppSettings::effective_chassis_hotkey(const char* core_key, JKEYS dbcid) const
{
    Binding custom = get_chassis_hotkey(core_key, dbcid);
    if (!custom.is_none()) return custom;
    return get_default_chassis_hotkey(core_key, dbcid);
}

// ---------------------------------------------------------------------------
// Per-core options  (INI section: [core_opts.{core_key}])
// ---------------------------------------------------------------------------

i32 AppSettings::get_core_option(const char* core_key, const char* opt_key, i32 default_val) const
{
    if (!core_key || !core_key[0] || !opt_key || !opt_key[0]) return default_val;
    std::string sec = std::string("core_opts.") + core_key;
    if (!m_ini.has(sec)) return default_val;
    auto s = m_ini.get(sec);
    if (!s.has(opt_key)) return default_val;
    try { return (i32)std::stoi(s.get(opt_key)); } catch (...) { return default_val; }
}

void AppSettings::set_core_option(const char* core_key, const char* opt_key, i32 value)
{
    if (!core_key || !core_key[0] || !opt_key || !opt_key[0]) return;
    m_ini[std::string("core_opts.") + core_key][opt_key] = std::to_string((int)value);
    save();
}

std::string AppSettings::get_core_option_str(const char* core_key, const char* opt_key, const char* default_val) const
{
    if (!core_key || !core_key[0] || !opt_key || !opt_key[0]) return default_val ? default_val : "";
    std::string sec = std::string("core_opts.") + core_key;
    if (!m_ini.has(sec)) return default_val ? default_val : "";
    auto s = m_ini.get(sec);
    if (!s.has(opt_key)) return default_val ? default_val : "";
    return s.get(opt_key);
}

void AppSettings::set_core_option_str(const char* core_key, const char* opt_key, const char* value)
{
    if (!core_key || !core_key[0] || !opt_key || !opt_key[0]) return;
    m_ini[std::string("core_opts.") + core_key][opt_key] = value ? value : "";
    save();
}

// ---------------------------------------------------------------------------
// Apple II slot configuration + named profiles
// ---------------------------------------------------------------------------

std::string AppSettings::get_slot_card(const char* core_key, u32 slot) const
{
    return get_core_option_str(core_key, slot_key(slot).c_str(), "empty");
}

void AppSettings::set_slot_card(const char* core_key, u32 slot, const char* card_name)
{
    set_core_option_str(core_key, slot_key(slot).c_str(), card_name ? card_name : "empty");
}

std::vector<std::string> AppSettings::list_slot_profiles(const char* core_key) const
{
    std::string prefix = std::string(core_key) + ".profile.";
    std::vector<std::string> out;
    for (auto& sec : m_ini) {
        if (sec.first.rfind(prefix, 0) == 0)
            out.push_back(sec.first.substr(prefix.size()));
    }
    return out;
}

void AppSettings::save_slot_profile(const char* core_key, const char* name)
{
    if (!core_key || !name || !name[0]) return;
    std::string sec = profile_section(core_key, name);
    for (u32 i = 0; i < 8; i++)
        m_ini[sec][slot_key(i)] = get_slot_card(core_key, i);
    save();
}

void AppSettings::load_slot_profile(const char* core_key, const char* name)
{
    if (!core_key || !name || !name[0]) return;
    std::string sec = profile_section(core_key, name);
    if (!m_ini.has(sec)) return;
    for (u32 i = 0; i < 8; i++) {
        std::string k = slot_key(i);
        if (m_ini[sec].has(k))
            set_slot_card(core_key, i, m_ini[sec].get(k).c_str());
    }
}

void AppSettings::delete_slot_profile(const char* core_key, const char* name)
{
    if (!core_key || !name || !name[0]) return;
    m_ini.remove(profile_section(core_key, name));
    save();
}

// ---------------------------------------------------------------------------
// Controller-connected state  (INI keys: p1_connected … p4_connected)
// ---------------------------------------------------------------------------

bool AppSettings::get_controller_connected(const char* core_key, int port, bool default_val) const
{
    if (!core_key || !core_key[0] || port < 1 || port > 4) return default_val;
    std::string sec = std::string("core_opts.") + core_key;
    std::string key = std::string("p") + std::to_string(port) + "_connected";
    if (!m_ini.has(sec)) return default_val;
    auto s = m_ini.get(sec);
    if (!s.has(key)) return default_val;
    return s.get(key) == "1";
}

void AppSettings::set_controller_connected(const char* core_key, int port, bool connected)
{
    if (!core_key || !core_key[0] || port < 1 || port > 4) return;
    std::string sec = std::string("core_opts.") + core_key;
    std::string key = std::string("p") + std::to_string(port) + "_connected";
    m_ini[sec][key] = connected ? "1" : "0";
    save();
}
