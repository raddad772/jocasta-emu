//
// Created by . on 5/12/26.
//

#include <cctype>
#include <cstring>

#include "ng_bus.h"
#include "ng_cart.h"

namespace NEOGEO {

void CART::reset() {
    P_bank = 0;
}

CART::CART(core *parent) :
    bus(parent)
{
}

static bool name_ends_with_neo(const char *name)
{
    size_t len = strlen(name);
    if (len < 4) return false;

    const char *ext = name + len - 4;
    return ext[0] == '.' &&
           std::tolower((unsigned char)ext[1]) == 'n' &&
           std::tolower((unsigned char)ext[2]) == 'e' &&
           std::tolower((unsigned char)ext[3]) == 'o';
}

static u32 read_le32(const u8 *ptr)
{
    return static_cast<u32>(ptr[0]) |
           (static_cast<u32>(ptr[1]) << 8) |
           (static_cast<u32>(ptr[2]) << 16) |
           (static_cast<u32>(ptr[3]) << 24);
}

static void clear_region(simplebuf8& dst)
{
    dst.allocate(0);
}

static bool copy_neo_region(simplebuf8& dst,
                            const u8 *src,
                            size_t file_size,
                            size_t& offset,
                            u32 region_size)
{
    clear_region(dst);

    if (region_size == 0)
        return true;

    if (offset + region_size > file_size)
        return false;

    dst.allocate(region_size);
    memcpy(dst.ptr, src + offset, region_size);
    offset += region_size;
    return true;
}

static bool copy_neo_program(simplebuf8& p0,
                             simplebuf8& p1,
                             const u8 *src,
                             size_t file_size,
                             size_t& offset,
                             u32 region_size)
{
    clear_region(p0);
    clear_region(p1);

    if (region_size == 0)
        return true;

    if (offset + region_size > file_size)
        return false;

    size_t p0_size = region_size;
    if (p0_size > 0x100000)
        p0_size = 0x100000;

    if (p0_size) {
        p0.allocate(p0_size);
        memcpy(p0.ptr, src + offset, p0_size);
    }

    size_t p1_size = region_size - p0_size;
    if (p1_size) {
        p1.allocate(p1_size);
        memcpy(p1.ptr, src + offset + p0_size, p1_size);
    }

    offset += region_size;
    return true;
}

static read_file_buf *find_neo_file(multi_file_set& mfs)
{
    for (auto& file : mfs.files) {
        if (name_ends_with_neo(file.name))
            return &file;
    }

    return nullptr;
}

bool CART::load(multi_file_set &mfs, physical_io_device &which_pio)
{
    (void)which_pio;

    P[0].allocate(0);
    P[1].allocate(0);
    M.allocate(0);
    C.allocate(0);
    S.allocate(0);
    V[0].allocate(0);
    V[1].allocate(0);

    read_file_buf *file = find_neo_file(mfs);
    if (!file) {
        printf("\nNeoGeo cart loader only supports .neo files right now");
        return false;
    }

    if (file->buf.ptr == nullptr || file->buf.size < 4096) {
        printf("\nBad .neo file %s", file->name);
        return false;
    }

    const auto *src = static_cast<const u8 *>(file->buf.ptr);
    u32 p_size = read_le32(src + 4);
    u32 s_size = read_le32(src + 8);
    u32 m_size = read_le32(src + 12);
    u32 v1_size = read_le32(src + 16);
    u32 v2_size = read_le32(src + 20);
    u32 c_size = read_le32(src + 24);
    u32 ngh = read_le32(src + 40);

    u64 total = 4096ULL + p_size + s_size + m_size + v1_size + v2_size + c_size;
    if (total > file->buf.size) {
        printf("\n.neo file %s is shorter than its header says", file->name);
        return false;
    }

    size_t offset = 4096;
    if (!copy_neo_program(P[0], P[1], src, file->buf.size, offset, p_size) ||
        !copy_neo_region(S, src, file->buf.size, offset, s_size) ||
        !copy_neo_region(M, src, file->buf.size, offset, m_size) ||
        !copy_neo_region(V[0], src, file->buf.size, offset, v1_size) ||
        !copy_neo_region(V[1], src, file->buf.size, offset, v2_size) ||
        !copy_neo_region(C, src, file->buf.size, offset, c_size)) {
        printf("\nFailed loading .neo file %s", file->name);
        return false;
    }

    printf("\nLoaded .neo %s NGH=%04x P=%u S=%u M=%u V1=%u V2=%u C=%u",
           file->name, ngh, p_size, s_size, m_size, v1_size, v2_size, c_size);
    return true;
}

template u16 CART::read_P<0>(u32 addr) const;
template u16 CART::read_P<1>(u32 addr) const;

template<u8 num>
u16 CART::read_P(u32 addr) const {
    if (P[num].sz) {
        if constexpr(num == 1) {
            addr &= 0xFFFFF;
            addr |= P_bank;
        }
        addr %= P[num].sz;
        return (P[num].ptr[addr]) | (P[num].ptr[addr+1] << 8);
    }
    return 0xFFFF;
}

u8 CART::read_M(u32 addr) const {
    if (M.sz) {
        addr %= M.sz;
        return M.ptr[addr];
    }
    return 0xFF;
}

u8 CART::read_C(u32 addr) const {
    if (C.sz) {
        addr %= C.sz;
        return C.ptr[addr];
    }
    return 0xFF;
}

u32 CART::V_size() const
{
    return static_cast<u32>(V[0].sz + V[1].sz);
}

u8 CART::read_V(u32 addr) const
{
    u32 sz = V_size();
    if (!sz) return 0;

    addr %= sz;
    if (addr < V[0].sz) return V[0].ptr[addr];

    addr -= static_cast<u32>(V[0].sz);
    return V[1].ptr[addr];
}

void CART::write_P(u32 addr, u8 val) {
    P_bank = static_cast<u32>(val & 7) << 20;
}

void CART::write_M(u32 addr, u8 val) {
    printf("\nWARN Z80 WRITE CART @%04x: %02x", addr, val);
}

u8 CART::read_S(u32 addr) const {
    if (S.sz) {
        addr %= S.sz;
        return S.ptr[addr];
    }
    return 0xFF;
}

}
