// Copyright 2026 Open Trust Group. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// mmaptest exercises the mmap path of a filesystem (in particular Fxfs mounted
// at /data): it writes a deterministic multi-page pattern to a file, reopens it
// read-only, maps it with mmap(PROT_READ, MAP_PRIVATE), and verifies every byte
// through the mapping. Reading the mapping faults pages in through the
// filesystem's pager, so a successful run validates the get_buffer/pager path
// end to end. Prints "MMAPTEST: PASS ..." on success.

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static uint8_t pattern_byte(size_t i) {
    return (uint8_t)(i % 251);
}

int main(int argc, char** argv) {
    const char* path = (argc > 1) ? argv[1] : "/data/mmaptest.bin";
    size_t size = (argc > 2) ? strtoul(argv[2], NULL, 0) : (256 * 1024);

    uint8_t* buf = malloc(size);
    if (buf == NULL) {
        printf("MMAPTEST: FAIL alloc %zu\n", size);
        return 1;
    }
    for (size_t i = 0; i < size; i++) {
        buf[i] = pattern_byte(i);
    }

    // 1) Write the pattern out to the file.
    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        printf("MMAPTEST: FAIL open-write %s\n", path);
        free(buf);
        return 1;
    }
    ssize_t written = write(fd, buf, size);
    if (written != (ssize_t)size) {
        printf("MMAPTEST: FAIL write %zd/%zu\n", written, size);
        close(fd);
        free(buf);
        return 1;
    }
    close(fd);
    printf("MMAPTEST: wrote %zu bytes\n", size);

    // 2) Reopen read-only and map it.
    fd = open(path, O_RDONLY);
    if (fd < 0) {
        printf("MMAPTEST: FAIL open-read %s\n", path);
        free(buf);
        return 1;
    }
    struct stat st;
    if (fstat(fd, &st) != 0 || (size_t)st.st_size != size) {
        printf("MMAPTEST: FAIL fstat size=%lld want=%zu\n",
               (long long)st.st_size, size);
        close(fd);
        free(buf);
        return 1;
    }
    printf("MMAPTEST: reopened, fstat size=%zu, calling mmap\n", size);
    void* map = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (map == MAP_FAILED) {
        printf("MMAPTEST: FAIL mmap %s errno=%d (%s)\n", path, errno, strerror(errno));
        close(fd);
        free(buf);
        return 1;
    }
    printf("MMAPTEST: mmap ok, reading %zu bytes through mapping\n", size);

    // 3) Read every byte through the mapping (faults pages in via the pager)
    //    and verify it against the pattern.
    const uint8_t* mapped = (const uint8_t*)map;
    size_t mismatches = 0;
    for (size_t i = 0; i < size; i++) {
        if (mapped[i] != pattern_byte(i)) {
            if (mismatches < 3) {
                printf("MMAPTEST: mismatch @%zu got=%u want=%u\n",
                       i, mapped[i], pattern_byte(i));
            }
            mismatches++;
        }
    }

    munmap(map, size);
    close(fd);
    free(buf);

    if (mismatches == 0) {
        printf("MMAPTEST: PASS %zu bytes via mmap on %s\n", size, path);
        return 0;
    }
    printf("MMAPTEST: FAIL %zu mismatches on %s\n", mismatches, path);
    return 1;
}
