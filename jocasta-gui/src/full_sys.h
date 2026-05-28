#pragma once

#include <vector>
#include <string>
#include <map>
#include "build.h"
#include "helpers/int.h"

class AppSettings; // forward declaration — full definition in app_settings.h

enum class DisplayLayout : u32 {
    TopBottom = 0,       // top screen above bottom screen
    Swapped,             // bottom screen above top screen
    SideBySide,          // top on left, bottom on right
    BigTopLittleBottom,  // top gets ~75% of height
    BigBottomLittleTop,  // bottom gets ~75% of height
    SeparateWindows,     // windowed only: two separate OS windows
    COUNT
};
static inline const char* display_layout_name(DisplayLayout l) {
    switch (l) {
        case DisplayLayout::TopBottom:          return "Top/Bottom";
        case DisplayLayout::Swapped:            return "Swapped";
        case DisplayLayout::SideBySide:         return "Side by Side";
        case DisplayLayout::BigTopLittleBottom: return "Big Top";
        case DisplayLayout::BigBottomLittleTop: return "Big Bottom";
        case DisplayLayout::SeparateWindows:    return "Separate Windows";
        default:                                return "?";
    }
}

#include "helpers/sys_interface.h"
#include "helpers/physical_io.h"
#include "helpers/debugger/debugger.h"
#include "helpers/cvec.h"

#include "my_texture.h"

#include "audiowrap.h"


#define MAX_IMAGE_VIEWS 5

struct system_io {
    system_io() {
        for (auto & i : p) {
            i.up = i.down = i.left = i.right = nullptr;
            i.fire1 = i.fire2 = i.fire3 = nullptr;
            i.fire4 = i.fire5 = i.fire6 = nullptr;
            i.shoulder_right = i.shoulder_left = nullptr;
            i.start = i.select = nullptr;
        }
        ch_power = ch_reset = ch_pause = nullptr;
    }
    struct CDKRKR {
        HID_digital_button* up;
        HID_digital_button* down;
        HID_digital_button* left;
        HID_digital_button* right;
        HID_digital_button* fire1;
        HID_digital_button* fire2;
        HID_digital_button* fire3;
        HID_digital_button* fire4;
        HID_digital_button* fire5;
        HID_digital_button* fire6;
        HID_digital_button* shoulder_left;
        HID_digital_button* shoulder_right;
        HID_digital_button* start;
        HID_digital_button* select;
    } p[4]{};
    struct kb {
        //SDL_KeyCode
        //HID_digital_button* 1
    };
    HID_digital_button* ch_power;
    HID_digital_button* ch_reset;
    HID_digital_button* ch_pause;
};

enum full_system_states {
    FSS_pause,
    FSS_play
};

struct fssothing {
    ImVec2 uv0, uv1;
    float x_size, y_size;
};

struct W2FORM {
    my_texture tex{};
    bool enabled{};
    debug::waveform2::wf *wf{};
    u32 szpo2{256};
    bool output_enabled{true};
    u32 height{80}, len{200};
    std::vector<u8> drawbuf{};
};

class WFORM {
public:
    my_texture tex{};
    bool enabled{};
    debug_waveform *wf{};
    bool output_enabled{true};
    u32 height{};
    std::vector<u8> drawbuf{};
};

struct W2VIEW {
public:
    debug::waveform2::view *view{};
    std::vector<W2FORM> waveform2s{};
    bool did_textures{};
};

class WVIEW {
public:
    waveform_view *view{};
    std::vector<WFORM> waveforms{};
};

class CVIEW {
public:
    bool enabled{};
    debugger_view *view;
};

class TVIEW {
public:
    bool enabled{};
    debugger_view *view;
};

class IVIEW {
public:
    bool enabled{};
    debugger_view *view{};
    my_texture texture;
};

class DLVIEW {
public:
    bool enabled{};
    debugger_view *view{};
};

class DVIEW {
public:
    debugger_view *view{};
    std::vector<disassembly_entry_strings> dasm_rows{};
};

// Decoded thumbnail data read from a .jsst save slot
struct slot_thumbnail {
    u32 width  = 0;
    u32 height = 0;
    std::vector<u8> rgba; // width * height * 4 RGBA bytes
    std::string timestamp; // ISO-8601 string from meta.ini, may be empty
    bool valid = false;   // true if RGBA pixels were decoded successfully
};

// ── Save-state file format version ───────────────────────────────────────────
// Bump MAJOR on any breaking change to state.bin layout.
// Bump MINOR on additive / backward-compatible changes (new meta keys, etc.).
// Version is written to meta.ini as "version=MAJOR.MINOR".
static constexpr int JSST_VERSION_MAJOR = 1;
static constexpr int JSST_VERSION_MINOR = 1;

struct full_system {
public:
#ifdef JSM_SDLR3
    SDL_Renderer *renderer;
    void platform_setup(SDL_Renderer *mrenderer) {
        renderer = mrenderer;
    }
#endif
#ifdef JSM_SDLGPU
    SDL_GPUDevice *device{};
    void platform_setup(SDL_GPUDevice *device_in) { device = device_in; }
#endif
    jsm_system *sys;
    debugger_interface dbgr{};

    multi_file_set ROMs;
    multi_file_set BIOSes;
    std::vector<WVIEW> waveform_views;
    std::vector<W2VIEW> waveform2_views;
    std::vector<DVIEW> dasm_views;
    std::vector<TVIEW> trace_views;
    std::vector<CVIEW> console_views;

    bool screenshot{};
    bool signal{};
    char bios_override_dir[512]{};  // if non-empty, passed to grab_BIOSes
    char status_msg[256]{};
    double status_msg_time = -9999.0;
    system_io inputs;
    std::vector<JSM_AUDIO_CHANNEL *> audiochans;
    bool has_played_once{};
    bool enable_debugger{};
    u32 worked;

    audiowrap audio{};
    std::vector<u32> available_audio_rates; // cached from probe_available_rates()

    u32 debugger_setup{};

    full_system() {
        sys = nullptr;
        //cvec_ptr_init(&dasm);
        worked = 0;
        run_state = FSS_pause;
    }

    ~full_system();
    full_system_states run_state;

    // ── Input mode (for cores that have both keyboard and controller I/O) ────
    // KEYBOARD : raw keystrokes → emulated keyboard; keyboard-bound joypad off
    // JOYPAD   : keyboard-bound joypad on; keyboard passthrough paused
    // BOTH     : both active simultaneously (for non-conflicting setups)
    enum class InputMode { KEYBOARD = 0, JOYPAD = 1, BOTH = 2 };
    InputMode input_mode = InputMode::KEYBOARD;

    // True when this core needs mode switching (has keyboard IO + at least one controller)
    [[nodiscard]] bool needs_input_mode() const;
    // Cycle KEYBOARD→JOYPAD→BOTH→KEYBOARD
    void cycle_input_mode();

    struct fsio {
        cvec_ptr<physical_io_device> touchscreen{};
        cvec_ptr<physical_io_device> controller1{};
        cvec_ptr<physical_io_device> controller2{};
        cvec_ptr<physical_io_device> controller3{};
        cvec_ptr<physical_io_device> controller4{};
        cvec_ptr<physical_io_device> display{};
        cvec_ptr<physical_io_device> display2{};
        cvec_ptr<physical_io_device> chassis{};
        cvec_ptr<physical_io_device> keyboard{};
        cvec_ptr<physical_io_device> hex_keypad{};
        cvec_ptr<physical_io_device> mouse{};
        cvec_ptr<physical_io_device> cartridge_port{};
        cvec_ptr<physical_io_device> disk_drive{};
        cvec_ptr<physical_io_device> audio_cassette{};
        cvec_ptr<physical_io_device> mem_card{};

        fsio() = default;
    } io{};

    struct {
        float u_overscan, v_overscan;
        fssothing with_overscan;
        fssothing without_overscan;
        JSM_DISPLAY *display;
        my_texture backbuffer_texture;
        void *backbuffer_backer{};
        bool blank_until_next_frame{};
        u64 blank_frame{};

        double x_scale_mult, y_scale_mult;

        bool hide_overscan, nearest_neighbor;
    } output{};

    struct {
        fssothing with_overscan;
        fssothing without_overscan;
        JSM_DISPLAY *display{};
        my_texture backbuffer_texture;
        void *backbuffer_backer{};
        double x_scale_mult{1}, y_scale_mult{1};
        bool nearest_neighbor{};
    } output2{};

    struct {
        memory_view *view{};
    } memory;

    struct {
        source_listing::view *view{};
    } source_listing;

    struct {
        my_texture texture;
        events_view *view{};
    } events;

    std::vector<IVIEW> images;
    std::vector<DLVIEW> dlviews;

    [[nodiscard]] ImVec2 output_size() const;
    [[nodiscard]] ImVec2 output_uv0() const;
    [[nodiscard]] ImVec2 output_uv1() const;
    [[nodiscard]] bool has_second_display() const;
    void setup_display2();
    void present2();

    // Multi-screen layout
    DisplayLayout current_layout{DisplayLayout::TopBottom};
    DisplayLayout fav_layout[2]{DisplayLayout::SideBySide, DisplayLayout::Swapped};
    void cycle_layout();
    void set_layout(DisplayLayout l);
    [[nodiscard]] bool uses_composite_layout() const;
    void load_layout_settings(AppSettings& settings);
    void save_layout_settings(AppSettings& settings);
    void setup_persistent_store(persistent_store &ps, multi_file_set &mfs);
    void setup_persistent_store(persistent_store &ps, const char *path);
    void sync_persistent_storage();
    void close_persistent_storage();
    // Called after insert_media succeeds. Updates ROMs and primes pending_sram
    // so that a subsequent apply_core_options points saves at the inserted media.
    void on_media_inserted(u32 io_index, const char* path, bool is_folder);
    void update_touch(i32 x, i32 y, i32 button_down);
    persistent_store *my_ps{};

    // Save/SRAM directory preferences (synced from AppSettings via apply_core_options)
    bool sram_in_state{false};          // restore SRAM from state zip on load
    bool states_in_saves_dir{false};    // put .jsst files in central saves dir
    bool sram_in_saves_dir{false};      // put .sram files in central saves dir
    bool universal_memcard{false};      // use a single shared memory card file

    std::string state_save_dir{};       // computed path for save-state files
    std::string sram_save_dir{};        // computed path for SRAM files

    // Deferred SRAM setup: load_current_ROMs records the store; apply_core_options
    // calls setup_persistent_store once the correct path is known.
    struct PendingSRAM {
        persistent_store *ps{};
        bool              valid{false};
    } pending_sram;
    void setup_system(jsm::systems which, AppSettings *settings = nullptr);
    u32  setup_system_from_path(jsm::systems which, const char *path, bool is_folder,
                                AppSettings *settings = nullptr);
    // Boot the system into its BIOS/shell without inserting any media.
    void setup_system_bios_only(jsm::systems which, AppSettings *settings = nullptr);
    void destroy_system();
    void save_state_to_slot(int slot);

    // Set by load_state_from_slot when the saved version != current version.
    // The GUI renders a one-shot warning modal then clears these.
    bool load_version_warned = false;
    char load_version_str[32]{};  // e.g. "1.0" or "(none)" for old saves
    bool load_bios_warned = false;
    char load_bios_warning_str[512]{};

    // GUI-triggered chassis button pulses, indexed by JKEYS (DBCID) value.
    // Set by the Play-window chassis UI; consumed (and cleared) by update_input
    // before the core runs.  Momentary buttons pulse for one frame; switches
    // use this to signal a toggle request.
    bool chassis_gui_pulse[DBCID_end + 1]{};

    // ── Media window ──────────────────────────────────────────────────────────
    // Currently loaded path for each media IO device, keyed by index in sys->IOs.
    // Set on initial load and updated whenever media is changed via the GUI.
    std::map<u32, std::string> media_paths;

    // Cassette deck transport state
    enum class CassetteState { Stopped, Playing } cassette_state = CassetteState::Stopped;

    // Insert medium from a file (or folder) into the given IO device.
    // Constructs the multi_file_set, calls the core's insert callback, stores path.
    // Returns true on success.
    bool insert_media(physical_io_device& pio, u32 io_index, const char* path, bool is_folder);

    // Eject / remove the medium in the given IO device
    // (calls unload_cart / remove_disc / remove_tape as appropriate).
    void eject_media(physical_io_device& pio, u32 io_index);

    // Set the initial media path after the system loads its first ROM.
    // Finds the first cart / disc / cassette slot and records the path.
    void set_initial_media_path(const char* path);
    bool supports_runtime_sideload() const;
    bool sideload_file(const char* path);
    bool can_hide_overscan() const;
    bool should_hide_overscan() const;

    struct present_widget {
        char ini_key[64]{};
        debugger_widget widget;
    };
    std::vector<present_widget> present_widgets;
    void setup_present_widgets(AppSettings& settings, const char* core_key);
    void load_state_from_slot(int slot);
    bool slot_has_save(int slot) const;
    // Read screenshot + timestamp from a saved slot (does not load state)
    bool read_slot_thumbnail(int slot, slot_thumbnail& out) const;
    void do_frame();

    // Load saved core options / controller-connected state from ini into the
    // live sys->options array and fire the corresponding callbacks.
    void apply_core_options(AppSettings& settings, const char* core_key);

    // True once at least one audio stream has been registered for this core.
    bool has_audio()         const { return audio.ok; }
    // Stereo pairs currently queued in the first (reference) stream.
    u32  audio_ring_queued() const { return audio.first_stream_queued(); }
    // Target stereo-pair count for the timing loop (~3.5 frames of the first stream).
    u32  audio_ring_target() const {
        u32 sr = audio.first_stream_sample_rate();
        return (audio.fps > 0.0f && sr > 0)
            ? (u32)((float)sr / audio.fps * 3.5f)
            : 2048u;
    }

    void check_new_frame();
    void update_fps_meter(double now);
    void reset_fps_meter();
    double actual_fps{};
    void discard_audio_buffers();
    void begin_fastforward();
    void end_fastforward();
    void advance_time(u32 cycles, u32 scanlines, u32 frames);
    struct {
        u64 cycles = {};
        u32 scanlines = {};
        u64 frames = 0xFFFFFFFFFFFFFFFF;
    } int_time = {};
    struct {
        u64 frame{};
        double time{};
        u64 history_frames[240]{};
        double history_times[240]{};
        u32 history_head{};
        u32 history_count{};
        bool initialized{};
    } fps_meter = {};
    framevars get_framevars() const;
    void present();
    void clear_output_screen();
    void blank_output_until_next_frame();
    void take_screenshot(void *where, u32 buf_width, u32 buf_height);
    void events_view_present();
    void pre_events_view_present();
    void waveform_view_present(WVIEW &wv);
    void waveform2_wf_present(W2FORM& wf);
    bool draw_waveform2(W2FORM& wf);
    void image_view_present(debugger_view &dview, my_texture &tex);
    void setup_audio(AppSettings *settings = nullptr);
    void setup_tracing();
private:
    void clear_runtime_state();
    void load_current_ROMs();
    void debugger_pre_frame();
    void debugger_pre_frame_waveforms(waveform_view &wv);
    void setup_ios();
    void load_default_ROM();
    void setup_bios();
    void get_savestate_filename(char *pth, size_t sz);
    void get_slot_path(char *pth, size_t sz, int slot) const;
    void get_slot_path_variant(char *pth, size_t sz, int slot, bool legacy) const;
    bool resolve_slot_path_for_load(char *pth, size_t sz, int slot, bool *wrong_system) const;
    void capture_screenshot_png(std::vector<u8>& out);
    void setup_display();
    void setup_debugger_interface();
    void add_trace_view(u32);
    void add_dbglog_view(u32);
    void add_console_view(u32);
    void add_disassembly_view(u32);
    void add_image_view(u32);
    void add_waveform_view(u32 idx);
    void add_waveform2_view(u32 idx);
    void waveform_view_present(debugger_view &dview, WFORM &wf);
    void waveform2_view_present(debugger_view &dview, WFORM &wf);
    void w2_create_node_texture(W2VIEW &myv, debug::waveform2::view_node *node, bool force_create);
};

void newsys(full_system *fsys);
void GET_HOME_BASE_SYS(char *out, size_t out_sz, jsm::systems which, const char* sec_path, u32 *worked);
