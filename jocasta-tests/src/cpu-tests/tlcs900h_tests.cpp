#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <string>
#include <vector>

#include "component/cpu/tlcs900h/tlcs900h.h"
#include "cpu-test-helpers.h"
#include "helpers/multisize_memaccess.cpp"
#include "tlcs900h_tests.h"

#define M8 1
#define M16 2
#define M32 4
#define M64 8

#define R8 (cR[M8 ](filebuf, ((offset += 1) - 1)))
#define R16 (cR[M16](filebuf, ((offset += 2) - 2)))
#define R32 (cR[M32](filebuf, ((offset += 4) - 4)))
#define R64 (cR[M64](filebuf, ((offset += 8) - 8)))

namespace {

constexpr u32 TLCS900H_RAM_SIZE = 0x1000000;
constexpr u32 TLCS900H_RAM_MASK = TLCS900H_RAM_SIZE - 1;

enum tlcs900h_bus_action : u32 {
    T9_BUS_READ = 1,
    T9_BUS_WRITE = 2
};

struct tlcs900h_cpu_state {
    u32 xwa[4];
    u32 xbc[4];
    u32 xde[4];
    u32 xhl[4];
    u32 xix;
    u32 xiy;
    u32 xiz;
    u32 xsp;
    u32 pc;
    u32 dmas[4];
    u32 dmad[4];
    u32 dmam[4];
    u32 intnest;
    u32 cf;
    u32 nf;
    u32 vf;
    u32 hf;
    u32 zf;
    u32 sf;
    u32 ca;
    u32 na;
    u32 va;
    u32 ha;
    u32 za;
    u32 sa;
    u32 rfp;
    u32 iff;
    u32 halt;
    u32 op;
    u32 pic;
    u32 piq_size;
    u32 piq[4];
    u32 mar;
    u32 mdr;
};

struct tlcs900h_ram_entry {
    u32 addr;
    u32 data;
};

struct tlcs900h_cycle {
    u64 clock;
    u32 action;
    u32 size;
    u32 addr;
    u32 data;
    u32 opcode;
};

struct tlcs900h_test {
    char family[256];
    char name[256];
    std::vector<u8> opcode;
    tlcs900h_cpu_state initial;
    tlcs900h_cpu_state final;
    std::vector<tlcs900h_ram_entry> initial_ram;
    std::vector<tlcs900h_ram_entry> final_ram;
    std::vector<tlcs900h_cycle> cycles;
};

struct tlcs900h_context {
    TLCS900H::core cpu{nullptr, 0, 1};
    tlcs900h_test test;
    u32 real_idx;
    bool failed;
};

tlcs900h_context gstate;
u8 *filebuf;

static u8 pack_flags(u32 c, u32 n, u32 v, u32 h, u32 z, u32 s)
{
    return (c & 1) | ((n & 1) << 1) | ((v & 1) << 2) | ((h & 1) << 4) | ((z & 1) << 6) | ((s & 1) << 7);
}

static u32 mask_data(u32 data, u32 size)
{
    switch (size) {
        case 1: return data & 0xFF;
        case 2: return data & 0xFFFF;
        default: return data;
    }
}

static u32 tlcs900h_reg_index(u32 bank, u32 slot)
{
    return (bank * 4) + slot;
}

static u32 get_xwa(u32 bank)
{
    return gstate.cpu.regs.R[tlcs900h_reg_index(bank, 0)].dw;
}

static u32 get_xbc(u32 bank)
{
    return gstate.cpu.regs.R[tlcs900h_reg_index(bank, 1)].dw;
}

static u32 get_xde(u32 bank)
{
    return gstate.cpu.regs.R[tlcs900h_reg_index(bank, 2)].dw;
}

static u32 get_xhl(u32 bank)
{
    return gstate.cpu.regs.R[tlcs900h_reg_index(bank, 3)].dw;
}

static void set_xwa(u32 bank, u32 value)
{
    gstate.cpu.regs.R[tlcs900h_reg_index(bank, 0)].dw = value;
}

static void set_xbc(u32 bank, u32 value)
{
    gstate.cpu.regs.R[tlcs900h_reg_index(bank, 1)].dw = value;
}

static void set_xde(u32 bank, u32 value)
{
    gstate.cpu.regs.R[tlcs900h_reg_index(bank, 2)].dw = value;
}

static void set_xhl(u32 bank, u32 value)
{
    gstate.cpu.regs.R[tlcs900h_reg_index(bank, 3)].dw = value;
}

static u32 do_access(u32 action, u32 size, u32 addr, u32 wval)
{
    addr &= TLCS900H_RAM_MASK;
    if (gstate.real_idx >= gstate.test.cycles.size()) {
        printf("\n    unexpected %s size=%u addr=%06X (only %zu transactions expected)",
               action == T9_BUS_READ ? "read" : "write", size, addr, gstate.test.cycles.size());
        gstate.failed = true;
        return 0;
    }

    const tlcs900h_cycle &c = gstate.test.cycles[gstate.real_idx];
    u32 caddr = c.addr & TLCS900H_RAM_MASK;
    u32 cdata = mask_data(c.data, c.size);

    if (c.action != action || c.size != size || caddr != addr ||
       (action == T9_BUS_WRITE && cdata != mask_data(wval, size))) {
        printf("\n    bus[%u] expected action=%u size=%u addr=%06X data=%08X  got action=%u size=%u addr=%06X data=%08X",
               gstate.real_idx, c.action, c.size, caddr, cdata,
               action, size, addr, mask_data(wval, size));
        gstate.failed = true;
    }

    u64 myclock = *gstate.cpu.clock;
    if (myclock != c.clock) {
        printf("\n    bus[%u] clock expected %llu got %llu", gstate.real_idx,
               (unsigned long long)c.clock, (unsigned long long)myclock);
        gstate.failed = true;
    }

    gstate.real_idx++;
    return cdata;
}

static u8 test_read8 (void *, u32 addr) { return do_access(T9_BUS_READ, 1, addr, 0); }
static u16 test_read16(void *, u32 addr) { return do_access(T9_BUS_READ, 2, addr, 0); }
static u32 test_read32(void *, u32 addr) { return do_access(T9_BUS_READ, 4, addr, 0); }

static void test_write8 (void *, u32 addr, u8 val) { do_access(T9_BUS_WRITE, 1, addr, val); }
static void test_write16(void *, u32 addr, u16 val) { do_access(T9_BUS_WRITE, 2, addr, val); }
static void test_write32(void *, u32 addr, u32 val) { do_access(T9_BUS_WRITE, 4, addr, val); }

static void test_idle(void *, u32)
{
}

static u32 read_string(char *out, size_t out_size, u32 offset)
{
    u32 len = R32;
    u32 copy_len = len;
    if (copy_len >= out_size) copy_len = static_cast<u32>(out_size - 1);

    memcpy(out, filebuf + offset, copy_len);
    out[copy_len] = 0;
    offset += len;
    return offset;
}

static u32 read_bytes(std::vector<u8> &out, u32 offset)
{
    u32 len = R32;
    out.resize(len);
    if (len > 0) {
        memcpy(out.data(), filebuf + offset, len);
    }
    offset += len;
    return offset;
}

static u32 read_cpu_state(tlcs900h_cpu_state &state, u32 offset)
{
    for (u32 n = 0; n < 4; n++) state.xwa[n] = R32;
    for (u32 n = 0; n < 4; n++) state.xbc[n] = R32;
    for (u32 n = 0; n < 4; n++) state.xde[n] = R32;
    for (u32 n = 0; n < 4; n++) state.xhl[n] = R32;
    state.xix = R32;
    state.xiy = R32;
    state.xiz = R32;
    state.xsp = R32;
    state.pc = R32;
    for (u32 n = 0; n < 4; n++) state.dmas[n] = R32;
    for (u32 n = 0; n < 4; n++) state.dmad[n] = R32;
    for (u32 n = 0; n < 4; n++) state.dmam[n] = R32;
    state.intnest = R32;
    state.cf = R32;
    state.nf = R32;
    state.vf = R32;
    state.hf = R32;
    state.zf = R32;
    state.sf = R32;
    state.ca = R32;
    state.na = R32;
    state.va = R32;
    state.ha = R32;
    state.za = R32;
    state.sa = R32;
    state.rfp = R32;
    state.iff = R32;
    state.halt = R32;
    state.op = R32;
    state.pic = R32;
    state.piq_size = R32;
    for (u32 n = 0; n < 4; n++) state.piq[n] = R32;
    state.mar = R32;
    state.mdr = R32;
    return offset;
}

static u32 read_ram_entries(std::vector<tlcs900h_ram_entry> &entries, u32 offset)
{
    u32 count = R32;
    entries.resize(count);
    for (u32 n = 0; n < count; n++) {
        entries[n].addr = R32;
        entries[n].data = R32;
    }
    return offset;
}

static u32 read_cycles(std::vector<tlcs900h_cycle> &cycles, u32 offset)
{
    u32 count = R32;
    cycles.resize(count);
    for (u32 n = 0; n < count; n++) {
        cycles[n].clock = R64;
        cycles[n].action = R32;
        cycles[n].size = R32;
        cycles[n].addr = R32;
        cycles[n].data = R32;
        cycles[n].opcode = R32;
    }
    return offset;
}

static u32 read_test(u32 offset)
{
    offset = read_string(gstate.test.name, sizeof(gstate.test.name), offset);
    offset = read_bytes(gstate.test.opcode, offset);
    offset = read_cpu_state(gstate.test.initial, offset);
    offset = read_cpu_state(gstate.test.final, offset);
    offset = read_ram_entries(gstate.test.initial_ram, offset);
    offset = read_ram_entries(gstate.test.final_ram, offset);
    offset = read_cycles(gstate.test.cycles, offset);
    return offset;
}

static void load_state_to_cpu()
{
    const tlcs900h_cpu_state &state = gstate.test.initial;

    gstate.cpu.regs = {};
    gstate.cpu.halted = state.halt != 0;
    gstate.cpu.my_cycles = 0;
    gstate.cpu.cur_sz = 0;
    gstate.cpu.cur_reg = 0;

    for (u32 n = 0; n < 4; n++) {
        set_xwa(n, state.xwa[n]);
        set_xbc(n, state.xbc[n]);
        set_xde(n, state.xde[n]);
        set_xhl(n, state.xhl[n]);
    }
    gstate.cpu.regs.R[TLCS900H::XIX].dw = state.xix;
    gstate.cpu.regs.R[TLCS900H::XIY].dw = state.xiy;
    gstate.cpu.regs.R[TLCS900H::XIZ].dw = state.xiz;
    gstate.cpu.regs.R[TLCS900H::XSP].dw = state.xsp;
    gstate.cpu.regs.PC = state.pc & TLCS900H_RAM_MASK;
    gstate.cpu.regs.INTNEST = state.intnest;

    gstate.cpu.regs.SR.u = 0;
    gstate.cpu.regs.SR.C = state.cf & 1;
    gstate.cpu.regs.SR.N = state.nf & 1;
    gstate.cpu.regs.SR.V = state.vf & 1;
    gstate.cpu.regs.SR.H = state.hf & 1;
    gstate.cpu.regs.SR.Z = state.zf & 1;
    gstate.cpu.regs.SR.S = state.sf & 1;
    gstate.cpu.regs.SR.RFP = state.rfp & 3;
    gstate.cpu.regs.SR.IFF = state.iff & 7;
    gstate.cpu.regs.SR.MAX = 1;
    gstate.cpu.regs.SR.SYSM = 1;
    gstate.cpu.regs.F_ = pack_flags(state.ca, state.na, state.va, state.ha, state.za, state.sa);

    for (u32 n = 0; n < 4; n++) {
        gstate.cpu.regs.dmas[n].dw = state.dmas[n];
        gstate.cpu.regs.dmad[n].dw = state.dmad[n];
        gstate.cpu.regs.dmam[n].dw = state.dmam[n];
    }

    gstate.cpu.PIC = static_cast<u8>(state.pic);
    gstate.cpu.PIQ_size = static_cast<u8>(state.piq_size);
    for (u32 n = 0; n < 4; n++) gstate.cpu.PIQ[n] = static_cast<u8>(state.piq[n]);
}

static bool compare_value(const char *name, u32 mine, u32 theirs)
{
    if (mine == theirs) return true;

    printf("\n    %s expected %08X got %08X", name, theirs, mine);
    gstate.failed = true;
    return false;
}

static void compare_state()
{
    const tlcs900h_cpu_state &state = gstate.test.final;
    char name[32];

    for (u32 n = 0; n < 4; n++) {
        snprintf(name, sizeof(name), "XWA%u", n);
        compare_value(name, get_xwa(n), state.xwa[n]);
        snprintf(name, sizeof(name), "XBC%u", n);
        compare_value(name, get_xbc(n), state.xbc[n]);
        snprintf(name, sizeof(name), "XDE%u", n);
        compare_value(name, get_xde(n), state.xde[n]);
        snprintf(name, sizeof(name), "XHL%u", n);
        compare_value(name, get_xhl(n), state.xhl[n]);
    }

    compare_value("XIX", gstate.cpu.regs.R[TLCS900H::XIX].dw, state.xix);
    compare_value("XIY", gstate.cpu.regs.R[TLCS900H::XIY].dw, state.xiy);
    compare_value("XIZ", gstate.cpu.regs.R[TLCS900H::XIZ].dw, state.xiz);
    compare_value("XSP", gstate.cpu.regs.R[TLCS900H::XSP].dw, state.xsp);
    compare_value("PC", gstate.cpu.regs.PC & TLCS900H_RAM_MASK, state.pc & TLCS900H_RAM_MASK);
    compare_value("INTNEST", gstate.cpu.regs.INTNEST, state.intnest);

    compare_value("CF", gstate.cpu.regs.SR.C, state.cf & 1);
    compare_value("NF", gstate.cpu.regs.SR.N, state.nf & 1);
    compare_value("VF", gstate.cpu.regs.SR.V, state.vf & 1);
    compare_value("HF", gstate.cpu.regs.SR.H, state.hf & 1);
    compare_value("ZF", gstate.cpu.regs.SR.Z, state.zf & 1);
    compare_value("SF", gstate.cpu.regs.SR.S, state.sf & 1);
    compare_value("RFP", gstate.cpu.regs.SR.RFP, state.rfp & 3);
    compare_value("IFF", gstate.cpu.regs.SR.IFF, state.iff & 7);
    compare_value("F_", gstate.cpu.regs.F_, pack_flags(state.ca, state.na, state.va, state.ha, state.za, state.sa));
    compare_value("HALT", gstate.cpu.halted ? 1 : 0, state.halt ? 1 : 0);

    for (u32 n = 0; n < 4; n++) {
        snprintf(name, sizeof(name), "DMAS%u", n);
        compare_value(name, gstate.cpu.regs.dmas[n].dw, state.dmas[n]);
        snprintf(name, sizeof(name), "DMAD%u", n);
        compare_value(name, gstate.cpu.regs.dmad[n].dw, state.dmad[n]);
        snprintf(name, sizeof(name), "DMAM%u", n);
        compare_value(name, gstate.cpu.regs.dmam[n].dw, state.dmam[n]);
    }
}

static bool run_current_test()
{
    gstate.failed = false;
    gstate.real_idx = 0;
    gstate.cpu.local_clock = 0;
    gstate.cpu.next_cycle = 0;
    load_state_to_cpu();

    gstate.cpu.decode_and_exec<false>();

    compare_state();

    if (gstate.real_idx != gstate.test.cycles.size()) {
        printf("\n    transactions: consumed %u of %zu", gstate.real_idx, gstate.test.cycles.size());
        gstate.failed = true;
    }

    if (gstate.failed) {
        printf("\n  %s failed", gstate.test.name);
    }
    return !gstate.failed;
}

static bool test_ends_with(const char *name, const char *suffix)
{
    size_t name_len = strlen(name);
    size_t suffix_len = strlen(suffix);
    if (name_len < suffix_len) return false;
    return strcmp(name + name_len - suffix_len, suffix) == 0;
}

static bool run_test_file(const char *path, const char *display_name)
{
    FILE *f = fopen(path, "rb");
    if (f == nullptr) {
        printf("\nCould not open %s", path);
        return false;
    }

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    assert(len >= 0);

    filebuf = static_cast<u8 *>(malloc(static_cast<size_t>(len)));
    assert(filebuf != nullptr);
    size_t read_len = fread(filebuf, 1, static_cast<size_t>(len), f);
    fclose(f);
    assert(read_len == static_cast<size_t>(len));

    u32 offset = 0;
    if (memcmp(filebuf, "T9SSTB1\0", 8) != 0) {
        printf("\nBad TLCS900H SST magic in %s", display_name);
        free(filebuf);
        return false;
    }
    offset += 8;

    u32 version = R32;
    u32 count = R32;
    offset = read_string(gstate.test.family, sizeof(gstate.test.family), offset);
    if (version != 1) {
        printf("\nUnsupported TLCS900H SST version %u in %s", version, display_name);
        free(filebuf);
        return false;
    }

    u32 fails = 0;
    for (u32 n = 0; n < count; n++) {
        offset = read_test(offset);
        if (!run_current_test()) fails++;
    }
    if (fails) printf("  (%u/%u tests failed)", fails, count);

    free(filebuf);
    filebuf = nullptr;
    return fails == 0;
}

}

void test_tlcs900h()
{
    char path[500];
    construct_cpu_test_path(path, "tlcs900h", "", sizeof(path));

    gstate.cpu.mem_ptr = &gstate;
    gstate.cpu.read8 = &test_read8;
    gstate.cpu.read16 = &test_read16;
    gstate.cpu.read32 = &test_read32;
    gstate.cpu.write8 = &test_write8;
    gstate.cpu.write16 = &test_write16;
    gstate.cpu.write32 = &test_write32;
    gstate.cpu.idle = &test_idle;

    DIR *d = opendir(path);
    if (d == nullptr) {
        printf("\nFailed to open TLCS900H test path %s", path);
        return;
    }

    std::vector<std::string> filenames;
    dirent *entry;
    while ((entry = readdir(d)) != nullptr) {
        if (test_ends_with(entry->d_name, ".bin")) {
            filenames.emplace_back(entry->d_name);
        }
    }
    closedir(d);
    std::sort(filenames.begin(), filenames.end());

    u32 passed = 0, failed = 0;
    for (size_t n = 0; n < filenames.size(); n++) {
        char full_path[700];
        snprintf(full_path, sizeof(full_path), "%s/%s", path, filenames[n].c_str());
        if (run_test_file(full_path, filenames[n].c_str())) passed++;
        else { failed++; printf("  <== FAIL: %s", filenames[n].c_str()); }
    }

    printf("\nTLCS900H: %u/%zu test files passed, %u failed", passed, filenames.size(), failed);
}
