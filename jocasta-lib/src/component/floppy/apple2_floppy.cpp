#include <cassert>
#include <cctype>
#include <cstring>
#include <cstdio>

#include <cmath>

#include "helpers/jsm_string.h"

#include "apple2_floppy.h"

namespace floppy::apple2 {

static bool ext_is(const char *fname, const char *ext)
{
    if (!fname || !ext) return false;
    size_t flen = strlen(fname);
    size_t elen = strlen(ext);
    if (elen > flen) return false;
    const char *tail = fname + flen - elen;
    for (size_t i = 0; i < elen; i++)
        if (tolower((unsigned char)tail[i]) != tolower((unsigned char)ext[i]))
            return false;
    return true;
}

static constexpr u8 gcr6fw_tb[0x40] =
        {
                0x96, 0x97, 0x9a, 0x9b, 0x9d, 0x9e, 0x9f, 0xa6,
                0xa7, 0xab, 0xac, 0xad, 0xae, 0xaf, 0xb2, 0xb3,
                0xb4, 0xb5, 0xb6, 0xb7, 0xb9, 0xba, 0xbb, 0xbc,
                0xbd, 0xbe, 0xbf, 0xcb, 0xcd, 0xce, 0xcf, 0xd3,
                0xd6, 0xd7, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde,
                0xdf, 0xe5, 0xe6, 0xe7, 0xe9, 0xea, 0xeb, 0xec,
                0xed, 0xee, 0xef, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6,
                0xf7, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff,
        };

static constexpr u32 TRACK_BITS = 50992;
static constexpr u32 NIB_TRACK_BYTES = 6656;
static constexpr u32 SECTOR_ORDER_DSK = 0;
static constexpr u32 SECTOR_ORDER_DO = 1;
static constexpr u32 SECTOR_ORDER_PO = 2;

static void write_byte(generic::TRACK<16, 3328> &track, u8 v)
{
    track.encoded_data.write_bits(8, v);
}

static void write_bit(generic::TRACK<16, 3328> &track, u8 v)
{
    track.encoded_data.write_bits(1, v);
}

static void write_selfsync(generic::TRACK<16, 3328> &track)
{
    track.encoded_data.write_bits(10, 0x3FC);
}

static void write_gap(generic::TRACK<16, 3328> &track, u32 count)
{
    while(count--)
        write_selfsync(track);
}

static void write_oddeven(generic::TRACK<16, 3328> &track, u8 v)
{
    write_byte(track, 0xAA | ((v >> 1) & 0x55));
    write_byte(track, 0xAA | ((v >> 0) & 0x55));
}

static void write_user_data_62(generic::TRACK<16, 3328> &track, const u8 *user_data)
{
    u8 buf[342] = {};

    for(u32 i = 0; i < 256; i++) {
        u32 lb = user_data[i] & 3;
        lb = ((lb << 1) | (lb >> 1)) & 3;
        buf[i % 86] |= lb << ((i / 86) * 2);
        buf[86 + i] = user_data[i] >> 2;
    }

    u8 prev = 0;
    for(u32 i = 0; i < 342; i++) {
        write_byte(track, gcr6fw_tb[prev ^ buf[i]]);
        prev = buf[i];
    }
    write_byte(track, gcr6fw_tb[prev]);
}

static void write_address_field(generic::TRACK<16, 3328> &track, u8 volume, u8 track_num, u8 sector_num)
{
    write_byte(track, 0xFF);
    write_byte(track, 0xD5);
    write_byte(track, 0xAA);
    write_byte(track, 0x96);
    write_oddeven(track, volume);
    write_oddeven(track, track_num);
    write_oddeven(track, sector_num);
    write_oddeven(track, volume ^ track_num ^ sector_num);
    write_byte(track, 0xDE);
    write_byte(track, 0xAA);
    write_byte(track, 0xEB);
}

static void write_data_field(generic::TRACK<16, 3328> &track, const u8 *user_data)
{
    write_byte(track, 0xD5);
    write_byte(track, 0xAA);
    write_byte(track, 0xAD);
    write_bit(track, 0);
    write_user_data_62(track, user_data);
    write_byte(track, 0xDE);
    write_byte(track, 0xAA);
    write_byte(track, 0xEB);
}

static void write_sector(generic::TRACK<16, 3328> &track, u8 volume, u8 track_num, generic::SECTOR &sector)
{
    write_address_field(track, volume, track_num, sector.sector);
    write_gap(track, 6);
    write_byte(track, 0xFF);
    write_bit(track, 0);
    write_data_field(track, sector.data);
    write_gap(track, 18);
}

void DISC::save()
{
}

bool DISC::load(const char* fname, BUF &b)
{
    if ((b.size == 35 * NIB_TRACK_BYTES) || ext_is(fname, ".nib")) {
        return load_nib(b);
    }
    if (b.size == 35 * 16 * 256) {
        if (ext_is(fname, ".po")) return load_plain(b, SECTOR_ORDER_PO);
        if (ext_is(fname, ".do")) return load_plain(b, SECTOR_ORDER_DO);
        return load_plain(b, SECTOR_ORDER_DO);
    }
    return false;
}

void DISC::fill_tracks()
{
    u64 track_radius = 395000 + 1875;
    for (u32 i = 0; i < 35; i++) {
        auto &track = disc.tracks[i];
        track.unencoded_data.allocate(16 * 256);
        track_radius -= 1875;
        track.id = i;
        track.head = 0;
        track.radius_mm = static_cast<double>(track_radius) / 1000.0;
        track.length_mm = track.radius_mm * track.radius_mm * M_PI;
        track.rpm = 300;

        for (u32 s = 0; s < 16; s++) {
            auto &sector = track.sectors[s];
            sector.track = track.id;
            sector.head = track.head;
            sector.sector = s;
            sector.info = 0;
            sector.tag = nullptr;
            sector.data = static_cast<u8 *>(track.unencoded_data.ptr) + (s * 256);
        }
    }
}

bool DISC::load_nib(BUF &b)
{
    if (b.size < 35 * NIB_TRACK_BYTES) return false;

    fill_tracks();
    auto *buf_ptr = static_cast<u8 *>(b.ptr);

    for (u32 i = 0; i < 35; i++) {
        auto &track = disc.tracks[i];
        track.encoded_data.write.pos = 0;
        for (u32 n = 0; n < NIB_TRACK_BYTES; n++)
            write_byte(track, *buf_ptr++);
        track.encoded_data.num_bits = track.encoded_data.write.pos;
    }
    return true;
}

bool DISC::load_plain(BUF &b, u32 sector_order)
{
    if (b.size < 35 * 16 * 256) return false;

    static constexpr u8 dos_order[16] = {
        0x0, 0x7, 0xE, 0x6, 0xD, 0x5, 0xC, 0x4, 0xB, 0x3, 0xA, 0x2, 0x9, 0x1, 0x8, 0xF
    };

    printf("\nDISC SIZE: %ldb %ldKb", b.size, b.size >> 10);
    auto *buf_ptr = static_cast<u8 *>(b.ptr);

    fill_tracks();

    for (u32 i = 0; i < 35; i++) {
        auto &track = disc.tracks[i];
        memcpy(track.unencoded_data.ptr, buf_ptr, 16 * 256);
        buf_ptr += 16 * 256;

        auto *track_data = static_cast<u8 *>(track.unencoded_data.ptr);
        for (u32 s = 0; s < 16; s++) {
            auto &sector = track.sectors[s];
            u32 image_sector = s;
            if (sector_order == SECTOR_ORDER_DO) image_sector = dos_order[s];
            if (sector_order == SECTOR_ORDER_PO) image_sector = ((s & 1) << 3) + (s >> 1);
            sector.data = track_data + (image_sector * 256);
        }

        encode_track(track);
    }
    return true;
}

void DISC::encode_track(generic::TRACK<16, 3328> &track)
{
    // Physical sector order around the track for DOS 3.3 interleave.
    // This is the inverse permutation of dos_order (physical pos -> sector num).
    static constexpr u8 phys_order[16] = {
        0, 13, 11, 9, 7, 5, 3, 1, 14, 12, 10, 8, 6, 4, 2, 15
    };

    track.encoded_data.write.pos = 0;

    write_gap(track, 40);
    for(u32 s = 0; s < 16; s++)
        write_sector(track, 0xFE, track.id, track.sectors[phys_order[s]]);
    while(track.encoded_data.write.pos < TRACK_BITS)
        write_bit(track, 1);
    track.encoded_data.num_bits = track.encoded_data.write.pos;
}

}
