#pragma once

#include <vector>
#include "helpers/int.h"
#include "helpers/audio_ring.h"

struct wkrr;
struct audiowrap_streams; // pimpl — keeps clownresampler out of this header

// ─────────────────────────────────────────────────────────────────────────────
// audiowrap  —  multi-stream audio output
//
// Usage when loading a core:
//   1. audio.fps = display_fps;
//   2. audio.output_rate = <rate from settings>;
//   3. For each HID_AUDIO_CHANNEL PIO:
//        chan.ring = audio.add_stream(chan.sample_rate, chan.num,
//                                    chan.mix_volume, chan.low_pass_filter);
//   4. audio.init_backend()
//
// Each stream is independently resampled from its native sample rate to
// output_rate Hz, multiplied by mix_volume, and summed into the output.
//
// Streams start locked: the audio callback outputs silence until
// prime_and_unlock() is called after the first emulated frame.
//
// On core unload:
//   audio.shutdown()  →  stops backend + releases all streams
// ─────────────────────────────────────────────────────────────────────────────

struct audiowrap {
    audiowrap();
    ~audiowrap();

    // ── Stream management ──────────────────────────────────────────────────
    // Set fps and output_rate before calling add_stream().
    float fps{};
    u32   output_rate{48000}; // target output sample rate; set from settings

    // Add one emulator audio stream.  Returns the ring the core should push into.
    // The new stream starts locked; call prime_and_unlock() to begin playback.
    // Must be called before init_backend().
    audio_output_ring* add_stream(u32 sample_rate, u32 num_channels,
                                  float mix_volume, u32 low_pass_filter = 0);

    // Drop all streams (call before reloading a core).
    void clear_streams();

    void discard_emulated_buffers();       // flush every ring
    void discard_all_but_latest_buffers(); // keep ~1 frame in each ring

    // ── Playback control ───────────────────────────────────────────────────
    // Lock all streams immediately: silence output, drain rings.
    // Call on pause or when about to discard audio (media swap, core load).
    void lock_all();

    // Begin a fade-out over `fade_steps` callback periods (~16 ms each at
    // 60 fps / 48 kHz).  When the fade completes each stream locks and drains.
    // fade_steps == 0 is equivalent to lock_all() (instant silence).
    void begin_fadeout(u32 fade_steps = 5);

    // Prepend a 0→first-sample ramp of pad_frac * (sample_rate / fps) samples
    // to every stream's ring, then unlock all streams to start output.
    // pad_frac == 0 skips the ramp and just unlocks.
    // Call after the first emulated frame has been pushed to the rings.
    void prime_and_unlock(float pad_frac);

    // ── Backend ────────────────────────────────────────────────────────────
    // Initialise and start miniaudio.  add_stream() must have been called first.
    int  init_backend();
    void shutdown_backend();
    bool backend_running() const { return started; }

    // ── Lifecycle ──────────────────────────────────────────────────────────
    void shutdown(); // stop backend + free all streams

    // ── Device probing ─────────────────────────────────────────────────────
    // Query the default playback device's native sample rates.
    // Returns a list sorted with 48000 first (if present), then ascending.
    // Can be called before any stream is added.
    static std::vector<u32> probe_available_rates();

    // ── State queries ──────────────────────────────────────────────────────
    bool ok{}; // true once at least one stream has been added

    // Reference values from the first stream (for timing-loop queries).
    u32 first_stream_sample_rate() const;
    u32 first_stream_queued()      const;

private:
    audiowrap_streams *impl{};
    wkrr              *w{};
    bool               started{};
};
