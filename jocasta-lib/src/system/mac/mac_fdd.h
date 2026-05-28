#pragma once

#include "component/misc/apple_iwm/apple_iwm.h"
#include "component/floppy/mac_floppy.h"

namespace mac {

struct core;

using FDD = APPLE_IWM::FDD<APPLE_IWM::mac, core, floppy::mac::DISC>;

}
