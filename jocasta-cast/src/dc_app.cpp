#include <kos.h>
#include <dc/sq.h>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>

static constexpr size_t TEST_SIZE = 1024;
static constexpr size_t ALIGN = 32;

static void dump_ptr(const char *name, void *ptr) {
    printf("%s = %p (alignment=%lu)\n",
           name,
           ptr,
           (unsigned long)((uintptr_t)ptr & 31));
}

static bool verify_fill(uint8_t *buf, uint8_t value, size_t size) {
    for(size_t i = 0; i < size; ++i) {
        if(buf[i] != value) {
            printf("FAIL: verify_fill @ %lu expected=0x%02x got=0x%02x\n",
                   (unsigned long)i, value, buf[i]);
            return false;
        }
    }
    return true;
}

static bool verify_copy(uint8_t *dst, uint8_t *src, size_t size) {
    for(size_t i = 0; i < size; ++i) {
        if(dst[i] != src[i]) {
            printf("FAIL: verify_copy @ %lu expected=0x%02x got=0x%02x\n",
                   (unsigned long)i, src[i], dst[i]);
            return false;
        }
    }
    return true;
}

static int test_sq_clear() {
    printf("\n[TEST] sq_clr\n");

    uint8_t *buf = (uint8_t *)memalign(ALIGN, TEST_SIZE);
    if(!buf) {
        printf("FAIL: allocation failed\n");
        return -1;
    }

    dump_ptr("buf", buf);

    memset(buf, 0xAA, TEST_SIZE);

    printf("Running sq_clr...\n");
    sq_clr(buf, TEST_SIZE);

    if(!verify_fill(buf, 0x00, TEST_SIZE)) {
        printf("sq_clr FAILED\n");
        free(buf);
        return -1;
    }

    printf("sq_clr OK\n");

    free(buf);
    return 0;
}

static int test_sq_copy() {
    printf("\n[TEST] sq_cpy\n");

    uint8_t *src = (uint8_t *)memalign(ALIGN, TEST_SIZE);
    uint8_t *dst = (uint8_t *)memalign(ALIGN, TEST_SIZE);

    if(!src || !dst) {
        printf("FAIL: allocation failed\n");
        free(src);
        free(dst);
        return -1;
    }

    dump_ptr("src", src);
    dump_ptr("dst", dst);

    for(size_t i = 0; i < TEST_SIZE; ++i) {
        src[i] = (uint8_t)(i ^ 0x5A);
        dst[i] = 0x00;
    }

    printf("Running sq_cpy...\n");
    sq_cpy(dst, src, TEST_SIZE);

    if(!verify_copy(dst, src, TEST_SIZE)) {
        printf("sq_cpy FAILED\n");
        free(src);
        free(dst);
        return -1;
    }

    printf("sq_cpy OK\n");

    free(src);
    free(dst);
    return 0;
}

static int test_alignment_failure() {
    printf("\n[TEST] misaligned destination (expected failure or undefined behavior)\n");

    uint8_t *buf = (uint8_t *)malloc(TEST_SIZE + ALIGN);

    if(!buf) {
        printf("FAIL: allocation failed\n");
        return -1;
    }

    uint8_t *misaligned = buf + 1; // intentionally break alignment

    dump_ptr("misaligned", misaligned);

    memset(misaligned, 0xAA, TEST_SIZE);

    printf("Running sq_clr on misaligned buffer...\n");

    // This is intentionally invalid usage
    sq_clr(misaligned, TEST_SIZE);

    // We don't strictly expect correctness here — just report what happened
    if(verify_fill(misaligned, 0x00, TEST_SIZE)) {
        printf("WARNING: misaligned sq_clr appeared to succeed (unexpected)\n");
    } else {
        printf("Expected: misaligned operation did not behave correctly\n");
    }

    free(buf);
    return 0;
}

extern "C" int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    printf("========================================\n");
    printf(" KOS Store Queue Diagnostics\n");
    printf("========================================\n");

#if defined(__SH4__)
    printf("CPU: SH4 detected\n");
#else
    printf("WARNING: Not compiling for SH4\n");
#endif

#if defined(__DREAMCAST__)
    printf("Platform: Dreamcast\n");
#else
    printf("WARNING: Not Dreamcast target\n");
#endif

    int failures = 0;

    if(test_sq_clear() != 0) failures++;
    if(test_sq_copy() != 0) failures++;
    if(test_alignment_failure() != 0) failures++;

    printf("\n========================================\n");

    if(failures == 0) {
        printf("ALL TESTS PASSED\n");
    } else {
        printf("TESTS FAILED: %d\n", failures);
    }

    printf("========================================\n");

    return failures ? 1 : 0;
}