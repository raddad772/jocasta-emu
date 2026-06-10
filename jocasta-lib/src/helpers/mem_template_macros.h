#pragma once

#define MEM_TEMPLATE_SIZES32_R(X, DEBUG, PEEK, FUNC, KIND) \
X(FUNC, 1, DEBUG, PEEK, KIND)                              \
X(FUNC, 2, DEBUG, PEEK, KIND)                              \
X(FUNC, 4, DEBUG, PEEK, KIND)

#define MEM_TEMPLATE_VARIANTS32_R(X, FUNC, KIND) \
MEM_TEMPLATE_SIZES32_R(X, false, false, FUNC, KIND) \
MEM_TEMPLATE_SIZES32_R(X, true,  false, FUNC, KIND) \
MEM_TEMPLATE_SIZES32_R(X, false, true,  FUNC, KIND) \
MEM_TEMPLATE_SIZES32_R(X, true,  true,  FUNC, KIND)

#define MEM_TEMPLATE_SIZES32_W(X, DEBUG, FUNC, KIND) \
X(FUNC, 1, DEBUG, KIND)                              \
X(FUNC, 2, DEBUG, KIND)                              \
X(FUNC, 4, DEBUG, KIND)

#define MEM_TEMPLATE_VARIANTS32_W(X, FUNC, KIND) \
MEM_TEMPLATE_SIZES32_W(X, false, FUNC, KIND)     \
MEM_TEMPLATE_SIZES32_W(X, true,  FUNC, KIND)

#define INSTANTIATE_READ_FUNC32(FUNC, SIZE, DEBUG, PEEK, KIND) \
template u32 core::FUNC<SIZE, DEBUG, PEEK>(KIND *, u32, u8);

#define INSTANTIATE_WRITE_FUNC32(FUNC, SIZE, DEBUG, KIND) \
template void core::FUNC<SIZE, DEBUG>(KIND *, u32, u8, u32);

#define MT_IR32(KIND, FUNC) \
MEM_TEMPLATE_VARIANTS32_R(INSTANTIATE_READ_FUNC32, FUNC, KIND)

#define MT_IW32(KIND, FUNC) \
MEM_TEMPLATE_VARIANTS32_W(INSTANTIATE_WRITE_FUNC32, FUNC, KIND)

#define MT_IRW32(KIND, RFUNC, WFUNC) \
        MT_IR32(KIND, RFUNC)                 \
        MT_IW32(KIND, WFUNC)

#define MT_READ124(mask, addr, ptrname) addr &= mask; if constexpr(sz == 1) return reinterpret_cast<const u8 *>(ptrname)[addr]; if constexpr(sz == 2) return reinterpret_cast<const u16 *>(ptrname)[addr >> 1]; if constexpr(sz == 4) return reinterpret_cast<const u32 *>(ptrname)[addr >> 2]; NOGOHERE
#define MT_READ1248(mask, addr, ptrname) addr &= mask; if constexpr(sz == 1) return reinterpret_cast<const u8 *>(ptrname)[addr]; if constexpr(sz == 2) return reinterpret_cast<const u16 *>(ptrname)[addr >> 1]; if constexpr(sz == 4) return reinterpret_cast<const u32 *>(ptrname)[addr >> 2]; if constexpr(sz == 8) return reinterpret_cast<const u64 *>(ptrname)[addr >> 3]; NOGOHERE


#define MT_CART_READ_ONE(cls, name, sz, dbg, pk) \
template u32 cls::name<sz, dbg, pk>(u32, u8)

#define MT_CART_READ_SZ(cls, name, sz) \
MT_CART_READ_ONE(cls, name, sz, false, false); \
MT_CART_READ_ONE(cls, name, sz, false, true);  \
MT_CART_READ_ONE(cls, name, sz, true,  false); \
MT_CART_READ_ONE(cls, name, sz, true,  true)

#define MT_CART_READ(cls, name) \
MT_CART_READ_SZ(cls, name, 1); \
MT_CART_READ_SZ(cls, name, 2); \
MT_CART_READ_SZ(cls, name, 4)
