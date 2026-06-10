//
// jocasta-headless: a tiny SDL-free driver for the Jocasta cores.
//
// Purpose (two-fold):
//   1. Let a developer (or an AI assistant) script a core, run it for N frames,
//      inject input at chosen frames, and dump screenshots to /tmp for inspection.
//   2. Be the seed of an automated regression harness (cores -> games -> input
//      "piano roll" -> screenshots at intervals/end).
//
// This first cut is deliberately simple: NO piano-roll file format yet. You build
// a run_config in code (or via the minimal CLI), it inits the system, loads BIOS +
// ROM, runs finish_frame() in a loop while applying timed button presses, and
// writes PNGs via stb_image_write.
//
// Usage:
//   jocasta-headless <ngp|ngpc> <rom-path> [options]
//     --bios <dir>        directory containing bios.ngp / bios.ngc
//     --frames <N>        number of frames to run (default 120)
//     --interval <N>      also screenshot every N frames (0 = only final)
//     --out <prefix>      output path prefix (default /tmp/<core>_<rom-stem>)
//     --press <btn:a:b>   hold <btn> from frame a..b inclusive (repeatable)
//                         btn in {up,down,left,right,a,b,option}
//
// Example: 20 frames idle, hit Option for 5 frames, run to 100, shoot the end:
//   jocasta-headless ngp game.ngp --frames 100 --press option:20:24
//

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "helpers/sys_interface.h"
#include "helpers/enums.h"
#include "helpers/physical_io.h"
#include "helpers/present/sys_present.h"
#include "helpers/audio_ring.h"
#include "helpers/debug.h"                  // ::dbg, dbg_enable_trace()
#include "helpers/debugger/debugger.h"      // debugger_interface
#include "helpers/jsm_string.h"
// NOTE: deliberately NO per-core headers here. The headless harness builds against the generic
// jsm_system interface only, so it can't be broken by churn in any individual core (e.g. NGP/CPU
// header edits). (The old NGP --disasm bus-peek lived here and coupled us to tlcs900h headers.)

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <string>
#include <vector>
#include <algorithm>
#include <filesystem>

namespace {

struct input_window {
    u64 start{}, end{};      // inclusive frame range
    std::string button;      // controller button name
};

struct run_config {
    jsm::systems kind{};
    std::string core_name;   // for output naming ("ngp"/"ngpc")
    std::string rom_path;
    std::string bios_dir;    // directory holding the BIOS file (may be empty)
    u64 frames = 120;
    u64 interval = 0;        // 0 = only the final frame
    std::string out_prefix;  // e.g. /tmp/ngp_mygame
    std::vector<input_window> inputs;
    std::vector<u64> shots;  // exact frames to screenshot (regression "capture points")
    std::vector<std::pair<std::string, int>> options;  // core options via option_changed() (e.g. cached_interp=0)
    std::vector<std::string> views;  // image-view labels (substring match, or "all") to capture per shot
    bool crc = false;        // also print a CRC32 of each captured frame's RGBA (frame hash)
    bool debug = false;      // mirror the GUI with a debug window open (::dbg.do_debug=1)
    bool view_list = false;  // just list the core's image views and exit
};

// ---- small helpers ---------------------------------------------------------

// CRC32 (IEEE, poly 0xEDB88320) -- matches Python zlib.crc32 / binascii.crc32. Used as the
// authoritative per-frame hash so the regression runner can compare frames without decoding PNGs.
u32 crc32_buf(const void *data, size_t len)
{
    static u32 table[256];
    static bool init = false;
    if (!init) {
        for (u32 i = 0; i < 256; i++) {
            u32 c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        init = true;
    }
    u32 crc = 0xFFFFFFFFu;
    const u8 *p = (const u8 *)data;
    for (size_t i = 0; i < len; i++) crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

physical_io_device *find_io(jsm_system *sys, IO_CLASSES kind)
{
    for (auto &pio : sys->IOs)
        if (pio.kind == kind) return &pio;
    return nullptr;
}

// Return the persistent_store a PIO owns (cart flash SRAM or generic NVRAM), or null.
persistent_store *pio_store(physical_io_device &pio)
{
    if (pio.kind == HID_CART_PORT) return &pio.cartridge_port.SRAM;
    if (pio.kind == HID_NVRAM)     return &pio.nvram.store;
    if (pio.kind == HID_MEM_CARD)  return &pio.memcard.store;   // PS1 memory card (game polls it at boot)
    return nullptr;
}

// Ready a store from a file if present (so saves persist across headless runs), else seed it
// from init_data/fill -- the minimal stand-in for the GUI's finish_setup_ps().
void ready_store(persistent_store &ps, const std::string &path)
{
    if (!ps.requested_size || ps.ready_to_use) return;
    ps.data = malloc(ps.requested_size);
    if (FILE *f = fopen(path.c_str(), "rb")) {
        size_t n = fread(ps.data, 1, ps.requested_size, f);
        (void)n; fclose(f);
        printf("  loaded store %s (%llu bytes)\n", path.c_str(), (unsigned long long)ps.requested_size);
    } else {
        memset(ps.data, ps.fill_value, ps.requested_size);
        if (ps.init_data && ps.init_data_size)
            memcpy(ps.data, ps.init_data,
                   ps.init_data_size < ps.requested_size ? ps.init_data_size : ps.requested_size);
        printf("  seeded store %s (%llu bytes)\n", path.c_str(), (unsigned long long)ps.requested_size);
    }
    ps.actual_size  = ps.requested_size;
    ps.ready_to_use = true;
    snprintf(ps.filename, sizeof(ps.filename), "%s", path.c_str());
}

void flush_store(persistent_store &ps)
{
    if (!ps.ready_to_use || !ps.data || !ps.filename[0]) return;
    if (FILE *f = fopen(ps.filename, "wb")) {
        fwrite(ps.data, 1, ps.actual_size, f);
        fclose(f);
        ps.dirty = false;
        printf("  flushed store %s\n", ps.filename);
    }
}

// Split "/a/b/c.ngp" -> dir "/a/b", name "c.ngp", stem "c".
void split_path(const std::string &full, std::string &dir, std::string &name, std::string &stem)
{
    size_t slash = full.find_last_of("/\\");
    if (slash == std::string::npos) { dir = "."; name = full; }
    else { dir = full.substr(0, slash); name = full.substr(slash + 1); }
    size_t dot = name.find_last_of('.');
    stem = (dot == std::string::npos) ? name : name.substr(0, dot);
}

// Fallback native framebuffer dimensions if the display reports none.
void core_dims(jsm::systems kind, u32 &w, u32 &h)
{
    switch (kind) {
        case jsm::systems::NEOGEO_POCKET:
        case jsm::systems::NEOGEO_POCKET_COLOR: w = 160; h = 152; break;
        default:                                w = 256; h = 256; break;
    }
}

// Real visible output dimensions for the current frame, from the display PIO's pixelometry
// (what jsm_present packs into out_buf). Falls back to core_dims if the core reports nothing.
// Clamped to a sane max so a misbehaving core can't overrun the screenshot buffer.
static constexpr u32 MAX_DIM = 2048;  // covers oversampled framebuffers (Genesis 1280, TG16 ~1365, PS1 hires)
void present_dims(physical_io_device *disp, jsm::systems kind, u32 &w, u32 &h)
{
    w = h = 0;
    if (disp) {
        // Variable-resolution cores (PS1/Dreamcast) report the live size here; fixed cores leave it 0.
        w = disp->display.cur_output_width;
        h = disp->display.cur_output_height;
        if (!w || !h) {
            auto &p = disp->display.pixelometry;   // the core's native (often oversampled) framebuffer
            w = p.cols.visible ? p.cols.visible : p.cols.max_visible;
            h = p.rows.visible ? p.rows.visible : p.rows.max_visible;
        }
    }
    if (!w || !h) core_dims(kind, w, h);
    if (w > MAX_DIM) w = MAX_DIM;
    if (h > MAX_DIM) h = MAX_DIM;
}

// Set every controller button to released, then press the ones whose window
// covers `frame`.
void apply_inputs(physical_io_device *pad, const run_config &cfg, u64 frame)
{
    if (!pad) return;
    for (auto &b : pad->controller.digital_buttons) b.state = 0;
    for (auto &win : cfg.inputs) {
        if (frame < win.start || frame > win.end) continue;
        for (auto &b : pad->controller.digital_buttons) {
            if (win.button == b.name) { b.state = 1; break; }
        }
    }
}

// Render the current frame to a PNG (+ optional CRC). Cores with multiple displays (NDS: Top +
// Bottom) are composited stacked vertically into one image (they share a common width). out_w/out_h
// receive the composited size.
void save_shot(jsm_system *sys, const std::vector<physical_io_device *> &disps, const run_config &cfg,
               u64 frame, void *rgba, u32 &out_w, u32 &out_h)
{
    u32 W = 0, y = 0;
    for (auto *d : disps) {
        u32 w = 0, h = 0;
        present_dims(d, cfg.kind, w, h);
        if (W == 0) W = w;                       // common stride = first display's width
        u32 outcols = 0, outrows = 0; bool updated_uv = false;
        // place this display's image at row `y`, stride W -> screens end up stacked top-to-bottom
        jsm_present(sys, cfg.kind, *d, static_cast<u8 *>(rgba) + (size_t)y * W * 4,
                    0, 0, W, h, nullptr, outcols, outrows, updated_uv);
        y += h;
    }
    out_w = W; out_h = y;

    {   // blank-frame stat
        const u32 *px = static_cast<const u32 *>(rgba);
        u32 first = px[0]; bool uniform = true;
        for (u32 i = 1; i < W * y; i++) { if (px[i] != first) { uniform = false; break; } }
        printf("  [shot f%llu] %ux%u uniform=%d px0=%08X\n",
               (unsigned long long)frame, W, y, uniform ? 1 : 0, first);
    }
    if (cfg.crc) {
        u32 c = crc32_buf(rgba, (size_t)W * y * 4);
        printf("CRC f%05llu %08X %ux%u\n", (unsigned long long)frame, c, W, y);
    }
    char path[512];
    snprintf(path, sizeof(path), "%s_f%05llu.png", cfg.out_prefix.c_str(), (unsigned long long)frame);
    if (stbi_write_png(path, (int)W, (int)y, 4, rgba, (int)(W * 4))) printf("  wrote %s\n", path);
    else printf("  FAILED to write %s\n", path);
}

// ---- debug-view capture ----------------------------------------------------
// The GUI renders each core debug window (PS1 VRAM, GBA tiles/sprites/palette, ...) by calling the
// view's update_func into a pow2-square scratch buffer, then uploading the top-left WxH sub-rect to a
// texture. We drive that exact same update_func here, so ANY core's image view is capturable headlessly
// with no per-core code -- the core declares the view, we render and crop it to a PNG.

static u32 next_pow2(u32 v) { u32 p = 1; while (p < v) p <<= 1; return p; }

// Lowercase, replace non-alphanumerics with '_', so a label like "VRAM view" becomes "vram_view".
static std::string slugify(const char *s)
{
    std::string out;
    for (const char *p = s; *p; p++) out += isalnum((unsigned char)*p) ? (char)tolower((unsigned char)*p) : '_';
    return out;
}

// Render every image-view whose label matches one of cfg.views (or all of them when "all"/"*"
// is given) to "<out_prefix>_<label-slug>_fNNNNN.png".
void save_debug_views(debugger_interface &dbgr, const run_config &cfg, u64 frame)
{
    for (auto &dview : dbgr.views) {
        if (dview.kind != dview_image) continue;
        image_view *iv = &dview.image;
        if (!iv->update_func.func || !iv->width || !iv->height) continue;

        bool want = false;
        for (auto &sel : cfg.views) {
            if (sel == "all" || sel == "*" || slugify(iv->label).find(slugify(sel.c_str())) != std::string::npos) {
                want = true; break;
            }
        }
        if (!want) continue;

        const u32 W = iv->width, H = iv->height;
        const u32 szpo2 = next_pow2(std::max(W, H));   // the GUI's square texture stride
        if (!iv->img_buf[0].ptr) {
            iv->img_buf[0].allocate((size_t)szpo2 * szpo2 * 4);
            iv->img_buf[1].allocate((size_t)szpo2 * szpo2 * 4);
        }
        iv->update_func.func(&dbgr, &dview, iv->update_func.ptr, szpo2);
        const u32 *src = static_cast<const u32 *>(iv->img_buf[iv->draw_which_buf].ptr);
        if (!src) continue;

        std::vector<u32> out((size_t)W * H);           // crop pow2-stride scratch down to real WxH
        for (u32 y = 0; y < H; y++) memcpy(&out[(size_t)y * W], src + (size_t)y * szpo2, (size_t)W * 4);

        char path[600];
        snprintf(path, sizeof(path), "%s_%s_f%05llu.png", cfg.out_prefix.c_str(),
                 slugify(iv->label).c_str(), (unsigned long long)frame);
        if (stbi_write_png(path, (int)W, (int)H, 4, out.data(), (int)(W * 4))) printf("  wrote %s\n", path);
        else printf("  FAILED to write %s\n", path);
    }
}

// ---- disc / multi-file ROM loading -----------------------------------------
// Ported from jocasta-gui full_sys.cpp. A PS1 .cue / Dreamcast .gdi is a playlist that references
// track files (.bin) by name; we load the playlist plus every referenced file into the
// multi_file_set, then the core's CDROM_DISC::parse_cue/parse_gdi maps the tracks from those
// buffers. A plain file (cart, .nds) is added as-is. A folder adds all its files (sorted).

bool ends_with_ci(const std::string &s, const char *suf) {
    size_t n = strlen(suf);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; i++)
        if (tolower((unsigned char)s[s.size() - n + i]) != tolower((unsigned char)suf[i])) return false;
    return true;
}

// Add the cue/gdi playlist (as files[0]) plus every track file it names, all from the playlist dir.
void mfs_add_playlist(multi_file_set *mfs, const std::string &path, bool gdi) {
    namespace fs = std::filesystem;
    fs::path fp = path;
    std::string dir = fp.parent_path().string(); if (dir.empty()) dir = ".";
    mfs->clear();
    mfs->add(fp.filename().string().c_str(), dir.c_str());
    if (mfs->files.empty() || mfs->files[0].buf.size == 0) return;
    const char *p = (const char *)mfs->files[0].buf.ptr, *end = p + mfs->files[0].buf.size;
    char line[512];
    auto next = [&]() -> bool {
        if (p >= end) return false;
        while (p < end && (*p == '\n' || *p == '\r')) p++;
        if (p >= end) return false;
        while (p < end && (*p == ' ' || *p == '\t')) p++;
        size_t i = 0;
        while (p < end && *p != '\n' && *p != '\r') { if (i + 1 < sizeof line) line[i++] = *p; p++; }
        line[i] = 0; return true;
    };
    while (next()) {
        if (!line[0]) continue;
        char fn[256] = {0};
        if (gdi) {  // "trackno startlba type sectorsize filename offset"
            int a, b, c; unsigned u;
            if (sscanf(line, "%d %u %d %d %255s %u", &a, &u, &b, &c, fn, &u) == 6) mfs->add(fn, dir.c_str());
        } else {    // cue: FILE "name" BINARY
            if (!strncmp(line, "FILE", 4) && sscanf(line, "FILE \"%255[^\"]\"", fn) == 1) mfs->add(fn, dir.c_str());
        }
    }
}

void mfs_add_folder(multi_file_set *mfs, const std::string &path) {
    namespace fs = std::filesystem;
    mfs->clear();
    std::vector<fs::path> files;
    std::error_code ec;
    for (auto &e : fs::directory_iterator(path, ec)) if (e.is_regular_file()) files.push_back(e.path());
    std::sort(files.begin(), files.end());
    for (auto &f : files) mfs->add(f.filename().string().c_str(), f.parent_path().string().c_str());
}

// Build the multi_file_set for any ROM path: folder, .cue, .gdi, or a plain single file.
bool build_rom_mfs(multi_file_set *mfs, const std::string &path) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (fs::is_directory(path, ec)) { mfs_add_folder(mfs, path); return !mfs->files.empty(); }
    if (!fs::is_regular_file(path, ec)) { printf("ROM not found: %s\n", path.c_str()); return false; }
    if (ends_with_ci(path, ".cue")) { mfs_add_playlist(mfs, path, false); return mfs->files.size() > 0; }
    if (ends_with_ci(path, ".gdi")) { mfs_add_playlist(mfs, path, true);  return mfs->files.size() > 0; }
    fs::path fp = path;
    std::string dir = fp.parent_path().string(); if (dir.empty()) dir = ".";
    mfs->clear();
    mfs->add(fp.filename().string().c_str(), dir.c_str());
    return !mfs->files.empty() && mfs->files[0].buf.size > 0;
}

// Insert the loaded media into whichever port the system exposes, mirroring the GUI's
// load_ROM_into_emu(): a disc drive (PS1/Apple/Mac/DC), an audio cassette (ZX Spectrum), or a
// cartridge port (all carts + TG16-CD, whose load_cart routes a cue to the CD path). Returns the
// port used, or null if the system has no loadable media port.
physical_io_device *load_media(jsm_system *sys, multi_file_set &mfs, const char **kind_out) {
    physical_io_device *disc = nullptr, *tape = nullptr, *cart = nullptr;
    for (auto &pio : sys->IOs) {
        if (pio.kind == HID_DISC_DRIVE && !disc) disc = &pio;
        else if (pio.kind == HID_AUDIO_CASSETTE && !tape) tape = &pio;
        else if (pio.kind == HID_CART_PORT && !cart) cart = &pio;
    }
    if (disc && disc->disc_drive.insert_disc) {
        if (disc->disc_drive.open_drive)  disc->disc_drive.open_drive(sys);
        disc->disc_drive.insert_disc(sys, *disc, mfs);
        if (disc->disc_drive.close_drive) disc->disc_drive.close_drive(sys);
        if (kind_out) *kind_out = "disc";
        return disc;
    }
    if (tape && tape->audio_cassette.insert_tape) {
        tape->audio_cassette.insert_tape(sys, *tape, mfs, nullptr);
        if (kind_out) *kind_out = "cassette";
        return tape;
    }
    if (cart && cart->cartridge_port.load_cart) {
        cart->cartridge_port.load_cart(sys, mfs, *cart);
        if (kind_out) *kind_out = "cart";
        return cart;
    }
    return nullptr;
}

// Default machine/slot config per core (the GUI builds this from user settings). Slot-based
// systems need their cards configured or the relevant IO ports never appear -- e.g. Apple IIe
// needs a Disk II controller in slot 6 or no disc drive exists to insert a .dsk into.
system_config default_config(jsm::systems kind) {
    system_config c{};
    if (kind == jsm::systems::APPLEIIe) {
        const char *slots[8] = {"empty","empty","empty","empty","empty","empty","disk2","empty"};
        for (int i = 0; i < 8; i++) snprintf(c.slots[i], sizeof(c.slots[i]), "%s", slots[i]);
    }
    return c;
}

// Print the controller's digital button names (for authoring input scripts). Only needs
// describe_io (new_system), no BIOS/ROM.
void print_buttons(jsm::systems kind) {
    jsm_system *sys = new_system(kind, default_config(kind));
    if (!sys) { printf("new_system failed\n"); return; }
    for (auto &pio : sys->IOs) {
        if (pio.kind != HID_CONTROLLER) continue;
        printf("buttons:");
        for (auto &b : pio.controller.digital_buttons) printf(" %s", b.name);
        printf("\n");
    }
    delete sys;
}

// ---- the run ---------------------------------------------------------------

std::vector<std::string> bios_files(jsm::systems kind);  // defined below

int run_game(const run_config &cfg)
{
    printf("== %s : %s ==\n", cfg.core_name.c_str(), cfg.rom_path.c_str());

    // The GUI flips ::dbg.do_debug on whenever a debug window is open; that selects a
    // different code path inside finish_frame() (extra waveform-sample events get scheduled
    // and the CPU runs through the do_debug-templated dispatch). Mirror that here so headless
    // can reproduce GUI-only behavior.
    if (cfg.debug) { dbg_enable_trace(); printf("debug: ::dbg.do_debug=1 (GUI debug-window path)\n"); }

    jsm_system *sys = new_system(cfg.kind, default_config(cfg.kind)); // also calls describe_io()
    if (!sys) { printf("new_system failed\n"); return 1; }

    // Audio: the APU pushes unconditionally into its ring, so it must be non-null even though
    // we discard the samples headlessly. The GUI allocates a ring per HID_AUDIO_CHANNEL PIO,
    // stores the pointer back in that PIO, then calls BOTH set_audio_ring() (back-compat for
    // single-stream cores) and audio_rings_ready() (cores grab the ring from their PIO). Do
    // the same so the core sees the exact wiring the GUI gives it.
    audio_output_ring ring;
    ring.init(1 << 15);
    if (physical_io_device *achan = find_io(sys, HID_AUDIO_CHANNEL))
        achan->audio_channel.ring = &ring;
    if (sys->has.set_audio_ring) sys->set_audio_ring(&ring);
    sys->audio_rings_ready();

    // Debugger interface. The GUI always builds this (setup_debugger_interface) before reset,
    // regardless of whether any debug window is visible: the core stashes the interface pointer
    // and allocates its waveform / image / dbglog views. Build the same so the core's finish_frame
    // debug branch has live view caches to read from.
    debugger_interface dbgr;
    sys->setup_debugger_interface(dbgr);

    if (cfg.view_list) {
        printf("image views for this core (match --view by label substring):\n");
        for (auto &dview : dbgr.views) {
            if (dview.kind != dview_image || !dview.image.update_func.func) continue;
            printf("  \"%s\"  (%ux%u)  slug=%s\n", dview.image.label, dview.image.width,
                   dview.image.height, slugify(dview.image.label).c_str());
        }
        return 0;
    }

    // BIOS: per-system file list (most carts need none). Loaded from --bios <dir>.
    {
        std::vector<std::string> need = bios_files(cfg.kind);
        if (need.empty()) {
            printf("no BIOS required for this core\n");
        } else if (cfg.bios_dir.empty()) {
            printf("WARNING: core needs BIOS (%s%s) but no --bios given; may not boot.\n",
                   need[0].c_str(), need.size() > 1 ? ", ..." : "");
        } else {
            multi_file_set bios;
            for (auto &fn : need) bios.add(fn.c_str(), cfg.bios_dir.c_str());
            sys->load_BIOS(bios);
            printf("loaded BIOS from %s (%zu file%s)\n", cfg.bios_dir.c_str(), need.size(), need.size() == 1 ? "" : "s");
        }
    }

    // Media. Kept alive for the whole run: disc cores (PS1) may reference the loaded track buffers.
    // Dispatch to the right port (cart / disc drive / cassette) like the GUI. The cart-load
    // callback may also reset() the core.
    multi_file_set rom;
    if (!build_rom_mfs(&rom, cfg.rom_path)) {
        printf("failed to load ROM %s\n", cfg.rom_path.c_str()); return 1;
    }
    const char *media_kind = "?";
    bool loaded = false;
    if (ends_with_ci(cfg.rom_path, ".exe")) {
        // Raw PS-X EXE (e.g. ps1-tests): sideload into RAM + patch the BIOS shell hook rather than
        // inserting it as a disc. sys->reset() below applies it (sideload_EXE).
        sys->sideload(rom);
        media_kind = "sideload-EXE";
        loaded = true;
    } else {
        loaded = load_media(sys, rom, &media_kind) != nullptr;
    }
    if (!loaded) { printf("no loadable media port (cart/disc/cassette) for this core\n"); return 1; }
    printf("loaded ROM %s (%zu file(s)) via %s port\n", cfg.rom_path.c_str(), rom.files.size(), media_kind);

    // Mirror the GUI: setup_system() loads the ROM (whose cart-load callback already reset the
    // core) and then calls sys->reset() a SECOND time. Do the same here so headless exercises the
    // exact same init sequence -- this is what surfaces the double-schedule_first() bug.
    sys->reset();

    // Ready every persistent store the system exposes, file-backed under the output prefix so
    // saves persist across runs (cart flash -> <prefix>.sram, NGP work RAM -> <prefix>.ram, ...).
    // The cart flash and NGP cpu_ram bind to store.data once ready (ensure_bound / finish_frame).
    for (auto &pio : sys->IOs) {
        persistent_store *ps = pio_store(pio);
        if (!ps || !ps->requested_size || ps->ready_to_use) continue;
        const char *ext = ps->ext[0] ? ps->ext : "sram";
        ready_store(*ps, cfg.out_prefix + "." + ext);
    }

    // Core options (after reset, like the GUI applying saved settings): e.g. cached_interp=0 to
    // run the interpreter instead of the block cache, arm7_mode/arm9_mode for NDS.
    for (auto &o : cfg.options) {
        sys->option_changed(o.first.c_str(), o.second);
        printf("option %s = %d\n", o.first.c_str(), o.second);
    }

    physical_io_device *pad  = find_io(sys, HID_CONTROLLER);
    // Gather ALL displays (NDS has two: Top + Bottom). save_shot stacks them vertically.
    std::vector<physical_io_device *> disps;
    for (auto &pio : sys->IOs) if (pio.kind == HID_DISPLAY) disps.push_back(&pio);
    if (disps.empty()) { printf("no display device\n"); return 1; }

    u32 w = 256, h = 256;
    std::vector<u8> rgba((size_t)MAX_DIM * MAX_DIM * 4);  // 16 MiB scratch, fits any core's framebuffer

    u64 last_frame = (u64)-1;
    u64 stuck = 0;
    for (u64 f = 0; f < cfg.frames; f++) {
        apply_inputs(pad, cfg, f);
        sys->finish_frame();
        ring.drain(); // keep the headless ring from saturating

        framevars fv;
        sys->get_framevars(fv);
        if (fv.master_frame == last_frame) stuck++; else stuck = 0;
        last_frame = fv.master_frame;

        bool want = (cfg.interval && (f % cfg.interval == 0));
        for (u64 s : cfg.shots) if (s == f) { want = true; break; }
        if (want) {
            save_shot(sys, disps, cfg, f, rgba.data(), w, h);
            if (!cfg.views.empty()) save_debug_views(dbgr, cfg, f);
        }

        if (stuck == 1) // report the first time the frame counter fails to move
            printf("  [frame %llu] master_frame stuck at %llu (scanline=%u, cycle=%llu)\n",
                   (unsigned long long)f, (unsigned long long)fv.master_frame,
                   fv.scanline, (unsigned long long)fv.master_cycle);
    }


    framevars fv;
    sys->get_framevars(fv);
    printf("ran %llu frames; final master_frame=%llu scanline=%u cycle=%llu\n",
           (unsigned long long)cfg.frames, (unsigned long long)fv.master_frame,
           fv.scanline, (unsigned long long)fv.master_cycle);

    // Default behavior (ad-hoc use): if no explicit --shot/script captures were requested,
    // grab the final frame so a bare run still produces an image.
    if (cfg.shots.empty() && cfg.frames) {
        save_shot(sys, disps, cfg, cfg.frames - 1, rgba.data(), w, h);
        if (!cfg.views.empty()) save_debug_views(dbgr, cfg, cfg.frames - 1);
    }

    // Flush persistent stores back to disk (cart SRAM if dirtied, NGP work RAM, ...) so a
    // subsequent run loads them -- the headless analogue of close_persistent_storage().
    for (auto &pio : sys->IOs) {
        persistent_store *ps = pio_store(pio);
        if (ps) flush_store(*ps);
    }

    delete sys;
    ring.destroy();
    return 0;
}

// Core name -> system kind. Names match AppSettings::sys_to_cli so the harness can use the
// same identifiers the GUI uses. "auto" defers to extension sniffing on the ROM path.
bool parse_core(const std::string &s, jsm::systems &kind)
{
    using K = jsm::systems;
    static const std::pair<const char *, K> tbl[] = {
        {"gb", K::DMG}, {"dmg", K::DMG}, {"gbc", K::GBC}, {"gba", K::GBA},
        {"nes", K::NES}, {"snes", K::SNES}, {"sg1000", K::SG1000},
        {"sms", K::SMS1}, {"sms2", K::SMS2}, {"gg", K::GG},
        {"genesis", K::GENESIS_USA}, {"genesis-jp", K::GENESIS_JAP}, {"megadrive-pal", K::MEGADRIVE_PAL},
        {"tg16", K::TURBOGRAFX16}, {"atari2600", K::ATARI2600}, {"pv1000", K::CASIO_PV1000},
        {"c64", K::COMMODORE64}, {"zx48", K::ZX_SPECTRUM_48K}, {"zx128", K::ZX_SPECTRUM_128K},
        {"ps1", K::PS1}, {"dreamcast", K::DREAMCAST}, {"nds", K::NDS},
        {"neogeo-aes", K::NEOGEO_AES}, {"neogeo-mvs", K::NEOGEO_MVS},
        {"ngp", K::NEOGEO_POCKET}, {"ngpc", K::NEOGEO_POCKET_COLOR}, {"ngc", K::NEOGEO_POCKET_COLOR},
        {"apple2e", K::APPLEIIe}, {"mac128k", K::MAC128K}, {"mac512k", K::MAC512K}, {"macplus", K::MACPLUS_1MB},
        {"vip2k", K::COSMAC_VIP_2k}, {"vip4k", K::COSMAC_VIP_4k}, {"galaksija", K::GALAKSIJA},
    };
    for (auto &e : tbl) if (s == e.first) { kind = e.second; return true; }
    return false;
}

// Infer the system from a ROM file extension (for core == "auto").
bool ext_to_core(const std::string &rom_path, jsm::systems &kind)
{
    size_t dot = rom_path.find_last_of('.');
    if (dot == std::string::npos) return false;
    std::string e = rom_path.substr(dot + 1);
    for (auto &c : e) c = (char)tolower((unsigned char)c);
    using K = jsm::systems;
    static const std::pair<const char *, K> tbl[] = {
        {"gb", K::DMG}, {"gbc", K::GBC}, {"gba", K::GBA}, {"nes", K::NES},
        {"sfc", K::SNES}, {"smc", K::SNES}, {"md", K::GENESIS_USA}, {"gen", K::GENESIS_USA},
        {"sms", K::SMS1}, {"sg", K::SG1000}, {"gg", K::GG}, {"pce", K::TURBOGRAFX16},
        {"a26", K::ATARI2600}, {"nds", K::NDS}, {"ngp", K::NEOGEO_POCKET}, {"ngc", K::NEOGEO_POCKET_COLOR},
        {"cue", K::PS1}, {"gdi", K::DREAMCAST}, {"tap", K::ZX_SPECTRUM_48K}, {"prg", K::COMMODORE64},
        {"dsk", K::APPLEIIe},
    };
    for (auto &t : tbl) if (e == t.first) { kind = t.second; return true; }
    return false;
}

// BIOS filenames a core needs, loaded from --bios <dir>. Empty list = no BIOS (most carts).
std::vector<std::string> bios_files(jsm::systems kind)
{
    using K = jsm::systems;
    switch (kind) {
        case K::NEOGEO_POCKET:       return {"bios.ngp"};
        case K::NEOGEO_POCKET_COLOR: return {"bios.ngc"};
        case K::DMG:                 return {"gb_bios.bin"};
        case K::GBC:                 return {"gbc_bios.bin"};
        case K::GBA:                 return {"gba_bios.bin"};
        case K::PS1:                 return {"scph1001.bin"};
        case K::NDS:                 return {"bios7.bin", "bios9.bin", "firmware.bin"};
        case K::DREAMCAST:           return {"dc_boot.bin", "dc_flash.bin"};
        case K::NEOGEO_AES:          return {"neo-po.bin"};
        case K::NEOGEO_MVS:          return {"sp-s2.sp1"};
        case K::APPLEIIe:            return {"apple2e.rom", "apple2e_video.rom",
                                             "Apple Disk II 16 Sector Interface Card ROM P5 - 341-0027.bin"};
        case K::ZX_SPECTRUM_48K:     return {"zx48.rom"};
        case K::ZX_SPECTRUM_128K:    return {"zx128.rom"};
        case K::COMMODORE64:         return {"c64 r2.u3", "c64 r2.u4", "c64 r2.u5"};
        case K::MAC128K:             return {"mac128k.rom"};
        case K::MAC512K:             return {"mac512k.rom"};
        case K::MACPLUS_1MB:         return {"macplus.rom"};
        default:                     return {};  // Genesis/NES/SMS/GG/SNES/TG16/Atari/PV1000/SG1000/CHIP-8
    }
}

// Resolve a script path entry: absolute stays, relative is joined under `base` (if given).
std::string resolve_path(const std::string &base, const std::string &p)
{
    if (p.empty() || p[0] == '/') return p;
    if (!base.empty()) return base + "/" + p;
    return p;
}

// File-based run script. One directive per line; '#' starts a full-line comment. Example:
//   core   genesis
//   rom    genesis/Sonic the Hedgehog (USA, Europe).md   # relative to --roms
//   bios   master_system                                 # subdir under --bios-base (optional)
//   frames 300
//   press  start 120 124                                 # hold <button> over frames [a..b]
//   shot   120
//   shot   299
//   crc    on
// `rom`/`bios` relative paths resolve against roms_dir / bios_base. Returns false on error.
bool load_script(const std::string &path, run_config &cfg,
                 const std::string &roms_dir, const std::string &bios_base)
{
    FILE *f = fopen(path.c_str(), "r");
    if (!f) { printf("cannot open script %s\n", path.c_str()); return false; }
    char line[2048];
    bool have_core = false, have_rom = false;
    while (fgets(line, sizeof line, f)) {
        std::string s(line);
        size_t nl = s.find_first_of("\r\n"); if (nl != std::string::npos) s.resize(nl);
        size_t b = s.find_first_not_of(" \t"); if (b == std::string::npos) continue;
        s = s.substr(b);
        if (s.empty() || s[0] == '#') continue;
        size_t sp = s.find_first_of(" \t");
        std::string key = (sp == std::string::npos) ? s : s.substr(0, sp);
        std::string rest;
        if (sp != std::string::npos) {
            size_t rb = s.find_first_not_of(" \t", sp);
            if (rb != std::string::npos) rest = s.substr(rb);
        }
        if (key == "core") {
            if (!parse_core(rest, cfg.kind)) { printf("script: bad core '%s'\n", rest.c_str()); fclose(f); return false; }
            cfg.core_name = rest; have_core = true;
        } else if (key == "rom")      { cfg.rom_path = resolve_path(roms_dir, rest); have_rom = true; }
        else if (key == "bios")       { cfg.bios_dir = resolve_path(bios_base, rest); }
        else if (key == "frames")     { cfg.frames = strtoull(rest.c_str(), nullptr, 10); }
        else if (key == "interval")   { cfg.interval = strtoull(rest.c_str(), nullptr, 10); }
        else if (key == "out")        { cfg.out_prefix = rest; }
        else if (key == "crc")        { cfg.crc = (rest != "off" && rest != "0" && rest != "false"); }
        else if (key == "shot")       { cfg.shots.push_back(strtoull(rest.c_str(), nullptr, 10)); }
        else if (key == "view")       { cfg.views.push_back(rest); }
        else if (key == "option")     { char k[64]; int v = 0;
            if (sscanf(rest.c_str(), "%63s %d", k, &v) == 2) cfg.options.push_back({k, v});
            else printf("script: bad option '%s' (want: option <key> <value>)\n", rest.c_str()); }
        else if (key == "press") {
            char btn[128]; unsigned long long a = 0, e = 0;
            if (sscanf(rest.c_str(), "%127s %llu %llu", btn, &a, &e) == 3) {
                input_window w; w.button = btn; w.start = a; w.end = e; cfg.inputs.push_back(w);
            } else printf("script: bad press '%s' (want: press <button> <start> <end>)\n", rest.c_str());
        } else printf("script: unknown directive '%s'\n", key.c_str());
    }
    fclose(f);
    if (!have_core || !have_rom) { printf("script %s missing core/rom\n", path.c_str()); return false; }
    return true;
}

bool parse_press(const std::string &s, input_window &win)
{
    // format: button:start:end
    size_t c1 = s.find(':');
    if (c1 == std::string::npos) return false;
    size_t c2 = s.find(':', c1 + 1);
    if (c2 == std::string::npos) return false;
    win.button = s.substr(0, c1);
    win.start = strtoull(s.substr(c1 + 1, c2 - c1 - 1).c_str(), nullptr, 10);
    win.end   = strtoull(s.substr(c2 + 1).c_str(), nullptr, 10);
    return true;
}

void usage(const char *argv0)
{
    printf("usage: %s <core|auto> <rom-path> [--bios <dir>] [--frames N]\n"
           "          [--interval N] [--out <prefix>] [--debug] [--press btn:start:end]...\n"
           "    core: gb gbc gba nes snes genesis tg16 sms gg sg1000 atari2600 pv1000 c64\n"
           "          zx48 zx128 ps1 dreamcast nds ngp ngpc neogeo-aes neogeo-mvs ...\n"
           "          'auto' infers the core from the ROM file extension\n"
           "    --debug   mirror the GUI with a debug window open (::dbg.do_debug=1)\n"
           "    --view <label>   also capture the named debug image-view (substring match, or 'all')\n"
           "                     at each shot, to <out>_<label>_fNNNNN.png. Repeatable.\n"
           "    --view-list      list this core's debug image-views and exit\n", argv0);
}

} // namespace

int main(int argc, char **argv)
{
    setvbuf(stdout, nullptr, _IONBF, 0); // unbuffered so progress shows live
    if (argc < 2) { usage(argv[0]); return 1; }

    run_config cfg;
    std::string script, roms_dir, bios_base;
    std::vector<std::string> pos;  // positional core + rom (non-script mode)

    // Single pass collects flags; the script (if any) is loaded after so CLI flags override it.
    struct deferred { std::string out, bios; u64 frames = 0, interval = 0; bool has_frames = false,
        has_interval = false, crc = false, has_crc = false; std::vector<u64> shots; std::vector<input_window> press;
        std::vector<std::pair<std::string,int>> options; std::vector<std::string> views; } cli;
    bool dbg = false, list_buttons = false, view_list = false;

    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&](const char *what) -> std::string {
            if (i + 1 >= argc) { printf("missing value for %s\n", what); exit(1); }
            return argv[++i];
        };
        if      (a == "--script")     script    = next("--script");
        else if (a == "--roms")       roms_dir  = next("--roms");
        else if (a == "--bios-base")  bios_base = next("--bios-base");
        else if (a == "--bios")       cli.bios  = next("--bios");
        else if (a == "--frames")   { cli.frames = strtoull(next("--frames").c_str(), nullptr, 10); cli.has_frames = true; }
        else if (a == "--interval") { cli.interval = strtoull(next("--interval").c_str(), nullptr, 10); cli.has_interval = true; }
        else if (a == "--out")        cli.out = next("--out");
        else if (a == "--debug")      dbg = true;
        else if (a == "--buttons")    list_buttons = true;
        else if (a == "--crc")      { cli.crc = true; cli.has_crc = true; }
        else if (a == "--shot")       cli.shots.push_back(strtoull(next("--shot").c_str(), nullptr, 10));
        else if (a == "--option")   { std::string kv = next("--option"); size_t eq = kv.find('=');
            if (eq != std::string::npos) cli.options.push_back({kv.substr(0, eq), atoi(kv.substr(eq+1).c_str())});
            else printf("bad --option (want key=value)\n"); }
        else if (a == "--press")    { input_window w; if (parse_press(next("--press"), w)) cli.press.push_back(w);
                                      else printf("bad --press (want btn:start:end)\n"); }
        else if (a == "--view")       cli.views.push_back(next("--view"));
        else if (a == "--view-list")  view_list = true;
        else if (a.rfind("--", 0) == 0) { printf("unknown option '%s'\n", a.c_str()); usage(argv[0]); return 1; }
        else pos.push_back(a);
    }

    // Establish core + rom: from --script file, else from positional <core|auto> <rom>.
    if (!script.empty()) {
        if (!load_script(script, cfg, roms_dir, bios_base)) return 1;
    } else {
        if (pos.size() < (list_buttons ? 1u : 2u)) { usage(argv[0]); return 1; }
        cfg.core_name = pos[0]; cfg.rom_path = pos.size() > 1 ? pos[1] : "";
        bool ok = (cfg.core_name == "auto") ? ext_to_core(cfg.rom_path, cfg.kind)
                                            : parse_core(cfg.core_name, cfg.kind);
        if (!ok) { printf("unknown core '%s' (or unrecognized ROM extension)\n", cfg.core_name.c_str());
                   usage(argv[0]); return 1; }
    }

    if (list_buttons) { print_buttons(cfg.kind); return 0; }

    // CLI flags override the script.
    cfg.debug = dbg;
    if (!cli.out.empty())   cfg.out_prefix = cli.out;
    if (!cli.bios.empty())  cfg.bios_dir   = cli.bios;
    if (cli.has_frames)     cfg.frames     = cli.frames;
    if (cli.has_interval)   cfg.interval   = cli.interval;
    if (cli.has_crc)        cfg.crc        = cli.crc;
    for (u64 s : cli.shots) cfg.shots.push_back(s);
    for (auto &p : cli.press) cfg.inputs.push_back(p);
    for (auto &o : cli.options) cfg.options.push_back(o);
    for (auto &v : cli.views) cfg.views.push_back(v);
    cfg.view_list = view_list;

    if (cfg.out_prefix.empty()) {
        std::string dir, name, stem;
        split_path(cfg.rom_path, dir, name, stem);
        cfg.out_prefix = "/tmp/" + (cfg.core_name.empty() ? "rom" : cfg.core_name) + "_" + stem;
    }

    return run_game(cfg);
}
