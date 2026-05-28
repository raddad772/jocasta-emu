#pragma once

#include <cstdlib>
#include <cstdio>
#include "user.h"

inline char* construct_path_with_home(char* w, size_t w_sz, const char* who)
{
    const char* homeDir = get_user_dir();
    return w + snprintf(w, w_sz, "%s/%s", homeDir, who);
}
