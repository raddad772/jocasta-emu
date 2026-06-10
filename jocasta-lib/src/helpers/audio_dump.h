#pragma once

#include <cstdio>

#include "helpers/int.h"

#define JSM_AUDIO_DUMP_ENABLE 1
#define JSM_AUDIO_DUMP_PATH "/tmp/jocasta-audio.raw"
#define JSM_AUDIO_DUMP_APPEND 0
#define JSM_AUDIO_DUMP_FLUSH_SAMPLES 16384

struct jsm_audio_dump {
    FILE *f{};
    const char *path{JSM_AUDIO_DUMP_PATH};
    u64 samples_written{};

    ~jsm_audio_dump()
    {
        close();
    }

    void set_path(const char *path_in)
    {
        if (path == path_in) return;
        close();
        path = path_in;
    }

    void close()
    {
        if (!f) return;
        fflush(f);
        fclose(f);
        f = nullptr;
    }

    void flush()
    {
        if (f) fflush(f);
    }

    void write_mono(i16 s)
    {
#if JSM_AUDIO_DUMP_ENABLE
        open();
        if (!f) return;
        write_i16(s);
        wrote_sample();
#endif
    }

    void write_stereo(i16 l, i16 r)
    {
#if JSM_AUDIO_DUMP_ENABLE
        open();
        if (!f) return;
        write_i16(l);
        write_i16(r);
        wrote_sample();
#endif
    }

private:
    void open()
    {
        if (f) return;
        f = fopen(path, JSM_AUDIO_DUMP_APPEND ? "ab" : "wb");
        samples_written = 0;
        if (!f) printf("\nUnable to open audio dump file: %s", path);
    }

    void write_i16(i16 s)
    {
        u16 v = static_cast<u16>(s);
        u8 b[2]{ static_cast<u8>(v), static_cast<u8>(v >> 8) };
        fwrite(b, 1, sizeof(b), f);
    }

    void wrote_sample()
    {
        samples_written++;
#if JSM_AUDIO_DUMP_FLUSH_SAMPLES
        if ((samples_written % JSM_AUDIO_DUMP_FLUSH_SAMPLES) == 0) flush();
#endif
    }
};
