//
// Created by . on 3/9/25.
//

#include <cstdarg>
#include <cstring>
#include <cstdio>
#include "../debug.h"
#include "dbglog.h"
#include "debugger.h"

#define JOCASTA_TRACE_TO_FILE 0
#define JOCASTA_TRACE_FILE_PATH "/tmp/jocasta-trace.txt"

namespace {
struct dbglog_file_sink {
    bool initialized{};
    FILE *file{};
};

static dbglog_file_sink &get_dbglog_file_sink()
{
    static dbglog_file_sink sink{};
    if (sink.initialized) return sink;

    sink.initialized = true;
#if JOCASTA_TRACE_TO_FILE
    sink.file = fopen(JOCASTA_TRACE_FILE_PATH, "w");
    if (sink.file) {
        setvbuf(sink.file, nullptr, _IOLBF, 0);
        fputs("# jocasta trace start\n", sink.file);
    } else {
        fprintf(stderr, "JOCASTA_TRACE_FILE: failed to open '%s'\n", JOCASTA_TRACE_FILE_PATH);
    }
#endif

    return sink;
}

static void dbglog_file_write_line(const char *txt)
{
    dbglog_file_sink &sink = get_dbglog_file_sink();
    if (sink.file) fprintf(sink.file, "%s\n", txt);
}

static void dbglog_file_vwrite_line(const char *format, va_list va)
{
    dbglog_file_sink &sink = get_dbglog_file_sink();
    if (!sink.file) return;
    vfprintf(sink.file, format, va);
    fputc('\n', sink.file);
}
}

dbglog_category_node &dbglog_view::get_category_root()
{
    return category_root;
}

dbglog_category_node &dbglog_category_node::add_node(dbglog_view &dv, const char *inname, const char *inshort_name, u32 id, u32 color)
{
    //printf("\nAdd %s with ID %d", inname, id);
    dbglog_category_node &d = children.emplace_back();
    d.parent = this;
    snprintf(d.name, sizeof(d.name), "%s", inname);
    if (inshort_name)
        snprintf(d.short_name, sizeof(d.short_name), "%s", inshort_name);
    d.category_id = id;
    d.color = color;
    dv.id_to_category[id] = &d;
    dv.id_to_color[id] = color;
    return d;
}

void dbglog_view::add_item(u32 id, u64 timecode, dbglog_severity severity, const char *txt)
{
    printf("\nLog ID %d", id);
    if (!this->ids_enabled[id]) return;
    if (this->items.len >= MAX_DBGLOG_LINES) {
        // Move first entry forward
        this->items.first_entry = (this->items.first_entry + 1) % MAX_DBGLOG_LINES;
    }
    dbglog_entry *e = &this->items.data[this->items.next_entry];
    e->category_id = id;
    e->timecode = timecode;
    e->severity = severity;
    e->text.quickempty();
    e->extra.quickempty();
    e->text.sprintf("%s", txt);
    e->file_written = false;
    dbglog_file_write_line(e->text.ptr);
    e->file_written = true;

    this->items.next_entry = (this->items.next_entry + 1) % MAX_DBGLOG_LINES;

    if (this->items.len < MAX_DBGLOG_LINES) {
        this->items.len++;
    }
    this->updated = 3;
}


void dbglog_view::extra_printf( const char *format, ...) {
    dbglog_entry *e = last_added;
    if (!e) {
        printf("\nWARN IT IS NULL!?");
        return;
    }
    e->extra.quickempty();
    va_list va;
    va_start(va, format);
    e->extra.vsprintf(format, va);
    va_end(va);
    if (e->file_written && e->extra.ptr[0]) dbglog_file_write_line(e->extra.ptr);
    updated = 3;
}

void dbglog_view::add_printf(u32 id, u64 timecode, dbglog_severity severity, const char *format, ...) {
    if (!ids_enabled[id]) return;
    if (items.len >= MAX_DBGLOG_LINES) {
        // Move first entry forward
        items.first_entry = (items.first_entry + 1) % MAX_DBGLOG_LINES;
    }
    id_to_last_fires[id] = timecode;
    dbglog_entry *e = &items.data[items.next_entry];
    last_added = e;
    e->category_id = id;
    e->timecode = timecode;
    e->severity = severity;
    e->text.quickempty();
    e->extra.quickempty();
    e->file_written = false;

    items.next_entry = (items.next_entry + 1) % MAX_DBGLOG_LINES;

    if (items.len < MAX_DBGLOG_LINES) {
        items.len++;
    }

    va_list va;
    va_start(va, format);
    va_list file_va;
    va_copy(file_va, va);
    e->text.vsprintf(format, va);;
    va_end(va);
    dbglog_file_vwrite_line(format, file_va);
    va_end(file_va);
    e->file_written = true;
    updated = 3;
    if (id_break[id]) {
        char reason[500];
        snprintf(reason, sizeof(reason), "DBG BREAK FOR CAT: %s - %s!", id_to_category[id]->name, e->text.ptr);
        dbg_break(reason, timecode);
    }
}

void dbglog_view::render_to_buffer(char *output, u64 sz)
{
    u32 total_line = 0;
    u32 total_len = 0;
    char *ptr = output;
    *ptr = 0;
    u32 idx = items.first_entry;
    while (total_line<items.len) {
        dbglog_entry *item = &items.data[idx];
        idx = (idx + 1) % items.len;
        total_line++;
        if (!ids_enabled[item->category_id]) continue;

        if (total_len >= sz) {
            printf("\nERROR EXCEED VIEW RENDER BUFFER SIZE!");
            return;
        }
        u32 len = snprintf(ptr, sz - total_len, "\n%s", item->text.ptr);
        total_len += len;
        ptr += len;
    }
}

u32 dbglog_view::count_visible_lines()
{
    u32 n = 0;
    u32 idx = items.first_entry;
    for (u32 i = 0; i < items.len; i++) {
        dbglog_entry *e = &items.data[idx];
        if (ids_enabled[e->category_id]) n++;

        idx = (idx + 1) % items.len;
    }
    return n;
}

u32 dbglog_view::get_nth_visible(u32 n)
{
    u32 nth = 0;
    u32 idx = items.first_entry;
    for (u32 i = 0; i < items.len; i++) {
        dbglog_entry *e = &items.data[idx];
        if (ids_enabled[e->category_id]) {
            if (nth == n) return idx;
            nth++;
        }

        idx = (idx + 1) % items.len;
    }
    return 0;
}

u32 dbglog_view::get_next_visible(u32 start)
{
    u32 idx = (start + 1) % items.len;
    for (u32 i = 0; i < items.len; i++) {
        dbglog_entry *e = &items.data[idx];
        if (ids_enabled[e->category_id]) {
            return idx;
        }

        idx = (idx + 1) % items.len;
    }
    return start;
}
