#include "chd_loader.h"
#include <libchdr/chd.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

static int chd_type_to_mode(const char* t)
{
    if (!strcmp(t, "AUDIO")) return 0;
    if (!strncmp(t, "MODE2", 5)) return 2;
    return 1;
}

static const int CHD_TRACK_PAD = 4;

bool mfs_add_chd_path(multi_file_set* ROMs, const char* path)
{
    chd_file* chd = nullptr;
    chd_error err = chd_open(path, CHD_OPEN_READ, nullptr, &chd);
    if (err != CHDERR_NONE) {
        printf("[CHD] chd_open failed (%s): error %d\n", path, static_cast<int>(err));
        return false;
    }

    const chd_header* hdr = chd_get_header(chd);
    uint32_t hunkbytes = hdr->hunkbytes;
    uint32_t unitbytes = hdr->unitbytes;
    uint32_t hunkcount = hdr->hunkcount;

    if (unitbytes == 0) unitbytes = 2448;
    uint32_t units_per_hunk = hunkbytes / unitbytes;

    struct TrackInfo {
        int number;
        char type[32];
        char subtype[32];
        int frames;
        int pregap;
        int postgap;
        bool is_v2;
    };

    std::vector<TrackInfo> tracks;
    for (int idx = 0; idx < 99; idx++) {
        char meta[256]{};
        uint32_t result_len = 0, result_tag = 0;
        uint8_t result_flags = 0;

        err = chd_get_metadata(chd, CDROM_TRACK_METADATA2_TAG, static_cast<uint32_t>(idx),
                               meta, sizeof(meta),
                               &result_len, &result_tag, &result_flags);
        if (err == CHDERR_NONE) {
            TrackInfo t{};
            t.is_v2 = true;
            char pgtype[32]{}, pgsub[32]{};
            sscanf(meta, CDROM_TRACK_METADATA2_FORMAT,
                   &t.number, t.type, t.subtype, &t.frames,
                   &t.pregap, pgtype, pgsub, &t.postgap);
            tracks.push_back(t);
            continue;
        }

        err = chd_get_metadata(chd, CDROM_TRACK_METADATA_TAG, static_cast<uint32_t>(idx),
                               meta, sizeof(meta),
                               &result_len, &result_tag, &result_flags);
        if (err == CHDERR_NONE) {
            TrackInfo t{};
            t.is_v2 = false;
            sscanf(meta, CDROM_TRACK_METADATA_FORMAT,
                   &t.number, t.type, t.subtype, &t.frames);
            tracks.push_back(t);
            continue;
        }

        break;
    }

    if (tracks.empty()) {
        printf("[CHD] No track metadata found in %s\n", path);
        chd_close(chd);
        return false;
    }

    uint32_t total_sectors = 0;
    for (const auto& t : tracks) {
        uint32_t stored = static_cast<uint32_t>(t.frames);
        stored = (stored + CHD_TRACK_PAD - 1) / CHD_TRACK_PAD * CHD_TRACK_PAD;
        total_sectors += stored;
    }

    std::vector<uint8_t> merged(static_cast<size_t>(total_sectors) * 2352, 0);
    std::vector<uint8_t> hunk_buf(hunkbytes);

    for (uint32_t h = 0; h < hunkcount; h++) {
        err = chd_read(chd, h, hunk_buf.data());
        if (err != CHDERR_NONE) {
            printf("[CHD] Error reading hunk %u: %d\n", h, static_cast<int>(err));
            break;
        }
        for (uint32_t u = 0; u < units_per_hunk; u++) {
            uint32_t sect_idx = h * units_per_hunk + u;
            if (sect_idx >= total_sectors) break;
            const uint8_t* src = hunk_buf.data() + u * unitbytes;
            uint8_t* dst = merged.data() + sect_idx * 2352;
            uint32_t copy_n = (unitbytes < 2352u) ? unitbytes : 2352u;
            memcpy(dst, src, copy_n);
        }
    }

    chd_close(chd);

    ROMs->clear();

    uint32_t sector_offset = 0;
    for (const auto& t : tracks) {
        cd_track_desc d{};
        d.track_no = t.number;
        d.mode = chd_type_to_mode(t.type);
        if (t.is_v2 && t.pregap > 0) {
            d.idx0_file_lba = sector_offset;
            d.idx1_file_lba = sector_offset + static_cast<uint32_t>(t.pregap);
        } else {
            d.idx0_file_lba = sector_offset;
            d.idx1_file_lba = sector_offset;
        }
        ROMs->disc_toc.push_back(d);

        uint32_t stored = static_cast<uint32_t>(t.frames);
        stored = (stored + CHD_TRACK_PAD - 1) / CHD_TRACK_PAD * CHD_TRACK_PAD;
        sector_offset += stored;
    }

    ROMs->add_from_buf("disc.bin", path, merged.data(), merged.size());

    return true;
}
