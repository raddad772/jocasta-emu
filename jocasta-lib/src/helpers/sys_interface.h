#pragma once

#include <helpers/sys_interface.h>
#include <helpers/enums.h>
#include <helpers/physical_io.h>
#include <helpers/audio_ring.h>
#include <helpers/serialize/serialize.h>
#include <vector>

#include "debugger/debugger.h"

// ─── Per-core configurable option ────────────────────────────────────────────
// Cores push these into jsm_system::options during describe_io().
// The GUI saves/loads the current value via AppSettings and notifies the core
// through jsm_system::option_changed() whenever a value is committed.

struct jsm_core_option_choice {
    char label[64]{};
    i32  value{};
};

struct jsm_core_option {
    static constexpr int MAX_CHOICES = 8;

    char key[64]{};    // ini storage key,  e.g. "arm7_mode"
    char label[64]{};  // display label,    e.g. "ARM7 CPU Mode"

    enum Kind { OPTION_ENUM, OPTION_BOOL, OPTION_STRING } kind = OPTION_ENUM;

    jsm_core_option_choice choices[MAX_CHOICES]{};
    int num_choices = 0;
    i32 value = 0;     // current value (for BOOL: 0/1; for ENUM: the choice value)
    char str_value[256]{};   // current value for OPTION_STRING

    void add_choice(const char* lbl, i32 val) {
        if (num_choices >= MAX_CHOICES) return;
        snprintf(choices[num_choices].label, sizeof(choices[0].label), "%s", lbl);
        choices[num_choices].value = val;
        num_choices++;
    }
    // Returns the index of the choice whose .value == this->value (or 0)
    int choice_index() const {
        for (int i = 0; i < num_choices; i++)
            if (choices[i].value == value) return i;
        return 0;
    }
};

// ─────────────────────────────────────────────────────────────────────────────

struct framevars {
    u64 master_frame{};
    i32 x{};
    u32 scanline{};
    u32 last_used_buffer{};
    u64 master_cycle{};
};

struct deserialize_ret {
    char reason[50]{};
    u32 success{};
};

// Per-system construction configuration passed before describe_io() runs.
struct system_config {
    char slots[8][64]{};  // card name per slot ("disk2", "empty", etc.)
};

jsm_system* new_system(jsm::systems which, const system_config& cfg = {});

// Returns the option definitions (with default values) for the given system
// kind.  Called internally by new_system() to pre-populate sys->options, but
// also callable without a live system — lets the GUI show and edit options
// for any core even before a ROM is loaded.
void populate_core_options(jsm::systems which, std::vector<jsm_core_option>& out);

struct jsm_system {
public:
    virtual ~jsm_system() = default;

    char label[100]{};

    jsm::systems kind{}; // Which system is it?
    virtual u32 finish_frame() { return 0; };
    virtual u32 finish_scanline() {return 0; };
    virtual u32 step_master(u32) { return 0; };
    virtual void reset() {};
    virtual void load_BIOS(multi_file_set& mfs) {};
    virtual void describe_io() {};
    virtual void get_framevars(framevars& out) {};

    virtual void sideload(multi_file_set& mfs) {};
    virtual void runtime_sideload(multi_file_set& mfs) { sideload(mfs); };

    virtual void set_audio_ring(audio_output_ring* /*ring*/) {}
    // Called by the GUI after populating ring pointers on every HID_AUDIO_CHANNEL PIO.
    // Multi-stream cores override this to scan their IOs and stash ring pointers.
    // Single-stream cores that still use set_audio_ring() don't need to override this.
    virtual void audio_rings_ready() {}
    virtual void play() {};
    virtual void pause() {};
    virtual void stop() {};

    virtual void setup_debugger_interface(debugger_interface &intf) {};

    struct {
        bool save_state=false, load_BIOS=false, sideload=false, set_audio_ring=false;
        u32 max_loaded_files = 0;
        u32 max_loaded_folders = 0;
    } has{};

    virtual void save_state(serialized_state &state) {};
    virtual void load_state(serialized_state &state, deserialize_ret &ret) {};

    // ── Core options ──────────────────────────────────────────────────────────
    // Cores push jsm_core_option entries into `options` during describe_io().
    // The GUI reads them from here, persists values in .ini, and calls
    // option_changed() whenever a value is committed by the user.
    std::vector<jsm_core_option> options{};
    virtual void option_changed(const char* key, i32 value) {}

    // Called when the user toggles a controller port on or off.
    // `port` is 1-based.  The default is port 1 = connected, others not.
    virtual void controller_connected_changed(int port, bool connected) {}

    // Called before describe_io() to configure expansion slots.
    // card_name is a stable string token (e.g. "disk2", "empty").
    virtual void configure_slot(u32 /*slot_num*/, const char* /*card_name*/) {}

    std::vector<physical_io_device> IOs{};
    std::vector<debugger_widget> opts{};
};
