//
// Created by Dave on 2/6/2024.
//
#include <cstdlib>
#include <cassert>
#include <cstdio>
#include <cstring>

#include "buf.h"
#include "file_exists.h"

void BUF::allocate(size_t insize)
{
    if (ptr != nullptr) {
        free(ptr);
        ptr = nullptr;
    }
    dirty = 0;
    if (insize == 0) {
        ptr = nullptr;
        size = 0;
        return;
    }
    ptr = malloc(insize);
    size = insize;
}

void BUF::del() {
    if (ptr)
        free(ptr);
    ptr = nullptr;
    size = 0;
}

void BUF::copy(const BUF* src) {
    if (src->ptr == nullptr) {
        del();
        return;
    }
    allocate(src->size);
    if (src->size > 0)
        memcpy(ptr, src->ptr, src->size);
}

int read_file_buf::read(const char *fname, const char *fpath)
{
    char OUTPATH[500];
    if (fname == nullptr) {
        snprintf(OUTPATH, sizeof(OUTPATH), "%s", fpath ? fpath : "");
        snprintf(path, sizeof(path), "%s", fpath ? fpath : "");
        name[0] = 0;
    }
    else {
        snprintf(OUTPATH, sizeof(OUTPATH), "%s/%s", fpath ? fpath : "", fname);
        snprintf(name, sizeof(name), "%s", fname);
        snprintf(path, sizeof(path), "%s", fpath ? fpath : "");
    }
    if (!file_exists(OUTPATH)) {
        printf("\nFILE \"%s\" NOT FOUND", OUTPATH);
        return 0;
    }
    //printf("\nFILE %s", OUTPATH);

    FILE *fil = fopen(OUTPATH, "rb");
    fseek(fil, 0L, SEEK_END);
    buf.allocate(ftell(fil));

    fseek(fil, 0L, SEEK_SET);
    fread(buf.ptr, sizeof(char), buf.size, fil);

    fclose(fil);
    return 1;
}

void multi_file_set::clear() {
    files.clear();
}


void multi_file_set::add(const char *fname, const char *fpath) {
    auto &r = files.emplace_back();
    if (!r.read(fname, fpath)) {
        printf("\nERROR GETTING FILE %s", fname);
    }
}

void multi_file_set::add_from_buf(const char *fname, const char *fpath, const void *ptr, size_t size)
{
    auto &r = files.emplace_back();
    snprintf(r.name, sizeof(r.name), "%s", fname ? fname : "");
    snprintf(r.path, sizeof(r.path), "%s", fpath ? fpath : "");
    r.pos = 0;
    r.buf.allocate(size);
    if (size > 0 && ptr)
        memcpy(r.buf.ptr, ptr, size);
}
