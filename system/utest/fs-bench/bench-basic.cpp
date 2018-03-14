// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <zircon/device/vfs.h>
#include <zircon/syscalls.h>
#include <fbl/alloc_checker.h>
#include <fbl/string_piece.h>
#include <fbl/unique_ptr.h>
#include <unittest/unittest.h>

#define MOUNT_POINT "/tmp/benchmark"

constexpr size_t KB = (1 << 10);
constexpr size_t MB = (1 << 20);
constexpr uint8_t kMagicByte = 0xee;

// Return "true" if the fs matches the 'banned' criteria.
template <size_t len>
bool benchmark_banned(int fd, const char (&banned_fs)[len]) {
    char buf[sizeof(vfs_query_info_t) + MAX_FS_NAME_LEN + 1];
    vfs_query_info_t* info = reinterpret_cast<vfs_query_info_t*>(buf);
    ssize_t r = ioctl_vfs_query_fs(fd, info, sizeof(buf) - 1);

    if (r != static_cast<ssize_t>(sizeof(vfs_query_info_t) + len)) {
        return false;
    }

    buf[r] = '\0';
    const char* name = reinterpret_cast<const char*>(buf + sizeof(vfs_query_info_t));
    return strncmp(banned_fs, name, len - 1) == 0;
}

inline void time_end(const char *str, uint64_t start) {
    uint64_t end = zx_ticks_get();
    uint64_t ticks_per_msec = zx_ticks_per_second() / 1000;
    printf("Benchmark %s: [%10lu] msec\n", str, (end - start) / ticks_per_msec);
}

constexpr int kWriteReadCycles = 3;

// The goal of this benchmark is to get a basic idea of some large read / write
// times for a file.
//
// Caching will no doubt play a part with this benchmark, but it's simple,
// and should give us a rough rule-of-thumb regarding how we're doing.
template <size_t DataSize, size_t NumOps>
bool benchmark_write_read(void) {
    BEGIN_TEST;
    int fd = open(MOUNT_POINT "/bigfile", O_CREAT | O_RDWR, 0644);
    ASSERT_GT(fd, 0, "Cannot create file (FS benchmarks assume mounted FS exists at '/benchmark')");
    const size_t size_mb = (DataSize * NumOps) / MB;
    if (size_mb > 64 && benchmark_banned(fd, "memfs")) {
        return true;
    }
    printf("\nBenchmarking Write + Read (%lu MB)\n", size_mb);

    fbl::AllocChecker ac;
    fbl::unique_ptr<uint8_t[]> data(new (&ac) uint8_t[DataSize]);
    ASSERT_EQ(ac.check(), true);
    memset(data.get(), kMagicByte, DataSize);

    uint64_t start;
    size_t count;

    for (int i = 0; i < kWriteReadCycles; i++) {
        char str[100];
        snprintf(str, sizeof(str), "write %d", i);

        start = zx_ticks_get();
        count = NumOps;
        while (count--) {
            ASSERT_EQ(write(fd, data.get(), DataSize), DataSize);
        }
        time_end(str, start);

        ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0);
        snprintf(str, sizeof(str), "read %d", i);

        start = zx_ticks_get();
        count = NumOps;
        while (count--) {
            ASSERT_EQ(read(fd, data.get(), DataSize), DataSize);
            ASSERT_EQ(data[0], kMagicByte);
        }
        time_end(str, start);

        ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0);
    }

    ASSERT_EQ(syncfs(fd), 0);
    ASSERT_EQ(close(fd), 0);
    ASSERT_EQ(unlink(MOUNT_POINT "/bigfile"), 0);

    END_TEST;
}

#define START_STRING "/aaa"

size_t constexpr kComponentLength = fbl::constexpr_strlen(START_STRING);

template <size_t len>
void increment_str(char* str) {
    // "Increment" the string alphabetically.
    // '/aaa' --> '/aab, '/aaz' --> '/aba', etc
    for (size_t j = len - 1; j > 0; j--) {
        str[j] = static_cast<char>(str[j] + 1);
        if (str[j] > 'z') {
            str[j] = 'a';
        } else {
            return;
        }
    }
}

template <size_t MaxComponents>
bool walk_down_path_components(char* path, bool (*cb)(const char* path)) {
    BEGIN_HELPER;
    static_assert(MaxComponents * kComponentLength + fbl::constexpr_strlen(MOUNT_POINT) < PATH_MAX,
                  "Path depth is too long");
    size_t path_len = strlen(path);
    char path_component[kComponentLength + 1];
    strcpy(path_component, START_STRING);

    for (size_t i = 0; i < MaxComponents; i++) {
        strcpy(path + path_len, path_component);
        path_len += kComponentLength;
        ASSERT_TRUE(cb(path), "Callback failure");

        increment_str<kComponentLength>(path_component);
    }
    END_HELPER;
}

bool walk_up_path_components(char* path, bool (*cb)(const char* path)) {
    BEGIN_HELPER;
    size_t path_len = strlen(path);

    while (path_len != fbl::constexpr_strlen(MOUNT_POINT)) {
        ASSERT_TRUE(cb(path), "Callback failure");
        path[path_len - kComponentLength] = 0;
        path_len -= kComponentLength;
    }
    END_HELPER;
}

bool mkdir_callback(const char* path) {
    BEGIN_HELPER;
    ASSERT_EQ(mkdir(path, 0666), 0, "Could not make directory");
    END_HELPER;
}

bool stat_callback(const char* path) {
    BEGIN_HELPER;
    struct stat buf;
    ASSERT_EQ(stat(path, &buf), 0, "Could not stat directory");
    END_HELPER;
}

bool unlink_callback(const char* path) {
    BEGIN_HELPER;
    ASSERT_EQ(unlink(path), 0, "Could not unlink directory");
    END_HELPER;
}

template <size_t MaxComponents>
bool benchmark_path_walk(void) {
    BEGIN_TEST;
    printf("\nBenchmarking Long path walk (%lu components)\n", MaxComponents);
    char path[PATH_MAX];
    strcpy(path, MOUNT_POINT);
    uint64_t start;

    start = zx_ticks_get();
    ASSERT_TRUE(walk_down_path_components<MaxComponents>(path, mkdir_callback));
    time_end("mkdir", start);

    strcpy(path, MOUNT_POINT);
    start = zx_ticks_get();
    ASSERT_TRUE(walk_down_path_components<MaxComponents>(path, stat_callback));
    time_end("stat", start);

    start = zx_ticks_get();
    ASSERT_TRUE(walk_up_path_components(path, unlink_callback));
    time_end("unlink", start);

    int fd = open(MOUNT_POINT, O_DIRECTORY | O_RDONLY);
    ASSERT_GE(fd, 0);
    ASSERT_EQ(syncfs(fd), 0);
    ASSERT_EQ(close(fd), 0);
    END_TEST;
}

BEGIN_TEST_CASE(basic_benchmarks)
RUN_TEST_PERFORMANCE((benchmark_write_read<16 * KB, 1024>))
RUN_TEST_PERFORMANCE((benchmark_write_read<16 * KB, 2048>))
RUN_TEST_PERFORMANCE((benchmark_write_read<16 * KB, 4096>))
RUN_TEST_PERFORMANCE((benchmark_write_read<16 * KB, 8192>))
RUN_TEST_PERFORMANCE((benchmark_write_read<16 * KB, 16384>))
RUN_TEST_PERFORMANCE((benchmark_path_walk<125>))
RUN_TEST_PERFORMANCE((benchmark_path_walk<250>))
RUN_TEST_PERFORMANCE((benchmark_path_walk<500>))
RUN_TEST_PERFORMANCE((benchmark_path_walk<1000>))
END_TEST_CASE(basic_benchmarks)
