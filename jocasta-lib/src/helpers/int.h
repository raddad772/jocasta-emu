#pragma once

#include <cstdint>
#include <type_traits>

#ifdef __cplusplus
extern "C" {
#endif

#define CPU_X86   2
#define CPU_X64   3
#define CPU_ARM   4
#define CPU_ARM64 5

#if defined(__arm__) // 32bit arm, and 32bit arm only.
#define HOST_CPU CPU_ARM
#elif defined(__aarch64__) // 64bit arm, and 64bit arm only.
#define HOST_CPU CPU_ARM64
#elif defined(__SH4_SINGLE__)
#define HOST_CPU CPU_SH4
#elif defined(__x86_64__) || defined(_M_X64)
#define HOST_CPU CPU_X64
#elif defined(i386) || defined(__i386__) || defined(__i386) || defined(_M_IX86)
#define HOST_CPU CPU_X86
#else
#error Unsupported CPU architecture
#endif

#define COMPILER_VC    1
#define COMPILER_GCC   2
#define COMPILER_CLANG 3

#if defined(__clang__)
#define BUILD_COMPILER COMPILER_CLANG
#elif defined(__GNUC__)
#define BUILD_COMPILER COMPILER_GCC
#elif defined(_MSC_VER)
#define BUILD_COMPILER COMPILER_VC
#else
#error Unsupported compiler
#endif

/* If you are somehow on a big-endian platform, you must change this */
#define ENDIAN_LITTLE
//#define ENDIAN_BIG


#include <stdbool.h>
#include <stdint.h>

/*typedef int8_t int8;
typedef int16_t int16;
typedef int32_t int32;
typedef int64_t int64;

typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t u32;
typedef uint64_t uint64;*/

typedef uint64_t u64;
typedef int64_t s64;
typedef int64_t i64;

typedef uint32_t u32;
typedef int32_t s32;
typedef int32_t i32;

typedef uint16_t u16;
typedef int16_t s16;
typedef int16_t i16;

typedef uint8_t u8;
typedef int8_t s8;
typedef int8_t i8;

typedef float f32;
typedef double f64;

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

#ifndef NULL
#define NULL 0
#endif

#if defined(__APPLE__)
// Mac os X
#include <libkern/OSByteOrder.h>
#define bswap_16(x) OSSwapInt16(x)
#define bswap_32(x) OSSwapInt32(x)
#define bswap_64(x) OSSwapInt64(x)
#elif defined(_MSC_VER)
#include <cstdlib>
#define bswap_16(x) _byteswap_ushort(x)
#define bswap_32(x) _byteswap_ulong(x)
#define bswap_64(x) _byteswap_uint64(x)
#else
#if HAVE_BYTESWAP_H
#include <byteswap.h>
#else
#define bswap_16 __builtin_bswap16
#define bswap_32 __builtin_bswap32
#define bswap_64 __builtin_bswap64
#endif
#endif

#ifdef ENDIAN_LITTLE

#define from_big16(x) bswap_16(x)
#define from_big32(x) bswap_32(x)
#define from_big64(x) bswap_64(x)

#define from_little16(x) (x)
#define from_little32(x) (x)
#define from_little64(x) (x)

#else

#define from_big16(x) (x)
#define from_big32(x) (x)
#define from_big64(x) (x)

#define from_little16(x) bswap_16(x)
#define from_little32(x) bswap_32(x)
#define from_little64(x) bswap_64(x)


#endif

union UN16 {
    struct {
        u8 lo;
        u8 hi;
    };
    u16 u;
    i16 i;
};


#ifdef __cplusplus
}
#endif

#ifdef DEBUG
#define NOGOHERE assert(1==2)
#else
#if defined(_MSC_VER)
#define NOGOHERE abort();
#else
//#define NOGOHERE { printf("\nABORT!"); fflush(stdout); abort(); __builtin_unreachable(); }
#define NOGOHERE \
do { \
printf("\nABORT! %s:%d (%s)\n", __FILE__, __LINE__, __func__); \
fflush(stdout); \
abort(); \
__builtin_unreachable(); \
} while (0)
#endif
#endif

#define nodefault default: NOGOHERE;

#if defined(_MSC_VER)
#define FALLTHROUGH // #TODO: Is there a way to do this in MSVC?
#else
#define FALLTHROUGH __attribute__((fallthrough))
#endif

template <unsigned bits, typename R = std::int32_t, typename T>
constexpr R sign_extend(T x)
{
    static_assert(bits > 0);
    static_assert(bits <= sizeof(R) * 8);

    using UR = std::make_unsigned_t<R>;

    constexpr UR sign_bit = UR{1} << (bits - 1);
    constexpr UR mask = (bits == sizeof(R) * 8)
        ? ~UR{0}
    : ((UR{1} << bits) - 1);

    UR v = static_cast<UR>(x) & mask;

    return static_cast<R>((v ^ sign_bit) - sign_bit);
}
