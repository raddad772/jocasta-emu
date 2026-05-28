#pragma once

#include <string>
#include <cstdio>
#include <vector>
#include <functional>
#include "nanosha256.h"
#include "helpers/enums.h"

// ---------------------------------------------------------------------------
// Status for a single BIOS file
// ---------------------------------------------------------------------------
enum class bios_status {
    unchecked,
    missing,
    bad_hash,
    unknown_hash,  // file present but no known-good hash to compare
    ok,
};

// ---------------------------------------------------------------------------
// Spec for one BIOS file
// ---------------------------------------------------------------------------
struct bios_file_spec {
    const char* filename;
    const char* sha256;   // nullptr = presence-only (any file is accepted)
    const char* label;    // human-readable name shown in the UI
    bool        optional; // if true, missing file is warned but does not block launch
};

// ---------------------------------------------------------------------------
// Known-good SHA256 hash table
// ---------------------------------------------------------------------------
// One entry per system. File order matches grab_BIOSes() expectations.
// Hashes verified against known-good dumps.
// ---------------------------------------------------------------------------

#define BFS(fn, hash, lbl)     { fn, hash,    lbl, false }
#define BFS_OPT(fn, hash, lbl) { fn, hash,    lbl, true  }
#define BFS_ANY(fn, lbl)       { fn, nullptr, lbl, false }
#define BFS_ANY_OPT(fn, lbl)   { fn, nullptr, lbl, true  }

struct bios_sys_spec {
    jsm::systems  sys;
    int           num_files;
    bios_file_spec files[4];
};

static const bios_sys_spec BIOS_SPECS[] = {

    { jsm::systems::DMG, 1, {
        BFS_ANY("gb_bios.bin", "Boot ROM"),
    }},

    { jsm::systems::GBC, 1, {
        BFS("gbc_bios.bin",
            "b4f2e416a35eef52cba161b159c7c8523a92594facb924b3ede0d722867c50c7",
            "Boot ROM"),
    }},

    { jsm::systems::GBA, 1, {
        BFS("gba_bios.bin",
            "fd2547724b505f487e6dcb29ec2ecff3af35a841a77ab2e85fd87350abd36570",
            "BIOS"),
    }},

    { jsm::systems::PS1, 1, {
        BFS("scph1001.bin",
            "71af94d1e47a68c11e8fdb9f8368040601514a42a5a399cda48c7d3bff1e99d3",
            "BIOS (SCPH-1001 US)"),
    }},

    { jsm::systems::DREAMCAST, 2, {
        BFS("dc_boot.bin",
            "88d6a666495ad14ab5988d8cb730533cfc94ec2cfd53a7eeda14642ab0d4abf9",
            "Boot ROM"),
        BFS("dc_flash.bin",
            "dd8b365521a9c08bb4c170da3592212580f7251f35a45d6083774d1c030bc3e3",
            "Flash"),
    }},

    { jsm::systems::NDS, 3, {
        BFS("bios7.bin",
            "ba65f690eb04ec92db67c0e299e21ad71de087d6d5de8a9cb17a62eaab563c17",
            "ARM7 BIOS"),
        BFS("bios9.bin",
            "1693983a7707ae394786fa526c0552457888a51d4e410d715ef07acd5a540555",
            "ARM9 BIOS"),
        BFS_ANY("firmware.bin", "Firmware"),
    }},

    { jsm::systems::MAC128K, 1, {
        BFS_ANY("mac128k.rom", "ROM"),
    }},

    { jsm::systems::MAC512K, 1, {
        BFS("mac512k.rom",
            "fe6a1ceff5b3eefe32f20efea967cdf8cd4cada291ede040600e7f6c9e2dfc0e",
            "ROM"),
    }},

    { jsm::systems::MACPLUS_1MB, 1, {
        BFS("macplus.rom",
            "06f598ff0f64c944e7c347ba55ae60c792824c09c74f4a55a32c0141bf91b8b3",
            "ROM"),
    }},

    { jsm::systems::APPLEIIe, 3, {
        BFS("apple2e.rom",
            "1fb812584c6633fa16b77b20915986ed1178d1e6fc07a647f7ee8d4e6ab9d40b",
            "Main ROM"),
        BFS("apple2e_video.rom",
            "52c3b87900ac939f6525402cab1ccfd8f8259290fc6df54da48fb4c98ae3ed0f",
            "Video ROM"),
        BFS_OPT("Apple Disk II 16 Sector Interface Card ROM P5 - 341-0027.bin",
            "de1e3e035878bab43d0af8fe38f5839c527e9548647036598ee6fe7ec74d2a7d",
            "Disk II Boot ROM (optional)"),
    }},

    { jsm::systems::ZX_SPECTRUM_48K, 1, {
        BFS("zx48.rom",
            "d55daa439b673b0e3f5897f99ac37ecb45f974d1862b4dadb85dec34af99cb42",
            "ROM"),
    }},

    { jsm::systems::ZX_SPECTRUM_128K, 1, {
        BFS("zx128.rom",
            "c1ff621d7910105d4ee45c31e9fd8fd0d79a545c78b66c69a562ee1ffbae8d72",
            "ROM"),
    }},

    { jsm::systems::GALAKSIJA, 2, {
        BFS("CHRGEN_MIPRO.bin",
            "9084463cf766f9082e93bfa002888b7acb102b5f29fe282386fb4bacaa4a523c",
            "Char Gen ROM"),
        BFS("ROM_A_28.bin",
            "a28fdb91262a7612251c8d2b216797414aa36b2f4276d613777b5aca7e35860b",
            "ROM A"),
    }},

    { jsm::systems::COMMODORE64, 3, {
        BFS("c64 r2.u3",
            "89878cea0a268734696de11c4bae593eaaa506465d2029d619c0e0cbccdfa62d",
            "BASIC ROM"),
        BFS("c64 r2.u4",
            "675cf19a3e04bf68e41cc7a9010b353e51845ed9069db65496c52b79f1e1385f",
            "Kernal ROM"),
        BFS("c64 r2.u5",
            "fd0d53b8480e86163ac98998976c72cc58d5dd8eb824ed7b829774e74213b420",
            "Chargen ROM"),
    }},

    { jsm::systems::NEOGEO_AES, 1, {
        // Individual file; neogeo.zip is also accepted by the loader
        BFS("neo-po.bin",
            "b6e770df3f83ec559e2727357224eb8d31666402fc673f5b77586a465fdfbde3",
            "AES BIOS"),
    }},

    { jsm::systems::NEOGEO_MVS, 1, {
        BFS("sp-s2.sp1",
            "e70a563e28f462f03ccf967bb717f81648b09751072355066974defbeaa2f58a",
            "MVS BIOS"),
    }},

    { jsm::systems::NEOGEO_POCKET, 1, {
        BFS("bios.ngp",
            "8fb845a2f71514cec20728e2f0fecfade69444f8d50898b92c2259f1ba63e10d",
            "BIOS"),
    }},

    { jsm::systems::NEOGEO_POCKET_COLOR, 1, {
        BFS("bios.ngc",
            "8fb845a2f71514cec20728e2f0fecfade69444f8d50898b92c2259f1ba63e10d",
            "BIOS"),
    }},
};
#undef BFS
#undef BFS_ANY

static const int NUM_BIOS_SPECS = (int)(sizeof(BIOS_SPECS) / sizeof(BIOS_SPECS[0]));

// ---------------------------------------------------------------------------
// Find the spec for a given system (returns nullptr if not in table)
// ---------------------------------------------------------------------------
static inline const bios_sys_spec* find_bios_spec(jsm::systems sys)
{
    for (int i = 0; i < NUM_BIOS_SPECS; i++)
        if (BIOS_SPECS[i].sys == sys) return &BIOS_SPECS[i];
    return nullptr;
}

// ---------------------------------------------------------------------------
// Hash one file with SHA-256 and return a lowercase hex string.
// Returns empty string on failure.
// ---------------------------------------------------------------------------
static inline std::string sha256_file(const std::string& path)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return {};

    SHA256_CTX ctx;
    SHA256_Init(&ctx);

    uint8_t buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        SHA256_Update(&ctx, buf, n);
    fclose(f);

    uint8_t hash[32];
    SHA256_Final(hash, &ctx);

    char hex[65];
    for (int i = 0; i < 32; i++)
        snprintf(hex + i * 2, 3, "%02x", hash[i]);
    hex[64] = '\0';
    return hex;
}

// ---------------------------------------------------------------------------
// Check a single BIOS file against its spec.
// dir should be the resolved bios directory for the system.
// ---------------------------------------------------------------------------
static inline bios_status check_bios_file(const std::string& dir, const bios_file_spec& spec)
{
    std::string path = dir;
    if (!path.empty() && path.back() != '/') path += '/';
    path += spec.filename;

    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return bios_status::missing;
    fclose(f);

    if (!spec.sha256) return bios_status::unknown_hash;

    std::string actual = sha256_file(path);
    if (actual.empty()) return bios_status::missing;
    return (actual == std::string(spec.sha256)) ? bios_status::ok : bios_status::bad_hash;
}

// ---------------------------------------------------------------------------
// Cached check results for all BIOS files.
// Call invalidate() whenever a bios_dir changes.
// Call run_checks() to re-hash all files (call once per settings-open).
// ---------------------------------------------------------------------------
struct bios_check_cache {
    struct entry {
        jsm::systems sys;
        int          file_idx;
        bios_status  status;
    };

    std::vector<entry> results;
    bool               dirty = true;

    void invalidate() { dirty = true; }

    void run_checks(const std::function<std::string(jsm::systems)>& get_dir)
    {
        results.clear();
        for (int si = 0; si < NUM_BIOS_SPECS; si++) {
            const bios_sys_spec& spec = BIOS_SPECS[si];
            std::string dir = get_dir(spec.sys);
            for (int fi = 0; fi < spec.num_files; fi++) {
                entry e;
                e.sys      = spec.sys;
                e.file_idx = fi;
                e.status   = check_bios_file(dir, spec.files[fi]);
                results.push_back(e);
            }
        }
        dirty = false;
    }

    bios_status get(jsm::systems sys, int file_idx) const
    {
        for (auto& e : results)
            if (e.sys == sys && e.file_idx == file_idx) return e.status;
        return bios_status::unchecked;
    }
};
