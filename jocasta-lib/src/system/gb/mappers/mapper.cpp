#include <cstdlib>
#include <cstdio>
#include <cassert>

#include "../cart.h"
#include "mapper.h"
#include "no_mapper.h"
#include "mbc1.h"
#include "mbc2.h"
#include "mbc3.h"
#include "mbc5.h"


namespace GB {
struct CLOCK;
struct core;
}

namespace GB::MAPPER {

base* new_mapper(CLOCK* clock_in, core* bus_in, mappers which)
{
	base* mapper = static_cast<base *>(malloc(sizeof(base)));
	mapper->which = which;
    mapper->ptr = nullptr;
	switch (which) {
	case mappers::NONE: // No mapper!
		none_new(mapper, clock_in, bus_in);
        printf("\nNO MAPPER!");
		break;
    case mappers::MBC1:
        MBC1_new(mapper, clock_in, bus_in);
        printf("\nMBC1");
        break;
    case mappers::MBC2:
        MBC2_new(mapper, clock_in, bus_in);
        printf("\nMBC2");
        break;
    case mappers::MBC3:
        MBC3_new(mapper, clock_in, bus_in);
        printf("\nMBC3");
        break;
    case mappers::MBC5:
        MBC5_new(mapper, clock_in, bus_in);
        printf("\nMBC5");
        break;
	default:
		printf("\nNO SUPPORTED MAPPER! %d", which);
		break;
	}
	return mapper;
}

void delete_mapper(base* whom)
{
	switch (whom->which) {
	case mappers::NONE: // No-mapper!
		none_delete(whom);
		break;
    case mappers::MBC1:
        MBC1_delete(whom);
        break;
    case mappers::MBC2:
        MBC2_delete(whom);
        break;
    case mappers::MBC3:
        MBC3_delete(whom);
        break;
    case mappers::MBC5:
        MBC5_delete(whom);
        break;
    default:
        assert(1==0);
        break;
	}
    free(whom);
}
}