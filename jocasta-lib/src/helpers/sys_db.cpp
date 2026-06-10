#include "sys_db.h"

#define BF(fn, hash, lbl) { fn, hash, lbl, false, false }
#define BF_OPT(fn, hash, lbl) { fn, hash, lbl, true, false }
#define BF_ANY(fn, lbl) { fn, nullptr, lbl, false, false }
#define BF_ANY_OPT(fn, lbl) { fn, nullptr, lbl, true, false }
#define BF_FW(fn, lbl) { fn, nullptr, lbl, false, true }
#define BF_FW_H(fn, hash, lbl) { fn, hash, lbl, false, true }

const BIOSEntry SYS_DB[] = {

    { jsm::systems::DMG, "gameboy", "Game Boy", false, 1, {
        BF_ANY("gb_bios.bin", "Boot ROM"),
    }},

    { jsm::systems::GBC, "gameboy", "Game Boy Color", false, 1, {
        BF("gbc_bios.bin",
           "b4f2e416a35eef52cba161b159c7c8523a92594facb924b3ede0d722867c50c7",
           "Boot ROM"),
    }},

    { jsm::systems::GBA, "gba", "Game Boy Advance", false, 1, {
        BF("gba_bios.bin",
           "fd2547724b505f487e6dcb29ec2ecff3af35a841a77ab2e85fd87350abd36570",
           "BIOS"),
    }},

    { jsm::systems::PS1, "ps1", "PlayStation", false, 1, {
        BF("scph1001.bin",
           "71af94d1e47a68c11e8fdb9f8368040601514a42a5a399cda48c7d3bff1e99d3",
           "BIOS (SCPH-1001 US)"),
    }},

    { jsm::systems::DREAMCAST, "dreamcast", "Dreamcast", false, 2, {
        BF("dc_boot.bin",
           "88d6a666495ad14ab5988d8cb730533cfc94ec2cfd53a7eeda14642ab0d4abf9",
           "Boot ROM"),
        BF_FW_H("dc_flash.bin",
           "dd8b365521a9c08bb4c170da3592212580f7251f35a45d6083774d1c030bc3e3",
           "Flash ROM"),
    }},

    { jsm::systems::NDS, "nds", "Nintendo DS", false, 3, {
        BF("bios7.bin",
           "ba65f690eb04ec92db67c0e299e21ad71de087d6d5de8a9cb17a62eaab563c17",
           "ARM7 BIOS"),
        BF("bios9.bin",
           "1693983a7707ae394786fa526c0552457888a51d4e410d715ef07acd5a540555",
           "ARM9 BIOS"),
        BF_FW("firmware.bin", "Firmware"),
    }},

    { jsm::systems::MAC128K, "mac", "Mac 128K", false, 1, {
        BF_ANY("mac128k.rom", "ROM"),
    }},

    { jsm::systems::MAC512K, "mac", "Mac 512K", false, 1, {
        BF("mac512k.rom",
           "fe6a1ceff5b3eefe32f20efea967cdf8cd4cada291ede040600e7f6c9e2dfc0e",
           "ROM"),
    }},

    { jsm::systems::MACPLUS_1MB, "mac", "Mac Plus 1MB", false, 1, {
        BF("macplus.rom",
           "06f598ff0f64c944e7c347ba55ae60c792824c09c74f4a55a32c0141bf91b8b3",
           "ROM"),
    }},

    { jsm::systems::APPLEIIe, "apple2", "Apple IIe", false, 3, {
        BF("apple2e.rom",
           "1fb812584c6633fa16b77b20915986ed1178d1e6fc07a647f7ee8d4e6ab9d40b",
           "Main ROM"),
        BF("apple2e_video.rom",
           "52c3b87900ac939f6525402cab1ccfd8f8259290fc6df54da48fb4c98ae3ed0f",
           "Video ROM"),
        BF_OPT("Apple Disk II 16 Sector Interface Card ROM P5 - 341-0027.bin",
               "de1e3e035878bab43d0af8fe38f5839c527e9548647036598ee6fe7ec74d2a7d",
               "Disk II Boot ROM"),
    }},

    { jsm::systems::ZX_SPECTRUM_48K, "zx_spectrum", "ZX Spectrum 48K", false, 1, {
        BF("zx48.rom",
           "d55daa439b673b0e3f5897f99ac37ecb45f974d1862b4dadb85dec34af99cb42",
           "ROM"),
    }},

    { jsm::systems::ZX_SPECTRUM_128K, "zx_spectrum", "ZX Spectrum 128K", false, 1, {
        BF("zx128.rom",
           "c1ff621d7910105d4ee45c31e9fd8fd0d79a545c78b66c69a562ee1ffbae8d72",
           "ROM"),
    }},

    { jsm::systems::GALAKSIJA, "galaksija", "Galaksija", false, 2, {
        BF("CHRGEN_MIPRO.bin",
           "9084463cf766f9082e93bfa002888b7acb102b5f29fe282386fb4bacaa4a523c",
           "Char Gen ROM"),
        BF("ROM_A_28.bin",
           "a28fdb91262a7612251c8d2b216797414aa36b2f4276d613777b5aca7e35860b",
           "ROM A"),
    }},

    { jsm::systems::COMMODORE64, "commodore64", "Commodore 64", false, 3, {
        BF("c64 r2.u3",
           "89878cea0a268734696de11c4bae593eaaa506465d2029d619c0e0cbccdfa62d",
           "BASIC ROM"),
        BF("c64 r2.u4",
           "675cf19a3e04bf68e41cc7a9010b353e51845ed9069db65496c52b79f1e1385f",
           "Kernal ROM"),
        BF("c64 r2.u5",
           "fd0d53b8480e86163ac98998976c72cc58d5dd8eb824ed7b829774e74213b420",
           "Chargen ROM"),
    }},

    { jsm::systems::NEOGEO_AES, "neogeo", "Neo Geo AES", true, 1, {
        BF("neo-po.bin",
           "b6e770df3f83ec559e2727357224eb8d31666402fc673f5b77586a465fdfbde3",
           "AES BIOS"),
    }},

    { jsm::systems::NEOGEO_MVS, "neogeo", "Neo Geo MVS", true, 1, {
        BF("sp-s2.sp1",
           "e70a563e28f462f03ccf967bb717f81648b09751072355066974defbeaa2f58a",
           "MVS BIOS"),
    }},

    { jsm::systems::NEOGEO_POCKET, "ngp", "Neo Geo Pocket", false, 1, {
        BF("bios.ngp",
           "8fb845a2f71514cec20728e2f0fecfade69444f8d50898b92c2259f1ba63e10d",
           "BIOS"),
    }},

    { jsm::systems::NEOGEO_POCKET_COLOR, "ngp", "Neo Geo Pocket Color", false, 1, {
        BF("bios.ngc",
           "8fb845a2f71514cec20728e2f0fecfade69444f8d50898b92c2259f1ba63e10d",
           "BIOS"),
    }},

    { jsm::systems::TURBOGRAFX16_CD, "tg16", "TurboGrafx-16 CD", false, 1, {
        BF("syscard3.pce",
           "df4f75feebb95e53dfef72dea0787df743e3474ba046ff5a4cb88e34dda93ff1",
           "System Card 3.0"),
    }},

    { jsm::systems::TURBOGRAFX16_ARCADE_CD, "tg16", "TurboGrafx-16 Arcade CD", false, 1, {
        BF_ANY("arcade_card_pro.pce", "Arcade Card Pro"),
    }},

};

#undef BF
#undef BF_OPT
#undef BF_ANY
#undef BF_ANY_OPT
#undef BF_FW
#undef BF_FW_H

const int SYS_DB_COUNT = static_cast<int>(sizeof(SYS_DB) / sizeof(SYS_DB[0]));

const BIOSEntry* sysdb_find_bios(jsm::systems sys)
{
    for (int i = 0; i < SYS_DB_COUNT; i++)
        if (SYS_DB[i].sys == sys) return &SYS_DB[i];
    return nullptr;
}

bool sysdb_bios_required(jsm::systems sys)
{
    const BIOSEntry* e = sysdb_find_bios(sys);
    if (!e) return false;
    for (int i = 0; i < e->num_files; i++)
        if (!e->files[i].optional) return true;
    return false;
}
