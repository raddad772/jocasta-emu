//
// Created by . on 2/11/26.
//
#include <cstdio>
#include <cstring>
#include <cassert>
#include "cdrom_formats.h"

void CDROM_DISC::build_gdrom_toc(GDROM_TOC &toc, GDROM_AREA area, multi_file_set &mfs) {
    memset(toc.data, 0, sizeof(toc.data));

    int toc_index = 0;

    for (auto &t : tracks) {
        if (t.area != area)
            continue;

        u8 m, s, f;
        CD::lba_to_msf(t.idx1_lba, m, s, f);

        u8 ctrl = (t.mode == CDMODE_AUDIO) ? 0x00 : 0x04;

        toc.data[toc_index * 4 + 0] = ctrl | 0x01;
        toc.data[toc_index * 4 + 1] = static_cast<u8>(t.track_no);
        toc.data[toc_index * 4 + 2] = m;
        toc.data[toc_index * 4 + 3] = s;
        toc.data[toc_index * 4 + 4] = f;

        toc_index++;
    }

    // Lead-out
    u32 end_lba = 0;
    for (auto &t : tracks) {
        if (t.area != area)
            continue;

        read_file_buf &f = mfs.files[t.file_index];
        u32 sector_size = (t.mode == CDMODE_AUDIO) ? 2352 : 2048;
        u32 sectors = f.buf.size / sector_size;

        u32 end = t.idx1_lba + sectors;
        if (end > end_lba)
            end_lba = end;
    }

    u8 m, s, f;
    CD::lba_to_msf(end_lba, m, s, f);

    toc.data[99 * 4 + 0] = 0x01;
    toc.data[99 * 4 + 1] = 0xAA;
    toc.data[99 * 4 + 2] = m;
    toc.data[99 * 4 + 3] = s;
    toc.data[99 * 4 + 4] = f;
}

static inline u32 msf_to_lba(u32 m, u32 s, u32 f) {
    return m * 60 * 75 + s * 75 + f;
}

u8 *CDROM_DISC::ptr_to_data(u32 lba) {
    u32 offset = lba * 2352;
    if (offset >= data.sz) {
        printf("\nBAD DATA REQ %d", lba);
        return nullptr;
    }
    return data.ptr + offset;
}

bool CDROM_DISC::parse_cue(multi_file_set &mfs) {
    valid = false;

    if (mfs.files.empty())
        return false;

    // Find .cue file
    int cue_index = -1;
    for (size_t i = 0; i < mfs.files.size(); i++) {
        const char *n = mfs.files[i].name;
        if (strstr(n, ".cue") || strstr(n, ".CUE")) {
            cue_index = static_cast<int>(i);
            break;
        }
    }
    if (cue_index < 0)
        return false;

    const char *cue = static_cast<const char *>(mfs.files[cue_index].buf.ptr);
    size_t cue_sz = mfs.files[cue_index].buf.size;

    int current_file = -1;
    CDROM_cue_track *current_track = nullptr;

    const char *p = cue;
    const char *end = cue + cue_sz;

    auto next_line = [&](char *out, size_t out_sz) -> bool {
        if (p >= end)
            return false;

        // Skip line endings from previous read
        while (p < end && (*p == '\n' || *p == '\r'))
            p++;

        if (p >= end)
            return false;

        // Skip leading whitespace
        while (p < end && (*p == ' ' || *p == '\t'))
            p++;

        size_t i = 0;
        while (p < end && *p != '\n' && *p != '\r') {
            if (i + 1 < out_sz)
                out[i++] = *p;
            p++;
        }
        out[i] = 0;

        return true;
    };

    char line[512];

    while (next_line(line, sizeof(line))) {
        //printf("\nLINE (%ld): %s", strlen(line), line);
        if (!line[0]) continue;
        // FILE "foo.bin" BINARY
        if (!strncmp(line, "FILE", 4)) {
            char fname[256]{};
            if (sscanf(line, R"(FILE "%255[^"]")", fname) == 1) {
                for (size_t i = 0; i < mfs.files.size(); i++) {
                    if (!strcmp(mfs.files[i].name, fname)) {
                        current_file = static_cast<int>(i);
                        break;
                    }
                }
            }
        }

        // TRACK nn MODE
        else if (!strncmp(line, "TRACK", 5)) {
            int num{};
            char mode[64]{};

            if (sscanf(line, "TRACK %d %63s", &num, mode) == 2) {
                tracks.push_back({});
                current_track = &tracks.back();
                current_track->track_no = num;
                current_track->file_index = current_file;

                if (!strcmp(mode, "AUDIO"))
                    current_track->mode = CDMODE_AUDIO;
                else if (!strcmp(mode, "MODE1/2352"))
                    current_track->mode = CDMODE_MODE1;
                else if (!strcmp(mode, "MODE2/2352"))
                    current_track->mode = CDMODE_MODE2;
                else {
                    printf("\nWARN UNKNOWN MODE TYPE");
                }
            }
        }
        // if PREGAP, generate time til INDEX 01
        // if INDEX01 == 00, generate none, INDEX0 also = 00
        // if INDEX01 @ >0, INDEX0 is implied

        // INDEX 00 mm:ss:ff
        else if (!strncmp(line, "INDEX 00", 8) && current_track) {
            u32 m{}, s{}, f{};
            if (sscanf(line, "INDEX 00 %u:%u:%u", &m, &s, &f) == 3) {
                current_track->pregap_lba += msf_to_lba(m, s, f);
            }
        }

        // INDEX 01 mm:ss:ff
        else if (!strncmp(line, "INDEX 01", 8) && current_track) {
            u32 m{}, s{}, f{};
            if (sscanf(line, "INDEX 01 %u:%u:%u", &m, &s, &f) == 3) {
                current_track->file_lba = msf_to_lba(m, s, f);
            }
        }
        // PREGAP mm:ss:ff
        else if (!strncmp(line, "PREGAP", 6) && current_track) {
            u32 m{}, s{}, f{};
            if (sscanf(line, "PREGAP %u:%u:%u", &m, &s, &f) == 3) {
                current_track->pregap_lba += msf_to_lba(m, s, f);
            }
        }
    }
    //printf("\nNUM TRACKS %ld", tracks.size());
    if (tracks.empty())
        return false;

    num_tracks = tracks.size();

    // Assign absolute LBAs (with global 150 sector pregap)
    u32 cur_lba = 150; // lead-in

    for (size_t i = 0; i < tracks.size(); i++) {
        CDROM_cue_track &t = tracks[i];

        t.idx0_lba = cur_lba;
        t.idx1_lba  = cur_lba + t.pregap_lba + t.file_lba;
        u8 mmm, mms, mmf;
        //lba_to_msf(t.data_lba, mmm, mms, mmf);
        //printf("\nT %ld LBA:%d MSF: %02d:%02d:%02d", i, cur_lba, mmm, mms, mmf);

        read_file_buf &f = mfs.files[t.file_index];
        u32 file_sectors = (u32)(f.buf.size / 2352);

        u32 next_file_lba = file_sectors;
        if (i + 1 < tracks.size() &&
            tracks[i + 1].file_index == t.file_index) {
            next_file_lba = tracks[i + 1].file_lba;
            }

        u32 data_sectors = next_file_lba - t.file_lba;
        cur_lba = t.idx1_lba + data_sectors;
    }

    // Allocate disc buffer
    u32 total_lba = cur_lba;
    end_of_last_track = cur_lba - 1;
    u32 total_size = total_lba * 2352;
    total_size = 74 * 60 * 75 * 2352; // 74min * 60sec * 75sector * 2352 bytes per
    //printf("\nALLOCATE %d BYTES", total_size);
    data.allocate(total_size);
    memset(data.ptr, 0, total_size);

    // Copy track data
    for (auto &t : tracks) {
        read_file_buf &f = mfs.files[t.file_index];
        u8 mm = t.idx0_lba / (60 * 75);
        u8 ss = (t.idx0_lba % (60 * 75)) / 75;
        u8 ff = t.idx0_lba % 75;
        CD::lba_to_msf(t.idx0_lba, mm, ss, ff);
        printf("\nTRACK! DATA_LBA:%d START_LBA:%d mmssff %02d:%02d:%02d", t.idx1_lba, t.idx0_lba, mm, ss, ff);

        u8 *dst = data.ptr + t.idx1_lba * 2352;
        u8 *src = static_cast<u8 *>(f.buf.ptr) + t.file_lba * 2352;

        u32 file_sectors = static_cast<u32>(f.buf.size / 2352);
        u32 copy_lba = file_sectors - t.file_lba;

        memcpy(dst, src, copy_lba * 2352);
    }

    valid = true;
    return true;
}


bool CDROM_DISC::parse_gdi(multi_file_set &mfs) {
    valid = false;
    tracks.clear();

    if (mfs.files.empty())
        return false;

    // Locate .gdi
    int gdi_index = -1;
    for (size_t i = 0; i < mfs.files.size(); i++) {
        const char *n = mfs.files[i].name;
        if (strstr(n, ".gdi") || strstr(n, ".GDI")) {
            gdi_index = (int)i;
            break;
        }
    }
    if (gdi_index < 0)
        return false;

    const char *gdi = (const char *)mfs.files[gdi_index].buf.ptr;
    size_t gdi_sz = mfs.files[gdi_index].buf.size;

    const char *p = gdi;
    const char *end = gdi + gdi_sz;

    auto next_line = [&](char *out, size_t out_sz) -> bool {
        if (p >= end) return false;
        while (p < end && (*p == '\n' || *p == '\r')) p++;
        if (p >= end) return false;

        size_t i = 0;
        while (p < end && *p != '\n' && *p != '\r') {
            if (i + 1 < out_sz)
                out[i++] = *p;
            p++;
        }
        out[i] = 0;
        return true;
    };

    char line[512]{};

    // First line = track count (informational)
    if (!next_line(line, sizeof(line)))
        return false;

    // Parse track lines
    while (next_line(line, sizeof(line))) {
        if (!line[0])
            continue;
        int track_no{};
        u32 start_lba{};
        int type{};
        int sector_size{};
        char fname[256]{};
        u32 file_offset{};

        if (sscanf(line, "%d %u %d %d %255s %u",
                   &track_no,
                   &start_lba,
                   &type,
                   &sector_size,
                   fname,
                   &file_offset) != 6)
            continue;


        int file_index = -1;
        for (size_t i = 0; i < mfs.files.size(); i++) {
            if (!strcmp(mfs.files[i].name, fname)) {
                file_index = (int)i;
                break;
            }
        }
        if (file_index < 0)
            return false;

        tracks.push_back({});
        CDROM_cue_track &t = tracks.back();

        t.track_no   = track_no;
        t.file_index = file_index;
        t.idx0_lba  = start_lba;
        t.idx1_lba   = start_lba;
        t.file_lba   = file_offset / sector_size;
        t.pregap_lba = 0;

        if (type == 0)
            t.mode = CDMODE_AUDIO;
        else
            t.mode = CDMODE_MODE1;

        // Critical: area split at fixed GD boundary
        t.area = (start_lba >= 45000)
                   ? GD_AREA_HIGH
                   : GD_AREA_LOW;
    }

    if (tracks.empty())
        return false;

    num_tracks = (u32)tracks.size();

    // Determine disc size (absolute)
    u32 max_lba = 0;

    for (auto &t : tracks) {
        read_file_buf &f = mfs.files[t.file_index];

        u32 sector_size =
            (t.mode == CDMODE_AUDIO) ? 2352 : 2048;

        u32 sectors = (u32)(f.buf.size / sector_size);
        u32 end_lba = t.idx1_lba + sectors;

        if (end_lba > max_lba)
            max_lba = end_lba;
    }

    end_of_last_track = max_lba - 1;

    // Allocate full 2352-byte-per-sector disc image
    u32 total_size = max_lba * 2352;
    data.allocate(total_size);
    memset(data.ptr, 0, total_size);

    // Copy track data
    for (auto &t : tracks) {
        read_file_buf &f = mfs.files[t.file_index];

        u32 sector_size =
            (t.mode == CDMODE_AUDIO) ? 2352 : 2048;

        u32 sectors = (u32)(f.buf.size / sector_size);

        u8 *src = (u8 *)f.buf.ptr + t.file_lba * sector_size;
        u8 *dst = data.ptr + t.idx1_lba * 2352;

        for (u32 i = 0; i < sectors; i++) {
            if (sector_size == 2352) {
                memcpy(dst, src, 2352);
            } else {
                // MODE1/2048 padded into 2352
                memset(dst, 0, 2352);
                memcpy(dst + 16, src, 2048);
            }
            src += sector_size;
            dst += 2352;
        }
    }
    build_gdrom_toc(gdrom_toc[0], GD_AREA_LOW, mfs);
    build_gdrom_toc(gdrom_toc[1], GD_AREA_HIGH, mfs);
    valid = true;
    return true;
}

void CDROM_DISC::global_CD_time(u32 LBA, u32 &mm, u32 &ss, u32 &ff)
{
    ff = LBA % 75;
    ss = (LBA / 75) % 60;
    mm = LBA / (75 * 60);

}

u32 CDROM_DISC::get_track_from_LBA(u32 LBA) {
    // Find current track...
    //printf("\nTRACK FROM LBA %d", LBA);
    u32 cur_track = 0;
    for (u32 i = 0; i < num_tracks; i++) {
        auto &t = tracks[i];
        if (t.idx0_lba > LBA) break;
        cur_track = i;
    }
    return cur_track;
}

void CDROM_DISC::track_CD_time(u32 LBA, u32 &track_num, u32 &index, u32 &mm, u32 &ss, u32 &ff)
{
    track_num = get_track_from_LBA(LBA);
    i32 track_LBA = tracks[track_num].idx0_lba;
    i32 rel_LBA = (LBA - track_LBA);
    if (rel_LBA < 0) {
        printf("\nERROR RELATIVE LBA <0");
        return;
    }
    ff = rel_LBA % 75;
    ss = (rel_LBA / 75) % 60;
    mm = (rel_LBA / (75 * 60));
    index = LBA < tracks[track_num].idx1_lba ? 0 : 1;
}
