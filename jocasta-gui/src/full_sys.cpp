#include <cassert>
#include <cstdio>
#include <iostream>
#include <filesystem>
#include <cctype>
#include <algorithm>
//#include <SDL.h>
#include <stdlib.h>
#if !defined(_WIN32)
#include <pwd.h>
#include <unistd.h>
#endif
#include <cstring>
#include <cmath>
#include <stdlib.h>
#include <cstdio>
#include <string>
#include <system_error>

#include "build.h"
#include "my_texture.h"
#include "application.h"
#include "helpers/sys_interface.h"
#include "helpers/cvec.h"
#include "helpers/present/sys_present.h"
#include "helpers/debug.h"
#include "helpers/buf.h"
#include "full_sys.h"
#include "helpers/physical_io.h"
#include "helpers/debugger/debugger.h"
#include "helpers/user.h"
#include "miniz.h"
#include "nanosha256.h"
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../vendor/stb_image_write.h"
#define STB_IMAGE_IMPLEMENTATION
#include "../vendor/stb_image.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
//#include "system/gb/gb_enums.h"

// mac overlay - 14742566
// i get to    - 88219648


#ifdef JSM_SDLR3
#define TS(f,a,b,c) f.setup(renderer, a, b, c);
#else
#if defined(JSM_SDLGPU)
#define TS(f,a,b,c) f.setup(device, a, b, c);
#else
#define TS(f,a,b,c) f##.setup(a,b,c);
#endif
#endif



#ifndef MAX
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif

static u32 get_closest_pow2(u32 b)
{
    //u32 b = MAX(w, h);
    u32 out = 128;
    while(out < b) {
        out <<= 1;
        assert(out < 0x40000000);
    }
    return out;
}

static const char *path_basename(const char *path)
{
    const char *slash = strrchr(path, '/');
    const char *bslash = strrchr(path, '\\');
    const char *base = path;
    if (slash && slash + 1 > base) base = slash + 1;
    if (bslash && bslash + 1 > base) base = bslash + 1;
    return base;
}

static bool str_ends_with_zip(const char *path)
{
    size_t len = strlen(path);
    if (len < 4) return false;
    const char *ext = path + len - 4;
    return std::tolower((unsigned char)ext[0]) == '.' &&
           std::tolower((unsigned char)ext[1]) == 'z' &&
           std::tolower((unsigned char)ext[2]) == 'i' &&
           std::tolower((unsigned char)ext[3]) == 'p';
}

static bool str_ends_with_ci(const char *path, const char *suffix)
{
    size_t path_len = strlen(path);
    size_t suffix_len = strlen(suffix);
    if (path_len < suffix_len) return false;

    const char *p = path + path_len - suffix_len;
    for (size_t i = 0; i < suffix_len; i++) {
        if (std::tolower((unsigned char)p[i]) != std::tolower((unsigned char)suffix[i]))
            return false;
    }
    return true;
}

static u32 mfs_add_zip(multi_file_set *mfs, const char *zip_path)
{
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));

    if (!mz_zip_reader_init_file(&zip, zip_path, 0)) {
        printf("\nCould not open zip file %s", zip_path);
        return 0;
    }

    u32 added = 0;
    mz_uint num_files = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < num_files; i++) {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&zip, i, &stat))
            continue;
        if (stat.m_is_directory || stat.m_is_encrypted || !stat.m_is_supported)
            continue;

        size_t uncomp_size = 0;
        void *buf = mz_zip_reader_extract_to_heap(&zip, i, &uncomp_size, 0);
        if (!buf) {
            printf("\nCould not extract %s from %s", stat.m_filename, zip_path);
            continue;
        }

        const char *name = path_basename(stat.m_filename);
        if (name[0]) {
            mfs->add_from_buf(name, zip_path, buf, uncomp_size);
            added++;
        }
        mz_free(buf);
    }

    mz_zip_reader_end(&zip);
    printf("\nLoaded %u file(s) from zip %s", added, zip_path);
    return added;
}

static u32 mfs_add_gdi_path(multi_file_set* ROMs, const char* path)
{
    std::filesystem::path fpath = path;
    std::filesystem::path dir = fpath.parent_path();
    if (dir.empty()) dir = ".";

    ROMs->clear();
    ROMs->add(fpath.filename().string().c_str(), dir.string().c_str());
    if (ROMs->files.size() < 1 || ROMs->files[0].buf.size == 0) return 0;

    const char *gdi = (const char *)ROMs->files[0].buf.ptr;
    size_t gdi_sz = ROMs->files[0].buf.size;
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
    if (!next_line(line, sizeof(line))) return 0;

    while (next_line(line, sizeof(line))) {
        if (!line[0]) continue;

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
        ROMs->add(fname, dir.string().c_str());
    }
    return 1;
}

static u32 mfs_add_cue_path(multi_file_set* ROMs, const char* path)
{
    std::filesystem::path fpath = path;
    std::filesystem::path dir = fpath.parent_path();
    if (dir.empty()) dir = ".";

    ROMs->clear();
    ROMs->add(fpath.filename().string().c_str(), dir.string().c_str());
    if (ROMs->files.size() < 1 || ROMs->files[0].buf.size == 0) return 0;

    const char *cue = static_cast<const char *>(ROMs->files[0].buf.ptr);
    size_t cue_sz = ROMs->files[0].buf.size;
    const char *p = cue;
    const char *end = cue + cue_sz;

    auto next_line = [&](char *out, size_t out_sz) -> bool {
        if (p >= end)
            return false;

        while (p < end && (*p == '\n' || *p == '\r'))
            p++;

        if (p >= end)
            return false;

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
        if (!line[0]) continue;
        if (!strncmp(line, "FILE", 4)) {
            char ffname[256]{};
            if (sscanf(line, R"(FILE "%255[^"]")", ffname) == 1) {
                ROMs->add(ffname, dir.string().c_str());
            }
        }
    }
    return 1;
}

static u32 mfs_add_folder(multi_file_set* ROMs, const char* path)
{
    ROMs->clear();

    namespace fs = std::filesystem;
    std::vector<fs::path> files;
    try {
        for (const auto& entry : fs::directory_iterator(path)) {
            if (entry.is_regular_file())
                files.push_back(entry.path());
        }
    } catch (const fs::filesystem_error& e) {
        std::cerr << "Filesystem error: " << e.what() << '\n';
        return 0;
    }

    std::sort(files.begin(), files.end());
    for (auto &file : files)
        ROMs->add(file.filename().string().c_str(), file.parent_path().string().c_str());

    return ROMs->files.size() > 0;
}

static u32 mfs_add_path(multi_file_set* ROMs, const char* path, bool is_folder)
{
    namespace fs = std::filesystem;

    ROMs->clear();

    if (is_folder || fs::is_directory(path))
        return mfs_add_folder(ROMs, path);

    if (!fs::is_regular_file(path)) {
        printf("\nCould not find ROM path %s", path);
        return 0;
    }

    if (str_ends_with_zip(path))
        return mfs_add_zip(ROMs, path) != 0;
    if (str_ends_with_ci(path, ".gdi"))
        return mfs_add_gdi_path(ROMs, path);
    if (str_ends_with_ci(path, ".cue"))
        return mfs_add_cue_path(ROMs, path);

    fs::path fpath = path;
    fs::path dir = fpath.parent_path();
    if (dir.empty()) dir = ".";

    ROMs->clear();
    ROMs->add(fpath.filename().string().c_str(), dir.string().c_str());
    return ROMs->files.size() > 0 && ROMs->files[0].buf.size > 0;
}

u32 grab_BIOSes(multi_file_set* BIOSes, jsm::systems which, const char* bios_dir_override)
{
    char BIOS_PATH[255];
    char BASE_PATH[255];

    bool using_override = (bios_dir_override && bios_dir_override[0]);
    snprintf(BASE_PATH, sizeof(BASE_PATH), "%s", using_override ? bios_dir_override : ".");

    u32 has_bios = 0;
    switch(which) {
        case jsm::systems::DREAMCAST:
            has_bios = 1;
            if (using_override)
                snprintf(BIOS_PATH, sizeof(BIOS_PATH), "%s", BASE_PATH);
            else
                snprintf(BIOS_PATH, sizeof(BIOS_PATH), "%s/dreamcast", BASE_PATH);
            BIOSes->add("dc_boot.bin", BIOS_PATH);
            BIOSes->add("dc_flash.bin", BIOS_PATH);
            break;
        case jsm::systems::DMG:
            has_bios = 1;
            if (using_override)
                snprintf(BIOS_PATH, sizeof(BIOS_PATH), "%s", BASE_PATH);
            else
                snprintf(BIOS_PATH, sizeof(BIOS_PATH), "%s/gameboy", BASE_PATH);
            BIOSes->add("gb_bios.bin", BIOS_PATH);
            break;
        case jsm::systems::GBC:
            has_bios = 1;
            if (using_override)
                snprintf(BIOS_PATH, sizeof(BIOS_PATH), "%s", BASE_PATH);
            else
                snprintf(BIOS_PATH, sizeof(BIOS_PATH), "%s/gameboy", BASE_PATH);
            BIOSes->add("gbc_bios.bin", BIOS_PATH);
            break;
        case jsm::systems::APPLEIIe:
            has_bios = 1;
            if (using_override)
                snprintf(BIOS_PATH, sizeof(BIOS_PATH), "%s", BASE_PATH);
            else
                snprintf(BIOS_PATH, sizeof(BIOS_PATH), "%s/apple2", BASE_PATH);
            BIOSes->add("apple2e.rom", BIOS_PATH);
            BIOSes->add("apple2e_video.rom", BIOS_PATH);
            BIOSes->add("Apple Disk II 16 Sector Interface Card ROM P5 - 341-0027.bin", BIOS_PATH);
            break;
        case jsm::systems::ZX_SPECTRUM_48K:
            has_bios = 1;
            if (using_override)
                snprintf(BIOS_PATH, sizeof(BIOS_PATH), "%s", BASE_PATH);
            else
                snprintf(BIOS_PATH, sizeof(BIOS_PATH), "%s/zx_spectrum", BASE_PATH);
            BIOSes->add("zx48.rom", BIOS_PATH);
            break;
        case jsm::systems::COMMODORE64:
            has_bios = 1;
            if (using_override)
                snprintf(BIOS_PATH, sizeof(BIOS_PATH), "%s", BASE_PATH);
            else
                snprintf(BIOS_PATH, sizeof(BIOS_PATH), "%s/commodore64", BASE_PATH);
            //BIOSes->add("64c.251913-01.bin", BIOS_PATH);
            /*
            *u3 = basic
            u4 = kernal
            u5 = chargen*/
            //TODO: get commodore BIOS
            BIOSes->add("c64 r2.u3", BIOS_PATH);
            BIOSes->add("c64 r2.u4", BIOS_PATH);
            BIOSes->add("c64 r2.u5", BIOS_PATH);
            break;
        case jsm::systems::ZX_SPECTRUM_128K:
            has_bios = 1;
            if (using_override)
                snprintf(BIOS_PATH, sizeof(BIOS_PATH), "%s", BASE_PATH);
            else
                snprintf(BIOS_PATH, sizeof(BIOS_PATH), "%s/zx_spectrum", BASE_PATH);
            BIOSes->add("zx128.rom", BIOS_PATH);
            break;
        case jsm::systems::MAC128K:
            has_bios = 1;
            if (using_override)
                snprintf(BIOS_PATH, sizeof(BIOS_PATH), "%s", BASE_PATH);
            else
                snprintf(BIOS_PATH, sizeof(BIOS_PATH), "%s/mac", BASE_PATH);
            BIOSes->add("mac128k.rom", BIOS_PATH);
            break;
        case jsm::systems::MAC512K:
            has_bios = 1;
            if (using_override)
                snprintf(BIOS_PATH, sizeof(BIOS_PATH), "%s", BASE_PATH);
            else
                snprintf(BIOS_PATH, sizeof(BIOS_PATH), "%s/mac", BASE_PATH);
            BIOSes->add("mac512k.rom", BIOS_PATH);
            break;
        case jsm::systems::MACPLUS_1MB:
            has_bios = 1;
            if (using_override)
                snprintf(BIOS_PATH, sizeof(BIOS_PATH), "%s", BASE_PATH);
            else
                snprintf(BIOS_PATH, sizeof(BIOS_PATH), "%s/mac", BASE_PATH);
            BIOSes->add("macplus.rom", BIOS_PATH);
            break;
        case jsm::systems::PS1:
            has_bios = 1;
            if (using_override)
                snprintf(BIOS_PATH, sizeof(BIOS_PATH), "%s", BASE_PATH);
            else
                snprintf(BIOS_PATH, sizeof(BIOS_PATH), "%s/ps1", BASE_PATH);
            BIOSes->add("scph1001.bin", BIOS_PATH);
            //BIOSes->add("PSXONPSP660.BIN", BIOS_PATH);
            break;
        case jsm::systems::GBA:
            has_bios = 1;
            if (using_override)
                snprintf(BIOS_PATH, sizeof(BIOS_PATH), "%s", BASE_PATH);
            else
                snprintf(BIOS_PATH, sizeof(BIOS_PATH), "%s/gba", BASE_PATH);
            BIOSes->add("gba_bios.bin", BIOS_PATH);
            break;
        case jsm::systems::NDS:
            has_bios = 1;
            if (using_override)
                snprintf(BIOS_PATH, sizeof(BIOS_PATH), "%s", BASE_PATH);
            else
                snprintf(BIOS_PATH, sizeof(BIOS_PATH), "%s/nds", BASE_PATH);
            BIOSes->add("bios7.bin", BIOS_PATH);
            BIOSes->add("bios9.bin", BIOS_PATH);
            BIOSes->add("firmware.bin", BIOS_PATH);
            break;
        case jsm::systems::GALAKSIJA:
            has_bios = 1;
            if (using_override)
                snprintf(BIOS_PATH, sizeof(BIOS_PATH), "%s", BASE_PATH);
            else
                snprintf(BIOS_PATH, sizeof(BIOS_PATH), "%s/galaksija", BASE_PATH);
            BIOSes->add("CHRGEN_MIPRO.bin", BIOS_PATH);
            BIOSes->add("ROM_A_28.bin", BIOS_PATH);
            //BIOSes->add("ROM_A_29.bin", BIOS_PATH);
            //BIOSes->add("ROM_B.bin", BIOS_PATH);
            break;
        case jsm::systems::NEOGEO_AES:
            has_bios = 1;
            if (using_override) {
                snprintf(BIOS_PATH, sizeof(BIOS_PATH), "%s/neogeo.zip", BASE_PATH);
                if (!std::filesystem::is_regular_file(BIOS_PATH) || !mfs_add_zip(BIOSes, BIOS_PATH)) {
                    snprintf(BIOS_PATH, sizeof(BIOS_PATH), "%s", BASE_PATH);
                    BIOSes->add("neo-po.bin", BIOS_PATH);
                }
            } else {
                snprintf(BIOS_PATH, sizeof(BIOS_PATH), "%s/neogeo/neogeo.zip", BASE_PATH);
                if (!std::filesystem::is_regular_file(BIOS_PATH) || !mfs_add_zip(BIOSes, BIOS_PATH)) {
                    snprintf(BIOS_PATH, sizeof(BIOS_PATH), "%s/neogeo/aes", BASE_PATH);
                    BIOSes->add("neo-po.bin", BIOS_PATH);
                }
            }
            break;
        case jsm::systems::NEOGEO_MVS:
            has_bios = 1;
            if (using_override) {
                snprintf(BIOS_PATH, sizeof(BIOS_PATH), "%s/neogeo.zip", BASE_PATH);
                if (!std::filesystem::is_regular_file(BIOS_PATH) || !mfs_add_zip(BIOSes, BIOS_PATH)) {
                    snprintf(BIOS_PATH, sizeof(BIOS_PATH), "%s", BASE_PATH);
                    BIOSes->add("sp-s2.sp1", BIOS_PATH);
                }
            } else {
                snprintf(BIOS_PATH, sizeof(BIOS_PATH), "%s/neogeo/neogeo.zip", BASE_PATH);
                if (!std::filesystem::is_regular_file(BIOS_PATH) || !mfs_add_zip(BIOSes, BIOS_PATH)) {
                    snprintf(BIOS_PATH, sizeof(BIOS_PATH), "%s/neogeo/mvs", BASE_PATH);
                    BIOSes->add("sp-s2.sp1", BIOS_PATH);
                }
            }
            break;
        case jsm::systems::NEOGEO_POCKET:
            has_bios = 1;
            if (using_override)
                snprintf(BIOS_PATH, sizeof(BIOS_PATH), "%s", BASE_PATH);
            else
                snprintf(BIOS_PATH, sizeof(BIOS_PATH), "%s/ngp", BASE_PATH);
            BIOSes->add("bios.ngp", BIOS_PATH);
            break;
        case jsm::systems::NEOGEO_POCKET_COLOR:
            has_bios = 1;
            if (using_override)
                snprintf(BIOS_PATH, sizeof(BIOS_PATH), "%s", BASE_PATH);
            else
                snprintf(BIOS_PATH, sizeof(BIOS_PATH), "%s/ngp", BASE_PATH);
            BIOSes->add("bios.ngc", BIOS_PATH);
            break;
        case jsm::systems::SG1000:
        case jsm::systems::GENESIS_JAP:
        case jsm::systems::GENESIS_USA:
        case jsm::systems::MEGADRIVE_PAL:
        case jsm::systems::NES:
        case jsm::systems::BBC_MICRO:
        case jsm::systems::GG:
        case jsm::systems::ATARI2600:
        case jsm::systems::SMS1:
        case jsm::systems::SMS2:
        case jsm::systems::SNES:
        case jsm::systems::TURBOGRAFX16:
        case jsm::systems::COSMAC_VIP_2k:
        case jsm::systems::COSMAC_VIP_4k:
        case jsm::systems::CASIO_PV1000:
            has_bios = 0;
            break;
        default:
            printf("\nNO BIOS SWITCH FOR CONSOLE %d", which);
            break;
    }
    return has_bios;
}


void GET_HOME_BASE_SYS(char *out, size_t out_sz, jsm::systems which, const char* sec_path, u32 *worked)
{
    char BASE_PATH[500];
    char BASER_PATH[500];
    const char *homeDir = get_user_dir();
    if (!homeDir || !homeDir[0]) homeDir = ".";

    snprintf(BASER_PATH, 500, "%s/Documents", homeDir);

    u32 has_bios = 0;
    switch(which) {
        case jsm::systems::SG1000:
        case jsm::systems::SMS1:
        case jsm::systems::SMS2:
            snprintf(out, out_sz, "%s/master_system", BASER_PATH);
            *worked = 1;
            break;
        case jsm::systems::DREAMCAST:
            if (sec_path)
                snprintf(out, out_sz, "%s/dreamcast/%s", BASER_PATH, sec_path);
            else
                snprintf(out, out_sz, "%s/dreamcast", BASER_PATH);
            *worked = 1;
            break;
        case jsm::systems::DMG:
        case jsm::systems::GBC:
            snprintf(out, out_sz, "%s/gameboy", BASER_PATH);
            *worked = 1;
            break;
        case jsm::systems::ATARI2600:
            snprintf(out, out_sz, "%s/atari2600", BASER_PATH);
            *worked = 1;
            break;
        case jsm::systems::CASIO_PV1000:
            snprintf(out, out_sz, "%s/pv1000", BASER_PATH);
            *worked = 1;
            break;
        case jsm::systems::NES:
            snprintf(out, out_sz, "%s/nes", BASER_PATH);
            *worked = 1;
            break;
        case jsm::systems::APPLEIIe:
            snprintf(out, out_sz, "%s/appleii", BASER_PATH);
            *worked = 1;
            break;
        case jsm::systems::GALAKSIJA:
            snprintf(out, out_sz, "%s/galaksija", BASER_PATH);
            *worked = 1;
            break;
        case jsm::systems::COSMAC_VIP_2k:
        case jsm::systems::COSMAC_VIP_4k:
            snprintf(out, out_sz, "%s/chip8", BASER_PATH);
            *worked = 1;
            break;
        case jsm::systems::ZX_SPECTRUM_48K:
        case jsm::systems::ZX_SPECTRUM_128K:
            snprintf(out, out_sz, "%s/zx_spectrum", BASER_PATH);
            *worked = 1;
            break;
        case jsm::systems::NEOGEO_AES:
        case jsm::systems::NEOGEO_MVS:
            snprintf(out, out_sz, "%s/neogeo", BASER_PATH);
            *worked = 1;
            break;
        case jsm::systems::NEOGEO_POCKET:
        case jsm::systems::NEOGEO_POCKET_COLOR:
            snprintf(out, out_sz, "%s/ngp", BASER_PATH);
            *worked = 1;
            break;
        case jsm::systems::PS1:
            snprintf(out, out_sz, "%s/ps1", BASER_PATH);
            *worked = 1;
            break;
        case jsm::systems::GBA:
            snprintf(out, out_sz, "%s/gba", BASER_PATH);
            *worked = 1;
            break;
        case jsm::systems::NDS:
            snprintf(out, out_sz, "%s/nds", BASER_PATH);
            *worked = 1;
            break;
        case jsm::systems::SNES:
            snprintf(out, out_sz, "%s/snes", BASER_PATH);
            *worked = 1;
            break;
        case jsm::systems::COMMODORE64:
            snprintf(out, out_sz, "%s/commodore64", BASER_PATH);
            *worked = 1;
            break;
        case jsm::systems::TURBOGRAFX16:
            snprintf(out, out_sz, "%s/tg16", BASER_PATH);
            *worked = 1;
            break;
        case jsm::systems::GENESIS_USA:
        case jsm::systems::GENESIS_JAP:
        case jsm::systems::MEGADRIVE_PAL:
            snprintf(out, out_sz, "%s/genesis", BASER_PATH);
            *worked = 1;
            break;
        case jsm::systems::MAC512K:
        case jsm::systems::MAC128K:
        case jsm::systems::MACPLUS_1MB:
            snprintf(out, out_sz, "%s/mac", BASER_PATH);
            *worked = 1;
            break;
        case jsm::systems::GG:
            snprintf(out, out_sz, "%s/gg", BASER_PATH);
            *worked = 1;
            break;
        case jsm::systems::BBC_MICRO:
            *worked = 0;
            break;
        default:
            *worked = 0;
            printf("\nNO CASE FOR SYSTEM %d", which);
            break;
    }
}

void mfs_add_IP_BIN(multi_file_set* mfs)
{
    //char BASER_PATH[255];
    char BASE_PATH[255];
    //char ROM_PATH[255];
    u32 worked = 0;

    GET_HOME_BASE_SYS(BASE_PATH, 255, jsm::systems::DREAMCAST, nullptr, &worked);
    if (worked == 0) return;

    mfs->add("IP.BIN", BASE_PATH);
    printf("\nLOADED IP.BIN SIZE %04zx", mfs->files[1].buf.size);
}

u32 grab_gdi(multi_file_set* ROMs, jsm::systems which, const char* fname, const char* sec_path) {
    char BASE_PATH[255];
    //char ROM_PATH[255];
    u32 worked = 0;

    GET_HOME_BASE_SYS(BASE_PATH, sizeof(BASE_PATH), which, sec_path, &worked);
    printf("\nBASE_PATH:%s", BASE_PATH);
    //char BASER_PATH[255];
    if (!worked) {
        printf("\nEARLY QUIT!");
        return 0;
    }
    //snprintf(BASER_PATH, sizeof(BASER_PATH), "%s/%s", BASE_PATH, fname);
    //printf("\nBASER_PATH %s", BASER_PATH);

    ROMs->clear();
    char fname2[500];
    snprintf(fname2, sizeof(fname2), "%s.gdi", fname);
    ROMs->add(fname2, BASE_PATH);
    // Now parse through it!!! GPT time!
    const char *gdi = (const char *)ROMs->files[0].buf.ptr;
    size_t gdi_sz = ROMs->files[0].buf.size;

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

    int declared_tracks = atoi(line);
    (void)declared_tracks;

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
        ROMs->add(fname, BASE_PATH);
    }
    return true;
}

u32 grab_ROMset(multi_file_set* ROMs, jsm::systems which, const char* fname, const char* sec_path) {
    char BASE_PATH[255];
    u32 worked = 0;
    GET_HOME_BASE_SYS(BASE_PATH, sizeof(BASE_PATH), which, sec_path, &worked);
    char BASER_PATH[255];
    if (!worked) {
        printf("\nEARLY QUIT!");
        return 0;
    }
    snprintf(BASER_PATH, sizeof(BASER_PATH), "%s/%s", BASE_PATH, fname);
    ROMs->clear();
    namespace fs = std::filesystem;
    if (fs::is_regular_file(BASER_PATH) && str_ends_with_zip(BASER_PATH))
        return mfs_add_zip(ROMs, BASER_PATH) != 0;

    char ZIP_PATH[500];
    snprintf(ZIP_PATH, sizeof(ZIP_PATH), "%s.zip", BASER_PATH);
    if (fs::is_regular_file(ZIP_PATH))
        return mfs_add_zip(ROMs, ZIP_PATH) != 0;

    char fname2[500];
    try {
        for (const auto& entry : fs::directory_iterator(BASER_PATH)) {
            ROMs->add(entry.path().filename().string().c_str(), BASER_PATH);
            worked = 1;
        }
    } catch (const fs::filesystem_error& e) {
        std::cerr << "Filesystem error: " << e.what() << '\n';
    }

    fname2[0] = 0;
    ROMs->add(fname2, BASER_PATH);
    return worked;
}

u32 grab_cue(multi_file_set* ROMs, jsm::systems which, const char* fname, const char* sec_path) {
    char BASE_PATH[255];
    //char ROM_PATH[255];
    u32 worked = 0;

    GET_HOME_BASE_SYS(BASE_PATH, sizeof(BASE_PATH), which, sec_path, &worked);
    char BASER_PATH[255];
    if (!worked) {
        printf("\nEARLY QUIT!");
        return 0;
    }
    snprintf(BASER_PATH, sizeof(BASER_PATH), "%s/%s", BASE_PATH, fname);
    printf("\nBASER_PATH %s", BASER_PATH);

    ROMs->clear();
    char fname2[500];
    snprintf(fname2, sizeof(fname2), "%s.cue", fname);
    ROMs->add(fname2, BASER_PATH);

    // Now parse through it!!! GPT time!
    const char *cue = static_cast<const char *>(ROMs->files[0].buf.ptr);
    size_t cue_sz = ROMs->files[0].buf.size;
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
        if (!line[0]) continue;
        if (!strncmp(line, "FILE", 4)) {
            char ffname[256]{};
            if (sscanf(line, R"(FILE "%255[^"]")", ffname) == 1) {
                ROMs->add(ffname, BASER_PATH);
            }
        }
    }
    return 1;

}

u32 grab_ROM(multi_file_set* ROMs, jsm::systems which, const char* fname, const char* sec_path)
{
    char BASE_PATH[255];
    //char ROM_PATH[255];
    u32 worked = 0;

    GET_HOME_BASE_SYS(BASE_PATH, sizeof(BASE_PATH), which, sec_path, &worked);
    if (!worked) {
        printf("\nEARLY QUIT!");
        return 0;
    }

    ROMs->add(fname, BASE_PATH);
    printf("\n%d %s %s", ROMs->files[ROMs->files.size()-1].buf.size > 0, BASE_PATH, fname);
    return ROMs->files[ROMs->files.size()-1].buf.size > 0;
}

u32 grab_dcsideload(multi_file_set* ROMs, jsm::systems which, const char* fname, const char* sec_path)
{
    char BASE_PATH[255];
    //char ROM_PATH[255];
    u32 worked = 0;

    GET_HOME_BASE_SYS(BASE_PATH, sizeof(BASE_PATH), which, sec_path, &worked);
    if (!worked) {
        printf("\nEARLY QUIT!");
        return 0;
    }

    ROMs->add(fname, BASE_PATH);
    printf("\n%d %s %s", ROMs->files[ROMs->files.size()-1].buf.size > 0, BASE_PATH, fname);

    return ROMs->files[ROMs->files.size()-1].buf.size > 0;
}


physical_io_device* load_ROM_into_emu(jsm_system* sys, std::vector<physical_io_device> &IOs, multi_file_set& mfs) {
    physical_io_device *pio = nullptr;
    switch(sys->kind) {
        case jsm::systems::ZX_SPECTRUM_48K:
        case jsm::systems::ZX_SPECTRUM_128K:
        case jsm::systems::DREAMCAST:
        case jsm::systems::MAC512K:
        case jsm::systems::MAC128K:
        case jsm::systems::MACPLUS_1MB:
        case jsm::systems::APPLEIIe:
        case jsm::systems::PS1:
            for (u32 i = 0; i < IOs.size(); i++) {
                pio = &IOs.at(i);
                if (pio->kind == HID_DISC_DRIVE) {
                    printf("\nINSERT DISC!");
                    pio->disc_drive.open_drive(sys);
                    pio->disc_drive.insert_disc(sys, *pio, mfs);
                    pio->disc_drive.close_drive(sys);
                    break;
                }
                else if (pio->kind == HID_AUDIO_CASSETTE) {
                    pio->audio_cassette.insert_tape(sys, *pio, mfs, nullptr);
                    break;
                }
                pio = nullptr;
            }
            return pio;
        default: // don't do anything for others...
            break;
    }
    pio = nullptr;
    for (u32 i = 0; i < IOs.size(); i++) {
        pio = &IOs.at(i);
        if (pio->kind == HID_CART_PORT) break;
        pio = nullptr;
    }
    if (pio) pio->cartridge_port.load_cart(sys, mfs, *pio);
    return pio;
}

static void setup_controller(system_io* io, physical_io_device& pio, u32 pnum)
{
    auto &dbs = pio.controller.digital_buttons;
    for (HID_digital_button &dbr : dbs) {
        //HID_digital_button* db = (HID_digital_button*)cvec_get(dbs, i);
        auto *db = &dbr;
        switch(db->common_id) {
            case DBCID_co_up:
                io->p[pnum].up = db;
                continue;
            case DBCID_co_down:
                io->p[pnum].down = db;
                continue;
            case DBCID_co_left:
                io->p[pnum].left = db;
                continue;
            case DBCID_co_right:
                io->p[pnum].right = db;
                continue;
            case DBCID_co_fire1:
                io->p[pnum].fire1 = db;
                continue;
            case DBCID_co_fire2:
                io->p[pnum].fire2 = db;
                continue;
            case DBCID_co_fire3:
                io->p[pnum].fire3 = db;
                continue;
            case DBCID_co_fire4:
                io->p[pnum].fire4 = db;
                continue;
            case DBCID_co_fire5:
                io->p[pnum].fire5 = db;
                continue;
            case DBCID_co_fire6:
                io->p[pnum].fire6 = db;
                continue;
            case DBCID_co_shoulder_left:
                io->p[pnum].shoulder_left = db;
                continue;
            case DBCID_co_shoulder_right:
                io->p[pnum].shoulder_right = db;
                continue;
            case DBCID_co_select:
                io->p[pnum].select = db;
                continue;
            case DBCID_co_start:
                io->p[pnum].start = db;
                continue;
        }
    }
}

void full_system::setup_ios()
{
    std::vector<physical_io_device> &IOs = sys->IOs;
    for (u32 i = 0; i < IOs.size(); i++) {
        physical_io_device &pio = IOs.at(i);
        switch(pio.kind) {
            case HID_TOUCHSCREEN:
                if (!io.touchscreen.vec) {
                    io.touchscreen.make(IOs, i);
                }
                continue;
            case HID_CONTROLLER: {
                if (!io.controller1.vec) {
                    io.controller1.make(IOs, i);
                    setup_controller(&inputs, pio, 0);
                } else if (!io.controller2.vec) {
                    io.controller2.make(IOs, i);
                    setup_controller(&inputs, pio, 1);
                } else if (!io.controller3.vec) {
                    io.controller3.make(IOs, i);
                    setup_controller(&inputs, pio, 2);
                } else if (!io.controller4.vec) {
                    io.controller4.make(IOs, i);
                    setup_controller(&inputs, pio, 3);
                }
                continue; }
            case HID_HEX_KEYPAD:
                io.hex_keypad.make(IOs, i);
                break;
            case HID_KEYBOARD: {
                io.keyboard.make(IOs, i);
                continue; }
            case HID_DISPLAY: {
                if (!io.display.vec) io.display.make(IOs, i);
                else if (!io.display2.vec) io.display2.make(IOs, i);
                continue; }
            case HID_CHASSIS: {
                io.chassis.make(IOs, i);
                //make_cvec_ptr(IOs, i);
                std::vector<HID_digital_button> &dbs = pio.chassis.digital_buttons;
                for (auto &dbptr : dbs) {
                    HID_digital_button *db = &dbptr;
                    switch(db->common_id) {
                        case DBCID_ch_pause:
                            inputs.ch_pause = db;
                            continue;
                        case DBCID_ch_power:
                            inputs.ch_power = db;
                            continue;
                        case DBCID_ch_reset:
                            inputs.ch_reset = db;
                            continue;
                        default:
                            continue;
                    }
                }
                break; }
            case HID_MOUSE:
                io.mouse.make(IOs, i);
                break;
            case HID_DISC_DRIVE:
                io.disk_drive.make(IOs, i);
                break;
            case HID_CART_PORT:
                io.cartridge_port.make(IOs, i);
                break;
            case HID_MEM_CARD:
                io.mem_card.make(IOs, i);
                break;
            case HID_AUDIO_CASSETTE:
                io.audio_cassette.make(IOs, i);
                break;
            default:
                break;
        }
    }
    assert(io.display.vec);
    assert(io.chassis.vec);


    output.display = &io.display.get().display;
    if (io.display2.vec)
        output2.display = &io.display2.get().display;
}


void full_system::setup_audio(AppSettings *settings)
{
    // ── Validate / auto-select output rate ───────────────────────────────────
    // Probe the device once per session (rates are cached in available_audio_rates).
    if (available_audio_rates.empty())
        available_audio_rates = audiowrap::probe_available_rates();

    u32 stored = settings ? settings->get_audio_output_rate() : 0;

    // Check whether the stored rate is in the available list
    bool valid = stored != 0 &&
                 std::find(available_audio_rates.begin(), available_audio_rates.end(), stored)
                 != available_audio_rates.end();

    if (!valid) {
        // Auto-select: prefer 48000, otherwise first in list (already sorted that way)
        stored = available_audio_rates.empty() ? 48000 : available_audio_rates[0];
        if (settings) settings->set_audio_output_rate(stored);
        printf("\nAudio: auto-selected output rate %u Hz", stored);
    }

    audio.output_rate = stored;

    // Use the display's actual frame rate so ring buffers are sized correctly.
    double core_fps = (output.display && output.display->fps > 0.0)
                      ? output.display->fps : 60.0;

    audio.fps = (float)core_fps;
    audio.clear_streams();

    for (auto &pio : sys->IOs) {
        if (pio.kind != HID_AUDIO_CHANNEL) continue;
        JSM_AUDIO_CHANNEL &chan = pio.audio_channel;
        if (!chan.sample_rate) continue;

        if (!chan.left && !chan.right) chan.left = chan.right = 1;
        if (!chan.num) chan.num = 2;

        // Allocate the ring and store the pointer in the PIO so the core
        // can grab it from audio_rings_ready().
        // mix_volume lives in a union zeroed at construction; cores that omit
        // it end up with 0.0f — treat that as "not set" and default to 1.0f.
        float vol = (chan.mix_volume > 0.0f) ? chan.mix_volume : 1.0f;
        chan.ring = audio.add_stream(chan.sample_rate, chan.num,
                                    vol, chan.low_pass_filter);
        audiochans.push_back(&chan);
    }

    if (audiochans.empty()) {
        printf("\nNo audio channel found in full_sys!");
        return;
    }

    // Backward compat: single-stream cores that override set_audio_ring.
    if (sys->has.set_audio_ring)
        sys->set_audio_ring(audiochans[0]->ring);

    // New: let multi-stream cores (and any other core) grab ring pointers
    // directly from their PIO structs.
    sys->audio_rings_ready();

    audio.init_backend();
}

void full_system::setup_bios()
{
    jsm::systems which = sys->kind;

    BIOSes.clear();

    u32 has_bios = grab_BIOSes(&BIOSes, which, bios_override_dir[0] ? bios_override_dir : nullptr);
    if (has_bios) {
        sys->load_BIOS(BIOSes);
    }
}

// Core helper: finish setting up a persistent_store once the file path is known.
// Called by both public overloads after they populate ps.filename.
static void finish_setup_ps(persistent_store &ps)
{
    if (ps.persistent) {
        ps.kind = PSK_SIMPLE_FILE;
        printf("\nFILENAME:%s", ps.filename);
        ps.fno = fopen(ps.filename, "rb+");
        if (!ps.fno) {
            // File doesn't exist yet — create it filled with the fill value.
            ps.fno = fopen(ps.filename, "wb");
            if (ps.fno) {
                u8 *a = static_cast<u8 *>(malloc(ps.requested_size));
                memset(a, ps.fill_value, ps.requested_size);
                fwrite(a, 1, ps.requested_size, ps.fno);
                free(a);
                fflush(ps.fno);
                fclose(ps.fno);
                ps.fno = fopen(ps.filename, "rb+");
            }
        }

        if (ps.fno) {
            fseek(ps.fno, 0, SEEK_SET);
            ps.data = malloc(ps.requested_size);
            fread(ps.data, 1, ps.requested_size, ps.fno);
        } else {
            // Couldn't open file — fall back to volatile RAM
            ps.data = malloc(ps.requested_size);
            memset(ps.data, ps.fill_value, ps.requested_size);
        }
        ps.old_requested_size = ps.requested_size;
    }
    else {
        ps.data = malloc(ps.requested_size);
    }

    if (ps.old_requested_size != ps.requested_size) {
        printf("\nSIZE CHANGE FROM %lldkB to %lldkB", ps.old_requested_size / 1024, ps.requested_size / 1024);
        printf("\nRemember to implement this...");
        ps.requested_size = ps.old_requested_size;
    }

    ps.actual_size = ps.requested_size;
    ps.ready_to_use = 1;
}

void full_system::setup_persistent_store(persistent_store &ps, multi_file_set &mfs)
{
    printf("\nSETTING UP PERSISTENT STORE!");
    read_file_buf *rfb = &mfs.files[0];
    snprintf(ps.filename, sizeof(ps.filename), "%s/%s.sram", rfb->path, rfb->name);
    finish_setup_ps(ps);
    my_ps = &ps;
}

void full_system::setup_persistent_store(persistent_store &ps, const char *path)
{
    printf("\nSETTING UP PERSISTENT STORE (path)!");
    snprintf(ps.filename, sizeof(ps.filename), "%s", path);
    finish_setup_ps(ps);
    my_ps = &ps;
}

void full_system::sync_persistent_storage()
{
    if (my_ps) {
        if (my_ps->dirty && my_ps->persistent) {
            //printf("\nWriting save data..,");
            fseek(my_ps->fno, 0, SEEK_SET);
            fwrite(my_ps->data, 1, my_ps->actual_size, my_ps->fno);
            fflush(my_ps->fno);
            my_ps->dirty = false;
        }
    }
}

void full_system::close_persistent_storage()
{
    sync_persistent_storage();
    if (!my_ps) return;

    if (my_ps->fno) {
        fclose(my_ps->fno);
        my_ps->fno = nullptr;
    }
    if (my_ps->data) {
        free(my_ps->data);
        my_ps->data = nullptr;
    }
    my_ps->actual_size = 0;
    my_ps->old_requested_size = 0;
    my_ps->ready_to_use = false;
    my_ps->dirty = false;
    my_ps = nullptr;
}

void full_system::on_media_inserted(u32 io_index, const char* path, bool is_folder)
{
    pending_sram = {};

    // Update ROMs to the new path so apply_core_options can derive the
    // correct save-state/SRAM filenames from the media stem.
    ROMs.clear();
    worked = mfs_add_path(&ROMs, path, is_folder);

    if (!sys || io_index >= (u32)sys->IOs.size()) return;

    // If the inserted media has persistent storage, prime pending_sram.
    // apply_core_options will open the file and load saved data into it.
    physical_io_device& pio = sys->IOs[io_index];
    if (pio.kind == HID_CART_PORT &&
        pio.cartridge_port.SRAM.requested_size > 0) {
        pending_sram.ps    = &pio.cartridge_port.SRAM;
        pending_sram.valid = true;
    }
    else if (pio.kind == HID_DISC_DRIVE &&
             sys->kind == jsm::systems::PS1 &&
             io.mem_card.vec) {
        pending_sram.ps    = &io.mem_card.get().memcard.store;
        pending_sram.valid = true;
    }
}

full_system::~full_system() {
    clear_runtime_state();
    if (output.backbuffer_backer) {
        free(output.backbuffer_backer);
        output.backbuffer_backer = nullptr;
    }
    output.blank_until_next_frame = false;
    output.blank_frame = 0;
    if (output2.backbuffer_backer) {
        free(output2.backbuffer_backer);
        output2.backbuffer_backer = nullptr;
    }
}

void full_system::clear_runtime_state()
{
    sync_persistent_storage();

    if (my_ps) {
        if (my_ps->fno)
            fclose(my_ps->fno);
        my_ps = nullptr;
    }

    audio.shutdown();
    destroy_system();

    ROMs.clear();
    BIOSes.clear();
    waveform_views.clear();
    waveform2_views.clear();
    dasm_views.clear();
    trace_views.clear();
    console_views.clear();
    images.clear();
    dlviews.clear();
    audiochans.clear();

    io = fsio{};
    inputs = system_io{};
    int_time = {};
    dbgr = debugger_interface{};
    memory.view = nullptr;
    source_listing.view = nullptr;
    events.view = nullptr;
    output.display = nullptr;
    output.blank_until_next_frame = false;
    output.blank_frame = 0;
    output2.display = nullptr;
    debugger_setup = 0;
    worked = 0;
    has_played_once = false;
    run_state = FSS_pause;

    // Reset deferred SRAM and cached path state
    pending_sram = {};
    state_save_dir.clear();
    sram_save_dir.clear();
    reset_fps_meter();
}

void full_system::load_current_ROMs()
{
    std::vector<physical_io_device> &IOs = sys->IOs;
    jsm::systems which = sys->kind;

    if (!worked) {
        printf("\nCouldn't open ROM!");
        if (which != jsm::systems::PS1) return;
    }

    switch (which) {
        case jsm::systems::DREAMCAST: {
            if (ROMs.files.size() > 0 && (ends_with(ROMs.files[0].name, "elf") || ends_with(ROMs.files[0].name, ".bin"))) {
                mfs_add_IP_BIN(&ROMs);
                sys->sideload(ROMs);
            }
            else {
                load_ROM_into_emu(sys, IOs, ROMs);
            }
            worked = 1;
            break;
        }
        case jsm::systems::PS1:
            if (worked) {
                if (ROMs.files.size() < 1) break;
                if (ends_with(ROMs.files[0].name, ".exe"))
                    sys->sideload(ROMs);
                else {

#ifdef SIDELOAD_WITH_CD
                    multi_file_set R;
                    grab_ROM(&R, which, "test/cdlsetloc.ps-exe", nullptr);
                    sys->sideload(R);
#endif
                    load_ROM_into_emu(sys, IOs, ROMs);
                }
                // Defer PS1 memory card setup — apply_core_options resolves
                // the correct path (per-game vs universal) and calls setup_persistent_store.
                pending_sram.ps    = &io.mem_card.get().memcard.store;
                pending_sram.valid = true;
            }
            worked = 1;
            break;
        case jsm::systems::COSMAC_VIP_2k:
        case jsm::systems::COSMAC_VIP_4k:
        case jsm::systems::COMMODORE64:
            sys->sideload(ROMs);
            break;
        default: {
            physical_io_device *fileioport = load_ROM_into_emu(sys, IOs, ROMs);
            if (fileioport != nullptr) {
                printf("\nSRAM requested size: %lld\n", fileioport->cartridge_port.SRAM.requested_size);
                if (fileioport->cartridge_port.SRAM.requested_size > 0) {
                    // Defer SRAM setup — apply_core_options resolves the correct
                    // path (ROM folder vs central saves dir) before opening the file.
                    pending_sram.ps    = &fileioport->cartridge_port.SRAM;
                    pending_sram.valid = true;
                }
            }
            break; }
    }
}

void full_system::load_default_ROM()
{
    jsm::systems which = sys->kind;

    ROMs.clear();
    assert(sys);
    switch(which) {
        case jsm::systems::NES:
            //worked = grab_ROM(&ROMs, which, "apu_test.nes", nullptr);
            //NROM
            //worked = grab_ROM(&ROMs, which, "drmario.nes", nullptr);
            worked = grab_ROM(&ROMs, which, "mario.nes", nullptr);
            //worked = grab_ROM(&ROMs, which, "dkong.nes", nullptr);

            // MMC3
            //worked = grab_ROM(&ROMs, which, "kirby.nes", nullptr);
            //worked = grab_ROM(&ROMs, which, "mario3.nes", nullptr);
            //worked = grab_ROM(&ROMs, which, "gauntlet.nes", nullptr);
            //worked = grab_ROM(&ROMs, which, "recca.nes", nullptr);

            // Sunsoft 5b
            //worked = grab_ROM(&ROMs, which, "gimmick_jp.nes", nullptr);

            // ANROM
            //worked = grab_ROM(&ROMs, which, "battletoads.nes", nullptr);
            //worked = grab_ROM(&ROMs, which, "marblemadness.nes", nullptr);

            // CNROM
            //worked = grab_ROM(&ROMs, which, "arkanoid.nes", nullptr);

            //GNROM
            //worked = grab_ROM(&ROMs, which, "doraemon.nes", nullptr);

            //MMC1
            //worked = grab_ROM(&ROMs, which, "zelda.nes", nullptr);
            //worked = grab_ROM(&ROMs, which, "tetris.nes", nullptr);
            //worked = grab_ROM(&ROMs, which, "metroid.nes", nullptr);
            //worked = grab_ROM(&ROMs, which, "bioniccommando.nes", nullptr);
            //worked = grab_ROM(&ROMs, which, "backtothefuture23.nes", nullptr);

            // DXROM
            // NEEDS WORK!?
            //worked = grab_ROM(&ROMs, which, "indianatod.nes", nullptr);

            // VRC4
            //worked = grab_ROM(&ROMs, which, "crisisforce.nes", nullptr);

            // UxROM
            //worked = grab_ROM(&ROMs, which, "castlevania.nes", nullptr);
            //worked = grab_ROM(&ROMs, which, "contra.nes", nullptr);

            // SunSoft 5
            //worked = grab_ROM(&ROMs, which, "gimmick_jp.nes", nullptr);

            // MMC5
            //worked = grab_ROM(&ROMs, which, "castlevania3.nes", nullptr);
            break;
        case jsm::systems::CASIO_PV1000:
            //worked = grab_ROM(&ROMs, which, "Dig Dug.bin", nullptr);
            worked = grab_ROM(&ROMs, which, "tutankham.bin", nullptr);
            break;
        case jsm::systems::SG1000:
            worked = grab_ROM(&ROMs, which, "choplifter.sg", nullptr);
            break;
        case jsm::systems::SMS1:
        case jsm::systems::SMS2:
            //worked = grab_ROM(&ROMs, which, "sinister.sms", nullptr);
            //worked = grab_ROM(&ROMs, which, "outrun.sms", nullptr);
            worked = grab_ROM(&ROMs, which, "sonic.sms", nullptr);
            //worked = grab_ROM(&ROMs, which, "space_harrier.sms", nullptr);
            break;
        case jsm::systems::GG:
            worked = grab_ROM(&ROMs, which, "megaman.gg", nullptr);
            //worked = grab_ROM(&ROMs, which, "sonic_triple.gg", nullptr);
            //worked = grab_ROM(&ROMs, which, "sonic_chaos.gg", nullptr);
            //worked = grab_ROM(&ROMs, which, "buttontest.gg", nullptr);
            //worked = grab_ROM(&ROMs, which, "gunstar.gg", nullptr);
            //worked = grab_ROM(&ROMs, which, "sonic_chaos.gg", nullptr);
            //worked = grab_ROM(&ROMs, which, "tails.gg", nullptr);
            //worked = grab_ROM(&ROMs, which, "sonicblast.gg", nullptr);
            break;
        case jsm::systems::DMG:
            worked = grab_ROM(&ROMs, which, "pokemonred.gb", nullptr);
            //worked = grab_ROM(&ROMs, which, "mm3.gb", nullptr);
            //worked = grab_ROM(&ROMs, which, "toystory.gb", nullptr);
            //worked = grab_ROM(&ROMs, which, "dmg-acid2.gb", nullptr);
            //worked = grab_ROM(&ROMs, which, "prehistorik.gb", nullptr);
            //worked = grab_ROM(&ROMs, which, "marioland2.gb", nullptr);
            //worked = grab_ROM(&ROMs, which, "tennis.gb", nullptr);
            //worked = grab_ROM(&ROMs, which, "link.gb", nullptr);
            //worked = grab_ROM(&ROMs, which, "drmario.gb", nullptr);
            //worked = grab_ROM(&ROMs, which, "mbc1_8mb.gb", nullptr);
            //worked = grab_ROM(&ROMs, which, "demo_in_pocket.gb", nullptr);
            //worked = grab_ROM(&ROMs, which, "m3_bgp_change_sprites.gb", nullptr);
            break;
        case jsm::systems::GBC:
            //worked = grab_ROM(&ROMs, which, "linkdx.gbc", nullptr);
            //worked = grab_ROM(&ROMs, which, "badapple.gbc", nullptr);
            // worked = grab_ROM(&ROMs, which, "densha.gbc", nullptr);
            //worked = grab_ROM(&ROMs, which, "cutedemo.gbc", nullptr);
            //worked = grab_ROM(&ROMs, which, "aitd.gbc", nullptr);
            //worked = grab_ROM(&ROMs, which, "mgs.gbc", nullptr);
            //worked = grab_ROM(&ROMs, which, "rayman.gbc", nullptr);
            worked = grab_ROM(&ROMs, which, "wario3.gbc", nullptr);
            //worked = grab_ROM(&ROMs, which, "tokitori.gbc", nullptr);
            //worked = grab_ROM(&ROMs, which, "m3_bgp_change_sprites.gb", nullptr);
            break;
        case jsm::systems::ATARI2600:
            //worked = grab_ROM(&ROMs, which, "space_invaders.a26", nullptr);
            //worked = grab_ROM(&ROMs, which, "frogger.a26", nullptr);
            //worked = grab_ROM(&ROMs, which, "tt4.bin", nullptr);
            //worked = grab_ROM(&ROMs, which, "pong.a26", nullptr);
            //worked = grab_ROM(&ROMs, which, "pitfall.a26", nullptr);
            //worked = grab_ROM(&ROMs, which, "enduro.a26", nullptr);
            break;
        case jsm::systems::DREAMCAST:
            //worked = grab_gdi(&ROMs, which, "crazytaxi", "crazy_taxi");
            //worked = grab_dcsideload(&ROMs, which, "roto.bin", nullptr);
            //worked = grab_dcsideload(&ROMs, which, "bloom612.bin", nullptr);
            //worked = grab_dcsideload(&ROMs, which, "armwrestler/ADC_1.s.bin", nullptr);
            //worked = grab_dcsideload(&ROMs, which, "kos/example.elf", nullptr);
            //worked = grab_dcsideload(&ROMs, which, "kos/hello.elf", nullptr);
            //worked = grab_dcsideload(&ROMs, which, "kos/latest_hello.elf", nullptr);
            //worked = grab_dcsideload(&ROMs, which, "kos/hello.elf", nullptr);
            worked = grab_dcsideload(&ROMs, which, "kos/2ndmix.elf", nullptr);

            break;
        case jsm::systems::MAC512K:
        case jsm::systems::MAC128K:
        case jsm::systems::MACPLUS_1MB:
            //worked = grab_ROM(&ROMs, which, "system1_1.img", nullptr);
            //worked = grab_ROM(&ROMs, which, "fd1.image", nullptr);
            worked = grab_ROM(&ROMs, which, "disk.bin", nullptr);
            break;
        case jsm::systems::GALAKSIJA:
            worked = 1;
            break;
        case jsm::systems::COMMODORE64:
            //worked = grab_ROM(&ROMs, which, "capture_tester.prg", nullptr);
            //worked = grab_ROM(&ROMs, which, "enduro_racer.prg", nullptr);
            //worked = grab_ROM(&ROMs, which, "stun_runner.prg", nullptr);
            //worked = grab_ROM(&ROMs, which, "they_are_spraying.prg", nullptr);
            break;
        case jsm::systems::COSMAC_VIP_2k:
        case jsm::systems::COSMAC_VIP_4k:
            //worked = grab_ROM(&ROMs, which, "1-chip8-logo.ch8", nullptr);
            //worked = grab_ROM(&ROMs, which, "2-ibm-logo.ch8", nullptr);
            //worked = grab_ROM(&ROMs, which, "3-corax+.ch8", nullptr);
            //worked = grab_ROM(&ROMs, which, "4-flags.ch8", nullptr);
            //worked = grab_ROM(&ROMs, which, "5-quirks.ch8", nullptr);
            //worked = grab_ROM(&ROMs, which, "6-keypad.ch8", nullptr);
            //worked = grab_ROM(&ROMs, which, "danm8ku.ch8", nullptr);
            //worked = grab_ROM(&ROMs, which, "Android Signboard Demo.cos", nullptr);
            //worked = grab_ROM(&ROMs, which, "Dragon.cos", nullptr);
            worked = grab_ROM(&ROMs, which, "Cosmac Demo.bin", nullptr);
            //worked = grab_ROM(&ROMs, which, "RCA Cosmac Picture.cos", nullptr);
            break;
        case jsm::systems::ZX_SPECTRUM_48K:
        case jsm::systems::ZX_SPECTRUM_128K:
            //worked = grab_ROM(&ROMs, which, "manic.tap", nullptr);
            worked = grab_ROM(&ROMs, which, "jetset.tap", nullptr);
            break;
        case jsm::systems::APPLEIIe:
            worked = 1;
            break;
        case jsm::systems::NDS:
            //worked = grab_ROM(&ROMs, which, "nfs2mw.nds", nullptr); // save data corrupt complaint
            //worked = grab_ROM(&ROMs, which, "sims3.nds", nullptr); // save data corrupt complaint
            //worked = grab_ROM(&ROMs, which, "rockwrestler.nds", nullptr);
            //worked = grab_ROM(&ROMs, which, "armwrestler.nds", nullptr);
            //worked = grab_ROM(&ROMs, which, "libnds/print_both_screens.nds", nullptr);
            //worked = grab_ROM(&ROMs, which, "armwrestler-2.nds", nullptr);
            //worked = grab_ROM(&ROMs, which, "libnds/hello_world.nds", nullptr);
            //worked = grab_ROM(&ROMs, which, "libnds/3D_Both_Screens.nds", nullptr);
            //worked = grab_ROM(&ROMs, which, "libnds/lesson02.nds", nullptr);
            //worked = grab_ROM(&ROMs, which, "libnds/lesson03.nds", nullptr);
            //worked = grab_ROM(&ROMs, which, "libnds/lesson04.nds", nullptr);
            //worked = grab_ROM(&ROMs, which, "libnds/lesson05.nds", nullptr);
            //worked = grab_ROM(&ROMs, which, "libnds/lesson06.nds", nullptr);
            //worked = grab_ROM(&ROMs, which, "libnds/lesson07.nds", nullptr);
            //worked = grab_ROM(&ROMs, which, "libnds/lesson06.nds", nullptr);
            //worked = grab_ROM(&ROMs, which, "libnds/lesson06.nds", nullptr);
            //worked = grab_ROM(&ROMs, which, "libnds/lesson10.nds", nullptr);
            //worked = grab_ROM(&ROMs, which, "libnds/lesson11.nds", nullptr);
            //worked = grab_ROM(&ROMs, which, "libnds/256colorTilemap.nds", nullptr);
            //worked = grab_ROM(&ROMs, which, "libnds/simple_tri.nds", nullptr);
            //worked = grab_ROM(&ROMs, which, "libnds/touch_test.nds", nullptr);
            //worked = grab_ROM(&ROMs, which, "libnds/2Dplus3D.nds", nullptr);
            //worked = grab_ROM(&ROMs, which, "libnds/bitmap_sprites.nds", nullptr);
            //worked = grab_ROM(&ROMs, which, "3d_demos/libnds3d/3d_ortho_projection.nds", nullptr);
            //worked = grab_ROM(&ROMs, which, "3d_demos/libnds3d/3d_box_test.nds", nullptr);
            //worked = grab_ROM(&ROMs, which, "3d_demos/gl2d/gl2d_spriteset.nds", nullptr);
            //worked = grab_ROM(&ROMs, which, "3d_demos/nitro-engine/font_from_ram.nds", nullptr);
            //worked = grab_ROM(&ROMs, which, "3d_compressed_texture.nds", nullptr);
            //worked = grab_ROM(&ROMs, which, "pokemon_diamond.nds", nullptr);
            //worked = grab_ROM(&ROMs, which, "pmdbrt.nds", nullptr);
            //worked = grab_ROM(&ROMs, which, "pmdes.nds", nullptr);
            //worked = grab_ROM(&ROMs, which, "mariokart.nds", nullptr);
            //worked = grab_ROM(&ROMs, which, "phoenixwright.nds", nullptr);
            worked = grab_ROM(&ROMs, which, "sm64.nds", nullptr);
            //worked = grab_ROM(&ROMs, which, "nintendogs.nds", nullptr);
            //worked = grab_ROM(&ROMs, which, "dbz2.nds", nullptr);
            //worked = grab_ROM(&ROMs, which, "rayman.nds", nullptr);
            //worked = grab_ROM(&ROMs, which, "mutha_truckers.nds", nullptr);
            //worked = grab_ROM(&ROMs, which, "barnyard_blast.nds", nullptr);
            //worked = grab_ROM(&ROMs, which, "cars.nds", nullptr);
            //worked = grab_ROM(&ROMs, which, "dolphin_trainer.nds", nullptr);
            //worked = grab_ROM(&ROMs, which, "recruit.nds", nullptr);
            //worked = grab_ROM(&ROMs, which, "tloz.nds", nullptr);
            //worked = grab_ROM(&ROMs, which, "dq9.nds", nullptr);
            //worked = grab_ROM(&ROMs, which, "fighting_fantasy.nds", nullptr);
            //worked = grab_ROM(&ROMs, which, "kirbycc.nds", nullptr);
            //worked = grab_ROM(&ROMs, which, "mp_hunters_rev1.nds", nullptr);
            //worked = grab_ROM(&ROMs, which, "okami_den.nds", nullptr); // try again after sprites
            //worked = grab_ROM(&ROMs, which, "tony_hawk.nds", nullptr);
            //worked = grab_ROM(&ROMs, which, "poke_black_1.nds", nullptr);
            //worked = grab_ROM(&ROMs, which, "infinite_space.nds", nullptr);
            //worked = grab_ROM(&ROMs, which, "nsmb.nds", nullptr);
            //worked = grab_ROM(&ROMs, which, "animal_crossing_ww.nds", nullptr);
            //worked = grab_ROM(&ROMs, which, "castlevania_dos.nds", nullptr);
            //worked = grab_ROM(&ROMs, which, "examples/graphics_2d/sprites_ext_palette.nds", nullptr);

            // NEXT: basic PPU mode 3, 5
            // better FIFO
            // NORMAL mode

            break;
        case jsm::systems::NEOGEO_AES:
            //worked = grab_ROM(&ROMs, which, "Blazing Star.neo", nullptr);
            //worked = grab_ROM(&ROMs, which, "Double Dragon.neo", nullptr);
            //worked = grab_ROM(&ROMs, which, "Fatal Fury - King of Fighters ~ Garou Densetsu - Shukumei no Tatakai (NGM-033 ~ NGH-033).neo", nullptr);
            //worked = grab_ROM(&ROMs, which, "Fatal Fury 2 ~ Garou Densetsu 2 - Arata-naru Tatakai (NGM-047 ~ NGH-047).neo", nullptr);
            //worked = grab_ROM(&ROMs, which, "Fatal Fury Special ~ Garou Densetsu Special (NGM-058 ~ NGH-058, set 2).neo", nullptr);
            //worked = grab_ROM(&ROMs, which, "Garou - Mark of the Wolves (NGM-2530 ~ NGH-2530).neo", nullptr);
            //worked = grab_ROM(&ROMs, which, "King of the Monsters (set 1).neo", nullptr); // Graphical errors on left of screen? mostly works
            //worked = grab_ROM(&ROMs, which, "Metal Slug - Super Vehicle-001.neo", nullptr); // runs!
            //worked = grab_ROM(&ROMs, which, "Metal Slug 2 - Super Vehicle-001II (NGM-2410 ~ NGH-2410).neo", nullptr);
            //worked = grab_ROM(&ROMs, which, "Metal Slug 3 (NGH-2560).neo", nullptr);
            worked = grab_ROM(&ROMs, which, "Metal Slug 4 (NGH-2630).neo", nullptr);
            //worked = grab_ROM(&ROMs, which, "Metal Slug 5 (NGH-2680).neo", nullptr);
            //worked = grab_ROM(&ROMs, which, "Metal Slug X - Super Vehicle-001 (NGM-2500 ~ NGH-2500).neo", nullptr);
            //worked = grab_ROM(&ROMs, which, "Prehistoric Isle 2.neo", nullptr);
            //worked = grab_ROM(&ROMs, which, "Pulstar.neo", nullptr);
            //worked = grab_ROM(&ROMs, which, "Puzzle Bobble ~ Bust-A-Move (Neo-Geo, NGM-083).neo", nullptr);
            //worked = grab_ROM(&ROMs, which, "SNK vs. Capcom - SVC Chaos (NGM-2690 ~ NGH-2690).neo", nullptr);
            //worked = grab_ROM(&ROMs, which, "The King of Fighters '98 - The Slugfest ~ King of Fighters '98 - Dream Match Never Ends (NGH-2420).neo", nullptr);
            //worked = grab_ROM(&ROMs, which, "The King of Fighters 2003 (NGH-2710).neo", nullptr);
            //worked = grab_ROM(&ROMs, which, "Viewpoint.neo", nullptr);
            //worked = grab_ROM(&ROMs, which, "Zupapa!.neo", nullptr);
            break;
        case jsm::systems::PS1:
            //RenderPolygon16BPP
            //worked = grab_ROM(&ROMs, which, "PS1MiniPadTestV0.4.exe", nullptr); // slammin'!
            //worked = grab_ROM(&ROMs, which, "psxtest_cpu.exe", nullptr); // slammin'!
            //worked = grab_ROM(&ROMs, which, "psxtest_cpx.exe", nullptr);
            //worked = grab_ROM(&ROMs, which, "psxtest_gte.exe", nullptr);
            //worked = grab_ROM(&ROMs, which, "ps1-tests-built/cpu/cop/cop.exe", nullptr);
            //worked = grab_ROM(&ROMs, which, "ps1-tests-built/dma/otc-test/otc-test.exe", nullptr);
            //worked = grab_ROM(&ROMs, which, "ps1-tests-built/dma/chain-looping/chain-looping.exe", nullptr);
            //worked = grab_ROM(&ROMs, which, "ps1-tests-built/gte/test-all/test-all.exe", nullptr);
            //worked = grab_ROM(&ROMs, which, "ps1-tests-built/timers/timers.exe", nullptr);
            //worked = grab_ROM(&ROMs, which, "ps1-tests-built/spu/memory-transfer/memory-transfer.exe", nullptr);
            //worked = grab_ROM(&ROMs, which, "ps1-tests-built/spu/test/test.exe", nullptr);
            //worked = grab_ROM(&ROMs, which`, "ps1-tests-built/gte-fuzz/gte-fuzz.exe", nullptr);
            //worked = grab_ROM(&ROMs, which, "redux_cpu.exe", nullptr);
            //worked = grab_ROM(&ROMs, which, "pad.exe", nullptr);
            //worked = grab_ROM(&ROMs, which, "PSX/MDEC/DCTBlockDecode/CLUT4BPP/DCTBlockDecodeCLUT4BPP.exe", nullptr);
            // DCTBlockDecodeCLUT4BPP
#undef CTEST
#define CTEST(x) worked = grab_ROM(&ROMs, which, "PSX/DMA/DMA" x "/DMA" x ".exe", nullptr)
            //CTEST("BLOCKGPU");
            //CTEST("CD");
            //CTEST("GPU");
            //CTEST("LINKEDLIST");
            //CTEST("OTCData");
            //CTEST("SPU");
            //CTEST("SPUDELAY");

#undef CTEST
#define CTEST(x) worked = grab_ROM(&ROMs, which, "PSX/TIMER/" x "/" x ".exe", nullptr)
            //CTEST("TimerCalib");
            //CTEST("TimerHBlank");
            //CTEST("TimerSet");
            //CTEST("TimerWrap");
#undef CTEST
#define CTEST(x) worked = grab_ROM(&ROMs, which, "PSX/BUS/" x "/" x ".exe", nullptr)
            //CTEST("Load816Unalign");
            //CTEST("LoadStoreReg");
            //CTEST("LoadStoreRegUnalign8Bit");
            //CTEST("LoadStoreRegUnalign16Bit");
#undef CTEST
#define CTEST(x) worked = grab_ROM(&ROMs, which, "PSX/CPUTest/CPU/" x "/CPU" x ".exe", nullptr)
            //CTEST("ADD");
            //CTEST("ADDI");
            //CTEST("ADDIU");
            //CTEST("ADDU");
            //CTEST("AND");
            //CTEST("ANDI");
            //CTEST("DIV");
            //CTEST("DIVU");
            //CTEST("MULT");
            //CTEST("MULTU");
            //CTEST("NOR");
            //CTEST("OR");
            //CTEST("ORI");
            //CTEST("SUB");
            //CTEST("SUBU");
            //CTEST("XOR");
            //CTEST("XORI");
#undef CTEST
#define CTEST(x) worked = grab_ROM(&ROMs, which, "PSX/CPUTest/CPU/LOADSTORE/" x "/CPU" x ".exe", nullptr);
            //CTEST("LB");
            //CTEST("LH");
            //CTEST("LW");
            //CTEST("SB");
            //CTEST("SH");
            //CTEST("SW");
#undef CTEST
#define CTEST(x) worked = grab_ROM(&ROMs, which, "PSX/CPUTest/CPU/SHIFT/" x "/CPU" x ".exe", nullptr);
            //CTEST("SLL");
            //CTEST("SLLV");
            //CTEST("SRA");
            //CTEST("SRAV");
            //CTEST("SRL");
            //CTEST("SRLV");
#undef CTEST
            //worked = grab_ROM(&ROMs, which, "PSX/GPU/16BPP/RenderPolygon/RenderPolygon16BPP.exe", nullptr);
            //worked = grab_ROM(&ROMs, which, "PSX/JOY/Joypad/Joypad.exe", nullptr);
            //worked = grab_ROM(&ROMs, which, "PSX/GPU/16BPP/RenderTexturePolygon/CLUT4BPP/RenderTexturePolygonCLUT4BPP.exe", nullptr);
            //worked = grab_ROM(&ROMs, which, "PSX/GPU/16BPP/RenderTexturePolygon/CLUT8BPP/RenderTexturePolygonCLUT8BPP.exe", nullptr);
            //worked = grab_ROM(&ROMs, which, "PSX/GPU/16BPP/RenderTexturePolygon/15BPP/RenderTexturePolygon15BPP.exe", nullptr);
            //worked = grab_ROM(&ROMs, which, "PSX/GPU/16BPP/RenderRectangle/RenderRectangle16BPP.exe", nullptr);
            //worked = grab_ROM(&ROMs, which, "PSX/GPU/16BPP/RenderTextureWindowRectangle/CLUT8BPP/RenderTextureWindowRectangleCLUT8BPP.exe", nullptr);
            //worked = grab_ROM(&ROMs, which, "PSX/GPU/16BPP/RenderTexturePolygon/MASK15BPP/RenderTexturePolygonMASK15BPP.exe", nullptr);
            //worked = grab_ROM(&ROMs, which, "PSX/GPU/16BPP/RenderTextureRectangle/CLUT4BPP/RenderTextureRectangleCLUT4BPP.exe", nullptr);
            //worked = grab_ROM(&ROMs, which, "PSX/GPU/16BPP/RenderTextureRectangle/CLUT8BPP/RenderTextureRectangleCLUT8BPP.exe", nullptr);
            //worked = grab_ROM(&ROMs, which, "PSX/GPU/16BPP/RenderTextureRectangle/15BPP/RenderTextureRectangle15BPP.exe", nullptr); // needs help!
            //worked = grab_ROM(&ROMs, which, "PSX/GPU/16BPP/RenderTextureRectangle/MASK15BPP/RenderTextureRectangleMASK15BPP.exe", nullptr);


            //worked = grab_ROM(&ROMs, which, "PSX/GPU/16BPP/RenderTextureRectangle/CLUT4BPP/RenderTextureRectangleCLUT4BPP.exe", nullptr);
            //worked = grab_ROM(&ROMs, which, "PSX/GPU/16BPP/MemoryTransfer/MemoryTransfer16BPP.exe", nullptr);
            //worked = grab_ROM(&ROMs, which, "PSX/HelloWorld/16BPP/HelloWorld16BPP.exe", nullptr);
            //worked = grab_ROM(&ROMs, which, "PSX/GTE/GTETransfer/GTETransfer.exe", nullptr);

            //worked = grab_ROM(&ROMs, which, "VBLANK.exe", nullptr);
//#define SIDELOAD_WITH_CD
#ifdef SIDELOAD_WITH_CD
            worked = grab_cue(&ROMs, which, "test", nullptr); // Redux CD tests
#endif
            //worked = grab_cue(&ROMs, which, "Resident Evil (USA)", nullptr); // appears to work somewhat
            //worked = grab_cue(&ROMs, which, "Resident Evil 2 (USA) (Disc 1)", nullptr);
            //worked = grab_cue(&ROMs, which, "Silent Hill (USA)", nullptr);
            //worked = grab_cue(&ROMs, which, "Metal Slug X (USA)", nullptr);
            //worked = grab_cue(&ROMs, which, "Gran Turismo 2 (USA) (Simulation Mode) (Rev 1)", nullptr);
            //worked = grab_cue(&ROMs, which, "Final Fantasy VII (USA) (Disc 1)", nullptr);
            //worked = grab_cue(&ROMs, which, "Tony Hawks Pro Skater 2 (USA)", nullptr); // error parsing cue
            //worked = grab_cue(&ROMs, which, "Crash Bandicoot 1", nullptr);
            //worked = grab_cue(&ROMs, which, "mk2", nullptr);
            worked = grab_cue(&ROMs, which, "Doom (USA)", nullptr);
            //worked = grab_cue(&ROMs, which, "Hydro Thunder (USA)", nullptr);
            //worked = grab_cue(&ROMs, which, "Cool Boarders 2 (USA)", nullptr);
            //worked = grab_cue(&ROMs, which, "Ridge Racer (USA)", nullptr);
            //worked = grab_cue(&ROMs, which, "Mega Man X4 (USA)", nullptr);
            //worked = grab_cue(&ROMs, which, "Spyro the Dragon (USA)", nullptr);
            //worked = grab_cue(&ROMs, which, "Spyro - Year of the Dragon (USA)", nullptr);
            //worked = grab_cue(&ROMs, which, "CTR - Crash Team Racing (USA)", nullptr);
            //worked = grab_cue(&ROMs, which, "Metal Slug X (USA)", nullptr);
            //worked = grab_cue(&ROMs, which, "Myst (USA)", nullptr);
            //worked = grab_cue(&ROMs, which, "Frogger (USA)", nullptr);
            //worked = grab_cue(&ROMs, which, "Vanishing Point (USA)", nullptr);

            //worked = grab_cue(&ROMs, which, "Rayman 2 - The Great Escape (USA) (En,Fr,Es)", nullptr);
            //worked = grab_cue(&ROMs, which, "Earthworm Jim 2 (Europe)", nullptr);
            //worked = grab_cue(&ROMs, which, "Castlevania - Symphony of the Night (USA)", nullptr);
            //worked = grab_cue(&ROMs, which, "Metal Gear Solid (USA) (Disc 1) (v1.1)", nullptr); // seems to work
            //worked = grab_cue(&ROMs, which, "Tomb Raider (USA) (Rev 1)", nullptr); // req. MDEC
            //worked = grab_cue(&ROMs, which, "WipEout (USA)", nullptr); // req. MDEC
            //worked = grab_cue(&ROMs, which, "Tomb Raider II - Starring Lara Croft (USA) (Rev 2)", nullptr);
            //worked = grab_cue(&ROMs, which, "Tomb Raider II - Starring Lara Croft (USA)", nullptr);
            //worked = grab_cue(&ROMs, which, "Legacy of Kain - Soul Reaver (USA)", nullptr);
            //
            //worked = grab_cue(&ROMs, which, "Parasite Eve II (Disc 1)", nullptr);
            break;
        case jsm::systems::GBA:
            //worked = grab_ROM(&ROMs, which, "OpenLara.gba", nullptr);
            //worked = grab_ROM(&ROMs, which, "panda.gba", nullptr);
            //worked = grab_ROM(&ROMs, which, "armwrestler-gba-fixed.gba", nullptr);
            //worked = grab_ROM(&ROMs, which, "arm.gba", nullptr);
            //worked = grab_ROM(&ROMs, which, "thumb.gba", nullptr);
            //worked = grab_ROM(&ROMs, which, "retaddr.gba", nullptr);

            //worked = grab_ROM(&ROMs, which, "tonc/hello.gba", nullptr);
            //worked = grab_ROM(&ROMs, which, "tonc/dma_demo.gba", nullptr);
            //worked = grab_ROM(&ROMs, which, "tonc/m3_demo.gba", nullptr);
            //worked = grab_ROM(&ROMs, which, "tonc/pageflip.gba", nullptr);
            //worked = grab_ROM(&ROMs, which, "tonc/bm_modes.gba", nullptr);
            //worked = grab_ROM(&ROMs, which, "tonc/key_demo.gba", nullptr);
            //worked = grab_ROM(&ROMs, which, "tonc/obj_demo.gba", nullptr);
            //worked = grab_ROM(&ROMs, which, "tonc/brin_demo.gba", nullptr);
            //worked = grab_ROM(&ROMs, which, "tonc/sbb_reg.gba", nullptr);
            //worked = grab_ROM(&ROMs, which, "tonc/cbb_demo.gba", nullptr);
            //worked = grab_ROM(&ROMs, which, "tonc/obj_aff.gba", nullptr);
            //worked = grab_ROM(&ROMs, which, "tonc/sbb_aff.gba", nullptr);
            //worked = grab_ROM(&ROMs, which, "tonc/dma_demo.gba", nullptr);
            //worked = grab_ROM(&ROMs, which, "tonc/irq_demo.gba", nullptr);
            //worked = grab_ROM(&ROMs, which, "tonc/tmr_demo.gba", nullptr);
            //worked = grab_ROM(&ROMs, which, "tonc/m7_demo.gba", nullptr);
            //worked = grab_ROM(&ROMs, which, "tonc/m7_demo_mb.gba", nullptr);
            //worked = grab_ROM(&ROMs, which, "tonc/m7_ex.gba", nullptr);
            //worked = grab_ROM(&ROMs, which, "tonc/mos_demo.gba", nullptr);

            //worked = grab_ROM(&ROMs, which, "poke_song_player.gba", nullptr);
            //worked = grab_ROM(&ROMs, which, "sram.gba", nullptr);
            //worked = grab_ROM(&ROMs, which, "flash64.gba", nullptr);
            //worked = grab_ROM(&ROMs, which, "flash128.gba", nullptr);
            //worked = grab_ROM(&ROMs, which, "memory.gba", nullptr);
            //worked = grab_ROM(&ROMs, which, "bios.gba", nullptr);

            //worked = grab_ROM(&ROMs, which, "alttp.gba", nullptr); // works!
            //worked = grab_ROM(&ROMs, which, "castlevania_hod.gba", nullptr); // works!

            //worked = grab_ROM(&ROMs, which, "kirby.gba", nullptr); // works!
            //worked = grab_ROM(&ROMs, which, "pokemon_ruby.gba", nullptr); // needs work! RTC, flash
            //worked = grab_ROM(&ROMs, which, "pokemon_emerald.gba", nullptr); // needs work! RTC, flash
            //worked = grab_ROM(&ROMs, which, "sonic_advance.gba", nullptr);
            //worked = grab_ROM(&ROMs, which, "sma2.gba", nullptr); // works great!
            //worked = grab_ROM(&ROMs, which, "advance_wars.gba", nullptr); // works!
            //worked = grab_ROM(&ROMs, which, "metroid_fusion.gba", nullptr); // works!
            //worked = grab_ROM(&ROMs, which, "doom.gba", nullptr); // works!
            //worked = grab_ROM(&ROMs, which, "doom2.gba", nullptr); // works great!
            //worked = `grab_ROM(&ROMs, which, "duke3d.gba", nullptr); // works great!
            //worked = grab_ROM(&ROMs, which, "fzero.gba", nullptr); // works great!
            worked = grab_ROM(&ROMs, which, "mariokart.gba", nullptr); // works great!
            //worked = grab_ROM(&ROMs, which, "superstar.gba", nullptr); // works great!
            //worked = grab_ROM(&ROMs, which, "sma4.gba", nullptr);
            //worked = grab_ROM(&ROMs, which, "sma3.gba", nullptr); // works!
            //worked = grab_ROM(&ROMs, which, "pcrysound.gba", nullptr); // works!
            //worked = grab_ROM(&ROMs, which, "airforce_delta_storm.gba", nullptr); // works!
            //worked = grab_ROM(&ROMs, which, "advance_guardian_heroes.gba", nullptr); // works!
            //worked = grab_ROM(&ROMs, which, "supersonic_warriors.gba", nullptr); // works!
            //worked = grab_ROM(&ROMs, which, "dbaa.gba", nullptr); // works!

            //worked = grab_ROM(&ROMs, which, "goldensun.gba", nullptr);
            //worked = grab_ROM(&ROMs, which, "goldensun2.gba", nullptr);
            //worked = grab_ROM(&ROMs, which, "wario4.gba", nullptr);
            //worked = grab_ROM(&ROMs, which, "suite.gba", nullptr);
            //worked = grab_ROM(&ROMs, which, "suite_built.gba", nullptr);
            //worked = grab_ROM(&ROMs, which, "oh my gah.gba", nullptr);
            //worked = grab_ROM(&ROMs, which, "aging_cart.gba", nullptr);
            //worked = grab_ROM(&ROMs, which, "funni.gba", nullptr);
            //worked = grab_ROM(&ROMs, which, "armfrag.gba", nullptr);

            //worked = grab_ROM(&ROMs, which, "hm_fomt.gba", nullptr); // needs work! RTC, flash
            //worked = grab_ROM(&ROMs, which, "metroid_zero.gba", nullptr); // works great!
            //worked = grab_ROM(&ROMs, which, "dual_blades.gba", nullptr); // works great!
            //worked = grab_ROM(&ROMs, which, "sonic_battle.gba", nullptr); // works great
            //worked = grab_ROM(&ROMs, which, "gunstar.gba", nullptr); // works great
            //worked = grab_ROM(&ROMs, which, "rave_master.gba", nullptr);
            //worked = grab_ROM(&ROMs, which, "ecks_sever2.gba", nullptr);
            //worked = grab_ROM(&ROMs, which, "ecks_sever.gba", nullptr); // works great!
            //worked = grab_ROM(&ROMs, which, "007_nightfire.gba", nullptr);
            //worked = grab_ROM(&ROMs, which, "mmz4.gba", nullptr);
            //worked = grab_ROM(&ROMs, which, "mmz.gba", nullptr);
            //worked = grab_ROM(&ROMs, which, "mmz3.gba", nullptr);
            //worked = grab_ROM(&ROMs, which, "drill_dozer.gba", nullptr);
            //worked = grab_ROM(&ROMs, which, "rhythm_tengoku.gba", nullptr);
            //worked = grab_ROM(&ROMs, which, "driv3r.gba", nullptr); // works great
            //worked = grab_ROM(&ROMs, which, "crazy_taxi.gba", nullptr); // plays fine, gfx issues in menus
            //worked = grab_ROM(&ROMs, which, "lunar_legend.gba", nullptr);
            //worked = grab_ROM(&ROMs, which, "tony_hawk_downhill.gba", nullptr); // works great
            //worked = grab_ROM(&ROMs, which, "car_battler_joe.gba", nullptr);
            //worked = grab_ROM(&ROMs, which, "nfs_most_wanted.gba", nullptr);
            //worked = grab_ROM(&ROMs, which, "big_mutha_truckers.gba", nullptr); // way too fast?
            //worked = grab_ROM(&ROMs, which, "kill_switch.gba", nullptr);
            //worked = grab_ROM(&ROMs, which, "ssx3.gba", nullptr); // works great
            //worked = grab_ROM(&ROMs, which, "fzero_gp_legends.gba", nullptr);
            //worked = grab_ROM(&ROMs, which, "srr.gba", nullptr);
            //worked = grab_ROM(&ROMs, which, "astro_boy.gba", nullptr);
            //worked = grab_ROM(&ROMs, which, "goodbye_galaxy.gba", nullptr);
            //worked = grab_ROM(&ROMs, which, "minish_cap.gba", nullptr);
            //worked = grab_ROM(&ROMs, which, "motoracer.gba", nullptr);

            //worked = grab_ROM(&ROMs, which, "gang-ldmstm.gba", nullptr);


            //dbg_enable_trace();

            break;
        case jsm::systems::SNES:
            //worked = grab_ROM(&ROMs, which, "smw.sfc", nullptr); // works!
            //worked = grab_ROM(&ROMs, which, "link_to_the_past.sfc", nullptr); // works! sprite issues
            //worked = grab_ROM(&ROMs, which, "super_metroid.sfc", nullptr); // gfx issues
            //worked = grab_ROM(&ROMs, which, "megamanx.sfc", nullptr); // some sound then notjing
            //worked = grab_ROM(&ROMs, which, "smwallstars.smc", nullptr); // seems to work!
            //worked = grab_ROM(&ROMs, which, "chrono_trigger.sfc", nullptr); //
            //worked = grab_ROM(&ROMs, which, "run_saber.sfc", nullptr); //

            //worked = grab_ROM(&ROMs, which, "contra3.sfc", nullptr); // SA-1 not support
            //worked = grab_ROM(&ROMs, which, "donkey_kong_c.smc", nullptr); // R logo appears, sound, then hangs
            //worked = grab_ROM(&ROMs, which, "donkey_kong_c2.smc", nullptr); // blank screen
            //worked = grab_ROM(&ROMs, which, "fzero.smc", nullptr); // no mode7 yet
            //worked = grab_ROM(&ROMs, which, "kirby_super.sfc", nullptr); // SA-1
            //worked = grab_ROM(&ROMs, which, "lostvikings.sfc", nullptr); // blank screen
            //worked = grab_ROM(&ROMs, which, "lostvikings2.sfc", nullptr); // ?
            //worked = grab_ROM(&ROMs, which, "mechwarrior.sfc", nullptr); // lorom. corrupt gfx, gets semi i ngame?
            //worked = grab_ROM(&ROMs, which, "pilotwings.sfc", nullptr); // ?
            //worked = grab_ROM(&ROMs, which, "pockyrocky.sfc", nullptr); //
            //worked = grab_ROM(&ROMs, which, "pockyrocky2.smc", nullptr); // appears to work
            //worked = grab_ROM(&ROMs, which, "rotj.smc", nullptr); // lots of gfx corruption
            //worked = grab_ROM(&ROMs, which, "shadowrun.sfc", nullptr); // kinda works? needs compare
            //worked = grab_ROM(&ROMs, which, "simcity.smc", nullptr); // gfx issues
            //worked = grab_ROM(&ROMs, which, "starfox.sfc", nullptr); // ?
            //worked = grab_ROM(&ROMs, which, "tetris_attack.sfc", nullptr); // ?
            //worked = grab_ROM(&ROMs, which, "tmnt4.sfc", nullptr); // cant even load ROM?
            break;
        case jsm::systems::TURBOGRAFX16:
            //worked = grab_ROM(&ROMs, which, "test.pce", nullptr);
            //worked = grab_ROM(&ROMs, which, "padtest2.pce", nullptr);
            //worked = grab_ROM(&ROMs, which, "bomberman.pce", nullptr); // work good
            //worked = grab_ROM(&ROMs, which, "detana_twinbee.pce", nullptr); // work good
            //worked = grab_ROM(&ROMs, which, "gradius_jp.pce", nullptr); // work good
            //worked = grab_ROM(&ROMs, which, "bonks_adventure.pce", nullptr); // seems work good
            //worked = grab_ROM(&ROMs, which, "screen_dim.pce", nullptr);
            //worked = grab_ROM(&ROMs, which, "cpu_test.pce", nullptr);
            //worked = grab_ROM(&ROMs, which, "wavy_sky.pce", nullptr);
            //worked = grab_ROM(&ROMs, which, "magical_chase.pce", nullptr); // works
            //worked = grab_ROM(&ROMs, which, "rtype.pce", nullptr); // seems perfect
            //worked = grab_ROM(&ROMs, which, "airzonk.pce", nullptr); // seems to work
            //worked = grab_ROM(&ROMs, which, "alien_crush.pce", nullptr); // works fine
            //worked = grab_ROM(&ROMs, which, "takeda_shingen.pce", nullptr); // bad vram
            //worked = grab_ROM(&ROMs, which, "bonk3.pce", nullptr); // seems to work fine
            //worked = grab_ROM(&ROMs, which, "dungeon_explorer.pce", nullptr); // has issues
            //worked = grab_ROM(&ROMs, which, "fantasy_zone.pce", nullptr); // seems to work fine
            //worked = grab_ROM(&ROMs, which, "keith_courage.pce", nullptr); // seems to work fine
            //worked = grab_ROM(&ROMs, which, "laxe.pce", nullptr); // seems to work fine
            worked = grab_ROM(&ROMs, which, "laxe2.pce", nullptr); // crash/hang after "press run"
            //worked = grab_ROM(&ROMs, which, "p47.pce", nullptr); // seems work good
            //worked = grab_ROM(&ROMs, which, "finalsoldier.pce", nullptr); // slight scroll instability
            //worked = grab_ROM(&ROMs, which, "neutopia.pce", nullptr); // seems good
            //worked = grab_ROM(&ROMs, which, "space_harrier.pce", nullptr); // seems good
            //worked = grab_ROM(&ROMs, which, "afterburner2.pce", nullptr); // seems good
            //worked = grab_ROM(&ROMs, which, "outrun.pce", nullptr); // seems good
            //worked = grab_ROM(&ROMs, which, "splatterhouse.pce", nullptr); // BAD!
            //worked = grab_ROM(&ROMs, which, "blazing_lazers.pce", nullptr); // seems good
            //worked = grab_ROM(&ROMs, which, "silent_debuggers.pce", nullptr); // seems work good
            //worked = grab_ROM(&ROMs, which, "neutopia2.pce", nullptr); // good
            //worked = grab_ROM(&ROMs, which, "devils_crush.pce", nullptr); // WORKS GOOD

            break;
        case jsm::systems::GENESIS_USA:
        case jsm::systems::GENESIS_JAP:
        case jsm::systems::MEGADRIVE_PAL:
            //dbg_enable_trace();
            //worked = grab_ROM(&ROMs, which, "sonic2.md", nullptr); // works!
            //worked = grab_ROM(&ROMs, which, "sonic3.md", nullptr); // works!
            //worked = grab_ROM(&ROMs, which, "sor3.md", nullptr);
            //worked = grab_ROM(&ROMs, which, "xmen.md", nullptr); // works!
            //worked = grab_ROM(&ROMs, which, "window.bin", nullptr); // works!
            //worked = grab_ROM(&ROMs, which, "sonick3.md", nullptr); // works!
            //worked = grab_ROM(&ROMs, which, "ecco.md", nullptr); // works!
            //worked = grab_ROM(&ROMs, which, "ecco2.md", nullptr); // cant detect console properly
            //worked = grab_ROM(&ROMs, which, "gunstar_heroes.md", nullptr); // works fine!
            //worked = grab_ROM(&ROMs, which, "overdrive.bin", nullptr);
            //worked = grab_ROM(&ROMs, which, "overdrive2.bin", nullptr);
            //worked = grab_ROM(&ROMs, which, "dynamite_headdy.bin", nullptr); // works!
            //worked = grab_ROM(&ROMs, which, "ristar.md", nullptr); // works!
            //worked = grab_ROM(&ROMs, which, "castlevania_b.md", nullptr); // works!
            //worked = grab_ROM(&ROMs, which, "contra_hc_jp.md", nullptr); // works great!
            //worked = grab_ROM(&ROMs, which, "sor2.md", nullptr); // works fine
            //worked = grab_ROM(&ROMs, which, "s1built.bin", nullptr); // works!
            //worked = grab_ROM(&ROMs, which, "batman.md", nullptr); // works!
            //worked = grab_ROM(&ROMs, which, "gen_test_ym.bin", nullptr); // works!
            //worked = grab_ROM(&ROMs, which, "outrun2019.bin", nullptr); // works!
            //worked = grab_ROM(&ROMs, which, "junglestrike.md", nullptr); // works!
            //worked = grab_ROM(&ROMs, which, "battletech.md", nullptr); // works!
            //worked = grab_ROM(&ROMs, which, "test1536.bin", nullptr); // works!
            //worked = grab_ROM(&ROMs, which, "crusader_centy.md", nullptr); // works!
            //worked = grab_ROM(&ROMs, which, "xmen2.md", nullptr); // works!
            //worked = grab_ROM(&ROMs, which, "roadrash2.bin", nullptr); // works fine
            //worked = grab_ROM(&ROMs, which, "roadrash.md", nullptr); // works

            //worked = grab_ROM(&ROMs, which, "240p.bin", nullptr);
            //worked = grab_ROM(&ROMs, which, "240p_emu.bin", nullptr);

            //worked = grab_ROM(&ROMs, which, "alien_storm.md", nullptr); // hangs at menu
            //worked = grab_ROM(&ROMs, which, "tyrants.md", nullptr); // :-(
            // worked = grab_ROM(&ROMs, which, "afterburner2.md", nullptr); // works!
            // worked = grab_ROM(&ROMs, which, "skitchin.md", nullptr); // works
            //worked = grab_ROM(&ROMs, which, "haunting.md", nullptr); // works
            //worked = grab_ROM(&ROMs, which, "herzog_zwei.md", nullptr); // works
            //worked = grab_ROM(&ROMs, which, "desert_strike.md", nullptr);
            //worked = grab_ROM(&ROMs, which, "flashback.md", nullptr); // 6-button doesn't work?
            //worked = grab_ROM(&ROMs, which, "rocket_knight.md", nullptr); // works

            //worked = grab_ROM(&ROMs, which, "vectorman.md", nullptr); // well!
            //worked = grab_ROM(&ROMs, which, "vectorman2.md", nullptr); // well!
            // worked = grab_ROM(&ROMs, which, "golden_axe3.bin", nullptr); // fine!
            //worked = grab_ROM(&ROMs, which, "golden_axe2.md", nullptr); // fine
            //worked = grab_ROM(&ROMs, which, "golden_axe.md", nullptr); // fine
            //worked = grab_ROM(&ROMs, which, "ghostbusters.md", nullptr); // fine
            //worked = grab_ROM(&ROMs, which, "kid_chameleon.md", nullptr); // fine
            //worked = grab_ROM(&ROMs, which, "kawasaki_superbike.md", nullptr); // fine
            //worked = grab_ROM(&ROMs, which, "panorama_cotton.bin", nullptr); // seems fine
            //worked = grab_ROM(&ROMs, which, "shadow_run.md", nullptr); // works fine!
            //worked = grab_ROM(&ROMs, which, "mega_man_wiley_wars.md", nullptr); // works fine!
            //worked = grab_ROM(&ROMs, which, "lost_vikings.md", nullptr); // does nothing
            //worked = grab_ROM(&ROMs, which, "shining_force_2.md", nullptr); // works fine!
            // worked = grab_ROM(&ROMs, which, "shining_force.md", nullptr); // works!
            // worked = grab_ROM(&ROMs, which, "battletoads.md", nullptr); // fine!
            //worked = grab_ROM(&ROMs, which, "comix_zone.md", nullptr); // works!


            //worked = grab_ROM(&ROMs, which, "megaturrican.md", nullptr); // works fine
            //worked = grab_ROM(&ROMs, which, "mortalkombat3.md", nullptr); // works fine
            worked = grab_ROM(&ROMs, which, "gauntlet4.md", nullptr); // runs unsteadily?
            //worked = grab_ROM(&ROMs, which, "tmnt_hyperstone.md", nullptr); // works fine
            //worked = grab_ROM(&ROMs, which, "zero_squirrel.md", nullptr); // works fine
            //worked = grab_ROM(&ROMs, which, "zero_tolerance.md", nullptr); // works fine
            //worked = grab_ROM(&ROMs, which, "zero_tolerance_2.md", nullptr); // works fine
            //worked = grab_ROM(&ROMs, which, "devilish.md", nullptr); // works fine
            //worked = grab_ROM(&ROMs, which, "toy_story.md", nullptr); // works great
            //worked = grab_ROM(&ROMs, which, "rbi_baseball_3.md", nullptr); // works fine
            //worked = grab_ROM(&ROMs, which, "rampart.md", nullptr); // no display
            //worked = grab_ROM(&ROMs, which, "ranger_x.md", nullptr); // works great
            //worked = grab_ROM(&ROMs, which, "red_zone.md", nullptr); // works great


            //worked = grab_ROM(&ROMs, which, "wonder_boy4.md", nullptr); // seems fine
            //worked = grab_ROM(&ROMs, which, "duke3d.bin", nullptr); // issues!
            //worked = grab_ROM(&ROMs, which, "star_cruiser.bin", nullptr); // seems fine? hard to say
            //worked = grab_ROM(&ROMs, which, "sonic_spinball.md", nullptr); // same sprite priority issue
            //worked = grab_ROM(&ROMs, which, "sonic_3d_blast.md", nullptr); // FMV vibrates up and down. otherwise works well
            //worked = grab_ROM(&ROMs, which, "blockout.md", nullptr); // works well
            //worked = grab_ROM(&ROMs, which, "street_fighter_2_special_championship.md", nullptr); // works great!
            //worked = grab_ROM(&ROMs, which, "sprite_masking_test.bin", nullptr); // works great!
            //worked = grab_ROM(&ROMs, which, "direct_color_dma.bin", nullptr); // does nothing
            //worked = grab_ROM(&ROMs, which, "alien_soldier_jp.md", nullptr); // works well
            //worked = grab_ROM(&ROMs, which, "alien_soldier_usa.md", nullptr);
            //worked = grab_ROM(&ROMs, which, "beyond_oasis.md", nullptr);
            //worked = grab_ROM(&ROMs, which, "granada.md", nullptr);

            break;
        default:
            printf("\nSYS NOT IMPLEMENTED!");
    }
    load_current_ROMs();
}

void full_system::w2_create_node_texture(W2VIEW &myv, debug::waveform2::view_node *node, bool force_create) {
    if (node->children.size() == 0 || force_create) {
        // Create/update texture
        W2FORM &wf = myv.waveform2s.emplace_back();
;
        wf.enabled = true;
        u32 height;
        u32 szpo2;
        u32 len;
        switch (node->data.kind) {
            case debug::waveform2::wk_medium:
                height = 80;
                szpo2 = 256; // 200x80
                len = 200;
                break;
            case debug::waveform2::wk_big:
                height = 80;
                szpo2 = 512; // 400x80 max
                len = 400;
                break;
            case debug::waveform2::wk_small:
                height = 40;
                szpo2 = 128; // 100x40
                len = 100;
                break;
            default:
                NOGOHERE;
        }

        wf.height = height;
        wf.wf = &node->data;
        wf.szpo2 = szpo2;
        wf.len = len;
        node->data.samples_requested = len;
        node->user_ptr = &myv.waveform2s.back();
    }
    else {
        for (auto &ch : node->children)
            w2_create_node_texture(myv, &ch, false);
    }
}

void full_system::add_waveform2_view(u32 idx) {
    auto &dview = dbgr.views.at(idx);
    W2VIEW &myv = waveform2_views.emplace_back();
    myv.view = &dview.waveform2;
    myv.waveform2s.reserve(256); // Reserve space for 256 waveforms! We won't need it all!!!
    myv.did_textures = false;

    w2_create_node_texture(myv, &myv.view->root, true);
    w2_create_node_texture(myv, &myv.view->root, false);
}

void full_system::add_waveform_view(u32 idx)
{
    auto &dview = dbgr.views.at(idx);
    WVIEW myv;
    myv.view = &dview.waveform;
    for (u32 i = 0; i < dview.waveform.waveforms.size(); i++) {
        WFORM wf;
        wf.enabled = true;
        wf.height = 80;
        wf.wf = &dview.waveform.waveforms.at(i);
        myv.waveforms.push_back(wf);
    }

    waveform_views.push_back(myv);
}

void full_system::add_console_view(u32 idx)
{
    auto *dview = &dbgr.views.at(idx);
    CVIEW myv;
    myv.view = dview;
    console_views.push_back(myv);
}


void full_system::add_trace_view(u32 idx)
{
    auto *dview = &dbgr.views.at(idx);
    TVIEW myv;
    myv.view = dview;
    trace_views.push_back(myv);
}

void full_system::add_dbglog_view(u32 idx)
{
    auto *dview = &dbgr.views.at(idx);
    DLVIEW myv;
    myv.view = dview;
    dlviews.push_back(myv);
}


void full_system::add_disassembly_view(u32 idx)
{
    auto *dview = &dbgr.views.at(idx);
    //printf("\nAdding disassembly view %s:", dview.disassembly.processor_name.ptr);
    DVIEW myv;
    myv.view = dview;
    myv.dasm_rows.reserve(150);
    for (u32 i = 0; i < 200; i++) {
        myv.dasm_rows.emplace_back();
        //memset(das, 0, sizeof(*das));
    }

    dasm_views.push_back(myv);
}

void full_system::add_image_view(u32 idx)
{
    auto *dview = &dbgr.views.at(idx);
    IVIEW myv;
    myv.enabled = true;
    myv.view = dview;
    images.push_back(myv);
    //printf("\nAdding image view %s: width %d", myv.view->image.label, myv.view->image.width);
}

void full_system::setup_debugger_interface()
{
    sys->setup_debugger_interface(dbgr);
    waveform2_views.reserve(4); // 4 different audio chips...
    debugger_setup = 1;
    for (u32 i = 0; i < dbgr.views.size(); i++) {
        auto &view = dbgr.views.at(i);
        switch(view.kind) {
            case dview_disassembly:
                add_disassembly_view(i);
                break;
            case dview_memory:
                memory.view = &view.memory;
                break;
            case dview_source_listing:
                source_listing.view = &view.source_listing;
                break;
            case dview_events:
                events.view = &view.events;
                break;
            case dview_trace:
                add_trace_view(i);
                break;
            case dview_dbglog:
                add_dbglog_view(i);
                break;
            case dview_console:
                add_console_view(i);
                break;
            case dview_image:
                add_image_view(i);
                break;
            case dview_waveforms:
                add_waveform_view(i);
                break;
            case dview_waveform2:
                add_waveform2_view(i);
                break;
            default:
                assert(1==2);
        }
    }
}


static system_config build_slot_config(jsm::systems which, AppSettings* settings)
{
    system_config cfg{};
    if (!settings) return cfg;
    const char* ck = AppSettings::sys_to_core_key(which);
    if (!ck) return cfg;
    for (u32 i = 0; i < 8; i++) {
        std::string card = settings->get_slot_card(ck, i);
        snprintf(cfg.slots[i], sizeof(cfg.slots[i]), "%s", card.c_str());
    }
    return cfg;
}

void full_system::setup_system(jsm::systems which, AppSettings *settings)
{
    clear_runtime_state();

    sys = new_system(which, build_slot_config(which, settings));
    assert(sys);

    setup_ios();
    setup_bios();
    setup_debugger_interface();

    load_default_ROM();

    setup_audio(settings);
    sys->reset();
    blank_output_until_next_frame();
}

void full_system::setup_system_bios_only(jsm::systems which, AppSettings *settings)
{
    clear_runtime_state();

    sys = new_system(which, build_slot_config(which, settings));
    assert(sys);

    setup_ios();
    setup_display();
    setup_display2();
    setup_bios();
    setup_debugger_interface();

    // No ROM/disc/tape — boot straight to the system's BIOS shell
    setup_audio(settings);
    sys->reset();
    blank_output_until_next_frame();
}

u32 full_system::setup_system_from_path(jsm::systems which, const char *path, bool is_folder,
                                        AppSettings *settings)
{
    clear_runtime_state();

    sys = new_system(which, build_slot_config(which, settings));
    assert(sys);

    setup_ios();
    setup_display();
    setup_display2();
    setup_bios();
    setup_debugger_interface();

    ROMs.clear();
    worked = mfs_add_path(&ROMs, path, is_folder);
    load_current_ROMs();

    setup_audio(settings);
    sys->reset();
    blank_output_until_next_frame();

    return worked;
}

void full_system::apply_core_options(AppSettings& settings, const char* core_key)
{
    if (!sys || !core_key || !core_key[0]) return;

    // Core-specific options (registered in populate_core_options; shown in play-window)
    for (auto& opt : sys->options) {
        if (opt.kind == jsm_core_option::OPTION_STRING) {
            std::string sv = settings.get_core_option_str(core_key, opt.key, opt.str_value);
            snprintf(opt.str_value, sizeof(opt.str_value), "%s", sv.c_str());
        } else {
            i32 saved = settings.get_core_option(core_key, opt.key, opt.value);
            opt.value = saved;
            sys->option_changed(opt.key, opt.value);
        }
    }

    // Fast boot — a startup preference shown in Settings, not the play-window,
    // so it is NOT in sys->options. Read and apply it directly. Default: enabled.
    {
        i32 fb = settings.get_core_option(core_key, "fast_boot", 1);
        sys->option_changed("fast_boot", fb);
    }

    // Controller-connected state for each port that this core exposes
    struct { cvec_ptr<physical_io_device>* ptr; int port; } ports[] = {
        { &io.controller1, 1 }, { &io.controller2, 2 },
        { &io.controller3, 3 }, { &io.controller4, 4 }
    };
    for (auto& p : ports) {
        if (!p.ptr->vec) continue;
        bool connected = settings.get_controller_connected(core_key, p.port, p.port == 1);
        sys->controller_connected_changed(p.port, connected);
    }

    // Sync save/SRAM directory preferences
    sram_in_state        = settings.effective_save_sram_with_state(core_key);
    states_in_saves_dir  = settings.effective_states_in_saves_dir(core_key);
    sram_in_saves_dir    = settings.effective_sram_in_saves_dir(core_key);
    output.hide_overscan = can_hide_overscan() &&
                           (settings.get_core_option(core_key, "hide_overscan", 0) != 0);

    // Determine universal_memcard from core option (PS1 only for now)
    universal_memcard = false;
    for (auto& opt : sys->options) {
        if (strcmp(opt.key, "memory_card_mode") == 0) {
            universal_memcard = (opt.value == 1);
            break;
        }
    }

    // Compute and cache save-state and SRAM directory paths.
    // For BIOS-only boots (no ROM loaded) we must use the central dir.
    bool bios_only = ROMs.files.empty();

    std::string global_saves = settings.get_saves_dir();
    if (global_saves.empty())
        global_saves = AppSettings::default_saves_dir();
    std::string core_saves_dir = global_saves + "/" + core_key;

    // Ensure core saves dir exists (best-effort)
    std::error_code ec;
    std::filesystem::create_directories(core_saves_dir, ec);

    if (bios_only || states_in_saves_dir)
        state_save_dir = core_saves_dir;
    else if (!ROMs.files.empty())
        state_save_dir = ROMs.files[0].path;
    else
        state_save_dir = core_saves_dir;

    if (bios_only || sram_in_saves_dir)
        sram_save_dir = core_saves_dir;
    else if (!ROMs.files.empty())
        sram_save_dir = ROMs.files[0].path;
    else
        sram_save_dir = core_saves_dir;

    // Complete deferred SRAM / memory-card setup now that we know the right path.
    if (pending_sram.valid && pending_sram.ps && !pending_sram.ps->ready_to_use) {
        persistent_store &ps = *pending_sram.ps;

        char sram_path[600];
        if (sys->kind == jsm::systems::PS1 && universal_memcard) {
            // Universal memory card — one shared file regardless of game
            snprintf(sram_path, sizeof(sram_path), "%s/memcard1.mcd",
                     sram_save_dir.c_str());
        } else if (!ROMs.files.empty()) {
            // Per-game: derive name from ROM stem
            std::filesystem::path rp =
                std::filesystem::path(ROMs.files[0].path) / ROMs.files[0].name;
            std::string stem = rp.stem().string();
            if (sys->kind == jsm::systems::PS1)
                snprintf(sram_path, sizeof(sram_path), "%s/%s.mcd",
                         sram_save_dir.c_str(), stem.c_str());
            else
                snprintf(sram_path, sizeof(sram_path), "%s/%s.sram",
                         sram_save_dir.c_str(), stem.c_str());
        } else {
            // BIOS-only boot — for PS1 use the user-configured card name (.mcd);
            // for other systems fall back to bios.sram.
            if (sys->kind == jsm::systems::PS1) {
                const char* card_stem = "bios";
                for (auto& opt : sys->options) {
                    if (strcmp(opt.key, "bios_memcard_name") == 0
                        && opt.kind == jsm_core_option::OPTION_STRING
                        && opt.str_value[0])
                        card_stem = opt.str_value;
                }
                snprintf(sram_path, sizeof(sram_path), "%s/%s.mcd",
                         sram_save_dir.c_str(), card_stem);
            } else {
                snprintf(sram_path, sizeof(sram_path), "%s/bios.sram",
                         sram_save_dir.c_str());
            }
        }

        setup_persistent_store(ps, sram_path);
        pending_sram = {};
    }

    // nearest_neighbor: load saved preference; leave the computed default if not yet saved
    {
        i32 nn_saved = settings.get_core_option(core_key, "nearest_neighbor", -1);
        if (nn_saved >= 0) output.nearest_neighbor = (nn_saved != 0);
    }

    setup_present_widgets(settings, core_key);
}

void full_system::update_touch(i32 x, i32 y, i32 button_down)
{
    if (io.touchscreen.vec) {
        physical_io_device &pio = io.touchscreen.get();
        JSM_TOUCHSCREEN &ts = pio.touchscreen;
        x += ts.params.x_offset;
        y += ts.params.y_offset;
        u32 in_screen =  (x >= 0) && (x < ts.params.width) && (y >= 0) && (y < ts.params.height);
        ts.touch.down = in_screen && button_down;
        if (in_screen) {
            ts.touch.x = x;
            ts.touch.y = y;
        }
    }
}

void full_system::get_savestate_filename(char *pth, size_t sz)
{
    read_file_buf *rfb = &ROMs.files[0];
    snprintf(pth, sz, "%s/%s.save", rfb->path, rfb->name);
}

// Forward declarations for screenshot helpers defined later in this file
static u32  ss_float_to_u32(float v);
static u32  ss_clamp_src_pos(float uv, u32 size);
static void ss_scale_rgba(u32 *dst, u32 dst_w, u32 dst_h, u32 *src, u32 src_stride,
                           u32 src_x, u32 src_y, u32 src_w, u32 src_h);

static std::string jsst_meta_get_val(const std::string& meta, const char* key)
{
    std::string k = std::string(key) + "=";
    size_t pos = 0;
    while ((pos = meta.find(k, pos)) != std::string::npos) {
        bool line_start = (pos == 0) || (meta[pos - 1] == '\n') || (meta[pos - 1] == '\r');
        if (line_start) {
            size_t value_start = pos + k.size();
            size_t end = meta.find('\n', value_start);
            std::string out = meta.substr(value_start,
                end == std::string::npos ? std::string::npos : end - value_start);
            if (!out.empty() && out.back() == '\r') out.pop_back();
            return out;
        }
        pos += k.size();
    }
    return {};
}

struct jsst_bios_manifest_entry {
    std::string name;
    size_t size{};
    std::string sha256;
};

static std::string jsst_sha256_bytes_hex(const void *ptr, size_t size)
{
    SHA256_CTX ctx;
    SHA256_Init(&ctx);
    if (ptr && size > 0)
        SHA256_Update(&ctx, ptr, size);

    uint8_t hash[32];
    SHA256_Final(hash, &ctx);

    char hex[65];
    for (int i = 0; i < 32; i++)
        snprintf(hex + i * 2, 3, "%02x", hash[i]);
    hex[64] = 0;
    return hex;
}

static bool jsst_is_mutable_firmware_file(const std::string& name)
{
    std::string lower = name;
    for (char& c : lower)
        c = (char)std::tolower((unsigned char)c);

    return lower == "firmware.bin" ||   // NDS firmware: writable by games
           lower == "dc_flash.bin";     // Dreamcast flash: mutable settings/NVRAM
}

static std::vector<jsst_bios_manifest_entry> jsst_bios_manifest_from_mfs(const multi_file_set& mfs)
{
    std::vector<jsst_bios_manifest_entry> out;
    for (const auto& file : mfs.files) {
        if (!file.buf.ptr || file.buf.size == 0) continue;
        if (jsst_is_mutable_firmware_file(file.name)) continue;

        jsst_bios_manifest_entry entry;
        entry.name = file.name;
        entry.size = file.buf.size;
        entry.sha256 = jsst_sha256_bytes_hex(file.buf.ptr, file.buf.size);
        out.push_back(entry);
    }

    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
        if (a.name != b.name) return a.name < b.name;
        if (a.size != b.size) return a.size < b.size;
        return a.sha256 < b.sha256;
    });
    return out;
}

static std::vector<jsst_bios_manifest_entry> jsst_bios_manifest_from_meta(const std::string& meta)
{
    std::vector<jsst_bios_manifest_entry> out;
    std::string count_str = jsst_meta_get_val(meta, "bios_count");
    if (count_str.empty()) return out;

    int count = 0;
    try { count = std::stoi(count_str); } catch (...) { count = 0; }
    if (count < 0) count = 0;

    for (int i = 0; i < count; i++) {
        char key[64];
        jsst_bios_manifest_entry entry;

        snprintf(key, sizeof(key), "bios_%d_name", i);
        entry.name = jsst_meta_get_val(meta, key);

        snprintf(key, sizeof(key), "bios_%d_size", i);
        std::string size_str = jsst_meta_get_val(meta, key);
        try { entry.size = (size_t)std::stoull(size_str); } catch (...) { entry.size = 0; }

        snprintf(key, sizeof(key), "bios_%d_sha256", i);
        entry.sha256 = jsst_meta_get_val(meta, key);

        out.push_back(entry);
    }

    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
        if (a.name != b.name) return a.name < b.name;
        if (a.size != b.size) return a.size < b.size;
        return a.sha256 < b.sha256;
    });
    return out;
}

static bool jsst_same_bios_manifest(const std::vector<jsst_bios_manifest_entry>& a,
                                    const std::vector<jsst_bios_manifest_entry>& b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++) {
        if (a[i].name != b[i].name) return false;
        if (a[i].size != b[i].size) return false;
        if (a[i].sha256 != b[i].sha256) return false;
    }
    return true;
}

static void jsst_append_bios_manifest_meta(std::string& meta,
                                           const std::vector<jsst_bios_manifest_entry>& bios)
{
    char line[512];
    snprintf(line, sizeof(line), "bios_count=%zu\n", bios.size());
    meta += line;
    for (size_t i = 0; i < bios.size(); i++) {
        snprintf(line, sizeof(line), "bios_%zu_name=%s\n", i, bios[i].name.c_str());
        meta += line;
        snprintf(line, sizeof(line), "bios_%zu_size=%llu\n", i, (unsigned long long)bios[i].size);
        meta += line;
        snprintf(line, sizeof(line), "bios_%zu_sha256=%s\n", i, bios[i].sha256.c_str());
        meta += line;
    }
}

static bool jsst_make_bios_warning(const std::string& meta, const multi_file_set& current_bioses,
                                   char *out, size_t out_sz)
{
    std::string count_str = jsst_meta_get_val(meta, "bios_count");
    if (count_str.empty()) return false;

    auto saved = jsst_bios_manifest_from_meta(meta);
    auto current = jsst_bios_manifest_from_mfs(current_bioses);
    if (jsst_same_bios_manifest(saved, current)) return false;

    const jsst_bios_manifest_entry *saved_first = nullptr;
    const jsst_bios_manifest_entry *current_first = nullptr;
    size_t max_count = std::max(saved.size(), current.size());
    for (size_t i = 0; i < max_count; i++) {
        const jsst_bios_manifest_entry *s = (i < saved.size()) ? &saved[i] : nullptr;
        const jsst_bios_manifest_entry *c = (i < current.size()) ? &current[i] : nullptr;
        if (!s || !c || s->name != c->name || s->size != c->size || s->sha256 != c->sha256) {
            saved_first = s;
            current_first = c;
            break;
        }
    }

    const char *saved_name = saved_first ? saved_first->name.c_str() : "(none)";
    const char *current_name = current_first ? current_first->name.c_str() : "(none)";
    const char *saved_hash = (saved_first && saved_first->sha256.size() >= 8) ? saved_first->sha256.c_str() : "";
    const char *current_hash = (current_first && current_first->sha256.size() >= 8) ? current_first->sha256.c_str() : "";

    snprintf(out, out_sz,
             "This save state was made with a different BIOS configuration.\n\n"
             "Saved BIOS files: %zu\nCurrent BIOS files: %zu\n\n"
             "First difference:\nSaved: %s %.8s\nCurrent: %s %.8s\n\n"
             "The state was loaded, but behavior may differ if the core reads BIOS ROM after restore.",
             saved.size(), current.size(), saved_name, saved_hash, current_name, current_hash);
    return true;
}

enum jsst_slot_compat {
    JSST_SLOT_MISSING,
    JSST_SLOT_USABLE,
    JSST_SLOT_INCOMPATIBLE_SYSTEM
};

static bool jsst_file_exists(const char* path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

static jsst_slot_compat jsst_check_slot_compat(const char* path, jsm::systems current_system, bool require_system_meta)
{
    if (!jsst_file_exists(path)) return JSST_SLOT_MISSING;

    mz_zip_archive zip;
    mz_zip_zero_struct(&zip);
    if (!mz_zip_reader_init_file(&zip, path, 0))
        return require_system_meta ? JSST_SLOT_INCOMPATIBLE_SYSTEM : JSST_SLOT_USABLE;

    size_t meta_sz = 0;
    void *meta_data = mz_zip_reader_extract_file_to_heap(&zip, "meta.ini", &meta_sz, 0);
    mz_zip_reader_end(&zip);

    if (!meta_data)
        return require_system_meta ? JSST_SLOT_INCOMPATIBLE_SYSTEM : JSST_SLOT_USABLE;

    std::string meta(static_cast<const char*>(meta_data), meta_sz);
    mz_free(meta_data);

    std::string system_str = jsst_meta_get_val(meta, "system");
    if (system_str.empty())
        return require_system_meta ? JSST_SLOT_INCOMPATIBLE_SYSTEM : JSST_SLOT_USABLE;

    char *end = nullptr;
    long saved_system = strtol(system_str.c_str(), &end, 10);
    if (!end || end == system_str.c_str())
        return require_system_meta ? JSST_SLOT_INCOMPATIBLE_SYSTEM : JSST_SLOT_USABLE;

    return (saved_system == (long)current_system) ? JSST_SLOT_USABLE : JSST_SLOT_INCOMPATIBLE_SYSTEM;
}

void full_system::get_slot_path_variant(char *pth, size_t sz, int slot, bool legacy) const
{
    // Use cached state_save_dir if available; fall back to ROM folder.
    const std::string &dir = !state_save_dir.empty() ? state_save_dir
                             : (!ROMs.files.empty() ? std::string(ROMs.files[0].path) : std::string("."));
    const char* sys_cli = sys ? AppSettings::sys_to_cli(sys->kind) : "";
    std::string prefix = (!legacy && sys_cli && sys_cli[0]) ? (std::string(sys_cli) + ".") : "";

    if (ROMs.files.empty()) {
        // BIOS-only boot: include the exact machine so variants do not share slots.
        snprintf(pth, sz, "%s/%sbios.slot%d.jsst", dir.c_str(), prefix.c_str(), slot);
    } else {
        const read_file_buf *rfb = &ROMs.files[0];
        std::filesystem::path p = std::filesystem::path(rfb->path) / rfb->name;
        std::string stem = p.stem().string();
        snprintf(pth, sz, "%s/%s%s.slot%d.jsst", dir.c_str(), prefix.c_str(), stem.c_str(), slot);
    }
}

void full_system::get_slot_path(char *pth, size_t sz, int slot) const
{
    get_slot_path_variant(pth, sz, slot, false);
}

bool full_system::resolve_slot_path_for_load(char *pth, size_t sz, int slot, bool *wrong_system) const
{
    if (wrong_system) *wrong_system = false;
    if (!sys) return false;

    char candidate[600];
    get_slot_path_variant(candidate, sizeof(candidate), slot, false);
    jsst_slot_compat compat = jsst_check_slot_compat(candidate, sys->kind, false);
    if (compat == JSST_SLOT_USABLE) {
        snprintf(pth, sz, "%s", candidate);
        return true;
    }
    bool saw_wrong_system = (compat == JSST_SLOT_INCOMPATIBLE_SYSTEM);

    // Compatibility with saves made before exact-system filenames. Only accept
    // legacy filenames when their metadata explicitly names this exact system.
    get_slot_path_variant(candidate, sizeof(candidate), slot, true);
    compat = jsst_check_slot_compat(candidate, sys->kind, true);
    if (compat == JSST_SLOT_USABLE) {
        snprintf(pth, sz, "%s", candidate);
        return true;
    }
    saw_wrong_system = saw_wrong_system || (compat == JSST_SLOT_INCOMPATIBLE_SYSTEM);

    if (wrong_system) *wrong_system = saw_wrong_system;
    return false;
}

bool full_system::slot_has_save(int slot) const
{
    if (!sys) return false;
    char path[600];
    return resolve_slot_path_for_load(path, sizeof(path), slot, nullptr);
}

bool full_system::read_slot_thumbnail(int slot, slot_thumbnail& out) const
{
    if (!sys) return false;
    char path[600];
    if (!resolve_slot_path_for_load(path, sizeof(path), slot, nullptr)) return false;

    mz_zip_archive zip;
    mz_zip_zero_struct(&zip);
    if (!mz_zip_reader_init_file(&zip, path, 0)) return false;

    // Extract timestamp from meta.ini
    size_t meta_sz = 0;
    void *meta_data = mz_zip_reader_extract_file_to_heap(&zip, "meta.ini", &meta_sz, 0);
    if (meta_data) {
        std::string meta(static_cast<const char*>(meta_data), meta_sz);
        mz_free(meta_data);
        auto pos = meta.find("timestamp=");
        if (pos != std::string::npos) {
            auto end = meta.find('\n', pos);
            out.timestamp = meta.substr(pos + 10,
                end == std::string::npos ? std::string::npos : end - pos - 10);
        }
    }

    // Extract and decode screenshot.png
    size_t png_sz = 0;
    void *png_data = mz_zip_reader_extract_file_to_heap(&zip, "screenshot.png", &png_sz, 0);
    mz_zip_reader_end(&zip);

    if (png_data) {
        int w, h, channels;
        stbi_uc *pixels = stbi_load_from_memory(
            static_cast<const stbi_uc*>(png_data), (int)png_sz,
            &w, &h, &channels, 4);
        mz_free(png_data);
        if (pixels) {
            out.width  = (u32)w;
            out.height = (u32)h;
            out.rgba.assign(pixels, pixels + (size_t)w * h * 4);
            stbi_image_free(pixels);
            out.valid = true;
        }
    }

    return true; // slot file exists (thumbnail may or may not be valid)
}

// PNG-to-memory callback for stbi_write_png_to_func
static void png_mem_write(void *ctx, void *data, int size)
{
    auto *buf = static_cast<std::vector<u8>*>(ctx);
    u8 *p = static_cast<u8*>(data);
    buf->insert(buf->end(), p, p + size);
}

void full_system::capture_screenshot_png(std::vector<u8>& out)
{
    if (!output.backbuffer_backer || !output.display) return;
    auto &view = should_hide_overscan() ? output.without_overscan : output.with_overscan;

    u32 bw = output.backbuffer_texture.width;
    u32 bh = output.backbuffer_texture.height;
    u32 src_x0 = ss_clamp_src_pos(view.uv0.x, bw);
    u32 src_y0 = ss_clamp_src_pos(view.uv0.y, bh);
    u32 src_x1 = ss_clamp_src_pos(view.uv1.x, bw);
    u32 src_y1 = ss_clamp_src_pos(view.uv1.y, bh);
    if (src_x1 <= src_x0 || src_y1 <= src_y0) return;

    u32 src_w = src_x1 - src_x0;
    u32 src_h = src_y1 - src_y0;
    u32 dst_w = ss_float_to_u32(view.x_size);
    u32 dst_h = ss_float_to_u32(view.y_size);

    u32 *scaled = static_cast<u32*>(malloc(dst_w * dst_h * 4));
    if (!scaled) return;
    ss_scale_rgba(scaled, dst_w, dst_h,
                  static_cast<u32*>(output.backbuffer_backer), bw,
                  src_x0, src_y0, src_w, src_h);
    stbi_write_png_to_func(png_mem_write, &out,
                           (int)dst_w, (int)dst_h, 4, scaled, (int)(dst_w * 4));
    free(scaled);
}

// Serialise a serialized_state to a byte vector (mirrors write_to_file format)
static std::vector<u8> state_to_bytes(const serialized_state &state)
{
    std::vector<u8> out;
    auto wr = [&](const void *src, size_t n) {
        const u8 *p = static_cast<const u8*>(src);
        out.insert(out.end(), p, p + n);
    };
    u32 magic = 0xD34DB33F;
    wr(&magic, 4);
    wr(&state.version, 4);
    wr(&state.kind, 4);
    for (const auto &sec : state.sections) {
        wr(&sec.kind, 4);
        wr(&sec.version, 4);
        wr(&sec.sz, 8);
        wr(sec.friendly_name, 50);
        wr(state.buf.data() + sec.offset, sec.sz);
    }
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// Media helpers
// ─────────────────────────────────────────────────────────────────────────────

bool full_system::insert_media(physical_io_device& pio, u32 io_index,
                               const char* path, bool is_folder)
{
    if (!sys || !path || !path[0]) return false;

    multi_file_set mfs;
    if (!mfs_add_path(&mfs, path, is_folder)) return false;

    switch (pio.kind) {
        case HID_CART_PORT:
            if (!pio.cartridge_port.load_cart) return false;
            close_persistent_storage();
            // Remove the current cart before inserting the new one
            if (pio.cartridge_port.unload_cart) pio.cartridge_port.unload_cart(sys);
            pio.cartridge_port.load_cart(sys, mfs, pio);
            break;

        case HID_DISC_DRIVE:
            if (!pio.disc_drive.insert_disc) return false;
            close_persistent_storage();
            // Full tray cycle: open → eject → insert → close
            if (pio.disc_drive.open_drive)   pio.disc_drive.open_drive(sys);
            if (pio.disc_drive.remove_disc)  pio.disc_drive.remove_disc(sys);
            pio.disc_drive.insert_disc(sys, pio, mfs);
            if (pio.disc_drive.close_drive)  pio.disc_drive.close_drive(sys);
            break;

        case HID_AUDIO_CASSETTE:
            if (!pio.audio_cassette.insert_tape) return false;
            close_persistent_storage();
            pio.audio_cassette.insert_tape(sys, pio, mfs, nullptr);
            cassette_state = CassetteState::Stopped;
            break;

        default:
            return false;
    }

    media_paths[io_index] = path;
    return true;
}

void full_system::eject_media(physical_io_device& pio, u32 io_index)
{
    if (!sys) return;

    switch (pio.kind) {
        case HID_CART_PORT:
            if (pio.cartridge_port.unload_cart) pio.cartridge_port.unload_cart(sys);
            break;
        case HID_DISC_DRIVE:
            if (pio.disc_drive.remove_disc) pio.disc_drive.remove_disc(sys);
            break;
        case HID_AUDIO_CASSETTE:
            if (pio.audio_cassette.remove_tape) pio.audio_cassette.remove_tape(sys);
            cassette_state = CassetteState::Stopped;
            break;
        default:
            return;
    }

    media_paths.erase(io_index);
}

void full_system::set_initial_media_path(const char* path)
{
    if (!sys || !path || !path[0]) return;

    // Prefer disc > cassette > cart (disc-based systems load first)
    static const IO_CLASSES priority[] = {
        HID_DISC_DRIVE, HID_AUDIO_CASSETTE, HID_CART_PORT
    };

    for (auto kind : priority) {
        for (u32 i = 0; i < (u32)sys->IOs.size(); i++) {
            if (sys->IOs[i].kind == kind) {
                media_paths[i] = path;
                return;
            }
        }
    }
}

bool full_system::supports_runtime_sideload() const
{
    return sys && sys->has.sideload;
}

bool full_system::sideload_file(const char* path)
{
    if (!supports_runtime_sideload() || !path || !path[0])
        return false;

    if (sys->kind == jsm::systems::COMMODORE64 && !str_ends_with_ci(path, ".prg")) {
        snprintf(status_msg, sizeof(status_msg), "C64 sideload supports .prg files");
        status_msg_time = ImGui::GetTime();
        return false;
    }

    multi_file_set mfs;
    if (!mfs_add_path(&mfs, path, false)) {
        snprintf(status_msg, sizeof(status_msg), "Sideload failed");
        status_msg_time = ImGui::GetTime();
        return false;
    }

    sys->runtime_sideload(mfs);

    snprintf(status_msg, sizeof(status_msg), "Sideloaded %s", path_basename(path));
    status_msg_time = ImGui::GetTime();
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────

void full_system::save_state_to_slot(int slot)
{
    if (!sys) return;
    if (!sys->has.save_state) {
        snprintf(status_msg, sizeof(status_msg), "Save states not supported");
        status_msg_time = ImGui::GetTime();
        return;
    }

    // 1. Serialise CPU/PPU/etc. state
    serialized_state state;
    sys->save_state(state);
    std::vector<u8> state_bytes = state_to_bytes(state);

    // 2. Capture screenshot as PNG
    std::vector<u8> png_bytes;
    capture_screenshot_png(png_bytes);

    std::vector<jsst_bios_manifest_entry> bios_manifest = jsst_bios_manifest_from_mfs(BIOSes);

    // 3. Build meta.ini
    std::string meta_str;
    {
        time_t now = time(nullptr);
        char ts[32];
        strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S", localtime(&now));
        char buf[256];
        snprintf(buf, sizeof(buf),
                 "slot=%d\ntimestamp=%s\nsystem=%d\nversion=%d.%d\n",
                 slot, ts, (int)sys->kind,
                 JSST_VERSION_MAJOR, JSST_VERSION_MINOR);
        meta_str = buf;

        jsst_append_bios_manifest_meta(meta_str, bios_manifest);

        // Persist all toggle-switch states from the chassis automatically.
        // Key format: sw_<dbcid_int>=<state>  — stable as long as DBCID enum
        // values don't change (new values appended at end are fine).
        if (io.chassis.vec) {
            for (auto& db : io.chassis.get().chassis.digital_buttons) {
                if (db.kind != DBK_SWITCH) continue;
                char line[64];
                snprintf(line, sizeof(line), "sw_%d=%u\n", (int)db.common_id, db.state);
                meta_str += line;
            }
        }
    }

    // 4. Write zip
    char path[600];
    get_slot_path(path, sizeof(path), slot);

    mz_zip_archive zip;
    mz_zip_zero_struct(&zip);
    if (!mz_zip_writer_init_file(&zip, path, 0)) {
        snprintf(status_msg, sizeof(status_msg), "Save state failed (zip init)");
        status_msg_time = ImGui::GetTime();
        return;
    }

    mz_zip_writer_add_mem(&zip, "state.bin",
                          state_bytes.data(), state_bytes.size(),
                          MZ_BEST_COMPRESSION);
    if (!png_bytes.empty())
        mz_zip_writer_add_mem(&zip, "screenshot.png",
                              png_bytes.data(), png_bytes.size(),
                              MZ_NO_COMPRESSION); // PNG is already compressed
    mz_zip_writer_add_mem(&zip, "meta.ini",
                          meta_str.c_str(), meta_str.size(),
                          MZ_BEST_COMPRESSION);

    // Always bundle SRAM so it can be restored even if the option changes later.
    if (my_ps && my_ps->data && my_ps->actual_size > 0)
        mz_zip_writer_add_mem(&zip, "sram.bin",
                              my_ps->data, my_ps->actual_size,
                              MZ_BEST_COMPRESSION);

    mz_zip_writer_finalize_archive(&zip);
    mz_zip_writer_end(&zip);

    snprintf(status_msg, sizeof(status_msg), "Saved to slot %d", slot);
    status_msg_time = ImGui::GetTime();
    printf("\nSaved state slot %d: %s", slot, path);
}

void full_system::load_state_from_slot(int slot)
{
    if (!sys) return;
    if (!sys->has.save_state) {
        snprintf(status_msg, sizeof(status_msg), "Save states not supported");
        status_msg_time = ImGui::GetTime();
        return;
    }
    load_version_warned = false;
    load_bios_warned = false;
    load_bios_warning_str[0] = 0;

    char path[600];
    bool wrong_system = false;
    if (!resolve_slot_path_for_load(path, sizeof(path), slot, &wrong_system)) {
        snprintf(status_msg, sizeof(status_msg),
                 wrong_system ? "Slot %d is for another system" : "Slot %d is empty",
                 slot);
        status_msg_time = ImGui::GetTime();
        return;
    }

    mz_zip_archive zip;
    mz_zip_zero_struct(&zip);
    if (!mz_zip_reader_init_file(&zip, path, 0)) {
        snprintf(status_msg, sizeof(status_msg), "Slot %d is empty", slot);
        status_msg_time = ImGui::GetTime();
        return;
    }

    // Extract state.bin
    size_t state_sz = 0;
    void *state_data = mz_zip_reader_extract_file_to_heap(&zip, "state.bin", &state_sz, 0);
    if (!state_data) {
        mz_zip_reader_end(&zip);
        snprintf(status_msg, sizeof(status_msg), "Slot %d: corrupt (no state)", slot);
        status_msg_time = ImGui::GetTime();
        return;
    }

    // Parse meta.ini — version check + switch state restore
    {
        size_t meta_sz = 0;
        void *meta_data = mz_zip_reader_extract_file_to_heap(&zip, "meta.ini", &meta_sz, 0);
        if (meta_data) {
            std::string meta(static_cast<const char*>(meta_data), meta_sz);
            mz_free(meta_data);

            // Version check
            std::string ver = jsst_meta_get_val(meta, "version");
            int saved_major = -1, saved_minor = -1;
            if (!ver.empty()) {
                sscanf(ver.c_str(), "%d.%d", &saved_major, &saved_minor);
            }
            bool version_ok = (saved_major == JSST_VERSION_MAJOR &&
                               saved_minor == JSST_VERSION_MINOR);
            if (!version_ok) {
                load_version_warned = true;
                if (ver.empty()) {
                    snprintf(load_version_str, sizeof(load_version_str), "(none)");
                } else {
                    snprintf(load_version_str, sizeof(load_version_str), "%s", ver.c_str());
                }
            }

            if (jsst_make_bios_warning(meta, BIOSes, load_bios_warning_str, sizeof(load_bios_warning_str)))
                load_bios_warned = true;

            // Restore toggle-switch states automatically (matches sw_{dbcid_int} save format)
            if (io.chassis.vec) {
                for (auto& db : io.chassis.get().chassis.digital_buttons) {
                    if (db.kind != DBK_SWITCH) continue;
                    char key[32];
                    snprintf(key, sizeof(key), "sw_%d", (int)db.common_id);
                    std::string v = jsst_meta_get_val(meta, key);
                    if (!v.empty()) db.state = (u32)std::stoul(v);
                }
            }
        } else {
            // No meta.ini at all → old save, warn
            load_version_warned = true;
            snprintf(load_version_str, sizeof(load_version_str), "(none)");
        }
    }

    // Restore SRAM from the bundled copy only if the user has opted in.
    // (SRAM is always saved into the zip so the option can change later.)
    if (sram_in_state) {
        size_t sram_sz = 0;
        void *sram_data = mz_zip_reader_extract_file_to_heap(&zip, "sram.bin", &sram_sz, 0);
        if (sram_data && sram_sz > 0 && my_ps && my_ps->data && sram_sz == my_ps->actual_size) {
            memcpy(my_ps->data, sram_data, sram_sz);
            my_ps->dirty = true;
        }
        if (sram_data) mz_free(sram_data);
    }

    mz_zip_reader_end(&zip);

    // Deserialise
    serialized_state state;
    deserialize_ret ret;
    memset(&ret, 0, sizeof(ret));

    FILE *f = tmpfile();
    if (f) {
        bool wrote = fwrite(state_data, 1, state_sz, f) == state_sz;
        rewind(f);
        if (wrote) {
            state.read_from_file(f, state_sz);
            sys->load_state(state, ret);
            snprintf(status_msg, sizeof(status_msg), "Loaded slot %d", slot);
            status_msg_time = ImGui::GetTime();
            printf("\nLoaded state slot %d: %s", slot, path);
        } else {
            snprintf(status_msg, sizeof(status_msg), "Slot %d: load failed", slot);
            status_msg_time = ImGui::GetTime();
        }
        fclose(f);
    } else {
        snprintf(status_msg, sizeof(status_msg), "Slot %d: load failed", slot);
        status_msg_time = ImGui::GetTime();
    }

    mz_free(state_data);
}

void full_system::destroy_system()
{
    if (sys == nullptr) return;
    delete sys;
    sys = nullptr;
    // Reset all IO cvec_ptrs so their vec pointers don't dangle into deleted memory
    io = fsio{};
}

framevars full_system::get_framevars() const
{
    framevars fv = {};
    if (sys) sys->get_framevars(fv);
    return fv;
}

#define SCALE_DOWN 1

void full_system::setup_display()
{
    JSM_DISPLAY_PIXELOMETRY *p = &output.display->pixelometry;

    // Determine final output resolution
    u32 wh = get_closest_pow2(MAX(p->cols.max_visible, p->rows.max_visible));
    TS(output.backbuffer_texture,"emulator backbuffer", wh, wh);
    //printf("\nMAX COLS:%d ROWS:%d POW2:%d", p->cols.max_visible, p->rows.max_visible, wh);

    u32 overscan_x_offset = p->overscan.left;
    u32 overscan_width = p->cols.visible - (p->overscan.left + p->overscan.right);
    u32 overscan_y_offset = p->overscan.top;
    u32 overscan_height = p->rows.visible - (p->overscan.top + p->overscan.bottom);

    // Determine aspect ratio correction
    double visible_width = p->cols.visible;
    double visible_height = p->rows.visible;

    double real_width = output.display->geometry.physical_aspect_ratio.width;
    double real_height = output.display->geometry.physical_aspect_ratio.height;

    // we want a multiplier of 1 in one direction, and >1 in the other
    double visible_how = visible_height / visible_width;  // .5
    double real_how = real_height / real_width;           // .6. real is narrower

    if (fabs(visible_how - real_how) < .01) {
        output.x_scale_mult = 1;
        output.y_scale_mult = 1;
        output.with_overscan.x_size = (float)p->cols.visible;
        output.with_overscan.y_size = (float)p->rows.visible;
        output.without_overscan.x_size = (float)overscan_width;
        output.without_overscan.y_size = (float)overscan_height;
    }
    else if (!SCALE_DOWN && (real_how > visible_how)) { // real is narrower, so we stretch vertically. visible= 4:2 .5  real=3:2  .6
        output.x_scale_mult = 1;
        output.y_scale_mult = (real_how / visible_how); // we must
        output.with_overscan.x_size = (float)p->cols.visible;
        output.with_overscan.y_size = (float)(visible_height * output.y_scale_mult);
        output.without_overscan.x_size = (float)overscan_width;
        output.without_overscan.y_size = (float)(overscan_height * output.y_scale_mult);
    }
    else { // real is wider, so we stretch horizontally  //    visible=4:2 = .5    real=5:2 = .4
        printf("\nNo stretch that way...");
        output.x_scale_mult = (visible_how / real_how);
        output.y_scale_mult = 1;
        output.with_overscan.x_size = (float)visible_width * output.x_scale_mult;
        output.with_overscan.y_size = (float)p->rows.visible;
        output.without_overscan.x_size = (float)overscan_width * output.x_scale_mult;
        output.without_overscan.y_size = (float)overscan_height;
    }
    printf("\nOutput with overscan size: %dx%d", (int)output.with_overscan.x_size, (int)output.with_overscan.y_size);
    printf("\nOutput without overscan size: %dx%d", (int)output.with_overscan.x_size, (int)output.with_overscan.y_size);

    // Default to nearest-neighbor if the screen is stretched or perfect, and blend if squished at all.
    output.nearest_neighbor = output.x_scale_mult >= 1 && output.y_scale_mult >= 1;

    // Calculate UV coords for full buffer
    output.with_overscan.uv0 = ImVec2(0, 0);
    output.with_overscan.uv1 = ImVec2((float)((double)p->cols.visible / (double)output.backbuffer_texture.width),
                                      (float)((double)p->rows.visible / (double)output.backbuffer_texture.height));

    // Calculate UV coords for buffer minus overscan
    // we need the left and top, which may be 0 or 10 or whatever... % of total width
    float total_u = output.with_overscan.uv1.x;
    float total_v = output.with_overscan.uv1.y;

    float start_u = (float)overscan_x_offset / (float)visible_width;
    float start_v = (float)overscan_y_offset / (float)visible_height;
    output.without_overscan.uv0 = ImVec2(start_u * total_u, start_v * total_v);

    float end_u = (float)(p->cols.visible - p->overscan.right) / (float)visible_width;
    float end_v = (float)(p->rows.visible - p->overscan.bottom) / (float)visible_height;
    output.without_overscan.uv1 = ImVec2(end_u * total_u, end_v * total_v);

    if (output.backbuffer_backer) free(output.backbuffer_backer);
    output.backbuffer_backer = malloc(output.backbuffer_texture.width*output.backbuffer_texture.height * 4);
    memset(output.backbuffer_backer, 0, output.backbuffer_texture.width*output.backbuffer_texture.height * 4);
    // Upload the zeroed pixels immediately so the GPU texture shows black,
    // not stale pixels from a previous game.
    output.backbuffer_texture.upload_data(output.backbuffer_backer,
        output.backbuffer_texture.width * output.backbuffer_texture.height * 4,
        output.backbuffer_texture.width, output.backbuffer_texture.height);
    //printf("\nX0:%f  X1:%f", output.without_overscan.uv0.x, output.without_overscan.uv1.x);
}

bool full_system::has_second_display() const
{
    return io.display2.vec && output2.display;
}

void full_system::cycle_layout()
{
    u32 next = (static_cast<u32>(current_layout) + 1) % static_cast<u32>(DisplayLayout::COUNT);
    current_layout = static_cast<DisplayLayout>(next);
}

void full_system::set_layout(DisplayLayout l)
{
    current_layout = l;
}

bool full_system::uses_composite_layout() const
{
    return has_second_display() && current_layout != DisplayLayout::SeparateWindows;
}

void full_system::load_layout_settings(AppSettings& settings)
{
    if (!has_second_display() || !sys) return;
    current_layout = static_cast<DisplayLayout>(settings.get_display_layout(sys->kind));
    fav_layout[0]  = static_cast<DisplayLayout>(settings.get_display_layout_fav(sys->kind, 0));
    fav_layout[1]  = static_cast<DisplayLayout>(settings.get_display_layout_fav(sys->kind, 1));
}

void full_system::save_layout_settings(AppSettings& settings)
{
    if (!has_second_display() || !sys) return;
    settings.set_display_layout(sys->kind, static_cast<u32>(current_layout));
    settings.set_display_layout_fav(sys->kind, 0, static_cast<u32>(fav_layout[0]));
    settings.set_display_layout_fav(sys->kind, 1, static_cast<u32>(fav_layout[1]));
}

void full_system::setup_display2()
{
    if (!has_second_display()) return;
    JSM_DISPLAY_PIXELOMETRY *p = &output2.display->pixelometry;

    u32 wh = get_closest_pow2(MAX(p->cols.max_visible, p->rows.max_visible));
    TS(output2.backbuffer_texture, "emulator backbuffer 2", wh, wh);

    double visible_width = p->cols.visible;
    double visible_height = p->rows.visible;
    double real_width = output2.display->geometry.physical_aspect_ratio.width;
    double real_height = output2.display->geometry.physical_aspect_ratio.height;
    double visible_how = visible_height / visible_width;
    double real_how = real_height / real_width;

    if (fabs(visible_how - real_how) < .01) {
        output2.x_scale_mult = 1;
        output2.y_scale_mult = 1;
        output2.with_overscan.x_size = (float)p->cols.visible;
        output2.with_overscan.y_size = (float)p->rows.visible;
    } else if (real_how > visible_how) {
        output2.x_scale_mult = 1;
        output2.y_scale_mult = (real_how / visible_how);
        output2.with_overscan.x_size = (float)p->cols.visible;
        output2.with_overscan.y_size = (float)(visible_height * output2.y_scale_mult);
    } else {
        output2.x_scale_mult = (visible_how / real_how);
        output2.y_scale_mult = 1;
        output2.with_overscan.x_size = (float)visible_width * output2.x_scale_mult;
        output2.with_overscan.y_size = (float)p->rows.visible;
    }
    output2.nearest_neighbor = output2.x_scale_mult >= 1 && output2.y_scale_mult >= 1;

    output2.with_overscan.uv0 = ImVec2(0, 0);
    output2.with_overscan.uv1 = ImVec2(
        (float)((double)p->cols.visible / (double)output2.backbuffer_texture.width),
        (float)((double)p->rows.visible / (double)output2.backbuffer_texture.height));
    output2.without_overscan = output2.with_overscan;

    if (output2.backbuffer_backer) free(output2.backbuffer_backer);
    output2.backbuffer_backer = malloc(output2.backbuffer_texture.width * output2.backbuffer_texture.height * 4);
    memset(output2.backbuffer_backer, 0, output2.backbuffer_texture.width * output2.backbuffer_texture.height * 4);
    output2.backbuffer_texture.upload_data(output2.backbuffer_backer,
        output2.backbuffer_texture.width * output2.backbuffer_texture.height * 4,
        output2.backbuffer_texture.width, output2.backbuffer_texture.height);
}

void full_system::present2()
{
    if (!has_second_display()) return;
    u32 outcols = output2.display->pixelometry.cols.visible;
    u32 outrows = output2.display->pixelometry.rows.visible;
    bool updated_uv = false;

    jsm_present(sys, sys->kind, io.display2.get(), output2.backbuffer_backer, 0, 0,
                output2.backbuffer_texture.width, output2.backbuffer_texture.height,
                nullptr, outcols, outrows, updated_uv, nullptr);
    if (updated_uv) {
        output2.with_overscan.uv1 = ImVec2(
            (float)((double)outcols / (double)output2.backbuffer_texture.width),
            (float)((double)outrows / (double)output2.backbuffer_texture.height));
        output2.without_overscan.uv1 = output2.with_overscan.uv1;
    }
    output2.backbuffer_texture.upload_data(output2.backbuffer_backer,
        output2.backbuffer_texture.width * output2.backbuffer_texture.height * 4,
        output2.backbuffer_texture.width, output2.backbuffer_texture.height);
}

void full_system::clear_output_screen()
{
    if (!output.backbuffer_backer || output.backbuffer_texture.width == 0 || output.backbuffer_texture.height == 0)
        return;

    const u32 bytes = output.backbuffer_texture.width * output.backbuffer_texture.height * 4;
    memset(output.backbuffer_backer, 0, bytes);
    output.backbuffer_texture.upload_data(output.backbuffer_backer,
                                          bytes,
                                          output.backbuffer_texture.width,
                                          output.backbuffer_texture.height);
}

void full_system::blank_output_until_next_frame()
{
    output.blank_until_next_frame = false;
    output.blank_frame = 0;

    if (sys && output.backbuffer_backer) {
        framevars fv = {};
        sys->get_framevars(fv);
        output.blank_frame = fv.master_frame;
        output.blank_until_next_frame = true;
    }

    if (output.display)
        output.display->clear_outputs();
    clear_output_screen();
}

static void sanitize_filename_piece(char *out, size_t out_sz, const char *in)
{
    if (out_sz == 0) return;

    size_t oi = 0;
    for (size_t ii = 0; in[ii] && oi < (out_sz - 1); ii++) {
        unsigned char c = static_cast<unsigned char>(in[ii]);
        if (std::isalnum(c)) {
            out[oi++] = static_cast<char>(std::tolower(c));
        }
        else if (c == '-' || c == '_') {
            out[oi++] = static_cast<char>(c);
        }
        else if (oi > 0 && out[oi - 1] != '_') {
            out[oi++] = '_';
        }
    }

    if (oi == 0) out[oi++] = 's';
    if (oi > 1 && out[oi - 1] == '_') oi--;
    out[oi] = 0;
}

static void get_screenshot_base(char *out, size_t out_sz, multi_file_set& ROMs, const char *label)
{
    char raw_name[255];
    raw_name[0] = 0;

    if (ROMs.files.size() > 0) {
        read_file_buf *rfb = &ROMs.files[0];
        std::filesystem::path rpath = rfb->path;
        std::error_code ec;
        if (rfb->path[0] && std::filesystem::is_regular_file(rpath, ec)) {
            snprintf(raw_name, sizeof(raw_name), "%s", rpath.stem().string().c_str());
        }
        else if (rfb->name[0]) {
            std::filesystem::path npath = rfb->name;
            snprintf(raw_name, sizeof(raw_name), "%s", npath.stem().string().c_str());
        }
        else if (rfb->path[0]) {
            snprintf(raw_name, sizeof(raw_name), "%s", rpath.stem().string().c_str());
        }
    }

    if (!raw_name[0])
        snprintf(raw_name, sizeof(raw_name), "%s", label ? label : "jocasta");

    sanitize_filename_piece(out, out_sz, raw_name);
}

static void construct_ss_path(char *out, size_t out_sz, multi_file_set& ROMs, const char *label)
{
    const char *homeDir = get_user_dir();
    std::filesystem::path dir;

    if (ROMs.files.size() > 0) {
        read_file_buf *rfb = &ROMs.files[0];
        std::filesystem::path rpath = rfb->path;
        std::error_code ec;
        if (rfb->path[0] && std::filesystem::is_regular_file(rpath, ec))
            dir = rpath.parent_path();
        else if (rfb->path[0])
            dir = rpath;
    }

    if (dir.empty())
        dir = std::filesystem::path(homeDir) / "dev" / "jocasta-emu" / "screenshots";

    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        dir = std::filesystem::path(homeDir) / "dev" / "jocasta-emu" / "screenshots";
        ec.clear();
        std::filesystem::create_directories(dir, ec);
    }

    char base[128];
    get_screenshot_base(base, sizeof(base), ROMs, label);

    for (u32 i = 0; i < 100000; i++) {
        char filename[256];
        snprintf(filename, sizeof(filename), "%s_%04u.png", base, i);
        std::filesystem::path p = dir / filename;
        if (!std::filesystem::exists(p)) {
            snprintf(out, out_sz, "%s", p.string().c_str());
            return;
        }
    }

    std::filesystem::path p = dir / (std::string(base) + "_99999.png");
    snprintf(out, out_sz, "%s", p.string().c_str());
}

static u32 ss_float_to_u32(float v)
{
    if (v < 1.0f) return 1;
    return static_cast<u32>(std::lround(v));
}

static u32 ss_clamp_src_pos(float uv, u32 size)
{
    if (uv <= 0.0f) return 0;
    u32 pos = static_cast<u32>(std::lround(uv * (float)size));
    if (pos > size) pos = size;
    return pos;
}

static void ss_scale_rgba(u32 *dst, u32 dst_w, u32 dst_h, u32 *src, u32 src_stride, u32 src_x, u32 src_y, u32 src_w, u32 src_h)
{
    for (u32 y = 0; y < dst_h; y++) {
        u32 sy = src_y + (u32)((((u64)y * src_h) + (dst_h >> 1)) / dst_h);
        if (sy >= (src_y + src_h)) sy = src_y + src_h - 1;
        u32 *dst_line = dst + (y * dst_w);
        u32 *src_line = src + (sy * src_stride);

        for (u32 x = 0; x < dst_w; x++) {
            u32 sx = src_x + (u32)((((u64)x * src_w) + (dst_w >> 1)) / dst_w);
            if (sx >= (src_x + src_w)) sx = src_x + src_w - 1;
            dst_line[x] = src_line[sx];
        }
    }
}

void full_system::take_screenshot(void *where, u32 buf_width, u32 buf_height)
{
    if (!where || !output.display) return;

    auto &view = should_hide_overscan() ? output.without_overscan : output.with_overscan;

    u32 src_x0 = ss_clamp_src_pos(view.uv0.x, buf_width);
    u32 src_y0 = ss_clamp_src_pos(view.uv0.y, buf_height);
    u32 src_x1 = ss_clamp_src_pos(view.uv1.x, buf_width);
    u32 src_y1 = ss_clamp_src_pos(view.uv1.y, buf_height);
    if (src_x1 <= src_x0 || src_y1 <= src_y0) return;

    u32 src_w  = src_x1 - src_x0;
    u32 src_h  = src_y1 - src_y0;
    u32 dst_w1 = ss_float_to_u32(view.x_size);
    u32 dst_h1 = ss_float_to_u32(view.y_size);

    char mpath[500];
    construct_ss_path(mpath, sizeof(mpath), ROMs, sys ? sys->label : "jocasta");

    // Dual-screen: stack both screens vertically (top screen above bottom screen)
    if (has_second_display() && output2.backbuffer_backer) {
        auto &view2 = output2.with_overscan;
        u32 bw2 = output2.backbuffer_texture.width;
        u32 bh2 = output2.backbuffer_texture.height;
        u32 s2_x0 = ss_clamp_src_pos(view2.uv0.x, bw2);
        u32 s2_y0 = ss_clamp_src_pos(view2.uv0.y, bh2);
        u32 s2_x1 = ss_clamp_src_pos(view2.uv1.x, bw2);
        u32 s2_y1 = ss_clamp_src_pos(view2.uv1.y, bh2);
        if (s2_x1 <= s2_x0 || s2_y1 <= s2_y0) goto single_screen;
        u32 s2_w  = s2_x1 - s2_x0;
        u32 s2_h  = s2_y1 - s2_y0;
        u32 dst_w2 = ss_float_to_u32(view2.x_size);
        u32 dst_h2 = ss_float_to_u32(view2.y_size);

        u32 combined_w = std::max(dst_w1, dst_w2);
        u32 combined_h = dst_h1 + dst_h2;
        u32 *png_buf = (u32 *)calloc(combined_w * combined_h, 4);
        if (!png_buf) {
            printf("\nCould not allocate screenshot buffer");
            return;
        }
        // Top screen (centred if narrower than combined_w)
        u32 off1 = (combined_w - dst_w1) / 2;
        u32 *top_dst = png_buf + off1;
        u32 *tmp1 = (u32 *)malloc(dst_w1 * dst_h1 * 4);
        if (tmp1) {
            ss_scale_rgba(tmp1, dst_w1, dst_h1, (u32 *)where, buf_width, src_x0, src_y0, src_w, src_h);
            for (u32 y = 0; y < dst_h1; y++)
                memcpy(top_dst + y * combined_w, tmp1 + y * dst_w1, dst_w1 * 4);
            free(tmp1);
        }
        // Bottom screen (centred)
        u32 off2 = (combined_w - dst_w2) / 2;
        u32 *bot_dst = png_buf + dst_h1 * combined_w + off2;
        u32 *tmp2 = (u32 *)malloc(dst_w2 * dst_h2 * 4);
        if (tmp2) {
            ss_scale_rgba(tmp2, dst_w2, dst_h2, (u32 *)output2.backbuffer_backer, bw2, s2_x0, s2_y0, s2_w, s2_h);
            for (u32 y = 0; y < dst_h2; y++)
                memcpy(bot_dst + y * combined_w, tmp2 + y * dst_w2, dst_w2 * 4);
            free(tmp2);
        }

        int ok = stbi_write_png(mpath, static_cast<int>(combined_w), static_cast<int>(combined_h), 4, png_buf, static_cast<int>(combined_w * 4));
        free(png_buf);
        if (ok) {
            printf("\nWrote screenshot: %s", mpath);
            const char* fname = strrchr(mpath, '/');
            fname = fname ? fname + 1 : mpath;
            snprintf(status_msg, sizeof(status_msg), "Saved %s", fname);
            status_msg_time = ImGui::GetTime();
        } else {
            printf("\nCould not write screenshot: %s", mpath);
            snprintf(status_msg, sizeof(status_msg), "Screenshot failed");
            status_msg_time = ImGui::GetTime();
        }
        return;
    }

single_screen:
    u32 *png_buf = (u32 *)malloc(dst_w1 * dst_h1 * 4);
    if (!png_buf) {
        printf("\nCould not allocate screenshot buffer");
        return;
    }

    ss_scale_rgba(png_buf, dst_w1, dst_h1, (u32 *)where, buf_width, src_x0, src_y0, src_w, src_h);

    int ok = stbi_write_png(mpath, static_cast<int>(dst_w1), static_cast<int>(dst_h1), 4, png_buf, static_cast<int>(dst_w1 * 4));
    free(png_buf);

    if (ok) {
        printf("\nWrote screenshot: %s", mpath);
        const char* fname = strrchr(mpath, '/');
        fname = fname ? fname + 1 : mpath;
        snprintf(status_msg, sizeof(status_msg), "Saved %s", fname);
        status_msg_time = ImGui::GetTime();
    } else {
        printf("\nCould not write screenshot: %s", mpath);
        snprintf(status_msg, sizeof(status_msg), "Screenshot failed");
        status_msg_time = ImGui::GetTime();
    }
}

void full_system::present()
{
    if (!sys) return;
    framevars fv = {};
    sys->get_framevars(fv);
    u32 outcols = output.display->pixelometry.cols.visible;
    u32 outrows = output.display->pixelometry.rows.visible;
    bool updated_uv = false;

    auto partial_present_supported = [](jsm::systems kind) -> bool {
        switch (kind) {
            case jsm::systems::PS1:
            case jsm::systems::DREAMCAST:
                return false;
            default:
                return true;
        }
    };

    jsm_present_context pctx = {};
    const jsm_present_context *pctx_ptr = nullptr;

    if (sys->kind == jsm::systems::APPLEIIe) {
        pctx.apple2_color_mode    = 0;
        pctx.apple2_respect_burst = true;
        for (auto& pw : present_widgets) {
            if (strcmp(pw.ini_key, "color_mode") == 0)
                pctx.apple2_color_mode = pw.widget.radiogroup.value;
            else if (strcmp(pw.ini_key, "respect_burst") == 0)
                pctx.apple2_respect_burst = (pw.widget.checkbox.value != 0);
        }
        pctx_ptr = &pctx;
    }

    if (run_state == FSS_pause && partial_present_supported(sys->kind)) {
        JSM_DISPLAY *d = output.display;
        auto &p = d->pixelometry;
        i64 split_y = static_cast<i64>(fv.scanline) - static_cast<i64>(p.offset.y);
        const i64 scanline_limit = static_cast<i64>(p.rows.visible) +
                                   static_cast<i64>(p.offset.y) +
                                   static_cast<i64>(p.rows.top_vblank) +
                                   static_cast<i64>(p.rows.bottom_vblank) +
                                   1024;
        if (static_cast<i64>(fv.scanline) > scanline_limit)
            split_y = 0;
        if (split_y < 0) split_y = 0;
        if (split_y > static_cast<i64>(p.rows.visible)) split_y = p.rows.visible;

        i64 split_x = static_cast<i64>(fv.x) - static_cast<i64>(p.offset.x);
        pctx.partial = true;
        pctx.complete_buffer = (d->active_draw_buffer ^ 1) & 1;
        pctx.draw_buffer = d->active_draw_buffer & 1;
        pctx.split_y = static_cast<u32>(split_y);
        if (fv.x >= 0) {
            if (split_x < 0) split_x = 0;
            if (split_x > static_cast<i64>(p.cols.visible)) split_x = p.cols.visible;
            pctx.split_x = static_cast<i32>(split_x);
        }
        pctx_ptr = &pctx;
    }

    if (output.blank_until_next_frame) {
        if (fv.master_frame == output.blank_frame) {
            clear_output_screen();
            if (screenshot) {
                take_screenshot(output.backbuffer_backer,
                                output.backbuffer_texture.width,
                                output.backbuffer_texture.height);
                screenshot = false;
            }
            return;
        }
        output.blank_until_next_frame = false;
    }

    jsm_present(sys, sys->kind, io.display.get(), output.backbuffer_backer, 0, 0, output.backbuffer_texture.width, output.backbuffer_texture.height, nullptr, outcols, outrows, updated_uv, pctx_ptr);
    if (updated_uv) {
        output.with_overscan.uv1 = ImVec2((float)((double)outcols / (double)output.backbuffer_texture.width),
                                      (float)((double)outrows / (double)output.backbuffer_texture.height));
        output.without_overscan.uv1 = ImVec2((float)((double)outcols / (double)output.backbuffer_texture.width),
                                      (float)((double)outrows / (double)output.backbuffer_texture.height));
    }

    if (screenshot) {
        take_screenshot(output.backbuffer_backer, output.backbuffer_texture.width, output.backbuffer_texture.height);
        screenshot = false;
    }
    output.backbuffer_texture.upload_data(output.backbuffer_backer, output.backbuffer_texture.width * output.backbuffer_texture.height * 4, output.backbuffer_texture.width, output.backbuffer_texture.height);
    present2();
}

void full_system::pre_events_view_present()
{
    if (events.view) {
        if (!events.texture.is_good) {
            u32 szpo2 = get_closest_pow2(MAX(events.view->display[0].width, events.view->display[0].height));
            TS(events.texture,"Events View texture", szpo2, szpo2);
            events.texture.uv0 = ImVec2(0, 0);
            events.texture.uv1 = ImVec2(
                    (float) ((double) events.view->display[0].width / (double) events.texture.width),
                    (float) ((double) events.view->display[0].height / (double) events.texture.height));
            events.texture.sz_for_display = ImVec2((float) events.view->display[0].width,
                                                   (float) events.view->display[0].height);
            events.view->display[0].buf = (u32 *) malloc(szpo2 * szpo2 * 4);
            events.view->display[1].buf = (u32 *) malloc(szpo2 * szpo2 * 4);
            // Setup UVs based off it
        }
    }
}



void full_system::events_view_present()
{
    if (events.view) {
        pre_events_view_present();
        assert(events.view->index_in_use<2);
        events_view::DVDP *evd = &events.view->display[events.view->index_in_use];

        framevars fv = {};
        sys->get_framevars(fv);
        JSM_DISPLAY *d = &(io.display.get()).display;
        memset(evd->buf, 0, events.texture.width*events.texture.height*4);
        u32 out_rows, out_cols;
        bool updated_rowcols = false;
        jsm_present(sys, sys->kind, io.display.get(), evd->buf, d->pixelometry.offset.x, d->pixelometry.offset.y, events.texture.width, events.texture.height, events.view, out_cols, out_rows, updated_rowcols);
        events.view->render(evd->buf, events.texture.width, events.texture.height);

        events.texture.upload_data(evd->buf, events.texture.width*events.texture.height*4, events.texture.width, events.texture.height);
    }

}

static void draw_box(u32 *ptr, u32 x0, u32 y0, u32 x1, u32 y1, u32 out_width, u32 out_height, u32 color)
{
    u32 stride = out_width;
    u32 *left_ptr = ptr + (y0 * out_width) + x0;
    u32 *right_ptr = ptr + (y0 * out_width) + x1;
    for (u32 y = y0; y < y1; y++) {
        *left_ptr = color;
        *right_ptr = color;
        left_ptr += stride;
        right_ptr += stride;
    }

    u32 *top_ptr = ptr + (y0 * out_width) + x0;
    u32 *bottom_ptr = ptr + (y1 * out_width) + x0;
    for (u32 x = x0; x < x1; x++) {
        *top_ptr = color;
        *bottom_ptr = color;
        top_ptr++;
        bottom_ptr++;
    }
}

bool full_system::draw_waveform2(W2FORM& wf) {
    bool over_max = false;

    u32 numrenders;
    struct RENDERS {
        u32 yoffset{};
        u32 xsize{};
        float ysize{};
        bool is_unsigned{};
        u32 bufstride{};
        u32 bufstart{};
    } renders[2]{};
    renders[0].bufstart = 0;

    if (wf.wf->stereo) {
        numrenders = 2;
        renders[0].is_unsigned = renders[1].is_unsigned = wf.wf->is_unsigned;
        renders[0].xsize = renders[1].xsize = wf.len;
        renders[1].bufstart = 1;
        renders[0].bufstride = renders[1].bufstride = 2;
        renders[0].yoffset = 0;
        renders[1].yoffset = wf.height >> 1;
        renders[0].ysize = renders[1].ysize = static_cast<float>(wf.height >> 1);
    }
    else {
        numrenders = 1;
        renders[0].bufstride = 1;
        renders[0].is_unsigned = wf.wf->is_unsigned;
        renders[0].xsize = wf.len;
        renders[0].yoffset = 0;
        renders[0].ysize = static_cast<float>(wf.height);
    }
    u32 xoffset = 0;
    u32 *outbuf = reinterpret_cast<u32 *>(&wf.drawbuf[0]);
    for (u32 rn = 0; rn < numrenders; rn++) {
        auto &r = renders[rn];
        auto *smp_ptr = static_cast<float *>(wf.wf->buf[wf.wf->rendering_buf].ptr);
        u32 offset = r.bufstart;;
        float last_y_val = r.is_unsigned ? 0 : r.ysize / 2.0f;
        for (u32 x = 0; x < r.xsize; x++) {
            u32 xpos = x + xoffset;
            float smp = smp_ptr[offset];
            offset += r.bufstride;
            float y_val;
            if (r.is_unsigned) {
                if (smp < 0.0f) {
                    over_max = true;
                    smp = 0.0f;
                }
                if (smp > 1.0f) {
                    over_max = true;
                    smp = 1.0f;
                }
                y_val = smp;
            }
            else {
                if (smp < -1.0f) {
                    over_max = true;
                    smp = -1.0f;
                }
                if (smp > 1.0f) {
                    over_max = true;
                    smp = 1.0f;
                }
                y_val = (smp + 1.0f) / 2.0f;
            }
            y_val = 1.0f - y_val;
            y_val *= r.ysize;
            u32 y1, y2;
            if (last_y_val < y_val) {
                y1 = floor(last_y_val);
                y2 = ceil(y_val);
            }
            else {
                y1 = floor(y_val);
                y2 = ceil(last_y_val);
            }
            last_y_val = y_val;
            u32 yaddr = (wf.szpo2 * (y1 + r.yoffset)) + xpos;
            if (y1 == y2) {
                outbuf[yaddr] = 0xFFFFFFFF;
            }
            else {
                for (u32 y = y1; y < y2; y++) {
                    outbuf[yaddr] = 0xFFFFFFFF;
                    yaddr += wf.szpo2;
                }
            }
        }

    }

    return over_max;
}

void full_system::waveform2_wf_present(W2FORM& wf) {
    if (!wf.tex.is_good) {
        u32 szpo2 = wf.szpo2;;
        TS(wf.tex, wf.wf->name, szpo2, szpo2);
        assert(wf.tex.is_good);
        wf.tex.uv0 = ImVec2(0, 0);
        float x_ratio = static_cast<float>(wf.len) / static_cast<float>(szpo2);
        float y_ratio = static_cast<float>(wf.height) / static_cast<float>(szpo2);
        wf.tex.uv1 = ImVec2(x_ratio, y_ratio);
        wf.drawbuf.resize(szpo2*szpo2*4);
    }
    memset(wf.drawbuf.data(), 0, wf.szpo2*wf.szpo2*4);

    u32 *ptr = reinterpret_cast<u32 *>(wf.drawbuf.data());

    // Draw box around
    bool overmax;
    overmax = draw_waveform2(wf);

    draw_box(ptr, 0, 0, wf.len-1, wf.height-1, wf.szpo2, wf.szpo2, overmax ? 0xFF0000FF : 0xFFFFFFFF);

    // Upload texture
    wf.tex.upload_data(wf.drawbuf.data(), wf.szpo2*wf.szpo2*4, wf.szpo2, wf.szpo2);

    //wf.tex.uv1 = ImVec2(wf.height, 0.5f);
    wf.tex.sz_for_display = ImVec2(wf.len, wf.height);
}

void full_system::waveform_view_present(WVIEW &wv)
{
    for (auto& wf : wv.waveforms) {
            u32 szpo2 = 1024;
        if (!wf.tex.is_good) {
            TS(wf.tex, wf.wf->name, szpo2, szpo2);
            assert(wf.tex.is_good);
            wf.tex.uv0 = ImVec2(0, 0);
            wf.drawbuf.resize(szpo2*szpo2*4);
        }

        memset(wf.drawbuf.data(), 0, szpo2*szpo2*4);

        // Draw box around
        float hrange = wf.height / 2.0f;
        u32 *ptr = (u32 *)(wf.drawbuf.data());
        draw_box(ptr, 0, 0, wf.wf->samples_requested-1, wf.height-1, szpo2, szpo2, 0xFF808080);
        i32 last_y = 0;
        if (wf.wf->samples_rendered > 0) {
            float *b = (float *)wf.wf->buf.ptr;
            for (u32 x = 0; x < wf.wf->samples_rendered; x++) {
                float smp = *b;
                if (smp < -1.0f) smp = -1.0f;
                if (smp > 1.0f) smp = 1.0f;
                float fy = (hrange * smp) * -1.0f;
                i32 iy = ((i32)floor(fy)) + (i32)hrange;
                if (x != 0) {
                    u32 starty = iy < last_y ? iy : last_y;
                    u32 endy = iy > last_y ? iy : last_y;
                    for (u32 sy = starty; sy <= endy; sy++) {
                        ptr[(sy * szpo2) + x] = 0xFFFFFFFF;
                    }
                }
                ptr[(iy * szpo2) + x] = 0xFFFFFFFF;
                last_y = iy;
                b++;
            }
        }
        wf.tex.upload_data(wf.drawbuf.data(), szpo2*szpo2*4, szpo2, szpo2);
        wf.tex.uv1 = ImVec2((float)wf.wf->samples_rendered / static_cast<float>(szpo2), (float)wf.height / static_cast<float>(szpo2));
        wf.tex.sz_for_display = ImVec2(wf.wf->samples_rendered, wf.height);
    }
}

void full_system::image_view_present(debugger_view &dview, my_texture &tex)
{
    image_view *iview = &dview.image;
    if (!tex.is_good) {
        u32 szpo2 = get_closest_pow2(MAX(iview->height, iview->width));
        TS(tex,iview->label, szpo2, szpo2);
        assert(tex.is_good);
        tex.uv0 = ImVec2(0, 0);
        tex.uv1 = ImVec2((float)((double)iview->width / (double)szpo2),
                                    (float)((double)iview->height / (double)szpo2));

        tex.sz_for_display = ImVec2((float)iview->width, (float)iview->height);
        iview->img_buf[0].allocate(szpo2*szpo2*4);
        iview->img_buf[1].allocate(szpo2*szpo2*4);
        memset(iview->img_buf[0].ptr, 0, szpo2*szpo2*4);
        memset(iview->img_buf[1].ptr, 0, szpo2*szpo2*4);
    }

    iview->update_func.func(&dbgr, &dview, iview->update_func.ptr, tex.width);
    void *buf = iview->img_buf[iview->draw_which_buf].ptr;
    assert(buf);

    tex.upload_data(buf, tex.width*tex.height*4, tex.width, tex.height);
}

ImVec2 full_system::output_size() const
{
    auto &v = should_hide_overscan() ? output.without_overscan : output.with_overscan;
    return {v.x_size, v.y_size};
}

ImVec2 full_system::output_uv0() const
{
    auto &v = should_hide_overscan() ? output.without_overscan.uv0 : output.with_overscan.uv0;
    return v;
}

ImVec2 full_system::output_uv1() const
{
    auto &v = should_hide_overscan() ? output.without_overscan.uv1 : output.with_overscan.uv1;
    return v;
}

bool full_system::can_hide_overscan() const
{
    return output.display && output.display->kind == jsm::display_kinds::CRT;
}

bool full_system::should_hide_overscan() const
{
    return output.hide_overscan && can_hide_overscan();
}

void full_system::setup_present_widgets(AppSettings& settings, const char* core_key)
{
    present_widgets.clear();
    if (!sys || !core_key || !core_key[0]) return;

    if (sys->kind == jsm::systems::APPLEIIe) {
        {
            i32 color_mode = settings.get_core_option(core_key, "color_mode", 0);
            std::vector<debugger_widget> tmp;
            debugger_widget& rg = debugger_widgets_add_radiogroup(tmp, "Color Mode", true, (u32)color_mode, false);
            debugger_widgets_add_checkbox(rg.radiogroup.buttons, "Monochrome",     true, false, false);
            debugger_widgets_add_checkbox(rg.radiogroup.buttons, "Green Phosphor", true, false, true);
            debugger_widgets_add_checkbox(rg.radiogroup.buttons, "Color NTSC",     true, false, true);
            rg.radiogroup.buttons[0].checkbox.value = 0;
            rg.radiogroup.buttons[1].checkbox.value = 1;
            rg.radiogroup.buttons[2].checkbox.value = 2;
            present_widget pw;
            snprintf(pw.ini_key, sizeof(pw.ini_key), "color_mode");
            pw.widget = std::move(tmp[0]);
            present_widgets.push_back(std::move(pw));
        }
        {
            bool respect_burst = (settings.get_core_option(core_key, "respect_burst", 1) != 0);
            std::vector<debugger_widget> tmp;
            debugger_widgets_add_checkbox(tmp, "Respect color burst suppression", true, respect_burst, false);
            present_widget pw;
            snprintf(pw.ini_key, sizeof(pw.ini_key), "respect_burst");
            pw.widget = std::move(tmp[0]);
            present_widgets.push_back(std::move(pw));
        }
    }
}

void full_system::debugger_pre_frame_waveforms(waveform_view &wv)
{

}

void full_system::debugger_pre_frame() {
    for (auto &wview : waveform_views) {
        for (auto &dwe: wview.waveforms) {
            debug_waveform *dw = dwe.wf;
            switch (dw->kind) {
                case dwk_none:
                    assert(1 == 2);
                    break;
                case dwk_main:
                    dw->samples_requested = 400;
                    break;
                case dwk_channel:
                    dw->samples_requested = 200;
                    break;
                default:
                    NOGOHERE;
            }
        }
    }
}

void full_system::discard_audio_buffers()
{
    audio.discard_emulated_buffers();
}

bool full_system::needs_input_mode() const
{
    if (!io.keyboard.vec) return false;
    return io.controller1.vec || io.controller2.vec ||
           io.controller3.vec || io.controller4.vec;
}

void full_system::cycle_input_mode()
{
    switch (input_mode) {
        case InputMode::KEYBOARD: input_mode = InputMode::JOYPAD;   break;
        case InputMode::JOYPAD:   input_mode = InputMode::BOTH;     break;
        case InputMode::BOTH:     input_mode = InputMode::KEYBOARD; break;
    }
}

void full_system::begin_fastforward()
{
    // Nothing needed — the ring overflows naturally and frames are dropped
    // silently (see audiowrap::commit_emu_buffer). end_fastforward() clears
    // any stale FF audio when returning to normal speed.
}

void full_system::end_fastforward()
{
    audio.discard_all_but_latest_buffers();
}

void full_system::reset_fps_meter()
{
    actual_fps = 0.0;
    fps_meter = {};
}

void full_system::update_fps_meter(double now)
{
    if (!sys) {
        reset_fps_meter();
        return;
    }

    framevars fv = {};
    sys->get_framevars(fv);

    if (!fps_meter.initialized) {
        fps_meter.frame = fv.master_frame;
        fps_meter.time = now;
        fps_meter.history_head = 0;
        fps_meter.history_count = 1;
        fps_meter.history_frames[0] = fv.master_frame;
        fps_meter.history_times[0] = now;
        fps_meter.initialized = true;
        actual_fps = 0.0;
        return;
    }

    if (fv.master_frame < fps_meter.frame || now < fps_meter.time) {
        fps_meter = {};
        fps_meter.frame = fv.master_frame;
        fps_meter.time = now;
        fps_meter.history_count = 1;
        fps_meter.history_frames[0] = fv.master_frame;
        fps_meter.history_times[0] = now;
        fps_meter.initialized = true;
        actual_fps = 0.0;
        return;
    }

    fps_meter.frame = fv.master_frame;
    fps_meter.time = now;

    constexpr u32 history_len = sizeof(fps_meter.history_frames) / sizeof(fps_meter.history_frames[0]);
    fps_meter.history_head = (fps_meter.history_head + 1) % history_len;
    fps_meter.history_frames[fps_meter.history_head] = fv.master_frame;
    fps_meter.history_times[fps_meter.history_head] = now;
    if (fps_meter.history_count < history_len)
        fps_meter.history_count++;

    constexpr double window = 2.5;
    u32 oldest = fps_meter.history_head;
    for (u32 i = 1; i < fps_meter.history_count; i++) {
        u32 idx = (fps_meter.history_head + history_len - i) % history_len;
        if ((now - fps_meter.history_times[idx]) > window)
            break;
        oldest = idx;
    }

    double elapsed = now - fps_meter.history_times[oldest];
    if (elapsed < 0.5)
        return;

    u64 frames = fv.master_frame - fps_meter.history_frames[oldest];
    actual_fps = static_cast<double>(frames) / elapsed;
}

void full_system::check_new_frame() {
    framevars fv;
    sys->get_framevars(fv);
    if (fv.master_frame != int_time.frames) {
        debugger_pre_frame();
    }

    sync_persistent_storage();
    int_time.scanlines = fv.scanline;
    int_time.cycles = fv.master_cycle;
    int_time.frames = fv.master_frame;
}

void full_system::advance_time(u32 cycles, u32 scanlines, u32 frames)
{
    check_new_frame();
    if (sys) {
        if (dbg.do_break) {
            printf("\nNO ADVANCe, DEBUG!");
            return;
        }
        if (cycles > 0) {
            sys->step_master(cycles);
            check_new_frame();
            if (dbg.do_break) {
                printf("\nNO ADVANCe, DEBUG!");
                return;
            }
        }
        if (scanlines) {
            for (u32 i = 0; i < scanlines; i++) {
                sys->finish_scanline();
                check_new_frame();
                if (dbg.do_break) {
                    printf("\nNO ADVANCe, DEBUG!");
                    return;
                }
            }
        }
        if (frames > 0) {
            for (u32 i = 0; i < frames; i++) {
                sys->finish_frame();
                check_new_frame();
                if (dbg.do_break) {
                    printf("\nNO ADVANCe, DEBUG!");
                    return;
                }
            }
        }
    }
    else {
        printf("\nCannot advance with no system.");
    }
}


void full_system::do_frame() {
    if (sys) {
        framevars fv = {};
        if (!dbg.do_break) {
            debugger_pre_frame();
            sys->finish_frame();
        }
        sys->get_framevars(fv);
        //dbg_flush();
        sync_persistent_storage();
        //TODO: here
    }
    else {
        printf("\nCannot do frame with no system.");
    }
}

void full_system::setup_tracing()
{
    //::dbg.traces.r3000.instruction = 1;
    //::dbg.traces.better_irq_multiplexer = 1;
    //dbg.traces.ps1.sio0.irq = 1;
    //dbg.traces.ps1.sio0.rw = 1;
    //dbg.traces.ps1.sio0.ack = 1;
    //dbg.traces.ps1.pad = 1;
    //dbg_enable_trace();
}
