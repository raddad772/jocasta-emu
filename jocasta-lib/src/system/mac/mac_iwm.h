#pragma once

#include "component/misc/apple_iwm/apple_iwm.h"
#include "component/floppy/mac_floppy.h"
#include "mac.h"

namespace mac {

struct core;

static u32 mac_iwm_num_drives(variants variant)
{
    switch(variant) {
        case mac128k:
        case mac512k:
            return 1;
        case macplus_1mb:
            return 2;
        default:
            NOGOHERE;
    }
    return 0;
}

struct IWM : APPLE_IWM::IWM<APPLE_IWM::mac, core, floppy::mac::DISC> {
    variants variant;

    IWM(core *parent, variants variant_in) :
        APPLE_IWM::IWM<APPLE_IWM::mac, core, floppy::mac::DISC>(parent, mac_iwm_num_drives(variant_in)),
        variant(variant_in) {}
};

}
