// Copyright 2026 Open Trust Group. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// clonetest exercises resizing a pager-backed VMO parent while a copy-on-write
// clone of it is live, in the real Fxfs path: it writes a file, maps it
// MAP_PRIVATE (Fxfs get_buffer hands back a SNAPSHOT_AT_LEAST_ON_WRITE child of
// the file's pager VMO), faults the clone's pages in, then truncates the file
// to 0 -- resizing the shared parent pager VMO while the clone is mapped -- and
// reads the mapping again. Mirrors the CloneResizeParentOK core test in the
// real cross-process Fxfs path. Prints "CLONETEST: PASS" if the clone survives.

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static uint8_t pattern_byte(size_t i) {
    return (uint8_t)(i % 251);
}

int main(int argc, char** argv) {
    const char* path = (argc > 1) ? argv[1] : "/data/clonefile";
    size_t size = (argc > 2) ? strtoul(argv[2], NULL, 0) : (64 * 1024);

    uint8_t* buf = malloc(size);
    if (buf == NULL) {
        printf("CLONETEST: FAIL alloc\n");
        return 1;
    }
    for (size_t i = 0; i < size; i++) {
        buf[i] = pattern_byte(i);
    }

    // 1) Write the file.
    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0 || write(fd, buf, size) != (ssize_t)size) {
        printf("CLONETEST: FAIL write %s\n", path);
        return 1;
    }
    close(fd);

    // 2) Map it MAP_PRIVATE (a COW clone of the file's pager VMO) and fault the
    //    clone's pages in.
    fd = open(path, O_RDONLY);
    if (fd < 0) {
        printf("CLONETEST: FAIL open-read\n");
        return 1;
    }
    volatile const uint8_t* m = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if ((void*)m == MAP_FAILED) {
        printf("CLONETEST: FAIL mmap errno=%d\n", errno);
        return 1;
    }
    size_t bad = 0;
    for (size_t i = 0; i < size; i++) {
        if (m[i] != pattern_byte(i)) {
            bad++;
        }
    }
    if (bad != 0) {
        printf("CLONETEST: FAIL pre-truncate %zu mismatches\n", bad);
        return 1;
    }
    printf("CLONETEST: clone mapped and faulted in (%zu bytes)\n", size);

    // 3) Truncate the file to 0, resizing the shared parent pager VMO while the
    //    clone is still mapped.
    int fd2 = open(path, O_RDWR);
    if (fd2 < 0 || ftruncate(fd2, 0) != 0) {
        printf("CLONETEST: FAIL ftruncate errno=%d\n", errno);
        return 1;
    }
    close(fd2);
    printf("CLONETEST: parent truncated to 0 while clone is live\n");

    // 4) The clone must still be accessible with its original contents
    //    (CloneResizeParentOK semantics).
    bad = 0;
    for (size_t i = 0; i < size; i++) {
        if (m[i] != pattern_byte(i)) {
            bad++;
        }
    }
    munmap((void*)m, size);
    close(fd);
    free(buf);

    if (bad == 0) {
        printf("CLONETEST: PASS clone intact after parent resize\n");
        return 0;
    }
    printf("CLONETEST: FAIL %zu mismatches after parent resize\n", bad);
    return 1;
}
