//
// Created by . on 8/4/24.
//

#include "file_exists.h"
#ifdef WIN32
#include <io.h>
#define F_OK 0
#define access _access

#else
#include <unistd.h>
#endif

#ifdef FOR_DREAMCAST
#include <cstdio>
#endif

u32 file_exists(const char *fname)
{
#ifdef FOR_DREAMCAST
    FILE *f = fopen(fname, "rb");
    bool did = f != nullptr;
    fclose(f);
    return did;
#else
    return access(fname, F_OK) == 0;
#endif
}


