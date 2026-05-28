//
// Created by . on 2/11/26.
//

#pragma once
#include <vector>

#include "helpers/int.h"
#include "helpers/buf.h"
#include "helpers/simplebuf.h"

/*
tracks    99 tracks per disk     (01h..99h) (usually only 01h on Data Disks)
index     99 indices per track   (01h..99h) (rarely used, usually always 01h)
minutes   74 minutes per disk    (00h..73h) (or more, with some restrictions)
seconds   60 seconds per minute  (00h..59h)
sectors   75 sectors per second  (00h..74h)
frames    98 frames per sector
bytes     33 bytes per frame (24+1+8 = data + subchannel + error correction)
*/

enum GDROM_AREA {
    GD_AREA_LOW  = 0,
    GD_AREA_HIGH = 1,
};

enum CDROM_TRACK_MODE {
    CDMODE_AUDIO,
    CDMODE_MODE1,
    CDMODE_MODE2,
};

struct CDROM_cue_track {
    int track_no{};
    CDROM_TRACK_MODE mode{};
    GDROM_AREA area{};
    int file_index{};

    u32 pregap_lba{};
    u32 file_lba{};

    u32 idx0_lba{};
    u32 idx1_lba{};
};

struct GDROM_TOC {
    u8 data[102 * 4]{}; // BIOS expects 99 + leadout
};

struct CDROM_DISC {
    GDROM_TOC gdrom_toc[2];
    simplebuf8 data{};
    bool valid{};
    u32 end_of_last_track{};
    u32 num_tracks{};
    bool parse_cue(multi_file_set &mfs);
    u8 *ptr_to_data(u32 lba);
    void build_gdrom_toc(GDROM_TOC &toc, GDROM_AREA area, multi_file_set &mfs);
    bool parse_gdi(multi_file_set &mfs);
    u32 get_track_from_LBA(u32 LBA);
    void global_CD_time(u32 LBA, u32 &mm, u32 &ss, u32 &ff);
    void track_CD_time(u32 LBA, u32 &track_num, u32 &index, u32 &mm, u32 &ss, u32 &ff);
    std::vector<CDROM_cue_track> tracks{};

    struct {
        u32 track_num=0;
    } parse{};
};

namespace CD {
    inline void lba_to_msf(u32 lba, u8 &m, u8 &s, u8 &f) {
        m = lba / (60 * 75);
        lba %= (60 * 75);
        s = lba / 75;
        f = lba % 75;
    }

    inline u32 msf_to_lba(u32 m, u32 s, u32 f) {
        return (m * 60 * 75) + (s * 75) + f;
    }
}