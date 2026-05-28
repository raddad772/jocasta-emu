#include <vector>
#include <algorithm>
#include <memory>
#include <cstring>
#include <cmath>
#include <cassert>
#include <cstdio>

#define MINIAUDIO_IMPLEMENTATION
#include "../vendor/miniaudio/miniaudio.h"

#define CLOWNRESAMPLER_IMPLEMENTATION
#define CLOWNRESAMPLER_STATIC
#include "../vendor/clownresampler/clownresampler.h"

#include "helpers/int.h"
#include "audiowrap.h"

// ── Shared resampler precomputed tables ───────────────────────────────────────

static ClownResampler_Precomputed precomputed;
static bool clown_init_done = false;

static void clown_ensure_init()
{
    if (!clown_init_done) {
        ClownResampler_Precompute(&precomputed);
        clown_init_done = true;
    }
}

// ── Per-stream state ──────────────────────────────────────────────────────────
// ClownResampler_HighLevel_State is not move-constructible, so streams are
// heap-allocated and held through unique_ptr so the vector never needs to
// move them on reallocation.

struct audio_stream {
    audio_output_ring              ring{};
    u32                            sample_rate{};
    float                          mix_volume{1.0f};
    bool                           resample{};
    ClownResampler_HighLevel_State resampler{};

    // ── Lock / fade state ─────────────────────────────────────────────────
    // locked: audio callback outputs silence and does not consume the ring.
    // Streams start locked; prime_and_unlock() clears the lock after priming.
    std::atomic<bool> locked{true};

    // Fade-out: emulator thread sets fade_out_steps > 0 to start a fade.
    // Audio callback decrements by 1 each period; at 0 it locks and drains.
    // fade_out_total is set before fade_out_steps to avoid a data race.
    std::atomic<int>  fade_out_steps{0};
    int               fade_out_total{1};  // read-only in callback

    // Effective volume for the current callback period (audio thread only).
    float             callback_volume{1.0f};
};

struct audiowrap_streams {
    std::vector<std::unique_ptr<audio_stream>> v;
};

// ── Miniaudio device wrapper ──────────────────────────────────────────────────

struct wkrr {
    ma_device_config config{};
    ma_device        device{};
};

// ── Resampling callbacks ──────────────────────────────────────────────────────

struct StreamResamplerData {
    float        *accum;            // start of the stereo float accumulator
    u32           frames_remaining;
    audio_stream *stream;
};

// Pull source samples: drain float ring → i16 pairs for clownresampler.
static size_t stream_rsin_cb(void *ud, cc_s16l *buf, size_t frames)
{
    auto *d = static_cast<StreamResamplerData *>(ud);
    constexpr u32 CHUNK = 128;
    float tmp[CHUNK * 2];
    u32 done = 0;
    while (done < (u32)frames) {
        u32 want = (u32)frames - done;
        if (want > CHUNK) want = CHUNK;
        u32 got = d->stream->ring.pop(tmp, want);
        for (u32 i = 0; i < got; i++) {
            buf[(done + i) * 2 + 0] = (cc_s16l)(tmp[i * 2 + 0] * 32767.0f);
            buf[(done + i) * 2 + 1] = (cc_s16l)(tmp[i * 2 + 1] * 32767.0f);
        }
        done += got;
        if (got < want) break; // ring empty
    }
    return done;
}

// Receive one resampled output frame — accumulate into the float buffer.
static cc_bool stream_rsout_cb(void *ud, const cc_s32f *frame, cc_u8f total_samples)
{
    auto *d = static_cast<StreamResamplerData *>(ud);
    float vol = d->stream->callback_volume; // set per-period by data_callback
    for (cc_u8f i = 0; i < total_samples; i++)
        *d->accum++ += (frame[i] / 32767.0f) * vol;
    return --d->frames_remaining != 0;
}

// ── Direct drain (no resampling, stream already at output rate) ───────────────

static void drain_stream_to_accum(audio_stream &s, float *accum, u32 frames)
{
    constexpr u32 CHUNK = 128;
    float tmp[CHUNK * 2];
    u32 done = 0;
    while (done < frames) {
        u32 want = frames - done;
        if (want > CHUNK) want = CHUNK;
        u32 got = s.ring.pop(tmp, want);
        for (u32 i = 0; i < got; i++) {
            accum[(done + i) * 2 + 0] += tmp[i * 2 + 0] * s.callback_volume;
            accum[(done + i) * 2 + 1] += tmp[i * 2 + 1] * s.callback_volume;
        }
        done += got;
        if (got < want) break;
    }
}

// ── Miniaudio data callback (audio thread) ────────────────────────────────────
// pUserData = audiowrap_streams*, so no private-member access is needed here.

static float g_accum[4096 * 2]; // stereo float accumulator, audio-thread-only

static void data_callback(ma_device *device, void *output,
                          const void * /*input*/, ma_uint32 frame_count)
{
    auto *streams = static_cast<audiowrap_streams *>(device->pUserData);

    memset(g_accum, 0, frame_count * 2 * sizeof(float));

    for (auto &sp : streams->v) {
        audio_stream &s = *sp;

        // ── Locked: output silence, do not consume ring ───────────────────
        if (s.locked.load(std::memory_order_acquire))
            continue;

        // ── Fade-out: compute per-period volume multiplier ─────────────────
        float vol_mult = 1.0f;
        int fade = s.fade_out_steps.load(std::memory_order_relaxed);
        if (fade > 0) {
            vol_mult = (float)fade / (float)s.fade_out_total;
            int new_fade = fade - 1;
            s.fade_out_steps.store(new_fade, std::memory_order_relaxed);
            if (new_fade == 0) {
                // Fade complete — lock and drain; skip this period's output
                s.locked.store(true, std::memory_order_release);
                s.ring.drain();
                continue;
            }
        }
        s.callback_volume = s.mix_volume * vol_mult;

        // ── Normal output: resample or passthrough ────────────────────────
        if (s.resample) {
            StreamResamplerData d{g_accum, frame_count, &s};
            ClownResampler_HighLevel_Resample(&s.resampler, &precomputed,
                                              stream_rsin_cb, stream_rsout_cb, &d);
            // Only warn on partial shortfalls — a 100% shortfall means the ring is
            // completely empty (emulator paused / debug break / not yet running).
            if (d.frames_remaining > 0 && d.frames_remaining < frame_count)
                printf("\nAudio shortfall (resample %u Hz): %u/%u output frames missing",
                       s.sample_rate, d.frames_remaining, frame_count);
        } else {
            u32 avail = s.ring.available();
            drain_stream_to_accum(s, g_accum, frame_count);
            // Only warn when some data arrived but not enough; avail==0 means not running.
            if (avail > 0 && avail < frame_count)
                printf("\nAudio shortfall (passthrough %u Hz): had %u, needed %u (%u missing)",
                       s.sample_rate, avail, frame_count, frame_count - avail);
        }
    }

    // Float accumulator → i16 output with hard clip.
    auto *out = static_cast<i16 *>(output);
    for (u32 i = 0; i < frame_count * 2; i++) {
        float f = g_accum[i];
        if (f >  1.0f) f =  1.0f;
        if (f < -1.0f) f = -1.0f;
        out[i] = (i16)(f * 32767.0f);
    }
}

// ── audiowrap implementation ──────────────────────────────────────────────────

audiowrap::audiowrap()
{
    clown_ensure_init();
    impl = new audiowrap_streams();
}

audiowrap::~audiowrap()
{
    shutdown();
    delete impl;
    impl = nullptr;
}

// ── Device probing ────────────────────────────────────────────────────────────

std::vector<u32> audiowrap::probe_available_rates()
{
    std::vector<u32> rates;

    ma_context ctx;
    ma_context_config ctx_cfg = ma_context_config_init();
    if (ma_context_init(nullptr, 0, &ctx_cfg, &ctx) != MA_SUCCESS) {
        printf("\nAudio probe: context init failed, defaulting to 48000 Hz");
        return {48000};
    }

    ma_device_info info;
    if (ma_context_get_device_info(&ctx, ma_device_type_playback, nullptr, &info) == MA_SUCCESS) {
        for (u32 i = 0; i < info.nativeDataFormatCount; i++) {
            u32 r = info.nativeDataFormats[i].sampleRate;
            if (r > 0 && std::find(rates.begin(), rates.end(), r) == rates.end())
                rates.push_back(r);
        }
    }

    ma_context_uninit(&ctx);

    if (rates.empty()) {
        printf("\nAudio probe: no native formats reported, defaulting to 48000 Hz");
        return {48000};
    }

    // Sort ascending, then move 48000 to front if present
    std::sort(rates.begin(), rates.end());
    auto it = std::find(rates.begin(), rates.end(), 48000u);
    if (it != rates.end()) {
        rates.erase(it);
        rates.insert(rates.begin(), 48000u);
    }

    printf("\nAudio probe: native rates:");
    for (u32 r : rates) printf(" %u", r);
    printf(" Hz");

    return rates;
}

// ── Stream management ─────────────────────────────────────────────────────────

audio_output_ring *audiowrap::add_stream(u32 sample_rate, u32 /*num_channels*/,
                                          float mix_volume, u32 low_pass_filter)
{
    assert(fps > 0.0f && "set audiowrap::fps before calling add_stream()");

    auto up = std::make_unique<audio_stream>();
    audio_stream &s = *up;

    s.sample_rate = sample_rate;
    s.mix_volume  = mix_volume;

    // Ring: next power-of-two >= 4 frames
    u32 spf = (u32)((float)sample_rate / fps + 1.0f);
    s.ring.init(audio_ring_next_pow2(spf * 4 + 16));

    if (sample_rate != output_rate) {
        s.resample = true;
        u32 lpf = (low_pass_filter == 0 || low_pass_filter >= (sample_rate >> 1))
                  ? (sample_rate >> 1) : low_pass_filter;
        // Always 2-channel (stereo interleaved) in the ring.
        ClownResampler_HighLevel_Init(&s.resampler, 2, sample_rate, output_rate, lpf);
        printf("\nAudio stream: resampling %u Hz → %d Hz (lpf %u, vol %.2f)",
               sample_rate, output_rate, lpf, (double)mix_volume);
    } else {
        printf("\nAudio stream: passthrough %u Hz (vol %.2f)",
               sample_rate, (double)mix_volume);
    }

    audio_output_ring *ring_ptr = &s.ring;
    impl->v.push_back(std::move(up));
    ok = true;
    return ring_ptr;
}

void audiowrap::clear_streams()
{
    for (auto &sp : impl->v) sp->ring.destroy();
    impl->v.clear();
    ok = false;
}

void audiowrap::discard_emulated_buffers()
{
    for (auto &sp : impl->v) sp->ring.drain();
}

void audiowrap::lock_all()
{
    if (!impl) return;
    for (auto &sp : impl->v) {
        // Cancel any in-progress fade first
        sp->fade_out_steps.store(0, std::memory_order_relaxed);
        // Lock and drain — audio callback will output silence from here on
        sp->ring.drain();
        sp->locked.store(true, std::memory_order_release);
    }
}

void audiowrap::begin_fadeout(u32 fade_steps)
{
    if (!impl) return;
    if (fade_steps == 0) {
        lock_all();
        return;
    }
    for (auto &sp : impl->v) {
        // Only fade streams that are currently playing (not already locked)
        if (sp->locked.load(std::memory_order_relaxed)) continue;
        sp->fade_out_total = (int)fade_steps;
        // Store after total is written so the callback sees a consistent value
        sp->fade_out_steps.store((int)fade_steps, std::memory_order_release);
    }
}

void audiowrap::prime_and_unlock(float pad_frac)
{
    if (!impl) return;
    for (auto &sp : impl->v) {
        audio_stream &s = *sp;

        if (pad_frac > 0.0f && fps > 0.0f) {
            u32 pad = (u32)(pad_frac * (float)s.sample_rate / fps + 0.5f);
            if (pad > 0) {
                // Peek at the first sample waiting in the ring (at read_pos).
                // The stream is locked so the callback is not consuming.
                u32 r = s.ring.read_pos.load(std::memory_order_relaxed);
                u32 w = s.ring.write_pos.load(std::memory_order_relaxed);
                float tl = 0.0f, tr = 0.0f;
                if (w != r) {
                    u32 idx = (r & s.ring.mask) * 2;
                    tl = s.ring.data[idx];
                    tr = s.ring.data[idx + 1];
                }
                s.ring.prepend_ramp(tl, tr, pad);
            }
        }

        // Reset any stale fade state and unlock
        s.fade_out_steps.store(0, std::memory_order_relaxed);
        s.callback_volume = s.mix_volume;
        s.locked.store(false, std::memory_order_release);
    }
}

void audiowrap::discard_all_but_latest_buffers()
{
    for (auto &sp : impl->v) {
        u32 keep = (fps > 0.0f) ? (u32)((float)sp->sample_rate / fps + 0.5f) : 0u;
        sp->ring.keep_latest(keep);
    }
}

// ── Backend ───────────────────────────────────────────────────────────────────

int audiowrap::init_backend()
{
    if (!impl || impl->v.empty()) return -1;
    if (started) shutdown_backend();

    w = new wkrr();
    w->config = ma_device_config_init(ma_device_type_playback);
    w->config.playback.format    = ma_format_s16;
    w->config.playback.channels  = 2;       // always stereo output
    w->config.sampleRate         = output_rate;
    w->config.dataCallback       = data_callback;
    w->config.pUserData          = impl;    // streams ptr; no private-member access needed
    w->config.periodSizeInFrames = (fps > 0.0f)
                                   ? (u32)roundf((float)output_rate / fps) : 800u;

    if (ma_device_init(nullptr, &w->config, &w->device) != MA_SUCCESS) {
        printf("\nAudio: device init failed");
        delete w;
        w = nullptr;
        return -1;
    }

    ma_device_start(&w->device);
    started = true;
    return 0;
}

void audiowrap::shutdown_backend()
{
    if (started && w) {
        ma_device_stop(&w->device);
        started = false;
    }
    if (w) {
        ma_device_uninit(&w->device);
        delete w;
        w = nullptr;
    }
}

void audiowrap::shutdown()
{
    shutdown_backend();
    clear_streams();
    fps = 0.0f;
}

// ── State queries ─────────────────────────────────────────────────────────────

u32 audiowrap::first_stream_sample_rate() const
{
    return (impl && !impl->v.empty()) ? impl->v[0]->sample_rate : 0u;
}

u32 audiowrap::first_stream_queued() const
{
    return (impl && !impl->v.empty()) ? impl->v[0]->ring.queued() : 0u;
}
