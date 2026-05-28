#include "user.h"

#if defined(_WIN32)
#include <windows.h>
#include <cstdlib>
#else
#include <cstdio>
#include <unistd.h>
#include <pwd.h>
#include <cstdlib>
#endif

const char* get_user_dir()
{
#if defined(_WIN32)
    const char* homeDir = getenv("USERPROFILE");
    if (!homeDir)
        homeDir = getenv("HOME");
#elif !defined (FOR_DREAMCAST)
    const char *homeDir = getenv("HOME");

    if (!homeDir) {
        passwd* pwd = getpwuid(getuid());
        if (pwd)
            homeDir = pwd->pw_dir;
    }
#else
    const char* homeDir = "/";
    printf("\nSOMEHOW GOT HERE...\n");
#endif
    return homeDir ? homeDir : "";
}
