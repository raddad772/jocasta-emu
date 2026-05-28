#include <cstdio>
#include <cstring>
#include <cstdlib>

#include "sys_interface.h"
#include "system/gb/gb.h"
#include "system/nes/nes.h"
#include "system/nds/nds.h"
#include "system/pv1000/pv1000.h"
#include "system/cosmac_vip/cosmac_vip.h"
#include "system/snes/snes.h"
#include "system/sms_gg/sms_gg.h"
#include "system/commodore64/commodore64.h"
#include "system/dreamcast/dreamcast.h"
#include "system/ps1/ps1.h"
#include "system/neogeo/neogeo.h"
#include "system/ngp/ngp.h"
#include "system/atari2600/atari2600.h"
#include "system/zxspectrum/zxspectrum.h"
#include "system/genesis/genesis.h"
#include "system/apple2/apple2.h"
#include "system/mac/mac.h"
#include "helpers/debug.h"
#include "system/gba/gba.h"
#include "system/galaksija/galaksija.h"
#include "system/gb/gb_enums.h"
#include "system/tg16/tg16.h"

jsm_system* new_system(jsm::systems which, const system_config& cfg)
{
    fflush(stdout);
    dbg_init();
    jsm_system* out = nullptr;
    switch (which) {
        case jsm::systems::GBA:
            out = GBA_new();
            break;
        case jsm::systems::NDS:
            out = NDS_new();
            break;
        case jsm::systems::COMMODORE64:
            out = Commodore64_new(which);
            break;
        case jsm::systems::SNES:
            out = SNES_new();
            break;
        case jsm::systems::TURBOGRAFX16:
            out = TG16_new(which);
            break;
        case jsm::systems::GENESIS_JAP:
        case jsm::systems::GENESIS_USA:
        case jsm::systems::MEGADRIVE_PAL:
            out = genesis_new(which);
            break;
        case jsm::systems::ATARI2600:
            out = atari2600_new();
            break;
		case jsm::systems::DMG:
			out = GB_new(GB::variants::DMG);
			break;
        case jsm::systems::GBC:
            out = GB_new(GB::variants::GBC);
            break;
        case jsm::systems::MAC128K:
            out = mac_new(mac::mac128k);
            break;
        case jsm::systems::MAC512K:
            out = mac_new(mac::mac512k);
            break;
        case jsm::systems::MACPLUS_1MB:
            out = mac_new(mac::macplus_1mb);
            break;
        case jsm::systems::COSMAC_VIP_2k:
        case jsm::systems::COSMAC_VIP_4k:
            out = VIP_new(which);
            break;
        case jsm::systems::NES:
            out = NES_new();
            break;
        case jsm::systems::CASIO_PV1000:
            out = CASIO_PV1000_new();
            break;
        case jsm::systems::NEOGEO_AES:
        case jsm::systems::NEOGEO_MVS:
            out = neogeo_new(which);
            break;
        case jsm::systems::NEOGEO_POCKET:
        case jsm::systems::NEOGEO_POCKET_COLOR:
            out = ngp_new(which);
            break;
        case jsm::systems::PS1:
            out = PS1_new();
            break;
        case jsm::systems::SG1000:
        case jsm::systems::SMS1:
        case jsm::systems::SMS2:
        case jsm::systems::GG:
            out = SMSGG_new(which, jsm::regions::USA);
            break;
        case jsm::systems::DREAMCAST:
            out = DC_new();
            break;
        case jsm::systems::APPLEIIe:
            out = apple2_new(cfg);
            break;
        case jsm::systems::ZX_SPECTRUM_48K:
            out = ZXSpectrum_new(ZXSpectrum::variants::spectrum48);
            break;
        case jsm::systems::GALAKSIJA:
            out = galaksija_new();
            break;
        case jsm::systems::ZX_SPECTRUM_128K:
            out = ZXSpectrum_new(ZXSpectrum::variants::spectrum128);
            break;
        default:
            printf("CREATE UNKNOWN SYSTEM!");
            break;
	}
    out->kind = which;
    //out->IOs->reserve(20);
    //out->opts->reserve(5);
    out->describe_io();
    populate_core_options(which, out->options);
    return out;
}

// ---------------------------------------------------------------------------
// Central core-options registry
// Defines option keys, labels, kinds, choices, and default values for every
// system.  Called by new_system() to pre-populate sys->options; also callable
// by the GUI for any system kind without a live core instance.
// ---------------------------------------------------------------------------

void populate_core_options(jsm::systems which, std::vector<jsm_core_option>& out)
{
    out.clear();

    auto add_enum = [&](const char* key, const char* label) -> jsm_core_option& {
        jsm_core_option opt;
        snprintf(opt.key,   sizeof(opt.key),   "%s", key);
        snprintf(opt.label, sizeof(opt.label), "%s", label);
        opt.kind = jsm_core_option::OPTION_ENUM;
        out.push_back(opt);
        return out.back();
    };
    auto add_bool = [&](const char* key, const char* label, i32 default_val = 0) {
        jsm_core_option opt;
        snprintf(opt.key,   sizeof(opt.key),   "%s", key);
        snprintf(opt.label, sizeof(opt.label), "%s", label);
        opt.kind  = jsm_core_option::OPTION_BOOL;
        opt.value = default_val;
        out.push_back(opt);
    };
    auto add_string = [&](const char* key, const char* label, const char* default_val = "") -> jsm_core_option& {
        jsm_core_option opt;
        snprintf(opt.key,       sizeof(opt.key),       "%s", key);
        snprintf(opt.label,     sizeof(opt.label),     "%s", label);
        snprintf(opt.str_value, sizeof(opt.str_value), "%s", default_val);
        opt.kind = jsm_core_option::OPTION_STRING;
        out.push_back(opt);
        return out.back();
    };
    (void)add_bool;   // suppress unused-warning until used
    (void)add_string;

    switch (which) {

        case jsm::systems::GBA: {
            auto& ci = add_enum("cached_interp", "CPU Mode");
            ci.add_choice("Interpreter",        0);
            ci.add_choice("Cached interpreter", 1);
            ci.value = 0; // default: off
            break;
        }

        case jsm::systems::DREAMCAST: {
            auto& ci = add_enum("aica_cached_interp", "AICA CPU Mode");
            ci.add_choice("Interpreter",        0);
            ci.add_choice("Cached interpreter", 1);
            ci.value = 0; // default: off
            break;
        }

        case jsm::systems::NDS: {
            // ARM7 CPU mode
            auto& arm7 = add_enum("arm7_mode", "ARM7 CPU Mode");
            arm7.add_choice("Interpreter",          0);
            arm7.add_choice("Cached interpreter",   1);
            arm7.value = 1; // default: cached

            // ARM9 CPU mode
            auto& arm9 = add_enum("arm9_mode", "ARM9 CPU Mode");
            arm9.add_choice("Interpreter",          0);
            arm9.add_choice("Cached interpreter",   1);
            arm9.value = 1; // default: cached
            break;
        }

        case jsm::systems::PS1: {
            // Memory card save location
            auto& mc = add_enum("memory_card_mode", "Memory Card");
            mc.add_choice("Per-game",  0);
            mc.add_choice("Universal", 1);
            mc.value = 0; // default: per-game

            // Name stem for the .mcd file used when booting from BIOS with no disc.
            // E.g. "bios" ... saves/ps1/bios.mcd
            add_string("bios_memcard_name", "BIOS Boot Card", "bios");
            break;
        }

        default:
            break;
    }
}
