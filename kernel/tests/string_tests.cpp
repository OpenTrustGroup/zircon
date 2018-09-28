// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <inttypes.h>
#include <fbl/auto_call.h>
#include <kernel/thread.h>
#include <malloc.h>
#include <platform.h>
#include <stdio.h>
#include <string.h>
#include <vm/physmap.h>
#include <vm/pmm.h>
#include <zircon/types.h>

static uint8_t* src;
static uint8_t* dst;

static uint8_t* src2;
static uint8_t* dst2;

#define BUFFER_SIZE (8 * 1024 * 1024)
#define ITERATIONS (1024 * 1024 * 1024 / BUFFER_SIZE) // enough iterations to have to copy/set 1GB of memory

#if 1
static inline void* mymemcpy(void* dst, const void* src, size_t len) {
    return memcpy(dst, src, len);
}
static inline void* mymemset(void* dst, int c, size_t len) {
    return memset(dst, c, len);
}
#else
// if we're testing our own memcpy, use this
extern void* mymemcpy(void* dst, const void* src, size_t len);
extern void* mymemset(void* dst, int c, size_t len);
#endif

/* reference implementations of memmove/memcpy */
typedef long word;

#define lsize sizeof(word)
#define lmask (lsize - 1)

static void* c_memmove(void* dest, void const* src, size_t count) {
    char* d = (char*)dest;
    const char* s = (const char*)src;
    size_t len;

    if (count == 0 || dest == src)
        return dest;

    if ((long)d < (long)s) {
        if (((long)d | (long)s) & lmask) {
            // src and/or dest do not align on word boundary
            if ((((long)d ^ (long)s) & lmask) || (count < lsize))
                len = count; // copy the rest of the buffer with the byte mover
            else
                len = lsize - ((long)d & lmask); // move the ptrs up to a word boundary

            count -= len;
            for (; len > 0; len--)
                *d++ = *s++;
        }
        for (len = count / lsize; len > 0; len--) {
            *(word*)d = *(word*)s;
            d += lsize;
            s += lsize;
        }
        for (len = count & lmask; len > 0; len--)
            *d++ = *s++;
    } else {
        d += count;
        s += count;
        if (((long)d | (long)s) & lmask) {
            // src and/or dest do not align on word boundary
            if ((((long)d ^ (long)s) & lmask) || (count <= lsize))
                len = count;
            else
                len = ((long)d & lmask);

            count -= len;
            for (; len > 0; len--)
                *--d = *--s;
        }
        for (len = count / lsize; len > 0; len--) {
            d -= lsize;
            s -= lsize;
            *(word*)d = *(word*)s;
        }
        for (len = count & lmask; len > 0; len--)
            *--d = *--s;
    }

    return dest;
}

static void* c_memset(void* s, int c, size_t count) {
    char* xs = (char*)s;
    size_t len = (-(size_t)s) & lmask;
    word cc = c & 0xff;

    if (count > len) {
        count -= len;
        cc |= cc << 8;
        cc |= cc << 16;
        if (sizeof(word) == 8)
            cc |= (uint64_t)cc << 32; // should be optimized out on 32 bit machines

        // write to non-aligned memory byte-wise
        for (; len > 0; len--)
            *xs++ = (char)c;

        // write to aligned memory dword-wise
        for (len = count / lsize; len > 0; len--) {
            *((word*)xs) = (word)cc;
            xs += lsize;
        }

        count &= lmask;
    }

    // write remaining bytes
    for (; count > 0; count--)
        *xs++ = (char)c;

    return s;
}

static void* null_memcpy(void* dst, const void* src, size_t len) {
    return dst;
}

static zx_duration_t bench_memcpy_routine(void* memcpy_routine(void*, const void*, size_t), size_t srcalign, size_t dstalign) {
    int i;
    zx_time_t t0;

    t0 = current_time();
    for (i = 0; i < ITERATIONS; i++) {
        memcpy_routine(dst + dstalign, src + srcalign, BUFFER_SIZE);
    }
    return current_time() - t0;
}

static void bench_memcpy(void) {
    zx_duration_t null, c, libc, mine;
    size_t srcalign, dstalign;

    printf("memcpy speed test\n");
    thread_sleep_relative(ZX_MSEC(200)); // let the debug string clear the serial port

    for (srcalign = 0; srcalign < 64;) {
        for (dstalign = 0; dstalign < 64;) {

            spin_lock_saved_state_t state;
            arch_interrupt_save(&state, ARCH_DEFAULT_SPIN_LOCK_FLAG_INTERRUPTS);
            null = bench_memcpy_routine(&null_memcpy, srcalign, dstalign) / ZX_MSEC(1);
            c = bench_memcpy_routine(&c_memmove, srcalign, dstalign) / ZX_MSEC(1);
            libc = bench_memcpy_routine(&memcpy, srcalign, dstalign) / ZX_MSEC(1);
            mine = bench_memcpy_routine(&mymemcpy, srcalign, dstalign) / ZX_MSEC(1);
            arch_interrupt_restore(state, ARCH_DEFAULT_SPIN_LOCK_FLAG_INTERRUPTS);

            printf("srcalign %zu, dstalign %zu: ", srcalign, dstalign);
            printf("   null memcpy %" PRIi64 " msecs\n", null);
            printf("c %" PRIi64 " msecs, %llu bytes/sec; ", c, (uint64_t)BUFFER_SIZE * ITERATIONS * 1000ULL / c);
            printf("libc %" PRIi64 " msecs, %llu bytes/sec; ", libc, (uint64_t)BUFFER_SIZE * ITERATIONS * 1000ULL / libc);
            printf("my %" PRIi64 " msecs, %llu bytes/sec; ", mine, (uint64_t)BUFFER_SIZE * ITERATIONS * 1000ULL / mine);
            printf("\n");

            if (dstalign < 8)
                dstalign++;
            else
                dstalign <<= 1;
        }
        if (srcalign < 8)
            srcalign++;
        else
            srcalign <<= 1;
    }
}

static void fillbuf(void* ptr, size_t len, uint32_t seed) {
    size_t i;
    uint8_t* cptr = (uint8_t*)ptr;

    for (i = 0; i < len; i++) {
        cptr[i] = (uint8_t)seed;
        seed *= 0x1234567;
    }
}

static void validate_memcpy(void) {
    size_t srcalign, dstalign, size;
    const size_t maxsize = 256;

    printf("testing memcpy for correctness\n");

    /*
     * do the simple tests to make sure that memcpy doesn't color outside
     * the lines for all alignment cases
     */
    for (srcalign = 0; srcalign < 64; srcalign++) {
        printf("srcalign %zu\n", srcalign);
        for (dstalign = 0; dstalign < 64; dstalign++) {
            //printf("\tdstalign %zu\n", dstalign);
            for (size = 0; size < maxsize; size++) {

                //printf("srcalign %zu, dstalign %zu, size %zu\n", srcalign, dstalign, size);

                fillbuf(src, maxsize * 2, 567);
                fillbuf(src2, maxsize * 2, 567);
                fillbuf(dst, maxsize * 2, 123514);
                fillbuf(dst2, maxsize * 2, 123514);

                c_memmove(dst + dstalign, src + srcalign, size);
                memcpy(dst2 + dstalign, src2 + srcalign, size);

                int comp = memcmp(dst, dst2, maxsize * 2);
                if (comp != 0) {
                    printf("error! srcalign %zu, dstalign %zu, size %zu\n", srcalign, dstalign, size);
                }
            }
        }
    }
}

static zx_duration_t bench_memset_routine(void* memset_routine(void*, int, size_t), size_t dstalign, size_t len) {
    int i;
    zx_time_t t0;

    t0 = current_time();
    for (i = 0; i < ITERATIONS; i++) {
        memset_routine(dst + dstalign, 0, len);
    }
    return current_time() - t0;
}

static void bench_memset(void) {
    zx_duration_t c, libc, mine;
    size_t dstalign;

    printf("memset speed test\n");
    thread_sleep_relative(ZX_MSEC(200)); // let the debug string clear the serial port

    for (dstalign = 0; dstalign < 64; dstalign++) {

        spin_lock_saved_state_t state;
        arch_interrupt_save(&state, ARCH_DEFAULT_SPIN_LOCK_FLAG_INTERRUPTS);
        c = bench_memset_routine(&c_memset, dstalign, BUFFER_SIZE) / ZX_MSEC(1);
        libc = bench_memset_routine(&memset, dstalign, BUFFER_SIZE) / ZX_MSEC(1);
        mine = bench_memset_routine(&mymemset, dstalign, BUFFER_SIZE) / ZX_MSEC(1);
        arch_interrupt_restore(state, ARCH_DEFAULT_SPIN_LOCK_FLAG_INTERRUPTS);

        printf("dstalign %zu: ", dstalign);
        printf("c %" PRIi64 " msecs, %llu bytes/sec; ", c, (uint64_t)BUFFER_SIZE * ITERATIONS * 1000ULL / c);
        printf("libc %" PRIi64 " msecs, %llu bytes/sec; ", libc, (uint64_t)BUFFER_SIZE * ITERATIONS * 1000ULL / libc);
        printf("my %" PRIi64 " msecs, %llu bytes/sec; ", mine, (uint64_t)BUFFER_SIZE * ITERATIONS * 1000ULL / mine);
        printf("\n");
    }
}

static void validate_memset(void) {
    size_t dstalign, size;
    int c;
    const size_t maxsize = 256;

    printf("testing memset for correctness\n");

    for (dstalign = 0; dstalign < 64; dstalign++) {
        printf("align %zu\n", dstalign);
        for (size = 0; size < maxsize; size++) {
            for (c = -1; c < 257; c++) {

                fillbuf(dst, maxsize * 2, 123514);
                fillbuf(dst2, maxsize * 2, 123514);

                c_memset(dst + dstalign, c, size);
                memset(dst2 + dstalign, c, size);

                int comp = memcmp(dst, dst2, maxsize * 2);
                if (comp != 0) {
                    printf("error! align %zu, c 0x%hhx, size %zu\n",
                           dstalign, (unsigned char)c, size);
                }
            }
        }
    }
}

#include <lib/console.h>

static int string_tests(int argc, const cmd_args* argv, uint32_t flags) {
    list_node list;
    list_initialize(&list);

    // free the physical pages on exit
    auto free_pages = fbl::MakeAutoCall([&list]() {
        pmm_free(&list);
        src = dst = src2 = dst2 = nullptr;
    });

    // allocate a large run of physically contiguous pages and get the address out
    // of the physmap
    size_t total_size = (BUFFER_SIZE + 256) * 4;
    size_t page_count = ROUNDUP(total_size, PAGE_SIZE) / PAGE_SIZE;
    paddr_t pa;
    if (pmm_alloc_contiguous(page_count, 0, PAGE_SIZE_SHIFT, &pa, &list) != ZX_OK) {
        printf("failed to allocate %zu bytes of contiguous memory for test\n", total_size);
        return -1;
    }

    uint8_t* base = (uint8_t *)paddr_to_physmap(pa);
    src = base;
    dst = base + BUFFER_SIZE + 256;
    src2 = base + (BUFFER_SIZE + 256) * 2;
    dst2 = base + (BUFFER_SIZE + 256) * 3;

    printf("src %p, dst %p\n", src, dst);
    printf("src2 %p, dst2 %p\n", src2, dst2);

    if (argc < 3) {
        printf("not enough arguments:\n");
    usage:
        printf("%s validate <routine>\n", argv[0].str);
        printf("%s bench <routine>\n", argv[0].str);
        return -1;
    }

    if (!strcmp(argv[1].str, "validate")) {
        if (!strcmp(argv[2].str, "memcpy")) {
            validate_memcpy();
        } else if (!strcmp(argv[2].str, "memset")) {
            validate_memset();
        }
    } else if (!strcmp(argv[1].str, "bench")) {
        if (!strcmp(argv[2].str, "memcpy")) {
            bench_memcpy();
        } else if (!strcmp(argv[2].str, "memset")) {
            bench_memset();
        }
    } else {
        goto usage;
    }

    return 0;
}

STATIC_COMMAND_START
STATIC_COMMAND("string", "memcpy tests", &string_tests)
STATIC_COMMAND_END(stringtests);
